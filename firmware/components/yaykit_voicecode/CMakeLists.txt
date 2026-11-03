# The firmware draws codes (the encoder) and may check one (the decoder): about 3 KB, no tables kept in RAM.
# The camera reader (yk_voicecode_scan.c) is for the apps and host tests: it needs megabytes per frame.
idf_component_register(SRCS "yk_voicecode.c" "yk_voicecode_decode.c"
    INCLUDE_DIRS "include")
