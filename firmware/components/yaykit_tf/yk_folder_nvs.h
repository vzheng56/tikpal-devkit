// This device's records of the person's answers to the shared folders' question, one per app, in NVS namespace
// `yk_sys_folder` (no app's own can be called that): yk_folder.c keeps its app's, Yay's check (yk_folder_sys.c) every
// app's. Keys: yk_grant_key(), a hash of the slug, a slug another took moving on, four slots at most; the record keeps
// its slug (yk_folder_logic.c). Call from a task with its stack in internal RAM: an NVS write stops the flash cache.
#pragma once

#include <stdbool.h>

#include "yk_folder_logic.h"

/** `slug`'s record ({0}, state NONE, when there is none), and the key it is under (a free one when none). */
yk_grant_t yk_folder_record_load(const char *slug, char key[YK_GRANT_KEY]);
/** Whether there is a record of `slug` at all (asked once on this device, whatever the answer). */
bool yk_folder_record_known(const char *slug);
/** Writes `g` as `slug`'s record under `key` (yk_folder_record_load's). */
void yk_folder_record_save(const char *slug, const char *key, const yk_grant_t *g);
/** The two say the same (every field). */
bool yk_folder_record_same(const yk_grant_t *a, const yk_grant_t *b);
/** Each record there is, its slug, key and answer to `fn` (no NVS writes inside it); how many. */
typedef void (*yk_folder_record_fn)(const char *slug, const char *key, const yk_grant_t *g, void *ctx);
int yk_folder_record_each(yk_folder_record_fn fn, void *ctx);
