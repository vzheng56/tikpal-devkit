// Host tests only (test_account.c): the console is linked there, not used.
#pragma once
#include "esp_err.h"
typedef int (*esp_console_cmd_func_t)(int argc, char **argv);
typedef struct {
    const char *command;
    const char *help;
    const char *hint;
    esp_console_cmd_func_t func;
    void *argtable;
} esp_console_cmd_t;
esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd);
