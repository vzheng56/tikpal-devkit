#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef enum { WIFI_PROV_STA_CONNECTING, WIFI_PROV_STA_CONNECTED, WIFI_PROV_STA_DISCONNECTED } wifi_prov_sta_state_t;
typedef enum { WIFI_PROV_STA_AUTH_ERROR, WIFI_PROV_STA_AP_NOT_FOUND } wifi_prov_sta_fail_reason_t;
typedef struct {
    char ip_addr[16];
    char bssid[6];
    char ssid[33];
    uint8_t channel;
    uint8_t auth_mode;
} wifi_prov_sta_conn_info_t;
typedef struct { uint32_t attempts_remaining; } wifi_prov_sta_connecting_info_t;
typedef struct {
    wifi_prov_sta_state_t wifi_state;
    union {
        wifi_prov_sta_fail_reason_t fail_reason;
        wifi_prov_sta_conn_info_t conn_info;
        wifi_prov_sta_connecting_info_t connecting_info;
    };
} wifi_prov_config_get_data_t;
typedef struct {
    char ssid[33];
    char password[64];
    char bssid[6];
    uint8_t channel;
} wifi_prov_config_set_data_t;
typedef void wifi_prov_ctx_t;
typedef struct {
    esp_err_t (*get_status_handler)(wifi_prov_config_get_data_t *resp_data, wifi_prov_ctx_t **ctx);
    esp_err_t (*set_config_handler)(const wifi_prov_config_set_data_t *req_data, wifi_prov_ctx_t **ctx);
    esp_err_t (*apply_config_handler)(wifi_prov_ctx_t **ctx);
    wifi_prov_ctx_t *ctx;
} wifi_prov_config_handlers_t;
