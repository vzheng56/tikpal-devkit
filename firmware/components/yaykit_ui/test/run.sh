#!/usr/bin/env bash
# Host tests for the pure parts of yaykit_ui (no ESP-IDF, LVGL stubbed): the rim, subtitles, notices, Yay; the app
# ring, the app catalog, the hint arrow and 「我的应用」; the system pull-down's words (test_sheet.c); the Tikpal mark's checks (test_ui.c) and its arrival against
# the brand kit's own state-spec.json (test_mark.c; python3 reads the file).
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
# The four languages' words (yaykit_strings, Yay 0.1.19 4.7).
WORDS=../../yaykit_strings
WORDS_I="-I$WORDS/include -I$WORDS/generated -I$WORDS/test"
WORDS_C="$WORDS/yk_words.c $WORDS/generated/yk_strings.c"
# The account's accent (yk_accent.c, 0.1.19 4.7): what yk_ui_logic's rim colours read; the original without a device.
ACCENT_I="-I../generated"
ACCENT_C="../yk_accent.c"
cc $ACCENT_I $WORDS_I -std=c11 -Wall -Wextra -Werror -O1 -I.. -I../include -Istub -o "$OUT/test_ui" test_ui.c ../yk_ui_logic.c $WORDS_C $ACCENT_C -lm
"$OUT/test_ui"
cc $ACCENT_I $WORDS_I -std=c11 -Wall -Wextra -Werror -O1 -I.. -I../include -Istub -o "$OUT/test_apps" test_apps.c ../yk_apps_logic.c \
  ../yk_ui_logic.c $WORDS_C $ACCENT_C -lm
"$OUT/test_apps"
cc $ACCENT_I $WORDS_I -std=c11 -Wall -Wextra -Werror -O1 -I.. -I../include -Istub -o "$OUT/test_ring" test_ring.c ../yk_ring_logic.c \
  ../yk_apps_logic.c ../yk_ui_logic.c $WORDS_C $ACCENT_C -lm
"$OUT/test_ring"
cc $ACCENT_I $WORDS_I -std=c11 -Wall -Wextra -Werror -O1 -I.. -I../include -Istub -o "$OUT/test_more" test_more.c ../yk_more_logic.c \
  ../yk_apps_logic.c ../yk_ui_logic.c $WORDS_C $ACCENT_C -lm
"$OUT/test_more"
cc $ACCENT_I $WORDS_I -std=c11 -Wall -Wextra -Werror -O1 -I.. -I../include -Istub -o "$OUT/test_sheet" test_sheet.c ../yk_sheet_logic.c \
  ../yk_apps_logic.c ../yk_ui_logic.c $WORDS_C $ACCENT_C -lm
"$OUT/test_sheet"
# The buddies' slots (Yay 0.1.18 4.9): their head, byte for byte 0.1.17's.
cc -std=c11 -Wall -Wextra -Werror -O1 -I../include -o "$OUT/test_buddy_slot" test_buddy_slot.c ../yk_buddy_slot.c
"$OUT/test_buddy_slot"
# A buddy's picture pack: every way of breaking its table, and each real one against its manifest and the rig: the
# mouths' three levels and the pose names. Nothing here writes a manifest: one that is out of date fails, here and on CI.
cc $ACCENT_I $WORDS_I -std=c11 -Wall -Wextra -Werror -O1 -I.. -I../include -Istub -o "$OUT/test_figure" test_figure.c ../yk_figure.c \
  ../yk_ui_logic.c $WORDS_C $ACCENT_C -lm
"$OUT/test_figure"
# Yay's picture cache (yk_yay.c itself, LVGL stubbed in stub_yay/): the home screen and the app ring's middle hang the
# same pack (0.1.18), and one's going must not take the other's picture. Which buddy a Yay is (yk_yay_buddy.c, kit
# 1.10): the user's by default in an extension. Built as Yay is (Pip from his pack, no buddy of its own accord) and as
# the other firmware are.
for BUILT in "-DYAY_ART_PACKED -UYK_YAY_BUDDY_DEFAULT" "-UYAY_ART_PACKED -DYK_YAY_BUDDY_DEFAULT" \
             "-UYAY_ART_PACKED -UYK_YAY_BUDDY_DEFAULT"; do
  # shellcheck disable=SC2086 # two flags in one
  cc $ACCENT_I $WORDS_I -std=c11 -Wall -Wextra -Werror -O1 $BUILT -Istub_yay -I.. -I../include -I../../../assets/yay -o "$OUT/test_yay" \
    test_yay.c ../yk_ui_logic.c ../yk_figure.c ../yk_buddy_slot.c $WORDS_C $ACCENT_C -lm
  "$OUT/test_yay"
done
YAYKIT=$(cd ../../../.. && pwd)
ROOT=$(cd "$YAYKIT/.." && pwd)
FIGURES="$YAYKIT/firmware/assets/figures"
python3 -I figure_manifest.py fresh "$FIGURES" "$ROOT"
HERE="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
for MANIFEST in "$FIGURES"/*.json; do
  read -r FIGURE THEME FILE MADE_ON < <(python3 -I figure_manifest.py info "$MANIFEST")
  PACK="$YAYKIT/artifacts/figures/$FIGURE/$FILE"  # not in Git
  # Only the platform that made a pack makes it byte for byte. --check compares it with the manifest and fails on a
  # difference; --keep-pack leaves the pack, never the manifest.
  if [ ! -f "$PACK" ] && [ "$HERE" = "$MADE_ON" ] && [ -d "$YAYKIT/node_modules" ] && command -v pnpm >/dev/null; then
    (cd "$YAYKIT" && pnpm --silent art:pack "$FIGURE" --check --keep-pack >/dev/null)
  fi
  if [ -f "$PACK" ]; then
    python3 -I figure_manifest.py lines "$MANIFEST" "$ROOT/packages/yay-art/dist/rig-$FIGURE.json" "$PACK" |
      "$OUT/test_figure" "$PACK" "$FIGURE" "$THEME"
  elif [ -n "${CI:-}" ]; then
    echo "yk_figure: no $PACK on CI, where the real pack must be tested" >&2
    exit 1
  else
    echo "yk_figure: the real pack skipped: no $PACK (pnpm art:pack $FIGURE)"
  fi
done
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
