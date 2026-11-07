// The battery (P20). The cell's voltage from the ESP32's own ADC on V_BAT / 2 (GPIO17), which reads on battery as on
// USB, and external power from USB 5 V / 2 (GPIO18), per the reference schematic. The SW6106 power chip (I2C 0x3C)
// is read for charging and full (0x11) and, on USB, its own voltage and charge current: its voltage registers read 0
// on battery. It is the power path: the one thing ever written to it is the 5 V charge current, down to its lowest at
// start (board_charge_limit, docs/delivery/2026-10-31-battery-charge-limit.md). The number shown is smoothed
// (yk_board_batt_feed, docs/delivery/2026-10-31-battery-display.md).
#include <stdio.h>

#include "board.h"
#include "board_priv.h"
#include "esp_timer.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "yk_board_logic.h"

static const char *TAG = "board_power";

#define SW6106_ADDR 0x3C
#define VBAT_CH ADC_CHANNEL_6  // GPIO17 on ADC2
#define VBUS_CH ADC_CHANNEL_7  // GPIO18 on ADC2

static i2c_master_dev_handle_t s_power;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali_bat, s_cali_bus;
static SemaphoreHandle_t s_lock;          // the ADC, and s_batt
static yk_board_batt_t s_batt;            // the smoothing, from this firmware's start
static int s_test_mv;                     // board_battery_test
static bool s_test_no_chip, s_chip_logged;

_Static_assert((int)BOARD_BATTERY_UNKNOWN == (int)YK_BOARD_BATT_UNKNOWN &&
                   (int)BOARD_BATTERY_STOPPED == (int)YK_BOARD_BATT_STOPPED &&
                   (int)BOARD_BATTERY_FULL == (int)YK_BOARD_BATT_FULL &&
                   (int)BOARD_BATTERY_FAULT_NO_RISE == (int)YK_BOARD_BATT_FAULT_NO_RISE,
               "board.h and yk_board_logic.h name the battery states alike");

static esp_err_t power_dev(void)
{
    if (!board_i2c()) return ESP_ERR_INVALID_STATE;
    if (s_power) return ESP_OK;
    const i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = SW6106_ADDR,
                                     .scl_speed_hz = 100000};
    return i2c_master_bus_add_device(board_i2c(), &cfg, &s_power);
}

/* One register, read: the address goes out, the value comes back (nothing is written into the chip). */
static esp_err_t power_read(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_power, &reg, 1, value, 1, 20);
}

/* One register, with retries: the touch controller shares the bus. */
static bool power_get(uint8_t reg, uint8_t *value)
{
    for (int i = 0; i < 3; i++)
        if (power_read(reg, value) == ESP_OK) return true;
    return false;
}

static yk_charge_limit_t s_charge = {.before = -1, .wrote = -1, .after = -1, .result = YK_CHARGE_NO_READ};
static bool s_charge_done;

static const char *reg_hex(int v, char buf[12])
{
    if (v < 0) return "none";
    snprintf(buf, 12, "0x%02x", (unsigned)(v & 0xFF));
    return buf;
}

/* The start's result, as one line (board_charge_limit, and again with the register dump). */
static void charge_log(void)
{
    const yk_charge_limit_t c = s_charge;
    char b[12], w[12], a[12];
    printf("YK_BOARD_CHARGE before=%s before_ma=%d wrote=%s after=%s after_ma=%d result=%s\n", reg_hex(c.before, b),
           c.before >= 0 ? yk_board_charge_5v_ma((uint8_t)c.before) : -1, reg_hex(c.wrote, w), reg_hex(c.after, a),
           c.after >= 0 ? yk_board_charge_5v_ma((uint8_t)c.after) : -1, yk_board_charge_result_name(c.result));
}

esp_err_t board_battery_regs(uint8_t regs[64])
{
    ESP_RETURN_ON_ERROR(power_dev(), TAG, "battery");
    int ok = 0;
    for (int r = 0; r < 64; r++) {
        regs[r] = 0xFF;
        for (int i = 0; i < 3; i++)  // the touch controller shares the bus
            if (power_read((uint8_t)r, &regs[r]) == ESP_OK) {
                ok++;
                break;
            }
    }
    if (s_charge_done) charge_log();
    return ok ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static int charge_read(void *ctx, uint8_t reg, uint8_t *value)
{
    (void)ctx;
    return power_get(reg, value) ? 0 : -1;  // retried: the touch controller shares the bus (not yet at board_init)
}

/* One register written: the only write this chip ever gets (board_charge_limit). */
static int charge_write(void *ctx, uint8_t reg, uint8_t value)
{
    (void)ctx;
    const uint8_t frame[2] = {reg, value};
    return i2c_master_transmit(s_power, frame, sizeof(frame), 20) == ESP_OK ? 0 : -1;
}

void board_charge_limit(void)
{
    // Every start, every firmware: the register keeps its value through ESP restarts and slot switches but goes back to
    // the chip's default (2.5 A) when the chip loses power, and a firmware cannot tell. none / -1: not read or written.
    if (power_dev() == ESP_OK) s_charge = yk_board_charge_limit(charge_read, charge_write, NULL);
    s_charge_done = true;
    charge_log();
}

static adc_cali_handle_t cali(adc_channel_t ch)
{
    adc_cali_handle_t h = NULL;
    const adc_cali_curve_fitting_config_t cfg = {
        .unit_id = ADC_UNIT_2, .chan = ch, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    if (adc_cali_create_scheme_curve_fitting(&cfg, &h) != ESP_OK) {
        ESP_LOGW(TAG, "no ADC calibration for channel %d: raw scaling", ch);
        return NULL;
    }
    return h;
}

static esp_err_t adc_start(void)
{
    if (s_adc) return ESP_OK;
    if (!s_lock && !(s_lock = xSemaphoreCreateMutex())) return ESP_ERR_NO_MEM;
    const adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_2, .ulp_mode = ADC_ULP_MODE_DISABLE};
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit, &s_adc), TAG, "ADC2");
    const adc_oneshot_chan_cfg_t ch = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, VBAT_CH, &ch), TAG, "VBAT");
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, VBUS_CH, &ch), TAG, "VBUS");
    s_cali_bat = cali(VBAT_CH);
    s_cali_bus = cali(VBUS_CH);
    return ESP_OK;
}

/** Millivolts at the pin, averaged over n reads; -1 when none worked (Wi-Fi holds ADC2 now and then). */
static int pin_mv(adc_channel_t ch, adc_cali_handle_t cal, int n)
{
    int sum = 0, ok = 0;
    for (int i = 0; i < n * 3 && ok < n; i++) {
        int raw = 0, mv = 0;
        if (adc_oneshot_read(s_adc, ch, &raw) != ESP_OK) {
            vTaskDelay(1);
            continue;
        }
        if (!cal || adc_cali_raw_to_voltage(cal, raw, &mv) != ESP_OK) mv = raw * 3100 / 4095;
        sum += mv;
        ok++;
    }
    return ok ? sum / ok : -1;
}

esp_err_t board_battery(board_battery_t *out)
{
    *out = (board_battery_t){.mv = 0, .percent = -1, .status = -1, .level = -1, .chip_mv = -1, .charge_ma = -1};
    esp_err_t err = adc_start();
    int bat = -1, bus = -1;
    if (err == ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bat = pin_mv(VBAT_CH, s_cali_bat, 8);
        bus = pin_mv(VBUS_CH, s_cali_bus, 4);
        xSemaphoreGive(s_lock);
    } else {
        ESP_LOGW(TAG, "ADC: %s", esp_err_to_name(err));
    }
    out->usb = bus >= 0 && yk_board_vbus_present(bus);
    if (!s_test_no_chip && power_dev() == ESP_OK) {
        uint8_t v, lo, hi;
        if (power_get(0x11, &v)) out->status = v;
        if (out->usb && power_get(0x14, &lo) && power_get(0x15, &hi)) out->chip_mv = yk_board_battery_mv(lo, hi);
        if (out->usb && power_get(0x17, &lo) && power_get(0x18, &hi)) out->charge_ma = yk_board_charge_ma(lo, hi);
        if (!s_chip_logged && power_get(0x3B, &v)) {  // what it charges to: 4.4 V on the boards read so far
            s_chip_logged = true;
            printf("YK_BOARD_BATTERY chip full_mv=%d end_ma=%d reg3b=0x%02x\n", yk_board_full_mv(v), yk_board_end_ma(v),
                   v);
        }
    }
    if (bat < 0) return ESP_ERR_NOT_FOUND;
    out->mv = s_test_mv > 0 ? s_test_mv : yk_board_vbat_mv(bat);
    if (!yk_board_battery_known(out->mv)) {
        out->state = out->usb ? BOARD_BATTERY_UNKNOWN : BOARD_BATTERY_DISCHARGING;
        return ESP_ERR_INVALID_RESPONSE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    yk_board_batt_feed(&s_batt, esp_timer_get_time() / 1000, out->mv, out->usb, out->status);
    out->percent = s_batt.percent;
    out->level = s_batt.level;
    out->state = (board_battery_state_t)s_batt.state;
    out->charging = s_batt.charging;
    out->low = s_batt.low;
    out->fault = (board_battery_fault_t)s_batt.fault;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

const char *board_battery_state_name(board_battery_state_t state)
{
    return yk_board_batt_state_name((yk_board_batt_state_t)state);
}

const char *board_battery_fault_name(board_battery_fault_t fault)
{
    return yk_board_batt_fault_name((yk_board_batt_fault_t)fault);
}

void board_battery_test(int mv, bool chip)
{
    s_test_mv = mv > 0 ? mv : 0;
    s_test_no_chip = !chip;
}
