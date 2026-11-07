#include "yk_board_logic.h"

void yk_board_round_area(int32_t *x1, int32_t *y1, int32_t *x2, int32_t *y2) {
    *x1 = (*x1 >> 1) << 1;
    *y1 = (*y1 >> 1) << 1;
    *x2 = ((*x2 >> 1) << 1) + 1;
    *y2 = ((*y2 >> 1) << 1) + 1;
}

bool yk_board_touch_point(const uint8_t report[6], int32_t width, int32_t height, int32_t *x, int32_t *y) {
    if ((report[1] & 0x0F) == 0) return false;
    int32_t px = ((report[2] & 0x0F) << 8) | report[3];
    int32_t py = ((report[4] & 0x0F) << 8) | report[5];
    *x = px >= width ? width - 1 : px;
    *y = py >= height ? height - 1 : py;
    return true;
}

bool yk_board_audio_rate_ok(uint32_t hz) {
    return hz == 16000 || hz == 24000;
}

int yk_board_battery_mv(uint8_t reg14, uint8_t reg15) { return (((reg15 & 0x0F) << 8) | reg14) * 12 / 10; }

bool yk_board_battery_known(int mv) { return mv >= 2500 && mv <= 4600; }

int yk_board_battery_percent(int mv) {
    static const struct {
        int mv, pct;
    } curve[] = {{3300, 0}, {3500, 5}, {3600, 10}, {3700, 25}, {3750, 40}, {3800, 50},
                 {3850, 60}, {3900, 70}, {4000, 80}, {4100, 90}, {4200, 100}};
    const int n = (int)(sizeof(curve) / sizeof(curve[0]));
    if (mv <= curve[0].mv) return 0;
    if (mv >= curve[n - 1].mv) return 100;
    for (int i = 1; i < n; i++)
        if (mv <= curve[i].mv)
            return curve[i - 1].pct + (mv - curve[i - 1].mv) * (curve[i].pct - curve[i - 1].pct) / (curve[i].mv - curve[i - 1].mv);
    return 100;
}

int yk_board_vbat_mv(int pin_mv) { return pin_mv * 2; }

bool yk_board_vbus_present(int pin_mv) { return pin_mv >= 1600; }

int yk_board_battery_level(int mv, bool charging) {
    if (!yk_board_battery_known(mv)) return -1;
    if (!charging) return yk_board_battery_percent(mv);
    const int pct = yk_board_battery_percent(mv - YK_BOARD_CHARGE_RISE_MV);
    return pct > 99 ? 99 : pct;
}

bool yk_board_battery_full(int mv) { return mv >= 4150; }

void yk_board_batt_feed(yk_board_batt_t *b, int64_t now_ms, int mv, bool usb, int sys) {
    const bool first = !b->started;
    if (first || usb != b->usb) {  // started, plugged or unplugged: the voltage steps, so the average starts again
        b->count = b->next = 0;
        b->plug_ms = now_ms;
        b->off_ms = -1;
        b->full = false;
        b->fault = YK_BOARD_BATT_FAULT_NONE;
        b->usb = usb;
        b->started = true;
    }
    b->ring[b->next] = mv;
    b->next = (b->next + 1) % YK_BOARD_BATT_AVG;
    if (b->count < YK_BOARD_BATT_AVG) b->count++;
    int sum = 0;
    for (int i = 0; i < b->count; i++) sum += b->ring[i];
    const int avg = b->avg_mv = sum / b->count;

    const int64_t plugged = now_ms - b->plug_ms;
    if (!usb) {
        b->state = YK_BOARD_BATT_DISCHARGING;
    } else if (sys < 0) {
        b->state = YK_BOARD_BATT_UNKNOWN;  // the chip's timers keep: a missed read is not a change
    } else {
        const bool on = (sys & YK_SW6106_SYS_CHARGING) != 0;
        if (on) b->off_ms = -1;
        else if (b->off_ms < 0) b->off_ms = now_ms;
        // At the top by this reading or the average: the average lags when the charger stops, and must not say
        // stopped for a reading first.
        const bool top = mv >= YK_BOARD_BATT_FULL_MV || avg >= YK_BOARD_BATT_FULL_MV;
        if (!on && plugged >= YK_BOARD_BATT_GRACE_MS && top) b->full = true;
        if (b->full) b->state = YK_BOARD_BATT_FULL;
        else if (on || plugged < YK_BOARD_BATT_GRACE_MS || now_ms - b->off_ms < YK_BOARD_BATT_STOP_MS)
            b->state = YK_BOARD_BATT_CHARGING;
        else b->state = YK_BOARD_BATT_STOPPED;
    }
    b->charging = b->state == YK_BOARD_BATT_CHARGING ||
                  (b->state == YK_BOARD_BATT_UNKNOWN && !yk_board_battery_full(avg));
    b->low = b->charging && avg < YK_BOARD_BATT_LOW_MV;
    if (usb && plugged >= YK_BOARD_BATT_NO_RISE_MS && avg < YK_BOARD_BATT_LOW_MV) b->fault = YK_BOARD_BATT_FAULT_NO_RISE;

    // The guards' number: this reading alone, the charging rise taken off; on the battery, the resting curve as before.
    b->level = yk_board_battery_percent(b->charging ? mv - YK_BOARD_CHARGE_RISE_MV : mv);
    int target = yk_board_battery_percent(b->charging ? avg - YK_BOARD_CHARGE_RISE_MV : avg);
    if (b->state == YK_BOARD_BATT_FULL) target = 100;
    else if (usb && target > 99) target = 99;  // 100 only once the chip says full
    if (first) {
        b->percent = target;
        b->step_ms = now_ms;
    } else if (now_ms - b->step_ms >= YK_BOARD_BATT_STEP_MS) {
        b->step_ms = now_ms;
        if (usb && target > b->percent) b->percent++;
        else if (!usb && target < b->percent) b->percent--;
    }
}

const char *yk_board_batt_state_name(yk_board_batt_state_t state) {
    switch (state) {
    case YK_BOARD_BATT_DISCHARGING: return "discharging";
    case YK_BOARD_BATT_CHARGING: return "charging";
    case YK_BOARD_BATT_FULL: return "full";
    case YK_BOARD_BATT_STOPPED: return "stopped";
    default: return "unknown";
    }
}

const char *yk_board_batt_fault_name(yk_board_batt_fault_t fault) {
    return fault == YK_BOARD_BATT_FAULT_NO_RISE ? "no_rise" : NULL;
}

int yk_board_charge_ma(uint8_t reg17, uint8_t reg18) { return (((reg18 & 0x0F) << 8) | reg17) * 25 / 7; }

int yk_board_full_mv(uint8_t reg3b) {
    static const int mv[] = {4200, 4300, 4350, 4400};
    return mv[(reg3b >> 2) & 3];
}

int yk_board_end_ma(uint8_t reg3b) { return reg3b & 0x80 ? 200 : 300; }

int yk_board_charge_5v_ma(uint8_t reg3a) {
    static const int ma[] = {500, 1000, 1500, 2000, 2500, 3000, 3200, 3500};
    return ma[(reg3a & YK_BOARD_CHARGE_5V_MASK) >> 3];
}

uint8_t yk_board_charge_5v_lowest(uint8_t reg3a) { return (uint8_t)(reg3a & ~YK_BOARD_CHARGE_5V_MASK); }

yk_charge_limit_t yk_board_charge_limit(int (*read)(void *ctx, uint8_t reg, uint8_t *value),
                                        int (*write)(void *ctx, uint8_t reg, uint8_t value), void *ctx) {
    yk_charge_limit_t c = {.before = -1, .wrote = -1, .after = -1, .result = YK_CHARGE_NO_READ};
    uint8_t v = 0;
    if (read(ctx, YK_BOARD_CHARGE_REG, &v) != 0) return c;
    c.before = v;
    const uint8_t want = yk_board_charge_5v_lowest(v);
    if (v == want) {
        c.result = YK_CHARGE_ALREADY;
        return c;
    }
    c.wrote = want;
    if (write(ctx, YK_BOARD_CHARGE_REG, want) != 0) {
        c.result = YK_CHARGE_WRITE_FAILED;
        return c;
    }
    if (read(ctx, YK_BOARD_CHARGE_REG, &v) == 0) c.after = v;
    c.result = c.after == want ? YK_CHARGE_SET : YK_CHARGE_MISMATCH;
    return c;
}

const char *yk_board_charge_result_name(yk_charge_result_t result) {
    switch (result) {
    case YK_CHARGE_SET: return "set";
    case YK_CHARGE_ALREADY: return "already";
    case YK_CHARGE_NO_READ: return "no_read";
    case YK_CHARGE_WRITE_FAILED: return "write_failed";
    default: return "mismatch";
    }
}

int yk_board_pref_default(yk_pref_t which) {
    switch (which) {
    case YK_PREF_VOLUME: return 6;
    case YK_PREF_BRIGHTNESS: return 80;
    case YK_PREF_BUDDY: return 0;  // Pip
    default: return 30;  // the screen off after 30 s (P22; 180 before)
    }
}

int yk_board_pref_clamp(yk_pref_t which, int v) {
    switch (which) {
    case YK_PREF_VOLUME: return v < 0 ? 0 : v > 10 ? 10 : v;
    case YK_PREF_BRIGHTNESS: return v < 5 ? 5 : v > 100 ? 100 : v;
    case YK_PREF_BUDDY: return v == 0 || (v >= 3 && v <= 5) ? v : 0;  // not a neighbour: one it does not know is Pip
    default: return v <= 0 ? 0 : v < 15 ? 15 : v > 3600 ? 3600 : v;
    }
}

int yk_board_pref_read(yk_pref_t which, int stored) {
    if (stored < 0) return yk_board_pref_default(which);
    return yk_board_pref_clamp(which, stored) == stored ? stored : yk_board_pref_default(which);
}

bool yk_board_tz_offset_ok(int minutes) { return minutes >= YK_PREF_TZ_OFF_MIN && minutes <= YK_PREF_TZ_OFF_MAX; }

bool yk_board_tz_ok(const char *posix) {
    if (!posix || !((posix[0] >= 'A' && posix[0] <= 'Z') || (posix[0] >= 'a' && posix[0] <= 'z') || posix[0] == '<'))
        return false;
    size_t n = 0;
    for (; posix[n]; n++)
        if (posix[n] <= ' ' || posix[n] > '~' || n >= YK_PREF_TZ_MAX - 1) return false;
    return n > 0;
}
