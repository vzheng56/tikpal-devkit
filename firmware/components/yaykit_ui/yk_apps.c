// The app ring (yk_apps.h): one opaque full-screen object on top of the screen draws the discs, icons, marks and the
// middle's words itself (LV_EVENT_DRAW_MAIN), from the layout and poses of yk_apps_logic.c (its sizes yk_rings.h's,
// shared with the settings ring); Yay in the middle is yk_yay. A frame of the motion repaints only the boxes of what
// moved; LVGL skips whatever the paper covers. A finger down on a place (0.1.18) grows it and says its name in the
// middle; the same frames repaint the place and the middle, and the others once each as they dim.
#include "yk_apps.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cjk_font.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "yaykit_ui.h"
#include "yk_apps_logic.h"
#include "yk_figure.h"
#include "yk_hint.h"
#include "yk_round.h"
#include "yk_ui_priv.h"
#include "yk_yay.h"
#include "yk_words.h"

static const char *TAG = "yk_apps";

#define FRAME_MS 16       // while it moves
#define IDLE_CHECK_MS 250 // while it waits

typedef struct {
    char slug[32];
    char first[5];               // stands in for a missing icon
    bool installed, current;
    bool cached;                 // in the package cache (not in a slot): a hollow sage ring, not dimmed
    bool more;                   // 「更多」: three dots
    bool settings;               // 「设置」: a gear
    bool update;                 // a newer version (0.1.12): a coral dot on the upper right
    bool phone;                  // its 应用码 (voice-code handoff 2.1②): the phone mark on the upper right
    const lv_image_dsc_t *icon;  // the caller's, or `own`
    lv_image_dsc_t own;          // from the catalog: RGB565A8 in PSRAM, already dimmed when not installed
} sat_t;

static bool s_home_update;  // yk_apps_home_update(): kept across opens (`s` is cleared at each)
static bool s_has_figure;    // yk_apps_figure() gave a pack (s_words->figure)

/** What only an open ring or yk_apps_figure() needs, in PSRAM, made the first time and kept: the internal RAM is short
 *  (the pairing page leaves some boards 1-3 KB), and nothing here is drawn often enough to want it. */
typedef struct {
    char name[YK_APPS_MAX][2][64];  // each place's name for the middle while it is pressed: one line or two
    uint8_t lines[YK_APPS_MAX];
    char label[2][24];              // the middle's own words, while it is pressed: Tikpal, 回到 Tikpal
    uint8_t label_lines;
    yk_figure_t figure;             // yk_apps_figure()'s copy of the buddy's pack (its bytes are the caller's)
} words_t;
static words_t *s_words;

static words_t *words(void) {
    if (!s_words) s_words = heap_caps_calloc(1, sizeof(*s_words), MALLOC_CAP_SPIRAM);
    return s_words;
}
static yk_apps_guard_t s_guard;  // yk_apps_set_guard()
static void *s_guard_ctx;

static struct {
    lv_obj_t *root;
    yk_yay_t *yay;
    lv_timer_t *timer;
    yk_apps_layout_t L;
    yk_apps_pose_t pose[YK_APPS_MAX + 1];
    lv_area_t drawn[YK_APPS_MAX + 1];  // what each element covered last frame
    sat_t sat[YK_APPS_MAX];
    bool home;
    const lv_font_t *font, *first_font;  // the names (YK_RINGS_NAME_PX), the first characters
    int line_h;
    int pressed;                 // yk_rings_pressed() as last drawn: 1..n a place, 0 the middle, -1 none
    int said;                    // ... and what the middle says (the same values): it changes in a frame of its own
    uint32_t press_t0;
    uint32_t dim;                // the places drawn dimmed (yk_rings_press): bit i-1 for place i
    bool press_moving;           // the press still changing the look, a step a frame ...
    bool press_drawn;            // ... the last step drawn: the next may come
    uint8_t yay_opa;             // what Yay's opacity was set to (setting it again repaints it)
    yk_apps_cb_t cb;
    void *ctx;
    uint32_t t0, last_input;
    bool closing, moving, report;
    yk_apps_touch_t touch;  // the press now: a swipe is not a tap (round-screen-gestures.md 7)
    int8_t away_dx, away_dy;  // closing after a swipe: towards the finger (0, 0: folding back)
    bool has_result;
    char result[32];
    int64_t opened_us;
    int frames;
} s;

static yk_apps_stats_t s_stats;

// ---------------------------------------------------------------- where things are at a pose

static float k_of(const yk_apps_pose_t *p) { return p->scale / 256.0f; }

/** Text cannot be scaled cheaply: a name fades in over the last part of its disc's growth instead. */
static lv_opa_t text_opa(const yk_apps_pose_t *p, float k0) {
    const float k = k_of(p), f = k <= k0 ? 0 : k >= 1 ? 1 : (k - k0) / (1 - k0);
    return (lv_opa_t)lroundf(p->opa * f);
}

static void join(lv_area_t *a, const lv_area_t *b) {
    a->x1 = LV_MIN(a->x1, b->x1), a->y1 = LV_MIN(a->y1, b->y1);
    a->x2 = LV_MAX(a->x2, b->x2), a->y2 = LV_MAX(a->y2, b->y2);
}

static lv_area_t disc_area(int cx, int cy, float r) {
    const int R = (int)ceilf(r) + 1;
    return (lv_area_t){cx - R, cy - R, cx + R, cy + R};
}

/** The scale the marks go with: the ring's motion, not a press (the disc grows under them). */
static float mark_k(int e, const yk_apps_pose_t *p) { return e == s.pressed && !s.moving ? 1.0f : k_of(p); }

/** Everything element e (0: the middle, 1..n: an app) covers at pose p. */
static lv_area_t element_area(int e, const yk_apps_pose_t *p) {
    const float k = k_of(p);
    if (e == 0) {
        lv_area_t a = disc_area(p->cx, p->cy, (YK_RINGS_CENTER_R + YK_RINGS_IN_USE_GAP) * k);
        if (s_home_update) {
            int dx, dy;
            float r, edge;
            yk_apps_home_dot(p->cx, p->cy, k, &dx, &dy, &r, &edge);
            const lv_area_t dot = disc_area(dx, dy, edge);
            join(&a, &dot);
        }
        return a;
    }
    lv_area_t a = disc_area(p->cx, p->cy, (YK_RINGS_SAT_R + YK_RINGS_IN_USE_GAP) * k);
    const float km = mark_k(e, p);
    for (int m = YK_MARK_UPDATE; m <= YK_MARK_DOWNLOAD; m++) {  // every mark it may have (a box or two more)
        int mx, my;
        float outer;
        yk_rings_mark((yk_mark_kind_t)m, p->cx, p->cy, km, &mx, &my, &outer);
        const lv_area_t mark = disc_area(mx, my, outer);
        join(&a, &mark);
    }
    return a;
}

// ---------------------------------------------------------------- drawing

void yk_draw_disc(lv_layer_t *layer, int cx, int cy, float r, uint32_t color, lv_opa_t opa) {
    const int R = (int)lroundf(r);
    if (R <= 0) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(color);
    d.bg_opa = opa;
    d.radius = LV_RADIUS_CIRCLE;
    const lv_area_t a = {cx - R, cy - R, cx + R - 1, cy + R - 1};
    lv_draw_rect(layer, &d, &a);
}

void yk_draw_in_use(lv_layer_t *layer, int cx, int cy, float r, float k, lv_opa_t opa) {
    lv_draw_arc_dsc_t a;
    lv_draw_arc_dsc_init(&a);
    a.center.x = cx;
    a.center.y = cy;
    a.radius = (uint16_t)lroundf((r + 2) * k);
    a.width = LV_MAX(1, (int32_t)lroundf(YK_APPS_RING_W * k));
    a.start_angle = 0;
    a.end_angle = 360;
    a.color = lv_color_hex(YK_SAGE);
    a.opa = opa;
    lv_draw_arc(layer, &a);
}

void yk_draw_text(lv_layer_t *layer, const char *str, const lv_font_t *font, uint32_t color, lv_opa_t opa,
                  const lv_area_t *a) {
    if (!font || !str[0]) return;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = str;  // the caller's, as long as what it draws
    d.font = font;
    d.color = lv_color_hex(color);
    d.opa = opa;
    d.align = LV_TEXT_ALIGN_CENTER;
    lv_draw_label(layer, &d, a);
}

static void line(lv_layer_t *layer, float x1, float y1, float x2, float y2, float w, lv_opa_t opa) {
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1.x = x1, d.p1.y = y1, d.p2.x = x2, d.p2.y = y2;
    d.width = LV_MAX(1, (int32_t)lroundf(w));
    d.color = lv_color_hex(YK_PAPER);
    d.opa = opa;
    d.round_start = d.round_end = 1;
    lv_draw_line(layer, &d);
}

void yk_draw_download_mark(lv_layer_t *layer, int x, int y, float k, lv_opa_t opa) {
    const float bx = (float)x, by = (float)y;
    yk_draw_disc(layer, x, y, YK_APPS_BADGE_R * k, YK_SAGE, opa);
    if (k < 0.5f) return;
    line(layer, bx, by - 5 * k, bx, by + 3 * k, 2.4f * k, opa);
    line(layer, bx - 4 * k, by, bx, by + 4 * k, 2.4f * k, opa);
    line(layer, bx, by + 4 * k, bx + 4 * k, by, 2.4f * k, opa);
}

void yk_draw_cached_mark(lv_layer_t *layer, int x, int y, float k, lv_opa_t opa) {
    yk_draw_disc(layer, x, y, YK_APPS_CACHE_R * k, YK_SAGE, opa);
    yk_draw_disc(layer, x, y, (YK_APPS_CACHE_R - YK_APPS_CACHE_W) * k, YK_PAPER, opa);
}

void yk_draw_update_dot(lv_layer_t *layer, int x, int y, float k, lv_opa_t opa) {
    yk_draw_disc(layer, x, y, (YK_APPS_UPDATE_R + YK_APPS_UPDATE_EDGE) * k, YK_PAPER, opa);
    yk_draw_disc(layer, x, y, YK_APPS_UPDATE_R * k, YK_CORAL, opa);
}

/** The phone mark (voice-code handoff 2.1②): a sage disc in a paper ring, a paper phone on it (a rounded outline
 *  and a short line at its foot), like the download mark's sage and paper. */
void yk_draw_phone_mark(lv_layer_t *layer, int x, int y, float k, lv_opa_t opa) {
    yk_draw_disc(layer, x, y, (YK_APPS_PHONE_R + YK_APPS_PHONE_EDGE) * k, YK_PAPER, opa);
    yk_draw_disc(layer, x, y, YK_APPS_PHONE_R * k, YK_SAGE, opa);
    if (k < 0.5f) return;
    const int w = (int)lroundf(10 * k), h = (int)lroundf(16 * k);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_TRANSP;
    d.border_color = lv_color_hex(YK_PAPER);
    d.border_opa = opa;
    d.border_width = LV_MAX(1, (int32_t)lroundf(2 * k));
    d.radius = (int32_t)lroundf(3 * k);
    const lv_area_t body = {x - w / 2, y - h / 2, x - w / 2 + w - 1, y - h / 2 + h - 1};
    lv_draw_rect(layer, &d, &body);
    line(layer, x - 1.5f * k, y + 4.5f * k, x + 1.5f * k, y + 4.5f * k, 2 * k, opa);
}

/** 「设置」's gear (the canvas's 24-unit icon at 36 px): a ring, a hub and eight spokes, on the disc. */
static void draw_gear(lv_layer_t *layer, int cx, int cy, float k, lv_opa_t opa) {
    const float u = YK_APPS_GEAR_UNIT * k, w = 2 * u;
    lv_draw_arc_dsc_t a;
    lv_draw_arc_dsc_init(&a);
    a.center.x = cx;
    a.center.y = cy;
    a.width = LV_MAX(1, (int32_t)lroundf(w));
    a.start_angle = 0;
    a.end_angle = 360;
    a.color = lv_color_hex(YK_INK);
    a.opa = opa;
    a.radius = (uint16_t)lroundf(6.5f * u + w / 2);
    lv_draw_arc(layer, &a);
    a.radius = (uint16_t)lroundf(3 * u + w / 2);
    lv_draw_arc(layer, &a);
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.width = a.width;
    d.color = lv_color_hex(YK_INK);
    d.opa = opa;
    d.round_start = d.round_end = 1;
    for (int i = 0; i < 8; i++) {
        const float t = (float)i * 3.14159265f / 4, dx = sinf(t), dy = -cosf(t);
        d.p1.x = cx + 6.8f * u * dx, d.p1.y = cy + 6.8f * u * dy;
        d.p2.x = cx + 9.2f * u * dx, d.p2.y = cy + 9.2f * u * dy;
        lv_draw_line(layer, &d);
    }
}

void yk_draw_pressed(lv_layer_t *layer, int cx, int cy, float r, lv_opa_t opa) {
    yk_draw_pressed_in(layer, cx, cy, r, opa, YK_SAGE);
}

void yk_draw_pressed_in(lv_layer_t *layer, int cx, int cy, float r, lv_opa_t opa, uint32_t color) {
    lv_draw_arc_dsc_t a;
    lv_draw_arc_dsc_init(&a);
    a.center.x = cx;
    a.center.y = cy;
    a.radius = (uint16_t)lroundf(r);
    a.width = YK_RINGS_PRESS_W;
    a.start_angle = 0;
    a.end_angle = 360;
    a.color = lv_color_hex(color);
    a.opa = opa;
    lv_draw_arc(layer, &a);
}

lv_area_t yk_rings_words_area(void) {
    // The glyphs' block (120 wide, three lines at most), its corners inside the middle's in-use ring (74 from the
    // centre): repainting it repaints no part of that ring.
    const int hw = YK_RINGS_WORDS_W / 2, hh = 42;
    return (lv_area_t){YK_CX - hw, YK_CY - hh, YK_CX + hw - 1, YK_CY + hh - 1};
}

void yk_draw_words(lv_layer_t *layer, int n, const char *const lines[], const lv_font_t *const fonts[],
                   const uint32_t colors[], lv_opa_t opa) {
    int h[3] = {0};
    if (n > 3) n = 3;
    for (int i = 0; i < n; i++) h[i] = fonts[i] ? lv_font_get_line_height(fonts[i]) : YK_RINGS_NAME_PX + 8;
    yk_box_t b[3] = {{0}};
    yk_rings_words(n, h, b);
    for (int i = 0; i < n; i++) {
        // 4 px wider each side so LVGL never wraps a measured line
        const lv_area_t a = {b[i].x - 4, b[i].y, b[i].x + b[i].w + 3, b[i].y + b[i].h - 1};
        yk_draw_text(layer, lines[i], fonts[i], colors[i], opa, &a);
    }
}

static void draw_sat(lv_layer_t *layer, int i) {
    const yk_apps_pose_t *p = &s.pose[i + 1];
    if (p->opa <= LV_OPA_MIN) return;
    const sat_t *t = &s.sat[i];
    const float k = k_of(p), km = mark_k(i + 1, p);
    const bool local = t->installed || t->cached, pressed = i + 1 == s.pressed && !s.moving;
    yk_draw_disc(layer, p->cx, p->cy, YK_RINGS_SAT_R * k, YK_PANEL, p->opa);
    if (t->more) {
        for (int d = -1; d <= 1; d++) {
            const int x = p->cx + (int)lroundf(d * YK_APPS_DOT_GAP * k);
            yk_draw_disc(layer, x, p->cy, YK_APPS_DOT_R * k, YK_INK, p->opa);
        }
    } else if (t->settings) {
        draw_gear(layer, p->cx, p->cy, k, p->opa);
    } else if (t->icon) {
        lv_draw_image_dsc_t d;
        lv_draw_image_dsc_init(&d);
        d.src = t->icon;
        d.scale_x = d.scale_y = (uint16_t)lroundf(256 * km);  // the ring's motion, not a press: no scaled copy each frame
        d.pivot.x = YK_CATALOG_ICON / 2, d.pivot.y = YK_CATALOG_ICON / 2;
        // the catalog's icons are dimmed already; the caller's are dimmed here
        d.opa = local || t->icon == &t->own ? p->opa : (lv_opa_t)(p->opa * YK_APPS_DIM_OPA / 255);
        const lv_area_t a = {p->cx - YK_CATALOG_ICON / 2, p->cy - YK_CATALOG_ICON / 2, p->cx + YK_CATALOG_ICON / 2 - 1,
                             p->cy + YK_CATALOG_ICON / 2 - 1};
        lv_draw_image(layer, &d, &a);
    } else if (t->first[0] && s.first_font) {
        const int h = lv_font_get_line_height(s.first_font);
        const lv_area_t a = {p->cx - YK_RINGS_SAT_R, p->cy - h / 2, p->cx + YK_RINGS_SAT_R - 1, p->cy - h / 2 + h - 1};
        yk_draw_text(layer, t->first, s.first_font, local ? YK_INK : yk_rim_mix(YK_INK, YK_PANEL, YK_APPS_DIM_OPA),
             text_opa(p, 0.6f), &a);
    }
    if (pressed) yk_draw_pressed(layer, p->cx, p->cy, YK_RINGS_SAT_R * k, p->opa);
    else if (t->current) yk_draw_in_use(layer, p->cx, p->cy, YK_RINGS_SAT_R, k, p->opa);
    const bool own = t->more || t->settings;  // the ring's own places: no marks, never dimmed
    int mx, my;
    float outer;
    if (t->cached && !t->installed && !own) {
        yk_rings_mark(YK_MARK_CACHED, p->cx, p->cy, km, &mx, &my, &outer);
        yk_draw_cached_mark(layer, mx, my, km, p->opa);
    } else if (!local && !own) {
        yk_rings_mark(YK_MARK_DOWNLOAD, p->cx, p->cy, km, &mx, &my, &outer);
        yk_draw_download_mark(layer, mx, my, km, p->opa);
    }
    // A newer version (0.1.12): on an app in a slot, or on 「设置」 when Tikpal or any app has one (the main firmware's).
    if (t->update && (t->installed || t->settings)) {
        yk_rings_mark(YK_MARK_UPDATE, p->cx, p->cy, km, &mx, &my, &outer);
        yk_draw_update_dot(layer, mx, my, km, p->opa);
    } else if (t->phone) {
        yk_rings_mark(YK_MARK_PHONE, p->cx, p->cy, km, &mx, &my, &outer);
        yk_draw_phone_mark(layer, mx, my, km, p->opa);
    }
}

static void draw_cb(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    s.press_drawn = true;
    // LVGL draws a frame in bands of a few lines (CONFIG_YK_BOARD_LCD_BUFFER_LINES), calling this for each: what does
    // not reach the band is not even asked for (every draw call costs a task, reached or not: a press's frame of two
    // places took ~55 ms on the device before this, 0.1.18).
    for (int i = 0; i < s.L.n; i++)
        if (yk_reaches(layer, &s.drawn[i + 1])) draw_sat(layer, i);
    const yk_apps_pose_t *p = &s.pose[0];
    if (p->opa <= LV_OPA_MIN || !yk_reaches(layer, &s.drawn[0])) return;
    const float k = k_of(p);
    yk_draw_disc(layer, p->cx, p->cy, YK_RINGS_CENTER_R * k, YK_PANEL, p->opa);
    if (s.home) yk_draw_in_use(layer, p->cx, p->cy, YK_RINGS_CENTER_R, k, p->opa);
    if (s_home_update) {  // Tikpal has a newer version (the main firmware's)
        int x, y;
        float r, edge;
        yk_apps_home_dot(p->cx, p->cy, k, &x, &y, &r, &edge);
        yk_draw_disc(layer, x, y, edge, YK_PAPER, p->opa);
        yk_draw_disc(layer, x, y, r, YK_CORAL, p->opa);
    }
    // Pressed (the ring at rest): what is under the finger says its name here, in place of Yay.
    if (s.moving || s.said < 0) return;  // Yay is a child: drawn over this
    if (!s_words) return;  // no memory for the words: the middle stays as it is
    const sat_t *t = s.said > 0 ? &s.sat[s.said - 1] : NULL;
    const words_t *w = s_words;
    const int i = t ? s.said - 1 : 0;
    const char *lines[2] = {t ? w->name[i][0] : w->label[0], t ? w->name[i][1] : w->label[1]};
    const int n = t ? w->lines[i] : w->label_lines;
    const bool local = !t || t->installed || t->cached || t->more || t->settings;
    const uint32_t ink = (t ? t->current : s.home) ? YK_SAGE_TEXT : local ? YK_INK : YK_INK2;
    const lv_font_t *const fonts[2] = {s.font, s.font};
    const uint32_t colors[2] = {ink, ink};
    yk_draw_words(layer, n, lines, fonts, colors, p->opa);
}

// ---------------------------------------------------------------- motion

/** Takes the poses: repaints what moved, sizes and fades Yay. */
/** Takes the poses: repaints what moved (true when anything did), sizes and fades Yay. */
static bool apply(const yk_apps_pose_t pose[YK_APPS_MAX + 1]) {
    bool any = false;
    for (int e = 0; e <= s.L.n; e++) {
        if (!memcmp(&pose[e], &s.pose[e], sizeof(pose[e]))) continue;
        s.pose[e] = pose[e];
        const lv_area_t a = element_area(e, &pose[e]);
        lv_obj_invalidate_area(s.root, &s.drawn[e]);
        lv_obj_invalidate_area(s.root, &a);
        s.drawn[e] = a;
        any = true;
    }
    if (s.yay) {
        const float k = k_of(&s.pose[0]);
        yk_yay_size(s.yay, (int)lroundf(YK_RINGS_FIGURE * k));
        yk_yay_move(s.yay, s.pose[0].cx, s.pose[0].cy);
        // Gone while the middle says what is under the finger. Only when it changes: LVGL repaints an object whenever
        // a style is set, the same value or not (a press's every frame repainted Yay's square, 0.1.18).
        const uint8_t opa = !s.moving && s.said >= 0 ? LV_OPA_TRANSP : s.pose[0].opa;
        if (opa != s.yay_opa) {
            s.yay_opa = opa;
            lv_obj_set_style_opa(yk_yay_obj(s.yay), opa, 0);
        }
    }
    return any;
}

/** The press as it is now (yk_rings_pressed): a place under the finger grows and says its name in the middle, the
 *  others dim; the middle says its own words. Called on every touch event and from the timer while the ring is at
 *  rest, so a press shows within a frame, and one held past YK_TAP_HOLD_MS lets go of its place within a tick. */
static void press_update(void) {
    if (!s.root || s.moving || s.closing) return;
    const uint32_t now = lv_tick_get();
    const int pressed = yk_rings_pressed(&s.touch, now);
    // Each frame of a press repaints little (a frame of all five places and the middle took ~70 ms, 0.1.18): first the
    // places alone (the one pressed gets its ring, the one let go its size back), then the middle alone (its words
    // and Yay's square), then a step at a time (the pressed one grows, one other dims or lights).
    if (pressed != s.pressed) {
        yk_apps_pose_t pose[YK_APPS_MAX + 1];
        memcpy(pose, s.pose, sizeof(pose));
        const int was = s.pressed;
        bool any = false;
        if (was > 0) {
            yk_apps_pose_t rest[YK_APPS_MAX + 1];
            yk_apps_frame(&s.L, YK_APPS_OPEN_MS, false, rest);
            pose[was].scale = rest[was].scale;
            lv_obj_invalidate_area(s.root, &s.drawn[was]);
            any = true;
        }
        s.pressed = pressed;
        apply(pose);
        if (pressed > 0) lv_obj_invalidate_area(s.root, &s.drawn[pressed]), any = true;
        s.press_moving = true;
        s.press_drawn = !any;  // nothing to draw: the middle at once
        lv_timer_set_period(s.timer, FRAME_MS);
        return;
    }
    // A step a drawn frame (the timer can tick twice before LVGL draws one).
    if (!s.press_moving || !s.press_drawn) return;
    if (s.said != s.pressed) {
        s.said = s.pressed;
        const lv_area_t words = yk_rings_words_area();  // not the whole middle (its disc and ring stay as they are)
        lv_obj_invalidate_area(s.root, &words);
        apply(s.pose);  // Yay's opacity: LVGL repaints its square
        s.press_t0 = now;  // the pressed place grows from now
        s.press_drawn = false;
        return;
    }
    yk_apps_pose_t pose[YK_APPS_MAX + 1];
    yk_apps_frame(&s.L, YK_APPS_OPEN_MS, false, pose);
    const bool still = yk_rings_press(s.L.n, s.pressed, (int)(now - s.press_t0), &s.dim, pose);
    if (apply(pose)) s.press_drawn = false;  // nothing to draw: the next step at once
    if (still) {
        s.press_moving = false;
        lv_timer_set_period(s.timer, IDLE_CHECK_MS);
    }
}

static void finish(void);

/** Closes it: folding back into the centre, or with (dx, dy) towards the finger after a swipe. */
static void start_close(bool report, const char *result, int dx, int dy) {
    if (s.closing) return;
    s.closing = true;
    yk_apps_touch_closing(&s.touch);
    s.away_dx = (int8_t)dx, s.away_dy = (int8_t)dy;
    s.moving = true;
    s.report = report;
    s.has_result = result != NULL;
    if (result) snprintf(s.result, sizeof(s.result), "%s", result);
    s.t0 = lv_tick_get();
    lv_timer_set_period(s.timer, FRAME_MS);
    lv_timer_reset(s.timer);
}

static void tick(lv_timer_t *t) {
    if (!s.root) return;
    // Stay on top of what the screen gets meanwhile.
    if (lv_obj_get_index(s.root) != (int32_t)lv_obj_get_child_count(lv_obj_get_parent(s.root)) - 1)
        lv_obj_move_to_index(s.root, -1);
    if (s.moving) {
        yk_apps_pose_t pose[YK_APPS_MAX + 1];
        const int t_ms = (int)lv_tick_elaps(s.t0);
        const bool done = s.closing && (s.away_dx || s.away_dy)
                              ? yk_apps_frame_away(&s.L, t_ms, s.away_dx, s.away_dy, pose)
                              : yk_apps_frame(&s.L, t_ms, s.closing, pose);
        apply(pose);
        if (!s.closing) s.frames++;
        if (!done) return;
        s.moving = false;
        if (s.closing) {
            finish();
            return;
        }
        s_stats.open_ms = (int)((esp_timer_get_time() - s.opened_us) / 1000);
        s_stats.frames = s.frames;
        lv_timer_set_period(t, IDLE_CHECK_MS);
        press_update();  // a finger that went down while it opened
    } else if (lv_tick_elaps(s.last_input) >= YK_APPS_IDLE_MS) {
        start_close(true, NULL, 0, 0);
    } else {
        press_update();
    }
}

/** The finger's point now (0, 0 without one). */
static lv_point_t finger(void) {
    lv_point_t p = {0};
    lv_indev_t *indev = lv_indev_active();
    if (indev) lv_indev_get_point(indev, &p);
    return p;
}

static void on_press(lv_event_t *e) {
    (void)e;
    s.last_input = lv_tick_get();
    const lv_point_t p = finger();
    yk_apps_touch_press(&s.touch, yk_apps_hit(&s.L, p.x, p.y), p.x, p.y, s.last_input);
    press_update();
}

static void on_pressing(lv_event_t *e) {
    (void)e;
    const lv_point_t p = finger();
    yk_apps_touch_move(&s.touch, p.x, p.y);
    press_update();
}

/** A swipe anywhere on it: closes towards the finger, nothing tapped. Its release goes nowhere (LVGL would still
 *  send CLICKED for it, at wherever the finger lets go). */
static void on_gesture(lv_event_t *e) {
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    const lv_dir_t d = lv_indev_get_gesture_dir(indev);
    const int dx = d == LV_DIR_LEFT ? -1 : d == LV_DIR_RIGHT ? 1 : 0;
    const int dy = d == LV_DIR_TOP ? -1 : d == LV_DIR_BOTTOM ? 1 : 0;
    const yk_apps_go_t go = yk_apps_touch_swipe(&s.touch, dx, dy);
    if (go.kind != YK_APPS_GO_AWAY) return;
    lv_indev_wait_release(indev);
    s.last_input = lv_tick_get();
    start_close(true, NULL, go.dx, go.dy);
    ESP_LOGI(TAG, "swipe %s: closes, nothing tapped",
             go.dx < 0 ? "left" : go.dx > 0 ? "right" : go.dy < 0 ? "up" : "down");
}

/** The finger let go: a tap (yk_tap: down and up on the same app, still, not held) on what it hit, else nothing. */
static void on_click(lv_event_t *e) {
    (void)e;
    const lv_point_t p = finger();
    const yk_apps_go_t go = yk_apps_touch_click(&s.touch, &s.L, p.x, p.y, lv_tick_get());
    char line[64];
    puts(yk_tap_line(&s.touch.tap, "apps", line, sizeof(line)));
    press_update();  // let go: as at rest (or folding back, for a tap)
    if (go.kind != YK_APPS_GO_TAP) return;
    const char *hit = go.hit < 0 ? NULL : go.hit == 0 ? YK_APPS_HOME : s.sat[go.hit - 1].slug;
    if (s_guard && s_guard(hit, s_guard_ctx)) {
        yk_apps_touch_stays(&s.touch);
        press_update();
        return;
    }
    start_close(true, hit, 0, 0);
}

/** The root is going (closed, or its screen deleted): everything else goes with it. */
static void on_delete(lv_event_t *e) {
    (void)e;
    if (s.timer) lv_timer_delete(s.timer);
    if (s.yay) yk_yay_delete(s.yay);
    for (int i = 0; i < YK_APPS_MAX; i++) heap_caps_free((void *)s.sat[i].own.data);
    memset(&s, 0, sizeof(s));
}

static void finish(void) {
    const bool report = s.report && s.cb;
    const yk_apps_cb_t cb = s.cb;
    void *ctx = s.ctx;
    char result[sizeof(s.result)];
    const bool has = s.has_result;
    memcpy(result, s.result, sizeof(result));
    lv_obj_delete(s.root);  // on_delete clears `s`
    if (report) cb(has ? result : NULL, ctx);
}

// ---------------------------------------------------------------- open, close

static int glyph_w(uint32_t cp, void *ctx) { return lv_font_get_glyph_width((const lv_font_t *)ctx, cp, 0); }

/** A name for the middle, at YK_RINGS_NAME_PX: one line or two. */
static int name_lines(const char *name, char *l1, char *l2, size_t size) {
    if (s.font) return yk_rings_name_lines(name, glyph_w, (void *)s.font, YK_RINGS_WORDS_W, l1, size, l2, size);
    snprintf(l1, size, "%s", name);
    l2[0] = 0;
    return name[0] ? 1 : 0;
}

/** The entry's icon as RGB565A8 in PSRAM (raw: a scratch buffer big enough for any format). */
static const lv_image_dsc_t *catalog_icon(sat_t *t, const yk_catalog_entry_t *e, uint8_t *raw) {
    const size_t bytes = yk_catalog_icon_bytes(e->icon_format);
    if (!bytes || !raw || yk_catalog_icon(e, raw, bytes) != ESP_OK) return NULL;
    uint8_t *px = heap_caps_malloc(YK_ICON_RGB565A8_BYTES, MALLOC_CAP_SPIRAM);
    if (!px) return NULL;
    yk_icon_rgb565a8(e->icon_format, raw, e->icon_color, t->installed || t->cached ? LV_OPA_COVER : YK_APPS_DIM_OPA, px);
    t->own = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8, .w = YK_CATALOG_ICON,
                   .h = YK_CATALOG_ICON, .stride = YK_CATALOG_ICON * 2},
        .data_size = YK_ICON_RGB565A8_BYTES,
        .data = px,
    };
    return &t->own;
}

void yk_apps_home_update(bool on) { s_home_update = on; }

void yk_apps_figure(const struct yk_figure *figure) {
    words_t *w = figure ? words() : NULL;
    s_has_figure = w != NULL;  // no memory for the copy: Pip
    if (w) w->figure = *figure;
    // The open ring at once: what it drew from the pack before goes now, so its bytes may go after the caller's switch.
    if (s.yay && !yk_yay_figure(s.yay, s_has_figure ? &s_words->figure : NULL)) yk_yay_figure(s.yay, NULL);
}

void yk_apps_set_guard(yk_apps_guard_t guard, void *ctx) {
    s_guard = guard;
    s_guard_ctx = ctx;
}

static void teardown(void) {
    if (s.root) lv_obj_delete(s.root);  // on_delete clears `s`
}

bool yk_apps_open(lv_obj_t *parent, const yk_apps_item_t *items, int n, const char *current, yk_apps_cb_t cb,
                  void *ctx) {
    if (!parent) return false;
    teardown();
    const int64_t t_start = esp_timer_get_time();
    memset(&s_stats, 0, sizeof(s_stats));
    if (n < 0 || !items) n = 0;
    if (n > YK_APPS_MAX) n = YK_APPS_MAX;
    const bool en = yk_ui_is_english();
    s.cb = cb;
    s.ctx = ctx;
    s.font = cjk_font_get(YK_RINGS_NAME_PX);
    s.first_font = cjk_font_get(YK_RINGS_FIRST_PX);
    s.line_h = s.font ? lv_font_get_line_height(s.font) : YK_RINGS_NAME_PX + 8;
    s.home = current && !strcmp(current, YK_APPS_HOME);
    s.pressed = s.said = -1;
    words_t *w = words();  // the names for the middle (NULL: none said there)
    if (w) {
        memset(w->name, 0, sizeof(w->name));
        memset(w->lines, 0, sizeof(w->lines));
        memset(w->label, 0, sizeof(w->label));
        w->label_lines = 0;
    }

    // The catalog (names and icons), read now and let go when the icons are made.
    yk_catalog_t *cat = heap_caps_malloc(sizeof(*cat), MALLOC_CAP_SPIRAM);
    const bool have_cat = cat && yk_catalog_read(cat) == ESP_OK;
    uint8_t *raw = NULL;
    const int64_t t_icons = esp_timer_get_time();
    int slots[YK_APPS_MAX];
    yk_apps_item_slots(items, n, slots);
    for (int i = 0; i < n; i++) {
        sat_t *t = &s.sat[i];
        snprintf(t->slug, sizeof(t->slug), "%s", items[i].slug ? items[i].slug : "");
        t->more = !strcmp(t->slug, YK_APPS_MORE);
        t->settings = !strcmp(t->slug, YK_APPS_SETTINGS);
        t->installed = items[i].installed;
        t->cached = items[i].cached;
        t->update = items[i].update && !t->more;
        t->phone = items[i].phone && !t->more && !t->settings;
        t->current = current && !strcmp(t->slug, current);
        const yk_catalog_entry_t *e = have_cat && !t->more && !t->settings ? yk_catalog_find(cat, t->slug) : NULL;
        const char *name = items[i].name ? items[i].name
                         : t->more        ? (yk_word_in(en, YK_STR_YAYKIT_SETTINGS_MORE))
                         : t->settings    ? (yk_word_in(en, YK_STR_YAYKIT_APPS_SETTINGS))
                         : e && e->name[0] ? e->name
                                           : t->slug;
        if (w) w->lines[i] = (uint8_t)name_lines(name, w->name[i][0], w->name[i][1], sizeof(w->name[i][0]));
        if (!t->more && !t->settings) yk_first_char(name, t->first);
        t->icon = t->more || t->settings ? NULL : items[i].icon;
        if (!t->icon && e && e->icon_format != YK_ICON_NONE) {
            if (!raw) raw = heap_caps_malloc(yk_catalog_icon_bytes(YK_ICON_ARGB8888), MALLOC_CAP_SPIRAM);
            t->icon = catalog_icon(t, e, raw);
            if (t->icon) s_stats.icons_cached++;
        }
    }
    heap_caps_free(raw);
    heap_caps_free(cat);
    s_stats.icons_ms = (int)((esp_timer_get_time() - t_icons) / 1000);
    const char *label = s.home ? "Tikpal" : yk_word_in(en, YK_STR_YAYKIT_APPS_BACK_TO_TIKPAL);
    if (w) w->label_lines = (uint8_t)name_lines(label, w->label[0], w->label[1], sizeof(w->label[0]));
    yk_apps_layout_at(n, slots, &s.L);
    for (int i = 0; i < n; i++) s.L.sat[i].phone = s.sat[i].phone && !s.sat[i].update;

    // The paper over everything, one object; Yay in the middle.
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, YK_CX * 2, YK_CY * 2);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(YK_PAPER), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_gesture_bubble(root, false);  // a swipe on the ring is not the page's
    lv_obj_set_clickable(root, true);
    lv_obj_add_event_cb(root, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(root, on_press, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(root, on_pressing, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(root, on_click, LV_EVENT_CLICKED, NULL);
    yk_ui_own_taps(root);  // its CLICKED settles a drag too: not for yk_ui_taps_strict (F-1018)
    lv_obj_add_event_cb(root, on_gesture, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(root, on_delete, LV_EVENT_DELETE, NULL);
    s.root = root;
    yk_apps_frame(&s.L, 0, false, s.pose);
    for (int e = 0; e <= s.L.n; e++) s.drawn[e] = element_area(e, &s.pose[e]);
    // On the middle's colour, its square inside the disc (r 75 against a 104 px square: corners at 74): a buddy's pack
    // draws on opaque paper only. (0.1.17's was transparent: a 100 px square on r 56 stood out past it, seen 10-22.)
    // In an extension the user's buddy when the app already read its pack, else Pip: opening the ring never reads one.
    s.yay = yk_yay_create_kept(root, (int)lroundf(YK_RINGS_FIGURE * k_of(&s.pose[0])), YK_CX, YK_CY, YK_PANEL);
    if (s.yay && s_has_figure && !yk_yay_figure(s.yay, &s_words->figure)) yk_yay_figure(s.yay, NULL);  // else Pip
    if (s.yay) lv_obj_set_style_opa(yk_yay_obj(s.yay), s.yay_opa = s.pose[0].opa, 0);
    s.timer = lv_timer_create(tick, FRAME_MS, NULL);
    s.t0 = s.last_input = lv_tick_get();
    s.moving = true;
    s.opened_us = t_start;
    s_stats.setup_ms = (int)((esp_timer_get_time() - t_start) / 1000);
    ESP_LOGI(TAG, "open: %d apps, catalog %s, %d icons from it, setup %d ms", n, have_cat ? "yes" : "none",
             s_stats.icons_cached, s_stats.setup_ms);
    yk_hint_hide();
    yk_hint_note(YK_HINT_EV_RING_OPENED);
    return true;
}

void yk_apps_close(bool animated) {
    if (!s.root) return;
    s.report = false;  // whatever was tapped before, the caller closed it
    if (animated) start_close(false, NULL, 0, 0);
    else teardown();
}

bool yk_apps_is_open(void) { return s.root != NULL; }

static bool ask_tap(void);

bool yk_apps_tap(const char *slug) {
    if (ask_tap()) return true;
    if (!s.root || s.closing) return false;
    const char *hit = NULL;
    if (slug && !strcmp(slug, YK_APPS_HOME)) hit = YK_APPS_HOME;
    for (int i = 0; slug && !hit && i < s.L.n; i++)
        if (!strcmp(s.sat[i].slug, slug)) hit = s.sat[i].slug;
    s.last_input = lv_tick_get();
    start_close(true, hit, 0, 0);
    return true;
}

bool yk_apps_on_paper(int x, int y) { return s.root && !s.closing && yk_apps_hit(&s.L, x, y) < 0; }

// ---------------------------------------------------------------- 「回到主屏幕说？」

#define ASK_CARD 0x1F2521  // the canvas's card colour (yay_pages' CARD)

static struct {
    lv_obj_t *root;
    lv_timer_t *timer;
    yk_apps_cb_t cb;
    void *ctx;
} s_ask;

static lv_obj_t *ask_label(lv_obj_t *parent, int y, int px, uint32_t color, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, 0, y);
    lv_obj_set_width(l, 290);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    const lv_font_t *f = cjk_font_get(px);
    if (f) lv_obj_set_style_text_font(l, f, 0);
    lv_label_set_text(l, text);
    return l;
}

/** Gone: the timer and the objects (the screen deleted takes the objects; this takes the timer). */
static void ask_deleted(lv_event_t *e) {
    (void)e;
    if (s_ask.timer) lv_timer_delete(s_ask.timer);
    memset(&s_ask, 0, sizeof(s_ask));
}

static void ask_end(const char *result) {
    const yk_apps_cb_t cb = s_ask.cb;
    void *ctx = s_ask.ctx;
    if (s_ask.root) lv_obj_delete(s_ask.root);  // ask_deleted clears s_ask
    if (cb) cb(result, ctx);
}

static void ask_tapped(lv_event_t *e) {
    (void)e;
    ask_end(YK_APPS_HOME);
}

static void ask_idle(lv_timer_t *t) {
    (void)t;
    ask_end(NULL);
}

bool yk_apps_ask(lv_obj_t *parent, yk_apps_cb_t cb, void *ctx) {
    if (!parent) return false;
    yk_apps_ask_close();
    const bool en = yk_ui_is_english();
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, YK_CX * 2, YK_CY * 2);
    lv_obj_set_style_bg_color(root, lv_color_hex(YK_PAPER), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_gesture_bubble(root, false);
    lv_obj_set_clickable(root, true);
    lv_obj_t *card = lv_obj_create(root);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, 88, 184);  // the prototype's 290 x 104, radius 26
    lv_obj_set_size(card, 290, 104);
    lv_obj_set_style_radius(card, 26, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(ASK_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_clickable(card, false);  // the tap is the paper's, anywhere
    ask_label(card, 22, 22, YK_INK, yk_apps_ask_text(en));  // 「回到主屏幕说？」: no buddy's name (kit 1.10)
    ask_label(card, 62, YK_SIZE_MIN, YK_INK3, yk_word_in(en, YK_STR_YAYKIT_APPS_TAP_TO_GO));
    yk_ui_tap_only(root);  // a tap anywhere: down and up on the screen, the finger still (a swipe or a drag is none)
    lv_obj_add_event_cb(root, ask_tapped, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(root, ask_deleted, LV_EVENT_DELETE, NULL);
    s_ask.root = root;
    s_ask.cb = cb;
    s_ask.ctx = ctx;
    s_ask.timer = lv_timer_create(ask_idle, YK_APPS_IDLE_MS, NULL);
    lv_timer_set_repeat_count(s_ask.timer, 1);
    lv_timer_set_auto_delete(s_ask.timer, false);
    yk_hint_hide();
    ESP_LOGI(TAG, "ask: back to Yay to talk?");
    return true;
}

void yk_apps_ask_close(void) {
    if (!s_ask.root) return;
    s_ask.cb = NULL;
    lv_obj_delete(s_ask.root);
}

bool yk_apps_ask_is_open(void) { return s_ask.root != NULL; }

/** yk_apps_tap() on the card: a tap anywhere on it. */
static bool ask_tap(void) {
    if (!s_ask.root) return false;
    ask_end(YK_APPS_HOME);
    return true;
}

static lv_obj_t *leaving_label(lv_obj_t *parent, int y, int px, uint32_t color, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, YK_CX - YK_R_CONTENT, y);
    lv_obj_set_width(l, YK_R_CONTENT * 2);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);  // one line
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    const lv_font_t *f = cjk_font_get(px);
    if (f) lv_obj_set_style_text_font(l, f, 0);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *s_leaving;  // the black screen, until the restart (or yk_apps_leaving_clear)

void yk_apps_leaving_clear(void) {
    if (s_leaving) lv_obj_delete(s_leaving);
    s_leaving = NULL;
}

void yk_apps_leaving(const char *slug, const char *name) {
    const bool home = !slug || !strcmp(slug, YK_APPS_HOME), en = yk_ui_is_english();
    char found[sizeof(((yk_catalog_entry_t *)0)->name)] = "";
    if (!home && !name) {
        yk_catalog_t *cat = heap_caps_malloc(sizeof(*cat), MALLOC_CAP_SPIRAM);
        const yk_catalog_entry_t *e = cat && yk_catalog_read(cat) == ESP_OK ? yk_catalog_find(cat, slug) : NULL;
        if (e) snprintf(found, sizeof(found), "%s", e->name);
        heap_caps_free(cat);
        name = found[0] ? found : slug;
    }
    yk_apps_leaving_clear();
    lv_obj_t *o = s_leaving = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, YK_CX * 2, YK_CY * 2);
    lv_obj_set_style_bg_color(o, lv_color_hex(YK_NIGHT), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, true);  // nothing under it takes a touch now
    // The prototype's baselines (214 and 252) for 16 and 24 px lines.
    leaving_label(o, 196, YK_SIZE_MIN, YK_INK2, home ? (yk_word_in(en, YK_STR_YAYKIT_APPS_BACK_TO)) : (yk_word_in(en, YK_STR_YAYKIT_HOME_OPENING_NOW)));
    leaving_label(o, 226, YK_SIZE_HEADING, YK_INK, home ? "Tikpal" : name);
    ESP_LOGI(TAG, "leaving for %s", home ? YK_APPS_HOME : slug);
}

void yk_apps_stats(yk_apps_stats_t *out) {
    if (out) *out = s_stats;
}
