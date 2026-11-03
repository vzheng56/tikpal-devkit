// Host test: activation and its fallback (Yay 0.1.10). When /device/v1/activate gets no answer, or a 5xx, the device
// asks for a token with its own key: a token means the cloud bound it all the same (10-27: bound, then the reply
// failed), DEVICE_NOT_ACTIVATED means it did not. yaykit_account.c built against test/stub (built with CONFIG_YAY_DEV
// for `account drop`), with HTTPS, NVS, the tasks and the signing answered here. The cases are the ones the round
// screen is to be tried with (the cloud's ACTIVATION_FAULT before/after, and the dropped reply).
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_random.h"
#include "argtable3/argtable3.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "yaykit_account.h"
#include "yaykit_link.h"
#include "yk_http.h"
#include "yk_sign.h"

static int checks, failures;
#define CHECK(c)                                                    \
    do {                                                            \
        checks++;                                                   \
        if (!(c)) {                                                 \
            failures++;                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
        }                                                           \
    } while (0)

static const char *CLAIM = "ykc_0123456789abcdefABCDEF-_";
static const char *ACTIVATED =
    "{\"device\":{\"id\":\"ykd_test\",\"name\":null},\"owner\":{\"nickname\":\"小鱼\",\"locale\":\"en\"},"
    "\"accessToken\":\"yk1.activated\",\"expiresIn\":900}";
static const char *CHALLENGE = "{\"nonce\":\"nonce-1\",\"expiresIn\":60}";
static const char *TOKEN = "{\"accessToken\":\"yk1.by-key\",\"expiresIn\":900}";
static const char *BUSY = "{\"error\":\"SERVICE_BUSY\",\"message\":\"try again\"}";
static const char *NOT_ACTIVATED = "{\"error\":\"DEVICE_NOT_ACTIVATED\"}";

// ---- what the cloud answers, in turn: an HTTP status and body; status 0 is no answer (the connection broke)

#define MAX_CALLS 8
static struct {
    int status;
    const char *body;
} s_replies[MAX_CALLS];
static int s_reply_count, s_calls;
static char s_paths[MAX_CALLS][40], s_bodies[MAX_CALLS][400];
static unsigned s_delayed_ms, s_changes, s_lock_depth, s_lock_overlaps;

static void reply(int status, const char *body) {
    s_replies[s_reply_count].status = status;
    s_replies[s_reply_count].body = body;
    s_reply_count++;
}

static bool called(const char *const *paths, int n) {
    if (s_calls != n) return false;
    for (int i = 0; i < n; i++)
        if (strcmp(s_paths[i], paths[i])) return false;
    return true;
}

static const char *const ACTIVATE_ONLY[] = {"/device/v1/activate"};
static const char *const ONE_TRY[] = {"/device/v1/activate", "/device/v1/token/challenge", "/device/v1/token"};
static const char *const TWO_TRIES[] = {"/device/v1/activate", "/device/v1/token/challenge", "/device/v1/token",
                                        "/device/v1/token/challenge", "/device/v1/token"};
static const char *const NO_ANSWERS[] = {"/device/v1/activate", "/device/v1/token/challenge",
                                         "/device/v1/token/challenge"};

esp_err_t yk_http_json(esp_http_client_method_t method, const char *path, const char *bearer, const char *json,
                       yk_http_response_t *out) {
    (void)method, (void)bearer;
    memset(out, 0, sizeof(*out));
    if (s_calls < MAX_CALLS) {
        snprintf(s_paths[s_calls], sizeof(s_paths[0]), "%s", path);
        snprintf(s_bodies[s_calls], sizeof(s_bodies[0]), "%s", json ? json : "");
    }
    const int i = s_calls++;
    if (i >= s_reply_count || !s_replies[i].status) return ESP_FAIL;
    out->status = s_replies[i].status;
    out->body = strdup(s_replies[i].body);
    out->len = strlen(out->body);
    return ESP_OK;
}
void yk_http_free(yk_http_response_t *response) {
    free(response->body);
    response->body = NULL;
    response->len = 0;
}
const char *yk_http_error_code(const yk_http_response_t *response) {  // as yk_http.c, for the codes used here
    static const char *const known[] = {"DEVICE_NOT_ACTIVATED", "SERVICE_BUSY", "DEVICE_OWNED_BY_OTHER",
                                        "CLAIM_INVALID", "SIGNATURE_INVALID", "CHALLENGE_INVALID"};
    const char *code = "SERVER";
    cJSON *json = response->body ? cJSON_Parse(response->body) : NULL;
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(json, "error");
    for (size_t i = 0; cJSON_IsString(error) && i < sizeof(known) / sizeof(known[0]); i++)
        if (!strcmp(error->valuestring, known[i])) code = known[i];
    cJSON_Delete(json);
    return code;
}
esp_err_t yk_http_prepare(const char **code) {
    (void)code;
    return ESP_OK;
}
void yk_http_set_app(const char *app, const char *version) { (void)app, (void)version; }

// ---- the device: its ID, its key, NVS in memory, tasks that run to their end at once

const char *yk_link_device_id(void) { return "ykd_test"; }
void esp_fill_random(void *buf, size_t len) { memset(buf, 7, len); }
int yk_key_generate(uint8_t sk[YK_KEY_PRIVATE_BYTES], uint8_t pk[YK_KEY_PUBLIC_BYTES], yk_rng_fn rng, void *rng_ctx) {
    (void)rng, (void)rng_ctx;
    memset(sk, 1, YK_KEY_PRIVATE_BYTES);
    memset(pk, 4, YK_KEY_PUBLIC_BYTES);
    return 0;
}
size_t yk_sign(const uint8_t sk[YK_KEY_PRIVATE_BYTES], const char *message, uint8_t sig[YK_SIGNATURE_MAX],
               yk_rng_fn rng, void *rng_ctx) {
    (void)sk, (void)message, (void)rng, (void)rng_ctx;
    memset(sig, 0x30, 8);
    return 8;
}
int64_t esp_timer_get_time(void) { return 0; }

static int s_mutex;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &s_mutex; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait) {
    (void)mutex, (void)wait;
    if (s_lock_depth++) s_lock_overlaps++;  // FreeRTOS's mutex is not recursive: taken twice is a deadlock
    return pdPASS;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) {
    (void)mutex;
    s_lock_depth--;
    return pdPASS;
}
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, unsigned priority,
                       TaskHandle_t *created) {
    (void)name, (void)stack, (void)priority, (void)created;
    fn(arg);
    return pdPASS;
}
void vTaskDelay(TickType_t ticks) { s_delayed_ms += ticks; }
void vTaskDelete(TaskHandle_t task) { (void)task; }

#define NVS_MAX 12
static struct {
    char key[16];
    unsigned char data[1024];
    size_t len;
} s_nvs[NVS_MAX];
static int s_nvs_n;

static int nvs_find(const char *key) {
    for (int i = 0; i < s_nvs_n; i++)
        if (!strcmp(s_nvs[i].key, key)) return i;
    return -1;
}
static esp_err_t nvs_put(const char *key, const void *value, size_t len) {
    int i = nvs_find(key);
    if (i < 0 && s_nvs_n < NVS_MAX) i = s_nvs_n++;
    if (i < 0 || len > sizeof(s_nvs[0].data)) return ESP_FAIL;
    snprintf(s_nvs[i].key, sizeof(s_nvs[i].key), "%s", key);
    memcpy(s_nvs[i].data, value, len);
    s_nvs[i].len = len;
    return ESP_OK;
}
static esp_err_t nvs_take(const char *key, void *out, size_t *len) {
    const int i = nvs_find(key);
    if (i < 0) return ESP_ERR_NVS_NOT_FOUND;
    if (!out) {
        *len = s_nvs[i].len;
        return ESP_OK;
    }
    if (*len < s_nvs[i].len) return ESP_ERR_INVALID_SIZE;
    memcpy(out, s_nvs[i].data, s_nvs[i].len);
    *len = s_nvs[i].len;
    return ESP_OK;
}
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *handle) {
    (void)mode;
    *handle = 1;
    return strcmp(ns, "yk_acct") ? ESP_FAIL : ESP_OK;
}
void nvs_close(nvs_handle_t handle) { (void)handle; }
esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle;
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    (void)handle;
    const int i = nvs_find(key);
    if (i < 0) return ESP_ERR_NVS_NOT_FOUND;
    s_nvs[i] = s_nvs[--s_nvs_n];
    return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value) {
    (void)handle;
    return nvs_put(key, &value, 1);
}
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value) {
    (void)handle;
    size_t len = 1;
    return nvs_take(key, value, &len);
}
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value) {
    (void)handle;
    return nvs_put(key, value, strlen(value) + 1);
}
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out, size_t *length) {
    (void)handle;
    return nvs_take(key, out, length);
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length) {
    (void)handle;
    return nvs_put(key, value, length);
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length) {
    (void)handle;
    return nvs_take(key, out, length);
}

static int nvs_bound(void) {  // yk_acct/bound as stored: -1 never written
    const int i = nvs_find("bound");
    return i < 0 ? -1 : s_nvs[i].data[0];
}

// ---- the console: only `account drop` is used

static esp_console_cmd_func_t s_account_cmd;
static struct arg_str s_args[2];
static const char *s_svals[2][1];
static struct arg_end s_end;
static int s_args_made;
esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd) {
    s_account_cmd = cmd->func;
    return ESP_OK;
}
struct arg_str *arg_str0(const char *shortopts, const char *longopts, const char *datatype, const char *glossary) {
    (void)shortopts, (void)longopts, (void)datatype, (void)glossary;
    struct arg_str *a = &s_args[s_args_made % 2];
    a->sval = s_svals[s_args_made++ % 2];
    return a;
}
struct arg_end *arg_end(int maxerrors) {
    (void)maxerrors;
    return &s_end;
}
int arg_parse(int argc, char **argv, void **argtable) {
    struct arg_str *action = argtable[0], *value = argtable[1];
    action->count = argc > 1;
    if (argc > 1) action->sval[0] = argv[1];
    value->count = argc > 2;
    if (argc > 2) value->sval[0] = argv[2];
    return 0;
}
void arg_print_errors(FILE *fp, struct arg_end *end, const char *progname) { (void)fp, (void)end, (void)progname; }

// ---- the cases

static void on_change(void *ctx) {
    (void)ctx;
    s_changes++;
}

/** A device that was never bound (or was unbound on the phone): nothing kept, the cloud's answers queued anew. */
static void fresh(void) {
    s_nvs_n = 0;
    memset(s_replies, 0, sizeof(s_replies));
    s_reply_count = s_calls = 0;
    s_delayed_ms = s_changes = 0;
    const yk_account_config_t cfg = {.app_slug = "yay", .app_version = "0.1.10", .board = "tikpal-r1",
                                     .on_change = on_change};
    CHECK(yk_account_init(&cfg) == ESP_OK);
    CHECK(yk_account_state() == YK_ACCOUNT_UNBOUND);
}

/** account.activate as the phone sends it; the activation task has run to its end when this returns. */
static void activate(void) {
    s_calls = 0;
    s_delayed_ms = 0;
    cJSON *args = cJSON_CreateObject(), *answer = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "claim", CLAIM);
    const char *code = NULL;
    CHECK(yk_account_command("account.activate", args, answer, &code) == ESP_OK);
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(answer, "state");
    CHECK(cJSON_IsString(state) && !strcmp(state->valuestring, "received"));
    cJSON_Delete(args);
    cJSON_Delete(answer);
}

/** The token the apps get now, and how many requests that took. */
static bool token_is(const char *want, int requests) {
    const int before = s_calls;
    char out[64];
    const char *code = NULL;
    return yk_account_token(out, sizeof(out), &code) == ESP_OK && !strcmp(out, want) && s_calls - before == requests;
}

static bool failed_with(const char *code) {
    return yk_account_state() == YK_ACCOUNT_FAILED && !strcmp(yk_account_error(), code) && nvs_bound() != 1;
}

static bool bound_by_key(void) {  // bound, the owner's name still to come (hello.ok or /me); the token is the key's
    char nick[64];
    yk_account_nickname(nick, sizeof(nick));
    return yk_account_state() == YK_ACCOUNT_BOUND && !*yk_account_error() && nvs_bound() == 1 && !*nick &&
           token_is("yk1.by-key", 0);
}

int main(void) {
    // Answered: bound with the owner and the activation's own token; no fallback
    fresh();
    reply(200, ACTIVATED);
    activate();
    CHECK(called(ACTIVATE_ONLY, 1) && s_delayed_ms == 1500);
    char nick[64];
    yk_account_nickname(nick, sizeof(nick));
    CHECK(yk_account_state() == YK_ACCOUNT_BOUND && nvs_bound() == 1 && !strcmp(nick, "小鱼"));
    CHECK(token_is("yk1.activated", 0));
    CHECK(s_changes >= 2);  // activating, then bound

    // ACTIVATION_FAULT before: 503 SERVICE_BUSY and nothing written. The key gets no token (DEVICE_NOT_ACTIVATED):
    // failed as a network problem (not the claim's), not bound, asked once only; the next claim activates as ever.
    fresh();
    reply(503, BUSY);
    reply(200, CHALLENGE);
    reply(401, NOT_ACTIVATED);
    activate();
    CHECK(called(ONE_TRY, 3) && s_delayed_ms == 1500 + 1000);
    CHECK(failed_with("NETWORK"));
    char out[64];
    const char *code = NULL;
    CHECK(yk_account_token(out, sizeof(out), &code) == ESP_ERR_INVALID_STATE && !strcmp(code, "DEVICE_NOT_ACTIVATED"));
    memset(s_replies, 0, sizeof(s_replies));
    s_reply_count = 0;
    reply(200, ACTIVATED);
    activate();
    CHECK(called(ACTIVATE_ONLY, 1) && yk_account_state() == YK_ACCOUNT_BOUND && token_is("yk1.activated", 0));

    // ACTIVATION_FAULT after: bound and the claim used, the reply a 503. The key gets a token: bound
    fresh();
    reply(503, BUSY);
    reply(200, CHALLENGE);
    reply(200, TOKEN);
    activate();
    CHECK(called(ONE_TRY, 3) && s_delayed_ms == 1500 + 1000);
    CHECK(bound_by_key());
    CHECK(strstr(s_bodies[1], "\"deviceId\":\"ykd_test\"") != NULL);
    CHECK(strstr(s_bodies[2], "\"app\":\"yay\"") && strstr(s_bodies[2], "\"nonce\":\"nonce-1\""));

    // The same with a plain 500 (the 10-27 incident) and with a proxy's 502 page
    fresh();
    reply(500, "{\"error\":\"INTERNAL\"}");
    reply(200, CHALLENGE);
    reply(200, TOKEN);
    activate();
    CHECK(called(ONE_TRY, 3) && bound_by_key());
    fresh();
    reply(502, "<html>bad gateway</html>");
    reply(200, CHALLENGE);
    reply(200, TOKEN);
    activate();
    CHECK(called(ONE_TRY, 3) && bound_by_key());

    // The connection broke on the reply's way: no answer at all. The key gets a token: bound
    fresh();
    reply(0, NULL);
    reply(200, CHALLENGE);
    reply(200, TOKEN);
    activate();
    CHECK(called(ONE_TRY, 3) && bound_by_key());

    // ... and when it never reached the cloud: not bound
    fresh();
    reply(0, NULL);
    reply(200, CHALLENGE);
    reply(401, NOT_ACTIVATED);
    activate();
    CHECK(called(ONE_TRY, 3) && failed_with("NETWORK"));

    // The binding still on its way: no token the first time (a 5xx), one 3 seconds later
    fresh();
    reply(503, BUSY);
    reply(200, CHALLENGE);
    reply(503, BUSY);
    reply(200, CHALLENGE);
    reply(200, TOKEN);
    activate();
    CHECK(called(TWO_TRIES, 5) && s_delayed_ms == 1500 + 1000 + 3000);
    CHECK(bound_by_key());

    // The network gone for good: two tries, then failed (the pairing page again, the phone can send a claim again)
    fresh();
    reply(503, BUSY);
    activate();
    CHECK(called(NO_ANSWERS, 3) && s_delayed_ms == 1500 + 1000 + 3000);
    CHECK(failed_with("NETWORK"));

    // A 200 without a token from the key's request is no binding to go by
    fresh();
    reply(503, BUSY);
    reply(200, CHALLENGE);
    reply(200, "{\"expiresIn\":900}");
    reply(200, CHALLENGE);
    reply(200, "{}");
    activate();
    CHECK(called(TWO_TRIES, 5) && failed_with("NETWORK"));

    // Refused (4xx): the cloud said why, no fallback
    fresh();
    reply(401, "{\"error\":\"CLAIM_INVALID\"}");
    activate();
    CHECK(called(ACTIVATE_ONLY, 1) && s_delayed_ms == 1500 && failed_with("CLAIM_INVALID"));
    fresh();
    reply(409, "{\"error\":\"DEVICE_OWNED_BY_OTHER\"}");
    activate();
    CHECK(called(ACTIVATE_ONLY, 1) && failed_with("DEVICE_OWNED_BY_OTHER"));

    // Development builds: `account drop` drops the next activation's reply once it has come (the cloud bound it): the
    // fallback binds, with the key's token, not the reply's. Only once: the activation after it is answered as ever.
    fresh();
    CHECK(yk_account_register_console() == ESP_OK && s_account_cmd);
    char *drop[] = {"account", "drop"};
    CHECK(s_account_cmd(2, drop) == 0);
    reply(200, ACTIVATED);
    reply(200, CHALLENGE);
    reply(200, TOKEN);
    activate();
    CHECK(called(ONE_TRY, 3) && bound_by_key());
    fresh();
    reply(200, ACTIVATED);
    activate();
    CHECK(called(ACTIVATE_ONLY, 1) && token_is("yk1.activated", 0));

    // Apps' tokens (the same requests, now shared with the fallback): fetched when stale; DEVICE_NOT_ACTIVATED
    // forgets the binding; no answer keeps it
    fresh();
    reply(200, ACTIVATED);
    activate();
    yk_account_token_expired();
    memset(s_replies, 0, sizeof(s_replies));
    s_reply_count = s_calls = 0;
    reply(200, CHALLENGE);
    reply(200, TOKEN);
    CHECK(token_is("yk1.by-key", 2));
    yk_account_token_expired();
    s_calls = 0;
    memset(s_replies, 0, sizeof(s_replies));
    s_reply_count = 0;
    reply(0, NULL);
    CHECK(yk_account_token(out, sizeof(out), &code) == ESP_FAIL && !strcmp(code, "NETWORK"));
    CHECK(yk_account_state() == YK_ACCOUNT_BOUND);
    s_calls = 0;
    memset(s_replies, 0, sizeof(s_replies));
    s_reply_count = 0;
    reply(200, CHALLENGE);
    reply(401, NOT_ACTIVATED);
    CHECK(yk_account_token(out, sizeof(out), &code) == ESP_FAIL && !strcmp(code, "DEVICE_NOT_ACTIVATED"));
    CHECK(yk_account_state() == YK_ACCOUNT_UNBOUND && nvs_bound() == 0);

    CHECK(s_lock_overlaps == 0 && s_lock_depth == 0);

    printf("yaykit_cloud account host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
