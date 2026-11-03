#include "yk_ticket.h"

#include <stddef.h>
#include <string.h>

#define MAGIC 0x31544B59u  // "YKT1"

// The header, copied in and out byte by byte (RTC memory, no alignment assumed).
typedef struct {
    uint32_t magic;
    uint32_t crc;       // CRC-32 of the rest of the header and the session
    uint32_t host;      // FNV-1a of the host name
    uint32_t age_ms;    // the ticket's age when kept
    uint64_t clock_us;  // the RTC clock then
    uint32_t len;       // bytes of the session after the header
    uint32_t life_s;    // what the ticket says it lasts (0: nothing said); the header has no padding
} head_t;

const char *yk_ticket_why_name(yk_ticket_why_t why) {
    switch (why) {
    case YK_TICKET_OK: return "ok";
    case YK_TICKET_NONE: return "none";
    case YK_TICKET_DAMAGED: return "damaged";
    case YK_TICKET_OTHER_HOST: return "other_host";
    case YK_TICKET_OLD: return "old";
    }
    return "?";
}

static uint32_t crc32(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = crc & 1 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return ~crc;
}

static uint32_t fnv(const char *s) {
    uint32_t h = 2166136261u;
    for (; s && *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

/** The CRC over the header after its crc field, and the session. */
static uint32_t seal(const head_t *h, const uint8_t *session) {
    const uint8_t *p = (const uint8_t *)h;
    const size_t skip = offsetof(head_t, host);
    return crc32(crc32(0, p + skip, sizeof(*h) - skip), session, h->len);
}

void yk_ticket_clear(uint8_t *area, size_t size) {
    if (area && size >= sizeof(uint32_t)) memset(area, 0, sizeof(uint32_t));  // the magic: nothing kept
}

bool yk_ticket_put(uint8_t *area, size_t size, const char *host, uint64_t clock_us, uint32_t age_ms,
                   uint32_t lifetime_s, const uint8_t *session, size_t len) {
    if (!area) return false;
    if (!session || !len || size < sizeof(head_t) || len > size - sizeof(head_t)) {
        yk_ticket_clear(area, size);
        return false;
    }
    head_t h = {.magic = 0, .host = fnv(host), .age_ms = age_ms, .clock_us = clock_us, .len = (uint32_t)len,
                .life_s = lifetime_s};
    yk_ticket_clear(area, size);  // a restart halfway leaves no record, not half of one
    memcpy(area + sizeof(h), session, len);
    h.crc = seal(&h, area + sizeof(h));
    memcpy(area, &h, sizeof(h));
    h.magic = MAGIC;
    memcpy(area, &h.magic, sizeof(h.magic));  // last
    return true;
}

yk_ticket_why_t yk_ticket_get(const uint8_t *area, size_t size, const char *host, uint64_t clock_us,
                              const uint8_t **session, size_t *len, uint32_t *age_ms) {
    *session = NULL;
    *len = 0;
    *age_ms = 0;
    if (!area || size < sizeof(head_t)) return YK_TICKET_NONE;
    head_t h;
    memcpy(&h, area, sizeof(h));
    if (h.magic != MAGIC) return h.magic ? YK_TICKET_DAMAGED : YK_TICKET_NONE;
    if (!h.len || h.len > size - sizeof(h) || seal(&h, area + sizeof(h)) != h.crc) return YK_TICKET_DAMAGED;
    if (h.host != fnv(host)) return YK_TICKET_OTHER_HOST;
    if (clock_us < h.clock_us) return YK_TICKET_OLD;  // the RTC started again: it was a power cut
    const uint64_t age = h.age_ms + (clock_us - h.clock_us) / 1000;
    // Its own lifetime or a day, whichever is shorter.
    const uint64_t life = h.life_s && (uint64_t)h.life_s * 1000 < (uint64_t)YK_TICKET_MAX_AGE_MS
                              ? (uint64_t)h.life_s * 1000
                              : (uint64_t)YK_TICKET_MAX_AGE_MS;
    if (age > life) return YK_TICKET_OLD;
    *session = area + sizeof(h);
    *len = h.len;
    *age_ms = (uint32_t)age;
    return YK_TICKET_OK;
}

bool yk_ticket_url(const char *url, char *host, size_t host_size, const char **path) {
    static const char scheme[] = "wss://";
    if (host_size) host[0] = 0;
    *path = "/";
    if (!url || strncmp(url, scheme, sizeof(scheme) - 1)) return false;
    const char *h = url + sizeof(scheme) - 1;
    size_t n = strcspn(h, ":/?");
    if (!n || n >= host_size) return false;
    memcpy(host, h, n);
    host[n] = 0;
    const char *slash = strchr(h + n, '/');
    if (slash) *path = slash;
    return true;
}
