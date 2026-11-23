// Host test: activation and its fallback (Yay 0.1.10). When /device/v1/activate gets no answer, or a 5xx, the device
// asks for a token with its own key: a token means the cloud bound it all the same (10-27: bound, then the reply
// failed), DEVICE_NOT_ACTIVATED means it did not. yaykit_account.c built against test/stub (built with CONFIG_YAY_DEV
// for `account drop`), with HTTPS, NVS, the tasks and the signing answered here. The cases are the ones the round
// screen is to be tried with (the cloud's ACTIVATION_FAULT before/after, and the dropped reply).
#define _POSIX_C_SOURCE 200809L  // dup(), dup2(), fileno(): what `region` prints, read back
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_console.h"
#include "esp_random.h"
#include "argtable3/argtable3.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "yaykit_account.h"
#include "yaykit_link.h"
#include "yk_codec.h"
#include "yk_http.h"
#include "yk_sign.h"
// The region (两套部署) itself, built in here: a restart (restart() below) forgets what it kept, as a device's does.
#include "../yk_region.c"

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
// Where each request went (yk_http.c puts yk_cloud_origin() before the path) and NVS's `rg` as it went ("": none)
static char s_origins[MAX_CALLS][48], s_rg_then[MAX_CALLS][24];
static const char *nvs_str(const char *key);
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
        snprintf(s_origins[s_calls], sizeof(s_origins[0]), "%s", yk_cloud_origin());
        const char *rg = nvs_str("rg");
        snprintf(s_rg_then[s_calls], sizeof(s_rg_then[0]), "%s", rg ? rg : "");
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

#define NVS_MAX 24
typedef struct {
    char key[NVS_KEY_NAME_MAX_SIZE];
    nvs_type_t type;
    unsigned char data[1024];
    size_t len;
} nvs_entry_t;
static nvs_entry_t s_nvs[NVS_MAX];
static int s_nvs_n;
static bool s_region_keys;  // the code wrote `rg` or a `bound_<region>` (never with CONFIG_YK_REGIONS off)

static int nvs_find(const char *key) {
    for (int i = 0; i < s_nvs_n; i++)
        if (!strcmp(s_nvs[i].key, key)) return i;
    return -1;
}
/** Into NVS as it is: what the code writes goes through nvs_set_*, a device seeded here straight through this. */
static esp_err_t nvs_put(const char *key, const void *value, size_t len, nvs_type_t type) {
    int i = nvs_find(key);
    if (i < 0 && s_nvs_n < NVS_MAX) i = s_nvs_n++;
    if (i < 0 || len > sizeof(s_nvs[0].data) || strlen(key) >= NVS_KEY_NAME_MAX_SIZE) return ESP_FAIL;
    snprintf(s_nvs[i].key, sizeof(s_nvs[i].key), "%s", key);
    s_nvs[i].type = type;
    memcpy(s_nvs[i].data, value, len);
    s_nvs[i].len = len;
    return ESP_OK;
}
static bool s_rg_refused;  // NVS takes no `rg` (full, broken)
static esp_err_t nvs_write(const char *key, const void *value, size_t len, nvs_type_t type) {
    if (!strcmp(key, "rg") || !strncmp(key, "bound_", 6)) s_region_keys = true;
    if (s_rg_refused && !strcmp(key, "rg")) return ESP_FAIL;
    return nvs_put(key, value, len, type);
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
    return nvs_write(key, &value, 1, NVS_TYPE_U8);
}
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value) {
    (void)handle;
    size_t len = 1;
    return nvs_take(key, value, &len);
}
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value) {
    (void)handle;
    return nvs_write(key, value, strlen(value) + 1, NVS_TYPE_STR);
}
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out, size_t *length) {
    (void)handle;
    return nvs_take(key, out, length);
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length) {
    (void)handle;
    return nvs_write(key, value, length, NVS_TYPE_BLOB);
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length) {
    (void)handle;
    return nvs_take(key, out, length);
}
// The iterator, as ESP-IDF's: entries of one type in yk_acct; at the end it is released and NULL.
struct nvs_opaque_iterator_t {
    int at;
    nvs_type_t type;
};
static struct nvs_opaque_iterator_t s_iterator;
static int s_iterators;  // open now: must come back to 0
static bool iterator_seek(struct nvs_opaque_iterator_t *it) {
    for (; it->at < s_nvs_n; it->at++)
        if (it->type == NVS_TYPE_ANY || s_nvs[it->at].type == it->type) return true;
    return false;
}
esp_err_t nvs_entry_find(const char *part_name, const char *namespace_name, nvs_type_t type, nvs_iterator_t *out) {
    *out = NULL;
    if (strcmp(part_name, NVS_DEFAULT_PART_NAME) || strcmp(namespace_name, "yk_acct") || s_iterators) return ESP_ERR_NVS_NOT_FOUND;
    s_iterator = (struct nvs_opaque_iterator_t){.at = 0, .type = type};
    if (!iterator_seek(&s_iterator)) return ESP_ERR_NVS_NOT_FOUND;
    s_iterators++;
    *out = &s_iterator;
    return ESP_OK;
}
esp_err_t nvs_entry_next(nvs_iterator_t *it) {
    (*it)->at++;
    if (iterator_seek(*it)) return ESP_OK;
    nvs_release_iterator(*it);
    *it = NULL;
    return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_entry_info(const nvs_iterator_t it, nvs_entry_info_t *info) {
    snprintf(info->namespace_name, sizeof(info->namespace_name), "yk_acct");
    snprintf(info->key, sizeof(info->key), "%s", s_nvs[it->at].key);
    info->type = s_nvs[it->at].type;
    return ESP_OK;
}
void nvs_release_iterator(nvs_iterator_t it) {
    if (it) s_iterators--;
}

static int nvs_u8(const char *key) {  // a u8 as stored: -1 never written
    const int i = nvs_find(key);
    return i < 0 ? -1 : s_nvs[i].data[0];
}
static int nvs_bound(void) { return nvs_u8("bound"); }  // yk_acct/bound as stored: -1 never written
static const char *nvs_str(const char *key) {  // a string as stored, NULL when there is none
    const int i = nvs_find(key);
    return i < 0 ? NULL : (const char *)s_nvs[i].data;
}

// ---- the console: `account drop`, and a development build's `region` (with CONFIG_YK_REGIONS)

static esp_console_cmd_func_t s_account_cmd, s_region_cmd;
static struct arg_str s_args[2];
static const char *s_svals[2][1];
static struct arg_end s_end;
static int s_args_made;
esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd) {
    if (!strcmp(cmd->command, "account")) s_account_cmd = cmd->func;
    else if (!strcmp(cmd->command, "region") && !cmd->argtable) s_region_cmd = cmd->func;
    else return ESP_ERR_INVALID_ARG;
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
#if CONFIG_YK_REGIONS
    s_row = -1;  // a new device: its region not read yet
#endif
#if CONFIG_YAY_DEV && CONFIG_YK_REGIONS
    free(s_test);  // ... nor its test deployments
    s_test = NULL;
#endif
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

// ---- 两套部署: the region with the account. run.sh builds this file twice, CONFIG_YK_REGIONS off (every firmware today)
// and on, with the Kconfig's origin as CONFIG_YK_CLOUD_ORIGIN.

#define SAME(a, b) ((a) && (b) && !strcmp((a), (b)))
static const char *const GLOBAL_ORIGIN = "https://device.yaykit.io";
static const char *const CN_ORIGIN = "https://device.tikpal.com.cn";
static const char *const CLAIM_CN = "ykcn_0123456789abcdefABCDEF-_";

/** The cloud's answers queued anew, nothing asked yet. */
static void answers(void) {
    memset(s_replies, 0, sizeof(s_replies));
    s_reply_count = s_calls = 0;
}

/** Every request so far went to `origin`. */
static bool all_to(const char *origin) {
    for (int i = 0; i < s_calls && i < MAX_CALLS; i++)
        if (strcmp(s_origins[i], origin)) return false;
    return true;
}

/** NVS as it stood, to say nothing changed: every key, its type and its bytes. */
static struct {
    nvs_entry_t e[NVS_MAX];
    int n;
} s_saved;
static void nvs_save(void) {
    memcpy(s_saved.e, s_nvs, sizeof(s_nvs));
    s_saved.n = s_nvs_n;
}
static bool nvs_unchanged(void) {
    if (s_saved.n != s_nvs_n) return false;
    for (int i = 0; i < s_saved.n; i++) {
        const int j = nvs_find(s_saved.e[i].key);
        if (j < 0 || s_nvs[j].type != s_saved.e[i].type || s_nvs[j].len != s_saved.e[i].len ||
            memcmp(s_nvs[j].data, s_saved.e[i].data, s_nvs[j].len))
            return false;
    }
    return true;
}

/** A start: what RAM held is gone (the region's cache too), NVS stays. `app` is the firmware starting. */
static void restart(const char *app) {
#if CONFIG_YK_REGIONS
    s_row = -1;
#endif
#if CONFIG_YAY_DEV && CONFIG_YK_REGIONS
    free(s_test);  // the test deployments read again with `rg` (and yk_account_init() lets tokens be kept again)
    s_test = NULL;
#endif
    answers();
    CHECK(yk_account_init(&(yk_account_config_t){.app_slug = app, .app_version = "0.2.0", .board = "tikpal-r1",
                                                 .on_change = on_change}) == ESP_OK);
}

/** Two apps' kept tokens (Yay's and an extension's), as kept_token_save leaves them: what a change of side must drop. */
static void keep_tokens(void) {
    char key[11];
    yk_token_key("yay", key);
    nvs_put(key, "kept", 5, NVS_TYPE_BLOB);
    yk_token_key("language-buddy", key);
    nvs_put(key, "kept", 5, NVS_TYPE_BLOB);
}

/** A device's NVS: its key, `rg` (NULL: none), a binding under `binding` (NULL: none, as after a factory reset), the
 *  owner, two apps' tokens. Nothing started on it yet. */
static void put_device(const char *rg, const char *binding) {
    fresh();
    uint8_t sk[YK_KEY_PRIVATE_BYTES], pk[YK_KEY_PUBLIC_BYTES];
    memset(sk, 1, sizeof(sk));
    memset(pk, 4, sizeof(pk));
    nvs_put("sk", sk, sizeof(sk), NVS_TYPE_BLOB);
    nvs_put("pk", pk, sizeof(pk), NVS_TYPE_BLOB);
    if (rg) nvs_put("rg", rg, strlen(rg) + 1, NVS_TYPE_STR);
    const uint8_t one = 1;
    if (binding) nvs_put(binding, &one, 1, NVS_TYPE_U8);
    nvs_put("nick", "小鱼", strlen("小鱼") + 1, NVS_TYPE_STR);
    nvs_put("loc", "zh-Hans", 8, NVS_TYPE_STR);
    keep_tokens();
}

/** yk-info's account object as yk_account_describe() fills it, unformatted, is `want`. */
static bool describes(const char *want) {
    cJSON *a = cJSON_CreateObject();
    yk_account_describe(a);
    char *got = cJSON_PrintUnformatted(a);
    const bool same = got && !strcmp(got, want);
    if (!same) printf("  described %s\n  wanted    %s\n", got ? got : "-", want);
    cJSON_free(got);
    cJSON_Delete(a);
    return same;
}

/** account.activate with `text`: NULL once received (the activation has run to its end), else the code it answered. */
static const char *claim_with(const char *text) {
    s_calls = 0;
    s_delayed_ms = 0;
    cJSON *args = cJSON_CreateObject(), *answer = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "claim", text);
    const char *code = NULL;
    const esp_err_t err = yk_account_command("account.activate", args, answer, &code);
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(answer, "state");
    if (err == ESP_OK) CHECK(cJSON_IsString(state) && !strcmp(state->valuestring, "received"));
    // Refused (BAD_CLAIM, BUSY, OTHER_REGION alike): an error and its code, nothing in the reply. yaykit_link's yk-cmd
    // answers every such one {"ok":false,"error":"<code>"} (run_command), so OTHER_REGION goes out as BAD_CLAIM does.
    else CHECK(code && *code && cJSON_GetArraySize(answer) == 0);
    cJSON_Delete(args);
    cJSON_Delete(answer);
    return err == ESP_OK ? NULL : code;
}

/** The contract's claims (fixtures/deployments.json `claim`, from deployments-cases.ts): on, each is redeemed at its
 *  side or refused, BAD_CLAIM, nothing asked; off, global's only, as ever, and a mainland claim never leaves the device. */
static void test_claim_cases(const char *fixture) {
    FILE *f = fixture ? fopen(fixture, "rb") : NULL;
    if (!f) {
        printf("yaykit_cloud account: the contract's claim cases skipped (no fixtures/deployments.json)\n");
        return;
    }
    static char text[256 * 1024];
    const size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = 0;
    cJSON *doc = cJSON_Parse(text);
    const cJSON *cases = cJSON_GetObjectItemCaseSensitive(doc, "claim");
    CHECK(cJSON_GetArraySize(cases) >= 10);
    int global = 0, cn = 0, refused = 0;
    const cJSON *c;
    cJSON_ArrayForEach(c, cases) {
        const cJSON *claim = cJSON_GetObjectItemCaseSensitive(c, "claim"), *expect = cJSON_GetObjectItemCaseSensitive(c, "expect");
        const cJSON *why = cJSON_GetObjectItemCaseSensitive(c, "why");
        CHECK(cJSON_IsString(claim) && (cJSON_IsString(expect) || cJSON_IsNull(expect)));
        if (!cJSON_IsString(claim)) continue;
        const char *side = cJSON_IsString(expect) ? expect->valuestring : NULL;
#if !CONFIG_YK_REGIONS
        if (side && strcmp(side, "global")) side = NULL;  // off: global's claims only
#endif
        fresh();
        reply(200, ACTIVATED);
        const int before = failures;
        const char *code = claim_with(claim->valuestring);
        CHECK(yk_claim_valid(claim->valuestring) == (side != NULL));
        if (!side) {
            CHECK(SAME(code, "BAD_CLAIM") && s_calls == 0 && yk_account_state() == YK_ACCOUNT_UNBOUND);
            refused++;
        } else {
            CHECK(!code && called(ACTIVATE_ONLY, 1) && yk_account_state() == YK_ACCOUNT_BOUND);
            CHECK(all_to(!strcmp(side, "cn") ? CN_ORIGIN : GLOBAL_ORIGIN));
            global += !strcmp(side, "global");
            cn += !strcmp(side, "cn");
        }
        if (failures != before) printf("  claim case: %s\n", cJSON_IsString(why) ? why->valuestring : "?");
    }
    cJSON_Delete(doc);
#if CONFIG_YK_REGIONS
    CHECK(global >= 2 && cn >= 2 && refused >= 5);
#else
    CHECK(global >= 2 && cn == 0 && refused >= 7);
#endif
}

#if CONFIG_YK_REGIONS
static const char *const OWNED = "{\"error\":\"DEVICE_OWNED_BY_OTHER\"}";
static const char *const CLAIM_REFUSED = "{\"error\":\"CLAIM_INVALID\"}";
static const char *const RELEASED = "{\"released\":true}";
static const char *const ASK_RELEASE[] = {"/device/v1/token/challenge", "/device/v1/release"};
static const char *const ASK_RELEASE_ACTIVATE[] = {"/device/v1/token/challenge", "/device/v1/release", "/device/v1/activate"};
static const char *const ASK_TOKEN[] = {"/device/v1/token/challenge", "/device/v1/token"};

/** The kept tokens in NVS (yk_token_key's "tk" and 8 hex digits), every app's. */
static int nvs_tokens(void) {
    int n = 0;
    for (int i = 0; i < s_nvs_n; i++) n += strlen(s_nvs[i].key) == 10 && !strncmp(s_nvs[i].key, "tk", 2);
    return n;
}

/** put_device(), Yay started on it, nothing in RAM from the case before; the cloud's answers anew. */
static void seed(const char *rg, const char *binding) {
    put_device(rg, binding);
    restart("yay");
    yk_account_token_expired();  // RAM's token from before; it drops Yay's kept one too, put back
    keep_tokens();
    answers();
}

/** 附录 C 2.6, each row on each side: where the region is written, what it keeps, and which side is asked. */
static void test_regions(void) {
    // The table as this test reads it: global's at the Kconfig's origin, cn's at its own
    const int cn = yk_region_find("cn");
    yk_region_row_t r;
    CHECK(yk_region_row(YK_REGION_GLOBAL, &r) && SAME(r.id, "global") && SAME(r.device_origin, GLOBAL_ORIGIN));
    CHECK(SAME(CONFIG_YK_CLOUD_ORIGIN, GLOBAL_ORIGIN));
    CHECK(yk_region_row(cn, &r) && SAME(r.device_origin, CN_ORIGIN) && SAME(r.claim_prefix, "ykcn_"));
    // Every row's binding key fits NVS (15 characters) and is its own: `bound` for row 0, `bound_<id>` the others
    char keys[YK_REGION_COUNT][32];
    for (int i = 0; i < yk_region_count(); i++) {
        CHECK(yk_region_row(i, &r));
        snprintf(keys[i], sizeof(keys[i]), i ? "bound_%s" : "bound", r.id);
        CHECK(strlen(keys[i]) < NVS_KEY_NAME_MAX_SIZE);
        for (int j = 0; j < i; j++) CHECK(strcmp(keys[i], keys[j]));
    }

    static const struct {
        const char *id, *claim, *origin, *key;  // this side
        const char *other, *other_claim, *other_origin, *other_key;
    } SIDES[] = {
        {"global", "ykc_0123456789abcdefABCDEF-_", "https://device.yaykit.io", "bound", "cn", "ykcn_0123456789abcdefABCDEF-_",
         "https://device.tikpal.com.cn", "bound_cn"},
        {"cn", "ykcn_0123456789abcdefABCDEF-_", "https://device.tikpal.com.cn", "bound_cn", "global",
         "ykc_0123456789abcdefABCDEF-_", "https://device.yaykit.io", "bound"},
    };
    for (size_t i = 0; i < sizeof(SIDES) / sizeof(SIDES[0]); i++) {
        const int before = failures;
        const char *id = SIDES[i].id, *mine = SIDES[i].claim, *origin = SIDES[i].origin, *key = SIDES[i].key;
        const char *other = SIDES[i].other, *theirs = SIDES[i].other_claim, *other_origin = SIDES[i].other_origin,
                   *other_key = SIDES[i].other_key;
        char out[64];
        const char *code = NULL;

        // 首次添加: never activated. The claim's side stored before the request, the request there, bound under its key
        // and no other; after a start, still that side.
        fresh();
        reply(200, ACTIVATED);
        CHECK(!claim_with(mine) && called(ACTIVATE_ONLY, 1) && all_to(origin) && SAME(s_rg_then[0], id));
        CHECK(yk_account_state() == YK_ACCOUNT_BOUND && SAME(nvs_str("rg"), id) && nvs_u8(key) == 1 && nvs_u8(other_key) < 0);
        CHECK(SAME(yk_region_name(), id) && SAME(yk_cloud_origin(), origin) && token_is("yk1.activated", 0));
        // yk-info's account says the side (B2: account.region, after what it said before)
        char said[160];
        snprintf(said, sizeof(said), "{\"state\":\"bound\",\"owner\":\"小鱼\",\"locale\":\"en\",\"region\":\"%s\"}", id);
        CHECK(describes(said));
        restart("yay");
        CHECK(yk_account_state() == YK_ACCOUNT_BOUND && SAME(yk_region_name(), id) && token_is("yk1.activated", 0));
        yk_account_token_expired();
        reply(200, CHALLENGE);
        reply(200, TOKEN);
        CHECK(token_is("yk1.by-key", 2) && all_to(origin));
        // NVS takes no `rg` (full, broken): nothing asked of the claim's side, NETWORK, the region as it was
        seed(id, NULL);
        s_rg_refused = true;
        CHECK(!claim_with(theirs) && failed_with("NETWORK") && s_calls == 0 && SAME(nvs_str("rg"), id) && nvs_tokens() == 2);
        CHECK(SAME(yk_region_name(), id));
        s_rg_refused = false;

        // 同一边换主人: bound, someone else of this side. The cloud says so; the region and the binding as they were.
        seed(id, key);
        reply(409, OWNED);
        CHECK(!claim_with(mine) && called(ACTIVATE_ONLY, 1) && all_to(origin));
        CHECK(yk_account_state() == YK_ACCOUNT_FAILED && SAME(yk_account_error(), "DEVICE_OWNED_BY_OTHER"));
        CHECK(SAME(nvs_str("rg"), id) && nvs_u8(key) == 1 && nvs_tokens() == 2);
        // 另一边的人来加, FAILED as it is now but bound all the same: OTHER_REGION, at once, nothing asked or changed
        nvs_save();
        unsigned changes = s_changes;
        CHECK(SAME(claim_with(theirs), "OTHER_REGION") && s_calls == 0 && nvs_unchanged() && s_changes == changes);
        // ... and bound as it started: the same, still bound, Bluetooth and the link keys as they were
        seed(id, key);
        nvs_save();
        int keys_before = s_keys_forgotten, owner_before = s_owner_changed;
        changes = s_changes;
        s_ble = true;
        CHECK(SAME(claim_with(theirs), "OTHER_REGION") && s_calls == 0 && nvs_unchanged() && s_changes == changes);
        CHECK(yk_account_state() == YK_ACCOUNT_BOUND && SAME(yk_region_name(), id) && s_ble && !s_ble_stops);
        CHECK(s_keys_forgotten == keys_before && s_owner_changed == owner_before);
        s_ble = false;

        // 同一账号重新添加: this side again, answered. Bound, the region the same, the other app's token kept.
        seed(id, key);
        reply(200, ACTIVATED);
        CHECK(!claim_with(mine) && called(ACTIVATE_ONLY, 1) && all_to(origin));
        CHECK(yk_account_state() == YK_ACCOUNT_BOUND && SAME(nvs_str("rg"), id) && nvs_u8(key) == 1 && nvs_tokens() == 2);

        // 令牌过期: fetched again, from this side
        seed(id, key);
        reply(200, CHALLENGE);
        reply(200, TOKEN);
        CHECK(token_is("yk1.by-key", 2) && all_to(origin));

        // App 里删设备 (服务端说不认识): this side's binding forgotten, never the region
        seed(id, key);
        reply(200, CHALLENGE);
        reply(401, NOT_ACTIVATED);
        CHECK(yk_account_token(out, sizeof(out), &code) == ESP_FAIL && SAME(code, "DEVICE_NOT_ACTIVATED"));
        CHECK(s_calls == 2 && all_to(origin));
        CHECK(yk_account_state() == YK_ACCOUNT_UNBOUND && nvs_u8(key) == 0 && SAME(nvs_str("rg"), id));
        // ... unbound, a start's recovery asks this side
        answers();
        reply(200, CHALLENGE);
        reply(401, NOT_ACTIVATED);
        CHECK(!yk_account_recover() && called(ASK_TOKEN, 2) && all_to(origin));
        // ... then the other side's claim: that side stored first, every app's kept token gone, the request there, bound
        // under that side's key; this side's left as it was (0)
        answers();
        reply(200, ACTIVATED);
        CHECK(!claim_with(theirs) && called(ACTIVATE_ONLY, 1) && all_to(other_origin) && SAME(s_rg_then[0], other));
        CHECK(yk_account_state() == YK_ACCOUNT_BOUND && SAME(nvs_str("rg"), other) && nvs_u8(other_key) == 1 &&
              nvs_u8(key) == 0);
        CHECK(nvs_tokens() == 1 && token_is("yk1.activated", 0));  // only the new activation's own, from there
        CHECK(s_iterators == 0);

        // account.forget (串口、蓝牙): this side's binding, not the region; nothing asked
        seed(id, key);
        {
            cJSON *answer = cJSON_CreateObject();
            CHECK(yk_account_command("account.forget", NULL, answer, &code) == ESP_OK);
            cJSON_Delete(answer);
        }
        CHECK(yk_account_state() == YK_ACCOUNT_UNBOUND && s_calls == 0 && nvs_u8(key) == 0 && SAME(nvs_str("rg"), id));

        // 恢复出厂 · 有网: the release to this side
        seed(id, key);
        yk_account_release_mark();
        reply(200, CHALLENGE);
        reply(200, RELEASED);
        CHECK(yk_account_release(NULL) == ESP_OK && called(ASK_RELEASE, 2) && all_to(origin));
        CHECK(SAME(nvs_str("rg"), id));

        // 恢复出厂 · 没网 (#1352): the wipe left the key, `rel` and `rg` (yay_reset.c), no binding. The next claim is the
        // other side's: the release first, to this side. Unanswered: NETWORK and nothing else, the region as it was.
        seed(id, NULL);
        yk_account_release_mark();
        restart("yay");
        CHECK(yk_account_state() == YK_ACCOUNT_UNBOUND && yk_account_release_pending());
        reply(0, NULL);
        CHECK(!claim_with(theirs));
        CHECK(failed_with("NETWORK") && s_calls == 1 && all_to(origin) && SAME(nvs_str("rg"), id) &&
              yk_account_release_pending() && nvs_tokens() == 2);
        // ... answered: released at this side, then the other side stored and the claim redeemed there
        answers();
        reply(200, CHALLENGE);
        reply(200, RELEASED);
        reply(200, ACTIVATED);
        CHECK(!claim_with(theirs) && called(ASK_RELEASE_ACTIVATE, 3));
        CHECK(SAME(s_origins[0], origin) && SAME(s_origins[1], origin) && SAME(s_origins[2], other_origin));
        CHECK(SAME(s_rg_then[1], id) && SAME(s_rg_then[2], other));
        CHECK(yk_account_state() == YK_ACCOUNT_BOUND && !yk_account_release_pending() && SAME(nvs_str("rg"), other) &&
              nvs_u8(other_key) == 1);

        // 激活失败: the claim's side stays stored (the device was not bound either way); the next claim stores its own
        seed(id, NULL);
        reply(401, CLAIM_REFUSED);
        CHECK(!claim_with(theirs) && failed_with("CLAIM_INVALID") && all_to(other_origin));
        CHECK(SAME(nvs_str("rg"), other) && nvs_u8(other_key) != 1 && nvs_tokens() == 0);
        answers();
        reply(200, ACTIVATED);
        CHECK(!claim_with(mine) && all_to(origin) && yk_account_state() == YK_ACCOUNT_BOUND && SAME(nvs_str("rg"), id) &&
              nvs_u8(key) == 1);

        // A power cut while the request was out (登录与开箱 8.1): the region was stored before it, so the next start's
        // recovery asks the side the claim went to, and binds there
        seed(id, NULL);
        reply(0, NULL);
        reply(200, CHALLENGE);
        reply(401, NOT_ACTIVATED);
        CHECK(!claim_with(theirs) && failed_with("NETWORK") && SAME(s_rg_then[0], other) && all_to(other_origin));
        restart("yay");
        CHECK(yk_account_state() == YK_ACCOUNT_UNBOUND && SAME(yk_region_name(), other));
        reply(200, CHALLENGE);
        reply(200, TOKEN);
        CHECK(yk_account_recover() && called(ASK_TOKEN, 2) && all_to(other_origin));
        CHECK(yk_account_state() == YK_ACCOUNT_BOUND && nvs_u8(other_key) == 1 && yk_account_binding_stored());

        // 整片 USB 安装: nothing left, global; the claim decides
        fresh();
        CHECK(SAME(yk_region_name(), "global") && !yk_account_binding_stored());
        reply(200, ACTIVATED);
        CHECK(!claim_with(mine) && all_to(origin) && SAME(nvs_str("rg"), id));

        if (failures != before) printf("  side: %s\n", id);
    }

    // A device from before the regions: no `rg`, bound in global under `bound`. Global and bound as it was; reading
    // writes nothing, and the mainland's claim is the other side's.
    seed(NULL, "bound");
    CHECK(yk_account_state() == YK_ACCOUNT_BOUND && SAME(yk_region_name(), "global"));
    reply(200, CHALLENGE);
    reply(200, TOKEN);
    CHECK(token_is("yk1.by-key", 2) && all_to(GLOBAL_ORIGIN) && !nvs_str("rg"));
    CHECK(SAME(claim_with(CLAIM_CN), "OTHER_REGION") && s_calls == 0);
    // A name this firmware does not know (a newer one's), its binding under its own key: global here, and not bound. A
    // global claim stores global and drops the tokens: they were that side's.
    seed("mars", "bound_mars");
    CHECK(yk_account_state() == YK_ACCOUNT_UNBOUND && SAME(yk_region_name(), "global"));
    reply(200, ACTIVATED);
    CHECK(!claim_with(CLAIM) && all_to(GLOBAL_ORIGIN) && SAME(nvs_str("rg"), "global") && nvs_tokens() == 1 && nvs_bound() == 1);
    // Many apps' tokens: every one goes, a batch at a time, the device key and the rest stay
    seed("global", NULL);
    for (int i = 0; i < 12; i++) {
        char slug[16], key[11];
        snprintf(slug, sizeof(slug), "app-%d", i);
        yk_token_key(slug, key);
        nvs_put(key, "kept", 5, NVS_TYPE_BLOB);
    }
    CHECK(nvs_tokens() == 14);
    reply(200, ACTIVATED);
    CHECK(!claim_with(CLAIM_CN) && nvs_tokens() == 1 && nvs_find("sk") >= 0 && nvs_find("pk") >= 0 && s_iterators == 0);
}
#else
/** Off (every firmware today): the activation's request byte for byte as before the regions, and nothing of them in
 *  NVS. */
static void test_off_today(void) {
    // Origin and body as before the regions, main's yaykit_account.c (1961ae05d) with these stubs: yk_http.c puts
    // yk_cloud_origin(), here the Kconfig's, before the path. The key is the stub's 65 bytes of 4, the signature 8 of '0'.
    static const char *const TODAY =
        "{\"deviceId\":\"ykd_test\","
        "\"publicKey\":\"BAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQ\","
        "\"claim\":\"ykc_0123456789abcdefABCDEF-_\",\"signature\":\"MDAwMDAwMDA\",\"app\":\"yay\",\"appVersion\":\"0.1.10\","
        "\"board\":\"tikpal-r1\"}";
    fresh();
    reply(200, ACTIVATED);
    activate();
    CHECK(called(ACTIVATE_ONLY, 1) && SAME(s_origins[0], GLOBAL_ORIGIN) && SAME(s_origins[0], CONFIG_YK_CLOUD_ORIGIN));
    CHECK(SAME(s_bodies[0], TODAY));
    if (!SAME(s_bodies[0], TODAY)) printf("  sent %s\n  today %s\n", s_bodies[0], TODAY);
    CHECK(nvs_bound() == 1 && !nvs_str("rg") && s_rg_then[0][0] == 0);
    // yk-info's account as before: no region (the App takes a firmware that says one for one that can go to the mainland)
    CHECK(describes("{\"state\":\"bound\",\"owner\":\"小鱼\",\"locale\":\"en\"}"));
    // The mainland's claim: BAD_CLAIM on the device, nothing asked of anyone
    fresh();
    CHECK(describes("{\"state\":\"unbound\"}"));
    CHECK(SAME(claim_with(CLAIM_CN), "BAD_CLAIM") && s_calls == 0 && yk_account_state() == YK_ACCOUNT_UNBOUND);
    // The region is always global, nothing read
    CHECK(SAME(yk_region_name(), "global") && SAME(yk_cloud_origin(), CONFIG_YK_CLOUD_ORIGIN));
}

/** 两套部署 4.3, the plan's main inference, tried: an extension (or app, or Yay rolled back) built before the regions,
 *  CONFIG_YK_REGIONS off, on a device bound in cn: `rg` cn, its binding under `bound_cn`, no `bound`. It must say not
 *  bound, ask nothing of anyone (its only address is global's), never forget() (no DEVICE_NOT_ACTIVATED comes), and
 *  clear no key. */
static void test_old_extension_on_cn(void) {
    put_device("cn", "bound_cn");
    CHECK(nvs_bound() < 0 && nvs_u8("bound_cn") == 1);
    nvs_save();
    const int forgotten = s_forgotten, keys = s_keys_forgotten, owners = s_owner_changed;
    restart("language-buddy");
    // Not bound, so no token from the cache and none asked for
    CHECK(yk_account_state() == YK_ACCOUNT_UNBOUND && !strcmp(yk_account_state_name(), "unbound"));
    char out[64];
    const char *code = NULL;
    CHECK(yk_account_token(out, sizeof(out), &code) == ESP_ERR_INVALID_STATE && SAME(code, "DEVICE_NOT_ACTIVATED"));
    CHECK(!yk_account_token_cached());
    // What yaykit_cloud's calls do first (yk_account_token), and the link key's, the same: refused here
    uint8_t key[32];
    uint32_t kid = 0;
    CHECK(yk_api_link_key_fetch(&kid, key) == ESP_ERR_INVALID_RESPONSE);
    // yk-info's account: unbound, no owner, no language
    cJSON *account = cJSON_CreateObject();
    yk_account_describe(account);
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(account, "state");
    CHECK(cJSON_IsString(state) && !strcmp(state->valuestring, "unbound") && !cJSON_GetObjectItemCaseSensitive(account, "owner") &&
          !cJSON_GetObjectItemCaseSensitive(account, "locale"));
    cJSON_Delete(account);
    // A session's hello.ok or /me naming an owner: not bound, nothing kept
    cJSON *owner = cJSON_Parse("{\"nickname\":\"别人\",\"locale\":\"en\"}");
    yk_account_owner_seen(owner);
    cJSON_Delete(owner);
    yk_account_locale_seen("en");
    CHECK(yk_account_state() == YK_ACCOUNT_UNBOUND);
    // Nothing asked, nothing forgotten, not a key cleared or written
    CHECK(s_calls == 0);
    CHECK(s_forgotten == forgotten && s_keys_forgotten == keys && s_owner_changed == owners);
    CHECK(nvs_unchanged());
}
#endif

#if CONFIG_YAY_DEV && CONFIG_YK_REGIONS
// ---- a development build's `region` (yk_region_test.h): step 3 of 两套部署 on it, the session's commands aside

static char s_said[4096];  // what the last region() printed

/** `region` with these words; what it printed is in s_said. */
static int region(const char *a1, const char *a2, const char *a3) {
    char *argv[] = {"region", (char *)a1, (char *)a2, (char *)a3, NULL};
    const int argc = 1 + (a1 != NULL) + (a2 != NULL) + (a3 != NULL);
    fflush(stdout);
    FILE *said = tmpfile();
    const int out = dup(fileno(stdout));
    dup2(fileno(said), fileno(stdout));
    const int rc = s_region_cmd(argc, argv);
    fflush(stdout);
    dup2(out, fileno(stdout));
    close(out);
    rewind(said);
    const size_t n = fread(s_said, 1, sizeof(s_said) - 1, said);
    s_said[n] = 0;
    fclose(said);
    return rc;
}

static bool said(const char *line) { return strstr(s_said, line) != NULL; }

/** Every request so far, and only that many, went to `origin`. */
static bool asked_at(const char *origin, int requests) { return s_calls == requests && all_to(origin); }

#define CN_TEST "https://cn-test.example"
#define GLOBAL_TEST "https://global-test.example"

static void test_region_command(void) {
    // Registered beside `account`; the device as it comes: global, no test deployment
    put_device(NULL, NULL);
    s_region_cmd = NULL;
    CHECK(yk_account_register_console() == ESP_OK && s_account_cmd && s_region_cmd);
    restart("yay");
    CHECK(region(NULL, NULL, NULL) == 0 && region("status", NULL, NULL) == 0);
    CHECK(said("YK_REGION region=global origin=https://device.yaykit.io\n"));
    CHECK(said("YK_REGION row=global origin=https://device.yaykit.io session=wss://device.yaykit.io/device/v1/session test=-\n"));
    CHECK(said("YK_REGION row=cn origin=https://device.tikpal.com.cn session=wss://device.tikpal.com.cn/device/v1/session "
               "test=-\n"));

    // Step 3: cn's row on the test deployment. Not this device's row (global): stored for the next start, nothing in
    // use changes, the kept tokens stay (they are global's, and global is still where it goes)
    CHECK(region("set", "cn", CN_TEST) == 0);
    CHECK(said("YK_REGION set=cn origin=" CN_TEST " (after a restart)\n"));
    CHECK(said("row=cn origin=https://device.tikpal.com.cn session=wss://device.tikpal.com.cn/device/v1/session test=- "
               "next=" CN_TEST " (after a restart)\n"));
    CHECK(SAME(nvs_str("rgdev_cn"), CN_TEST) && nvs_tokens() == 2 && SAME(yk_cloud_origin(), GLOBAL_ORIGIN));
    // Restarted: cn's addresses are the test deployment's, global's as they were
    restart("yay");
    CHECK(region(NULL, NULL, NULL) == 0);
    CHECK(said("row=cn origin=" CN_TEST " session=wss://cn-test.example/device/v1/session test=on\n"));
    CHECK(said("row=global origin=https://device.yaykit.io session=wss://device.yaykit.io/device/v1/session test=-\n"));
    // A ykcn_ claim: activated at the test deployment, bound there (bound_cn), tokens from it
    answers();
    reply(200, ACTIVATED);
    CHECK(!claim_with(CLAIM_CN) && asked_at(CN_TEST, 1) && SAME(yk_region_name(), "cn"));
    CHECK(yk_account_state() == YK_ACCOUNT_BOUND && nvs_u8("bound_cn") == 1 && SAME(yk_cloud_origin(), CN_TEST));
    yk_account_token_expired();
    answers();
    reply(200, CHALLENGE);
    reply(200, TOKEN);
    CHECK(token_is("yk1.by-key", 2) && asked_at(CN_TEST, 2));
    restart("language-buddy");  // an extension reads the same: bound, its HTTPS at the test deployment
    CHECK(yk_account_state() == YK_ACCOUNT_BOUND && SAME(yk_cloud_origin(), CN_TEST));
    // Yay's factory reset (yay_reset.c, a development build with the regions): the binding goes, the key, `rg` and the
    // test deployment stay. A ykc_ claim then: back to global, global's addresses; cn's row still the test deployment
    const char *gone[] = {"bound_cn", "nick", "loc"};
    for (size_t i = 0; i < sizeof(gone) / sizeof(gone[0]); i++) nvs_erase_key(1, gone[i]);
    while (nvs_tokens()) {
        for (int i = 0; i < s_nvs_n; i++)
            if (strlen(s_nvs[i].key) == 10 && !strncmp(s_nvs[i].key, "tk", 2)) nvs_erase_key(1, s_nvs[i].key);
    }
    restart("yay");
    CHECK(yk_account_state() == YK_ACCOUNT_UNBOUND && SAME(yk_region_name(), "cn") && SAME(yk_cloud_origin(), CN_TEST));
    answers();
    reply(200, ACTIVATED);
    CHECK(!claim_with(CLAIM) && asked_at(GLOBAL_ORIGIN, 1) && SAME(yk_region_name(), "global"));
    CHECK(yk_account_state() == YK_ACCOUNT_BOUND && nvs_bound() == 1 && SAME(yk_region_test_origin(yk_region_find("cn")), CN_TEST));

    // This device's own row: the kept tokens are dropped, none is kept again before the restart, and bound here it says
    // what a deployment that does not know it does
    keep_tokens();
    CHECK(nvs_tokens() == 2);
    CHECK(region("set", "global", GLOBAL_TEST) == 0);
    CHECK(said("YK_REGION set=global origin=" GLOBAL_TEST " (after a restart, kept tokens dropped)\n"));
    CHECK(said("YK_REGION note=bound in global"));
    CHECK(nvs_tokens() == 0 && SAME(nvs_str("rgdev_global"), GLOBAL_TEST) && SAME(yk_cloud_origin(), GLOBAL_ORIGIN));
    yk_account_token_expired();
    answers();
    reply(200, CHALLENGE);
    reply(200, TOKEN);
    CHECK(token_is("yk1.by-key", 2) && asked_at(GLOBAL_ORIGIN, 2));  // still this start's server ...
    CHECK(nvs_tokens() == 0);                                        // ... and its token not kept for the next
    // The same again: as it was, nothing dropped
    keep_tokens();
    CHECK(region("set", "global", GLOBAL_TEST) == 0 && said("(after a restart, as it was)") && nvs_tokens() == 2);
    // Restarted: global's at the test deployment; a token fetched there is kept again
    restart("yay");
    yk_account_token_expired();
    answers();
    reply(200, CHALLENGE);
    reply(200, TOKEN);
    CHECK(token_is("yk1.by-key", 2) && asked_at(GLOBAL_TEST, 2) && nvs_tokens() >= 1);
    CHECK(region(NULL, NULL, NULL) == 0 && said("YK_REGION region=global origin=" GLOBAL_TEST "\n"));
    CHECK(said("row=global origin=" GLOBAL_TEST " session=wss://global-test.example/device/v1/session test=on\n"));

    // Cleared: the table's from the next start (said as next=-), cn's left
    CHECK(region("clear", "global", NULL) == 0 && said("YK_REGION clear=global (after a restart, kept tokens dropped)\n"));
    CHECK(said("row=global origin=" GLOBAL_TEST " session=wss://global-test.example/device/v1/session test=on next=- "
               "(after a restart)\n"));
    CHECK(!nvs_str("rgdev_global") && SAME(nvs_str("rgdev_cn"), CN_TEST));
    CHECK(region("clear", "global", NULL) == 0 && said("(after a restart, as it was)"));
    restart("yay");
    CHECK(SAME(yk_cloud_origin(), GLOBAL_ORIGIN));

    // Refused, nothing written: not https, too long, a region not in the table, words missing or too many
    nvs_save();
    char too_long[80];
    snprintf(too_long, sizeof(too_long), "https://%0*d.example", YK_REGION_TEST_ORIGIN_MAX - 15, 0);
    CHECK(region("set", "cn", "http://cn-test.example") == 1 && said("YK_REGION error=not https://\n"));
    CHECK(region("set", "cn", too_long) == 1 && said("YK_REGION error=too long\n"));
    CHECK(region("set", "cn", "https://cn-test.example/device") == 1 && said("YK_REGION error=https://<host>[:port] only"));
    CHECK(region("set", "mars", CN_TEST) == 1 && said("YK_REGION error=not a region of this firmware's table\n"));
    CHECK(region("clear", "mars", NULL) == 1 && said("YK_REGION error=not a region"));
    CHECK(region("set", "cn", NULL) == 1 && said("usage: region status | set <global|cn>"));
    CHECK(region("clear", NULL, NULL) == 1 && region("status", "cn", NULL) == 1 && region("forget", NULL, NULL) == 1);
    CHECK(region("set", "CN", CN_TEST) == 1 && said("not a region"));
    CHECK(nvs_unchanged());
}
#else
static void test_region_command(void) {
    // Regions off (run.sh builds this file as a development build): `account` alone
    fresh();
    s_region_cmd = NULL;
    CHECK(yk_account_register_console() == ESP_OK && s_account_cmd && !s_region_cmd);
}
#endif

int main(int argc, char **argv) {
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

    // 两套部署
    test_claim_cases(argc > 1 ? argv[1] : NULL);
    test_region_command();
#if CONFIG_YK_REGIONS
    test_regions();
    const char *setting = "on";
#else
    test_off_today();
    test_old_extension_on_cn();
    // Off, nothing above (every case of today's, the contract's claims) wrote `rg` or another region's binding key
    CHECK(!s_region_keys);
    const char *setting = "off";
#endif
    CHECK(s_iterators == 0);

    CHECK(s_lock_overlaps == 0 && s_lock_depth == 0);

    printf("yaykit_cloud account host tests (CONFIG_YK_REGIONS %s): %d checks, %d failures\n", setting, checks, failures);
    return failures ? 1 : 0;
}
