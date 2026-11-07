// 声波码 on the round screen (yk_vc_view.h). The code is one LVGL object drawn by hand (ring, dot, 48 bars) with the
// Tikpal mark in the middle; while the bars talk, a timer moves them and invalidates only the boxes of the pairs that
// changed (yk_vc_box_px), never the whole screen. A frame at most every 33 ms, and the next only once the last is on
// the screen and LVGL has slept YK_VC_YIELD_MS: a frame takes about as long to draw as that, and frame after frame
// the LVGL task kept its lock the whole talking (Yay's code pages: its screen updates, waiting 200 ms for the lock,
// failed). Settled, the timer is gone and nothing redraws. The pixels and
// the talking are pure (yk_vc_view_logic.c, host tested); drawn as the test firmware the iPhone read on 10-30 (#975).
#include "yk_vc_view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "yk_mark.h"
#include "yaykit_ui.h"
#include "yk_round.h"
#include "yk_vc_view_logic.h"

// Colour carries nothing (the app reads brightness): the theme's ink, coral for the longest level and the dot.
#define VC_INK YK_INK
#define VC_CORAL YK_CORAL
#define VC_BG YK_NIGHT  // pure black: the best contrast on the AMOLED, whatever the theme (BOSS-2, 11-01)

struct yk_vc_view {
    lv_obj_t *obj;
    lv_obj_t *mark;     // the Tikpal mark in the middle ...
    lv_obj_t *middle;   // ... or a line of text in its place (yk_vc_view_set_middle)
    lv_timer_t *timer;  // while the bars talk ...
    lv_display_t *disp; // ... whose refresh the next frame waits for
    int64_t frame_ms;   // when the last frame began
    bool waiting;       // its refresh not done yet
    int frames;         // frames while talking (the settled logs)
    yk_vc_px_t px;
    int half;           // the object's half side: the code's centre in the object
    int16_t boxes[YK_VC_BOXES][4];
    int16_t ends[YK_VC_BARS][4];  // what is drawn, from the centre
    uint8_t colour[YK_VC_BARS];   // the level nearest each bar's length: level 3 coral
    yk_vc_anim_t anim;
    int64_t t0_ms;
    bool settled;
    yk_vc_settled_fn on_settled;
    void *settled_ctx;
};

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

// ---------------------------------------------------------------- drawing

static bool clip_meets(const lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    const lv_area_t *c = &layer->_clip_area;
    return x2 >= c->x1 && x1 <= c->x2 && y2 >= c->y1 && y1 <= c->y2;
}

/** Whether the area being redrawn reaches the ring between radii r0 and r1 about (cx, cy). */
static bool clip_meets_ring(const lv_layer_t *layer, int cx, int cy, float r0, float r1) {
    const lv_area_t *c = &layer->_clip_area;
    const float nx = fmaxf(0, fmaxf((float)(c->x1 - cx), (float)(cx - c->x2)));
    const float ny = fmaxf(0, fmaxf((float)(c->y1 - cy), (float)(cy - c->y2)));
    const float fx = fmaxf(fabsf((float)(c->x1 - cx)), fabsf((float)(c->x2 - cx)));
    const float fy = fmaxf(fabsf((float)(c->y1 - cy)), fabsf((float)(c->y2 - cy)));
    return sqrtf(nx * nx + ny * ny) <= r1 && sqrtf(fx * fx + fy * fy) >= r0;
}

static void disc(lv_layer_t *layer, int cx, int cy, int r, uint32_t color) {
    const lv_area_t a = {cx - r, cy - r, cx + r - 1, cy + r - 1};
    if (!clip_meets(layer, a.x1, a.y1, a.x2, a.y2)) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(color);
    d.bg_opa = LV_OPA_COVER;
    d.radius = LV_RADIUS_CIRCLE;
    lv_draw_rect(layer, &d, &a);
}

/** The ring about (cx, cy), the middle of its stroke at r. LVGL's radius is the outer edge, the stroke grows inwards. */
static void ring(lv_layer_t *layer, int cx, int cy, int r, int w, uint32_t color) {
    if (!clip_meets_ring(layer, cx, cy, r - w / 2.0f - 1, r + w / 2.0f + 1)) return;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = cx;
    d.center.y = cy;
    d.radius = (uint16_t)lroundf(r + w / 2.0f);
    d.width = w;
    d.start_angle = 0;
    d.end_angle = 360;
    d.color = lv_color_hex(color);
    d.opa = LV_OPA_COVER;
    lv_draw_arc(layer, &d);
}

/** Ring, dot and bars; LVGL hands in only the areas being redrawn (_clip_area). The mark is a child object. */
static void on_draw(lv_event_t *e) {
    yk_vc_view_t *v = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(v->obj, &a);
    const int cx = a.x1 + v->half, cy = a.y1 + v->half;
    ring(layer, cx, cy, v->px.r, v->px.ring_w, VC_INK);
    disc(layer, cx, cy - v->px.dot_y, v->px.dot_r, VC_CORAL);
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.width = v->px.bar_w;
    d.opa = LV_OPA_COVER;
    d.round_start = d.round_end = 1;
    const int32_t pad = v->px.bar_w;
    for (int k = 0; k < YK_VC_BARS; k++) {
        const int16_t *p = v->ends[k];
        if (!clip_meets(layer, cx + LV_MIN(p[0], p[2]) - pad, cy + LV_MIN(p[1], p[3]) - pad,
                        cx + LV_MAX(p[0], p[2]) + pad, cy + LV_MAX(p[1], p[3]) + pad))
            continue;
        d.p1.x = cx + p[0], d.p1.y = cy + p[1];
        d.p2.x = cx + p[2], d.p2.y = cy + p[3];
        d.color = lv_color_hex(v->colour[k] == YK_VC_LEVELS - 1 ? VC_CORAL : VC_INK);
        lv_draw_line(layer, &d);
    }
}

/** The bars at the animation's lengths: only the boxes of pairs that changed are redrawn. Returns whether any. */
static bool update_bars(yk_vc_view_t *v) {
    bool dirty[YK_VC_BOXES] = {0};
    for (int k = 0; k < YK_VC_BARS; k++) {
        int16_t ends[4];
        yk_vc_bar_px(&v->px, k, v->anim.lengths[k], ends);
        const uint8_t level = (uint8_t)yk_vc_nearest_level(v->anim.lengths[k]);
        if (memcmp(ends, v->ends[k], sizeof(ends)) || level != v->colour[k]) {
            memcpy(v->ends[k], ends, sizeof(ends));
            v->colour[k] = level;
            dirty[k / 2] = true;
        }
    }
    lv_area_t a;
    lv_obj_get_coords(v->obj, &a);
    const int cx = a.x1 + v->half, cy = a.y1 + v->half;
    bool any = false;
    for (int j = 0; j < YK_VC_BOXES; j++) {
        if (!dirty[j]) continue;
        const int16_t *b = v->boxes[j];
        const lv_area_t box = {cx + b[0], cy + b[1], cx + b[2], cy + b[3]};
        lv_obj_invalidate_area(v->obj, &box);
        any = true;
    }
    return any;
}

static void on_refr_ready(lv_event_t *e);

/** The timer and the wait for the display's refreshes gone. */
static void stop_talking(yk_vc_view_t *v) {
    if (v->timer) {
        lv_timer_delete(v->timer);
        v->timer = NULL;
    }
    if (v->disp) {
        lv_display_remove_event_cb_with_user_data(v->disp, on_refr_ready, v);
        v->disp = NULL;
    }
    v->waiting = false;
}

static void settle(yk_vc_view_t *v) {
    v->settled = true;
    stop_talking(v);
    if (v->on_settled) v->on_settled(v, v->settled_ctx);
}

static void on_frame(lv_timer_t *timer) {
    yk_vc_view_t *v = lv_timer_get_user_data(timer);
    v->frame_ms = now_ms();
    const bool done = yk_vc_anim_step(&v->anim, (int)(v->frame_ms - v->t0_ms));
    const bool drawn = update_bars(v);
    v->frames++;
    if (done) {
        settle(v);
        return;
    }
    // Something to draw: the next frame once it is on the screen (on_refr_ready), or YK_VC_WAIT_MS on if that never
    // comes. Nothing: in YK_VC_FRAME_MS, as ever.
    v->waiting = drawn && v->disp;
    lv_timer_set_period(timer, v->waiting ? YK_VC_WAIT_MS : YK_VC_FRAME_MS);
}

/** The display's refresh done (the frame on the screen): the next frame YK_VC_YIELD_MS from now at least, the LVGL
 *  task asleep and its lock free in between. */
static void on_refr_ready(lv_event_t *e) {
    yk_vc_view_t *v = lv_event_get_user_data(e);
    if (!v->waiting || !v->timer) return;
    v->waiting = false;
    lv_timer_set_period(v->timer, (uint32_t)yk_vc_next_frame_ms((int)(now_ms() - v->frame_ms)));
    lv_timer_reset(v->timer);
}

static void on_delete(lv_event_t *e) {
    yk_vc_view_t *v = lv_event_get_user_data(e);
    stop_talking(v);
    free(v);
}

// ---------------------------------------------------------------- the widget

yk_vc_view_t *yk_vc_view_create(lv_obj_t *parent, int cx, int cy, int r) {
    // Its state in PSRAM: internal RAM is the scarce one (firmware guide, 40,000 bytes at least).
    yk_vc_view_t *v = heap_caps_calloc(1, sizeof(*v), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!v) v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    yk_vc_px(r, &v->px);
    v->half = v->px.reach + 2;
    for (int j = 0; j < YK_VC_BOXES; j++) yk_vc_box_px(&v->px, j, 1, v->boxes[j]);
    yk_vc_anim_init(&v->anim);
    for (int k = 0; k < YK_VC_BARS; k++) yk_vc_bar_px(&v->px, k, v->anim.lengths[k], v->ends[k]);
    v->settled = true;  // nothing to talk about yet

    lv_obj_t *o = v->obj = lv_obj_create(parent);
    if (!o) {
        free(v);
        return NULL;
    }
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 2 * v->half, 2 * v->half);
    lv_obj_set_pos(o, cx - v->half, cy - v->half);
    lv_obj_set_clickable(o, false);  // taps go to what is under it
    lv_obj_set_scrollable(o, false);
    lv_obj_add_event_cb(o, on_draw, LV_EVENT_DRAW_MAIN, v);
    lv_obj_add_event_cb(o, on_delete, LV_EVENT_DELETE, v);
    v->mark = yk_ui_mark(o, v->half, v->half, v->px.logo);
    return v;
}

void yk_vc_view_set_middle(yk_vc_view_t *v, const char *text) {
    if (!v) return;
    if (v->mark) lv_obj_set_hidden(v->mark, text != NULL);
    if (!text) {
        if (v->middle) lv_obj_set_hidden(v->middle, true);
        return;
    }
    if (!v->middle) {
        // As wide as the logo's circle allows at a digit's height; centred on the code.
        const float in = YK_VC_LOGO_MAX_R * v->px.r, h = YK_VC_DIGITS_H / 2.0f;
        const int w = 2 * (int)sqrtf(in * in - h * h);
        v->middle = yk_ui_label(v->obj, "", v->half - w / 2, 0, w, YK_VC_MIDDLE_PX, VC_INK);
        lv_label_set_long_mode(v->middle, LV_LABEL_LONG_MODE_CLIP);
    }
    lv_label_set_text(v->middle, text);
    lv_obj_set_hidden(v->middle, false);
    lv_obj_update_layout(v->middle);
    lv_obj_set_y(v->middle, v->half - lv_obj_get_height(v->middle) / 2);
}

int yk_vc_view_set(yk_vc_view_t *v, const yk_vc_payload_t *payload, bool talk) {
    uint8_t levels[YK_VC_BARS];
    if (!v || yk_vc_encode(payload, levels) != 0) return -1;
    yk_vc_anim_start(&v->anim, levels, esp_random() | 1);
    v->t0_ms = now_ms();
    v->settled = false;
    if (!talk) {
        yk_vc_anim_step(&v->anim, YK_VC_ANIM_MS);
        update_bars(v);
        settle(v);
        return 0;
    }
    yk_vc_anim_step(&v->anim, 0);
    v->frame_ms = v->t0_ms;
    v->frames = 1;
    update_bars(v);
    if (!v->timer) v->timer = lv_timer_create(on_frame, YK_VC_FRAME_MS, v);
    if (!v->timer) return 0;
    if (!v->disp) {
        v->disp = lv_obj_get_display(v->obj);
        if (v->disp) lv_display_add_event_cb(v->disp, on_refr_ready, LV_EVENT_REFR_READY, v);
    }
    // The first frame too (the whole screen when the code's layer comes up, ~110 ms): the next once it is on the
    // screen and LVGL has slept.
    v->waiting = v->disp != NULL;
    lv_timer_set_period(v->timer, v->waiting ? YK_VC_WAIT_MS : YK_VC_FRAME_MS);
    lv_timer_reset(v->timer);
    return 0;
}

void yk_vc_view_on_settled(yk_vc_view_t *v, yk_vc_settled_fn fn, void *ctx) {
    v->on_settled = fn;
    v->settled_ctx = ctx;
}

bool yk_vc_view_settled(const yk_vc_view_t *v) { return v && v->settled; }

lv_obj_t *yk_vc_view_obj(yk_vc_view_t *v) { return v ? v->obj : NULL; }

// ---------------------------------------------------------------- the app's code over its screen

static struct {
    lv_obj_t *root;  // pure black over the app's screen, takes the taps; the view is its child
    yk_vc_view_t *view;
    lv_timer_t *hide_timer;
    yk_vc_show_opts_t opts;
    yk_vc_payload_t payload;
} s;

static const char *hidden_name(yk_vc_hidden_t why) {
    static const char *const names[] = {"app", "timeout", "tap", "key", "replaced", "swipe"};
    return (unsigned)why < sizeof(names) / sizeof(names[0]) ? names[why] : "?";
}

/** Forgets the shown code and tells the app; the objects go with the root (deleted by the caller or already). */
static void forget(yk_vc_hidden_t why) {
    if (s.hide_timer) {
        lv_timer_delete(s.hide_timer);
        s.hide_timer = NULL;
    }
    const yk_vc_show_opts_t o = s.opts;
    s.root = NULL;
    s.view = NULL;
    memset(&s.opts, 0, sizeof(s.opts));
    printf("YK_VC hidden why=%s\n", hidden_name(why));
    if (o.on_hidden) o.on_hidden(why, o.ctx);
}

static void hide_now(yk_vc_hidden_t why) {
    if (!s.root) return;
    lv_obj_t *root = s.root;
    lv_obj_set_hidden(root, true);
    forget(why);
    lv_obj_delete_async(root);  // perhaps in its own tap's event
}

static void on_root_tap(lv_event_t *e) {
    (void)e;
    if (!s.opts.keep_on_tap) hide_now(YK_VC_HIDDEN_TAP);
}

/** A swipe on it, any way: its own (the app's screen never sees it), and its release goes nowhere. */
static void on_root_swipe(lv_event_t *e) {
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (indev) lv_indev_wait_release(indev);
    if (!s.opts.keep_on_tap) hide_now(YK_VC_HIDDEN_SWIPE);
}

/** The app's screen went away with the code on it. */
static void on_root_delete(lv_event_t *e) {
    if (lv_event_get_target(e) == s.root) forget(YK_VC_HIDDEN_APP);
}

static void on_hide_timer(lv_timer_t *timer) {
    (void)timer;
    s.hide_timer = NULL;  // a single run: LVGL deletes it after this
    hide_now(YK_VC_HIDDEN_TIMEOUT);
}

static void on_shown_settled(yk_vc_view_t *view, void *ctx) {
    (void)ctx;
    if (view != s.view) return;
    // The bars only for codes that carry no secret: a pairing code's bars are its 8 digits.
    if (s.payload.kind == YK_VC_APP || s.payload.kind == YK_VC_TEST) {
        char levels[YK_VC_BARS + 1];
        for (int k = 0; k < YK_VC_BARS; k++) levels[k] = (char)('0' + view->anim.target[k]);
        levels[YK_VC_BARS] = 0;
        printf("YK_VC settled levels=%s frames=%d\n", levels, view->frames);
    } else {
        printf("YK_VC settled frames=%d\n", view->frames);
    }
}

esp_err_t yk_vc_show_payload(const yk_vc_payload_t *payload, const yk_vc_show_opts_t *opts) {
    uint8_t levels[YK_VC_BARS];
    if (!payload || yk_vc_encode(payload, levels) != 0) return ESP_ERR_INVALID_ARG;
    if (board_lock(-1) != ESP_OK) return ESP_ERR_TIMEOUT;
    if (!lv_screen_active()) {
        board_unlock();
        return ESP_ERR_INVALID_STATE;  // no screen yet
    }
    // A code shown before gives way: its bars talk on from where they are; it is told once this one is up.
    const bool replacing = s.root != NULL;
    const yk_vc_show_opts_t was = s.opts;
    if (replacing) {
        if (s.hide_timer) {
            lv_timer_delete(s.hide_timer);
            s.hide_timer = NULL;
        }
        printf("YK_VC hidden why=%s\n", hidden_name(YK_VC_HIDDEN_REPLACED));
    } else {
        // Over the app's screen, so that the app ring (opened on the screen later) still comes above it.
        lv_obj_t *root = lv_obj_create(lv_screen_active());
        if (!root) {
            board_unlock();
            return ESP_ERR_NO_MEM;
        }
        lv_obj_remove_style_all(root);
        lv_obj_set_size(root, 2 * YK_CX, 2 * YK_CY);
        lv_obj_set_pos(root, 0, 0);
        lv_obj_set_style_bg_color(root, lv_color_hex(VC_BG), 0);
        lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
        lv_obj_set_scrollable(root, false);
        lv_obj_set_clickable(root, true);
        lv_obj_set_gesture_bubble(root, false);  // a swipe on the code is not the app's page's
        lv_obj_move_foreground(root);
        yk_vc_view_t *view = yk_vc_view_create(root, YK_CX, YK_CY, YK_VC_VIEW_R);
        if (!view) {
            lv_obj_delete(root);
            board_unlock();
            return ESP_ERR_NO_MEM;
        }
        lv_obj_add_event_cb(root, on_root_tap, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(root, on_root_swipe, LV_EVENT_GESTURE, NULL);
        lv_obj_add_event_cb(root, on_root_delete, LV_EVENT_DELETE, NULL);
        yk_vc_view_on_settled(view, on_shown_settled, NULL);
        s.root = root;
        s.view = view;
    }
    s.opts = opts ? *opts : (yk_vc_show_opts_t){0};
    s.payload = *payload;
    char text[48];
    yk_vc_payload_text(payload, text, sizeof(text));
    printf("YK_VC show %s\n", text);
    yk_vc_view_set(s.view, payload, true);
    const int after_s = s.opts.hide_after_s ? s.opts.hide_after_s : YK_VC_HIDE_AFTER_S;
    if (after_s > 0) {
        s.hide_timer = lv_timer_create(on_hide_timer, (uint32_t)after_s * 1000u, NULL);
        if (s.hide_timer) lv_timer_set_repeat_count(s.hide_timer, 1);
    }
    if (replacing && was.on_hidden) was.on_hidden(YK_VC_HIDDEN_REPLACED, was.ctx);
    board_unlock();
    return ESP_OK;
}

esp_err_t yk_vc_show(uint32_t action, uint32_t value, const yk_vc_show_opts_t *opts) {
    yk_vc_payload_t p;
    uint8_t levels[YK_VC_BARS];
    const int r = yk_vc_app_code(action, value, &p, levels);
    if (r == YK_VC_NO_NUMBER) {
        printf("YK_VC no number: app.json has no voiceCode\n");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (r != 0) return ESP_ERR_INVALID_ARG;
    return yk_vc_show_payload(&p, opts);
}

void yk_vc_hide(void) {
    if (board_lock(-1) != ESP_OK) return;
    hide_now(YK_VC_HIDDEN_APP);
    board_unlock();
}

bool yk_vc_showing(void) {
    if (board_lock(-1) != ESP_OK) return false;
    const bool shown = s.root != NULL;
    board_unlock();
    return shown;
}

bool yk_vc_settled(void) {
    if (board_lock(-1) != ESP_OK) return false;
    const bool settled = s.root && yk_vc_view_settled(s.view);
    board_unlock();
    return settled;
}

static void (*s_ring_hidden)(int why);

static void ring_hidden(yk_vc_hidden_t why, void *ctx) {
    (void)ctx;
    void (*fn)(int) = s_ring_hidden;
    s_ring_hidden = NULL;
    if (fn) fn((int)why);
}

esp_err_t yk_vc_show_ring(void (*hidden)(int why)) {
    const yk_vc_show_opts_t o = {.on_hidden = ring_hidden};
    if (board_lock(-1) != ESP_OK) return ESP_ERR_TIMEOUT;  // (recursive: yk_vc_show takes it again)
    if (s.root && s.opts.on_hidden == ring_hidden) {  // the ring's code is up already: it stays as it is
        board_unlock();
        return ESP_OK;
    }
    s_ring_hidden = hidden;
    const esp_err_t err = yk_vc_show(YK_VC_ACTION_PAGE, 0, &o);
    if (err != ESP_OK) s_ring_hidden = NULL;
    board_unlock();
    return err;
}

bool yk_vc_on_key(const yk_key_event_t *event) {
    if (!event || event->type != YK_KEY_TAP) return false;
    if (board_lock(-1) != ESP_OK) return false;
    const bool shown = s.root != NULL;
    if (shown) hide_now(YK_VC_HIDDEN_KEY);
    board_unlock();
    return shown;
}

// ---------------------------------------------------------------- the pairing page

static void on_pair_settled(yk_vc_view_t *view, void *ctx) {
    (void)ctx;
    printf("YK_VC pair settled frames=%d\n", view->frames);
}

lv_obj_t *yk_vc_pair_layer(lv_obj_t *screen, const char *suffix, const char *digits) {
    yk_vc_payload_t p;
    if (!screen || yk_vc_pair(suffix, digits, &p) != 0) return NULL;
    lv_obj_t *layer = lv_obj_create(screen);
    if (!layer) return NULL;
    lv_obj_remove_style_all(layer);
    lv_obj_set_size(layer, 2 * YK_CX, 2 * YK_CY);
    lv_obj_set_pos(layer, 0, 0);
    lv_obj_set_style_bg_color(layer, lv_color_hex(VC_BG), 0);
    lv_obj_set_style_bg_opa(layer, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(layer, false);
    lv_obj_set_clickable(layer, true);
    yk_vc_view_t *view = yk_vc_view_create(layer, YK_CX, YK_CY, YK_VC_VIEW_R);
    if (!view) {
        lv_obj_delete(layer);
        return NULL;
    }
    char text[16];
    snprintf(text, sizeof(text), "%.4s %.4s", digits, digits + 4);
    yk_vc_view_set_middle(view, text);
    yk_vc_view_on_settled(view, on_pair_settled, NULL);
    yk_vc_view_set(view, &p, true);
    char shown[48];
    yk_vc_payload_text(&p, shown, sizeof(shown));
    printf("YK_VC show %s\n", shown);  // "pair YK-dd76": never the digits
    return layer;
}
