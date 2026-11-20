// Host tests only: what yk_link_keep.h and yk_link_keys.c need from ESP-IDF's esp_err.h.
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_NOT_SUPPORTED 0x106
const char *esp_err_to_name(esp_err_t code);
