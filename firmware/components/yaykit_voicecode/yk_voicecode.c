// 声波码: layout, payloads and the encoder (README.md; the contract is packages/contracts/src/voice-code.ts). The
// decoder is yk_voicecode_decode.c, the camera reader yk_voicecode_scan.c.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "yk_voicecode.h"
#include "yk_voicecode_priv.h"

#define PI_F 3.14159265358979f
#define BITS44 ((1ULL << 44) - 1)

const uint8_t yk_vc_mask[YK_VC_BYTES] = {0x5B, 0xC2, 0x37, 0x9E, 0x64, 0xA9, 0x1D, 0xF0, 0x83, 0x4E, 0xB6, 0x29};

void yk_vc_gf_init(yk_vc_gf_t *gf) {
    unsigned x = 1;
    for (int i = 0; i < 255; i++) {
        gf->exp[i] = (uint8_t)x;
        gf->log[x] = (uint8_t)i;
        x <<= 1;
        if (x & 0x100) x ^= 0x11d;
    }
    for (int i = 255; i < 512; i++) gf->exp[i] = gf->exp[i - 255];
    gf->log[0] = 0;  // never used: yk_vc_gf_mul checks for zero first
}

float yk_vc_bar_angle(int k) { return -PI_F / 2 + (k + 0.5f) * (2 * PI_F / YK_VC_BARS); }

float yk_vc_bar_length(int level) { return YK_VC_LEN0 + YK_VC_LEN_STEP * (float)level; }

static int parse_text(const char *s, int len, int base, uint32_t *out) {
    if (!s) return -1;
    uint32_t v = 0;
    for (int i = 0; i < len; i++) {
        const char c = s[i];
        if (c >= '0' && c <= '9') v = v * (uint32_t)base + (uint32_t)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') v = v * 16 + (uint32_t)(c - 'a' + 10);
        else return -1;  // also the terminator of a shorter string
    }
    if (s[len] != 0) return -1;
    *out = v;
    return 0;
}

static int pair_or_selfie(yk_vc_kind_t kind, const char *suffix, const char *digits, yk_vc_payload_t *p) {
    uint32_t sx, dg;
    if (!p || parse_text(suffix, 4, 16, &sx) != 0 || parse_text(digits, 8, 10, &dg) != 0) return -1;
    memset(p, 0, sizeof(*p));
    p->kind = kind;
    p->suffix = (uint16_t)sx;
    p->digits = dg;
    return 0;
}

int yk_vc_pair(const char *suffix, const char *digits, yk_vc_payload_t *p) {
    return pair_or_selfie(YK_VC_PAIR, suffix, digits, p);
}

int yk_vc_selfie(const char *suffix, const char *digits, yk_vc_payload_t *p) {
    return pair_or_selfie(YK_VC_SELFIE, suffix, digits, p);
}

/** Payload -> the 6 data bytes. Returns 0, or -1 for a payload that does not fit. */
static int pack(const yk_vc_payload_t *p, uint8_t data[YK_VC_DATA_BYTES]) {
    uint64_t bits;
    unsigned kind;
    switch (p->kind) {
        case YK_VC_TEST:
        case YK_VC_LOOKUP:
            if (p->value > BITS44) return -1;
            kind = (unsigned)p->kind;
            bits = p->value;
            break;
        case YK_VC_PAIR:
        case YK_VC_SELFIE:
            if (p->digits > 99999999u) return -1;
            // suffix in bits 43..28, the 8 digits in bits 27..1, bit 0: 0 pair, 1 selfie
            kind = YK_VC_PAIR;
            bits = ((uint64_t)p->suffix << 28) | ((uint64_t)p->digits << 1) | (p->kind == YK_VC_SELFIE ? 1u : 0u);
            break;
        case YK_VC_APP:
            if (p->app < 1 || p->app > 0xffff || p->action > 15 || p->value >> 24) return -1;
            // the app's number in bits 43..28, the action in 27..24, the value in 23..0
            kind = YK_VC_APP;
            bits = ((uint64_t)p->app << 28) | ((uint64_t)p->action << 24) | p->value;
            break;
        default:
            return -1;
    }
    data[0] = (uint8_t)((YK_VC_VERSION << 6) | (kind << 4) | (unsigned)((bits >> 40) & 0x0f));
    for (int i = 1; i < YK_VC_DATA_BYTES; i++) data[i] = (uint8_t)(bits >> (8 * (YK_VC_DATA_BYTES - 1 - i)));
    return 0;
}

int yk_vc_unpack(const uint8_t data[YK_VC_DATA_BYTES], yk_vc_payload_t *p) {
    const unsigned kind = (data[0] >> 4) & 3;
    uint64_t bits = data[0] & 0x0f;
    for (int i = 1; i < YK_VC_DATA_BYTES; i++) bits = (bits << 8) | data[i];
    memset(p, 0, sizeof(*p));
    switch (kind) {
        case YK_VC_TEST:
        case YK_VC_LOOKUP:
            p->kind = (yk_vc_kind_t)kind;
            p->value = bits;
            return 0;
        case YK_VC_PAIR:
            p->kind = (bits & 1) ? YK_VC_SELFIE : YK_VC_PAIR;
            p->suffix = (uint16_t)(bits >> 28);
            p->digits = (uint32_t)((bits >> 1) & 0x7ffffff);
            return p->digits > 99999999u ? -1 : 0;
        default:  // YK_VC_APP
            p->kind = YK_VC_APP;
            p->app = (uint32_t)(bits >> 28);
            p->action = (uint32_t)((bits >> 24) & 0x0f);
            p->value = bits & 0xffffff;
            return p->app == 0 ? -1 : 0;
    }
}

void yk_vc_encode_data(const uint8_t data[YK_VC_DATA_BYTES], uint8_t levels[YK_VC_BARS]) {
    uint8_t cw[YK_VC_BYTES];
    memcpy(cw, data, YK_VC_DATA_BYTES);

    yk_vc_gf_t gf;
    yk_vc_gf_init(&gf);
    // g(x) = (x - a^0)(x - a^1)...(x - a^5); gen[i] is the coefficient of x^i.
    uint8_t gen[YK_VC_PARITY_BYTES + 1] = {1};
    for (int i = 0; i < YK_VC_PARITY_BYTES; i++) {
        const uint8_t root = gf.exp[i];
        for (int j = i + 1; j > 0; j--) gen[j] = gen[j - 1] ^ yk_vc_gf_mul(&gf, gen[j], root);
        gen[0] = yk_vc_gf_mul(&gf, gen[0], root);
    }
    // Systematic: the parity is data(x) * x^6 mod g(x), highest power first after the data.
    uint8_t reg[YK_VC_PARITY_BYTES] = {0};  // reg[i]: coefficient of x^i of the running remainder
    for (int k = 0; k < YK_VC_DATA_BYTES; k++) {
        const uint8_t feedback = cw[k] ^ reg[YK_VC_PARITY_BYTES - 1];
        for (int j = YK_VC_PARITY_BYTES - 1; j > 0; j--) reg[j] = reg[j - 1] ^ yk_vc_gf_mul(&gf, feedback, gen[j]);
        reg[0] = yk_vc_gf_mul(&gf, feedback, gen[0]);
    }
    for (int i = 0; i < YK_VC_PARITY_BYTES; i++) cw[YK_VC_DATA_BYTES + i] = reg[YK_VC_PARITY_BYTES - 1 - i];

    for (int j = 0; j < YK_VC_BYTES; j++) {
        const uint8_t b = cw[j] ^ yk_vc_mask[j];
        for (int t = 0; t < 4; t++) levels[4 * j + t] = (uint8_t)((b >> (6 - 2 * t)) & 3);
    }
}

int yk_vc_encode(const yk_vc_payload_t *payload, uint8_t levels[YK_VC_BARS]) {
    uint8_t data[YK_VC_DATA_BYTES];
    if (!payload || !levels || pack(payload, data) != 0) return -1;
    yk_vc_encode_data(data, levels);
    return 0;
}

void yk_vc_suffix_text(const yk_vc_payload_t *p, char out[5]) { snprintf(out, 5, "%04x", (unsigned)p->suffix); }

void yk_vc_digits_text(const yk_vc_payload_t *p, char out[9]) {
    snprintf(out, 9, "%08lu", (unsigned long)(p->digits % 100000000u));
}

int yk_vc_selfie_hotspot(const yk_vc_payload_t *p, char ssid[YK_VC_SELFIE_SSID_SIZE], char password[9]) {
    if (!p || p->kind != YK_VC_SELFIE) return -1;
    snprintf(ssid, YK_VC_SELFIE_SSID_SIZE, "%s%04x", YK_VC_SELFIE_SSID_PREFIX, (unsigned)p->suffix);
    yk_vc_digits_text(p, password);
    return 0;
}

const char *yk_vc_action_does(uint32_t action) {
    switch (action) {
        case YK_VC_ACTION_SERVICE: return "service";
        case YK_VC_ACTION_DAY: return "day";
        case YK_VC_ACTION_ASK: return "ask";
        default: return action >= YK_VC_ACTION_OWN_FROM && action <= YK_VC_ACTION_OWN_TO ? "own" : "page";
    }
}

void yk_vc_payload_text(const yk_vc_payload_t *p, char *out, int size) {
    if (!out || size <= 0) return;
    const size_t n = (size_t)size;
    switch (p->kind) {
        case YK_VC_TEST:
            snprintf(out, n, "test 0x%011llX", (unsigned long long)p->value);
            break;
        case YK_VC_LOOKUP:
            snprintf(out, n, "lookup 0x%011llX", (unsigned long long)p->value);
            break;
        case YK_VC_PAIR:
            snprintf(out, n, "pair YK-%04x", (unsigned)p->suffix);
            break;
        case YK_VC_SELFIE:
            snprintf(out, n, "selfie %s%04x", YK_VC_SELFIE_SSID_PREFIX, (unsigned)p->suffix);
            break;
        case YK_VC_APP:
            snprintf(out, n, "app %lu action %lu value %llu", (unsigned long)p->app, (unsigned long)p->action,
                     (unsigned long long)p->value);
            break;
        default:
            snprintf(out, n, "unknown");
            break;
    }
}
