// Host test for yk_http_prepare (yk_http.c itself, on test/stub): what stands between a request and its TLS. Bluetooth on as
// the request comes: the phone has had its turn, it is stopped. Bluetooth on after that, during the wait for Wi-Fi (up to
// 20 s): that phone has not, and it is there to set Wi-Fi up, so nothing goes out and the link is left alone. Bluetooth
// and a TLS connection together do not fit in internal RAM (11-07: 32,003 bytes after Wi-Fi was set up over the link).
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "yaykit_link.h"
#include "yaykit_wifi.h"
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

static bool s_ble, s_ble_comes, s_wifi;
static int s_stops, s_waits;
static uint32_t s_waited_ms;

bool yk_link_ble_active(void) { return s_ble; }
esp_err_t yk_link_ble_stop(void) {
    s_ble = false;
    s_stops++;
    return ESP_OK;
}
bool yk_wifi_wait_connected(uint32_t timeout_ms) {
    s_waits++;
    s_waited_ms = timeout_ms;
    if (s_ble_comes) s_ble = true;  // the phone link's start, in another task, while this one waits
    return s_wifi;
}

// The client itself: nothing here reaches it.
static int s_rx, s_tx;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    s_rx = config->buffer_size, s_tx = config->buffer_size_tx;
    return NULL;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int len) { (void)client, (void)len; return ESP_FAIL; }
int esp_http_client_write(esp_http_client_handle_t client, const char *b, int len) { (void)client, (void)b, (void)len; return -1; }
int esp_http_client_fetch_headers(esp_http_client_handle_t client) { (void)client; return -1; }
int esp_http_client_get_status_code(esp_http_client_handle_t client) { (void)client; return 0; }
int esp_http_client_read(esp_http_client_handle_t client, char *b, int len) { (void)client, (void)b, (void)len; return 0; }
esp_err_t esp_http_client_close(esp_http_client_handle_t client) { (void)client; return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) { (void)client; return ESP_OK; }
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *k, const char *v) { (void)client, (void)k, (void)v; return ESP_OK; }
esp_err_t esp_crt_bundle_attach(void *conf) { (void)conf; return ESP_OK; }
const char *esp_err_to_name(esp_err_t err) { return err == ESP_OK ? "ESP_OK" : "ERR"; }

static esp_err_t prepare(bool ble, bool ble_comes, bool wifi, const char **code) {
    s_ble = ble, s_ble_comes = ble_comes, s_wifi = wifi;
    s_stops = s_waits = 0;
    s_waited_ms = 0;
    *code = NULL;
    return yk_http_prepare(code);
}

int main(void) {
    const char *code;
    // Nothing in the way
    CHECK(prepare(false, false, true, &code) == ESP_OK && !code && s_stops == 0 && s_waits == 1 && s_waited_ms == 20000);
    // Bluetooth on as it comes: stopped, then Wi-Fi, then it goes
    CHECK(prepare(true, false, true, &code) == ESP_OK && !code && s_stops == 1 && !s_ble);
    // No Wi-Fi in its time
    CHECK(prepare(false, false, false, &code) == ESP_ERR_TIMEOUT && !strcmp(code, "NO_WIFI") && s_stops == 0);
    // Bluetooth on during the wait, and Wi-Fi came (the phone set it up): not sent, the link left alone
    CHECK(prepare(false, true, true, &code) == ESP_ERR_INVALID_STATE && !strcmp(code, "LINK"));
    CHECK(s_stops == 0 && s_ble && s_waits == 1);
    // The same, and no Wi-Fi: what it was before, the link left alone as well
    CHECK(prepare(false, true, false, &code) == ESP_ERR_TIMEOUT && !strcmp(code, "NO_WIFI") && s_stops == 0 && s_ble);
    // Bluetooth stopped and on again during the wait: this phone is another turn
    CHECK(prepare(true, true, true, &code) == ESP_ERR_INVALID_STATE && !strcmp(code, "LINK") && s_stops == 1 && s_ble);
    // Callers that do not ask why
    s_ble = false, s_ble_comes = true, s_wifi = true;
    CHECK(yk_http_prepare(NULL) == ESP_ERR_INVALID_STATE);
    s_ble = false, s_ble_comes = false, s_wifi = false;
    CHECK(yk_http_prepare(NULL) == ESP_ERR_TIMEOUT);

    // The client's buffers: as written, or the firmware's smallest (above its malloc limit they are in PSRAM)
    CHECK(!yk_http_client("/device/v1/status", HTTP_METHOD_POST, NULL, NULL, 1000));
#if CONFIG_YK_CLOUD_HTTP_BUFFER_MIN
    CHECK(s_rx == CONFIG_YK_CLOUD_HTTP_BUFFER_MIN && s_tx == CONFIG_YK_CLOUD_HTTP_BUFFER_MIN);
#else
    CHECK(s_rx == 2048 && s_tx == 1536);
#endif

    printf("yaykit_cloud http host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
