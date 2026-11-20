// A whole picture pack for the host tests: what yaykit/scripts/figure-pack.ts writes, 16 pictures and their patches. Used by
// test_figure.c, which breaks it, and by yay/test/test_buddy_run.c, which sends it to Yay.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PICTURE_TABLE 80
#define PICTURE_BYTES 32
#define PATCH_BYTES 16
#define BIN_176 (12 + 1024 + 176 * 176)
#define BIN_264 (12 + 1024 + 264 * 264)

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) {
    put16(p, v & 0xffff);
    put16(p + 2, v >> 16);
}
static uint32_t crc32(const uint8_t *d, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static const struct {
    const char *pose;
    int size;
    bool eyes;
} LIST[] = {
    {"ready", 176, true},   {"listening", 176, true}, {"thinking", 176, true}, {"working", 176, true},
    {"approval", 176, true}, {"done", 176, false},    {"reminder", 176, true}, {"help", 176, true},
    {"resting", 176, false}, {"speaking", 176, true}, {"ack", 176, true},      {"greet", 176, true},
    {"working-laptop", 176, true}, {"ready", 264, true}, {"resting", 264, false}, {"greet", 264, true},
};
#define PICTURES 16
#define SPEAKING 9

static uint8_t *s_pack;
static uint32_t s_bytes, s_patch_table;
static int s_extra;  // make(): a picture more, of a pose this firmware has no name for
static const char *s_figure = "dial";  // make(): whose pack it is

/** The header's checksum again, after a test changed something in it. */
static void seal(void) { put32(s_pack + 8, crc32(s_pack + 12, 2048 - 12)); }

/** A whole pack: 16 pictures, two eyes on each with its eyes open, three mouths on `speaking`, offsets as written. */
static void make(void) {
    uint32_t patches = 0;
    for (int i = 0; i < PICTURES; i++) patches += LIST[i].eyes ? 2 : 0;
    patches += 3;
    s_patch_table = PICTURE_TABLE + (PICTURES + (uint32_t)s_extra) * PICTURE_BYTES;
    uint32_t at = 2048;
    uint32_t offset[PICTURES];
    for (int i = 0; i < PICTURES; i++) {
        offset[i] = at;
        at = (at + (LIST[i].size == 176 ? BIN_176 : BIN_264) + 3) & ~3u;
    }
    uint32_t patch_at = at;
    at += patches * 16;  // each patch 4 × 4 here
    free(s_pack);
    s_bytes = at;
    s_pack = calloc(1, s_bytes);
    uint8_t *p = s_pack;
    memcpy(p, "YKFG", 4);
    put16(p + 4, 1);
    put16(p + 6, 2048);
    put32(p + 12, s_bytes);
    put32(p + 16, 7);
    strcpy((char *)p + 20, s_figure);
    strcpy((char *)p + 36, "original");
    put16(p + 52, PICTURES + (uint32_t)s_extra);
    put16(p + 54, (uint16_t)patches);
    put32(p + 56, PICTURE_TABLE);
    put32(p + 60, s_patch_table);
    put16(p + 64, 130);
    put16(p + 66, 4000);
    put16(p + 68, 6000);
    put16(p + 70, 2);
    put16(p + 72, 5000);
    int patch = 0;
    for (int i = 0; i < PICTURES; i++) {
        uint8_t *e = p + PICTURE_TABLE + i * PICTURE_BYTES;
        strcpy((char *)e, LIST[i].pose);
        const int size = LIST[i].size;
        put16(e + 16, (uint32_t)size);
        e[18] = LIST[i].eyes;
        put32(e + 20, offset[i]);
        put32(e + 24, (uint32_t)(size == 176 ? BIN_176 : BIN_264));
        const int first = patch;
        uint8_t *bin = p + offset[i];
        bin[0] = 0x19, bin[1] = 0x0A;
        put16(bin + 4, (uint32_t)size);
        put16(bin + 6, (uint32_t)size);
        put16(bin + 8, (uint32_t)size);
        const int mine = (LIST[i].eyes ? 2 : 0) + (i == SPEAKING ? 3 : 0);
        for (int k = 0; k < mine; k++, patch++) {
            uint8_t *q = p + s_patch_table + patch * PATCH_BYTES;
            const bool eye = LIST[i].eyes && k < 2;
            q[0] = (uint8_t)i, q[1] = eye ? 0 : 1, q[2] = (uint8_t)(eye ? k : k - 2);
            put16(q + 4, 10 + (uint32_t)k), put16(q + 6, 20), put16(q + 8, 4), put16(q + 10, 4);
            put32(q + 12, patch_at);
            memset(p + patch_at, patch + 1, 16);
            patch_at += 16;
        }
        put16(e + 28, (uint32_t)first);
        put16(e + 30, (uint32_t)mine);
    }
    if (s_extra) {
        uint8_t *e = p + PICTURE_TABLE + PICTURES * PICTURE_BYTES;
        memcpy(e, p + PICTURE_TABLE, PICTURE_BYTES);
        strcpy((char *)e, "working-mail");
        put16(e + 28, 0), put16(e + 30, 0);
    }
    seal();
}
