// Host test of yk_app_dev.c itself, on stubs (stub_dev/): development builds' `yay <name>` commands, as many as are
// registered (Yay's development build registers 42; a table of 40 left two out), each block from PSRAM, and no PSRAM
// for a block: ESP_ERR_NO_MEM and the log line, what was registered kept. Run: bash test/run.sh
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../yk_app_dev.h"
#include "esp_heap_caps.h"

static int failures, checks;
#define CHECK(cond)                                               \
    do {                                                          \
        checks++;                                                 \
        if (!(cond)) {                                            \
            failures++;                                           \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
        }                                                         \
    } while (0)

// ---------------------------------------------------------------- stubs

static int s_allocs, s_not_psram, s_fail_next;
static size_t s_block_bytes;
void *heap_caps_calloc(size_t n, size_t size, unsigned caps) {
    if (!(caps & MALLOC_CAP_SPIRAM) || (caps & MALLOC_CAP_INTERNAL)) s_not_psram++;
    if (s_fail_next) {
        s_fail_next = 0;
        return NULL;
    }
    s_allocs++;
    s_block_bytes = n * size;
    return calloc(n, size);
}

static int s_errors;
static char s_last_error[200];
void stub_log(char level, const char *tag, const char *fmt, ...) {
    if (level != 'E' || strcmp(tag, "yk_app")) return;
    s_errors++;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_last_error, sizeof(s_last_error), fmt, ap);
    va_end(ap);
}

// ---------------------------------------------------------------- the commands

// Four commands, each answers with its own number and the argument count, to tell which one ran and with what.
static int cmd0(int argc, char **argv) { return (void)argv, 1000 + argc; }
static int cmd1(int argc, char **argv) { return (void)argv, 2000 + argc; }
static int cmd2(int argc, char **argv) { return (void)argv, 3000 + argc; }
static int cmd3(int argc, char **argv) { return (void)argv, 4000 + argc; }
static const yk_app_dev_fn CMDS[] = {cmd0, cmd1, cmd2, cmd3};

#define MANY 112  // seven blocks of 16, full: 42 is Yay's (11-21), the 40 of before far behind
static char s_names[MANY][8], s_helps[MANY][24];

/** What yk_app_dev_help() wrote, NUL-terminated (a static buffer). */
static const char *help_text(int *count) {
    static char text[8192];
    FILE *f = tmpfile();
    *count = yk_app_dev_help(f);
    rewind(f);
    const size_t n = fread(text, 1, sizeof(text) - 1, f);
    text[n] = 0;
    fclose(f);
    return text;
}

static int run(const char *name) {
    const yk_app_dev_fn fn = yk_app_dev_find(name);
    char *argv[] = {(char *)name, "x", NULL};
    return fn ? fn(2, argv) : -1;
}

int main(void) {
    int count;

    // Nothing registered yet: nothing found, an empty list.
    CHECK(yk_app_dev_find("state") == NULL);
    CHECK(!strcmp(help_text(&count), "") && count == 0);
    CHECK(s_allocs == 0);

    // No PSRAM for the first block: ESP_ERR_NO_MEM and the log line, nothing registered, nothing taken.
    s_fail_next = 1;
    CHECK(yk_app_dev_register("first", "first [x]", cmd0) == ESP_ERR_NO_MEM);
    CHECK(s_errors == 1 && strstr(s_last_error, "no room for `yay first`"));
    CHECK(yk_app_dev_find("first") == NULL);
    CHECK(!strcmp(help_text(&count), "") && count == 0);
    CHECK(s_allocs == 0);

    // 100 of them, every one registered: one block of PSRAM per 16, never internal RAM.
    for (int i = 0; i < MANY; i++) {
        snprintf(s_names[i], sizeof(s_names[i]), "c%d", i);
        snprintf(s_helps[i], sizeof(s_helps[i]), "c%d [arg %d]", i, i);
    }
    int failed = 0;
    for (int i = 0; i < 100; i++)
        if (yk_app_dev_register(s_names[i], i % 7 == 3 ? NULL : s_helps[i], CMDS[i % 4]) != ESP_OK) failed++;
    CHECK(failed == 0);
    CHECK(s_errors == 1);
    CHECK(s_allocs == 7);  // 16 each: 6 full and 4 in the seventh
    CHECK(s_not_psram == 0);
    CHECK(s_block_bytes >= 16 * 3 * sizeof(void *) && s_block_bytes <= 16 * 3 * sizeof(void *) + 32);

    // Each one found, the right command run with what follows its name; the 41st and 42nd too (badge, folders).
    int wrong = 0;
    for (int i = 0; i < 100; i++)
        if (yk_app_dev_find(s_names[i]) != CMDS[i % 4] || run(s_names[i]) != (i % 4 + 1) * 1000 + 2) wrong++;
    CHECK(wrong == 0);
    CHECK(run("c40") == 1002 && run("c41") == 2002);
    // Whole names only.
    CHECK(yk_app_dev_find("c100") == NULL);
    CHECK(yk_app_dev_find("c1x") == NULL);
    CHECK(yk_app_dev_find("c") == NULL);
    CHECK(yk_app_dev_find("") == NULL);
    CHECK(yk_app_dev_find("first") == NULL);

    // The list: each in the order registered, its name when it has no help.
    char want[8192] = "";
    for (int i = 0; i < 100; i++) {
        strcat(want, " | ");
        strcat(want, i % 7 == 3 ? s_names[i] : s_helps[i]);
    }
    const char *got = help_text(&count);
    CHECK(count == 100);
    CHECK(!strcmp(got, want));
    if (strcmp(got, want)) printf("  got  %.200s\n  want %.200s\n", got, want);

    // Filled to the end of the seventh block, then no PSRAM for the eighth: ESP_ERR_NO_MEM, the log line, the 112
    // before it all still there.
    failed = 0;
    for (int i = 100; i < MANY; i++)
        if (yk_app_dev_register(s_names[i], s_helps[i], CMDS[i % 4]) != ESP_OK) failed++;
    CHECK(failed == 0 && s_allocs == 7);
    s_fail_next = 1;
    CHECK(yk_app_dev_register("late", "late [now]", cmd3) == ESP_ERR_NO_MEM);
    CHECK(s_errors == 2 && strstr(s_last_error, "no room for `yay late`"));
    CHECK(yk_app_dev_find("late") == NULL);
    wrong = 0;
    for (int i = 0; i < MANY; i++)
        if (yk_app_dev_find(s_names[i]) != CMDS[i % 4]) wrong++;
    CHECK(wrong == 0);
    help_text(&count);
    CHECK(count == MANY);

    // PSRAM again: the next one goes into an eighth block.
    CHECK(yk_app_dev_register("late", "late [now]", cmd3) == ESP_OK);
    CHECK(s_allocs == 8 && s_not_psram == 0);
    CHECK(run("late") == 4002);
    got = help_text(&count);
    CHECK(count == MANY + 1);
    CHECK(strlen(got) > 13 && !strcmp(got + strlen(got) - 13, " | late [now]"));  // the last

    // A name twice, as before: the first one runs, the list shows both.
    CHECK(yk_app_dev_register("c5", "c5 again", cmd0) == ESP_OK);
    CHECK(yk_app_dev_find("c5") == CMDS[5 % 4]);
    got = help_text(&count);
    CHECK(count == MANY + 2 && strstr(got, " | c5 [arg 5]") && strstr(got, " | c5 again"));
    // A NULL name is skipped, as an empty slot was.
    CHECK(yk_app_dev_register(NULL, "nameless", cmd0) == ESP_OK);
    got = help_text(&count);
    CHECK(count == MANY + 2 && !strstr(got, "nameless"));
    CHECK(s_errors == 2);

    printf("yk_app_dev: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
