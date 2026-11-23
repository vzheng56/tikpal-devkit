// Host test: yk_cloud_me() keeps the owner it reads, yk_cloud_me_ex(…, false) leaves it as it is (Yay's daily check
// reads /me for its time zone; its language comes only with hello.ok). yaykit_cloud.c built against test/stub, with
// the HTTPS helpers and the account answered here. Also 问字's refusal held for want of voice consent (两套部署 · 语音
// 单独同意 V5: yk_cloud_hanzi_consent), against fixtures/deployments.json's voiceConsentHttp (the first argument).
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
    if (response->body && strstr(response->body, "\"OPERATION_NOT_ALLOWED\"")) return "OPERATION_NOT_ALLOWED";
    return "SERVER";
}
// 问字's streamed POST (yk_cloud_hanzi_lookup, yk_cloud_hanzi_text) on a client of its own: in turn, each request's
// answer, an HTTP status (0: none, the connection never opened) and its body, a refusal's (yk_http_read_body) or the
// events of a 200 (esp_http_client_read). Off, nothing is reached (yk_http_client: NULL, as for /me).
static struct {
    bool on;
    int status[2];
    const char *body[2];
    int posts;   // requests opened
    size_t at;   // of the events read
    char type[32];
} H;
static const char *h_body(void) { return H.posts > 0 && H.posts <= 2 && H.body[H.posts - 1] ? H.body[H.posts - 1] : ""; }

static bool s_prepare_ok;
esp_err_t yk_http_prepare(const char **code) {
    if (s_prepare_ok) return ESP_OK;
    *code = "NO_WIFI";
    return ESP_FAIL;
}
static int s_handle;
esp_http_client_handle_t yk_http_client(const char *path, esp_http_client_method_t method, const char *bearer,
                                        const char *content_type, int timeout_ms) {
    (void)method, (void)bearer, (void)timeout_ms;
    if (!H.on || strcmp(path, "/device/v1/hanzi/lookup")) return NULL;
    snprintf(H.type, sizeof(H.type), "%s", content_type ? content_type : "");
    return (esp_http_client_handle_t)&s_handle;
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
    if (!H.on) return;
    out->body = strdup(h_body());
    out->len = strlen(out->body);
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
    if (H.on) {
        H.at = 0;
        return ++H.posts <= 2 && H.status[H.posts - 1] ? ESP_OK : ESP_FAIL;
    }
    if (F.on) F.opened++;
    return F.on ? ESP_OK : ESP_FAIL;
}
int esp_http_client_write(esp_http_client_handle_t c, const char *b, int n) { (void)c, (void)b; return H.on ? n : -1; }
int esp_http_client_fetch_headers(esp_http_client_handle_t c) {
    if (H.on) return 0;
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
int esp_http_client_get_status_code(esp_http_client_handle_t c) {
    (void)c;
    if (H.on) return H.posts > 0 && H.posts <= 2 ? H.status[H.posts - 1] : 0;
    return F.on ? F.status : 0;
}
int esp_http_client_read(esp_http_client_handle_t c, char *b, int n) {
    (void)c;
    if (H.on) {  // a 200's events, then the end
        const char *body = h_body();
        size_t k = strlen(body) - H.at < (size_t)n ? strlen(body) - H.at : (size_t)n;
        memcpy(b, body + H.at, k);
        H.at += k;
        return (int)k;
    }
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

// ---------------------------------------------------------------- 问字: a refusal's consent (两套部署 · 语音单独同意 V5)

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) n = 0;
    if (buf) buf[n] = 0;
    fclose(f);
    return buf;
}

static int s_events;
static bool s_no_wifi;  // the next lookup finds no Wi-Fi (yk_http_prepare)
static void on_hanzi(const char *event, const cJSON *data, void *ctx) {
    (void)event, (void)data, (void)ctx;
    s_events++;
}

static const char VOICE[] = "{\"error\":\"OPERATION_NOT_ALLOWED\",\"consent\":\"voice\"}";
static const char ANSWER[] = "event: heard\ndata: {\"text\":\"老鹰的鹰\"}\n\n"
                             "event: answer\ndata: {\"found\":true,\"char\":\"鹰\",\"pinyin\":\"yīng\"}\n\n";

/** One lookup, a recording (`text` NULL) or a typed question, answered `status` with `body`, and after a new token
 *  `then_status` with `then_body`. Its code in *code. */
static esp_err_t hanzi(const char *text, int status, const char *body, int then_status, const char *then_body,
                       const char **code) {
    memset(&H, 0, sizeof(H));
    H.on = true;
    H.status[0] = status, H.body[0] = body, H.status[1] = then_status, H.body[1] = then_body;
    s_events = 0;
    s_prepare_ok = !s_no_wifi;
    static const uint8_t wav[64];
    *code = NULL;
    const esp_err_t err = text ? yk_cloud_hanzi_text(text, on_hanzi, NULL, code)
                               : yk_cloud_hanzi_lookup(wav, sizeof(wav), on_hanzi, NULL, code);
    H.on = false;
    return err;
}

/** A refusal's body (an object) as it is, or without its consent. */
static char *printed(const cJSON *body, bool without) {
    cJSON *b = cJSON_Duplicate(body, 1);
    if (without) cJSON_DeleteItemFromObjectCaseSensitive(b, "consent");
    char *out = cJSON_PrintUnformatted(b);
    cJSON_Delete(b);
    return out;
}

/** `with` and `plain` (the same refusal without its consent) at 403, to a recording and to a typed question: `want`
 *  with it, none without; the code the same either way; asked once (nothing tried again for it), no event. */
static void refused_as(const char *with, const char *plain, yk_cloud_consent_t want) {
    for (int typed = 0; typed < 2; typed++) {
        const char *text = typed ? "鹰" : NULL, *code = NULL, *plain_code = NULL;
        CHECK(hanzi(text, 403, plain, 0, NULL, &plain_code) == ESP_FAIL);
        CHECK(yk_cloud_hanzi_consent() == YK_CLOUD_CONSENT_NONE);
        CHECK(hanzi(text, 403, with, 0, NULL, &code) == ESP_FAIL);
        CHECK(yk_cloud_hanzi_consent() == want);
        CHECK(code && plain_code && !strcmp(code, plain_code));
        CHECK(H.posts == 1 && !s_events);
        CHECK(!strcmp(H.type, typed ? "application/json" : "audio/wav"));
    }
}

/** yk_cloud_hanzi_consent() (consent.ts VOICE_CONSENT_GATES.device: 403 before the stream, a recording's lookup), as
 *  device-api.ts readRefusalConsent reads it (yk_api_error_consent): the contract's own cases, then what comes after. */
static void hanzi_consent(const char *deployments) {
    const char *code = NULL;
    CHECK(yk_cloud_hanzi_consent() == YK_CLOUD_CONSENT_NONE);  // before any lookup
    char *text = deployments ? slurp(deployments) : NULL;
    cJSON *doc = text ? cJSON_Parse(text) : NULL;
    const cJSON *http = cJSON_GetObjectItemCaseSensitive(doc, "voiceConsentHttp"), *c;
    if (!http) printf("yaykit_cloud me: the voiceConsentHttp cases left out (no fixtures/deployments.json)\n");
    int voice = 0, none = 0;
    cJSON_ArrayForEach(c, http) {
        const cJSON *body = cJSON_GetObjectItemCaseSensitive(c, "body");
        const cJSON *want = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(c, "expect"), "consent");
        CHECK(cJSON_IsObject(body) && want);
        if (!cJSON_IsObject(body)) continue;
        const bool is_voice = cJSON_IsString(want) && !strcmp(want->valuestring, "voice");
        char *with = printed(body, false), *plain = printed(body, true);
        refused_as(with, plain, is_voice ? YK_CLOUD_CONSENT_VOICE : YK_CLOUD_CONSENT_NONE);
        cJSON_free(with);
        cJSON_free(plain);
        if (is_voice) voice++;
        else none++;
    }
    if (http) CHECK(voice >= 1 && none >= 4);
    cJSON_Delete(doc);
    free(text);
    // The device service's (VOICE_CONSENT_GATES.device); without it (a frozen account, an app not allowed): as today.
    refused_as(VOICE, "{\"error\":\"OPERATION_NOT_ALLOWED\"}", YK_CLOUD_CONSENT_VOICE);
    CHECK(hanzi(NULL, 403, VOICE, 0, NULL, &code) == ESP_FAIL && !strcmp(code, "OPERATION_NOT_ALLOWED"));
    // Held, then the next lookup gets through: none (the notice goes); so after no answer, or another refusal.
    CHECK(hanzi(NULL, 200, ANSWER, 0, NULL, &code) == ESP_OK && s_events == 2);
    CHECK(yk_cloud_hanzi_consent() == YK_CLOUD_CONSENT_NONE);
    CHECK(hanzi(NULL, 403, VOICE, 0, NULL, &code) == ESP_FAIL && yk_cloud_hanzi_consent() == YK_CLOUD_CONSENT_VOICE);
    CHECK(hanzi(NULL, 0, NULL, 0, NULL, &code) == ESP_FAIL && !strcmp(code, "NETWORK"));
    CHECK(yk_cloud_hanzi_consent() == YK_CLOUD_CONSENT_NONE);
    CHECK(hanzi(NULL, 403, VOICE, 0, NULL, &code) == ESP_FAIL && yk_cloud_hanzi_consent() == YK_CLOUD_CONSENT_VOICE);
    CHECK(hanzi(NULL, 429, "{\"error\":\"RATE_LIMITED\"}", 0, NULL, &code) == ESP_FAIL);
    CHECK(yk_cloud_hanzi_consent() == YK_CLOUD_CONSENT_NONE);
    CHECK(hanzi(NULL, 403, VOICE, 0, NULL, &code) == ESP_FAIL && yk_cloud_hanzi_consent() == YK_CLOUD_CONSENT_VOICE);
    s_no_wifi = true;  // nothing sent at all
    CHECK(hanzi(NULL, 403, VOICE, 0, NULL, &code) == ESP_FAIL && !strcmp(code, "NO_WIFI") && !H.posts);
    s_no_wifi = false;
    CHECK(yk_cloud_hanzi_consent() == YK_CLOUD_CONSENT_NONE);
    CHECK(hanzi("鹰", 403, VOICE, 0, NULL, &code) == ESP_FAIL && yk_cloud_hanzi_consent() == YK_CLOUD_CONSENT_VOICE);
    CHECK(hanzi("鹰", 200, ANSWER, 0, NULL, &code) == ESP_OK && yk_cloud_hanzi_consent() == YK_CLOUD_CONSENT_NONE);
    // A new token first, then held: the second answer's, asked twice in all. Held first: asked once.
    CHECK(hanzi(NULL, 401, "{\"error\":\"TOKEN_EXPIRED\"}", 403, VOICE, &code) == ESP_FAIL && H.posts == 2);
    CHECK(yk_cloud_hanzi_consent() == YK_CLOUD_CONSENT_VOICE && !strcmp(code, "OPERATION_NOT_ALLOWED"));
    CHECK(hanzi(NULL, 403, VOICE, 200, ANSWER, &code) == ESP_FAIL && H.posts == 1 && !s_events);
    CHECK(yk_cloud_hanzi_consent() == YK_CLOUD_CONSENT_VOICE);
    // An error among a 200's events is the stream's, not a refusal before it: not read, none.
    CHECK(hanzi(NULL, 200, "event: error\ndata: {\"error\":\"OPERATION_NOT_ALLOWED\",\"consent\":\"voice\"}\n\n", 0, NULL,
                &code) == ESP_OK && s_events == 1);
    CHECK(yk_cloud_hanzi_consent() == YK_CLOUD_CONSENT_NONE);
}

int main(int argc, char **argv) {
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
    hanzi_consent(argc > 1 ? argv[1] : NULL);

    printf("yaykit_cloud me host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
