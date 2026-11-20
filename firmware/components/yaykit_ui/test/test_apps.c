// Host tests for the app ring, the app catalog and the hint arrow (yk_apps_logic.c). Run: bash test/run.sh
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yk_apps_logic.h"
#include "yaykit_ui.h"  // yk_ui_tap_t, yk_ui_tap_pair_line
#include "yk_more.h"
#include "yk_round.h"
#include "yk_each_lang.h"

static int failures, checks;
#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

/** Like the 16 px font: CJK 16, ASCII 9 (wide, to be safe), the ellipsis 16. */
static int width16(uint32_t cp, void *ctx) {
    (void)ctx;
    return cp < 0x80 ? 9 : 16;
}

static bool corner_in(int x, int y, int r) {
    const double dx = x - YK_CX, dy = y - YK_CY;
    return dx * dx + dy * dy <= (double)r * r;
}

static bool box_in(const yk_box_t *b, int r) {
    return corner_in(b->x, b->y, r) && corner_in(b->x + b->w, b->y, r) && corner_in(b->x, b->y + b->h, r) &&
           corner_in(b->x + b->w, b->y + b->h, r);
}

static void test_slots(void) {
    int s[YK_APPS_MAX];
    yk_apps_slots(5, s);
    for (int i = 0; i < 5; i++) CHECK(s[i] == i);
    yk_apps_slots(1, s);
    CHECK(s[0] == 0 && s[1] == -1);
    yk_apps_slots(2, s);
    CHECK(s[0] == 1 && s[1] == 4 && s[2] == -1);  // upper right, upper left
    yk_apps_slots(3, s);
    CHECK(s[0] == 0 && s[1] == 2 && s[2] == 3);   // top, lower right, lower left
    yk_apps_slots(4, s);
    CHECK(s[0] == 1 && s[1] == 2 && s[2] == 3 && s[3] == 4);
    yk_apps_slots(0, s);
    CHECK(s[0] == -1);
    yk_apps_slots(9, s);
    CHECK(s[4] == 4);
    // Symmetric about the vertical for every count: the slots' x offsets sum to 0.
    for (int n = 1; n <= 5; n++) {
        yk_apps_layout_t L;
        yk_apps_layout(n, &L);
        int sum = 0;
        for (int i = 0; i < n; i++) sum += L.sat[i].cx - YK_CX;
        CHECK(abs(sum) <= 1);
    }
}

static int spec_failures;
static void spec(bool ok, const char *what, int n, int i) {
    if (ok) return;
    spec_failures++;
    printf("  spec: %s (n=%d, place %d)\n", what, n, i);
}

/** The round-screen spec on a ring's n places (dev doc 4.7, yk_rings.h): targets at least 96 x 64, 12 apart and from the
 *  middle's; every disc, pressed too, and the in-use ring out of the rim band; the marks (`marks`: a bit per
 *  yk_mark_kind_t) inside the rim, on their disc's edge, clear of the other discs and the middle. */
static bool ring_spec(int n, const int16_t cx[], const int16_t cy[], unsigned marks) {
    const int before = spec_failures;
    const int in_use = YK_RINGS_SAT_R + YK_RINGS_IN_USE_GAP;  // the in-use ring's outer edge (yk_draw_in_use)
    spec(2 * YK_RINGS_HIT_R >= YK_TOUCH_MIN_W && 2 * YK_RINGS_HIT_R >= YK_TOUCH_MIN_H, "a target's size", n, -1);
    spec(2 * YK_RINGS_CENTER_HIT_R >= YK_TOUCH_MIN_W, "the middle's target", n, -1);
    for (int i = 0; i < n; i++) {
        const double r = hypot(cx[i] - YK_CX, cy[i] - YK_CY);
        spec(fabs(r - YK_RINGS_R) < 1.0, "on the circle", n, i);
        spec(r - YK_RINGS_HIT_R - YK_RINGS_CENTER_HIT_R >= YK_TOUCH_GAP, "apart from the middle's target", n, i);
        spec(r + YK_RINGS_HIT_R <= YK_R_EDGE, "its target on the glass", n, i);
        spec(r + YK_RINGS_PRESS_R < YK_R_BAND, "pressed, out of the rim band", n, i);
        spec(r + in_use < YK_R_BAND, "the in-use ring out of the rim band", n, i);
        spec(r - YK_RINGS_PRESS_R > YK_RINGS_CENTER_R + YK_RINGS_IN_USE_GAP + YK_RINGS_IN_USE_W, "pressed, clear of the middle", n, i);
        for (int j = i + 1; j < n; j++) {
            const double d = hypot(cx[i] - cx[j], cy[i] - cy[j]);
            if (n > YK_RINGS_SPACED) continue;
            spec(d - 2 * YK_RINGS_HIT_R >= YK_TOUCH_GAP, "targets 12 apart", n, i);
            spec(d > YK_RINGS_PRESS_R + in_use + YK_RINGS_IN_USE_W, "pressed, clear of the next", n, i);
        }
        for (int m = YK_MARK_UPDATE; m <= YK_MARK_DOWNLOAD; m++) {
            if (!(marks & 1u << m)) continue;
            int x, y;
            float outer;
            yk_rings_mark((yk_mark_kind_t)m, cx[i], cy[i], 1.0f, &x, &y, &outer);
            spec(hypot(x - YK_CX, y - YK_CY) + outer <= YK_RIM_R - YK_RIM_W, "a mark inside the rim", n, i);
            // on its disc's edge (the cached one a little past it, as on the old disc)
            spec(fabs(hypot(x - cx[i], y - cy[i]) - YK_RINGS_SAT_R) < 5, "a mark on its disc's edge", n, i);
            spec(hypot(x - YK_CX, y - YK_CY) > outer + YK_RINGS_CENTER_R + YK_RINGS_IN_USE_GAP, "a mark clear of the middle", n, i);
            for (int j = 0; j < n; j++)
                if (j != i) spec(hypot(x - cx[j], y - cy[j]) > outer + in_use, "a mark clear of the next disc", n, i);
        }
    }
    return spec_failures == before;
}

#define ALL_MARKS 0xFu

static void test_layout(void) {
    yk_apps_layout_t L;
    // Five apps, 12 o'clock first, every 72 degrees on the rings' circle (r 150).
    yk_apps_layout(5, &L);
    CHECK(L.n == 5);
    CHECK(L.sat[0].cx == 233 && L.sat[0].cy == 233 - 150);
    CHECK(L.sat[1].cx == 233 + 143 && L.sat[1].cy == 233 - 46);
    CHECK(L.sat[2].cx == 233 + 88 && L.sat[2].cy == 233 + 121);
    CHECK(L.sat[3].cx == 233 - 88 && L.sat[4].cx == 233 - 143);
    // Every count, the 5-place ring with 「更多」 and the 6-place ring with 「设置」: by the spec.
    for (int n = 0; n <= 5; n++) {
        yk_apps_layout(n, &L);
        int16_t cx[YK_APPS_MAX], cy[YK_APPS_MAX];
        for (int i = 0; i < L.n; i++) cx[i] = L.sat[i].cx, cy[i] = L.sat[i].cy;
        CHECK(L.n == n && ring_spec(L.n, cx, cy, ALL_MARKS));
    }
    for (int apps = 0; apps <= 4; apps++) {
        yk_apps_item_t items[YK_APPS_MAX];
        const int k = apps + 2;
        for (int i = 0; i < apps; i++) items[i] = (yk_apps_item_t){.slug = "x", .installed = true};
        items[apps] = (yk_apps_item_t){.slug = YK_APPS_MORE};
        items[apps + 1] = (yk_apps_item_t){.slug = YK_APPS_SETTINGS};
        int slots[YK_APPS_MAX];
        yk_apps_item_slots(items, k, slots);
        yk_apps_layout_at(k, slots, &L);
        int16_t cx[YK_APPS_MAX], cy[YK_APPS_MAX];
        for (int i = 0; i < L.n; i++) cx[i] = L.sat[i].cx, cy[i] = L.sat[i].cy;
        CHECK(ring_spec(L.n, cx, cy, ALL_MARKS));
    }
    CHECK(spec_failures == 0);
    // What a disc holds stays inside it: the icon (its corners clear, but drawn), 「更多」's dots, 「设置」's gear.
    CHECK(YK_RINGS_ICON / 2 * 1.4143f < YK_RINGS_SAT_R + 0.5f);
    CHECK(YK_APPS_DOT_GAP + YK_APPS_DOT_R < YK_RINGS_SAT_R);
    CHECK(9.2f * YK_APPS_GEAR_UNIT + YK_APPS_GEAR_UNIT < YK_RINGS_SAT_R);
    // The cached mark within the download mark's room (what each frame repaints).
    CHECK(YK_APPS_CACHE_D + YK_APPS_CACHE_R <= YK_APPS_BADGE_D + YK_APPS_BADGE_R);
    // The buddy inside the middle's disc, its square too (it is drawn on the disc's colour, corners and all).
    CHECK(YK_RINGS_FIGURE / 2.0 * 1.41422 <= YK_RINGS_CENTER_R);
}

/** Like the 24 px font: CJK 24, ASCII 13 (wide, to be safe), the ellipsis 24. */
static int width24(uint32_t cp, void *ctx) {
    (void)ctx;
    return cp < 0x80 ? 13 : 24;
}

/** The middle's words (yk_rings.h): a block of up to three lines about the centre, every line inside the middle's
 *  disc; a pressed place's name on one line or two. */
static void test_words(void) {
    const int heights[][3] = {{35, 0, 0}, {35, 35, 0}, {35, 23, 0}, {35, 23, 23}, {23, 23, 23}, {30, 30, 30}};
    for (size_t c = 0; c < sizeof(heights) / sizeof(heights[0]); c++) {
        const int n = heights[c][2] ? 3 : heights[c][1] ? 2 : 1;
        yk_box_t b[3];
        yk_rings_words(n, heights[c], b);
        int all = 0;
        for (int i = 0; i < n; i++) {
            all += b[i].h;
            CHECK(b[i].w == YK_RINGS_WORDS_W && b[i].x + b[i].w / 2 == YK_CX);
            CHECK(corner_in(b[i].x, b[i].y, YK_RINGS_CENTER_R) && corner_in(b[i].x + b[i].w, b[i].y + b[i].h, YK_RINGS_CENTER_R));
            CHECK(box_in(&b[i], YK_R_CONTENT));
            if (i) CHECK(b[i].y == b[i - 1].y + b[i - 1].h);
        }
        CHECK(all <= YK_RINGS_WORDS_H && abs(b[0].y + all / 2 - YK_CY) <= 1);
    }
    char l1[64], l2[64];
    CHECK(yk_rings_name_lines("呼吸之间", width24, NULL, YK_RINGS_WORDS_W, l1, sizeof(l1), l2, sizeof(l2)) == 1);
    CHECK(!strcmp(l1, "呼吸之间") && !l2[0]);
    CHECK(yk_rings_name_lines("面对面翻译", width24, NULL, YK_RINGS_WORDS_W, l1, sizeof(l1), l2, sizeof(l2)) == 1);  // 120
    CHECK(yk_rings_name_lines("面对面翻译机器", width24, NULL, YK_RINGS_WORDS_W, l1, sizeof(l1), l2, sizeof(l2)) == 2);
    CHECK(!strcmp(l1, "面对面翻译") && !strcmp(l2, "机器"));
    CHECK(yk_rings_name_lines("Speaking Coach", width24, NULL, YK_RINGS_WORDS_W, l1, sizeof(l1), l2, sizeof(l2)) == 2);
    CHECK(!strcmp(l1, "Speaking") && !strcmp(l2, "Coach"));
    CHECK(yk_rings_name_lines("回到 Tikpal", width24, NULL, YK_RINGS_WORDS_W, l1, sizeof(l1), l2, sizeof(l2)) == 2);
    CHECK(!strcmp(l1, "回到") && !strcmp(l2, "Tikpal"));
    CHECK(yk_rings_name_lines("Back to Tikpal", width24, NULL, YK_RINGS_WORDS_W, l1, sizeof(l1), l2, sizeof(l2)) == 2);
    CHECK(!strcmp(l1, "Back to") && !strcmp(l2, "Tikpal"));
    // A word longer than a line: cut where the line is full; the rest on the second, cut with "…".
    CHECK(yk_rings_name_lines("Supercalifragilistic", width24, NULL, YK_RINGS_WORDS_W, l1, sizeof(l1), l2, sizeof(l2)) == 2);
    CHECK(!strcmp(l1, "Supercali") && !strcmp(l2, "fragili…"));
    CHECK(yk_rings_name_lines("一二三四五六七八九十甲乙丙丁", width24, NULL, YK_RINGS_WORDS_W, l1, sizeof(l1), l2, sizeof(l2)) == 2);
    CHECK(!strcmp(l1, "一二三四五") && !strcmp(l2, "六七八九…"));
    CHECK(yk_rings_name_lines("", width24, NULL, YK_RINGS_WORDS_W, l1, sizeof(l1), l2, sizeof(l2)) == 0 && !l1[0]);
    CHECK(yk_rings_name_lines(NULL, width24, NULL, YK_RINGS_WORDS_W, l1, sizeof(l1), l2, sizeof(l2)) == 0);
    // Two lines of the name fit the middle's block.
    yk_box_t two[2];
    yk_rings_words(2, (const int[]){35, 35}, two);
    CHECK(corner_in(two[0].x, two[0].y, YK_RINGS_CENTER_R) && corner_in(two[1].x + two[1].w, two[1].y + two[1].h, YK_RINGS_CENTER_R));
}

static void test_motion(void) {
    yk_apps_layout_t L;
    yk_apps_layout(5, &L);
    yk_apps_pose_t p[YK_APPS_MAX + 1];
    // Start: the middle at 0.6 and clear; every app at the centre, 0.2, clear.
    CHECK(!yk_apps_frame(&L, 0, false, p));
    CHECK(p[0].scale == lroundf(256 * 0.6f) && p[0].opa == 0 && p[0].cx == YK_CX);
    for (int i = 1; i <= 5; i++) CHECK(p[i].cx == YK_CX && p[i].cy == YK_CY && p[i].scale == lroundf(256 * 0.2f) && p[i].opa == 0);
    // One after another: at 100 ms the first has gone further than the third, the fifth has not left yet.
    yk_apps_frame(&L, 100, false, p);
    const int d1 = abs(p[1].cy - YK_CY) + abs(p[1].cx - YK_CX), d3 = abs(p[3].cy - YK_CY) + abs(p[3].cx - YK_CX);
    CHECK(d1 > d3 && d3 > 0);
    CHECK(p[5].cx == YK_CX && p[5].cy == YK_CY && p[5].opa == 0);
    // The middle is grown at 240 ms.
    yk_apps_frame(&L, YK_APPS_CENTER_IN_MS, false, p);
    CHECK(p[0].scale == 256 && p[0].opa == 255);
    // Five in place before 300 ms (each flies 160 ms since the ring has 6 places).
    CHECK(yk_apps_frame(&L, YK_APPS_OPEN_MS, false, p));
    for (int i = 1; i <= 5; i++) CHECK(p[i].cx == L.sat[i - 1].cx && p[i].cy == L.sat[i - 1].cy && p[i].scale == 256 && p[i].opa == 255);
    CHECK(YK_APPS_FLY_MS == 160);
    // A full ring (6 places with 「设置」): everything in place by 300 ms, not before.
    {
        const yk_apps_item_t six[6] = {{.slug = "a", .installed = true}, {.slug = "b", .installed = true},
                                       {.slug = "c", .installed = true}, {.slug = "d", .installed = true},
                                       {.slug = YK_APPS_MORE}, {.slug = YK_APPS_SETTINGS}};
        int slots[YK_APPS_MAX];
        yk_apps_item_slots(six, 6, slots);
        yk_apps_layout_at(6, slots, &L);
        CHECK(!yk_apps_frame(&L, YK_APPS_OPEN_MS - 1, false, p));
        CHECK(yk_apps_frame(&L, YK_APPS_OPEN_MS, false, p));
        for (int i = 1; i <= 6; i++) CHECK(p[i].cx == L.sat[i - 1].cx && p[i].cy == L.sat[i - 1].cy && p[i].opa == 255);
    }
    // Fewer apps: done sooner, never later than 300.
    yk_apps_layout(2, &L);
    CHECK(yk_apps_frame(&L, YK_APPS_CENTER_IN_MS, false, p));
    // Closing: 180 ms back into the centre, gone.
    yk_apps_layout(5, &L);
    CHECK(!yk_apps_frame(&L, 0, true, p));
    CHECK(p[1].cx == L.sat[0].cx && p[1].opa == 255 && p[0].scale == 256);
    CHECK(!yk_apps_frame(&L, 90, true, p));
    CHECK(p[1].opa > 0 && p[1].opa < 255);
    CHECK(yk_apps_frame(&L, YK_APPS_CLOSE_MS, true, p));
    for (int i = 1; i <= 5; i++) CHECK(p[i].cx == YK_CX && p[i].cy == YK_CY && p[i].opa == 0);
    CHECK(p[0].opa == 0 && p[0].scale == lroundf(256 * 0.8f));
}

static void test_hit(void) {
    yk_apps_layout_t L;
    yk_apps_layout(5, &L);
    CHECK(yk_apps_hit(&L, YK_CX, YK_CY) == 0);
    CHECK(yk_apps_hit(&L, YK_CX, YK_CY + 70) == 0);  // the middle's disc, to its edge
    for (int i = 0; i < 5; i++) {
        CHECK(yk_apps_hit(&L, L.sat[i].cx, L.sat[i].cy) == i + 1);
        CHECK(yk_apps_hit(&L, L.sat[i].cx + 40, L.sat[i].cy + 38) == i + 1);  // its 116-wide target, past the disc
    }
    CHECK(yk_apps_hit(&L, YK_CX, YK_CY + 160) == -1);  // between the lower two
    CHECK(yk_apps_hit(&L, 20, 20) == -1);
    CHECK(yk_apps_hit(&L, YK_CX + 85, YK_CY - 120) == -1);  // between the top and the upper right
    // No touch hits two things: the targets never overlap (centre first, every pixel), on every ring.
    for (int n = 1; n <= 6; n++) {
        if (n == 6) {
            const yk_apps_item_t six[6] = {{.slug = "a"}, {.slug = "b"}, {.slug = "c"}, {.slug = "d"},
                                           {.slug = YK_APPS_MORE}, {.slug = YK_APPS_SETTINGS}};
            int slots[YK_APPS_MAX];
            yk_apps_item_slots(six, 6, slots);
            yk_apps_layout_at(6, slots, &L);
        } else {
            yk_apps_layout(n, &L);
        }
        for (int y = 0; y < 466; y += 2)
            for (int x = 0; x < 466; x += 2) {
                int hits = 0;
                for (int i = 0; i < L.n; i++) {
                    const int dx = x - L.sat[i].cx, dy = y - L.sat[i].cy;
                    hits += dx * dx + dy * dy <= YK_APPS_HIT_R * YK_APPS_HIT_R;
                }
                const int dx = x - YK_CX, dy = y - YK_CY;
                hits += dx * dx + dy * dy <= YK_APPS_CENTER_HIT_R * YK_APPS_CENTER_HIT_R;
                if (hits > 1) {
                    CHECK(hits <= 1);
                    y = 466;
                    break;
                }
            }
    }
}

/** Gestures doc 7: a swipe on the ring closes it towards the finger; letting go on an app (or on the app in use) is
 *  not a tap. Taps on an app, the middle and the paper are as before. */
#define T0 1000u  // a press's time (ms): a tap lets go well inside YK_TAP_HOLD_MS

/** A finger down at (x, y) on the app ring at T0. */
static void press(yk_apps_touch_t *t, const yk_apps_layout_t *L, int x, int y) {
    yk_apps_touch_press(t, yk_apps_hit(L, x, y), x, y, T0);
}

/** The same rule at the input device, for an app's own objects (0.1.20, F-1018: yk_ui_taps_strict): a drag lifted on a
 *  button is no click any more; a tap still is. */
static void test_tap_strict(void) {
    yk_tap_t t = {0};
    // A tap: down and up on the object, the finger still.
    yk_tap_press(&t, 0, 200, 300, T0);
    CHECK(yk_tap_strict(&t, true, true, false, 203, 302, T0 + 90) && !t.down);
    // Dragged across it and lifted on it (LVGL's CLICKED comes all the same): let go further than YK_TAP_SLOP off.
    yk_tap_press(&t, 0, 200, 300, T0);
    CHECK(!yk_tap_strict(&t, true, true, false, 200 + YK_TAP_SLOP + 1, 300, T0 + 300) && t.why == YK_TAP_MOVED);
    // ... or a jump between two readings, or a swipe recognised, though it ends near where it began.
    yk_tap_press(&t, 0, 200, 300, T0);
    CHECK(!yk_tap_strict(&t, true, true, true, 202, 300, T0 + 300) && t.why == YK_TAP_SWIPED);
    // Let go off it (LVGL keeps the press with what it went down on): none.
    yk_tap_press(&t, 0, 200, 300, T0);
    CHECK(!yk_tap_strict(&t, true, false, false, 204, 300, T0 + 90) && t.why == YK_TAP_OTHER);
    // A click for another object than the press went down on, or with no press seen: none.
    yk_tap_press(&t, 0, 200, 300, T0);
    CHECK(!yk_tap_strict(&t, false, true, false, 200, 300, T0 + 90) && t.why == YK_TAP_OTHER);
    CHECK(!yk_tap_strict(&t, true, true, false, 200, 300, T0 + 90));  // the press is over
    // Held YK_TAP_HOLD_MS: none; a slow second still taps.
    yk_tap_press(&t, 0, 200, 300, T0);
    CHECK(!yk_tap_strict(&t, true, true, false, 200, 300, T0 + YK_TAP_HOLD_MS) && t.why == YK_TAP_HELD);
    yk_tap_press(&t, 0, 200, 300, T0);
    CHECK(yk_tap_strict(&t, true, true, false, 200, 300, T0 + 1000));
}

/** A tap (0.1.18, dev doc 4.8): down and up on the same thing, the finger still, not held. */
static void test_tap(void) {
    yk_tap_t t = {0};
    // Down and up on the same: a tap, once.
    yk_tap_press(&t, 3, 100, 100, T0);
    CHECK(yk_tap_release(&t, 3, 100, 100, T0 + 90));
    CHECK(!t.down);
    // Up on another, or down on another and up on this one: none.
    yk_tap_press(&t, 3, 100, 100, T0);
    CHECK(!yk_tap_release(&t, 4, 104, 100, T0 + 90));
    yk_tap_press(&t, -1, 100, 100, T0);
    CHECK(!yk_tap_release(&t, 3, 102, 100, T0 + 90));
    // The finger may wander YK_TAP_SLOP each way, no more; gone further and back is none either (a slow drag).
    yk_tap_press(&t, 0, 100, 100, T0);
    CHECK(yk_tap_move(&t, 100 + YK_TAP_SLOP, 100 - YK_TAP_SLOP));
    CHECK(yk_tap_release(&t, 0, 100 - YK_TAP_SLOP, 100 + YK_TAP_SLOP, T0 + 200));
    yk_tap_press(&t, 0, 100, 100, T0);
    CHECK(!yk_tap_move(&t, 100 + YK_TAP_SLOP + 1, 100));
    CHECK(!yk_tap_move(&t, 100, 100));
    CHECK(!yk_tap_release(&t, 0, 100, 100, T0 + 200));
    // Let go further off than PRESSING last saw (a sample between): none.
    yk_tap_press(&t, 0, 100, 100, T0);
    CHECK(!yk_tap_release(&t, 0, 100, 100 + YK_TAP_SLOP + 1, T0 + 200));
    // Held YK_TAP_HOLD_MS or more: none; the tick's wrap is no hold.
    yk_tap_press(&t, 0, 100, 100, T0);
    CHECK(!yk_tap_release(&t, 0, 100, 100, T0 + YK_TAP_HOLD_MS));
    yk_tap_press(&t, 0, 100, 100, T0);
    CHECK(yk_tap_release(&t, 0, 100, 100, T0 + YK_TAP_HOLD_MS - 1));
    // A slow press (a child's, an old hand's: a second) still taps (1.5 s since the firmware lead's 11-11 call).
    CHECK(YK_TAP_HOLD_MS == 1500);
    yk_tap_press(&t, 0, 100, 100, T0);
    CHECK(yk_tap_release(&t, 0, 100, 100, T0 + 1000));
    yk_tap_press(&t, 0, 100, 100, 0xFFFFFFF0u);
    CHECK(yk_tap_release(&t, 0, 100, 100, 0x40u));
    // A swipe, a press lost: none.
    yk_tap_press(&t, 0, 100, 100, T0);
    yk_tap_cancel(&t);
    CHECK(!yk_tap_release(&t, 0, 100, 100, T0 + 90));
    // Pressed down and never let go: nothing ever said it was one.
    yk_tap_press(&t, 0, 100, 100, T0);
    CHECK(t.down && yk_tap_move(&t, 100, 100));
    // Why not, for the YK_TAP line the acceptance's scripts read (`yay finger` plays the finger).
    char line[64];
    yk_tap_press(&t, 2, 100, 100, T0);
    CHECK(yk_tap_release(&t, 2, 101, 100, T0 + 80) && t.why == YK_TAP_YES);
    CHECK(!strcmp(yk_tap_line(&t, "apps", line, sizeof(line)), "YK_TAP apps tap down=2 up=2"));
    yk_tap_press(&t, 2, 100, 100, T0);
    CHECK(!yk_tap_release(&t, 3, 104, 100, T0 + 80) && t.why == YK_TAP_OTHER);
    CHECK(!strcmp(yk_tap_line(&t, "ring", line, sizeof(line)), "YK_TAP ring other down=2 up=3"));
    yk_tap_press(&t, 2, 100, 100, T0);
    yk_tap_move(&t, 140, 100);
    yk_tap_cancel(&t);  // a swipe after it moved: it moved first
    CHECK(!yk_tap_release(&t, 2, 100, 100, T0 + 900) && t.why == YK_TAP_MOVED);
    yk_tap_press(&t, 2, 100, 100, T0);
    CHECK(!yk_tap_release(&t, 2, 100, 100, T0 + YK_TAP_HOLD_MS) && t.why == YK_TAP_HELD);
    yk_tap_press(&t, 2, 100, 100, T0);
    yk_tap_cancel(&t);
    CHECK(!yk_tap_release(&t, 2, 100, 100, T0 + 80) && t.why == YK_TAP_SWIPED);
    CHECK(!strcmp(yk_tap_why_name(YK_TAP_SWIPED), "swiped") && !strcmp(yk_tap_why_name(9), "?"));
    // A release with no press seen here (it went down elsewhere), even right after a tap: none.
    yk_tap_press(&t, 2, 100, 100, T0);
    CHECK(yk_tap_release(&t, 2, 100, 100, T0 + 80));
    CHECK(!yk_tap_release(&t, 2, 100, 100, T0 + 160) && t.why == YK_TAP_OTHER);
    yk_tap_t none = {0};
    CHECK(!yk_tap_release(&none, 0, 0, 0, 10));
}

/** The ring's taps by the rule: what went down on an app and let go on another, slow drags, holds (dev doc 4.8). */
static void test_touch_tap(void) {
    yk_apps_layout_t L;
    yk_apps_layout(5, &L);
    const int ax = L.sat[0].cx, ay = L.sat[0].cy, bx = L.sat[1].cx, by = L.sat[1].cy;
    // Down on app 1, up on app 2 (and the other way, and from the paper or the middle onto an app): nothing opens.
    const int from[][2] = {{ax, ay}, {bx, by}, {20, 20}, {YK_CX, YK_CY}};
    const int to[][2] = {{bx, by}, {ax, ay}, {ax, ay}, {ax, ay}};
    for (int k = 0; k < 4; k++) {
        yk_apps_touch_t t = {0};
        press(&t, &L, from[k][0], from[k][1]);
        yk_apps_touch_move(&t, to[k][0], to[k][1]);
        CHECK(yk_apps_touch_click(&t, &L, to[k][0], to[k][1], T0 + 300).kind == YK_APPS_GO_NONE);
        // ... and the ring stays: the next finger's tap counts
        press(&t, &L, bx, by);
        const yk_apps_go_t go = yk_apps_touch_click(&t, &L, bx, by, T0 + 80);
        CHECK(go.kind == YK_APPS_GO_TAP && go.hit == 2);
    }
    // Down on an app, slid off it onto the paper: nothing.
    {
        yk_apps_touch_t t = {0};
        press(&t, &L, ax, ay);
        CHECK(yk_apps_touch_click(&t, &L, YK_CX + 150, YK_CY, T0 + 200).kind == YK_APPS_GO_NONE);
    }
    // A slow drag within one app's target (no swipe: LVGL saw it too slow): no tap. A little wobble is still one.
    {
        yk_apps_touch_t t = {0};
        press(&t, &L, ax, ay);
        yk_apps_touch_move(&t, ax + 30, ay);
        yk_apps_touch_move(&t, ax + 2, ay);
        CHECK(yk_apps_touch_click(&t, &L, ax + 2, ay, T0 + 400).kind == YK_APPS_GO_NONE);
        press(&t, &L, ax, ay);
        yk_apps_touch_move(&t, ax + 6, ay - 5);
        const yk_apps_go_t go = yk_apps_touch_click(&t, &L, ax + 4, ay - 3, T0 + 150);
        CHECK(go.kind == YK_APPS_GO_TAP && go.hit == 1);
    }
    // Held on an app past YK_TAP_HOLD_MS: no tap.
    {
        yk_apps_touch_t t = {0};
        press(&t, &L, ax, ay);
        CHECK(yk_apps_touch_click(&t, &L, ax, ay, T0 + YK_TAP_HOLD_MS).kind == YK_APPS_GO_NONE);
    }
    // A press that never lets go: no CLICKED, and the press after it starts over.
    {
        yk_apps_touch_t t = {0};
        press(&t, &L, ax, ay);
        press(&t, &L, bx, by);
        CHECK(yk_apps_touch_click(&t, &L, bx, by, T0 + 80).hit == 2);
    }
}

static void test_touch(void) {
    yk_apps_layout_t L;
    yk_apps_layout(5, &L);
    // A tap: on an app, the middle, the paper.
    for (int i = 0; i < 5; i++) {
        yk_apps_touch_t t = {0};
        press(&t, &L, L.sat[i].cx, L.sat[i].cy);
        const yk_apps_go_t go = yk_apps_touch_click(&t, &L, L.sat[i].cx, L.sat[i].cy, T0 + 80);
        CHECK(go.kind == YK_APPS_GO_TAP && go.hit == i + 1);
        // the ring is closing: a second click (another finger, a late event) does nothing
        press(&t, &L, L.sat[i].cx, L.sat[i].cy);
        CHECK(yk_apps_touch_click(&t, &L, L.sat[i].cx, L.sat[i].cy, T0 + 80).kind == YK_APPS_GO_NONE);
    }
    {
        yk_apps_touch_t t = {0};
        press(&t, &L, YK_CX, YK_CY);
        const yk_apps_go_t go = yk_apps_touch_click(&t, &L, YK_CX, YK_CY, T0 + 80);
        CHECK(go.kind == YK_APPS_GO_TAP && go.hit == 0);
    }
    {
        yk_apps_touch_t t = {0};
        press(&t, &L, 20, 20);
        const yk_apps_go_t go = yk_apps_touch_click(&t, &L, 20, 20, T0 + 80);
        CHECK(go.kind == YK_APPS_GO_TAP && go.hit == -1);
    }
    // A swipe each way, letting go on every app and on the middle: it goes away towards the finger, nothing tapped.
    const int ways[4][2] = {{1, 0}, {-1, 0}, {0, -1}, {0, 1}};
    for (int w = 0; w < 4; w++)
        for (int i = 0; i <= 5; i++) {
            yk_apps_touch_t t = {0};
            const int x = i < 5 ? L.sat[i].cx : YK_CX, y = i < 5 ? L.sat[i].cy : YK_CY;
            // even one that went down where it lets go, quickly (LVGL measured a fast flick there)
            press(&t, &L, x, y);
            const yk_apps_go_t go = yk_apps_touch_swipe(&t, ways[w][0], ways[w][1]);
            CHECK(go.kind == YK_APPS_GO_AWAY && go.dx == ways[w][0] && go.dy == ways[w][1]);
            CHECK(yk_apps_touch_click(&t, &L, x, y, T0 + 80).kind == YK_APPS_GO_NONE);
            CHECK(yk_apps_touch_swipe(&t, 1, 0).kind == YK_APPS_GO_NONE);  // one swipe, one close
            // A tap the guard kept (first setup's ring): the ring stays and the next finger closes it.
            yk_apps_touch_t kept = {0};
            press(&kept, &L, L.sat[0].cx, L.sat[0].cy);
            CHECK(yk_apps_touch_click(&kept, &L, L.sat[0].cx, L.sat[0].cy, T0 + 80).kind == YK_APPS_GO_TAP);
            yk_apps_touch_stays(&kept);
            press(&kept, &L, L.sat[0].cx, L.sat[0].cy);
            CHECK(yk_apps_touch_swipe(&kept, ways[w][0], ways[w][1]).kind == YK_APPS_GO_AWAY);
        }
    // A swipe is a swipe however LVGL measured it (the way is only ever one of four).
    {
        yk_apps_touch_t t = {0};
        press(&t, &L, YK_CX, YK_CY);
        CHECK(yk_apps_touch_swipe(&t, 0, 0).kind == YK_APPS_GO_NONE);
        const yk_apps_go_t go = yk_apps_touch_swipe(&t, -3, 0);
        CHECK(go.kind == YK_APPS_GO_AWAY && go.dx == -1 && go.dy == 0);
    }
    // Closing some other way (8 s, the side key, yk_apps_tap): neither a tap nor a swipe counts any more.
    {
        yk_apps_touch_t t = {0};
        press(&t, &L, L.sat[0].cx, L.sat[0].cy);
        yk_apps_touch_closing(&t);
        CHECK(yk_apps_touch_click(&t, &L, L.sat[0].cx, L.sat[0].cy, T0 + 80).kind == YK_APPS_GO_NONE);
        CHECK(yk_apps_touch_swipe(&t, 0, 1).kind == YK_APPS_GO_NONE);
    }
    // The motion: 200 ms that way, full size, clear at the end; the other axis stays.
    yk_apps_pose_t p[YK_APPS_MAX + 1];
    CHECK(!yk_apps_frame_away(&L, 0, 1, 0, p));
    CHECK(p[0].cx == YK_CX && p[0].cy == YK_CY && p[0].opa == 255 && p[0].scale == 256);
    for (int i = 1; i <= 5; i++) CHECK(p[i].cx == L.sat[i - 1].cx && p[i].cy == L.sat[i - 1].cy && p[i].opa == 255);
    CHECK(!yk_apps_frame_away(&L, YK_APPS_AWAY_MS / 2, 1, 0, p));
    for (int i = 1; i <= 5; i++)
        CHECK(p[i].cx > L.sat[i - 1].cx && p[i].cx < L.sat[i - 1].cx + YK_APPS_AWAY_PX && p[i].cy == L.sat[i - 1].cy &&
              p[i].opa > 0 && p[i].opa < 255 && p[i].scale == 256);
    CHECK(yk_apps_frame_away(&L, YK_APPS_AWAY_MS, 1, 0, p));
    CHECK(p[0].cx == YK_CX + YK_APPS_AWAY_PX && p[0].opa == 0);
    for (int i = 1; i <= 5; i++)
        CHECK(p[i].cx == L.sat[i - 1].cx + YK_APPS_AWAY_PX && p[i].opa == 0 && p[i].scale == 256);
    CHECK(yk_apps_frame_away(&L, YK_APPS_AWAY_MS, 0, -1, p));
    CHECK(p[0].cx == YK_CX && p[0].cy == YK_CY - YK_APPS_AWAY_PX);
    for (int i = 1; i <= 5; i++) CHECK(p[i].cx == L.sat[i - 1].cx && p[i].cy == L.sat[i - 1].cy - YK_APPS_AWAY_PX);
    CHECK(yk_apps_frame_away(&L, YK_APPS_AWAY_MS, -1, 0, p) && p[0].cx == YK_CX - YK_APPS_AWAY_PX);
    CHECK(yk_apps_frame_away(&L, YK_APPS_AWAY_MS, 0, 1, p) && p[0].cy == YK_CY + YK_APPS_AWAY_PX);
}

/** The phone mark (voice-code handoff 2.1②): on the disc's upper right, in the safe circle at the places the app in
 *  use can take, apart from the badges, the names, the middle's label and the other places; a touch on it is its
 *  place's, and no touch hits two things. */
/** A finger down on a place (yk_rings.h): it grows and the others dim while the press may still be a tap there; moved
 *  off, held YK_TAP_HOLD_MS, swiped, let go, the ring going: as at rest. */
static void test_press(void) {
    yk_apps_layout_t L;
    yk_apps_layout(5, &L);
    yk_apps_pose_t rest[YK_APPS_MAX + 1], p[YK_APPS_MAX + 1];
    CHECK(yk_apps_frame(&L, YK_APPS_OPEN_MS, false, rest));
    // Nothing pressed, or the middle: as at rest.
    uint32_t dim = 0;
    memcpy(p, rest, sizeof(p));
    CHECK(yk_rings_press(5, -1, 50, &dim, p) && !memcmp(p, rest, sizeof(p)) && !dim);
    CHECK(yk_rings_press(5, 0, 50, &dim, p) && !memcmp(p, rest, sizeof(p)) && !dim);
    CHECK(yk_rings_press(5, 6, 50, &dim, p) && !memcmp(p, rest, sizeof(p)) && !dim);
    // Place 2, a frame at a time: it grows over 100 ms; the others dim one a frame, clockwise from it (3, 4, 5, 1), so
    // that a frame repaints the pressed place and one other at most (0.1.18: all five at once took ~60 ms a frame).
    static const int order[4] = {3, 4, 5, 1};
    for (int f = 0; f < 4; f++) {
        memcpy(p, rest, sizeof(p));
        const uint32_t before = dim;
        const bool still = yk_rings_press(5, 2, f * 16, &dim, p);
        CHECK(dim == (before | 1u << (order[f] - 1)) && !(dim & 1u << 1));  // one more, never the pressed one
        CHECK(still == (f == 3 && f * 16 >= YK_RINGS_PRESS_MS));
        for (int e = 1; e <= 5; e++) CHECK(p[e].opa == (dim & 1u << (e - 1) ? YK_RINGS_DIM_OPA : 255));
        CHECK(p[0].opa == 255 && p[0].scale == 256);
        if (f == 0) CHECK(p[2].scale == 256);
        if (f == 2) CHECK(p[2].scale > 256 && p[2].scale < 256 * YK_RINGS_PRESS_R / YK_RINGS_SAT_R);
    }
    CHECK(dim == 0x1Du);  // all but place 2
    memcpy(p, rest, sizeof(p));
    CHECK(yk_rings_press(5, 2, YK_RINGS_PRESS_MS, &dim, p) && dim == 0x1Du);  // grown and dimmed: still
    CHECK(abs(p[2].scale * YK_RINGS_SAT_R - 256 * YK_RINGS_PRESS_R) <= YK_RINGS_SAT_R);  // 124 across
    for (int e = 0; e <= 5; e++) CHECK(p[e].cx == rest[e].cx && p[e].cy == rest[e].cy);
    CHECK(YK_RINGS_PRESS_MS <= YK_RESPONSE_MS);
    // Let go (moved off, held): back to full size at once, the others back one a frame, clockwise from the top.
    int frames = 0;
    for (bool still = false; !still && frames < 10; frames++) {
        memcpy(p, rest, sizeof(p));
        const uint32_t before = dim;
        still = yk_rings_press(5, -1, 0, &dim, p);
        CHECK(p[2].scale == 256 && __builtin_popcount(before ^ dim) == 1);
    }
    CHECK(frames == 4 && dim == 0);
    // Pressed again on another place before the others came back: they go straight to the new one's look.
    dim = 0x1Du;
    memcpy(p, rest, sizeof(p));
    yk_rings_press(5, 4, 0, &dim, p);
    CHECK(dim == 0x15u);  // one change a frame, the pressed one (4) first: it lights ...
    yk_rings_press(5, 4, 16, &dim, p);
    CHECK(dim == 0x17u);  // ... then place 2 dims
    // The 6-place ring: 5 others, five frames.
    dim = 0;
    for (frames = 0; frames < 10 && dim != 0x3Eu; frames++) yk_rings_press(6, 1, 200, &dim, p);
    CHECK(frames == 5);

    // What a press shows: what it went down on, while it may still be a tap there.
    yk_apps_touch_t t = {0};
    CHECK(yk_rings_pressed(&t, T0) == -1);  // no press
    const int bx = L.sat[1].cx, by = L.sat[1].cy;
    press(&t, &L, bx, by);
    CHECK(yk_rings_pressed(&t, T0) == 2 && yk_rings_pressed(&t, T0 + YK_TAP_HOLD_MS - 1) == 2);
    CHECK(yk_rings_pressed(&t, T0 + YK_TAP_HOLD_MS) == -1);  // held: it lets go of the place, and nothing opens
    yk_apps_touch_move(&t, bx + YK_TAP_SLOP, by);
    CHECK(yk_rings_pressed(&t, T0 + 100) == 2);
    yk_apps_touch_move(&t, bx + YK_TAP_SLOP + 1, by);
    CHECK(yk_rings_pressed(&t, T0 + 100) == -1);  // moved off ...
    yk_apps_touch_move(&t, bx, by);
    CHECK(yk_rings_pressed(&t, T0 + 100) == -1);  // ... and back: still none
    press(&t, &L, bx, by);
    CHECK(yk_apps_touch_swipe(&t, 0, -1).kind == YK_APPS_GO_AWAY && yk_rings_pressed(&t, T0 + 50) == -1);
    t = (yk_apps_touch_t){0};
    press(&t, &L, YK_CX, YK_CY);
    CHECK(yk_rings_pressed(&t, T0 + 50) == 0);  // the middle: its own words there
    press(&t, &L, 20, 20);
    CHECK(yk_rings_pressed(&t, T0 + 50) == -1);  // the paper
    press(&t, &L, bx, by);
    CHECK(yk_apps_touch_click(&t, &L, bx, by, T0 + 80).kind == YK_APPS_GO_TAP);
    CHECK(yk_rings_pressed(&t, T0 + 90) == -1);  // let go: it opens, the ring goes
    t = (yk_apps_touch_t){0};
    press(&t, &L, bx, by);
    yk_apps_touch_closing(&t);
    CHECK(yk_rings_pressed(&t, T0 + 50) == -1);  // the ring going some other way
}

static void test_phone_mark(void) {
    _Static_assert(offsetof(yk_apps_item_t, phone) > offsetof(yk_apps_item_t, update), "phone is added last");
    const yk_apps_item_t plain = {.slug = "breathe", .installed = true};  // as an extension built before makes them
    CHECK(!plain.phone);
    int x, y;
    float r, edge;
    yk_apps_phone_mark(233, 105, 1.0f, &x, &y, &r, &edge);  // a YK_MARK_DISC_R disc's (「我的应用」 scales it)
    CHECK(x == 233 + YK_APPS_PHONE_D && y == 105 - YK_APPS_PHONE_D && r == YK_APPS_PHONE_R && edge > r);
    // On the rings: where the update dot would be, on the bigger disc's edge, the size it was made.
    float outer;
    yk_rings_mark(YK_MARK_PHONE, 233, 83, 1.0f, &x, &y, &outer);
    CHECK(x > 233 && y < 83 && outer == edge && fabs(hypot(x - 233, y - 83) - YK_RINGS_SAT_R) < 3);
    for (int six = 0; six < 2; six++)
        for (int k = 1; k <= (six ? 1 : 5); k++) {
            yk_apps_layout_t L;
            if (six) {
                yk_apps_item_t items[6] = {{.slug = "a"}, {.slug = "b"}, {.slug = "c"}, {.slug = "d"},
                                           {.slug = YK_APPS_MORE}, {.slug = YK_APPS_SETTINGS}};
                int slots[YK_APPS_MAX];
                yk_apps_item_slots(items, 6, slots);
                yk_apps_layout_at(6, slots, &L);
            } else {
                yk_apps_layout(k, &L);
            }
            // The app in use is in a slot, and yk_apps_pick_ring() puts the (at most two) apps in a slot first: the mark
            // is on one of the first two places. Both with a mark at once is the hardest case.
            const int napps = L.n < 2 ? L.n : 2;
            for (int i = 0; i < napps; i++) L.sat[i].phone = true;
            for (int i = 0; i < napps; i++) {
                const int cx = L.sat[i].cx, cy = L.sat[i].cy;
                yk_rings_mark(YK_MARK_PHONE, cx, cy, 1.0f, &x, &y, &outer);
                int bx, by;
                float bo;
                yk_rings_mark(YK_MARK_DOWNLOAD, cx, cy, 1.0f, &bx, &by, &bo);
                CHECK(hypot(x - bx, y - by) > outer + bo);
                // a touch on the mark, even at its edge, is its place's
                const int e = (int)ceilf(outer);
                CHECK(yk_apps_hit(&L, x, y) == i + 1);
                CHECK(yk_apps_hit(&L, x + e, y - e / 2) == i + 1);
            }
            // No touch hits two things, the marks' targets included.
            for (int py = 0; py < 466; py += 2)
                for (int px = 0; px < 466; px += 2) {
                    int hits = 0;
                    for (int i = 0; i < L.n; i++) {
                        const int dx = px - L.sat[i].cx, dy = py - L.sat[i].cy;
                        bool on = dx * dx + dy * dy <= YK_APPS_HIT_R * YK_APPS_HIT_R;
                        if (L.sat[i].phone) {
                            yk_rings_mark(YK_MARK_PHONE, L.sat[i].cx, L.sat[i].cy, 1.0f, &x, &y, &outer);
                            const int R = (int)ceilf(outer) + 2, ex = px - x, ey = py - y;
                            on |= ex * ex + ey * ey <= R * R;
                        }
                        hits += on;
                    }
                    const int dx = px - YK_CX, dy = py - YK_CY;
                    hits += dx * dx + dy * dy <= YK_APPS_CENTER_HIT_R * YK_APPS_CENTER_HIT_R;
                    if (hits > 1) {
                        CHECK(hits <= 1);
                        py = 466;
                        break;
                    }
                }
        }
}

static yk_catalog_t demo_catalog(void) {
    yk_catalog_t c = {.count = 6, .written_ms = 1792549233000LL, .lang = "zh", .seq = 7};
    const char *slugs[] = {"language-buddy", "breathe", "party-play", "homework-buddy", "translator", "yay"};
    const char *names[] = {"口语陪练", "呼吸之间", "聚会玩", "作业伙伴", "面对面翻译", "Yay"};
    for (int i = 0; i < c.count; i++) {
        strcpy(c.app[i].slug, slugs[i]);
        strcpy(c.app[i].name, names[i]);
        strcpy(c.app[i].version, "1.0.0");
        c.app[i].size = 2800000 + i;
    }
    return c;
}

static void test_pick(void) {
    yk_catalog_t cat = demo_catalog();
    yk_apps_item_t out[YK_APPS_MAX];
    // Installed: Party Play used before Breathe; Yay and an empty slot are skipped.
    const yk_apps_slot_t inst[] = {{"party-play", 100}, {"yay", 999}, {"breathe", 200}, {"", 300}, {NULL, 0}};
    int n = yk_apps_pick(inst, 5, &cat, out, YK_APPS_MAX);
    CHECK(n == 5);
    CHECK(!strcmp(out[0].slug, "breathe") && out[0].installed && !strcmp(out[0].name, "呼吸之间"));
    CHECK(!strcmp(out[1].slug, "party-play") && out[1].installed);
    CHECK(!strcmp(out[2].slug, "language-buddy") && !out[2].installed && !strcmp(out[2].name, "口语陪练"));
    CHECK(!strcmp(out[3].slug, "homework-buddy") && !strcmp(out[4].slug, "translator"));
    for (int i = 0; i < n; i++) CHECK(strcmp(out[i].slug, "yay") && !out[i].icon);
    // No catalog (never online): only what is installed, names left to the ring.
    n = yk_apps_pick(inst, 5, NULL, out, YK_APPS_MAX);
    CHECK(n == 2 && !strcmp(out[0].slug, "breathe") && out[0].name == NULL && !strcmp(out[1].slug, "party-play"));
    // A limit, and the same app twice in the slots (an old copy) counted once.
    const yk_apps_slot_t twice[] = {{"breathe", 5}, {"breathe", 9}};
    n = yk_apps_pick(twice, 2, &cat, out, 3);
    CHECK(n == 3 && !strcmp(out[0].slug, "breathe") && !strcmp(out[1].slug, "language-buddy") && !strcmp(out[2].slug, "party-play"));
    CHECK(yk_apps_pick(NULL, 0, NULL, out, 5) == 0);
    CHECK(yk_catalog_find(&cat, "party-play") == &cat.app[2] && !yk_catalog_find(&cat, "nope") && !yk_catalog_find(NULL, "x"));
}

/** The ring with 「更多」 (app-ecosystem.md 4.5): at most 4 apps, those in a slot, then those in the package cache,
 *  then the catalog's; 「更多」, always, last. Apps only since 0.1.14 (system-pulldown.md 2): no 「设置」 (0.1.6 to 0.1.13
 *  put it after 「更多」, settings-u.md 5), so the 5-place ring again. */
static void test_pick_ring(void) {
    yk_catalog_t cat = demo_catalog();
    yk_apps_item_t out[YK_APPS_MAX];
    const yk_apps_slot_t inst[] = {{"party-play", 100}, {"yay", 999}, {"breathe", 200}};
    const char *const cached[] = {"homework-buddy", "breathe", "yay", "+more", "+settings", ""};
    int n = yk_apps_pick_ring(inst, 3, cached, 6, &cat, out, YK_APPS_MAX);
    CHECK(n == 5);
    CHECK(!strcmp(out[0].slug, "breathe") && out[0].installed && !out[0].cached && !strcmp(out[0].name, "呼吸之间"));
    CHECK(!strcmp(out[1].slug, "party-play") && out[1].installed);
    CHECK(!strcmp(out[2].slug, "homework-buddy") && !out[2].installed && out[2].cached && out[2].name);
    CHECK(!strcmp(out[3].slug, "language-buddy") && !out[3].installed && !out[3].cached);  // the catalog's first
    CHECK(!strcmp(out[4].slug, YK_APPS_MORE) && !out[4].installed && !out[4].cached && !out[4].name);
    for (int i = 0; i < n; i++) CHECK(strcmp(out[i].slug, YK_APPS_SETTINGS));  // not even from the cache's list
    // Nothing anywhere (a new device, never online): 「更多」 alone.
    n = yk_apps_pick_ring(NULL, 0, NULL, 0, NULL, out, YK_APPS_MAX);
    CHECK(n == 1 && !strcmp(out[0].slug, YK_APPS_MORE));
    // Only what is on the device, no catalog: names left to the ring.
    n = yk_apps_pick_ring(inst, 3, cached, 2, NULL, out, YK_APPS_MAX);
    CHECK(n == 4 && !strcmp(out[2].slug, "homework-buddy") && out[2].cached && !out[2].name && !strcmp(out[3].slug, YK_APPS_MORE));
    // A limit counts 「更多」; 「更多」 alone when only one fits.
    n = yk_apps_pick_ring(inst, 3, cached, 6, &cat, out, 3);
    CHECK(n == 3 && !strcmp(out[0].slug, "breathe") && !strcmp(out[1].slug, "party-play") && !strcmp(out[2].slug, YK_APPS_MORE));
    n = yk_apps_pick_ring(inst, 3, cached, 6, &cat, out, 1);
    CHECK(n == 1 && !strcmp(out[0].slug, YK_APPS_MORE));
    CHECK(yk_apps_pick_ring(inst, 3, NULL, 0, NULL, out, 0) == 0);
    // Places: the 5-place ring, the apps from the top, 「更多」 upper left. Items that end with 「设置」 (given by hand: no
    // ring picks it now) still get the 6-place ring, the apps from the top, 「更多」 lower left, 「设置」 upper left.
    int slots[YK_APPS_MAX];
    n = yk_apps_pick_ring(inst, 3, cached, 6, &cat, out, YK_APPS_MAX);
    yk_apps_item_slots(out, n, slots);
    CHECK(slots[0] == 0 && slots[1] == 1 && slots[2] == 2 && slots[3] == 3 && slots[4] == YK_APPS_MORE_PLACE);
    const yk_apps_item_t with_settings[] = {{.slug = "a"}, {.slug = "b"}, {.slug = "c"}, {.slug = "d"},
                                            {.slug = YK_APPS_MORE}, {.slug = YK_APPS_SETTINGS}};
    yk_apps_item_slots(with_settings, 6, slots);
    CHECK(slots[0] == YK_APPS_SIX && slots[1] == YK_APPS_SIX + 1 && slots[2] == YK_APPS_SIX + 2 && slots[3] == YK_APPS_SIX + 3);
    CHECK(slots[4] == YK_APPS_MORE_PLACE6 && slots[5] == YK_APPS_SETTINGS_PLACE);
    yk_apps_item_slots(out, 2, slots);  // the first two alone: no 「更多」 among them
    CHECK(slots[0] == 1 && slots[1] == 4);
    const yk_apps_item_t two[] = {{.slug = "breathe", .installed = true}, {.slug = YK_APPS_MORE}};
    yk_apps_item_slots(two, 2, slots);
    CHECK(slots[0] == 0 && slots[1] == YK_APPS_MORE_PLACE && slots[2] == -1);
    // Laid out there: 「更多」 upper left; a tap on it reports its index.
    for (int k = 1; k <= 5; k++) {
        yk_apps_item_t items[YK_APPS_MAX];
        for (int i = 0; i < k - 1; i++) items[i] = (yk_apps_item_t){.slug = "breathe", .installed = true};
        items[k - 1] = (yk_apps_item_t){.slug = YK_APPS_MORE};
        yk_apps_item_slots(items, k, slots);
        yk_apps_layout_t L;
        yk_apps_layout_at(k, slots, &L);
        const yk_apps_sat_t *m = &L.sat[k - 1];
        CHECK(L.n == k && m->slot == YK_APPS_MORE_PLACE && m->cx < YK_CX - 100 && m->cy < YK_CY);
        CHECK(yk_apps_hit(&L, m->cx, m->cy) == k);
    }
    // The 6-place ring, 0 to 4 apps: 「设置」 upper left, 「更多」 lower left, on the same circle; a tap on each reports
    // its index.
    for (int apps = 0; apps <= 4; apps++) {
        yk_apps_item_t items[YK_APPS_MAX];
        const int k = apps + 2;
        for (int i = 0; i < apps; i++) items[i] = (yk_apps_item_t){.slug = "breathe", .installed = true};
        items[apps] = (yk_apps_item_t){.slug = YK_APPS_MORE};
        items[apps + 1] = (yk_apps_item_t){.slug = YK_APPS_SETTINGS};
        yk_apps_item_slots(items, k, slots);
        yk_apps_layout_t L;
        yk_apps_layout_at(k, slots, &L);
        const yk_apps_sat_t *m = &L.sat[k - 2], *g = &L.sat[k - 1];
        CHECK(L.n == k && m->slot == YK_APPS_MORE_PLACE6 && m->cx < YK_CX - 100 && m->cy > YK_CY);
        CHECK(g->slot == YK_APPS_SETTINGS_PLACE && g->cx < YK_CX - 100 && g->cy < YK_CY);
        for (int i = 0; i < k; i++) {
            const double dx = L.sat[i].cx - YK_CX, dy = L.sat[i].cy - YK_CY;
            CHECK(fabs(sqrt(dx * dx + dy * dy) - YK_RINGS_R) < 1.0);
            CHECK(yk_apps_hit(&L, L.sat[i].cx, L.sat[i].cy) == i + 1);
        }
        CHECK(apps < 1 || (L.sat[0].cx == YK_CX && L.sat[0].cy == YK_CY - YK_RINGS_R));  // the most recent at 12
    }
}

static void test_text(void) {
    char out[64];
    CHECK(yk_text_fit("面对面翻译", width16, NULL, 96, out, sizeof(out)) == 80 && !strcmp(out, "面对面翻译"));
    // Too long: cut on a character, "…" at the end, within the width.
    const int w = yk_text_fit("Language Buddy Plus", width16, NULL, 96, out, sizeof(out));
    CHECK(w <= 96 && w > 60);
    CHECK(strlen(out) > 3 && !strcmp(out + strlen(out) - 3, "\xE2\x80\xA6"));
    CHECK(!strncmp(out, "Language", 8));
    CHECK(yk_text_fit("一二三四五六七八", width16, NULL, 96, out, sizeof(out)) == 96 && !strcmp(out, "一二三四五\xE2\x80\xA6"));
    CHECK(yk_text_fit(NULL, width16, NULL, 96, out, sizeof(out)) == 0 && out[0] == 0);
    char c[5];
    yk_first_char("呼吸之间", c);
    CHECK(!strcmp(c, "呼"));
    yk_first_char("breathe", c);
    CHECK(!strcmp(c, "B"));
    yk_first_char("", c);
    CHECK(c[0] == 0);
    yk_first_char(NULL, c);
    CHECK(c[0] == 0);
}

// 「回到主屏幕说？」 (kit 1.10): no buddy's name in any language, the user's may be any of four (开发包四个伙伴需求 4).
static void test_ask_text(void) {
    CHECK(!strcmp(yk_apps_ask_text(false), "回到主屏幕说？"));
    CHECK(!strcmp(yk_apps_ask_text(true), "Talk on the home screen?"));
    static const char *const names[] = {"Pip", "Mote", "Dial", "Bloom", "Yay", "Wow", "Aha", "%"};
    YK_EACH_LANG(en) {
        const char *ask = yk_apps_ask_text(en);
        static const char *const want[YK_LOCALE_COUNT] = {
            [YK_LOCALE_EN] = "Talk on the home screen?", [YK_LOCALE_ZH_HANS] = "回到主屏幕说？",
            [YK_LOCALE_JA] = "ホーム画面に戻って話す？", [YK_LOCALE_ES_419] = "¿Ir al inicio a hablar?"};
        CHECK(!strcmp(ask, want[yk_words_locale()]));
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) CHECK(!strstr(ask, names[i]));
        // On the card: 290 px at 22, Han and kana a full em, the rest 9/16.
        int w = 0, len;
        for (const char *p = ask; *p; p += len) w += yk_utf8_next(p, &len) < 0x80 ? 22 * 9 / 16 : 22;
        CHECK(w <= 290);
    }
}

static void test_catalog(void) {
    yk_catalog_t cat = demo_catalog();
    cat.count = 5;
    static uint8_t a8[4096], argb[16384], argb2[16384];
    for (int i = 0; i < 4096; i++) a8[i] = (uint8_t)i;
    for (int i = 0; i < 16384; i++) argb[i] = (uint8_t)(i * 7), argb2[i] = (uint8_t)(i * 13);
    cat.app[0].icon_format = YK_ICON_ARGB8888;
    cat.app[1].icon_format = YK_ICON_A8;
    cat.app[1].icon_color = YK_SAGE;
    cat.app[2].icon_format = YK_ICON_NONE;
    cat.app[3].icon_format = YK_ICON_ARGB8888;
    cat.app[4].icon_format = YK_ICON_A8;
    const uint8_t *icons[YK_CATALOG_MAX] = {argb, a8, NULL, argb2, a8};
    static uint8_t img[YK_CATALOG_REGION];
    const size_t used = yk_catalog_encode(&cat, icons, img, sizeof(img));
    CHECK(used == 4096 + 5 * 128 + 2 * 16384 + 2 * 4096);
    // Read back like the device does: the header, then the table.
    yk_catalog_t back;
    uint32_t len, crc;
    CHECK(yk_catalog_decode_header(img, &back, &len, &crc));
    CHECK(back.count == 5 && len == 5 * 128 && back.seq == 7 && back.written_ms == 1792549233000LL && !strcmp(back.lang, "zh"));
    CHECK(yk_catalog_decode_table(img + YK_CATALOG_TABLE_AT, len, crc, &back));
    for (int i = 0; i < 5; i++) {
        CHECK(!strcmp(back.app[i].slug, cat.app[i].slug) && !strcmp(back.app[i].name, cat.app[i].name));
        CHECK(!strcmp(back.app[i].version, "1.0.0") && back.app[i].size == cat.app[i].size);
        CHECK(back.app[i].icon_format == cat.app[i].icon_format);
        const size_t bytes = yk_catalog_icon_bytes(back.app[i].icon_format);
        if (!bytes) continue;
        CHECK(back.app[i].icon_offset % 4 == 0 && back.app[i].icon_offset >= YK_CATALOG_TABLE_AT + len);
        CHECK(!memcmp(img + back.app[i].icon_offset, icons[i], bytes));
        CHECK(yk_catalog_crc32(0, img + back.app[i].icon_offset, bytes) == back.app[i].icon_crc);
    }
    CHECK(back.app[1].icon_color == YK_SAGE);
    // The channel (10-24) in byte 116 of the entry: stable is 0, as every older catalog reads.
    for (int i = 0; i < 5; i++) CHECK(back.app[i].channel == YK_CHANNEL_STABLE && !img[YK_CATALOG_TABLE_AT + i * 128 + 116]);
    {
        yk_catalog_t pv = cat;
        pv.app[2].channel = YK_CHANNEL_PREVIEW;
        pv.app[3].channel = 7;  // a value this writer does not know: stable
        static uint8_t img3[YK_CATALOG_REGION];
        CHECK(yk_catalog_encode(&pv, icons, img3, sizeof(img3)) == used);
        CHECK(img3[YK_CATALOG_TABLE_AT + 2 * 128 + 116] == 1 && img3[YK_CATALOG_TABLE_AT + 3 * 128 + 116] == 0);
        yk_catalog_t pb;
        CHECK(yk_catalog_decode_header(img3, &pb, &len, &crc) && yk_catalog_decode_table(img3 + YK_CATALOG_TABLE_AT, len, crc, &pb));
        CHECK(pb.app[2].channel == YK_CHANNEL_PREVIEW && pb.app[1].channel == YK_CHANNEL_STABLE && !strcmp(pb.app[2].slug, cat.app[2].slug));
        // Everything else in the bytes as before: an older reader (it reads no channel) gets the same entries.
        CHECK(!memcmp(img3 + YK_CATALOG_TABLE_AT + 2 * 128, img + YK_CATALOG_TABLE_AT + 2 * 128, 116));
    }
    // The icon's sha256 (Yay 0.1.5) in bytes 117..124: only with an icon; zero in older catalogs (fetched again)
    for (int i = 0; i < 5; i++) {
        CHECK(!back.app[i].icon_sha[0] && !back.app[i].icon_sha[7]);
        for (int b = 117; b < 128; b++) CHECK(!img[YK_CATALOG_TABLE_AT + i * 128 + b]);
    }
    {
        yk_catalog_t sh = cat;
        static const uint8_t sha[YK_CATALOG_ICON_SHA] = {0x6a, 0x87, 0x63, 0x64, 0xcb, 0x36, 0xb4, 0x72};
        memcpy(sh.app[1].icon_sha, sha, sizeof(sha));  // an A8
        memcpy(sh.app[2].icon_sha, sha, sizeof(sha));  // no icon: not written
        static uint8_t img4[YK_CATALOG_REGION];
        CHECK(yk_catalog_encode(&sh, icons, img4, sizeof(img4)) == used);
        CHECK(!memcmp(img4 + YK_CATALOG_TABLE_AT + 128 + 117, sha, sizeof(sha)));
        CHECK(!img4[YK_CATALOG_TABLE_AT + 2 * 128 + 117] && !img4[YK_CATALOG_TABLE_AT + 128 + 125]);
        yk_catalog_t sb;
        CHECK(yk_catalog_decode_header(img4, &sb, &len, &crc) && yk_catalog_decode_table(img4 + YK_CATALOG_TABLE_AT, len, crc, &sb));
        CHECK(!memcmp(sb.app[1].icon_sha, sha, sizeof(sha)) && !sb.app[2].icon_sha[0]);
        // All else as before: an older reader gets the same entry and icon
        CHECK(!memcmp(img4 + YK_CATALOG_TABLE_AT + 128, img + YK_CATALOG_TABLE_AT + 128, 117));
        CHECK(sb.app[1].icon_crc == back.app[1].icon_crc && sb.app[1].icon_offset == back.app[1].icon_offset);
    }
    // The same content encodes to the same bytes (the writer compares before erasing).
    static uint8_t img2[YK_CATALOG_REGION];
    CHECK(yk_catalog_encode(&cat, icons, img2, sizeof(img2)) == used && !memcmp(img, img2, used));
    // A changed table or header is not read; an erased region (0xFF) neither.
    img[YK_CATALOG_TABLE_AT + 40] ^= 1;
    CHECK(yk_catalog_decode_header(img, &back, &len, &crc) && !yk_catalog_decode_table(img + YK_CATALOG_TABLE_AT, len, crc, &back));
    img[YK_CATALOG_TABLE_AT + 40] ^= 1;
    img[9] ^= 1;
    CHECK(!yk_catalog_decode_header(img, &back, &len, &crc));
    memset(img2, 0xFF, 64);
    CHECK(!yk_catalog_decode_header(img2, &back, &len, &crc));
    // What cannot be written: a name too long for its 48 bytes, an icon missing, too many apps.
    yk_catalog_t bad = cat;
    memset(bad.app[2].name, 'x', sizeof(bad.app[2].name));  // no room for its NUL
    CHECK(yk_catalog_encode(&bad, icons, img2, sizeof(img2)) == 0);
    bad = cat;
    bad.app[2].slug[0] = 0;
    CHECK(yk_catalog_encode(&bad, icons, img2, sizeof(img2)) == 0);
    bad = cat;
    const uint8_t *missing[YK_CATALOG_MAX] = {NULL};
    CHECK(yk_catalog_encode(&bad, missing, img2, sizeof(img2)) == 0);
    bad.count = YK_CATALOG_MAX + 1;
    CHECK(yk_catalog_encode(&bad, icons, img2, sizeof(img2)) == 0);
    // The most there can be: 8 apps with ARGB icons fit the region.
    yk_catalog_t full = demo_catalog();
    full.count = YK_CATALOG_MAX;
    const uint8_t *all[YK_CATALOG_MAX];
    for (int i = 0; i < YK_CATALOG_MAX; i++) {
        snprintf(full.app[i].slug, sizeof(full.app[i].slug), "app-%d", i);
        full.app[i].icon_format = YK_ICON_ARGB8888;
        all[i] = argb;
    }
    const size_t most = yk_catalog_encode(&full, all, img2, sizeof(img2));
    CHECK(most > 0 && most <= YK_CATALOG_REGION);
    // An icon format from a later writer: read as none (the first character), the rest still read.
    CHECK(yk_catalog_encode(&cat, icons, img, sizeof(img)) == used);
    img[YK_CATALOG_TABLE_AT + 2 * 128 + 100] = 9;  // the third app's format
    const uint32_t c2 = yk_catalog_crc32(0, img + YK_CATALOG_TABLE_AT, len);
    img[16] = (uint8_t)c2, img[17] = (uint8_t)(c2 >> 8), img[18] = (uint8_t)(c2 >> 16), img[19] = (uint8_t)(c2 >> 24);
    const uint32_t hc = yk_catalog_crc32(0, img, 60);
    img[60] = (uint8_t)hc, img[61] = (uint8_t)(hc >> 8), img[62] = (uint8_t)(hc >> 16), img[63] = (uint8_t)(hc >> 24);
    CHECK(yk_catalog_decode_header(img, &back, &len, &crc) && yk_catalog_decode_table(img + YK_CATALOG_TABLE_AT, len, crc, &back));
    CHECK(back.app[2].icon_format == YK_ICON_NONE && back.app[3].icon_format == YK_ICON_ARGB8888);
    CHECK(yk_catalog_crc32(0, "123456789", 9) == 0xCBF43926u);  // the standard check value
}

static uint16_t to565(uint32_t c) { return (uint16_t)((((c >> 16) & 0xF8) << 8) | (((c >> 8) & 0xFC) << 3) | ((c & 0xFF) >> 3)); }

static void test_icon(void) {
    static uint8_t a8[4096], argb[16384], px[YK_ICON_RGB565A8_BYTES];
    const uint8_t *alpha = px + 2 * 4096;
#define PX(i) ((uint16_t)(px[2 * (i)] | (px[2 * (i) + 1] << 8)))
    a8[0] = 255, a8[1] = 0, a8[2] = 128;
    yk_icon_rgb565a8(YK_ICON_A8, a8, YK_SAGE, 255, px);
    CHECK(PX(0) == to565(YK_SAGE) && alpha[0] == 255 && alpha[1] == 0 && alpha[2] == 128);
    // Not installed: 42 % of the icon's own alpha.
    yk_icon_rgb565a8(YK_ICON_A8, a8, YK_SAGE, YK_APPS_DIM_OPA, px);
    CHECK(PX(0) == to565(YK_SAGE) && alpha[0] == YK_APPS_DIM_OPA && alpha[1] == 0 && alpha[2] == 128 * YK_APPS_DIM_OPA / 255);
    // ARGB8888 is B, G, R, A.
    argb[0] = 0x5D, argb[1] = 0x79, argb[2] = 0xDF, argb[3] = 255;  // coral
    argb[4 + 3] = 0;
    yk_icon_rgb565a8(YK_ICON_ARGB8888, argb, 0, 255, px);
    CHECK(PX(0) == to565(YK_CORAL) && alpha[0] == 255 && alpha[1] == 0);
    yk_icon_rgb565a8(YK_ICON_NONE, argb, 0, 255, px);
    CHECK(alpha[0] == 0);
#undef PX
}

static void test_hint_geometry(void) {
    yk_hint_geo_t g;
    // The prototype: key at 3 o'clock, "按住这里说话" (6 x 18 px).
    yk_hint_geometry(90, 108, &g);
    CHECK(fabsf(g.ux - 1) < 1e-4f && fabsf(g.uy) < 1e-4f);
    CHECK(lroundf(g.tip_x) == 434 && lroundf(g.tip_y) == 233);
    CHECK(lroundf(g.a1_x) == 418 && lroundf(g.a1_y) == 219 && lroundf(g.a2_x) == 418 && lroundf(g.a2_y) == 247);
    CHECK(g.pill.x + g.pill.w == 404 && g.pill.h == YK_HINT_PILL_H && g.pill.w == 108 + 2 * YK_HINT_PILL_PAD);
    CHECK(g.pill.y + g.pill.h / 2 == 233);
    // Any key angle, any line up to 300 px: the pill in the content circle, on the key's side, short of the V
    // (which stays inside the rim band with its nudge).
    for (int deg = 0; deg < 360; deg += 5)
        for (int w = 0; w <= 300; w += 20) {
            yk_hint_geometry((float)deg, w, &g);
            bool ok = box_in(&g.pill, YK_R_CONTENT);
            const float reach = (g.pill.x + g.pill.w / 2.0f - YK_CX) * g.ux + (g.pill.y + g.pill.h / 2.0f - YK_CY) * g.uy +
                                fabsf(g.ux) * g.pill.w / 2.0f + fabsf(g.uy) * g.pill.h / 2.0f;
            ok = ok && reach <= YK_HINT_PILL_R + 1;
            const float back = YK_HINT_TIP_R - YK_HINT_V_DEPTH - YK_HINT_STROKE / 2.0f;
            ok = ok && reach < back;
            ok = ok && YK_HINT_TIP_R + YK_HINT_NUDGE + YK_HINT_STROKE / 2.0f < YK_R_BAND;
            if (w <= 160) ok = ok && reach > 120;  // a usual line sits next to the arrow, not in the middle
            if (!ok) printf("  hint deg=%d w=%d reach=%.1f pill=%d,%d %dx%d\n", deg, w, reach, g.pill.x, g.pill.y, g.pill.w, g.pill.h);
            CHECK(ok);
        }
}

/** 18 px text (YK_HINT_TEXT_SIZE), roughly: a Chinese character 18 px, a Latin letter 10. */
static int width18(uint32_t cp, void *ctx) {
    (void)ctx;
    return cp < 0x80 ? 10 : 18;
}

/** The key where it is on the tikpal-r1 (YK_KEY_ANGLE_DEG): half past one, 45 degrees clockwise from 12. Every line in
 *  both languages: the arrow points up and to the right, the pill and its text are in the 200 px content circle and
 *  on the screen, and the V (with its nudge and its stroke) is on the glass short of the rim band's ticks. */
static void test_hint_key_angle(void) {
    CHECK(YK_KEY_ANGLE_DEG == 45);
    for (int k = 0; k < YK_HINT_KIND_COUNT; k++)
        YK_EACH_LANG(en) {
            char line[96];
            const int w = yk_text_fit(yk_hint_text((yk_hint_kind_t)k, en), width18, NULL, 300, line, sizeof(line));
            yk_hint_geo_t g;
            yk_hint_geometry(YK_KEY_ANGLE_DEG, w, &g);
            bool ok = fabsf(g.ux - 0.7071068f) < 1e-4f && fabsf(g.uy + 0.7071068f) < 1e-4f;  // towards half past one
            ok = ok && g.tip_x > YK_CX && g.tip_y < YK_CY;
            ok = ok && box_in(&g.pill, YK_R_CONTENT);
            ok = ok && g.pill.x >= 0 && g.pill.y >= 0 && g.pill.x + g.pill.w <= 2 * YK_CX && g.pill.y + g.pill.h <= 2 * YK_CY;
            const float xs[3] = {g.a1_x, g.tip_x, g.a2_x}, ys[3] = {g.a1_y, g.tip_y, g.a2_y};
            for (int i = 0; i < 3; i++) {
                const float x = xs[i] + g.ux * YK_HINT_NUDGE, y = ys[i] + g.uy * YK_HINT_NUDGE;
                const float r = sqrtf((x - YK_CX) * (x - YK_CX) + (y - YK_CY) * (y - YK_CY)) + YK_HINT_STROKE / 2.0f;
                ok = ok && r < YK_R_BAND && x >= 0 && y >= 0;
            }
            // The pill short of the V's arms.
            const float reach = (g.pill.x + g.pill.w / 2.0f - YK_CX) * g.ux + (g.pill.y + g.pill.h / 2.0f - YK_CY) * g.uy +
                                fabsf(g.ux) * g.pill.w / 2.0f + fabsf(g.uy) * g.pill.h / 2.0f;
            ok = ok && reach < YK_HINT_TIP_R - YK_HINT_V_DEPTH - YK_HINT_STROKE / 2.0f;
            if (!ok)
                printf("  hint at the key kind=%d en=%d w=%d pill=%d,%d %dx%d tip=%.1f,%.1f\n", k, en, w, g.pill.x, g.pill.y,
                       g.pill.w, g.pill.h, g.tip_x, g.tip_y);
            CHECK(ok);
        }
}

static void test_hint_rules(void) {
    CHECK(YK_HINT_SHOW_MS == 4000);  // yk_hint_show() as it ever was; yk_hint_show_stay() has no time at all
    yk_hint_state_t s = {0};
    s.app_bit = yk_hint_app_bit("breathe");
    // Just added: TALK for the first three wakes, then no more.
    for (int i = 0; i < 3; i++) {
        CHECK(yk_hint_rule(&s, YK_HINT_TALK));
        CHECK(yk_hint_shown(&s, YK_HINT_TALK));  // saved
    }
    CHECK(!yk_hint_rule(&s, YK_HINT_TALK) && !yk_hint_shown(&s, YK_HINT_TALK) && s.talk_shown == YK_HINT_CAP);
    // ... or after three turns, whichever comes first.
    yk_hint_state_t t = {0};
    CHECK(yk_hint_apply(&t, YK_HINT_EV_TALKED) && yk_hint_apply(&t, YK_HINT_EV_TALKED) && yk_hint_rule(&t, YK_HINT_TALK));
    CHECK(yk_hint_apply(&t, YK_HINT_EV_TALKED) && !yk_hint_rule(&t, YK_HINT_TALK));
    CHECK(!yk_hint_apply(&t, YK_HINT_EV_TALKED) && t.talked == YK_HINT_CAP);  // capped: no more writes
    // TAP: once per wake, never saved.
    CHECK(yk_hint_rule(&s, YK_HINT_TAP));
    CHECK(!yk_hint_shown(&s, YK_HINT_TAP) && !yk_hint_rule(&s, YK_HINT_TAP));
    CHECK(!yk_hint_apply(&s, YK_HINT_EV_WAKE) && yk_hint_rule(&s, YK_HINT_TAP));
    // APPS: the first time in this extension, until the ring was opened once.
    CHECK(yk_hint_rule(&s, YK_HINT_APPS));
    CHECK(yk_hint_shown(&s, YK_HINT_APPS) && !yk_hint_rule(&s, YK_HINT_APPS));
    CHECK(!yk_hint_shown(&s, YK_HINT_APPS));
    s.app_bit = yk_hint_app_bit("party-play");
    CHECK(s.app_bit != yk_hint_app_bit("breathe") && yk_hint_rule(&s, YK_HINT_APPS));
    CHECK(yk_hint_apply(&s, YK_HINT_EV_RING_OPENED) && !yk_hint_rule(&s, YK_HINT_APPS));
    CHECK(!yk_hint_apply(&s, YK_HINT_EV_RING_OPENED));  // once is enough: no second write
    // Never in Yay itself; BACK always.
    yk_hint_state_t y = {0};
    y.app_bit = yk_hint_app_bit("yay");
    CHECK(y.app_bit == 0 && !yk_hint_rule(&y, YK_HINT_APPS) && !yk_hint_shown(&y, YK_HINT_APPS));
    CHECK(yk_hint_rule(&s, YK_HINT_BACK) && !yk_hint_shown(&s, YK_HINT_BACK));
    CHECK(!yk_hint_rule(&s, YK_HINT_KIND_COUNT));
    // The five extensions have bits of their own.
    const char *ext[] = {"breathe", "party-play", "language-buddy", "homework-buddy", "translator"};
    uint32_t all = 0;
    for (int i = 0; i < 5; i++) {
        const uint32_t b = yk_hint_app_bit(ext[i]);
        CHECK(b && !(all & b));
        all |= b;
    }
    CHECK(yk_hint_app_bit(NULL) == 0 && yk_hint_app_bit("") == 0);
    // The lines, in the content circle at 18 px: the longest is short enough for the pill.
    for (int k = 0; k < YK_HINT_KIND_COUNT; k++) {
        CHECK(yk_hint_text((yk_hint_kind_t)k, false)[0] && yk_hint_text((yk_hint_kind_t)k, true)[0]);
        char out[64];
        CHECK(yk_text_fit(yk_hint_text((yk_hint_kind_t)k, true), width16, NULL, 300, out, sizeof(out)) <= 300);
    }
}

// 0.1.12 (docs/delivery/2026-10-30-yay-0.1.12.md 5): yk_apps_item_t's `update` is added last, so that an extension
// built before keeps its layout; nobody but the main firmware sets it (the picks leave it false); the dot sits on each
// disc's upper right, inside the content circle on every ring, clear of the lower right's marks and of the names.
static void test_update_dot(void) {
    _Static_assert(offsetof(yk_apps_item_t, update) > offsetof(yk_apps_item_t, cached), "update is added last");
    _Static_assert(offsetof(yk_apps_item_t, update) >= offsetof(yk_apps_item_t, icon), "update is added last");
    _Static_assert(offsetof(yk_more_item_t, update) > offsetof(yk_more_item_t, icon_color), "update is added last");
    const yk_apps_item_t plain = {.slug = "breathe", .installed = true};  // as an extension's ring makes them
    CHECK(!plain.update);
    yk_catalog_t cat;
    memset(&cat, 0, sizeof(cat));
    const yk_apps_slot_t inst[] = {{"party-play", 100}, {"breathe", 200}};
    yk_apps_item_t out[YK_APPS_MAX];
    const int n = yk_apps_pick_ring(inst, 2, NULL, 0, &cat, out, YK_APPS_MAX);
    bool any = false;
    for (int i = 0; i < n; i++) any |= out[i].update;
    CHECK(n == 3 && !any);
    // Where it goes: on a YK_MARK_DISC_R disc's edge at 45 degrees, upper right; scaled with the disc (「我的应用」).
    int x, y;
    float r, edge;
    yk_apps_update_dot(233, 91, 1.0f, &x, &y, &r, &edge);
    CHECK(x == 233 + YK_APPS_UPDATE_D && y == 91 - YK_APPS_UPDATE_D && r == YK_APPS_UPDATE_R && edge > r);
    CHECK(fabs(hypot(x - 233, y - 91) - YK_MARK_DISC_R) < 2);
    yk_apps_update_dot(233, 233, 100.0f / (2 * YK_MARK_DISC_R), &x, &y, &r, &edge);  // 「我的应用」's middle one (100 px)
    CHECK(fabs(hypot(x - 233, y - 233) - 50) < 2 && r > YK_APPS_UPDATE_R);
    // On the rings: on their bigger disc's edge, as big as it was made; with the ring's motion it scales.
    float outer;
    yk_rings_mark(YK_MARK_UPDATE, 233, 83, 1.0f, &x, &y, &outer);
    CHECK(x > 233 && y < 83 && outer == YK_APPS_UPDATE_R + YK_APPS_UPDATE_EDGE);
    CHECK(fabs(hypot(x - 233, y - 83) - YK_RINGS_SAT_R) < 3);
    int x2, y2;
    yk_rings_mark(YK_MARK_UPDATE, 233, 83, 0.5f, &x2, &y2, &outer);
    CHECK(abs((x2 - 233) * 2 - (x - 233)) <= 1 && outer == (YK_APPS_UPDATE_R + YK_APPS_UPDATE_EDGE) * 0.5f);
    // Lower right, the cached and the download marks; apart from the dot.
    int cx2, cy2;
    yk_rings_mark(YK_MARK_DOWNLOAD, 233, 83, 1.0f, &cx2, &cy2, &outer);
    CHECK(cx2 > 233 && cy2 > 83 && hypot(cx2 - x, cy2 - y) > outer + YK_APPS_UPDATE_R + YK_APPS_UPDATE_EDGE);
    // The middle's (0.1.14: Tikpal itself has a newer version): the same dot on the middle disc's edge, upper right; at
    // full size it meets no app's disc on any ring.
    yk_apps_home_dot(YK_CX, YK_CY, 1.0f, &x, &y, &r, &edge);
    CHECK(r == YK_APPS_UPDATE_R && edge > r && x > YK_CX && y < YK_CY);
    CHECK(fabs(hypot(x - YK_CX, y - YK_CY) - YK_APPS_CENTER_R) < 2);
    for (int six = 0; six < 2; six++)
        for (int k = 1; k <= (six ? 1 : 5); k++) {
            yk_apps_layout_t L;
            if (six) {
                yk_apps_item_t items[6] = {{.slug = "a"}, {.slug = "b"}, {.slug = "c"}, {.slug = "d"},
                                           {.slug = YK_APPS_MORE}, {.slug = YK_APPS_SETTINGS}};
                int slots[YK_APPS_MAX];
                yk_apps_item_slots(items, 6, slots);
                yk_apps_layout_at(6, slots, &L);
            } else {
                yk_apps_layout(k, &L);
            }
            for (int i = 0; i < L.n; i++)
                CHECK(hypot(x - L.sat[i].cx, y - L.sat[i].cy) > edge + YK_RINGS_PRESS_R);  // pressed too
        }
}

/** Two taps in a row for the log (0.1.22: the double tap's window from real taps). */
static void test_tap_pair(void) {
    char out[160];
    const yk_ui_tap_t a = {.x = 200, .y = 200, .down_ms = 1000, .up_ms = 1080, .press = 7};
    yk_ui_tap_t b = {.x = 224, .y = 232, .down_ms = 1260, .up_ms = 1330, .press = 8};  // 40 px, 180 ms after
    CHECK(yk_ui_tap_pair_line(out, sizeof(out), "app", &a, &b, true, NULL, 450, 40) > 0);
    CHECK(!strcmp(out, "YAY_TAP_PAIR where=app gap_ms=180 dist_px=40 hold1_ms=80 hold2_ms=70 double=1 why=double"));
    yk_ui_tap_pair_line(out, sizeof(out), "home", &a, &b, false, NULL, 450, 40);
    CHECK(strstr(out, "where=home") && strstr(out, "double=0 why=third"));  // near, quick, next: a double's second first
    yk_ui_tap_pair_line(out, sizeof(out), "app", &a, &b, false, "system", 450, 40);
    CHECK(strstr(out, "why=system"));
    b.down_ms = 1080 + 451, b.up_ms = b.down_ms + 70;  // 451 ms: too slow
    yk_ui_tap_pair_line(out, sizeof(out), "app", &a, &b, false, NULL, 450, 40);
    CHECK(strstr(out, "gap_ms=451") && strstr(out, "why=slow"));
    b.down_ms = 1260, b.x = 241;  // 41 px across (and 32 down): far
    yk_ui_tap_pair_line(out, sizeof(out), "app", &a, &b, false, NULL, 450, 40);
    CHECK(strstr(out, "why=far"));
    b.x = 224, b.press = 9;  // a press between them
    yk_ui_tap_pair_line(out, sizeof(out), "app", &a, &b, false, NULL, 450, 40);
    CHECK(strstr(out, "why=between"));
    b.press = 8, b.down_ms = 1080 + YK_UI_TAP_PAIR_MS;  // a second or more apart: no pair, nothing written
    out[0] = 0;
    CHECK(yk_ui_tap_pair_line(out, sizeof(out), "app", &a, &b, false, NULL, 450, 40) == 0 && !out[0]);
    const yk_ui_tap_t w1 = {.x = 10, .y = 10, .down_ms = 0xFFFFFF00u, .up_ms = 0xFFFFFF50u, .press = 1};
    const yk_ui_tap_t w2 = {.x = 10, .y = 10, .down_ms = 0xFFFFFF50u + 200u, .up_ms = 0xFFFFFF50u + 260u, .press = 2};
    yk_ui_tap_pair_line(out, sizeof(out), "home", &w1, &w2, true, NULL, 450, 40);  // the clock wrapped between them
    CHECK(strstr(out, "gap_ms=200 dist_px=0 hold1_ms=80 hold2_ms=60"));
}

int main(void) {
    test_slots();
    test_layout();
    test_words();
    test_press();
    test_update_dot();
    test_motion();
    test_hit();
    test_tap();
    test_tap_strict();
    test_tap_pair();
    test_touch();
    test_touch_tap();
    test_phone_mark();
    test_pick();
    test_pick_ring();
    test_text();
    test_ask_text();
    test_catalog();
    test_icon();
    test_hint_geometry();
    test_hint_key_angle();
    test_hint_rules();
    printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "ok", checks, failures);
    return failures ? 1 : 0;
}
