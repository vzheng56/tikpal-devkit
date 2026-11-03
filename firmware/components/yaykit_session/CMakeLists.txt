# The main firmware's session with the cloud (docs/design/yay-device.md 4): WebSocket over TLS, reconnects, and in
# development builds a USB bridge. yk_proto (the messages and frames), yk_conn_timing (the YAY_CONN line) and yk_ticket
# (the TLS ticket kept across restarts) are pure C, host tested (test/run.sh).
idf_component_register(SRCS "yaykit_session.c" "yk_proto.c" "yk_conn_timing.c" "yk_ticket.c"
    INCLUDE_DIRS "include" "."
    REQUIRES json esp_common
    PRIV_REQUIRES espressif__esp_websocket_client esp-tls mbedtls console esp_timer esp_netif esp_event tcp_transport
                  esp_hw_support soc)
