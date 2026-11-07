// The Tikpal mark (yk_mark.h): the 伴环 mark of the brand kit (0.1.14). In a box `size` across: the ring an LVGL arc
// with round ends in the theme's coral, the dot a circle in its sage; the geometry in the master's units is
// yk_mark_fit.h's. A pose only resizes and moves the two (and sets their own opacity): no transform, no layer.
#include "yk_mark.h"

#include <math.h>

static lv_opa_t opa(float v) { return (lv_opa_t)lroundf(255.0f * (v < 0 ? 0 : v > 1 ? 1 : v)); }

static void place(lv_obj_t *ring, lv_obj_t *dot, int size, const yk_mark_pose_t *p) {
    // The ring about the box's centre: its size keeps the box's parity, so the centre stays on the same pixel.
    const int rs = size - 2 * (int)lroundf((float)size * (1 - p->ring_scale) / 2);
    lv_obj_set_size(ring, rs, rs);
    lv_obj_set_pos(ring, (size - rs) / 2, (size - rs) / 2);
    lv_obj_set_style_arc_width(ring, yk_mark_px(YK_MARK_STROKE, rs), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(ring, opa(p->ring_opa), LV_PART_INDICATOR);
    // The dot about its own centre, moved in master units; at rest where yk_mark_within looks for it.
    const int d = (int)lroundf(2.0f * YK_MARK_DOT_R * (float)size * p->dot_scale / YK_MARK_UNITS);
    const int x = size / 2 + yk_mark_px(YK_MARK_DOT_X, size) + (int)lroundf(p->dot_x * (float)size / YK_MARK_UNITS);
    const int y = size / 2 + yk_mark_px(YK_MARK_DOT_Y, size) + (int)lroundf(p->dot_y * (float)size / YK_MARK_UNITS);
    lv_obj_set_size(dot, d, d);
    lv_obj_set_pos(dot, x - d / 2, y - d / 2);
    lv_obj_set_style_bg_opa(dot, opa(p->dot_opa), 0);
}

lv_obj_t *yk_ui_mark(lv_obj_t *parent, int cx, int cy, int size) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, size, size);
    lv_obj_set_pos(box, cx - size / 2, cy - size / 2);
    lv_obj_set_clickable(box, false);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_overflow_visible(box, true);  // the arrival moves the dot a little past the box
    lv_obj_t *ring = lv_arc_create(box);
    lv_obj_remove_style_all(ring);
    lv_arc_set_bg_angles(ring, YK_MARK_ARC_START, YK_MARK_ARC_END);
    lv_arc_set_angles(ring, YK_MARK_ARC_START, YK_MARK_ARC_END);
    lv_obj_set_style_arc_color(ring, lv_color_hex(YK_CORAL), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_clickable(ring, false);
    lv_obj_t *dot = lv_obj_create(box);
    lv_obj_remove_style_all(dot);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(YK_SAGE), 0);
    lv_obj_set_clickable(dot, false);
    lv_obj_set_scrollable(dot, false);
    place(ring, dot, size, &YK_MARK_REST);
    return box;
}

void yk_ui_mark_pose(lv_obj_t *mark, const yk_mark_pose_t *pose) {
    lv_obj_t *ring = mark && pose ? lv_obj_get_child(mark, 0) : NULL, *dot = ring ? lv_obj_get_child(mark, 1) : NULL;
    if (!dot) return;
    place(ring, dot, (int)lv_obj_get_style_width(mark, LV_PART_MAIN), pose);
}
