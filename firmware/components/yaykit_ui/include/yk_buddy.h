// A buddy's picture pack, read for drawing (Yay 0.1.18 开发文档 4.9; 开发包四个伙伴-需求确认.md §8): the newest buddy slot
// that holds it, else the TF card, else none, and Pip is drawn, who needs no pack. Read only: nothing here writes a slot
// or the card. The pack goes into PSRAM (about 630 KB) and is checked whole (its head's sha256, then yk_figure_read)
// before anyone draws from it; read it only when a buddy is really to be drawn, and free it once nothing draws from it.
//
// The card is read through a function the firmware registers (yk_buddy_card_set), so that yaykit_ui does not need
// yaykit_tf: most firmwares that draw with yaykit_ui have no card and no FatFs (yaykit_tf.c stops a build without its
// FatFs settings). With none registered the card is skipped.
//
// 0.1.18: for Yay alone (its home screen and its app ring, one pack for both). From the development kit 1.10
// (YK_API_LEVEL 5) an extension's Yay draws the user's buddy through it (yk_yay_buddy): the slots only, as no extension
// registers a card reader (it has no way to the system's part of the card, nor needs `tf_card` for this).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yk_buddy_slot.h"
#include "yk_figure.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { YK_BUDDY_FROM_NONE, YK_BUDDY_FROM_SLOT, YK_BUDDY_FROM_CARD } yk_buddy_from_t;

/** A pack read whole and checked. */
typedef struct {
    uint8_t *bytes;     // PSRAM, the caller's (yk_buddy_pack_free); NULL when none
    uint32_t size;
    uint8_t sha[32];
    yk_figure_t figure;  // points into `bytes`
    yk_buddy_from_t from;
    int slot;            // FROM_SLOT's, else -1
    const char *why;     // none: a word for the log (NO_SLOT, NOT_ON_CARD, SHA256, a pack's own, LVGL_TASK, NO_MEM, ...)
} yk_buddy_pack_t;

/** The card's side: the newest pack of `id` on the card, read whole into PSRAM (`*bytes`, the caller's, freed with
 *  heap_caps_free) and checked against the sha256 it was stored with, given in `sha`. False: none, no card, Bluetooth on.
 *  Called on the task that called yk_buddy_load, which therefore has the stack the card needs (4 KB). */
typedef bool (*yk_buddy_card_fn)(const char *id, uint8_t **bytes, uint32_t *size, uint8_t sha[32]);
/** Yay 0.1.18 registers yay_tf.c's at start. NULL: no card. */
void yk_buddy_card_set(yk_buddy_card_fn read);

/** Both heads as they are on flash. False (all empty): no `models` partition, or one too small for the slots. */
bool yk_buddy_slots_read(yk_buddy_slots_t *out);

/** The pack in `slot` (as `slots` says it is), whole: read a sector at a time into PSRAM, against its head's sha256, its
 *  table read and its figure the head's buddy. */
bool yk_buddy_load_slot(const yk_buddy_slots_t *slots, int slot, yk_buddy_pack_t *out);

/** `buddy`'s pack: the newest slot holding it, else the card, else none (out->why says why; Pip is always none). Not on
 *  the LVGL task, where a card's second would stall the screen: false there, why LVGL_TASK. */
bool yk_buddy_load(int buddy, yk_buddy_pack_t *out);

void yk_buddy_pack_free(yk_buddy_pack_t *p);

#ifdef __cplusplus
}
#endif
