// The Tikpal mark's size on the 0.1.12 pages and the checks their layouts are held to (yk_mark.h draws it): plain C,
// no LVGL, for the pages' host tests too. Where it goes: docs/delivery/2026-10-30-yay-0.1.12.md 3; its size is settled
// from the screenshots (8.3).
#pragma once

#include <stdbool.h>

#include "yk_round.h"

/** The mark across its outer edge on the pages that show it. A trial build may set another
 *  (`build-yay.sh YK_MARK_PX=88`: yaykit_ui's CMakeLists passes it on). */
#ifndef YK_MARK_PX
#define YK_MARK_PX 64
#endif

/** The mark on a page with room for at most `most` px: YK_MARK_PX, or `most` when that is smaller. */
#define YK_MARK_UPTO(most) ((YK_MARK_PX) < (most) ? (YK_MARK_PX) : (most))

/** The mark of `size` centred on (cx, cy) wholly inside the circle of `radius` about the screen's centre
 *  (YK_R_CONTENT: the safe circle). */
static inline bool yk_mark_within(int cx, int cy, int size, int radius) {
    const long dx = cx - YK_CX, dy = cy - YK_CY, r = radius - (size + 1) / 2;
    return r >= 0 && dx * dx + dy * dy <= r * r;
}

/** The mark's square (which holds it) clear of the box (x, y, w, h), `gap` px apart at least. */
static inline bool yk_mark_clear(int cx, int cy, int size, int gap, int x, int y, int w, int h) {
    const int l = cx - size / 2 - gap, t = cy - size / 2 - gap, s = size + 2 * gap;
    return l + s <= x || x + w <= l || t + s <= y || y + h <= t;
}
