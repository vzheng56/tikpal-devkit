# The unpaired mode's link keys (yk_link_keys.c, yk_link_seal.c) in every firmware: whichever is running when the
# device leaves its account forgets them (yk_link_keys_forget(), from yaykit_cloud). yk_link_rules.c (pure C) with
# them: the keys' file makes the addresses by its rules.
set(srcs "yaykit_link.c" "yaykit_slots.c" "yk_line.c" "yk_srp_salt.c" "yk_mem.c" "yk_link_bond.c" "yk_link_text.c"
         "yk_link_keys.c" "yk_link_seal.c" "yk_link_rules.c")
# The long and unpaired modes (yk_link_keep.h) talk to NimBLE themselves: only firmware with NimBLE on builds them
# (Party Play has no Bluetooth at all). Firmware that never starts either (Yay) links none of it.
if(CONFIG_BT_NIMBLE_ENABLED)
    list(APPEND srcs "yk_link_keep.c")
endif()
idf_component_register(SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES json
    PRIV_REQUIRES app_update protocomm wifi_provisioning bt console esp_timer esp_event mbedtls esp_psram spi_flash esp_hw_support
                  heap nvs_flash)
