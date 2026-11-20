// The vibration motor (docs/delivery/2026-11-13-本事和规矩-振动和姿态.md 1 and 3): GPIO46 → R24 1 kΩ → Q1 MMBT3904 →
// M2, an ERM with D2 across it and R55 10 kΩ pulling GPIO46 down. Every unit has one (the user, 11-13). Only this
// component drives GPIO46: never gpio_* or LEDC on it yourself.
//
// Sound first, vibration beside it (round-screen-design.md rule 7): every confirmation, step and result has its sound;
// a vibration may go with it, never stand in for it. So there is no "vibrate, or else a sound": play the sound as
// always, and yk_haptic_play() next to it.
//
//   yk_haptic_init()       once, after nvs_flash_init(): reads the settings; GPIO46 is not touched until a first play
//   yk_haptic_play(kind)   one of the five kinds; returns at once, from any task (not an ISR): the motion callback too
//   yk_haptic_pattern()    an app's own: on, off, on, ... in ms
//   yk_haptic_stop()       off now
//
// The strength is the user's (Settings: on or off, light, medium, strong; yk_pref `vibe_on`, `vibe_level`), not the
// app's: an app asks for a kind, the device decides how strong. Light and medium start each pulse at 100 % for 25 ms
// (the motor does not start from still below 50 %), then 50 % or 75 %.
//
// It does not vibrate (play returns ESP_ERR_NOT_ALLOWED, and what plays stops within 20 ms) while:
//   - the user has turned vibration off;
//   - the microphones are open (yaykit_audio: a recording, push-to-talk, listening; yaykit_voice): the motor's hum reaches
//     them at -22 dBFS and noise suppression does not take it out. Read through yk_audio_running() and
//     yk_voice_recording(), linked weakly: an app without them has no microphones open. The motor runs on 150–250 ms
//     after it goes off, so do not vibrate on the press that opens them. Not while they only read the level
//     (yk_haptic_metering(), the main firmware's badge);
//   - an install writes flash (yaykit_install's yk_install_busy(), weakly; it also stops the motor as it starts), or
//     the main firmware does (yk_haptic_flash());
//   - an extension's battery guard is on (yaykit_app's yk_app_battery_low(), weakly), or the main firmware's battery
//     line (yk_haptic_set_battery_low(): Yay has a battery service of its own, and yk_app_battery_low() is false there).
// Not yet: Yay's night-time quiet hours (there is no such shared state yet).
//
// GPIO46 is a strapping pin and must be low at reset (GPIO0 low with GPIO46 high is not a valid boot). The component
// leaves it alone until the first play, then: gpio_reset_pin, low, output, LEDC (20 kHz, 10 bits, LEDC_TIMER_3,
// LEDC_CHANNEL_7); off is ledc_stop() with the pin held low. Every way out stops it first:
//   - yaykit_app's way out (a double click, yk_return_home, yk_app_open, yk_app_recovery) and its battery sleep;
//   - every esp_restart(): a shutdown handler, registered at the first play (one of ESP-IDF's five; when none is free
//     it says so on the log, and the reset alone still lets go of the pin, which R55 pulls low);
//   - yaykit_install, as an install starts.
// Not hooked, so the caller's: before esp_deep_sleep_start() of its own (Yay's yay_run.c, not yaykit_app's), and before
// writing flash itself (esp_partition_erase_range/_write, esp_ota_*), call yk_haptic_stop() and play nothing until it is
// done: with the cache off the timer that ends a pulse runs late. For flash, yk_haptic_flash(true) before and
// yk_haptic_flash(false) after does both. In deep sleep GPIO46 (not an RTC pad) is not held and falls back to its
// pull-down, so the motor stops anyway.
//
// The motor is never on longer than 2 s in one go: a pattern's on time is at most 2 s in all, and plays that keep
// replacing each other while it runs are cut at 2 s, then it rests 1 s. Pulses closer than 150 ms merge into one, so a
// play that comes less than 150 ms after the motor went off waits for that.
//
// Development builds (CONFIG_YAY_DEV): console `vibe` (see yk_haptic_register_console).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YK_HAPTIC_GPIO 46

/** The five kinds (docs/delivery/2026-11-13-本事和规矩-振动和姿态.md 1, measured: a pulse under 30 ms is not felt). */
typedef enum {
    YK_HAPTIC_TICK = 1,  // 40 ms: a pick, one step of a dial (with the settings ring's 「嗒」)
    YK_HAPTIC_DOUBLE,    // 40 / 160 / 40: confirmed, done
    YK_HAPTIC_SHORT,     // 120 ms: a hint
    YK_HAPTIC_LONG,      // 500 ms: a timer's end, an alarm's beat
    YK_HAPTIC_NOTIFY,    // 200 on, 250 off, three times: a message, a reminder
} yk_haptic_kind_t;

#define YK_HAPTIC_PATTERN_MAX 16    // segments in yk_haptic_pattern() at most
#define YK_HAPTIC_SEGMENT_MIN 20    // ms, each segment at least
#define YK_HAPTIC_ON_MAX      2000  // ms, a pattern's on segments in all at most

/** Reads the settings (NVS must be initialised; from a task whose stack is in internal RAM) and makes the timers.
 *  GPIO46 stays as the reset left it until the first play. Idempotent. ESP_ERR_NO_MEM when the timers could not be
 *  made. */
esp_err_t yk_haptic_init(void);

/** It can vibrate: initialised, the motor set up (or not yet tried), and the user has vibration on, and an extension's
 *  battery guard is off. It may still not vibrate at a given moment: the microphones open, an install writing. */
bool yk_haptic_available(void);

/** Plays a kind at the user's strength, replacing whatever plays or waits. While the motor runs it goes on from there;
 *  otherwise it starts 150 ms after the motor last went off at the soonest.
 *  ESP_OK it plays (or starts after that gap); ESP_ERR_INVALID_ARG not a kind; ESP_ERR_NOT_ALLOWED not now (the user's
 *  setting, the microphones, an install, the battery); ESP_ERR_INVALID_STATE before yk_haptic_init(), or the firmware is
 *  restarting; ESP_ERR_NOT_SUPPORTED the motor's pin or LEDC could not be set up. */
esp_err_t yk_haptic_play(yk_haptic_kind_t kind);

/** An app's own pattern: on_off_ms[0] on, [1] off, [2] on, ...; 1..YK_HAPTIC_PATTERN_MAX segments, each at least
 *  YK_HAPTIC_SEGMENT_MIN ms, the on ones at most YK_HAPTIC_ON_MAX ms in all (ESP_ERR_INVALID_ARG otherwise). An off
 *  under 150 ms is kept as given, and feels as one long pulse. The array is copied. Otherwise as yk_haptic_play(). */
esp_err_t yk_haptic_pattern(const uint16_t *on_off_ms, int n);

/** Off now; what plays and what waits are gone. The motor runs on 150–250 ms by itself (no brake). Any task. */
void yk_haptic_stop(void);

/** A play is running or waiting to start. */
bool yk_haptic_busy(void);

/** Reads the settings again (the settings ring changed them; NVS: a task whose stack is in internal RAM). Off stops what
 *  plays; a new strength applies from the next play. */
void yk_haptic_reload(void);

/** The main firmware's (its settings ring), never an app's: keeps `on` and `level` (0 light, 1 medium, 2 strong; any
 *  other light) in yk_pref `vibe_on` and `vibe_level` (both u8) for every firmware, and uses them at once. Writes only
 *  what changed. NVS: a task whose stack is in internal RAM. */
esp_err_t yk_haptic_set_settings(bool on, int level);

/** The settings as kept (yk_pref `vibe_on`, `vibe_level`; neither written yet: on, light), for the main firmware's
 *  settings page. Before yk_haptic_init() they are read from NVS (a task whose stack is in internal RAM). */
void yk_haptic_get_settings(bool *on, int *level);

/** The main firmware's battery line, never an app's (an extension's guard is read from yaykit_app by itself): Yay's own
 *  battery service at most 15 % and not plugged in, the line its settings ring's middle turns coral at. While it is
 *  low nothing vibrates (play returns ESP_ERR_NOT_ALLOWED, yk_haptic_available() is false) and what plays stops within
 *  20 ms. Any task, as often as the battery is read. */
void yk_haptic_set_battery_low(bool low);

/** The caller writes flash itself (esp_partition_erase_range/_write, esp_ota_*; yaykit_install's own writes need none
 *  of this): `true` as a write starts stops the motor and keeps it still until as many `false` have come (writes in
 *  other tasks count apart). Any task, before yk_haptic_init() too. */
void yk_haptic_flash(bool writing);

/** The main firmware's, never an app's (Yay 0.1.20 开发文档 4.6, the badge): `true` while the microphones it opened only
 *  read the level (blowing, clapping; nothing recorded, nothing sent), so they do not keep it still; the caller keeps its
 *  own buzz and the motor's 250 ms tail out of what it reads. `false` before they record (talking): still again while
 *  open, as ever. Any task. */
void yk_haptic_metering(bool on);

/** Development builds (CONFIG_YAY_DEV): `vibe` (where it stands) | `vibe tick|double|short|long|notify [light|medium|
 *  strong]` (the strength for this once, the setting untouched) | `vibe duty <0-100> <ms>` (that duty, no kick, at most
 *  2000 ms) | `vibe stop`. After esp_console_init(). Release builds: ESP_ERR_NOT_SUPPORTED, nothing registered. */
esp_err_t yk_haptic_register_console(void);

#ifdef __cplusplus
}
#endif
