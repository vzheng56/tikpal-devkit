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
                                        "CLAIM_INVALID", "SIGNATURE_INVALID", "CHALLENGE_INVALID", "TOKEN_EXPIRED"};
    const char *code = "SERVER";
    cJSON *json = response->body ? cJSON_Parse(response->body) : NULL;
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(json, "error");
    for (size_t i = 0; cJSON_IsString(error) && i < sizeof(known) / sizeof(known[0]); i++)
        if (!strcmp(error->valuestring, known[i])) code = known[i];
    cJSON_Delete(json);
    return code;
}
static const char *s_prepare_says;  // yk_http_prepare refuses with this code (LINK: Bluetooth came on while it waited)
esp_err_t yk_http_prepare(const char **code) {
    if (!s_prepare_says) return ESP_OK;
    *code = s_prepare_says;
    return ESP_ERR_INVALID_STATE;
}
void yk_http_set_app(const char *app, const char *version) { (void)app, (void)version; }
// The shared folders' calls, handed to yaykit_tf as the account starts (yk_folder_net.c).
static int s_offered;
void yk_folder_net_offer(void) { s_offered++; }
static int s_forgotten;
void yk_folder_net_forget(void) { s_forgotten++; }
// The unpaired mode's link keys (yaykit_link): forgotten with the owner.
static int s_keys_forgotten;
void yk_link_keys_forget(void) { s_keys_forgotten++; }
// yaykit_recstore's (Yay 0.1.20 4.1 换主人时清): Yay's recordings that wait dropped with the owner, linked weakly.
static int s_owner_changed;
void yk_rs_owner_changed(void) { s_owner_changed++; }
esp_err_t yk_api_link_key_fetch(uint32_t *kid, uint8_t key[32]);  // yaykit_account.c, in no header

// ---- the device: its ID, its key, NVS in memory, tasks that run to their end at once

const char *yk_link_device_id(void) { return "ykd_test"; }

// Bluetooth: what stood beside it. The activation's own stack must not; the stop runs after the reply's 1.5 s, in the
// task pinned to NimBLE's core, so that yk_link_ble_stop() makes no stop task of its own.
#define NIMBLE_CORE 1
static bool s_ble;
static int s_ble_stops, s_task_core = -1, s_stop_core = -1;
static unsigned s_stop_delayed_ms, s_big_beside_ble;
static uint32_t s_pinned_stack, s_activate_stack;
static const char *s_no_task;  // this task cannot be made
bool yk_link_ble_active(void) { return s_ble; }
esp_err_t yk_link_ble_stop(void) {
    s_ble = false;
    s_ble_stops++;
    s_stop_delayed_ms = s_delayed_ms;
    s_stop_core = s_task_core;
    return ESP_OK;
}
int yk_link_ble_core(void) { return NIMBLE_CORE; }
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
    (void)priority, (void)created;
    if (s_no_task && !strcmp(s_no_task, name)) return pdFAIL;
    if (s_ble) s_big_beside_ble += stack;
    s_activate_stack = stack;
    const int core = s_task_core;
    s_task_core = -1;  // anywhere: a task of its own
    fn(arg);
    s_task_core = core;
    return pdPASS;
}
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, unsigned priority,
                                   TaskHandle_t *created, int core) {
    (void)priority, (void)created;
    if (s_no_task && !strcmp(s_no_task, name)) return pdFAIL;
    s_pinned_stack = stack;
    s_task_core = core;
    fn(arg);
    s_task_core = -1;
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
    s_ble = false;
    s_ble_stops = 0, s_stop_core = -1;
    s_stop_delayed_ms = s_big_beside_ble = 0;
    s_pinned_stack = s_activate_stack = 0;
    s_no_task = s_prepare_says = NULL;
    const yk_account_config_t cfg = {.app_slug = "yay", .app_version = "0.1.10", .board = "tikpal-r1",
                                     .on_change = on_change};
    CHECK(yk_account_init(&cfg) == ESP_OK);
    CHECK(s_offered >= 1);  // the shared folders' calls go to yaykit_tf as it starts
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
    int keys_before = s_keys_forgotten, owner_before = s_owner_changed;
    activate();
    CHECK(called(ACTIVATE_ONLY, 1) && s_delayed_ms == 1500);
    // A new binding: the link keys of the one before go (the old owner's phone holds them; the platform has new ones),
    // and the recordings Yay kept for the one before.
    CHECK(s_keys_forgotten == keys_before + 1);
    CHECK(s_owner_changed == owner_before + 1);
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
    keys_before = s_keys_forgotten;
    owner_before = s_owner_changed;
    activate();
    CHECK(called(ONE_TRY, 3) && s_delayed_ms == 1500 + 1000);
    CHECK(failed_with("NETWORK"));
    CHECK(s_keys_forgotten == keys_before);  // not claimed: the keys stay
    CHECK(s_owner_changed == owner_before);  // ... and the recordings
    char out[64];
    const char *code = NULL;
    CHECK(yk_account_token(out, sizeof(out), &code) == ESP_ERR_INVALID_STATE && !strcmp(code, "DEVICE_NOT_ACTIVATED"));
    memset(s_replies, 0, sizeof(s_replies));
    s_reply_count = 0;
    reply(200, ACTIVATED);
    activate();
    CHECK(called(ACTIVATE_ONLY, 1) && yk_account_state() == YK_ACCOUNT_BOUND && token_is("yk1.activated", 0));

    // A power cut took the answer (登录与开箱 8.1): at the next start the key is there and the device not bound. The key
    // gets a token: bound. It gets none: not, and it says so at once, no second try.
    fresh();
    reply(503, BUSY);
    reply(200, CHALLENGE);
    reply(401, NOT_ACTIVATED);
    activate();
    static const char *const RECOVER_TRY[] = {"/device/v1/token/challenge", "/device/v1/token"};
    memset(s_replies, 0, sizeof(s_replies));
    s_reply_count = s_calls = 0;
    CHECK(yk_account_init(&(yk_account_config_t){.app_slug = "yay", .app_version = "0.1.0", .board = "tikpal-r1"}) == ESP_OK);
    CHECK(yk_account_state() == YK_ACCOUNT_UNBOUND);
    reply(200, CHALLENGE);
    reply(401, NOT_ACTIVATED);
    CHECK(!yk_account_recover() && called(RECOVER_TRY, 2) && yk_account_state() == YK_ACCOUNT_UNBOUND && nvs_bound() != 1);
    memset(s_replies, 0, sizeof(s_replies));
    s_reply_count = s_calls = 0;
    reply(200, CHALLENGE);
    reply(200, TOKEN);
    owner_before = s_owner_changed;
    CHECK(yk_account_recover() && called(RECOVER_TRY, 2) && yk_account_state() == YK_ACCOUNT_BOUND && nvs_bound() == 1);
    CHECK(s_owner_changed == owner_before + 1);  // bound by the claim whose answer was lost: a new binding
    // Bound already, or no key yet: nothing is asked.
    s_calls = 0;
    CHECK(!yk_account_recover() && s_calls == 0);
    fresh();
    CHECK(!yk_account_recover() && s_calls == 0);

    // A factory reset (11.4): the mark first, the signed release, answered: gone. No mark: nothing is asked.
    static const char *const RELEASE_TRY[] = {"/device/v1/token/challenge", "/device/v1/release"};
    fresh();
    reply(200, ACTIVATED);
    activate();
    memset(s_replies, 0, sizeof(s_replies));
    s_reply_count = s_calls = 0;
    CHECK(yk_account_release(NULL) == ESP_OK && s_calls == 0);
    yk_account_release_mark();
    CHECK(yk_account_release_pending());
    // Marked and still bound (the reset under way): no new token, whatever is asked.
    char rel_token[64];
    const char *rel_why = NULL;
    yk_account_token_expired();
    CHECK(yk_account_token(rel_token, sizeof(rel_token), &rel_why) != ESP_OK && s_calls == 0);
    reply(200, CHALLENGE);
    reply(200, "{\"released\":true}");
    CHECK(yk_account_release(NULL) == ESP_OK && called(RELEASE_TRY, 2) && !yk_account_release_pending());
    CHECK(strstr(s_bodies[1], "\"deviceId\":\"ykd_test\"") && strstr(s_bodies[1], "\"nonce\":\"nonce-1\"") &&
          strstr(s_bodies[1], "\"signature\":"));
    // A stale nonce: once more. Not the key it was added with: on to activating, nothing here can change that.
    yk_account_release_mark();
    memset(s_replies, 0, sizeof(s_replies));
    s_reply_count = s_calls = 0;
    reply(200, CHALLENGE);
    reply(401, "{\"error\":\"CHALLENGE_INVALID\"}");
    reply(200, CHALLENGE);
    reply(401, "{\"error\":\"SIGNATURE_INVALID\"}");
    CHECK(yk_account_release(NULL) == ESP_OK && s_calls == 4 && !yk_account_release_pending());
    // No network, or a server error: the mark stays, kept across a start, and no token is taken meanwhile.
    yk_account_release_mark();
    memset(s_replies, 0, sizeof(s_replies));
    s_reply_count = s_calls = 0;
    reply(0, NULL);
    const char *rel_code = NULL;
    CHECK(yk_account_release(&rel_code) == ESP_FAIL && !strcmp(rel_code, "NETWORK") && yk_account_release_pending());
    memset(s_replies, 0, sizeof(s_replies));
    s_reply_count = s_calls = 0;
    reply(200, CHALLENGE);
    reply(500, "{\"error\":\"INTERNAL\"}");
    CHECK(yk_account_release(NULL) == ESP_FAIL && yk_account_release_pending());
    const int forgotten = s_forgotten, keys_forgotten = s_keys_forgotten;
    yk_account_unbound_by_server();  // what the wipe leaves of the binding: the keys and the mark
    CHECK(s_forgotten == forgotten + 1);  // ... and the shared folders' answers go with the owner
    CHECK(s_keys_forgotten == keys_forgotten + 1);  // ... and the link keys: the old owner's phone holds them too
    CHECK(yk_account_init(&(yk_account_config_t){.app_slug = "yay", .app_version = "0.1.0", .board = "tikpal-r1"}) == ESP_OK);
    CHECK(yk_account_release_pending() && yk_account_state() == YK_ACCOUNT_UNBOUND);
    memset(s_replies, 0, sizeof(s_replies));
    s_reply_count = s_calls = 0;
    CHECK(!yk_account_recover() && s_calls == 0);  // the old owner's token stays out of reach
    // The next activation asks first; refused to the network, it does not activate.
    reply(0, NULL);
    activate();
    CHECK(failed_with("NETWORK") && s_calls == 1 && yk_account_release_pending());
    memset(s_replies, 0, sizeof(s_replies));
    s_reply_count = s_calls = 0;
    reply(200, CHALLENGE);
    reply(200, "{\"released\":true}");
    reply(200, ACTIVATED);
    activate();
    static const char *const RELEASE_THEN[] = {"/device/v1/token/challenge", "/device/v1/release", "/device/v1/activate"};
    CHECK(called(RELEASE_THEN, 3) && yk_account_state() == YK_ACCOUNT_BOUND && !yk_account_release_pending());

    // ACTIVATION_FAULT after: bound and the claim used, the reply a 503. The key gets a token: bound
    fresh();
    reply(503, BUSY);
    reply(200, CHALLENGE);
    reply(200, TOKEN);
    keys_before = s_keys_forgotten;
    activate();
    CHECK(called(ONE_TRY, 3) && s_delayed_ms == 1500 + 1000);
    CHECK(bound_by_key());
    CHECK(s_keys_forgotten == keys_before + 1);  // claimed all the same: a new binding
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
    owner_before = s_owner_changed;
    CHECK(yk_account_token(out, sizeof(out), &code) == ESP_FAIL && !strcmp(code, "DEVICE_NOT_ACTIVATED"));
    CHECK(yk_account_state() == YK_ACCOUNT_UNBOUND && nvs_bound() == 0);
    CHECK(s_owner_changed == owner_before + 1);  // unbound by the cloud: the recordings go too

    // Bluetooth on as the claim comes (the phone's, always): the reply's 1.5 s first, then off, from the core it runs on
    // (no stop task beside the caller's stack), and only then the activation's 10 KB. On Bluetooth's 31.6 KB together
    // they came to about 33 KB, under the 40,000 bytes line.
    fresh();
    s_ble = true;
    reply(200, ACTIVATED);
    activate();
    CHECK(called(ACTIVATE_ONLY, 1) && yk_account_state() == YK_ACCOUNT_BOUND);
    CHECK(s_ble_stops == 1 && !s_ble && s_stop_delayed_ms == 1500);
    CHECK(s_stop_core == NIMBLE_CORE && s_pinned_stack <= 4096);
    CHECK(s_big_beside_ble == 0 && s_activate_stack == 10240);
    // No Bluetooth (the console's `account claim`): nothing to stop, the same activation
    fresh();
    reply(200, ACTIVATED);
    activate();
    CHECK(s_ble_stops == 0 && s_delayed_ms == 1500 && yk_account_state() == YK_ACCOUNT_BOUND);
    // No room for the first task: told at once, as ever
    fresh();
    s_no_task = "yk_act_link";
    {
        cJSON *args = cJSON_CreateObject(), *answer = cJSON_CreateObject();
        cJSON_AddStringToObject(args, "claim", CLAIM);
        const char *why = NULL;
        CHECK(yk_account_command("account.activate", args, answer, &why) == ESP_ERR_NO_MEM && !strcmp(why, "NO_MEMORY"));
        cJSON_Delete(args);
        cJSON_Delete(answer);
    }
    CHECK(failed_with("NETWORK") && s_calls == 0);
    // None for the second, Bluetooth already off: it fails as a network problem, and the phone hears it from the cloud
    fresh();
    s_no_task = "yk_activate";
    s_ble = true;
    activate();
    CHECK(failed_with("NETWORK") && s_calls == 0 && s_ble_stops == 1);

    // A stale token with Bluetooth come on during yk_http_prepare's wait for Wi-Fi: no request at all, the binding as it was
    fresh();
    reply(200, ACTIVATED);
    activate();
    yk_account_token_expired();
    s_calls = 0;
    s_prepare_says = "LINK";
    CHECK(yk_account_token(out, sizeof(out), &code) != ESP_OK && !strcmp(code, "LINK"));
    CHECK(s_calls == 0 && yk_account_state() == YK_ACCOUNT_BOUND);
    s_prepare_says = NULL;

    // The unpaired mode's link key (device-link.ts GET /device/v1/link-key, the app's token): the contract fixture's
    // key and kid; a token gone stale fetched again once; a refusal, nonsense or no answer keep nothing.
    {
        static const char *LINK_KEY = "{\"kid\":1513918951,\"key\":\"ax-TLNQHXqgx8kydYBW34wqeRMF9KLVj7xKKVjnQS_c\"}";
        static const char *const ONE[] = {"/device/v1/link-key"};
        static const char *const AGAIN[] = {"/device/v1/link-key", "/device/v1/token/challenge", "/device/v1/token",
                                            "/device/v1/link-key"};
        uint8_t key[32];
        uint32_t kid = 0;
        fresh();
        reply(200, ACTIVATED);
        activate();
        memset(s_replies, 0, sizeof(s_replies));
        s_reply_count = s_calls = 0;
        reply(200, LINK_KEY);
        CHECK(yk_api_link_key_fetch(&kid, key) == ESP_OK && called(ONE, 1));
        CHECK(kid == 1513918951u && key[0] == 0x6b && key[1] == 0x1f && key[31] == 0xf7);
        memset(s_replies, 0, sizeof(s_replies));
        s_reply_count = s_calls = 0;
        reply(401, "{\"error\":\"TOKEN_EXPIRED\"}");
        reply(200, CHALLENGE);
        reply(200, TOKEN);
        reply(200, LINK_KEY);
        kid = 0;
        CHECK(yk_api_link_key_fetch(&kid, key) == ESP_OK && called(AGAIN, 4) && kid == 1513918951u);
        const char *const nonsense[] = {"{\"kid\":0,\"key\":\"ax-TLNQHXqgx8kydYBW34wqeRMF9KLVj7xKKVjnQS_c\"}",
                                        "{\"kid\":7,\"key\":\"ax-TLNQHXqgx8kydYBW34wqeRMF9KLVj7xKKVjnQS_\"}",
                                        "{\"kid\":7,\"key\":\"ax+TLNQHXqgx8kydYBW34wqeRMF9KLVj7xKKVjnQS_c\"}",
                                        "{\"kid\":4294967296,\"key\":\"ax-TLNQHXqgx8kydYBW34wqeRMF9KLVj7xKKVjnQS_c\"}",
                                        "not json"};
        for (size_t i = 0; i < sizeof(nonsense) / sizeof(nonsense[0]); i++) {
            memset(s_replies, 0, sizeof(s_replies));
            s_reply_count = s_calls = 0;
            reply(200, nonsense[i]);
            memset(key, 0x55, sizeof(key));
            CHECK(yk_api_link_key_fetch(&kid, key) == ESP_ERR_INVALID_RESPONSE && key[0] == 0 && key[31] == 0);
        }
        memset(s_replies, 0, sizeof(s_replies));
        s_reply_count = s_calls = 0;
        reply(403, "{\"error\":\"OPERATION_NOT_ALLOWED\"}");  // the app may not use the link
        CHECK(yk_api_link_key_fetch(&kid, key) == ESP_ERR_INVALID_RESPONSE && called(ONE, 1));
        memset(s_replies, 0, sizeof(s_replies));
        s_reply_count = s_calls = 0;
        reply(0, NULL);
        CHECK(yk_api_link_key_fetch(&kid, key) == ESP_FAIL);
        memset(s_replies, 0, sizeof(s_replies));
        s_reply_count = s_calls = 0;
        reply(401, NOT_ACTIVATED);  // unbound on the account page meanwhile: the binding and the link keys go
        const int before = s_keys_forgotten;
        CHECK(yk_api_link_key_fetch(&kid, key) == ESP_ERR_INVALID_RESPONSE);
        CHECK(yk_account_state() == YK_ACCOUNT_UNBOUND && s_keys_forgotten == before + 1);
    }

    CHECK(s_lock_overlaps == 0 && s_lock_depth == 0);

    printf("yaykit_cloud account host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
