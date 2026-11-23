// Streamed recordings (yk_cloud_stream.h): the three calls on one HTTPS connection to the device API, kept between
// them. esp_http_client reuses an open connection when a request is opened on the same client again (its connect step
// starts a new request and only dials when not connected), as long as the answer before was read to its end and the
// server keeps the connection; otherwise it is closed here and the next call dials again.
#include "yk_cloud_stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "yaykit_account.h"
#include "yaykit_cloud.h"
#include "yk_api.h"  // yk_api_error_consent
#include "yk_api_stream.h"
#include "yk_http.h"

#define IDLE_MS    45000  // a kept connection unused this long is closed: under the service's 72 s keep-alive and
                          // whatever the hop before it keeps (the next chunk comes every ~30 s)
// A request with no progress this long lets go (each read or write, not the whole request: a 64 KB chunk on a slow
// uplink keeps going), tried again later: the recordings' flash task also takes these steps, and a request whose
// network went held it 51 s, a recording's start and end waiting behind it (新四号机 11-18; 30 s before).
#define TIMEOUT_MS 15000
// The completion: the platform joins the chunks, by the recording's length (yk_api_stream_complete_ms: 15 s, 130 s for
// an hour's).

static const char *TAG = "yk_stream";
static esp_http_client_handle_t s_client;
static int64_t s_used_ms;
static bool (*s_stop)(void);  // yk_cloud_stream_set_stop (Yay 0.2 F6c), NULL none
static yk_cloud_consent_t s_consent;  // the last call's refusal held for want of (yk_cloud_stream_consent), NONE none

void yk_cloud_stream_set_stop(bool (*stop)(void)) { s_stop = stop; }

yk_cloud_consent_t yk_cloud_stream_consent(void) { return s_consent; }

static bool stopped(void) { return s_stop && s_stop(); }

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

void yk_cloud_stream_close(void) {
    if (!s_client) return;
    esp_http_client_close(s_client);
    esp_http_client_cleanup(s_client);
    s_client = NULL;
}

/** The answer's headers. With a stop (yk_cloud_stream_set_stop) the answer is awaited on the socket in pieces of
 *  YK_API_WAIT_PIECE_MS, the stop asked between them, `wait_ms` in all (*why: STOPPED, TIMEOUT); once something comes
 *  it is read as ever. Without one (or no socket to look at), as ever: each read up to the client's timeout. */
static esp_err_t answer(int wait_ms, const char **why) {
    const int fd = s_stop ? esp_http_client_get_socket(s_client) : -1;
    const int64_t t0 = now_ms();
    while (fd >= 0) {
        const yk_api_wait_t w = yk_api_stream_wait(now_ms() - t0, wait_ms, stopped());
        if (w != YK_API_WAIT_ON) {
            *why = w == YK_API_WAIT_STOP ? "STOPPED" : "TIMEOUT";
            return ESP_FAIL;
        }
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(fd, &readable);
        struct timeval tv = {.tv_sec = 0, .tv_usec = YK_API_WAIT_PIECE_MS * 1000};
        const int n = select(fd + 1, &readable, NULL, NULL, &tv);
        if (n < 0) return ESP_FAIL;
        if (n > 0) break;  // the answer is coming
    }
    return esp_http_client_fetch_headers(s_client) < 0 ? ESP_FAIL : ESP_OK;
}

/** One request on the kept connection, or a new one. A kept connection the server had closed is tried once more on
 *  a new one. Fills `res` (yk_http_free); ESP_FAIL when no answer came (*why: STOPPED or TIMEOUT when that was it).
 *  `wait_ms`: with a stop set, the answer awaited this long in all (TIMEOUT after it). */
static esp_err_t send_once(esp_http_client_method_t method, const char *path, const char *token,
                           const char *content_type, const uint8_t *body, size_t len, const char *sha256,
                           int timeout_ms, int wait_ms, yk_http_response_t *res, const char **why) {
    memset(res, 0, sizeof(*res));
    if (stopped()) {  // no connection opened, no handshake begun
        *why = "STOPPED";
        return ESP_FAIL;
    }
    if (s_client && now_ms() - s_used_ms > IDLE_MS) yk_cloud_stream_close();
    for (int round = 0; round < 2; round++) {
        const bool kept = s_client != NULL;
        if (!kept) {
            s_client = yk_http_client(path, method, token, content_type, timeout_ms);
            if (!s_client) return ESP_ERR_NO_MEM;
        } else {
            esp_http_client_set_timeout_ms(s_client, timeout_ms);
            char url[256], auth[640];
            snprintf(url, sizeof(url), "%s%s", yk_cloud_origin(), path);
            snprintf(auth, sizeof(auth), "Bearer %s", token);
            esp_http_client_set_url(s_client, url);
            esp_http_client_set_method(s_client, method);
            esp_http_client_set_header(s_client, "Authorization", auth);
            if (content_type) esp_http_client_set_header(s_client, "Content-Type", content_type);
            else esp_http_client_delete_header(s_client, "Content-Type");
        }
        if (sha256) esp_http_client_set_header(s_client, "x-yay-sha256", sha256);
        else esp_http_client_delete_header(s_client, "x-yay-sha256");
        esp_err_t err = esp_http_client_open(s_client, (int)len);
        for (size_t sent = 0; err == ESP_OK && sent < len;) {
            if (stopped()) {  // before each piece: what went is the cloud's to drop (no 200: not kept here)
                *why = "STOPPED";
                err = ESP_FAIL;
                break;
            }
            const int n = esp_http_client_write(s_client, (const char *)body + sent,
                                                (int)(len - sent > 4096 ? 4096 : len - sent));
            if (n <= 0) err = ESP_FAIL;
            else sent += (size_t)n;
        }
        if (err == ESP_OK) err = answer(wait_ms, why);
        if (err == ESP_OK) {
            res->status = esp_http_client_get_status_code(s_client);
            yk_http_read_body(s_client, res);
            s_used_ms = now_ms();
            if (!esp_http_client_is_complete_data_received(s_client) ||
                !esp_http_client_is_persistent_connection(s_client))
                yk_cloud_stream_close();
            return ESP_OK;
        }
        ESP_LOGW(TAG, "%s: %s on a %s connection", path, *why ? *why : esp_err_to_name(err), kept ? "kept" : "new");
        yk_cloud_stream_close();
        if (!kept || *why) break;  // stopped or out of time: not again on a new one
    }
    return ESP_FAIL;
}

/** A call with the app's token, again once with a new token when it expired. ESP_OK for a 2xx answer; `res` holds
 *  the body either way, `*code` the refusal. */
static esp_err_t call(esp_http_client_method_t method, const char *path, const char *content_type, const uint8_t *body,
                      size_t len, const char *sha256, int timeout_ms, int wait_ms, bool says_timeout,
                      yk_http_response_t *res, const char **code) {
    memset(res, 0, sizeof(*res));
    if (stopped()) {  // before the token too (a new one is an HTTPS call of its own)
        *code = "STOPPED";
        return ESP_FAIL;
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        char token[512];
        if (!yk_account_token_cached()) yk_cloud_stream_close();  // a new token comes on a connection of its own
        if (yk_account_token(token, sizeof(token), code) != ESP_OK) return ESP_FAIL;
        if (yk_http_prepare(code) != ESP_OK) {
            yk_cloud_stream_close();  // no Wi-Fi: whatever was open is gone
            return ESP_FAIL;
        }
        const char *why = NULL;
        if (send_once(method, path, token, content_type, body, len, sha256, timeout_ms, wait_ms, res, &why) != ESP_OK) {
            // An answer not in time is the network's (as ever: everything waits), but a completion's (says_timeout):
            // registering again tells whether the platform joined it.
            *code = why && (says_timeout || strcmp(why, "TIMEOUT")) ? why : "NETWORK";
            return ESP_FAIL;
        }
        if (res->status >= 200 && res->status < 300) return ESP_OK;
        *code = yk_api_stream_code(res->status, res->body);
        s_consent = yk_api_error_consent(res->body);  // why, beside the code (两套部署 · 语音单独同意 V4): never instead
        ESP_LOGW(TAG, "%s refused: HTTP %d %s", path, res->status, *code);
        if (res->status == 401 && !strcmp(*code, "DEVICE_NOT_ACTIVATED")) yk_account_unbound_by_server();
        if (attempt || res->status != 401 || strcmp(*code, "TOKEN_EXPIRED")) return ESP_FAIL;
        yk_account_token_expired();
        yk_http_free(res);
    }
    return ESP_FAIL;
}

esp_err_t yk_cloud_stream_register(const yk_cloud_stream_t *file, yk_cloud_stream_state_t *out, const char **code) {
    char path[128];
    s_consent = YK_CLOUD_CONSENT_NONE;  // each of the three: only its own refusal says it
    memset(out, 0, sizeof(*out));
    if (!yk_api_stream_path(file->id, 0, path, sizeof(path))) {
        *code = "VALIDATION_ERROR";
        return ESP_ERR_INVALID_ARG;
    }
    char *body = yk_api_stream_body(file);
    if (!body) {
        *code = "NETWORK";
        return ESP_ERR_NO_MEM;
    }
    yk_http_response_t res;
    esp_err_t err = call(HTTP_METHOD_POST, path, "application/json", (const uint8_t *)body, strlen(body), NULL,
                         TIMEOUT_MS, TIMEOUT_MS, false, &res, code);
    cJSON_free(body);
    if (err == ESP_OK && !yk_api_stream_state(res.body, out)) {
        *code = "SERVER";
        err = ESP_FAIL;
    }
    yk_http_free(&res);
    return err;
}

esp_err_t yk_cloud_stream_chunk(const char *id, int seq, const uint8_t *data, size_t len, const char sha256[65],
                                const char **code) {
    char path[128];
    s_consent = YK_CLOUD_CONSENT_NONE;
    if (seq < 1 || !yk_api_stream_path(id, seq, path, sizeof(path))) {
        *code = "VALIDATION_ERROR";
        return ESP_ERR_INVALID_ARG;
    }
    yk_http_response_t res;
    const esp_err_t err =
        call(HTTP_METHOD_PUT, path, "application/octet-stream", data, len, sha256, TIMEOUT_MS, TIMEOUT_MS, false, &res,
             code);
    yk_http_free(&res);
    return err;
}

esp_err_t yk_cloud_stream_complete(const char *id, const yk_cloud_stream_end_t *end, const char **code) {
    char path[128];
    s_consent = YK_CLOUD_CONSENT_NONE;
    if (!yk_api_stream_path(id, -1, path, sizeof(path))) {
        *code = "VALIDATION_ERROR";
        return ESP_ERR_INVALID_ARG;
    }
    char *body = yk_api_stream_end_body(end);
    if (!body) {
        *code = "NETWORK";
        return ESP_ERR_NO_MEM;
    }
    yk_http_response_t res;
    // Its answer comes once the platform has joined the chunks: with a stop set, YK_CLOUD_STREAM_COMPLETE_STOPPABLE_MS
    // at most (TIMEOUT: registering again says whether it is joined), the stop asked every half second.
    const int ms = yk_api_stream_complete_ms(end->duration_ms);
    const esp_err_t err = call(HTTP_METHOD_POST, path, "application/json", (const uint8_t *)body, strlen(body), NULL, ms,
                               s_stop ? yk_api_stream_complete_stoppable_ms(end->duration_ms) : ms, s_stop != NULL, &res,
                               code);
    cJSON_free(body);
    yk_http_free(&res);
    return err;
}
