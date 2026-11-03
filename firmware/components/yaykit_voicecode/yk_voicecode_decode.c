// 声波码: Reed-Solomon decoding with erasures, and yk_vc_decode_levels (README.md; the contract's decodeVoiceCode).
// Plain C99.
// Thread-safe: nothing outside the caller's stack; the GF(256) tables are built per call (yk_vc_gf_t).
//
// The codeword c[0..11] is c(x) = c[0] x^11 + ... + c[11], a multiple of g(x) = (x - a^0)...(x - a^5); byte j sits at
// x^(11 - j), so its locator is X_j = a^(11 - j). Decoding: syndromes S_i = r(a^i) -> erasure locator ->
// Berlekamp-Massey started from it -> Chien search over the 12 positions -> Forney -> syndromes again, all zero.
#include <string.h>

#include "yk_voicecode.h"
#include "yk_voicecode_priv.h"

#define NB YK_VC_BYTES
#define NS YK_VC_PARITY_BYTES

static uint8_t gf_inv(const yk_vc_gf_t *gf, uint8_t a) { return gf->exp[255 - gf->log[a]]; }  // a != 0

static uint8_t gf_pow(const yk_vc_gf_t *gf, uint8_t a, int n) {
    if (n == 0) return 1;
    if (a == 0) return 0;
    return gf->exp[(gf->log[a] * n) % 255];
}

/** p(x) at x, p[i] the coefficient of x^i, len coefficients. */
static uint8_t poly_eval(const yk_vc_gf_t *gf, const uint8_t *p, int len, uint8_t x) {
    uint8_t y = 0;
    for (int i = len - 1; i >= 0; i--) y = yk_vc_gf_mul(gf, y, x) ^ p[i];
    return y;
}

/** S_i = c(a^i), i = 0..5. Returns whether any is non-zero. */
static int syndromes(const yk_vc_gf_t *gf, const uint8_t cw[NB], uint8_t s[NS]) {
    uint8_t any = 0;
    for (int i = 0; i < NS; i++) {
        const uint8_t x = gf->exp[i];
        uint8_t y = 0;
        for (int j = 0; j < NB; j++) y = yk_vc_gf_mul(gf, y, x) ^ cw[j];  // Horner, c[0] first
        s[i] = y;
        any |= y;
    }
    return any != 0;
}

/** Corrects cw in place; erased[j] marks the erasures (their bytes are ignored). Returns the bytes repaired (every
 *  erasure, plus the wrong bytes found), or -1 when 2 x errors + erasures > 6 or the result is not a codeword. */
static int rs_decode(const yk_vc_gf_t *gf, uint8_t cw[NB], const uint8_t erased[NB]) {
    int ne = 0;
    for (int j = 0; j < NB; j++) {
        if (erased[j]) {
            cw[j] = 0;
            ne++;
        }
    }
    if (ne > NS) return -1;
    uint8_t s[NS];
    if (!syndromes(gf, cw, s)) return ne;  // the erased bytes were zero and nothing else is wrong

    // Erasure locator Gamma(x) = prod (1 + X_j x); lam[i] is the coefficient of x^i.
    uint8_t lam[NS + 1] = {1}, b[NS + 1], t[NS + 1];
    int deg = 0;
    for (int j = 0; j < NB; j++) {
        if (!erased[j]) continue;
        const uint8_t X = gf->exp[NB - 1 - j];
        for (int i = deg + 1; i > 0; i--) lam[i] ^= yk_vc_gf_mul(gf, lam[i - 1], X);
        deg++;
    }
    memcpy(b, lam, sizeof b);

    // Berlekamp-Massey from the erasure locator (Blahut's errata decoder): L starts at the number of erasures.
    int L = ne;
    for (int r = ne + 1; r <= NS; r++) {
        uint8_t delta = 0;
        for (int i = 0; i <= L && i <= r - 1; i++) delta ^= yk_vc_gf_mul(gf, lam[i], s[r - 1 - i]);
        memmove(b + 1, b, NS);  // b <- x b (its degree stays <= 6, see the loop bound)
        b[0] = 0;
        if (delta == 0) continue;
        for (int i = 0; i <= NS; i++) t[i] = lam[i] ^ yk_vc_gf_mul(gf, delta, b[i]);
        if (2 * L <= r + ne - 1) {
            const uint8_t inv = gf_inv(gf, delta);
            for (int i = 0; i <= NS; i++) b[i] = yk_vc_gf_mul(gf, inv, lam[i]);
            L = r + ne - L;
        }
        memcpy(lam, t, sizeof lam);
    }
    const int errors = L - ne;
    if (errors < 0 || 2 * errors + ne > NS) return -1;
    int dl = NS;
    while (dl > 0 && lam[dl] == 0) dl--;
    if (dl != L) return -1;

    // Chien search over the 12 positions only: every root must be one of them.
    int pos[NS], roots = 0;
    for (int j = 0; j < NB; j++) {
        const uint8_t xinv = gf->exp[(255 - (NB - 1 - j)) % 255];
        if (poly_eval(gf, lam, dl + 1, xinv) == 0) {
            if (roots == NS) return -1;
            pos[roots++] = j;
        }
    }
    if (roots != dl) return -1;

    // Forney (first root a^0): e = X * Omega(X^-1) / Lambda'(X^-1), Omega = S(x) Lambda(x) mod x^6.
    uint8_t om[NS] = {0};
    for (int i = 0; i < NS; i++)
        for (int k = 0; k <= i && k <= dl; k++) om[i] ^= yk_vc_gf_mul(gf, lam[k], s[i - k]);
    int repaired = ne;
    for (int r = 0; r < roots; r++) {
        const int j = pos[r];
        const uint8_t X = gf->exp[NB - 1 - j];
        const uint8_t xinv = gf_inv(gf, X);
        uint8_t den = 0;
        for (int i = 1; i <= dl; i += 2) den ^= yk_vc_gf_mul(gf, lam[i], gf_pow(gf, xinv, i - 1));
        if (den == 0) return -1;
        const uint8_t e = yk_vc_gf_mul(gf, yk_vc_gf_mul(gf, X, poly_eval(gf, om, NS, xinv)), gf_inv(gf, den));
        if (e && !erased[j]) repaired++;
        cw[j] ^= e;
    }
    if (syndromes(gf, cw, s)) return -1;
    return repaired;
}

int yk_vc_decode_codeword(const int8_t levels[YK_VC_BARS], uint8_t cw[YK_VC_BYTES]) {
    uint8_t erased[NB];
    for (int j = 0; j < NB; j++) {
        unsigned byte = 0;
        erased[j] = 0;
        for (int t = 0; t < 4; t++) {
            const int level = levels[4 * j + t];
            if (level < 0 || level >= YK_VC_LEVELS) {
                erased[j] = 1;  // -1 is an unreadable bar; anything else out of range is treated the same
            } else {
                byte |= (unsigned)level << (6 - 2 * t);
            }
        }
        cw[j] = erased[j] ? 0 : (uint8_t)(byte ^ yk_vc_mask[j]);
    }
    yk_vc_gf_t gf;
    yk_vc_gf_init(&gf);
    return rs_decode(&gf, cw, erased);
}

int yk_vc_decode_levels(const int8_t levels[YK_VC_BARS], yk_vc_payload_t *payload, int *corrected) {
    if (corrected) *corrected = 0;
    if (!levels || !payload) return -1;
    uint8_t cw[NB];
    const int repaired = yk_vc_decode_codeword(levels, cw);
    if (repaired < 0) return -1;
    if ((cw[0] >> 6) != YK_VC_VERSION) {
        if (corrected) *corrected = repaired;
        return YK_VC_DECODE_LATER;
    }
    yk_vc_payload_t p;
    if (yk_vc_unpack(cw, &p) != 0) return -1;
    *payload = p;
    if (corrected) *corrected = repaired;
    return 0;
}
