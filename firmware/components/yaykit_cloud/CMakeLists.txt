# 0.1.19: the shared folders' grants at the device service (yk_folder_net.c), for yaykit_tf's yk_folder.c, which calls
# it by name; its yk_grant_t is yaykit_tf's yk_folder_logic.h (the header alone: yaykit_tf is not required here).
idf_component_register(SRCS "yaykit_account.c" "yaykit_cloud.c" "yk_http.c" "yk_codec.c" "yk_sse.c" "yk_sign.c" "yk_api.c" "yk_locale.c"
    "yk_api_stream.c" "yk_stream.c" "yk_folder_net.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../yaykit_tf"
    REQUIRES json esp_http_client
    PRIV_REQUIRES mbedtls nvs_flash esp_timer console esp_hw_support esp_psram yaykit_link yaykit_wifi)
