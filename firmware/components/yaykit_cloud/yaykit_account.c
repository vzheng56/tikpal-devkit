#include "yaykit_account.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "yaykit_cloud.h"
#include "yaykit_link.h"
#include "yk_codec.h"
#include "yk_folder_net.h"
#include "yk_http.h"
#include "yk_locale.h"
#include "yk_region.h"
#include "yk_region_store.h"
#if CONFIG_YAY_DEV && CONFIG_YK_REGIONS
#include "yk_region_test.h"
#endif
#include "yk_sign.h"

static const char *TAG = "yk_account";
static const char *NS = "yk_acct";

static yk_account_config_t cfg;
static SemaphoreHandle_t lock;
static yk_account_state_t state = YK_ACCOUNT_UNBOUND;
static const char *error = "";
static char nickname[64];
static char locale[8] = "zh";
static bool have_key;
static bool release_wanted;  // yk_acct `rel`: a factory reset's release not answered yet (11.4)
static uint8_t private_key[YK_KEY_PRIVATE_BYTES], public_key[YK_KEY_PUBLIC_BYTES];
static char token[512];
static int64_t token_expires_us;
static char claim[128];
#if CONFIG_YAY_DEV
static bool drop_reply;  // `account drop`: the next activation's reply is dropped, as if the connection broke
#endif
#if CONFIG_YK_REGIONS
// This region's binding key as stored (or about to be): what OTHER_REGION goes by. Not `state`: a claim of the device's
// own side that the cloud refused leaves it FAILED here and bound all the same, there and in NVS.
static bool bound_kept;
#endif
#if CONFIG_YAY_DEV && CONFIG_YK_REGIONS
// `region set|clear` changed a test deployment (yk_region_test.h) for the next start: the kept tokens are dropped then,
// and none is kept again before the restart (this start's server made it, not the next one's).
static bool test_changed;
#endif

// 两套部署 4.3: the binding is kept per region, under a key of its own in yk_acct. Row 0, global, keeps `bound`, the key
// every firmware before the regions reads; another row keeps `bound_<id>` ("bound_cn"), which none of them reads. So an
// extension or app built before, or a Yay rolled back, sees a device bound in cn as not bound: it takes no token with
// the device key from global's (the only address it has), gets no DEVICE_NOT_ACTIVATED, and never forget()s what all the
// firmware share (附录 C 2.4). Ids are short enough (test/test_account.c checks every row's key fits NVS's 15).
#define BINDING_KEY_SIZE 16  // NVS_KEY_NAME_MAX_SIZE: 15 characters and the end

/** This device's region's binding key, in `out` or a literal. Off: `bound`, as ever. */
static const char *binding_key(char out[BINDING_KEY_SIZE]) {
#if CONFIG_YK_REGIONS
    if (yk_region_current() != YK_REGION_GLOBAL) {
        snprintf(out, BINDING_KEY_SIZE, "bound_%s", yk_region_name());
        return out;
    }
#endif
    (void)out;
    return "bound";
}

static int rng(void *ctx, unsigned char *buf, size_t len) {
    (void)ctx;
    esp_fill_random(buf, len);
    return 0;
}

static void changed(void) {
    if (cfg.on_change) cfg.on_change(cfg.ctx);
}

static void set_state(yk_account_state_t next, const char *why) {
    xSemaphoreTake(lock, portMAX_DELAY);
    state = next;
    error = why ? why : "";
    xSemaphoreGive(lock);
    ESP_LOGI(TAG, "%s%s%s", yk_account_state_name(), *error ? " " : "", error);
    changed();
}

/** Stores the binding (under this region's key) and the owner. NVS writes: call from a task whose stack is in internal
 *  RAM. */
static void save_binding(bool bound) {
    char nick[sizeof(nickname)], loc[sizeof(locale)];
    xSemaphoreTake(lock, portMAX_DELAY);
    strcpy(nick, nickname);
    strcpy(loc, locale);
#if CONFIG_YK_REGIONS
    bound_kept = bound;
#endif
    xSemaphoreGive(lock);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    char key[BINDING_KEY_SIZE];
    nvs_set_u8(h, binding_key(key), bound ? 1 : 0);
    if (bound) {
        nvs_set_str(h, "nick", nick);
        nvs_set_str(h, "loc", loc);
    } else {
        nvs_erase_key(h, "nick");
        nvs_erase_key(h, "loc");
    }
    nvs_commit(h);
    nvs_close(h);
}

/** Loads the key, or makes and stores one on first activation. */
static bool ensure_key(void) {
    if (have_key) return true;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    size_t sk_len = sizeof(private_key), pk_len = sizeof(public_key);
    bool ok = nvs_get_blob(h, "sk", private_key, &sk_len) == ESP_OK && sk_len == sizeof(private_key) &&
              nvs_get_blob(h, "pk", public_key, &pk_len) == ESP_OK && pk_len == sizeof(public_key);
    if (!ok) {
        ok = yk_key_generate(private_key, public_key, rng, NULL) == 0 &&
             nvs_set_blob(h, "sk", private_key, sizeof(private_key)) == ESP_OK &&
             nvs_set_blob(h, "pk", public_key, sizeof(public_key)) == ESP_OK && nvs_commit(h) == ESP_OK;
        if (ok) ESP_LOGI(TAG, "made the device key");
    }
    nvs_close(h);
    have_key = ok;
    return ok;
}

// The token is also kept in NVS with its end in wall-clock time, so that after a restart (an app opened and
// left, say) it is used again instead of fetched: two TLS handshakes less before the session can connect. Only
// with a known clock (yk_token_left_s); the device key it was made with is in the same namespace anyway.
typedef struct {
    char app[48];
    int64_t expires_unix;
    char token[sizeof(token)];
} kept_token_t;

static void kept_token_save(int64_t ttl_s) {
#if CONFIG_YAY_DEV && CONFIG_YK_REGIONS
    if (test_changed) return;  // `region set`: the next start talks to another server
#endif
    const int64_t now = (int64_t)time(NULL);
    kept_token_t *k = calloc(1, sizeof(*k));
    if (!k) return;
    snprintf(k->app, sizeof(k->app), "%s", cfg.app_slug);
    k->expires_unix = now + ttl_s;
    xSemaphoreTake(lock, portMAX_DELAY);
    memcpy(k->token, token, sizeof(k->token));
    xSemaphoreGive(lock);
    char key[11];
    yk_token_key(cfg.app_slug, key);
    nvs_handle_t h;
    if (yk_token_left_s(now, k->expires_unix) && nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_blob(h, key, k, sizeof(*k)) == ESP_OK) nvs_commit(h);
        nvs_close(h);
    }
    memset(k, 0, sizeof(*k));
    free(k);
}

static void kept_token_load(void) {
    kept_token_t *k = calloc(1, sizeof(*k));
    if (!k) return;
    char key[11];
    yk_token_key(cfg.app_slug, key);
    nvs_handle_t h;
    size_t len = sizeof(*k);
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, key, k, &len) != ESP_OK || len != sizeof(*k)) k->app[0] = 0;
        nvs_close(h);
    }
    k->app[sizeof(k->app) - 1] = k->token[sizeof(k->token) - 1] = 0;
    const int64_t left = !strcmp(k->app, cfg.app_slug) ? yk_token_left_s((int64_t)time(NULL), k->expires_unix) : 0;
    if (left && k->token[0]) {
        strcpy(token, k->token);
        token_expires_us = esp_timer_get_time() + left * 1000000;
        ESP_LOGI(TAG, "token kept from before the restart: %lld s left", (long long)left);
    }
    memset(k, 0, sizeof(*k));
    free(k);
}

static void kept_token_drop(void) {
    char key[11];
    yk_token_key(cfg.app_slug, key);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_erase_key(h, key) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

#if CONFIG_YK_REGIONS
/** yk_token_key's: "tk" and 8 hex digits. */
static bool token_key(const char *key) {
    if (strlen(key) != 10 || key[0] != 't' || key[1] != 'k') return false;
    for (int i = 2; i < 10; i++)
        if (!((key[i] >= '0' && key[i] <= '9') || (key[i] >= 'a' && key[i] <= 'f'))) return false;
    return true;
}

/** Every app's kept token, not only this one's: they were the other region's, its user's id in them, and an extension
 *  would send its own to the new region before it asked for one (附录 C 2.5). A batch at a time: an NVS iterator does
 *  not outlive a change. NVS writes: an internal-RAM stack. */
static void kept_tokens_drop_all(void) {
    enum { BATCH = 8 };
    int dropped = 0;
    for (int round = 0; round < 8; round++) {
        char keys[BATCH][BINDING_KEY_SIZE];
        int n = 0;
        nvs_iterator_t it = NULL;
        esp_err_t err = nvs_entry_find(NVS_DEFAULT_PART_NAME, NS, NVS_TYPE_BLOB, &it);
        while (err == ESP_OK && n < BATCH) {
            nvs_entry_info_t info;
            nvs_entry_info(it, &info);
            if (token_key(info.key)) snprintf(keys[n++], sizeof(keys[0]), "%s", info.key);
            err = nvs_entry_next(&it);
        }
        nvs_release_iterator(it);
        nvs_handle_t h;
        if (!n || nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) break;
        for (int i = 0; i < n; i++) nvs_erase_key(h, keys[i]);
        nvs_commit(h);
        nvs_close(h);
        dropped += n;
    }
    ESP_LOGI(TAG, "another region: %d kept token%s dropped", dropped, dropped == 1 ? "" : "s");
}
#endif

static void keep_token(const cJSON *json) {
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(json, "accessToken");
    const cJSON *ttl = cJSON_GetObjectItemCaseSensitive(json, "expiresIn");
    if (!cJSON_IsString(t) || strlen(t->valuestring) >= sizeof(token) || !cJSON_IsNumber(ttl)) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    strcpy(token, t->valuestring);
    token_expires_us = esp_timer_get_time() + (int64_t)ttl->valuedouble * 1000000;
    xSemaphoreGive(lock);
    kept_token_save((int64_t)ttl->valuedouble);
}

/** Keeps owner.nickname and owner.locale. True when either changed. */
static bool keep_owner(const cJSON *owner) {
    const cJSON *nick = cJSON_GetObjectItemCaseSensitive(owner, "nickname");
    const cJSON *loc = cJSON_GetObjectItemCaseSensitive(owner, "locale");
    char next_nick[sizeof(nickname)], next_loc[sizeof(locale)];
    snprintf(next_nick, sizeof(next_nick), "%s", cJSON_IsString(nick) ? nick->valuestring : "");
    snprintf(next_loc, sizeof(next_loc), "%s", cJSON_IsString(loc) && *loc->valuestring ? loc->valuestring : "zh");
    xSemaphoreTake(lock, portMAX_DELAY);
    bool same = !strcmp(nickname, next_nick) && !strcmp(locale, next_loc);
    strcpy(nickname, next_nick);
    strcpy(locale, next_loc);
    xSemaphoreGive(lock);
    return !same;
}

/** Signs `message` and returns it as base64url in `out`. */
static bool sign_b64(const char *message, char *out, size_t size) {
    uint8_t sig[YK_SIGNATURE_MAX];
    size_t len = yk_sign(private_key, message, sig, rng, NULL);
    return len && yk_b64url_encode(sig, len, out, size);
}

/** A token for the device key: the challenge, then its nonce signed. NULL once one came back and is kept; else the
 *  code, `unknown` set when the cloud answered DEVICE_NOT_ACTIVATED (it does not know this key as bound). */
static const char *fetch_token(bool *unknown) {
    *unknown = false;
    if (release_wanted) return "RELEASE_PENDING";  // that token would be the old owner's
    const char *device_id = yk_link_device_id();
    char json[400];
    snprintf(json, sizeof(json), "{\"deviceId\":\"%s\"}", device_id);
    yk_http_response_t res;
    if (yk_http_json(HTTP_METHOD_POST, "/device/v1/token/challenge", NULL, json, &res) != ESP_OK) return "NETWORK";
    cJSON *reply = res.status == 200 ? cJSON_Parse(res.body ? res.body : "") : NULL;
    const cJSON *nonce = cJSON_GetObjectItemCaseSensitive(reply, "nonce");
    char nonce_text[96] = "";
    if (cJSON_IsString(nonce)) snprintf(nonce_text, sizeof(nonce_text), "%s", nonce->valuestring);
    cJSON_Delete(reply);
    yk_http_free(&res);
    char message[220], signature[128];
    if (!*nonce_text || !yk_token_message(device_id, cfg.app_slug, nonce_text, message, sizeof(message)) ||
        !sign_b64(message, signature, sizeof(signature)))
        return "SERVER";
    snprintf(json, sizeof(json), "{\"deviceId\":\"%s\",\"app\":\"%s\",\"nonce\":\"%s\",\"signature\":\"%s\"}",
             device_id, cfg.app_slug, nonce_text, signature);
    if (yk_http_json(HTTP_METHOD_POST, "/device/v1/token", NULL, json, &res) != ESP_OK) return "NETWORK";
    const char *code = NULL;
    if (res.status == 200) {
        reply = cJSON_Parse(res.body ? res.body : "");
        keep_token(reply);
        cJSON_Delete(reply);
        xSemaphoreTake(lock, portMAX_DELAY);
        if (!token[0]) code = "SERVER";
        xSemaphoreGive(lock);
    } else {
        code = yk_http_error_code(&res);
        *unknown = !strcmp(code, "DEVICE_NOT_ACTIVATED");
    }
    yk_http_free(&res);
    return code;
}

/** After an activation that got no answer or a 5xx: whether the cloud bound the device all the same (it could fail
 *  after the binding was written: #611, fixed on the cloud's side in #737; this is the device's side). The device key
 *  gets a token only once it is bound to someone. Twice, a moment apart: the binding may still be on its way. */
static bool bound_anyway(void) {
    for (int i = 0; i < 2; i++) {
        vTaskDelay(pdMS_TO_TICKS(i ? 3000 : 1000));
        bool unknown;
        const char *why = fetch_token(&unknown);
        ESP_LOGW(TAG, "activation unanswered: the device key %s%s", why ? "got no token: " : "got a token: bound",
                 why ? why : "");
        if (!why) return true;
        if (unknown) return false;
    }
    return false;
}

static void release_keep(bool on) {
    release_wanted = on;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (on) nvs_set_u8(h, "rel", 1);
    else nvs_erase_key(h, "rel");
    nvs_commit(h);
    nvs_close(h);
}

void yk_account_release_mark(void) { release_keep(true); }

bool yk_account_release_pending(void) { return release_wanted; }

bool yk_account_binding_stored(void) {
    nvs_handle_t h;
    uint8_t bound = 0;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        char key[BINDING_KEY_SIZE];
        nvs_get_u8(h, binding_key(key), &bound);
        nvs_close(h);
    }
    return bound;
}

esp_err_t yk_account_release(const char **code) {
    if (code) *code = NULL;
    if (!release_wanted) return ESP_OK;
    if (!have_key) {  // nothing to sign with: nothing to say to the cloud, and no key the cloud could know
        release_keep(false);
        return ESP_OK;
    }
    if (yk_http_prepare(code) != ESP_OK) return ESP_FAIL;
    const char *device_id = yk_link_device_id();
    for (int attempt = 0; attempt < 2; attempt++) {
        char json[400], nonce_text[96] = "", message[200], signature[128];
        snprintf(json, sizeof(json), "{\"deviceId\":\"%s\"}", device_id);
        yk_http_response_t res;
        if (yk_http_json(HTTP_METHOD_POST, "/device/v1/token/challenge", NULL, json, &res) != ESP_OK) break;
        cJSON *reply = res.status == 200 ? cJSON_Parse(res.body ? res.body : "") : NULL;
        const cJSON *nonce = cJSON_GetObjectItemCaseSensitive(reply, "nonce");
        if (cJSON_IsString(nonce)) snprintf(nonce_text, sizeof(nonce_text), "%s", nonce->valuestring);
        cJSON_Delete(reply);
        yk_http_free(&res);
        if (!*nonce_text || !yk_release_message(device_id, nonce_text, message, sizeof(message)) ||
            !sign_b64(message, signature, sizeof(signature)))
            break;
        snprintf(json, sizeof(json), "{\"deviceId\":\"%s\",\"nonce\":\"%s\",\"signature\":\"%s\"}", device_id, nonce_text,
                 signature);
        if (yk_http_json(HTTP_METHOD_POST, "/device/v1/release", NULL, json, &res) != ESP_OK) break;
        const int status = res.status;
        const char *why = status == 200 ? "" : yk_http_error_code(&res);
        yk_http_free(&res);
        // Released, or never added; or not the key it was added with, which nothing here can change: on to activating.
        if (status == 200 || !strcmp(why, "SIGNATURE_INVALID")) {
            release_keep(false);
            return ESP_OK;
        }
        if (strcmp(why, "CHALLENGE_INVALID")) break;  // a stale nonce: once more; anything else waits for the next time
    }
    if (code) *code = "NETWORK";
    return ESP_FAIL;
}

/** Activates with `claim`. Returns NULL or the failure code. */
static const char *activate_now(const char *claim_text) {
    if (!ensure_key()) return "NETWORK";
    char message[200], signature[128], key[100];
    const char *device_id = yk_link_device_id();
    if (!yk_activation_message(device_id, claim_text, message, sizeof(message)) ||
        !sign_b64(message, signature, sizeof(signature)) ||
        !yk_b64url_encode(public_key, sizeof(public_key), key, sizeof(key)))
        return "NETWORK";
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "deviceId", device_id);
    cJSON_AddStringToObject(body, "publicKey", key);
    cJSON_AddStringToObject(body, "claim", claim_text);
    cJSON_AddStringToObject(body, "signature", signature);
    cJSON_AddStringToObject(body, "app", cfg.app_slug);
    cJSON_AddStringToObject(body, "appVersion", cfg.app_version);
    cJSON_AddStringToObject(body, "board", cfg.board);
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    yk_http_response_t res;
    esp_err_t err = json ? yk_http_json(HTTP_METHOD_POST, "/device/v1/activate", NULL, json, &res) : ESP_ERR_NO_MEM;
    cJSON_free(json);
#if CONFIG_YAY_DEV
    xSemaphoreTake(lock, portMAX_DELAY);
    const bool drop = drop_reply;
    drop_reply = false;
    xSemaphoreGive(lock);
    if (drop && err == ESP_OK) {  // the cloud has answered (and bound, if it could); the device never hears it
        ESP_LOGW(TAG, "development: the activation's reply (HTTP %d) dropped, as if the connection broke", res.status);
        yk_http_free(&res);
        err = ESP_FAIL;
    }
#endif
    if (err != ESP_OK) {
        if (err == ESP_ERR_NO_MEM || !bound_anyway()) return "NETWORK";
        save_binding(true);  // the owner's name and language come with the next hello or /me
        return NULL;
    }
    const char *code = NULL;
    if (res.status == 200) {
        cJSON *reply = cJSON_Parse(res.body ? res.body : "");
        keep_token(reply);
        keep_owner(cJSON_GetObjectItemCaseSensitive(reply, "owner"));
        cJSON_Delete(reply);
        save_binding(true);
    } else {
        code = yk_http_error_code(&res);
        // A 5xx is the cloud's trouble whatever it names (503 SERVICE_BUSY too), never the claim's: try again.
        if (res.status >= 500 || !strcmp(code, "SERVER")) code = "NETWORK";
        else if (strcmp(code, "DEVICE_OWNED_BY_OTHER")) code = "CLAIM_INVALID";
        ESP_LOGW(TAG, "activation refused: HTTP %d %s", res.status, code);
        if (res.status >= 500 && bound_anyway()) {
            code = NULL;
            save_binding(true);  // the owner's name and language come with the next hello or /me
        }
    }
    yk_http_free(&res);
    return code;
}

/** yaykit_recstore's (Yay 0.1.20 开发文档 4.1 换主人时清): Yay's recordings that wait on the device are dropped when the
 *  device leaves its owner or gets a new one. Linked weakly: a firmware without the component has nothing to drop. */
void yk_rs_owner_changed(void) __attribute__((weak));

static void owner_changed(void) {
    if (yk_rs_owner_changed) yk_rs_owner_changed();
}

#if CONFIG_YK_REGIONS
/** The claim's region becomes the device's (yk_region_store), before anything is asked of it: from here
 *  yk_cloud_origin() is that side's. Another region than before: every app's kept token goes. NULL, or NETWORK when NVS
 *  would not take it: then nothing is asked (a binding stored under the claim's region's key, with `rg` still the old
 *  one, would be lost at the next start). Not undone if the activation fails: the device is not bound either way, and
 *  the next claim stores its own. */
static const char *take_region(const char *claim_text) {
    bool changed = false;
    if (!yk_region_store(yk_region_for_claim(claim_text), &changed)) return "NETWORK";
    if (changed) kept_tokens_drop_all();
    return NULL;
}
#endif

static void activate_task(void *arg) {
    (void)arg;
    const char *code = NULL;
#if CONFIG_YK_REGIONS
    // 两套部署 (附录 C 2.6): Wi-Fi; a factory reset's release not answered yet, to the region it is for (`rg` as it
    // stands: refused or unreachable, NETWORK, and nothing changes); the claim's region stored, before the request, so
    // that what a power cut leaves (bound_anyway, yk_account_recover) asks the side it went to; then the activation there.
    if (yk_http_prepare(&code) == ESP_OK && yk_account_release(&code) == ESP_OK && !(code = take_region(claim)))
        code = activate_now(claim);
#else
    if (yk_http_prepare(&code) == ESP_OK && yk_account_release(&code) == ESP_OK) code = activate_now(claim);
#endif
    memset(claim, 0, sizeof(claim));
    // Claimed: a new binding, so a new link key on the platform's side (device-link.ts: claimed again, by anyone, the
    // key changes). What the unpaired mode kept was the binding before's, and forget() never saw it go when the claim
    // came without the cloud's DEVICE_NOT_ACTIVATED first (the device removed in the app while it was off).
    if (!code) yk_link_keys_forget();
    if (!code) owner_changed();  // a new binding: nothing of the one before goes up under it
    set_state(code ? YK_ACCOUNT_FAILED : YK_ACCOUNT_BOUND, code);
    vTaskDelete(NULL);
}

/** Bluetooth off first, then the activation's own task: its 10 KB of stack beside Bluetooth's 31.6 KB, and the stop task
 *  yk_link_ble_stop() makes from another core, would come to under the 40,000 bytes line. On NimBLE's core, so that the
 *  stop runs here (#1259 the same way), with the 4 KB the stop has everywhere. */
static void activate_after_link(void *arg) {
    (void)arg;
    // Let the {"state":"received"} reply reach the phone before Bluetooth goes off.
    vTaskDelay(pdMS_TO_TICKS(1500));
    if (yk_link_ble_active()) yk_link_ble_stop();
    // Internal-RAM stack: TLS, signing and NVS writes happen here.
    if (xTaskCreate(activate_task, "yk_activate", 10240, NULL, 5, NULL) != pdPASS) {
        memset(claim, 0, sizeof(claim));
        set_state(YK_ACCOUNT_FAILED, "NETWORK");
    }
    vTaskDelete(NULL);
}

/** Starts activation with a claim. */
static esp_err_t start_activation(const char *claim_text, const char **code) {
    if (!yk_claim_valid(claim_text)) {
        *code = "BAD_CLAIM";
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    bool busy = state == YK_ACCOUNT_ACTIVATING;
#if CONFIG_YK_REGIONS
    // Bound, and the claim is the other side's (附录 C 2.6): refused now, nothing asked of either side, nothing changed.
    // The owner removes it there first, or resets it (the region is cached: no NVS read here).
    const bool other = !busy && bound_kept && yk_region_for_claim(claim_text) != yk_region_current();
    if (!busy && !other) {
#else
    if (!busy) {
#endif
        snprintf(claim, sizeof(claim), "%s", claim_text);
        state = YK_ACCOUNT_ACTIVATING;
        error = "";
        token[0] = 0;
    }
    xSemaphoreGive(lock);
    if (busy) {
        *code = "BUSY";
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_YK_REGIONS
    if (other) {
        ESP_LOGW(TAG, "a claim of another region, bound in %s: refused", yk_region_name());
        *code = "OTHER_REGION";
        return ESP_ERR_INVALID_STATE;
    }
#endif
    changed();
    if (xTaskCreatePinnedToCore(activate_after_link, "yk_act_link", 4096, NULL, 5, NULL, yk_link_ble_core()) != pdPASS) {
        memset(claim, 0, sizeof(claim));
        set_state(YK_ACCOUNT_FAILED, "NETWORK");
        *code = "NO_MEMORY";
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool yk_account_recover(void) {
    if (state != YK_ACCOUNT_UNBOUND || !have_key || release_wanted) return false;
    if (yk_http_prepare(NULL) != ESP_OK || !bound_anyway()) return false;
    save_binding(true);  // the owner's name and language come with the next hello or /me
    owner_changed();     // bound by an activation whose answer was lost: a new binding all the same
    set_state(YK_ACCOUNT_BOUND, NULL);
    return true;
}

static void forget(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    nickname[0] = 0;
    strcpy(locale, "zh");
    token[0] = 0;
    xSemaphoreGive(lock);
    kept_token_drop();
    save_binding(false);
    yk_folder_net_forget();  // the shared folders' answers were this owner's: none is told to the next one
    yk_link_keys_forget();   // the unpaired mode's link keys too: the old owner's phone could still open them
    owner_changed();         // and the recordings Yay kept for them (yaykit_recstore)
    set_state(YK_ACCOUNT_UNBOUND, NULL);
}

/** The unpaired mode's link key for this device and this app (device-link.ts: GET /device/v1/link-key with the app's
 *  token). Only yaykit_link calls it (yk_link_unpaired_refresh(), a weak reference, so it lives here, in the file
 *  every firmware with yaykit_cloud links): no public header declares it and the review blocks it, so an app never
 *  holds a key. ESP_FAIL: no network; ESP_ERR_INVALID_RESPONSE: the platform said no, or nonsense. */
esp_err_t yk_api_link_key_fetch(uint32_t *kid, uint8_t key[32]);
esp_err_t yk_api_link_key_fetch(uint32_t *kid, uint8_t key[32]) {
    for (int attempt = 0; attempt < 2; attempt++) {
        char bearer[512];
        const char *code = NULL;
        if (yk_account_token(bearer, sizeof(bearer), &code) != ESP_OK)
            return code && !strcmp(code, "NETWORK") ? ESP_FAIL : ESP_ERR_INVALID_RESPONSE;
        yk_http_response_t res;
        const esp_err_t sent = yk_http_json(HTTP_METHOD_GET, "/device/v1/link-key", bearer, NULL, &res);
        memset(bearer, 0, sizeof(bearer));
        if (sent != ESP_OK) return ESP_FAIL;
        if (res.status == 200) {
            cJSON *reply = cJSON_Parse(res.body ? res.body : "");
            const cJSON *k = cJSON_GetObjectItemCaseSensitive(reply, "kid");
            const cJSON *text = cJSON_GetObjectItemCaseSensitive(reply, "key");
            bool ok = cJSON_IsNumber(k) && k->valuedouble >= 1 && k->valuedouble <= 4294967295.0 &&
                      cJSON_IsString(text) && strlen(text->valuestring) == 43 &&
                      yk_b64url_decode(text->valuestring, key, 32) == 32;
            if (ok) *kid = (uint32_t)k->valuedouble;
            if (cJSON_IsString(text)) memset(text->valuestring, 0, strlen(text->valuestring));
            cJSON_Delete(reply);
            if (res.body) memset(res.body, 0, res.len);
            yk_http_free(&res);
            if (!ok) memset(key, 0, 32);
            return ok ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
        }
        code = yk_http_error_code(&res);
        const int status = res.status;
        yk_http_free(&res);
        if (status == 401 && !strcmp(code, "TOKEN_EXPIRED")) {
            yk_account_token_expired();
            continue;  // once more with a new token
        }
        if (status == 401 && !strcmp(code, "DEVICE_NOT_ACTIVATED")) yk_account_unbound_by_server();
        ESP_LOGW(TAG, "link key: %d %s", status, code);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

esp_err_t yk_account_init(const yk_account_config_t *config) {
    if (!config || !config->app_slug || !config->app_version || !config->board) return ESP_ERR_INVALID_ARG;
    cfg = *config;
    yk_http_set_app(cfg.app_slug, cfg.app_version);
    yk_folder_net_offer();  // the shared folders' grants at the service, to yaykit_tf when the firmware has it
    if (!lock) lock = xSemaphoreCreateMutex();
    if (!lock) return ESP_ERR_NO_MEM;
#if CONFIG_YK_REGIONS
    yk_region_current();  // the region's `rg` read once now, from the start's task (its stack internal), and kept
#endif
#if CONFIG_YAY_DEV && CONFIG_YK_REGIONS
    test_changed = false;  // a start: its test deployments are the ones in use (read with `rg` just now)
#endif
    nvs_handle_t h;
    uint8_t bound = 0;
    release_wanted = false;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        char key[BINDING_KEY_SIZE];
        nvs_get_u8(h, binding_key(key), &bound);  // this region's: `bound` off, and in global
        uint8_t rel = 0;
        nvs_get_u8(h, "rel", &rel);
        release_wanted = rel;
        size_t len = sizeof(nickname);
        if (nvs_get_str(h, "nick", nickname, &len) != ESP_OK) nickname[0] = 0;
        len = sizeof(locale);
        if (nvs_get_str(h, "loc", locale, &len) != ESP_OK) strcpy(locale, "zh");
        size_t sk_len = sizeof(private_key), pk_len = sizeof(public_key);
        have_key = nvs_get_blob(h, "sk", private_key, &sk_len) == ESP_OK && sk_len == sizeof(private_key) &&
                   nvs_get_blob(h, "pk", public_key, &pk_len) == ESP_OK && pk_len == sizeof(public_key);
        nvs_close(h);
    }
#if CONFIG_YK_REGIONS
    bound_kept = bound;
#endif
    state = bound && have_key ? YK_ACCOUNT_BOUND : YK_ACCOUNT_UNBOUND;
    if (state == YK_ACCOUNT_BOUND) kept_token_load();
    ESP_LOGI(TAG, "%s", yk_account_state_name());
    return ESP_OK;
}

yk_account_state_t yk_account_state(void) { return state; }

const char *yk_account_state_name(void) {
    switch (state) {
        case YK_ACCOUNT_ACTIVATING: return "activating";
        case YK_ACCOUNT_BOUND: return "bound";
        case YK_ACCOUNT_FAILED: return "failed";
        default: return "unbound";
    }
}

const char *yk_account_error(void) { return error; }

void yk_account_nickname(char *out, size_t size) {
    xSemaphoreTake(lock, portMAX_DELAY);
    snprintf(out, size, "%s", nickname);
    xSemaphoreGive(lock);
}

int yk_account_status(char *out, size_t size) {
    if (out && size) yk_account_nickname(out, size);
    return (int)state;
}

void yk_account_locale(char *out, size_t size) {
    xSemaphoreTake(lock, portMAX_DELAY);
    snprintf(out, size, "%s", locale);
    xSemaphoreGive(lock);
}

bool yk_account_english(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    const bool en = yk_locale_english(locale);
    xSemaphoreGive(lock);
    return en;
}

bool yk_account_locale_seen(const char *seen) {
    xSemaphoreTake(lock, portMAX_DELAY);
    const bool took = yk_locale_take(locale, seen);
    const bool bound = state == YK_ACCOUNT_BOUND;
    xSemaphoreGive(lock);
    if (!took) return false;
    nvs_handle_t h;
    if (bound && nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {  // with the binding, as save_binding keeps it
        nvs_set_str(h, "loc", seen);
        nvs_commit(h);
        nvs_close(h);
    }
    changed();
    return true;
}

void yk_account_owner_seen(const cJSON *owner) {
    if (!owner || state != YK_ACCOUNT_BOUND || !keep_owner(owner)) return;
    save_binding(true);
    changed();
}

void yk_account_describe(cJSON *account) {
    cJSON_AddStringToObject(account, "state", yk_account_state_name());
    if (*error) cJSON_AddStringToObject(account, "error", error);
    char nick[64];
    yk_account_nickname(nick, sizeof(nick));
    if (state == YK_ACCOUNT_BOUND && *nick) cJSON_AddStringToObject(account, "owner", nick);
    if (state == YK_ACCOUNT_BOUND) {  // the owner's language as kept ("zh-Hans", "en"…; #545)
        char loc[sizeof(locale)];
        yk_account_locale(loc, sizeof(loc));
        cJSON_AddStringToObject(account, "locale", loc);
    }
#if CONFIG_YK_REGIONS
    // 两套部署 (deployments.ts deviceRegionOf): the side this device is on, bound or not, global's too. Off it is not
    // said at all: a firmware that says it is one that picks its side by the claim (with yk-info's `region` capability).
    cJSON_AddStringToObject(account, "region", yk_region_name());
#endif
}

esp_err_t yk_account_command(const char *name, const cJSON *args, cJSON *reply, const char **code) {
    if (!strcmp(name, "account.activate")) {
        const cJSON *c = cJSON_GetObjectItemCaseSensitive(args, "claim");
        esp_err_t err = start_activation(cJSON_IsString(c) ? c->valuestring : NULL, code);
        if (err == ESP_OK) cJSON_AddStringToObject(reply, "state", "received");
        return err;
    }
    if (!strcmp(name, "account.forget")) forget();
    else if (strcmp(name, "account.status")) return ESP_ERR_NOT_FOUND;
    yk_account_describe(reply);
    return ESP_OK;
}

void yk_account_token_expired(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    token[0] = 0;
    xSemaphoreGive(lock);
    kept_token_drop();
}

void yk_account_unbound_by_server(void) {
    ESP_LOGW(TAG, "the cloud no longer knows this binding");
    forget();
}

static bool token_fresh(void) { return token[0] && esp_timer_get_time() < token_expires_us - 60LL * 1000000; }

bool yk_account_token_cached(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    const bool cached = state == YK_ACCOUNT_BOUND && have_key && token_fresh();
    xSemaphoreGive(lock);
    return cached;
}

esp_err_t yk_account_token(char *out, size_t size, const char **code) {
    xSemaphoreTake(lock, portMAX_DELAY);
    bool bound = state == YK_ACCOUNT_BOUND && have_key;
    bool fresh = token_fresh();
    if (bound && fresh && strlen(token) < size) strcpy(out, token);
    xSemaphoreGive(lock);
    if (!bound) {
        *code = "DEVICE_NOT_ACTIVATED";
        return ESP_ERR_INVALID_STATE;
    }
    if (fresh) return ESP_OK;
    if (yk_http_prepare(code) != ESP_OK) return ESP_FAIL;
    bool unknown;
    const char *why = fetch_token(&unknown);
    if (why) {
        *code = why;
        if (unknown) yk_account_unbound_by_server();
        return ESP_FAIL;
    }
    esp_err_t err = ESP_OK;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (token[0] && strlen(token) < size) strcpy(out, token);
    else err = ESP_FAIL;
    xSemaphoreGive(lock);
    if (err != ESP_OK) *code = "SERVER";
    return err;
}

#if CONFIG_YAY_DEV
#define ACCOUNT_ACTIONS "status | claim <ykc_...> | forget | drop"
#else
#define ACCOUNT_ACTIONS "status | claim <ykc_...> | forget"
#endif

static struct {
    struct arg_str *action;
    struct arg_str *value;
    struct arg_end *end;
} account_args;

static int account_cmd(int argc, char **argv) {
    if (arg_parse(argc, argv, (void **)&account_args)) {
        arg_print_errors(stdout, account_args.end, argv[0]);
        return 1;
    }
    const char *action = account_args.action->count ? account_args.action->sval[0] : "status";
    const char *code = NULL;
    if (!strcmp(action, "claim") && account_args.value->count) {
        if (start_activation(account_args.value->sval[0], &code) != ESP_OK) {
            printf("YK_ACCOUNT error=%s\n", code);
            return 1;
        }
    } else if (!strcmp(action, "forget")) {
        forget();
#if CONFIG_YAY_DEV
    } else if (!strcmp(action, "drop")) {  // development builds: try the activation fallback without the cloud's help
        xSemaphoreTake(lock, portMAX_DELAY);
        drop_reply = true;
        xSemaphoreGive(lock);
        printf("YK_ACCOUNT drop=next (the next activation's reply is dropped once it has come)\n");
#endif
    } else if (strcmp(action, "status")) {
        printf("usage: account %s\n", ACCOUNT_ACTIONS);
        return 1;
    }
    char nick[64];
    yk_account_nickname(nick, sizeof(nick));
    printf("YK_ACCOUNT state=%s error=%s owner=%s device=%s origin=%s\n", yk_account_state_name(), error,
           *nick ? nick : "-", yk_link_device_id(), yk_cloud_origin());
    return 0;
}

#if CONFIG_YAY_DEV && CONFIG_YK_REGIONS
// `region` (development builds with the regions; yk_region_test.h): the region this device is in, each row's two
// addresses as used now and whether a test deployment stands in for it, and a row's test deployment set or cleared for
// the next start. The session commands of before (`yay server` and the like) are not this: with one set too, the session
// is that one's and HTTPS this row's.
#define REGION_ACTIONS "status | set <global|cn> <https://host[:port]> | clear <global|cn>"

static void region_lines(void) {
    printf("YK_REGION region=%s origin=%s\n", yk_region_name(), yk_cloud_origin());
    for (int i = 0; i < yk_region_count(); i++) {
        yk_region_row_t r;
        if (!yk_region_row(i, &r)) continue;
        const char *test = yk_region_test_origin(i), *session = yk_region_test_session(i);
        const char *origin = test ? test : i == YK_REGION_GLOBAL ? CONFIG_YK_CLOUD_ORIGIN : r.device_origin;
        char next[YK_REGION_TEST_ORIGIN_MAX + 2];
        const bool read = yk_region_test_stored(i, next, sizeof(next));
        printf("YK_REGION row=%s origin=%s session=%s test=%s", r.id, origin, session ? session : r.session_url,
               test ? "on" : "-");
        if (!read) printf(" next=? (not read)");
        else if (strcmp(next, test ? test : "")) printf(" next=%s (after a restart)", *next ? next : "-");
        printf("\n");
    }
}

static int region_cmd(int argc, char **argv) {
    const char *action = argc > 1 ? argv[1] : "status";
    const bool set = !strcmp(action, "set"), clear = !strcmp(action, "clear");
    if ((set && argc != 4) || (clear && argc != 3) || (!set && !clear && (strcmp(action, "status") || argc > 2))) {
        printf("usage: region %s\n", REGION_ACTIONS);
        return 1;
    }
    if (set || clear) {
        const int row = yk_region_find(argv[2]);
        bool was_changed = false;
        const char *why = NULL;
        if (!yk_region_test_store(row, set ? argv[3] : NULL, &was_changed, &why)) {
            printf("YK_REGION error=%s\n", why ? why : "not stored");
            return 1;
        }
        // This device's own row: the next start talks to another server, and what this one made is not sent to it.
        // Another row's: nothing kept is that side's (an activation there drops them anyway, yk_region_store()).
        const bool own = was_changed && row == yk_region_current();
        if (own) {
            test_changed = true;
            kept_tokens_drop_all();
        }
        printf("YK_REGION %s=%s%s%s (after a restart%s)\n", action, argv[2], set ? " origin=" : "", set ? argv[3] : "",
               !was_changed ? ", as it was" : own ? ", kept tokens dropped" : "");
        // Bound on that side: a deployment that does not know this device answers DEVICE_NOT_ACTIVATED, and the binding
        // here is forgotten (yk_account_unbound_by_server(); the other side's is not touched).
        if (own && bound_kept)
            printf("YK_REGION note=bound in %s: a deployment that does not know this device unbinds it here\n", argv[2]);
    }
    region_lines();
    return 0;
}
#endif

esp_err_t yk_account_register_console(void) {
    account_args.action = arg_str0(NULL, NULL, "<status|claim|forget>", "action");
    account_args.value = arg_str0(NULL, NULL, "<claim>", "one-time claim for bench activation");
    account_args.end = arg_end(2);
    const esp_console_cmd_t cmd = {
        .command = "account",
        .help = "Yaykit account: " ACCOUNT_ACTIONS,
        .func = account_cmd,
        .argtable = &account_args,
    };
#if CONFIG_YAY_DEV && CONFIG_YK_REGIONS
    const esp_err_t err = esp_console_cmd_register(&cmd);
    if (err != ESP_OK) return err;
    const esp_console_cmd_t region = {
        .command = "region",
        .help = "Development: the region's addresses; a test deployment for a row, from the next start",
        .hint = REGION_ACTIONS,
        .func = region_cmd,
    };
    return esp_console_cmd_register(&region);
#else
    return esp_console_cmd_register(&cmd);
#endif
}
