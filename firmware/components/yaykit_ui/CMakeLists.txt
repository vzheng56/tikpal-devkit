# Shared device UI for Yaykit firmware on the Tikpal board: labels and buttons in the
# Yaykit style, the "Connect phone" pairing page and the `snap` frame capture command; v2 (P11): the round-screen
# spec's rim, Yay, notices, transitions and subtitles (pure parts in yk_ui_logic.c, host tested).
# Yay's art (assets/yay) is embedded here; only firmware that draws Yay (yk_yay) pulls it in at link time. Likewise the
# pairing page (yk_ui_pair.c) is the only part that uses Yaykit Link's Bluetooth and LVGL's QR code: firmware without
# Bluetooth (Party Play, Breathe) can use the rest.
# P23/P24: the app ring (yk_apps), the app catalog cache it reads (yk_catalog, in `userfs`) and the hint arrow
# (yk_hint, counts in NVS `yk_hint`); pure parts in yk_apps_logic.c, host tested. App ecosystem stage 1: 「我的应用」
# (yk_more, Yay's page behind the ring's 「更多」); pure parts in yk_more_logic.c, host tested. The system pull-down
# (yk_sheet, docs/delivery/2026-11-02-system-pulldown.md): the status sheet every firmware shares. A buddy's picture pack
# (yk_figure, host tested): read here, hung on one Yay at a time with yk_yay_figure(); Yay's home screen is the only one.
# The settings ring and its dial (yk_ring, round-screen-gestures.md 6, 9): pure parts in yk_ring_logic.c, host tested.
# The buddies' packs (Yay 0.1.18 4.9): the slots' head (yk_buddy_slot.c, pure, host tested) and the read-only fetch of a
# pack, slot then card then none (yk_buddy.c); which buddy a Yay is, the user's by default (yk_yay_buddy.c, kit 1.10). The card through a function the firmware registers, not yaykit_tf: most
# firmwares here have no card, and yaykit_tf stops a build without its FatFs settings.
include(${CMAKE_CURRENT_LIST_DIR}/../../assets/yay/yay_art.cmake)
# Pip's pictures (Kconfig YK_UI_PIP_BUILTIN, on unless a firmware turns it off): all of them in the image, or greet_132
# alone and the rest from Pip's pack at run time (yk_yay_pip; Yay from 0.1.18, its 开发文档 4.2).
if(CONFIG_YK_UI_PIP_BUILTIN OR NOT DEFINED CONFIG_YK_UI_PIP_BUILTIN)
    set(YK_UI_ART_EMBED_FILES ${YAY_ART_EMBED_FILES})
else()
    set(YK_UI_ART_EMBED_FILES ${YAY_ART_FALLBACK_EMBED_FILES})
endif()
idf_component_register(SRCS "yaykit_ui.c" "yk_ui_pair.c" "yk_ui_logic.c" "yk_rim.c" "yk_yay.c" "yk_notice.c" "yk_caption.c"
                            "yk_transition.c" "yk_apps_logic.c" "yk_apps.c" "yk_catalog.c" "yk_hint.c" "yk_more_logic.c" "yk_more.c" "yk_mark.c" "yk_mark_fit.c"
                            "yk_sheet.c" "yk_sheet_logic.c" "yk_figure.c" "yk_ring.c" "yk_ring_logic.c" "yk_buddy_slot.c" "yk_buddy.c"
                            "yk_yay_buddy.c" "yk_accent.c"
                            ${YAY_ART_SRCS}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS ${YAY_ART_INCLUDE_DIRS} "generated"
    EMBED_FILES ${YK_UI_ART_EMBED_FILES}
    REQUIRES lvgl__lvgl
    PRIV_REQUIRES yaykit_board yaykit_link yaykit_strings console mbedtls esp_psram esp_partition esp_timer nvs_flash)
if(DEFINED CONFIG_YK_UI_PIP_BUILTIN AND NOT CONFIG_YK_UI_PIP_BUILTIN)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE YAY_ART_PACKED)
endif()
# Every Yay is the user's buddy (Kconfig YK_UI_BUDDY_DEFAULT, on unless a firmware turns it off; development kit 1.10):
# yk_yay_create() hangs the current buddy's pack (yk_yay_buddy.c). Yay hangs its packs itself and turns it off.
if(CONFIG_YK_UI_BUDDY_DEFAULT OR NOT DEFINED CONFIG_YK_UI_BUDDY_DEFAULT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE YK_YAY_BUDDY_DEFAULT)
endif()
# The Tikpal mark's size for a trial build (`build-yay.sh YK_MARK_PX=88`), passed on to whatever draws with yaykit_ui;
# yk_mark_fit.h has the size the pages use otherwise.
if(YK_MARK_PX)
  target_compile_definitions(${COMPONENT_LIB} PUBLIC "YK_MARK_PX=${YK_MARK_PX}")
endif()
