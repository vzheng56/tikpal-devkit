# Playback: Opus or PCM into a 2 s PSRAM ring, the voice's loudness, the effects mixer (P13), the amplifier, fades,
# level, cues, volume.
# The pure C parts (yk_speak_dsp, yk_mix, yk_adpcm, yk_loud) are host tested with test/run.sh; yaykit_speak.c is the
# device side. The speaker's tuning is the board's (yaykit_board: board_speaker.h).
idf_component_register(SRCS "yaykit_speak.c" "yk_speak_dsp.c" "yk_mix.c" "yk_adpcm.c" "yk_loud.c"
    INCLUDE_DIRS "include" "."
    REQUIRES esp_common
    PRIV_REQUIRES yaykit_board espressif__esp_audio_codec espressif__esp_codec_dev esp_driver_gpio esp_timer console)
