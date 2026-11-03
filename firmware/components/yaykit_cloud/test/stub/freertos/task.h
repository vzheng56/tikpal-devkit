// Host tests only (test_account.c): a task runs to its end inside xTaskCreate; vTaskDelay only counts.
#pragma once
#include "freertos/FreeRTOS.h"
typedef void (*TaskFunction_t)(void *arg);
typedef void *TaskHandle_t;
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, unsigned priority,
                       TaskHandle_t *created);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
