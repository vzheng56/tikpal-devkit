// Host tests for yaykit_voicecode. Run: bash yaykit/firmware/components/yaykit_voicecode/test/run.sh
// (the contract's own examples are test_fixtures.c).
//  1. Bits: the README example, round trips of random payloads of every kind, payloads that do not fit.
//  2. Reed-Solomon: wrong and erased bytes within 2 x errors + erasures <= 6 always decode; beyond it the decoder
//     must refuse or (rarely, inherent to RS) decode to another payload: counted and reported.
//  3. Camera frames: a renderer draws codes by the README's geometry (anti-aliased bars with round caps, the ring,
//     the coral dot, the logo disc with its dark ring) into 1280x720 luma, with tilt, perspective, blur, noise,
//     exposure, glare, a dark sector and clutter; yk_vc_scan must read them, and say YK_VC_LATER for a code of a later
//     version. Negative frames (the bars' animation, clutter alone, half a code) must never read as a code. Frames
//     are rendered and read on all cores (yk_vc_scan is thread-safe); the time per frame is measured on one thread.
//  4. How far a frame got (yk_vc_status_stage): a frame that finds the centre but not the levels says bad_levels, one
//     that reads the levels but has no dot says no_dot, and a frame with both says the furthest, no_dot (the status
//     numbers have no_dot first). Runs with --quick too.
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "yk_voicecode.h"
#include "yk_voicecode_priv.h"

#define PI 3.14159265358979323846
#define W 1280
#define H 720

static int failures, checks;
static const char *current = "";
#define CHECK(cond)                                                              \
    do {                                                                         \
        checks++;                                                                \
        if (!(cond)) {                                                           \
            failures++;                                                          \
            printf("FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, current, #cond); \
        }                                                                        \
    } while (0)

// ---- random numbers (per frame, so results do not depend on threads) ----

static uint64_t splitmix(uint64_t *s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static double uni(uint64_t *s) { return (double)(splitmix(s) >> 11) * (1.0 / 9007199254740992.0); }
static double range(uint64_t *s, double a, double b) { return a + (b - a) * uni(s); }
static int irange(uint64_t *s, int a, int b) { return a + (int)(splitmix(s) % (uint64_t)(b - a + 1)); }

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static int same_payload(const yk_vc_payload_t *a, const yk_vc_payload_t *b) {
    return a->kind == b->kind && a->value == b->value && a->suffix == b->suffix && a->digits == b->digits &&
           a->app == b->app && a->action == b->action;
}

#define KINDS 5
static const yk_vc_kind_t kinds[KINDS] = {YK_VC_TEST, YK_VC_PAIR, YK_VC_LOOKUP, YK_VC_APP, YK_VC_SELFIE};

static yk_vc_payload_t random_payload(uint64_t *s) {
    yk_vc_payload_t p;
    memset(&p, 0, sizeof p);
    p.kind = kinds[splitmix(s) % KINDS];
    switch (p.kind) {
        case YK_VC_TEST:
        case YK_VC_LOOKUP:
            p.value = splitmix(s) & ((1ULL << 44) - 1);
            break;
        case YK_VC_PAIR:
        case YK_VC_SELFIE:
            p.suffix = (uint16_t)splitmix(s);
            p.digits = (uint32_t)(splitmix(s) % 100000000u);
            break;
        default:
            p.app = 1 + (uint32_t)(splitmix(s) % 0xffff);
            p.action = (uint32_t)(splitmix(s) & 15);
            p.value = splitmix(s) & 0xffffff;
            break;
    }
    return p;
}

// ---- 1. bits ----

static void test_bits(void) {
    current = "bits";
    const yk_vc_payload_t ex = {.kind = YK_VC_TEST, .value = 0x05A3C91E2B4ULL};
    uint8_t lv[YK_VC_BARS];
    CHECK(yk_vc_encode(&ex, lv) == 0);
    const char *want = "112321200023003320120131132123011102220031033313";
    int same = 1;
    for (int i = 0; i < YK_VC_BARS; i++) same &= lv[i] == want[i] - '0';
    CHECK(same);
    char text[48];
    yk_vc_payload_text(&ex, text, sizeof text);
    CHECK(strcmp(text, "test 0x05A3C91E2B4") == 0);

    // the 8 digits stay out of the text for logs
    yk_vc_payload_t pair, selfie;
    CHECK(yk_vc_pair("dd76", "06418273", &pair) == 0 && yk_vc_selfie("7770", "48213095", &selfie) == 0);
    yk_vc_payload_text(&pair, text, sizeof text);
    CHECK(strcmp(text, "pair YK-dd76") == 0);
    yk_vc_payload_text(&selfie, text, sizeof text);
    CHECK(strcmp(text, "selfie Tikpal-Selfie-7770") == 0);
    const yk_vc_payload_t app = {.kind = YK_VC_APP, .app = 300, .action = YK_VC_ACTION_DAY, .value = 20756};
    yk_vc_payload_text(&app, text, sizeof text);
    CHECK(strcmp(text, "app 300 action 2 value 20756") == 0);
    yk_vc_payload_text(&app, text, 4);
    CHECK(strcmp(text, "app") == 0);

    // text the constructors refuse (beyond the contract's own examples)
    yk_vc_payload_t q;
    CHECK(yk_vc_pair(NULL, "06418273", &q) != 0 && yk_vc_pair("dd76", NULL, &q) != 0);
    CHECK(yk_vc_pair("dd7", "06418273", &q) != 0 && yk_vc_pair("dd76", "0641827", &q) != 0);
    CHECK(yk_vc_pair("dg76", "06418273", &q) != 0 && yk_vc_pair("dd76", "0641827a", &q) != 0);
    CHECK(yk_vc_pair("dd76", " 6418273", &q) != 0 && yk_vc_selfie("dd76", "-6418273", &q) != 0);

    yk_vc_payload_t bad = {.kind = YK_VC_TEST, .value = 1ULL << 44};
    CHECK(yk_vc_encode(&bad, lv) != 0);
    bad.kind = YK_VC_LOOKUP;
    CHECK(yk_vc_encode(&bad, lv) != 0);
    bad.kind = YK_VC_PAIR;
    bad.digits = 100000000u;
    CHECK(yk_vc_encode(&bad, lv) != 0);
    bad.kind = YK_VC_SELFIE;
    CHECK(yk_vc_encode(&bad, lv) != 0);
    bad = (yk_vc_payload_t){.kind = YK_VC_APP, .app = 1, .action = 0, .value = 0};
    CHECK(yk_vc_encode(&bad, lv) == 0);
    bad.value = 1u << 24;
    CHECK(yk_vc_encode(&bad, lv) != 0);
    bad.value = 0;
    bad.action = 16;
    CHECK(yk_vc_encode(&bad, lv) != 0);
    bad.action = 0;
    bad.app = 0x10000;
    CHECK(yk_vc_encode(&bad, lv) != 0);
    bad.kind = (yk_vc_kind_t)5;
    CHECK(yk_vc_encode(&bad, lv) != 0);
    CHECK(yk_vc_encode(NULL, lv) != 0);
    CHECK(strcmp(yk_vc_status_name(YK_VC_OK), "ok") == 0);
    CHECK(strcmp(yk_vc_status_name(YK_VC_NOT_DECODED), "not_decoded") == 0);
    CHECK(strcmp(yk_vc_status_name(YK_VC_LATER), "later") == 0);

    uint64_t s = 1;
    int ok = 0, total = 0;
    for (int t = 0; t < KINDS; t++) {
        for (int i = 0; i < 10000; i++) {
            yk_vc_payload_t p;
            do p = random_payload(&s);
            while (p.kind != kinds[t]);
            int8_t l8[YK_VC_BARS];
            int corrected = -1;
            yk_vc_payload_t q;
            total++;
            if (yk_vc_encode(&p, lv) != 0) continue;
            for (int k = 0; k < YK_VC_BARS; k++) l8[k] = (int8_t)lv[k];
            if (yk_vc_decode_levels(l8, &q, &corrected) == 0 && same_payload(&p, &q) && corrected == 0) ok++;
        }
    }
    CHECK(ok == total);
    printf("round trips: %d/%d (10000 each of test, pair, lookup, app, selfie)\n", ok, total);
}

// ---- 2. Reed-Solomon ----

/** Damages `errors` bytes (one to four bars wrong) and erases `erasures` others (one to four bars at -1). */
static void damage(uint64_t *s, int8_t l8[YK_VC_BARS], int errors, int erasures) {
    int used[YK_VC_BYTES] = {0};
    for (int e = 0; e < errors + erasures; e++) {
        int j;
        do j = irange(s, 0, YK_VC_BYTES - 1);
        while (used[j]);
        used[j] = 1;
        const int nbars = irange(s, 1, 4);
        int changed = 0;
        for (int t = 0; t < 4; t++) {
            if (t >= nbars && changed) break;
            const int k = 4 * j + (t + j) % 4;
            if (e < errors) l8[k] = (int8_t)((l8[k] + irange(s, 1, 3)) % 4);
            else l8[k] = -1;
            changed = 1;
        }
    }
}

static void test_rs(void) {
    current = "rs";
    uint64_t s = 2;
    int within = 0, within_ok = 0, corrected_right = 1;
    for (int i = 0; i < 60000; i++) {
        const yk_vc_payload_t p = random_payload(&s);
        uint8_t lv[YK_VC_BARS];
        yk_vc_encode(&p, lv);
        int8_t l8[YK_VC_BARS];
        for (int k = 0; k < YK_VC_BARS; k++) l8[k] = (int8_t)lv[k];
        int errors, erasures;
        switch (i % 3) {
            case 0:  // 1..3 wrong bytes
                errors = irange(&s, 1, 3);
                erasures = 0;
                break;
            case 1:  // 1..6 erased bytes
                errors = 0;
                erasures = irange(&s, 1, 6);
                break;
            default:  // a mix at the limit
                errors = irange(&s, 0, 3);
                erasures = irange(&s, 0, 6 - 2 * errors);
                break;
        }
        damage(&s, l8, errors, erasures);
        yk_vc_payload_t q;
        int corrected = -1;
        within++;
        if (yk_vc_decode_levels(l8, &q, &corrected) == 0 && same_payload(&p, &q)) {
            within_ok++;
            if (corrected != errors + erasures) corrected_right = 0;
        }
    }
    CHECK(within_ok == within);
    CHECK(corrected_right);
    printf("within 2e+f<=6: %d/%d decoded right, `corrected` = wrong + erased bytes: %s\n", within_ok, within,
           corrected_right ? "yes" : "NO");

    // beyond: (errors, erasures) with 2e + f > 6
    static const int beyond[][2] = {{4, 0}, {5, 0}, {6, 0}, {3, 1}, {2, 3}, {1, 5}, {0, 7}, {3, 2}};
    printf("beyond the decoder's reach (100000 each): refused / decoded to another payload\n");
    for (size_t b = 0; b < sizeof beyond / sizeof beyond[0]; b++) {
        int refused = 0, wrong = 0, right = 0;
        for (int i = 0; i < 100000; i++) {
            const yk_vc_payload_t p = random_payload(&s);
            uint8_t lv[YK_VC_BARS];
            yk_vc_encode(&p, lv);
            int8_t l8[YK_VC_BARS];
            for (int k = 0; k < YK_VC_BARS; k++) l8[k] = (int8_t)lv[k];
            damage(&s, l8, beyond[b][0], beyond[b][1]);
            yk_vc_payload_t q;
            if (yk_vc_decode_levels(l8, &q, NULL) != 0) refused++;
            else if (same_payload(&p, &q)) right++;
            else wrong++;
        }
        printf("  %d wrong + %d erased: %6d refused, %3d other payload (%.4f%%)%s\n", beyond[b][0], beyond[b][1],
               refused, wrong, wrong / 1000.0, right ? " (some decoded right: damage hit nothing)" : "");
        if (beyond[b][1] > 6) CHECK(refused == 100000);
    }
    // random bars: how often noise decodes at all
    int any = 0;
    for (int i = 0; i < 1000000; i++) {
        int8_t l8[YK_VC_BARS];
        for (int k = 0; k < YK_VC_BARS; k++) l8[k] = (int8_t)(splitmix(&s) & 3);
        yk_vc_payload_t q;
        if (yk_vc_decode_levels(l8, &q, NULL) == 0) any++;
    }
    printf("random bars (1000000): %d decode with the decoder's full reach (%.2e); yk_vc_scan asks for more\n", any,
           any / 1e6);
}

// ---- 3. the renderer ----

enum { KIND_CODE, KIND_LATER, KIND_ANIM_LEVELS, KIND_ANIM_FREE, KIND_CLUTTER, KIND_HALF_EDGE, KIND_HALF_BARS };

typedef struct {
    int kind;
    double radius, cx, cy, rot, tilt, tilt_axis, persp;
    double blur, noise, gain, offset;
    double motion, motion_at;  // hand shake: a straight streak this many pixels long
    double bg, bg_gx, bg_gy, bezel;
    double glare_frac, glare_amp, glare_at;
    double occl, occl_at;
    int clutter;
    int no_dot;             // the coral dot left out
    float len[YK_VC_BARS];  // cap centre to cap centre; 0: no bar
    int coral[YK_VC_BARS];
    int pad;  // stride - width
    double h[9], hinv[9];
} scene_t;

static void mat_mul(const double a[9], const double b[9], double o[9]) {
    double t[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) t[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
    memcpy(o, t, sizeof t);
}

static void mat_inv(const double m[9], double o[9]) {
    const double c0 = m[4] * m[8] - m[5] * m[7], c1 = m[5] * m[6] - m[3] * m[8], c2 = m[3] * m[7] - m[4] * m[6];
    const double id = 1.0 / (m[0] * c0 + m[1] * c1 + m[2] * c2);
    o[0] = c0 * id;
    o[1] = (m[2] * m[7] - m[1] * m[8]) * id;
    o[2] = (m[1] * m[5] - m[2] * m[4]) * id;
    o[3] = c1 * id;
    o[4] = (m[0] * m[8] - m[2] * m[6]) * id;
    o[5] = (m[2] * m[3] - m[0] * m[5]) * id;
    o[6] = c2 * id;
    o[7] = (m[1] * m[6] - m[0] * m[7]) * id;
    o[8] = (m[0] * m[4] - m[1] * m[3]) * id;
}

static void rot_m(double a, double o[9]) {
    const double r[9] = {cos(a), -sin(a), 0, sin(a), cos(a), 0, 0, 0, 1};
    memcpy(o, r, sizeof r);
}

/** model (ring units) -> pixel: T(c) S(radius) R(axis) D(1, cos tilt) R(-axis) R(rot) P, where P is a small
 *  perspective along the foreshortened direction (points on one side of the tilt axis come nearer). */
static void scene_geometry(scene_t *s) {
    double m[9] = {1, 0, s->cx, 0, 1, s->cy, 0, 0, 1}, t[9];
    const double sc[9] = {s->radius, 0, 0, 0, s->radius, 0, 0, 0, 1};
    mat_mul(m, sc, m);
    rot_m(s->tilt_axis, t);
    mat_mul(m, t, m);
    const double d[9] = {1, 0, 0, 0, cos(s->tilt), 0, 0, 0, 1};
    mat_mul(m, d, m);
    rot_m(-s->tilt_axis, t);
    mat_mul(m, t, m);
    rot_m(s->rot, t);
    mat_mul(m, t, m);
    const double qa = s->tilt_axis - s->rot;  // the foreshortened direction, in model coordinates
    const double p[9] = {1, 0, 0, 0, 1, 0, -sin(qa) * s->persp, cos(qa) * s->persp, 1};
    mat_mul(m, p, s->h);
    mat_inv(s->h, s->hinv);
}

static void to_pixel(const scene_t *s, double X, double Y, double *x, double *y) {
    const double w = s->h[6] * X + s->h[7] * Y + s->h[8];
    *x = (s->h[0] * X + s->h[1] * Y + s->h[2]) / w;
    *y = (s->h[3] * X + s->h[4] * Y + s->h[5]) / w;
}

static inline float cover(double sdf, double px) {
    const double c = 0.5 - sdf / px;
    return c <= 0 ? 0.0f : c >= 1 ? 1.0f : (float)c;
}
static inline float mixf(float a, float b, float t) { return a + (b - a) * t; }

#define WHITE 0.95f
#define CORAL (0.55f * WHITE)
#define SCREEN 0.015f

/** Distance from (x, y) to the segment a..b. */
static double seg_dist(double x, double y, double ax, double ay, double bx, double by) {
    const double vx = bx - ax, vy = by - ay;
    const double l2 = vx * vx + vy * vy;
    double t = l2 > 0 ? ((x - ax) * vx + (y - ay) * vy) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    const double dx = x - ax - t * vx, dy = y - ay - t * vy;
    return sqrt(dx * dx + dy * dy);
}

typedef struct {
    int line;
    double x0, y0, x1, y1, a, b, ang, v, wid;
} clutter_t;

static void draw_clutter(float *buf, const clutter_t *c) {
    double minx, maxx, miny, maxy;
    if (c->line) {
        minx = fmin(c->x0, c->x1) - c->wid - 2;
        maxx = fmax(c->x0, c->x1) + c->wid + 2;
        miny = fmin(c->y0, c->y1) - c->wid - 2;
        maxy = fmax(c->y0, c->y1) + c->wid + 2;
    } else {
        const double r = fmax(c->a, c->b) + 2;
        minx = c->x0 - r;
        maxx = c->x0 + r;
        miny = c->y0 - r;
        maxy = c->y0 + r;
    }
    const int x0 = (int)fmax(0, minx), x1 = (int)fmin(W - 1, maxx), y0 = (int)fmax(0, miny), y1 = (int)fmin(H - 1, maxy);
    const double ca = cos(c->ang), sa = sin(c->ang);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            double sdf;
            if (c->line) {
                sdf = seg_dist(x, y, c->x0, c->y0, c->x1, c->y1) - 0.5 * c->wid;
            } else {
                const double dx = x - c->x0, dy = y - c->y0;
                const double u = (dx * ca + dy * sa) / c->a, v = (-dx * sa + dy * ca) / c->b;
                sdf = (sqrt(u * u + v * v) - 1) * fmin(c->a, c->b);
            }
            const float cv = cover(sdf, 1.0);
            if (cv > 0) buf[y * W + x] = mixf(buf[y * W + x], (float)c->v, cv);
        }
}

/** Radiance of the device and code at model point (X, Y); px = model units per pixel there. */
static float device_value(const scene_t *s, double X, double Y, double px, float v) {
    const double r = sqrt(X * X + Y * Y);
    if (s->kind == KIND_CLUTTER) return v;
    if (r > 1.30 + px) return v;
    v = mixf(v, (float)s->bezel, cover(r - 1.28, px));
    v = mixf(v, SCREEN, cover(r - 1.10, px));
    if (r < 0.40) {
        v = mixf(v, CORAL, cover(r - 0.36, px));
        v = mixf(v, SCREEN, cover(fabs(r - 0.26) - 0.02, px));
    }
    if (r > 0.42 && r < 0.86) {
        const double a = atan2(Y, X);
        const double f = (a + PI / 2) / (2 * PI / YK_VC_BARS) - 0.5;
        const int k0 = (int)floor(f + 0.5);
        for (int dk = -1; dk <= 1; dk++) {
            const int k = ((k0 + dk) % YK_VC_BARS + YK_VC_BARS) % YK_VC_BARS;
            if (s->len[k] <= 0) continue;
            const double ba = -PI / 2 + (k + 0.5) * 2 * PI / YK_VC_BARS;
            const double ux = cos(ba), uy = sin(ba);
            const double r1 = YK_VC_BASE_R - 0.5 * s->len[k], r2 = YK_VC_BASE_R + 0.5 * s->len[k];
            const double d = seg_dist(X, Y, r1 * ux, r1 * uy, r2 * ux, r2 * uy) - 0.5 * YK_VC_BAR_W;
            const float cv = cover(d, px);
            if (cv > 0) v = mixf(v, s->coral[k] ? CORAL : WHITE, cv);
        }
    }
    if (!s->no_dot && r > 0.78 && r < 0.98 && Y < 0)
        v = mixf(v, CORAL, cover(hypot(X, Y + YK_VC_DOT_R) - YK_VC_DOT_SIZE, px));
    if (fabs(r - 1) < 0.06) v = mixf(v, WHITE, cover(fabs(r - 1) - 0.5 * YK_VC_RING_W, px));
    if (s->occl > 0 && r > 0.30 && r < 1.40) {
        // a dark sector (a finger, a shadow): angular distance from its middle, in model units
        const double da = fabs(atan2(sin(atan2(Y, X) - s->occl_at), cos(atan2(Y, X) - s->occl_at)));
        const double sdf = (da - 0.5 * s->occl) * r;
        v = mixf(v, 0.06f, cover(sdf, px));
    }
    return v;
}

static void gaussian_blur(float *buf, float *tmp, double sigma) {
    const int r = (int)ceil(3 * sigma);
    float k[64];
    double sum = 0;
    for (int i = -r; i <= r; i++) sum += (k[i + r] = (float)exp(-0.5 * i * i / (sigma * sigma)));
    for (int i = 0; i <= 2 * r; i++) k[i] = (float)(k[i] / sum);
    for (int y = 0; y < H; y++) {
        const float *row = buf + y * W;
        float *o = tmp + y * W;
        for (int x = 0; x < W; x++) {
            float a = 0;
            for (int i = -r; i <= r; i++) {
                int xx = x + i;
                xx = xx < 0 ? 0 : xx >= W ? W - 1 : xx;
                a += k[i + r] * row[xx];
            }
            o[x] = a;
        }
    }
    for (int y = 0; y < H; y++) {
        float *o = buf + y * W;
        for (int x = 0; x < W; x++) o[x] = 0;
        for (int i = -r; i <= r; i++) {
            int yy = y + i;
            yy = yy < 0 ? 0 : yy >= H ? H - 1 : yy;
            const float *row = tmp + yy * W;
            const float kk = k[i + r];
            for (int x = 0; x < W; x++) o[x] += kk * row[x];
        }
    }
}

/** A straight streak `len` pixels long at angle `at`: the mean of bilinear samples along it. */
static void motion_blur(float *buf, float *tmp, double len, double at) {
    const int n = (int)ceil(len) + 1;
    const double dx = cos(at), dy = sin(at);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float a = 0;
            for (int i = 0; i < n; i++) {
                const double t = len * ((double)i / (n - 1) - 0.5);
                double sx = x + t * dx, sy = y + t * dy;
                sx = sx < 0 ? 0 : sx > W - 1.001 ? W - 1.001 : sx;
                sy = sy < 0 ? 0 : sy > H - 1.001 ? H - 1.001 : sy;
                const int x0 = (int)sx, y0 = (int)sy;
                const float fx = (float)(sx - x0), fy = (float)(sy - y0);
                const float *p = buf + y0 * W + x0;
                a += (1 - fy) * ((1 - fx) * p[0] + fx * p[1]) + fy * ((1 - fx) * p[W] + fx * p[W + 1]);
            }
            tmp[y * W + x] = a / n;
        }
    memcpy(buf, tmp, sizeof(float) * W * H);
}

/** Renders the scene into luma (stride W + pad). buf, tmp: W*H floats. */
static void render(const scene_t *s, uint64_t *rng, float *buf, float *tmp, uint8_t *luma) {
    // background: a lit room, a gradient and vignetting
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const double u = (x - W / 2.0) / W, v = (y - H / 2.0) / H;
            buf[y * W + x] = (float)(s->bg + s->bg_gx * u + s->bg_gy * v - 0.15 * s->bg * (u * u + v * v));
        }
    // clutter, kept off the device
    const double keep = s->kind == KIND_CLUTTER ? 0 : 1.40 * s->radius * (1 + fabs(s->persp));
    for (int i = 0; i < s->clutter; i++) {
        clutter_t c;
        memset(&c, 0, sizeof c);
        const int type = irange(rng, 0, 2);
        c.v = range(rng, 0.35, 1.0);
        for (int tries = 0; tries < 50; tries++) {
            c.x0 = range(rng, 0, W);
            c.y0 = range(rng, 0, H);
            if (type == 0) {  // blob
                c.line = 0;
                c.a = range(rng, 3, 40);
                c.b = c.a * range(rng, 0.3, 1);
                c.ang = range(rng, 0, PI);
                if (hypot(c.x0 - s->cx, c.y0 - s->cy) > keep + c.a) break;
            } else {  // a line (type 1) or a short dash (type 2, bar-like)
                c.line = 1;
                const double l = type == 1 ? range(rng, 20, 260) : range(rng, 6, 35);
                const double a = range(rng, 0, PI);
                c.wid = type == 1 ? range(rng, 1.5, 7) : range(rng, 2, 6);
                c.x1 = c.x0 + l * cos(a);
                c.y1 = c.y0 + l * sin(a);
                if (keep == 0 || seg_dist(s->cx, s->cy, c.x0, c.y0, c.x1, c.y1) > keep + c.wid) break;
            }
            c.v = -1;
        }
        if (c.v >= 0) draw_clutter(buf, &c);
    }
    // the device and the code, evaluated in model space
    if (s->kind != KIND_CLUTTER) {
        double minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
        for (int i = 0; i < 96; i++) {
            double x, y;
            to_pixel(s, 1.32 * cos(i * 2 * PI / 96), 1.32 * sin(i * 2 * PI / 96), &x, &y);
            minx = fmin(minx, x);
            maxx = fmax(maxx, x);
            miny = fmin(miny, y);
            maxy = fmax(maxy, y);
        }
        const int x0 = (int)fmax(0, minx - 2), x1 = (int)fmin(W - 1, maxx + 2);
        const int y0 = (int)fmax(0, miny - 2), y1 = (int)fmin(H - 1, maxy + 2);
        const double *hi = s->hinv;
        const double det = fabs(hi[0] * (hi[4] * hi[8] - hi[5] * hi[7]) - hi[1] * (hi[3] * hi[8] - hi[5] * hi[6]) +
                                hi[2] * (hi[3] * hi[7] - hi[4] * hi[6]));
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                const double w = hi[6] * x + hi[7] * y + hi[8];
                const double X = (hi[0] * x + hi[1] * y + hi[2]) / w, Y = (hi[3] * x + hi[4] * y + hi[5]) / w;
                const double px = sqrt(det / fabs(w * w * w));
                buf[y * W + x] = device_value(s, X, Y, px, buf[y * W + x]);
            }
    }
    // glare: a bright soft ellipse on the screen, along the bars' circle
    if (s->glare_frac > 0) {
        double gx, gy, tx, ty;
        const double a = s->glare_at;
        to_pixel(s, YK_VC_BASE_R * cos(a), YK_VC_BASE_R * sin(a), &gx, &gy);
        to_pixel(s, YK_VC_BASE_R * cos(a + 0.01), YK_VC_BASE_R * sin(a + 0.01), &tx, &ty);
        double ux = tx - gx, uy = ty - gy;
        const double ul = hypot(ux, uy);
        ux /= ul;
        uy /= ul;
        const double bars = s->glare_frac * YK_VC_BARS;
        const double ga = fmax(0.5 * bars * (2 * PI / YK_VC_BARS) * YK_VC_BASE_R * s->radius, 0.05 * s->radius);
        const double gb = 0.13 * s->radius;
        const double ext = 1.3 * fmax(ga, gb) + 2;
        for (int y = (int)fmax(0, gy - ext); y <= (int)fmin(H - 1, gy + ext); y++)
            for (int x = (int)fmax(0, gx - ext); x <= (int)fmin(W - 1, gx + ext); x++) {
                const double dx = x - gx, dy = y - gy;
                const double u = (dx * ux + dy * uy) / ga, v = (-dx * uy + dy * ux) / gb;
                const double e = sqrt(u * u + v * v);
                double g = 0;
                if (e < 0.85) g = 1;
                else if (e < 1.25) {
                    const double t = (1.25 - e) / 0.4;
                    g = t * t * (3 - 2 * t);
                }
                buf[y * W + x] += (float)(s->glare_amp * g);
            }
    }
    if (s->blur >= 0.3) gaussian_blur(buf, tmp, s->blur);
    if (s->motion >= 1) motion_blur(buf, tmp, s->motion, s->motion_at);
    // exposure, sensor noise, 8 bits; the padding of each row is garbage
    const int stride = W + s->pad;
    const float nz = (float)(s->noise / 255.0 * 1.7320508);
    for (int y = 0; y < H; y++) {
        uint8_t *o = luma + (size_t)y * stride;
        for (int x = 0; x < W; x++) {
            float v = (float)(s->gain * buf[y * W + x] + s->offset);
            if (s->noise > 0) {
                const uint64_t r = splitmix(rng);
                const float n = ((float)(r & 0xffff) + (float)((r >> 16) & 0xffff) + (float)((r >> 32) & 0xffff) +
                                 (float)(r >> 48)) / 65536.0f - 2.0f;  // Irwin-Hall: variance 1/3
                v += nz * n;
            }
            const int q = (int)lrintf(v * 255);
            o[x] = (uint8_t)(q < 0 ? 0 : q > 255 ? 255 : q);
        }
        for (int x = W; x < stride; x++) o[x] = (uint8_t)(splitmix(rng) | 0x80);
    }
}

// ---- conditions ----

typedef struct {
    const char *name;
    int frames, kind;
    double rmin, rmax, tilt_min, tilt_max, blur_min, blur_max, noise_min, noise_max;
    double glare_prob, glare_min, glare_max, occl_prob, occl_min, occl_max, persp_max;
    double gain_min, gain_max, off_min, off_max;
    int clutter_min, clutter_max;
    int target;           // the moderate condition: must reach 95%
    double motion_max;    // hand shake, pixels (0: none)
} cond_t;

//                                               frames kind            r          tilt     blur      noise     glare p,min,max   occl p,min,max  persp gain       offset     clutter target motion
static const cond_t positives[] = {
    {"ideal: r 120-300, straight, sharp",          300, KIND_CODE,      120, 300,  0,  0,  0,   0,   0, 0,  0,   0,    0,     0, 0,  0,   0,    1.0, 1.0, 0.02, 0.02, 0, 0, 0, 0},
    {"MODERATE: r>=120 tilt<=30 blur<=1.5 noise<=8 glare<=10%", 1000, KIND_CODE, 120, 300, 0, 30, 0, 1.5, 0, 8, 0.5, 0, 0.10, 0, 0, 0, 0.12, 0.6, 1.2, 0, 0.10, 0, 15, 1, 0},
    {"small: r 80-120, else moderate",             300, KIND_CODE,       80, 120,  0, 30,  0, 1.5,   0, 8,  0.5, 0, 0.10, 0,     0, 0,  0.12, 0.6, 1.2, 0,   0.10, 0, 15, 0, 0},
    {"tilt 30-35 + perspective",                   300, KIND_CODE,      120, 300, 30, 35,  0, 1.5,   0, 8,  0.5, 0, 0.10, 0,     0, 0,  0.15, 0.6, 1.2, 0,   0.10, 0, 15, 0, 0},
    {"blur 1.5-2.5",                               300, KIND_CODE,      120, 300,  0, 30, 1.5, 2.5,  0, 8,  0.5, 0, 0.10, 0,     0, 0,  0.12, 0.6, 1.2, 0,   0.10, 0, 15, 0, 0},
    {"noise 8-12",                                 300, KIND_CODE,      120, 300,  0, 30,  0, 1.5,   8, 12, 0.5, 0, 0.10, 0,     0, 0,  0.12, 0.6, 1.2, 0,   0.10, 0, 15, 0, 0},
    {"glare 10-20% of the bars",                   300, KIND_CODE,      120, 300,  0, 30,  0, 1.5,   0, 8,  1.0, 0.10, 0.20, 0,  0, 0,  0.12, 0.6, 1.2, 0,   0.10, 0, 15, 0, 0},
    {"dark sector 15-30 deg",                      300, KIND_CODE,      120, 300,  0, 30,  0, 1.5,   0, 8,  0,   0, 0,    1, 15, 30,  0.12, 0.6, 1.2, 0,   0.10, 0, 15, 0, 0},
    {"dark sector 30-45 deg",                      300, KIND_CODE,      120, 300,  0, 30,  0, 1.5,   0, 8,  0,   0, 0,    1, 30, 45,  0.12, 0.6, 1.2, 0,   0.10, 0, 15, 0, 0},
    {"dark exposure (gain 0.3-0.5)",               300, KIND_CODE,      120, 300,  0, 30,  0, 1.5,   0, 8,  0.5, 0, 0.10, 0,     0, 0,  0.12, 0.3, 0.5, 0,   0.03, 0, 15, 0, 0},
    {"bright exposure (offset 0.2-0.35)",          300, KIND_CODE,      120, 300,  0, 30,  0, 1.5,   0, 8,  0.5, 0, 0.10, 0,     0, 0,  0.12, 0.7, 1.0, 0.2, 0.35, 0, 15, 0, 0},
    {"heavy clutter (30-60 items)",                300, KIND_CODE,      120, 300,  0, 30,  0, 1.5,   0, 8,  0.5, 0, 0.10, 0,     0, 0,  0.12, 0.6, 1.2, 0,   0.10, 30, 60, 0, 0},
    {"hand shake: motion streak 2-12 px",          300, KIND_CODE,      120, 300,  0, 30,  0, 1.0,   0, 8,  0.5, 0, 0.10, 0,     0, 0,  0.12, 0.6, 1.2, 0,   0.10, 0, 15, 0, 12},
    {"a later version, else moderate: reads as later", 300, KIND_LATER,  120, 300,  0, 30,  0, 1.5,   0, 8,  0.5, 0, 0.10, 0,     0, 0,  0.12, 0.6, 1.2, 0,   0.10, 0, 15, 0, 0},
    {"everything: r 80-300 tilt<=35 blur<=2.5 noise<=12", 300, KIND_CODE, 80, 300, 0, 35, 0, 2.5, 0, 12, 0.5, 0, 0.20, 0.3, 15, 40, 0.15, 0.4, 1.3, 0, 0.15, 0, 40, 0, 0},
};

static const cond_t negatives[] = {
    {"animation: random levels",                  1200, KIND_ANIM_LEVELS, 80, 300, 0, 35,  0, 2.0,   0, 10, 0.3, 0, 0.10, 0,     0, 0,  0.12, 0.5, 1.2, 0,   0.10, 0, 20, 0, 0},
    {"animation: lengths between levels",          800, KIND_ANIM_FREE,   80, 300, 0, 35,  0, 2.0,   0, 10, 0.3, 0, 0.10, 0,     0, 0,  0.12, 0.5, 1.2, 0,   0.10, 0, 20, 0, 0},
    {"clutter only",                               600, KIND_CLUTTER,     80, 300, 0, 35,  0, 2.0,   0, 10, 0,   0, 0,    0,     0, 0,  0,    0.5, 1.2, 0,   0.10, 20, 80, 0, 0},
    {"half a code: at the frame's edge",           300, KIND_HALF_EDGE,   80, 300, 0, 35,  0, 2.0,   0, 10, 0,   0, 0,    0,     0, 0,  0.12, 0.5, 1.2, 0,   0.10, 0, 20, 0, 0},
    {"half a code: 24 bars drawn",                 300, KIND_HALF_BARS,   80, 300, 0, 35,  0, 2.0,   0, 10, 0,   0, 0,    0,     0, 0,  0.12, 0.5, 1.2, 0,   0.10, 0, 20, 0, 0},
};

static void make_scene(const cond_t *cd, uint64_t seed, scene_t *s, yk_vc_payload_t *truth, uint64_t *rng) {
    *rng = seed;
    memset(s, 0, sizeof *s);
    s->kind = cd->kind;
    s->radius = range(rng, cd->rmin, cd->rmax);
    s->tilt = range(rng, cd->tilt_min, cd->tilt_max) * PI / 180;
    s->tilt_axis = range(rng, 0, PI);
    s->rot = range(rng, 0, 2 * PI);
    s->persp = range(rng, 0, cd->persp_max) * tan(s->tilt) / tan(35 * PI / 180) * (uni(rng) < 0.5 ? -1 : 1);
    s->blur = range(rng, cd->blur_min, cd->blur_max);
    if (cd->motion_max > 0) {
        s->motion = range(rng, 2, cd->motion_max);
        s->motion_at = range(rng, 0, PI);
    }
    s->noise = range(rng, cd->noise_min, cd->noise_max);
    s->gain = range(rng, cd->gain_min, cd->gain_max);
    s->offset = range(rng, cd->off_min, cd->off_max);
    s->bg = range(rng, 0.1, 0.55);
    s->bg_gx = range(rng, -0.15, 0.15);
    s->bg_gy = range(rng, -0.15, 0.15);
    s->bezel = uni(rng) < 0.7 ? range(rng, 0.03, 0.2) : range(rng, 0.5, 0.85);  // dark or light device
    if (uni(rng) < cd->glare_prob) {
        s->glare_frac = range(rng, cd->glare_min, cd->glare_max);
        s->glare_amp = range(rng, 0.9, 1.5);
        s->glare_at = range(rng, 0, 2 * PI);
    }
    if (uni(rng) < cd->occl_prob) {
        s->occl = range(rng, cd->occl_min, cd->occl_max) * PI / 180;
        s->occl_at = range(rng, 0, 2 * PI);
    }
    s->clutter = irange(rng, cd->clutter_min, cd->clutter_max);
    s->pad = (int)(splitmix(rng) % 3) * 37;  // stride = width, +37, +74
    // where the code sits: the ring inside the frame (or, for half a code, its centre on an edge)
    const double ext = 1.04 * s->radius * (1 + fabs(s->persp));
    s->cx = ext < W - ext ? range(rng, ext, W - ext) : W / 2.0;
    s->cy = ext < H - ext ? range(rng, ext, H - ext) : H / 2.0;
    if (s->kind == KIND_HALF_EDGE) {
        const double off = range(rng, -0.08, 0.08) * s->radius;
        switch (irange(rng, 0, 3)) {
            case 0: s->cx = off; break;
            case 1: s->cx = W + off; break;
            case 2: s->cy = off; break;
            default: s->cy = H + off; break;
        }
    }
    scene_geometry(s);

    *truth = random_payload(rng);
    uint8_t lv[YK_VC_BARS];
    yk_vc_encode(truth, lv);
    if (s->kind == KIND_LATER) {  // version 1..3, any kind and bits: the reader cannot know what they mean
        uint8_t data[YK_VC_DATA_BYTES];
        for (int i = 0; i < YK_VC_DATA_BYTES; i++) data[i] = (uint8_t)splitmix(rng);
        data[0] = (uint8_t)((irange(rng, 1, 3) << 6) | (data[0] & 0x3f));
        yk_vc_encode_data(data, lv);
        memset(truth, 0, sizeof *truth);
    }
    const int pattern = irange(rng, 0, 9);  // animation: one frame in ten is a regular pattern
    for (int k = 0; k < YK_VC_BARS; k++) {
        int level = lv[k];
        if (s->kind == KIND_ANIM_LEVELS) level = pattern == 0 ? (k * 0 + (int)(seed % 4)) : pattern == 1 ? k % 4 : pattern == 2 ? (k / 2) % 2 * 3 : irange(rng, 0, 3);
        s->len[k] = (float)yk_vc_bar_length(level);
        s->coral[k] = level == 3;
        if (s->kind == KIND_ANIM_FREE) {
            s->len[k] = (float)range(rng, 0.6 * YK_VC_LEN0, yk_vc_bar_length(3));
            s->coral[k] = s->len[k] > yk_vc_bar_length(3) - 0.02;
        }
    }
    if (s->kind == KIND_HALF_BARS) {
        const int start = irange(rng, 0, YK_VC_BARS - 1);
        for (int i = 0; i < YK_VC_BARS / 2; i++) s->len[(start + i) % YK_VC_BARS] = 0;
    }
}

// ---- running conditions on all cores ----

typedef struct {
    int frames, read, misread, status[7];
    double erased, corrected;  // sums over reads
    double centre_err;         // sum of centre errors (pixels) over reads
} tally_t;

typedef struct {
    const cond_t *cond;
    int cond_id;
    int next;
    pthread_mutex_t lock;
    tally_t tally;
    int negative;
} job_t;

static void *worker(void *arg) {
    job_t *job = arg;
    float *buf = malloc(sizeof(float) * W * H), *tmp = malloc(sizeof(float) * W * H);
    uint8_t *luma = malloc((size_t)(W + 80) * H);
    if (!buf || !tmp || !luma) abort();
    for (;;) {
        pthread_mutex_lock(&job->lock);
        const int i = job->next++;
        pthread_mutex_unlock(&job->lock);
        if (i >= job->cond->frames) break;
        scene_t s;
        yk_vc_payload_t truth;
        uint64_t rng;
        make_scene(job->cond, 0x5eed0000ULL * (uint64_t)(job->cond_id + 1) + (uint64_t)i * 7919u, &s, &truth, &rng);
        render(&s, &rng, buf, tmp, luma);
        yk_vc_result_t r;
        const yk_vc_status_t st = yk_vc_scan(luma, W, H, W + s.pad, &r);
        int read = 0, misread = 0;
        if (st == YK_VC_OK || st == YK_VC_LATER) {
            const int later = s.kind == KIND_LATER, half = s.kind == KIND_HALF_EDGE || s.kind == KIND_HALF_BARS;
            const int right = (st == YK_VC_LATER) == later && (later || ((!job->negative || half) && same_payload(&r.payload, &truth)));
            if (right) read = 1;
            else misread = 1;
            if (misread) {
                char a[48], b[48];
                yk_vc_payload_text(&r.payload, a, sizeof a);
                yk_vc_payload_text(&truth, b, sizeof b);
                if (st == YK_VC_LATER) snprintf(a, sizeof a, "later");
                if (later) snprintf(b, sizeof b, "later");
                printf("  MISREAD [%s] frame %d: read \"%s\" (shown \"%s\"), erased %d corrected %d\n", job->cond->name, i, a,
                       b, r.erased, r.corrected);
            }
        }
        pthread_mutex_lock(&job->lock);
        tally_t *t = &job->tally;
        t->frames++;
        t->status[st]++;
        t->read += read;
        t->misread += misread;
        if (read) {
            t->erased += r.erased;
            t->corrected += r.corrected;
            double x, y;
            to_pixel(&s, 0, 0, &x, &y);
            t->centre_err += hypot(x - r.centre_x, y - r.centre_y);
        }
        pthread_mutex_unlock(&job->lock);
    }
    free(buf);
    free(tmp);
    free(luma);
    return NULL;
}

static int threads(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) n = 1;
    if (n > 24) n = 24;
    return (int)n;
}

static tally_t run_condition(const cond_t *cd, int cond_id, int negative) {
    job_t job;
    memset(&job, 0, sizeof job);
    job.cond = cd;
    job.cond_id = cond_id;
    job.negative = negative;
    pthread_mutex_init(&job.lock, NULL);
    const int nt = threads();
    pthread_t th[24];
    for (int i = 0; i < nt; i++) pthread_create(&th[i], NULL, worker, &job);
    for (int i = 0; i < nt; i++) pthread_join(th[i], NULL);
    pthread_mutex_destroy(&job.lock);
    return job.tally;
}

static void test_frames(void) {
    current = "frames";
    printf("\nrendered 1280x720 frames, read with yk_vc_scan (%d threads)\n", threads());
    printf("%-58s %6s %7s %5s | %7s %9s %6s %10s %11s | %6s %6s %6s\n", "condition", "frames", "read", "wrong", "no_bars",
           "no_centre", "no_dot", "bad_levels", "not_decoded", "erased", "fixed", "c_err");
    int misreads = 0;
    double target_rate = 1, later_rate = 1;
    for (size_t i = 0; i < sizeof positives / sizeof positives[0]; i++) {
        const tally_t t = run_condition(&positives[i], (int)i, 0);
        const double rate = (double)t.read / t.frames;
        printf("%-58s %6d %6.1f%% %5d | %7d %9d %6d %10d %11d | %6.2f %6.2f %5.2fpx\n", positives[i].name, t.frames, 100 * rate,
               t.misread, t.status[YK_VC_NO_BARS], t.status[YK_VC_NO_CENTRE], t.status[YK_VC_NO_DOT],
               t.status[YK_VC_BAD_LEVELS], t.status[YK_VC_NOT_DECODED], t.read ? t.erased / t.read : 0,
               t.read ? t.corrected / t.read : 0, t.read ? t.centre_err / t.read : 0);
        misreads += t.misread;
        if (positives[i].target) target_rate = rate;
        if (positives[i].kind == KIND_LATER) later_rate = rate;
        fflush(stdout);
    }
    printf("\nnegative frames: any \"ok\" (or \"later\") that is not the code shown is a misread\n");
    printf("%-58s %6s %7s %5s | %7s %9s %6s %10s %11s\n", "condition", "frames", "ok", "wrong", "no_bars", "no_centre",
           "no_dot", "bad_levels", "not_decoded");
    int negatives_run = 0;
    for (size_t i = 0; i < sizeof negatives / sizeof negatives[0]; i++) {
        const tally_t t = run_condition(&negatives[i], 100 + (int)i, 1);
        printf("%-58s %6d %7d %5d | %7d %9d %6d %10d %11d\n", negatives[i].name, t.frames, t.read + t.misread, t.misread,
               t.status[YK_VC_NO_BARS], t.status[YK_VC_NO_CENTRE], t.status[YK_VC_NO_DOT], t.status[YK_VC_BAD_LEVELS],
               t.status[YK_VC_NOT_DECODED]);
        misreads += t.misread;
        negatives_run += t.frames;
        fflush(stdout);
    }
    printf("\nmisreads (ok or later, not what was shown) over all frames: %d (%d negative frames)\n", misreads, negatives_run);
    printf("moderate condition: %.1f%% read (target 95%%); a later version: %.1f%% read as later (target 95%%)\n",
           100 * target_rate, 100 * later_rate);
    CHECK(misreads == 0);
    CHECK(target_rate >= 0.95);
    CHECK(later_rate >= 0.95);
}

// ---- thread safety and time ----

static int cmp_double(const void *a, const void *b) {
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void test_time(void) {
    current = "time";
    float *buf = malloc(sizeof(float) * W * H), *tmp = malloc(sizeof(float) * W * H);
    uint8_t *luma = malloc((size_t)(W + 80) * H);
    if (!buf || !tmp || !luma) abort();
    const size_t last = sizeof positives / sizeof positives[0] - 1;  // "everything"
    const cond_t *sets[4] = {&positives[1], &positives[last], &negatives[0], &negatives[2]};
    printf("\ntime per frame, yk_vc_scan only, one thread, 1280x720, -O2, 200 frames each\n");
    printf("(each frame read 3 times: \"best\" is its fastest read, which leaves out other programs taking the CPU;"
           " \"all\" is every read)\n");
    double worst_mean = 0;
    for (int c = 0; c < 4; c++) {
        enum { N = 200 };
        double best[N], all = 0;
        for (int i = 0; i < N; i++) {
            scene_t s;
            yk_vc_payload_t truth;
            uint64_t rng;
            make_scene(sets[c], 0xabcdef00ULL + (uint64_t)i * 104729u + (uint64_t)c, &s, &truth, &rng);
            render(&s, &rng, buf, tmp, luma);
            best[i] = 1e9;
            for (int k = 0; k < 3; k++) {
                yk_vc_result_t r;
                const double t0 = now_ms();
                yk_vc_scan(luma, W, H, W + s.pad, &r);
                const double dt = now_ms() - t0;
                all += dt;
                if (dt < best[i]) best[i] = dt;
            }
        }
        double mean = 0;
        for (int i = 0; i < N; i++) mean += best[i] / N;
        qsort(best, N, sizeof(double), cmp_double);
        printf("  %-50s best: mean %5.1f ms, median %5.1f, p95 %5.1f, max %5.1f | all: mean %5.1f ms\n", sets[c]->name, mean,
               best[N / 2], best[(int)(N * 0.95)], best[N - 1], all / (3 * N));
        if (mean > worst_mean) worst_mean = mean;
    }
    if (worst_mean > 30) printf("  NOTE: over the 30 ms budget on this machine\n");
    free(buf);
    free(tmp);
    free(luma);
}

typedef struct {
    const uint8_t *luma;
    int stride;
    yk_vc_result_t want;
    int bad;
} same_job_t;

static void *same_worker(void *arg) {
    same_job_t *j = arg;
    for (int i = 0; i < 10; i++) {
        yk_vc_result_t r;
        yk_vc_scan(j->luma, W, H, j->stride, &r);
        if (memcmp(&r, &j->want, sizeof r) != 0) j->bad++;
    }
    return NULL;
}

static void test_same_on_threads(void) {
    current = "threads";
    float *buf = malloc(sizeof(float) * W * H), *tmp = malloc(sizeof(float) * W * H);
    uint8_t *luma = malloc((size_t)(W + 80) * H);
    if (!buf || !tmp || !luma) abort();
    scene_t s;
    yk_vc_payload_t truth;
    uint64_t rng;
    make_scene(&positives[1], 42, &s, &truth, &rng);
    render(&s, &rng, buf, tmp, luma);
    same_job_t jobs[8];
    pthread_t th[8];
    yk_vc_result_t want;
    memset(&want, 0, sizeof want);
    yk_vc_scan(luma, W, H, W + s.pad, &want);
    for (int i = 0; i < 8; i++) {
        jobs[i].luma = luma;
        jobs[i].stride = W + s.pad;
        jobs[i].want = want;
        jobs[i].bad = 0;
        pthread_create(&th[i], NULL, same_worker, &jobs[i]);
    }
    int bad = 0;
    for (int i = 0; i < 8; i++) {
        pthread_join(th[i], NULL);
        bad += jobs[i].bad;
    }
    CHECK(bad == 0);
    printf("same frame on 8 threads x 10: %s\n", bad ? "DIFFERENT RESULTS" : "identical results");
    // and the arguments it must refuse
    yk_vc_result_t r;
    CHECK(yk_vc_scan(NULL, W, H, W, &r) != YK_VC_OK);
    CHECK(yk_vc_scan(luma, W, H, W - 1, &r) != YK_VC_OK);
    free(buf);
    free(tmp);
    free(luma);
}

// ---- 4. how far a frame got ----

/** A plain code at (cx, cy): straight, sharp, a mid-grey room, no noise. Bars: `bars` 0 the code of a random payload,
 *  1 lengths anywhere from below level 0 to level 3 (the bars' animation), 2 random levels; `dot`: the coral dot. */
static void status_scene(scene_t *s, double cx, double cy, int bars, int dot, uint64_t *rng) {
    memset(s, 0, sizeof *s);
    s->kind = KIND_CODE;
    s->radius = 150;
    s->cx = cx;
    s->cy = cy;
    s->blur = 0.6;
    s->gain = 1;
    s->bg = 0.3;
    s->bezel = 0.1;
    s->no_dot = !dot;
    scene_geometry(s);
    const yk_vc_payload_t p = random_payload(rng);
    uint8_t lv[YK_VC_BARS];
    yk_vc_encode(&p, lv);
    for (int k = 0; k < YK_VC_BARS; k++) {
        const int level = bars == 2 ? irange(rng, 0, 3) : lv[k];
        s->len[k] = (float)(bars == 1 ? range(rng, 0.6 * YK_VC_LEN0, yk_vc_bar_length(3)) : yk_vc_bar_length(level));
        s->coral[k] = s->len[k] > yk_vc_bar_length(3) - 0.02;
    }
}

static void test_status_stage(void) {
    current = "status";
    // The numbers stay as they are; the stages are the reader's order.
    CHECK(YK_VC_OK == 0 && YK_VC_NO_BARS == 1 && YK_VC_NO_CENTRE == 2 && YK_VC_NO_DOT == 3 && YK_VC_BAD_LEVELS == 4 &&
          YK_VC_NOT_DECODED == 5 && YK_VC_LATER == 6);
    const yk_vc_status_t order[] = {YK_VC_NO_BARS, YK_VC_NO_CENTRE, YK_VC_BAD_LEVELS, YK_VC_NO_DOT,
                                    YK_VC_NOT_DECODED, YK_VC_LATER, YK_VC_OK};
    for (int i = 0; i < 7; i++) CHECK(yk_vc_status_stage(order[i]) == i);
    CHECK(yk_vc_status_stage((yk_vc_status_t)7) == -1 && yk_vc_status_stage((yk_vc_status_t)-1) == -1);

    float *buf = malloc(sizeof(float) * W * H), *tmp = malloc(sizeof(float) * W * H);
    uint8_t *a = malloc((size_t)W * H), *b = malloc((size_t)W * H), *both = malloc((size_t)W * H);
    if (!buf || !tmp || !a || !b || !both) abort();
    scene_t s;
    yk_vc_result_t r;
    int frames = 0;
    // 'a': the centre found, the levels not (bars between levels); 'b': the levels read, no dot to turn them by.
    // Both codes sit apart in one frame: the reader tries each centre and must say how far the furthest got.
    for (uint64_t seed = 1; seed <= 6; seed++) {
        uint64_t rng = seed * 0x9e3779b97f4a7c15ULL;
        status_scene(&s, 330, 360, 1, 1, &rng);
        render(&s, &rng, buf, tmp, a);
        const yk_vc_status_t sa = yk_vc_scan(a, W, H, W, &r);
        status_scene(&s, 950, 360, 2, 0, &rng);
        render(&s, &rng, buf, tmp, b);
        const yk_vc_status_t sb = yk_vc_scan(b, W, H, W, &r);
        if (sa != YK_VC_BAD_LEVELS || sb != YK_VC_NO_DOT) continue;  // a seed whose bars land elsewhere
        for (int y = 0; y < H; y++) {
            memcpy(both + (size_t)y * W, a + (size_t)y * W, W / 2);
            memcpy(both + (size_t)y * W + W / 2, b + (size_t)y * W + W / 2, W / 2);
        }
        const yk_vc_status_t st = yk_vc_scan(both, W, H, W, &r);
        CHECK(st == YK_VC_NO_DOT);
        if (st != YK_VC_NO_DOT) printf("  seed %llu: both codes in one frame said %s\n", (unsigned long long)seed,
                                       yk_vc_status_name(st));
        frames++;
    }
    CHECK(frames >= 3);
    // A plain code still reads, dot or no dot (with no dot every turn is tried).
    uint64_t rng = 77;
    status_scene(&s, 640, 360, 0, 1, &rng);
    render(&s, &rng, buf, tmp, a);
    CHECK(yk_vc_scan(a, W, H, W, &r) == YK_VC_OK);
    printf("status stages: bad_levels alone, no_dot alone, and both in one frame -> no_dot (%d frames)\n", frames);
    free(buf);
    free(tmp);
    free(a);
    free(b);
    free(both);
}

int main(int argc, char **argv) {
    const int quick = argc > 1 && strcmp(argv[1], "--quick") == 0;
    test_bits();
    test_rs();
    test_status_stage();
    if (!quick) {
        test_same_on_threads();
        test_frames();
        test_time();
    }
    printf("\nyaykit_voicecode host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
