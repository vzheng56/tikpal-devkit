// The Tikpal mark (yk_mark.h). The SVG: a ring of radius 300 and stroke 100 in a 1024 box, so 700 across its outer
// edge; the coral arc from (512, 212) to (230.09, 614.61), that is from 270 to 160 degrees clockwise in LVGL's angles
// (0 at 3 o'clock, clockwise), 250 degrees, round ends.
#include "yk_mark.h"

static lv_obj_t *arc(lv_obj_t *parent, int size, int width, uint32_t color, int start, int end, bool rounded) {
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_remove_style_all(a);
    lv_obj_set_size(a, size, size);
    lv_obj_center(a);
    lv_arc_set_bg_angles(a, start, end);
    lv_arc_set_angles(a, start, end);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, rounded, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_clickable(a, false);
    return a;
}

lv_obj_t *yk_ui_mark(lv_obj_t *parent, int cx, int cy, int size) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, size, size);
    lv_obj_set_pos(box, cx - size / 2, cy - size / 2);
    lv_obj_set_clickable(box, false);
    lv_obj_set_scrollable(box, false);
    const int width = (size * 100 + 350) / 700;  // the stroke, rounded
    arc(box, size, width, YK_MARK_TRACK, 0, 360, false);
    arc(box, size, width, YK_MARK_CORAL, 270, 160, true);
    return box;
}
