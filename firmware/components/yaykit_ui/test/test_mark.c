// Host test of the 伴环 mark's arrival (yk_mark_fit.c, yk_mark_arrival) against the brand kit's own file: run.sh reads
// design/tikpal/2026-11-01-brand-kit-v1/03-motion/state-spec.json and gives each arrival keyframe on stdin as
// "<track> <t> <value>" (t of the 900 ms), after a line "ms <duration>".
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yk_mark_fit.h"

static int checks, failures;
#define CHECK(c)                                                       \
    do {                                                               \
        checks++;                                                      \
        if (!(c)) {                                                    \
            failures++;                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
        }                                                              \
    } while (0)

static float field(const yk_mark_pose_t *p, const char *track) {
    if (!strcmp(track, "ring_scale")) return p->ring_scale;
    if (!strcmp(track, "ring_opacity")) return p->ring_opa;
    if (!strcmp(track, "dot_scale")) return p->dot_scale;
    if (!strcmp(track, "dot_opacity")) return p->dot_opa;
    if (!strcmp(track, "dot_x")) return p->dot_x;
    if (!strcmp(track, "dot_y")) return p->dot_y;
    return NAN;
}

static bool rest(yk_mark_pose_t p) {
    const yk_mark_pose_t r = YK_MARK_REST;
    return !memcmp(&p, &r, sizeof(p));
}

/** Every keyframe of the file, at its own millisecond: the value it names. */
static void test_keyframes(void) {
    char track[32];
    float t, v;
    int ms = 0, keys = 0, tracks = 0;
    char last[32] = "";
    CHECK(scanf(" ms %d", &ms) == 1 && ms == YK_MARK_ARRIVAL_MS);
    while (scanf(" %31s %f %f", track, &t, &v) == 3) {
        const int at = (int)lroundf(t * YK_MARK_ARRIVAL_MS);
        CHECK(fabsf(t * YK_MARK_ARRIVAL_MS - at) < 1e-3f);  // on a whole millisecond
        const yk_mark_pose_t p = yk_mark_arrival(at);
        const float got = field(&p, track);
        if (!(fabsf(got - v) < 1e-4f)) printf("  %s at %d ms: %.5f, the file says %.5f\n", track, at, got, v);
        CHECK(fabsf(got - v) < 1e-4f);
        keys++;
        if (strcmp(track, last)) tracks++, snprintf(last, sizeof(last), "%s", track);
    }
    CHECK(tracks == 6 && keys == 17);
    printf("arrival: %d keyframes of %d tracks as state-spec.json has them\n", keys, tracks);
}

/** Between keyframes: the ease is LVGL's ease-in-out (half way at half time, flat at both ends), nothing overshoots,
 *  and each track only moves one way within a step. */
static void test_between(void) {
    // Half way through the ring's opacity (0..252 ms): exactly half.
    CHECK(fabsf(yk_mark_arrival(126).ring_opa - 0.5f) < 1e-3f);
    // Eased: a tenth of the way in time is well under a tenth of the change (cubic-bezier(0.42, 0, 0.58, 1) gives
    // 0.0197 at 0.1).
    const float p10 = yk_mark_arrival(25).ring_opa;  // 25.2 ms would be 0.1
    CHECK(p10 > 0.01f && p10 < 0.03f);
    // Held steps: the dot stays small, unseen and off to the top right until 162 / 198 ms.
    CHECK(yk_mark_arrival(150).dot_opa == 0 && yk_mark_arrival(190).dot_scale == 0.62f);
    CHECK(yk_mark_arrival(190).dot_x == 12 && yk_mark_arrival(190).dot_y == -10);
    // The dot grows past its size (1.07 at 522 ms) and settles back; nothing else overshoots.
    float prev_ring = 0, prev_opa = 0, peak = 0;
    for (int ms = 0; ms <= YK_MARK_ARRIVAL_MS; ms++) {
        const yk_mark_pose_t p = yk_mark_arrival(ms);
        CHECK(p.ring_scale >= 0.94f - 1e-6f && p.ring_scale <= 1 + 1e-6f && p.ring_scale >= prev_ring - 1e-6f);
        CHECK(p.ring_opa >= prev_opa - 1e-6f && p.ring_opa <= 1 + 1e-6f && p.dot_opa >= 0 && p.dot_opa <= 1 + 1e-6f);
        CHECK(p.dot_scale >= 0.62f - 1e-6f && p.dot_scale <= 1.07f + 1e-6f);
        CHECK(p.dot_x >= -1e-6f && p.dot_x <= 12 + 1e-6f && p.dot_y <= 1e-6f && p.dot_y >= -10 - 1e-6f);
        if (ms <= 522) CHECK(p.dot_scale >= peak - 1e-6f);
        else CHECK(p.dot_scale <= peak + 1e-6f);
        if (p.dot_scale > peak) peak = p.dot_scale;
        prev_ring = p.ring_scale, prev_opa = p.ring_opa;
    }
    CHECK(fabsf(peak - 1.07f) < 1e-4f);
}

/** Its ends: nothing to see before it starts, the mark at rest from 900 ms on, however late a frame comes (a frame
 *  cut short ends on the rest pose, never part way). */
static void test_ends(void) {
    const yk_mark_pose_t a = yk_mark_arrival(-40), b = yk_mark_arrival(0);
    CHECK(!memcmp(&a, &b, sizeof(a)) && b.ring_opa == 0 && b.dot_opa == 0);
    CHECK(rest(yk_mark_arrival(YK_MARK_ARRIVAL_MS)) && rest(yk_mark_arrival(YK_MARK_ARRIVAL_MS + 1)));
    CHECK(rest(yk_mark_arrival(5000)) && rest(yk_mark_arrival(0x7fffffff)));
    // The last millisecond before it is all but there: under a pixel of a 264 mark from rest.
    const yk_mark_pose_t c = yk_mark_arrival(YK_MARK_ARRIVAL_MS - 1);
    CHECK(fabsf(c.dot_scale - 1) * 61 * 264 / 388 < 0.5f && c.ring_scale == 1 && c.ring_opa == 1 && c.dot_x == 0);
}

/** Inside the true ring (the master's shape at `outer` px), at (x, y) from its centre. */
static bool in_ring(float outer, float x, float y) {
    const float half = outer * YK_MARK_STROKE / (2.0f * YK_MARK_UNITS), mid = outer / 2 - half;
    const float rad = 3.14159265f / 180;
    float deg = atan2f(y, x) / rad;
    if (deg < 0) deg += 360;
    if (deg >= YK_MARK_ARC_START && deg <= YK_MARK_ARC_END && fabsf(hypotf(x, y) - mid) <= half) return true;
    const float e1 = YK_MARK_ARC_START * rad, e2 = YK_MARK_ARC_END * rad;
    return hypotf(x - mid * cosf(e1), y - mid * sinf(e1)) <= half || hypotf(x - mid * cosf(e2), y - mid * sinf(e2)) <= half;
}

/** yk_mark_ring_cover against 8 x 8 samples a pixel of the true shape, at the brand's size and a page's: the area
 *  within half a percent, every pixel within one edge's antialiasing, nothing in the opening where the dot goes. */
static void test_cover(void) {
    const float sizes[] = {200, 188, 64, 88};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        const float s = sizes[i];
        yk_mark_ring_t r;
        yk_mark_ring_init(&r, s);
        double got = 0, want = 0, err = 0;
        int worst = 0;
        const int n = (int)s + 4;
        for (int py = 0; py < n; py++)
            for (int px = 0; px < n; px++) {
                const float x = px + 0.5f - n / 2.0f, y = py + 0.5f - n / 2.0f;
                const int c = (int)yk_mark_ring_cover(&r, x, y);
                int in = 0;
                for (int j = 0; j < 8; j++)
                    for (int k = 0; k < 8; k++) in += in_ring(s, px + (k + 0.5f) / 8 - n / 2.0f, py + (j + 0.5f) / 8 - n / 2.0f);
                const int ref = in * 255 / 64, d = abs(c - ref);
                got += c, want += ref, err += d;
                if (d > worst) worst = d;
            }
        const double area = got / want;
        if (!(fabs(area - 1) < 0.005 && worst <= 160)) printf("  %.0f px: area %.4f, worst %d\n", s, area, worst);
        CHECK(fabs(area - 1) < 0.005);
        CHECK(worst <= 160);                     // one pixel at an edge, never a hole or a speck
        CHECK(err / (double)(n * n) < 3.0);      // on average a fraction of a level
        // Solid in the middle of the stroke at 6 o'clock, nothing at the dot's place in the opening or the centre.
        CHECK(yk_mark_ring_cover(&r, 0.5f, r.mid) == 255);
        const float k = s / YK_MARK_UNITS;
        CHECK(yk_mark_ring_cover(&r, YK_MARK_DOT_X * k, YK_MARK_DOT_Y * k) == 0 && yk_mark_ring_cover(&r, 0.5f, 0.5f) == 0);
    }
    // The disc: solid inside, nothing past its edge and a pixel of antialiasing, its area within half a percent.
    double a = 0;
    for (int y = -40; y < 40; y++)
        for (int x = -40; x < 40; x++) a += yk_mark_disc_cover(30.5f, x + 0.5f, y + 0.5f) / 255.0;
    CHECK(fabs(a / (3.14159265 * 30.5 * 30.5) - 1) < 0.005);
    CHECK(yk_mark_disc_cover(30.5f, 0, 0) == 255 && yk_mark_disc_cover(30.5f, 31.1f, 0) == 0);
    printf("ring and dot coverage: within an edge pixel of the true shape at 200, 188, 88 and 64 px\n");
}

int main(void) {
    test_cover();
    test_keyframes();
    test_between();
    test_ends();
    printf("mark arrival host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
