// The vibration motor, device side: GPIO46 through LEDC, one esp_timer for the core's next wake (no task of its own),
// a second as the 2 s backstop, a shutdown handler for esp_restart(), the settings in yk_pref, the development console.
// The scheduling itself is yk_haptic_core.c (pure, host tested). See yaykit_haptic.h.
#include "yaykit_haptic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "yk_haptic_core.h"

#if defined(CONFIG_YAY_DEV) && CONFIG_YAY_DEV
#include "esp_console.h"
#endif

static const char *TAG = "yk_haptic";

#define NS        "yk_pref"  // the device's shared settings (yaykit_board's board_pref.c keeps the others there)
#define KEY_ON    "vibe_on"     // u8: 0 off, 1 on
#define KEY_LEVEL "vibe_level"  // u8: 0 light, 1 medium, 2 strong

#define LEDC_MODE  LEDC_LOW_SPEED_MODE
#define LEDC_TIMER LEDC_TIMER_3    // the last timer and channel: an app's own LEDC most likely starts at 0
#define LEDC_CH    LEDC_CHANNEL_7
#define PWM_HZ     20000            // not heard; measured on GPIO46 (unit 2, 11-13)
#define GUARD_MS   (YKH_ON_MAX_MS + 50)  // the backstop, a little after the core's own cut
#define LOCK_WAIT_MS 100            // stop() and the shutdown handler never wait longer: they write the pin anyway

// The conditions it stays still under, from components an app may not have: linked weakly, NULL without them.
bool yk_audio_running(void) __attribute__((weak));    // yaykit_audio: the microphones are open
bool yk_voice_recording(void) __attribute__((weak));  // yaykit_voice (Homework Buddy's recorder)
bool yk_install_busy(void) __attribute__((weak));     // yaykit_install: an install runs (it writes flash)
bool yk_app_battery_low(void) __attribute__((weak));  // yaykit_app: an extension's battery guard

typedef enum { PIN_UNTOUCHED, PIN_READY, PIN_BROKEN } pin_t;

static struct {
    bool inited;
    pin_t pin;
    bool hooked;           // the shutdown handler is registered
    volatile bool shut;    // esp_restart() is on its way
    int written;           // the duty LEDC gives now; 0 stopped, the pin low
    SemaphoreHandle_t lock;
    StaticSemaphore_t lock_buf;
    esp_timer_handle_t timer;  // the core's next wake
    esp_timer_handle_t guard;  // the motor still on GUARD_MS after it went on: off
    ykh_core_t core;
    volatile bool sys_low;     // the main firmware's battery line (yk_haptic_set_battery_low)
    int flash;                 // the main firmware's writes of its own under way (yk_haptic_flash), atomically
    volatile bool metering;    // the open microphones only read the level (yk_haptic_metering): they do not stop it
} H;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static unsigned quiet_now(void) {
    unsigned q = 0;
    if ((yk_audio_running && yk_audio_running() && !H.metering) || (yk_voice_recording && yk_voice_recording()))
        q |= YKH_QUIET_MIC;
    if ((yk_install_busy && yk_install_busy()) || __atomic_load_n(&H.flash, __ATOMIC_ACQUIRE) > 0) q |= YKH_QUIET_INSTALL;
    if ((yk_app_battery_low && yk_app_battery_low()) || H.sys_low) q |= YKH_QUIET_BATTERY;
    return q;
}

static bool lock(uint32_t ms) { return xSemaphoreTake(H.lock, ms ? pdMS_TO_TICKS(ms) : portMAX_DELAY) == pdTRUE; }
static void unlock(void) { xSemaphoreGive(H.lock); }

/** GPIO46 as a plain output, low (a strapping pin: low before it is an output). */
static esp_err_t pin_low(void) {
    esp_err_t err = gpio_reset_pin(YK_HAPTIC_GPIO);
    if (err == ESP_OK) err = gpio_set_level(YK_HAPTIC_GPIO, 0);
    if (err == ESP_OK) err = gpio_set_pull_mode(YK_HAPTIC_GPIO, GPIO_PULLDOWN_ONLY);
    if (err == ESP_OK) err = gpio_set_direction(YK_HAPTIC_GPIO, GPIO_MODE_OUTPUT);
    return err;
}

/** Off now, whatever the lock: LEDC stopped with the pin held low. */
static void off_now(void) {
    if (H.pin == PIN_READY) ledc_stop(LEDC_MODE, LEDC_CH, 0);
    if (H.guard) esp_timer_stop(H.guard);
    H.written = 0;
}

static void on_shutdown(void) {
    H.shut = true;
    const bool locked = lock(LOCK_WAIT_MS);
    ykh_out_t o;
    if (locked) ykh_shut(&H.core, now_ms(), &o);
    esp_timer_stop(H.timer);
    off_now();
    if (locked) unlock();
}

/** The first play: GPIO46 low, an output, then LEDC's (20 kHz, 10 bits), stopped low; and the shutdown handler. */
static bool pin_setup(void) {
    if (H.pin != PIN_UNTOUCHED) return H.pin == PIN_READY;
    esp_err_t err = pin_low();
    if (err == ESP_OK) {
        const ledc_timer_config_t t = {
            .speed_mode = LEDC_MODE,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .timer_num = LEDC_TIMER,
            .freq_hz = PWM_HZ,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        err = ledc_timer_config(&t);
    }
    if (err == ESP_OK) {
        const ledc_channel_config_t ch = {
            .gpio_num = YK_HAPTIC_GPIO,
            .speed_mode = LEDC_MODE,
            .channel = LEDC_CH,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER,
            .duty = 0,
            .hpoint = 0,
        };
        err = ledc_channel_config(&ch);
    }
    if (err == ESP_OK) err = ledc_stop(LEDC_MODE, LEDC_CH, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d and LEDC not set up (%s): no vibration", YK_HAPTIC_GPIO, esp_err_to_name(err));
        pin_low();
        H.pin = PIN_BROKEN;
        return false;
    }
    // Every esp_restart() stops it first. One of ESP-IDF's five: without a free one the reset alone lets go of the pin
    // (an input again, R55 pulls it low).
    if (!H.hooked) {
        const esp_err_t hook = esp_register_shutdown_handler(on_shutdown);
        H.hooked = hook == ESP_OK;
        if (!H.hooked) ESP_LOGW(TAG, "no shutdown handler (%s): a restart stops the motor by the reset only",
                                esp_err_to_name(hook));
    }
    H.pin = PIN_READY;
    return true;
}

/** What the core says, under the lock: the duty to LEDC, the next wake armed. */
static void apply(const ykh_out_t *o) {
    const int duty = H.shut || H.pin != PIN_READY ? 0 : o->duty;
    if (duty != H.written) {
        if (duty > 0) {
            ledc_set_duty(LEDC_MODE, LEDC_CH, (uint32_t)duty);
            ledc_update_duty(LEDC_MODE, LEDC_CH);
            if (H.written == 0) {
                esp_timer_stop(H.guard);
                esp_timer_start_once(H.guard, (uint64_t)GUARD_MS * 1000);
            }
            H.written = duty;
        } else {
            off_now();
        }
    }
    esp_timer_stop(H.timer);
    if (!H.shut && o->wake_ms >= 0) {
        int64_t us = o->wake_ms * 1000 - esp_timer_get_time();  // never early: now_ms() is the floor at the wake
        esp_timer_start_once(H.timer, us > 0 ? (uint64_t)us : 0);
    }
}

static void on_timer(void *arg) {
    (void)arg;
    if (!lock(0)) return;
    ykh_out_t o;
    ykh_step(&H.core, now_ms(), quiet_now(), &o);
    apply(&o);
    unlock();
}

static void on_guard(void *arg) {
    (void)arg;
    if (!lock(0)) return;
    if (H.written > 0) {
        ESP_LOGW(TAG, "on for %d ms in one go: off", GUARD_MS);
        ykh_out_t o;
        ykh_stop(&H.core, now_ms(), &o);
        apply(&o);
    }
    unlock();
}

static ykh_settings_t read_settings(void) {
    int on = -1, level = -1;  // missing: a Yay from before vibration
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v;
        if (nvs_get_u8(h, KEY_ON, &v) == ESP_OK) on = v;
        if (nvs_get_u8(h, KEY_LEVEL, &v) == ESP_OK) level = v;
        nvs_close(h);
    }
    return ykh_settings_read(on, level);
}

static esp_err_t to_err(ykh_result_t r) {
    switch (r) {
    case YKH_PLAYED: return ESP_OK;
    case YKH_BAD: return ESP_ERR_INVALID_ARG;
    case YKH_SHUT: return ESP_ERR_INVALID_STATE;
    default: return ESP_ERR_NOT_ALLOWED;  // the user's setting, the microphones, an install, the battery
    }
}

/** A play. level < 0: the user's. */
static esp_err_t start(const uint16_t *seg, int n, int level, int duty, bool kick) {
    if (!H.inited || H.shut) return ESP_ERR_INVALID_STATE;
    if (ykh_check(seg, n) != YKH_OK) return ESP_ERR_INVALID_ARG;
    lock(0);
    const unsigned quiet = quiet_now();
    esp_err_t err;
    if (H.core.set.on && !quiet && !H.shut && !pin_setup()) {  // the pin only when it is about to play
        err = ESP_ERR_NOT_SUPPORTED;
    } else {
        if (!duty) duty = ykh_level_duty(level < 0 ? H.core.set.level : level);
        ykh_out_t o;
        err = to_err(ykh_play(&H.core, now_ms(), seg, n, duty, kick, quiet, &o));
        apply(&o);
    }
    unlock();
    return err;
}

// ---------------------------------------------------------------- yaykit_haptic.h

esp_err_t yk_haptic_init(void) {
    if (H.inited) return ESP_OK;
    if (!H.lock) H.lock = xSemaphoreCreateMutexStatic(&H.lock_buf);
    const esp_timer_create_args_t wake = {.callback = on_timer, .name = "yk_haptic"};
    const esp_timer_create_args_t cap = {.callback = on_guard, .name = "yk_haptic_cap"};
    if (!H.timer && esp_timer_create(&wake, &H.timer) != ESP_OK) return ESP_ERR_NO_MEM;
    if (!H.guard && esp_timer_create(&cap, &H.guard) != ESP_OK) return ESP_ERR_NO_MEM;
    ykh_init(&H.core, read_settings());
    H.inited = true;
    return ESP_OK;
}

bool yk_haptic_available(void) {
    return H.inited && !H.shut && H.pin != PIN_BROKEN && H.core.set.on && !H.sys_low &&
           !(yk_app_battery_low && yk_app_battery_low());
}

esp_err_t yk_haptic_play(yk_haptic_kind_t kind) {
    uint16_t seg[YKH_SEG_MAX];
    const int n = ykh_builtin((int)kind, seg);
    if (!n) return H.inited ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE;
    return start(seg, n, -1, 0, true);
}

esp_err_t yk_haptic_pattern(const uint16_t *on_off_ms, int n) { return start(on_off_ms, n, -1, 0, true); }

void yk_haptic_stop(void) {
    if (!H.inited) return;  // never played: the pin is as the reset left it
    const bool locked = lock(LOCK_WAIT_MS);
    ykh_out_t o;
    if (locked) {
        ykh_stop(&H.core, now_ms(), &o);
        apply(&o);
        unlock();
    } else {
        off_now();  // the lock held up (it never is for long): the pin low all the same
    }
}

bool yk_haptic_busy(void) { return H.inited && ykh_busy(&H.core); }

void yk_haptic_reload(void) {
    if (!H.inited) return;
    const ykh_settings_t s = read_settings();
    lock(0);
    ykh_out_t o;
    ykh_set_settings(&H.core, s, now_ms(), &o);
    apply(&o);
    unlock();
}

esp_err_t yk_haptic_set_settings(bool on, int level) {
    const uint8_t want_on = on ? 1 : 0, want_level = (uint8_t)ykh_level_clamp(level);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "settings not saved: %s", esp_err_to_name(err));
        return err;
    }
    bool changed = false;
    uint8_t old;
    if (nvs_get_u8(h, KEY_ON, &old) != ESP_OK || old != want_on) {  // no flash write for the same value
        err = nvs_set_u8(h, KEY_ON, want_on);
        changed = true;
    }
    if (err == ESP_OK && (nvs_get_u8(h, KEY_LEVEL, &old) != ESP_OK || old != want_level)) {
        err = nvs_set_u8(h, KEY_LEVEL, want_level);
        changed = true;
    }
    if (err == ESP_OK && changed) err = nvs_commit(h);
    nvs_close(h);
    if (H.inited) {
        lock(0);
        ykh_out_t o;
        ykh_set_settings(&H.core, (ykh_settings_t){.on = on, .level = want_level}, now_ms(), &o);
        apply(&o);
        unlock();
    }
    return err;
}

void yk_haptic_get_settings(bool *on, int *level) {
    ykh_settings_t s;
    if (H.inited) {
        lock(0);
        s = H.core.set;
        unlock();
    } else {
        s = read_settings();
    }
    if (on) *on = s.on;
    if (level) *level = ykh_level_clamp(s.level);
}

/** A quiet condition came: what plays stops now, not at the next 20 ms look. */
static void quiet_came(void) {
    if (!H.inited) return;
    if (!lock(LOCK_WAIT_MS)) {
        off_now();
        return;
    }
    ykh_out_t o;
    ykh_step(&H.core, now_ms(), quiet_now(), &o);
    apply(&o);
    unlock();
}

void yk_haptic_set_battery_low(bool low) {
    const bool was = H.sys_low;
    H.sys_low = low;
    if (low && !was) quiet_came();
}

void yk_haptic_metering(bool on) { H.metering = on; }

void yk_haptic_flash(bool writing) {
    if (writing) {
        __atomic_add_fetch(&H.flash, 1, __ATOMIC_ACQ_REL);
        yk_haptic_stop();  // off now; and still until the write is done (quiet_now)
        return;
    }
    int n = __atomic_load_n(&H.flash, __ATOMIC_ACQUIRE);
    while (n > 0 && !__atomic_compare_exchange_n(&H.flash, &n, n - 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
    }
}

// ---------------------------------------------------------------- development console

#if defined(CONFIG_YAY_DEV) && CONFIG_YAY_DEV

static const char *const KIND_NAMES[] = {"tick", "double", "short", "long", "notify"};  // by yk_haptic_kind_t - 1
static const char *const LEVEL_NAMES[] = {"light", "medium", "strong"};
static const char *const PIN_NAMES[] = {"untouched", "ready", "broken"};

static const char *err_word(esp_err_t err) {
    switch (err) {
    case ESP_OK: return "ok";
    case ESP_ERR_NOT_ALLOWED: return "not_now";
    case ESP_ERR_INVALID_ARG: return "bad";
    case ESP_ERR_NOT_SUPPORTED: return "no_motor";
    default: return "not_ready";
    }
}

static int vibe_cmd(int argc, char **argv) {
    if (argc < 2) {
        const unsigned q = H.inited ? quiet_now() : 0;
        printf("YK_HAPTIC on=%d level=%s available=%d busy=%d duty=%d mic=%d install=%d battery=%d pin=%s hook=%d "
               "sys_low=%d flash=%d\n",
               H.core.set.on, LEVEL_NAMES[ykh_level_clamp(H.core.set.level)], yk_haptic_available(), yk_haptic_busy(),
               H.written, !!(q & YKH_QUIET_MIC), !!(q & YKH_QUIET_INSTALL), !!(q & YKH_QUIET_BATTERY),
               PIN_NAMES[H.pin], H.hooked, H.sys_low, __atomic_load_n(&H.flash, __ATOMIC_ACQUIRE));
        return 0;
    }
    if (!strcmp(argv[1], "stop")) {
        yk_haptic_stop();
        printf("YK_HAPTIC stop\n");
        return 0;
    }
    if (!strcmp(argv[1], "duty")) {
        const int pct = argc == 4 ? atoi(argv[2]) : -1, ms = argc == 4 ? atoi(argv[3]) : 0;
        if (pct < 0 || pct > 100 || ms < YKH_SEG_MIN_MS || ms > YKH_ON_MAX_MS) {
            printf("YK_HAPTIC usage: vibe duty <0-100> <%d-%d ms>\n", YKH_SEG_MIN_MS, YKH_ON_MAX_MS);
            return 1;
        }
        const int duty = pct * YKH_DUTY_FULL / 100;
        if (!duty) {
            yk_haptic_stop();
            printf("YK_HAPTIC duty 0: stop\n");
            return 0;
        }
        const uint16_t seg[] = {(uint16_t)ms};
        const esp_err_t err = start(seg, 1, -1, duty, false);
        printf("YK_HAPTIC duty pct=%d raw=%d ms=%d result=%s\n", pct, duty, ms, err_word(err));
        return err == ESP_OK ? 0 : 1;
    }
    for (int k = 0; k < 5; k++) {
        if (strcmp(argv[1], KIND_NAMES[k])) continue;
        int level = -1;
        if (argc >= 3) {
            for (int l = 0; l < 3; l++)
                if (!strcmp(argv[2], LEVEL_NAMES[l])) level = l;
            if (level < 0) {
                printf("YK_HAPTIC usage: vibe %s [light|medium|strong]\n", KIND_NAMES[k]);
                return 1;
            }
        }
        uint16_t seg[YKH_SEG_MAX];
        const int n = ykh_builtin(k + 1, seg);
        const esp_err_t err = start(seg, n, level, 0, true);
        printf("YK_HAPTIC play kind=%s level=%s result=%s\n", KIND_NAMES[k],
               LEVEL_NAMES[ykh_level_clamp(level < 0 ? H.core.set.level : level)], err_word(err));
        return err == ESP_OK ? 0 : 1;
    }
    printf("YK_HAPTIC usage: vibe | tick|double|short|long|notify [light|medium|strong] | duty <0-100> <ms> | stop\n");
    return 1;
}

esp_err_t yk_haptic_register_console(void) {
    const esp_console_cmd_t cmd = {
        .command = "vibe",
        .help = "vibe | tick|double|short|long|notify [light|medium|strong] | duty <0-100> <ms> | stop",
        .func = vibe_cmd,
    };
    return esp_console_cmd_register(&cmd);
}

#else

esp_err_t yk_haptic_register_console(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif
