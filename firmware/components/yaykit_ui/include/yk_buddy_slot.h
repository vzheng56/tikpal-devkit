// The buddies' picture packs as Yay keeps them on flash (Yay 0.1.18 开发文档 4.9; 开发包四个伙伴-需求确认.md §8): which
// buddy a YK_PREF_BUDDY value is, the two slots in `models` and the head each starts with. Pure C, host tested
// (test/test_buddy_slot.c). Only Yay writes the slots (yay/main/yay_buddy.c, the head last, so that a power cut leaves a
// slot empty or whole); this is how anyone reads them (yk_buddy.h). Moved here from yay/main/yay_buddy.h in 0.1.18,
// byte for byte: the slots a 0.1.17 wrote read the same.
//
// 0.1.18: for Yay alone. From the development kit 1.10 (YK_API_LEVEL 5) an extension names a buddy with yk_buddy_t
// (yk_yay_buddy); it reads the slots through yk_buddy.h and never writes them (review: `models` is not an app's).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------- who

/** The buddies, by YK_PREF_BUDDY's values (BOARD_BUDDY_*): 1 and 2 were Wow and Aha and read as Pip. Pip needs no pack:
 *  he is in the image or in Pip's place (Yay 0.1.18 4.2). New buddies only ever come after, with values of their own.
 *  YK_BUDDY_CURRENT (development kit 1.10, YK_API_LEVEL 5; 开发包四个伙伴-需求确认.md §4) is no buddy of its own: the
 *  one the user chose, for yk_yay_buddy(), which is what an extension draws unless it names one. */
typedef enum {
    YK_BUDDY_CURRENT = -1,
    YK_BUDDY_PIP = 0,
    YK_BUDDY_MOTE = 3,
    YK_BUDDY_DIAL = 4,
    YK_BUDDY_BLOOM = 5,
} yk_buddy_t;
#define YK_BUDDY_ID 16  // a buddy's name with its terminator; YK_FIGURE_NAME, a figure's name in a pack

/** "pip", "mote", "dial", "bloom" (buddySchema) for a YK_PREF_BUDDY value; anything else is Pip, as it reads. */
const char *yk_buddy_id(int buddy);
/** And back: -1 for one this firmware does not know (YK_BUDDY_CURRENT's value: one it cannot name is drawn as the
 *  current buddy). */
int yk_buddy_from_id(const char *id);

// ---------------------------------------------------------------- the slots

// `models` is 3 MB: the speech models at its start (626,015 bytes), from 1 MB on two slots of 640 KB, 64 KB aligned, then
// Pip's place from 0x240000 (Yay 0.1.18 4.2: never written over by a slot). Each slot starts with a 4 KB sector for its
// head, the pack after it. Little-endian:
//   0 "YKBP"   4 format (1)   5..7 0   8 seq u32   12 id[16], NUL-padded   28 size u32   32 sha256[32]
//   64 CRC-32 (IEEE) of bytes 0..63   68.. 0xFF
// A head that does not hold together is an empty slot. Written last, after the pack is read back against its sha256.
#define YK_BUDDY_PARTITION "models"
#define YK_BUDDY_AREA_AT 0x100000u
#define YK_BUDDY_AREA_BYTES 0x200000u
#define YK_BUDDY_SLOTS 2
#define YK_BUDDY_SLOT_BYTES 0xA0000u
#define YK_BUDDY_SECTOR 4096u
#define YK_BUDDY_PACK_AT YK_BUDDY_SECTOR  // in a slot
#define YK_BUDDY_PACK_MAX 643072u         // BUDDY_PACK_MAX_BYTES
#define YK_BUDDY_HEAD 68
#define YK_BUDDY_PIP_AT 0x240000u  // Pip's place: the slots end before it

_Static_assert(YK_BUDDY_AREA_AT % 0x10000u == 0 && YK_BUDDY_SLOT_BYTES % 0x10000u == 0, "slots are 64 KB aligned");
_Static_assert(YK_BUDDY_SLOTS * YK_BUDDY_SLOT_BYTES <= YK_BUDDY_AREA_BYTES, "both slots in the area");
_Static_assert(YK_BUDDY_AREA_AT + YK_BUDDY_SLOTS * YK_BUDDY_SLOT_BYTES <= YK_BUDDY_PIP_AT, "no slot over Pip's place");
_Static_assert(YK_BUDDY_PACK_AT + YK_BUDDY_PACK_MAX <= YK_BUDDY_SLOT_BYTES, "the largest pack in a slot");

typedef struct {
    uint32_t seq;  // the higher is the newer, when both slots hold the same buddy
    char id[YK_BUDDY_ID];
    uint32_t size;
    uint8_t sha[32];
} yk_buddy_head_t;

/** The two slots as their heads say. */
typedef struct {
    bool ok[YK_BUDDY_SLOTS];
    yk_buddy_head_t head[YK_BUDDY_SLOTS];
} yk_buddy_slots_t;

/** Where a slot starts in the partition. */
uint32_t yk_buddy_slot_at(int slot);
void yk_buddy_head_pack(const yk_buddy_head_t *h, uint8_t out[YK_BUDDY_HEAD]);
/** False (`h` zeroed): not a head, a later format, or one that does not hold together. */
bool yk_buddy_head_read(const uint8_t *buf, size_t len, yk_buddy_head_t *h);

/** The slot that holds exactly this pack, -1 none. */
int yk_buddy_slot_of(const yk_buddy_slots_t *s, const char *id, const uint8_t sha[32], uint32_t size);
/** The newest slot that holds this buddy, -1 none. */
int yk_buddy_slot_newest(const yk_buddy_slots_t *s, const char *id);

#ifdef __cplusplus
}
#endif
