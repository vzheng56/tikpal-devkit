#!/usr/bin/env bash
# Host tests for yaykit_haptic's scheduling (no ESP-IDF needed): the five kinds, the strengths and their kick, an app's
# own pattern, the gap between two pulses, the 2 s cap, stopping, the settings and the quiet conditions.
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O1 -o "$OUT/test_haptic" test_haptic.c ../yk_haptic_core.c
"$OUT/test_haptic"
rm -rf "$OUT"
