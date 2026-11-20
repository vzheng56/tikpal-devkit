// Host test: yk_cloud_me() keeps the owner it reads, yk_cloud_me_ex(…, false) leaves it as it is (Yay's daily check
// reads /me for its time zone; its language comes only with hello.ok). yaykit_cloud.c built against test/stub, with
// the HTTPS helpers and the account answered here.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yaykit_account.h"
#include "yaykit_cloud.h"
#include "yk_http.h"

static int checks, failures;
#define CHECK(c)                                                    \
    do {                                                            \
        checks++;                                                   \
        if (!(c)) {                                                 \
            failures++;                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
        }                                                           \
    } while (0)

static const char *ME =
    "{\"credits\":{\"remaining\":120,\"dailyLimit\":200,\"dailyRemaining\":80,\"resetAt\":\"2026-10-27T00:00:00+08:00\"},"
    "\"owner\":{\"nickname\":\"小鱼\",\"locale\":\"zh\",\"timezone\":\"Asia/Shanghai\"},\"apiVersion\":1}";

// What the cloud answers, in turn: an HTTP status and body; status 0 is no answer (the network).
static struct {
    int status;
    const char *body;
} s_replies[4];
static int s_reply_count, s_replies_used;
static char s_path[64], s_bearer[32];
static int s_owner_seen, s_token_expired, s_unbound;
static char s_owner_locale[16];

static void reset(void) {
    memset(s_replies, 0, sizeof(s_replies));
    s_reply_count = s_replies_used = 0;
    s_path[0] = s_bearer[0] = s_owner_locale[0] = 0;
    s_owner_seen = s_token_expired = s_unbound = 0;
}

static void reply(int status, const char *body) {
    s_replies[s_reply_count].status = status;
    s_replies[s_reply_count].body = body;
    s_reply_count++;
}

// ---- yk_http (yk_http.c is ESP-IDF's client)

esp_err_t yk_http_json(esp_http_client_method_t method, const char *path, const char *bearer, const char *json,
                       yk_http_response_t *out) {
    (void)method, (void)json;
    memset(out, 0, sizeof(*out));
    snprintf(s_path, sizeof(s_path), "%s", path);
    snprintf(s_bearer, sizeof(s_bearer), "%s", bearer ? bearer : "");
    if (s_replies_used >= s_reply_count || !s_replies[s_replies_used].status) {
        s_replies_used++;
        return ESP_FAIL;
    }
    out->status = s_replies[s_replies_used].status;
    out->body = strdup(s_replies[s_replies_used].body);
    out->len = strlen(out->body);
    s_replies_used++;
    return ESP_OK;
}
void yk_http_free(yk_http_response_t *response) {
    free(response->body);
    response->body = NULL;
}
const char *yk_http_error_code(const yk_http_response_t *response) {
    if (response->body && strstr(response->body, "TOKEN_EXPIRED")) return "TOKEN_EXPIRED";
    if (response->body && strstr(response->body, "DEVICE_NOT_ACTIVATED")) return "DEVICE_NOT_ACTIVATED";
    return "SERVER";
}
static bool s_prepare_ok;
esp_err_t yk_http_prepare(const char **code) {
    if (s_prepare_ok) return ESP_OK;
    *code = "NO_WIFI";
    return ESP_FAIL;
}
esp_http_client_handle_t yk_http_client(const char *path, esp_http_client_method_t method, const char *bearer,
                                        const char *content_type, int timeout_ms) {
    (void)path, (void)method, (void)bearer, (void)content_type, (void)timeout_ms;
    return NULL;
}
esp_err_t yk_http_send(esp_http_client_method_t method, const char *path, const char *bearer, const char *content_type,
                       const uint8_t *body, size_t body_len, int timeout_ms, yk_http_response_t *out) {
    (void)content_type, (void)body, (void)body_len, (void)timeout_ms;
    return yk_http_json(method, path, bearer, NULL, out);  // the same replies (the device API's other calls, the library's)
}
esp_err_t yk_http_on_event(esp_http_client_event_t *event) {
    (void)event;
    return ESP_OK;
}

void yk_http_read_body(esp_http_client_handle_t client, yk_http_response_t *out) {
    (void)client;
    memset(out, 0, sizeof(*out));
}

// ---- yaykit_account

esp_err_t yk_account_token(char *out, size_t size, const char **code) {
    (void)code;
    snprintf(out, size, "token-%d", s_token_expired);
    return ESP_OK;
}
void yk_account_token_expired(void) { s_token_expired++; }
void yk_account_unbound_by_server(void) { s_unbound++; }
void yk_account_owner_seen(const cJSON *owner) {
    s_owner_seen++;
    const cJSON *loc = cJSON_GetObjectItemCaseSensitive(owner, "locale");
    snprintf(s_owner_locale, sizeof(s_owner_locale), "%s", cJSON_IsString(loc) ? loc->valuestring : "");
}

// ---- ESP-IDF, not reached on the /me path

// A response for yk_cloud_get_to, as the real client gives it: each header through the config's event handler
// (HTTP_EVENT_ON_HEADER, with its user_data), then the body read in pieces. Off, nothing is reached (init: NULL).
static struct {
    bool on;
    int status;
    const char *content_range;  // the header, NULL none
    const uint8_t *body;
    size_t len, at, cut_at;     // cut_at: the connection goes there (0: never)
    char asked_range[48];
    esp_err_t (*handler)(esp_http_client_event_t *);
    void *user_data;
    int opened;
} F;
static int s_handle;

static int s_rx, s_tx;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    s_rx = config->buffer_size, s_tx = config->buffer_size_tx;
    if (!F.on) return NULL;
    F.handler = config->event_handler;
    F.user_data = config->user_data;
    return (esp_http_client_handle_t)&s_handle;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int n) {
    (void)c, (void)n;
    if (F.on) F.opened++;
    return F.on ? ESP_OK : ESP_FAIL;
}
int esp_http_client_write(esp_http_client_handle_t c, const char *b, int n) { (void)c, (void)b, (void)n; return -1; }
int esp_http_client_fetch_headers(esp_http_client_handle_t c) {
    if (!F.on) return -1;
    char type_key[] = "Content-Type", type_value[] = "application/octet-stream", range_key[] = "content-range";
    esp_http_client_event_t e = {.event_id = HTTP_EVENT_ON_HEADER, .client = c, .user_data = F.user_data,
                                 .header_key = type_key, .header_value = type_value};
    if (F.handler) F.handler(&e);
    if (F.content_range && F.handler) {
        char value[64];
        snprintf(value, sizeof(value), "%s", F.content_range);
        e.header_key = range_key;  // header names fold cases
        e.header_value = value;
        F.handler(&e);
    }
    return (int)(F.len - F.at);
}
int esp_http_client_get_status_code(esp_http_client_handle_t c) { (void)c; return F.on ? F.status : 0; }
int esp_http_client_read(esp_http_client_handle_t c, char *b, int n) {
    (void)c;
    if (!F.on) return -1;
    if (F.cut_at && F.at >= F.cut_at) return -1;
    size_t k = F.len - F.at < (size_t)n ? F.len - F.at : (size_t)n;
    if (F.cut_at && F.at + k > F.cut_at) k = F.cut_at - F.at;
    memcpy(b, F.body + F.at, k);
    F.at += k;
    return (int)k;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t c, int ms) { (void)c, (void)ms; return ESP_OK; }
esp_err_t esp_http_client_close(esp_http_client_handle_t c) { (void)c; return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) { (void)c; return ESP_OK; }
esp_err_t esp_http_client_set_url(esp_http_client_handle_t c, const char *u) { (void)c, (void)u; return ESP_OK; }
esp_err_t esp_http_client_set_method(esp_http_client_handle_t c, esp_http_client_method_t m) { (void)c, (void)m; return ESP_OK; }
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *k, const char *v) {
    (void)c;
    if (F.on && !strcmp(k, "Range")) snprintf(F.asked_range, sizeof(F.asked_range), "%s", v);
    return ESP_OK;
}
int esp_http_client_get_socket(esp_http_client_handle_t c) { (void)c; return -1; }
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c) { (void)c; return F.on && F.at == F.len; }
bool esp_http_client_is_persistent_connection(esp_http_client_handle_t c) { (void)c; return false; }
esp_err_t esp_crt_bundle_attach(void *conf) { (void)conf; return ESP_OK; }
int64_t esp_timer_get_time(void) { return 0; }

// What yk_cloud_get_to handed over: each piece's place and bytes, laid end to end.
static struct {
    uint8_t got[4096];
    uint64_t first_at, next_at;
    int pieces;
    bool refuse_after_first, stop_after_first, out_of_order;
} P;
static bool piece(void *ctx, uint64_t at, const uint8_t *data, size_t len) {
    (void)ctx;
    if (!P.pieces) P.first_at = at, P.next_at = at;
    if (at != P.next_at) P.out_of_order = true;
    if (at + len <= sizeof(P.got)) memcpy(P.got + at, data, len);
    P.next_at = at + len;
    P.pieces++;
    return !(P.refuse_after_first && P.pieces >= 1);
}
static bool go(void *ctx) {
    (void)ctx;
    return !(P.stop_after_first && P.pieces >= 1);
}

static uint8_t file[2000];

/** One GET in pieces of 256 against a scripted response. */
static esp_err_t get_to(uint64_t from, int status, const char *range, size_t cut_at, uint64_t *got, const char **code) {
    memset(&P, 0, sizeof(P));
    memset(&F, 0, sizeof(F));
    F.on = true;
    F.status = status;
    F.content_range = range;
    F.body = status == 206 ? file + from : file;
    F.len = status == 206 ? sizeof(file) - (size_t)from : sizeof(file);
    F.cut_at = cut_at;
    uint8_t buf[256];
    const esp_err_t err = yk_cloud_get_to("https://assets.test/f.bin", from, buf, sizeof(buf), piece, go, NULL, 1000, got, code);
    F.on = false;
    return err;
}

static void pieces(void) {
    for (size_t i = 0; i < sizeof(file); i++) file[i] = (uint8_t)(i * 7 + 3);
    uint64_t got = 0;
    const char *code = NULL;
    s_prepare_ok = true;
    // from the start: no Range asked, all of it in order
    CHECK(get_to(0, 200, NULL, 0, &got, &code) == ESP_OK && got == sizeof(file) && !F.asked_range[0]);
    CHECK(P.first_at == 0 && !P.out_of_order && P.pieces == 8 && !memcmp(P.got, file, sizeof(file)));
    // on from 1000: Range asked, a 206 from there, its pieces at 1000 on
    CHECK(get_to(1000, 206, "bytes 1000-1999/2000", 0, &got, &code) == ESP_OK && got == 1000);
    CHECK(!strcmp(F.asked_range, "bytes=1000-") && P.first_at == 1000 && !P.out_of_order);
    CHECK(!memcmp(P.got + 1000, file + 1000, 1000));
    // a 200 to the Range: the whole file, its first piece at 0 (the caller starts again)
    CHECK(get_to(1000, 200, NULL, 0, &got, &code) == ESP_OK && got == sizeof(file) && P.first_at == 0);
    // a 206 from elsewhere, or none said: not this file's next bytes, nothing handed over
    CHECK(get_to(1000, 206, "bytes 0-1999/2000", 0, &got, &code) == ESP_FAIL && !strcmp(code, "SERVER") && !P.pieces);
    CHECK(get_to(1000, 206, NULL, 0, &got, &code) == ESP_FAIL && !strcmp(code, "SERVER") && !P.pieces);
    CHECK(get_to(0, 404, NULL, 0, &got, &code) == ESP_FAIL && !strcmp(code, "SERVER") && !P.pieces);
    // cut short: what came is the caller's, NETWORK
    CHECK(get_to(0, 200, NULL, 700, &got, &code) == ESP_FAIL && !strcmp(code, "NETWORK") && got == 700);
    // the caller's write refused (the card full): WRITE, at once
    memset(&P, 0, sizeof(P));
    P.refuse_after_first = true;
    {
        memset(&F, 0, sizeof(F));
        F.on = true, F.status = 200, F.body = file, F.len = sizeof(file);
        uint8_t buf[256];
        CHECK(yk_cloud_get_to("https://assets.test/f.bin", 0, buf, sizeof(buf), piece, go, NULL, 1000, &got, &code) ==
                  ESP_FAIL &&
              !strcmp(code, "WRITE") && got == 0 && P.pieces == 1);
        F.on = false;
    }
    // stopped (someone came): before connecting, no connection; after a piece, STOPPED with what came
    memset(&P, 0, sizeof(P));
    P.stop_after_first = true;
    P.pieces = 1;  // already told
    {
        memset(&F, 0, sizeof(F));
        F.on = true, F.status = 200, F.body = file, F.len = sizeof(file);
        uint8_t buf[256];
        CHECK(yk_cloud_get_to("https://assets.test/f.bin", 0, buf, sizeof(buf), piece, go, NULL, 1000, &got, &code) ==
                  ESP_ERR_INVALID_STATE &&
              !strcmp(code, "STOPPED") && !F.opened && got == 0);
        memset(&P, 0, sizeof(P));
        P.stop_after_first = true;
        F.at = 0;
        CHECK(yk_cloud_get_to("https://assets.test/f.bin", 0, buf, sizeof(buf), piece, go, NULL, 1000, &got, &code) ==
                  ESP_ERR_INVALID_STATE &&
              !strcmp(code, "STOPPED") && got == 256);
        F.on = false;
    }
    // not https: nothing
    uint8_t buf[16];
    CHECK(yk_cloud_get_to("http://assets.test/f.bin", 0, buf, sizeof(buf), piece, NULL, NULL, 1000, &got, &code) ==
              ESP_ERR_INVALID_ARG &&
          !strcmp(code, "BAD_URL"));
    // the device API's library: its path, a language only of safe characters
    char *json = NULL;
    reset();
    reply(200, "{\"revision\":1,\"items\":[]}");
    CHECK(yk_cloud_library("zh-Hans", &json, &code) == ESP_OK && json && !strcmp(s_path, "/device/v1/library?lang=zh-Hans"));
    free(json);
    json = NULL;
    CHECK(yk_cloud_library("zh&x=1", &json, &code) == ESP_ERR_INVALID_ARG && !strcmp(code, "BAD_REQUEST") && !json);
    CHECK(yk_cloud_library(NULL, &json, &code) == ESP_ERR_INVALID_ARG);
    reset();
    reply(404, "{\"error\":\"NOT_FOUND\"}");
    CHECK(yk_cloud_library("en", &json, &code) == ESP_FAIL && !strcmp(code, "NOT_FOUND") && !json);
}

int main(void) {
    yk_cloud_me_t me;
    const char *code = NULL;

    // An app's /me: the owner kept, as ever
    reset();
    reply(200, ME);
    CHECK(yk_cloud_me(&me, &code) == ESP_OK);
    CHECK(!strcmp(s_path, "/device/v1/me") && !strcmp(s_bearer, "token-0"));
    CHECK(s_owner_seen == 1 && !strcmp(s_owner_locale, "zh"));
    CHECK(!strcmp(me.timezone, "Asia/Shanghai") && me.remaining == 120);

    // Yay's daily check: the time zone read, the kept owner (its language) left alone
    reset();
    reply(200, ME);
    CHECK(yk_cloud_me_ex(&me, false, &code) == ESP_OK);
    CHECK(s_owner_seen == 0);
    CHECK(!strcmp(me.timezone, "Asia/Shanghai") && !strcmp(me.locale, "zh"));
    CHECK(yk_cloud_me_ex(&me, true, &code) == ESP_FAIL);  // nothing more to answer: NETWORK
    CHECK(s_owner_seen == 0);

    // ... also after a token's refresh
    reset();
    reply(401, "{\"error\":\"TOKEN_EXPIRED\"}");
    reply(200, ME);
    CHECK(yk_cloud_me_ex(&me, false, &code) == ESP_OK);
    CHECK(s_token_expired == 1 && s_replies_used == 2 && !strcmp(s_bearer, "token-1"));
    CHECK(s_owner_seen == 0 && !strcmp(me.timezone, "Asia/Shanghai"));

    // No answer, or refused: nothing kept either way
    reset();
    CHECK(yk_cloud_me_ex(&me, false, &code) == ESP_FAIL && !strcmp(code, "NETWORK"));
    CHECK(!me.timezone[0] && s_owner_seen == 0);
    reset();
    reply(401, "{\"error\":\"DEVICE_NOT_ACTIVATED\"}");
    CHECK(yk_cloud_me(&me, &code) == ESP_FAIL && !strcmp(code, "DEVICE_NOT_ACTIVATED"));
    CHECK(s_unbound == 1 && s_owner_seen == 0);

    // yk_cloud_get's client: its buffers as written, or the firmware's smallest
    uint8_t got[4];
    size_t len = 0;
    const char *why = NULL;
    s_prepare_ok = true;
    CHECK(yk_cloud_get("https://assets.test/a", got, sizeof(got), &len, 1000, &why) == ESP_ERR_NO_MEM);
#if CONFIG_YK_CLOUD_HTTP_BUFFER_MIN
    CHECK(s_rx == CONFIG_YK_CLOUD_HTTP_BUFFER_MIN && s_tx == CONFIG_YK_CLOUD_HTTP_BUFFER_MIN);
#else
    CHECK(s_rx == 2048 && s_tx == 1024);
#endif

    pieces();

    printf("yaykit_cloud me host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
