// The settings ring and its dial (yk_ring.h). As the app ring (yk_apps.c), and as big (yk_rings.h): one opaque full-screen
// object on top of the screen draws everything itself, from the layout and poses of yk_ring_logic.c; a frame of the motion
// repaints only what moved. A finger down on a place grows it and says its name in the middle (0.1.18).
#include "yk_ring.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cjk_font.h"
#include "esp_heap_caps.h"
#include "yaykit_ui.h"
#include "yk_ring_logic.h"
#include "yk_accent.h"
#include "yk_round.h"
#include "yk_ui_logic.h"
#include "yk_ui_priv.h"

#define FRAME_MS 16        // while it moves
#define IDLE_CHECK_MS 250  // while it waits
#define NAME_PX 16         // the middle's smaller lines (under the title, below the places), the dial's note
#define TITLE_PX YK_RINGS_NAME_PX  // the middle's title, when it fits; else NAME_PX; a pressed place's name
#define RIM_R YK_RIM_R     // the progress along the edge
#define RIM_W YK_RIM_W
#define MOST_NAME 48

typedef struct {
    char name[2][MOST_NAME], first[5];  // the name for the middle while it is pressed: one line or two
    int lines;
    const lv_image_dsc_t *icon;
    bool dot, more, busy, stays, leveled;
    int level;  // 0..1000, when leveled
} place_t;

typedef struct {
    lv_obj_t *root;
    lv_timer_t *timer;
    yk_ring_layout_t L;
    yk_apps_pose_t pose[YK_RING_MAX + 1];
    lv_area_t drawn[YK_RING_MAX + 1];  // what each element covered last frame
    place_t place[YK_RING_MAX];
    char title[MOST_NAME], sub[MOST_NAME], below[96];
    uint32_t fill;
    bool action, hold, keep;
    int permille;
    const lv_font_t *font, *title_font, *name_font, *first_font;
    int line_h;
    int pressed;  // yk_rings_pressed() as last drawn: 1..n a place, 0 the middle, -1 none
    int said;     // ... and what the middle says (the same values): it changes in a frame of its own
    uint32_t press_t0;
    uint32_t dim;       // the places drawn dimmed (yk_rings_press): bit i-1 for place i
    bool press_moving;  // the press still changing the look, a step a frame ...
    bool press_drawn;   // ... the last step drawn: the next may come
    yk_ring_cb_t cb;
    void *ctx;
    uint32_t t0, last_input;
    bool closing, moving, report;
    yk_apps_touch_t touch;
    int8_t away_dx, away_dy;  // closing after a swipe: towards the finger (0, 0: folding back)
    yk_ring_what_t what;
    int index;
} ring_t;

static ring_t *s;  // in PSRAM while it is open

static int glyph_w(uint32_t cp, void *ctx) { return lv_font_get_glyph_width((const lv_font_t *)ctx, cp, 0); }

static int fit(const char *text, const lv_font_t *font, int room, char *out, size_t size) {
    if (font) return yk_text_fit(text, glyph_w, (void *)font, room, out, size);
    snprintf(out, size, "%s", text);
    return 0;
}

// ---------------------------------------------------------------- where things are at a pose

static float k_of(const yk_apps_pose_t *p) { return p->scale / 256.0f; }

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

/** The scale the update dot goes with: the ring's motion, not a press (the disc grows under it). */
static float mark_k(int e, const yk_apps_pose_t *p) { return e == s->pressed && !s->moving ? 1.0f : k_of(p); }

/** Everything element e (0: the middle, 1..n: a place) covers at pose p, its dot and the sage ring included. */
static lv_area_t element_area(int e, const yk_apps_pose_t *p) {
    const float k = k_of(p);
    if (e == 0) return disc_area(p->cx, p->cy, YK_RINGS_CENTER_R * k);
    lv_area_t a = disc_area(p->cx, p->cy, (YK_RINGS_SAT_R + YK_RINGS_LEVEL_GAP + YK_RINGS_LEVEL_W) * k);  // past the
                                                                                                      // in-use ring
    int mx, my;
    float outer;
    yk_rings_mark(YK_MARK_UPDATE, p->cx, p->cy, mark_k(e, p), &mx, &my, &outer);
    const lv_area_t dot = disc_area(mx, my, outer);
    join(&a, &dot);
    return a;
}

// ---------------------------------------------------------------- drawing

static void draw_place(lv_layer_t *layer, int i) {
    const yk_apps_pose_t *p = &s->pose[i + 1];
    if (p->opa <= LV_OPA_MIN) return;
    const place_t *t = &s->place[i];
    const float k = k_of(p);
    yk_draw_disc(layer, p->cx, p->cy, YK_RINGS_SAT_R * k, YK_PANEL, p->opa);
    if (t->more) {
        for (int d = -1; d <= 1; d++)
            yk_draw_disc(layer, p->cx + (int)lroundf(d * YK_APPS_DOT_GAP * k), p->cy, YK_APPS_DOT_R * k, YK_INK, p->opa);
    } else if (t->icon) {
        lv_draw_image_dsc_t d;
        lv_draw_image_dsc_init(&d);
        d.src = t->icon;
        d.scale_x = d.scale_y = (uint16_t)lroundf(256 * mark_k(i + 1, p));  // not the press's: no scaled copy each frame
        d.pivot.x = d.pivot.y = YK_RING_ICON / 2;
        d.opa = p->opa;
        const lv_area_t a = {p->cx - YK_RING_ICON / 2, p->cy - YK_RING_ICON / 2, p->cx + YK_RING_ICON / 2 - 1,
                             p->cy + YK_RING_ICON / 2 - 1};
        lv_draw_image(layer, &d, &a);
    } else if (t->first[0] && s->first_font) {
        const int h = lv_font_get_line_height(s->first_font);
        const lv_area_t a = {p->cx - YK_RINGS_SAT_R, p->cy - h / 2, p->cx + YK_RINGS_SAT_R - 1, p->cy - h / 2 + h - 1};
        yk_draw_text(layer, t->first, s->first_font, YK_INK, text_opa(p, 0.6f), &a);
    }
    const bool pressed = i + 1 == s->pressed && !s->moving;
    // Pressed: the outline a thin line in the account's accent, as the levels (强调色只画细线, 0.1.20 4.8: it was sage
    // whatever the colourway); the levels go while a place is pressed, its own and those dimmed round it (0.1.19 4.7
    // 「按下格子时弧藏起来」: they only dimmed).
    if (pressed) yk_draw_pressed_in(layer, p->cx, p->cy, YK_RINGS_SAT_R * k, p->opa, yk_accent_text());
    else if (t->busy) yk_draw_in_use(layer, p->cx, p->cy, YK_RINGS_SAT_R, k, p->opa);
    if (t->leveled && !pressed && !t->busy && !(s->dim & 1u << i)) {
        // The level (0.1.19 4.7): the disc's colour all along, the accent's thin line on the part reached. LVGL's angles
        // run clockwise from 3 o'clock: 7:30 is 135.
        lv_draw_arc_dsc_t a;
        lv_draw_arc_dsc_init(&a);
        a.center.x = p->cx;
        a.center.y = p->cy;
        a.radius = (uint16_t)lroundf((YK_RINGS_SAT_R + YK_RINGS_LEVEL_GAP + YK_RINGS_LEVEL_W) * k);  // the outer edge
        a.width = LV_MAX(1, (int32_t)lroundf(YK_RINGS_LEVEL_W * k));
        a.start_angle = 135;
        a.end_angle = 135 + YK_RINGS_LEVEL_DEG;
        a.color = lv_color_hex(YK_PANEL);
        a.opa = p->opa;
        lv_draw_arc(layer, &a);
        const int level = t->level < 0 ? 0 : t->level > 1000 ? 1000 : t->level;
        if (level > 0) {
            a.end_angle = 135 + (YK_RINGS_LEVEL_DEG * level + 500) / 1000;
            a.color = lv_color_hex(yk_accent_text());
            lv_draw_arc(layer, &a);
        }
    }
    if (t->dot) {
        int mx, my;
        float outer;
        const float km = mark_k(i + 1, p);
        yk_rings_mark(YK_MARK_UPDATE, p->cx, p->cy, km, &mx, &my, &outer);
        yk_draw_update_dot(layer, mx, my, km, p->opa);
    }
}

static void draw_middle(lv_layer_t *layer) {
    const yk_apps_pose_t *p = &s->pose[0];
    if (p->opa <= LV_OPA_MIN) return;
    const float k = k_of(p);
    yk_draw_disc(layer, p->cx, p->cy, YK_RINGS_CENTER_R * k, s->fill ? s->fill : YK_PANEL, p->opa);
    const lv_opa_t opa = text_opa(p, 0.8f);
    const bool coral = s->fill == YK_CORAL;
    const char *lines[3] = {"", "", ""};
    const lv_font_t *fonts[3] = {NULL, NULL, NULL};
    uint32_t colors[3] = {0, 0, 0};
    int n = 0;
    if (s->said > 0 && !s->moving) {  // a place under the finger: its name
        const place_t *t = &s->place[s->said - 1];
        for (int i = 0; i < t->lines; i++)
            lines[n] = t->name[i], fonts[n] = s->name_font, colors[n++] = t->busy ? YK_SAGE_TEXT : coral ? YK_PAPER : YK_INK;
    } else {  // the ring's words: the title, the line under it, the line below the places
        lines[n] = s->title, fonts[n] = s->title_font, colors[n++] = coral ? YK_PAPER : YK_INK;
        if (s->sub[0]) lines[n] = s->sub, fonts[n] = s->font, colors[n++] = coral ? YK_PAPER : YK_INK2;
        if (s->below[0]) lines[n] = s->below, fonts[n] = s->font, colors[n++] = coral ? YK_PAPER : YK_INK2;
    }
    yk_draw_words(layer, n, lines, fonts, colors, opa);
}

static void draw_cb(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    s->press_drawn = true;
    // Called once a band of a few lines: what does not reach the band is not even asked for (as yk_apps.c).
    for (int i = 0; i < s->L.n; i++)
        if (yk_reaches(layer, &s->drawn[i + 1])) draw_place(layer, i);
    if (yk_reaches(layer, &s->drawn[0])) draw_middle(layer);
    if (s->permille < 0 || s->closing) return;
    lv_draw_arc_dsc_t a;
    lv_draw_arc_dsc_init(&a);
    a.center.x = YK_CX;
    a.center.y = YK_CY;
    a.radius = RIM_R;
    a.width = RIM_W;
    a.start_angle = 270;
    a.end_angle = 270 + 360 * s->permille / 1000;
    a.color = lv_color_hex(YK_SAGE);
    lv_draw_arc(layer, &a);
}

// ---------------------------------------------------------------- motion

/** Takes the poses: repaints what moved; true when anything did. */
static bool apply(const yk_apps_pose_t pose[YK_RING_MAX + 1]) {
    bool any = false;
    for (int e = 0; e <= s->L.n; e++) {
        if (!memcmp(&pose[e], &s->pose[e], sizeof(pose[e]))) continue;
        s->pose[e] = pose[e];
        const lv_area_t a = element_area(e, &pose[e]);
        lv_obj_invalidate_area(s->root, &s->drawn[e]);
        lv_obj_invalidate_area(s->root, &a);
        s->drawn[e] = a;
        any = true;
    }
    return any;
}

/** The press as it is now (yk_rings_pressed), as the app ring's: called on every touch event and from the timer while
 *  the ring is at rest. */
static void press_update(void) {
    if (!s || !s->root || s->moving || s->closing) return;
    const uint32_t now = lv_tick_get();
    const int pressed = yk_rings_pressed(&s->touch, now);
    // As the app ring's: the places alone, then the middle alone, then a step at a time.
    if (pressed != s->pressed) {
        yk_apps_pose_t pose[YK_RING_MAX + 1];
        memcpy(pose, s->pose, sizeof(pose));
        const int was = s->pressed;
        bool any = false;
        if (was > 0) {
            yk_apps_pose_t rest[YK_RING_MAX + 1];
            yk_ring_frame(&s->L, YK_APPS_OPEN_MS, false, rest);
            pose[was].scale = rest[was].scale;
            lv_obj_invalidate_area(s->root, &s->drawn[was]);
            any = true;
        }
        s->pressed = pressed;
        apply(pose);
        if (pressed > 0) lv_obj_invalidate_area(s->root, &s->drawn[pressed]), any = true;
        s->press_moving = true;
        s->press_drawn = !any;  // nothing to draw: the middle at once
        lv_timer_set_period(s->timer, FRAME_MS);
        return;
    }
    // A step a drawn frame (the timer can tick twice before LVGL draws one).
    if (!s->press_moving || !s->press_drawn) return;
    if (s->said != s->pressed) {
        s->said = s->pressed;
        const lv_area_t words = yk_rings_words_area();  // not the whole middle (its disc stays as it is)
        lv_obj_invalidate_area(s->root, &words);
        s->press_t0 = now;  // the pressed place grows from now
        s->press_drawn = false;
        return;
    }
    yk_apps_pose_t pose[YK_RING_MAX + 1];
    yk_ring_frame(&s->L, YK_APPS_OPEN_MS, false, pose);
    const bool still = yk_rings_press(s->L.n, s->pressed, (int)(now - s->press_t0), &s->dim, pose);
    if (apply(pose)) s->press_drawn = false;  // nothing to draw: the next step at once
    if (still) {
        s->press_moving = false;
        lv_timer_set_period(s->timer, IDLE_CHECK_MS);
    }
}

static void start_close(bool report, yk_ring_what_t what, int index, int dx, int dy) {
    if (s->closing) return;
    s->closing = true;
    yk_apps_touch_closing(&s->touch);
    s->away_dx = (int8_t)dx, s->away_dy = (int8_t)dy;
    s->moving = true;
    s->report = report;
    s->what = what;
    s->index = index;
    s->t0 = lv_tick_get();
    lv_timer_set_period(s->timer, FRAME_MS);
    lv_timer_reset(s->timer);
}

static void finish(void) {
    const bool report = s->report && s->cb;
    const yk_ring_cb_t cb = s->cb;
    void *ctx = s->ctx;
    const yk_ring_what_t what = s->what;
    const int index = s->index;
    lv_obj_delete(s->root);  // on_delete frees `s`
    if (report) cb(what, index, ctx);
}

static void tick(lv_timer_t *t) {
    if (!s || !s->root) return;
    if (lv_obj_get_index(s->root) != (int32_t)lv_obj_get_child_count(lv_obj_get_parent(s->root)) - 1)
        lv_obj_move_to_index(s->root, -1);  // on top of what the screen gets meanwhile
    if (s->moving) {
        yk_apps_pose_t pose[YK_RING_MAX + 1];
        const int t_ms = (int)lv_tick_elaps(s->t0);
        const bool done = s->closing && (s->away_dx || s->away_dy) ? yk_ring_frame_away(&s->L, t_ms, s->away_dx, s->away_dy, pose)
                                                                   : yk_ring_frame(&s->L, t_ms, s->closing, pose);
        apply(pose);
        if (!done) return;
        s->moving = false;
        if (s->closing) {
            finish();
            return;
        }
        lv_timer_set_period(t, IDLE_CHECK_MS);
        press_update();  // a finger that went down while it opened
    } else if (!s->keep && !s->hold && lv_tick_elaps(s->last_input) >= YK_APPS_IDLE_MS) {
        start_close(true, YK_RING_GONE, 0, 0, 0);
    } else {
        press_update();
    }
}

static lv_point_t finger(void) {
    lv_point_t p = {0};
    lv_indev_t *indev = lv_indev_active();
    if (indev) lv_indev_get_point(indev, &p);
    return p;
}

static void on_press(lv_event_t *e) {
    (void)e;
    s->last_input = lv_tick_get();
    const lv_point_t p = finger();
    yk_apps_touch_press(&s->touch, yk_ring_hit(&s->L, p.x, p.y), p.x, p.y, s->last_input);
    press_update();
}

static void on_pressing(lv_event_t *e) {
    (void)e;
    const lv_point_t p = finger();
    yk_apps_touch_move(&s->touch, p.x, p.y);
    press_update();
}

static void on_gesture(lv_event_t *e) {
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    const lv_dir_t d = lv_indev_get_gesture_dir(indev);
    const int dx = d == LV_DIR_LEFT ? -1 : d == LV_DIR_RIGHT ? 1 : 0, dy = d == LV_DIR_TOP ? -1 : d == LV_DIR_BOTTOM ? 1 : 0;
    const yk_apps_go_t go = yk_apps_touch_swipe(&s->touch, dx, dy);
    if (go.kind != YK_APPS_GO_AWAY) return;
    lv_indev_wait_release(indev);
    s->last_input = lv_tick_get();
    if (s->hold) {
        yk_apps_touch_stays(&s->touch);  // under way: it goes nowhere, and the next finger counts
        return;
    }
    start_close(true, YK_RING_GONE, 0, go.dx, go.dy);
}

/** The finger let go: a tap (yk_tap: down and up on the same place, still, not held) on what it hit, else nothing. */
static void on_click(lv_event_t *e) {
    (void)e;
    const lv_point_t p = finger();
    const yk_apps_go_t go = yk_ring_touch_click(&s->touch, &s->L, p.x, p.y, lv_tick_get());
    char line[64];
    puts(yk_tap_line(&s->touch.tap, "ring", line, sizeof(line)));
    press_update();  // let go: as at rest (or folding back, for a tap)
    if (go.kind != YK_APPS_GO_TAP) return;
    const bool place = go.hit > 0, middle = go.hit == 0;
    if (place && !s->place[go.hit - 1].stays) {
        start_close(true, YK_RING_PLACE, go.hit - 1, 0, 0);
        return;
    }
    yk_apps_touch_stays(&s->touch);
    press_update();
    if (place) {
        if (s->cb) s->cb(YK_RING_PLACE, go.hit - 1, s->ctx);
    } else if (middle && s->action) {
        if (s->cb) s->cb(YK_RING_ACTION, 0, s->ctx);
    } else if (!s->hold) {
        start_close(true, YK_RING_GONE, 0, 0, 0);
    }
}

static void on_delete(lv_event_t *e) {
    (void)e;
    if (s->timer) lv_timer_delete(s->timer);
    heap_caps_free(s);
    s = NULL;
}

// ---------------------------------------------------------------- open, close

/** Takes the view: the words cut to fit, the places laid out. */
static void take(const yk_ring_view_t *v) {
    const int n = !v->items || v->n < 0 ? 0 : v->n > YK_RING_MAX ? YK_RING_MAX : v->n;
    for (int i = 0; i < n; i++) {
        place_t *t = &s->place[i];
        const yk_ring_item_t *it = &v->items[i];
        const char *name = it->name ? it->name : "";
        if (s->name_font) {
            t->lines = yk_rings_name_lines(name, glyph_w, (void *)s->name_font, YK_RINGS_WORDS_W, t->name[0],
                                           sizeof(t->name[0]), t->name[1], sizeof(t->name[1]));
        } else {
            snprintf(t->name[0], sizeof(t->name[0]), "%s", name);
            t->name[1][0] = 0;
            t->lines = name[0] ? 1 : 0;
        }
        yk_first_char(name, t->first);
        t->icon = it->icon;
        t->dot = it->dot, t->more = it->more, t->busy = it->busy, t->stays = it->stays;
        t->leveled = it->leveled, t->level = it->level;
    }
    const char *title = v->title ? v->title : "";
    // The bigger size when the words fit it: 全部更新 does, 76% · Home-5G does not.
    s->title_font = s->name_font;
    if (s->title_font) {
        lv_point_t size;
        lv_text_get_size(&size, title, s->title_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x > YK_RINGS_WORDS_W) s->title_font = s->font;
    }
    fit(title, s->title_font, YK_RINGS_WORDS_W, s->title, sizeof(s->title));
    fit(v->sub ? v->sub : "", s->font, YK_RINGS_WORDS_W, s->sub, sizeof(s->sub));
    fit(v->below ? v->below : "", s->font, YK_RINGS_WORDS_W, s->below, sizeof(s->below));
    s->fill = v->fill, s->action = v->action, s->hold = v->hold, s->keep = v->keep, s->permille = v->permille;
    yk_ring_layout(n, &s->L);
}

bool yk_ring_open(lv_obj_t *parent, const yk_ring_view_t *view, yk_ring_cb_t cb, void *ctx) {
    if (!parent || !view) return false;
    yk_ring_close(false);
    s = heap_caps_calloc(1, sizeof(*s), MALLOC_CAP_SPIRAM);
    if (!s) return false;
    s->cb = cb;
    s->ctx = ctx;
    s->font = cjk_font_get(NAME_PX);
    s->name_font = cjk_font_get(TITLE_PX);
    s->first_font = cjk_font_get(YK_RINGS_FIRST_PX);
    s->line_h = s->font ? lv_font_get_line_height(s->font) : NAME_PX + 6;
    s->pressed = s->said = -1;
    take(view);
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
    s->root = root;
    s->t0 = s->last_input = lv_tick_get();
    s->moving = true;
    yk_ring_frame(&s->L, 0, false, s->pose);
    for (int e = 0; e <= s->L.n; e++) s->drawn[e] = element_area(e, &s->pose[e]);
    s->timer = lv_timer_create(tick, FRAME_MS, NULL);
    return true;
}

void yk_ring_show(const yk_ring_view_t *view) {
    if (!s || !view || s->closing) return;
    const int n = s->L.n;
    take(view);
    if (s->L.n == n && !s->moving) {
        yk_ring_frame(&s->L, YK_APPS_OPEN_MS, false, s->pose);
        for (int e = 0; e <= s->L.n; e++) s->drawn[e] = element_area(e, &s->pose[e]);
        s->press_moving = s->press_drawn = true;  // a finger still down, places dimmed: as they were
        press_update();
    } else {
        s->moving = true;  // other places: they come out again
        s->t0 = lv_tick_get();
        lv_timer_set_period(s->timer, FRAME_MS);
    }
    lv_obj_invalidate(s->root);
}

void yk_ring_close(bool animated) {
    if (!s) return;
    if (animated) start_close(false, YK_RING_GONE, 0, 0, 0);
    else lv_obj_delete(s->root);
}

bool yk_ring_is_open(void) { return s != NULL; }

// ---------------------------------------------------------------- the dial

// The button under the note (声音和振动): as yay_pages' secondary pill, the paper's colour with a thin edge, CARD while it
// is pressed (big_button's).
#define BUTTON_EDGE 0x3D423E     // yay_pages' OUTLINE: rgba(250,247,239,0.16) over the paper
#define BUTTON_PRESSED 0x1F2521  // yay_pages' CARD

typedef struct {
    lv_obj_t *root;
    int stops, stop, last;
    char value[32], note[96], title[48], title2[48], button[64];
    int title_lines;      // 1, or 2 when one does not hold it (0: none)
    bool button_pressed;  // the button as last drawn
    bool released;        // the finger let go (LVGL's RELEASED comes before CLICKED)
    const lv_font_t *font, *value_font, *title_font;
    yk_dial_label_t label;
    yk_dial_cb_t cb;
    void *ctx;
    yk_dial_touch_t touch;
} dial_t;

static dial_t *d;  // in PSRAM while it is open

static void dial_words(void) {
    d->value[0] = 0;
    if (d->label) d->label(d->stop, d->value, sizeof(d->value), d->ctx);
}

/** LVGL's arc angle (0 at 3 o'clock) of a stop. */
static int arc_angle(int stop) { return (int)lroundf(yk_dial_angle(stop, d->stops)) - 90; }

static void dial_draw(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_arc_dsc_t a;
    lv_draw_arc_dsc_init(&a);
    a.center.x = YK_CX;
    a.center.y = YK_CY;
    a.radius = YK_DIAL_TICK_OUT;  // LVGL's: the outer edge, the width inwards
    a.width = YK_DIAL_TICK_OUT - YK_DIAL_TICK_IN;
    a.start_angle = arc_angle(0);
    a.end_angle = arc_angle(d->last);
    a.color = lv_color_hex(YK_PANEL);
    lv_draw_arc(layer, &a);
    if (d->stop > 0) {
        a.end_angle = arc_angle(d->stop);
        a.color = lv_color_hex(yk_accent_text());  // the account's accent (0.1.19 4.7), as the settings ring's levels
        lv_draw_arc(layer, &a);
    }
    for (int i = 0; i <= d->last; i++) {  // a stop's mark, so that the steps show
        int x, y;
        yk_dial_point(i, d->stops, (YK_DIAL_TICK_OUT + YK_DIAL_TICK_IN) / 2, &x, &y);
        yk_draw_disc(layer, x, y, YK_DIAL_STOP_R, i <= d->stop ? YK_PAPER : YK_INK3, LV_OPA_COVER);
    }
    const int title_h = d->title_font ? lv_font_get_line_height(d->title_font) : TITLE_PX + 8;
    const int value_h = d->value_font ? lv_font_get_line_height(d->value_font) : YK_DIAL_VALUE_PX;
    const int note_h = d->font ? lv_font_get_line_height(d->font) : NAME_PX + 6;
    yk_dial_layout_t D;
    yk_dial_layout_full(d->title_lines == 2 ? 2 : 1, title_h, value_h, note_h, d->touch.button ? note_h : 0, &D);
    const yk_box_t *b[4] = {&D.title, &D.title2, &D.value, &D.note};
    const char *text[4] = {d->title, d->title2, d->value, d->note};
    const lv_font_t *font[4] = {d->title_font, d->title_font, d->value_font, d->font};
    const uint32_t color[4] = {YK_INK, YK_INK, YK_INK, YK_INK2};
    for (int i = 0; i < 4; i++) {
        if (!b[i]->h) continue;  // one title line
        const lv_area_t box = {b[i]->x, b[i]->y, b[i]->x + b[i]->w - 1, b[i]->y + b[i]->h - 1};
        yk_draw_text(layer, text[i], font[i], color[i], LV_OPA_COVER, &box);
    }
    if (!d->touch.button) return;
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.radius = YK_DIAL_BUTTON_H / 2;
    r.bg_color = lv_color_hex(d->button_pressed ? BUTTON_PRESSED : YK_PAPER);
    r.border_color = lv_color_hex(BUTTON_EDGE);
    r.border_width = 1;
    const lv_area_t pill = {D.button.x, D.button.y, D.button.x + D.button.w - 1, D.button.y + D.button.h - 1};
    lv_draw_rect(layer, &r, &pill);
    const lv_area_t words = {D.button_words.x, D.button_words.y, D.button_words.x + D.button_words.w - 1,
                             D.button_words.y + D.button_words.h - 1};
    yk_draw_text(layer, d->button, d->font, YK_INK2, LV_OPA_COVER, &words);
}

/** The button pressed or not, as the finger is now: only its own area drawn again, and only when that changed. */
static void dial_button_look(void) {
    if (!d || !d->touch.button) return;
    const bool pressed = !d->released && yk_dial_button_pressed(&d->touch);
    if (pressed == d->button_pressed) return;
    d->button_pressed = pressed;
    const lv_area_t pill = {YK_DIAL_BUTTON_X, YK_DIAL_BUTTON_Y, YK_DIAL_BUTTON_X + YK_DIAL_BUTTON_W - 1,
                            YK_DIAL_BUTTON_Y + YK_DIAL_BUTTON_H - 1};
    lv_obj_invalidate_area(d->root, &pill);
}

static void dial_moved(int stop) {
    if (stop == d->stop) return;
    d->stop = stop;
    dial_words();
    lv_obj_invalidate(d->root);
    if (d->cb) d->cb(YK_DIAL_TURN, stop, d->ctx);
}

static void dial_done(void) {
    const yk_dial_cb_t cb = d->cb;
    void *ctx = d->ctx;
    const int stop = d->stop;
    lv_obj_delete(d->root);  // dial_delete frees `d`
    if (cb) cb(YK_DIAL_DONE, stop, ctx);
}

static void dial_touch(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = {0};
    if (indev) lv_indev_get_point(indev, &p);
    int stop = d->stop;
    yk_dial_go_t go;
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {  // only with a button: its look as the finger goes
        d->released = true;
        dial_button_look();
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        d->released = false;
        go = yk_dial_touch_press(&d->touch, p.x, p.y, d->stops, d->last, lv_tick_get(), &stop);
    } else if (code == LV_EVENT_PRESSING) {
        go = yk_dial_touch_move(&d->touch, p.x, p.y, d->stops, d->last, &stop);
    } else if (code == LV_EVENT_GESTURE) {
        go = yk_dial_touch_swipe(&d->touch);
    } else {
        go = yk_dial_touch_click(&d->touch, p.x, p.y, d->stops, d->last, lv_tick_get(), &stop);
        char line[64];
        puts(yk_tap_line(&d->touch.tap, d->touch.following ? "dial-turn" : "dial", line, sizeof(line)));
    }
    dial_button_look();
    if (go == YK_DIAL_GO_TURN) {
        dial_moved(stop);
    } else if (go == YK_DIAL_GO_DONE) {
        if (code == LV_EVENT_GESTURE && indev) lv_indev_wait_release(indev);
        dial_done();
    } else if (go == YK_DIAL_GO_BUTTON && d->cb) {
        d->cb(YK_DIAL_BUTTON, d->stop, d->ctx);  // last: the callback may close the dial
    }
}

static void dial_delete(lv_event_t *e) {
    (void)e;
    heap_caps_free(d);
    d = NULL;
}

bool yk_dial_open(lv_obj_t *parent, const yk_dial_view_t *view, yk_dial_label_t label, yk_dial_cb_t cb, void *ctx) {
    if (!parent || !view || view->stops < 2) return false;
    yk_dial_close();
    d = heap_caps_calloc(1, sizeof(*d), MALLOC_CAP_SPIRAM);
    if (!d) return false;
    d->stops = view->stops;
    d->last = view->last < 0 || view->last > view->stops - 1 ? view->stops - 1 : view->last;
    d->stop = view->stop < 0 ? 0 : view->stop > d->last ? d->last : view->stop;
    d->label = label;
    d->cb = cb;
    d->ctx = ctx;
    d->font = cjk_font_get(NAME_PX);
    d->value_font = cjk_font_get(YK_DIAL_VALUE_PX);
    d->title_font = cjk_font_get(TITLE_PX);
    fit(view->note ? view->note : "", d->font, YK_DIAL_NOTE_W, d->note, sizeof(d->note));
    // The title: one line as ever when it fits; else two at the same size (声音和振动), cut only when two do not hold it.
    if (d->title_font) {
        d->title_lines = yk_dial_title_lines(view->title, glyph_w, (void *)d->title_font, YK_DIAL_TITLE_W, d->title,
                                             sizeof(d->title), d->title2, sizeof(d->title2));
    } else {
        fit(view->title ? view->title : "", NULL, YK_DIAL_TITLE_W, d->title, sizeof(d->title));
        d->title_lines = 1;
    }
    if (view->button && view->button[0]) {
        fit(view->button, d->font, YK_DIAL_BUTTON_W - 2 * YK_DIAL_BUTTON_PAD, d->button, sizeof(d->button));
        d->touch.button = true;
    }
    dial_words();
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, YK_CX * 2, YK_CY * 2);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(YK_PAPER), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_gesture_bubble(root, false);
    lv_obj_set_clickable(root, true);
    lv_obj_add_event_cb(root, dial_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(root, dial_touch, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(root, dial_touch, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(root, dial_touch, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(root, dial_touch, LV_EVENT_CLICKED, NULL);
    yk_ui_own_taps(root);  // its CLICKED settles a drag too: not for yk_ui_taps_strict (F-1018)
    if (d->touch.button) {  // the button's look as the finger lets go (a dial without one asks for neither)
        lv_obj_add_event_cb(root, dial_touch, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(root, dial_touch, LV_EVENT_PRESS_LOST, NULL);
    }
    lv_obj_add_event_cb(root, dial_delete, LV_EVENT_DELETE, NULL);
    d->root = root;
    return true;
}

void yk_dial_set(int stop) {
    if (!d) return;
    d->stop = stop < 0 ? 0 : stop > d->last ? d->last : stop;
    dial_words();
    lv_obj_invalidate(d->root);
}

void yk_dial_close(void) {
    if (d) lv_obj_delete(d->root);
}

bool yk_dial_is_open(void) { return d != NULL; }
