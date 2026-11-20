# The TF card as shared infrastructure (docs/delivery/2026-11-06-固件公共能力.md section 3): held by count, an app's own
# directory, and the system's store of public packages. A component of its own, not in yaykit_board, so that only the
# firmware that uses the card links sdmmc and FatFs. The pure parts are host tested (test/run.sh).
#
# Two halves (Yay 0.1.19 开发文档 6.1 step 3): what every firmware with the card compiles (the card, the clean-up after a
# power cut, the space an app takes), and the system's (sys_include/: the store of app images and 形象 packs, the
# index in NVS), only with CONFIG_YK_TF_SYSTEM. The firmware that has it puts sys_include/ on its own include path.
# The shared folders (yk_folder.h, 开发文档 4.6) are every firmware's: an app reads them. Their grant is the device
# service's too (2.1, 3.1), whose calls yaykit_cloud hands over as it starts (yk_folder_net_set): only an app that uses
# the cloud asks; yaykit_tf needs nothing of it. The system's half keeps every app's record in line (yk_folder_sys.c).
set(YK_TF_SRCS "yaykit_tf.c" "yk_tf_logic.c" "yk_tf_fat.c" "yk_folder.c" "yk_folder_logic.c" "yk_folder_read.c"
    "yk_folder_nvs.c")
set(YK_TF_PRIV_INCLUDE ".")
if(CONFIG_YK_TF_SYSTEM)
    # A kit has none of it (devkit-check.ts EXCLUDE): say so, rather than a missing file.
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/sys_include/yk_tf_sys.h" OR NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/yk_tf_pkg.c"
       OR NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/yk_folder_sys.c")
        message(FATAL_ERROR "yaykit_tf: CONFIG_YK_TF_SYSTEM is the system's own (Yay, shared-test): this copy has no "
                            "system side (sys_include/, yk_tf_sys.c, yk_tf_pkg.c, yk_folder_sys.c), and an app never "
                            "turns it on")
    endif()
    list(APPEND YK_TF_SRCS "yk_tf_sys.c" "yk_tf_pkg.c" "yk_folder_sys.c" "yk_tf_layout.c" "yk_tf_lib.c")
    list(APPEND YK_TF_PRIV_INCLUDE "sys_include")
endif()
idf_component_register(SRCS ${YK_TF_SRCS}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS ${YK_TF_PRIV_INCLUDE}
    REQUIRES esp_common mbedtls
    PRIV_REQUIRES esp_driver_sdmmc sdmmc fatfs vfs esp_timer esp_system esp_app_format heap hal soc nvs_flash)

# The kinds the project's app.json asks for, into yk_folder.c (folders.cmake); CMake configures again when it changes.
include(${CMAKE_CURRENT_LIST_DIR}/folders.cmake)
idf_build_get_property(project_dir PROJECT_DIR)
yk_folder_declared("${project_dir}/app.json" "${YK_FOLDER_KINDS}" yk_folder_mask)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${project_dir}/app.json")
set_source_files_properties(yk_folder.c PROPERTIES COMPILE_DEFINITIONS "YK_FOLDER_DECLARED=${yk_folder_mask}")
message(STATUS "yaykit_tf: shared folders asked for ${yk_folder_mask}")
