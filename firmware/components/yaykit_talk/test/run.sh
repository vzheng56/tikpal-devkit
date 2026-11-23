#!/usr/bin/env bash
# Host tests for yaykit_talk's plain C parts (the turn state machine, the settings by voice, the hello), the hello
# against the platform's own examples (packages/contracts/fixtures/device.json); and the account's (两套部署 E,
# test_account.c) against yaykit_session.c itself. cJSON comes from ESP-IDF (IDF_PATH, or ~/esp/esp-idf).
set -euo pipefail
cd "$(dirname "$0")"
# 两套部署 E: what test_account.c takes from yaykit_talk.c, as it is there. yk_talk_start asks before it makes anything
# (the microphones, the queues, the task, the session); the session's token is yk_talk_account_token's, on the account
# as it is (added: YK_ACCOUNT_BOUND), and a device not added holds the session.
python3 - ../yaykit_talk.c <<'PY'
import re, sys
src = open(sys.argv[1]).read()
def body(signature):
    return re.search(re.escape(signature) + r" \{\n(.*?)\n\}\n", src, re.S).group(1)
start = body("esp_err_t yk_talk_start(const yk_talk_config_t *cfg)")
gate = start.index("yk_talk_may_start(T.production, yk_account_state() == YK_ACCOUNT_BOUND)")
assert start.index("T.production = ") < gate, "asked once production is known"
answer = start.index("return YK_TALK_ERR_NOT_ADDED;")
for made in ("yk_tsm_init(", "yk_talk_mic_init()", "yk_talk_play_init()", "xQueueCreate", "xTaskCreatePinnedToCore",
             "yk_session_start(&s)", "yk_speak_set_live(true)"):
    assert gate < answer < start.index(made), "nothing made before the answer: " + made
assert ".token = token," in start
assert body("static esp_err_t token(char *out, size_t size, bool fresh)").strip() == \
    "return yk_talk_account_token(&ACCOUNT, T.production, out, size, fresh);"
account = re.search(r"static const yk_talk_account_env_t ACCOUNT = \{(.*?)\};", src, re.S).group(1)
for member in (".token = account_token", ".expired = account_expired", ".added = account_added",
               ".not_added = account_not_added"):
    assert member in account, member
assert "return yk_account_token(out, size, &code);" in body("static esp_err_t account_token(char *out, size_t size, void *ctx)")
assert "yk_account_token_expired();" in body("static void account_expired(void *ctx)")
assert "yk_account_state() == YK_ACCOUNT_BOUND" in src[src.index("static bool account_added(void *ctx)"):][:120]
assert "yk_session_hold(true);" in body("static void account_not_added(void *ctx)")
PY
IDF="${IDF_PATH:-$HOME/esp/esp-idf}"
CJSON="$IDF/components/json/cJSON"
if [ ! -f "$CJSON/cJSON.c" ]; then
  echo "yaykit_talk host tests skipped: no ESP-IDF for cJSON (set IDF_PATH)"
  exit 0
fi
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O1 -I../include -I.. -I../../yaykit_session -I"$CJSON" -o "$OUT/test_talk" \
  test_talk.c ../yk_talk_sm.c ../yk_talk_setting.c ../yk_talk_hello.c ../yk_talk_msg.c ../yk_talk_flush.c ../yk_talk_close.c ../yk_talk_setup.c \
  ../../yaykit_session/yk_proto.c "$CJSON/cJSON.c" -lm
# 两套部署 · 语音单独同意 V3: the contract's refusals (deployments.json voiceConsentSession) as the turn's end.
"$OUT/test_talk" ../../../../../packages/contracts/fixtures/device.json ../../../../../packages/contracts/fixtures/deployments.json
# The account's: yaykit_session.c itself, on its test/stub
S=../../yaykit_session
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -O1 -I "$S/test/stub" -I "$S" -I "$S/include" -I"$CJSON" \
  -o "$OUT/test_account" test_account.c ../yk_talk_account.c "$S/yk_proto.c" "$S/yk_conn_timing.c" "$S/yk_ticket.c" \
  "$S/yk_conn_drop.c" "$CJSON/cJSON.c" -lm
"$OUT/test_account"
rm -rf "$OUT"
