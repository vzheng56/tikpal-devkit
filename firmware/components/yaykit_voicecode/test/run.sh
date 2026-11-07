#!/usr/bin/env bash
# Host tests for yaykit_voicecode, no ESP-IDF build:
#  - test_fixtures.c: every example in packages/contracts/fixtures/voice-code.json, the contract's own. cJSON comes
#    from ESP-IDF (IDF_PATH, or ~/esp/esp-idf); in CI a missing cJSON is a failure, not a skip.
#  - test_voicecode.c: bits, Reed-Solomon, and yk_vc_scan on rendered camera frames. Plain C99 + libm (+ pthreads for
#    the test's own parallel rendering). `--quick` skips the frames.
#  - test_view.c: the round screen's part (yk_vc_view_logic.c): the contract's app examples drawn, the last frame,
#    the safe circle, the drawn screen read back; built with no app number, two of the examples' and a test number.
#  - voicecode_number.cmake: the number from app.json (CMake from PATH or ESP-IDF's tools; in CI a missing one fails).
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
SRC=(../yk_voicecode.c ../yk_voicecode_decode.c ../yk_voicecode_scan.c)
FIXTURES="$(cd ../../../../.. && pwd)/packages/contracts/fixtures/voice-code.json"
VIEW=(-I ../include -I .. -I ../../yaykit_ui/include test_view.c ../yk_vc_view_logic.c "${SRC[@]}")

IDF="${IDF_PATH:-$HOME/esp/esp-idf}"
CJSON="$IDF/components/json/cJSON"
if [ -f "$CJSON/cJSON.c" ]; then
  cc -std=c99 -Wall -Wextra -Werror -O1 -I ../include -I "$CJSON" -o "$OUT/test_fixtures" test_fixtures.c \
    "${SRC[@]}" "$CJSON/cJSON.c" -lm
  "$OUT/test_fixtures" "$FIXTURES"
  # The app's number as built in: none, two of the contract's examples (300, 65535), a test number.
  for n in 0 300 65535 65280; do
    def=()
    [ "$n" != 0 ] && def=(-DYK_VC_APP_NUMBER="$n")
    cc -std=c99 -Wall -Wextra -Werror -O2 -DHAVE_CJSON -DEXPECT_NUMBER="$n" ${def[@]+"${def[@]}"} -I "$CJSON" \
      -o "$OUT/test_view_$n" "${VIEW[@]}" "$CJSON/cJSON.c" -lm
    "$OUT/test_view_$n" "$FIXTURES"
  done
elif [ -n "${CI:-}" ]; then
  echo "yaykit_voicecode: no ESP-IDF for cJSON (set IDF_PATH); the contract's examples were not checked" >&2
  exit 1
else
  echo "yaykit_voicecode contract examples skipped: no ESP-IDF for cJSON (set IDF_PATH)"
  cc -std=c99 -Wall -Wextra -Werror -O2 -o "$OUT/test_view" "${VIEW[@]}" -lm
  "$OUT/test_view"
fi

CMAKE=$(command -v cmake || ls -d "${IDF_TOOLS_PATH:-$HOME/.espressif}"/tools/cmake/*/{CMake.app/Contents/,}bin/cmake 2>/dev/null |
  tail -1 || true)
if [ -n "$CMAKE" ]; then
  bash test_number.sh "$CMAKE"
elif [ -n "${CI:-}" ]; then
  echo "yaykit_voicecode: no CMake (PATH or ESP-IDF's tools); voicecode_number.cmake was not checked" >&2
  exit 1
else
  echo "yaykit_voicecode app number from app.json skipped: no CMake"
fi

cc -std=c99 -Wall -Wextra -Werror -O2 -I ../include -I .. -o "$OUT/test_voicecode" "${SRC[@]}" test_voicecode.c -lm -lpthread
"$OUT/test_voicecode" "$@"
