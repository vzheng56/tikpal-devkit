#!/usr/bin/env bash
# Host tests for the pure parts of yaykit_ui (no ESP-IDF, LVGL stubbed): the rim, subtitles, notices, Yay; the app
# ring, the app catalog, the hint arrow and 「我的应用」; the system pull-down's words (test_sheet.c); the Tikpal mark's checks (test_ui.c) and its arrival against
# the brand kit's own state-spec.json (test_mark.c; python3 reads the file).
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
cc -std=c11 -Wall -Wextra -Werror -O1 -I.. -I../include -Istub -o "$OUT/test_ui" test_ui.c ../yk_ui_logic.c -lm
"$OUT/test_ui"
cc -std=c11 -Wall -Wextra -Werror -O1 -I.. -I../include -Istub -o "$OUT/test_apps" test_apps.c ../yk_apps_logic.c \
  ../yk_ui_logic.c -lm
"$OUT/test_apps"
cc -std=c11 -Wall -Wextra -Werror -O1 -I.. -I../include -Istub -o "$OUT/test_more" test_more.c ../yk_more_logic.c \
  ../yk_apps_logic.c ../yk_ui_logic.c -lm
"$OUT/test_more"
cc -std=c11 -Wall -Wextra -Werror -O1 -I.. -I../include -Istub -o "$OUT/test_sheet" test_sheet.c ../yk_sheet_logic.c
"$OUT/test_sheet"
cc -std=c11 -Wall -Wextra -Werror -O1 -I.. -I../include -o "$OUT/test_mark" test_mark.c ../yk_mark_fit.c -lm
python3 -I - ../../../../../design/tikpal/2026-11-01-brand-kit-v1/03-motion/state-spec.json <<'PY' | "$OUT/test_mark"
import json, sys
arrival = json.load(open(sys.argv[1]))["states"]["arrival"]
print("ms", arrival["ms"])
for track, keys in arrival["tracks"].items():
    for t, v in keys:
        print(track, t, v)
PY
rm -rf "$OUT"
