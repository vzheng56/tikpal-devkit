// The device's settings and the battery guard, the device side (0.1.13; yk_app_power.h has the rules, pure).
//
// Settings: read from NVS in yk_app_start() (yaykit_board's yk_pref, and the owner's language from yaykit_cloud's
// yk_acct "loc", read only) and again at each look at the battery, so the getters an app's tick calls never touch
// flash. Battery guard, only in an extension in an app slot: a look before anything draws power in yk_app_start(),
// then an LVGL timer from yk_app_ready() (the LVGL task's stack is in internal RAM: the ADC, NVS; no task of our own
// stays, so no internal RAM is held for it). The sleep runs in a task of its own, made only then.
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "cjk_font.h"
#include "driver/rtc_io.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "yaykit_app.h"
#include "yaykit_key.h"
#include "yk_app_ring.h"
#include "yk_apps.h"
#include "yk_hint.h"
#include "yk_round.h"

static const char *TAG = "yk_power";

// Weak, so firmware without it still links: Homework Buddy has no yaykit_speak (its chime drives the amplifier, whose
// pull-down keeps it off asleep). It resolves where the firmware uses yaykit_speak. The radio is not stopped here:
// deep sleep powers it down by itself (ESP-IDF's PHY sleep callback), the app's wrap-up has closed its connection, and a
// reference to esp_wifi_stop() would keep the whole Wi-Fi stack (7 KB of internal RAM) in an app that never starts it
// (Recording Card links yaykit_wifi through yaykit_cloud only).
void yk_speak_sleep(void) __attribute__((weak));

#define WATCH_MS 1000      // the LVGL timer: is a look at the battery due (a time compare, nothing more)
#define SLEEP_STACK 6144   // the page, the app's wrap-up wait and esp_ota's check of Yay's image (as leaving's task)
#define KEY_UP_WAIT_MS 30000

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static struct {
    int volume, brightness, screen_off_s;
    char locale[8];
} s_stored = {BOARD_PREF_VOLUME_DEFAULT, BOARD_PREF_BRIGHTNESS_DEFAULT, BOARD_PREF_SCREEN_OFF_DEFAULT, "zh"};  // s_mux
static yk_power_t s_power = {.percent = -1};  // the watch's; s_mux
static volatile bool s_keep_on;
static bool s_guard;                // an extension in an app slot: the battery guard is on
static volatile bool s_sleeping;    // going to sleep: nothing more from the watch
static int64_t s_next_read_ms;      // the watch's next look (LVGL task)
static int s_fake_pct = -2;         // development: `yay power <pct>` (-2: the battery's own)
static bool s_fake_usb;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

// ---------------------------------------------------------------- the settings

static void read_locale(char *out, size_t size) {
    // yaykit_cloud keeps the owner's language here (yk_locale.h): read only, this one key, never the rest of yk_acct.
    snprintf(out, size, "zh");
    nvs_handle_t h;
    if (nvs_open("yk_acct", NVS_READONLY, &h) != ESP_OK) return;
    size_t len = size;
    if (nvs_get_str(h, "loc", out, &len) != ESP_OK) snprintf(out, size, "zh");
    nvs_close(h);
}

/** NVS: from a task whose stack is in internal RAM. */
static void load(void) {
    const int volume = board_pref_volume(), brightness = board_pref_brightness(), off = board_pref_screen_off_s();
    char locale[sizeof(s_stored.locale)];
    read_locale(locale, sizeof(locale));
    taskENTER_CRITICAL(&s_mux);
    s_stored.volume = volume;
    s_stored.brightness = brightness;
    s_stored.screen_off_s = off;
    memcpy(s_stored.locale, locale, sizeof(locale));
    taskEXIT_CRITICAL(&s_mux);
}

void yk_app_settings(yk_settings_t *out) {
    taskENTER_CRITICAL(&s_mux);
    const int volume = s_stored.volume, brightness = s_stored.brightness, off = s_stored.screen_off_s;
    char locale[sizeof(s_stored.locale)];
    memcpy(locale, s_stored.locale, sizeof(locale));
    const bool low = yk_power_low(&s_power);
    taskEXIT_CRITICAL(&s_mux);
    yk_settings_merge(volume, brightness, off, locale, low, s_keep_on, out);
}

int yk_app_brightness(void) {
    yk_settings_t s;
    yk_app_settings(&s);
    return s.brightness;
}

int yk_app_screen_off_s(void) {
    yk_settings_t s;
    yk_app_settings(&s);
    return s.screen_off_s;
}

int yk_app_volume(void) {
    yk_settings_t s;
    yk_app_settings(&s);
    return s.volume;
}

bool yk_app_english(void) {
    yk_settings_t s;
    yk_app_settings(&s);
    return s.english;
}

void yk_app_locale(char *out, size_t size) {
    taskENTER_CRITICAL(&s_mux);
    char locale[sizeof(s_stored.locale)];
    memcpy(locale, s_stored.locale, sizeof(locale));
    taskEXIT_CRITICAL(&s_mux);
    snprintf(out, size, "%s", locale);
}

esp_err_t yk_app_set_volume(int level) {
    const esp_err_t err = board_pref_set_volume(level);
    const int kept = board_pref_volume();
    taskENTER_CRITICAL(&s_mux);
    s_stored.volume = kept;
    taskEXIT_CRITICAL(&s_mux);
    return err;
}

void yk_app_settings_reload(void) { load(); }

void yk_app_keep_screen_on(bool on) {
    if (on != s_keep_on) printf("YAY_APP screen keep_on=%d\n", on);
    s_keep_on = on;
}

bool yk_app_battery_low(void) {
    taskENTER_CRITICAL(&s_mux);
    const bool low = yk_power_low(&s_power);
    taskEXIT_CRITICAL(&s_mux);
    return low;
}

// ---------------------------------------------------------------- the battery

#if CONFIG_YAY_DEV
// Development: `yay power next <pct> [usb]`, the next start's reading made up once (RTC memory: a restart keeps it).
#define NEXT_MAGIC 0x594b4e58u
static RTC_NOINIT_ATTR struct {
    uint32_t magic;
    int8_t pct;
    uint8_t usb;
} s_next;
#endif

/** The level (-1 when not known) and USB, as board_battery() reads them (or `yay power` made them up). False when
 *  nothing could be read. */
static bool reading(int *pct, bool *usb) {
    if (s_fake_pct > -2) {
        *pct = s_fake_pct, *usb = s_fake_usb;
        return true;
    }
    board_battery_t b;
    const esp_err_t err = board_battery(&b);
    if (err != ESP_OK && err != ESP_ERR_INVALID_RESPONSE) return false;
    *pct = err == ESP_OK ? b.percent : -1;
    *usb = b.usb;
    return true;
}

/** Deep sleep until the side key (GPIO0, low pressed: ext0) or USB 5 V (GPIO18, high: ext1) wakes the chip, and every
 *  YK_POWER_LOOK_S the timer, as Yay's own sleep (yay_run.c deep_sleep). The boot partition is pointed at Yay first:
 *  whatever wakes it starts Yay, which looks at the battery itself (yay_power_boot). Last, the amplifier off and the
 *  panel (`panel`: it was drawn) into sleep-in with the peripheral power (GPIO21) off, both held. Never returns. */
static void deep_sleep(bool panel, int pct) {
    const char *home = "yay";
    const esp_err_t boot = yk_app_boot_home_next(&home);
    if (boot != ESP_OK) ESP_LOGE(TAG, "boot partition not set to %s: %s", home, esp_err_to_name(boot));
    panel = panel && board_lock(500) == ESP_OK;  // kept: nothing draws from here on
    if (panel) board_display_sleep();
    // GPIO0 is the download pin, and held down it would wake the chip again at once: up first.
    const bool key = yk_key_wait_release(KEY_UP_WAIT_MS);  // held on and on: USB and the timer still wake it
    if (key) {
        rtc_gpio_pullup_en(GPIO_NUM_0);
        rtc_gpio_pulldown_dis(GPIO_NUM_0);
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
    }
    rtc_gpio_pullup_dis(BOARD_PIN_VBUS);  // the divider holds it at 0 V without power
    rtc_gpio_pulldown_dis(BOARD_PIN_VBUS);
    esp_sleep_enable_ext1_wakeup_io(1ULL << BOARD_PIN_VBUS, ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_sleep_enable_timer_wakeup((uint64_t)YK_POWER_LOOK_S * 1000000ULL);
    if (yk_speak_sleep) yk_speak_sleep();
    const esp_err_t held = board_sleep_prepare(panel);
    printf("YAY_APP power deep_sleep next=%s boot_set=%d key=%d usb=1 look_s=%d pct=%d panel_sleep=%d held=%d\n", home,
           boot == ESP_OK, key, YK_POWER_LOOK_S, pct, panel, held == ESP_OK);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(50));  // the line out over USB
    esp_deep_sleep_start();
}

static lv_obj_t *plain(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(o, false);
    return o;
}

/** 「电量用完了，请充电」 over everything, as Yay's battery page draws it (yay_ui_power_screen): a battery lying down,
 *  nearly empty, and the line under it. Under the board lock. */
static void sleep_page(void) {
    yk_apps_close(false);  // the app ring and 「回到 Yay 说？」, when up
    yk_apps_ask_close();
    yk_hint_hide();
    if (board_display_asleep()) board_display_wake(0);  // lit below, once drawn
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_scrollable(scr, false);
    lv_obj_set_style_bg_color(scr, lv_color_hex(YK_PAPER), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_t *cell = plain(scr, 233 - 66 - 6, 150, 132, 64, YK_PAPER);
    lv_obj_set_style_radius(cell, 14, 0);
    lv_obj_set_style_border_width(cell, 5, 0);
    lv_obj_set_style_border_color(cell, lv_color_hex(YK_INK2), 0);
    lv_obj_t *knob = plain(scr, 233 + 66 - 6 + 3, 150 + 20, 10, 24, YK_INK2);
    lv_obj_set_style_radius(knob, 4, 0);
    lv_obj_t *left = plain(cell, 5, 5, 16, 64 - 20, YK_AMBER);
    lv_obj_set_style_radius(left, 5, 0);
    lv_obj_t *l = lv_label_create(scr);
    lv_obj_set_pos(l, 78, 250);
    lv_obj_set_width(l, 310);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(YK_INK), 0);
    const lv_font_t *f = cjk_font_get(22);
    if (f) lv_obj_set_style_text_font(l, f, 0);
    lv_label_set_text(l, yk_app_english() ? "Out of battery. Please charge me." : "电量用完了，请充电");
    lv_screen_load(scr);
    lv_display_trigger_activity(NULL);  // the app's own screen-off waits from now (it is not shown anyway)
}

static void sleep_task(void *arg) {
    const int pct = (int)(intptr_t)arg;
    const int64_t t0 = now_ms();
    yk_app_ring_quit();  // the key does nothing more
    bool shown = false;
    if (board_lock(1000) == ESP_OK) {
        sleep_page();
        board_unlock();
        shown = true;
    }
    if (shown) {
        vTaskDelay(pdMS_TO_TICKS(120));  // the LVGL task draws it, then it is lit, dim
        board_brightness_set(YK_POWER_DIM);
    }
    printf("YAY_APP power sleeping pct=%d shown=%d\n", pct, shown);
    yk_app_run_leave("sleep");  // the app's wrap-up: its sound out, its result and record written
    const int64_t rest = t0 + YK_POWER_SLEEP_SHOW_MS - now_ms();
    if (rest > 0) vTaskDelay(pdMS_TO_TICKS(rest));
    while (yk_app_leave_held()) vTaskDelay(pdMS_TO_TICKS(500));  // never while a slot is being written
    deep_sleep(shown, pct);
}

/** From the watch (the LVGL task, lock held) or `yay power sleep`: the page, then the sleep, in a task of its own. */
static void go_to_sleep(int pct) {
    if (s_sleeping || yk_app_leaving()) return;  // leaving already: the restart is Yay's, which looks itself
    s_sleeping = true;
    if (xTaskCreate(sleep_task, "yk_sleep", SLEEP_STACK, (void *)(intptr_t)pct, 6, NULL) == pdPASS) return;
    ESP_LOGW(TAG, "no room for the sleep's task: sleeping from here, no page");
    deep_sleep(true, pct);
}

static void watch(lv_timer_t *t) {
    (void)t;
    const int64_t now = now_ms();
    if (s_sleeping || now < s_next_read_ms) return;
    int pct = -1;
    bool usb = false;
    if (!reading(&pct, &usb)) {  // the ADC was busy (Wi-Fi shares ADC2): again soon
        s_next_read_ms = now + YK_POWER_READ_LOW_MS;
        return;
    }
    load();  // and the settings: the phone may have changed them through yaykit_talk
    taskENTER_CRITICAL(&s_mux);
    const yk_power_ev_t e = yk_power_feed(&s_power, pct, usb);
    s_next_read_ms = now + yk_power_read_ms(&s_power);
    taskEXIT_CRITICAL(&s_mux);
    if (e == YK_POWER_NONE) return;
    printf("YAY_APP power %s pct=%d usb=%d\n", yk_power_ev_name(e), pct, usb);
    // Low: a lit screen comes down to the cap at once (the app's own lighting reads yk_app_brightness from here on).
    if (e == YK_POWER_LOW_NOW && !board_display_asleep() && board_brightness_get() > YK_POWER_BRIGHTNESS_MAX)
        board_brightness_set(YK_POWER_BRIGHTNESS_MAX);
    if (e == YK_POWER_SLEEP) go_to_sleep(pct);
}

void yk_app_power_start(void) {
    load();
    s_guard = yk_app_is_extension();
    if (!s_guard) return;
    const esp_reset_reason_t reset = esp_reset_reason();
    int pct = -1;
    bool usb = false;
    bool read = reading(&pct, &usb);
#if CONFIG_YAY_DEV
    if (s_next.magic == NEXT_MAGIC) {
        pct = s_next.pct, usb = s_next.usb, read = true;
        s_next.magic = 0;
        printf("YAY_APP power next pct=%d usb=%d (made up)\n", pct, usb);
    }
#endif
    if (read) {
        taskENTER_CRITICAL(&s_mux);
        yk_power_feed(&s_power, pct, usb);
        taskEXIT_CRITICAL(&s_mux);
    }
    // Nothing read: start (USB is not known either).
    const bool sleep = read && yk_power_boot_sleep(reset == ESP_RST_BROWNOUT, pct, usb);
    printf("YAY_APP power boot=%s pct=%d usb=%d reset=%d\n", sleep ? "sleep" : "run", pct, usb, (int)reset);
    if (!sleep) return;
    s_sleeping = true;
    deep_sleep(false, pct);  // dark: nothing was lit, nothing is
}

void yk_app_power_ready(void) {
    if (!s_guard || board_brightness_raw_get() < 0) return;  // no display: no LVGL task to watch from
    s_next_read_ms = now_ms() + yk_power_read_ms(&s_power);  // the start's reading was the first
    if (board_lock(2000) != ESP_OK) {
        ESP_LOGE(TAG, "board lock not had: the battery is not watched");
        return;
    }
    lv_timer_create(watch, WATCH_MS, NULL);
    board_unlock();
}

int yk_app_power_dev(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "sleep")) {
        go_to_sleep(s_power.percent);
        return 0;
    }
#if CONFIG_YAY_DEV
    if (argc > 2 && !strcmp(argv[1], "next")) {
        s_next.pct = (int8_t)atoi(argv[2]);
        s_next.usb = argc > 3 && !strcmp(argv[3], "usb");
        s_next.magic = NEXT_MAGIC;
        printf("YAY_APP power next start pct=%d usb=%d\n", s_next.pct, s_next.usb);
        return 0;
    }
#endif
    if (argc > 1 && !strcmp(argv[1], "off")) {
        s_fake_pct = -2;
    } else if (argc > 1) {
        s_fake_pct = atoi(argv[1]);
        s_fake_usb = argc > 2 && !strcmp(argv[2], "usb");
        s_next_read_ms = 0;  // looked at on the next tick
    }
    yk_settings_t s;
    yk_app_settings(&s);
    printf("YAY_APP power pct=%d usb=%d low=%d empty_reads=%d next_s=%d fake=%d keep_on=%d brightness=%d screen_off=%d "
           "volume=%d english=%d\n",
           s_power.percent, s_power.usb, yk_power_low(&s_power), s_power.empty_reads,
           s_next_read_ms ? (int)((s_next_read_ms - now_ms()) / 1000) : 0, s_fake_pct > -2, s_keep_on, s.brightness,
           s.screen_off_s, s.volume, s.english);
    return 0;
}
