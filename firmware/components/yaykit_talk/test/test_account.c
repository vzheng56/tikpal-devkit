// Host test for 两套部署 E (yk_talk_account.c): yaykit_talk's production session only for a device added to an account.
// Against yaykit_session.c itself, on yaykit_session's test/stub (as its test_hold.c): the session's task runs here, with
// yaykit_talk's token, until it is parked (held, or waiting with no end). What it would send is counted: the account's
// token requests and the WebSocket connections, with their headers.
#include <setjmp.h>

#include "../../yaykit_session/yaykit_session.c"
#include "../yk_talk_account.h"

static int checks, failures;
#define CHECK(c)                                                    \
    do {                                                            \
        checks++;                                                   \
        if (!(c)) {                                                 \
            failures++;                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
        }                                                           \
    } while (0)

// ---- FreeRTOS: one task, the test's

static jmp_buf s_parked;
static uint32_t s_bits;
static int s_ws_inits, s_ws_destroys, s_backoffs;
static char s_headers[1200];    // the last connection's
static bool s_up_on_start;      // the WebSocket comes up (its CONNECTED) as it starts
static uint32_t s_after_hello;  // set once the hello is sent: BIT_DOWN (the server closed it), BIT_REAUTH (session.reauth)

EventGroupHandle_t xEventGroupCreate(void) { return &s_bits; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits) {
    (void)group;
    return s_bits |= bits;
}
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits) {
    (void)group;
    const EventBits_t was = s_bits;
    s_bits &= ~bits;
    return was;
}
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits, BaseType_t clear, BaseType_t all, TickType_t wait) {
    (void)group, (void)all;
    const EventBits_t got = s_bits & bits;
    if (clear) s_bits &= ~got;
    if (!got && wait == portMAX_DELAY) longjmp(s_parked, 1);  // held, or up with nothing more: as far as this goes
    // A back-off ran its time: on to the next round, a few at most (one that never parks is a failure)
    if (!got && bits == BIT_KICK && ++s_backoffs > 3) longjmp(s_parked, 2);
    return got;
}
static int s_mutex;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &s_mutex; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait) { (void)s, (void)wait; return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) { (void)s; return pdTRUE; }
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg, unsigned priority,
                                   TaskHandle_t *created, int core) {
    (void)fn, (void)name, (void)stack, (void)arg, (void)priority, (void)core;
    *created = &s_mutex;  // the test runs session_task() itself
    return pdPASS;
}

// ---- the rest of ESP-IDF

const esp_event_base_t IP_EVENT = "IP_EVENT";
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *arg) {
    (void)base, (void)id, (void)handler, (void)arg;
    return ESP_OK;
}
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) { (void)key; return (esp_netif_t *)&s_mutex; }
uint8_t yk_wifi_last_reason(void) { return 0; }
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info) {
    (void)netif;
    info->ip.addr = 1;  // Wi-Fi has its address
    return ESP_OK;
}
esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd) { (void)cmd; return ESP_OK; }
esp_err_t esp_crt_bundle_attach(void *conf) { (void)conf; return ESP_OK; }
void mbedtls_ssl_conf_verify(mbedtls_ssl_config *conf, int (*f)(void *, mbedtls_x509_crt *, int, uint32_t *), void *p) {
    (void)conf, (void)f, (void)p;
}
int mbedtls_base64_encode(unsigned char *d, size_t n, size_t *o, const unsigned char *s, size_t m) { (void)d, (void)n, (void)o, (void)s, (void)m; return -1; }
int mbedtls_base64_decode(unsigned char *d, size_t n, size_t *o, const unsigned char *s, size_t m) { (void)d, (void)n, (void)o, (void)s, (void)m; return -1; }
int64_t esp_timer_get_time(void) { return 0; }
uint32_t esp_random(void) { return 0; }
const char *esp_err_to_name(esp_err_t err) { return err == ESP_OK ? "ESP_OK" : "ERR"; }
static const char GLOBAL_SESSION[] = "wss://device.yaykit.io/device/v1/session";
const char *yk_session_url(void) { return GLOBAL_SESSION; }

esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *config) {
    snprintf(s_headers, sizeof(s_headers), "%s", config->headers ? config->headers : "");
    s_ws_inits++;
    return (esp_websocket_client_handle_t)&s_mutex;
}
esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t c, int event, esp_event_handler_t handler, void *arg) {
    (void)c, (void)event, (void)handler, (void)arg;
    return ESP_OK;
}
esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t c) {
    (void)c;
    if (s_up_on_start) s_bits |= BIT_UP;
    return ESP_OK;
}
esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t c) { (void)c; return ESP_OK; }
esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t c) {
    (void)c;
    s_ws_destroys++;
    return ESP_OK;
}
int esp_websocket_client_send_text(esp_websocket_client_handle_t c, const char *d, int n, TickType_t t) {
    (void)c, (void)d, (void)t;
    s_bits |= s_after_hello;  // the hello is the first text sent
    s_after_hello = 0;
    return n;
}
int esp_websocket_client_send_bin(esp_websocket_client_handle_t c, const char *d, int n, TickType_t t) { (void)c, (void)d, (void)t; return n; }
esp_err_t esp_websocket_client_send_with_opcode(esp_websocket_client_handle_t c, int o, const uint8_t *d, int n, TickType_t t) {
    (void)c, (void)o, (void)d, (void)n, (void)t;
    return ESP_OK;
}

// ---- the account, and yaykit_talk's token on it (yaykit_talk.c's ACCOUNT and token(), as they are there)

static bool s_added;     // yk_account_state() is YK_ACCOUNT_BOUND
static int s_forgot_at;  // this request is answered DEVICE_NOT_ACTIVATED (the device removed in the app), 0 none
static int s_fail_at;    // this request does not get through (no network in its time; added all the same), 0 none
static int s_requests;   // token requests sent to the cloud (yk_account_token's HTTPS)
static int s_expired, s_not_added;

static esp_err_t account_token(char *out, size_t size, void *ctx) {
    (void)ctx;
    if (!s_added) return ESP_ERR_INVALID_STATE;  // yk_account_token: DEVICE_NOT_ACTIVATED from RAM, nothing sent
    s_requests++;  // (one answered from the cache is the same to the session: counted here as one sent)
    if (s_requests == s_forgot_at) {
        s_added = false;  // yk_account_unbound_by_server(): forget()
        return ESP_FAIL;
    }
    if (s_requests == s_fail_at) return ESP_FAIL;
    snprintf(out, size, "yk1.token");
    return ESP_OK;
}
static void account_expired(void *ctx) { (void)ctx, s_expired++; }
static bool account_added(void *ctx) { return (void)ctx, s_added; }
static void account_not_added(void *ctx) {
    (void)ctx;
    s_not_added++;
    yk_session_hold(true);  // as yaykit_talk.c's (test/run.sh checks that it does)
}
static const yk_talk_account_env_t ACCOUNT = {.token = account_token, .expired = account_expired,
                                              .added = account_added, .not_added = account_not_added};
static bool s_production;
static esp_err_t token(char *out, size_t size, bool fresh) {
    return yk_talk_account_token(&ACCOUNT, s_production, out, size, fresh);
}

static char *hello(void *ctx) {
    (void)ctx;
    return strdup("{\"type\":\"hello\"}");
}

static const char WITH_TOKEN[] = "Authorization: Bearer yk1.token\r\nX-Yaykit-Firmware: translator/0.1.2\r\n";
static const char WITHOUT[] = "X-Yaykit-Firmware: translator/0.1.2\r\n";

/** A new start: nothing held, nothing counted; the WebSocket comes up as it starts. */
static void fresh(bool production, bool added) {
    s_production = production, s_added = added;
    s_forgot_at = s_fail_at = 0;
    s_up_on_start = true;
    s_after_hello = 0;
    s_ws_inits = s_ws_destroys = s_backoffs = s_requests = s_expired = s_not_added = 0;
    s_headers[0] = 0;
    s_bits = 0;
    S.hold = false;
    S.state = YK_SESSION_IDLE;
    S.ws = NULL;
    S.attempt = 0;
}

/** The session's task from the top until it parks: 1 held, or up with nothing more; 2 it went on round after round. */
static int run(void) {
    const int why = setjmp(s_parked);
    if (!why) session_task(NULL);
    return why;
}

int main(void) {
    const yk_session_config_t cfg = {.url = GLOBAL_SESSION, .firmware = "translator/0.1.2", .token = token, .hello = hello};
    CHECK(yk_session_start(&cfg) == ESP_OK);

    // Not added, the production session (a device added in cn, seen by a build with CONFIG_YK_REGIONS off): not started
    // (yk_talk_start's question). And had it been, its first round holds before anything: no token asked, no WebSocket.
    fresh(true, false);
    CHECK(!yk_talk_may_start(true, false));
    CHECK(run() == 1);
    CHECK(s_requests == 0 && s_ws_inits == 0 && s_not_added == 1 && S.hold && S.state == YK_SESSION_DOWN);
    // Let go (the app's yk_talk_hold(false)): asked again, from RAM; held again, still nothing sent
    CHECK(yk_session_hold(false));
    CHECK(run() == 1);
    CHECK(s_requests == 0 && s_ws_inits == 0 && s_not_added == 2 && S.hold);

    // Added: as before. Its token, then one WebSocket with it, up; never held
    fresh(true, true);
    CHECK(yk_talk_may_start(true, true));
    CHECK(run() == 1);
    CHECK(s_requests == 1 && s_ws_inits == 1 && !strcmp(s_headers, WITH_TOKEN) && S.state == YK_SESSION_UP);
    CHECK(s_not_added == 0 && !S.hold && s_expired == 0);
    // ... session.reauth: a fresh token (the cached one dropped first), sent as `auth`; the connection stays
    fresh(true, true);
    s_after_hello = BIT_REAUTH;
    CHECK(run() == 1);
    CHECK(s_requests == 2 && s_expired == 1 && s_ws_inits == 1 && s_ws_destroys == 0 && S.state == YK_SESSION_UP);
    CHECK(s_not_added == 0 && !S.hold);
    // Added, but the token not got (no network in its time): it still dials without one, as it always has
    fresh(true, true);
    s_fail_at = 1;
    CHECK(run() == 1);
    CHECK(s_requests == 1 && s_ws_inits == 1 && !strcmp(s_headers, WITHOUT) && s_not_added == 0 && !S.hold);

    // Not added, a development server (`tr server ws://…`, `usb`): started, connects without a token, the account
    // never asked
    fresh(false, false);
    CHECK(yk_talk_may_start(false, false));
    CHECK(run() == 1);
    CHECK(s_requests == 0 && s_ws_inits == 1 && !strcmp(s_headers, WITHOUT) && s_not_added == 0 && !S.hold);
    CHECK(S.state == YK_SESSION_UP);

    // Not added since: the session went (a drop, or the token ran out: 4001), and the next connection's token is
    // answered DEVICE_NOT_ACTIVATED. Before, the next round dialled without a token (401), and so on every back-off (30 s
    // at most) for as long as the app ran. Now held: that request was the last thing sent.
    fresh(true, true);
    s_after_hello = BIT_DOWN;
    s_forgot_at = 2;
    CHECK(run() == 1);
    CHECK(s_ws_inits == 1 && s_ws_destroys == 1 && s_requests == 2 && s_not_added == 1 && S.hold && !s_added);
    CHECK(s_backoffs == 1 && S.state == YK_SESSION_DOWN);
    CHECK(yk_session_hold(false));  // let go: from RAM, held again
    CHECK(run() == 1);
    CHECK(s_ws_inits == 1 && s_requests == 2 && s_not_added == 2 && S.hold);
    // ... or at session.reauth while it is up: the connection closed, none again
    fresh(true, true);
    s_after_hello = BIT_REAUTH;
    s_forgot_at = 2;
    CHECK(run() == 1);
    CHECK(s_requests == 2 && s_expired == 1 && s_not_added == 1 && s_ws_inits == 1 && s_ws_destroys == 1);
    CHECK(S.hold && S.state == YK_SESSION_DOWN && !s_added && s_backoffs == 0);

    // The token itself: a development server's is "", nothing asked; production asks (after dropping the cached one
    // when fresh); a device not added is told so at each ask, an added one never
    char out[16] = "x";
    s_requests = s_expired = s_not_added = 0;
    s_added = false;
    CHECK(yk_talk_account_token(&ACCOUNT, false, out, sizeof(out), true) == ESP_OK && !out[0]);
    CHECK(s_requests == 0 && s_expired == 0 && s_not_added == 0);
    s_added = true;
    s_forgot_at = s_fail_at = 0;
    CHECK(yk_talk_account_token(&ACCOUNT, true, out, sizeof(out), true) == ESP_OK && !strcmp(out, "yk1.token"));
    CHECK(s_requests == 1 && s_expired == 1 && s_not_added == 0);
    S.hold = false;
    s_added = false;
    CHECK(yk_talk_account_token(&ACCOUNT, true, out, sizeof(out), false) != ESP_OK && s_not_added == 1 && S.hold);
    CHECK(yk_talk_may_start(false, true));

    printf("yaykit_talk account host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
