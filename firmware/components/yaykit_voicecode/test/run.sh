#!/usr/bin/env bash
# Host tests for yaykit_voicecode, no ESP-IDF build:
#  - test_fixtures.c: every example in packages/contracts/fixtures/voice-code.json, the contract's own. cJSON comes
#    from ESP-IDF (IDF_PATH, or ~/esp/esp-idf); in CI a missing cJSON is a failure, not a skip.
#  - test_voicecode.c: bits, Reed-Solomon, and yk_vc_scan on rendered camera frames. Plain C99 + libm (+ pthreads for
#    the test's own parallel rendering). `--quick` skips the frames.
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
SRC=(../yk_voicecode.c ../yk_voicecode_decode.c ../yk_voicecode_scan.c)

IDF="${IDF_PATH:-$HOME/esp/esp-idf}"
CJSON="$IDF/components/json/cJSON"
if [ -f "$CJSON/cJSON.c" ]; then
  cc -std=c99 -Wall -Wextra -Werror -O1 -I ../include -I "$CJSON" -o "$OUT/test_fixtures" test_fixtures.c \
    "${SRC[@]}" "$CJSON/cJSON.c" -lm
  "$OUT/test_fixtures" "$(cd ../../../../.. && pwd)/packages/contracts/fixtures/voice-code.json"
elif [ -n "${CI:-}" ]; then
  echo "yaykit_voicecode: no ESP-IDF for cJSON (set IDF_PATH); the contract's examples were not checked" >&2
  exit 1
else
  echo "yaykit_voicecode contract examples skipped: no ESP-IDF for cJSON (set IDF_PATH)"
fi

cc -std=c99 -Wall -Wextra -Werror -O2 -I ../include -I .. -o "$OUT/test_voicecode" "${SRC[@]}" test_voicecode.c -lm -lpthread
"$OUT/test_voicecode" "$@"
