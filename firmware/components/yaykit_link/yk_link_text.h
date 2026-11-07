// yk info's text a part at a time (yaykit_link.c yk_link_info_json): each part a small cJSON tree, printed and freed
// at once, the text growing in a buffer of its own. The whole tree at once took ~6 KB of internal RAM (its blocks are
// under CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL), enough to take Yay under the 40,000 bytes line when read while its
// start's https ran (#1239); a part now takes a few hundred bytes. Pure C, host-tested (test/run.sh).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *(*alloc)(size_t size);     // the buffer's first choice (PSRAM on the device)
    void *(*fallback)(size_t size);  // when that fails (internal RAM): yk info never fails for want of PSRAM
    void (*release)(void *p);        // frees either
    char *buf;
    size_t len, cap;
    bool failed;  // an allocation or a part failed: yk_text_take() gives NULL
} yk_text_t;

void yk_text_init(yk_text_t *t, void *(*alloc)(size_t), void *(*fallback)(size_t), void (*release)(void *));
/** Appends `text` as it is ("{", "[", "]", "}"). */
void yk_text_raw(yk_text_t *t, const char *text);
/** Appends `"key":` (a comma first when the object has members already); then a value must follow. `key` is plain
 *  ASCII with nothing to escape (yaykit_link.c's own keys). */
void yk_text_key(yk_text_t *t, const char *key);
/** Appends `item` printed (a comma first when the array or object has members already; `"key":` first when `key` is
 *  not NULL), then deletes it. A NULL item (a failed allocation) fails the text. */
void yk_text_add(yk_text_t *t, const char *key, cJSON *item);
/** The text, NUL-terminated, to free with t->release (free() on the device); NULL when anything failed (all freed). */
char *yk_text_take(yk_text_t *t);

#ifdef __cplusplus
}
#endif
