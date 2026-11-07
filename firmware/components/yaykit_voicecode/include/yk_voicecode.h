// 声波码 (voice code): Tikpal's own round code, shown on the round screen and read by the Tikpal app
// (docs/delivery/2026-10-30-voice-code.md). The contract is packages/contracts/src/voice-code.ts; its examples,
// packages/contracts/fixtures/voice-code.json, are checked by test/run.sh. Plain C99, no ESP-IDF: the firmware
// links the encoder and the decoder; the apps and the host tests also link the image reader (yk_voicecode_scan.c).
//
// The code is light marks on a dark round screen, all sizes in units of the ring's radius (YK_VC_RING_R = 1):
//   - a thin ring;
//   - a coral dot at 12 o'clock (which way is up);
//   - 48 bars, each centred on the circle of radius YK_VC_BASE_R and pointing at the centre; bar k sits at
//     yk_vc_bar_angle(k), clockwise from 12 o'clock, and its length is one of four (2 bits);
//   - the logo in the middle, inside YK_VC_LOGO_MAX_R, which carries nothing.
// Bits: 6 data bytes (version, kind, 44 payload bits) + 6 Reed-Solomon parity bytes over GF(256), XORed with a
// fixed mask so that no payload draws an even ring; byte j is bars 4j..4j+3, most significant pair first.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YK_VC_VERSION 0
#define YK_VC_BARS 48
#define YK_VC_LEVELS 4
#define YK_VC_DATA_BYTES 6
#define YK_VC_PARITY_BYTES 6
#define YK_VC_BYTES (YK_VC_DATA_BYTES + YK_VC_PARITY_BYTES)

// Layout, in units of the ring's radius.
#define YK_VC_RING_R 1.0f
#define YK_VC_RING_W 0.02f       // stroke width of the ring
#define YK_VC_DOT_R 0.88f        // centre of the coral dot, straight up from the centre
#define YK_VC_DOT_SIZE 0.045f    // radius of the coral dot
#define YK_VC_BASE_R 0.64f       // every bar is centred on this circle
#define YK_VC_BAR_W 0.03f        // stroke width of a bar (round caps add half of it at each end)
#define YK_VC_LEN0 0.08f         // length of a level-0 bar, cap centre to cap centre
#define YK_VC_LEN_STEP 0.07f     // each level adds this
#define YK_VC_LOGO_MAX_R 0.40f   // the logo stays inside this circle

/** What a code carries. The value is the kind written in the code (bits 5..4 of byte 0), except YK_VC_SELFIE: it is
 *  written as YK_VC_PAIR with payload bit 0 set. */
typedef enum {
    YK_VC_TEST = 0,    // a 44-bit number, for tests
    YK_VC_PAIR = 1,    // Bluetooth pairing: the device YK-<suffix> and its 8-digit pairing code
    YK_VC_LOOKUP = 2,  // a 44-bit id the server hands out and resolves (not used yet)
    YK_VC_APP = 3,     // 应用码: an app's number, an action and a value; the app opens that app's page
    YK_VC_SELFIE = 4,  // 自拍屏's hotspot: Tikpal-Selfie-<suffix>, its WPA2 password the 8 digits
} yk_vc_kind_t;

// 应用码 actions (yk_vc_payload_t.action). 4..7 are not used yet: an app that does not know one opens the app page.
// 8..15 are the app's own: the value's meaning is in its registration.
#define YK_VC_ACTION_PAGE 0     // the app page; value 0
#define YK_VC_ACTION_SERVICE 1  // its whole service page; value 0
#define YK_VC_ACTION_DAY 2      // its records of a day; value: days since 1970-01-01, the round screen's local date
#define YK_VC_ACTION_ASK 3      // 「问 Yay」 with a sentence from the app's registration; value: its index from 0
#define YK_VC_ACTION_OWN_FROM 8
#define YK_VC_ACTION_OWN_TO 15

#define YK_VC_SELFIE_SSID_PREFIX "Tikpal-Selfie-"
#define YK_VC_SELFIE_SSID_SIZE 19  // "Tikpal-Selfie-xxxx" and its terminator

typedef struct {
    yk_vc_kind_t kind;
    uint64_t value;   // YK_VC_TEST: the number, YK_VC_LOOKUP: the id (below 2^44); YK_VC_APP: the value (below 2^24)
    uint16_t suffix;  // YK_VC_PAIR, YK_VC_SELFIE: the device id's last 4 hex digits ("dd76" is 0xdd76)
    uint32_t digits;  // YK_VC_PAIR: the pairing code, YK_VC_SELFIE: the hotspot's password; 0..99999999, 8 digits
    uint32_t app;     // YK_VC_APP: the app's number, 1..65535
    uint32_t action;  // YK_VC_APP: 0..15, YK_VC_ACTION_*
} yk_vc_payload_t;

/** Bar k's angle in radians, screen coordinates (x right, y down): -pi/2 is 12 o'clock and angles grow clockwise.
 *  Bar 0 is the first bar clockwise of 12 o'clock. */
float yk_vc_bar_angle(int k);

/** A bar's length for level 0..3, cap centre to cap centre, in units of the ring's radius. */
float yk_vc_bar_length(int level);

/** A pairing or 自拍屏 payload from text as the device has it: `suffix` exactly 4 lower-case hex digits ("dd76"),
 *  `digits` exactly 8 decimal digits ("06418273"). Returns 0, or -1 when either is not (payload untouched). */
int yk_vc_pair(const char *suffix, const char *digits, yk_vc_payload_t *payload);
int yk_vc_selfie(const char *suffix, const char *digits, yk_vc_payload_t *payload);

/** Payload -> the 48 bar levels (0..3). Returns 0, or -1 for a payload out of range (an app number of 0, a value
 *  too big for its kind, more than 8 digits ...). */
int yk_vc_encode(const yk_vc_payload_t *payload, uint8_t levels[YK_VC_BARS]);

#define YK_VC_DECODE_LATER (-2)

/** The 48 levels read off a code (-1 where a bar could not be read: an erasure) -> payload. Corrects up to 3 wrong
 *  bytes, or up to 6 erased ones (2 x errors + erasures <= 6). Returns 0; YK_VC_DECODE_LATER for a code of a later
 *  version than YK_VC_VERSION (the app asks to be updated; payload untouched); or -1 when the levels are not a code,
 *  have more damage than Reed-Solomon repairs, or carry what no payload can (app number 0, 8 digits over 99999999).
 *  `corrected` (may be NULL) gets the bytes repaired, erased ones included. */
int yk_vc_decode_levels(const int8_t levels[YK_VC_BARS], yk_vc_payload_t *payload, int *corrected);

/** YK_VC_PAIR, YK_VC_SELFIE: the suffix as 4 lower-case hex digits and the 8 digits with their leading zeros. */
void yk_vc_suffix_text(const yk_vc_payload_t *payload, char out[5]);
void yk_vc_digits_text(const yk_vc_payload_t *payload, char out[9]);

/** YK_VC_SELFIE: the hotspot the app joins, "Tikpal-Selfie-<suffix>" and the 8-digit password. Returns 0, or -1 for
 *  another kind. */
int yk_vc_selfie_hotspot(const yk_vc_payload_t *payload, char ssid[YK_VC_SELFIE_SSID_SIZE], char password[9]);

/** YK_VC_APP: what the app does with an action: "page", "service", "day", "ask" or "own" (4..7 open the page). */
const char *yk_vc_action_does(uint32_t action);

/** The payload as text for logs, e.g. "test 0x05A3C91E2B4", "pair YK-dd76", "selfie Tikpal-Selfie-7770",
 *  "app 300 action 2 value 20756". The 8 digits are left out: they are a pairing code or a password. */
void yk_vc_payload_text(const yk_vc_payload_t *payload, char *out, int size);

// ---- Reading a code from a camera frame (yk_voicecode_scan.c; the apps and host tests, not the firmware) ----

/** Why a frame was not read. The numbers never change (logs, tests and both apps know them), but they are not the
 *  reader's order: it reads the levels before it looks for the dot, so YK_VC_NO_DOT got further than
 *  YK_VC_BAD_LEVELS. Which of two got further: yk_vc_status_stage(). */
typedef enum {
    YK_VC_OK = 0,
    YK_VC_NO_BARS,      // too few bar-shaped marks
    YK_VC_NO_CENTRE,    // the marks do not point at a common centre
    YK_VC_NO_DOT,       // no coral dot where 12 o'clock should be
    YK_VC_BAD_LEVELS,   // too many bars missing or between levels
    YK_VC_NOT_DECODED,  // the bits do not decode (too many errors, or not a voice code)
    YK_VC_LATER,        // a code of a later version (YK_VC_DECODE_LATER): the app asks to be updated
} yk_vc_status_t;

typedef struct {
    yk_vc_status_t status;
    yk_vc_payload_t payload;  // when status is YK_VC_OK
    int marks;                // bright marks found in the frame
    int bars;                 // marks accepted as bars of this code
    int erased;               // bars that could not be read
    int corrected;            // bytes repaired by Reed-Solomon
    float centre_x, centre_y; // the code's centre, in pixels
    float radius;             // the ring's radius in pixels, roughly (along the ellipse's longer axis)
    float tilt;               // longer / shorter axis of the bars' circle: 1 seen straight on
    int8_t levels[YK_VC_BARS];
} yk_vc_result_t;

/** Reads a voice code from an 8-bit luma image (bright marks on a dark screen). `stride` is bytes per row.
 *  Allocates and frees its own working memory. Returns result->status. Thread-safe. */
yk_vc_status_t yk_vc_scan(const uint8_t *luma, int width, int height, int stride, yk_vc_result_t *result);

/** A short name for a status, for logs: "ok", "no_bars", "no_centre", "no_dot", "bad_levels", "not_decoded",
 *  "later". */
const char *yk_vc_status_name(yk_vc_status_t status);

/** How far the reader got, in the order of its steps: 0 no_bars, 1 no_centre, 2 bad_levels, 3 no_dot, 4 not_decoded,
 *  5 later, 6 ok; -1 for a number that is no status. yk_vc_scan keeps the try (centre, binarisation) that got
 *  furthest by this, and an app picking the hint for a run of frames should too. */
int yk_vc_status_stage(yk_vc_status_t status);

#ifdef __cplusplus
}
#endif
