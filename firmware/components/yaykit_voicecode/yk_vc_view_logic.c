// The round screen's side of 声波码 without LVGL (yk_vc_view_logic.h). The drawing and the talking come from the test
// firmware the iPhone read on 10-30 (#975, exp/voice-code: voicecode_test/main/vc_logic.c).
#include "yk_vc_view_logic.h"

#include <math.h>
#include <string.h>

// From app.json at build time (voicecode_number.cmake); 0 when the app has no voiceCode.
#ifndef YK_VC_APP_NUMBER
#define YK_VC_APP_NUMBER 0
#endif
#if YK_VC_APP_NUMBER < 0 || YK_VC_APP_NUMBER > 65535
#error "YK_VC_APP_NUMBER is an app number, 1..65535"
#endif

#define PI_F 3.14159265358979f

static float len_max(void) { return yk_vc_bar_length(YK_VC_LEVELS - 1); }

static int px_at_least_1(float v) {
    const int n = (int)lroundf(v);
    return n < 1 ? 1 : n;
}

void yk_vc_px(int r, yk_vc_px_t *out) {
    memset(out, 0, sizeof(*out));
    out->r = r;
    out->ring_w = px_at_least_1(YK_VC_RING_W * r);
    out->bar_w = px_at_least_1(YK_VC_BAR_W * r);
    out->dot_y = (int)lroundf(YK_VC_DOT_R * r);
    out->dot_r = px_at_least_1(YK_VC_DOT_SIZE * r);
    out->logo = (int)(YK_VC_LOGO_D * r);  // down: never over
    // LVGL draws the ring with its outer radius round(r + w/2) and the stroke inwards; the bars' outer caps and the
    // dot stay well inside it.
    const int ring = (int)lroundf(r + out->ring_w / 2.0f);
    const int bars = (int)ceilf((YK_VC_BASE_R + 0.5f * len_max()) * r + out->bar_w / 2.0f + 0.5f);
    const int dot = out->dot_y + out->dot_r;
    out->reach = ring > bars ? (ring > dot ? ring : dot) : (bars > dot ? bars : dot);
}

static void bar_ends(const yk_vc_px_t *px, int k, float len, float e[4]) {
    const float a = yk_vc_bar_angle(k), ux = cosf(a), uy = sinf(a);
    const float mid = YK_VC_BASE_R * px->r, half = 0.5f * len * px->r;
    e[0] = (mid - half) * ux, e[1] = (mid - half) * uy;  // towards the centre
    e[2] = (mid + half) * ux, e[3] = (mid + half) * uy;
}

void yk_vc_bar_px(const yk_vc_px_t *px, int k, float len, int16_t ends[4]) {
    float e[4];
    bar_ends(px, k, len, e);
    for (int i = 0; i < 4; i++) ends[i] = (int16_t)lroundf(e[i]);
}

void yk_vc_box_px(const yk_vc_px_t *px, int j, int margin, int16_t box[4]) {
    float lo_x = 1e9f, lo_y = 1e9f, hi_x = -1e9f, hi_y = -1e9f;
    for (int k = 2 * j; k <= 2 * j + 1; k++) {
        // The longest bar holds every shorter one (all share the midpoint and the direction).
        float e[4];
        bar_ends(px, k, len_max(), e);
        lo_x = fminf(lo_x, fminf(e[0], e[2])), hi_x = fmaxf(hi_x, fmaxf(e[0], e[2]));
        lo_y = fminf(lo_y, fminf(e[1], e[3])), hi_y = fmaxf(hi_y, fmaxf(e[1], e[3]));
    }
    // The cap, the half pixel the ends are rounded by, and a pixel of antialiasing.
    const float pad = px->bar_w / 2.0f + 1.5f + (float)margin;
    box[0] = (int16_t)floorf(lo_x - pad), box[1] = (int16_t)floorf(lo_y - pad);
    box[2] = (int16_t)ceilf(hi_x + pad), box[3] = (int16_t)ceilf(hi_y + pad);
}

int yk_vc_nearest_level(float len) {
    const int level = (int)lroundf((len - YK_VC_LEN0) / YK_VC_LEN_STEP);
    return level < 0 ? 0 : level > YK_VC_LEVELS - 1 ? YK_VC_LEVELS - 1 : level;
}

bool yk_vc_middle_fits(int r, int w, int h) {
    const float x = w / 2.0f, y = h / 2.0f, in = YK_VC_LOGO_MAX_R * r;
    return x * x + y * y <= in * in;
}

// ---------------------------------------------------------------- the talking

static uint32_t next_rand(yk_vc_anim_t *a) {
    uint32_t x = a->rng;  // xorshift32
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return a->rng = x;
}

static float rand01(yk_vc_anim_t *a) { return (float)(next_rand(a) >> 8) * (1.0f / 16777216.0f); }

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void yk_vc_anim_init(yk_vc_anim_t *a) {
    memset(a, 0, sizeof(*a));
    for (int k = 0; k < YK_VC_BARS; k++) a->lengths[k] = YK_VC_LEN0;
    a->rng = 0x9e3779b9u;
}

void yk_vc_anim_start(yk_vc_anim_t *a, const uint8_t target[YK_VC_BARS], uint32_t seed) {
    memcpy(a->target, target, YK_VC_BARS);
    a->rng = seed ? seed : 0x9e3779b9u;
    const float span = len_max() - YK_VC_LEN0;
    for (int k = 0; k < YK_VC_BARS; k++) a->drive[k] = clamp01((a->lengths[k] - YK_VC_LEN0) / span);
    a->phase1 = rand01(a) * 2 * PI_F;
    a->phase2 = rand01(a) * 2 * PI_F;
    a->running = true;
}

bool yk_vc_anim_step(yk_vc_anim_t *a, int t_ms) {
    if (!a->running) return false;
    if (t_ms >= YK_VC_ANIM_MS) {
        for (int k = 0; k < YK_VC_BARS; k++) a->lengths[k] = yk_vc_bar_length(a->target[k]);
        a->running = false;
        return true;
    }
    const float ts = (float)(t_ms < 0 ? 0 : t_ms) / 1000.0f;
    // Like talking: four or five syllables a second (|sin| at 2.3 Hz) louder and softer, under a slower swell; each
    // bar random at that loudness, a little like its neighbours and smoothed, a level meter rather than snow.
    const float syllable = fabsf(sinf(2 * PI_F * 2.3f * ts + a->phase1));
    const float envelope = 0.2f + 0.8f * syllable * (0.65f + 0.35f * sinf(2 * PI_F * 0.8f * ts + a->phase2));
    float r[YK_VC_BARS];
    for (int k = 0; k < YK_VC_BARS; k++) r[k] = rand01(a);
    const float span = len_max() - YK_VC_LEN0;
    const int settle_from = YK_VC_ANIM_MS - YK_VC_SETTLE_MS;
    float ease = 0;
    if (t_ms > settle_from) {
        const float u = (float)(t_ms - settle_from) / (float)YK_VC_SETTLE_MS;
        ease = u * u * (3 - 2 * u);
    }
    for (int k = 0; k < YK_VC_BARS; k++) {
        const float s = 0.5f * r[k] + 0.25f * (r[(k + YK_VC_BARS - 1) % YK_VC_BARS] + r[(k + 1) % YK_VC_BARS]);
        const float goal = envelope * (0.3f + 0.7f * s);
        a->drive[k] += (goal - a->drive[k]) * 0.55f;
        const float talking = YK_VC_LEN0 + span * clamp01(a->drive[k]);
        a->lengths[k] = talking + (yk_vc_bar_length(a->target[k]) - talking) * ease;
    }
    return false;
}

int yk_vc_next_frame_ms(int since_ms) {
    const int left = YK_VC_FRAME_MS - since_ms;
    return left > YK_VC_YIELD_MS ? left : YK_VC_YIELD_MS;
}

// ---------------------------------------------------------------- the app's own code

uint32_t yk_vc_app_number(void) { return YK_VC_APP_NUMBER; }

int yk_vc_app_code(uint32_t action, uint32_t value, yk_vc_payload_t *payload, uint8_t levels[YK_VC_BARS]) {
    const uint32_t number = yk_vc_app_number();
    if (!number) return YK_VC_NO_NUMBER;
    if (action > YK_VC_ACTION_OWN_TO || value >= (1u << 24)) return -1;
    yk_vc_payload_t p = {.kind = YK_VC_APP, .app = number, .action = action, .value = value};
    if (yk_vc_encode(&p, levels) != 0) return -1;
    *payload = p;
    return 0;
}
