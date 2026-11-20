# The vibration motor on GPIO46 (docs/delivery/2026-11-13-本事和规矩-振动和姿态.md 1 and 3): five kinds and an app's own
# pattern at the user's strength, through LEDC and one esp_timer, no task of its own. The scheduling (yk_haptic_core.c)
# is pure C, host tested with test/run.sh; yaykit_haptic.c is the device side. What keeps it still (the microphones, an
# install, the battery guard) is read from yaykit_audio, yaykit_voice, yaykit_install and yaykit_app through weak
# references: an app links none of them for this. Not in the development kit yet (devkit-check.ts EXCLUDE).
idf_component_register(SRCS "yk_haptic_core.c" "yaykit_haptic.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_timer nvs_flash console)
