// Development builds' `yay <name>` commands (yk_app_dev.h). Yay's development build registers 42 (11-21) and more keep
// coming: the table of 40 in internal RAM (12 bytes each, .bss) left the last two out (`no room for yay badge`,
// `... folders`). Here the only limit is PSRAM: blocks of BLOCK commands, one more each time the last is full, nothing
// in internal RAM but the two pointers. Registered at start-up (app_main and what it calls), after ESP-IDF's start-up
// has put the PSRAM into the heap (CONFIG_SPIRAM_USE_MALLOC, before app_main). No PSRAM for a block: the log line and
// ESP_ERR_NO_MEM, as a full table before; what was registered stays.
// One task registers at a time (app_main, as before); the console's task may look meanwhile (it starts first): a
// command is written before its block counts it, and a block is linked in before it is used, never moved or freed.
// Host tested (test/test_dev.c, on stubs). Other builds: nothing here, yaykit_app.c's table of 30 as before.
#include "sdkconfig.h"

#if CONFIG_YAY_DEV
#include "yk_app_dev.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#define BLOCK 16  // Yay's 42 in three blocks of 200 bytes

typedef struct block {
    struct block *next;
    int n;  // used, 0..BLOCK
    struct {
        const char *name, *help;
        yk_app_dev_fn fn;
    } cmd[BLOCK];
} block_t;

static const char *TAG = "yk_app";
static block_t *s_first, *s_last;  // s_last: the registering task's own

esp_err_t yk_app_dev_register(const char *name, const char *help, yk_app_dev_fn fn) {
    block_t *b = s_last;
    if (!b || b->n == BLOCK) {
        block_t *more = heap_caps_calloc(1, sizeof(*more), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!more) {
            ESP_LOGE(TAG, "no room for `yay %s` (no PSRAM for %u bytes)", name, (unsigned)sizeof(*more));
            return ESP_ERR_NO_MEM;
        }
        __atomic_store_n(b ? &b->next : &s_first, more, __ATOMIC_RELEASE);
        s_last = b = more;
    }
    const int i = b->n;
    b->cmd[i].name = name, b->cmd[i].help = help, b->cmd[i].fn = fn;
    __atomic_store_n(&b->n, i + 1, __ATOMIC_RELEASE);
    return ESP_OK;
}

static const block_t *first(void) { return __atomic_load_n(&s_first, __ATOMIC_ACQUIRE); }
static const block_t *next(const block_t *b) { return __atomic_load_n(&b->next, __ATOMIC_ACQUIRE); }
static int used(const block_t *b) { return __atomic_load_n(&b->n, __ATOMIC_ACQUIRE); }

yk_app_dev_fn yk_app_dev_find(const char *name) {
    for (const block_t *b = first(); b; b = next(b))
        for (int i = 0, n = used(b); i < n; i++)
            if (b->cmd[i].name && !strcmp(b->cmd[i].name, name)) return b->cmd[i].fn;
    return NULL;
}

int yk_app_dev_help(FILE *out) {
    int count = 0;
    for (const block_t *b = first(); b; b = next(b))
        for (int i = 0, n = used(b); i < n; i++)
            if (b->cmd[i].name) {
                fprintf(out, " | %s", b->cmd[i].help ? b->cmd[i].help : b->cmd[i].name);
                count++;
            }
    return count;
}
#endif
