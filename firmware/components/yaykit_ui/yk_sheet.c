// The system pull-down (yk_sheet.h): the main firmware's status sheet as it was in yay_pages.c, moved here so that
// every firmware draws the same one; the round-screen spec's sizes since 0.1.14.
#include "yk_sheet.h"

#include <string.h>

#include "cjk_font.h"
#include "yaykit_ui.h"
#include "yk_round.h"
#include "yk_accent.h"

#define CARD 0x1F2521      // the canvas's card (the main firmware's pages)
#define ON_CORAL 0x1B1411  // words on a coral button
#define ROW_H 34           // a row (18 px, YK_SIZE_STATUS), its top to the next one's
#define ROWS_Y 152         // the rows' top on the panel (96 px down the screen)
#define BUTTON_Y 226       // the button under the line (offline)
#define BUTTON_GAP 16      // ... or this far under the last row
#define BUTTON2_W 220      // the second button (a longer word: 「退出极简模式」, "Exit minimal mode")
#define BUTTON2_GAP 12     // ... under the first
#define PANEL_Y (-56)      // the panel's top (only its lower corners show rounded)
#define PANEL_H 374        // ... and its height, unless a second button wants more
#define HINT_Y 330         // the hint on the panel
#define HINT_BELOW 16      // ... this far under a second button
#define BUTTON_H 46
#define HINT_GAP 10        // the hint this far under the first button when four rows push it down (F-1077: 360)

struct yk_sheet {
    lv_obj_t *layer, *panel, *clock, *rows, *name[YK_SHEET_ROWS], *value[YK_SHEET_ROWS], *title, *line, *button,
        *button_label, *hint;
    lv_obj_t *button2, *button2_label;  // made at the first view that has one
    bool swipes;                        // yk_sheet_take_swipes: what it did, done to the second button too
    lv_event_cb_t on_swipe;
    void *swipe_ctx;
    lv_obj_t *hit[YK_SHEET_ROWS];  // a row that takes taps (yk_sheet_on_row): over its words; made only then
    yk_sheet_row_cb_t on_row;
    void *row_ctx;
};

static void take_swipes(lv_obj_t *o, lv_event_cb_t on_swipe, void *ctx) {
    lv_obj_set_gesture_bubble(o, false);
    lv_obj_set_scroll_chain(o, false);
    if (on_swipe) lv_obj_add_event_cb(o, on_swipe, LV_EVENT_GESTURE, ctx);
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, int32_t bg) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_scrollable(o, false);
    if (bg >= 0) {
        lv_obj_set_style_bg_color(o, lv_color_hex((uint32_t)bg), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    }
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, int x, int y, int w, int px, uint32_t color, lv_text_align_t align) {
    lv_obj_t *o = lv_label_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, w);
    lv_obj_set_style_text_align(o, align, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    const lv_font_t *f = cjk_font_get(px);
    if (f) lv_obj_set_style_text_font(o, f, 0);
    lv_label_set_text(o, "");
    return o;
}

/** One line, cut with … (a long network name or nickname must not spill onto the next row or the button). */
static void one_line(lv_obj_t *o, int height) {
    lv_label_set_long_mode(o, LV_LABEL_LONG_DOT);
    lv_obj_set_height(o, height);
}

static void set(lv_obj_t *o, const char *s) {
    if (strcmp(lv_label_get_text(o), s ? s : "")) lv_label_set_text(o, s ? s : "");
}

/** A new colour only when it is one: LVGL restyles (and redraws) on every set. */
static void color(lv_obj_t *o, uint32_t c) {
    if (!lv_color_eq(lv_obj_get_style_text_color(o, 0), lv_color_hex(c))) lv_obj_set_style_text_color(o, lv_color_hex(c), 0);
}

static void hidden(lv_obj_t *o, bool hide) {
    if (lv_obj_is_hidden(o) != hide) lv_obj_set_hidden(o, hide);
}

static void on_delete(lv_event_t *e) { lv_free(lv_event_get_user_data(e)); }

static void row_clicked(lv_event_t *e) {
    yk_sheet_t *s = lv_event_get_user_data(e);
    lv_obj_t *o = lv_event_get_current_target_obj(e);
    for (int i = 0; i < YK_SHEET_ROWS; i++)
        if (s->hit[i] == o && s->on_row) s->on_row(i, s->row_ctx);
}

yk_sheet_t *yk_sheet_create(lv_obj_t *parent, lv_event_cb_t on_dim, void *dim_ctx, lv_event_cb_t on_button,
                            void *button_ctx) {
    yk_sheet_t *s = lv_malloc_zeroed(sizeof(*s));
    if (!s) return NULL;
    lv_obj_t *l = s->layer = box(parent, 0, 0, 466, 466, 0x000000);
    lv_obj_set_style_bg_opa(l, LV_OPA_50, 0);
    lv_obj_set_clickable(l, true);
    lv_obj_set_hidden(l, true);
    lv_obj_add_event_cb(l, on_delete, LV_EVENT_DELETE, s);
    // A tap on the dimmed part closes it (round-screen-gestures.md 7); the panel takes its own taps, so that only a tap
    // below it reaches the layer.
    // A tap is down and up on the dimmed part, the finger still (0.1.18): a press on it let go on the panel is none.
    yk_ui_tap_only(l);
    if (on_dim) lv_obj_add_event_cb(l, on_dim, LV_EVENT_CLICKED, dim_ctx);
    s->panel = box(l, 0, PANEL_Y, 466, PANEL_H, CARD);
    lv_obj_set_style_radius(s->panel, 56, 0);
    lv_obj_set_clickable(s->panel, true);
    s->clock = text(s->panel, 83, 96, 300, 32, YK_INK, LV_TEXT_ALIGN_CENTER);
    // The rows from 96 px down the screen: 294 wide (x 86..380), in the safe circle (r 200) from the first row's glyphs
    // down (84..382 at y 100, wider below). The names are short (「今天还能用」 is 90 px at 18); the values get the rest, 196: a
    // network's name with its signal (「Xiaomi_5334 · 信号好」, 182) fits on one line (on device, 11-02: at 180 it was cut).
    s->rows = box(s->panel, 86, ROWS_Y, 294, YK_SHEET_ROWS * ROW_H, -1);
    for (int i = 0; i < YK_SHEET_ROWS; i++) {
        s->name[i] = text(s->rows, 0, i * ROW_H, 110, YK_SIZE_STATUS, YK_INK2, LV_TEXT_ALIGN_LEFT);
        s->value[i] = text(s->rows, 98, i * ROW_H, 196, YK_SIZE_STATUS, YK_INK, LV_TEXT_ALIGN_RIGHT);
        one_line(s->value[i], 26);
    }

    s->title = text(s->panel, 83, 154, 300, 20, YK_CORAL_TEXT, LV_TEXT_ALIGN_CENTER);
    s->line = text(s->panel, 83, 188, 300, YK_SIZE_MIN, YK_INK2, LV_TEXT_ALIGN_CENTER);
    one_line(s->line, 22);
    s->button = box(s->panel, 153, BUTTON_Y, 160, BUTTON_H, YK_CORAL);
    lv_obj_set_style_radius(s->button, 23, 0);
    s->button_label = text(s->button, 0, 10, 160, YK_SIZE_STATUS, ON_CORAL, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_clickable(s->button, true);
    yk_ui_tap_only(s->button);  // down on it and let go off it: no tap
    if (on_button) lv_obj_add_event_cb(s->button, on_button, LV_EVENT_CLICKED, button_ctx);
    s->hint = text(s->panel, 83, HINT_Y, 300, YK_SIZE_MIN, YK_INK3, LV_TEXT_ALIGN_CENTER);
    return s;
}

lv_obj_t *yk_sheet_obj(const yk_sheet_t *sheet) { return sheet ? sheet->layer : NULL; }

/** The second button, made the first time it is wanted (a sheet without one has nothing more than before). */
static lv_obj_t *button2(yk_sheet_t *s) {
    if (s->button2) return s->button2;
    s->button2 = box(s->panel, (466 - BUTTON2_W) / 2, BUTTON_Y, BUTTON2_W, 46, YK_CORAL);
    lv_obj_set_style_radius(s->button2, 23, 0);
    s->button2_label = text(s->button2, 0, 10, BUTTON2_W, YK_SIZE_STATUS, ON_CORAL, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_clickable(s->button2, true);
    yk_ui_tap_only(s->button2);
    lv_obj_set_hidden(s->button2, true);
    if (s->swipes) take_swipes(s->button2, s->on_swipe, s->swipe_ctx);
    return s->button2;
}

void yk_sheet_on_button2(yk_sheet_t *s, lv_event_cb_t on_button2, void *ctx) {
    if (s && on_button2) lv_obj_add_event_cb(button2(s), on_button2, LV_EVENT_CLICKED, ctx);
}

void yk_sheet_fill(yk_sheet_t *s, const yk_sheet_view_t *v) {
    if (!s || !v) return;
    set(s->clock, v->clock);
    const bool rows = !v->title;
    hidden(s->rows, !rows);
    hidden(s->title, rows);
    hidden(s->line, rows);
    hidden(s->button, !v->button);
    int button_y = BUTTON_Y;
    if (rows) {
        int k = 0;  // the rows shown so far: one without a name leaves no gap
        for (int i = 0; i < YK_SHEET_ROWS; i++) {
            const yk_sheet_row_t *r = &v->row[i];
            hidden(s->name[i], !r->name);
            hidden(s->value[i], !r->name);
            if (s->hit[i]) hidden(s->hit[i], !r->name || !r->tap);
            if (!r->name) continue;
            if (lv_obj_get_y(s->name[i]) != k * ROW_H) {
                lv_obj_set_y(s->name[i], k * ROW_H);
                lv_obj_set_y(s->value[i], k * ROW_H);
            }
            if (s->hit[i] && lv_obj_get_y(s->hit[i]) != k * ROW_H - 4) lv_obj_set_y(s->hit[i], k * ROW_H - 4);
            set(s->name[i], r->name);
            set(s->value[i], r->value);
            color(s->value[i], r->problem ? YK_CORAL_TEXT : YK_INK);
            k++;
        }
        button_y = ROWS_Y + k * ROW_H + BUTTON_GAP;
    } else {
        set(s->title, v->title);
        color(s->title, v->title_calm ? YK_INK : YK_CORAL_TEXT);
        set(s->line, v->line);
    }
    if (v->button) {
        // The main action: the account's accent, whichever it is now (0.1.19 4.7); the title over it stays coral.
        lv_obj_set_style_bg_color(s->button, lv_color_hex(yk_accent()), 0);
        color(s->button_label, yk_accent_on());
        set(s->button_label, v->button);
        if (lv_obj_get_y(s->button) != button_y) lv_obj_set_y(s->button, button_y);
    }
    // Four rows and the first button (an app with 「音量」 and 「在手机上打开」, F-1077): the hint goes under the button, the
    // panel growing with it (its words stay in the safe circle: 304..324 down the screen, 300 wide).
    int panel_h = PANEL_H, hint_y = HINT_Y;
    if (v->button && button_y + BUTTON_H + HINT_GAP > hint_y) {
        hint_y = button_y + BUTTON_H + HINT_GAP;
        panel_h = hint_y + 44;
    }
    // A second button under the first (or where the first would be), as the first; the panel grows to take it and the
    // hint goes under it. Without one, the panel and the hint as above.
    if (v->button2) {
        lv_obj_t *b = button2(s);
        const int y = v->button ? button_y + 46 + BUTTON2_GAP : button_y;
        lv_obj_set_style_bg_color(b, lv_color_hex(yk_accent()), 0);
        color(s->button2_label, yk_accent_on());
        set(s->button2_label, v->button2);
        if (lv_obj_get_y(b) != y) lv_obj_set_y(b, y);
        hint_y = y + 46 + HINT_BELOW;
        if (hint_y + 44 > panel_h) panel_h = hint_y + 44;
    }
    if (s->button2) hidden(s->button2, !v->button2);
    if (lv_obj_get_height(s->panel) != panel_h) lv_obj_set_height(s->panel, panel_h);
    if (lv_obj_get_y(s->hint) != hint_y) lv_obj_set_y(s->hint, hint_y);
    hidden(s->hint, !v->hint);
    set(s->hint, v->hint);
}


void yk_sheet_on_row(yk_sheet_t *s, yk_sheet_row_cb_t cb, void *ctx) {
    if (!s) return;
    s->on_row = cb;
    s->row_ctx = ctx;
    // Over the words, one hit a row: the row's whole width, lit while pressed, shown only for a row with `tap` (the
    // next fill). Made here, not with the sheet: a firmware that never asks (Yay) has none.
    if (s->hit[0]) return;  // made already: only the callback changed
    for (int i = 0; i < YK_SHEET_ROWS; i++) {
        lv_obj_t *h = box(s->rows, 0, lv_obj_get_y(s->name[i]) - 4, 294, ROW_H, -1);
        lv_obj_set_style_radius(h, 10, 0);
        lv_obj_set_style_bg_color(h, lv_color_hex(YK_INK), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(h, LV_OPA_10, LV_STATE_PRESSED);
        lv_obj_set_hidden(h, true);
        lv_obj_set_clickable(h, true);
        yk_ui_tap_only(h);
        lv_obj_add_event_cb(h, row_clicked, LV_EVENT_CLICKED, s);
        if (s->swipes) take_swipes(h, s->on_swipe, s->swipe_ctx);
        s->hit[i] = h;
    }
}

void yk_sheet_take_swipes(yk_sheet_t *s, lv_event_cb_t on_swipe, void *ctx) {
    if (!s) return;
    // What a finger can press on it (the labels are not clickable): a swipe goes to the one pressed. A second button
    // made later gets the same (button2).
    lv_obj_t *const pressable[] = {s->layer, s->panel, s->button, s->button2};
    for (size_t i = 0; i < sizeof(pressable) / sizeof(pressable[0]); i++)
        if (pressable[i]) take_swipes(pressable[i], on_swipe, ctx);
    s->swipes = true;
    s->on_swipe = on_swipe;
    s->swipe_ctx = ctx;
    for (int i = 0; i < YK_SHEET_ROWS; i++)  // the rows that take taps, when yk_sheet_on_row came first
        if (s->hit[i]) take_swipes(s->hit[i], on_swipe, ctx);
}

void yk_sheet_delete(yk_sheet_t *sheet) {
    if (!sheet) return;
    lv_obj_set_hidden(sheet->layer, true);  // gone from this frame and from touches at once
    lv_obj_delete_async(sheet->layer);      // from its own events too; on_delete frees it
}
