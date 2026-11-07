# The firmware draws codes (the encoder) and may check one (the decoder): about 3 KB, no tables kept in RAM.
# The camera reader (yk_voicecode_scan.c) is for the apps and host tests: it needs megabytes per frame.
# The round screen's part (11-01, handoff 2026-11-01 2.1①): the code drawn and talking (yk_vc_view.c, LVGL; its pure
# parts in yk_vc_view_logic.c, host tested) and yk_vc_show(), an app's own 应用码, its number from the project's
# app.json at build time (voicecode_number.cmake); the pairing page's code (2.1③, yk_vc_pair_layer). Every firmware with
# yaykit_app builds it since 0.1.14 (yaykit_app requires it; firmware/reproducible.cmake adds this directory).
idf_component_register(SRCS "yk_voicecode.c" "yk_voicecode_decode.c" "yk_vc_view_logic.c" "yk_vc_view.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl__lvgl esp_common yaykit_key
    PRIV_REQUIRES yaykit_ui yaykit_board esp_timer esp_hw_support heap)

include(${CMAKE_CURRENT_LIST_DIR}/voicecode_number.cmake)
idf_build_get_property(project_dir PROJECT_DIR)
yk_vc_number("${project_dir}/app.json" "${YK_VC_APP_NUMBER}" yk_vc_app_number)
# A new number in app.json builds a new image: CMake configures again when app.json changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${project_dir}/app.json")
if(yk_vc_app_number)
  set_source_files_properties(yk_vc_view_logic.c PROPERTIES COMPILE_DEFINITIONS "YK_VC_APP_NUMBER=${yk_vc_app_number}")
endif()
# The app ring's 「在手机上打开」 (handoff 2.1②): yaykit_app links yk_vc_show_ring() and yk_vc_app_number() weakly; -u keeps
# them in every image, even when the app itself never shows a code. The pairing page (2.1③) likewise: yaykit_ui's
# yk_ui_pair_content() links yk_vc_pair_layer() weakly and shows the voice code where it is, the QR code where it is not.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u yk_vc_show_ring" "-u yk_vc_app_number" "-u yk_vc_hide"
    "-u yk_vc_pair_layer")
# What is built in, beside the image, for pack-firmware.ts to hold against app.json (empty: none).
idf_build_get_property(build_dir BUILD_DIR)
file(WRITE "${build_dir}/yk_voice_code.txt" "${yk_vc_app_number}\n")
message(STATUS "yaykit_voicecode: app number ${yk_vc_app_number}")
