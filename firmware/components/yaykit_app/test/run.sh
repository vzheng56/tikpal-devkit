#!/usr/bin/env bash
# Host tests for the pure parts of yaykit_app (no ESP-IDF needed).
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O1 -I../include -I../../yaykit_key/include -I../../yaykit_key -o "$OUT/test_app" \
  test_app.c ../yk_app_logic.c ../yk_recovery_logic.c ../yk_app_power_logic.c ../../yaykit_key/yk_key_sm.c
"$OUT/test_app"
# The folders' question's words (Yay 0.1.19 开发文档 4.6, the table's yaykit.folders.*), held to yay/test/text_width.h:
# a kit has no yay/, and goes without it.
if [ -f ../../../yay/test/text_width.h ]; then
  S=../../yaykit_strings
  cc -std=c11 -Wall -Wextra -Werror -O1 -I.. -I../../../yay/test -I"$S/include" -I"$S/generated" -o "$OUT/test_grant" \
    test_grant.c ../yk_app_grant_words.c "$S/yk_words.c" "$S/generated/yk_strings.c" -lm
  "$OUT/test_grant"
else
  echo "yk_app grant words: widths left out (no yay/test/text_width.h)"
fi
rm -rf "$OUT"
