// The Tikpal mark's geometry, its size on the 0.1.12 pages and the checks their layouts are held to (yk_mark.h draws
// it): plain C, no LVGL, for the pages' host tests too. Where it goes: docs/delivery/2026-10-30-yay-0.1.12.md 3; its
// size is settled from the screenshots (8.3). 0.1.14: the 伴环 mark of the brand kit (docs/delivery/
// 2026-11-01-banhuan-logo-firmware.md 4), a ring and a dot, and its arrival (yk_mark_arrival, yk_mark_fit.c).
#pragma once

#include <math.h>
#include <stdbool.h>

#include "yk_round.h"

// The 伴环 mark on its 512 master (design/tikpal/2026-11-01-brand-kit-v1/01-master/geometry.json): a ring about
// (256, 256), its middle radius 146 and stroke 96, from -96 degrees counterclockwise to -355 with round ends; a dot
// about (389, 124), radius 61. The ring is 388 across its outer edge (62..450), and the dot stays inside that square
// (63..450). In the master's units, of those 388:
#define YK_MARK_UNITS 388      // the mark's `size` across: the ring's outer edge
#define YK_MARK_STROKE 96      // the ring's stroke (its round ends are half of it)
#define YK_MARK_ARC_START 5    // the ring in LVGL's angles (0 at 3 o'clock, clockwise): the master's arc, drawn the
#define YK_MARK_ARC_END 264    // other way round
#define YK_MARK_DOT_X 133      // the dot's centre from the ring's: right ...
#define YK_MARK_DOT_Y (-132)   // ... and up
#define YK_MARK_DOT_R 61       // its radius

/** The mark across its outer edge on the pages that show it. A trial build may set another
 *  (`build-yay.sh YK_MARK_PX=88`: yaykit_ui's CMakeLists passes it on). */
#ifndef YK_MARK_PX
#define YK_MARK_PX 64
#endif

/** The mark on a page with room for at most `most` px: YK_MARK_PX, or `most` when that is smaller. */
#define YK_MARK_UPTO(most) ((YK_MARK_PX) < (most) ? (YK_MARK_PX) : (most))

/** `units` of the master on a mark `size` px across, to the nearest pixel (halves away from zero): as yk_mark.c
 *  draws it. */
static inline int yk_mark_px(int units, int size) {
    const long n = (long)units * size;
    return (int)(n >= 0 ? (n + YK_MARK_UNITS / 2) / YK_MARK_UNITS : -((-n + YK_MARK_UNITS / 2) / YK_MARK_UNITS));
}

/** The circle of diameter `d` centred on (x, y) wholly inside the circle of `radius` about the screen's centre. */
static inline bool yk_mark_disc_within(int x, int y, int d, int radius) {
    const long dx = x - YK_CX, dy = y - YK_CY, r = radius - (d + 1) / 2;
    return r >= 0 && dx * dx + dy * dy <= r * r;
}

/** The mark of `size` centred on (cx, cy) wholly inside the circle of `radius` about the screen's centre
 *  (YK_R_CONTENT: the safe circle): the ring (`size` across) and the dot, which reaches past the ring's circle up and
 *  to the right (0.64 of `size` from the centre: yk_mark_reach). */
static inline bool yk_mark_within(int cx, int cy, int size, int radius) {
    return yk_mark_disc_within(cx, cy, size, radius) &&
           yk_mark_disc_within(cx + yk_mark_px(YK_MARK_DOT_X, size), cy + yk_mark_px(YK_MARK_DOT_Y, size),
                               yk_mark_px(2 * YK_MARK_DOT_R, size), radius);
}

/** How far the mark of `size` reaches from its centre at most: the dot's far edge, up and to the right. */
static inline float yk_mark_reach(int size) {
    const float dx = (float)yk_mark_px(YK_MARK_DOT_X, size), dy = (float)yk_mark_px(YK_MARK_DOT_Y, size);
    return sqrtf(dx * dx + dy * dy) + yk_mark_px(2 * YK_MARK_DOT_R, size) / 2.0f;
}

/** The mark's square (which holds it, the dot too) clear of the box (x, y, w, h), `gap` px apart at least. */
static inline bool yk_mark_clear(int cx, int cy, int size, int gap, int x, int y, int w, int h) {
    const int l = cx - size / 2 - gap, t = cy - size / 2 - gap, s = size + 2 * gap;
    return l + s <= x || x + w <= l || t + s <= y || y + h <= t;
}

/** The mark at a moment of its motion (design/tikpal/2026-11-01-brand-kit-v1/03-motion/state-spec.json): the ring's and
 *  the dot's scale (each about its own centre) and opacity (0..1), and the dot moved by (dot_x, dot_y) master units
 *  (x right, y down). yk_ui_mark_pose (yk_mark.h) shows one. */
typedef struct {
    float ring_scale, ring_opa;
    float dot_scale, dot_opa;
    float dot_x, dot_y;
} yk_mark_pose_t;

/** The mark at rest: as yk_ui_mark draws it, and where every motion ends. */
#define YK_MARK_REST ((yk_mark_pose_t){1, 1, 1, 1, 0, 0})

/** The ring as coverage, for drawing it straight into a frame's band instead of with an lv_arc (a 200 px lv_arc costs
 *  tens of ms a frame on the device: its masks live in PSRAM). Set up once per size with yk_mark_ring_init (the ring
 *  `outer` px across, about (0, 0)), then yk_mark_ring_cover per pixel. */
typedef struct {
    float mid, half;             // the stroke's middle radius and half its width, px
    float lo2, hi2;              // nothing nearer the centre than lo2 (squared radius) or farther than hi2
    float ex1, ey1, ex2, ey2;    // the round ends' centres
    float gx1, gy1, gx2, gy2;    // the opening's two edges (unit vectors, YK_MARK_ARC_END then YK_MARK_ARC_START)
} yk_mark_ring_t;

void yk_mark_ring_init(yk_mark_ring_t *r, float outer);

/** The ring's coverage 0..255 at (x, y) px from its centre (a pixel's centre; y down): the stroke from
 *  YK_MARK_ARC_START to YK_MARK_ARC_END and its round ends, a pixel of antialiasing at every edge. */
static inline unsigned yk_mark_ring_cover(const yk_mark_ring_t *r, float x, float y) {
    const float d2 = x * x + y * y;
    if (d2 > r->hi2 || d2 < r->lo2) return 0;
    float c;
    if (r->gx1 * y - r->gy1 * x > 0 && x * r->gy2 - y * r->gx2 > 0) {  // in the opening: only the round ends
        const float a = r->half + 0.5f - sqrtf((x - r->ex1) * (x - r->ex1) + (y - r->ey1) * (y - r->ey1));
        const float b = r->half + 0.5f - sqrtf((x - r->ex2) * (x - r->ex2) + (y - r->ey2) * (y - r->ey2));
        c = a > b ? a : b;
    } else {
        c = r->half + 0.5f - fabsf(sqrtf(d2) - r->mid);
    }
    return c <= 0 ? 0 : c >= 1 ? 255 : (unsigned)(c * 255.0f + 0.5f);
}

/** A disc of radius `rad` about (0, 0), its coverage at (x, y) the same way. */
static inline unsigned yk_mark_disc_cover(float rad, float x, float y) {
    const float d2 = x * x + y * y, out = rad + 0.5f;
    if (d2 >= out * out) return 0;
    const float c = out - sqrtf(d2);
    return c >= 1 ? 255 : (unsigned)(c * 255.0f + 0.5f);
}

#define YK_MARK_ARRIVAL_MS 900  // state-spec.json's arrival: once, not looped

/** The arrival `ms` after it began: state-spec.json's keyframes, each step eased cubic-bezier(0.42, 0, 0.58, 1) (LVGL's
 *  lv_anim_path_ease_in_out). Before 0 its first moment (nothing seen yet); from YK_MARK_ARRIVAL_MS on, YK_MARK_REST. */
yk_mark_pose_t yk_mark_arrival(int ms);
