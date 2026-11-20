#!/usr/bin/env bash
# Host tests for yaykit_tf's pure parts, no device (SHA-256 from test/shim):
#  - test_tf_logic.c: paths, quota, memory, holders, pauses, failures, the restart after a timeout.
#  - the two halves: what every firmware compiles against the system's side (Yay 0.1.19 开发文档 6.1).
#  - test_tf_pkg.c: the store on a real directory, with writes cut short, failing and the power going at every step.
#  - test_folder_logic.c, test_folder_fat.c: the shared folders (yk_folder.h): names, the answer, reading on FatFs.
#  - test_tf_fat.c: the store on ESP-IDF's own FatFs over a RAM disk, the power going at every sector write. FatFs comes
#    from ESP-IDF (IDF_PATH, or ~/esp/esp-idf); in CI a missing one is a failure, not a skip. TF_SKIP_FAT=1 leaves it out.
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
FLAGS=(-std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I.. -I../include -I../sys_include -I shim)
cc "${FLAGS[@]}" -o "$OUT/test_tf_logic" test_tf_logic.c ../yk_tf_logic.c
"$OUT/test_tf_logic"
# The store's tests build on its system side (yk_tf_pkg.c), which a kit does not have (Yay 0.1.19 开发文档 6.1): there they
# are left out, with it.
STORE=1
[ -f ../yk_tf_pkg.c ] || { STORE=; echo "yaykit_tf store: left out (no yk_tf_pkg.c: a kit has not the system's side)"; }
[ -z "$STORE" ] || cc "${FLAGS[@]}" -o "$OUT/test_tf_pkg" test_tf_pkg.c ../yk_tf_logic.c
[ -z "$STORE" ] || "$OUT/test_tf_pkg" "$OUT"
# What the card has to have (yk_tf_layout.c, the system's side too), first on a directory of the host.
[ -z "$STORE" ] || cc "${FLAGS[@]}" -o "$OUT/test_tf_layout" test_tf_layout.c ../yk_tf_logic.c
[ -z "$STORE" ] || "$OUT/test_tf_layout" "$OUT"
# The factory library (yk_tf_lib.c, the system's side too, 开发文档 4.5): its rules, a part taken up again, the records.
[ -z "$STORE" ] || cc "${FLAGS[@]}" -o "$OUT/test_tf_lib" test_tf_lib.c ../yk_tf_logic.c
[ -z "$STORE" ] || "$OUT/test_tf_lib" "$OUT"
# The test's four folders are yk_tf_layout.c's.
if [ -n "$STORE" ]; then
  folders() { grep -h 'YK_TF_FOLDERS\[YK_TF_FOLDER_COUNT\] = ' "$1" | sed 's/.*= //'; }
  [ "$(folders ../yk_tf_layout.c)" = "$(folders test_tf_lib.c)" ] || { echo "test_tf_lib.c's folders are not yk_tf_layout.c's" >&2; exit 1; }
fi

# The two halves (Yay 0.1.19 开发文档 6.1 step 3): what every firmware compiles needs nothing of sys_include/, which a kit
# does not have, and nothing in include/ hands an app the system's functions.
cc -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Wextra -Werror -O1 -I.. -I../include -I shim -c ../yk_tf_fat.c \
  -o "$OUT/yk_tf_fat.o"
python3 -I - .. ../../yaykit_app/yk_app_power.c <<'PY2'
import os, re, sys
tf, power = sys.argv[1], open(sys.argv[2], encoding="utf8").read()
read = lambda name: open(os.path.join(tf, name), encoding="utf8").read()
code = lambda text: re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.S)
always = ("yaykit_tf.c", "yk_tf_logic.c", "yk_tf_fat.c", "yk_folder.c", "yk_folder_logic.c", "yk_folder_read.c",
          "yk_folder_nvs.c")
for name in always + ("yk_tf_fat.h", "yk_tf_card.h", "yk_tf_logic.h", "yk_folder_logic.h", "yk_folder_read.h",
                      "yk_folder_nvs.h"):
    for header in re.findall(r'#include\s+"([^"]+)"', read(name)):
        assert header not in ("yk_tf_sys.h", "yk_tf_pkg.h", "yk_folder_sys.h"), f"{name} needs the system's {header}"
for name in os.listdir(os.path.join(tf, "include")):
    found = re.search(r"\b(yk_tf_(?:sys|pkg)_\w+)\s*\(", code(read(os.path.join("include", name))))
    assert not found, f"include/{name} declares {found.group(1)}, the system's own"
# The system's files are compiled only with CONFIG_YK_TF_SYSTEM, and the private include path with them.
cmake = read("CMakeLists.txt")
listed = re.search(r'set\(YK_TF_SRCS ([^)]*)\)', cmake).group(1)
assert sorted(re.findall(r'"([^"]+)"', listed)) == sorted(always), listed
system = re.search(r"^if\(CONFIG_YK_TF_SYSTEM\)(.*?)^endif\(\)", cmake, re.S | re.M).group(1)  # the top-level one
assert '"yk_tf_sys.c" "yk_tf_pkg.c" "yk_folder_sys.c"' in system and '"sys_include"' in system, system
# Deep sleep stops the card in any firmware with it: yk_app_power.c links yaykit_tf's own stop weakly, by its name, and
# a wrong name would only make it NULL. yaykit_tf.c defines it with no condition around it.
weak = re.search(r"esp_err_t (\w+)\(uint32_t wait_ms\) __attribute__\(\(weak\)\);", power).group(1)
assert weak == "yk_tf_card_suspend", weak
assert re.search(r"esp_err_t %s\(uint32_t wait_ms\);" % weak, read("yk_tf_card.h"))
card = read("yaykit_tf.c")
at = card.index("esp_err_t %s(uint32_t wait_ms) {" % weak)
depth = 0
for line in card[:at].splitlines():
    if re.match(r"\s*#\s*if", line): depth += 1
    elif re.match(r"\s*#\s*endif", line): depth -= 1
assert depth == 0, f"{weak} is inside an #if in yaykit_tf.c"
assert "if (%s) %s(" % (weak, weak) in power
# The shared folders answer 卡正在当 U 盘 while the card is lent to a computer: yk_folder.c links the lending's own
# yk_tf_card_usb() weakly, by its name (Yay 0.1.19 开发文档 4.2, 4.6); a wrong name would only make it NULL.
usb = re.search(r"bool (\w+)\(void\) __attribute__\(\(weak\)\);", read("yk_folder.c")).group(1)
assert usb == "yk_tf_card_usb", usb
assert re.search(r"bool %s\(void\);" % usb, read("yk_tf_card.h"))
at = card.index("bool %s(void) {" % usb)
depth = 0
for line in card[:at].splitlines():
    if re.match(r"\s*#\s*if", line): depth += 1
    elif re.match(r"\s*#\s*endif", line): depth -= 1
assert depth == 0, f"{usb} is inside an #if in yaykit_tf.c"
assert "if (%s && %s())" % (usb, usb) in read("yk_folder.c")
# The answers' NVS namespace is no app's own: an app owns "yk_" + its slug without hyphens, cut to 15
# (apps/backend/src/submissions.ts ownNamespace), and a slug is a-z, 0-9 and '-' (yk_tf_name_ok).
ns = re.search(r'#define NVS_NS "([^"]+)"', read("yk_folder_nvs.c")).group(1)
assert len(ns) <= 15, ns
assert not re.fullmatch(r"yk_[a-z0-9]{0,12}", ns), f"{ns}: an app with the slug {ns[3:]!r} would own it"
# The folders' question: yk_folder.c links yaykit_app's by name, weakly (a wrong name would never ask, only NOT_NOW).
folder = read("yk_folder.c")
asked = re.search(r"int (yk_app_folder_question)\((uint8_t folders, uint32_t wait_ms)\) __attribute__\(\(weak\)\);", folder)
grant = os.path.join(tf, "..", "yaykit_app", "yk_app_grant.c")
if os.path.exists(grant):
    assert asked and "int %s(%s) {" % asked.groups() in open(grant, encoding="utf8").read(), "the question's name"
# The device service's calls: handed over by yaykit_cloud as it starts (固件总策划 11-13), never required: yaykit_tf
# needs nothing of yaykit_cloud, and only an app that uses the cloud carries HTTPS for them. yk_account_init offers
# them; yk_folder_net.c reaches yk_folder_net_set weakly (a firmware without the card has no yaykit_tf), with the same
# words yk_folder_logic.h declares; yk_folder.c and yk_folder_sys.c call through yk_folder_net() only.
cloud = os.path.join(tf, "..", "yaykit_cloud")
cread = lambda name: open(os.path.join(cloud, name), encoding="utf8").read()
assert not re.search(r"\byaykit_cloud\b", re.sub(r"#[^\n]*", "", cmake)), "yaykit_tf requires yaykit_cloud"
decl = re.search(r"^(void yk_folder_net_set\(const yk_folder_net_t \*net\));", read("yk_folder_logic.h"), re.M).group(1)
assert decl + " __attribute__((weak));" in cread("yk_folder_net.c"), "yk_folder_net_set: weakly, the same words"
assert "if (yk_folder_net_set) yk_folder_net_set(" in cread("yk_folder_net.c")
init = cread("yaykit_account.c")
at = init.index("esp_err_t yk_account_init(const yk_account_config_t *config) {")
assert "yk_folder_net_offer();" in init[at:at + 600], "yk_account_init offers the calls"
# yk_folder_sys.c is the system's half, which a kit has not (devkit-check.ts EXCLUDE): there it is left out, as the store's
# tests are without yk_tf_pkg.c above. Where the system's half is (yk_tf_pkg.c), yk_folder_sys.c is read and checked.
system_side = os.path.exists(os.path.join(tf, "yk_tf_pkg.c"))
for name in ("yk_folder.c",) + (("yk_folder_sys.c",) if system_side else ()):
    assert not re.search(r"\bnet_(get|put|list|put_now)\(|\byk_folder_net_(get|put)\(", code(read(name))), name
print("yaykit_tf halves: every firmware's needs no sys_include, include/ has none of the system's, deep sleep stops the card, the folders see the USB disk")
PY2

# The shared folders (yk_folder.h, Yay 0.1.19 开发文档 4.3, 4.6): the person's names and the answer, pure.
cc "${FLAGS[@]}" -o "$OUT/test_folder_logic" test_folder_logic.c ../yk_folder_logic.c ../yk_tf_logic.c
"$OUT/test_folder_logic"
# Yay's side of the grants (sys_include/yk_folder_sys.h: its check at connecting, the fetch before it opens an app):
# yk_folder_sys.c and yk_folder_nvs.c themselves over NVS in memory and a service as the contract has it. The system's
# side: a kit has not yk_folder_sys.c, and leaves it out.
if [ -f ../yk_folder_sys.c ]; then
  cc "${FLAGS[@]}" -I sync_shim -o "$OUT/test_folder_sync" test_folder_sync.c ../yk_folder_sys.c ../yk_folder_nvs.c \
    ../yk_folder_logic.c ../yk_tf_logic.c
  "$OUT/test_folder_sync"
fi
# What app.json asks for, read at build time (folders.cmake).
CMAKE=$(command -v cmake || ls -d "${IDF_TOOLS_PATH:-$HOME/.espressif}"/tools/cmake/*/{CMake.app/Contents/,}bin/cmake 2>/dev/null |
  tail -1 || true)
if [ -n "$CMAKE" ]; then
  mkdir "$OUT/json"
  printf '{"permissions":{"folders":{"music":{"zh":"放音乐"},"documents":{"zh":"读说明"}}}}' >"$OUT/json/two.json"
  printf '{"permissions":{"folders":{"stories":{},"pictures":{},"music":{},"documents":{}}}}' >"$OUT/json/all.json"
  printf '{"permissions":{"device":["tf_card"]}}' >"$OUT/json/none.json"
  printf '{"permissions":{"folders":{"recordings":{}}}}' >"$OUT/json/recordings.json"
  printf '{"permissions":{"folders":["music"]}}' >"$OUT/json/list.json"
  printf 'not json' >"$OUT/json/broken.json"
  said() { "$CMAKE" -P ../folders.cmake "$@" 2>&1; }
  [ "$(said "$OUT/json/two.json")" = 9 ] && [ "$(said "$OUT/json/all.json")" = 15 ] &&
    [ "$(said "$OUT/json/none.json")" = 0 ] && [ "$(said "$OUT/json/missing.json")" = 0 ] &&
    [ "$(said "$OUT/json/none.json" 6)" = 6 ] && [ "$(said "$OUT/json/two.json" 0)" = 0 ] ||
    { echo "folders.cmake: the kinds are not app.json's" >&2; exit 1; }
  if "$CMAKE" -P ../folders.cmake "$OUT/json/none.json" 16 >/dev/null 2>&1; then
    echo "folders.cmake: YK_FOLDER_KINDS=16 should stop the build" >&2
    exit 1
  fi
  for bad in recordings list broken; do
    if "$CMAKE" -P ../folders.cmake "$OUT/json/$bad.json" >/dev/null 2>&1; then
      echo "folders.cmake: $bad.json should stop the build" >&2
      exit 1
    fi
  done
  echo "yk_folder kinds from app.json: music+documents 9, all 15, none 0, a test build's own; Recordings and a broken one stop the build"
elif [ -n "${CI:-}" ]; then
  echo "yaykit_tf: no CMake (PATH or ESP-IDF's tools); folders.cmake was not checked" >&2
  exit 1
else
  echo "yk_folder kinds from app.json skipped: no CMake"
fi
# A character the fonts lack is drawn as a box (4.3: 「□」), LVGL's glyph placeholder: on unless someone turns it off.
# From test/: ../../../ is firmware/, so the settings are ../../../../config and ../../../<project>/sdkconfig.* (in this
# repository yaykit/config and yaykit/firmware/*; in a kit its config/ and firmware/*). Where to look must be there:
# grep -s says nothing of a path that is not, and the check would pass on nothing.
PLACEHOLDER_IN=(../../../../config ../../../*/sdkconfig.*)
if [ ! -d ../../../../config ] || [ ! -f "${PLACEHOLDER_IN[1]}" ]; then
  echo "yaykit_tf: no ../../../../config or ../../../*/sdkconfig.* to look for CONFIG_LV_USE_FONT_PLACEHOLDER in" >&2
  exit 1
fi
if grep -rqsE '^(CONFIG_LV_USE_FONT_PLACEHOLDER=n|# CONFIG_LV_USE_FONT_PLACEHOLDER is not set)' "${PLACEHOLDER_IN[@]}"; then
  echo "yaykit_tf: CONFIG_LV_USE_FONT_PLACEHOLDER is off somewhere: a name's missing character would not show as a box" >&2
  exit 1
fi

FATFS="${IDF_PATH:-$HOME/esp/esp-idf}/components/fatfs/src"
if [ -n "${TF_SKIP_FAT:-}" ]; then
  echo "yaykit_tf on FatFs: left out (TF_SKIP_FAT)"
elif [ -f "$FATFS/ff.c" ]; then
  cc -std=c11 -O2 -w -I fat_shim -I "$FATFS" -c "$FATFS/ff.c" -o "$OUT/ff.o"
  cc -std=c11 -O2 -w -I fat_shim -I "$FATFS" -c "$FATFS/ffunicode.c" -o "$OUT/ffunicode.o"
  # The shared folders read through FatFs itself: the person's names, eight levels deep, and nothing written.
  cc -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I.. \
    -I../include -I folder_shim -I fat_shim -I "$FATFS" -o "$OUT/test_folder_fat" test_folder_fat.c ../yk_folder_read.c \
    ../yk_folder_logic.c ../yk_tf_logic.c "$OUT/ff.o" "$OUT/ffunicode.o"
  "$OUT/test_folder_fat"
  if [ -z "$STORE" ]; then
    echo "yaykit_tf on FatFs: the store's power cuts left out with the store's tests"
  else
    # No sanitizers: ten thousand places take four minutes with them, and test_tf_pkg.c has them on the same code.
    cc -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Wextra -Werror -O2 -g -I.. -I../include -I../sys_include -I shim -I fat_shim \
      -I "$FATFS" -o "$OUT/test_tf_fat" test_tf_fat.c ../yk_tf_logic.c "$OUT/ff.o" "$OUT/ffunicode.o"
    "$OUT/test_tf_fat"
    # ... and on FatFs: attributes, the reserve's clusters, cases folded.
    cc -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -DLAYOUT_ON_FAT=1 -Wall -Wextra -Werror -O1 -g -I.. -I../include -I../sys_include \
      -I shim -I fat_shim -I "$FATFS" -o "$OUT/test_tf_layout_fat" test_tf_layout.c ../yk_tf_logic.c "$OUT/ff.o" "$OUT/ffunicode.o"
    "$OUT/test_tf_layout_fat"
    # ... and the factory library on FatFs, the power lost before every sector write of putting one in.
    cc -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -DLIB_ON_FAT=1 -Wall -Wextra -Werror -O1 -g -I.. -I../include -I../sys_include \
      -I shim -I fat_shim -I "$FATFS" -o "$OUT/test_tf_lib_fat" test_tf_lib.c ../yk_tf_logic.c "$OUT/ff.o" "$OUT/ffunicode.o"
    "$OUT/test_tf_lib_fat"
  fi
elif [ -n "${CI:-}" ]; then
  echo "yaykit_tf: no ESP-IDF for FatFs (set IDF_PATH); the power cuts and the shared folders on FatFs were not checked" >&2
  exit 1
else
  echo "yaykit_tf on FatFs skipped: no ESP-IDF for FatFs (set IDF_PATH)"
fi
