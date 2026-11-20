// Host tests for yk_figure: the pack's table, read the way yaykit/scripts/figure-pack.ts readFigurePack reads it. A pack
// made here, every way of breaking it, and (with a real pack: test_figure <pack> <figure> <theme>) that one against its
// manifest: the lines figure_manifest.py prints come on stdin from run.sh.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yk_figure.h"
#include "yk_ui_logic.h"

static int failures, checks;
#define CHECK(cond)                                                \
    do {                                                           \
        checks++;                                                  \
        if (!(cond)) {                                             \
            failures++;                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

#include "figure_maker.h"

static const char *read_why(void) {
    yk_figure_t f;
    const char *why = NULL;
    return yk_figure_read(s_pack, s_bytes, &f, &why) ? NULL : why;
}

static uint8_t *picture_entry(int i) { return s_pack + PICTURE_TABLE + i * PICTURE_BYTES; }
static uint8_t *patch_entry(int i) { return s_pack + s_patch_table + i * PATCH_BYTES; }

static void test_whole(void) {
    make();
    yk_figure_t f;
    const char *why = "none";
    CHECK(yk_figure_read(s_pack, s_bytes, &f, &why));
    CHECK(f.pictures == 16 && f.patches == 29 && f.art_version == 7 && f.bytes == s_bytes && f.format == 1);
    CHECK(!strcmp(f.figure, "dial") && !strcmp(f.theme, "original"));
    CHECK(f.blink_ms == 130 && f.gap_min_ms == 4000 && f.gap_max_ms == 6000 && f.breathe_px == 2 && f.breathe_ms == 5000);

    yk_figure_picture_t pic;
    yk_figure_patch_t patch;
    CHECK(yk_figure_pick(&f, YK_YAY_READY, 264, &pic) && pic.size == 264 && pic.eyes_open);
    CHECK(yk_figure_pick(&f, YK_YAY_READY, 176, &pic) && pic.size == 176);
    CHECK(pic.palette == s_pack + 2048 + 12 && pic.pixels == pic.palette + 1024);
    // Its own size, the next larger, the largest smaller, as Pip's are picked.
    CHECK(yk_figure_pick(&f, YK_YAY_READY, 132, &pic) && pic.size == 176);
    CHECK(yk_figure_pick(&f, YK_YAY_LISTENING, 264, &pic) && pic.size == 176);
    CHECK(yk_figure_pick(&f, YK_YAY_THINKING_GLASS, 176, &pic) && pic.size == 176);  // the pack has none: thinking
    CHECK(!yk_figure_pick(&f, YK_YAY_POSE_COUNT, 176, &pic));

    CHECK(yk_figure_pick(&f, YK_YAY_DONE, 176, &pic) && !pic.eyes_open && !pic.patches);
    CHECK(!yk_figure_patch(&f, &pic, YK_FIGURE_EYE, 0, &patch));
    CHECK(yk_figure_pick(&f, YK_YAY_READY, 176, &pic));
    CHECK(yk_figure_patch(&f, &pic, YK_FIGURE_EYE, 1, &patch) && patch.x == 11 && patch.w == 4);
    CHECK(!yk_figure_patch(&f, &pic, YK_FIGURE_MOUTH, 0, &patch));  // only `speaking` moves its mouth
    CHECK(yk_figure_pick(&f, YK_YAY_SPEAKING, 176, &pic));
    for (int m = YK_YAY_MOUTH_CLOSED; m <= YK_YAY_MOUTH_OPEN; m++)
        CHECK(yk_figure_patch(&f, &pic, YK_FIGURE_MOUTH, m, &patch) && patch.indices >= s_pack + 2048 &&
              patch.indices + 16 <= s_pack + s_bytes);

    // A picture as the LVGL file it is (Pip's art from Pip's pack, yk_yay_pip): that size exactly, nothing near it.
    uint32_t bytes = 0;
    CHECK(yk_figure_pick(&f, YK_YAY_READY, 176, &pic));
    const uint8_t *file = yk_figure_file(&f, "ready", 176, &bytes);
    CHECK(file && file + 12 == pic.palette && bytes == 12 + 1024 + 176 * 176 && file[0] == 0x19 && file[1] == 0x0A);
    CHECK(file && file >= s_pack + 2048 && file + bytes <= s_pack + s_bytes);
    CHECK(yk_figure_file(&f, "ready", 264, &bytes) && bytes == 12 + 1024 + 264 * 264);
    CHECK(!yk_figure_file(&f, "ready", 132, &bytes));      // no nearest size here: an exact one or none
    CHECK(!yk_figure_file(&f, "thinking-glass", 176, NULL));
    CHECK(!yk_figure_file(&f, "nobody", 176, NULL));
    CHECK(!yk_figure_file(NULL, "ready", 176, NULL) && !yk_figure_file(&f, NULL, 176, NULL));
}

/** What the header says, broken one way at a time. */
static void test_header(void) {
    make();
    CHECK(!read_why());
    yk_figure_t f;
    const char *why = NULL;
    CHECK(!yk_figure_read(NULL, 4096, &f, &why) && !strcmp(why, "short"));
    CHECK(!yk_figure_read(s_pack, 2047, &f, &why) && !strcmp(why, "short"));
    CHECK(yk_figure_read(s_pack, s_bytes, &f, NULL));  // `why` may be NULL
    s_pack[0] = 'X';
    CHECK(!strcmp(read_why(), "magic"));

    make();
    put16(s_pack + 4, 2);  // a format newer than this firmware, however good the rest
    CHECK(!strcmp(read_why(), "format"));
    put16(s_pack + 4, 0);
    CHECK(!strcmp(read_why(), "format"));

    make();
    put16(s_pack + 6, 1024);
    CHECK(!strcmp(read_why(), "header"));

    make();
    s_pack[2047] ^= 1;  // the last byte the checksum covers
    CHECK(!strcmp(read_why(), "crc"));
    make();
    s_pack[100] ^= 1;
    CHECK(!strcmp(read_why(), "crc"));

    make();  // a pack cut short, or with more behind it: the size it says is not its size
    CHECK(!yk_figure_read(s_pack, s_bytes - 1, &f, &why) && !strcmp(why, "bytes"));
    CHECK(!yk_figure_read(s_pack, s_bytes + 1, &f, &why) && !strcmp(why, "bytes"));
    put32(s_pack + 12, s_bytes + 4);
    seal();
    CHECK(!strcmp(read_why(), "bytes"));

    make();
    memset(s_pack + 20, 'x', 16);  // no terminator
    seal();
    CHECK(!strcmp(read_why(), "name"));
    make();
    memset(s_pack + 20, 0, 16);  // no figure
    seal();
    CHECK(!strcmp(read_why(), "name"));

    make();
    uint8_t *big = calloc(1, YK_FIGURE_MAX_BYTES + 4);
    memcpy(big, s_pack, 2048);
    put32(big + 12, YK_FIGURE_MAX_BYTES + 4);
    put32(big + 8, crc32(big + 12, 2048 - 12));
    CHECK(!yk_figure_read(big, YK_FIGURE_MAX_BYTES + 4, &f, &why) && !strcmp(why, "too_big"));
    free(big);
}

static void test_tables(void) {
    make();
    put32(s_pack + 56, 40);  // over the fixed part
    seal();
    CHECK(!strcmp(read_why(), "picture_table"));
    make();
    put16(s_pack + 52, 40);  // pictures running into the patches
    seal();
    CHECK(!strcmp(read_why(), "picture_table"));
    make();
    put32(s_pack + 56, 0xFFFFFFF0u);  // adds up past the top: not small
    seal();
    CHECK(!strcmp(read_why(), "picture_table"));
    make();
    put16(s_pack + 54, 200);  // patches out of the header
    seal();
    CHECK(!strcmp(read_why(), "patch_table"));
    make();
    put32(s_pack + 60, 0xFFFFFFF0u);
    seal();
    CHECK(!strcmp(read_why(), "patch_table"));
    static const int ROUND[] = {64, 66, 72};  // no blink, no gap, no breath: nothing would ever tick
    for (int i = 0; i < 3; i++) {
        make();
        put16(s_pack + ROUND[i], 0);
        seal();
        CHECK(!strcmp(read_why(), "round"));
    }
    make();
    put16(s_pack + 66, 7000);  // the shortest gap past the longest
    seal();
    CHECK(!strcmp(read_why(), "round"));
}

static void test_pictures(void) {
    make();
    // Not aligned, and nothing else wrong: inside the pack, and a good LVGL header where it now says the picture is.
    const uint32_t at = (uint32_t)(picture_entry(3)[20] | picture_entry(3)[21] << 8 | picture_entry(3)[22] << 16);
    memmove(s_pack + at + 2, s_pack + at, 12);
    put32(picture_entry(3) + 20, at + 2);
    seal();
    CHECK(!strcmp(read_why(), "picture"));
    make();
    put32(picture_entry(3) + 20, 1024);  // in the header
    seal();
    CHECK(!strcmp(read_why(), "picture"));
    make();
    put32(picture_entry(3) + 20, s_bytes - BIN_176 + 4);  // past the end
    seal();
    CHECK(!strcmp(read_why(), "picture"));
    make();
    put32(picture_entry(3) + 20, 0xFFFFFFFCu);  // past the top
    seal();
    CHECK(!strcmp(read_why(), "picture"));
    make();
    put32(picture_entry(3) + 24, BIN_176 - 1);  // not the size of a 176 picture
    seal();
    CHECK(!strcmp(read_why(), "picture"));
    make();
    put16(picture_entry(3) + 16, 0);
    seal();
    CHECK(!strcmp(read_why(), "picture"));
    make();
    memset(picture_entry(3), 'x', 16);  // a name with no end
    seal();
    CHECK(!strcmp(read_why(), "picture"));
    make();
    put16(picture_entry(0) + 30, 60);  // more patches than the table has
    seal();
    CHECK(!strcmp(read_why(), "picture"));
    make();
    const uint32_t bin = (uint32_t)(picture_entry(3)[20] | picture_entry(3)[21] << 8 | picture_entry(3)[22] << 16);
    s_pack[bin + 1] = 0x12;  // not I8: outside the header, so no checksum says so
    CHECK(!strcmp(read_why(), "picture"));
    make();
    put16(s_pack + bin + 4, 264);  // says another size than the table
    CHECK(!strcmp(read_why(), "picture"));
    make();
    picture_entry(3)[0] = 'z';  // one of the home screen's 16 not there
    seal();
    CHECK(!strcmp(read_why(), "missing"));
    // One this firmware has no pose for is no reason to refuse the rest (a later art export).
    s_extra = 1;
    make();
    CHECK(!read_why());
    s_extra = 0;
}

static void test_patches(void) {
    make();
    put32(patch_entry(4) + 12, s_bytes - 32 + 2);  // not aligned, and inside the pack
    seal();
    CHECK(!strcmp(read_why(), "patch"));
    make();
    put32(patch_entry(4) + 12, s_bytes - 12);  // past the end
    seal();
    CHECK(!strcmp(read_why(), "patch"));
    make();
    put32(patch_entry(4) + 12, 512);  // in the header
    seal();
    CHECK(!strcmp(read_why(), "patch"));
    make();
    put16(patch_entry(4) + 8, 0xFFFF), put16(patch_entry(4) + 10, 0xFFFF);  // w × h past 32 bits of care
    seal();
    CHECK(!strcmp(read_why(), "patch"));
    make();
    put16(patch_entry(4) + 8, 0);
    seal();
    CHECK(!strcmp(read_why(), "patch"));
    make();
    patch_entry(4)[1] = 2;  // a kind it does not know
    seal();
    CHECK(!strcmp(read_why(), "patch"));
    make();
    patch_entry(4)[1] = 0, patch_entry(4)[2] = 2;  // a third eye
    seal();
    CHECK(!strcmp(read_why(), "patch"));
    make();
    patch_entry(0)[1] = 1, patch_entry(0)[2] = 3;  // a fourth mouth
    seal();
    CHECK(!strcmp(read_why(), "patch"));
    make();
    patch_entry(4)[0] = 16;  // a picture that is not there
    seal();
    CHECK(!strcmp(read_why(), "patch"));
    make();
    patch_entry(0)[0] = 1;  // another picture's, in this one's list
    seal();
    CHECK(!strcmp(read_why(), "picture"));
    make();
    put16(patch_entry(0) + 4, 174);  // past the picture's right edge: 174 + 4
    seal();
    CHECK(!strcmp(read_why(), "picture"));
    make();
    put16(patch_entry(0) + 6, 174);
    seal();
    CHECK(!strcmp(read_why(), "picture"));
}

static uint8_t *read_all(const char *path, size_t *len) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    const long n = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)n);
    if (data && fread(data, 1, (size_t)n, file) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(file);
    *len = (size_t)n;
    return data;
}

/** The real thing against its manifest. stdin, from run.sh: `pack <bytes> <pictures>`, `round <blink> <lo> <hi> <px> <ms>`,
 *  then a `picture <pose> <size> <eyes> <offset> <bytes> <patches>` and its `patch <kind> <slot> <x> <y> <w> <h>` lines,
 *  and `mouths <n>`: the rig's round.mouths. */
static void test_real(const char *path, const char *figure, const char *theme) {
    size_t len = 0;
    uint8_t *pack = read_all(path, &len);
    CHECK(pack != NULL);
    if (!pack) return;
    yk_figure_t f;
    const char *why = "none";
    CHECK(yk_figure_read(pack, len, &f, &why));
    if (failures) printf("  why %s\n", why);
    CHECK(!strcmp(f.figure, figure) && !strcmp(f.theme, theme));
    char line[200], name[32];
    unsigned a, b, c, d, e, g;
    yk_figure_picture_t pic = {0};
    int pictures = 0, patches = 0, kinds[YK_YAY_POSE_COUNT] = {0};
    int mouths_in_pack = 0;
    while (fgets(line, sizeof(line), stdin)) {
        if (sscanf(line, "pack %u %u", &a, &b) == 2) {
            CHECK(a == len && b == f.pictures);
        } else if (sscanf(line, "round %u %u %u %u %u", &a, &b, &c, &d, &e) == 5) {
            CHECK(a == f.blink_ms && b == f.gap_min_ms && c == f.gap_max_ms && d == f.breathe_px && e == f.breathe_ms);
        } else if (sscanf(line, "picture %31s %u %u %u %u %u", name, &a, &b, &c, &d, &e) == 6) {
            // The art's names are the firmware's: every picture of the pack is a pose yk_yay draws.
            const yk_yay_pose_t pose = yk_yay_pose_from_name(name);
            CHECK(pose != YK_YAY_POSE_COUNT);
            if (pose == YK_YAY_POSE_COUNT) continue;
            kinds[pose]++;
            CHECK(yk_figure_pick(&f, pose, (int)a, &pic) && pic.size == a);
            CHECK(pic.eyes_open == (b != 0));
            CHECK(pic.palette == pack + c + 12 && pic.pixels == pack + c + 12 + 1024);
            CHECK(d == 12 + 1024 + a * a);
            CHECK(pic.patches == e);
            pictures++;
        } else if (sscanf(line, "patch %u %u %u %u %u %u", &a, &b, &c, &d, &e, &g) == 6) {
            yk_figure_patch_t p;
            CHECK(yk_figure_patch(&f, &pic, (yk_figure_kind_t)a, (int)b, &p));
            CHECK(p.x == c && p.y == d && p.w == e && p.h == g);
            CHECK(p.x + p.w <= pic.size && p.y + p.h <= pic.size);
            CHECK(p.indices >= pack + 2048 && p.indices + (size_t)p.w * p.h <= pack + len);
            CHECK(((size_t)(p.indices - pack) & 3) == 0);
            if (a == YK_FIGURE_MOUTH) mouths_in_pack++;
            patches++;
        } else if (sscanf(line, "mouths %u", &a) == 1) {
            CHECK(a == YK_YAY_MOUTH_OPEN + 1);  // shut, slightly open, open: yk_yay_mouth_t, in that order
        }
    }
    CHECK(pictures == 16 && patches == f.patches);
    CHECK(mouths_in_pack == YK_YAY_MOUTH_OPEN + 1);
    // 13 poses of the 14 yk_yay_pose_t has: three at two sizes; THINKING_GLASS is not exported, nor drawn on home.
    int poses = 0;
    for (int i = 0; i < YK_YAY_POSE_COUNT; i++) poses += kinds[i] != 0;
    CHECK(poses == 13 && !kinds[YK_YAY_THINKING_GLASS]);
    CHECK(kinds[YK_YAY_READY] == 2 && kinds[YK_YAY_RESTING] == 2 && kinds[YK_YAY_GREET] == 2);
    free(pack);
    printf("yk_figure: the real pack %s reads, %d pictures, %d patches\n", path, pictures, patches);
}

int main(int argc, char **argv) {
    test_whole();
    test_header();
    test_tables();
    test_pictures();
    test_patches();
    free(s_pack);
    if (argc > 3) test_real(argv[1], argv[2], argv[3]);
    printf("yk_figure host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
