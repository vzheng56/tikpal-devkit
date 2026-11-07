// yk info's text a part at a time (yk_link_text.h). Pure C, host-tested (test/run.sh).
#include "yk_link_text.h"

#include <string.h>

#define FIRST_CAP 1536  // Yay's yk info is ~1,300 bytes: one buffer, no growing

void yk_text_init(yk_text_t *t, void *(*alloc)(size_t), void *(*fallback)(size_t), void (*release)(void *)) {
    memset(t, 0, sizeof(*t));
    t->alloc = alloc;
    t->fallback = fallback;
    t->release = release;
}

static bool reserve(yk_text_t *t, size_t more) {
    if (t->failed) return false;
    if (t->len + more + 1 <= t->cap) return true;
    size_t cap = t->cap ? t->cap : FIRST_CAP;
    while (cap < t->len + more + 1) cap *= 2;
    char *buf = t->alloc(cap);
    if (!buf) buf = t->fallback(cap);
    if (!buf) {
        t->failed = true;
        return false;
    }
    if (t->len) memcpy(buf, t->buf, t->len);
    buf[t->len] = 0;
    if (t->buf) t->release(t->buf);
    t->buf = buf;
    t->cap = cap;
    return true;
}

static void append(yk_text_t *t, const char *s, size_t n) {
    if (!reserve(t, n)) return;
    memcpy(t->buf + t->len, s, n);
    t->len += n;
    t->buf[t->len] = 0;
}

void yk_text_raw(yk_text_t *t, const char *text) { append(t, text, strlen(text)); }

/** A comma unless this is the first member of the object or array just opened. */
static void comma(yk_text_t *t) {
    if (t->len && t->buf[t->len - 1] != '{' && t->buf[t->len - 1] != '[') append(t, ",", 1);
}

void yk_text_key(yk_text_t *t, const char *key) {
    // The keys are yaykit_link.c's own: plain ASCII, nothing to escape.
    comma(t);
    append(t, "\"", 1);
    yk_text_raw(t, key);
    append(t, "\":", 2);
}

void yk_text_add(yk_text_t *t, const char *key, cJSON *item) {
    if (!item) {
        t->failed = true;
        return;
    }
    if (key) yk_text_key(t, key);
    else comma(t);
    char *text = t->failed ? NULL : cJSON_PrintUnformatted(item);
    cJSON_Delete(item);
    if (!text) {
        t->failed = true;
        return;
    }
    yk_text_raw(t, text);
    cJSON_free(text);
}

char *yk_text_take(yk_text_t *t) {
    char *buf = t->buf;
    const bool ok = !t->failed && buf;
    if (!ok && buf) t->release(buf);
    t->buf = NULL;
    t->len = t->cap = 0;
    return ok ? buf : NULL;
}
