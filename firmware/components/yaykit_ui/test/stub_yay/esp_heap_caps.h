// Host tests only (test_yay.c): PSRAM is the heap.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM (1 << 10)
static inline void *heap_caps_malloc(size_t n, uint32_t caps) {
    (void)caps;
    return malloc(n);
}
static inline void *heap_caps_calloc(size_t n, size_t size, uint32_t caps) {
    (void)caps;
    return calloc(n, size);
}
static inline void heap_caps_free(void *p) { free(p); }
