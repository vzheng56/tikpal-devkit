#include "yk_figure.h"

#include <string.h>

#define MAGIC "YKFG"
#define FIXED_BYTES 74     // the fixed part of the header: the tables start after it
#define PICTURE_BYTES 32
#define PATCH_BYTES 16
#define LV_MAGIC 0x19      // LV_IMAGE_HEADER_MAGIC
#define LV_I8 0x0A         // LV_COLOR_FORMAT_I8
#define LV_HEADER_BYTES 12
#define LV_PALETTE_BYTES 1024
#define MOUTH_SLOTS 3
#define EYE_SLOTS 2

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/** IEEE, as zlib's and esp_rom_crc32_le(0, ...). */
static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/** A name in a field of YK_FIGURE_NAME bytes: it must end inside the field. */
static bool name_of(const uint8_t *field, char out[YK_FIGURE_NAME]) {
    const void *end = memchr(field, 0, YK_FIGURE_NAME);
    if (!end) return false;
    memcpy(out, field, YK_FIGURE_NAME);
    return true;
}

/** `length` bytes at `offset` lie in the body of a pack of `bytes` bytes, at an offset that divides by 4. 64 bits: two
 *  fields of a broken table must not add up past the top and look small. */
static bool in_body(uint64_t offset, uint64_t length, uint32_t bytes) {
    return !(offset & 3) && offset >= YK_FIGURE_HEADER && offset + length <= bytes;
}

static bool patch_ok(const uint8_t *e, uint32_t bytes, uint16_t pictures) {
    const uint8_t picture = e[0], kind = e[1], slot = e[2];
    if (picture >= pictures || kind > YK_FIGURE_MOUTH) return false;
    if (slot >= (kind == YK_FIGURE_MOUTH ? MOUTH_SLOTS : EYE_SLOTS)) return false;
    const uint16_t w = u16(e + 8), h = u16(e + 10);
    return w && h && in_body(u32(e + 12), (uint64_t)w * h, bytes);
}

static const uint8_t *picture_entry(const yk_figure_t *f, int index) {
    return f->data + f->picture_table + (uint32_t)index * PICTURE_BYTES;
}
static const uint8_t *patch_entry(const yk_figure_t *f, int index) {
    return f->data + f->patch_table + (uint32_t)index * PATCH_BYTES;
}

/** The LVGL I8 file a picture is: its own 12-byte header says the same as the table. */
static bool bin_ok(const uint8_t *bin, uint16_t size) {
    return bin[0] == LV_MAGIC && bin[1] == LV_I8 && u16(bin + 4) == size && u16(bin + 6) == size && u16(bin + 8) == size;
}

static bool picture_ok(const yk_figure_t *f, int index) {
    const uint8_t *e = picture_entry(f, index);
    char name[YK_FIGURE_NAME];
    if (!name_of(e, name)) return false;
    const uint16_t size = u16(e + 16);
    const uint32_t offset = u32(e + 20), length = u32(e + 24);
    const uint16_t first = u16(e + 28), count = u16(e + 30);
    if (size == 0 || (uint64_t)length != (uint64_t)LV_HEADER_BYTES + LV_PALETTE_BYTES + (uint64_t)size * size) return false;
    if (!in_body(offset, length, f->bytes) || !bin_ok(f->data + offset, size)) return false;
    if ((uint32_t)first + count > f->patches) return false;
    for (int i = first; i < first + count; i++) {
        const uint8_t *p = patch_entry(f, i);
        if (p[0] != index) return false;
        if ((uint32_t)u16(p + 4) + u16(p + 8) > size || (uint32_t)u16(p + 6) + u16(p + 10) > size) return false;
    }
    return true;
}

static bool find(const yk_figure_t *f, const char *pose, int size, int *index) {
    for (int i = 0; i < f->pictures; i++) {
        const uint8_t *e = picture_entry(f, i);
        if (u16(e + 16) == size && !strcmp((const char *)e, pose)) {
            *index = i;
            return true;
        }
    }
    return false;
}

/** The home screen's 16 (step 15 of the art pipeline): a pack without one of them would leave it nothing to draw. */
static bool complete(const yk_figure_t *f) {
    static const struct {
        yk_yay_pose_t pose;
        int size;
    } NEEDED[] = {
        {YK_YAY_READY, 176},    {YK_YAY_LISTENING, 176}, {YK_YAY_THINKING, 176},       {YK_YAY_WORKING, 176},
        {YK_YAY_APPROVAL, 176}, {YK_YAY_DONE, 176},      {YK_YAY_REMINDER, 176},       {YK_YAY_HELP, 176},
        {YK_YAY_RESTING, 176},  {YK_YAY_SPEAKING, 176},  {YK_YAY_ACK, 176},            {YK_YAY_GREET, 176},
        {YK_YAY_WORKING_LAPTOP, 176}, {YK_YAY_READY, 264}, {YK_YAY_RESTING, 264},      {YK_YAY_GREET, 264},
    };
    int at;
    for (size_t i = 0; i < sizeof(NEEDED) / sizeof(NEEDED[0]); i++)
        if (!find(f, yk_yay_pose_name(NEEDED[i].pose), NEEDED[i].size, &at)) return false;
    return true;
}

bool yk_figure_read(const uint8_t *p, size_t len, yk_figure_t *out, const char **why) {
    const char *bad = NULL;
    yk_figure_t f;
    memset(&f, 0, sizeof(f));
    if (!p || !out || len < YK_FIGURE_HEADER) {
        bad = "short";
    } else if (memcmp(p, MAGIC, 4)) {
        bad = "magic";
    } else if (!(f.format = u16(p + 4)) || f.format > YK_FIGURE_FORMAT) {
        bad = "format";
    } else if (u16(p + 6) != YK_FIGURE_HEADER) {
        bad = "header";
    } else if (u32(p + 8) != crc32(p + 12, YK_FIGURE_HEADER - 12)) {
        bad = "crc";
    } else if ((f.bytes = u32(p + 12)) != len) {
        bad = "bytes";
    } else if (f.bytes > YK_FIGURE_MAX_BYTES) {
        bad = "too_big";
    } else if (!name_of(p + 20, f.figure) || !name_of(p + 36, f.theme) || !f.figure[0]) {
        bad = "name";
    } else {
        f.data = p;
        f.art_version = u32(p + 16);
        f.pictures = u16(p + 52);
        f.patches = u16(p + 54);
        f.picture_table = u32(p + 56);
        f.patch_table = u32(p + 60);
        f.blink_ms = u16(p + 64);
        f.gap_min_ms = u16(p + 66);
        f.gap_max_ms = u16(p + 68);
        f.breathe_px = u16(p + 70);
        f.breathe_ms = u16(p + 72);
        if (f.picture_table < FIXED_BYTES ||
            (uint64_t)f.picture_table + (uint64_t)f.pictures * PICTURE_BYTES > f.patch_table) {
            bad = "picture_table";
        } else if ((uint64_t)f.patch_table + (uint64_t)f.patches * PATCH_BYTES > YK_FIGURE_HEADER) {
            bad = "patch_table";
        } else if (!f.blink_ms || !f.gap_min_ms || f.gap_min_ms > f.gap_max_ms || !f.breathe_ms) {
            bad = "round";
        }
    }
    for (int i = 0; !bad && i < f.patches; i++)
        if (!patch_ok(patch_entry(&f, i), f.bytes, f.pictures)) bad = "patch";
    for (int i = 0; !bad && i < f.pictures; i++)
        if (!picture_ok(&f, i)) bad = "picture";
    if (!bad && !complete(&f)) bad = "missing";
    if (bad) {
        if (why) *why = bad;
        return false;
    }
    *out = f;
    return true;
}

static void picture_at(const yk_figure_t *f, int index, yk_figure_picture_t *out) {
    const uint8_t *e = picture_entry(f, index);
    const uint8_t *bin = f->data + u32(e + 20);
    *out = (yk_figure_picture_t){.size = u16(e + 16),
                                 .eyes_open = e[18] & 1,
                                 .palette = bin + LV_HEADER_BYTES,
                                 .pixels = bin + LV_HEADER_BYTES + LV_PALETTE_BYTES,
                                 .first_patch = u16(e + 28),
                                 .patches = u16(e + 30)};
}

bool yk_figure_pick(const yk_figure_t *f, yk_yay_pose_t pose, int size, yk_figure_picture_t *out) {
    for (int attempt = 0; attempt < 2; attempt++) {
        const char *name = yk_yay_pose_name(pose);
        if (!name) return false;
        int larger = -1, smaller = -1, larger_size = 0, smaller_size = 0;
        for (int i = 0; i < f->pictures; i++) {
            const uint8_t *e = picture_entry(f, i);
            if (strcmp((const char *)e, name)) continue;
            const int have = u16(e + 16);
            if (have == size) {
                picture_at(f, i, out);
                return true;
            }
            if (have > size && (larger < 0 || have < larger_size)) larger = i, larger_size = have;
            if (have < size && (smaller < 0 || have > smaller_size)) smaller = i, smaller_size = have;
        }
        const int best = larger >= 0 ? larger : smaller;
        if (best >= 0) {
            picture_at(f, best, out);
            return true;
        }
        if (pose != YK_YAY_THINKING_GLASS) break;
        pose = YK_YAY_THINKING;
    }
    return false;
}

bool yk_figure_patch(const yk_figure_t *f, const yk_figure_picture_t *pic, yk_figure_kind_t kind, int slot,
                     yk_figure_patch_t *out) {
    for (int i = pic->first_patch; i < pic->first_patch + pic->patches; i++) {
        const uint8_t *e = patch_entry(f, i);
        if (e[1] != kind || e[2] != slot) continue;
        *out = (yk_figure_patch_t){.x = u16(e + 4), .y = u16(e + 6), .w = u16(e + 8), .h = u16(e + 10),
                                   .indices = f->data + u32(e + 12)};
        return true;
    }
    return false;
}

const uint8_t *yk_figure_file(const yk_figure_t *f, const char *pose, int size, uint32_t *bytes) {
    int at;
    if (!f || !f->data || !pose || !find(f, pose, size, &at)) return NULL;
    const uint8_t *e = picture_entry(f, at);
    if (bytes) *bytes = u32(e + 24);
    return f->data + u32(e + 20);
}
