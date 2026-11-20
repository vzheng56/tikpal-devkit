// Host tests only (test_yay.c): no clock.
#pragma once
#include <stdint.h>
int64_t esp_timer_get_time(void);
