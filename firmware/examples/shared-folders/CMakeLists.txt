cmake_minimum_required(VERSION 3.16)
# Shared folders example (development kit 1.10, docs/app-guide.md 「存储卡和共享文件夹」): a tap asks to read Music, lists
# it, reads the start of the first song, and writes one file into the app's own directory on the card. It is to read and
# build, not a template: start an app with `./kit new` and take what it needs from main/main.c.
if(NOT DEFINED PROJECT_VER)
    set(PROJECT_VER "0.1.0")
endif()
# The shared firmware directory: in this repository and in the kit the example sits in firmware/examples/<name>; copied
# into an app's own repository, the kit is its devkit/ submodule.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/devkit/firmware/components")
    set(YAYKIT_FIRMWARE "${CMAKE_CURRENT_LIST_DIR}/devkit/firmware")
else()
    set(YAYKIT_FIRMWARE "${CMAKE_CURRENT_LIST_DIR}/../..")
endif()
set(EXTRA_COMPONENT_DIRS
    "${YAYKIT_FIRMWARE}/components/yaykit_app"
    "${YAYKIT_FIRMWARE}/components/yaykit_board"
    "${YAYKIT_FIRMWARE}/components/yaykit_key"
    "${YAYKIT_FIRMWARE}/components/yaykit_link"
    "${YAYKIT_FIRMWARE}/components/yaykit_tf"
    "${YAYKIT_FIRMWARE}/components/yaykit_ui"
    "${YAYKIT_FIRMWARE}/components/yaykit_voicecode")
set(COMPONENTS main espcoredump)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${YAYKIT_FIRMWARE}/reproducible.cmake)
project(yaykit_shared_folders)
