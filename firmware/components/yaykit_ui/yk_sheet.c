// The system pull-down (yk_sheet.h): the main firmware's status sheet as it was in yay_pages.c, moved here so that
// every firmware draws the same one; the round-screen spec's sizes since 0.1.14.
#include "yk_sheet.h"

#include <string.h>

#include "cjk_font.h"
#include "yk_round.h"

#define CARD 0x1F2521      // the canvas's card (the main firmware's pages)
#define ON_CORAL 0x1B1411  // words on a coral button
#define ROW_H 34           // a row (18 px, YK_SIZE_STATUS), its top to the next one's
#define ROWS_Y 152         // the rows' top on the panel (96 px down the screen)
#define BUTTON_Y 226       // the button under the line (offline)
#define BUTTON_GAP 16      // ... or this far under the last row

struct yk_sheet {
    lv_obj_t *layer, *panel, *clock, *rows, *name[YK_SHEET_ROWS], *value[YK_SHEET_ROWS], *title, *line, *button,
        *button_label, *hint;
};

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
    if (on_dim) lv_obj_add_event_cb(l, on_dim, LV_EVENT_CLICKED, dim_ctx);
    s->panel = box(l, 0, -56, 466, 374, CARD);
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
    s->button = box(s->panel, 153, BUTTON_Y, 160, 46, YK_CORAL);
    lv_obj_set_style_radius(s->button, 23, 0);
    s->button_label = text(s->button, 0, 10, 160, YK_SIZE_STATUS, ON_CORAL, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_clickable(s->button, true);
    if (on_button) lv_obj_add_event_cb(s->button, on_button, LV_EVENT_CLICKED, button_ctx);
    s->hint = text(s->panel, 83, 330, 300, YK_SIZE_MIN, YK_INK3, LV_TEXT_ALIGN_CENTER);
    return s;
}

lv_obj_t *yk_sheet_obj(const yk_sheet_t *sheet) { return sheet ? sheet->layer : NULL; }

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
            if (!r->name) continue;
            if (lv_obj_get_y(s->name[i]) != k * ROW_H) {
                lv_obj_set_y(s->name[i], k * ROW_H);
                lv_obj_set_y(s->value[i], k * ROW_H);
            }
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
        set(s->button_label, v->button);
        if (lv_obj_get_y(s->button) != button_y) lv_obj_set_y(s->button, button_y);
    }
    hidden(s->hint, !v->hint);
    set(s->hint, v->hint);
}

void yk_sheet_take_swipes(yk_sheet_t *s, lv_event_cb_t on_swipe, void *ctx) {
    if (!s) return;
    // What a finger can press on it (the labels are not clickable): a swipe goes to the one pressed.
    lv_obj_t *const pressable[] = {s->layer, s->panel, s->button};
    for (size_t i = 0; i < sizeof(pressable) / sizeof(pressable[0]); i++) {
        lv_obj_set_gesture_bubble(pressable[i], false);
        lv_obj_set_scroll_chain(pressable[i], false);
        if (on_swipe) lv_obj_add_event_cb(pressable[i], on_swipe, LV_EVENT_GESTURE, ctx);
    }
}

void yk_sheet_delete(yk_sheet_t *sheet) {
    if (!sheet) return;
    lv_obj_set_hidden(sheet->layer, true);  // gone from this frame and from touches at once
    lv_obj_delete_async(sheet->layer);      // from its own events too; on_delete frees it
}
