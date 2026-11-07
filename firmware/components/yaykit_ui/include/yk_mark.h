// The Tikpal mark, drawn with LVGL instead of an image: crisp at any size, nothing in flash. Since 0.1.14 the 伴环
// mark of the brand kit (design/tikpal/2026-11-01-brand-kit-v1, 01-master/geometry.json; docs/delivery/
// 2026-11-01-banhuan-logo-firmware.md 4): a coral ring with round ends open at the top right, and a sage dot in the
// opening, in the theme's own colours. 0.1.12 to 0.1.13 drew the earlier ring and 250-degree arc (design/tikpal/brand/
// mark/mark-on-dark.svg). Where it goes (0.1.12, docs/delivery/2026-10-30-yay-0.1.12.md 3): Yay's pairing page,
// Settings › 关于 and › 更新 (yay_mark_pages.h, yay_update_view.h), the recovery's update and install pages
// (launcher_mark.h); from 0.1.14 also Yay's brand on a cold start (yay_brand.h). Its size and geometry: yk_mark_fit.h.
// Added beside the rest of yaykit_ui: nothing else changed (YK_API_LEVEL as it was).
#pragma once

#include "lvgl.h"
#include "yk_mark_fit.h"  // YK_MARK_PX: its size on those pages; the geometry; yk_mark_pose_t

#ifdef __cplusplus
extern "C" {
#endif

// The 0.1.12 mark's own colours. No longer used (the 伴环 mark is the theme's YK_CORAL and YK_SAGE); kept for code
// that names them.
#define YK_MARK_CORAL 0xE5826A
#define YK_MARK_TRACK 0x2A332E

/** The mark centred on (cx, cy), `size` pixels across the ring's outer edge (the master's 388 of 512), at rest.
 *  Returns its container (not clickable, `size` square, the dot inside it), a child of `parent`. */
lv_obj_t *yk_ui_mark(lv_obj_t *parent, int cx, int cy, int size);

/** A mark made by yk_ui_mark shown at a moment of its motion (yk_mark_arrival gives one; YK_MARK_REST puts it back).
 *  Only the ring and the dot are resized, moved and made fainter (their own opacity): nothing is transformed or drawn
 *  off screen, and only the mark's square (with as far as the dot moves past it) is drawn again. */
void yk_ui_mark_pose(lv_obj_t *mark, const yk_mark_pose_t *pose);

#ifdef __cplusplus
}
#endif
