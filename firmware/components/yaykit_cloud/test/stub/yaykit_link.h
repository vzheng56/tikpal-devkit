// Host tests only (test_account.c, test_http.c): the device ID and Bluetooth, from the tests.
#pragma once
#include <stdbool.h>
#include "esp_err.h"
const char *yk_link_device_id(void);
void yk_link_keys_forget(void);
bool yk_link_ble_active(void);
esp_err_t yk_link_ble_stop(void);
int yk_link_ble_core(void);
