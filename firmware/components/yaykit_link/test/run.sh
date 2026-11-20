#!/usr/bin/env bash
# Host tests for the pure C parts of yaykit_link (no ESP-IDF build; cJSON comes from ESP-IDF: IDF_PATH, or
# ~/esp/esp-idf).
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O1 -DYK_MEM_HOST -I../include -o "$OUT/test_link" test_link.c ../yk_line.c ../yk_mem.c
"$OUT/test_link"
# The long mode's one-phone rule (yk_link_keep.h).
cc -std=c11 -Wall -Wextra -Werror -O1 -o "$OUT/test_bond" test_bond.c ../yk_link_bond.c
"$OUT/test_bond"
# The long and unpaired modes' flags, configurations and advertising (yk_link_rules.h).
cc -std=c11 -Wall -Wextra -Werror -O1 -I../include -Istub -o "$OUT/test_rules" test_rules.c ../yk_link_rules.c
"$OUT/test_rules"
# yk info's text a part at a time (yk_link_text.h, #1239).
CJSON="${IDF_PATH:-$HOME/esp/esp-idf}/components/json/cJSON"
cc -std=c11 -Wall -Wextra -Werror -O1 -I "$CJSON" -o "$OUT/test_text" test_text.c ../yk_link_text.c "$CJSON/cJSON.c" -lm
"$OUT/test_text"
# The unpaired mode's mark, handshake and envelopes (yk_link_seal.h) against the contract's fixtures, built with
# ESP-IDF's mbedTLS (packages/contracts/fixtures/device-link.json, #2080).
if [ -n "${IDF_PATH:-}" ] && [ -d "$IDF_PATH/components/mbedtls/mbedtls/library" ]; then
  MBED="$IDF_PATH/components/mbedtls/mbedtls"
  FIX=../../../../../packages/contracts/fixtures/device-link.json
  cc -std=c11 -Wall -Wextra -Werror -O1 -I "$MBED/include" -c ../yk_link_seal.c -o "$OUT/yk_link_seal.o"
  cc -std=c11 -Wall -Wextra -Werror -O1 -I "$MBED/include" -I "$CJSON" -c test_seal.c -o "$OUT/test_seal.o"
  cc -std=c11 -O1 -w -I "$MBED/include" -I "$CJSON" -o "$OUT/test_seal" "$OUT/yk_link_seal.o" "$OUT/test_seal.o" \
    "$CJSON/cJSON.c" "$MBED"/library/*.c -lm
  "$OUT/test_seal" "$FIX"
  # The unpaired mode's keys and static random addresses (yk_link_keys.c), NVS in memory: yk_link_seal.h wants
  # mbedTLS's headers (nothing of it is linked).
  cc -std=c11 -Wall -Wextra -Werror -O1 -I../include -Istub -I "$MBED/include" -I "$CJSON" -o "$OUT/test_keys" \
    test_keys.c ../yk_link_keys.c ../yk_link_rules.c
  "$OUT/test_keys"
else
  echo "test_seal, test_keys skipped: set IDF_PATH to build with ESP-IDF's mbedTLS"
fi
rm -rf "$OUT"
