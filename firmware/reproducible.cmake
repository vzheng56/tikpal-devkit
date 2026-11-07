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

# yaykit_app links yaykit_voicecode (0.1.14), and ESP-IDF only finds components in EXTRA_COMPONENT_DIRS: added here, so
# that no project has to list it and an app from before keeps its CMakeLists. Not again for one that lists it. Only the
# projects that use yaykit_app build it: the directory alone (COMPONENTS main espcoredump) pulls nothing in.
if(NOT "${EXTRA_COMPONENT_DIRS}" MATCHES "/yaykit_voicecode(;|$)")
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components/yaykit_voicecode")
endif()
