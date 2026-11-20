// 设备通道 (device.link) on Tikpal Voice: the unpaired mode's mark, handshake and envelopes, as the contract
// (packages/contracts/src/device-link.ts LINK) has them. Pure C over mbedTLS, host tested against
// fixtures/device-link.json (test/run.sh); yk_link_keep.c holds one yk_seal_link_t per start and calls it from
// NimBLE's host task (and yk_link_keep_send()'s task), under its own lock.
//
// - The mark: SHA-256("yay-mark-v1" ‖ device id), its first 20 bits big-endian. Not a secret: it picks the device.
// - The session characteristic: the device's 8 random bytes (new on every connection), the kid it holds (0: none),
//   the app's number. LinkOpen, the phone's first write (with response, in the clear): version, kind 2, the phone's 8
//   random bytes, the kid it uses. A kid that is not the device's: KEY_MISMATCH. One open a connection: a second one
//   would start the counters again under the same connection key (the device's random bytes do not change), so it is
//   refused (BUSY).
// - The connection's key: HMAC-SHA256(link key, "tikpal-link-session-v1" ‖ app u16 LE ‖ device's 8 ‖ phone's 8 ‖ 1),
//   the first 16 bytes (HKDF's Expand only; ESP-IDF's HKDF stays off).
// - An envelope: counter u32 LE ‖ AES-128-GCM ciphertext ‖ 16-byte tag; nonce = direction (1 to the device, 2 to the
//   phone) ‖ 7 zero bytes ‖ counter u32 LE. Each way counts from 1 on every connection and only goes up: an envelope
//   whose counter is not above the last one taken is a replay and is dropped like one that does not open.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mbedtls/gcm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YK_SEAL_NONCE 8
#define YK_SEAL_KEY 32          // the link key
#define YK_SEAL_SESSION_KEY 16  // the connection's AES-128 key
#define YK_SEAL_COUNTER 4
#define YK_SEAL_TAG 16
#define YK_SEAL_OVERHEAD (YK_SEAL_COUNTER + YK_SEAL_TAG)
#define YK_SEAL_FRAME_MAX 160   // LINK.frame.maxBytes (180, one write) less the envelope
#define YK_SEAL_SERVICE_DATA 5  // the scan response's service data: mark (3, LE), app (2, LE)
#define YK_SEAL_SESSION 14      // the session characteristic
#define YK_SEAL_OPEN 14         // LinkOpen
#define YK_SEAL_VERSION 1
#define YK_SEAL_KIND_OPEN 2
#define YK_SEAL_TO_DEVICE 1
#define YK_SEAL_TO_PHONE 2
#define YK_SEAL_ERR_KEY_MISMATCH 0x80  // ATT application errors on LinkOpen (LINK.openErrors)
#define YK_SEAL_ERR_BUSY 0x81
#define YK_SEAL_ERR_BAD 0x13           // ATT "invalid attribute value length / value": not a LinkOpen
#define YK_SEAL_FAILS_MAX 5            // envelopes in a row that do not open before the phone is let go
#define YK_SEAL_KEY_STALE_S (7 * 24 * 3600)  // fetched again when online after this

/** The device's mark: 0..2^20-1. */
uint32_t yk_seal_mark(const char *device_id);
/** The scan response's service data. */
void yk_seal_service_data(uint32_t mark, uint16_t app, uint8_t out[YK_SEAL_SERVICE_DATA]);
/** The session characteristic. */
void yk_seal_session(const uint8_t device[YK_SEAL_NONCE], uint32_t kid, uint16_t app, uint8_t out[YK_SEAL_SESSION]);
/** A LinkOpen: its phone bytes and kid (not 0); false when it is not one. */
bool yk_seal_open_parse(const uint8_t *b, size_t n, uint8_t phone[YK_SEAL_NONCE], uint32_t *kid);
/** The connection's key. */
bool yk_seal_session_key(const uint8_t key[YK_SEAL_KEY], uint16_t app, const uint8_t device[YK_SEAL_NONCE],
                         const uint8_t phone[YK_SEAL_NONCE], uint8_t out[YK_SEAL_SESSION_KEY]);
/** An envelope of `n` bytes of `plain` into `out` (n + YK_SEAL_OVERHEAD bytes), `gcm` keyed with the connection key. */
bool yk_seal(mbedtls_gcm_context *gcm, uint8_t direction, uint32_t counter, const uint8_t *plain, size_t n, uint8_t *out);
/** An envelope opened into `plain` (n − YK_SEAL_OVERHEAD bytes): its length, or -1 (too short, a counter not above
 *  *last, a tag that does not match). On success *last is its counter. */
int yk_unseal(mbedtls_gcm_context *gcm, uint8_t direction, uint32_t *last, const uint8_t *env, size_t n, uint8_t *plain);
/** The link key was fetched at `fetched_s` (Unix seconds; 0 unknown): fetch it again when online. */
bool yk_seal_key_stale(int64_t fetched_s, int64_t now_s);

/** One connection's side of the link, on the device. */
typedef struct {
    uint16_t app;
    bool have_key;
    uint32_t kid;
    uint8_t key[YK_SEAL_KEY];
    uint8_t device[YK_SEAL_NONCE];  // this connection's random bytes
    bool open;                      // LinkOpen taken: envelopes go both ways
    bool mismatch;                  // a phone came with another kid (this device's key is likely old)
    uint32_t tx, rx;                // the last counter sent, taken
    int fails;                      // envelopes in a row that did not open
    mbedtls_gcm_context gcm;
} yk_seal_link_t;

/** Once, with the key the device holds (NULL: none). */
void yk_seal_link_init(yk_seal_link_t *l, uint16_t app, const uint8_t *key, uint32_t kid);
void yk_seal_link_free(yk_seal_link_t *l);
/** A new connection: this connection's random bytes; nothing open. */
void yk_seal_link_connect(yk_seal_link_t *l, const uint8_t device[YK_SEAL_NONCE]);
/** The session characteristic for this connection. */
void yk_seal_link_session(const yk_seal_link_t *l, uint8_t out[YK_SEAL_SESSION]);
/** A write of the phone's before the link is open: 0 (open now), or the ATT error to answer with. */
int yk_seal_link_open(yk_seal_link_t *l, const uint8_t *b, size_t n);
/** An envelope from the phone: the frame's length in `frame` (YK_SEAL_FRAME_MAX or more bytes of room), or -1 (not
 *  open, too long, does not open, a replay; fails counts those in a row once open). */
int yk_seal_link_receive(yk_seal_link_t *l, const uint8_t *env, size_t n, uint8_t *frame);
/** A frame to the phone sealed into `env` (n + YK_SEAL_OVERHEAD bytes): its length, or -1 (not open, too long). */
int yk_seal_link_send(yk_seal_link_t *l, const uint8_t *frame, size_t n, uint8_t *env);

#ifdef __cplusplus
}
#endif
