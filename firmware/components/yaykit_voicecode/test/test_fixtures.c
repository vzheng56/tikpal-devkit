// yaykit_voicecode against the contract's own examples: packages/contracts/fixtures/voice-code.json, written from
// packages/contracts/src/voice-code.ts (voice-code-cases.ts). Every case of every section is checked: constants,
// encode (levels, and back through the decoder), refuse, damaged, notCodes, later, actions. cJSON comes from
// ESP-IDF (test/run.sh).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "yk_voicecode.h"

#define PI 3.14159265358979323846

static int failures, checks;
#define CHECK_CASE(cond, name)                                                     \
    do {                                                                           \
        checks++;                                                                  \
        if (!(cond)) {                                                             \
            failures++;                                                            \
            printf("FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #cond, name);      \
        }                                                                          \
    } while (0)

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = malloc((size_t)n + 1);
    if (s && fread(s, 1, (size_t)n, f) != (size_t)n) {
        free(s);
        s = NULL;
    }
    if (s) s[n] = 0;
    fclose(f);
    return s;
}

static const cJSON *get(const cJSON *o, const char *key) { return cJSON_GetObjectItemCaseSensitive(o, key); }

static const char *str(const cJSON *o, const char *key) {
    const cJSON *v = get(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static double num(const cJSON *o, const char *key) {
    const cJSON *v = get(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : NAN;
}

/** A JSON number as the C field takes it: what a caller holding that number would pass. -1 becomes 2^64 - 1. */
static uint64_t as_u64(double v) { return v < 0 ? (uint64_t)(int64_t)v : (uint64_t)v; }
static uint32_t as_u32(double v) { return v < 0 ? (uint32_t)(int32_t)v : v > 4294967295.0 ? UINT32_MAX : (uint32_t)v; }

/** The fixture's payload -> the C payload, as a caller would build it. Returns 0, or -1 where the C side refuses it
 *  on the way in (the pair and selfie text, through yk_vc_pair / yk_vc_selfie; an unknown kind). */
static int payload_in(const cJSON *j, yk_vc_payload_t *p) {
    const char *kind = str(j, "kind");
    memset(p, 0, sizeof *p);
    if (!kind) return -1;
    if (!strcmp(kind, "test")) {
        p->kind = YK_VC_TEST;
        p->value = as_u64(num(j, "value"));
        return 0;
    }
    if (!strcmp(kind, "lookup")) {
        p->kind = YK_VC_LOOKUP;
        p->value = as_u64(num(j, "id"));
        return 0;
    }
    if (!strcmp(kind, "pair")) return yk_vc_pair(str(j, "suffix"), str(j, "code"), p);
    if (!strcmp(kind, "selfie")) return yk_vc_selfie(str(j, "suffix"), str(j, "password"), p);
    if (!strcmp(kind, "app")) {
        p->kind = YK_VC_APP;
        p->app = as_u32(num(j, "app"));
        p->action = as_u32(num(j, "action"));
        p->value = as_u64(num(j, "value"));
        return 0;
    }
    return -1;
}

/** The decoded payload is the fixture's, field by field and in the contract's own text. */
static int payload_is(const yk_vc_payload_t *p, const cJSON *j) {
    const char *kind = str(j, "kind");
    if (!kind) return 0;
    char suffix[5], digits[9];
    yk_vc_suffix_text(p, suffix);
    yk_vc_digits_text(p, digits);
    if (!strcmp(kind, "test")) return p->kind == YK_VC_TEST && (double)p->value == num(j, "value");
    if (!strcmp(kind, "lookup")) return p->kind == YK_VC_LOOKUP && (double)p->value == num(j, "id");
    if (!strcmp(kind, "pair"))
        return p->kind == YK_VC_PAIR && !strcmp(suffix, str(j, "suffix")) && !strcmp(digits, str(j, "code"));
    if (!strcmp(kind, "selfie"))
        return p->kind == YK_VC_SELFIE && !strcmp(suffix, str(j, "suffix")) && !strcmp(digits, str(j, "password"));
    if (!strcmp(kind, "app"))
        return p->kind == YK_VC_APP && (double)p->app == num(j, "app") && (double)p->action == num(j, "action") &&
               (double)p->value == num(j, "value");
    return 0;
}

/** "0".."3" and "." (unread) -> levels. Returns -1 unless the text is 48 bars: the C side takes exactly 48. */
static int levels_in(const char *text, int8_t lv[YK_VC_BARS]) {
    if (!text || strlen(text) != YK_VC_BARS) return -1;
    for (int k = 0; k < YK_VC_BARS; k++) {
        const char c = text[k];
        if (c == '.') lv[k] = -1;
        else if (c >= '0' && c <= '3') lv[k] = (int8_t)(c - '0');
        else return -1;
    }
    return 0;
}

static void check_constants(const cJSON *c) {
    const char *n = "constants";
    CHECK_CASE(num(c, "version") == YK_VC_VERSION && num(c, "bars") == YK_VC_BARS && num(c, "levels") == YK_VC_LEVELS, n);
    CHECK_CASE(num(c, "dataBytes") == YK_VC_DATA_BYTES && num(c, "parityBytes") == YK_VC_PARITY_BYTES, n);
    // the mask, through the encoder: data bytes that make an all-zero codeword draw the mask itself
    const cJSON *mask = get(c, "mask");
    CHECK_CASE(cJSON_GetArraySize(mask) == YK_VC_BYTES, n);
    yk_vc_payload_t zero = {.kind = YK_VC_TEST, .value = 0};
    uint8_t lv[YK_VC_BARS];
    CHECK_CASE(yk_vc_encode(&zero, lv) == 0, n);
    for (int j = 0; j < YK_VC_BYTES; j++) {
        const unsigned byte = (unsigned)(lv[4 * j] << 6 | lv[4 * j + 1] << 4 | lv[4 * j + 2] << 2 | lv[4 * j + 3]);
        CHECK_CASE(byte == (unsigned)cJSON_GetArrayItem(mask, j)->valueint, "constants.mask");
    }
    const cJSON *l = get(c, "layout");
    const struct {
        const char *key;
        float c;
    } layout[] = {{"ring", YK_VC_RING_R},       {"ringWidth", YK_VC_RING_W}, {"dot", YK_VC_DOT_R},
                  {"dotSize", YK_VC_DOT_SIZE},  {"base", YK_VC_BASE_R},      {"barWidth", YK_VC_BAR_W},
                  {"length0", YK_VC_LEN0},      {"lengthStep", YK_VC_LEN_STEP}, {"logoMax", YK_VC_LOGO_MAX_R}};
    for (size_t i = 0; i < sizeof layout / sizeof layout[0]; i++)
        CHECK_CASE(fabs(num(l, layout[i].key) - layout[i].c) < 1e-6, layout[i].key);
    for (int level = 0; level < YK_VC_LEVELS; level++)
        CHECK_CASE(fabs(yk_vc_bar_length(level) - (num(l, "length0") + num(l, "lengthStep") * level)) < 1e-6, "bar length");
    for (int k = 0; k < YK_VC_BARS; k++)
        CHECK_CASE(fabs(yk_vc_bar_angle(k) - (-PI / 2 + (k + 0.5) * 2 * PI / YK_VC_BARS)) < 1e-5, "bar angle");
    const cJSON *kinds = get(c, "kinds");
    CHECK_CASE(num(kinds, "test") == YK_VC_TEST && num(kinds, "pair") == YK_VC_PAIR && num(kinds, "lookup") == YK_VC_LOOKUP &&
                   num(kinds, "app") == YK_VC_APP && cJSON_GetArraySize(kinds) == 4,
               "constants.kinds");
    CHECK_CASE(num(get(c, "pairBit"), "pair") == 0 && num(get(c, "pairBit"), "selfie") == 1, "constants.pairBit");
    const char *prefix = str(get(c, "selfie"), "ssidPrefix");
    CHECK_CASE(prefix && !strcmp(prefix, YK_VC_SELFIE_SSID_PREFIX), "constants.selfie.ssidPrefix");
    const cJSON *actions = get(get(c, "app"), "actions"), *own = get(get(c, "app"), "own");
    CHECK_CASE(num(actions, "page") == YK_VC_ACTION_PAGE && num(actions, "service") == YK_VC_ACTION_SERVICE &&
                   num(actions, "day") == YK_VC_ACTION_DAY && num(actions, "ask") == YK_VC_ACTION_ASK,
               "constants.app.actions");
    CHECK_CASE(num(own, "from") == YK_VC_ACTION_OWN_FROM && num(own, "to") == YK_VC_ACTION_OWN_TO, "constants.app.own");
}

static int check_encode(const cJSON *cases) {
    int n = 0;
    const cJSON *c;
    cJSON_ArrayForEach(c, cases) {
        const char *name = str(c, "name");
        const cJSON *pj = get(c, "payload");
        yk_vc_payload_t p;
        uint8_t lv[YK_VC_BARS];
        CHECK_CASE(payload_in(pj, &p) == 0, name);
        CHECK_CASE(yk_vc_encode(&p, lv) == 0, name);
        char text[YK_VC_BARS + 1];
        for (int k = 0; k < YK_VC_BARS; k++) text[k] = (char)('0' + lv[k]);
        text[YK_VC_BARS] = 0;
        const char *want = str(c, "levels");
        CHECK_CASE(want && !strcmp(text, want), name);
        if (want && strcmp(text, want)) printf("  levels %s\n  want   %s\n", text, want);
        // and back
        int8_t l8[YK_VC_BARS];
        yk_vc_payload_t q;
        int corrected = -1;
        CHECK_CASE(levels_in(want, l8) == 0 && yk_vc_decode_levels(l8, &q, &corrected) == 0, name);
        CHECK_CASE(payload_is(&q, pj) && corrected == 0, name);
        const cJSON *hotspot = get(c, "hotspot");
        char ssid[YK_VC_SELFIE_SSID_SIZE], password[9];
        if (hotspot) {
            CHECK_CASE(yk_vc_selfie_hotspot(&q, ssid, password) == 0, name);
            CHECK_CASE(!strcmp(ssid, str(hotspot, "ssid")) && !strcmp(password, str(hotspot, "password")), name);
        } else {
            CHECK_CASE(yk_vc_selfie_hotspot(&q, ssid, password) != 0, name);
        }
        n++;
    }
    return n;
}

static int check_refuse(const cJSON *cases) {
    int n = 0;
    const cJSON *c;
    cJSON_ArrayForEach(c, cases) {
        yk_vc_payload_t p;
        uint8_t lv[YK_VC_BARS];
        CHECK_CASE(payload_in(get(c, "payload"), &p) != 0 || yk_vc_encode(&p, lv) != 0, str(c, "name"));
        n++;
    }
    return n;
}

static int check_damaged(const cJSON *cases) {
    int n = 0;
    const cJSON *c;
    cJSON_ArrayForEach(c, cases) {
        const char *name = str(c, "name");
        int8_t lv[YK_VC_BARS];
        CHECK_CASE(levels_in(str(c, "levels"), lv) == 0, name);
        const cJSON *reading = get(c, "reading");
        yk_vc_payload_t p;
        int corrected = -1;
        const int r = yk_vc_decode_levels(lv, &p, &corrected);
        if (cJSON_IsNull(reading)) {
            CHECK_CASE(r == -1, name);
        } else {
            CHECK_CASE(r == 0 && payload_is(&p, get(reading, "payload")), name);
            CHECK_CASE(corrected == (int)num(reading, "corrected"), name);
        }
        n++;
    }
    return n;
}

static int check_not_codes(const cJSON *cases) {
    int n = 0;
    const cJSON *c;
    cJSON_ArrayForEach(c, cases) {
        int8_t lv[YK_VC_BARS];
        yk_vc_payload_t p;
        CHECK_CASE(levels_in(str(c, "levels"), lv) != 0 || yk_vc_decode_levels(lv, &p, NULL) == -1, str(c, "name"));
        n++;
    }
    return n;
}

static int check_later(const cJSON *cases) {
    int n = 0;
    const cJSON *c;
    cJSON_ArrayForEach(c, cases) {
        int8_t lv[YK_VC_BARS];
        yk_vc_payload_t p;
        CHECK_CASE(levels_in(str(c, "levels"), lv) == 0 && yk_vc_decode_levels(lv, &p, NULL) == YK_VC_DECODE_LATER,
                   str(c, "name"));
        n++;
    }
    return n;
}

static int check_actions(const cJSON *cases) {
    int n = 0;
    const cJSON *c;
    cJSON_ArrayForEach(c, cases) {
        const char *does = str(c, "does");
        CHECK_CASE(does && !strcmp(yk_vc_action_does(as_u32(num(c, "action"))), does), "actions");
        n++;
    }
    return n;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "../../../../../packages/contracts/fixtures/voice-code.json";
    char *text = read_file(path);
    if (!text) {
        printf("FAIL: cannot read %s\n", path);
        return 1;
    }
    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) {
        printf("FAIL: %s is not JSON\n", path);
        return 1;
    }
    check_constants(get(root, "constants"));
    const int enc = check_encode(get(root, "encode"));
    const int ref = check_refuse(get(root, "refuse"));
    const int dmg = check_damaged(get(root, "damaged"));
    const int nc = check_not_codes(get(root, "notCodes"));
    const int lat = check_later(get(root, "later"));
    const int act = check_actions(get(root, "actions"));
    cJSON_Delete(root);
    // an empty section would pass silently: every section must have cases
    CHECK_CASE(enc > 0 && ref > 0 && dmg > 0 && nc > 0 && lat > 0 && act > 0, "sections");
    printf("yaykit_voicecode contract examples (fixtures/voice-code.json): encode %d, refuse %d, damaged %d, "
           "notCodes %d, later %d, actions %d cases; %d checks, %d failures\n",
           enc, ref, dmg, nc, lat, act, checks, failures);
    return failures ? 1 : 0;
}
