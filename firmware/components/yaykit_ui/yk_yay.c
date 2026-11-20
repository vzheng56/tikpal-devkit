// Yay in an app (yk_yay.h). The art is yaykit/firmware/assets/yay: I8 images with a 256-colour palette (alpha not
// premultiplied) and the face's boxes. Each pose is blended once over the paper it sits on into opaque RGB565 in
// PSRAM (drawing it is then a copy; the main firmware measured ~40 ms of a 45 ms frame for the same pose with
// alpha), or for YK_YAY_TRANSPARENT kept with its alpha (RGB565A8), and kept in a small cache. Blinking, looking and the mouth are drawn over the art: the drawn eye or mouth is
// covered with the skin colour (grown by YAY_ART_COVER_PAD) and a new one drawn in YAY_ART_INK.
//
// The cache is every Yay's together, and LVGL draws a picture straight from its slot, at the size it read when the
// image's source was set. So a slot some Yay's image shows is never made again or freed under it, however long ago it
// was asked for: the home screen asks for nothing while the badge, over it, goes through its poses (Yay 0.1.20, 11-17:
// home's slot went to a black 176 one and home drew it as 264, a row and a half a line, into noise). With every slot on
// a screen the cache takes one more (CACHE_MAX), and goes back to CACHE as soon as some are not.
//
// A figure (yk_yay_figure, a buddy's pack) is the other way: its pictures come out of the pack through the same cache, and
// the eyes and the mouth are small blocks of the picture's own palette, pasted at (x, y). Colours are baked in, so there
// is no skin and no ink here. Without one nothing below changes: `fg` stays NULL and Pip goes the way he always went.
//
// YAY_ART_PACKED (CONFIG_YK_UI_PIP_BUILTIN off, Yay from 0.1.18): the image has Pip's greet_132 only, and every pose is
// drawn as it until Pip's pack is hung (yk_yay_pip); then Pip's pictures come from the pack, drawn the way they always
// were. Built with Pip's pictures (every other firmware), none of that is compiled.
//
// YK_YAY_BUDDY_DEFAULT (CONFIG_YK_UI_BUDDY_DEFAULT, on but in Yay; development kit 1.10): every Yay made is the user's
// buddy (yk_yay_buddy), its pack from yk_yay_buddy.c. Yay hangs its packs itself and turns it off.
#include "yk_yay.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#endif
#include "src/misc/cache/instance/lv_image_cache.h"  // lv_image_cache_drop: not in LVGL 9.6's public headers
#include "yay_art.h"
#include "yk_figure.h"
#include "yk_round.h"
#include "yk_ui_logic.h"

static const char *TAG = "yk_yay";

#define CACHE 8       // pictures kept: the one least lately asked for that no Yay shows goes for a new one
#define CACHE_MAX 16  // over CACHE only while every one kept is on some Yay's screen
#define BLINK_MS 130
#define LOOK_PX 3  // how far the eyes move to look somewhere (at 176 px)

static struct {
    const void *key;  // what it was made from: Pip's yay_art_t, or a figure's picture
    uint32_t paper;
    lv_image_dsc_t dsc;
    uint32_t used;  // lv_tick of the last time it was asked for (the oldest not on a screen goes)
} s_cache[CACHE_MAX];

#define PATCHES 3  // the left eye, the right eye, the mouth
enum { PATCH_LEFT, PATCH_RIGHT, PATCH_MOUTH };

/** A Yay's figure, in PSRAM (one allocation, made the first time one is hung on it; none for any other Yay). */
typedef struct {
    bool on;                      // drawing from `f`; else Pip
    yk_figure_t f;
    yk_figure_picture_t pic;      // the picture drawn
    uint16_t rgb[256];            // its palette over the paper, for the patches
    uint8_t alpha[256];
    lv_obj_t *patch[PATCHES];
    lv_image_dsc_t dsc[PATCHES];
    uint16_t *px[PATCHES];        // the pasted block as RGB565
    uint32_t cap[PATCHES];        // pixels px has room for
    const uint8_t *shown[PATCHES];  // the patch it shows now; NULL hidden
} figure_t;

struct yk_yay {
    lv_obj_t *box;          // size x size, centred on (cx, cy)
    lv_obj_t *img[2];       // the pose and, while crossfading, the one before
    int cur;
    lv_obj_t *eye_cover[2], *eye[2], *mouth_cover, *mouth;
    yk_yay_pose_t pose;
    const yay_art_t *art;   // the art drawn (maybe another size, scaled)
    int size, cx, cy;
    uint32_t paper;
    float scale;            // size / art size
    float img_scale;        // what the image drawn is scaled by: `scale`, or 1 for a pre-scaled copy (yk_yay_prescale)
    float rot;              // degrees
    float sqx, sqy;         // squashed: width and height times these about the middle (1, 1: not)
    bool auto_blink, blinking, breathing;
    lv_timer_t *timer;
    uint32_t blink_seed, next_blink;
    int look_dx, look_dy;   // eye offset in art pixels; 0, 0 = ahead
    yk_yay_mouth_t mouth_set;  // fixed shape, or POSE
    yk_yay_mouth_t mouth_level;
    bool mouth_from_level;
    figure_t *fg;           // NULL until a figure is hung on it
#ifdef YAY_ART_PACKED
    uint32_t pip_gen;       // the s_pip_gen its art was picked with
#endif
    yk_yay_t *next;         // in s_yays
};

/** The figure this Yay draws now, NULL for Pip. */
static figure_t *figure_of(const yk_yay_t *y) { return y->fg && y->fg->on ? y->fg : NULL; }

/** Every Yay there is. The cache is theirs together: the home screen and the app ring's middle hang the same pack
 *  (0.1.18), and the ring's going took the home screen's picture with it (its eyes went on blinking alone, 11-12). */
static yk_yay_t *s_yays;

/** Some Yay's image shows cache slot `i` now: not hidden, though its screen may be under another (home's under the
 *  badge's). Such a slot is not made again or freed. A hidden image is set up again (show()) before it is shown. */
static bool on_screen(int i) {
    const lv_image_dsc_t *d = &s_cache[i].dsc;
    for (const yk_yay_t *o = s_yays; o; o = o->next)
        for (int k = 0; k < 2; k++)
            if (!lv_obj_is_hidden(o->img[k]) && lv_image_get_src(o->img[k]) == (const void *)d) return true;
    return false;
}

/** Slot `i` was asked for longer ago than slot `j`. */
static bool older(int i, int j) { return (int32_t)(s_cache[i].used - s_cache[j].used) < 0; }

/** Slot `i` empty again: what LVGL kept of its picture, its memory, its key (which a later pack may land on). */
static void slot_free(int i) {
    lv_image_cache_drop(&s_cache[i].dsc);
    heap_caps_free((void *)s_cache[i].dsc.data);
    memset(&s_cache[i], 0, sizeof(s_cache[i]));
}

/** Back down to CACHE pictures when there are more: the oldest no Yay shows go, never `keep` (-1: none to keep). */
static void trim(int keep) {
    for (;;) {
        int n = 0, oldest = -1;
        for (int i = 0; i < CACHE_MAX; i++) {
            if (!s_cache[i].key) continue;
            n++;
            if (i != keep && !on_screen(i) && (oldest < 0 || older(i, oldest))) oldest = i;
        }
        if (n <= CACHE || oldest < 0) return;
        slot_free(oldest);
    }
}

/** Some Yay draws from the pack at `data` now, so what the cache made from it stays. */
static bool pack_held(const uint8_t *data) {
    for (const yk_yay_t *o = s_yays; o; o = o->next) {
        const figure_t *g = figure_of(o);
        if (g && g->f.data == data) return true;
    }
    return false;
}

#ifdef YAY_ART_PACKED
// Pip's pictures from Pip's pack (yk_yay_pip): per yay_art entry with no data in the image, the picture's data (its
// palette, then the indices) in the pack; NULL: not there. In PSRAM (internal RAM has the pointer only), NULL until a
// pack is hung. s_pip_gen counts the calls that hung one (one at most).
static const uint8_t **s_pip;
static uint32_t s_pip_gen;

/** Where an art entry's pixels are: the image's, or Pip's pack's; NULL: nowhere yet. */
static const uint8_t *art_pixels(const yay_art_t *a) {
    return a->image->data ? a->image->data : s_pip ? s_pip[a - yay_art] : NULL;
}
#else
static const uint8_t *art_pixels(const yay_art_t *a) { return a->image->data; }
#endif

static const yay_art_t *art_for(yk_yay_pose_t pose, int size) {
    const char *name = yk_yay_pose_name(pose);
    const yay_art_t *a = name ? yay_art_find(name, (uint16_t)size) : NULL;
    return a && art_pixels(a) ? a : NULL;
}

bool yk_yay_has(yk_yay_pose_t pose, int size) { return art_for(pose, size) != NULL; }

/** The pose blended over `paper` as RGB565, from the cache or made now: `w` × `h` of `idx` through the palette `pal`,
 *  kept under `key`. */
static const lv_image_dsc_t *decoded(const void *key, const uint8_t *pal, const uint8_t *idx, uint32_t w, uint32_t h,
                                     uint32_t paper) {
    int free_slot = -1, oldest = -1, n_kept = 0;
    for (int i = 0; i < CACHE_MAX; i++) {
        if (!s_cache[i].key) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (s_cache[i].key == key && s_cache[i].paper == paper) {
            s_cache[i].used = lv_tick_get();
            return &s_cache[i].dsc;
        }
        n_kept++;
        if (!on_screen(i) && (oldest < 0 || older(i, oldest))) oldest = i;
    }
    // Under CACHE an empty slot; else the oldest no Yay shows; else (every one on a screen) an empty one over CACHE.
    const int slot = n_kept < CACHE && free_slot >= 0 ? free_slot : oldest >= 0 ? oldest : free_slot;
    if (slot < 0) {
        ESP_LOGE(TAG, "no slot for a %ux%u pose: all %d on screens", (unsigned)w, (unsigned)h, CACHE_MAX);
        return NULL;
    }
    const bool clear = paper == YK_YAY_TRANSPARENT;
    const size_t n = (size_t)w * h, bytes = n * (clear ? 3 : 2);
    uint8_t *mem = (uint8_t *)s_cache[slot].dsc.data;
    if (s_cache[slot].key) lv_image_cache_drop(&s_cache[slot].dsc);  // another picture now: LVGL forgets the one before
    if (!s_cache[slot].key || s_cache[slot].dsc.data_size != bytes) {
        heap_caps_free(mem);
        mem = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
        if (!mem) {
            s_cache[slot].key = NULL;
            s_cache[slot].dsc.data = NULL;
            ESP_LOGE(TAG, "no PSRAM for a %ux%u pose", (unsigned)w, (unsigned)h);
            return NULL;
        }
    }
    // I8: 256 palette entries of B, G, R, A, then one index a pixel. Opaque: RGB565 over the paper. Transparent:
    // RGB565A8, the colour plane then the alpha plane (LVGL's software renderer draws it, turned and scaled too).
    uint16_t rgb[256];
    uint8_t alpha[256];
    yk_yay_palette(pal, paper, rgb, alpha);
    uint16_t *px = (uint16_t *)mem;
    for (size_t i = 0; i < n; i++) px[i] = rgb[idx[i]];
    if (clear)
        for (size_t i = 0; i < n; i++) mem[2 * n + i] = alpha[idx[i]];
    s_cache[slot].key = key;
    s_cache[slot].paper = paper;
    s_cache[slot].used = lv_tick_get();
    s_cache[slot].dsc = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = clear ? LV_COLOR_FORMAT_RGB565A8 : LV_COLOR_FORMAT_RGB565,
                   .w = w, .h = h, .stride = w * 2},
        .data_size = bytes,
        .data = mem,
    };
    if (n_kept >= CACHE) trim(slot);  // over: back as soon as some are off the screens
    return &s_cache[slot].dsc;
}

/** What was made from the pack at [from, to) goes: its memory, and its key, which a later pack may land on. */
static void cache_drop(const uint8_t *from, const uint8_t *to) {
    for (int i = 0; i < CACHE_MAX; i++) {
        const uint8_t *key = s_cache[i].key;
        if (key && key >= from && key < to) slot_free(i);
    }
}

static lv_obj_t *rect(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_hidden(o, true);
    return o;
}

/** An art box (image pixels) to the box object's coordinates: scaled, turned 180 when upside down. */
static void place(yk_yay_t *y, lv_obj_t *o, float x, float yy, float w, float h, int radius) {
    float k = y->scale, X = x * k, Y = yy * k, W = w * k, H = h * k;
    if (fabsf(y->rot - 180) < 0.5f) X = y->size - X - W, Y = y->size - Y - H;
    lv_obj_set_pos(o, (int32_t)lroundf(X), (int32_t)lroundf(Y));
    lv_obj_set_size(o, (int32_t)lroundf(W) > 0 ? (int32_t)lroundf(W) : 1, (int32_t)lroundf(H) > 0 ? (int32_t)lroundf(H) : 1);
    lv_obj_set_style_radius(o, (int32_t)lroundf(radius * k), 0);
}

/** Pastes `p` (NULL: nothing) as patch `i`. Nothing to do when it is there already, which is most ticks of the mouth. */
static void patch_show(yk_yay_t *y, int i, const yk_figure_patch_t *p) {
    figure_t *g = y->fg;
    lv_obj_t *o = g->patch[i];
    if (!p) {
        if (g->shown[i]) lv_obj_set_hidden(o, true);
        g->shown[i] = NULL;
        return;
    }
    if (g->shown[i] == p->indices) return;
    const uint32_t n = (uint32_t)p->w * p->h;
    if (n > g->cap[i]) {
        uint16_t *px = heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM);
        if (!px) {
            ESP_LOGE(TAG, "no PSRAM for a %ux%u patch", (unsigned)p->w, (unsigned)p->h);
            lv_obj_set_hidden(o, true);
            g->shown[i] = NULL;
            return;
        }
        heap_caps_free(g->px[i]);
        g->px[i] = px;
        g->cap[i] = n;
    }
    for (uint32_t k = 0; k < n; k++) g->px[i][k] = g->rgb[p->indices[k]];
    g->dsc[i] = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = p->w, .h = p->h, .stride = p->w * 2u},
        .data_size = n * 2,
        .data = (const uint8_t *)g->px[i],
    };
    lv_image_set_src(o, &g->dsc[i]);
    lv_obj_set_size(o, p->w, p->h);
    lv_obj_set_pos(o, (int32_t)lroundf(p->x * y->scale), (int32_t)lroundf(p->y * y->scale));
    lv_image_set_pivot(o, 0, 0);
    lv_image_set_scale(o, (uint32_t)lroundf(256 * y->scale));
    lv_obj_set_hidden(o, false);
    g->shown[i] = p->indices;
}

static bool squashed(const yk_yay_t *y) { return fabsf(y->sqx - 1) > 0.004f || fabsf(y->sqy - 1) > 0.004f; }

/** A figure's face: only what differs from the picture is pasted, and only upright. */
static void face_figure(yk_yay_t *y) {
    figure_t *g = y->fg;
    const bool can = fabsf(y->rot) < 0.5f && !squashed(y);
    yk_figure_patch_t p;
    for (int i = PATCH_LEFT; i <= PATCH_RIGHT; i++) {
        const bool shut = can && y->blinking && g->pic.eyes_open && yk_figure_patch(&g->f, &g->pic, YK_FIGURE_EYE, i, &p);
        patch_show(y, i, shut ? &p : NULL);
    }
    const yk_yay_mouth_t m = y->mouth_from_level ? y->mouth_level : y->mouth_set;
    const bool mouth = can && m != YK_YAY_MOUTH_POSE && yk_figure_patch(&g->f, &g->pic, YK_FIGURE_MOUTH, m, &p);
    patch_show(y, PATCH_MOUTH, mouth ? &p : NULL);
}

/** The face drawn over the art: eyes (blinking or looking) and the mouth. Only upright or upside down. */
static void face(yk_yay_t *y) {
    if (figure_of(y)) {
        face_figure(y);
        return;
    }
    const yay_art_t *a = y->art;
    const bool can = a && (fabsf(y->rot) < 0.5f || fabsf(y->rot - 180) < 0.5f) && !squashed(y);
    const bool eyes = can && a->eyes_open && (y->blinking || y->look_dx || y->look_dy);
    for (int i = 0; i < 2; i++) {
        if (!eyes) {
            lv_obj_set_hidden(y->eye_cover[i], true);
            lv_obj_set_hidden(y->eye[i], true);
            continue;
        }
        const yay_art_box_t *e = i ? &a->eye_right : &a->eye_left;
        const int p = YAY_ART_COVER_PAD;
        lv_obj_set_style_bg_color(y->eye_cover[i], lv_color_hex(a->skin), 0);
        place(y, y->eye_cover[i], e->x - p, e->y - p, e->w + 2 * p, e->h + 2 * p, a->eye_radius + p);
        lv_obj_set_hidden(y->eye_cover[i], false);
        lv_obj_set_style_bg_color(y->eye[i], lv_color_hex(YAY_ART_INK), 0);
        if (y->blinking) {
            const float h = e->h / 5.0f > 2 ? e->h / 5.0f : 2;  // a closed eye: a short thick line
            place(y, y->eye[i], e->x, e->y + e->h * 0.6f, e->w, h, (int)(h / 2));
        } else {
            place(y, y->eye[i], e->x + y->look_dx, e->y + y->look_dy, e->w, e->h, a->eye_radius);
        }
        lv_obj_set_hidden(y->eye[i], false);
    }
    const yk_yay_mouth_t m = y->mouth_from_level ? y->mouth_level : y->mouth_set;
    if (!can || m == YK_YAY_MOUTH_POSE) {
        lv_obj_set_hidden(y->mouth_cover, true);
        lv_obj_set_hidden(y->mouth, true);
        return;
    }
    const yay_art_box_t *mb = &a->mouth;
    const int p = YAY_ART_COVER_PAD;
    lv_obj_set_style_bg_color(y->mouth_cover, lv_color_hex(a->skin), 0);
    place(y, y->mouth_cover, mb->x - p, mb->y - p, mb->w + 2 * p, mb->h + 2 * p, p);
    lv_obj_set_hidden(y->mouth_cover, false);
    const yay_art_size_t sz = a->mouth_levels[m];
    const float mx = mb->x + mb->w / 2.0f - sz.w / 2.0f, my = mb->y + mb->h / 2.0f - sz.h / 2.0f;
    lv_obj_set_style_bg_color(y->mouth, lv_color_hex(YAY_ART_INK), 0);
    place(y, y->mouth, mx, my, sz.w, sz.h, sz.h / 2 + 1);
    lv_obj_set_hidden(y->mouth, false);
}

bool yk_yay_face(const yk_yay_t *y, yk_yay_face_t *out) {
    if (!y || !out || fabsf(y->rot) >= 0.5f) return false;
    const float k = y->scale, x0 = (float)(y->cx - y->size / 2), y0 = (float)(y->cy - y->size / 2);
    const figure_t *g = figure_of(y);
    float box[2][4];
    if (g) {
        if (!g->pic.eyes_open || !g->pic.pixels) return false;
        yk_figure_patch_t p;
        for (int i = 0; i < 2; i++) {
            if (!yk_figure_patch(&g->f, &g->pic, YK_FIGURE_EYE, i, &p)) return false;
            box[i][0] = p.x, box[i][1] = p.y, box[i][2] = p.w, box[i][3] = p.h;
        }
        // The skin: the picture's own colour just left of the left eye, halfway down it (over the paper, as drawn).
        int sx = (int)box[0][0] - 2, sy = (int)(box[0][1] + box[0][3] / 2);
        if (sx < 0) sx = 0;
        if (sy >= g->pic.size) sy = g->pic.size - 1;
        const uint16_t c = g->rgb[g->pic.pixels[(uint32_t)sy * g->pic.size + (uint32_t)sx]];
        out->skin = ((uint32_t)(c >> 11 & 0x1F) * 255 / 31) << 16 | ((uint32_t)(c >> 5 & 0x3F) * 255 / 63) << 8 |
                    (uint32_t)(c & 0x1F) * 255 / 31;
    } else {
        const yay_art_t *a = y->art;
        if (!a || !a->eyes_open) return false;
        for (int i = 0; i < 2; i++) {
            const yay_art_box_t *e = i ? &a->eye_right : &a->eye_left;
            box[i][0] = e->x, box[i][1] = e->y, box[i][2] = e->w, box[i][3] = e->h;
        }
        out->skin = a->skin;
    }
    for (int i = 0; i < 2; i++) {
        if (squashed(y)) {  // its middle and its size moved and scaled about the picture's middle
            const float mx = (box[i][0] + box[i][2] / 2) * k - y->size / 2.0f, my = (box[i][1] + box[i][3] / 2) * k - y->size / 2.0f;
            const float w = box[i][2] * k * y->sqx, h = box[i][3] * k * y->sqy;
            out->eye[i].x = (int16_t)lroundf(y->cx + mx * y->sqx - w / 2);
            out->eye[i].y = (int16_t)lroundf(y->cy + my * y->sqy - h / 2);
            out->eye[i].w = (int16_t)lroundf(w);
            out->eye[i].h = (int16_t)lroundf(h);
            continue;
        }
        out->eye[i].x = (int16_t)lroundf(x0 + box[i][0] * k);
        out->eye[i].y = (int16_t)lroundf(y0 + box[i][1] * k);
        out->eye[i].w = (int16_t)lroundf(box[i][2] * k);
        out->eye[i].h = (int16_t)lroundf(box[i][3] * k);
    }
    return true;
}

/** The next gap between blinks, in ms. */
static int blink_gap(yk_yay_t *y) {
    const figure_t *g = figure_of(y);
    return g ? yk_yay_blink_gap_between(&y->blink_seed, g->f.gap_min_ms, g->f.gap_max_ms)
             : yk_yay_blink_gap_ms(&y->blink_seed);
}

#ifdef YAY_ART_PACKED
static void pip_refresh(yk_yay_t *y);
#endif

static void tick(lv_timer_t *t) {
    yk_yay_t *y = lv_timer_get_user_data(t);
#ifdef YAY_ART_PACKED
    if (y->pip_gen != s_pip_gen) pip_refresh(y);  // Pip's pack was hung: his own pictures now
#endif
    const uint32_t now = lv_tick_get();
    const figure_t *g = figure_of(y);
    if (y->blinking && now - y->next_blink >= (g ? g->f.blink_ms : BLINK_MS)) {
        y->blinking = false;
        y->next_blink = now + (uint32_t)blink_gap(y);
        face(y);
    } else if (!y->blinking && y->auto_blink && (int32_t)(now - y->next_blink) >= 0) {
        yk_yay_blink(y);
    }
}

static void layout(yk_yay_t *y) {
    lv_obj_set_size(y->box, y->size, y->size);
    lv_obj_set_pos(y->box, y->cx - y->size / 2, y->cy - y->size / 2);
}

/** A Yay as Pip, nothing hung (yk_yay_create, yk_yay_create_kept). */
static yk_yay_t *yay_make(lv_obj_t *parent, int size, int cx, int cy, uint32_t paper) {
    yk_yay_t *y = calloc(1, sizeof(*y));
    if (!y) return NULL;
    y->next = s_yays;
    s_yays = y;
    y->size = size, y->cx = cx, y->cy = cy, y->paper = paper;
#ifdef YAY_ART_PACKED
    y->pip_gen = s_pip_gen;
#endif
    y->pose = YK_YAY_POSE_COUNT;
    y->sqx = y->sqy = 1;
    y->mouth_set = YK_YAY_MOUTH_POSE;
    y->mouth_level = YK_YAY_MOUTH_CLOSED;
    y->box = lv_obj_create(parent);
    lv_obj_remove_style_all(y->box);
    lv_obj_set_clickable(y->box, false);
    lv_obj_set_scrollable(y->box, false);
    layout(y);
    for (int i = 0; i < 2; i++) {
        y->img[i] = lv_image_create(y->box);
        lv_obj_set_hidden(y->img[i], true);
    }
    for (int i = 0; i < 2; i++) y->eye_cover[i] = rect(y->box), y->eye[i] = rect(y->box);
    y->mouth_cover = rect(y->box);
    y->mouth = rect(y->box);
    y->blink_seed = (uint32_t)(uintptr_t)y ^ lv_tick_get();
    y->auto_blink = true;
    y->next_blink = lv_tick_get() + (uint32_t)yk_yay_blink_gap_ms(&y->blink_seed);
    y->timer = lv_timer_create(tick, 50, y);
    yk_yay_pose(y, YK_YAY_READY, 0);
    return y;
}

yk_yay_t *yk_yay_create(lv_obj_t *parent, int size, int cx, int cy, uint32_t paper) {
    yk_yay_t *y = yay_make(parent, size, cx, cy, paper);
#ifdef YK_YAY_BUDDY_DEFAULT
    if (y) yk_yay_buddy(y, YK_BUDDY_CURRENT);  // an extension's Yay is the user's buddy, with nothing written (kit 1.10)
#endif
    return y;
}

int yk_yay_buddy_resolve(int buddy);
const yk_figure_t *yk_yay_buddy_kept(int buddy);

yk_yay_t *yk_yay_create_kept(lv_obj_t *parent, int size, int cx, int cy, uint32_t paper) {
    yk_yay_t *y = yay_make(parent, size, cx, cy, paper);
#ifdef YK_YAY_BUDDY_DEFAULT
    const yk_figure_t *f = y && paper != YK_YAY_TRANSPARENT ? yk_yay_buddy_kept(yk_yay_buddy_resolve(YK_BUDDY_CURRENT)) : NULL;
    if (f && !yk_yay_figure(y, f)) yk_yay_figure(y, NULL);
#endif
    return y;
}

void yk_yay_delete(yk_yay_t *y) {
    if (!y) return;
    for (yk_yay_t **p = &s_yays; *p; p = &(*p)->next) {
        if (*p == y) {
            *p = y->next;
            break;
        }
    }
    if (y->fg) {  // its patches go with the box; its pack's pictures when no other Yay draws from it
        if (y->fg->on && !pack_held(y->fg->f.data)) cache_drop(y->fg->f.data, y->fg->f.data + y->fg->f.bytes);
        for (int i = 0; i < PATCHES; i++) heap_caps_free(y->fg->px[i]);
        heap_caps_free(y->fg);
    }
    lv_timer_delete(y->timer);
    lv_anim_delete(y->box, NULL);
    lv_anim_delete(y->img[0], NULL);
    lv_anim_delete(y->img[1], NULL);
    lv_obj_delete(y->box);
    free(y);
    trim(-1);  // what was over CACHE for it
}

lv_obj_t *yk_yay_obj(yk_yay_t *y) { return y ? y->box : NULL; }

static void fade_exec(void *obj, int32_t v) { lv_obj_set_style_opa(obj, (lv_opa_t)v, 0); }

static void fade_done(lv_anim_t *a) {
    yk_yay_t *y = lv_anim_get_user_data(a);
    lv_obj_set_hidden(y->img[!y->cur], true);
    lv_obj_set_style_opa(y->img[y->cur], LV_OPA_COVER, 0);
}

static const lv_image_dsc_t *pre_find(const void *key, uint32_t paper, int size);

/** Shows y->art, or the figure's picture, in the current image (scaled to the size, turned). False: no memory for it. */
static bool show(yk_yay_t *y, lv_obj_t *img) {
    const figure_t *g = figure_of(y);
    const lv_image_dsc_t *src = g ? NULL : y->art->image;
    const uint8_t *px = g ? NULL : art_pixels(y->art);
    const void *key = g ? (const void *)g->pic.pixels : (const void *)y->art;
    y->scale = (float)y->size / (float)(g ? g->pic.size : y->art->size);
    // a copy made at this size (yk_yay_prescale): drawn as it is, no scaling as it is drawn
    const lv_image_dsc_t *pre = y->scale != 1.0f ? pre_find(key, y->paper, y->size) : NULL;
    const lv_image_dsc_t *d =
        pre ? pre
        : g ? decoded(g->pic.pixels, g->pic.palette, g->pic.pixels, g->pic.size, g->pic.size, y->paper)
            : decoded(y->art, px, px + 1024, src->header.w, src->header.h, y->paper);
    if (!d) return false;
    lv_image_set_src(img, d);
    y->img_scale = pre ? 1.0f : y->scale;
    lv_obj_set_size(img, d->header.w, d->header.h);
    lv_obj_set_pos(img, (y->size - (int32_t)d->header.w) / 2, (y->size - (int32_t)d->header.h) / 2);
    lv_image_set_pivot(img, d->header.w / 2, d->header.h / 2);
    lv_image_set_scale_x(img, (uint32_t)lroundf(256 * y->img_scale * y->sqx));
    lv_image_set_scale_y(img, (uint32_t)lroundf(256 * y->img_scale * y->sqy));
    lv_image_set_rotation(img, (int32_t)lroundf(y->rot * 10));
    lv_obj_set_hidden(img, false);
    return true;
}

static bool art_has_cb(yk_yay_pose_t pose, int size) { return art_for(pose, size) != NULL; }

/** Takes `pose` for what is drawn from now on: Pip's art, or the figure's picture with its palette for the patches.
 *  False: there is none. */
static bool pick(yk_yay_t *y, yk_yay_pose_t pose) {
    figure_t *g = figure_of(y);
    if (g) {
        if (!yk_figure_pick(&g->f, pose, y->size, &g->pic)) return false;
        yk_yay_palette(g->pic.palette, y->paper, g->rgb, g->alpha);
        return true;
    }
    yk_yay_pose_t use;
    int size;
    if (!yk_yay_pick(pose, y->size, art_has_cb, &use, &size)) {
#ifdef YAY_ART_PACKED
        // No pack yet (or not this pose in it): the one picture the image has, for every pose.
        const yay_art_t *a = &yay_art[YAY_ART_GREET_132];
        if (!art_pixels(a)) return false;
        y->art = a;
        return true;
#else
        return false;
#endif
    }
    y->art = art_for(use, size);
    return true;
}

/** The eyes are open in what is drawn. */
static bool eyes_open(const yk_yay_t *y) {
    const figure_t *g = figure_of(y);
    return g ? g->pic.eyes_open : y->art && y->art->eyes_open;
}

void yk_yay_pose(yk_yay_t *y, yk_yay_pose_t pose, int fade_ms) {
    if (!y || pose == y->pose) return;
    if (!pick(y, pose)) return;
    y->pose = pose;
    if (fade_ms <= 0 || lv_obj_is_hidden(y->img[y->cur])) {
        lv_anim_delete(y->img[!y->cur], NULL);
        lv_obj_set_hidden(y->img[!y->cur], true);
        lv_obj_set_style_opa(y->img[y->cur], LV_OPA_COVER, 0);
        show(y, y->img[y->cur]);
    } else {
        // the new pose fades in over the old
        y->cur = !y->cur;
        lv_obj_move_to_index(y->img[y->cur], 1);
        lv_obj_set_style_opa(y->img[y->cur], LV_OPA_TRANSP, 0);
        show(y, y->img[y->cur]);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, y->img[y->cur]);
        lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_duration(&a, (uint32_t)fade_ms);
        lv_anim_set_exec_cb(&a, fade_exec);
        lv_anim_set_user_data(&a, y);
        lv_anim_set_completed_cb(&a, fade_done);
        lv_anim_start(&a);
    }
    if (!eyes_open(y)) y->blinking = false;
    face(y);
}

yk_yay_pose_t yk_yay_get_pose(const yk_yay_t *y) { return y ? y->pose : YK_YAY_POSE_COUNT; }

void yk_yay_size(yk_yay_t *y, int size) {
    if (!y || size == y->size) return;
    y->size = size;
    layout(y);
    const yk_yay_pose_t p = y->pose;
    y->pose = YK_YAY_POSE_COUNT;
    yk_yay_pose(y, p, 0);
}

void yk_yay_move(yk_yay_t *y, int cx, int cy) {
    if (!y) return;
    y->cx = cx, y->cy = cy;
    layout(y);
}

void yk_yay_auto_blink(yk_yay_t *y, bool on) {
    if (y) y->auto_blink = on;
}

void yk_yay_blink(yk_yay_t *y) {
    if (!y || !eyes_open(y)) return;
    y->blinking = true;
    y->next_blink = lv_tick_get();  // the blink's start; tick() opens the eyes BLINK_MS later
    face(y);
}

void yk_yay_look_at(yk_yay_t *y, int x, int yy) {
    if (!y || !y->art || figure_of(y)) return;
    // towards the point, a few art pixels at most
    float dx = (float)(x - y->cx), dy = (float)(yy - y->cy);
    if (fabsf(y->rot - 180) < 0.5f) dx = -dx, dy = -dy;
    const float d = sqrtf(dx * dx + dy * dy);
    const float reach = LOOK_PX * (float)y->art->size / 176.0f;
    y->look_dx = d > 1 ? (int)lroundf(dx / d * reach) : 0;
    y->look_dy = d > 1 ? (int)lroundf(dy / d * reach * 0.6f) : 0;
    face(y);
}

void yk_yay_look_ahead(yk_yay_t *y) {
    if (!y) return;
    y->look_dx = y->look_dy = 0;
    face(y);
}

void yk_yay_mouth(yk_yay_t *y, yk_yay_mouth_t mouth) {
    if (!y) return;
    y->mouth_from_level = false;
    y->mouth_set = mouth;
    face(y);
}

void yk_yay_mouth_level(yk_yay_t *y, uint8_t level) {
    if (!y) return;
    const yk_yay_mouth_t m = yk_yay_mouth_step(y->mouth_level, level);
    if (y->mouth_from_level && m == y->mouth_level) return;
    y->mouth_from_level = true;
    y->mouth_level = m;
    face(y);
}

static void breathe_exec(void *obj, int32_t v) { lv_obj_set_style_translate_y(obj, v, 0); }

void yk_yay_breathe(yk_yay_t *y, bool on) {
    if (!y || on == y->breathing) return;
    y->breathing = on;
    lv_anim_delete(y->box, breathe_exec);
    lv_obj_set_style_translate_y(y->box, 0, 0);
    if (!on) return;
    lv_anim_t a;
    lv_anim_init(&a);
    const figure_t *g = figure_of(y);
    const uint32_t half = g ? g->f.breathe_ms / 2u : 2500;
    lv_anim_set_var(&a, y->box);
    lv_anim_set_values(&a, 0, g ? -(int32_t)g->f.breathe_px : -3);
    lv_anim_set_duration(&a, half);
    lv_anim_set_reverse_duration(&a, half);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, breathe_exec);
    lv_anim_start(&a);
}

/** The memory a figure needs on this Yay, and its pasted blocks' images over everything else in the box. */
static bool figure_make(yk_yay_t *y) {
    if (y->fg) return true;
    y->fg = heap_caps_calloc(1, sizeof(*y->fg), MALLOC_CAP_SPIRAM);
    if (!y->fg) return false;
    for (int i = 0; i < PATCHES; i++) {
        y->fg->patch[i] = lv_image_create(y->box);
        lv_obj_set_hidden(y->fg->patch[i], true);
    }
    return true;
}

bool yk_yay_figure(yk_yay_t *y, const yk_figure_t *figure) {
    if (!y || (figure && (y->paper == YK_YAY_TRANSPARENT || !figure_make(y)))) return false;
    const figure_t *was = figure_of(y);
    if (!was && !figure) return true;
    const uint8_t *from = was ? was->f.data : NULL, *to = was ? was->f.data + was->f.bytes : NULL;
    const bool breathing = y->breathing;
    yk_yay_breathe(y, false);
    y->blinking = false;
    for (int i = 0; i < 2; i++) lv_anim_delete(y->img[i], NULL);
    lv_obj_t *img = y->img[y->cur];
    lv_obj_set_hidden(y->img[!y->cur], true);
    lv_obj_set_style_opa(img, LV_OPA_COVER, 0);
    if (was) {
        for (int i = 0; i < PATCHES; i++) patch_show(y, i, NULL);
    } else {
        for (int i = 0; i < 2; i++) lv_obj_set_hidden(y->eye_cover[i], true), lv_obj_set_hidden(y->eye[i], true);
        lv_obj_set_hidden(y->mouth_cover, true);
        lv_obj_set_hidden(y->mouth, true);
    }
    if (figure) y->fg->f = *figure;
    y->fg->on = figure != NULL;
    bool ok = pick(y, y->pose) && show(y, img);
    bool shown = ok;
    if (!ok && figure) {  // nothing to draw it with: Pip, who needs no pack
        y->fg->on = false;
        shown = pick(y, y->pose) && show(y, img);
    }
    if (!shown) lv_obj_set_hidden(img, true);  // never left on memory that goes in the next line
    // The pack before goes when no Yay draws from it now (this one again, or another: whichever switches first).
    if (from && !pack_held(from)) cache_drop(from, to);
    y->next_blink = lv_tick_get() + (uint32_t)blink_gap(y);
    face(y);
    yk_yay_breathe(y, breathing);
    return ok;
}

// Which buddy (yk_yay_buddy.c: the current one read once a run, each buddy's pack read once a run and kept; the host
// test brings its own).
int yk_yay_buddy_resolve(int buddy);
const yk_figure_t *yk_yay_buddy_pack(int buddy);

bool yk_yay_buddy(yk_yay_t *y, int buddy) {
    if (!y) return false;
    buddy = yk_yay_buddy_resolve(buddy);
    // On a transparent paper no pack draws: it is not read for nothing (630 KB, a tenth of a second).
    const yk_figure_t *f = buddy == YK_BUDDY_PIP || y->paper == YK_YAY_TRANSPARENT ? NULL : yk_yay_buddy_pack(buddy);
    if (f && yk_yay_figure(y, f)) return true;
    yk_yay_figure(y, NULL);  // Pip, who is always there
    return buddy == YK_BUDDY_PIP;
}

#ifdef YAY_ART_PACKED
/** Pip's pack was hung since `y` picked its art: the pose again from the pack, at once, as yk_yay_figure changes over. */
static void pip_refresh(yk_yay_t *y) {
    y->pip_gen = s_pip_gen;
    if (figure_of(y) || y->pose == YK_YAY_POSE_COUNT) return;  // a buddy's pictures, or nothing drawn yet
    const bool breathing = y->breathing;
    yk_yay_breathe(y, false);
    for (int i = 0; i < 2; i++) lv_anim_delete(y->img[i], NULL);
    lv_obj_set_hidden(y->img[!y->cur], true);
    lv_obj_set_style_opa(y->img[y->cur], LV_OPA_COVER, 0);
    if (pick(y, y->pose)) show(y, y->img[y->cur]);
    face(y);
    yk_yay_breathe(y, breathing);
}

int yk_yay_pip(const yk_figure_t *pack) {
    if (!pack || s_pip_gen) return 0;
    const uint8_t **found = heap_caps_calloc(YAY_ART_COUNT, sizeof(*found), MALLOC_CAP_SPIRAM);
    if (!found) return 0;
    int n = 0;
    for (int i = 0; i < YAY_ART_COUNT; i++) {
        const lv_image_dsc_t *img = yay_art[i].image;
        if (img->data) continue;  // in the image (greet_132)
        uint32_t bytes = 0;
        const uint8_t *file = yk_figure_file(pack, yay_art[i].pose, yay_art[i].size, &bytes);
        // The same LVGL I8 file as the image would have had: its size, and the header saying the same as the entry's.
        if (!file || bytes != 12 + img->data_size || file[1] != img->header.cf) continue;
        found[i] = file + 12;
        n++;
    }
    if (!n) {
        heap_caps_free(found);
        return 0;
    }
    s_pip = found;  // for good: a Yay may draw from it any time from now on
    s_pip_gen++;
    ESP_LOGI(TAG, "Pip's pictures from his pack: %d", n);
    return n;
}
#else
int yk_yay_pip(const yk_figure_t *pack) {
    (void)pack;
    return 0;
}
#endif

static void rotate_exec(void *var, int32_t v) {
    yk_yay_t *y = var;
    y->rot = (float)v / 10.0f;
    lv_image_set_rotation(y->img[0], v);
    lv_image_set_rotation(y->img[1], v);
    face(y);
}

void yk_yay_rotate(yk_yay_t *y, float deg, int ms) {
    if (!y) return;
    deg = fmodf(deg, 360);
    if (deg < 0) deg += 360;
    lv_anim_delete(y, rotate_exec);
    if (ms <= 0) {
        rotate_exec(y, (int32_t)lroundf(deg * 10));
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, y);
    lv_anim_set_values(&a, (int32_t)lroundf(y->rot * 10), (int32_t)lroundf(deg * 10));
    lv_anim_set_duration(&a, (uint32_t)ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, rotate_exec);
    lv_anim_start(&a);
}

void yk_yay_fast(yk_yay_t *y, bool on) {
    if (!y) return;
    for (int i = 0; i < 2; i++) lv_image_set_antialias(y->img[i], !on);
}

void yk_yay_squash(yk_yay_t *y, float sx, float sy) {
    if (!y || (y->sqx == sx && y->sqy == sy)) return;
    const bool was = squashed(y);
    y->sqx = sx, y->sqy = sy;
    for (int i = 0; i < 2; i++) {
        lv_image_set_scale_x(y->img[i], (uint32_t)lroundf(256 * y->img_scale * sx));
        lv_image_set_scale_y(y->img[i], (uint32_t)lroundf(256 * y->img_scale * sy));
    }
    if (was != squashed(y)) face(y);  // its own blink and mouth wait while squashed
}

// ---------------------------------------------------------------- pre-scaled copies (yk_yay_prescale)

// A picture drawn at a size other than its own is scaled as it is drawn, every frame: Yay's badge jumping in (to 330
// and back), growing back to 264 and popping out of the big eyes drew at 5–13 frames a second (新四号机 11-18, ~50 ms
// a frame). Copies of one pose at a few sizes, made once in PSRAM by a task of their own at the lowest priority, are
// drawn as they are: a copy, as a picture at its own size. Bilinear from the pose's largest picture over the paper, the
// pixel centres lined up as LVGL's scaled draw does.

#define PRE_MAX 12
#define PRE_SIDE 400  // the largest size made

typedef struct {
    const void *key;  // made from: decoded()'s key (Pip's yay_art_t, a figure picture's pixels); NULL: free
    uint32_t paper;
    uint16_t size;
    bool todo;        // to be made by the job now
    bool ready;       // made: drawn from now (the task's store, LVGL's load)
    lv_image_dsc_t dsc;
} pre_t;

typedef struct {
    pre_t e[PRE_MAX];
    const void *key;  // the job: its picture's key, indices (w × w) and palette over its paper
    const uint8_t *idx;
    uint32_t w;
    uint16_t rgb[256];
    bool busy, stop;  // a job going; told to stop (a drop)
    uint16_t x0[PRE_SIDE], x1[PRE_SIDE];  // a row's source columns and the weight between them
    uint8_t fx[PRE_SIDE];
    uint32_t made_px, made_ms;  // what the last job made, and how long it took (the badge's log)
} pre_state_t;

static pre_state_t *s_pre;  // PSRAM, the first time one is asked for

/** The source pixel under target pixel `i` of `n`, from `w`: 16.16, the pixel centres lined up. */
static int32_t pre_at(int i, int n, uint32_t w) {
    const int32_t v = (int32_t)(((int64_t)(2 * i + 1) * w << 15) / n) - (1 << 15);
    return v < 0 ? 0 : v;
}

static uint16_t pre_mix(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint32_t fx, uint32_t fy) {
    if (a == b && a == c && a == d) return a;  // inside a flat colour: most of a picture
    const uint32_t wa = (256 - fx) * (256 - fy), wb = fx * (256 - fy), wc = (256 - fx) * fy, wd = fx * fy;
    const uint32_t r = ((a >> 11) * wa + (b >> 11) * wb + (c >> 11) * wc + (d >> 11) * wd + 32768) >> 16;
    const uint32_t g = (((a >> 5) & 63) * wa + ((b >> 5) & 63) * wb + ((c >> 5) & 63) * wc + ((d >> 5) & 63) * wd + 32768) >> 16;
    const uint32_t bl = ((a & 31) * wa + (b & 31) * wb + (c & 31) * wc + (d & 31) * wd + 32768) >> 16;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

/** One copy, `size` × `size`, into `out`. False: told to stop. */
static bool pre_make(pre_state_t *P, uint16_t *out, int size) {
    const uint32_t w = P->w;
    for (int i = 0; i < size; i++) {
        const int32_t v = pre_at(i, size, w);
        const uint32_t c = (uint32_t)v >> 16;
        P->x0[i] = (uint16_t)(c < w ? c : w - 1), P->x1[i] = (uint16_t)(c + 1 < w ? c + 1 : w - 1);
        P->fx[i] = (uint8_t)((uint32_t)v >> 8 & 0xFF);
    }
    for (int y = 0; y < size; y++) {
        if (__atomic_load_n(&P->stop, __ATOMIC_ACQUIRE)) return false;
        const int32_t v = pre_at(y, size, w);
        const uint32_t r = (uint32_t)v >> 16, r0 = r < w ? r : w - 1, r1 = r + 1 < w ? r + 1 : w - 1, fy = (uint32_t)v >> 8 & 0xFF;
        const uint8_t *a = P->idx + (size_t)r0 * w, *b = P->idx + (size_t)r1 * w;
        uint16_t *o = out + (size_t)y * size;
        for (int x = 0; x < size; x++) {
            const int i0 = P->x0[x], i1 = P->x1[x];
            o[x] = pre_mix(P->rgb[a[i0]], P->rgb[a[i1]], P->rgb[b[i0]], P->rgb[b[i1]], P->fx[x], fy);
        }
    }
    return true;
}

/** The job: every copy still to make, in the order asked; then not busy. */
static void pre_run(pre_state_t *P) {
#ifdef ESP_PLATFORM
    const int64_t t0 = esp_timer_get_time();
#endif
    uint32_t px = 0;
    for (int i = 0; i < PRE_MAX && !__atomic_load_n(&P->stop, __ATOMIC_ACQUIRE); i++) {
        pre_t *e = &P->e[i];
        if (!e->todo) continue;
        e->todo = false;
        const size_t n = (size_t)e->size * e->size;
        uint16_t *mem = heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM);
        if (!mem) {  // no room: that size is drawn scaled
            e->key = NULL;
            continue;
        }
        if (!pre_make(P, mem, e->size)) {
            heap_caps_free(mem);
            e->key = NULL;
            break;
        }
        e->dsc = (lv_image_dsc_t){
            .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = e->size, .h = e->size,
                       .stride = (uint32_t)e->size * 2},
            .data_size = (uint32_t)(n * 2),
            .data = (const uint8_t *)mem,
        };
        px += (uint32_t)n;
        __atomic_store_n(&e->ready, true, __ATOMIC_RELEASE);
    }
    P->made_px = px;
#ifdef ESP_PLATFORM
    P->made_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
#endif
    __atomic_store_n(&P->busy, false, __ATOMIC_RELEASE);
}

#ifdef ESP_PLATFORM
static TaskHandle_t s_pre_task;

static void pre_task(void *arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        pre_run(s_pre);
    }
}
#endif

/** A job going is stopped and over (it checks every row: a moment). */
static void pre_halt(void) {
    if (!s_pre || !__atomic_load_n(&s_pre->busy, __ATOMIC_ACQUIRE)) return;
    __atomic_store_n(&s_pre->stop, true, __ATOMIC_RELEASE);
#ifdef ESP_PLATFORM
    for (int i = 0; i < 100 && __atomic_load_n(&s_pre->busy, __ATOMIC_ACQUIRE); i++) vTaskDelay(1);
#endif
}

static const lv_image_dsc_t *pre_find(const void *key, uint32_t paper, int size) {
    if (!s_pre) return NULL;
    for (int i = 0; i < PRE_MAX; i++) {
        const pre_t *e = &s_pre->e[i];
        if (e->key == key && e->paper == paper && e->size == size && __atomic_load_n(&e->ready, __ATOMIC_ACQUIRE))
            return &e->dsc;
    }
    return NULL;
}

/** Some Yay draws this copy now. */
static bool pre_shown(const lv_image_dsc_t *d) {
    for (yk_yay_t *y = s_yays; y; y = y->next)
        for (int i = 0; i < 2; i++)
            if (lv_image_get_src(y->img[i]) == d) return true;
    return false;
}

int yk_yay_prescale(yk_yay_t *y, yk_yay_pose_t pose, uint32_t paper, const uint16_t *sizes, int n) {
    if (!y || paper == YK_YAY_TRANSPARENT || !sizes || n <= 0) return 0;
    // the pose's largest picture (what a Yay between it and the next smaller is drawn from)
    const void *key;
    const uint8_t *pal, *idx;
    uint32_t w;
    figure_t *g = figure_of(y);
    if (g) {
        yk_figure_picture_t pic;
        if (!yk_figure_pick(&g->f, pose, 1 << 14, &pic)) return 0;
        key = pic.pixels, pal = pic.palette, idx = pic.pixels, w = pic.size;
    } else {
        yk_yay_pose_t use;
        int size;
        if (!yk_yay_pick(pose, 1 << 14, art_has_cb, &use, &size)) return 0;
        const yay_art_t *a = art_for(use, size);
        const uint8_t *px = a ? art_pixels(a) : NULL;
        if (!px) return 0;
        key = a, pal = px, idx = px + 1024, w = a->image->header.w;
    }
    if (!s_pre && !(s_pre = heap_caps_calloc(1, sizeof(*s_pre), MALLOC_CAP_SPIRAM))) return 0;
    pre_state_t *P = s_pre;
    if (__atomic_load_n(&P->busy, __ATOMIC_ACQUIRE)) {
        if (P->key == key) return 0;  // being made already
        pre_halt();
        if (__atomic_load_n(&P->busy, __ATOMIC_ACQUIRE)) return 0;
    }
    P->stop = false;
    int asked = 0;
    for (int k = 0; k < n; k++) {
        const int s = sizes[k];
        if (s == (int)w || s < 16 || s > PRE_SIDE || pre_find(key, paper, s)) continue;
        int free_e = -1;
        for (int i = 0; i < PRE_MAX && free_e < 0; i++)
            if (!P->e[i].key) free_e = i;
        if (free_e < 0) break;  // full: the rest are drawn scaled
        P->e[free_e] = (pre_t){.key = key, .paper = paper, .size = (uint16_t)s, .todo = true};
        asked++;
    }
    if (!asked) return 0;
    P->key = key, P->idx = idx, P->w = w;
    uint8_t alpha[256];
    yk_yay_palette(pal, paper, P->rgb, alpha);
    __atomic_store_n(&P->busy, true, __ATOMIC_RELEASE);
#ifdef ESP_PLATFORM
    // its stack and its block in PSRAM: nothing of internal RAM (the main firmware's line), lowest priority
    if (!s_pre_task && xTaskCreatePinnedToCoreWithCaps(pre_task, "yk_prescale", 3072, NULL, 1, &s_pre_task, tskNO_AFFINITY,
                                                       MALLOC_CAP_SPIRAM) != pdPASS) {
        s_pre_task = NULL;
        for (int i = 0; i < PRE_MAX; i++)
            if (P->e[i].todo) P->e[i].key = NULL, P->e[i].todo = false;
        __atomic_store_n(&P->busy, false, __ATOMIC_RELEASE);
        return 0;
    }
    xTaskNotifyGive(s_pre_task);
#else
    pre_run(P);  // the host: at once
#endif
    return asked;
}

bool yk_yay_prescale_stats(uint32_t *ready, uint32_t *bytes, uint32_t *made_ms) {
    uint32_t n = 0, b = 0;
    if (s_pre)
        for (int i = 0; i < PRE_MAX; i++)
            if (s_pre->e[i].key && __atomic_load_n(&s_pre->e[i].ready, __ATOMIC_ACQUIRE)) n++, b += s_pre->e[i].dsc.data_size;
    if (ready) *ready = n;
    if (bytes) *bytes = b;
    if (made_ms) *made_ms = s_pre ? s_pre->made_ms : 0;
    return s_pre && __atomic_load_n(&s_pre->busy, __ATOMIC_ACQUIRE);
}

void yk_yay_prescale_drop(void) {
    if (!s_pre) return;
    pre_halt();
    if (__atomic_load_n(&s_pre->busy, __ATOMIC_ACQUIRE)) return;  // never: it stops within a row
    for (int i = 0; i < PRE_MAX; i++) {
        pre_t *e = &s_pre->e[i];
        if (!e->key) continue;
        if (e->ready && pre_shown(&e->dsc)) continue;  // drawn now: kept until the next drop
        if (e->ready) {
            lv_image_cache_drop(&e->dsc);  // LVGL forgets what it kept of it
            heap_caps_free((void *)e->dsc.data);
        }
        *e = (pre_t){0};
    }
}
