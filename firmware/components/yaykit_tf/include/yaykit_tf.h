// The TF card (development doc docs/delivery/2026-11-06-固件公共能力.md 3.3): an app's own directory on it, mounted only
// while someone holds it.
//
//   if (yk_tf_acquire() == ESP_OK) {
//       char path[128];
//       if (yk_tf_path("sounds/rain.pcm", path, sizeof(path)) == ESP_OK) { ... fopen(path, "r") ... }
//       yk_tf_release();
//   }
//
// The card is never needed: anything but ESP_OK from yk_tf_acquire(), and any read or write that fails, means carry on
// without it (another source, or nothing). There is nothing to tell the user but what the app itself does without it.
//
// Rules:
//  - Hold it only while using it. Mounted it costs internal RAM, about 760 bytes; a transfer about 4.9 KB, or 1.3 KB
//    when internal RAM is low. Close your files before yk_tf_release(), and in yk_app_on_leave().
//  - Only your own directory, yk_tf_dir(). Nothing else on the card is yours, and nothing here reaches it. Do not mount
//    the card yourself.
//  - Not while Bluetooth is on (yk_tf_acquire() refuses), and not from a screen or audio task: it can block for a second
//    or more, and a stalled card holds up everyone using it. The caller's task needs 4 KB of stack for it.
//  - FAT32 only: exFAT and anything else is refused, never formatted.
//  - How long a write takes is not known yet (nothing measured on any card): no timing may depend on it.
//
// Build: `CONFIG_FATFS_LFN_HEAP=y` and `CONFIG_FATFS_ALLOC_PREFER_EXTRAM=y` in sdkconfig.defaults, or FatFs's buffers
// would take internal RAM (the build stops with an #error).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "yk_tf_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One more holder; the first one mounts. Every ESP_OK needs its yk_tf_release().
 *  ESP_ERR_INVALID_STATE: not now (Bluetooth on, the system wants the card, the chip is restarting).
 *  ESP_ERR_NO_MEM: internal RAM is too low for it. ESP_ERR_NOT_FOUND: the card has failed too often and rests.
 *  ESP_ERR_NOT_SUPPORTED: not a FAT volume. Anything else: the card did not answer. */
esp_err_t yk_tf_acquire(void);

/** One holder less; the last one unmounts. A release without an acquire does nothing. */
void yk_tf_release(void);

/** This app's directory, /sdcard/apps/<slug>: the slug from the image's project name. Exists once the card is held.
 *  NULL when the image's name is no app's. */
const char *yk_tf_dir(void);

/** The full path of `rel` in yk_tf_dir(), into out (n bytes). Mounted or not; no file is touched.
 *  ESP_ERR_INVALID_ARG: `rel` is not a plain relative path: up to 200 characters, 8 levels, of A-Z a-z 0-9 . _ - and /
 *  (no `..`, no absolute path; no part starts or ends with a dot). ESP_ERR_INVALID_SIZE: out is too small.
 *  out is "" unless ESP_OK. */
esp_err_t yk_tf_path(const char *rel, char *out, size_t n);

/** While holding the card: what this app takes, its quota, and what is free on the card, in bytes (any may be NULL).
 *  Reads every directory of the app: not for every frame. The quota is not enforced for you: stay under it.
 *  ESP_ERR_INVALID_STATE: nobody holds the card. */
esp_err_t yk_tf_space(uint64_t *used, uint64_t *quota, uint64_t *card_free);

yk_tf_state_t yk_tf_state(void);

#ifdef __cplusplus
}
#endif
