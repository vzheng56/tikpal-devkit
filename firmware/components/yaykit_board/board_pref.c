// The device's shared preferences (P10): volume, brightness, screen-off time in NVS namespace yk_pref, read and
// written by every firmware; since 0.1.12 also the buddy, since 0.1.14 the time zone (both written by the main
// firmware only). Values are checked on the way in and out (yk_board_logic.c).
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "board.h"
#include "esp_log.h"
#include "nvs.h"
#include "yk_board_logic.h"

static const char *TAG = "board_pref";

#define NS "yk_pref"

static const char *const KEYS[] = {"volume", "bright", "screen_off", "buddy"};  // by yk_pref_t; buddy is a u8

/** The stored value or -1. */
static int load(yk_pref_t which) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return -1;
    int32_t v = -1;
    if (nvs_get_i32(h, KEYS[which], &v) != ESP_OK) v = -1;
    nvs_close(h);
    return (int)v;
}

static esp_err_t store(yk_pref_t which, int value) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s not saved: %s", KEYS[which], esp_err_to_name(err));
        return err;
    }
    int32_t old = -1;
    if (nvs_get_i32(h, KEYS[which], &old) != ESP_OK || old != value) {  // no flash write for the same value
        err = nvs_set_i32(h, KEYS[which], value);
        if (err == ESP_OK) err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

int board_pref_volume(void) {
    int v = load(YK_PREF_VOLUME);
    if (v < 0) {
        // Before P10 the main firmware kept its volume in its own namespace: take it over once.
        nvs_handle_t h;
        uint8_t old = 0xFF;
        if (nvs_open("yay", NVS_READONLY, &h) == ESP_OK) {
            if (nvs_get_u8(h, "volume", &old) != ESP_OK) old = 0xFF;
            nvs_close(h);
        }
        if (old <= 10) {
            store(YK_PREF_VOLUME, old);
            return old;
        }
    }
    return yk_board_pref_read(YK_PREF_VOLUME, v);
}

esp_err_t board_pref_set_volume(int level) { return store(YK_PREF_VOLUME, yk_board_pref_clamp(YK_PREF_VOLUME, level)); }

int board_pref_brightness(void) { return yk_board_pref_read(YK_PREF_BRIGHTNESS, load(YK_PREF_BRIGHTNESS)); }

esp_err_t board_pref_set_brightness(int percent) {
    return store(YK_PREF_BRIGHTNESS, yk_board_pref_clamp(YK_PREF_BRIGHTNESS, percent));
}

int board_pref_screen_off_s(void) { return yk_board_pref_read(YK_PREF_SCREEN_OFF, load(YK_PREF_SCREEN_OFF)); }

esp_err_t board_pref_set_screen_off_s(int seconds) {
    return store(YK_PREF_SCREEN_OFF, yk_board_pref_clamp(YK_PREF_SCREEN_OFF, seconds));
}

// The buddy (0.1.12): a u8 under its own key. Only additions: the keys above keep their names and types, and
// YK_API_LEVEL stays where it is. Firmware built before this never reads it (its 「回到 Yay 说？」 keeps Yay);
// firmware built with it, on a main firmware that never wrote it (0.1.12 does not), reads the default: Yay.

int board_pref_buddy(void) {
    nvs_handle_t h;
    uint8_t v = 0xFF;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, KEYS[YK_PREF_BUDDY], &v) != ESP_OK) v = 0xFF;
        nvs_close(h);
    }
    return yk_board_pref_read(YK_PREF_BUDDY, v == 0xFF ? -1 : v);
}

esp_err_t board_pref_set_buddy(int buddy) {
    const uint8_t value = (uint8_t)yk_board_pref_clamp(YK_PREF_BUDDY, buddy);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s not saved: %s", KEYS[YK_PREF_BUDDY], esp_err_to_name(err));
        return err;
    }
    uint8_t old = 0xFF;
    if (nvs_get_u8(h, KEYS[YK_PREF_BUDDY], &old) != ESP_OK || old != value) {  // no flash write for the same value
        err = nvs_set_u8(h, KEYS[YK_PREF_BUDDY], value);
        if (err == ESP_OK) err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

// The time zone (0.1.14): a string under its own key, added as the buddy was.
_Static_assert(BOARD_PREF_TZ_MAX == YK_PREF_TZ_MAX, "board.h's buffer is the logic's");

bool board_pref_tz(char *out, size_t size) {
    if (!out || !size) return false;
    out[0] = 0;
    char v[YK_PREF_TZ_MAX];
    size_t n = sizeof(v);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    const bool got = nvs_get_str(h, "tz", v, &n) == ESP_OK && yk_board_tz_ok(v);
    nvs_close(h);
    if (got) snprintf(out, size, "%s", v);
    return got;
}

esp_err_t board_pref_set_tz(const char *posix) {
    const bool remove = !posix || !posix[0];
    if (!remove && !yk_board_tz_ok(posix)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "tz not saved: %s", esp_err_to_name(err));
        return err;
    }
    char old[YK_PREF_TZ_MAX] = "";
    size_t n = sizeof(old);
    const bool had = nvs_get_str(h, "tz", old, &n) == ESP_OK;
    const bool change = remove ? had : !had || strcmp(old, posix) != 0;  // no flash write for the same value
    if (change) {
        err = remove ? nvs_erase_key(h, "tz") : nvs_set_str(h, "tz", posix);
        if (err == ESP_OK) err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

bool board_pref_tz_offset(int *minutes) {
    nvs_handle_t h;
    if (!minutes || nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    int32_t v = 0;
    const bool got = nvs_get_i32(h, "tz_off", &v) == ESP_OK && yk_board_tz_offset_ok((int)v);
    nvs_close(h);
    if (got) *minutes = (int)v;
    return got;
}

esp_err_t board_pref_set_tz_offset(bool known, int minutes) {
    if (known && !yk_board_tz_offset_ok(minutes)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "tz_off not saved: %s", esp_err_to_name(err));
        return err;
    }
    int32_t old = 0;
    const bool had = nvs_get_i32(h, "tz_off", &old) == ESP_OK;
    const bool change = known ? !had || old != minutes : had;  // no flash write for the same value
    if (change) {
        err = known ? nvs_set_i32(h, "tz_off", minutes) : nvs_erase_key(h, "tz_off");
        if (err == ESP_OK) err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

bool board_pref_tz_apply(const char *fallback) {
    char tz[YK_PREF_TZ_MAX];
    const bool shared = board_pref_tz(tz, sizeof(tz));
    if (shared) setenv("TZ", tz, 1);
    else if (fallback) setenv("TZ", fallback, 1);
    else unsetenv("TZ");
    tzset();
    return shared;
}
