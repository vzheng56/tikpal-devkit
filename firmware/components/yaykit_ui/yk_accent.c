// The accent (yk_accent.h).
#include "yk_accent.h"

#include <string.h>

#include "yk_accent_table.h"

static volatile int s_colorway = -1;  // not read yet

/** The original when nothing reads the shared setting (host tests); yaykit_ui.c's reads it on the device. */
__attribute__((weak)) int yk_accent_stored(void) { return 0; }

static int current(void) {
    int c = s_colorway;
    if (c < 0) {
        c = yk_accent_stored();
        if (c < 0 || c >= YK_ACCENT_COUNT) c = 0;
        s_colorway = c;
    }
    return c;
}

uint32_t yk_accent(void) { return YK_ACCENT_TABLE[current()].accent; }
uint32_t yk_accent_text(void) { return YK_ACCENT_TABLE[current()].text; }
uint32_t yk_accent_on(void) { return YK_ACCENT_TABLE[current()].on; }
int yk_accent_colorway(void) { return current(); }

void yk_accent_use(int colorway) { s_colorway = colorway >= 0 && colorway < YK_ACCENT_COUNT ? colorway : 0; }

int yk_accent_find(const char *name) {
    for (int i = 0; name && i < YK_ACCENT_COUNT; i++)
        if (!strcmp(name, YK_ACCENT_TABLE[i].name)) return i;
    return -1;
}

const char *yk_accent_name(int colorway) {
    return YK_ACCENT_TABLE[colorway >= 0 && colorway < YK_ACCENT_COUNT ? colorway : 0].name;
}
