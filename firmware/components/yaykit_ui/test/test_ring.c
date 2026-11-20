// Host tests for the settings ring and its dial (yk_ring_logic.c; round-screen-gestures.md 13): the layout for every number of
// places, what a touch hits, a swipe that is not a tap, a turn apart from a swipe.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yk_ring_logic.h"
#include "yk_round.h"
#include "yk_accent_table.h"
#include "yk_round.h"

static int failures, checks;
#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

/** The spec on the ring's places (yk_rings.h, round-screen-design.md 2): as test_apps.c's ring_spec, the update dot
 *  the only mark a settings place has. */
static bool ring_spec(const yk_ring_layout_t *L) {
    bool ok = 2 * YK_RINGS_HIT_R >= YK_TOUCH_MIN_W && 2 * YK_RINGS_HIT_R >= YK_TOUCH_MIN_H;
    const int in_use = YK_RINGS_SAT_R + YK_RINGS_IN_USE_GAP;
    for (int i = 0; i < L->n; i++) {
        const double r = hypot(L->cx[i] - YK_CX, L->cy[i] - YK_CY);
        ok = ok && fabs(r - YK_RINGS_R) < 1.0 && r - YK_RINGS_HIT_R - YK_RINGS_CENTER_HIT_R >= YK_TOUCH_GAP;
        ok = ok && r + YK_RINGS_PRESS_R < YK_R_BAND && r + in_use < YK_R_BAND;
        for (int j = i + 1; j < L->n && L->n <= YK_RINGS_SPACED; j++) {
            const double d = hypot(L->cx[i] - L->cx[j], L->cy[i] - L->cy[j]);
            ok = ok && d - 2 * YK_RINGS_HIT_R >= YK_TOUCH_GAP && d > YK_RINGS_PRESS_R + in_use + YK_RINGS_IN_USE_W;
        }
        int x, y;
        float outer;
        yk_rings_mark(YK_MARK_UPDATE, L->cx[i], L->cy[i], 1.0f, &x, &y, &outer);
        ok = ok && hypot(x - YK_CX, y - YK_CY) + outer <= YK_RIM_R - YK_RIM_W;
        for (int j = 0; j < L->n; j++)
            if (j != i) ok = ok && hypot(x - L->cx[j], y - L->cy[j]) > outer + in_use;
    }
    return ok;
}

static void test_layout(void) {
    for (int n = 1; n <= YK_RING_MAX; n++) {
        yk_ring_layout_t L;
        yk_ring_layout(n, &L);
        CHECK(L.n == n);
        CHECK(L.cx[0] == YK_CX && L.cy[0] == YK_CY - YK_RINGS_R);  // the first at 12 o'clock
        if (n >= 3) CHECK(L.cx[1] > YK_CX);  // clockwise: the second to the right of the first
        CHECK(ring_spec(&L));
    }
    // The app ring's size (dev doc 4.7, the user's 「两个环大小、半径一致」): five places where its five are, six where
    // its six are (the ring with 「设置」).
    yk_ring_layout_t L;
    yk_apps_layout_t A;
    yk_ring_layout(5, &L);
    yk_apps_layout(5, &A);
    for (int i = 0; i < 5; i++) CHECK(L.cx[i] == A.sat[i].cx && L.cy[i] == A.sat[i].cy);
    const yk_apps_item_t six[6] = {{.slug = "a"}, {.slug = "b"}, {.slug = "c"}, {.slug = "d"},
                                   {.slug = YK_APPS_MORE}, {.slug = YK_APPS_SETTINGS}};
    int slots[YK_APPS_MAX];
    yk_apps_item_slots(six, 6, slots);
    yk_apps_layout_at(6, slots, &A);
    yk_ring_layout(6, &L);
    for (int i = 0; i < 6; i++) {
        bool found = false;  // 「更多」 and 「设置」 stand at the 6-place ring's own places: the same set
        for (int j = 0; j < 6; j++) found |= L.cx[i] == A.sat[j].cx && L.cy[i] == A.sat[j].cy;
        CHECK(found);
    }
    // Yay's settings: 6 places (Wi-Fi in 「更多」), 7 the most that keep 12 apart.
    CHECK(YK_RINGS_SPACED >= 6 && YK_RINGS_SPACED < YK_RING_MAX);
    // None, and more than it has.
    yk_ring_layout(0, &L);
    CHECK(L.n == 0);
    yk_ring_layout(YK_RING_MAX + 3, &L);
    CHECK(L.n == YK_RING_MAX);
    // What a disc holds stays inside it: the line icon (its square in the disc's box, what it draws, 11 of its 24 units
    // from the middle, in the disc), 「更多」's dots.
    CHECK(YK_RING_ICON / 2 < YK_RINGS_SAT_R && YK_RING_ICON * 11 / 24 < YK_RINGS_SAT_R);
    CHECK(YK_APPS_DOT_GAP + YK_APPS_DOT_R < YK_RINGS_SAT_R);
}

static void test_hit(void) {
    for (int n = 1; n <= YK_RING_MAX; n++) {
        yk_ring_layout_t L;
        yk_ring_layout(n, &L);
        CHECK(yk_ring_hit(&L, YK_CX, YK_CY) == 0);
        CHECK(yk_ring_hit(&L, YK_CX + YK_RINGS_CENTER_HIT_R - 1, YK_CY) == 0);
        for (int i = 0; i < n; i++) {
            CHECK(yk_ring_hit(&L, L.cx[i], L.cy[i]) == i + 1);
            // past the disc, towards the outside: still its own
            const double ux = (L.cx[i] - YK_CX) / (double)YK_RINGS_R, uy = (L.cy[i] - YK_CY) / (double)YK_RINGS_R;
            CHECK(yk_ring_hit(&L, L.cx[i] + (int)lround(56 * ux), L.cy[i] + (int)lround(56 * uy)) == i + 1);
        }
        CHECK(yk_ring_hit(&L, 4, 4) == -1);  // the paper
        // Every pixel: one thing at most, and on 8 places (targets overlapping) the nearer place.
        for (int y = 0; y < 466; y += 3)
            for (int x = 0; x < 466; x += 3) {
                const int h = yk_ring_hit(&L, x, y);
                if (h <= 0) continue;
                const double d = hypot(x - L.cx[h - 1], y - L.cy[h - 1]);
                for (int j = 0; j < n; j++)
                    if (hypot(x - L.cx[j], y - L.cy[j]) < d - 0.5) {
                        CHECK(!"a nearer place");
                        y = 466;
                        break;
                    }
            }
    }
    // Two places: the sides are paper.
    yk_ring_layout_t L;
    yk_ring_layout(2, &L);
    CHECK(yk_ring_hit(&L, YK_CX + 150, YK_CY) == -1);
}

static void test_motion(void) {
    yk_ring_layout_t L;
    yk_ring_layout(8, &L);
    yk_apps_pose_t p[YK_RING_MAX + 1];
    CHECK(!yk_ring_frame(&L, 0, false, p));
    CHECK(p[0].cx == YK_CX && p[0].scale == 154 && p[0].opa == 0);  // the middle from 0.6, clear
    for (int i = 1; i <= 8; i++) CHECK(p[i].cx == YK_CX && p[i].cy == YK_CY);  // the places from the centre
    CHECK(yk_ring_frame(&L, YK_APPS_OPEN_MS, false, p));  // all of them out by then
    CHECK(p[0].scale == 256 && p[0].opa == 255);
    for (int i = 0; i < 8; i++) CHECK(p[i + 1].cx == L.cx[i] && p[i + 1].cy == L.cy[i] && p[i + 1].opa == 255);
    CHECK(!yk_ring_frame(&L, YK_APPS_CLOSE_MS - 1, true, p));
    CHECK(yk_ring_frame(&L, YK_APPS_CLOSE_MS, true, p));
    for (int i = 0; i < 8; i++) CHECK(p[i + 1].cx == YK_CX && p[i + 1].opa == 0);
    // After a swipe: every element towards the finger, as big as it was.
    CHECK(yk_ring_frame_away(&L, YK_APPS_AWAY_MS, 1, 0, p));
    CHECK(p[0].cx == YK_CX + YK_APPS_AWAY_PX && p[0].opa == 0);
    for (int i = 0; i < 8; i++) CHECK(p[i + 1].cx == L.cx[i] + YK_APPS_AWAY_PX && p[i + 1].cy == L.cy[i] && p[i + 1].scale == 256);
    CHECK(yk_ring_frame_away(&L, YK_APPS_AWAY_MS, 0, -1, p) && p[1].cy == L.cy[0] - YK_APPS_AWAY_PX);
}

#define T0 1000u  // a press's time (ms)

/** A finger down at (x, y) on the ring at T0. */
static void press(yk_apps_touch_t *t, const yk_ring_layout_t *L, int x, int y) {
    yk_apps_touch_press(t, yk_ring_hit(L, x, y), x, y, T0);
}

static void test_touch(void) {
    yk_ring_layout_t L;
    yk_ring_layout(7, &L);
    // A tap: what it hits, once.
    yk_apps_touch_t t = {0};
    press(&t, &L, L.cx[3], L.cy[3]);
    yk_apps_go_t go = yk_ring_touch_click(&t, &L, L.cx[3], L.cy[3], T0 + 80);
    CHECK(go.kind == YK_APPS_GO_TAP && go.hit == 4);
    press(&t, &L, L.cx[3], L.cy[3]);
    CHECK(yk_ring_touch_click(&t, &L, L.cx[3], L.cy[3], T0 + 80).kind == YK_APPS_GO_NONE);
    // A swipe that lets go on a place taps nothing, wherever it lands.
    for (int i = 0; i < 7; i++) {
        memset(&t, 0, sizeof(t));
        press(&t, &L, L.cx[i], L.cy[i]);
        CHECK(yk_apps_touch_swipe(&t, 0, -1).kind == YK_APPS_GO_AWAY);
        CHECK(yk_ring_touch_click(&t, &L, L.cx[i], L.cy[i], T0 + 80).kind == YK_APPS_GO_NONE);
    }
    // Held in place (something under way): the swipe went nowhere, and its release is still no tap.
    memset(&t, 0, sizeof(t));
    press(&t, &L, L.cx[1], L.cy[1]);
    CHECK(yk_apps_touch_swipe(&t, 1, 0).kind == YK_APPS_GO_AWAY);
    yk_apps_touch_stays(&t);
    CHECK(yk_ring_touch_click(&t, &L, L.cx[1], L.cy[1], T0 + 80).kind == YK_APPS_GO_NONE);
    // A place that stays: the next finger counts.
    memset(&t, 0, sizeof(t));
    press(&t, &L, L.cx[0], L.cy[0]);
    CHECK(yk_ring_touch_click(&t, &L, L.cx[0], L.cy[0], T0 + 80).kind == YK_APPS_GO_TAP);
    yk_apps_touch_stays(&t);
    press(&t, &L, YK_CX, YK_CY);
    CHECK(yk_ring_touch_click(&t, &L, YK_CX, YK_CY, T0 + 80).hit == 0);
    // 0.1.18 (dev doc 4.8): down on one place and up on the next, either way, or from the paper or the middle onto a
    // place: nothing, and the ring stays for the next finger.
    const int from[][2] = {{L.cx[0], L.cy[0]}, {L.cx[1], L.cy[1]}, {20, 20}, {YK_CX, YK_CY}};
    const int to[][2] = {{L.cx[1], L.cy[1]}, {L.cx[0], L.cy[0]}, {L.cx[0], L.cy[0]}, {L.cx[0], L.cy[0]}};
    for (int k = 0; k < 4; k++) {
        memset(&t, 0, sizeof(t));
        press(&t, &L, from[k][0], from[k][1]);
        yk_apps_touch_move(&t, to[k][0], to[k][1]);
        CHECK(yk_ring_touch_click(&t, &L, to[k][0], to[k][1], T0 + 300).kind == YK_APPS_GO_NONE);
        press(&t, &L, L.cx[2], L.cy[2]);
        go = yk_ring_touch_click(&t, &L, L.cx[2], L.cy[2], T0 + 80);
        CHECK(go.kind == YK_APPS_GO_TAP && go.hit == 3);
    }
    // Down on a place and slid off onto the paper; a slow drag inside one place; held too long: nothing.
    memset(&t, 0, sizeof(t));
    press(&t, &L, L.cx[2], L.cy[2]);
    CHECK(yk_ring_touch_click(&t, &L, YK_CX + 90, YK_CY + 90, T0 + 200).kind == YK_APPS_GO_NONE);
    press(&t, &L, L.cx[2], L.cy[2]);
    yk_apps_touch_move(&t, L.cx[2] + 25, L.cy[2]);
    CHECK(yk_ring_touch_click(&t, &L, L.cx[2] + 3, L.cy[2], T0 + 400).kind == YK_APPS_GO_NONE);
    press(&t, &L, L.cx[2], L.cy[2]);
    CHECK(yk_ring_touch_click(&t, &L, L.cx[2], L.cy[2], T0 + YK_TAP_HOLD_MS).kind == YK_APPS_GO_NONE);
}

static void test_dial_scale(void) {
    // 7:30 clockwise to 4:30, the stops evenly along it.
    CHECK(yk_dial_angle(0, 11) == 225);
    CHECK(yk_dial_angle(10, 11) == 495);
    CHECK(fabsf(yk_dial_angle(5, 11) - 360) < 0.01f);
    int x, y;
    yk_dial_point(5, 11, 100, &x, &y);
    CHECK(x == YK_CX && y == YK_CY - 100);  // the middle stop at 12 o'clock
    yk_dial_point(0, 11, 100, &x, &y);
    CHECK(x < YK_CX && y > YK_CY);
    yk_dial_point(10, 11, 100, &x, &y);
    CHECK(x > YK_CX && y > YK_CY);
    // Each stop's own place gives it back, for every scale in use.
    static const int scales[] = {2, 4, 5, 10, 11};
    for (size_t s = 0; s < sizeof(scales) / sizeof(scales[0]); s++)
        for (int i = 0; i < scales[s]; i++) {
            yk_dial_point(i, scales[s], 210, &x, &y);
            CHECK(yk_dial_stop_at(x, y, scales[s], -1) == i);
            CHECK(yk_dial_in_band(x, y));
        }
    // Under the scale, the end nearer the finger.
    CHECK(yk_dial_stop_at(YK_CX - 20, YK_CY + 200, 11, -1) == 0);
    CHECK(yk_dial_stop_at(YK_CX + 20, YK_CY + 200, 11, -1) == 10);
    // The cap: it ends there.
    yk_dial_point(10, 11, 210, &x, &y);
    CHECK(yk_dial_stop_at(x, y, 11, 6) == 6);
    CHECK(yk_dial_stop_at(x, y, 11, 99) == 10);
    // The band: the outer 65 px.
    CHECK(yk_dial_in_band(YK_CX + 233 - 65, YK_CY) && !yk_dial_in_band(YK_CX + 233 - 66, YK_CY));
    CHECK(!yk_dial_in_band(YK_CX, YK_CY));
}

/** The dial's words (0.1.18, 方案一): the setting's name at the top, the value and the note as one block about the centre,
 *  all in the content circle, clear of the scale and of each other. */
static void test_dial_layout(void) {
    static const int value_hs[] = {72, 86, 104};  // the 72 px font's line, as tall as it may come
    for (size_t v = 0; v < sizeof(value_hs) / sizeof(value_hs[0]); v++) {
        yk_dial_layout_t D;
        yk_dial_layout(35, value_hs[v], 23, &D);
        const yk_box_t *b[3] = {&D.title, &D.value, &D.note};
        for (int i = 0; i < 3; i++) {
            const int xs[2] = {b[i]->x - YK_CX, b[i]->x + b[i]->w - YK_CX}, ys[2] = {b[i]->y - YK_CY, b[i]->y + b[i]->h - YK_CY};
            for (int p = 0; p < 4; p++) CHECK(hypot(xs[p & 1], ys[p >> 1]) <= YK_R_CONTENT);
            CHECK(b[i]->x + b[i]->w / 2 == YK_CX);
        }
        CHECK(D.title.y == YK_DIAL_TITLE_Y && D.title.y + D.title.h <= D.value.y);
        CHECK(D.value.y + D.value.h + YK_DIAL_NOTE_GAP == D.note.y);
        CHECK(abs((D.value.y + D.note.y + D.note.h) / 2 - YK_CY) <= 1);
        CHECK(D.note.y + D.note.h <= YK_CY + YK_DIAL_MIDDLE_R + 20);
    }
    // The scale in the rim band, 22 wide; its stops' marks on it.
    CHECK(YK_DIAL_TICK_IN == 206 && YK_DIAL_TICK_OUT == 228 && YK_DIAL_TICK_OUT <= YK_R_EDGE);
    CHECK(YK_DIAL_STOP_R * 2 == 6 && YK_DIAL_TICK_IN >= YK_R_CONTENT);
}

/** A box's four corners inside radius r of the centre. */
static bool box_inside(const yk_box_t *b, double r) {
    const int xs[2] = {b->x - YK_CX, b->x + b->w - YK_CX}, ys[2] = {b->y - YK_CY, b->y + b->h - YK_CY};
    for (int p = 0; p < 4; p++)
        if (hypot(xs[p & 1], ys[p >> 1]) > r) return false;
    return true;
}

/** 声音和振动 (交互稿第 4 版): a title on two lines, and the button under the note. What a dial without them had stays as
 *  it was, to the pixel. */
static void test_dial_layout_full(void) {
    static const int value_hs[] = {72, 86, 104};
    for (size_t v = 0; v < sizeof(value_hs) / sizeof(value_hs[0]); v++) {
        yk_dial_layout_t one, D;
        yk_dial_layout(35, value_hs[v], 23, &one);
        // One line, no button: as yk_dial_layout() was, nothing else in it.
        CHECK(one.title.x == YK_CX - YK_DIAL_TITLE_W / 2 && one.title.y == YK_DIAL_TITLE_Y && one.title.w == YK_DIAL_TITLE_W &&
              one.title.h == 35);
        CHECK(!one.title2.h && !one.title2.w && !one.button.w && !one.button.h && !one.button_words.w);
        yk_dial_layout_full(1, 35, value_hs[v], 23, 0, &D);
        CHECK(!memcmp(&D, &one, sizeof(D)));
        // Two lines, 30 apart, about the one line's middle: y 81 and 111 (81-141 for a 30 px line), the value and the note
        // where they were, under the second line, all in the content circle.
        yk_dial_layout_full(2, 35, value_hs[v], 23, 23, &D);
        CHECK(D.title.y == YK_DIAL_TITLE_Y - 15 && D.title2.y == YK_DIAL_TITLE_Y + 15 && D.title2.y - D.title.y == YK_DIAL_TITLE_LINE_H);
        CHECK(D.title.h == 35 && D.title2.h == 35 && D.title2.x == D.title.x && D.title2.w == YK_DIAL_TITLE_W);
        CHECK((D.title.y + D.title2.y + D.title2.h) / 2 == one.title.y + one.title.h / 2);  // the same middle
        CHECK(!memcmp(&D.value, &one.value, sizeof(D.value)) && !memcmp(&D.note, &one.note, sizeof(D.note)));
        CHECK(D.title2.y + D.title2.h <= D.value.y);
        CHECK(box_inside(&D.title, YK_R_CONTENT) && box_inside(&D.title2, YK_R_CONTENT));
        // The button: 143, 326, 180 x 46; under the note, clear of the middle's tap (90) and of the edge band (168 from the
        // centre), in the content circle; its words 156 wide in its middle.
        CHECK(D.button.x == 143 && D.button.y == 326 && D.button.w == 180 && D.button.h == 46);
        CHECK(D.button.x + D.button.w / 2 == YK_CX);
        CHECK(D.button.y >= D.note.y + D.note.h && D.button.y - YK_CY > YK_DIAL_MIDDLE_R);
        CHECK(box_inside(&D.button, YK_CX - YK_DIAL_BAND) && box_inside(&D.button, YK_R_CONTENT));
        CHECK(D.button_words.w == 156 && D.button_words.x == 143 + 12 && D.button_words.h == 23 &&
              D.button_words.y == 326 + (46 - 23) / 2);
    }
    yk_dial_layout_t D;
    yk_dial_layout_full(2, 30, 86, 23, 0, &D);
    CHECK(D.title.y == 81 && D.title2.y + D.title2.h == 141 && !D.button.w);
}

/** A glyph's width here: half an em for ASCII and the rest, a full em for Han and kana (the real font's are checked
 *  against Yay's words in yay/test/test_words_fit.c, with the same breaking). */
static int host_glyph(uint32_t cp, void *ctx) {
    const int px = *(const int *)ctx;
    return cp >= 0x2E80 ? px : px / 2;
}

/** A line's width as host_glyph has it. */
static int line_w(const char *s, int px) {
    int w = 0, len;
    for (; *s; s += len) w += host_glyph(yk_utf8_next(s, &len), &px);
    return w;
}

/** The title's lines: one as ever; two at the same size where one does not hold it; cut only when two do not either. */
static void test_dial_title(void) {
    int px = 24;
    char a[48], b[48];
    // One line: as before, whatever the language.
    CHECK(yk_dial_title_lines("声音", host_glyph, &px, 200, a, sizeof(a), b, sizeof(b)) == 1 && !strcmp(a, "声音") && !b[0]);
    CHECK(yk_dial_title_lines("声音和振动", host_glyph, &px, 200, a, sizeof(a), b, sizeof(b)) == 1 && !strcmp(a, "声音和振动"));
    CHECK(yk_dial_title_lines("サウンドと振動", host_glyph, &px, 200, a, sizeof(a), b, sizeof(b)) == 1);
    CHECK(yk_dial_title_lines("Screen off", host_glyph, &px, 200, a, sizeof(a), b, sizeof(b)) == 1 && !strcmp(a, "Screen off"));
    // Two: at the last space that lets the first fit (交互稿: 「Sound &／vibration」, 「Sonido y／vibración」).
    CHECK(yk_dial_title_lines("Sound & vibration", host_glyph, &px, 200, a, sizeof(a), b, sizeof(b)) == 2);
    CHECK(!strcmp(a, "Sound &") && !strcmp(b, "vibration"));
    CHECK(yk_dial_title_lines("Sonido y vibración", host_glyph, &px, 200, a, sizeof(a), b, sizeof(b)) == 2);
    CHECK(!strcmp(a, "Sonido y") && !strcmp(b, "vibración"));
    // ... the last such space, not the first.
    CHECK(yk_dial_title_lines("Sound and the vibration", host_glyph, &px, 200, a, sizeof(a), b, sizeof(b)) == 2);
    CHECK(!strcmp(a, "Sound and the") && !strcmp(b, "vibration"));
    // Han and kana break anywhere between them: as many as fit (8 of 24 px in 200), then the rest.
    CHECK(yk_dial_title_lines("一二三四五六七八九十", host_glyph, &px, 200, a, sizeof(a), b, sizeof(b)) == 2);
    CHECK(!strcmp(a, "一二三四五六七八") && !strcmp(b, "九十"));
    // ... never before ー or closing punctuation: one character back.
    CHECK(yk_dial_title_lines("あいうえおかきくーけこ", host_glyph, &px, 200, a, sizeof(a), b, sizeof(b)) == 2);
    CHECK(!strcmp(a, "あいうえおかき") && !strcmp(b, "くーけこ"));
    CHECK(yk_dial_title_lines("一二三四五六七八、九十", host_glyph, &px, 200, a, sizeof(a), b, sizeof(b)) == 2);
    CHECK(!strcmp(a, "一二三四五六七") && !strcmp(b, "八、九十"));
    // A word beside Han breaks at the Han too; spaces between words go.
    CHECK(yk_dial_title_lines("Tikpal 的声音和振动设置", host_glyph, &px, 200, a, sizeof(a), b, sizeof(b)) == 2);
    CHECK(line_w(a, 24) <= 200 && line_w(b, 24) <= 200 && a[strlen(a) - 1] != ' ' && b[0] != ' ');
    // Two lines do not hold it: one, cut with …, as before.
    CHECK(yk_dial_title_lines("Sound and vibration and all the rest", host_glyph, &px, 200, a, sizeof(a), b, sizeof(b)) == 1);
    CHECK(!b[0] && strstr(a, "\xE2\x80\xA6") && line_w(a, 24) <= 200);
    CHECK(yk_dial_title_lines("Vibrationvibrationvibration", host_glyph, &px, 200, a, sizeof(a), b, sizeof(b)) == 1);
    CHECK(!b[0] && strstr(a, "\xE2\x80\xA6"));
    CHECK(yk_dial_title_lines("一二三四五六七八九十一二三四五六七八", host_glyph, &px, 200, a, sizeof(a), b, sizeof(b)) == 1);
    CHECK(!b[0] && strstr(a, "\xE2\x80\xA6"));
    // Nothing: none.
    CHECK(yk_dial_title_lines("", host_glyph, &px, 200, a, sizeof(a), b, sizeof(b)) == 0 && !a[0] && !b[0]);
    CHECK(yk_dial_title_lines(NULL, host_glyph, &px, 200, a, sizeof(a), b, sizeof(b)) == 0 && !a[0]);
}

/** The button under the note: a tap on it is GO_BUTTON and the dial stays; a dial without one is as it was there. */
static void test_dial_button(void) {
    int stop = -1;
    const int bx = YK_DIAL_BUTTON_X + YK_DIAL_BUTTON_W / 2, by = YK_DIAL_BUTTON_Y + YK_DIAL_BUTTON_H / 2;
    yk_dial_touch_t t = {.button = true};
    CHECK(yk_dial_touch_press(&t, bx, by, 11, -1, T0, &stop) == YK_DIAL_GO_NONE && stop == -1);
    CHECK(yk_dial_button_pressed(&t));  // CARD while the finger is on it
    CHECK(yk_dial_touch_move(&t, bx + 3, by - 2, 11, -1, &stop) == YK_DIAL_GO_NONE && yk_dial_button_pressed(&t));
    CHECK(yk_dial_touch_click(&t, bx + 3, by - 2, 11, -1, T0 + 120, &stop) == YK_DIAL_GO_BUTTON && stop == -1);
    CHECK(!t.over && !yk_dial_button_pressed(&t));  // the dial stays, the button let go
    // Its corners count, and the next press as ever (the button stays the dial's).
    CHECK(t.button && yk_dial_touch_press(&t, YK_DIAL_BUTTON_X, YK_DIAL_BUTTON_Y, 11, -1, T0, &stop) == YK_DIAL_GO_NONE);
    CHECK(yk_dial_touch_click(&t, YK_DIAL_BUTTON_X, YK_DIAL_BUTTON_Y, 11, -1, T0 + 80, &stop) == YK_DIAL_GO_BUTTON);
    yk_dial_touch_press(&t, YK_DIAL_BUTTON_X + YK_DIAL_BUTTON_W - 1, YK_DIAL_BUTTON_Y + YK_DIAL_BUTTON_H - 1, 11, -1, T0, &stop);
    CHECK(yk_dial_touch_click(&t, YK_DIAL_BUTTON_X + YK_DIAL_BUTTON_W - 1, YK_DIAL_BUTTON_Y + YK_DIAL_BUTTON_H - 1, 11, -1,
                              T0 + 80, &stop) == YK_DIAL_GO_BUTTON);
    // Just outside it: nothing (not the middle, not the band).
    yk_dial_touch_press(&t, YK_DIAL_BUTTON_X - 1, by, 11, -1, T0, &stop);
    CHECK(!yk_dial_button_pressed(&t) && yk_dial_touch_click(&t, YK_DIAL_BUTTON_X - 1, by, 11, -1, T0 + 80, &stop) == YK_DIAL_GO_NONE);
    // Down on it and up off it, moved further than a tap may, held: no tap, and it no longer looks pressed once moved.
    yk_dial_touch_press(&t, bx, by, 11, -1, T0, &stop);
    CHECK(yk_dial_touch_click(&t, YK_CX, YK_CY, 11, -1, T0 + 80, &stop) == YK_DIAL_GO_NONE && !t.over);
    yk_dial_touch_press(&t, bx, by, 11, -1, T0, &stop);
    yk_dial_touch_move(&t, bx + YK_TAP_SLOP + 1, by, 11, -1, &stop);
    CHECK(!yk_dial_button_pressed(&t));
    CHECK(yk_dial_touch_click(&t, bx, by, 11, -1, T0 + 80, &stop) == YK_DIAL_GO_NONE);
    yk_dial_touch_press(&t, bx, by, 11, -1, T0, &stop);
    CHECK(yk_dial_touch_click(&t, bx, by, 11, -1, T0 + YK_TAP_HOLD_MS, &stop) == YK_DIAL_GO_NONE);
    // Down on the middle, up on the button; down on the button, up on the middle: nothing.
    yk_dial_touch_press(&t, YK_CX, YK_CY, 11, -1, T0, &stop);
    CHECK(yk_dial_touch_click(&t, bx, by, 11, -1, T0 + 80, &stop) == YK_DIAL_GO_NONE && !t.over);
    // A swipe from it goes back, as from anywhere inside (the page's rule).
    yk_dial_touch_press(&t, bx, by, 11, -1, T0, &stop);
    CHECK(yk_dial_touch_swipe(&t) == YK_DIAL_GO_DONE && t.over && !yk_dial_button_pressed(&t));
    // The middle is still 「好了」 and the scale still turns with a button there.
    memset(&t, 0, sizeof(t));
    t.button = true;
    yk_dial_touch_press(&t, YK_CX, YK_CY, 11, -1, T0, &stop);
    CHECK(yk_dial_touch_click(&t, YK_CX, YK_CY, 11, -1, T0 + 80, &stop) == YK_DIAL_GO_DONE);
    int x, y;
    yk_dial_point(4, 11, 210, &x, &y);
    memset(&t, 0, sizeof(t));
    t.button = true;
    yk_dial_touch_press(&t, x, y, 11, -1, T0, &stop);
    CHECK(!yk_dial_button_pressed(&t) && yk_dial_touch_click(&t, x, y, 11, -1, T0 + 80, &stop) == YK_DIAL_GO_TURN && stop == 4);
    // No button: the same place is paper, as it was.
    memset(&t, 0, sizeof(t));
    yk_dial_touch_press(&t, bx, by, 11, -1, T0, &stop);
    CHECK(t.tap.hit == -1 && !yk_dial_button_pressed(&t));
    CHECK(yk_dial_touch_click(&t, bx, by, 11, -1, T0 + 80, &stop) == YK_DIAL_GO_NONE && !t.over);
}

static void test_dial_touch(void) {
    int stop = -1, x, y, x2, y2, x3, y3;
    yk_dial_point(2, 11, 200, &x, &y);
    yk_dial_point(7, 11, 200, &x2, &y2);
    yk_dial_point(3, 11, 200, &x3, &y3);
    // From the edge: a turn that follows the finger, and LVGL's swipe on a fast one does not end it. Going down turns
    // nothing yet (0.1.18): the dial moves once the finger does.
    yk_dial_touch_t t = {0};
    CHECK(yk_dial_touch_press(&t, x, y, 11, -1, T0, &stop) == YK_DIAL_GO_NONE && stop == -1);
    CHECK(yk_dial_touch_move(&t, x2, y2, 11, -1, &stop) == YK_DIAL_GO_TURN && stop == 7);
    CHECK(yk_dial_touch_swipe(&t) == YK_DIAL_GO_NONE);
    CHECK(yk_dial_touch_move(&t, x, y, 11, -1, &stop) == YK_DIAL_GO_TURN && stop == 2);
    // Dragged off the band on the way, it still turns; let go on the middle, it is not 「好了」, nor a tap on a stop.
    CHECK(yk_dial_touch_move(&t, YK_CX + 10, YK_CY - 50, 11, -1, &stop) == YK_DIAL_GO_TURN);
    stop = -1;
    CHECK(yk_dial_touch_click(&t, YK_CX, YK_CY, 11, -1, T0 + 300, &stop) == YK_DIAL_GO_NONE && stop == -1);
    // A drag that went back to the stop it went down on still follows, and lets go there with nothing more.
    memset(&t, 0, sizeof(t));
    CHECK(yk_dial_touch_press(&t, x, y, 11, -1, T0, &stop) == YK_DIAL_GO_NONE);
    CHECK(yk_dial_touch_move(&t, x3, y3, 11, -1, &stop) == YK_DIAL_GO_TURN && stop == 3);
    CHECK(yk_dial_touch_move(&t, x, y, 11, -1, &stop) == YK_DIAL_GO_TURN && stop == 2);
    stop = -1;
    CHECK(yk_dial_touch_click(&t, x, y, 11, -1, T0 + 300, &stop) == YK_DIAL_GO_NONE && stop == -1);
    // Moved further than a tap may without leaving the stop: it follows from then on (a drag, not a tap).
    memset(&t, 0, sizeof(t));
    CHECK(yk_dial_touch_press(&t, x, y, 11, -1, T0, &stop) == YK_DIAL_GO_NONE);
    CHECK(yk_dial_touch_move(&t, x + 3, y + 2, 11, -1, &stop) == YK_DIAL_GO_NONE);  // a wobble: still a tap
    int xs, ys;
    yk_dial_point(2, 11, 222, &xs, &ys);  // the same stop, further out
    CHECK(abs(xs - x) > YK_TAP_SLOP || abs(ys - y) > YK_TAP_SLOP);
    CHECK(yk_dial_touch_move(&t, xs, ys, 11, -1, &stop) == YK_DIAL_GO_TURN && stop == 2);
    // A tap on a stop (0.1.18): nothing as it goes down, that stop as it lets go on it.
    memset(&t, 0, sizeof(t));
    stop = -1;
    CHECK(yk_dial_touch_press(&t, x2, y2, 11, -1, T0, &stop) == YK_DIAL_GO_NONE && stop == -1);
    CHECK(yk_dial_touch_click(&t, x2 + 2, y2 - 1, 11, -1, T0 + 120, &stop) == YK_DIAL_GO_TURN && stop == 7);
    // ... held too long, or let go on the stop beside it: nothing.
    memset(&t, 0, sizeof(t));
    stop = -1;
    yk_dial_touch_press(&t, x2, y2, 11, -1, T0, &stop);
    CHECK(yk_dial_touch_click(&t, x2, y2, 11, -1, T0 + YK_TAP_HOLD_MS, &stop) == YK_DIAL_GO_NONE && stop == -1);
    memset(&t, 0, sizeof(t));
    yk_dial_touch_press(&t, x, y, 11, -1, T0, &stop);
    CHECK(yk_dial_touch_click(&t, x3, y3, 11, -1, T0 + 120, &stop) == YK_DIAL_GO_NONE && stop == -1);
    // From inside: nothing turns, and a swipe goes back, once.
    memset(&t, 0, sizeof(t));
    stop = -1;
    CHECK(yk_dial_touch_press(&t, YK_CX + 40, YK_CY, 11, -1, T0, &stop) == YK_DIAL_GO_NONE && stop == -1);
    CHECK(yk_dial_touch_move(&t, x, y, 11, -1, &stop) == YK_DIAL_GO_NONE && stop == -1);
    CHECK(yk_dial_touch_swipe(&t) == YK_DIAL_GO_DONE);
    CHECK(yk_dial_touch_swipe(&t) == YK_DIAL_GO_NONE);
    CHECK(yk_dial_touch_click(&t, YK_CX, YK_CY, 11, -1, T0 + 80, &stop) == YK_DIAL_GO_NONE);
    CHECK(yk_dial_touch_press(&t, x, y, 11, -1, T0, &stop) == YK_DIAL_GO_NONE);
    CHECK(yk_dial_touch_click(&t, x, y, 11, -1, T0 + 80, &stop) == YK_DIAL_GO_NONE);  // over: no tap either
    // A tap on the middle is 「好了」; down there and up beside it, or down beside it and up there, nothing.
    memset(&t, 0, sizeof(t));
    CHECK(yk_dial_touch_press(&t, YK_CX, YK_CY, 11, -1, T0, &stop) == YK_DIAL_GO_NONE);
    CHECK(yk_dial_touch_click(&t, YK_CX + 5, YK_CY - 5, 11, -1, T0 + 80, &stop) == YK_DIAL_GO_DONE);
    memset(&t, 0, sizeof(t));
    yk_dial_touch_press(&t, YK_CX + YK_DIAL_MIDDLE_R - 3, YK_CY, 11, -1, T0, &stop);
    CHECK(yk_dial_touch_click(&t, YK_CX + YK_DIAL_MIDDLE_R + 5, YK_CY, 11, -1, T0 + 80, &stop) == YK_DIAL_GO_NONE);
    memset(&t, 0, sizeof(t));
    yk_dial_touch_press(&t, YK_CX + YK_DIAL_MIDDLE_R + 5, YK_CY, 11, -1, T0, &stop);
    CHECK(yk_dial_touch_click(&t, YK_CX + YK_DIAL_MIDDLE_R - 3, YK_CY, 11, -1, T0 + 80, &stop) == YK_DIAL_GO_NONE);
    // ... and a slow drag on the middle, nothing.
    memset(&t, 0, sizeof(t));
    yk_dial_touch_press(&t, YK_CX - 30, YK_CY, 11, -1, T0, &stop);
    yk_dial_touch_move(&t, YK_CX + 30, YK_CY, 11, -1, &stop);
    CHECK(yk_dial_touch_click(&t, YK_CX, YK_CY, 11, -1, T0 + 500, &stop) == YK_DIAL_GO_NONE);
    // The cap holds a turn and a tap.
    memset(&t, 0, sizeof(t));
    yk_dial_touch_press(&t, x, y, 11, 5, T0, &stop);
    CHECK(yk_dial_touch_move(&t, x2, y2, 11, 5, &stop) == YK_DIAL_GO_TURN && stop == 5);
    memset(&t, 0, sizeof(t));
    yk_dial_touch_press(&t, x2, y2, 11, 5, T0, &stop);
    CHECK(yk_dial_touch_click(&t, x2, y2, 11, 5, T0 + 80, &stop) == YK_DIAL_GO_TURN && stop == 5);
}

/** WCAG 2's contrast of two 0xRRGGBB colours. */
static double luminance(uint32_t c) {
    double v[3];
    for (int i = 0; i < 3; i++) {
        const double x = ((c >> (16 - 8 * i)) & 0xFF) / 255.0;
        v[i] = x <= 0.03928 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
    }
    return 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2];
}
static double contrast(uint32_t a, uint32_t b) {
    const double la = luminance(a), lb = luminance(b);
    return (fmax(la, lb) + 0.05) / (fmin(la, lb) + 0.05);
}

int main(void) {
    // A setting's level (0.1.19 4.7): its arc 3 px off the disc, 4 wide (r 57-61), 270 degrees; the top place's outer edge
    // 211 px from the centre, clear of the rim band (214).
    CHECK(YK_RINGS_SAT_R + YK_RINGS_LEVEL_GAP == 57 && YK_RINGS_SAT_R + YK_RINGS_LEVEL_GAP + YK_RINGS_LEVEL_W == 61);
    CHECK(YK_RINGS_R + YK_RINGS_SAT_R + YK_RINGS_LEVEL_GAP + YK_RINGS_LEVEL_W == 211 &&
          YK_RINGS_R + YK_RINGS_SAT_R + YK_RINGS_LEVEL_GAP + YK_RINGS_LEVEL_W < YK_R_BAND);
    CHECK(YK_RINGS_LEVEL_DEG == 270 && YK_RINGS_SAT_R + YK_RINGS_IN_USE_GAP + YK_RINGS_IN_USE_W <= 61);
    // The middle in coral (battery low, 全部更新): its words #171C19 on #DF795D, 5.8 : 1 (the design's); every accent's thin
    // line readable on a place (3 : 1 for a line), and icons in ink on it.
    CHECK(YK_PAPER == 0x171C19 && contrast(YK_PAPER, YK_CORAL) >= 4.5);
    for (int c = 0; c < YK_ACCENT_COUNT; c++) {
        CHECK(contrast(YK_ACCENT_TABLE[c].text, YK_PANEL) >= 3.0);
        CHECK(contrast(YK_ACCENT_TABLE[c].on, YK_ACCENT_TABLE[c].accent) >= 4.5);
    }
    CHECK(contrast(YK_INK, YK_PANEL) >= 7.0);
    test_layout();
    test_hit();
    test_motion();
    test_touch();
    test_dial_scale();
    test_dial_layout();
    test_dial_layout_full();
    test_dial_title();
    test_dial_touch();
    test_dial_button();
    printf("yk_ring host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
