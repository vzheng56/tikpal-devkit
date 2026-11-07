#!/usr/bin/env bash
# Host tests for the pure C parts of yaykit_link (no ESP-IDF build; cJSON comes from ESP-IDF: IDF_PATH, or
# ~/esp/esp-idf).
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
cc -std=c11 -Wall -Wextra -Werror -O1 -DYK_MEM_HOST -I../include -o "$OUT/test_link" test_link.c ../yk_line.c ../yk_mem.c
"$OUT/test_link"
# The long mode's one-phone rule (yk_link_keep.h).
cc -std=c11 -Wall -Wextra -Werror -O1 -o "$OUT/test_bond" test_bond.c ../yk_link_bond.c
"$OUT/test_bond"
# yk info's text a part at a time (yk_link_text.h, #1239).
CJSON="${IDF_PATH:-$HOME/esp/esp-idf}/components/json/cJSON"
cc -std=c11 -Wall -Wextra -Werror -O1 -I "$CJSON" -o "$OUT/test_text" test_text.c ../yk_link_text.c "$CJSON/cJSON.c" -lm
"$OUT/test_text"
rm -rf "$OUT"
