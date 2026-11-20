// How the session went away and came back (yk_conn_drop.h).
#include "yk_conn_drop.h"

#include <string.h>

const char *yk_drop_cause_word(yk_drop_cause_t c) {
    static const char *const W[] = {"", "wifi", "no_internet", "server", "closed", "timeout", "other"};
    return c >= 0 && c < (int)(sizeof(W) / sizeof(W[0])) ? W[c] : "other";
}

yk_drop_cause_t yk_drop_classify(const yk_drop_seen_t *s) {
    if (!s->ip) return YK_DROP_WIFI;                     // nothing else can be told without the router
    if (s->close_code > 0) return YK_DROP_CLOSED;        // the server said so
    switch (s->tls_error) {                              // on the router: did it get out?
    case YK_DROP_TLS_CANNOT_RESOLVE:
    case YK_DROP_TLS_CANNOT_CREATE_SOCKET:
    case YK_DROP_TLS_UNSUPPORTED_FAMILY:
    case YK_DROP_TLS_FAILED_CONNECT: return YK_DROP_NO_INTERNET;
    case YK_DROP_TLS_CONNECTION_TIMEOUT: return YK_DROP_TIMEOUT;
    case 0: break;
    default: return YK_DROP_SERVER;                      // out, but TLS did not go through
    }
    if (s->handshake) return YK_DROP_SERVER;             // the upgrade refused
    if (s->pong_timeout || s->wait_timeout) return YK_DROP_TIMEOUT;
    return YK_DROP_OTHER;
}

static bool vague(yk_drop_cause_t c) { return c == YK_DROP_TIMEOUT || c == YK_DROP_OTHER; }

void yk_drop_went(yk_drop_t *d, yk_drop_cause_t cause, int wifi_reason, int close_code, int64_t now_ms, int64_t unix_s) {
    d->drops++;
    d->have = true;
    d->cause = cause == YK_DROP_NONE ? YK_DROP_OTHER : cause;
    d->wifi_reason = cause == YK_DROP_WIFI ? wifi_reason : -1;
    d->close_code = close_code > 0 ? close_code : -1;
    d->at_unix_s = unix_s;
    d->down_ms = now_ms;
    d->down = true;
    d->down_s = -1;
    d->tries = 0;
}

void yk_drop_try(yk_drop_t *d, yk_drop_cause_t failed, int wifi_reason) {
    if (!d->have || !d->down) return;
    if (failed == YK_DROP_NONE) {
        d->tries++;
        return;
    }
    if (vague(d->cause) && !vague(failed) && failed != YK_DROP_CLOSED) {  // why it stayed down
        d->cause = failed;
        d->wifi_reason = failed == YK_DROP_WIFI ? wifi_reason : -1;
    }
}

void yk_drop_back(yk_drop_t *d, int64_t now_ms) {
    if (!d->have || !d->down) return;
    d->down = false;
    const int64_t ms = now_ms - d->down_ms;
    d->down_s = ms <= 0 ? 0 : (int32_t)((ms + 500) / 1000);
    if (d->tries < 1) d->tries = 1;  // the one that came back
}

void yk_drop_taken(yk_drop_t *d, uint32_t n) { d->drops = n >= d->drops ? 0 : d->drops - n; }
