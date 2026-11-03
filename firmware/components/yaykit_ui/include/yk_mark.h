// The Tikpal mark (design/tikpal/brand/mark/mark-on-dark.svg), drawn with two LVGL arcs instead of an image: a full
// ring in the track colour and a coral arc with round ends from 12 o'clock clockwise for 250 degrees. Crisp at any
// size, nothing in flash. Where it goes (0.1.12, docs/delivery/2026-10-30-yay-0.1.12.md 3): Yay's pairing page,
// Settings › 关于 and › 更新 (yay_mark_pages.h, yay_update_view.h), the recovery's update and install pages
// (launcher_mark.h); the boot animation stays the buddy. Its size: yk_mark_fit.h. Added beside the rest of yaykit_ui:
// nothing else changed (YK_API_LEVEL as it was).
#pragma once

#include "lvgl.h"
#include "yk_mark_fit.h"  // YK_MARK_PX: its size on those pages

#ifdef __cplusplus
extern "C" {
#endif

#define YK_MARK_CORAL 0xE5826A  // the mark's own colours (not the theme's YK_CORAL): the brand's file
#define YK_MARK_TRACK 0x2A332E

/** The mark centred on (cx, cy), `size` pixels across its outer edge (the SVG's 700 of 1024). Returns its container
 *  (not clickable), a child of `parent`. */
lv_obj_t *yk_ui_mark(lv_obj_t *parent, int cx, int cy, int size);

#ifdef __cplusplus
}
#endif
