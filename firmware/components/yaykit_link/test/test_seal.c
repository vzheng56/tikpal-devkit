// Host tests for the unpaired mode's mark, handshake and envelopes (yk_link_seal.c) against the contract's
// fixtures/device-link.json (made with Node's crypto): what the phones check themselves against too.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../yk_link_seal.h"
#include "cJSON.h"

static int failures, checks;
#define CHECK(c)                                                         \
    do {                                                                 \
        checks++;                                                        \
        if (!(c)) {                                                      \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); \
            failures++;                                                  \
        }                                                                \
    } while (0)

static cJSON *load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)n + 1);
    const size_t got = fread(text, 1, (size_t)n, f);
    fclose(f);
    text[got] = 0;
    cJSON *j = cJSON_Parse(text);
    free(text);
    return j;
}

/** Hex into bytes; the count, or -1. */
static int hex(const char *s, uint8_t *out, size_t room) {
    const size_t n = strlen(s);
    if (n % 2 || n / 2 > room) return -1;
    for (size_t i = 0; i < n / 2; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) return -1;
        out[i] = (uint8_t)v;
    }
    return (int)(n / 2);
}

static const char *str(const cJSON *o, const char *k) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? v->valuestring : "";
}
static double num(const cJSON *o, const char *k) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : -1;
}

int main(int argc, char **argv) {
    cJSON *fx = load(argc > 1 ? argv[1] : "device-link.json");
    if (!fx) {
        fprintf(stderr, "test_seal: no fixtures\n");
        return 1;
    }
    uint8_t key[YK_SEAL_KEY], dev[YK_SEAL_NONCE], phone[YK_SEAL_NONCE], buf[600], out[600];
    CHECK(hex(str(fx, "key"), key, sizeof(key)) == YK_SEAL_KEY);
    CHECK(hex(str(fx, "deviceNonce"), dev, sizeof(dev)) == YK_SEAL_NONCE);
    CHECK(hex(str(fx, "phoneNonce"), phone, sizeof(phone)) == YK_SEAL_NONCE);
    const uint32_t kid = (uint32_t)num(fx, "kid");
    const uint16_t app = (uint16_t)num(fx, "app");

    // The limits the firmware restates.
    const cJSON *lim = cJSON_GetObjectItemCaseSensitive(fx, "limits");
    const cJSON *crypto = cJSON_GetObjectItemCaseSensitive(lim, "crypto"), *frame = cJSON_GetObjectItemCaseSensitive(lim, "frame");
    CHECK(num(cJSON_GetObjectItemCaseSensitive(lim, "key"), "bytes") == YK_SEAL_KEY);
    CHECK(num(crypto, "counterBytes") == YK_SEAL_COUNTER && num(crypto, "tagBytes") == YK_SEAL_TAG);
    CHECK(num(crypto, "toDevice") == YK_SEAL_TO_DEVICE && num(crypto, "toPhone") == YK_SEAL_TO_PHONE);
    CHECK(!strcmp(str(crypto, "sessionInfo"), "tikpal-link-session-v1"));
    CHECK(num(frame, "maxBytes") == YK_SEAL_FRAME_MAX + YK_SEAL_OVERHEAD && num(frame, "version") == YK_SEAL_VERSION);
    CHECK(num(cJSON_GetObjectItemCaseSensitive(lim, "kind"), "open") == YK_SEAL_KIND_OPEN);
    CHECK(num(lim, "badEnvelopes") == YK_SEAL_FAILS_MAX);
    const cJSON *errs = cJSON_GetObjectItemCaseSensitive(lim, "openErrors");
    CHECK(num(errs, "keyMismatch") == YK_SEAL_ERR_KEY_MISMATCH && num(errs, "busy") == YK_SEAL_ERR_BUSY);
    CHECK(num(cJSON_GetObjectItemCaseSensitive(lim, "mark"), "serviceDataBytes") == YK_SEAL_SERVICE_DATA);
    CHECK(!strcmp(str(cJSON_GetObjectItemCaseSensitive(lim, "mark"), "label"), "yay-mark-v1"));

    // Marks and the scan response's service data.
    const cJSON *m;
    cJSON_ArrayForEach(m, cJSON_GetObjectItemCaseSensitive(fx, "marks")) {
        const uint32_t mark = yk_seal_mark(str(m, "deviceId"));
        CHECK(mark == (uint32_t)num(m, "mark"));
        uint8_t sd[YK_SEAL_SERVICE_DATA], want[8];
        yk_seal_service_data(mark, app, sd);
        CHECK(hex(str(m, "serviceData"), want, sizeof(want)) == YK_SEAL_SERVICE_DATA && !memcmp(sd, want, sizeof(sd)));
    }

    // The session characteristic and LinkOpen.
    uint8_t session[YK_SEAL_SESSION];
    yk_seal_session(dev, kid, app, session);
    CHECK(hex(str(fx, "session"), buf, sizeof(buf)) == YK_SEAL_SESSION && !memcmp(session, buf, sizeof(session)));
    uint8_t open[YK_SEAL_OPEN], got_phone[YK_SEAL_NONCE];
    uint32_t got_kid = 0;
    CHECK(hex(str(fx, "open"), open, sizeof(open)) == YK_SEAL_OPEN);
    CHECK(yk_seal_open_parse(open, sizeof(open), got_phone, &got_kid) && got_kid == kid &&
          !memcmp(got_phone, phone, sizeof(phone)));
    cJSON_ArrayForEach(m, cJSON_GetObjectItemCaseSensitive(fx, "badOpens")) {
        const int n = hex(str(m, "bytes"), buf, sizeof(buf));
        CHECK(n > 0 && !yk_seal_open_parse(buf, (size_t)n, got_phone, &got_kid));
    }

    // The connection's key.
    uint8_t skey[YK_SEAL_SESSION_KEY];
    CHECK(yk_seal_session_key(key, app, dev, phone, skey));
    CHECK(hex(str(fx, "sessionKey"), buf, sizeof(buf)) == YK_SEAL_SESSION_KEY && !memcmp(skey, buf, sizeof(skey)));

    // The link as the device runs it: session, open, the phone's envelopes in, the device's out, byte for byte.
    yk_seal_link_t *l = calloc(1, sizeof(*l));
    yk_seal_link_init(l, app, key, kid);
    yk_seal_link_connect(l, dev);
    yk_seal_link_session(l, buf);
    CHECK(!memcmp(buf, session, sizeof(session)));
    uint8_t first_in[200];
    int first_in_n = -1;
    CHECK(yk_seal_link_receive(l, open, sizeof(open), out) == -1);  // nothing open yet
    CHECK(yk_seal_link_open(l, open, sizeof(open)) == 0 && l->open);
    CHECK(yk_seal_link_open(l, open, sizeof(open)) == YK_SEAL_ERR_BUSY);  // one open a connection
    cJSON_ArrayForEach(m, cJSON_GetObjectItemCaseSensitive(fx, "envelopes")) {
        uint8_t plain[300], env[300];
        const int pn = hex(str(m, "plain"), plain, sizeof(plain)), en = hex(str(m, "envelope"), env, sizeof(env));
        CHECK(pn >= 0 && en == pn + YK_SEAL_OVERHEAD);
        if (!strcmp(str(m, "direction"), "toDevice")) {
            const int n = yk_seal_link_receive(l, env, (size_t)en, out);
            CHECK(n == pn && !memcmp(out, plain, (size_t)pn) && l->rx == (uint32_t)num(m, "counter"));
            if (first_in_n < 0) memcpy(first_in, env, (size_t)en), first_in_n = en;
        } else {
            CHECK(l->tx + 1 == (uint32_t)num(m, "counter"));
            const int n = yk_seal_link_send(l, plain, (size_t)pn, out);
            CHECK(n == en && !memcmp(out, env, (size_t)en));
        }
    }
    CHECK(l->fails == 0);
    // A replay, a flipped bit, a short one: dropped, counted; five in a row and the phone is let go (the caller).
    CHECK(first_in_n > 0 && yk_seal_link_receive(l, first_in, (size_t)first_in_n, out) == -1 && l->fails == 1);
    uint8_t forged[200];
    memcpy(forged, first_in, (size_t)first_in_n);
    forged[0] = 0x7f;  // a fresh counter, the old tag
    CHECK(yk_seal_link_receive(l, forged, (size_t)first_in_n, out) == -1 && l->fails == 2);
    CHECK(yk_seal_link_receive(l, forged, 10, out) == -1 && l->fails == 3);
    memset(forged, 0, sizeof(forged));
    CHECK(yk_seal_link_receive(l, forged, YK_SEAL_FRAME_MAX + YK_SEAL_OVERHEAD + 1, out) == -1 && l->fails == 4);
    // One that opens sets the count back.
    uint8_t mine[64];
    {
        yk_seal_link_t *phone_side = calloc(1, sizeof(*phone_side));
        yk_seal_link_init(phone_side, app, key, kid);
        mbedtls_gcm_setkey(&phone_side->gcm, MBEDTLS_CIPHER_ID_AES, skey, 128);
        const uint8_t hello[] = {1, 4, 42};
        CHECK(yk_seal(&phone_side->gcm, YK_SEAL_TO_DEVICE, 100, hello, sizeof(hello), mine));
        CHECK(yk_seal_link_receive(l, mine, sizeof(hello) + YK_SEAL_OVERHEAD, out) == 3 && out[2] == 42 && l->fails == 0);
        // A message the device sealed is not one the device takes (the direction is in the nonce).
        const int n = yk_seal_link_send(l, hello, sizeof(hello), buf);
        CHECK(n == 3 + YK_SEAL_OVERHEAD);
        uint32_t last = 0;
        CHECK(yk_unseal(&phone_side->gcm, YK_SEAL_TO_PHONE, &last, buf, (size_t)n, out) == 3 && last == l->tx);
        last = 0;
        CHECK(yk_unseal(&phone_side->gcm, YK_SEAL_TO_DEVICE, &last, buf, (size_t)n, out) == -1);
        yk_seal_link_free(phone_side);
        free(phone_side);
    }
    CHECK(yk_seal_link_send(l, buf, YK_SEAL_FRAME_MAX, out) == YK_SEAL_FRAME_MAX + YK_SEAL_OVERHEAD);
    CHECK(yk_seal_link_send(l, buf, YK_SEAL_FRAME_MAX + 1, out) == -1);

    // A new connection: nothing open, counters from 1 again (under a new key: new random bytes).
    uint8_t dev2[YK_SEAL_NONCE] = {1, 2, 3, 4, 5, 6, 7, 8};
    yk_seal_link_connect(l, dev2);
    CHECK(!l->open && l->tx == 0 && l->rx == 0 && l->fails == 0);
    CHECK(yk_seal_link_send(l, buf, 3, out) == -1);
    // A phone with another kid: KEY_MISMATCH, and the device knows its key is likely old.
    uint8_t other[YK_SEAL_OPEN];
    memcpy(other, open, sizeof(other));
    other[10] ^= 1;
    CHECK(yk_seal_link_open(l, other, sizeof(other)) == YK_SEAL_ERR_KEY_MISMATCH && l->mismatch && !l->open);
    // Not a LinkOpen at all.
    CHECK(yk_seal_link_open(l, open, sizeof(open) - 1) == YK_SEAL_ERR_BAD && !l->open);
    // The right one after all (the phone fetched the key again): open, and the mismatch is gone.
    CHECK(yk_seal_link_open(l, open, sizeof(open)) == 0 && l->open && !l->mismatch);
    // This second connection's envelopes are exactly what a fresh context makes under its own key, both ways (11-14:
    // ESP-IDF's GCM kept the first connection's GHASH H in a re-keyed context, every tag wrong; yk_seal_link_open now
    // starts each connection from a fresh one. mbedTLS's own GCM, here, never showed it: this holds the behaviour).
    {
        uint8_t skey2[YK_SEAL_SESSION_KEY], env2[64], back[64];
        CHECK(yk_seal_session_key(key, app, dev2, phone, skey2));
        mbedtls_gcm_context g;
        mbedtls_gcm_init(&g);
        CHECK(mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, skey2, 128) == 0);
        const uint8_t tick[] = {1, 0, 7, 0};
        CHECK(yk_seal(&g, YK_SEAL_TO_DEVICE, 1, tick, sizeof(tick), env2));
        CHECK(yk_seal_link_receive(l, env2, sizeof(tick) + YK_SEAL_OVERHEAD, out) == 4 && out[2] == 7);
        const int n = yk_seal_link_send(l, tick, sizeof(tick), env2);
        uint32_t last = 0;
        CHECK(n == 4 + YK_SEAL_OVERHEAD && yk_unseal(&g, YK_SEAL_TO_PHONE, &last, env2, (size_t)n, back) == 4);
        mbedtls_gcm_free(&g);
    }
    yk_seal_link_free(l);

    // No key on the device: every open is KEY_MISMATCH, and nothing says the key is old (there is none).
    yk_seal_link_init(l, app, NULL, 0);
    yk_seal_link_connect(l, dev);
    yk_seal_link_session(l, buf);
    CHECK(buf[8] == 0 && buf[9] == 0 && buf[10] == 0 && buf[11] == 0);  // kid 0: none
    CHECK(yk_seal_link_open(l, open, sizeof(open)) == YK_SEAL_ERR_KEY_MISMATCH && !l->mismatch);
    yk_seal_link_free(l);
    free(l);

    // When the key is fetched again.
    const int64_t day = 24 * 3600, now = 1794500000;
    CHECK(!yk_seal_key_stale(now - 6 * day, now));
    CHECK(yk_seal_key_stale(now - 8 * day, now));
    CHECK(yk_seal_key_stale(now + day, now));  // fetched "later" than now: a clock that went back
    CHECK(yk_seal_key_stale(0, now));          // fetched before the clock was known
    CHECK(!yk_seal_key_stale(now - 8 * day, 0));  // no clock now: cannot tell

    cJSON_Delete(fx);
    printf("test_seal: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
