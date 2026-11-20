#include "yk_buddy_slot.h"

#include <stdio.h>
#include <string.h>

#define HEAD_CRC_AT 64

const char *yk_buddy_id(int buddy) {
    switch (buddy) {
    case YK_BUDDY_MOTE: return "mote";
    case YK_BUDDY_DIAL: return "dial";
    case YK_BUDDY_BLOOM: return "bloom";
    default: return "pip";
    }
}

int yk_buddy_from_id(const char *id) {
    static const int ALL[] = {YK_BUDDY_PIP, YK_BUDDY_MOTE, YK_BUDDY_DIAL, YK_BUDDY_BLOOM};
    for (size_t i = 0; id && i < sizeof(ALL) / sizeof(ALL[0]); i++)
        if (!strcmp(id, yk_buddy_id(ALL[i]))) return ALL[i];
    return -1;
}

uint32_t yk_buddy_slot_at(int slot) { return YK_BUDDY_AREA_AT + (uint32_t)slot * YK_BUDDY_SLOT_BYTES; }

static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

void yk_buddy_head_pack(const yk_buddy_head_t *h, uint8_t out[YK_BUDDY_HEAD]) {
    memset(out, 0, YK_BUDDY_HEAD);
    memcpy(out, "YKBP", 4);
    out[4] = 1;
    put32(out + 8, h->seq);
    memcpy(out + 12, h->id, strnlen(h->id, YK_BUDDY_ID - 1));
    put32(out + 28, h->size);
    memcpy(out + 32, h->sha, 32);
    put32(out + HEAD_CRC_AT, crc32(out, HEAD_CRC_AT));
}

bool yk_buddy_head_read(const uint8_t *b, size_t len, yk_buddy_head_t *h) {
    memset(h, 0, sizeof(*h));
    if (!b || len < YK_BUDDY_HEAD || memcmp(b, "YKBP", 4) || b[4] != 1 ||
        get32(b + HEAD_CRC_AT) != crc32(b, HEAD_CRC_AT))
        return false;
    yk_buddy_head_t r;
    memset(&r, 0, sizeof(r));
    r.seq = get32(b + 8);
    memcpy(r.id, b + 12, YK_BUDDY_ID);
    r.size = get32(b + 28);
    memcpy(r.sha, b + 32, 32);
    if (r.id[YK_BUDDY_ID - 1] || !r.id[0] || !r.size || r.size > YK_BUDDY_PACK_MAX) return false;
    *h = r;
    return true;
}

int yk_buddy_slot_of(const yk_buddy_slots_t *s, const char *id, const uint8_t sha[32], uint32_t size) {
    for (int i = 0; i < YK_BUDDY_SLOTS; i++)
        if (s->ok[i] && !strcmp(s->head[i].id, id) && s->head[i].size == size && !memcmp(s->head[i].sha, sha, 32)) return i;
    return -1;
}

int yk_buddy_slot_newest(const yk_buddy_slots_t *s, const char *id) {
    int best = -1;
    for (int i = 0; i < YK_BUDDY_SLOTS; i++)
        if (s->ok[i] && !strcmp(s->head[i].id, id) && (best < 0 || s->head[i].seq > s->head[best].seq)) best = i;
    return best;
}
