// Host tests only (test_hold.c): yaykit_session.c itself on these; test_hold.c answers the calls.
#pragma once
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg, unsigned priority,
                                   TaskHandle_t *created, int core);
