// Host tests for the vibration's scheduling. Run: bash firmware/components/yaykit_haptic/test/run.sh
// A driver here does what yaykit_haptic.c does on the device: it steps the core at each wake it asks for and writes down
// every change of the output as "<ms>:<duty>", so a whole play reads as one line ("0:1024 25:512 40:0").
#include <stdio.h>
#include <string.h>

#include "../yk_haptic_core.h"

static int failures, checks;
#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                  \
    } while (0)
#define CHECK_LINE(want)                                                   \
    do {                                                                   \
        checks++;                                                          \
        if (strcmp(line, (want))) {                                        \
            failures++;                                                    \
            printf("FAIL %s:%d\n  want \"%s\"\n  got  \"%s\"\n", __FILE__, __LINE__, (want), line); \
        }                                                                  \
    } while (0)

enum { TICK = 1, DOUBLE, SHORT, LONG, NOTIFY };

static ykh_core_t c;
static ykh_out_t o;
static char line[1024];
static int last;              // the output as last written down
static unsigned quiet;        // what the device side would find now
static int64_t poll_gap;      // the longest time between two wakes while the motor was on
static int64_t prev;          // the last time the core was called

static void note(int64_t t) {
    if (o.duty == last) return;
    char one[32];
    snprintf(one, sizeof(one), "%s%lld:%d", line[0] ? " " : "", (long long)t, o.duty);
    strncat(line, one, sizeof(line) - strlen(line) - 1);
    last = o.duty;
}

static void fresh(bool on, int level) {
    ykh_init(&c, ykh_settings_read(on ? 1 : 0, level));
    memset(&o, 0, sizeof(o));
    o.wake_ms = -1;
    line[0] = 0;
    last = 0;
    quiet = 0;
    poll_gap = 0;
    prev = 0;
}

/** Every wake the core asks for up to t, as the esp_timer would give them. */
static void until(int64_t t) {
    for (int guard = 0; o.wake_ms >= 0 && o.wake_ms <= t; guard++) {
        if (guard > 10000) {
            printf("FAIL the core keeps waking at %lld\n", (long long)o.wake_ms);
            failures++;
            return;
        }
        const int64_t at = o.wake_ms;
        const bool was_on = o.duty > 0;
        ykh_step(&c, at, quiet, &o);
        if (was_on && at - prev > poll_gap) poll_gap = at - prev;
        prev = at;
        note(at);
    }
}

/** A built-in kind at the settings' level, as yk_haptic_play() does it. */
static ykh_result_t play_at(int64_t t, int kind) {
    until(t);
    uint16_t seg[YKH_SEG_MAX];
    const int n = ykh_builtin(kind, seg);
    const ykh_result_t r = ykh_play(&c, t, seg, n, ykh_level_duty(c.set.level), true, quiet, &o);
    prev = t;
    note(t);
    return r;
}

static ykh_result_t pattern_at(int64_t t, const uint16_t *seg, int n) {
    until(t);
    const ykh_result_t r = ykh_play(&c, t, seg, n, ykh_level_duty(c.set.level), true, quiet, &o);
    prev = t;
    note(t);
    return r;
}

static void stop_at(int64_t t) {
    until(t);
    ykh_stop(&c, t, &o);
    note(t);
}

// ---------------------------------------------------------------- the five kinds, at each strength

static void test_kinds(void) {
    fresh(true, YKH_LEVEL_STRONG);
    CHECK(play_at(0, TICK) == YKH_PLAYED);
    until(10000);
    CHECK_LINE("0:1024 40:0");
    CHECK(!ykh_busy(&c));

    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, DOUBLE);
    until(10000);
    CHECK_LINE("0:1024 40:0 200:1024 240:0");

    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, SHORT);
    until(10000);
    CHECK_LINE("0:1024 120:0");

    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, LONG);
    until(10000);
    CHECK_LINE("0:1024 500:0");

    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, NOTIFY);
    until(10000);
    CHECK_LINE("0:1024 200:0 450:1024 650:0 900:1024 1100:0");

    // light and medium: 100 % for the first 25 ms of each pulse (from still), then the level's duty
    fresh(true, YKH_LEVEL_LIGHT);
    play_at(0, TICK);
    until(10000);
    CHECK_LINE("0:1024 25:512 40:0");

    fresh(true, YKH_LEVEL_MEDIUM);
    play_at(0, SHORT);
    until(10000);
    CHECK_LINE("0:1024 25:768 120:0");

    fresh(true, YKH_LEVEL_LIGHT);
    play_at(0, DOUBLE);
    until(10000);
    CHECK_LINE("0:1024 25:512 40:0 200:1024 225:512 240:0");

    fresh(true, YKH_LEVEL_LIGHT);
    play_at(0, NOTIFY);
    until(10000);
    CHECK_LINE("0:1024 25:512 200:0 450:1024 475:512 650:0 900:1024 925:512 1100:0");

    fresh(true, YKH_LEVEL_MEDIUM);
    play_at(0, LONG);
    until(10000);
    CHECK_LINE("0:1024 25:768 500:0");

    // not a kind
    fresh(true, YKH_LEVEL_LIGHT);
    uint16_t seg[YKH_SEG_MAX];
    CHECK(ykh_builtin(0, seg) == 0);
    CHECK(ykh_builtin(6, seg) == 0);
    CHECK(ykh_play(&c, 0, seg, ykh_builtin(6, seg), 512, true, 0, &o) == YKH_BAD);
    CHECK(o.duty == 0 && o.wake_ms == -1);
}

// ---------------------------------------------------------------- strength

static void test_levels(void) {
    CHECK(ykh_level_duty(YKH_LEVEL_LIGHT) == 512);
    CHECK(ykh_level_duty(YKH_LEVEL_MEDIUM) == 768);
    CHECK(ykh_level_duty(YKH_LEVEL_STRONG) == 1024);
    CHECK(ykh_level_duty(-1) == 512);
    CHECK(ykh_level_duty(3) == 512);
    CHECK(ykh_level_clamp(2) == 2);
    CHECK(ykh_level_clamp(7) == YKH_LEVEL_LIGHT);

    // a pulse shorter than the kick is all at full duty
    fresh(true, YKH_LEVEL_LIGHT);
    const uint16_t twenty[] = {20};
    CHECK(pattern_at(0, twenty, 1) == YKH_PLAYED);
    until(1000);
    CHECK_LINE("0:1024 20:0");

    // the console's duty: no kick, exactly what was asked
    fresh(true, YKH_LEVEL_LIGHT);
    const uint16_t ms[] = {300};
    CHECK(ykh_play(&c, 0, ms, 1, 300, false, 0, &o) == YKH_PLAYED);
    note(0);
    until(1000);
    CHECK_LINE("0:300 300:0");
    CHECK(ykh_play(&c, 2000, ms, 1, 0, false, 0, &o) == YKH_BAD);
    CHECK(ykh_play(&c, 2000, ms, 1, 1025, false, 0, &o) == YKH_BAD);

    // a new level applies from the next play; what plays keeps its own
    fresh(true, YKH_LEVEL_LIGHT);
    play_at(0, LONG);
    until(100);
    ykh_set_settings(&c, (ykh_settings_t){.on = true, .level = YKH_LEVEL_STRONG}, 100, &o);
    note(100);
    until(1000);
    play_at(1000, SHORT);
    until(2000);
    CHECK_LINE("0:1024 25:512 500:0 1000:1024 1120:0");
}

// ---------------------------------------------------------------- an app's own pattern

static void test_custom(void) {
    uint16_t seg[YKH_SEG_MAX + 1];
    for (int i = 0; i <= YKH_SEG_MAX; i++) seg[i] = 50;
    CHECK(ykh_check(seg, YKH_SEG_MAX) == YKH_OK);
    CHECK(ykh_check(seg, YKH_SEG_MAX + 1) == YKH_BAD_COUNT);
    CHECK(ykh_check(seg, 0) == YKH_BAD_COUNT);
    CHECK(ykh_check(NULL, 1) == YKH_BAD_COUNT);
    seg[3] = 19;
    CHECK(ykh_check(seg, YKH_SEG_MAX) == YKH_BAD_SEGMENT);
    seg[3] = 20;
    CHECK(ykh_check(seg, YKH_SEG_MAX) == YKH_OK);
    // on in all at most 2 s; the offs do not count
    const uint16_t two_s[] = {1000, 5000, 1000};
    CHECK(ykh_check(two_s, 3) == YKH_OK);
    const uint16_t over[] = {1000, 20, 1001};
    CHECK(ykh_check(over, 3) == YKH_TOO_LONG);
    const uint16_t one_over[] = {2001};
    CHECK(ykh_check(one_over, 1) == YKH_TOO_LONG);

    // a bad pattern changes nothing: what plays goes on
    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, LONG);
    CHECK(pattern_at(100, over, 3) == YKH_BAD);
    CHECK(ykh_busy(&c));
    until(2000);
    CHECK_LINE("0:1024 500:0");

    // sixteen segments played as given, a trailing off keeping it busy
    fresh(true, YKH_LEVEL_STRONG);
    uint16_t sixteen[YKH_SEG_MAX];
    for (int i = 0; i < YKH_SEG_MAX; i++) sixteen[i] = i % 2 ? 200 : 30;
    CHECK(pattern_at(0, sixteen, YKH_SEG_MAX) == YKH_PLAYED);
    until(1839);
    CHECK(ykh_busy(&c));
    until(5000);
    CHECK(!ykh_busy(&c));
    CHECK_LINE("0:1024 30:0 230:1024 260:0 460:1024 490:0 690:1024 720:0 920:1024 950:0 1150:1024 1180:0 "
               "1380:1024 1410:0 1610:1024 1640:0");

    // the whole 2 s it may have
    fresh(true, YKH_LEVEL_STRONG);
    const uint16_t whole[] = {2000};
    CHECK(pattern_at(0, whole, 1) == YKH_PLAYED);
    until(5000);
    CHECK_LINE("0:1024 2000:0");
}

// ---------------------------------------------------------------- two pulses: 150 ms apart at least

static void test_gap(void) {
    // a tick 60 ms after the last one went off waits until 150 ms after it
    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, TICK);
    CHECK(play_at(100, TICK) == YKH_PLAYED);
    CHECK(ykh_busy(&c) && o.duty == 0 && o.wake_ms == 190);
    until(1000);
    CHECK_LINE("0:1024 40:0 190:1024 230:0");

    // past the gap: at once
    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, TICK);
    play_at(300, TICK);
    until(1000);
    CHECK_LINE("0:1024 40:0 300:1024 340:0");

    // ticks faster than the gap (a dial turned quickly): the latest waits, the ones between are dropped; one that
    // comes while a pulse runs makes it longer (200 and 400 here)
    fresh(true, YKH_LEVEL_STRONG);
    for (int t = 0; t <= 400; t += 50) play_at(t, TICK);
    until(2000);
    CHECK_LINE("0:1024 40:0 190:1024 240:0 390:1024 440:0");

    // a new play waiting for the gap replaces the one that waited
    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, TICK);
    play_at(100, TICK);
    play_at(150, DOUBLE);
    until(2000);
    CHECK_LINE("0:1024 40:0 190:1024 230:0 390:1024 430:0");

    // a play in another's off segment: the gap counts from that pulse
    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, NOTIFY);
    play_at(250, SHORT);  // 50 ms after the first pulse went off
    until(2000);
    CHECK_LINE("0:1024 200:0 350:1024 470:0");

    // while the motor runs a new play goes on from there: no gap, no new kick
    fresh(true, YKH_LEVEL_LIGHT);
    play_at(0, LONG);
    play_at(100, TICK);
    until(2000);
    CHECK_LINE("0:1024 25:512 140:0");

    // ... and one that comes during the kick keeps the kick to its end
    fresh(true, YKH_LEVEL_LIGHT);
    play_at(0, TICK);
    play_at(10, TICK);
    until(2000);
    CHECK_LINE("0:1024 25:512 50:0");

    // the gap counts across a stop too
    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, LONG);
    stop_at(100);
    play_at(200, TICK);
    until(2000);
    CHECK_LINE("0:1024 100:0 250:1024 290:0");
}

// ---------------------------------------------------------------- 2 s on at most, in one go

static void test_cap(void) {
    // plays that replace plays while the motor runs (an app calling LONG every 400 ms): cut at 2 s, then 1 s off
    fresh(true, YKH_LEVEL_STRONG);
    for (int t = 0; t < 2000; t += 400) play_at(t, LONG);
    until(2000);
    CHECK(o.duty == 0 && !ykh_busy(&c));
    CHECK(play_at(2100, TICK) == YKH_PLAYED);
    CHECK(o.duty == 0 && o.wake_ms == 3000);
    until(5000);
    CHECK_LINE("0:1024 2000:0 3000:1024 3040:0");

    // a wake that comes late (the esp_timer task held up) still cuts as soon as it comes, and the core asks for the
    // cap's moment itself
    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, LONG);
    play_at(400, LONG);
    play_at(800, LONG);
    play_at(1200, LONG);
    play_at(1600, LONG);
    CHECK(o.wake_ms <= 2000);
    ykh_step(&c, 2600, 0, &o);
    CHECK(o.duty == 0 && !ykh_busy(&c));

    // while on, the core wakes at least every YKH_POLL_MS (to see the microphones open)
    fresh(true, YKH_LEVEL_LIGHT);
    play_at(0, LONG);
    until(2000);
    CHECK(poll_gap > 0 && poll_gap <= YKH_POLL_MS);
}

// ---------------------------------------------------------------- stop, leaving, restarting

static void test_stop(void) {
    // stop in a pulse: off now, nothing waits
    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, NOTIFY);
    stop_at(100);
    CHECK(!ykh_busy(&c) && o.duty == 0 && o.wake_ms == -1);
    until(5000);
    CHECK_LINE("0:1024 100:0");

    // stop between pulses
    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, NOTIFY);
    stop_at(300);
    until(5000);
    CHECK_LINE("0:1024 200:0");
    CHECK(!ykh_busy(&c));

    // stop drops one that waits for the gap
    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, TICK);
    play_at(100, TICK);
    stop_at(120);
    until(5000);
    CHECK_LINE("0:1024 40:0");

    // leaving (yaykit_app's way out calls stop), and esp_restart (shut): nothing plays after it
    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, LONG);
    until(50);
    ykh_shut(&c, 50, &o);
    note(50);
    CHECK(o.duty == 0 && o.wake_ms == -1);
    CHECK(play_at(1000, TICK) == YKH_SHUT);
    until(5000);
    CHECK_LINE("0:1024 50:0");
}

// ---------------------------------------------------------------- the settings

static void test_settings(void) {
    // a Yay from before vibration wrote neither key: on, light
    ykh_settings_t s = ykh_settings_read(-1, -1);
    CHECK(s.on && s.level == YKH_LEVEL_LIGHT);
    s = ykh_settings_read(1, -1);
    CHECK(s.on && s.level == YKH_LEVEL_LIGHT);
    s = ykh_settings_read(-1, 2);
    CHECK(s.on && s.level == YKH_LEVEL_STRONG);
    s = ykh_settings_read(0, 1);
    CHECK(!s.on && s.level == YKH_LEVEL_MEDIUM);
    s = ykh_settings_read(1, 1);
    CHECK(s.on && s.level == YKH_LEVEL_MEDIUM);
    // a value this build does not know (a newer Yay's): on, light
    s = ykh_settings_read(7, 9);
    CHECK(s.on && s.level == YKH_LEVEL_LIGHT);

    // off: nothing plays
    fresh(false, YKH_LEVEL_LIGHT);
    CHECK(play_at(0, TICK) == YKH_OFF);
    CHECK(o.duty == 0 && o.wake_ms == -1 && !ykh_busy(&c));

    // turned off while it plays: stops at once; on again, plays again
    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, LONG);
    until(100);
    ykh_set_settings(&c, (ykh_settings_t){.on = false, .level = YKH_LEVEL_STRONG}, 100, &o);
    note(100);
    CHECK(!ykh_busy(&c));
    CHECK(play_at(500, TICK) == YKH_OFF);
    ykh_set_settings(&c, (ykh_settings_t){.on = true, .level = 99}, 600, &o);
    CHECK(c.set.level == YKH_LEVEL_LIGHT);
    CHECK(play_at(700, TICK) == YKH_PLAYED);
    until(5000);
    CHECK_LINE("0:1024 100:0 700:1024 725:512 740:0");
}

// ---------------------------------------------------------------- the microphones, an install, the battery

static void test_quiet(void) {
    // the microphones open: refused, nothing changes on the pin
    fresh(true, YKH_LEVEL_STRONG);
    quiet = YKH_QUIET_MIC;
    CHECK(play_at(0, TICK) == YKH_QUIET);
    CHECK(o.duty == 0 && !ykh_busy(&c));

    // they open while it plays: off at the next look, within YKH_POLL_MS
    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, LONG);
    until(100);
    quiet = YKH_QUIET_MIC;  // open from 101
    until(2000);
    CHECK(!ykh_busy(&c));
    CHECK_LINE("0:1024 120:0");  // off at 120: the next look after 100

    // they open while it is between pulses: the next pulse never comes
    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, NOTIFY);
    until(300);
    quiet = YKH_QUIET_MIC;
    until(5000);
    CHECK_LINE("0:1024 200:0");
    CHECK(!ykh_busy(&c));

    // a play waiting for the gap when they open is dropped
    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, TICK);
    play_at(100, TICK);
    quiet = YKH_QUIET_MIC;
    until(5000);
    CHECK_LINE("0:1024 40:0");

    // closed again: plays again
    quiet = 0;
    CHECK(play_at(6000, TICK) == YKH_PLAYED);
    until(7000);
    CHECK_LINE("0:1024 40:0 6000:1024 6040:0");

    // an install writing flash, the battery guard: the same
    fresh(true, YKH_LEVEL_STRONG);
    quiet = YKH_QUIET_INSTALL;
    CHECK(play_at(0, TICK) == YKH_QUIET);
    quiet = YKH_QUIET_BATTERY;
    CHECK(play_at(10, TICK) == YKH_QUIET);
    quiet = 0;
    play_at(20, LONG);
    until(60);
    quiet = YKH_QUIET_INSTALL;
    until(2000);
    CHECK_LINE("20:1024 80:0");
    CHECK(!ykh_busy(&c) && o.duty == 0);

    // a refused play stops one that still ran (the device side found them open before its next look)
    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, LONG);
    until(30);
    quiet = YKH_QUIET_MIC;
    CHECK(play_at(35, TICK) == YKH_QUIET);
    CHECK(o.duty == 0 && !ykh_busy(&c));
    CHECK_LINE("0:1024 35:0");
}

// ---------------------------------------------------------------- a late wake

static void test_late(void) {
    // each segment gets its whole length from when it really began: a pulse is never cut short by a late wake
    fresh(true, YKH_LEVEL_STRONG);
    play_at(0, DOUBLE);
    ykh_step(&c, 70, 0, &o);  // the end of the first pulse (40) seen 30 ms late
    note(70);
    until(2000);
    CHECK_LINE("0:1024 70:0 230:1024 270:0");
}

int main(void) {
    test_kinds();
    test_levels();
    test_custom();
    test_gap();
    test_cap();
    test_stop();
    test_settings();
    test_quiet();
    test_late();
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
