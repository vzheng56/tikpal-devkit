#!/usr/bin/env bash
# The app's number from app.json into the image (voicecode_number.cmake, BOSS-2 11-01): what the build reads, what it
# refuses, and that the number carries through to the compiled code: a new number in app.json makes a new object, the
# same app.json the very same one (rebuilds stay reproducible). Usage: test_number.sh <cmake>.
set -euo pipefail
cd "$(dirname "$0")"
CMAKE=$1
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
failed=0
checks=0

number() { "$CMAKE" -P ../voicecode_number.cmake "$@" 2>&1 | sed -n 's/^number=//p'; }
expect() {  # expect <want> <app.json> [override]
  local want=$1
  shift
  checks=$((checks + 1))
  local got
  got=$(number "$@")
  if [ "$got" != "$want" ]; then
    echo "FAIL voicecode_number.cmake $*: got '$got', want '$want'"
    failed=$((failed + 1))
  fi
}
refuse() {  # refuse <app.json> [override]
  checks=$((checks + 1))
  if "$CMAKE" -P ../voicecode_number.cmake "$@" >/dev/null 2>&1; then
    echo "FAIL voicecode_number.cmake $*: should stop the build"
    failed=$((failed + 1))
  fi
}
app() { printf '{"slug": "x", "version": "0.1.0"%s}\n' "$2" >"$T/$1.json"; }

app none ''
app six ', "voiceCode": {"number": 6, "ask": {"zh": ["今天练了多久？"]}}'
app seven ', "voiceCode": {"number": 7}'
app test ', "voiceCode": {"number": 65280}'
app top ', "voiceCode": {"number": 65535}'
app zero ', "voiceCode": {"number": 0}'
app big ', "voiceCode": {"number": 65536}'
app text ', "voiceCode": {"number": "6"}'
app frac ', "voiceCode": {"number": 6.5}'
app empty ', "voiceCode": {}'
app notobj ', "voiceCode": 6'
printf '{"slug": "x", "voiceCode": {"number": 6}' >"$T/broken.json"

expect "" "$T/missing.json"      # no app.json: no number
expect "" "$T/none.json"         # no voiceCode: no number
expect 6 "$T/six.json"
expect 7 "$T/seven.json"
expect 65280 "$T/test.json"      # a test number is built in like any other
expect 65535 "$T/top.json"
expect 65280 "$T/none.json" 65280  # -D YK_VC_APP_NUMBER wins
expect 65281 "$T/six.json" 65281
expect 65280 "$T/missing.json" 65280
for bad in zero big text frac empty notobj broken; do refuse "$T/$bad.json"; done
refuse "$T/none.json" 0
refuse "$T/none.json" 70000
refuse "$T/none.json" abc

# Through to the code: app.json -> the number -> yk_vc_view_logic.c's object.
object() {  # object <app.json> <out>
  local n
  n=$(number "$1")
  cc -std=c99 -O2 -I ../include -c ../yk_vc_view_logic.c -DYK_VC_APP_NUMBER="$n" -o "$2"
}
object "$T/six.json" "$T/a.o"
object "$T/six.json" "$T/b.o"
object "$T/seven.json" "$T/c.o"
checks=$((checks + 2))
if ! cmp -s "$T/a.o" "$T/b.o"; then
  echo "FAIL the same app.json built twice gives two objects"
  failed=$((failed + 1))
fi
if cmp -s "$T/a.o" "$T/c.o"; then
  echo "FAIL another number in app.json gives the same object"
  failed=$((failed + 1))
fi

if [ "$failed" -gt 0 ]; then
  echo "FAIL: app number from app.json, $checks checks, $failed failed"
  exit 1
fi
echo "ok: app number from app.json, $checks checks (a new number builds a new object, the same one the same)"
