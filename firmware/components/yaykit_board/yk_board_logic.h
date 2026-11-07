// Pure C parts of yaykit_board, host tested with test/run.sh (no ESP-IDF needed).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The panel controller wants every update to start on an even and end on an odd pixel, in both directions.
void yk_board_round_area(int32_t *x1, int32_t *y1, int32_t *x2, int32_t *y2);

// Reads a CST820 report (registers 0x01..0x06: gesture, finger count, x hi/lo, y hi/lo). With a finger down it
// writes the point, clamped to width x height, and returns true; otherwise it leaves the point and returns false.
bool yk_board_touch_point(const uint8_t report[6], int32_t width, int32_t height, int32_t *x, int32_t *y);

// Sample rates the shared I2S port runs at. Both directions use MCLK = 256 x fs, which ES8311 and ES7210
// take at 16 kHz (speech, ESP-SR) and 24 kHz (the default the current apps were tuned at).
bool yk_board_audio_rate_ok(uint32_t hz);

// The battery (SW6106, I2C 0x3C, read-only): registers 0x14 (low byte) and 0x15 (low nibble = the top 4 bits) are a
// 12-bit ADC value at 1.2 mV a step (tikpal-firmware docs/硬件信息参考.md 5.8, checked against the factory firmware's
// own log: raw 3344 = 4012.8 mV).
int yk_board_battery_mv(uint8_t reg14, uint8_t reg15);
// A rough level from the voltage, 0..100: a resting Li-ion cell's curve (3.3 V empty, 4.2 V full). Rough on purpose:
// the voltage reads high while charging, and the factory firmware's own formula is not known.
int yk_board_battery_percent(int mv);
// Whether a voltage from those registers is the battery's: they read 0 while the device runs on its battery (first
// end-to-end test, 2026-10-19: 0 mV unplugged, 4.39–4.43 V on USB), so the level is known only on USB for now.
bool yk_board_battery_known(int mv);

// The battery from the ESP32's ADC (P20): V_BAT through 100k + 100k on GPIO17, USB 5 V through 200k + 200k on GPIO18
// (reference schematic). The ADC reads millivolts at the pin (calibrated, 12 dB attenuation: up to ~3.1 V).
int yk_board_vbat_mv(int pin_mv);
// External power: the USB pin reads about 2.5 V with 5 V present, ~0 without (threshold 1.6 V = 3.2 V on the bus).
bool yk_board_vbus_present(int pin_mv);
// The level while charging: the cell reads higher under charge current, so the rise is taken off (YK_BOARD_CHARGE_RISE_MV)
// and the level stays below 100 until the charger stops (full: no rise, the resting curve says 100).
#define YK_BOARD_CHARGE_RISE_MV 120
int yk_board_battery_level(int mv, bool charging);
// Full while on USB: the cell at the charger's end voltage (4.15 V and up).
bool yk_board_battery_full(int mv);

// What the round screen says about the battery (docs/delivery/2026-10-31-battery-display.md 4): charging and full as
// the SW6106 says (register 0x11, bit 4: its charger on), the number smoothed. Fed every reading by board_battery().
#define YK_SW6106_SYS_CHARGING 0x10    // 0x11 bit 4: the charger is on
#define YK_BOARD_BATT_AVG 4             // readings averaged (each already 8 ADC reads), started again on plugging
#define YK_BOARD_BATT_STEP_MS 9500      // the number moves at most 1 a reading, and not again within 10 s (0.5 s slack)
#define YK_BOARD_BATT_GRACE_MS 60000    // just plugged in: the chip may not have started yet, taken as charging
#define YK_BOARD_BATT_STOP_MS 60000     // the charger off below the top this long: stopped
#define YK_BOARD_BATT_FULL_MV 4100      // the charger off with the reading or the average at or above this: full
#define YK_BOARD_BATT_LOW_MV 3300       // charging below this: 「电量很低，正在充电」, no number
#define YK_BOARD_BATT_NO_RISE_MS 1800000 // plugged in 30 min, still under YK_BOARD_BATT_LOW_MV: may be faulty
typedef enum {
    YK_BOARD_BATT_DISCHARGING,  // on the battery
    YK_BOARD_BATT_CHARGING,     // plugged in, the chip charging (or the first minute, before it starts)
    YK_BOARD_BATT_FULL,         // plugged in, the chip stopped at the top: kept until unplugged (its top-ups too)
    YK_BOARD_BATT_STOPPED,      // plugged in, the chip stopped below the top for over a minute
    YK_BOARD_BATT_UNKNOWN,      // plugged in, the chip did not answer: charging guessed from the voltage, never full
} yk_board_batt_state_t;
typedef enum { YK_BOARD_BATT_FAULT_NONE, YK_BOARD_BATT_FAULT_NO_RISE } yk_board_batt_fault_t;
typedef struct {
    // From the last reading.
    int avg_mv;     // the average the number and the states use
    int level;      // this reading straight from the voltage, not smoothed or capped: the battery guards use it
    int percent;    // the number on the screen: smoothed, 99 at most until full
    yk_board_batt_state_t state;
    bool charging;  // the chip charging (guessed when it did not answer)
    bool low;       // charging below YK_BOARD_BATT_LOW_MV
    yk_board_batt_fault_t fault;  // stays until unplugged: told once a plugging
    // Kept between readings.
    bool started, usb;
    int ring[YK_BOARD_BATT_AVG], count, next;
    int64_t step_ms, plug_ms, off_ms;  // off_ms: since when the charger is off, -1 while on
    bool full;
} yk_board_batt_t;
// One reading: now (monotonic ms), the cell's voltage (ADC, mV), USB present, and SW6106 0x11 (-1 when it did not
// answer). The first reading after start is taken as it is; after that the number moves 1 at most a reading (10 s
// apart), only up while plugged in and only down on the battery, so plugging or unplugging never makes it jump.
void yk_board_batt_feed(yk_board_batt_t *b, int64_t now_ms, int mv, bool usb, int sys);
// The status report's words (contract statusReport.battery.state / fault): "discharging", "charging", "full",
// "stopped", "unknown"; "no_rise", NULL for no fault.
const char *yk_board_batt_state_name(yk_board_batt_state_t state);
const char *yk_board_batt_fault_name(yk_board_batt_fault_t fault);
// The charge current the SW6106 measures, mA: ((0x18[3:0] << 8) + 0x17) x 25 / 7 (RG006 v1.2).
int yk_board_charge_ma(uint8_t reg17, uint8_t reg18);
// 0x3B (read-only bits from the chip's BSET pin or OTP): bits 3-2 the voltage it charges to, 4200/4300/4350/4400 mV;
// bit 7 the current it stops at, 300 or 200 mA.
int yk_board_full_mv(uint8_t reg3b);
int yk_board_end_ma(uint8_t reg3b);

// The 5 V charge current (docs/delivery/2026-10-31-battery-charge-limit.md): SW6106 register 0x3A, bits 5-3 pick
// 0.5/1.0/1.5/2.0/2.5/3.0/3.2/3.5 A (SW6106 I2C Register List RG006 v1.2, 2.32); the other bits are the high-voltage
// input's current and the temperature loop, left as they are. The cell (BPR 663030, 800 mAh) takes 400 mA at most, the
// chip's default read 2.5 A on the second test device (2026-10-26): 0.5 A is the lowest the chip offers.
#define YK_BOARD_CHARGE_REG 0x3A
#define YK_BOARD_CHARGE_5V_MASK 0x38
int yk_board_charge_5v_ma(uint8_t reg3a);
// The same register with only bits 5-3 changed, to 0.5 A.
uint8_t yk_board_charge_5v_lowest(uint8_t reg3a);
typedef enum {
    YK_CHARGE_SET,           // written and read back at 0.5 A
    YK_CHARGE_ALREADY,       // read at 0.5 A already: nothing written
    YK_CHARGE_NO_READ,       // 0x3A did not answer: nothing written
    YK_CHARGE_WRITE_FAILED,  // the write was not acknowledged
    YK_CHARGE_MISMATCH,      // read back as something else, or not at all
} yk_charge_result_t;
typedef struct {
    int before;  // 0x3A as read first, -1 not read
    int wrote;   // what was written, -1 nothing
    int after;   // read back after the write, -1 not read (or nothing written)
    yk_charge_result_t result;
} yk_charge_limit_t;
// The steps, with the bus passed in so the host can test them: read 0x3A; at 0.5 A already, leave it; else write it
// once with only bits 5-3 changed (no retry: a failure is logged, never in the way of starting) and read it back.
// read and write return 0 on success.
yk_charge_limit_t yk_board_charge_limit(int (*read)(void *ctx, uint8_t reg, uint8_t *value),
                                        int (*write)(void *ctx, uint8_t reg, uint8_t value), void *ctx);
// "set", "already", "no_read", "write_failed", "mismatch": the log line's result.
const char *yk_board_charge_result_name(yk_charge_result_t result);

// The shared preferences (P10): the valid range of each, a bad stored value reading as the default.
// YK_PREF_BUDDY (0.1.12, docs/delivery/2026-11-06-形象设计.md 8): who talks with you, 0 Pip, 3 Mote, 4 Dial, 5 Bloom
// (BOARD_BUDDY_*); any other value, 1 and 2 (retired) too, reads and is set as Pip. Added at the end: the others keep
// their numbers.
typedef enum { YK_PREF_VOLUME, YK_PREF_BRIGHTNESS, YK_PREF_SCREEN_OFF, YK_PREF_BUDDY } yk_pref_t;
int yk_board_pref_clamp(yk_pref_t which, int value);
int yk_board_pref_default(yk_pref_t which);
// A value read back from NVS (or -1 when missing): in range it stands, out of range it is the default.
int yk_board_pref_read(yk_pref_t which, int stored);
// The time zone (0.1.14, YK_PREF_TZ; docs/delivery/2026-11-02-system-pulldown.md 8): a POSIX TZ string newlib reads
// ("CST-8", "EST5EDT,M3.2.0,M11.1.0", "<+0530>-5:30"), at most YK_PREF_TZ_MAX - 1 characters: printable ASCII without
// spaces, starting with a letter or '<'. Anything else is not one (a bad value reads as not written).
#define YK_PREF_TZ_MAX 64
bool yk_board_tz_ok(const char *posix);
// ... and its offset east of UTC now, in minutes (key `tz_off`, BOSS-2 11-02): -12:00 to +14:00.
#define YK_PREF_TZ_OFF_MIN (-720)
#define YK_PREF_TZ_OFF_MAX 840
bool yk_board_tz_offset_ok(int minutes);
