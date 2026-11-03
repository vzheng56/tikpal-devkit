# The life of every Yay firmware: banner, image confirmation, launch parameters and results, going home,
# recovery, `app` and (development builds) `yay` console commands. Pure parts host tested (test/run.sh).
# P23: in an extension the double click opens yaykit_ui's app ring (yk_app_ring.c), and a hold the app did not use
# asks 「回到 Yay 说？」.
# P2: the recovery's version from its ELF sha256 (yk_recovery_logic.c, pure) and, for one not in the table, its image's
# sha256 (yk_recovery.c).
# 0.1.13: the device's settings for every app and an extension's battery guard (yk_app_power.c; the rules pure in
# yk_app_power_logic.c).
idf_component_register(SRCS "yaykit_app.c" "yk_app_logic.c" "yk_app_ring.c" "yk_recovery_logic.c" "yk_recovery.c"
                            "yk_app_power.c" "yk_app_power_logic.c"
    INCLUDE_DIRS "include"
    REQUIRES json esp_common yaykit_key
    PRIV_REQUIRES yaykit_link yaykit_ui yaykit_board lvgl__lvgl nvs_flash app_update console esp_system esp_timer
                  esp_psram bootloader_support esp_partition mbedtls esp_driver_gpio)
