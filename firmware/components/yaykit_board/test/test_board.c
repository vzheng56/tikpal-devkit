// Host tests for the pure C parts of yaykit_board. Run: bash firmware/components/yaykit_board/test/run.sh
#include <stdio.h>
#include <string.h>

#include "../rtc_regs.h"
#include "../yk_board_logic.h"
#include "../yk_font_pack.h"
#include "board_speaker.h"

static int failures, checks;
#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                  \
    } while (0)

static void test_round_area(void) {
    int32_t x1 = 7, y1 = 10, x2 = 100, y2 = 465;
    yk_board_round_area(&x1, &y1, &x2, &y2);
    CHECK(x1 == 6 && y1 == 10);    // starts move down to even
    CHECK(x2 == 101 && y2 == 465); // ends move up to odd
    // An area that is already aligned stays as it is, and the full screen stays inside 466 px.
    x1 = 0, y1 = 0, x2 = 465, y2 = 465;
    yk_board_round_area(&x1, &y1, &x2, &y2);
    CHECK(x1 == 0 && y1 == 0 && x2 == 465 && y2 == 465);
    // A one-pixel area grows to the 2x2 block around it.
    x1 = x2 = 233, y1 = y2 = 120;
    yk_board_round_area(&x1, &y1, &x2, &y2);
    CHECK(x1 == 232 && x2 == 233 && y1 == 120 && y2 == 121);
}

static void test_touch_point(void) {
    int32_t x = -1, y = -1;
    // No finger: released, point untouched (LVGL keeps the last one).
    const uint8_t none[6] = {0, 0x00, 0x01, 0x10, 0x00, 0x20};
    CHECK(!yk_board_touch_point(none, 466, 466, &x, &y));
    CHECK(x == -1 && y == -1);
    // One finger at (272, 32); the high nibble of the finger byte and of the coordinate high bytes is not data.
    const uint8_t one[6] = {0, 0xF1, 0xF1, 0x10, 0x80, 0x20};
    CHECK(yk_board_touch_point(one, 466, 466, &x, &y));
    CHECK(x == 272 && y == 32);
    // Reports past the edge are clamped to the last pixel.
    const uint8_t edge[6] = {0, 0x01, 0x01, 0xD2, 0x0F, 0xFF};
    CHECK(yk_board_touch_point(edge, 466, 466, &x, &y));
    CHECK(x == 465 && y == 465);
}

static void test_audio_rates(void) {
    CHECK(yk_board_audio_rate_ok(16000));
    CHECK(yk_board_audio_rate_ok(24000));
    CHECK(!yk_board_audio_rate_ok(0));
    CHECK(!yk_board_audio_rate_ok(22050));
    CHECK(!yk_board_audio_rate_ok(48000));
}

static void test_rtc_regs(void) {
    // 2026-10-18 12:34:56 UTC, a Sunday: BCD seconds .. years, weekday 0 = Sunday.
    uint8_t r[RTC_TIME_REGS];
    rtc_regs_from_unix(1792326896, r);
    const uint8_t want[RTC_TIME_REGS] = {0x56, 0x34, 0x12, 0x18, 0, 0x10, 0x26};
    int same = 1;
    for (int i = 0; i < RTC_TIME_REGS; i++) same &= r[i] == want[i];
    CHECK(same);
    int64_t t = 0;
    CHECK(rtc_regs_to_unix(r, &t) && t == 1792326896);
    // Round trips: the first trusted second, a leap day, the last two-digit year.
    const int64_t times[] = {1767225600, 1835481599, 4102444799};
    for (int k = 0; k < 3; k++) {
        rtc_regs_from_unix(times[k], r);
        CHECK(rtc_regs_to_unix(r, &t) && t == times[k]);
    }
    rtc_regs_from_unix(1835481599, r);
    CHECK(r[3] == 0x29 && r[5] == 0x02 && r[4] == 2); // 2028-02-29, a Tuesday
    // Not trusted: oscillator stopped, before 2026, impossible or non-BCD values.
    rtc_regs_from_unix(1792326896, r);
    r[0] |= RTC_OS;
    CHECK(!rtc_regs_to_unix(r, &t));
    rtc_regs_from_unix(1767225599, r);
    CHECK(!rtc_regs_to_unix(r, &t));
    rtc_regs_from_unix(1792326896, r);
    r[5] = 0x13;
    CHECK(!rtc_regs_to_unix(r, &t));
    rtc_regs_from_unix(1792326896, r);
    r[1] = 0x1A;
    CHECK(!rtc_regs_to_unix(r, &t));
}

static void test_battery(void) {
    // The factory firmware's own log: raw 3344 is 4012.8 mV (0x14 = 0x10, 0x15 = 0x0D; high nibble of 0x15 ignored).
    CHECK(yk_board_battery_mv(0x10, 0x0D) == 4012);
    CHECK(yk_board_battery_mv(0x10, 0xFD) == 4012);
    CHECK(yk_board_battery_mv(0x00, 0x00) == 0 && yk_board_battery_mv(0xFF, 0x0F) == 4914);
    CHECK(yk_board_battery_percent(3000) == 0 && yk_board_battery_percent(3300) == 0);
    CHECK(yk_board_battery_percent(4200) == 100 && yk_board_battery_percent(4400) == 100);
    CHECK(yk_board_battery_percent(3800) == 50 && yk_board_battery_percent(4012) == 81);
    // Unplugged, the registers read 0: not a battery at 0 %.
    CHECK(!yk_board_battery_known(yk_board_battery_mv(0x00, 0x00)) && !yk_board_battery_known(1200));
    CHECK(yk_board_battery_known(yk_board_battery_mv(0x6C, 0x5E)) && yk_board_battery_known(3300));
    CHECK(!yk_board_battery_known(yk_board_battery_mv(0xFF, 0x0F)));  // 4.9 V: not a cell
    for (int mv = 3300, last = 0; mv <= 4200; mv += 5) {  // never goes down as the voltage goes up
        const int p = yk_board_battery_percent(mv);
        CHECK(p >= last && p >= 0 && p <= 100);
        last = p;
    }
}

/** A pack's index as gen_font_pack.py writes it. */
static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}
static void make_index(uint8_t tail[YK_FONT_PACK_TAIL], int count, const char *const langs[],
                       const uint32_t offsets[], const uint32_t sizes[]) {
    memset(tail, 0, YK_FONT_PACK_TAIL);
    memcpy(tail, "YKFP", 4);
    tail[4] = 1;
    tail[5] = (uint8_t)count;
    for (int i = 0; i < count; i++) {
        strncpy((char *)tail + 8 + 12 * i, langs[i], 4);
        put32(tail + 12 + 12 * i, offsets[i]);
        put32(tail + 16 + 12 * i, sizes[i]);
    }
    put32(tail + 56, yk_crc32(tail, 56));
}

static void test_battery_adc_and_prefs(void) {
    // V_BAT / 2 on the pin; 1.9 V at the pin is a 3.8 V cell.
    CHECK(yk_board_vbat_mv(1900) == 3800);
    CHECK(yk_board_vbus_present(2480) && !yk_board_vbus_present(40) && !yk_board_vbus_present(1599));
    // On battery the resting curve; charging takes the rise off and never says 100 % while still charging.
    CHECK(yk_board_battery_level(3800, false) == 50);
    CHECK(yk_board_battery_level(3920, true) == 50);
    CHECK(yk_board_battery_level(4390, true) == 99);
    CHECK(yk_board_battery_level(4200, false) == 100);
    CHECK(yk_board_battery_level(0, false) == -1 && yk_board_battery_level(5000, true) == -1);
    CHECK(yk_board_battery_full(4180) && !yk_board_battery_full(4000));
    // Preferences: defaults, ranges, bad stored values read as the default.
    CHECK(yk_board_pref_default(YK_PREF_VOLUME) == 6 && yk_board_pref_default(YK_PREF_BRIGHTNESS) == 80 &&
          yk_board_pref_default(YK_PREF_SCREEN_OFF) == 30);
    CHECK(yk_board_pref_clamp(YK_PREF_VOLUME, 14) == 10 && yk_board_pref_clamp(YK_PREF_VOLUME, -2) == 0);
    CHECK(yk_board_pref_clamp(YK_PREF_BRIGHTNESS, 0) == 5 && yk_board_pref_clamp(YK_PREF_BRIGHTNESS, 101) == 100);
    CHECK(yk_board_pref_clamp(YK_PREF_SCREEN_OFF, 0) == 0 && yk_board_pref_clamp(YK_PREF_SCREEN_OFF, 3) == 15 &&
          yk_board_pref_clamp(YK_PREF_SCREEN_OFF, 99999) == 3600);
    CHECK(yk_board_pref_read(YK_PREF_VOLUME, -1) == 6 && yk_board_pref_read(YK_PREF_VOLUME, 3) == 3 &&
          yk_board_pref_read(YK_PREF_VOLUME, 200) == 6);
    CHECK(yk_board_pref_read(YK_PREF_BRIGHTNESS, 2) == 80 && yk_board_pref_read(YK_PREF_SCREEN_OFF, 0) == 0);
    // The time zone (0.1.14): the POSIX strings Yay's table gives; nothing else is one.
    CHECK(yk_board_tz_ok("CST-8") && yk_board_tz_ok("EST5EDT,M3.2.0,M11.1.0") && yk_board_tz_ok("<+0530>-5:30"));
    CHECK(!yk_board_tz_ok(NULL) && !yk_board_tz_ok("") && !yk_board_tz_ok("8CST") && !yk_board_tz_ok("CST -8") &&
          !yk_board_tz_ok("CST-8\n") && !yk_board_tz_ok("CST-8\xe2\x80\x8b"));
    char tz_long[YK_PREF_TZ_MAX + 1];
    memset(tz_long, 'A', sizeof(tz_long) - 1);
    tz_long[YK_PREF_TZ_MAX] = 0;
    CHECK(!yk_board_tz_ok(tz_long));
    tz_long[YK_PREF_TZ_MAX - 1] = 0;
    CHECK(yk_board_tz_ok(tz_long));  // 63 characters: the most
    CHECK(yk_board_tz_offset_ok(480) && yk_board_tz_offset_ok(-720) && yk_board_tz_offset_ok(840) &&
          yk_board_tz_offset_ok(345) && yk_board_tz_offset_ok(0));  // Shanghai, the ends, Kathmandu, UTC
    CHECK(!yk_board_tz_offset_ok(-721) && !yk_board_tz_offset_ok(841) && !yk_board_tz_offset_ok(-2147483647));
    // The buddy (0.1.12): added after the others, which keep their numbers (NVS keys are looked up by them).
    CHECK(YK_PREF_VOLUME == 0 && YK_PREF_BRIGHTNESS == 1 && YK_PREF_SCREEN_OFF == 2 && YK_PREF_BUDDY == 3);
    // Never written (no main firmware does yet): Pip. 0 Pip, 3 Mote, 4 Dial, 5 Bloom stand.
    CHECK(yk_board_pref_default(YK_PREF_BUDDY) == 0 && yk_board_pref_read(YK_PREF_BUDDY, -1) == 0);
    CHECK(yk_board_pref_read(YK_PREF_BUDDY, 0) == 0 && yk_board_pref_read(YK_PREF_BUDDY, 3) == 3 &&
          yk_board_pref_read(YK_PREF_BUDDY, 4) == 4 && yk_board_pref_read(YK_PREF_BUDDY, 5) == 5);
    // 1 and 2 (Wow and Aha, retired) and a buddy this build does not know (a later main firmware's, or a bad byte) are
    // Pip, not a neighbour.
    CHECK(yk_board_pref_read(YK_PREF_BUDDY, 1) == 0 && yk_board_pref_read(YK_PREF_BUDDY, 2) == 0 &&
          yk_board_pref_read(YK_PREF_BUDDY, 6) == 0 && yk_board_pref_read(YK_PREF_BUDDY, 254) == 0 &&
          yk_board_pref_read(YK_PREF_BUDDY, 255) == 0);
    CHECK(yk_board_pref_clamp(YK_PREF_BUDDY, 3) == 3 && yk_board_pref_clamp(YK_PREF_BUDDY, 5) == 5 &&
          yk_board_pref_clamp(YK_PREF_BUDDY, 1) == 0 && yk_board_pref_clamp(YK_PREF_BUDDY, 7) == 0 &&
          yk_board_pref_clamp(YK_PREF_BUDDY, -1) == 0);
}

static void test_font_pack(void) {
    CHECK(yk_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);  // the standard check value
    const uint32_t part = 0x300000;
    uint8_t tail[YK_FONT_PACK_TAIL];
    const char *const langs[] = {"zh", "ja"};
    const uint32_t offs[] = {0, 1300000}, sizes[] = {1290000, 1100000};
    make_index(tail, 2, langs, offs, sizes);
    yk_font_pack_t p;
    CHECK(yk_font_pack_parse(tail, part, &p) && p.count == 2);
    CHECK(!strcmp(p.fonts[0].lang, "zh") && p.fonts[0].offset == 0 && p.fonts[0].size == 1290000);
    CHECK(!strcmp(p.fonts[1].lang, "ja") && p.fonts[1].offset == 1300000 && p.fonts[1].size == 1100000);
    // Which font first: the language's own, the others after it as fallbacks.
    int order[YK_FONT_PACK_MAX];
    CHECK(yk_font_pack_order(&p, "ja", order) == 2 && order[0] == 1 && order[1] == 0);
    CHECK(yk_font_pack_order(&p, "ja-JP", order) == 2 && order[0] == 1);
    CHECK(yk_font_pack_order(&p, "JA", order) == 2 && order[0] == 1);
    CHECK(yk_font_pack_order(&p, "zh-CN", order) == 2 && order[0] == 0 && order[1] == 1);
    CHECK(yk_font_pack_order(&p, "en", order) == 2 && order[0] == 0 && order[1] == 1);  // Latin is in both
    CHECK(yk_font_pack_order(&p, "es-419", order) == 2 && order[0] == 0);
    CHECK(yk_font_pack_order(&p, NULL, order) == 2 && order[0] == 0);
    CHECK(yk_font_pack_order(&p, "jav", order) == 2 && order[0] == 0);  // not "ja"
    // What an old partition (one font, the rest erased) or a damaged index reads as: no pack.
    uint8_t erased[YK_FONT_PACK_TAIL];
    memset(erased, 0xFF, sizeof(erased));
    CHECK(!yk_font_pack_parse(erased, part, &p) && p.count == 0);
    uint8_t bad[YK_FONT_PACK_TAIL];
    memcpy(bad, tail, sizeof(bad));
    bad[20] ^= 1;  // a changed offset: the CRC no longer matches
    CHECK(!yk_font_pack_parse(bad, part, &p));
    const uint32_t over[] = {0, part - YK_FONT_PACK_TAIL - 100};  // the second font would run into the index
    make_index(tail, 2, langs, over, sizes);
    CHECK(!yk_font_pack_parse(tail, part, &p));
    const uint32_t far[] = {0, 0xFFFFFFF0u};  // an offset past the end: no wrap-around
    make_index(tail, 2, langs, far, sizes);
    CHECK(!yk_font_pack_parse(tail, part, &p));
    make_index(tail, 2, langs, offs, sizes);
    tail[5] = 5;  // more fonts than an index holds
    CHECK(!yk_font_pack_parse(tail, part, &p));
    const char *const unnamed[] = {"", "ja"};
    make_index(tail, 2, unnamed, offs, sizes);
    CHECK(!yk_font_pack_parse(tail, part, &p));
    // One font is a pack too.
    make_index(tail, 1, langs, offs, sizes);
    CHECK(yk_font_pack_parse(tail, part, &p) && p.count == 1);
    CHECK(yk_font_pack_order(&p, "ja", order) == 1 && order[0] == 0);
}

// `n` readings `gap_ms` apart, the first `gap_ms` after `t`, the same voltage, USB and 0x11 each time; returns the
// time of the last.
static int64_t feed_n(yk_board_batt_t *b, int64_t t, int gap_ms, int n, int mv, bool usb, int sys) {
    for (int i = 0; i < n; i++) yk_board_batt_feed(b, t += gap_ms, mv, usb, sys);
    return t;
}

static void test_battery_display(void) {
    // H1: the registers, from the two snapshots (docs/delivery/2026-10-31-battery-display.md 3). Old Tikpal, on USB:
    // 0x11 = 0x12 (port B, charging), 0x14/0x15 = DE 5C (3952 mV), 0x17/0x18 = A0 00 (571 mA).
    CHECK((0x12 & YK_SW6106_SYS_CHARGING) && !(0x00 & YK_SW6106_SYS_CHARGING));
    CHECK(yk_board_battery_mv(0xDE, 0x5C) == 3952);
    CHECK(yk_board_charge_ma(0xA0, 0x00) == 571 && yk_board_charge_ma(0x00, 0x00) == 0);
    CHECK(yk_board_charge_ma(0x00, 0xF1) == 914);  // the top nibble is the discharge current's
    CHECK(yk_board_full_mv(0x7C) == 4400 && yk_board_end_ma(0x7C) == 300);  // the second test device
    CHECK(yk_board_full_mv(0xFC) == 4400 && yk_board_end_ma(0xFC) == 200);  // old Tikpal
    CHECK(yk_board_full_mv(0x00) == 4200 && yk_board_full_mv(0x04) == 4300 && yk_board_full_mv(0x08) == 4350);
    CHECK(strcmp(yk_board_batt_state_name(YK_BOARD_BATT_DISCHARGING), "discharging") == 0 &&
          strcmp(yk_board_batt_state_name(YK_BOARD_BATT_CHARGING), "charging") == 0 &&
          strcmp(yk_board_batt_state_name(YK_BOARD_BATT_FULL), "full") == 0 &&
          strcmp(yk_board_batt_state_name(YK_BOARD_BATT_STOPPED), "stopped") == 0 &&
          strcmp(yk_board_batt_state_name(YK_BOARD_BATT_UNKNOWN), "unknown") == 0);
    CHECK(yk_board_batt_fault_name(YK_BOARD_BATT_FAULT_NONE) == NULL &&
          strcmp(yk_board_batt_fault_name(YK_BOARD_BATT_FAULT_NO_RISE), "no_rise") == 0);

    // H2: states. Plugged in, the chip charging.
    yk_board_batt_t b = {0};
    int64_t t = feed_n(&b, 0, 30000, 1, 3800, true, 0x12);
    CHECK(b.state == YK_BOARD_BATT_CHARGING && b.charging && !b.low && b.fault == YK_BOARD_BATT_FAULT_NONE);
    // The charger off right after plugging in: still charging for the first minute, then stopped after a minute off.
    b = (yk_board_batt_t){0};
    t = feed_n(&b, 0, 10000, 6, 3800, true, 0x02);  // 0..50 s
    CHECK(b.state == YK_BOARD_BATT_CHARGING && b.charging);
    t = feed_n(&b, t, 10000, 1, 3800, true, 0x02);  // 60 s: a minute plugged in, the charger off a minute
    CHECK(b.state == YK_BOARD_BATT_STOPPED && !b.charging);
    // Charging, then the charger off below the top: stopped only once it has been off a minute.
    b = (yk_board_batt_t){0};
    t = feed_n(&b, 0, 30000, 4, 3900, true, 0x12);  // 0..90 s charging
    t = feed_n(&b, t, 30000, 2, 3900, true, 0x02);  // off at 120 s, 150 s
    CHECK(b.state == YK_BOARD_BATT_CHARGING);
    t = feed_n(&b, t, 30000, 1, 3900, true, 0x02);  // 180 s: off a minute
    CHECK(b.state == YK_BOARD_BATT_STOPPED);
    t = feed_n(&b, t, 30000, 1, 3900, true, 0x12);  // charging again
    CHECK(b.state == YK_BOARD_BATT_CHARGING);
    // Full: the charger off at the top after the first minute; kept while the chip tops up, until unplugged.
    b = (yk_board_batt_t){0};
    t = feed_n(&b, 0, 30000, 3, 4380, true, 0x12);
    CHECK(b.state == YK_BOARD_BATT_CHARGING && b.percent == 99);
    t = feed_n(&b, t, 30000, 1, 4150, true, 0x02);
    CHECK(b.state == YK_BOARD_BATT_FULL && !b.charging);
    t = feed_n(&b, t, 30000, 4, 4120, true, 0x12);  // topping up from the recharge point
    CHECK(b.state == YK_BOARD_BATT_FULL);
    t = feed_n(&b, t, 30000, 1, 4150, false, -1);
    CHECK(b.state == YK_BOARD_BATT_DISCHARGING);
    t = feed_n(&b, t, 30000, 1, 4150, true, 0x02);  // plugged in again at the top: the first minute is charging
    CHECK(b.state == YK_BOARD_BATT_CHARGING);
    // The chip did not answer: unknown, charging guessed from the voltage, never full.
    b = (yk_board_batt_t){0};
    t = feed_n(&b, 0, 30000, 1, 3900, true, -1);
    CHECK(b.state == YK_BOARD_BATT_UNKNOWN && b.charging);
    t = feed_n(&b, t, 30000, 200, 4300, true, -1);
    CHECK(b.state == YK_BOARD_BATT_UNKNOWN && !b.charging && b.percent == 99);
    // On the battery the chip does not matter.
    b = (yk_board_batt_t){0};
    feed_n(&b, 0, 30000, 1, 3800, false, -1);
    CHECK(b.state == YK_BOARD_BATT_DISCHARGING && !b.charging && b.percent == 50);

    // H3: smoothing. 38 % on the battery, plugged in: no jump, then 1 at most a reading.
    b = (yk_board_batt_t){0};
    t = feed_n(&b, 0, 30000, 4, 3720, false, 0x00);
    const int before = b.percent;
    CHECK(before == 31);
    t = feed_n(&b, t, 30000, 1, 4020, true, 0x12);  // reads 78 % under charge (4020 - 120 = 3900: 70 %)
    CHECK(b.percent == before + 1);
    for (int i = 0; i < 10; i++) {
        const int was = b.percent;
        t = feed_n(&b, t, 30000, 1, 4020, true, 0x12);
        CHECK(b.percent - was <= 1 && b.percent >= was);
    }
    CHECK(b.percent == before + 11);
    // Read often: no faster (readings under 10 s apart do not move it).
    int was = b.percent;
    t = feed_n(&b, t, 2000, 4, 4020, true, 0x12);
    CHECK(b.percent == was);
    t = feed_n(&b, t, 2000, 1, 4020, true, 0x12);  // 10 s after the last move
    CHECK(b.percent == was + 1);
    // Lower readings while plugged in do not take it down.
    was = b.percent;
    t = feed_n(&b, t, 30000, 4, 3700, true, 0x12);  // the average falls to 3700 (9 %)
    CHECK(b.percent >= was);
    was = b.percent;
    t = feed_n(&b, t, 30000, 4, 3700, true, 0x12);
    CHECK(b.percent == was);
    // Unplugged: no jump; down 1 at most a reading towards the resting value; never up on the battery.
    t = feed_n(&b, t, 30000, 1, 3760, false, -1);  // 3760: 42 %
    CHECK(b.percent == was - 1);
    for (int i = 0; i < 40; i++) t = feed_n(&b, t, 30000, 1, 3760, false, -1);
    CHECK(b.percent == 42);
    t = feed_n(&b, t, 30000, 4, 3900, false, -1);  // a higher reading on the battery: stays
    CHECK(b.percent == 42);
    // The average starts again on plugging: the reading just after unplugging is not mixed with the charging ones.
    b = (yk_board_batt_t){0};
    t = feed_n(&b, 0, 30000, 4, 4300, true, 0x12);
    t = feed_n(&b, t, 30000, 1, 3800, false, -1);
    CHECK(b.avg_mv == 3800 && b.count == 1);
    t = feed_n(&b, t, 30000, 1, 3780, false, -1);
    CHECK(b.avg_mv == 3790);
    // The first reading after start is taken as it is (no number before it).
    b = (yk_board_batt_t){0};
    feed_n(&b, 0, 30000, 1, 4020, true, 0x12);
    CHECK(b.percent == 70);

    // H4: 100 only once full; then 1 a reading up to 100; a top-up does not take it back; unknown never 100.
    b = (yk_board_batt_t){0};
    t = feed_n(&b, 0, 30000, 20, 4420, true, 0x12);
    CHECK(b.percent == 99);
    t = feed_n(&b, t, 30000, 1, 4180, true, 0x02);
    CHECK(b.state == YK_BOARD_BATT_FULL && b.percent == 100);
    b = (yk_board_batt_t){0};
    t = feed_n(&b, 0, 30000, 3, 4000, true, 0x12);  // 66 % (4000 - 120)
    CHECK(b.percent == 66);
    t = feed_n(&b, t, 30000, 1, 4110, true, 0x02);  // the chip stops early (300 mA end current): full at once,
    CHECK(b.state == YK_BOARD_BATT_FULL && b.percent == 67);  // not stopped for a reading while the average catches up
    for (int i = 0; i < 40; i++) t = feed_n(&b, t, 30000, 1, i % 5 ? 4110 : 4130, true, i % 7 ? 0x02 : 0x12);
    CHECK(b.state == YK_BOARD_BATT_FULL && b.percent == 100);
    b = (yk_board_batt_t){0};
    feed_n(&b, 0, 30000, 300, 4450, true, -1);
    CHECK(b.percent == 99);
    // Already 100 on the battery and plugged in: it does not go down to 99 for it.
    b = (yk_board_batt_t){0};
    t = feed_n(&b, 0, 30000, 1, 4250, false, -1);
    CHECK(b.percent == 100);
    feed_n(&b, t, 30000, 3, 4380, true, 0x12);
    CHECK(b.percent == 100);

    // H5: low. Charging under 3.3 V: low, no number to show; not 0 % for an hour.
    b = (yk_board_batt_t){0};
    t = feed_n(&b, 0, 30000, 1, 3150, true, 0x12);
    CHECK(b.low && b.state == YK_BOARD_BATT_CHARGING && b.fault == YK_BOARD_BATT_FAULT_NONE);
    t = feed_n(&b, t, 30000, 59, 3200, true, 0x12);  // 30 min - 30 s
    CHECK(b.low && b.fault == YK_BOARD_BATT_FAULT_NONE);
    t = feed_n(&b, t, 30000, 1, 3200, true, 0x12);  // 30 min plugged in, still under 3.3 V
    CHECK(b.fault == YK_BOARD_BATT_FAULT_NO_RISE);
    t = feed_n(&b, t, 30000, 4, 3350, true, 0x12);  // rising after all: told once, kept until unplugged
    CHECK(!b.low && b.fault == YK_BOARD_BATT_FAULT_NO_RISE);
    t = feed_n(&b, t, 30000, 1, 3150, false, -1);  // unplugged: cleared
    CHECK(b.fault == YK_BOARD_BATT_FAULT_NONE && !b.low);
    t = feed_n(&b, t, 30000, 1, 3150, true, 0x02);  // plugged in again: the 30 min start again (the chip stopped)
    CHECK(b.fault == YK_BOARD_BATT_FAULT_NONE);
    t = feed_n(&b, t, 30000, 60, 3150, true, 0x02);
    CHECK(b.fault == YK_BOARD_BATT_FAULT_NO_RISE && b.state == YK_BOARD_BATT_STOPPED && !b.low);
    // The chip not answering: low and the fault all the same.
    b = (yk_board_batt_t){0};
    feed_n(&b, 0, 30000, 61, 3100, true, -1);
    CHECK(b.low && b.fault == YK_BOARD_BATT_FAULT_NO_RISE && b.state == YK_BOARD_BATT_UNKNOWN);

    // H6: the guards' level is this reading straight from the voltage. On the battery it is the old percent for
    // every voltage, so a guard at 5 % (3.52 V) fires on the same reading as before, smoothing or not.
    for (int mv = 3000; mv <= 4400; mv += 7) {
        b = (yk_board_batt_t){0};
        feed_n(&b, 0, 30000, 1, 4000, false, -1);  // the screen at 80 %: the smoothed number lags
        feed_n(&b, 30000, 30000, 1, mv, false, -1);
        CHECK(b.level == yk_board_battery_level(mv, false));
    }
    b = (yk_board_batt_t){0};
    t = feed_n(&b, 0, 30000, 1, 4000, false, -1);  // 80 % on the screen, then the voltage falls fast
    int fired_level = -1, fired_shown = -1, empty_reads = 0;
    for (int i = 0; i < 40 && fired_level < 0; i++) {
        t = feed_n(&b, t, 10000, 1, 3600 - i * 5, false, -1);
        empty_reads = b.level <= 5 ? empty_reads + 1 : 0;  // yay_power: 5 % twice in a row
        if (empty_reads == 2) fired_level = i, fired_shown = b.percent;
    }
    CHECK(fired_level == 18);  // 3515 mV then 3510 mV: the same reading the old percent fired on
    CHECK(fired_shown > 5);    // the screen's smoothed number had not got there yet: the guard does not wait for it
    // Plugged in, the level takes the rise off as the old percent did, but is not capped at 99.
    b = (yk_board_batt_t){0};
    feed_n(&b, 0, 30000, 1, 3920, true, 0x12);
    CHECK(b.level == yk_board_battery_level(3920, true) && b.level == 50);
    b = (yk_board_batt_t){0};
    feed_n(&b, 0, 30000, 1, 4400, true, 0x12);
    CHECK(b.level == 100);
}

// A pretend SW6106 for the 0x3A steps: one register, reads that may fail, a write that may fail or not stick.
typedef struct {
    uint8_t reg3a;
    int reads, writes;
    bool read_fails, write_fails, write_ignored;
    uint8_t last_reg;
} fake_chip_t;

static int fake_read(void *ctx, uint8_t reg, uint8_t *value) {
    fake_chip_t *f = ctx;
    f->reads++;
    f->last_reg = reg;
    if (f->read_fails) return -1;
    *value = f->reg3a;
    return 0;
}

static int fake_write(void *ctx, uint8_t reg, uint8_t value) {
    fake_chip_t *f = ctx;
    f->writes++;
    f->last_reg = reg;
    if (f->write_fails) return -1;
    if (!f->write_ignored) f->reg3a = value;
    return 0;
}

static void test_charge_limit(void) {
    // Bits 5-3 of 0x3A: the two snapshots (docs/delivery/2026-10-31-battery-display.md 3).
    CHECK(yk_board_charge_5v_ma(0xA3) == 2500 && yk_board_charge_5v_ma(0x83) == 500);
    CHECK(yk_board_charge_5v_ma(0x00) == 500 && yk_board_charge_5v_ma(0x38) == 3500 && yk_board_charge_5v_ma(0x30) == 3200);
    CHECK(yk_board_charge_5v_lowest(0xA3) == 0x83 && yk_board_charge_5v_lowest(0xFF) == 0xC7);
    // Already at 0.5 A (the old Tikpal snapshot): read once, nothing written.
    fake_chip_t f = {.reg3a = 0x83};
    yk_charge_limit_t c = yk_board_charge_limit(fake_read, fake_write, &f);
    CHECK(c.result == YK_CHARGE_ALREADY && c.before == 0x83 && c.wrote == -1 && c.after == -1);
    CHECK(f.reads == 1 && f.writes == 0 && f.reg3a == 0x83);
    // The default 2.5 A (the second test device): written once with only bits 5-3 changed, read back.
    f = (fake_chip_t){.reg3a = 0xA3};
    c = yk_board_charge_limit(fake_read, fake_write, &f);
    CHECK(c.result == YK_CHARGE_SET && c.before == 0xA3 && c.wrote == 0x83 && c.after == 0x83);
    CHECK(f.reads == 2 && f.writes == 1 && f.reg3a == 0x83 && f.last_reg == YK_BOARD_CHARGE_REG);
    // The high-voltage current and the temperature loop stay as read.
    f = (fake_chip_t){.reg3a = 0x7D};  // 3.7 A HV, 3.5 A at 5 V, 85 C
    c = yk_board_charge_limit(fake_read, fake_write, &f);
    CHECK(c.result == YK_CHARGE_SET && f.reg3a == 0x45 && (f.reg3a & 0xC7) == (0x7D & 0xC7));
    // Read back as something else: one write, no retry, mismatch.
    f = (fake_chip_t){.reg3a = 0xA3, .write_ignored = true};
    c = yk_board_charge_limit(fake_read, fake_write, &f);
    CHECK(c.result == YK_CHARGE_MISMATCH && c.wrote == 0x83 && c.after == 0xA3 && f.writes == 1);
    // The write not acknowledged: no read back, no retry.
    f = (fake_chip_t){.reg3a = 0xA3, .write_fails = true};
    c = yk_board_charge_limit(fake_read, fake_write, &f);
    CHECK(c.result == YK_CHARGE_WRITE_FAILED && c.after == -1 && f.writes == 1 && f.reads == 1);
    // The chip does not answer: nothing written.
    f = (fake_chip_t){.reg3a = 0xA3, .read_fails = true};
    c = yk_board_charge_limit(fake_read, fake_write, &f);
    CHECK(c.result == YK_CHARGE_NO_READ && c.before == -1 && f.writes == 0);
    CHECK(strcmp(yk_board_charge_result_name(YK_CHARGE_SET), "set") == 0 &&
          strcmp(yk_board_charge_result_name(YK_CHARGE_ALREADY), "already") == 0 &&
          strcmp(yk_board_charge_result_name(YK_CHARGE_NO_READ), "no_read") == 0 &&
          strcmp(yk_board_charge_result_name(YK_CHARGE_WRITE_FAILED), "write_failed") == 0 &&
          strcmp(yk_board_charge_result_name(YK_CHARGE_MISMATCH), "mismatch") == 0);
}

static void test_speaker(void) {
    const board_speaker_t *s = board_speaker();
    CHECK(s->ceiling_dbfs == -2.0f && s->voice_hp_hz == 800.0f);  // the top the owner chose by ear
    CHECK(board_speaker_codec_vol(0) == 0 && board_speaker_voice_boost_db(0) == 0);  // muted: nothing raised
    CHECK(board_speaker_codec_vol(10) == 100 && board_speaker_voice_boost_db(10) == 20.0f);
    CHECK(board_speaker_codec_vol(1) == 68 && board_speaker_voice_boost_db(1) == 10.0f);
    for (int v = 1; v < 10; v++) {
        CHECK(board_speaker_codec_vol(v + 1) > board_speaker_codec_vol(v));
        CHECK(board_speaker_voice_boost_db(v + 1) > board_speaker_voice_boost_db(v));
    }
    // Out of range: taken to the ends.
    CHECK(board_speaker_codec_vol(-3) == 0 && board_speaker_codec_vol(14) == 100);
    CHECK(board_speaker_voice_boost_db(-3) == board_speaker_voice_boost_db(0));
    CHECK(board_speaker_voice_boost_db(14) == 20.0f);
}

int main(void) {
    test_speaker();
    test_battery_display();
    test_charge_limit();
    test_font_pack();
    test_battery();
    test_battery_adc_and_prefs();
    test_round_area();
    test_touch_point();
    test_audio_rates();
    test_rtc_regs();
    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "ok", checks, failures);
    return failures ? 1 : 0;
}
