// yk_yay.c's picture cache on the host (LVGL stubbed in stub_yay/): the cache is every Yay's together. Yay 0.1.18 hangs
// the same buddy pack on the home screen's Yay and on the app ring's middle (dev doc 4.7), and closing the ring took the
// home screen's picture with it: only its eye patches were left, blinking (11-12, seen on 0.1.18 63816e5d with bloom).
// Pinned here: a pack's pictures go when the last Yay that hung it takes another or is deleted, in whatever order. And
// a picture some Yay draws is never refilled under it (the badge over the home screen, Yay 0.1.20, 11-17).
#include <stdio.h>

#include "../yk_yay.c"
#include "../yk_yay_buddy.c"
#include "figure_maker.h"

static int s_checks, s_failures;
#define CHECK(c)                                                       \
    do {                                                               \
        s_checks++;                                                    \
        if (!(c)) {                                                    \
            s_failures++;                                              \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); \
        }                                                              \
    } while (0)

// Pip: one small picture for every pose and size (Pip's own drawings are not what is tested).
static uint8_t s_pip_px[1024 + 8 * 8];
static const lv_image_dsc_t s_pip_img = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .w = 8, .h = 8, .stride = 8}, .data_size = sizeof(s_pip_px),
    .data = s_pip_px};
const yay_art_t yay_art[YAY_ART_COUNT] = {[0 ... YAY_ART_COUNT - 1] = {.image = &s_pip_img, .pose = "ready",
                                                                        .size = 176, .eyes_open = true}};
const yay_art_t *yay_art_find(const char *pose, uint16_t size) {
    (void)pose, (void)size;
    return &yay_art[0];
}

/** A pack of its own (figure_maker's), read. */
static yk_figure_t pack(const char *figure) {
    s_figure = figure;
    make();
    yk_figure_t f;
    const char *why = NULL;
    if (!yk_figure_read(s_pack, s_bytes, &f, &why)) {
        fprintf(stderr, "pack %s: %s\n", figure, why ? why : "?");
        exit(1);
    }
    s_pack = NULL;  // the next make() makes another; this one is the test's
    return f;
}

#define SLOTS ((int)(sizeof(s_cache) / sizeof(s_cache[0])))

/** The cache's slot `img` draws from now; -1: none (hidden, or not a cache entry). */
static int slot_drawn(const lv_obj_t *img) {
    if (img->hidden || !img->src) return -1;
    for (int i = 0; i < SLOTS; i++)
        if (img->src == &s_cache[i].dsc) return i;
    return -1;
}
static int slot_of(const yk_yay_t *y) { return slot_drawn(y->img[y->cur]); }

/** Pictures the cache holds now. */
static int kept(void) {
    int n = 0;
    for (int i = 0; i < SLOTS; i++) n += s_cache[i].key != NULL;
    return n;
}

/** What slot `i` holds, to see it again unchanged: its entry, and a sum of its pixels. */
typedef struct {
    const void *key;
    uint32_t paper;
    lv_image_dsc_t dsc;
    uint32_t sum;
} held_t;
static held_t held(int i) {
    held_t h = {.key = s_cache[i].key, .paper = s_cache[i].paper, .dsc = s_cache[i].dsc};
    for (uint32_t k = 0; h.dsc.data && k < h.dsc.data_size; k++) h.sum = h.sum * 31u + h.dsc.data[k];
    return h;
}
static bool same(held_t a, held_t b) {
    return a.key && a.key == b.key && a.paper == b.paper && a.dsc.data == b.dsc.data && a.dsc.data_size == b.dsc.data_size &&
           a.dsc.header.w == b.dsc.header.w && a.dsc.header.h == b.dsc.header.h &&
           a.dsc.header.stride == b.dsc.header.stride && a.sum == b.sum;
}

/** `y` draws the picture it picked from its pack: a cache entry made from that picture, with its pixels. */
static bool draws_pack(const yk_yay_t *y) {
    const int i = slot_of(y);
    const figure_t *g = figure_of(y);
    return i >= 0 && g && s_cache[i].key == g->pic.pixels && s_cache[i].dsc.data && s_cache[i].dsc.header.w > 0 &&
           s_cache[i].paper == y->paper;
}

/** How many of the cache's entries were made from `f`. */
static int cached_from(const yk_figure_t *f) {
    int n = 0;
    for (int i = 0; i < CACHE; i++) {
        const uint8_t *key = s_cache[i].key;
        if (key && key >= f->data && key < f->data + f->bytes) n++;
    }
    return n;
}

// yk_yay_buddy.c's side, made up: the shared setting and what the buddy slots hold (the main firmware's to fill).
static int s_pref;                                           // board_pref_buddy(): Pip until a test says otherwise
static const yk_figure_t *s_in_slot[YK_BUDDY_BLOOM + 1];     // by buddy; NULL: not in a slot
static int s_loads;                                          // packs read whole
static bool s_on_lvgl;                                       // yk_buddy_load says LVGL_TASK, as on LVGL's own task
int board_pref_buddy(void) { return s_pref; }
int64_t esp_timer_get_time(void) { return 0; }
static void got(const yk_figure_t *f, int slot, yk_buddy_pack_t *out) {
    s_loads++;
    out->figure = *f, out->bytes = (uint8_t *)f->data, out->size = f->bytes, out->from = YK_BUDDY_FROM_SLOT, out->slot = slot;
    out->why = NULL;
}
bool yk_buddy_load(int buddy, yk_buddy_pack_t *out) {
    memset(out, 0, sizeof(*out));
    out->slot = -1;
    out->why = s_on_lvgl ? "LVGL_TASK" : "NO_SLOT";
    const yk_figure_t *f = buddy > 0 && buddy <= YK_BUDDY_BLOOM ? s_in_slot[buddy] : NULL;
    if (s_on_lvgl || !f) return false;
    got(f, 0, out);
    return true;
}
bool yk_buddy_slots_read(yk_buddy_slots_t *out) {
    memset(out, 0, sizeof(*out));
    for (int b = 1, slot = 0; b <= YK_BUDDY_BLOOM && slot < YK_BUDDY_SLOTS; b++)
        if (s_in_slot[b]) {
            out->ok[slot] = true;
            snprintf(out->head[slot].id, sizeof(out->head[slot].id), "%s", yk_buddy_id(b));
            out->head[slot++].seq = 1;
        }
    return true;
}
bool yk_buddy_load_slot(const yk_buddy_slots_t *slots, int slot, yk_buddy_pack_t *out) {
    memset(out, 0, sizeof(*out));
    const int b = yk_buddy_from_id(slots->head[slot].id);
    if (b <= 0 || !s_in_slot[b]) return false;
    got(s_in_slot[b], slot, out);
    return true;
}
/** A run of an extension from its start: nothing read yet. */
static void new_run(int pref) {
    s_pref = pref, s_loads = 0, s_on_lvgl = false;
    s_buddy_current = YK_BUDDY_CURRENT;
    s_buddy_tried = 0;
    for (int b = 0; b <= YK_BUDDY_BLOOM; b++) free(s_buddy_kept[b]), s_buddy_kept[b] = NULL;  // the packs are the test's
    memset(s_in_slot, 0, sizeof(s_in_slot));
}
static bool draws(const yk_yay_t *y, const yk_figure_t *f) { return draws_pack(y) && figure_of(y)->f.data == f->data; }

// The home screen's Yay and the app ring's middle (yay_ui.c, yk_apps.c): the same pack, other papers and sizes.
static yk_yay_t *home(void) { return yk_yay_create(NULL, 264, 233, 202, YK_PAPER); }
static yk_yay_t *ring(void) { return yk_yay_create(NULL, 104, 233, 233, YK_PANEL); }
// The badge's (yay_badge_view.c): on black, 264 or 176 as it goes, through the poses it uses.
#define BADGE_BLACK 0x000000
static const yk_yay_pose_t BADGE_POSES[] = {YK_YAY_READY, YK_YAY_LISTENING, YK_YAY_THINKING, YK_YAY_SPEAKING, YK_YAY_ACK,
                                            YK_YAY_DONE,  YK_YAY_HELP,      YK_YAY_RESTING,  YK_YAY_GREET};
#define BADGE_WAYS ((int)(sizeof(BADGE_POSES) / sizeof(BADGE_POSES[0])))

/** One time into the badge and out: its Yay made, `steps` poses and sizes, deleted. False: it lost its own picture. */
static bool badge_round(const yk_figure_t *f, int steps) {
    yk_yay_t *b = yk_yay_create(NULL, 264, 233, 233, BADGE_BLACK);
    bool ok = b && yk_yay_figure(b, f) && draws_pack(b);
    for (int k = 0; ok && k < steps; k++) {
        const int drops = s_lv_cache_drops;
        yk_yay_size(b, k % 3 ? 176 : 264);
        yk_yay_pose(b, BADGE_POSES[k % BADGE_WAYS], 0);
        ok = draws_pack(b);
        // A slot made again: LVGL forgets what it kept of the picture before (its header, a decoded copy).
        if (s_lv_cache_drops != drops) ok = ok && s_lv_cache_drop == &s_cache[slot_of(b)].dsc;
    }
    yk_yay_delete(b);
    return ok;
}


// Another paper (yk_yay_paper, Yay 0.2's home at night): the pose drawn again over it, a figure's patches too; what
// was made over the old paper goes unless another Yay shows it; the same paper, or a transparent one, nothing.
static void test_paper(void) {
    const yk_figure_t bloom = pack("bloom");
    yk_yay_t *h = home();
    CHECK(h && yk_yay_figure(h, &bloom) && draws_pack(h));
    const held_t day = held(slot_of(h));
    yk_yay_paper(h, YK_PAPER);  // the same: nothing made again
    CHECK(same(day, held(slot_of(h))));
    yk_yay_paper(h, YK_NIGHT);
    CHECK(h->paper == YK_NIGHT && draws_pack(h) && s_cache[slot_of(h)].paper == YK_NIGHT);
    CHECK(s_cache[slot_of(h)].key == day.key && held(slot_of(h)).sum != day.sum);  // the same picture, other pixels
    for (int i = 0; i < SLOTS; i++) CHECK(!(s_cache[i].key == day.key && s_cache[i].paper == YK_PAPER));  // gone
    uint16_t rgb[256];
    uint8_t alpha[256];
    yk_yay_palette(figure_of(h)->pic.palette, YK_NIGHT, rgb, alpha);
    CHECK(!memcmp(rgb, figure_of(h)->rgb, sizeof(rgb)));  // the patches' colours over black
    // As one made on black from the start: the same slot, the same pixels.
    yk_yay_t *b = yk_yay_create(NULL, 264, 233, 202, YK_NIGHT);
    CHECK(b && yk_yay_figure(b, &bloom) && slot_of(b) == slot_of(h));
    // Back at 7:00: the day's made again; the black one stays while the other Yay shows it.
    const int night_slot = slot_of(h);
    yk_yay_paper(h, YK_PAPER);
    CHECK(h->paper == YK_PAPER && draws_pack(h) && s_cache[slot_of(h)].paper == YK_PAPER);
    CHECK(held(slot_of(h)).sum == day.sum);  // the day's pixels as they were
    CHECK(slot_of(b) == night_slot && s_cache[night_slot].paper == YK_NIGHT && draws_pack(b));
    yk_yay_delete(b);
    // Never from or to transparent.
    yk_yay_paper(h, YK_YAY_TRANSPARENT);
    CHECK(h->paper == YK_PAPER && draws_pack(h));
    yk_yay_t *t = yk_yay_create(NULL, 132, 233, 233, YK_YAY_TRANSPARENT);
    yk_yay_paper(t, YK_NIGHT);
    CHECK(t && t->paper == YK_YAY_TRANSPARENT);
    yk_yay_delete(t);
    yk_yay_paper(NULL, YK_NIGHT);  // nothing to do
    yk_yay_delete(h);
    free((void *)bloom.data);
}

// Pre-scaled copies (yk_yay_prescale, Yay 0.1.21's badge): made once, from the pose's largest picture, bilinear with the
// pixel centres lined up; any Yay at that size draws the copy as it is; a drop keeps the one some Yay draws.
static void test_prescale(void) {
    const yk_figure_t pk = pack("bloom"), *f = &pk;  // its own: main's are let go by the buddy tests before
    // the scaler: a picture half one colour, half another, twice as big: each half its colour, a blend only between
    static pre_state_t P;
    static uint8_t idx[8 * 8];
    for (int i = 0; i < 64; i++) idx[i] = (uint8_t)(i % 8 < 4 ? 1 : 2);
    P.idx = idx, P.w = 8;
    P.rgb[1] = 0xF800, P.rgb[2] = 0x001F;  // red, blue
    static uint16_t out[16 * 16];
    CHECK(pre_make(&P, out, 16));
    bool halves = true, between = false;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            const uint16_t c = out[y * 16 + x];
            if (x < 7) halves &= c == 0xF800;
            else if (x > 8) halves &= c == 0x001F;
            else between |= c != 0xF800 && c != 0x001F && (c >> 11) > 0 && (c & 31) > 0;
        }
    CHECK(halves && between);
    // ... and smaller: still each half its colour away from the middle
    CHECK(pre_make(&P, out, 6));
    CHECK(out[0] == 0xF800 && out[5] == 0x001F);

    yk_yay_t *b = yk_yay_create(NULL, 264, 233, 233, BADGE_BLACK);
    CHECK(b && yk_yay_figure(b, f));
    static const uint16_t SIZES[] = {200, 300, 264};  // 264: the picture's own, nothing to make
    CHECK(yk_yay_prescale(b, YK_YAY_READY, BADGE_BLACK, SIZES, 3) == 2);
    uint32_t n = 0, bytes = 0;
    CHECK(!yk_yay_prescale_stats(&n, &bytes, NULL) && n == 2 && bytes == (200 * 200 + 300 * 300) * 2);
    CHECK(yk_yay_prescale(b, YK_YAY_READY, BADGE_BLACK, SIZES, 3) == 0);  // made already
    // at 300 it draws the copy as it is; at its own size the picture; at a size not made, the picture scaled
    yk_yay_size(b, 300);
    const lv_image_dsc_t *d = lv_image_get_src(b->img[b->cur]);
    CHECK(d && d->header.w == 300 && b->img_scale == 1.0f && b->scale > 1.1f);
    yk_yay_squash(b, 1.1f, 0.9f);  // squashed: the copy still, scaled by the squash alone
    CHECK(lv_image_get_src(b->img[b->cur]) == d && b->img_scale == 1.0f);
    yk_yay_squash(b, 1, 1);
    yk_yay_size(b, 264);
    d = lv_image_get_src(b->img[b->cur]);
    CHECK(d && d->header.w == 264 && b->img_scale == 1.0f);
    yk_yay_size(b, 250);
    d = lv_image_get_src(b->img[b->cur]);
    CHECK(d && d->header.w == 264 && b->img_scale < 1.0f);
    // another Yay with the same pack, on the same paper, draws the same copy; on another paper, not
    yk_yay_t *o = yk_yay_create(NULL, 200, 233, 233, BADGE_BLACK);
    CHECK(o && yk_yay_figure(o, f));
    CHECK(((const lv_image_dsc_t *)lv_image_get_src(o->img[o->cur]))->header.w == 200 && o->img_scale == 1.0f);
    yk_yay_t *h = yk_yay_create(NULL, 200, 233, 233, YK_PAPER);
    CHECK(h && yk_yay_figure(h, f) && h->img_scale != 1.0f);
    // dropped while `o` draws the 200 one: that one stays, the 300 one goes (LVGL told)
    const int drops = s_lv_cache_drops;
    yk_yay_prescale_drop();
    CHECK(!yk_yay_prescale_stats(&n, &bytes, NULL) && n == 1 && bytes == 200 * 200 * 2 && s_lv_cache_drops == drops + 1);
    yk_yay_delete(o);
    yk_yay_delete(h);
    yk_yay_prescale_drop();
    CHECK(!yk_yay_prescale_stats(&n, &bytes, NULL) && n == 0 && bytes == 0);
    yk_yay_size(b, 200);  // gone: the picture scaled again
    CHECK(b->img_scale != 1.0f);
    yk_yay_delete(b);
}

int main(void) {
    const yk_figure_t bloom = pack("bloom"), mote = pack("mote");
    yk_yay_t *h = home();
    CHECK(h && yk_yay_figure(h, &bloom) && draws_pack(h));

    // The ring opens with the buddy in its middle and closes: the home screen keeps its picture, every time.
    for (int round = 0; round < 3; round++) {
        yk_yay_t *r = ring();
        CHECK(r && yk_yay_figure(r, &bloom) && draws_pack(r));
        CHECK(draws_pack(h));
        CHECK(cached_from(&bloom) == 2);  // 264 on the paper, 176 on the panel
        yk_yay_delete(r);
        CHECK(draws_pack(h));             // was: its slot freed and zeroed, the eyes alone
        CHECK(cached_from(&bloom) == 2);  // the ring's stays for its next opening, and goes with the pack
    }

    // Another buddy while the ring is open, the ring first (yay_ui_figure's order): neither loses its picture, and the
    // old pack's are gone once both took the new one (its bytes may go then).
    yk_yay_t *r = ring();
    CHECK(r && yk_yay_figure(r, &bloom));
    CHECK(yk_yay_figure(r, &mote) && draws_pack(r));
    CHECK(draws_pack(h));  // still bloom
    CHECK(yk_yay_figure(h, &mote) && draws_pack(h) && draws_pack(r));
    CHECK(cached_from(&bloom) == 0);
    // ... and the home screen first.
    CHECK(yk_yay_figure(h, &bloom) && draws_pack(h));
    CHECK(draws_pack(r));  // still mote
    CHECK(yk_yay_figure(r, &bloom) && draws_pack(r) && draws_pack(h));
    CHECK(cached_from(&mote) == 0);

    // The same pack again keeps what it draws.
    CHECK(yk_yay_figure(h, &bloom) && draws_pack(h));

    // Pip on the home screen: the ring keeps the pack's picture until it goes, the last to draw from it.
    CHECK(yk_yay_figure(h, NULL) && !figure_of(h));
    CHECK(draws_pack(r));
    yk_yay_delete(r);
    CHECK(cached_from(&bloom) == 0);

    // The home screen's Yay going last takes what it drew.
    CHECK(yk_yay_figure(h, &mote) && draws_pack(h));
    yk_yay_delete(h);
    CHECK(cached_from(&mote) == 0);
    CHECK(s_yays == NULL);

    // The badge over the home screen (Yay 0.1.20): many more pictures than the cache keeps, on black, 264 and 176, while
    // home's Yay under it asks the cache for nothing. Home's picture stays, memory and all. Was: home's slot, the oldest
    // used, went to a black 176 one (or, the same size, was written over in place), and home's image, still set up for
    // 264, drew it a row and a half a line, past its end into noise (四号机 11-17, after 30-odd times in and out).
    h = home();
    CHECK(h && yk_yay_figure(h, &bloom) && draws_pack(h));
    const int home_slot = slot_of(h);
    const held_t home_pic = held(home_slot);
    s_lv_cache_drops = 0;
    for (int round = 0; round < 40; round++) CHECK(badge_round(&bloom, 3 * BADGE_WAYS));
    CHECK(s_lv_cache_drops > 0);  // slots were made again, and LVGL told each time
    CHECK(slot_of(h) == home_slot && draws_pack(h) && same(held(home_slot), home_pic));
    CHECK(kept() <= CACHE);

    // Home's pose fading into another: both its pictures are drawn while it fades, and both stay. Once the fade is
    // done the one before is hidden, and may go.
    yk_yay_pose(h, YK_YAY_GREET, 300);
    const int fading_in = slot_drawn(h->img[h->cur]), fading_out = slot_drawn(h->img[!h->cur]);
    CHECK(fading_in >= 0 && fading_out == home_slot && fading_in != fading_out);
    const held_t in_pic = held(fading_in);
    for (int round = 0; round < 3; round++) CHECK(badge_round(&bloom, 3 * BADGE_WAYS));
    CHECK(same(held(fading_in), in_pic) && same(held(fading_out), home_pic));
    lv_anim_t done;
    lv_anim_init(&done);
    lv_anim_set_user_data(&done, h);
    fade_done(&done);
    CHECK(slot_of(h) == fading_in && slot_drawn(h->img[!h->cur]) == -1);
    for (int round = 0; round < 3; round++) CHECK(badge_round(&bloom, 3 * BADGE_WAYS));
    CHECK(draws_pack(h) && same(held(fading_in), in_pic));

    // More Yays drawing at once than the cache keeps, each its own picture (a paper each): the cache goes over rather
    // than take one from under any of them, and back to CACHE once they are gone.
    yk_yay_t *many[CACHE + 3];
    for (int i = 0; i < CACHE + 3; i++) {
        many[i] = yk_yay_create(NULL, 176, 233, 233, 0x010203u * (uint32_t)(i + 1));
        CHECK(many[i] && yk_yay_figure(many[i], &bloom) && draws_pack(many[i]));
    }
    for (int i = 0; i < CACHE + 3; i++) CHECK(draws_pack(many[i]));
    CHECK(draws_pack(h) && same(held(fading_in), in_pic));
    CHECK(kept() > CACHE && kept() <= SLOTS);
    for (int i = 0; i < CACHE + 3; i++) yk_yay_delete(many[i]);
    CHECK(kept() <= CACHE && draws_pack(h) && same(held(fading_in), in_pic));
    yk_yay_delete(h);
    CHECK(cached_from(&bloom) == 0 && s_yays == NULL);

    // Which buddy (development kit 1.10, 开发包四个伙伴-需求确认.md §4). The user's buddy, Mote, in a slot as the main
    // firmware keeps it: built as an extension (YK_YAY_BUDDY_DEFAULT) every Yay is Mote with nothing written; built as
    // Yay, Pip until it is named. One read a run, whatever draws it.
    new_run(YK_BUDDY_MOTE);
    s_in_slot[YK_BUDDY_MOTE] = &mote;
    yk_yay_t *a = home(), *b = ring();
#ifdef YK_YAY_BUDDY_DEFAULT
    CHECK(draws(a, &mote) && draws(b, &mote));
#else
    CHECK(!figure_of(a) && !figure_of(b) && s_loads == 0);
    CHECK(yk_yay_buddy(a, YK_BUDDY_CURRENT) && draws(a, &mote));
    CHECK(yk_yay_buddy(b, YK_BUDDY_CURRENT) && draws(b, &mote));
#endif
    CHECK(s_loads == 1);
    // Chosen while it runs: the next time it opens.
    s_pref = YK_BUDDY_BLOOM;
    s_in_slot[YK_BUDDY_BLOOM] = &bloom;
    CHECK(yk_yay_buddy(a, YK_BUDDY_CURRENT) && draws(a, &mote));
    // One by name; one not in a slot is Pip, asked once; Pip asked is Pip; one it cannot name is the user's.
    CHECK(yk_yay_buddy(a, YK_BUDDY_BLOOM) && draws(a, &bloom) && draws(b, &mote));
    CHECK(s_loads == 2);
    CHECK(!yk_yay_buddy(a, YK_BUDDY_DIAL) && !figure_of(a) && slot_of(a) >= 0);
    CHECK(!yk_yay_buddy(a, YK_BUDDY_DIAL) && !figure_of(a));
    CHECK((s_buddy_tried & 1u << YK_BUDDY_DIAL) && !s_buddy_kept[YK_BUDDY_DIAL]);
    CHECK(yk_yay_buddy(a, YK_BUDDY_PIP) && !figure_of(a));
    CHECK(yk_yay_buddy(a, 42) && draws(a, &mote));
    CHECK(yk_yay_buddy(a, 1) && draws(a, &mote));  // Wow's old number is no buddy to name: the user's
    CHECK(s_loads == 2);
    // On a transparent paper no pack draws: Pip, and nothing read for it.
    yk_yay_t *c = yk_yay_create(NULL, 176, 233, 233, YK_YAY_TRANSPARENT);
    CHECK(c && !figure_of(c) && slot_of(c) >= 0);
    free(s_buddy_kept[YK_BUDDY_BLOOM]), s_buddy_kept[YK_BUDDY_BLOOM] = NULL;
    s_buddy_tried &= (uint8_t)~(1u << YK_BUDDY_BLOOM);
    CHECK(!yk_yay_buddy(c, YK_BUDDY_BLOOM) && !figure_of(c) && !(s_buddy_tried & 1u << YK_BUDDY_BLOOM));
    yk_yay_delete(c);
    yk_yay_delete(a);
    yk_yay_delete(b);
    CHECK(s_yays == NULL);

    // The app ring's middle in an extension (yk_yay_create_kept): the user's buddy only when the app already has its
    // pack, else Pip, and opening the ring never reads one (固件总策划 on #1874: 0.1 s and 630 KB at the ring's first
    // opening, in an app that draws no buddy).
    new_run(YK_BUDDY_MOTE);
    s_in_slot[YK_BUDDY_MOTE] = &mote;
    yk_yay_t *m = yk_yay_create_kept(NULL, 104, 233, 233, YK_PANEL);
    CHECK(m && !figure_of(m) && slot_of(m) >= 0 && s_loads == 0);
    yk_yay_delete(m);
    yk_yay_t *page = home();  // the app draws its buddy (an extension's build: by default)
#ifdef YK_YAY_BUDDY_DEFAULT
    CHECK(draws(page, &mote) && s_loads == 1);
    m = yk_yay_create_kept(NULL, 104, 233, 233, YK_PANEL);
    CHECK(m && draws(m, &mote) && s_loads == 1);
#else
    CHECK(!figure_of(page) && s_loads == 0);  // Yay: its ring is yk_apps_figure()'s, nothing of its own accord
    m = yk_yay_create_kept(NULL, 104, 233, 233, YK_PANEL);
    CHECK(m && !figure_of(m) && s_loads == 0);
#endif
    yk_yay_delete(m);
    yk_yay_delete(page);

    // Wow and Aha, as the setting may still say, and a value it does not know: Pip, nothing read.
    for (int pref = 1; pref <= 7; pref += 6) {
        new_run(pref);
        s_in_slot[YK_BUDDY_MOTE] = &mote;
        yk_yay_t *p = home();
        CHECK(yk_yay_buddy(p, YK_BUDDY_CURRENT) && !figure_of(p) && s_loads == 0);
        yk_yay_delete(p);
    }
    // No pack for the user's buddy (a main firmware before 0.1.18, or not fetched yet): Pip, no error.
    new_run(YK_BUDDY_DIAL);
    yk_yay_t *d = home();
    CHECK(!yk_yay_buddy(d, YK_BUDDY_CURRENT) && !figure_of(d) && slot_of(d) >= 0);
    yk_yay_delete(d);
    // Made on LVGL's own task (an event callback): yk_buddy_load says no for the card's sake; the slots still do.
    new_run(YK_BUDDY_BLOOM);
    s_in_slot[YK_BUDDY_BLOOM] = &bloom;
    s_on_lvgl = true;
    yk_yay_t *e = home();
    CHECK(yk_yay_buddy(e, YK_BUDDY_CURRENT) && draws(e, &bloom) && s_loads == 1);
    yk_yay_delete(e);
    CHECK(s_yays == NULL);

    free((void *)bloom.data);
    free((void *)mote.data);
    test_prescale();
    test_paper();
    printf("yk_yay host tests: %d checks, %d failures\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
