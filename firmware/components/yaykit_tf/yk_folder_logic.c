#include "yk_folder_logic.h"

#include <stdio.h>
#include <string.h>

#include "yk_tf_logic.h"

static const char *const DIRS[YK_FOLDER_KINDS] = {"Music", "Stories", "Pictures", "Documents"};
static const char *const WORDS[YK_FOLDER_KINDS] = {"music", "stories", "pictures", "documents"};

const char *yk_folder_dir_name(int kind) { return kind >= 0 && kind < YK_FOLDER_KINDS ? DIRS[kind] : NULL; }
const char *yk_folder_kind_word(int kind) { return kind >= 0 && kind < YK_FOLDER_KINDS ? WORDS[kind] : NULL; }

// ---------------------------------------------------------------- names

/** The code point at s, its bytes in *len; -1 and 0 bytes for anything but well-formed UTF-8 (RFC 3629). *len is
 *  set on every path, so a caller never reads it unset (gcc -Os could not tell that -1 always returns first). */
static long utf8_next(const unsigned char *s, int *len) {
    *len = 0;
    const unsigned c = s[0];
    if (c < 0x80) return *len = 1, (long)c;
    int n;
    long cp;
    if (c >= 0xC2 && c <= 0xDF) n = 2, cp = c & 0x1F;
    else if (c >= 0xE0 && c <= 0xEF) n = 3, cp = c & 0x0F;
    else if (c >= 0xF0 && c <= 0xF4) n = 4, cp = c & 0x07;
    else return -1;  // a continuation byte, an overlong 2-byte lead (C0, C1), or past F4
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) return -1;  // the NUL included
        cp = cp << 6 | (s[i] & 0x3F);
    }
    if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF))) return -1;  // overlong, too far
    if (cp >= 0xD800 && cp <= 0xDFFF) return -1;                                          // a surrogate
    *len = n;
    return cp;
}

bool yk_folder_path_ok(const char *path, bool empty_ok) {
    if (!path) return false;
    if (!path[0]) return empty_ok;
    const unsigned char *p = (const unsigned char *)path;
    int levels = 0;
    while (*p) {
        // One part, up to the next slash or the end.
        const unsigned char *start = p;
        unsigned units = 0;
        long last = -1;
        while (*p && *p != '/') {
            int len;
            const long cp = utf8_next(p, &len);
            if (cp < 0x20 || cp == 0x7F) return false;  // malformed (-1) or a control character
            if (cp == '\\' || cp == '"' || cp == '*' || cp == ':' || cp == '<' || cp == '>' || cp == '?' || cp == '|')
                return false;
            units += cp >= 0x10000 ? 2 : 1;
            if (units > YK_FOLDER_PATH_PART) return false;
            last = cp;
            p += len;
        }
        const size_t n = (size_t)(p - start);
        if (n == 0) return false;                 // a leading slash, two together, or one at the end
        if (start[0] == '.') return false;        // `.`, `..` and a computer's own files
        if (last == '.' || last == ' ') return false;  // FAT drops them: another name
        if (++levels > YK_FOLDER_PATH_LEVELS) return false;
        if (*p == '/') {
            p++;
            if (!*p) return false;  // ends with a slash
        }
    }
    return true;
}

bool yk_folder_name_hidden(const char *name, bool hidden_or_system) {
    return hidden_or_system || !name || !name[0] || name[0] == '.';
}

// ---------------------------------------------------------------- the answer

yk_grant_t yk_grant_merge(yk_grant_t here, yk_net_t net, const yk_grant_t *service) {
    if (net != YK_NET_ANSWERED || !service) return here;
    // Answered here, not told yet: it stands unless the service changed it since the rev it was made on (the App did).
    if (here.pending && (!here.known ? service->state == YK_GRANT_NONE : service->rev <= here.rev)) return here;
    yk_grant_t g = *service;
    g.known = service->state != YK_GRANT_NONE;
    g.pending = false;
    g.full = false;
    if (g.state == YK_GRANT_NONE) g.folders = 0, g.rev = 0;
    return g;
}

yk_ask_t yk_grant_decide(const yk_grant_t *g, uint8_t declared) {
    declared &= YK_FOLDER_ALL;
    if (g->state == YK_GRANT_DENIED) return YK_ASK_DENIED;
    if (g->state == YK_GRANT_GRANTED && (declared & ~g->folders) == 0) return YK_ASK_ALLOWED;
    return YK_ASK_QUESTION;
}

yk_grant_t yk_grant_answered(const yk_grant_t *before, bool yes, uint8_t declared) {
    yk_grant_t g = *before;
    g.state = yes ? YK_GRANT_GRANTED : YK_GRANT_DENIED;
    g.folders = declared & YK_FOLDER_ALL;
    g.pending = true;
    g.full = false;  // a new answer: the service hears it once more
    return g;
}

bool yk_grant_shrink(yk_grant_t *g, uint8_t declared) {
    const uint8_t keep = g->folders & declared & YK_FOLDER_ALL;
    if (keep == g->folders) return false;
    g->folders = keep;
    return true;
}

yk_grant_t yk_grant_reported(yk_grant_t here, yk_put_t put, const yk_grant_t *service) {
    if (put == YK_PUT_FULL) {
        here.full = true;  // still pending: the next ask tells it once more, nothing in between
        return here;
    }
    if (put == YK_PUT_SILENT || !service) return here;
    yk_grant_t g = *service;
    g.known = true;
    g.pending = false;
    g.full = false;
    return g;
}

yk_grant_t yk_grant_synced(yk_grant_t here, const yk_grant_t *listed) {
    if (here.pending || here.full) return here;  // waits to be told: the 409 then says what the App made of it
    if (!listed || listed->state == YK_GRANT_NONE) return (yk_grant_t){0};
    yk_grant_t g = *listed;
    g.known = true;
    g.pending = false;
    g.full = false;
    return g;
}

yk_grant_t yk_grant_pushed(yk_grant_t here, const yk_grant_t *pushed) {
    if (!pushed || pushed->state == YK_GRANT_NONE || pushed->rev == 0) return here;
    if (here.known && pushed->rev <= here.rev) return here;  // the same or older: what is here stands
    yk_grant_t g = *pushed;
    g.known = true;
    g.pending = false;
    g.full = false;
    return g;
}

bool yk_grant_to_tell(const yk_grant_t *g) {
    return g->pending && !g->full && (g->state == YK_GRANT_GRANTED || g->state == YK_GRANT_DENIED);
}

// ---------------------------------------------------------------- the service's calls, handed over

static const yk_folder_net_t *s_net;

void yk_folder_net_set(const yk_folder_net_t *net) { __atomic_store_n(&s_net, net, __ATOMIC_RELEASE); }
const yk_folder_net_t *yk_folder_net(void) { return __atomic_load_n(&s_net, __ATOMIC_ACQUIRE); }

// ---------------------------------------------------------------- the record

#define FORMAT 1
#define FLAG_KNOWN 1u
#define FLAG_PENDING 2u
#define FLAG_FULL 4u  // 0.1.19's records before it have it 0: the same format
enum { R_FORMAT = 0, R_STATE = 1, R_FOLDERS = 2, R_FLAGS = 3, R_REV = 4, R_SLUG = 8, R_SLUG_SIZE = 32, R_CRC = 40 };
_Static_assert(R_CRC + 4 == YK_GRANT_RECORD, "the record's size");

static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t get32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

void yk_grant_encode(const char *slug, const yk_grant_t *g, uint8_t out[YK_GRANT_RECORD]) {
    memset(out, 0, YK_GRANT_RECORD);
    out[R_FORMAT] = FORMAT;
    out[R_STATE] = g->state;
    out[R_FOLDERS] = g->folders & YK_FOLDER_ALL;
    out[R_FLAGS] = (g->known ? FLAG_KNOWN : 0) | (g->pending ? FLAG_PENDING : 0) | (g->full ? FLAG_FULL : 0);
    put32(out + R_REV, g->rev);
    strncpy((char *)out + R_SLUG, slug, R_SLUG_SIZE - 1);
    put32(out + R_CRC, yk_tf_crc32(out, R_CRC));
}

bool yk_grant_decode(const uint8_t in[YK_GRANT_RECORD], size_t size, const char *slug, yk_grant_t *out) {
    if (size != YK_GRANT_RECORD || in[R_FORMAT] != FORMAT || get32(in + R_CRC) != yk_tf_crc32(in, R_CRC)) return false;
    if (in[R_STATE] > YK_GRANT_DENIED || in[R_SLUG + R_SLUG_SIZE - 1] || strcmp((const char *)in + R_SLUG, slug))
        return false;
    out->state = in[R_STATE];
    out->folders = in[R_FOLDERS] & YK_FOLDER_ALL;
    out->known = in[R_FLAGS] & FLAG_KNOWN;
    out->pending = in[R_FLAGS] & FLAG_PENDING;
    out->full = in[R_FLAGS] & FLAG_FULL;
    out->rev = get32(in + R_REV);
    return true;
}

bool yk_grant_record_slug(const uint8_t in[YK_GRANT_RECORD], size_t size, char slug[YK_GRANT_SLUG]) {
    _Static_assert(R_SLUG_SIZE == YK_GRANT_SLUG, "the record's slug");
    if (size != YK_GRANT_RECORD || in[R_FORMAT] != FORMAT || get32(in + R_CRC) != yk_tf_crc32(in, R_CRC)) return false;
    if (in[R_SLUG + R_SLUG_SIZE - 1] || !in[R_SLUG]) return false;
    memcpy(slug, in + R_SLUG, R_SLUG_SIZE);
    return true;
}

void yk_grant_key(const char *slug, int slot, char out[YK_GRANT_KEY]) {
    snprintf(out, YK_GRANT_KEY, "g%08x%d", (unsigned)yk_tf_crc32(slug, strlen(slug)), slot & 3);
}
