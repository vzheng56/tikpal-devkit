// Host tests only (test_hold.c): yaykit_session.c itself on these; test_hold.c answers the calls.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
typedef struct esp_websocket_client *esp_websocket_client_handle_t;
#define WS_TRANSPORT_OPCODES_CLOSE 8
enum {
    WEBSOCKET_EVENT_ANY = -1,
    WEBSOCKET_EVENT_ERROR,
    WEBSOCKET_EVENT_CONNECTED,
    WEBSOCKET_EVENT_DISCONNECTED,
    WEBSOCKET_EVENT_DATA,
    WEBSOCKET_EVENT_CLOSED,
};
typedef enum {
    WEBSOCKET_ERROR_TYPE_NONE = 0,
    WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT,
    WEBSOCKET_ERROR_TYPE_PONG_TIMEOUT,
    WEBSOCKET_ERROR_TYPE_HANDSHAKE,
    WEBSOCKET_ERROR_TYPE_SERVER_CLOSE
} esp_websocket_error_type_t;
typedef struct {
    esp_err_t esp_tls_last_esp_err;
    esp_websocket_error_type_t error_type;
    int esp_ws_handshake_status_code;
} esp_websocket_error_codes_t;
typedef struct {
    const char *data_ptr;
    int data_len;
    bool fin;
    uint8_t op_code;
    int payload_len, payload_offset;
    esp_websocket_error_codes_t error_handle;
} esp_websocket_event_data_t;
typedef struct {
    const char *uri, *headers;
    esp_err_t (*crt_bundle_attach)(void *conf);
    int ping_interval_sec, pingpong_timeout_sec;
    bool disable_auto_reconnect;
    int network_timeout_ms, buffer_size, task_stack, task_prio;
} esp_websocket_client_config_t;
esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *config);
esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t client, int event, esp_event_handler_t handler, void *arg);
esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client);
esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t client);
esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t client);
int esp_websocket_client_send_text(esp_websocket_client_handle_t client, const char *data, int len, TickType_t timeout);
int esp_websocket_client_send_bin(esp_websocket_client_handle_t client, const char *data, int len, TickType_t timeout);
esp_err_t esp_websocket_client_send_with_opcode(esp_websocket_client_handle_t client, int opcode, const uint8_t *data, int len,
                                                TickType_t timeout);
