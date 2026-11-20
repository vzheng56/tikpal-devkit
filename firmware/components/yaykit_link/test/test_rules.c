// Host tests for the long and unpaired modes' pure parts (yk_link_rules.c): the flags each characteristic gets, the
// configurations the unpaired mode refuses, how it advertises.
#include <stdio.h>
#include <string.h>

#include "../yk_link_rules.h"

static int failures;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); \
            failures++;                                                  \
        }                                                                \
    } while (0)

static yk_link_keep_unpaired_t good(void) {
    return (yk_link_keep_unpaired_t){.nvs_ns = "yk_navigation"};
}

int main(void) {
    // The long mode: every read and write encrypted, no writes without response (asked for or not).
    CHECK(yk_rules_chr_flags(YK_KEEP_READ, false) == (YK_RULES_F_READ | YK_RULES_F_READ_ENC));
    CHECK(yk_rules_chr_flags(YK_KEEP_WRITE, false) == (YK_RULES_F_WRITE | YK_RULES_F_WRITE_ENC));
    CHECK(yk_rules_chr_flags(YK_KEEP_WRITE | YK_RULES_WRITE_NR, false) == (YK_RULES_F_WRITE | YK_RULES_F_WRITE_ENC));
    CHECK(yk_rules_chr_flags(YK_KEEP_NOTIFY, false) == YK_RULES_F_NOTIFY);
    CHECK(yk_rules_chr_flags(YK_KEEP_READ | YK_KEEP_WRITE | YK_KEEP_NOTIFY, false) ==
          (YK_RULES_F_READ | YK_RULES_F_READ_ENC | YK_RULES_F_WRITE | YK_RULES_F_WRITE_ENC | YK_RULES_F_NOTIFY));

    // The unpaired mode: never encrypted; writes without response when asked for, alone or beside ones with.
    CHECK(yk_rules_chr_flags(YK_KEEP_READ, true) == YK_RULES_F_READ);
    CHECK(yk_rules_chr_flags(YK_KEEP_WRITE, true) == YK_RULES_F_WRITE);
    CHECK(yk_rules_chr_flags(YK_RULES_WRITE_NR, true) == YK_RULES_F_WRITE_NO_RSP);
    CHECK(yk_rules_chr_flags(YK_KEEP_WRITE | YK_RULES_WRITE_NR, true) == (YK_RULES_F_WRITE | YK_RULES_F_WRITE_NO_RSP));
    CHECK(yk_rules_chr_flags(YK_KEEP_READ | YK_KEEP_NOTIFY, true) == (YK_RULES_F_READ | YK_RULES_F_NOTIFY));
    for (int f = 0; f < 16; f++)
        CHECK(!(yk_rules_chr_flags((uint8_t)f, true) & (YK_RULES_F_READ_ENC | YK_RULES_F_WRITE_ENC)));

    // Configurations: a namespace, nothing else asked (the app's number is the one built in from app.json).
    yk_link_keep_unpaired_t c = good();
    CHECK(yk_rules_unpaired_ok(&c));
    CHECK(!yk_rules_unpaired_ok(NULL));
    c = good(), c.nvs_ns = NULL;
    CHECK(!yk_rules_unpaired_ok(&c));
    c = good(), c.nvs_ns = "";
    CHECK(!yk_rules_unpaired_ok(&c));

    // Advertising: 20-30 ms for 30 s, then 152.5 ms until connected.
    const yk_rules_adv_t fast = yk_rules_adv(true), slow = yk_rules_adv(false);
    CHECK(fast.itvl_min * 625 == 20000 && fast.itvl_max * 625 == 30000 && fast.duration_ms == 30000);
    CHECK(slow.itvl_min * 625 == 152500 && slow.itvl_max == slow.itvl_min && slow.duration_ms == 0);

    // What fits a write or a notification.
    CHECK(yk_rules_payload_max(0) == 0 && yk_rules_payload_max(3) == 0);
    CHECK(yk_rules_payload_max(23) == 20 && yk_rules_payload_max(185) == 182);
    CHECK(yk_rules_payload_max(517) == YK_RULES_VALUE_MAX);  // 514, but ATT holds 512
    CHECK(yk_rules_payload_max(527) == YK_RULES_VALUE_MAX);

    // The longest frame a notification holds sealed: the MTU less 3, less the envelope's 20, at most 160.
    CHECK(yk_rules_frame_room(0) == 0 && yk_rules_frame_room(23) == 0);
    CHECK(yk_rules_frame_room(185) == 160 && yk_rules_frame_room(517) == YK_LINK_FRAME_MAX);
    CHECK(yk_rules_frame_room(131) == 108);  // a phone at the contract's least (108-byte writes) still takes 86 + 2

    // A static random address: the two top bits of the most significant byte (NimBLE's byte 5) set, the other 46 bits
    // neither all 0s nor all 1s.
    {
        uint8_t a[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0x3C};
        CHECK(!yk_rules_static_addr_ok(a));  // top bits 00: not static random
        CHECK(yk_rules_static_addr_make(a) && a[5] == 0xFC && a[0] == 0x12 && yk_rules_static_addr_ok(a));
        const uint8_t resolvable[6] = {1, 2, 3, 4, 5, 0x41}, nonres[6] = {1, 2, 3, 4, 5, 0x01};
        CHECK(!yk_rules_static_addr_ok(resolvable) && !yk_rules_static_addr_ok(nonres));
        uint8_t zeros[6] = {0, 0, 0, 0, 0, 0}, ones[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3F};
        CHECK(!yk_rules_static_addr_make(zeros) && zeros[5] == 0xC0);  // all of the 46 bits 0: draw again
        CHECK(!yk_rules_static_addr_make(ones) && ones[5] == 0xFF);    // all 1s: draw again
        const uint8_t one_bit[6] = {0, 0, 0, 0, 0, 0xC1}, one_zero[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF};
        CHECK(yk_rules_static_addr_ok(one_bit) && yk_rules_static_addr_ok(one_zero));
    }

    if (failures) {
        fprintf(stderr, "test_rules: %d failed\n", failures);
        return 1;
    }
    printf("test_rules: ok\n");
    return 0;
}
