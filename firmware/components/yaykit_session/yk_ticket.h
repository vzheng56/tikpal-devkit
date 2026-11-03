// A TLS session ticket kept across restarts (docs/delivery/2026-10-27-connecting-and-fast-reconnect.md 4, fast
// reconnect 3): an app switch is a restart, and with the ticket the next firmware (Yay, or an extension on
// yaykit_talk) resumes the session's TLS instead of a whole handshake. It lives in RTC memory, which a software
// restart keeps and a power cut does not; never in flash. yaykit_session puts the record in the last YK_TICKET_AREA
// bytes of RTC slow memory, the same address in every firmware (each one's own RTC variables sit at the bottom).
//
// The rules (the device service, 10-28): a ticket is good for the lifetime it says (Fly's edge gives 86400 s, not
// ours to set) and never more than a day, whichever is shorter; one is used once (offered again it is taken
// 1 time in 10), so it is dropped when offered and the connection's new one kept at hello.ok. Not taken (another
// edge of the region, or the server's keys changed): a whole handshake, nothing fails.
//
// The record's format and these rules are pure C (test/run.sh).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bytes of RTC slow memory, the header included. A TLS 1.2 session carries the server's own certificate (914 bytes
// for device.yaykit.io, 10-28): ~1.3 KB in all; TLS 1.3's ~0.4 KB. RTC slow memory is 8 KB, each firmware's own RTC
// variables tens of bytes at its bottom.
#define YK_TICKET_AREA 2048
#define YK_TICKET_MAX_AGE_MS (24LL * 3600 * 1000)   // older than a day (or its own lifetime, if shorter): not offered

typedef enum {
    YK_TICKET_OK,
    YK_TICKET_NONE,        // nothing kept (or used already)
    YK_TICKET_DAMAGED,     // the record does not check out (a power cut leaves RTC memory random)
    YK_TICKET_OTHER_HOST,  // kept for another server (a development one)
    YK_TICKET_OLD,         // past its lifetime or a day, or the clock went back (a power cut)
} yk_ticket_why_t;

const char *yk_ticket_why_name(yk_ticket_why_t why);

/** Keeps `len` bytes of a serialized session for `host` in `area` (`size` bytes): `clock_us` is a clock that goes on
 *  across restarts (the RTC's), `age_ms` the ticket's age now, `lifetime_s` what the ticket says it lasts (0: it says
 *  nothing; a day). False when it does not fit: the area is cleared. */
bool yk_ticket_put(uint8_t *area, size_t size, const char *host, uint64_t clock_us, uint32_t age_ms,
                   uint32_t lifetime_s, const uint8_t *session, size_t len);
/** The session kept for `host` if it is still good at `clock_us`: `*session` points into `area`, `*age_ms` is its
 *  age now. */
yk_ticket_why_t yk_ticket_get(const uint8_t *area, size_t size, const char *host, uint64_t clock_us,
                              const uint8_t **session, size_t *len, uint32_t *age_ms);
/** Forgets it (used once, or not wanted). */
void yk_ticket_clear(uint8_t *area, size_t size);

/** "wss://host[:port]/path": the host into `host` and the path (or "/") in *path. False for anything but wss://. */
bool yk_ticket_url(const char *url, char *host, size_t host_size, const char **path);

#ifdef __cplusplus
}
#endif
