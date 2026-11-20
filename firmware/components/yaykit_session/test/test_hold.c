// Host test for yk_session_hold() against a connection being made (yaykit_session.c itself, on test/stub): a hold that
// comes while the session's task is getting its token, in the seconds a fetch takes, ends with no WebSocket at all. It
// used to go on and dial, beside whatever held it: the phone link's Bluetooth and a WebSocket together are under the
// 40,000 bytes line (11-06: 32.4 KB on `yk ble on`; 11-07: 32,003 at the first report after Wi-Fi was set up). The task
// itself runs here, until it is parked: waiting with no end, as it does while held.
#include <setjmp.h>

#include "../yaykit_session.c"

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
static int s_ws_inits, s_ws_starts, s_ws_stops, s_ws_destroys, s_tokens;
static int s_waited_out;  // a wait for the connection that ran its time, nothing having come
static bool s_address = true, s_hold_at_token, s_hold_flag_only, s_token_fails;

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
    // Nothing set and no end to the wait (held), or the back-off after a connection that was made: as far as this goes.
    if (!got && (wait == portMAX_DELAY || (bits == BIT_KICK && s_ws_destroys))) longjmp(s_parked, 1);
    if (!got && (bits & BIT_UP)) s_waited_out++;
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
uint8_t yk_wifi_last_reason(void) { return 0; }  // yaykit_wifi's, linked weakly on the device
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info) {
    (void)netif;
    info->ip.addr = s_address ? 1 : 0;
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

esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *config) {
    (void)config;
    s_ws_inits++;
    return (esp_websocket_client_handle_t)&s_mutex;
}
esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t c, int event, esp_event_handler_t handler, void *arg) {
    (void)c, (void)event, (void)handler, (void)arg;
    return ESP_OK;
}
esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t c) {
    (void)c;
    s_ws_starts++;
    return ESP_OK;
}
esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t c) {
    (void)c;
    s_ws_stops++;
    return ESP_OK;
}
esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t c) {
    (void)c;
    s_ws_destroys++;
    return ESP_OK;
}
int esp_websocket_client_send_text(esp_websocket_client_handle_t c, const char *d, int n, TickType_t t) { (void)c, (void)d, (void)t; return n; }
int esp_websocket_client_send_bin(esp_websocket_client_handle_t c, const char *d, int n, TickType_t t) { (void)c, (void)d, (void)t; return n; }
esp_err_t esp_websocket_client_send_with_opcode(esp_websocket_client_handle_t c, int o, const uint8_t *d, int n, TickType_t t) {
    (void)c, (void)o, (void)d, (void)n, (void)t;
    return ESP_OK;
}

// ---- the cases

/** The firmware's token: a hold may come while it is fetched; it comes back with one, or without (no Wi-Fi in its time). */
static esp_err_t token(char *out, size_t size, bool fresh) {
    (void)fresh;
    s_tokens++;
    if (s_hold_at_token) yk_session_hold(true);
    if (s_hold_flag_only) S.hold = true;  // yk_session_hold() on the other core, its BIT_DOWN not set yet
    if (s_token_fails) return ESP_FAIL;
    snprintf(out, size, "yk1.token");
    return ESP_OK;
}
static char *hello(void *ctx) { (void)ctx; return NULL; }

static void run(bool hold_at_token, bool token_fails) {
    s_hold_at_token = hold_at_token, s_token_fails = token_fails;
    s_ws_inits = s_ws_starts = s_ws_stops = s_ws_destroys = s_tokens = s_waited_out = 0;
    s_bits = 0;
    S.hold = false;
    S.state = YK_SESSION_IDLE;
    S.ws = NULL;
    if (!setjmp(s_parked)) session_task(NULL);
}

int main(void) {
    const yk_session_config_t cfg = {.url = "ws://device.test/session", .firmware = "0.1.17", .token = token, .hello = hello};
    CHECK(yk_session_start(&cfg) == ESP_OK);

    // Nothing in the way: the token, then the WebSocket
    run(false, false);
    CHECK(s_tokens == 1 && s_ws_inits == 1 && s_ws_starts == 1 && s_ws_stops == 1 && s_ws_destroys == 1);
    // Offline, no token to be had: it still dials, as it always has (its failure is the retry)
    run(false, true);
    CHECK(s_tokens == 1 && s_ws_inits == 1 && s_ws_starts == 1);

    // Held while the token came: no WebSocket, down at once (the link's start waits for that), parked until let go
    run(true, false);
    CHECK(s_tokens == 1 && s_ws_inits == 0 && s_ws_starts == 0);
    CHECK(S.state == YK_SESSION_DOWN && S.hold && !S.ws);
    // The same with no token: the 20 s wait for Wi-Fi that ran out beside the phone link
    run(true, true);
    CHECK(s_tokens == 1 && s_ws_inits == 0 && s_ws_starts == 0 && S.state == YK_SESSION_DOWN);

    // The hold seen before its own BIT_DOWN: down at once, not after the connection's 15 s
    s_hold_flag_only = true;
    run(false, false);
    s_hold_flag_only = false;
    CHECK(s_ws_inits == 0 && s_waited_out == 0 && S.state == YK_SESSION_DOWN);

    // Let go: the next round connects as ever
    s_hold_at_token = false, s_token_fails = false;
    s_ws_inits = s_ws_starts = s_ws_stops = s_ws_destroys = s_tokens = 0;
    CHECK(yk_session_hold(false));
    s_bits = 0;  // the kick that woke it
    if (!setjmp(s_parked)) session_task(NULL);
    CHECK(s_tokens == 1 && s_ws_inits == 1 && s_ws_starts == 1 && !S.hold);

    // Held before it even began: no token asked either
    run(false, false);
    s_tokens = s_ws_inits = 0;
    s_bits = 0;
    S.state = YK_SESSION_DOWN;
    CHECK(yk_session_hold(true));
    s_bits = 0;
    if (!setjmp(s_parked)) session_task(NULL);
    CHECK(s_tokens == 0 && s_ws_inits == 0);

    printf("yaykit_session hold host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
