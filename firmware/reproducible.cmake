# Same commit, same image, in any checkout. CONFIG_APP_REPRODUCIBLE_BUILD (on in every project) only remaps the paths
# of ESP-IDF, the project, the build directory and, in the debug info, each component directory. The shared
# components (firmware/components) and Yay's art (firmware/assets, compiled into yaykit_ui) sit outside the project,
# so their absolute paths stayed in the image: __FILE__ in assert and ESP_ERROR_CHECK messages, and the debug info of
# yay_art.c, which changes the ELF hash the image header records. This maps the whole firmware directory to /yaykit
# (an assert shows /yaykit/components/yaykit_ui/yaykit_ui.c). ESP-IDF's own maps come later on the command line and
# GCC takes the last match, so they still win where they apply (a project's files stay ./main/…).
# Every project includes this between include(project.cmake) and project().
idf_build_set_property(COMPILE_OPTIONS "-fmacro-prefix-map=${CMAKE_CURRENT_LIST_DIR}=/yaykit" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-fdebug-prefix-map=${CMAKE_CURRENT_LIST_DIR}=/yaykit" APPEND)

# A checkout reached through a symbolic link (macOS's /tmp is /private/tmp, Yay 0.1.18): GCC writes each object's compile
# directory (DW_AT_comp_dir) as getcwd() gives it, the real path, while every map here and ESP-IDF's name the paths as
# CMake has them. So the build directory's real path stayed in 852 compile units of the ELF, and through the ELF's hash in
# the image's header: a checkout under /tmp made another image. The same maps again for the real paths, only where they
# differ (a usual checkout's command line stays as it was), the most specific last: GCC takes the last that matches.
function(yaykit_map_real_path path to)
    if(NOT path)
        return()
    endif()
    file(REAL_PATH "${path}" real)
    if(NOT real STREQUAL path)
        idf_build_set_property(COMPILE_OPTIONS "-fdebug-prefix-map=${real}=${to}" APPEND)
    endif()
endfunction()
yaykit_map_real_path("$ENV{IDF_PATH}" /IDF)
yaykit_map_real_path("${CMAKE_CURRENT_LIST_DIR}" /yaykit)
yaykit_map_real_path("${CMAKE_SOURCE_DIR}" /IDF_PROJECT)
yaykit_map_real_path("${CMAKE_BINARY_DIR}" /IDF_BUILD)

# yaykit_app links yaykit_voicecode (0.1.14), and ESP-IDF only finds components in EXTRA_COMPONENT_DIRS: added here, so
# that no project has to list it and an app from before keeps its CMakeLists. Not again for one that lists it. Only the
# projects that use yaykit_app build it: the directory alone (COMPONENTS main espcoredump) pulls nothing in.
if(NOT "${EXTRA_COMPONENT_DIRS}" MATCHES "/yaykit_voicecode(;|$)")
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components/yaykit_voicecode")
endif()

# yaykit_ui, yaykit_app and yaykit_install take their words from yaykit_strings (Yay 0.1.19 开发文档 4.7: four
# languages, one table): added here as yaykit_voicecode is, so that no project has to list it. Only the projects that
# use those build it.
if(NOT "${EXTRA_COMPONENT_DIRS}" MATCHES "/yaykit_strings(;|$)")
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components/yaykit_strings")
endif()
