# The main firmware's session with the cloud (docs/design/yay-device.md 4): WebSocket over TLS, reconnects, and in
# development builds a USB bridge. yk_proto (the messages and frames), yk_conn_timing (the YAY_CONN line) and yk_ticket
# (the TLS ticket kept across restarts) are pure C, host tested (test/run.sh). yk_session_url.c picks the production
# session by the device's region (yaykit_cloud's yk_region.h, 两套部署): yaykit_cloud needs nothing of this component,
# and every firmware with a session has it already (Yay itself, the others through yaykit_talk). A development build's
# test deployment for a row is yaykit_cloud's own header (yk_region_test.h, not in its include/): hence its folder here.
idf_component_register(SRCS "yaykit_session.c" "yk_proto.c" "yk_conn_timing.c" "yk_ticket.c" "yk_conn_drop.c"
                            "yk_session_url.c"
    INCLUDE_DIRS "include" "."
    PRIV_INCLUDE_DIRS "../yaykit_cloud"
    REQUIRES json esp_common
    PRIV_REQUIRES espressif__esp_websocket_client esp-tls mbedtls console esp_timer esp_netif esp_event tcp_transport
                  esp_hw_support soc yaykit_cloud)
