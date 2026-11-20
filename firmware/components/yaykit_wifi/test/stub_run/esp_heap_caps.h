#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_8BIT (1 << 2)
void *heap_caps_calloc(size_t n, size_t size, unsigned caps);
