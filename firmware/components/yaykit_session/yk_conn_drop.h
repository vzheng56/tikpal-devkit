// How the session went away and came back (Yay 0.1.20 开发文档 4.8, 4.10 ③; F-1078, F-1127): the report's `net`
// (packages/contracts device-api.ts statusReport.net). Only reason codes, counts and lengths: no network name, no
// address. Pure C (test/test_conn.c); yaykit_session.c feeds it and keeps it behind its lock.
//
// A drop is the session going down after it was up (hello sent), not closed on purpose (held for the phone link,
// yk_session_close). Its cause is read at that moment; while it stays down, a connection that fails for a plainer
// reason than the drop's own (a timeout, or something else, then the router is gone) tells why it stayed down, and
// that is the cause kept. Back up: how long it took and how many connections were tried.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YK_DROP_NONE,
    YK_DROP_WIFI,         // off the router: Wi-Fi disconnected or never joined
    YK_DROP_NO_INTERNET,  // on the router, no way out: the name not resolved, the host not reached
    YK_DROP_SERVER,       // out, but the platform did not take it: TLS, HTTP or the WebSocket refused
    YK_DROP_CLOSED,       // the server closed the session (its close code)
    YK_DROP_TIMEOUT,      // nothing came back in time (pings, the connection's wait)
    YK_DROP_OTHER,
} yk_drop_cause_t;

/** The contract's word for a cause: "wifi", "no_internet", "server", "closed", "timeout", "other" ("" none). */
const char *yk_drop_cause_word(yk_drop_cause_t c);

/** What was seen as a connection ended or failed. */
typedef struct {
    bool ip;              // an address on Wi-Fi then
    int close_code;       // the server's close frame's code, 0 none
    bool pong_timeout;    // the WebSocket client gave up waiting for pongs
    bool wait_timeout;    // the connection's own wait ran out, nothing back
    bool handshake;       // the HTTP upgrade was refused (the WebSocket client's HANDSHAKE error)
    int tls_error;        // esp-tls's last error then (ESP_ERR_ESP_TLS_*), 0 none
} yk_drop_seen_t;

// esp-tls's errors as the classifier knows them (esp_tls_errors.h; mirrored so that this stays pure)
#define YK_DROP_TLS_BASE 0x8000
#define YK_DROP_TLS_CANNOT_RESOLVE (YK_DROP_TLS_BASE + 0x01)
#define YK_DROP_TLS_CANNOT_CREATE_SOCKET (YK_DROP_TLS_BASE + 0x02)
#define YK_DROP_TLS_UNSUPPORTED_FAMILY (YK_DROP_TLS_BASE + 0x03)
#define YK_DROP_TLS_FAILED_CONNECT (YK_DROP_TLS_BASE + 0x04)
#define YK_DROP_TLS_CONNECTION_TIMEOUT (YK_DROP_TLS_BASE + 0x06)

yk_drop_cause_t yk_drop_classify(const yk_drop_seen_t *s);

typedef struct {
    uint32_t drops;         // since the last report that carried them went out (yk_drop_taken)
    bool have;              // a drop is known: the fields below
    yk_drop_cause_t cause;
    int wifi_reason;        // ESP-IDF's wifi_err_reason_t at the drop (or the try that told), -1 none
    int close_code;         // -1 none
    int64_t at_unix_s;      // when it went, 0 the clock not set
    int64_t down_ms;        // ... by the monotonic clock
    bool down;              // not back yet
    int32_t down_s;         // seconds until it was back (-1 while down)
    int32_t tries;          // connections tried since it went (counting while down)
} yk_drop_t;

/** It went: `cause`, `wifi_reason` (-1 none), `close_code` (0 none), at `now_ms` (monotonic) and `unix_s` (0 not set). */
void yk_drop_went(yk_drop_t *d, yk_drop_cause_t cause, int wifi_reason, int close_code, int64_t now_ms, int64_t unix_s);
/** A connection tried while down, and how it failed (YK_DROP_NONE: it did not fail, or not yet known). A plainer
 *  reason than the drop's own (it was a timeout or other) is kept as the cause. */
void yk_drop_try(yk_drop_t *d, yk_drop_cause_t failed, int wifi_reason);
/** Up again at `now_ms`. */
void yk_drop_back(yk_drop_t *d, int64_t now_ms);
/** A report carrying `n` drops went out: those are said. The latest drop stays known (the next report says it too,
 *  with 0 drops, until a newer one). */
void yk_drop_taken(yk_drop_t *d, uint32_t n);

#ifdef __cplusplus
}
#endif
