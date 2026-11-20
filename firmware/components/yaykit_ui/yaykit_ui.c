#include "yaykit_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "cjk_font.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "yk_round.h"
#include "yk_apps_logic.h"
#include "yk_ui_priv.h"
#include "yk_ui_logic.h"
#include "yk_words.h"
#include "yk_accent.h"

static volatile bool capturing;

// The language is yaykit_strings' (yk_words_set): English or not, as before, for the callers that say only that. On
// keeps a Japanese or Spanish screen as it is; off is Chinese.
void yk_ui_set_english(bool on) {
    if (on == yk_words_chinese()) yk_words_set(on ? YK_LOCALE_EN : YK_LOCALE_ZH_HANS);
}

bool yk_ui_is_english(void) { return !yk_words_chinese(); }

const char *yk_ui_buddy_name(void) { return yk_buddy_name(board_pref_buddy()); }

// The accent's colourway, from the shared setting the main firmware keeps (yk_accent.h; board_pref_colorway's order is
// the table's, packages/contracts COLORWAYS).
int yk_accent_stored(void) { return board_pref_colorway(); }

lv_obj_t *yk_ui_label(lv_obj_t *parent, const char *text, int x, int y, int width, int size, uint32_t color) {
    lv_obj_t *o = lv_label_create(parent);
    lv_label_set_text(o, text);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, width);
    lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    const lv_font_t *font = cjk_font_get(size);
    if (font) lv_obj_set_style_text_font(o, font, 0);
    return o;
}

// Every tap on the device moves no further than LVGL's scroll limit: the rings, 「我的应用」, the buttons here, and Yay's
// whole-screen tap (yay_run.c, LV_INDEV_DEF_SCROLL_LIMIT).
_Static_assert(YK_TAP_SLOP == LV_INDEV_DEF_SCROLL_LIMIT, "a tap's slop is LVGL's scroll limit everywhere");

// One press at a time (one finger, one pointer): what it went down on and how it went.
static struct {
    lv_obj_t *obj;
    yk_tap_t tap;
    bool tapped;  // its CLICKED's verdict
    uint32_t presses;  // every press on a guarded object, tap or not (yk_ui_last_tap)
    bool have_last;
    yk_ui_tap_t last;  // the last tap let through
} s_tap;

static lv_point_t finger(void) {
    lv_point_t p = {0};
    lv_indev_t *in = lv_indev_active();
    if (in) lv_indev_get_point(in, &p);
    return p;
}

/** Ahead of everything else on the object (LV_EVENT_PREPROCESS): a CLICKED that was no tap stops here. */
static void tap_guard(lv_event_t *e) {
    lv_obj_t *o = lv_event_get_current_target_obj(e), *pressed = lv_event_get_target_obj(e);
    lv_point_t p = finger();
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        s_tap.presses++;  // one guard per object (yk_ui_tap_only): once per press
        s_tap.obj = pressed;
        yk_tap_press(&s_tap.tap, 0, p.x, p.y, lv_tick_get());
        break;
    case LV_EVENT_PRESSING:
        if (s_tap.obj == pressed) yk_tap_move(&s_tap.tap, p.x, p.y);
        break;
    case LV_EVENT_PRESS_LOST:
        if (s_tap.obj == pressed) yk_tap_cancel(&s_tap.tap);
        break;
    case LV_EVENT_CLICKED:
        // Let go on what it went down on: the topmost clickable thing under the finger there (a panel over a dimmed
        // layer is not the layer).
        if (s_tap.obj == pressed && s_tap.tap.down) {
            const uint32_t now = lv_tick_get();
            s_tap.tapped = yk_tap_release(&s_tap.tap, lv_indev_search_obj(o, &p) == pressed ? 0 : -1, p.x, p.y, now);
            if (s_tap.tapped) {
                s_tap.last = (yk_ui_tap_t){.x = s_tap.tap.x0, .y = s_tap.tap.y0, .down_ms = s_tap.tap.t0, .up_ms = now,
                                           .press = s_tap.presses};
                s_tap.have_last = true;
            }
            char line[64];
            puts(yk_tap_line(&s_tap.tap, "button", line, sizeof(line)));
        }
        if (s_tap.obj != pressed || !s_tap.tapped) lv_event_stop_processing(e);
        break;
    default:
        break;
    }
}

void yk_ui_tap_only(lv_obj_t *o) {
    if (!o) return;
    lv_obj_remove_event_cb(o, tap_guard);  // asked again: still one guard (a press counts once, yk_ui_last_tap)
    static const lv_event_code_t codes[] = {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_PRESS_LOST, LV_EVENT_CLICKED};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
        lv_obj_add_event_cb(o, tap_guard, (lv_event_code_t)(codes[i] | LV_EVENT_PREPROCESS), NULL);
}

bool yk_ui_last_tap(yk_ui_tap_t *out) {
    if (!s_tap.have_last) return false;
    if (out) *out = s_tap.last;
    return true;
}

// ---------------------------------------------------------------- the rule for every object (0.1.20, F-1018)

#define OWN_TAPS_MAX 8
static lv_obj_t *s_own[OWN_TAPS_MAX];  // roots that judge their own taps (yk_ui_own_taps)
static struct {
    lv_obj_t *obj;  // what the press went down on (the indev's object)
    yk_tap_t tap;
    bool tapped;    // its RELEASED's verdict, for the CLICKED (and SHORT_CLICKED) after it
    bool on;
} s_strict;

static void own_gone(lv_event_t *e) {
    lv_obj_t *o = lv_event_get_current_target_obj(e);
    for (int i = 0; i < OWN_TAPS_MAX; i++)
        if (s_own[i] == o) s_own[i] = NULL;
}

void yk_ui_own_taps(lv_obj_t *root) {
    if (!root) return;
    for (int i = 0; i < OWN_TAPS_MAX; i++)
        if (s_own[i] == root) return;
    for (int i = 0; i < OWN_TAPS_MAX; i++)
        if (!s_own[i]) {
            s_own[i] = root;
            lv_obj_add_event_cb(root, own_gone, LV_EVENT_DELETE, NULL);
            return;
        }
}

static bool judges_itself(lv_obj_t *o) {
    for (; o; o = lv_obj_get_parent(o))
        for (int i = 0; i < OWN_TAPS_MAX; i++)
            if (s_own[i] == o) return true;
    return false;
}

/** The input device's own events, before the object's (LVGL: lv_indev_stop_processing keeps it from the object). */
static void strict_event(lv_event_t *e) {
    lv_indev_t *in = lv_indev_active();
    lv_obj_t *obj = lv_event_get_param(e);
    if (!in || !obj) return;
    lv_point_t p = {0};
    lv_indev_get_point(in, &p);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        s_strict.obj = obj;
        s_strict.tapped = false;
        yk_tap_press(&s_strict.tap, 0, p.x, p.y, lv_tick_get());
        break;
    case LV_EVENT_RELEASED: {
        // Let go over it (or something inside it): the topmost clickable thing there, on its own screen or layer.
        lv_obj_t *under = lv_indev_search_obj(lv_obj_get_screen(obj), &p);
        bool over = false;
        for (lv_obj_t *u = under; u && !over; u = lv_obj_get_parent(u)) over = u == obj;
        const bool swiped = lv_indev_get_press_moved(in) || lv_indev_get_gesture_dir(in) != LV_DIR_NONE;
        s_strict.tapped = yk_tap_strict(&s_strict.tap, s_strict.obj == obj, over, swiped, p.x, p.y, lv_tick_get());
        break;
    }
    case LV_EVENT_SHORT_CLICKED:
    case LV_EVENT_CLICKED:
        if (s_strict.tapped || judges_itself(obj)) break;
        lv_indev_stop_processing(in);  // not to the object: no tap
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            char line[64];
            puts(yk_tap_line(&s_strict.tap, "strict", line, sizeof(line)));
        }
        break;
    default:
        break;
    }
}

static void strict_attach(lv_indev_t *indev) {
    static const lv_event_code_t codes[] = {LV_EVENT_PRESSED, LV_EVENT_RELEASED, LV_EVENT_SHORT_CLICKED, LV_EVENT_CLICKED};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) lv_indev_add_event_cb(indev, strict_event, codes[i], NULL);
}

void yk_ui_taps_strict(lv_indev_t *indev) {
    if (!indev || s_strict.on) return;
    s_strict.on = true;
    strict_attach(indev);
    puts("YK_TAP strict on");
}

// ---------------------------------------------------------------- every tap on the screen (kit 1.11)

static struct {
    yk_ui_screen_tap_fn fn;
    void *ctx;
    bool attached;   // to the touch screen (once)
    yk_tap_t tap;    // the press going on
    uint32_t presses;
    yk_ui_tap_do_t verdict;  // fn's for this press's clicks
} s_screen;

// The input devices whose reads we see first (the touch screen; yk_ui_finger's finger, the latest), and what they read
// with: a new press is fn's before LVGL processes it, and a held tap's clicks go out inside a read.
static struct {
    lv_indev_t *indev;
    lv_indev_read_cb_t read;
    bool down;  // its last read
} s_reads[2];

// The tap held back (YK_UI_TAP_HOLD): what it went down on and which of its clicks came.
static struct {
    lv_obj_t *obj;        // NULL: none held
    lv_indev_t *indev;
    bool short_clicked, clicked;
    bool kept;            // yk_ui_taps_strict kept them from the object: they stay kept
    bool due;             // yk_ui_screen_tap_give(): out at the next read of its input device
    uint32_t due_ms;
} s_held;
static lv_timer_t *s_held_timer;  // no read came (the screen asleep, the input device off): out without one

#define HELD_NO_READ_MS 100  // due this long with no read: given from a timer (lv_indev_active() NULL then)

static void held_gone(lv_event_t *e) {
    if (lv_event_get_current_target_obj(e) == s_held.obj) memset(&s_held, 0, sizeof(s_held));
}

/** None held from now on (its clicks given or not). */
static void held_clear(void) {
    if (s_held.obj) lv_obj_remove_event_cb(s_held.obj, held_gone);
    memset(&s_held, 0, sizeof(s_held));
    if (s_held_timer) lv_timer_pause(s_held_timer);
}

/** The held tap's clicks to its object now. Inside its input device's read (screen_read) lv_indev_active() is that
 *  device and its point the last one LVGL processed: the tap's own release when no press came since. */
static void held_send(const char *how) {
    lv_obj_t *o = s_held.obj;
    if (!o) return;
    lv_indev_t *in = s_held.indev;
    const bool give = !s_held.kept && !lv_obj_has_state(o, LV_STATE_DISABLED);
    const bool short_clicked = give && s_held.short_clicked, clicked = give && s_held.clicked;
    const uint32_t late = lv_tick_elaps(s_held.due_ms);
    held_clear();
    printf("YK_TAP given late short=%d clicked=%d by=%s due_ms=%u\n", short_clicked, clicked, how, (unsigned)late);
    if (short_clicked && lv_obj_send_event(o, LV_EVENT_SHORT_CLICKED, in) != LV_RESULT_OK) return;  // it went
    if (clicked) lv_obj_send_event(o, LV_EVENT_CLICKED, in);
}

static void held_no_read(lv_timer_t *t) {
    if (s_held.due && lv_tick_elaps(s_held.due_ms) >= HELD_NO_READ_MS) held_send("timer");
    else if (!s_held.due) lv_timer_pause(t);
}

void yk_ui_screen_tap_drop(void) { held_clear(); }

void yk_ui_screen_tap_give(void) {
    if (!s_held.obj || s_held.due) return;
    s_held.due = true;
    s_held.due_ms = lv_tick_get();
    if (!s_held_timer && (s_held_timer = lv_timer_create(held_no_read, HELD_NO_READ_MS, NULL)) == NULL) {
        held_send("now");  // no room for the timer: at once
        return;
    }
    lv_timer_reset(s_held_timer);
    lv_timer_resume(s_held_timer);
}

static void held_start(lv_obj_t *obj, lv_indev_t *in) {
    if (s_held.obj) held_send("next");  // one at a time
    s_held.obj = obj;
    s_held.indev = in;
    lv_obj_add_event_cb(obj, held_gone, LV_EVENT_DELETE, NULL);
}

/** Every read of a device of ours: its own read first; a finger that came down since the last is fn's press now, before
 *  LVGL processes it (a held tap it cannot be the second of goes out first, in order); then a held tap due goes out. */
static void screen_read(lv_indev_t *in, lv_indev_data_t *data) {
    int i = 0;
    while (i < 2 && s_reads[i].indev != in) i++;
    if (i == 2) return;  // (not ours: never)
    s_reads[i].read(in, data);
    const bool down = data->state == LV_INDEV_STATE_PRESSED;
    if (down && !s_reads[i].down && s_screen.fn) {
        const uint32_t now = lv_tick_get();
        s_screen.presses++;
        const yk_ui_tap_t t = {.x = (int16_t)data->point.x, .y = (int16_t)data->point.y, .down_ms = now, .up_ms = now,
                               .press = s_screen.presses};
        s_screen.fn(YK_UI_PRESS_DOWN, &t, s_screen.ctx);
    }
    s_reads[i].down = down;
    if (s_held.due && s_held.indev == in) held_send("read");
}

static void screen_event(lv_event_t *e) {
    lv_indev_t *in = lv_indev_active();
    lv_obj_t *obj = lv_event_get_param(e);
    if (!in || !s_screen.fn) return;
    lv_point_t p = {0};
    lv_indev_get_point(in, &p);
    const uint32_t now = lv_tick_get();
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        s_screen.verdict = YK_UI_TAP_GIVE;
        yk_tap_press(&s_screen.tap, 0, p.x, p.y, now);  // (its number: counted at the read, screen_read)
        break;
    case LV_EVENT_RELEASED: {
        const bool swiped = lv_indev_get_press_moved(in) || lv_indev_get_gesture_dir(in) != LV_DIR_NONE;
        const bool tap = yk_tap_strict(&s_screen.tap, true, true, swiped, p.x, p.y, now);
        const yk_ui_tap_t t = {.x = s_screen.tap.x0, .y = s_screen.tap.y0, .down_ms = s_screen.tap.t0,
                               .up_ms = now, .press = s_screen.presses};
        const yk_ui_tap_do_t v = s_screen.fn(tap ? YK_UI_PRESS_TAP : YK_UI_PRESS_OTHER, &t, s_screen.ctx);
        s_screen.verdict = tap ? v : YK_UI_TAP_GIVE;
        if (s_screen.verdict == YK_UI_TAP_HOLD && obj) held_start(obj, in);
        break;
    }
    case LV_EVENT_SHORT_CLICKED:
    case LV_EVENT_CLICKED:
        if (s_screen.verdict == YK_UI_TAP_GIVE) break;
        if (s_screen.verdict == YK_UI_TAP_HOLD && obj && obj == s_held.obj) {
            // yk_ui_taps_strict's verdict on this press stands (its RELEASED came before this, whichever runs first).
            s_held.kept = s_strict.on && !s_strict.tapped && !judges_itself(obj);
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) s_held.clicked = true;
            else s_held.short_clicked = true;
        }
        lv_indev_stop_processing(in);  // not to the object now: the system's, or later
        break;
    default:
        break;
    }
}

/** `slot` 0 the touch screen, 1 yk_ui_finger's finger (each new one in its place). */
static void screen_attach(lv_indev_t *indev, int slot) {
    static const lv_event_code_t codes[] = {LV_EVENT_PRESSED, LV_EVENT_RELEASED, LV_EVENT_SHORT_CLICKED, LV_EVENT_CLICKED};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) lv_indev_add_event_cb(indev, screen_event, codes[i], NULL);
    const lv_indev_read_cb_t read = lv_indev_get_read_cb(indev);
    if (!read || read == screen_read) return;
    s_reads[slot].indev = indev;
    s_reads[slot].read = read;
    s_reads[slot].down = false;
    lv_indev_set_read_cb(indev, screen_read);
}

void yk_ui_screen_taps(lv_indev_t *indev, yk_ui_screen_tap_fn fn, void *ctx) {
    s_screen.fn = fn;
    s_screen.ctx = ctx;
    if (!fn) held_clear();
    if (!indev || !fn || s_screen.attached) return;
    s_screen.attached = true;
    screen_attach(indev, 0);
    puts("YK_TAP screen on");
}

lv_obj_t *yk_ui_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h, lv_event_cb_t cb,
                       void *user_data, uint32_t color) {
    lv_obj_t *o = lv_button_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_radius(o, h / 2, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    yk_ui_tap_only(o);
    if (cb) lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, user_data);
    // Words on the accent in its own dark (0.1.19 4.7), on coral the paper's, on anything else ink.
    yk_ui_label(o, text, 0, (h - 27) / 2, w, 22, color == yk_accent() ? yk_accent_on() : color == YK_CORAL ? YK_PAPER : YK_INK);
    return o;
}

static int snapshot_cmd(int argc, char **argv) {
    (void)argc, (void)argv;
    capturing = true;
    board_lock(-1);
    lv_draw_buf_t *buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    board_unlock();
    if (!buf) {
        capturing = false;
        puts("SNAP_ERROR out of memory");
        return 1;
    }
    printf("SNAP_BEGIN %u %u %u rgb565\n", (unsigned)buf->header.w, (unsigned)buf->header.h, (unsigned)buf->header.stride);
    // Numbered lines, "SN <seq> <base64>": another task's log line can land between them, and the reader keeps only
    // lines in sequence (an unnumbered line from a log would be decoded as pixels and shift the rest).
    unsigned char out[80];
    size_t total = buf->header.stride * buf->header.h;
    for (size_t off = 0; off < total; off += 57) {
        size_t n = total - off < 57 ? total - off : 57, len;
        mbedtls_base64_encode(out, sizeof(out), &len, buf->data + off, n);
        out[len] = 0;
        printf("SN %u %s\n", (unsigned)(off / 57), (char *)out);
        if ((off / 57) % 256 == 255) vTaskDelay(1);
    }
    puts("SNAP_END");
    lv_draw_buf_destroy(buf);
    capturing = false;
    return 0;
}

void yk_ui_register_snapshot(void) {
    const esp_console_cmd_t cmd = {.command = "snap", .help = "capture the actual LVGL frame", .func = snapshot_cmd};
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

bool yk_ui_capturing(void) { return capturing; }

// ---------------------------------------------------------------- a finger for development (yk_ui_finger)

static struct {
    lv_indev_t *indev;
    int x0, y0, x1, y1, ms;
    uint32_t t0;
    bool started, at_end, released;
} s_finger;

static void finger_gone(lv_timer_t *t) {
    if (s_reads[1].indev == s_finger.indev) memset(&s_reads[1], 0, sizeof(s_reads[1]));
    if (s_held.indev == s_finger.indev) s_held.indev = NULL;  // a tap it held goes out without it (held_no_read)
    lv_indev_delete(s_finger.indev);
    memset(&s_finger, 0, sizeof(s_finger));
    lv_timer_delete(t);
}

static void finger_read(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    if (!s_finger.started) s_finger.started = true, s_finger.t0 = lv_tick_get();
    const int t = (int)lv_tick_elaps(s_finger.t0);
    const float k = t < s_finger.ms ? (float)t / s_finger.ms : 1;
    data->point.x = s_finger.x0 + (int32_t)lroundf((s_finger.x1 - s_finger.x0) * k);
    data->point.y = s_finger.y0 + (int32_t)lroundf((s_finger.y1 - s_finger.y0) * k);
    // Down along the way and once more at its end (as a finger is where it lets go); then up.
    data->state = s_finger.at_end ? LV_INDEV_STATE_RELEASED : LV_INDEV_STATE_PRESSED;
    if (k >= 1) s_finger.at_end = true;
    if (data->state == LV_INDEV_STATE_RELEASED && !s_finger.released) {
        s_finger.released = true;
        lv_timer_set_repeat_count(lv_timer_create(finger_gone, 100, NULL), 1);  // not from inside its own read
    }
}

bool yk_ui_finger(int x0, int y0, int x1, int y1, int ms) {
    if (s_finger.indev) return false;
    memset(&s_finger, 0, sizeof(s_finger));
    s_finger.x0 = x0, s_finger.y0 = y0, s_finger.x1 = x1, s_finger.y1 = y1, s_finger.ms = ms < 0 ? 0 : ms;
    s_finger.indev = lv_indev_create();
    if (!s_finger.indev) return false;
    lv_indev_set_type(s_finger.indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_finger.indev, finger_read);
    if (s_strict.on) strict_attach(s_finger.indev);  // as the touch screen's (yk_ui_taps_strict)
    if (s_screen.attached) screen_attach(s_finger.indev, 1);  // ... and yk_ui_screen_taps
    printf("YK_FINGER %d,%d -> %d,%d in %d ms\n", x0, y0, x1, y1, s_finger.ms);
    return true;
}

// ---------------------------------------------------------------- v2 helpers (P11)

lv_obj_t *yk_ui_arc_label(lv_obj_t *parent, const char *text, int size, uint32_t color, float angle_deg, int radius,
                          bool ccw) {
    const lv_font_t *font = cjk_font_get(size);
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_clickable(box, false);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_size(box, YK_CX * 2, YK_CY * 2);
    lv_obj_set_pos(box, 0, 0);
    if (!font || !text) return box;
    // The characters' widths along the circle, centred on angle_deg.
    int widths[16], n = 0, total = 0, len;
    uint32_t cps[16];
    for (const char *p = text; *p && n < 16; p += len) {
        cps[n] = yk_utf8_next(p, &len);
        widths[n] = lv_font_get_glyph_width(font, cps[n], 0);
        total += widths[n++];
    }
    const float rad_per_px = 1.0f / (float)radius, dir = ccw ? -1.0f : 1.0f;
    float at = angle_deg * 3.14159265f / 180.0f - dir * total * rad_per_px / 2;
    // ccw (the bottom half): the baseline is on the circle, the characters' tops towards the centre.
    const int r_mid = ccw ? radius + size / 2 : radius - size / 2;
    for (int i = 0; i < n; i++) {
        const float mid = at + dir * widths[i] * rad_per_px / 2;
        char ch[5] = {0};
        // the code point back to UTF-8, one label each
        const uint32_t c = cps[i];
        if (c < 0x80) ch[0] = (char)c;
        else if (c < 0x800) ch[0] = (char)(0xC0 | (c >> 6)), ch[1] = (char)(0x80 | (c & 0x3F));
        else if (c < 0x10000)
            ch[0] = (char)(0xE0 | (c >> 12)), ch[1] = (char)(0x80 | ((c >> 6) & 0x3F)), ch[2] = (char)(0x80 | (c & 0x3F));
        else
            ch[0] = (char)(0xF0 | (c >> 18)), ch[1] = (char)(0x80 | ((c >> 12) & 0x3F)),
            ch[2] = (char)(0x80 | ((c >> 6) & 0x3F)), ch[3] = (char)(0x80 | (c & 0x3F));
        lv_obj_t *l = lv_label_create(box);
        lv_label_set_text(l, ch);
        lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
        const int w = widths[i] > 0 ? widths[i] : 1, h = lv_font_get_line_height(font);
        lv_obj_set_size(l, w, h);
        const float x = YK_CX + r_mid * sinf(mid), y = YK_CY - r_mid * cosf(mid);
        lv_obj_set_pos(l, (int32_t)lroundf(x - w / 2.0f), (int32_t)lroundf(y - h / 2.0f));
        lv_obj_set_style_transform_pivot_x(l, w / 2, 0);
        lv_obj_set_style_transform_pivot_y(l, h / 2, 0);
        lv_obj_set_style_transform_rotation(l, (int32_t)lroundf((mid * 180.0f / 3.14159265f + (ccw ? 180 : 0)) * 10), 0);
        at += dir * w * rad_per_px;
    }
    return box;
}

lv_obj_t *yk_ui_turn_box(lv_obj_t *parent) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_size(box, YK_CX * 2, YK_CY * 2);
    lv_obj_set_pos(box, 0, 0);
    lv_obj_set_style_transform_pivot_x(box, YK_CX, 0);
    lv_obj_set_style_transform_pivot_y(box, YK_CY, 0);
    return box;
}

static void turn_exec(void *obj, int32_t v) { lv_obj_set_style_transform_rotation(obj, v, 0); }

void yk_ui_turn(lv_obj_t *box, float deg, int ms) {
    lv_anim_delete(box, turn_exec);
    const int32_t to = (int32_t)lroundf(deg * 10);
    if (ms <= 0) {
        turn_exec(box, to);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, box);
    lv_anim_set_values(&a, lv_obj_get_style_transform_rotation(box, 0), to);
    lv_anim_set_duration(&a, (uint32_t)ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, turn_exec);
    lv_anim_start(&a);
}

#define SHEET_H 210

lv_obj_t *yk_ui_status_sheet(lv_obj_t *parent) {
    lv_obj_t *sheet = lv_obj_create(parent);
    lv_obj_remove_style_all(sheet);
    lv_obj_set_scrollable(sheet, false);
    lv_obj_set_size(sheet, YK_CX * 2, SHEET_H);
    lv_obj_set_pos(sheet, 0, -SHEET_H);
    lv_obj_set_style_bg_color(sheet, lv_color_hex(YK_PANEL), 0);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_set_hidden(sheet, true);
    // four lines, filled by yk_ui_status_show(): clock; network; battery and volume; points
    for (int i = 0; i < 4; i++) yk_ui_label(sheet, "", 33, 52 + i * 34, 400, i ? YK_SIZE_STATUS : YK_SIZE_HEADING, i ? YK_INK2 : YK_INK);
    return sheet;
}

static void sheet_y(void *obj, int32_t v) { lv_obj_set_y(obj, v); }

static void sheet_hidden(lv_anim_t *a) {
    if (lv_obj_get_y(a->var) <= -SHEET_H) lv_obj_set_hidden(a->var, true);
}

/** The battery's line, worded from the power chip's state (docs/delivery/2026-10-31-battery-display.md 5): the app
 *  passes the number, the words come from board_battery() here, so every app says the same without changing. */
static void battery_line(char *line, size_t size, const yk_status_t *st) {
    board_battery_t b;
    const bool read = board_battery(&b) == ESP_OK;
    char pct[12];
    yk_words_num(pct, st->battery);
    if (read && b.fault == BOARD_BATTERY_FAULT_NO_RISE) {
        snprintf(line, size, "%s", yk_word(YK_STR_YAYKIT_BATTERY_FAULTY));
    } else if (read && b.low) {
        snprintf(line, size, "%s", yk_word(YK_STR_YAYKIT_BATTERY_LOW_CHARGING));
    } else if (read && b.state == BOARD_BATTERY_FULL) {
        yk_wordf(line, size, YK_STR_YAYKIT_STATUS_BATTERY_FULL, pct);
    } else if (read && b.state == BOARD_BATTERY_STOPPED) {
        yk_wordf(line, size, YK_STR_YAYKIT_STATUS_BATTERY_STOPPED, pct);
    } else {
        yk_wordf(line, size, st->charging ? YK_STR_YAYKIT_STATUS_BATTERY_CHARGING : YK_STR_YAYKIT_BATTERY_PERCENT, pct);
    }
}

void yk_ui_status_show(lv_obj_t *sheet, const yk_status_t *st, bool show) {
    if (!sheet) return;
    if (st) {
        char line[64];
        lv_obj_t *l;
        int k = 0;
        if ((l = lv_obj_get_child(sheet, k++))) lv_label_set_text(l, st->clock ? st->clock : "");
        if ((l = lv_obj_get_child(sheet, k++))) {
            static const yk_str_t wifi[] = {YK_STR_YAYKIT_STATUS_WIFI_OFF, YK_STR_YAYKIT_STATUS_CONNECTING,
                                            YK_STR_YAYKIT_STATUS_ONLINE};
            lv_label_set_text(l, st->wifi >= 0 && st->wifi <= 2 ? yk_word(wifi[st->wifi]) : "");
        }
        if ((l = lv_obj_get_child(sheet, k++))) {
            line[0] = 0;
            if (st->battery >= 0) battery_line(line, sizeof(line), st);
            if (st->volume >= 0) {
                char n[12], volume[32];
                yk_wordf(volume, sizeof(volume), YK_STR_YAYKIT_STATUS_VOLUME, yk_words_num(n, st->volume));
                snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%s", line[0] ? "  ·  " : "", volume);
            }
            lv_label_set_text(l, line);
        }
        if ((l = lv_obj_get_child(sheet, k++))) {
            line[0] = 0;
            char n[12];
            if (st->credits >= 0) yk_wordf(line, sizeof(line), YK_STR_YAYKIT_STATUS_POINTS_LEFT, yk_words_num(n, st->credits));
            lv_label_set_text(l, line);
        }
    }
    lv_anim_delete(sheet, sheet_y);
    if (show) lv_obj_set_hidden(sheet, false);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, sheet);
    lv_anim_set_values(&a, lv_obj_get_y(sheet), show ? 0 : -SHEET_H);
    lv_anim_set_duration(&a, YK_PAGE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, sheet_y);
    lv_anim_set_completed_cb(&a, sheet_hidden);
    lv_anim_start(&a);
}
