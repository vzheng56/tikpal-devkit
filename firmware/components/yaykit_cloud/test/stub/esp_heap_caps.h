// Host tests only (test_me.c; test_region.c and test_account.c: a development build's test deployments).
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define heap_caps_malloc(size, caps) malloc(size)
#define heap_caps_calloc(n, size, caps) calloc(n, size)
