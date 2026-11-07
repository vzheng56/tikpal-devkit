# The life of every Yay firmware: banner, image confirmation, launch parameters and results, going home,
# recovery, `app` and (development builds) `yay` console commands. Pure parts host tested (test/run.sh).
# P23: in an extension the double click opens yaykit_ui's app ring (yk_app_ring.c), and a hold the app did not use
# asks 「回到 Yay 说？」.
# P2: the recovery's version from its ELF sha256 (yk_recovery_logic.c, pure) and, for one not in the table, its image's
# sha256 (yk_recovery.c).
# 0.1.13: the device's settings for every app and an extension's battery guard (yk_app_power.c; the rules pure in
# yk_app_power_logic.c). 0.1.14: the system pull-down from an extension's top edge (yk_app_sheet.c; yaykit_ui's yk_sheet;
# yaykit_wifi, yaykit_cloud and yaykit_account linked weakly for its rows).
# 0.1.14: yaykit_voicecode in every firmware, so that any app with a number in its app.json has the pull-down's
# 「在手机上打开」 once rebuilt, its own CMakeLists unchanged (firmware/reproducible.cmake adds the directory). Its
# yk_vc_show_ring() and the others stay weak references in yk_app_ring.c: an image still decides by the number.
idf_component_register(SRCS "yaykit_app.c" "yk_app_logic.c" "yk_app_ring.c" "yk_recovery_logic.c" "yk_recovery.c"
                            "yk_app_power.c" "yk_app_power_logic.c" "yk_app_sheet.c"
    INCLUDE_DIRS "include"
    REQUIRES json esp_common yaykit_key
    PRIV_REQUIRES yaykit_link yaykit_ui yaykit_voicecode yaykit_board lvgl__lvgl nvs_flash app_update console esp_system
                  esp_timer esp_psram bootloader_support esp_partition mbedtls esp_driver_gpio
                  esp_driver_usb_serial_jtag)
