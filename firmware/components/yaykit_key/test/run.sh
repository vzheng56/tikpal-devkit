#!/usr/bin/env bash
# Host tests for the side-key state machine (no ESP-IDF needed).
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O1 -I../include -o "$OUT/test_key" test_key.c ../yk_key_sm.c
"$OUT/test_key"
rm -rf "$OUT"
