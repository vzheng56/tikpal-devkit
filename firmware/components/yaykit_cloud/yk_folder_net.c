// The shared folders' grants at the device service (device-api.ts folderGrantsAnswer, folderGrantPut; Yay 0.1.19
// 开发文档 2.1, 3.1), handed to yaykit_tf as the cloud starts (yk_folder_net_offer, from yk_account_init; the
// hand-over is yaykit_tf's yk_folder_net_set, linked weakly: a firmware without the card has none). With the firmware's
// own token: an extension that uses the cloud asks for its own at its first ask of a run and tells its answers; Yay
// lists every app's at connecting and tells what waits, and fetches an app's before it opens it (固件总策划 11-13).
// An extension that does not use the cloud carries none of this: Yay keeps its record in line.
//
// get and put wait `wait_ms` at most (3.1: 2 s, and then the record on this device stands): the call runs on a task of its own
// and the asker stops waiting when the time is up; the task finishes by itself (its HTTP timeouts), its late answer
// unused: the next ask asks again, and a PUT that did land is met by its rev (a 409 that takes it, or a GET that has it).
// One call at a time; none at all without Wi-Fi up or with Bluetooth on (an extension uses the network it has; the
// phone's turn is not taken from it). The kinds and the JSON are yk_api.c's (pure, test/test_api.c).
//
// The task's stack is internal RAM, NET_STACK bytes, only while a call is under way: TLS, as Yay's store list and
// update check have it (yay_run.c STORE_STACK, yay_update_run.c CHECK_STACK).
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "yaykit_account.h"
#include "yaykit_link.h"
#include "yaykit_wifi.h"
#include "yk_api.h"
#include "yk_folder_logic.h"  // yaykit_tf's: yk_grant_t and yk_folder_net_t
#include "yk_folder_net.h"
#include "yk_http.h"

#define NET_STACK 7168
#define NET_PRIORITY 3
#define NET_TIMEOUT_MS 10000  // each HTTP step: the task's own, past the asker's wait
#define SLUG_MAX 48           // device-api.ts: ^[a-z][a-z0-9-]{1,40}$

typedef struct {
    char slug[SLUG_MAX];
    bool put;
    uint8_t state, folders;  // the PUT's
    bool known;
    uint32_t rev;
    int result;  // what the asker returns
    yk_api_grant_t got;
    SemaphoreHandle_t done;
    int refs;  // the asker's and the task's: the last to let go frees it
} job_t;

// A critical section, not C11 atomics: a one-byte exchange and a fetch-and-subtract are library calls ESP-IDF keeps in
// IRAM (about 180 bytes of internal RAM in every firmware with the cloud); portMUX is there already.
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_busy;

static bool busy_take(void) {
    portENTER_CRITICAL(&s_mux);
    const bool was = s_busy;
    s_busy = true;
    portEXIT_CRITICAL(&s_mux);
    return was;
}
static void busy_clear(void) {
    portENTER_CRITICAL(&s_mux);
    s_busy = false;
    portEXIT_CRITICAL(&s_mux);
}

static void let_go(job_t *j) {
    portENTER_CRITICAL(&s_mux);
    const int left = --j->refs;
    portEXIT_CRITICAL(&s_mux);
    if (left) return;
    vSemaphoreDelete(j->done);
    heap_caps_free(j);
}

/** One request with the app's token, once more with a new one when it expired; the HTTP status, 0 for none. */
static int request(esp_http_client_method_t method, const char *path, const char *json, yk_http_response_t *res) {
    for (int attempt = 0; attempt < 2; attempt++) {
        char token[512];
        const char *code = NULL;
        if (yk_account_token(token, sizeof(token), &code) != ESP_OK) return 0;
        if (yk_http_prepare(&code) != ESP_OK) return 0;
        if (yk_http_send(method, path, token, json ? "application/json" : NULL, (const uint8_t *)json,
                         json ? strlen(json) : 0, NET_TIMEOUT_MS, res) != ESP_OK)
            return 0;
        if (res->status != 401 || attempt) return res->status;
        code = yk_api_error_code(res->body);
        if (!strcmp(code, "DEVICE_NOT_ACTIVATED")) yk_account_unbound_by_server();
        if (strcmp(code, "TOKEN_EXPIRED")) return res->status;
        yk_account_token_expired();
        yk_http_free(res);
    }
    return 0;
}

#if CONFIG_YAY_DEV
// `yay folders slow <ms>` (Yay's development build): each call waits this long first, so that the bench sees the asker's
// wait run out (开发文档 3.1: 2 s, then the record stands) without cutting the network.
static uint32_t s_dev_slow_ms;
void yk_folder_net_dev_slow(uint32_t ms) { s_dev_slow_ms = ms; }
#endif

static void net_task(void *arg) {
    job_t *j = arg;
#if CONFIG_YAY_DEV
    if (s_dev_slow_ms) vTaskDelay(pdMS_TO_TICKS(s_dev_slow_ms));
#endif
    char path[96];
    yk_http_response_t res = {0};
    yk_api_grant_t got = {0};
    int result = 0, status = 0;
    if (!j->put) {
        snprintf(path, sizeof(path), "/device/v1/folders?app=%s", j->slug);
        status = request(HTTP_METHOD_GET, path, NULL, &res);
        result = status == 200 && yk_api_folder_grants(res.body, j->slug, &got) >= 0 ? 1 : 0;
    } else {
        snprintf(path, sizeof(path), "/device/v1/folders/%s", j->slug);
        char *body = yk_api_folder_put_body(j->state, j->folders, j->known, j->rev);
        status = body ? request(HTTP_METHOD_PUT, path, body, &res) : 0;
        cJSON_free(body);
        result = yk_api_folder_put_answer(status, res.body, j->slug, &got);
    }
    yk_http_free(&res);
    printf("YK_FOLDER net %s status=%d result=%d\n", j->put ? "put" : "get", status, result);
    j->got = got;
    j->result = result;
    busy_clear();
    xSemaphoreGive(j->done);
    let_go(j);
    vTaskDelete(NULL);
}

/** Runs `proto` on a task and waits `wait_ms` for it: its result, or 0 when there is none in time (or none at all). */
static int run(const job_t *proto, uint32_t wait_ms, yk_api_grant_t *got) {
    memset(got, 0, sizeof(*got));
    if (!yk_wifi_wait_connected(0) || yk_link_ble_active()) return 0;
    if (busy_take()) return 0;  // the last one is still out
    job_t *j = heap_caps_malloc(sizeof(*j), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    SemaphoreHandle_t done = j ? xSemaphoreCreateBinary() : NULL;
    if (done) {
        *j = *proto;
        j->done = done;
        j->refs = 2;
    }
    if (!done || xTaskCreatePinnedToCore(net_task, "yk_folder_net", NET_STACK, j, NET_PRIORITY, NULL, tskNO_AFFINITY) != pdPASS) {
        if (done) vSemaphoreDelete(done);
        heap_caps_free(j);
        busy_clear();
        return 0;
    }
    int result = 0;
    if (xSemaphoreTake(done, pdMS_TO_TICKS(wait_ms)) == pdTRUE) {
        result = j->result;
        *got = j->got;
    }
    let_go(j);
    return result;
}

static bool slug_in(job_t *j, const char *slug) {
    if (!slug || !slug[0] || strlen(slug) >= sizeof(j->slug)) return false;
    strcpy(j->slug, slug);
    return true;
}

static yk_grant_t from_api(const yk_api_grant_t *a) {
    return (yk_grant_t){.state = a->state, .folders = a->folders, .known = a->state != YK_GRANT_NONE, .rev = a->rev};
}

static int net_get(const char *slug, yk_grant_t *out, uint32_t wait_ms) {
    job_t j = {.put = false};
    if (!slug_in(&j, slug)) return 0;
    yk_api_grant_t got;
    const int r = run(&j, wait_ms, &got);
    if (r == 1) *out = from_api(&got);
    return r;
}

static int net_put(const char *slug, const yk_grant_t *grant, yk_grant_t *out, uint32_t wait_ms) {
    job_t j = {.put = true, .state = grant->state, .folders = grant->folders, .known = grant->known, .rev = grant->rev};
    if (!slug_in(&j, slug) || (grant->state != YK_GRANT_GRANTED && grant->state != YK_GRANT_DENIED)) return 0;
    yk_api_grant_t got;
    const int r = run(&j, wait_ms, &got);
    if (r == 1 || r == 2) *out = from_api(&got);
    return r;
}

// ---------------------------------------------------------------- Yay's, on its own task (the store's list's TLS)

typedef struct {
    yk_folder_net_each_fn each;
    void *ctx;
} each_t;

static void each_grant(const char *slug, const yk_api_grant_t *a, void *ctx) {
    const each_t *e = ctx;
    const yk_grant_t g = from_api(a);
    e->each(slug, &g, e->ctx);
}

static int net_list(yk_folder_net_each_fn each, void *ctx) {
    yk_http_response_t res = {0};
    const int status = request(HTTP_METHOD_GET, "/device/v1/folders", NULL, &res);
    each_t e = {.each = each, .ctx = ctx};
    const int n = status == 200 ? yk_api_folder_each(res.body, each_grant, &e) : -1;
    yk_http_free(&res);
    printf("YK_FOLDER net list status=%d grants=%d\n", status, n);
    return n >= 0 ? 1 : 0;
}

static int net_put_now(const char *slug, const yk_grant_t *grant, yk_grant_t *out) {
    if (!slug || !slug[0] || strlen(slug) >= SLUG_MAX || (grant->state != YK_GRANT_GRANTED && grant->state != YK_GRANT_DENIED))
        return 0;
    char path[96];
    snprintf(path, sizeof(path), "/device/v1/folders/%s", slug);
    char *body = yk_api_folder_put_body(grant->state, grant->folders, grant->known, grant->rev);
    yk_http_response_t res = {0};
    const int status = body ? request(HTTP_METHOD_PUT, path, body, &res) : 0;
    cJSON_free(body);
    yk_api_grant_t got;
    const int r = yk_api_folder_put_answer(status, res.body, slug, &got);
    yk_http_free(&res);
    if (r == 1 || r == 2) *out = from_api(&got);
    printf("YK_FOLDER net put slug=%s status=%d result=%d\n", slug, status, r);
    return r;
}

// ---------------------------------------------------------------- the hand-over

void yk_folder_net_set(const yk_folder_net_t *net) __attribute__((weak));

static const yk_folder_net_t NET = {.get = net_get, .put = net_put, .list = net_list, .put_now = net_put_now};

void yk_folder_net_offer(void) {
    if (yk_folder_net_set) yk_folder_net_set(&NET);
}

void yk_folder_records_forget(void) __attribute__((weak));

void yk_folder_net_forget(void) {
    if (yk_folder_records_forget) yk_folder_records_forget();
}
