// The 伴环 mark's arrival (yk_mark_fit.h): state-spec.json's tracks as tables, plain C (host tested: test/test_mark.c
// checks every keyframe against the file itself).
#include "yk_mark_fit.h"

typedef struct {
    float t, v;  // a keyframe: its time (of the 900 ms) and value
} keyframe_t;

// design/tikpal/2026-11-01-brand-kit-v1/03-motion/state-spec.json, states.arrival.tracks.
static const keyframe_t RING_SCALE[] = {{0, 0.94f}, {0.42f, 1}};
static const keyframe_t RING_OPA[] = {{0, 0}, {0.28f, 1}};
static const keyframe_t DOT_SCALE[] = {{0, 0.62f}, {0.22f, 0.62f}, {0.58f, 1.07f}, {1, 1}};
static const keyframe_t DOT_OPA[] = {{0, 0}, {0.18f, 0}, {0.38f, 1}};
static const keyframe_t DOT_X[] = {{0, 12}, {0.22f, 12}, {0.75f, 0}};
static const keyframe_t DOT_Y[] = {{0, -10}, {0.22f, -10}, {0.75f, 0}};

/** cubic-bezier(0.42, 0, 0.58, 1) at x = p (0..1): the curve's x solved by halving (24 steps, well under a pixel). */
static float ease(float p) {
    float lo = 0, hi = 1, s = p;
    for (int i = 0; i < 24; i++) {
        s = (lo + hi) / 2;
        const float x = 3 * (1 - s) * (1 - s) * s * 0.42f + 3 * (1 - s) * s * s * 0.58f + s * s * s;
        if (x < p) lo = s;
        else hi = s;
    }
    return 3 * (1 - s) * s * s + s * s * s;
}

static float track(const keyframe_t *k, int n, float t) {
    if (t <= k[0].t) return k[0].v;
    for (int i = 1; i < n; i++)
        if (t < k[i].t) return k[i - 1].v + (k[i].v - k[i - 1].v) * ease((t - k[i - 1].t) / (k[i].t - k[i - 1].t));
    return k[n - 1].v;
}

#define TRACK(k, t) track(k, (int)(sizeof(k) / sizeof(k[0])), t)

void yk_mark_ring_init(yk_mark_ring_t *r, float outer) {
    const float half = outer * YK_MARK_STROKE / (2.0f * YK_MARK_UNITS), mid = outer / 2 - half;
    const float rad = 3.14159265f / 180, a1 = YK_MARK_ARC_END * rad, a2 = YK_MARK_ARC_START * rad;
    const float lo = mid - half - 1, hi = mid + half + 1;
    *r = (yk_mark_ring_t){.mid = mid,
                          .half = half,
                          .lo2 = lo > 0 ? lo * lo : 0,
                          .hi2 = hi * hi,
                          .ex1 = mid * cosf(a1),
                          .ey1 = mid * sinf(a1),
                          .ex2 = mid * cosf(a2),
                          .ey2 = mid * sinf(a2),
                          .gx1 = cosf(a1),
                          .gy1 = sinf(a1),
                          .gx2 = cosf(a2),
                          .gy2 = sinf(a2)};
}

yk_mark_pose_t yk_mark_arrival(int ms) {
    if (ms >= YK_MARK_ARRIVAL_MS) return YK_MARK_REST;
    const float t = ms <= 0 ? 0 : (float)ms / YK_MARK_ARRIVAL_MS;
    return (yk_mark_pose_t){.ring_scale = TRACK(RING_SCALE, t),
                            .ring_opa = TRACK(RING_OPA, t),
                            .dot_scale = TRACK(DOT_SCALE, t),
                            .dot_opa = TRACK(DOT_OPA, t),
                            .dot_x = TRACK(DOT_X, t),
                            .dot_y = TRACK(DOT_Y, t)};
}
