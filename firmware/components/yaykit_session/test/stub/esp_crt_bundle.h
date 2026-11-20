// Host tests only (test_hold.c): yaykit_session.c itself on these; test_hold.c answers the calls.
#pragma once
#include "esp_err.h"
esp_err_t esp_crt_bundle_attach(void *conf);
