// Host tests only (test_hold.c): yaykit_session.c itself on these; test_hold.c answers the calls.
#pragma once
#include <stdio.h>
#define ESP_LOGE(tag, ...) (printf("E %s: ", tag), printf(__VA_ARGS__), printf("\n"))
#define ESP_LOGW(tag, ...) (printf("W %s: ", tag), printf(__VA_ARGS__), printf("\n"))
#define ESP_LOGI(tag, ...)          \
    do {                            \
        (void)(tag);                \
        if (0) printf(__VA_ARGS__); \
    } while (0)
