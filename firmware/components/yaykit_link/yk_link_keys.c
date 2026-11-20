// The unpaired mode's link keys (yk_link_keep.h): one for each app, in this component's own NVS namespace
// `yk_link_keys` under "k<app number>" (NVS keys are at most 15 characters: a slug would not fit), fetched from the
// platform through yaykit_cloud with the app's token (yk_api_link_key_fetch(), a weak reference: firmware without
// yaykit_cloud has no fetch). Beside each app's key, its static random address, "a<app number>" (BOSS-2 11-14): made
// the first time, then the same, so a phone reconnects by it (Android's autoConnect, iOS's identifier). Yay and the
// apps clear the namespace when the device leaves its account or is claimed (yaykit_account.c forget(),
// activate_task()): keys and addresses go together, and the owner before cannot pick the device out by the address
// its phone kept. Keys are never logged.
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_random.h"
#include "sdkconfig.h"
#include "nvs.h"
#include "yaykit_link.h"
#include "yk_link_keep.h"
#include "yk_link_priv.h"
#include "yk_link_rules.h"
#include "yk_link_seal.h"

#define NS "yk_link_keys"
#define MAGIC 0x314C4B59u  // "YKL1"

static const char *TAG = "yk_link_keys";

/** yaykit_cloud's (yaykit_account.c): GET /device/v1/link-key with the app's token. Not declared in any public header,
 *  and the review blocks yk_api_* (yaykit_cloud's insides): an app never fetches a key itself. */
esp_err_t yk_api_link_key_fetch(uint32_t *kid, uint8_t key[YK_SEAL_KEY]) __attribute__((weak));

/** yaykit_voicecode's: the 应用码 number built in from app.json (kept in every image with yaykit_app by `-u`). */
uint32_t yk_vc_app_number(void) __attribute__((weak));

uint16_t yk_link_keys_app(void) {
    const uint32_t n = yk_vc_app_number ? yk_vc_app_number() : 0;
    return n <= 0xFFFF ? (uint16_t)n : 0;
}

bool yk_link_keys_stack_ok(void) {
    volatile int here = 0;  // on this task's stack: NVS writes from a PSRAM stack assert (the flash cache goes off)
    return !esp_ptr_external_ram((const void *)&here);
}

typedef struct {
    uint32_t magic;
    uint32_t kid;
    int64_t fetched;  // Unix seconds (0: the clock was not known)
    uint8_t key[YK_SEAL_KEY];
} blob_t;

static volatile uint16_t s_mismatch_app;  // a phone came to this app with another kid (this boot)

static void wipe(void *p, size_t n) {
    volatile uint8_t *v = p;
    while (n--) *v++ = 0;
}

static void key_name(uint16_t app, char out[8]) { snprintf(out, 8, "k%u", (unsigned)app); }

static bool load(uint16_t app, blob_t *b) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    char k[8];
    key_name(app, k);
    size_t n = sizeof(*b);
    const bool ok = nvs_get_blob(h, k, b, &n) == ESP_OK && n == sizeof(*b) && b->magic == MAGIC && b->kid != 0;
    nvs_close(h);
    if (!ok) wipe(b, sizeof(*b));
    return ok;
}

bool yk_link_keys_load(uint16_t app, uint8_t key[YK_SEAL_KEY], uint32_t *kid) {
    blob_t b;
    if (!load(app, &b)) return false;
    memcpy(key, b.key, YK_SEAL_KEY);
    *kid = b.kid;
    wipe(&b, sizeof(b));
    return true;
}

void yk_link_keys_mismatch(uint16_t app, bool on) {
    if (on) s_mismatch_app = app;
    else if (s_mismatch_app == app) s_mismatch_app = 0;
}

yk_link_key_state_t yk_link_unpaired_key_state(void) {
    const uint16_t app = yk_link_keys_app();
    blob_t b;
    if (!app || !load(app, &b)) return YK_LINK_KEY_NONE;
    wipe(&b, sizeof(b));
    return s_mismatch_app == app ? YK_LINK_KEY_MISMATCH : YK_LINK_KEY_OK;
}

static int64_t now_s(void) {
    const time_t t = time(NULL);
    return t > 1735689600 ? (int64_t)t : 0;  // before 2025: the clock is not known yet
}

esp_err_t yk_link_unpaired_refresh(bool force) {
    const uint16_t app = yk_link_keys_app();
    if (!app) return ESP_ERR_NOT_SUPPORTED;  // no voiceCode number in app.json
    if (yk_link_keep_running || !yk_link_keys_stack_ok()) return ESP_ERR_INVALID_STATE;  // Bluetooth on; a PSRAM stack
    blob_t b;
    const bool have = load(app, &b);
    const bool stale = have && yk_seal_key_stale(b.fetched, now_s());
    const uint32_t old_kid = have ? b.kid : 0;
    wipe(&b, sizeof(b));
    if (have && !force && !stale && s_mismatch_app != app) return ESP_OK;
    if (!yk_api_link_key_fetch) return ESP_ERR_NOT_SUPPORTED;
    blob_t n = {.magic = MAGIC};
    esp_err_t err = yk_api_link_key_fetch(&n.kid, n.key);
    if (err == ESP_OK && !n.kid) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) {
        wipe(&n, sizeof(n));
        printf("YK_LINK key refresh failed err=%s had=%d\n", esp_err_to_name(err), have);
        return err;
    }
    n.fetched = now_s();
    nvs_handle_t h;
    err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        char k[8];
        key_name(app, k);
        err = nvs_set_blob(h, k, &n, sizeof(n));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    const uint32_t kid = n.kid;
    wipe(&n, sizeof(n));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "not kept: %s", esp_err_to_name(err));
        return err;
    }
    yk_link_keys_mismatch(app, false);
    printf("YK_LINK key refreshed app=%u changed=%d\n", (unsigned)app, kid != old_kid);
    return ESP_OK;
}

static void addr_name(uint16_t app, char out[8]) { snprintf(out, 8, "a%u", (unsigned)app); }

bool yk_link_keys_addr(uint16_t app, uint8_t addr[6]) {
    nvs_handle_t h;
    const bool open = nvs_open(NS, NVS_READWRITE, &h) == ESP_OK;
    char k[8];
    addr_name(app, k);
    size_t n = 6;
    if (open && nvs_get_blob(h, k, addr, &n) == ESP_OK && n == 6 && yk_rules_static_addr_ok(addr)) {
        nvs_close(h);
        return true;
    }
    do {
        esp_fill_random(addr, 6);
    } while (!yk_rules_static_addr_make(addr));
    bool kept = false;
    if (open) {
        kept = nvs_set_blob(h, k, addr, 6) == ESP_OK && nvs_commit(h) == ESP_OK;
        nvs_close(h);
    }
    printf("YK_LINK address new app=%u kept=%d\n", (unsigned)app, kept);
    return kept;
}

void yk_link_keys_forget(void) {
    // Every app's key and address (the next start makes a new address). s_mismatch_app is left alone: with no key the
    // state is NONE whatever it says (and Yay, which calls this, then keeps no static RAM of the unpaired mode).
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;  // never written: nothing to clear, and no namespace made
    nvs_close(h);
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    const esp_err_t err = nvs_erase_all(h);
    if (err == ESP_OK) nvs_commit(h);
    nvs_close(h);
    printf("YK_LINK keys forgotten err=%s\n", esp_err_to_name(err));
}

#if CONFIG_YAY_DEV
bool yk_link_keys_put(uint32_t kid, const uint8_t key[YK_SEAL_KEY]) {
    const uint16_t app = yk_link_keys_app();
    if (!app || !kid || !yk_link_keys_stack_ok()) return false;
    blob_t b = {.magic = MAGIC, .kid = kid, .fetched = now_s()};
    memcpy(b.key, key, YK_SEAL_KEY);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        char k[8];
        key_name(app, k);
        err = nvs_set_blob(h, k, &b, sizeof(b));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    wipe(&b, sizeof(b));
    yk_link_keys_mismatch(app, false);
    return err == ESP_OK;
}
#endif
