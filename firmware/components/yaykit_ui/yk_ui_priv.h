// Inside yaykit_ui: what its files share (not for apps).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

/** yk_ui_set_english() was called with true. */
bool yk_ui_is_english(void);

/** A Yay for the system's own screens over an app, the app ring's middle (development kit 1.10): the user's buddy only if
 *  this run has its pack already (the app drew its buddy), else Pip; never a read of a pack there and then (about 0.1 s
 *  and 630 KB of PSRAM in an app that draws no buddy, the first time the ring opens). yk_yay.c. */
typedef struct yk_yay yk_yay_t;
yk_yay_t *yk_yay_create_kept(lv_obj_t *parent, int size, int cx, int cy, uint32_t paper);

// Drawing shared by the two rings (yk_apps.c, yk_ring.c) and 「我的应用」 (yk_more.c). `k` scales a mark made for the
// ring's old disc (r 38: YK_MARK_DISC_R) to a disc of another size; the rings place theirs with yk_rings_mark().
/** Whether area a reaches the band LVGL draws now (its clip area): the rings skip what does not. */
static inline bool yk_reaches(const lv_layer_t *layer, const lv_area_t *a) {
    const lv_area_t *c = &layer->_clip_area;
    return a->x1 <= c->x2 && c->x1 <= a->x2 && a->y1 <= c->y2 && c->y1 <= a->y2;
}
/** A filled circle. */
void yk_draw_disc(lv_layer_t *layer, int cx, int cy, float r, uint32_t color, lv_opa_t opa);
/** The sage ring of what is in use: 3 px (times k), just outside a disc of radius r (times k). */
void yk_draw_in_use(lv_layer_t *layer, int cx, int cy, float r, float k, lv_opa_t opa);
/** The sage ring of a place under the finger (yk_rings.h): YK_RINGS_PRESS_W wide, inside the edge of a disc of radius r. */
void yk_draw_pressed(lv_layer_t *layer, int cx, int cy, float r, lv_opa_t opa);
/** The same in `color` (the settings ring's: the account's accent, a thin line as its levels, 0.1.20 4.8). */
void yk_draw_pressed_in(lv_layer_t *layer, int cx, int cy, float r, lv_opa_t opa, uint32_t color);
/** A line of text centred in `a`. */
void yk_draw_text(lv_layer_t *layer, const char *str, const lv_font_t *font, uint32_t color, lv_opa_t opa,
                  const lv_area_t *a);
/** What changes in the middle when it says something else (yk_rings.h): its words' block, not its disc or in-use ring. */
lv_area_t yk_rings_words_area(void);
/** The middle's words (yk_rings_words): n lines (at most 3), each in its font and colour, about the centre. */
void yk_draw_words(lv_layer_t *layer, int n, const char *const lines[], const lv_font_t *const fonts[],
                   const uint32_t colors[], lv_opa_t opa);
// The marks, centred at (x, y) (yk_rings_mark(), or 「我的应用」's own), at scale k.
/** To download: a sage dot with a paper arrow pointing down. */
void yk_draw_download_mark(lv_layer_t *layer, int x, int y, float k, lv_opa_t opa);
/** In the package cache: a hollow sage ring (paper inside). */
void yk_draw_cached_mark(lv_layer_t *layer, int x, int y, float k, lv_opa_t opa);
/** A newer version (0.1.12): a coral dot in a paper ring. */
void yk_draw_update_dot(lv_layer_t *layer, int x, int y, float k, lv_opa_t opa);
/** Its 应用码 (voice-code handoff 2.1②): a sage dot in a paper ring with a paper phone. */
void yk_draw_phone_mark(lv_layer_t *layer, int x, int y, float k, lv_opa_t opa);
