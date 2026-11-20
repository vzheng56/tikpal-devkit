// A plain SHA-256 behind mbedTLS's names, so that the host tests run with no ESP-IDF. test_tf_pkg.c holds it to the
// standard's examples.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t h[8];
    uint64_t total;
    uint8_t block[64];
    size_t used;
} mbedtls_sha256_context;

static const uint32_t SHIM_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static inline uint32_t shim_ror(uint32_t x, int n) { return x >> n | x << (32 - n); }

static inline void shim_block(mbedtls_sha256_context *c, const uint8_t *p) {
    uint32_t w[64], v[8];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        const uint32_t a = shim_ror(w[i - 15], 7) ^ shim_ror(w[i - 15], 18) ^ w[i - 15] >> 3;
        const uint32_t b = shim_ror(w[i - 2], 17) ^ shim_ror(w[i - 2], 19) ^ w[i - 2] >> 10;
        w[i] = w[i - 16] + a + w[i - 7] + b;
    }
    memcpy(v, c->h, sizeof(v));
    for (int i = 0; i < 64; i++) {
        const uint32_t t1 = v[7] + (shim_ror(v[4], 6) ^ shim_ror(v[4], 11) ^ shim_ror(v[4], 25)) +
                            ((v[4] & v[5]) ^ (~v[4] & v[6])) + SHIM_K[i] + w[i];
        const uint32_t t2 = (shim_ror(v[0], 2) ^ shim_ror(v[0], 13) ^ shim_ror(v[0], 22)) +
                            ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        memmove(v + 1, v, sizeof(v[0]) * 7);
        v[4] += t1;
        v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) c->h[i] += v[i];
}

static inline void mbedtls_sha256_init(mbedtls_sha256_context *c) { memset(c, 0, sizeof(*c)); }
static inline void mbedtls_sha256_free(mbedtls_sha256_context *c) { memset(c, 0, sizeof(*c)); }

static inline int mbedtls_sha256_starts(mbedtls_sha256_context *c, int is224) {
    static const uint32_t H[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    (void)is224;
    memcpy(c->h, H, sizeof(H));
    c->total = 0;
    c->used = 0;
    return 0;
}

static inline int mbedtls_sha256_update(mbedtls_sha256_context *c, const unsigned char *in, size_t size) {
    c->total += size;
    while (size) {
        const size_t n = 64 - c->used < size ? 64 - c->used : size;
        memcpy(c->block + c->used, in, n);
        c->used += n;
        in += n;
        size -= n;
        if (c->used == 64) {
            shim_block(c, c->block);
            c->used = 0;
        }
    }
    return 0;
}

static inline int mbedtls_sha256_finish(mbedtls_sha256_context *c, unsigned char out[32]);

static inline int mbedtls_sha256(const unsigned char *in, size_t size, unsigned char out[32], int is224) {
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c);
    mbedtls_sha256_starts(&c, is224);
    mbedtls_sha256_update(&c, in, size);
    return mbedtls_sha256_finish(&c, out);
}

static inline int mbedtls_sha256_finish(mbedtls_sha256_context *c, unsigned char out[32]) {
    const uint64_t bits = c->total * 8;
    const uint8_t one = 0x80, zero = 0;
    mbedtls_sha256_update(c, &one, 1);
    while (c->used != 56) mbedtls_sha256_update(c, &zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - i * 8));
    mbedtls_sha256_update(c, len, 8);
    for (int i = 0; i < 32; i++) out[i] = (uint8_t)(c->h[i / 4] >> (24 - i % 4 * 8));
    return 0;
}
