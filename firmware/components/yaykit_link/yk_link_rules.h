// The long and unpaired modes' pure parts (yk_link_keep.h): which ATT flags a characteristic gets, which
// configurations are refused, how advertising is paced. Pure C, host tested (test/run.sh); yk_link_keep.c applies it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yk_link_keep.h"

#ifdef __cplusplus
extern "C" {
#endif

// NimBLE's BLE_GATT_CHR_F_* (host/ble_gatt.h), restated so the rules build without NimBLE; yk_link_keep.c checks
// they are the same.
#define YK_RULES_F_READ 0x0002
#define YK_RULES_F_WRITE_NO_RSP 0x0004
#define YK_RULES_F_WRITE 0x0008
#define YK_RULES_F_NOTIFY 0x0010
#define YK_RULES_F_READ_ENC 0x0200
#define YK_RULES_F_WRITE_ENC 0x1000

#define YK_RULES_WRITE_NR 8      // beside YK_KEEP_READ/WRITE/NOTIFY: writes without response (the unpaired mode only)
#define YK_RULES_VALUE_MAX 512   // ATT's own limit on a value

/** A characteristic's ATT flags: the long mode encrypts every read and write and has no writes without response;
 *  the unpaired mode never encrypts. */
uint16_t yk_rules_chr_flags(uint8_t flags, bool unpaired);

/** The unpaired mode's configuration is good: a namespace (the app's number is the one built in, never given). */
bool yk_rules_unpaired_ok(const yk_link_keep_unpaired_t *cfg);

/** How the unpaired mode advertises: interval in 0.625 ms units, for how long (0: until connected). */
typedef struct {
    uint16_t itvl_min, itvl_max;
    int32_t duration_ms;
} yk_rules_adv_t;

#define YK_RULES_FAST_MS 30000

/** Fast (20-30 ms) for YK_RULES_FAST_MS after the start and after each disconnection, so a phone reconnecting in
 *  the background (iOS's pending connection, Android's autoConnect) finds it quickly; then 152.5 ms, one of the
 *  intervals Apple's accessory guidelines name for after the first 30 s. */
yk_rules_adv_t yk_rules_adv(bool fast);

/** A static random address as NimBLE holds it (little-endian: byte 5 the most significant): the two top bits set, and
 *  of the other 46 at least one 0 and at least one 1 (Bluetooth Core, Vol 6 Part B, 1.3.2.1). */
bool yk_rules_static_addr_ok(const uint8_t addr[6]);
/** Six random bytes made a static random address: the two top bits set. False when what is left is all 0s or all 1s
 *  (then draw again). */
bool yk_rules_static_addr_make(uint8_t addr[6]);

/** What a write or a notification may hold at this ATT MTU (0 for none: MTU 0 is "not connected"). */
uint16_t yk_rules_payload_max(uint16_t mtu);
/** The longest frame yk_link_keep_send() can seal into one notification at this MTU (0: none). */
uint16_t yk_rules_frame_room(uint16_t mtu);

#ifdef __cplusplus
}
#endif
