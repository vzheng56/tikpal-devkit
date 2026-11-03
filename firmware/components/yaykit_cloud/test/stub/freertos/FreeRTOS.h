// Host tests only (test_account.c): the FreeRTOS types yaykit_account.c uses; test_account.c answers the calls.
#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef long BaseType_t;
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
