// The accent (Yay 0.1.19 开发文档 4.7): the account's colourway on the round screen, as packages/yay-art accents.json's
// dark set has it (generated/yk_accent_table.h, the table the apps read). It is the shared setting the main firmware keeps
// (board_pref_colorway: hello.ok owner.colorway, colorway.set), read once and kept; the original until one is written.
// Every firmware draws with it: Yay, and an extension once rebuilt (its settings ring too).
//
// What it colours, and what stays coral (YK_CORAL): the main action, the talk button, what is selected and progress take
// the accent; what waits for the user, goes wrong or is new (the update dot, battery low, errors, 全部更新) stays coral.
// On the settings ring the accent only draws thin lines (yk_accent_text) and coral only solid shapes, so the original
// accent, which is coral's own colour, still tells apart from a warning. Pure but for the shared setting: host tested.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** dark.accent, 0xRRGGBB: filled, the main action's button, the talk button. */
uint32_t yk_accent(void);
/** dark.accentText: a thin line or small words on the dark ground (the settings ring's level arcs, the dial's scale). */
uint32_t yk_accent_text(void);
/** dark.onAccent: words on yk_accent(). */
uint32_t yk_accent_on(void);
/** The colourway it is, BOARD_COLORWAY_* (0 the original). */
int yk_accent_colorway(void);

/** The main firmware, once it has kept a new one in the shared setting: from now on (no NVS read). */
void yk_accent_use(int colorway);
/** A colourway by its name ("honey": 2); -1 for one this build does not know (a later one): it keeps what it has. */
int yk_accent_find(const char *name);
/** Its name; "original" out of range. */
const char *yk_accent_name(int colorway);

/** The shared setting, read the first time the accent is asked for (yaykit_ui.c: board_pref_colorway). */
int yk_accent_stored(void);

#ifdef __cplusplus
}
#endif
