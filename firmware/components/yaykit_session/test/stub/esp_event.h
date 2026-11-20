// Host tests only (test_hold.c): yaykit_session.c itself on these; test_hold.c answers the calls.
#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef const char *esp_event_base_t;
typedef void (*esp_event_handler_t)(void *arg, esp_event_base_t base, int32_t id, void *data);
extern const esp_event_base_t IP_EVENT;
#define IP_EVENT_STA_GOT_IP 0
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *arg);
