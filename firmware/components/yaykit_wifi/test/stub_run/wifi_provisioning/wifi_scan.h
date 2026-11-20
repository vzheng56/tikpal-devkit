#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef void wifi_prov_scan_ctx_t;
typedef struct {
    char ssid[33];
    char bssid[6];
    uint8_t channel;
    int rssi;
    uint8_t auth;
} wifi_prov_scan_result_t;
typedef struct {
    esp_err_t (*scan_start)(bool blocking, bool passive, uint8_t group_channels, uint32_t period_ms, wifi_prov_scan_ctx_t **ctx);
    esp_err_t (*scan_status)(bool *scan_finished, uint16_t *result_count, wifi_prov_scan_ctx_t **ctx);
    esp_err_t (*scan_result)(uint16_t result_index, wifi_prov_scan_result_t *result, wifi_prov_scan_ctx_t **ctx);
    wifi_prov_scan_ctx_t *ctx;
} wifi_prov_scan_handlers_t;
