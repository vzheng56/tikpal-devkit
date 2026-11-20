// yaykit_install's types, and what a firmware with a TF card gives it: apart from yaykit_install.h so that the code on the
// other side of these needs no cJSON or ESP-IDF to be tested on the host.
//
// The card is reached through these and nothing else (docs/delivery/2026-11-06-固件公共能力.md 3.3): this component does not
// link FatFs, and the recovery, which shares it, sets no source and has no card.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What to install: app.install's fields, or a /device/v1/apps item. */
typedef struct {
    char slug[32];
    char name[48];        // for people ("呼吸之间"); may be empty
    char version[32];
    uint32_t size;
    char sha256[65];      // 64 lowercase hex
    char url[384];        // https:// (development builds: http:// too); empty: from the package cache only
    char signature[112];  // "yks1.<key id>.<r‖s>" (firmware/signing/README.md)
} yk_install_manifest_t;

/** One image in the package cache. */
typedef struct {
    char slug[32];
    char name[48];
    char version[32];
    uint32_t size;
} yk_install_cached_t;

/** Somewhere beside the package cache that signed images may be: the TF card. Every call but `known` runs on the install's
 *  task and may take seconds; a card that fails is a miss, never an error of the install. */
typedef struct {
    /** This very image is there, as far as is known without touching the card: from any task, at once. May be wrong. */
    bool (*known)(void *ctx, const char *slug, const char *sha256);
    /** This very image is there, read whole and right: an app the slot can do without keeping a backup of. */
    bool (*holds)(void *ctx, const char *slug, const char *sha256);
    /** The same, and open from its first byte. True: read() it to the end, then close(). */
    bool (*open)(void *ctx, const char *slug, const char *sha256, uint32_t size);
    /** The next `size` bytes. False when they cannot be had. */
    bool (*read)(void *ctx, void *buf, uint32_t size);
    void (*close)(void *ctx);
} yk_install_source_t;

/** At start, after yk_install_init(), before any install; the firmware's own, for good. NULL (the default): none. */
void yk_install_set_source(const yk_install_source_t *source, void *ctx);

/** begin()'s answer when the image is there already: nothing comes, finish() is not called. */
#define YK_INSTALL_SINK_HAVE "HAVE"

/** Where yk_install_fetch_to() puts an image instead of the package cache. Each error is an install error code, or the
 *  sink's own: the listener hears it. */
typedef struct {
    const char *(*begin)(void *ctx, const yk_install_manifest_t *m);
    const char *(*write)(void *ctx, const void *data, uint32_t size);
    /** The server would not resume: from the first byte again. */
    const char *(*restart)(void *ctx);
    /** `whole`: everything came, keep it if it is right; else drop it. */
    const char *(*finish)(void *ctx, bool whole);
} yk_install_sink_t;

#ifdef __cplusplus
}
#endif
