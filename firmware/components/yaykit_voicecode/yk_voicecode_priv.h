// Shared by the encoder (yk_voicecode.c), the decoder (yk_voicecode_decode.c) and the reader (yk_voicecode_scan.c):
// GF(256) with the polynomial x^8 + x^4 + x^3 + x^2 + 1 (0x11d) and alpha = 2. The tables are built on the caller's
// stack each time, so nothing is shared between threads (the apps decode on a camera queue).
#pragma once

#include <stdint.h>

#include "yk_voicecode.h"

typedef struct {
    uint8_t exp[512];
    uint8_t log[256];
} yk_vc_gf_t;

void yk_vc_gf_init(yk_vc_gf_t *gf);

static inline uint8_t yk_vc_gf_mul(const yk_vc_gf_t *gf, uint8_t a, uint8_t b) {
    return (a && b) ? gf->exp[gf->log[a] + gf->log[b]] : 0;
}

/** The fixed mask XORed over the 12 codeword bytes after encoding (and before decoding). */
extern const uint8_t yk_vc_mask[YK_VC_BYTES];

/** The 6 data bytes as they are (any version and kind) -> the 48 levels: parity, then the mask. The contract's
 *  encodeVoiceCodeBytes; for the reader and the tests. */
void yk_vc_encode_data(const uint8_t data[YK_VC_DATA_BYTES], uint8_t levels[YK_VC_BARS]);

/** The 48 levels -> the repaired 12-byte codeword (unmasked). Returns the bytes repaired, or -1 (yk_vc_decode_levels
 *  without looking at the version or the payload). */
int yk_vc_decode_codeword(const int8_t levels[YK_VC_BARS], uint8_t cw[YK_VC_BYTES]);

/** The data bytes of a codeword of this version -> payload. Returns 0, or -1 for bits no payload has. */
int yk_vc_unpack(const uint8_t data[YK_VC_DATA_BYTES], yk_vc_payload_t *payload);
