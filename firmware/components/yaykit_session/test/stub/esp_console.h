// Host tests only (test_hold.c): yaykit_session.c itself on these; test_hold.c answers the calls.
#pragma once
#include "esp_err.h"
typedef int (*esp_console_cmd_func_t)(int argc, char **argv);
typedef struct {
    const char *command, *help;
    esp_console_cmd_func_t func;
} esp_console_cmd_t;
esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd);
