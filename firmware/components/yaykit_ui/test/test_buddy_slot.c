// Host tests for yk_buddy_slot.c: the buddies' values are YK_PREF_BUDDY's, and a slot's head is byte for byte what Yay
// 0.1.17 wrote (moved here from yay/main/yay_buddy.c in 0.1.18): a device updated from 0.1.17 reads its slots as they are.
// yay/test/test_buddy.c tests the same head through Yay's names, on a flash that loses power.
#include <stdio.h>
#include <string.h>

#include "yk_buddy_slot.h"

static int failures, checks;
#define CHECK(cond)                                                \
    do {                                                           \
        checks++;                                                  \
        if (!(cond)) {                                             \
            failures++;                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

static void test_values(void) {
    CHECK(YK_BUDDY_PIP == 0 && YK_BUDDY_MOTE == 3 && YK_BUDDY_DIAL == 4 && YK_BUDDY_BLOOM == 5);
    const int all[] = {YK_BUDDY_PIP, YK_BUDDY_MOTE, YK_BUDDY_DIAL, YK_BUDDY_BLOOM};
    for (int i = 0; i < 4; i++) CHECK(yk_buddy_from_id(yk_buddy_id(all[i])) == all[i]);
    CHECK(!strcmp(yk_buddy_id(1), "pip") && !strcmp(yk_buddy_id(2), "pip") && !strcmp(yk_buddy_id(99), "pip"));
    CHECK(yk_buddy_from_id("wow") == -1 && yk_buddy_from_id(NULL) == -1 && yk_buddy_from_id("") == -1);
}

static void test_layout(void) {
    CHECK(yk_buddy_slot_at(0) == 0x100000u && yk_buddy_slot_at(1) == 0x1A0000u);
    CHECK(yk_buddy_slot_at(1) + YK_BUDDY_SLOT_BYTES == YK_BUDDY_PIP_AT);  // the slots end where Pip's place starts
}

/** 0.1.17's head of seq 7, "dial", 618,496 bytes, sha256 00 01 … 1f: its 68 bytes, CRC included. */
static void test_bytes(void) {
    yk_buddy_head_t h = {.seq = 7, .size = 618496};
    memcpy(h.id, "dial", 5);
    for (int i = 0; i < 32; i++) h.sha[i] = (uint8_t)i;
    uint8_t b[YK_BUDDY_HEAD];
    yk_buddy_head_pack(&h, b);
    static const uint8_t WANT[28] = {'Y', 'K', 'B', 'P', 1, 0, 0, 0, 7, 0, 0, 0, 'd', 'i', 'a', 'l', 0, 0, 0, 0,
                                     0,   0,   0,   0,   0, 0, 0, 0};
    CHECK(!memcmp(b, WANT, sizeof(WANT)));
    CHECK(b[28] == 0x00 && b[29] == 0x70 && b[30] == 0x09 && b[31] == 0x00);  // 618,496 little-endian
    for (int i = 0; i < 32; i++) CHECK(b[32 + i] == i);
    // The CRC-32 (IEEE) of bytes 0..63, worked out apart from the code under test.
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < 64; i++) {
        crc ^= b[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    crc = ~crc;
    CHECK(b[64] == (uint8_t)crc && b[65] == (uint8_t)(crc >> 8) && b[66] == (uint8_t)(crc >> 16) && b[67] == (uint8_t)(crc >> 24));
    yk_buddy_head_t r;
    CHECK(yk_buddy_head_read(b, sizeof(b), &r) && r.seq == 7 && !strcmp(r.id, "dial") && r.size == 618496 &&
          !memcmp(r.sha, h.sha, 32));
    b[4] = 2;  // Pip's place's file has format 2: never a slot's
    CHECK(!yk_buddy_head_read(b, sizeof(b), &r) && r.size == 0);
}

static void test_choosing(void) {
    yk_buddy_slots_t s;
    memset(&s, 0, sizeof(s));
    uint8_t sha[32] = {1};
    CHECK(yk_buddy_slot_newest(&s, "mote") == -1 && yk_buddy_slot_of(&s, "mote", sha, 10) == -1);
    s.ok[0] = s.ok[1] = true;
    s.head[0] = (yk_buddy_head_t){.seq = 4, .size = 10};
    s.head[1] = (yk_buddy_head_t){.seq = 5, .size = 10};
    memcpy(s.head[0].id, "mote", 5);
    memcpy(s.head[1].id, "mote", 5);
    memcpy(s.head[0].sha, sha, 32);
    CHECK(yk_buddy_slot_newest(&s, "mote") == 1);  // two of the same buddy: the newer
    CHECK(yk_buddy_slot_of(&s, "mote", sha, 10) == 0 && yk_buddy_slot_of(&s, "mote", sha, 11) == -1);
    s.ok[1] = false;
    CHECK(yk_buddy_slot_newest(&s, "mote") == 0 && yk_buddy_slot_newest(&s, "dial") == -1);
}

int main(void) {
    test_values();
    test_layout();
    test_bytes();
    test_choosing();
    printf("yk_buddy_slot host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
