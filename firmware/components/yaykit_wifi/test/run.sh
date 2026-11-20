#!/usr/bin/env bash
# Host tests for yaykit_wifi (no ESP-IDF build needed): the pure C parts, and with IDF_PATH set (ESP-IDF's cJSON)
# yaykit_wifi.c itself on stubs (stub_run/: a simulated radio, timers, events and NVS).
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O1 -o "$OUT/test_wifi" test_wifi.c ../yk_wifi_creds.c ../yk_wifi_timing.c ../yk_wifi_list.c
"$OUT/test_wifi"
if [ -n "${IDF_PATH:-}" ] && [ -f "$IDF_PATH/components/json/cJSON/cJSON.c" ]; then
  CJSON="$IDF_PATH/components/json/cJSON"
  cc -std=gnu11 -Wall -Wextra -Werror -O1 -Istub_run -I../include -I.. -I"$CJSON" -o "$OUT/test_wifi_run" \
    test_wifi_run.c ../yk_wifi_creds.c ../yk_wifi_timing.c ../yk_wifi_list.c "$CJSON/cJSON.c"
  "$OUT/test_wifi_run"
else
  echo "yaykit_wifi on stubs skipped: set IDF_PATH for ESP-IDF's cJSON"
fi
rm -rf "$OUT"
