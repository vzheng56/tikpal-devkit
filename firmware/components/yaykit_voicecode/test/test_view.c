// The round screen's side of 声波码 (yk_vc_view_logic.c), on the host:
//  - the app's number as built in (EXPECT_NUMBER: test/run.sh builds this with none, a number of the contract's
//    examples and a test number), and the app code it shows;
//  - the code as drawn, whole pixels and all, reads back as the contract's app examples, character for character
//    (fixtures/voice-code.json, with cJSON: HAVE_CJSON);
//  - the bars' last frame is the code exactly, and nothing changes after it;
//  - every frame stays inside the safe circle (radius 200) and inside the 24 boxes a frame redraws;
//  - the screen as drawn (ring, dot, bars, the Tikpal mark of 0.40R: the 伴环 ring and dot from Yay 0.1.14)
//    photographed: yk_vc_scan reads it, and a frame of the talking never reads as another code;
//  - the pairing page (handoff 2.1③): the contract's pair examples drawn give back their levels, its 8 digits in the
//    middle stay inside the logo's circle, and the page photographed with the digits in place of the mark reads back
//    as its pairing code (suffix and digits), never as another while the bars talk.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yk_mark_fit.h"
#include "yk_round.h"
#include "yk_vc_view_logic.h"
#include "yk_voicecode.h"
#ifdef HAVE_CJSON
#include "cJSON.h"
#endif

#ifndef EXPECT_NUMBER
#define EXPECT_NUMBER 0
#endif

static int failures, checks;
#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                    \
    } while (0)

static uint32_t rng_state = 0x2545f491u;
static uint32_t rnd(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return rng_state = x;
}

static void random_code(uint8_t levels[YK_VC_BARS]) {
    yk_vc_payload_t p = {.kind = YK_VC_TEST, .value = ((uint64_t)rnd() << 32 | rnd()) & ((1ULL << 44) - 1)};
    if (yk_vc_encode(&p, levels) != 0) abort();
}

static void levels_text(const uint8_t levels[YK_VC_BARS], char out[YK_VC_BARS + 1]) {
    for (int k = 0; k < YK_VC_BARS; k++) out[k] = (char)('0' + levels[k]);
    out[YK_VC_BARS] = 0;
}

/** Runs the talking to its end at 33 ms frames: the levels the drawn pixels give back (each bar's length between
 *  its two drawn cap centres, over r). */
static void drawn_levels(const uint8_t target[YK_VC_BARS], int r, uint32_t seed, char out[YK_VC_BARS + 1]) {
    yk_vc_anim_t a;
    yk_vc_anim_init(&a);
    yk_vc_anim_start(&a, target, seed);
    for (int t = 0; !yk_vc_anim_step(&a, t); t += YK_VC_FRAME_MS) {}
    yk_vc_px_t px;
    yk_vc_px(r, &px);
    for (int k = 0; k < YK_VC_BARS; k++) {
        int16_t e[4];
        yk_vc_bar_px(&px, k, a.lengths[k], e);
        const float len = hypotf((float)(e[2] - e[0]), (float)(e[3] - e[1])) / (float)r;
        out[k] = (char)('0' + yk_vc_nearest_level(len));
    }
    out[YK_VC_BARS] = 0;
}

// ---------------------------------------------------------------- the number built in

static void test_number(void) {
    CHECK(yk_vc_app_number() == EXPECT_NUMBER);
    yk_vc_payload_t p;
    uint8_t levels[YK_VC_BARS];
    if (EXPECT_NUMBER == 0) {
        CHECK(yk_vc_app_code(0, 0, &p, levels) == YK_VC_NO_NUMBER);
        CHECK(yk_vc_app_code(2, 20756, &p, levels) == YK_VC_NO_NUMBER);
        return;
    }
    for (uint32_t action = 0; action <= YK_VC_ACTION_OWN_TO; action++) {
        const uint32_t value = action == YK_VC_ACTION_DAY ? 20756u : action * 1000u;
        memset(&p, 0, sizeof(p));
        CHECK(yk_vc_app_code(action, value, &p, levels) == 0);
        CHECK(p.kind == YK_VC_APP && p.app == EXPECT_NUMBER && p.action == action && p.value == value);
        int8_t read[YK_VC_BARS];
        for (int k = 0; k < YK_VC_BARS; k++) read[k] = (int8_t)levels[k];
        yk_vc_payload_t back;
        CHECK(yk_vc_decode_levels(read, &back, NULL) == 0);
        CHECK(back.kind == YK_VC_APP && back.app == EXPECT_NUMBER && back.action == action && back.value == value);
    }
    CHECK(yk_vc_app_code(15, (1u << 24) - 1, &p, levels) == 0);
    CHECK(yk_vc_app_code(16, 0, &p, levels) == -1);
    CHECK(yk_vc_app_code(0, 1u << 24, &p, levels) == -1);
    printf("number %u: built in, all 16 actions\n", (unsigned)EXPECT_NUMBER);
}

// ---------------------------------------------------------------- the contract's app examples

#ifdef HAVE_CJSON
static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = malloc((size_t)n + 1);
    if (s && fread(s, 1, (size_t)n, f) != (size_t)n) {
        free(s);
        s = NULL;
    }
    if (s) s[n] = 0;
    fclose(f);
    return s;
}

static double num(const cJSON *o, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : NAN;
}

static void test_fixtures(const char *path) {
    char *text = read_file(path);
    CHECK(text != NULL);
    if (!text) return;
    cJSON *root = cJSON_Parse(text);
    CHECK(root != NULL);
    const cJSON *cases = cJSON_GetObjectItemCaseSensitive(root, "encode"), *c;
    int apps = 0, mine = 0, pairs = 0;
    cJSON_ArrayForEach(c, cases) {
        const cJSON *j = cJSON_GetObjectItemCaseSensitive(c, "payload");
        const cJSON *kind = cJSON_GetObjectItemCaseSensitive(j, "kind");
        const cJSON *want = cJSON_GetObjectItemCaseSensitive(c, "levels");
        if (cJSON_IsString(kind) && !strcmp(kind->valuestring, "pair") && cJSON_IsString(want)) {
            // The pairing page's code, from the text the device has (yk_vc_pair_layer's way), drawn at its radius.
            const cJSON *suffix = cJSON_GetObjectItemCaseSensitive(j, "suffix");
            const cJSON *code = cJSON_GetObjectItemCaseSensitive(j, "code");
            yk_vc_payload_t p;
            uint8_t levels[YK_VC_BARS];
            char got[YK_VC_BARS + 1];
            pairs++;
            CHECK(cJSON_IsString(suffix) && cJSON_IsString(code));
            if (!cJSON_IsString(suffix) || !cJSON_IsString(code)) continue;
            CHECK(yk_vc_pair(suffix->valuestring, code->valuestring, &p) == 0 && yk_vc_encode(&p, levels) == 0);
            levels_text(levels, got);
            CHECK(!strcmp(got, want->valuestring));
            drawn_levels(levels, YK_VC_VIEW_R, 0x77u + (uint32_t)pairs, got);
            CHECK(!strcmp(got, want->valuestring));
            continue;
        }
        if (!cJSON_IsString(kind) || strcmp(kind->valuestring, "app") || !cJSON_IsString(want)) continue;
        apps++;
        const yk_vc_payload_t p = {.kind = YK_VC_APP, .app = (uint32_t)num(j, "app"),
                                   .action = (uint32_t)num(j, "action"), .value = (uint64_t)num(j, "value")};
        uint8_t levels[YK_VC_BARS];
        char got[YK_VC_BARS + 1];
        CHECK(yk_vc_encode(&p, levels) == 0);
        levels_text(levels, got);
        CHECK(!strcmp(got, want->valuestring));
        // Drawn at the screen's radius and at others: the pixels give back the very levels.
        const int radii[] = {YK_VC_VIEW_R, 120, 150, 198};
        for (size_t i = 0; i < sizeof(radii) / sizeof(radii[0]); i++) {
            drawn_levels(levels, radii[i], 0x1234u + (uint32_t)i, got);
            CHECK(!strcmp(got, want->valuestring));
            if (strcmp(got, want->valuestring)) printf("  r=%d drew %s, want %s\n", radii[i], got, want->valuestring);
        }
        // The app whose number is built in shows exactly that code through yk_vc_app_code (yk_vc_show's way).
        if (p.app == EXPECT_NUMBER) {
            mine++;
            yk_vc_payload_t shown;
            CHECK(yk_vc_app_code(p.action, (uint32_t)p.value, &shown, levels) == 0);
            levels_text(levels, got);
            CHECK(!strcmp(got, want->valuestring));
        }
    }
    CHECK(apps >= 5);
    CHECK(pairs >= 2);
    if (EXPECT_NUMBER == 300 || EXPECT_NUMBER == 65535) CHECK(mine == 1);
    printf("contract: %d app examples drawn, levels identical (%d through the built-in number); %d pair examples drawn "
           "at r=%d, levels identical\n", apps, mine, pairs, YK_VC_VIEW_R);
    cJSON_Delete(root);
    free(text);
}
#endif

// ---------------------------------------------------------------- the last frame

static void check_settles(yk_vc_anim_t *a, const uint8_t target[YK_VC_BARS], int schedule) {
    int t = 0, settled = 0;
    for (int frames = 0; frames < 10000; frames++) {
        if (yk_vc_anim_step(a, t)) settled++;
        if (!a->running) break;
        switch (schedule) {
        case 0: t += YK_VC_FRAME_MS; break;
        case 1: t += 18 + (int)(rnd() % 31); break;  // a busy LVGL task: 18..48 ms
        case 2: t += 1; break;
        case 3: t += 500; break;
        case 4: {  // as yk_vc_view paces it: a frame drawn in 20..90 ms, the next once it is on the screen
            const int draw = 20 + (int)(rnd() % 71);
            t += draw + yk_vc_next_frame_ms(draw);
            break;
        }
        default: t = 5000; break;  // the first frame comes late
        }
    }
    CHECK(settled == 1);
    CHECK(!a->running);
    for (int k = 0; k < YK_VC_BARS; k++) CHECK(a->lengths[k] == yk_vc_bar_length(target[k]));
    float before[YK_VC_BARS];
    memcpy(before, a->lengths, sizeof(before));
    CHECK(!yk_vc_anim_step(a, t + 100));
    CHECK(!yk_vc_anim_step(a, 0));
    CHECK(!memcmp(before, a->lengths, sizeof(before)));
}

static void test_last_frame(void) {
    for (int i = 0; i < 40; i++) {
        uint8_t target[YK_VC_BARS], next[YK_VC_BARS];
        random_code(target);
        random_code(next);
        yk_vc_anim_t a;
        yk_vc_anim_init(&a);
        yk_vc_anim_start(&a, target, rnd() | 1);
        check_settles(&a, target, i % 6);
        // Another code in its place: talks on from the last and settles on the new one.
        yk_vc_anim_start(&a, next, rnd() | 1);
        check_settles(&a, next, (i + 2) % 6);
    }
    printf("last frame: the code exactly, 80 runs, 6 frame schedules\n");
}

/** The pacing (yk_vc_view): YK_VC_FRAME_MS from a frame's start, YK_VC_YIELD_MS after it is drawn at least, so that
 *  LVGL sleeps (its lock free) between frames however long one takes to draw. */
static void test_pacing(void) {
    CHECK(yk_vc_next_frame_ms(0) == YK_VC_FRAME_MS);
    CHECK(yk_vc_next_frame_ms(YK_VC_FRAME_MS - YK_VC_YIELD_MS) == YK_VC_YIELD_MS);
    CHECK(yk_vc_next_frame_ms(YK_VC_FRAME_MS) == YK_VC_YIELD_MS);
    CHECK(yk_vc_next_frame_ms(500) == YK_VC_YIELD_MS);
    for (int draw = 0; draw <= 200; draw++) {
        const int gap = yk_vc_next_frame_ms(draw);
        CHECK(gap >= YK_VC_YIELD_MS);                                         // asleep this long, every frame
        CHECK(draw + gap >= YK_VC_FRAME_MS);                                  // never faster than before
        CHECK(draw >= YK_VC_FRAME_MS - YK_VC_YIELD_MS || draw + gap == YK_VC_FRAME_MS);  // as before when quick
    }
    CHECK(YK_VC_WAIT_MS > YK_VC_FRAME_MS && YK_VC_WAIT_MS < 1000);
    printf("pacing: a frame every %d ms at most, %d ms asleep after each at least\n", YK_VC_FRAME_MS, YK_VC_YIELD_MS);
}

// ---------------------------------------------------------------- the safe circle and the boxes

static float dist(float x, float y) { return sqrtf(x * x + y * y); }

static void test_geometry(void) {
    yk_vc_px_t px;
    yk_vc_px(YK_VC_VIEW_R, &px);
    CHECK(px.reach <= YK_R_CONTENT);
    CHECK(px.ring_w >= 1 && px.bar_w >= 1);
    CHECK(px.dot_y + px.dot_r <= px.reach);
    // The logo: at most 0.40R across (BOSS-2, 11-01), clear of the bars' inner caps. The 伴环 mark's dot reaches past
    // the ring's circle (0.64 of its size): inside the contract's YK_VC_LOGO_MAX_R and clear of the bars all the same.
    CHECK(px.logo <= YK_VC_LOGO_D * px.r);
    CHECK(px.logo > 0);
    const float bars_in = (YK_VC_BASE_R - 0.5f * yk_vc_bar_length(3)) * px.r - px.bar_w / 2.0f - 1;
    CHECK(px.logo / 2.0f < bars_in);
    CHECK(yk_mark_reach(px.logo) < bars_in && yk_mark_reach(px.logo) <= YK_VC_LOGO_MAX_R * px.r);
    // The pairing page's 8 digits in the mark's place (yk_vc_view_set_middle): inside the logo's circle, so clear of
    // the bars; the label as wide as yk_vc_view.c makes it holds them.
    const float in = YK_VC_LOGO_MAX_R * px.r;
    CHECK(in < bars_in);
    CHECK(yk_vc_middle_fits(px.r, YK_VC_DIGITS_W, YK_VC_DIGITS_H));
    CHECK(!yk_vc_middle_fits(px.r, 2 * (int)ceilf(in) + 2, YK_VC_DIGITS_H));
    CHECK(2 * (int)sqrtf(in * in - YK_VC_DIGITS_H * YK_VC_DIGITS_H / 4.0f) >= YK_VC_DIGITS_W);
    printf("layout at r=%d: ring %d px, bars %d px, dot %d at %d, logo %d across, reach %d (safe circle %d)\n", px.r,
           px.ring_w, px.bar_w, px.dot_r, px.dot_y, px.logo, px.reach, YK_R_CONTENT);

    int16_t boxes[YK_VC_BOXES][4];
    for (int j = 0; j < YK_VC_BOXES; j++) {
        yk_vc_box_px(&px, j, 1, boxes[j]);
        // Each box stays out of the logo and the ring, and inside the safe circle.
        const int16_t *b = boxes[j];
        const float nx = fmaxf(0, fmaxf((float)b[0], (float)-b[2])), ny = fmaxf(0, fmaxf((float)b[1], (float)-b[3]));
        const float fx = fmaxf(fabsf((float)b[0]), fabsf((float)b[2])), fy = fmaxf(fabsf((float)b[1]), fabsf((float)b[3]));
        CHECK(dist(nx, ny) > yk_mark_reach(px.logo));
        CHECK(dist(fx, fy) < px.r - px.ring_w / 2.0f || dist(nx, ny) > px.r);  // the corners may pass the ring's arc
        CHECK(fmaxf(fx, fy) <= YK_R_CONTENT);
    }
    // Every frame of the talking: each bar (caps and antialiasing included) inside the safe circle and its box, at
    // a length between level 0's and level 3's.
    float worst = 0;
    int frames = 0;
    const float cap = px.bar_w / 2.0f + 1.0f;
    for (int run = 0; run < 30; run++) {
        uint8_t target[YK_VC_BARS];
        random_code(target);
        yk_vc_anim_t a;
        yk_vc_anim_init(&a);
        yk_vc_anim_start(&a, target, rnd() | 1);
        for (int t = 0;; t += 1 + (int)(rnd() % 40)) {
            const bool done = yk_vc_anim_step(&a, t);
            frames++;
            for (int k = 0; k < YK_VC_BARS; k++) {
                CHECK(a.lengths[k] >= YK_VC_LEN0 - 1e-5f && a.lengths[k] <= yk_vc_bar_length(3) + 1e-5f);
                int16_t e[4];
                yk_vc_bar_px(&px, k, a.lengths[k], e);
                const float far = fmaxf(dist(e[0], e[1]), dist(e[2], e[3])) + cap;
                worst = fmaxf(worst, far);
                CHECK(far <= YK_R_CONTENT);
                const int16_t *b = boxes[k / 2];
                for (int i = 0; i < 4; i += 2) {
                    CHECK(e[i] - cap >= b[0] && e[i] + cap <= b[2]);
                    CHECK(e[i + 1] - cap >= b[1] && e[i + 1] + cap <= b[3]);
                }
            }
            if (done) break;
        }
    }
    printf("safe circle: %d frames, farthest bar pixel %.1f px from the centre (limit %d), all inside their boxes\n",
           frames, worst, YK_R_CONTENT);
}

// ---------------------------------------------------------------- the drawn screen, read by the camera reader

#define W 1280
#define H 720
#define SCALE 1.25f  // camera pixels per screen pixel: the ring's radius is 230 px in the frame
#define SS 3         // samples a pixel, each way

typedef struct {
    yk_vc_px_t px;
    int16_t ends[YK_VC_BARS][4];
    uint8_t colour[YK_VC_BARS];
    const char *middle;  // the pairing page's digits in the mark's place ("0641 8273"); NULL: the mark
} drawn_t;

/** What the camera sees of a colour: luma, the theme's ink at 0.95 of white (test_voicecode.c's scale). */
static float seen(uint32_t rgb) {
    const float l = 0.299f * (rgb >> 16) + 0.587f * ((rgb >> 8) & 255) + 0.114f * (rgb & 255);
    const float ink = 0.299f * (YK_INK >> 16) + 0.587f * ((YK_INK >> 8) & 255) + 0.114f * (YK_INK & 255);
    return 0.95f * 255.0f * l / ink;
}

static float seg_dist(float x, float y, float x0, float y0, float x1, float y1) {
    const float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? ((x - x0) * dx + (y - y0) * dy) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return dist(x - x0 - t * dx, y - y0 - t * dy);
}

/** The 伴环 mark of `size` (yk_mark.c) at (x, y) from its centre: the coral ring from 5 to 264 degrees with round ends,
 *  the sage dot; 0 where it is not. */
static uint32_t mark_at(int size, float x, float y) {
    const float d = yk_mark_px(2 * YK_MARK_DOT_R, size) / 2.0f;
    if (dist(x - yk_mark_px(YK_MARK_DOT_X, size), y - yk_mark_px(YK_MARK_DOT_Y, size)) <= d) return YK_SAGE;
    const float out = size / 2.0f, w = (float)yk_mark_px(YK_MARK_STROKE, size), mid = out - w / 2, r = dist(x, y);
    float deg = atan2f(y, x) * 180.0f / 3.14159265f;  // 0 at 3 o'clock, clockwise (y down)
    if (deg < 0) deg += 360;
    if (r <= out && r >= out - w && deg >= YK_MARK_ARC_START && deg <= YK_MARK_ARC_END) return YK_CORAL;
    const int ends[2] = {YK_MARK_ARC_START, YK_MARK_ARC_END};
    for (int i = 0; i < 2; i++) {
        const float a = ends[i] * 3.14159265f / 180.0f;
        if (dist(x - mid * cosf(a), y - mid * sinf(a)) <= w / 2) return YK_CORAL;
    }
    return 0;
}

/** The middle's digits as the round screen's font draws them, near enough for the reader: seven strokes 3 px wide in
 *  a digit 13 x YK_VC_DIGITS_H px, a digit every 16 px and the space 6 (YK_VC_DIGITS_W across), centred. */
static bool digit_at(const char *text, float x, float y) {
    static const uint8_t strokes[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};  // gfedcba
    const float w = 13, h = YK_VC_DIGITS_H, s = 3;
    float left = -YK_VC_DIGITS_W / 2.0f;
    for (const char *c = text; *c; c++) {
        if (*c < '0' || *c > '9') {
            left += 6;
            continue;
        }
        const float lx = x - (left + 1.5f), ly = y + h / 2;
        left += 16;
        if (lx < 0 || lx > w || ly < 0 || ly > h) continue;
        const uint8_t m = strokes[*c - '0'];
        const bool l = lx <= s, r = lx >= w - s, up = ly <= h / 2;
        if (((m & 1) && ly <= s) || ((m & 2) && r && up) || ((m & 4) && r && !up) || ((m & 8) && ly >= h - s) ||
            ((m & 16) && l && !up) || ((m & 32) && l && up) || ((m & 64) && fabsf(ly - h / 2) <= s / 2))
            return true;
    }
    return false;
}

/** The screen at (x, y) from the code's centre, as yk_vc_view.c draws it (the mark: yk_mark.c's ring and dot; or the
 *  pairing page's digits in its place). */
static float shade(const drawn_t *d, float x, float y) {
    const float r = dist(x, y);
    if (r > YK_R_EDGE) return 14;  // the bezel
    const yk_vc_px_t *px = &d->px;
    const float outer = (float)lroundf(px->r + px->ring_w / 2.0f);
    if (r <= outer && r >= outer - px->ring_w) return seen(YK_INK);
    if (dist(x, y + px->dot_y) <= px->dot_r) return seen(YK_CORAL);
    if (d->middle) {
        if (r <= YK_VC_LOGO_MAX_R * px->r && digit_at(d->middle, x, y)) return seen(YK_INK);
    } else {
        const uint32_t mark = r <= yk_mark_reach(px->logo) + 1 ? mark_at(px->logo, x, y) : 0;
        if (mark) return seen(mark);
    }
    if (r > 0.42f * px->r && r < 0.86f * px->r) {
        const float f = (atan2f(y, x) + 3.14159265f / 2) / (2 * 3.14159265f / YK_VC_BARS) - 0.5f;
        const int k0 = (int)floorf(f + 0.5f);
        for (int dk = -1; dk <= 1; dk++) {
            const int k = ((k0 + dk) % YK_VC_BARS + YK_VC_BARS) % YK_VC_BARS;
            const int16_t *e = d->ends[k];
            if (seg_dist(x, y, e[0], e[1], e[2], e[3]) <= px->bar_w / 2.0f)
                return seen(d->colour[k] == YK_VC_LEVELS - 1 ? YK_CORAL : YK_INK);
        }
    }
    return 0;  // pure black
}

static void photograph(const drawn_t *d, uint8_t *luma) {
    for (int Y = 0; Y < H; Y++)
        for (int X = 0; X < W; X++) {
            float sum = 0;
            for (int i = 0; i < SS; i++)
                for (int j = 0; j < SS; j++)
                    sum += shade(d, (X + (i + 0.5f) / SS - W / 2.0f) / SCALE, (Y + (j + 0.5f) / SS - H / 2.0f) / SCALE);
            luma[Y * W + X] = (uint8_t)lroundf(sum / (SS * SS));
        }
}

static void draw(drawn_t *d, const yk_vc_anim_t *a) {
    yk_vc_px(YK_VC_VIEW_R, &d->px);
    for (int k = 0; k < YK_VC_BARS; k++) {
        yk_vc_bar_px(&d->px, k, a->lengths[k], d->ends[k]);
        d->colour[k] = (uint8_t)yk_vc_nearest_level(a->lengths[k]);
    }
}

static bool same(const yk_vc_payload_t *a, const yk_vc_payload_t *b) {
    return a->kind == b->kind && a->app == b->app && a->action == b->action && a->value == b->value &&
           a->suffix == b->suffix && a->digits == b->digits;
}

static void test_read_back(void) {
    uint8_t *luma = malloc((size_t)W * H);
    drawn_t *d = malloc(sizeof(*d));
    if (!luma || !d) abort();
    yk_vc_payload_t codes[8] = {
        {.kind = YK_VC_APP, .app = 3, .action = 0, .value = 0},  // 录音卡's page
        {.kind = YK_VC_APP, .app = 65280, .action = 0, .value = 0},
        {.kind = YK_VC_APP, .app = 300, .action = 2, .value = 20756},
        {.kind = YK_VC_APP, .app = 65535, .action = 15, .value = 16777215},
    };
    const char *middles[8] = {0};
    // The pairing page (yk_vc_pair_layer): the code from the text the device has, its 8 digits in the middle.
    static const char *const pairs[][2] = {{"dd76", "06418273"}, {"00a1", "00000042"}, {"ffff", "99999999"},
                                           {"0000", "00000000"}};
    static char digits[4][10];
    int n = 4;
    for (int i = 0; i < 4; i++, n++) {
        CHECK(yk_vc_pair(pairs[i][0], pairs[i][1], &codes[n]) == 0);
        snprintf(digits[i], sizeof(digits[i]), "%.4s %.4s", pairs[i][1], pairs[i][1] + 4);
        middles[n] = digits[i];
    }
    int read = 0, talking = 0, talking_read = 0;
    for (int i = 0; i < n; i++) {
        uint8_t levels[YK_VC_BARS];
        CHECK(yk_vc_encode(&codes[i], levels) == 0);
        d->middle = middles[i];
        yk_vc_anim_t a;
        yk_vc_anim_init(&a);
        yk_vc_anim_start(&a, levels, 0x51u + (uint32_t)i);
        // A few frames of the talking: never another code.
        for (int t = 0; !yk_vc_anim_step(&a, t); t += 7 * YK_VC_FRAME_MS) {
            draw(d, &a);
            photograph(d, luma);
            yk_vc_result_t r;
            talking++;
            if (yk_vc_scan(luma, W, H, W, &r) == YK_VC_OK) {
                talking_read++;
                CHECK(same(&r.payload, &codes[i]));
            }
        }
        draw(d, &a);
        photograph(d, luma);
        yk_vc_result_t r;
        const yk_vc_status_t st = yk_vc_scan(luma, W, H, W, &r);
        CHECK(st == YK_VC_OK);
        if (st == YK_VC_OK) {
            read++;
            CHECK(same(&r.payload, &codes[i]));
        } else {
            char text[48];
            yk_vc_payload_text(&codes[i], text, sizeof(text));
            printf("  %s not read: %s (bars %d, erased %d)\n", text, yk_vc_status_name(st), r.bars, r.erased);
        }
    }
    printf("read back: %d of %d settled codes read as drawn (4 with the mark, %d px, in the middle; 4 pairing pages "
           "with their digits there); %d talking frames, %d read, none as another code\n", read, n, d->px.logo,
           talking, talking_read);
    free(luma);
    free(d);
}

int main(int argc, char **argv) {
    test_number();
#ifdef HAVE_CJSON
    if (argc < 2) {
        printf("usage: test_view <voice-code.json>\n");
        return 2;
    }
    test_fixtures(argv[1]);
#else
    (void)argc, (void)argv;
#endif
    test_last_frame();
    test_pacing();
    test_geometry();
    test_read_back();
    printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "ok", checks, failures);
    return failures ? 1 : 0;
}
