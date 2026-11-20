// Host tests only: every stack is internal RAM here.
#pragma once
#include <stdbool.h>
static inline bool esp_ptr_external_ram(const void *p) { (void)p; return false; }
