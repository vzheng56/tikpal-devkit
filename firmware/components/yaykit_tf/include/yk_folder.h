// The four shared folders on the card (Yay 0.1.19 开发文档 4.6, 6.2): Music, Stories, Pictures and Documents, the
// person's own files, copied in from a computer. An app only reads them, never writes, deletes or renames, and only
// the kinds its app.json asks for (`permissions.folders`); the first time it really needs one, the round screen asks
// the person once. Its own directory is yaykit_tf.h's, apart from these.
//
//   static void on_answer(yk_folder_answer_t answer, void *ctx) {
//       if (answer != YK_FOLDER_ALLOWED) return;            // say nothing more: the person has answered
//       yk_folder_entry_t *e = heap_caps_malloc(8 * sizeof(*e), MALLOC_CAP_SPIRAM);
//       size_t n = 0;
//       if (e && yk_folder_list(YK_FOLDER_MUSIC, "", 0, e, 8, &n) == ESP_OK) { ... e[i].name ... }
//       free(e);
//   }
//   yk_folder_ask(YK_FOLDER_MUSIC, on_answer, NULL);      // when the person taps play, not at the start
//
// Rules:
//  - Ask when the person wants it (a tap on play), never at the start, on a timer or again and again: the question
//    covers the app's screen. YK_FOLDER_NOT_NOW (the side key, or 30 s untouched) is not a no: ask again the next
//    time they want it, and do not send them to the phone.
//  - Reading needs YK_FOLDER_ALLOWED from yk_folder_ask() in this run: until then every call below says
//    ESP_ERR_NOT_ALLOWED, and none of them ever shows the question.
//  - The card is held for you: from the first open (or a list) to the last close. Close your files in
//    yk_app_on_leave(). Not while Bluetooth is on (yaykit_tf.h), nor while the card is a computer's USB disk:
//    ESP_ERR_INVALID_STATE then.
//  - Not from a screen or audio task: a call may wait a second or more for the card. The caller's task needs 4 KB of
//    stack, as for yaykit_tf.h.
//  - Names are the person's, as UTF-8: Chinese and every other script. A name the screen's fonts lack a character of
//    is still read the same; draw it as it is (the screen shows 「□」 where a glyph is missing).
//  - Only reading. Neither the system's area (/sdcard/.tikpal) nor another app's directory can be reached through
//    here; the review turns down an app that goes around it (mounting the card itself, FatFs, paths of its own).
//
// The question's words are the system's (four languages), never the app's: its name, 「想读你的音乐和故事」, 「允许」 and
// 「不允许」. The person changes their mind on the phone (the device's 「应用权限」), which this app sees the next time it
// asks. Build: as yaykit_tf.h; the kinds come from the project's app.json at build time.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The four folders, at the card's root under these names. Only ever added to, at the end. */
typedef enum {
    YK_FOLDER_MUSIC = 0,      // Music
    YK_FOLDER_STORIES = 1,    // Stories
    YK_FOLDER_PICTURES = 2,   // Pictures
    YK_FOLDER_DOCUMENTS = 3,  // Documents
    YK_FOLDER_COUNT,
} yk_folder_t;

/** What yk_folder_ask() answers. */
typedef enum {
    YK_FOLDER_ALLOWED = 0,      // read away
    YK_FOLDER_DENIED,           // the person said no, here or on the phone: nothing of any folder, until they change it
    YK_FOLDER_NOT_NOW,          // they let it go (the side key, 30 s): not a no; ask again when they next want it
    YK_FOLDER_NOT_DECLARED,     // app.json's permissions.folders does not have this kind: never asked, never read
    YK_FOLDER_NO_CARD,          // no card, or one that cannot be read (not FAT, failing): nothing to read
    YK_FOLDER_USB,              // the card is a computer's USB disk right now: back once it is ejected
} yk_folder_answer_t;

typedef void (*yk_folder_cb_t)(yk_folder_answer_t answer, void *ctx);

/** Before reading `folder`: may this app? Returns at once; `cb` comes later, once, on a task of yaykit_tf's (it may
 *  read from there). Shows the question only when the person has not answered for every kind the app asks for.
 *  ESP_OK: cb will come. ESP_ERR_INVALID_ARG: no such folder or no cb. ESP_ERR_INVALID_STATE: another ask is under
 *  way (no cb for this one). ESP_ERR_NO_MEM: nothing could start (no cb). */
esp_err_t yk_folder_ask(yk_folder_t folder, yk_folder_cb_t cb, void *ctx);

// ---------------------------------------------------------------- reading (after YK_FOLDER_ALLOWED)

#define YK_FOLDER_NAME_MAX 255  // bytes of a name, UTF-8; a longer one is listed by its short name (8.3), which opens it
#define YK_FOLDER_LEVELS 8      // a path's parts at most
#define YK_FOLDER_PART_MAX 255  // UTF-16 units in one part (FatFs's long names): a character, or two for one outside
                                // the basic plane (most emoji)

/** One thing in a folder. */
typedef struct {
    char name[YK_FOLDER_NAME_MAX + 1];  // UTF-8
    uint64_t size;                      // bytes; 0 for a folder
    bool dir;
} yk_folder_entry_t;

/** What is in `sub` (a path as below; "" the folder itself), from the `from`th thing on, up to `max` of them, in the
 *  card's order: `*got` says how many. Names that start with a dot, and hidden or system ones, are left out (a
 *  computer's own files) and not counted in `from`. Fewer than `max`: that was the last. Put `out` in PSRAM.
 *  ESP_ERR_NOT_FOUND: no such folder. ESP_ERR_INVALID_ARG: `sub` is not such a path. ESP_ERR_NOT_ALLOWED: not
 *  allowed (above). ESP_ERR_INVALID_STATE: the card cannot be used now (Bluetooth, a computer's USB disk, none).
 *  ESP_FAIL: the card did not answer. */
esp_err_t yk_folder_list(yk_folder_t folder, const char *sub, size_t from, yk_folder_entry_t *out, size_t max,
                         size_t *got);

typedef struct yk_folder_file yk_folder_file_t;

/** Opens `path` in `folder` to read. A path: parts split by single slashes, relative to the folder (no leading
 *  slash), none of them `.`, `..`, starting with a dot, or ending in a dot or a space; no backslash, no control
 *  characters, none of `"*:<>?|`; well-formed UTF-8; at most YK_FOLDER_LEVELS parts of YK_FOLDER_PART_MAX each.
 *  `size` (may be NULL): the file's bytes. At most 4 open at a time.
 *  ESP_ERR_NOT_FOUND: no such file (or a folder). ESP_ERR_INVALID_ARG: not such a path. ESP_ERR_NOT_ALLOWED: not
 *  allowed. ESP_ERR_INVALID_STATE: the card cannot be used now. ESP_ERR_NO_MEM: 4 open already, or no memory.
 *  ESP_FAIL: the card. */
esp_err_t yk_folder_open(yk_folder_t folder, const char *path, yk_folder_file_t **out, uint64_t *size);
/** Up to `size` bytes into `buf`; `*got` 0 at the end. ESP_FAIL: the card did not answer. */
esp_err_t yk_folder_read(yk_folder_file_t *f, void *buf, size_t size, size_t *got);
/** To `offset` bytes from the start (past the end: the end). */
esp_err_t yk_folder_seek(yk_folder_file_t *f, uint64_t offset);
/** Done with it; the last one closed lets the card go. NULL does nothing. */
void yk_folder_close(yk_folder_file_t *f);

#ifdef __cplusplus
}
#endif
