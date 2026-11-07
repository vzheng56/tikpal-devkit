#!/usr/bin/env bash
# Host tests for the pure parts of yaykit_speak (no ESP-IDF needed).
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
cc -std=c11 -Wall -Wextra -Werror -O1 -I.. -o "$OUT/test_speak" test_speak.c ../yk_speak_dsp.c -lm
"$OUT/test_speak"
cc -std=c11 -Wall -Wextra -Werror -O1 -I.. -I../include -Istub -o "$OUT/test_mix" test_mix.c ../yk_mix.c ../yk_adpcm.c -lm
"$OUT/test_mix"
cc -std=c11 -Wall -Wextra -Werror -O1 -I.. -o "$OUT/test_loud" test_loud.c ../yk_loud.c -lm
"$OUT/test_loud"
# The real replies the volume table was solved on stay out of the development kit (scripts/devkit-check.ts, EXCLUDE).
if [ -f replies/t1.wav ] && [ -f replies/t2.wav ] && [ -f replies/t3.wav ] && [ -f replies/t4.wav ]; then
  cc -std=c11 -Wall -Wextra -Werror -O1 -I.. -I../../yaykit_board/include -o "$OUT/test_steps" test_steps.c ../yk_loud.c \
    ../yk_speak_dsp.c ../../yaykit_board/board_speaker.c -lm
  "$OUT/test_steps"
else
  echo "test_steps skipped: no replies/ here (real replies, not in the development kit)"
fi
rm -rf "$OUT"
