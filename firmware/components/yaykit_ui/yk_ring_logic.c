// The settings ring's and the dial's pure parts (yk_ring_logic.h).
#include "yk_ring_logic.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yk_round.h"

#define PI 3.14159265f

void yk_ring_place(int i, int n, int16_t *cx, int16_t *cy) {
    const float t = n > 0 ? (float)i * 2 * PI / (float)n : 0;
    *cx = (int16_t)lroundf(YK_CX + YK_RINGS_R * sinf(t));
    *cy = (int16_t)lroundf(YK_CY - YK_RINGS_R * cosf(t));
}

void yk_ring_layout(int n, yk_ring_layout_t *out) {
    memset(out, 0, sizeof(*out));
    out->n = n < 0 ? 0 : n > YK_RING_MAX ? YK_RING_MAX : n;
    for (int i = 0; i < out->n; i++) yk_ring_place(i, out->n, &out->cx[i], &out->cy[i]);
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float ease_out(float p) { return 1 - (1 - p) * (1 - p) * (1 - p); }
static float ease_in(float p) { return p * p; }

static yk_apps_pose_t pose_at(float x0, float y0, float x1, float y1, float k, float s0, float s1, float o0, float o1) {
    return (yk_apps_pose_t){
        .cx = (int16_t)lroundf(x0 + (x1 - x0) * k),
        .cy = (int16_t)lroundf(y0 + (y1 - y0) * k),
        .scale = (uint16_t)lroundf(256 * (s0 + (s1 - s0) * k)),
        .opa = (uint8_t)lroundf(255 * (o0 + (o1 - o0) * k)),
    };
}

bool yk_ring_frame(const yk_ring_layout_t *L, int t_ms, bool closing, yk_apps_pose_t pose[YK_RING_MAX + 1]) {
    const float t = (float)(t_ms < 0 ? 0 : t_ms);
    if (closing) {
        const float k = ease_in(clamp01(t / YK_APPS_CLOSE_MS));
        pose[0] = pose_at(YK_CX, YK_CY, YK_CX, YK_CY, k, 1.0f, 0.8f, 1, 0);
        for (int i = 0; i < L->n; i++) pose[i + 1] = pose_at(L->cx[i], L->cy[i], YK_CX, YK_CY, k, 1.0f, 0.2f, 1, 0);
        return t >= YK_APPS_CLOSE_MS;
    }
    pose[0] = pose_at(YK_CX, YK_CY, YK_CX, YK_CY, ease_out(clamp01(t / YK_APPS_CENTER_IN_MS)), 0.6f, 1.0f, 0, 1);
    const int fly = YK_APPS_OPEN_MS - (YK_RING_MAX - 1) * YK_RING_STAGGER_MS;
    int end = YK_APPS_CENTER_IN_MS;
    for (int i = 0; i < L->n; i++) {
        const int start = i * YK_RING_STAGGER_MS;
        pose[i + 1] = pose_at(YK_CX, YK_CY, L->cx[i], L->cy[i], ease_out(clamp01((t - start) / fly)), 0.2f, 1.0f, 0, 1);
        if (start + fly > end) end = start + fly;
    }
    return t >= end;
}

bool yk_ring_frame_away(const yk_ring_layout_t *L, int t_ms, int dx, int dy, yk_apps_pose_t pose[YK_RING_MAX + 1]) {
    const float t = (float)(t_ms < 0 ? 0 : t_ms), k = ease_in(clamp01(t / YK_APPS_AWAY_MS));
    const float ox = (float)(dx > 0 ? 1 : dx < 0 ? -1 : 0) * YK_APPS_AWAY_PX, oy = (float)(dy > 0 ? 1 : dy < 0 ? -1 : 0) * YK_APPS_AWAY_PX;
    pose[0] = pose_at(YK_CX, YK_CY, YK_CX + ox, YK_CY + oy, k, 1.0f, 1.0f, 1, 0);
    for (int i = 0; i < L->n; i++) pose[i + 1] = pose_at(L->cx[i], L->cy[i], L->cx[i] + ox, L->cy[i] + oy, k, 1.0f, 1.0f, 1, 0);
    return t >= YK_APPS_AWAY_MS;
}

int yk_ring_hit(const yk_ring_layout_t *L, int x, int y) {
    int best = -1, best_d = 0;
    for (int i = 0; i < L->n; i++) {
        const int dx = x - L->cx[i], dy = y - L->cy[i], d = dx * dx + dy * dy;
        if (d <= YK_RINGS_HIT_R * YK_RINGS_HIT_R && (best < 0 || d < best_d)) best = i + 1, best_d = d;
    }
    if (best > 0) return best;
    const int dx = x - YK_CX, dy = y - YK_CY;
    return dx * dx + dy * dy <= YK_RINGS_CENTER_HIT_R * YK_RINGS_CENTER_HIT_R ? 0 : -1;
}

yk_apps_go_t yk_ring_touch_click(yk_apps_touch_t *t, const yk_ring_layout_t *L, int x, int y, uint32_t now_ms) {
    return yk_apps_touch_up(t, yk_ring_hit(L, x, y), x, y, now_ms);
}

float yk_dial_angle(int stop, int stops) {
    return stops > 1 ? YK_DIAL_START + (float)YK_DIAL_SWEEP * (float)stop / (float)(stops - 1) : YK_DIAL_START;
}

void yk_dial_point(int stop, int stops, int r, int *x, int *y) {
    const float t = yk_dial_angle(stop, stops) * PI / 180;
    *x = (int)lroundf(YK_CX + (float)r * sinf(t));
    *y = (int)lroundf(YK_CY - (float)r * cosf(t));
}

void yk_dial_layout(int title_h, int value_h, int note_h, yk_dial_layout_t *out) {
    yk_dial_layout_full(1, title_h, value_h, note_h, 0, out);
}

void yk_dial_layout_full(int title_lines, int title_h, int value_h, int note_h, int button_h, yk_dial_layout_t *out) {
    memset(out, 0, sizeof(*out));
    // Two lines: YK_DIAL_TITLE_LINE_H apart, about the middle of the one line (each box as tall as the font's line, so
    // nothing of a glyph is cut).
    const int title_y = title_lines == 2 ? YK_DIAL_TITLE_Y - YK_DIAL_TITLE_LINE_H / 2 : YK_DIAL_TITLE_Y;
    out->title = (yk_box_t){(int16_t)(YK_CX - YK_DIAL_TITLE_W / 2), (int16_t)title_y, YK_DIAL_TITLE_W, (int16_t)title_h};
    if (title_lines == 2) {
        out->title2 = out->title;
        out->title2.y = (int16_t)(title_y + YK_DIAL_TITLE_LINE_H);
    }
    const int all = value_h + YK_DIAL_NOTE_GAP + note_h, top = YK_CY - all / 2;
    out->value = (yk_box_t){(int16_t)(YK_CX - YK_DIAL_NOTE_W / 2), (int16_t)top, YK_DIAL_NOTE_W, (int16_t)value_h};
    out->note = (yk_box_t){(int16_t)(YK_CX - YK_DIAL_NOTE_W / 2), (int16_t)(top + value_h + YK_DIAL_NOTE_GAP),
                           YK_DIAL_NOTE_W, (int16_t)note_h};
    if (button_h > 0) {
        out->button = (yk_box_t){YK_DIAL_BUTTON_X, YK_DIAL_BUTTON_Y, YK_DIAL_BUTTON_W, YK_DIAL_BUTTON_H};
        out->button_words = (yk_box_t){YK_DIAL_BUTTON_X + YK_DIAL_BUTTON_PAD,
                                       (int16_t)(YK_DIAL_BUTTON_Y + (YK_DIAL_BUTTON_H - button_h) / 2),
                                       YK_DIAL_BUTTON_W - 2 * YK_DIAL_BUTTON_PAD, (int16_t)button_h};
    }
}

/** Han, kana, full-width forms (as the host tests' text_width counts them a full em): a line may break beside them. */
static bool wide(uint32_t cp) { return cp >= 0x2E80; }

/** Never at the start of a line: closing punctuation, the middle dot, the long-vowel mark. */
static bool no_break_before(uint32_t cp) {
    switch (cp) {
    case 0x3001: case 0x3002: case 0x300D: case 0x300F: case 0x3011: case 0x30FB: case 0x30FC:
    case 0xFF01: case 0xFF09: case 0xFF0C: case 0xFF0E: case 0xFF1A: case 0xFF1B: case 0xFF1F: return true;
    default: return false;
    }
}

int yk_dial_title_lines(const char *title, yk_glyph_width_fn width, void *ctx, int max_w, char *line1, size_t size1,
                        char *line2, size_t size2) {
    if (size1) line1[0] = 0;
    if (size2) line2[0] = 0;
    if (!title || !title[0] || !size1) return 0;
    int total = 0, len;
    for (const char *p = title; *p; p += len) total += width(yk_utf8_next(p, &len), ctx);
    if (total <= max_w) {  // one line, as ever
        snprintf(line1, size1, "%s", title);
        return 1;
    }
    // The last break whose first line fits: the width before it only grows, so the first that would not fit ends the look.
    int cut = -1, w = 0;
    uint32_t prev = 0;
    for (const char *p = title; *p; p += len) {
        const uint32_t cp = yk_utf8_next(p, &len);
        const int at = (int)(p - title);
        if (at > 0 && w <= max_w && prev != ' ' &&
            (cp == ' ' || (!no_break_before(cp) && (wide(cp) || wide(prev)))))
            cut = at;
        w += width(cp, ctx);
        if (w > max_w) break;
        prev = cp;
    }
    if (cut > 0) {
        const char *rest = title + cut;
        while (*rest == ' ') rest++;
        int rest_w = 0;
        for (const char *p = rest; *p; p += len) rest_w += width(yk_utf8_next(p, &len), ctx);
        if (rest[0] && rest_w <= max_w && size2) {
            snprintf(line1, size1, "%.*s", cut, title);
            snprintf(line2, size2, "%s", rest);
            return 2;
        }
    }
    yk_text_fit(title, width, ctx, max_w, line1, size1);  // two lines do not hold it: cut, as before
    return 1;
}

bool yk_dial_in_band(int x, int y) {
    const int dx = x - YK_CX, dy = y - YK_CY, r = YK_CX - YK_DIAL_BAND;
    return dx * dx + dy * dy >= r * r;
}

int yk_dial_stop_at(int x, int y, int stops, int last) {
    if (stops < 2) return 0;
    if (last < 0) last = stops - 1;
    float a = atan2f((float)(x - YK_CX), (float)(YK_CY - y)) * 180 / PI;  // clockwise from 12 o'clock, -180..180
    if (a < 0) a += 360;
    // From the scale's start, clockwise; what is past its end, below it, goes to the nearer end.
    float along = a - YK_DIAL_START;
    if (along < 0) along += 360;
    if (along > YK_DIAL_SWEEP) along = along > (YK_DIAL_SWEEP + 360) / 2.0f ? 0 : YK_DIAL_SWEEP;
    const int stop = (int)lroundf(along * (float)(stops - 1) / YK_DIAL_SWEEP);
    return stop > last ? last : stop;
}

/** What a finger at (x, y) is on, for yk_tap: 0 the middle, 1 + the stop on the band, YK_DIAL_HIT_BUTTON the button (only
 *  when there is one), -1 elsewhere. */
static int dial_hit(const yk_dial_touch_t *t, int x, int y, int stops, int last) {
    const int dx = x - YK_CX, dy = y - YK_CY;
    if (dx * dx + dy * dy <= YK_DIAL_MIDDLE_R * YK_DIAL_MIDDLE_R) return 0;
    if (yk_dial_in_band(x, y)) return 1 + yk_dial_stop_at(x, y, stops, last);
    if (t->button && x >= YK_DIAL_BUTTON_X && x < YK_DIAL_BUTTON_X + YK_DIAL_BUTTON_W && y >= YK_DIAL_BUTTON_Y &&
        y < YK_DIAL_BUTTON_Y + YK_DIAL_BUTTON_H)
        return YK_DIAL_HIT_BUTTON;
    return -1;
}

yk_dial_go_t yk_dial_touch_press(yk_dial_touch_t *t, int x, int y, int stops, int last, uint32_t now_ms, int *stop) {
    (void)stop;
    t->turning = yk_dial_in_band(x, y);
    t->following = false;
    t->down_stop = yk_dial_stop_at(x, y, stops, last);
    yk_tap_press(&t->tap, dial_hit(t, x, y, stops, last), x, y, now_ms);
    return YK_DIAL_GO_NONE;
}

yk_dial_go_t yk_dial_touch_move(yk_dial_touch_t *t, int x, int y, int stops, int last, int *stop) {
    const bool still = yk_tap_move(&t->tap, x, y);
    if (t->over || !t->turning) return YK_DIAL_GO_NONE;
    const int at = yk_dial_stop_at(x, y, stops, last);
    if (!t->following && still && at == t->down_stop) return YK_DIAL_GO_NONE;  // a tap so far
    t->following = true;
    *stop = at;
    return YK_DIAL_GO_TURN;
}

yk_dial_go_t yk_dial_touch_swipe(yk_dial_touch_t *t) {
    if (t->over || t->turning) return YK_DIAL_GO_NONE;
    t->over = true;
    return YK_DIAL_GO_DONE;
}

yk_dial_go_t yk_dial_touch_click(yk_dial_touch_t *t, int x, int y, int stops, int last, uint32_t now_ms, int *stop) {
    const int hit = dial_hit(t, x, y, stops, last);
    const bool tap = yk_tap_release(&t->tap, hit, x, y, now_ms);
    if (t->over || !tap) return YK_DIAL_GO_NONE;
    if (t->turning) {  // a tap on a stop: there, as it lets go (a turn that followed the finger is no tap)
        if (t->following) return YK_DIAL_GO_NONE;
        *stop = t->down_stop;
        return YK_DIAL_GO_TURN;
    }
    if (hit == YK_DIAL_HIT_BUTTON) return YK_DIAL_GO_BUTTON;  // the dial stays: what it leads to is the caller's
    if (hit != 0) return YK_DIAL_GO_NONE;
    t->over = true;
    return YK_DIAL_GO_DONE;
}

bool yk_dial_button_pressed(const yk_dial_touch_t *t) {
    return t->button && !t->over && !t->turning && t->tap.down && !t->tap.off && t->tap.hit == YK_DIAL_HIT_BUTTON;
}
