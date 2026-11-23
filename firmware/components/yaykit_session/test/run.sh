#!/usr/bin/env bash
# Host tests for yaykit_session's protocol code, against the platform's own examples
# (packages/contracts/fixtures/device.json). cJSON comes from ESP-IDF (IDF_PATH, or ~/esp/esp-idf). The connection's
# timing line and the kept TLS ticket's record need nothing (test_conn.c).
set -euo pipefail
cd "$(dirname "$0")"
# Down from up, the back-off waits (0.1.22, 新四号机 09:31 and ④: a KICK left from while it was up ended it at once, the
# handshake 2-4 ms after the drop): BIT_KICK cleared after the up loop's break, before the drop is counted; the failed
# connection's branch (a KICK there is new) and yk_session_hold untouched.
python3 - ../yaykit_session.c <<'PY'
import re, sys
src = open(sys.argv[1]).read()
task = re.search(r"static void session_task\(void \*arg\) \{\n(.*?)\n\}\n", src, re.S).group(1)
up = task.index("if (got & BIT_DOWN) break;")
clear = task.index("xEventGroupClearBits(S.events, BIT_KICK);")
drops = task.index("S.st.drops++;")
assert up < clear < drops, "cleared after the up loop, before the drop is counted"
assert task.count("xEventGroupClearBits(S.events, BIT_KICK)") == 1, "cleared once, there only"
failed = task[task.index("} else if (!S.usb && !S.hold) {"):task.index("if (!S.usb) ws_close();")]
assert "xEventGroupClearBits" not in failed, "a failed connection: as it was"
hold = re.search(r"bool yk_session_hold\(bool hold\) \{\n(.*?)\n\}\n", src, re.S).group(1)
assert "xEventGroupSetBits(S.events, hold ? BIT_DOWN : BIT_KICK);" in hold and "ClearBits" not in hold
PY
OUT0=$(mktemp -d)
trap 'rm -rf "$OUT0"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O1 -I.. -o "$OUT0/test_conn" test_conn.c ../yk_conn_timing.c ../yk_ticket.c ../yk_conn_drop.c
"$OUT0/test_conn"
rm -rf "$OUT0"
IDF="${IDF_PATH:-$HOME/esp/esp-idf}"
CJSON="$IDF/components/json/cJSON"
if [ ! -f "$CJSON/cJSON.c" ]; then
  echo "yaykit_session host tests skipped: no ESP-IDF for cJSON (set IDF_PATH)"
  exit 0
fi
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O1 -I.. -I"$CJSON" -o "$OUT/test_session" test_session.c ../yk_proto.c \
  "$CJSON/cJSON.c" -lm
# ... and the deployments' hello with its region (两套部署), when the contract is here (not in a kit)
DEPLOYMENTS=../../../../../packages/contracts/fixtures/deployments.json
[ -f "$DEPLOYMENTS" ] || DEPLOYMENTS=
"$OUT/test_session" ../../../../../packages/contracts/fixtures/device.json ${DEPLOYMENTS:+"$DEPLOYMENTS"}
# A hold against a connection being made: yaykit_session.c itself, on test/stub
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -O1 -I stub -I.. -I../include -I"$CJSON" -o "$OUT/test_hold" test_hold.c \
  ../yk_proto.c ../yk_conn_timing.c ../yk_ticket.c ../yk_conn_drop.c "$CJSON/cJSON.c" -lm
"$OUT/test_hold"
rm -rf "$OUT"
