// The vibration's scheduling, pure C (no ESP-IDF), host tested with test/run.sh. The device side (yaykit_haptic.c)
// reads the clock, the settings and the quiet conditions, hands them in, writes the duty the core gives to LEDC and
// arms one esp_timer for the core's next wake; the core never reads the clock itself.
//
// The motor (docs/delivery/2026-11-13-本事和规矩-振动和姿态.md 1, measured on unit 2): an ERM behind one low-side
// transistor, so on, off and PWM only, no brake. It is heard 20 ms after it goes on and runs on 150–250 ms after it goes
// off; 30 ms is felt as one tap; two taps less than 150 ms apart merge into one; at 50 % duty and up it clearly
// vibrates, at 20 % it does not start.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define YKH_SEG_MAX     16    // a pattern's segments at most: on, off, on, ...
#define YKH_SEG_MIN_MS  20    // each at least (shorter the motor does not start, nor stop)
#define YKH_ON_MAX_MS   2000  // a play's on time in all; and the motor never on longer than this in one go
#define YKH_REST_MS     1000  // after the motor was cut at YKH_ON_MAX_MS in one go: off this long before the next play
#define YKH_GAP_MS      150   // from the motor going off to the next play's first on (closer, two taps feel as one)
#define YKH_KICK_MS     25    // light and medium: 100 % for this long from rest, then the level's duty (start from still)
#define YKH_POLL_MS     20    // while the motor is on: how often the quiet conditions are looked at again
#define YKH_DUTY_FULL   1024  // LEDC at 10 bits: 1024 is on all the time

// The strength (yk_pref `vibe_level`): light 50 %, medium 75 %, strong 100 %.
typedef enum { YKH_LEVEL_LIGHT = 0, YKH_LEVEL_MEDIUM = 1, YKH_LEVEL_STRONG = 2 } ykh_level_t;

// Why the motor is not to run now. The device side finds them; any of them stops a play and refuses a new one.
enum {
    YKH_QUIET_MIC = 1,      // the microphones are open (a recording, push-to-talk, listening): the motor's 205 Hz hum
                            // reaches them at -22 dBFS, and noise suppression does not take it out
    YKH_QUIET_INSTALL = 2,  // an install (or the main firmware itself) is writing flash: the cache is off, the timer that
                            // ends a pulse may be late
    YKH_QUIET_BATTERY = 4,  // the extension's battery guard is on (low this discharge), or the main firmware's line
};

typedef struct {
    bool on;    // yk_pref `vibe_on`
    int level;  // ykh_level_t, yk_pref `vibe_level`
} ykh_settings_t;

typedef struct {
    ykh_settings_t set;
    bool shut;              // the firmware is restarting: nothing plays any more
    // the play
    bool active;            // a play is running, or waiting for its start
    uint16_t seg[YKH_SEG_MAX];
    int n;
    int i;                  // the segment running, -1 while waiting for start_ms
    int duty;               // the play's duty (its level's, or the console's)
    bool kick;              // start each on from rest at full duty for YKH_KICK_MS
    int64_t start_ms;       // waiting: when segment 0 begins
    int64_t seg_end_ms;     // running: when segment i ends
    int64_t kick_end_ms;    // the kick of this on segment ends; 0 none
    // the output
    int out;                // the duty written now, 0 off
    int64_t on_since_ms;    // the output on without a break since then (for YKH_ON_MAX_MS)
    int64_t off_at_ms;      // the output last went off then (YKH_GAP_MS); INT64_MIN / 2 never
    int64_t rest_until_ms;  // after a cut at YKH_ON_MAX_MS: no play starts before this
} ykh_core_t;

/** What to do now: write `duty` (0: stopped, the pin low), and step again at `wake_ms` (-1: nothing to wake for). */
typedef struct {
    int duty;
    int64_t wake_ms;
} ykh_out_t;

typedef enum {
    YKH_PLAYED = 0,  // playing, or starting once the gap after the last pulse is over
    YKH_BAD,         // not a kind, or a pattern that breaks the rules (ykh_check)
    YKH_OFF,         // the user turned vibration off
    YKH_QUIET,       // not now (a YKH_QUIET_* condition)
    YKH_SHUT,        // the firmware is restarting
} ykh_result_t;

typedef enum { YKH_OK = 0, YKH_BAD_COUNT, YKH_BAD_SEGMENT, YKH_TOO_LONG } ykh_check_t;

/** The settings as yk_pref keeps them: each stored value, or -1 when the key is missing (a Yay from before vibration).
 *  `vibe_on` 0 is off, 1 on; `vibe_level` 0 light, 1 medium, 2 strong. Missing or not one of those: on, light (the
 *  user's default, 11-13). */
ykh_settings_t ykh_settings_read(int stored_on, int stored_level);
/** The level as it is stored: 0..2; anything else is light. */
int ykh_level_clamp(int level);
/** The LEDC duty of a level: 512, 768, 1024 (of YKH_DUTY_FULL). */
int ykh_level_duty(int level);
/** The built-in patterns by yk_haptic_kind_t (1 tick .. 5 notify): writes the segments, returns their count, 0 for a
 *  kind it does not know. */
int ykh_builtin(int kind, uint16_t seg[YKH_SEG_MAX]);
/** A pattern's rules: 1..YKH_SEG_MAX segments starting with on, each >= YKH_SEG_MIN_MS, the on ones <= YKH_ON_MAX_MS
 *  in all. */
ykh_check_t ykh_check(const uint16_t *seg, int n);

void ykh_init(ykh_core_t *c, ykh_settings_t set);
/** New settings (the settings ring changed them): off stops what plays; a new level applies from the next play. */
void ykh_set_settings(ykh_core_t *c, ykh_settings_t set, int64_t now_ms, ykh_out_t *out);

/** A play, replacing whatever plays or waits. It starts now while the motor is on (the pulse just goes on), otherwise
 *  YKH_GAP_MS after the motor last went off (and not before a rest after a cut). duty 1..YKH_DUTY_FULL; kick: from rest
 *  each on segment starts at full duty for YKH_KICK_MS (no kick at full duty). `quiet`: YKH_QUIET_* now. `out` is
 *  written whatever the result (what plays goes on when the new one is refused for being bad). */
ykh_result_t ykh_play(ykh_core_t *c, int64_t now_ms, const uint16_t *seg, int n, int duty, bool kick, unsigned quiet,
                      ykh_out_t *out);
/** Time passing (the wake the last out asked for, or any other): segments end and begin, the kick ends, a quiet
 *  condition or the cap stops it. */
void ykh_step(ykh_core_t *c, int64_t now_ms, unsigned quiet, ykh_out_t *out);
/** Off now, the play and anything waiting gone. */
void ykh_stop(ykh_core_t *c, int64_t now_ms, ykh_out_t *out);
/** Off now and nothing plays from here on (esp_restart is on its way). */
void ykh_shut(ykh_core_t *c, int64_t now_ms, ykh_out_t *out);

/** A play is running or waiting to start. */
bool ykh_busy(const ykh_core_t *c);
