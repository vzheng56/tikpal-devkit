#!/usr/bin/env bash
# Host tests for the pure C parts of yaykit_cloud (no ESP-IDF needed).
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O1 -I stub -I ../include -I .. -o "$OUT/test_cloud" test_cloud.c ../yk_codec.c ../yk_sse.c \
  ../yk_locale.c
"$OUT/test_cloud"
rm -rf "$OUT"
# Every firmware's settings leave CONFIG_YK_CLOUD_ORIGIN at the Kconfig's default, the region table's row 0
# (yaykit/tests/deployments.test.ts holds the two equal): a release build's device API is global's, and with
# CONFIG_YK_REGIONS global's row is that setting (yk_cloud_origin()). A development build points elsewhere by its own
# development settings, not a settings file here. From test/: ../../../ is firmware/ (here and in a kit), ../../../../config
# beside it; sdkconfig.old is a build's own copy, not a setting.
shopt -s nullglob
SETTINGS=(../../../*/sdkconfig.* ../../../examples/*/sdkconfig.* ../../../components/*/bench/sdkconfig.* ../../../../config/*.defaults)
shopt -u nullglob
[ ${#SETTINGS[@]} -gt 0 ] || { echo "yaykit_cloud: no settings files found to look for CONFIG_YK_CLOUD_ORIGIN in" >&2; exit 1; }
set_origin=()
looked=0
for f in "${SETTINGS[@]}"; do
  case "$f" in *.old | *.h) continue ;; esac
  looked=$((looked + 1))
  if grep -qE '^[[:space:]]*CONFIG_YK_CLOUD_ORIGIN[[:space:]]*=' "$f"; then set_origin+=("$f"); fi
done
if [ ${#set_origin[@]} -gt 0 ]; then
  printf 'CONFIG_YK_CLOUD_ORIGIN is set in %s: leave it at the Kconfig'"'"'s default (the region table'"'"'s row 0)\n' "${set_origin[@]}" >&2
  exit 1
fi
echo "yaykit_cloud: $looked settings files (sdkconfig.*, config/*.defaults), none sets CONFIG_YK_CLOUD_ORIGIN"
# The region table (yk_regions_gen.h, generated from the contract) is read by yk_region.c alone, and the host tests:
# everything else asks yk_region.h, yk_cloud_origin() or yk_session_url().
others=$(grep -rl --include='*.c' --include='*.h' --include='*.cpp' '#include *"yk_regions_gen\.h"' ../../.. |
  grep -v -e '/yaykit_cloud/yk_region\.c$' -e '/test/' || true)
if [ -n "$others" ]; then
  printf 'yk_regions_gen.h is yk_region.c'"'"'s alone; also included by:\n%s\n' "$others" >&2
  exit 1
fi

# The device API v1 readers and builders against the platform's contract fixtures, with ESP-IDF's cJSON.
if [ -n "${IDF_PATH:-}" ] && [ -f "$IDF_PATH/components/json/cJSON/cJSON.c" ]; then
  CJSON="$IDF_PATH/components/json/cJSON"
  OUT=$(mktemp -d)
  cc -std=c11 -Wall -Wextra -Werror -O1 -I ../include -I "$CJSON" -o "$OUT/test_api" test_api.c ../yk_api.c "$CJSON/cJSON.c"
  "$OUT/test_api" "$(cd ../../../../.. && pwd)/packages/contracts/fixtures/device-api.json"
  rm -rf "$OUT"
  # Streamed recordings (yk_cloud_stream.h, 录音卡 0.2) against the same fixtures; a refusal's consent (V4 of
  # 两套部署 · 语音单独同意) also against fixtures/deployments.json's voiceConsentHttp (a kit has none: left out, said so)
  OUT=$(mktemp -d)
  cc -std=c11 -Wall -Wextra -Werror -O1 -I stub -I ../include -I "$CJSON" -o "$OUT/test_stream" test_stream.c \
    ../yk_api_stream.c ../yk_api.c "$CJSON/cJSON.c"
  CONSENT_FIXTURE="$(cd ../../../../.. && pwd)/packages/contracts/fixtures/deployments.json"
  [ -f "$CONSENT_FIXTURE" ] || CONSENT_FIXTURE=
  "$OUT/test_stream" "$(cd ../../../../.. && pwd)/packages/contracts/fixtures/device-api.json" ${CONSENT_FIXTURE:+"$CONSENT_FIXTURE"}
  rm -rf "$OUT"
  # yk_stream.c (the device's: esp_http_client) can be stopped (Yay 0.2 F6c) where it says: before the token and the
  # connection, before each 4 KB of the body, while the answer is awaited; without a stop, the calls as 0.4.0's.
  python3 -I - ../yk_stream.c <<'PY'
import re, sys
src = open(sys.argv[1], encoding="utf-8").read()
send = re.search(r"static esp_err_t send_once\(.*?\n\}\n", src, re.S).group(0)
assert send.index("if (stopped())") < send.index("esp_http_client_open"), "a stopped step opens a connection"
loop = send[send.index("for (size_t sent = 0;"):send.index("err = answer(")]
assert loop.index("if (stopped())") < loop.index("esp_http_client_write"), "the body is not stopped between pieces"
assert "if (!kept || *why) break;" in send, "a stopped step is tried again on a new connection"
answer = re.search(r"static esp_err_t answer\(.*?\n\}\n", src, re.S).group(0)
assert "s_stop ? esp_http_client_get_socket" in answer and "yk_api_stream_wait(" in answer and "select(" in answer
assert answer.rstrip().endswith("return esp_http_client_fetch_headers(s_client) < 0 ? ESP_FAIL : ESP_OK;\n}"), "no stop: not as ever"
call = re.search(r"static esp_err_t call\(.*?\n\}\n", src, re.S).group(0)
assert call.index("if (stopped())") < call.index("yk_account_token("), "a stopped step fetches a token"
assert "s_stop ? yk_api_stream_complete_stoppable_ms(end->duration_ms) : ms, s_stop != NULL" in src
# 两套部署 · 语音单独同意 V4: a refusal's consent read from the same body as its code, beside it (the code as ever), and
# each of the three calls forgets the one before's first.
assert "*code = yk_api_stream_code(res->status, res->body);\n        s_consent = yk_api_error_consent(res->body);" in call, \
    "the refusal's consent is not read beside its code"
assert src.count("s_consent = yk_api_error_consent(") == 1 and src.count("s_consent = ") == 4, "consent set elsewhere"
for fn in ("yk_cloud_stream_register(", "yk_cloud_stream_chunk(", "yk_cloud_stream_complete("):
    body = src[src.index("esp_err_t " + fn):]
    body = body[:body.index("\n}\n")]
    assert body.index("s_consent = YK_CLOUD_CONSENT_NONE;") < body.index("*code = "), fn + " keeps the call before's consent"
print("yk_stream: stops before the connection, between pieces and while awaiting the answer; as 0.4.0 without one")
print("yk_stream: a refusal's consent read beside its code, forgotten at each call")
PY
  # /me and the kept owner: yaykit_cloud.c itself, against test/stub (Yay's check leaves the language alone); 问字's
  # refusal held for want of voice consent (V5 of 两套部署 · 语音单独同意: yk_cloud_hanzi_consent) also against
  # fixtures/deployments.json's voiceConsentHttp (a kit has none: left out, said so)
  OUT=$(mktemp -d)
  cc -std=c11 -Wall -Wextra -Werror -O1 -I stub -I ../include -I .. -I "$CJSON" -o "$OUT/test_me" test_me.c ../yaykit_cloud.c \
    ../yk_api.c ../yk_sse.c ../yk_locale.c ../yk_codec.c ../yk_region.c "$CJSON/cJSON.c"
  "$OUT/test_me" ${CONSENT_FIXTURE:+"$CONSENT_FIXTURE"}
  cc -std=c11 -Wall -Wextra -Werror -O1 -DCONFIG_YK_CLOUD_HTTP_BUFFER_MIN=4100 -I stub -I ../include -I .. -I "$CJSON" \
    -o "$OUT/test_me" test_me.c ../yaykit_cloud.c ../yk_api.c ../yk_sse.c ../yk_locale.c ../yk_codec.c ../yk_region.c \
    "$CJSON/cJSON.c"
  "$OUT/test_me" ${CONSENT_FIXTURE:+"$CONSENT_FIXTURE"}
  rm -rf "$OUT"
  # Activation and its fallback (a token by the device key after no answer or a 5xx): yaykit_account.c itself,
  # against test/stub, as a development build (`account drop`), with the Kconfig's origin. CONFIG_YK_REGIONS off
  # (every firmware today: the request byte for byte as before, nothing of the regions in NVS, an extension built so on a
  # device bound in cn) and on (附录 C 2.6 on each side); both with the contract's claims (fixtures/deployments.json).
  ORIGIN=$(sed -n '/^config YK_CLOUD_ORIGIN$/,/^config /s/^ *default "\(.*\)"$/\1/p' ../Kconfig)
  [ -n "$ORIGIN" ] || { echo "CONFIG_YK_CLOUD_ORIGIN's default not found in ../Kconfig" >&2; exit 1; }
  FIXTURE="$(cd ../../../../.. && pwd)/packages/contracts/fixtures/deployments.json"
  [ -f "$FIXTURE" ] || FIXTURE=  # a kit has no contract: the claims' cases are left out, said so
  OUT=$(mktemp -d)
  for regions in 0 1; do
    flags=(-DCONFIG_YAY_DEV=1 -DCONFIG_YK_CLOUD_ORIGIN="\"$ORIGIN\"")
    [ $regions = 1 ] && flags+=(-DCONFIG_YK_REGIONS=1)
    cc -std=c11 -Wall -Wextra -Werror -O1 "${flags[@]}" -I stub -I ../include -I .. -I "$CJSON" \
      -o "$OUT/test_account" test_account.c ../yaykit_account.c ../yk_codec.c ../yk_locale.c "$CJSON/cJSON.c"
    "$OUT/test_account" ${FIXTURE:+"$FIXTURE"}
  done
  rm -rf "$OUT"
  # What stands before a request's TLS: yk_http.c itself, against test/stub
  OUT=$(mktemp -d)
  cc -std=c11 -Wall -Wextra -Werror -O1 -I stub -I ../include -I .. -I "$CJSON" -o "$OUT/test_http" test_http.c ../yk_http.c \
    ../yk_region.c "$CJSON/cJSON.c"
  "$OUT/test_http"
  # ... with the buffers a firmware asks for above its malloc limit (Yay)
  cc -std=c11 -Wall -Wextra -Werror -O1 -DCONFIG_YK_CLOUD_HTTP_BUFFER_MIN=4100 -I stub -I ../include -I .. -I "$CJSON" \
    -o "$OUT/test_http" test_http.c ../yk_http.c ../yk_region.c "$CJSON/cJSON.c"
  "$OUT/test_http"
  rm -rf "$OUT"
  # The device's region (两套部署) and its two addresses, yk_cloud_origin() and yaykit_session's yk_session_url():
  # yk_region.c itself, on test/stub, with the Kconfig's origin. CONFIG_YK_REGIONS off (every firmware today: global
  # whatever NVS holds, nothing read), on, and on with a development build's own origin; each also as a development build
  # (CONFIG_YAY_DEV), whose test deployments for a row (yk_region_test.h) only the one with the regions has.
  SESSION=../../yaykit_session
  OUT=$(mktemp -d)
  for build in off on own dev-off dev dev-own; do
    flags=()
    origin=$ORIGIN
    case $build in
      on) flags=(-DCONFIG_YK_REGIONS=1) ;;
      own) flags=(-DCONFIG_YK_REGIONS=1 -DTEST_OWN_ORIGIN=1) origin=https://device.test ;;
      dev-off) flags=(-DCONFIG_YAY_DEV=1) ;;
      dev) flags=(-DCONFIG_YAY_DEV=1 -DCONFIG_YK_REGIONS=1) ;;
      dev-own) flags=(-DCONFIG_YAY_DEV=1 -DCONFIG_YK_REGIONS=1 -DTEST_OWN_ORIGIN=1) origin=https://device.test ;;
    esac
    cc -std=c11 -Wall -Wextra -Werror -O1 ${flags[@]+"${flags[@]}"} -DCONFIG_YK_CLOUD_ORIGIN="\"$origin\"" -I stub \
      -I ../include -I .. -I "$SESSION/include" -I "$SESSION" -I "$CJSON" -o "$OUT/test_region" test_region.c \
      "$SESSION/yk_session_url.c"
    "$OUT/test_region"
  done
  # ... and a build without both settings has none of the test deployments' code, not only none of its use: no
  # yk_region_test_* in yk_region.c's, yaykit_account.c's or yk_session_url.c's object (the one with both has them).
  for build in "" "-DCONFIG_YAY_DEV=1" "-DCONFIG_YK_REGIONS=1" "-DCONFIG_YAY_DEV=1 -DCONFIG_YK_REGIONS=1"; do
    for src in ../yk_region.c ../yaykit_account.c "$SESSION/yk_session_url.c"; do
      # shellcheck disable=SC2086 # the build's settings, as words
      cc -std=c11 -Wall -Wextra -Werror -O1 $build -DCONFIG_YK_CLOUD_ORIGIN="\"$ORIGIN\"" -I stub -I ../include -I .. \
        -I "$SESSION/include" -I "$SESSION" -I "$CJSON" -c -o "$OUT/obj.o" "$src"
      has=$(nm "$OUT/obj.o" | grep -c 'yk_region_test_' || true)
      case "$build" in
        *DEV*REGIONS*) [ "$has" -gt 0 ] || { echo "$src ($build): no yk_region_test_* where it should be" >&2; exit 1; } ;;
        *) [ "$has" = 0 ] || { echo "$src (${build:-release, no regions}): yk_region_test_* built in" >&2; exit 1; } ;;
      esac
    done
  done
  echo "yk_region_test: built only with CONFIG_YAY_DEV and CONFIG_YK_REGIONS (4 builds, 3 sources)"
  rm -rf "$OUT"
else
  echo "device API check skipped: set IDF_PATH for ESP-IDF's cJSON"
fi

# Signatures: build yk_sign.c with ESP-IDF's mbedTLS and verify them with the server's verifier.
if [ -n "${IDF_PATH:-}" ] && [ -d "$IDF_PATH/components/mbedtls/mbedtls/library" ]; then
  MBED="$IDF_PATH/components/mbedtls/mbedtls"
  OUT=$(mktemp -d)
  cc -std=c11 -O1 -w -I "$MBED/include" -I stub -I ../include -o "$OUT/sign_vectors" sign_vectors.c ../yk_sign.c ../yk_codec.c "$MBED"/library/*.c
  ROOT=$(cd ../../../.. && pwd)
  "$OUT/sign_vectors" | (cd "$ROOT" && pnpm exec tsx firmware/components/yaykit_cloud/test/verify_vectors.ts)
  rm -rf "$OUT"
else
  echo "signature check skipped: set IDF_PATH to build with ESP-IDF's mbedTLS"
fi
