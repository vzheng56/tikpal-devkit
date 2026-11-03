// The device's settings and the battery guard as every app uses them (0.1.13): pure C, host tested (test/run.sh).
// yaykit_app.h has the device side (yk_app_settings(), yk_app_brightness() ...).
//
// Settings (docs/delivery/2026-10-27-connecting-and-fast-reconnect.md 4, 共享设置): Yay's volume, brightness and
// screen-off time (yaykit_board's shared NVS yk_pref) and its owner's language, merged with what the battery allows and
// what the app holds (the screen kept on while it is busy), so that an app follows Yay with one call.
//
// Battery guard (docs/delivery/2026-10-27-battery-overdischarge.md 5, 第 2 步): an extension built with this yaykit_app
// (0.1.13; YK_API_LEVEL stays 3: these are additions, and the behaviour is in the extension's own image)
//   - looks at the battery in yk_app_start(), before the screen and the radio: too low (or the last run ended in a
//     brownout) on battery, it does not start but sleeps at once, dark, the boot partition back on Yay;
//   - while it runs (from yk_app_ready()), reads the level every YK_POWER_READ_MS (every YK_POWER_READ_LOW_MS once
//     close): at YK_POWER_EMPTY YK_POWER_EMPTY_READS times in a row it shows 「电量用完了，请充电」, runs the app's
//     wrap-up (yk_app_on_leave), points the boot partition at Yay and sleeps; the side key or USB wakes Yay, which does
//     its own start check (yay_power.h);
//   - at YK_POWER_LOW caps the brightness at YK_POWER_BRIGHTNESS_MAX and the screen-off time at
//     YK_POWER_SCREEN_OFF_LOW_S, as Yay does; plugged in, every cap goes.
// The thresholds are Yay's step 1 as it is today (yay_power.h, 0.1.8): percent on yaykit_board's curve of a resting cell
// (3.30 V = 0 %, 3.50 V = 5 %, 3.60 V = 10 %, 3.70 V = 25 %). They become millivolts once a device on its battery has
// been measured (battery over-discharge 4.7): only these constants change then.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YK_POWER_LOW 15                // on battery: brightness and screen-off capped (Yay's YAY_POWER_LOW)
#define YK_POWER_EMPTY 5               // about 3.50 V: an app's start at or below it sleeps; running, see the next
#define YK_POWER_EMPTY_READS 2         // readings in a row at YK_POWER_EMPTY or below before a running app sleeps
                                       // (one reading pulled down by the speaker does not)
#define YK_POWER_BRIGHTNESS_MAX 60     // percent, while low (Yay's YAY_POWER_BRIGHTNESS_MAX)
#define YK_POWER_SCREEN_OFF_LOW_S 15   // seconds at rest before the screen goes off while low, whatever is set
#define YK_POWER_READ_MS 30000         // the level, normally
#define YK_POWER_READ_LOW_MS 10000     // ... at YK_POWER_LOW and below, or once one reading said YK_POWER_EMPTY
#define YK_POWER_LOOK_S 1800           // asleep: the timer wakes it (into Yay) this often to look for USB, as Yay's
                                       // own sleep does (GPIO18's 2.5 V may not wake it)
#define YK_POWER_SLEEP_SHOW_MS 2500    // 「电量用完了，请充电」 stays this long before the sleep
#define YK_POWER_DIM 30                // percent: that page's brightness

// ---------------------------------------------------------------- the battery

typedef enum {
    YK_POWER_NONE,
    YK_POWER_LOW_NOW,  // YK_POWER_LOW reached on battery, the first time this discharge: caps from now on
    YK_POWER_PLUGGED,  // external power came: caps lifted
    YK_POWER_SLEEP,    // YK_POWER_EMPTY YK_POWER_EMPTY_READS times in a row on battery: sleep (said once)
} yk_power_ev_t;

typedef struct {
    bool known;       // a reading came
    int percent;      // the last level, -1 not known
    bool usb;         // external power, at the last reading
    bool low;         // YK_POWER_LOW reached this discharge (false on USB)
    int empty_reads;  // readings in a row at YK_POWER_EMPTY or below, on battery
    bool sleep;       // YK_POWER_SLEEP was said
} yk_power_t;

void yk_power_init(yk_power_t *p);
/** A reading: the level (-1 when not known: never low, never a sleep) and whether USB is there. */
yk_power_ev_t yk_power_feed(yk_power_t *p, int percent, bool usb);
const char *yk_power_ev_name(yk_power_ev_t ev);
/** Low this discharge, on battery: brightness and screen-off capped. */
bool yk_power_low(const yk_power_t *p);
/** How long until the next look at the level. */
int yk_power_read_ms(const yk_power_t *p);

/** An app's start (before the screen and the radio): true when it should sleep instead. On USB never; after a
 *  brownout on battery always (lit and on the radio, a nearly empty cell dipped; starting again would only do it again,
 *  as Yay's YAY_BOOT_BROWNOUT); otherwise at YK_POWER_EMPTY or below (-1, not read: start). */
bool yk_power_boot_sleep(bool brownout, int percent, bool usb);

// ---------------------------------------------------------------- the settings

/** What an app uses. */
typedef struct {
    int volume;        // 0..10, yaykit_speak's steps
    int brightness;    // percent the screen comes on at, 5..100
    int screen_off_s;  // seconds of no input before the screen goes off; 0 never
    bool english;      // screens in English: the owner's language is not Chinese
} yk_settings_t;

/** Screens in English for this locale (yk_acct "loc": "zh-Hans", "en", "ja", "es-419"; "zh" from before): anything but
 *  "zh…" (yaykit_cloud's yk_locale_english, kept the same here: yaykit_app does not need yaykit_cloud). NULL or "":
 *  Chinese. */
bool yk_settings_english(const char *locale);

/** The settings an app uses now, from what is stored (board_pref_*: already in range) and the locale, what the battery
 *  allows (`low`: yk_power_low) and whether the app keeps the screen on (`keep_on`, yk_app_keep_screen_on):
 *  - brightness: the stored one, at most YK_POWER_BRIGHTNESS_MAX while low;
 *  - screen-off: 0 (never) while the app keeps the screen on; else YK_POWER_SCREEN_OFF_LOW_S while low (even when set
 *    to never); else the stored one;
 *  - volume, language: as stored. */
void yk_settings_merge(int volume, int brightness, int screen_off_s, const char *locale, bool low, bool keep_on,
                       yk_settings_t *out);

#ifdef __cplusplus
}
#endif
