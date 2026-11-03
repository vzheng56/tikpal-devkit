// How long a connection took, in the three moments the device service also logs (docs/delivery/
// 2026-10-27-connecting-and-fast-reconnect.md 4, B): the 101 that upgrades it to a WebSocket, hello sent, hello.ok
// received. One line at each connection's hello.ok:
//
//   YAY_CONN at=2026-10-28T09:12:33.456Z fw=0.1.11 app=yay tls=resumed upgradeMs=812 helloMs=3 helloOkMs=640
//
//   at          hello.ok's own `time` (the platform's Date.now() as it made it, UTC), else the device's clock: only
//               to find the service's line (off by about half a round trip). The two clocks are not one: compare the
//               lengths, not the moments (this helloOkMs - the service's helloOkMs = a round trip to the service)
//   fw, app     from X-Yaykit-Firmware ("yay/0.1.11"); the device service logs the same two, never the device's id
//   tls         resumed (a kept session ticket was taken), full (a whole handshake), none (ws://, development)
//   upgradeMs   the WebSocket client set off (token in hand) to its 101: DNS, TCP, TLS and the service's admitMs +
//               dialMs
//   helloMs     the 101 to hello handed to TLS
//   helloOkMs   hello sent to hello.ok read: one round trip to Fly and the device service's own helloOkMs
//
// Pure C (test/run.sh).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { YK_CONN_TLS_NONE, YK_CONN_TLS_FULL, YK_CONN_TLS_RESUMED } yk_conn_tls_t;

typedef struct {
    int64_t dial_us;       // esp_timer: the WebSocket client set off
    int64_t upgrade_us;    // its 101 came (WEBSOCKET_EVENT_CONNECTED)
    int64_t hello_us;      // hello handed to TLS
    int64_t ok_us;         // hello.ok read
    int64_t at_ms;         // UTC milliseconds of hello.ok (0: not known)
    const char *firmware;  // "<app>/<version>"
    yk_conn_tls_t tls;
} yk_conn_timing_t;

const char *yk_conn_tls_name(yk_conn_tls_t tls);
/** The line above, without a newline. A moment not seen (0) or out of order is "-". Returns snprintf's length. */
int yk_conn_timing_line(char *out, size_t size, const yk_conn_timing_t *t);

#ifdef __cplusplus
}
#endif
