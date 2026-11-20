// Host tests only (test_hold.c): yaykit_session.c itself on these; test_hold.c answers the calls.
#pragma once
#include <stddef.h>
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define heap_caps_malloc(size, caps) malloc(size)
#define heap_caps_calloc(n, size, caps) calloc(n, size)
#define heap_caps_free(p) free(p)
#define heap_caps_get_free_size(caps) ((size_t)60000)
#define heap_caps_get_minimum_free_size(caps) ((size_t)50000)
