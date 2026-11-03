// Host tests only (test_me.c, test_account.c): warnings printed, information left out.
#pragma once
#include <stdio.h>
#define ESP_LOGW(tag, ...) (printf("W %s: ", tag), printf(__VA_ARGS__), printf("\n"))
#define ESP_LOGI(tag, ...)          \
    do {                            \
        (void)(tag);                \
        if (0) printf(__VA_ARGS__); \
    } while (0)
