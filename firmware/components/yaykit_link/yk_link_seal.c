// yk_link_seal.h.
#include "yk_link_seal.h"

#include <string.h>

#include "mbedtls/md.h"
#include "mbedtls/sha256.h"

static const char MARK_LABEL[] = "yay-mark-v1";
static const char SESSION_LABEL[] = "tikpal-link-session-v1";

static void put32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)v, b[1] = (uint8_t)(v >> 8), b[2] = (uint8_t)(v >> 16), b[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t *b) {
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static void wipe(void *p, size_t n) {
    volatile uint8_t *v = p;
    while (n--) *v++ = 0;
}

uint32_t yk_seal_mark(const char *device_id) {
    uint8_t buf[sizeof(MARK_LABEL) - 1 + 32], h[32];
    const size_t id = device_id ? strnlen(device_id, 32) : 0;
    memcpy(buf, MARK_LABEL, sizeof(MARK_LABEL) - 1);
    memcpy(buf + sizeof(MARK_LABEL) - 1, device_id ? device_id : "", id);
    if (mbedtls_sha256(buf, sizeof(MARK_LABEL) - 1 + id, h, 0) != 0) return 0;
    return (uint32_t)h[0] << 12 | (uint32_t)h[1] << 4 | h[2] >> 4;
}

void yk_seal_service_data(uint32_t mark, uint16_t app, uint8_t out[YK_SEAL_SERVICE_DATA]) {
    out[0] = (uint8_t)mark, out[1] = (uint8_t)(mark >> 8), out[2] = (uint8_t)(mark >> 16) & 0x0f;
    out[3] = (uint8_t)app, out[4] = (uint8_t)(app >> 8);
}

void yk_seal_session(const uint8_t device[YK_SEAL_NONCE], uint32_t kid, uint16_t app, uint8_t out[YK_SEAL_SESSION]) {
    memcpy(out, device, YK_SEAL_NONCE);
    put32(out + 8, kid);
    out[12] = (uint8_t)app, out[13] = (uint8_t)(app >> 8);
}

bool yk_seal_open_parse(const uint8_t *b, size_t n, uint8_t phone[YK_SEAL_NONCE], uint32_t *kid) {
    if (!b || n != YK_SEAL_OPEN || b[0] != YK_SEAL_VERSION || b[1] != YK_SEAL_KIND_OPEN) return false;
    const uint32_t k = get32(b + 10);
    if (!k) return false;
    memcpy(phone, b + 2, YK_SEAL_NONCE);
    *kid = k;
    return true;
}

bool yk_seal_session_key(const uint8_t key[YK_SEAL_KEY], uint16_t app, const uint8_t device[YK_SEAL_NONCE],
                         const uint8_t phone[YK_SEAL_NONCE], uint8_t out[YK_SEAL_SESSION_KEY]) {
    uint8_t info[sizeof(SESSION_LABEL) - 1 + 2 + 2 * YK_SEAL_NONCE + 1], t[32];
    size_t at = sizeof(SESSION_LABEL) - 1;
    memcpy(info, SESSION_LABEL, at);
    info[at++] = (uint8_t)app, info[at++] = (uint8_t)(app >> 8);
    memcpy(info + at, device, YK_SEAL_NONCE), at += YK_SEAL_NONCE;
    memcpy(info + at, phone, YK_SEAL_NONCE), at += YK_SEAL_NONCE;
    info[at++] = 0x01;  // HKDF-Expand's T(1)
    const bool ok = mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, YK_SEAL_KEY, info, at, t) == 0;
    if (ok) memcpy(out, t, YK_SEAL_SESSION_KEY);
    wipe(t, sizeof(t));
    return ok;
}

static void nonce(uint8_t n[12], uint8_t direction, uint32_t counter) {
    memset(n, 0, 12);
    n[0] = direction;
    put32(n + 8, counter);
}

bool yk_seal(mbedtls_gcm_context *gcm, uint8_t direction, uint32_t counter, const uint8_t *plain, size_t n, uint8_t *out) {
    uint8_t iv[12];
    nonce(iv, direction, counter);
    put32(out, counter);
    return mbedtls_gcm_crypt_and_tag(gcm, MBEDTLS_GCM_ENCRYPT, n, iv, sizeof(iv), NULL, 0, plain, out + YK_SEAL_COUNTER,
                                     YK_SEAL_TAG, out + YK_SEAL_COUNTER + n) == 0;
}

int yk_unseal(mbedtls_gcm_context *gcm, uint8_t direction, uint32_t *last, const uint8_t *env, size_t n, uint8_t *plain) {
    if (n < YK_SEAL_OVERHEAD) return -1;
    const uint32_t counter = get32(env);
    if (counter <= *last) return -1;  // a replay (or counted from 0: counters start at 1)
    const size_t len = n - YK_SEAL_OVERHEAD;
    uint8_t iv[12];
    nonce(iv, direction, counter);
    if (mbedtls_gcm_auth_decrypt(gcm, len, iv, sizeof(iv), NULL, 0, env + n - YK_SEAL_TAG, YK_SEAL_TAG,
                                 env + YK_SEAL_COUNTER, plain) != 0) {
        wipe(plain, len);
        return -1;
    }
    *last = counter;
    return (int)len;
}

bool yk_seal_key_stale(int64_t fetched_s, int64_t now_s) {
    if (fetched_s <= 0 || now_s <= 0) return now_s > 0;  // unknown when: fetch again once the clock is known
    return now_s - fetched_s > YK_SEAL_KEY_STALE_S || now_s < fetched_s;
}

// ---------------------------------------------------------------- the link

void yk_seal_link_init(yk_seal_link_t *l, uint16_t app, const uint8_t *key, uint32_t kid) {
    memset(l, 0, sizeof(*l));
    l->app = app;
    if (key && kid) {
        memcpy(l->key, key, YK_SEAL_KEY);
        l->kid = kid;
        l->have_key = true;
    }
    mbedtls_gcm_init(&l->gcm);
}

void yk_seal_link_free(yk_seal_link_t *l) {
    mbedtls_gcm_free(&l->gcm);
    wipe(l, sizeof(*l));
}

void yk_seal_link_connect(yk_seal_link_t *l, const uint8_t device[YK_SEAL_NONCE]) {
    memcpy(l->device, device, YK_SEAL_NONCE);
    l->open = false;
    l->tx = l->rx = 0;
    l->fails = 0;
}

void yk_seal_link_session(const yk_seal_link_t *l, uint8_t out[YK_SEAL_SESSION]) {
    yk_seal_session(l->device, l->have_key ? l->kid : 0, l->app, out);
}

int yk_seal_link_open(yk_seal_link_t *l, const uint8_t *b, size_t n) {
    uint8_t phone[YK_SEAL_NONCE], skey[YK_SEAL_SESSION_KEY];
    uint32_t kid;
    if (l->open) return YK_SEAL_ERR_BUSY;
    if (!yk_seal_open_parse(b, n, phone, &kid)) return YK_SEAL_ERR_BAD;
    if (!l->have_key || kid != l->kid) {
        if (l->have_key) l->mismatch = true;
        return YK_SEAL_ERR_KEY_MISMATCH;
    }
    if (!yk_seal_session_key(l->key, l->app, l->device, phone, skey)) return YK_SEAL_ERR_BAD;
    // A fresh context for every connection key, never a re-keyed one: ESP-IDF's GCM (esp_aes_gcm.c, the path every
    // ESP32-S3 envelope takes: no hardware GCM there) works out GHASH's H only on a context's first use after init, and
    // setkey does not send it back there. A re-keyed context so seals and opens with the new key under the first one's
    // H: every tag wrong. Found 11-14 between two Tikpal Voices: a reconnection without the app restarting failed every
    // envelope from then on (mbedTLS's own GCM, the host tests', redoes H in setkey and never showed it).
    mbedtls_gcm_free(&l->gcm);
    mbedtls_gcm_init(&l->gcm);
    const int rc = mbedtls_gcm_setkey(&l->gcm, MBEDTLS_CIPHER_ID_AES, skey, 8 * YK_SEAL_SESSION_KEY);
    wipe(skey, sizeof(skey));
    if (rc) return YK_SEAL_ERR_BAD;
    l->open = true;
    l->mismatch = false;
    l->tx = l->rx = 0;
    l->fails = 0;
    return 0;
}

int yk_seal_link_receive(yk_seal_link_t *l, const uint8_t *env, size_t n, uint8_t *frame) {
    if (!l->open) return -1;
    if (n > YK_SEAL_FRAME_MAX + YK_SEAL_OVERHEAD) {
        l->fails++;
        return -1;
    }
    const int len = yk_unseal(&l->gcm, YK_SEAL_TO_DEVICE, &l->rx, env, n, frame);
    if (len < 0) {
        l->fails++;
        return -1;
    }
    l->fails = 0;
    return len;
}

int yk_seal_link_send(yk_seal_link_t *l, const uint8_t *frame, size_t n, uint8_t *env) {
    if (!l->open || n > YK_SEAL_FRAME_MAX || l->tx == UINT32_MAX) return -1;
    if (!yk_seal(&l->gcm, YK_SEAL_TO_PHONE, l->tx + 1, frame, n, env)) return -1;
    l->tx++;
    return (int)(n + YK_SEAL_OVERHEAD);
}
