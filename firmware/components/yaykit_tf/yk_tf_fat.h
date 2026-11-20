// What every firmware with yaykit_tf compiles and the system's half shares with it (Yay 0.1.19 开发文档 6.1 step 3). Not in
// include/: no app gets it, and the review does not let an app call what it declares.
//
//  - The store's file format and how a file of it is read and checked (yk_tf_pkg.h has the rest of the store, for the
//    system only): the clean-up after a power cut needs them in every firmware. Whoever writes the card after a boot,
//    an app too, must first have it run: a short name a power cut left may share a chain of clusters with a .PKG, and
//    cleared after someone else has taken those clusters, it would free them under that file.
//  - What the store needs from below (yaykit_tf.c, or a test's stand-ins).
// Plain C, no ESP-IDF: the host tests build it (test/run.sh). The card's own half of yk_tf_sys.h is yk_tf_card.h.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "yk_tf_logic.h"

#define YK_TF_PKG_HEADER 512             // the body starts here
#define YK_TF_PKG_KEEP 2
#define YK_TF_PKG_MAX_SIZE (16u << 20)
#define YK_TF_PKG_PATH 128
// A write needs room for the file, and for the directory entries and a cluster's rounding beside it.
#define YK_TF_PKG_SLACK (64u * 1024)

typedef enum {
    YK_TF_PKG_OK,
    YK_TF_PKG_HAVE,     // begin: this very package is stored already, nothing to write
    YK_TF_PKG_MISS,     // find: not there
    YK_TF_PKG_BAD,      // the bytes are not what they should be; a stored one is gone now
    YK_TF_PKG_FULL,     // the card has no room: nothing was kept of it
    YK_TF_PKG_IO,       // the card did not do it: a miss, the card's failures are counted below this layer
    YK_TF_PKG_ARG,
    YK_TF_PKG_STOPPED,  // the system wants the card (Bluetooth, a restart): abort and release
    YK_TF_PKG_NO_MEM,
    YK_TF_PKG_BUSY,     // another writer, or a clean-up: nothing was done, try later
} yk_tf_pkg_err_t;

typedef struct {
    char slug[32];
    char version[32];
    uint32_t size;        // of the body
    uint8_t sha256[32];   // of the body
    char signature[112];  // "yks1.<key id>.<r‖s>" (firmware/signing/README.md), or empty: yk_tf_pkg.h does not judge it
    uint32_t seq;         // the order they were stored in: set here, not by the caller
} yk_tf_pkg_meta_t;

/** The fixed layout of the first 512 bytes, little-endian; the last 4 are a CRC-32 of the rest. */
void yk_tf_pkg_header_encode(const yk_tf_pkg_meta_t *meta, uint8_t out[YK_TF_PKG_HEADER]);
bool yk_tf_pkg_header_decode(const uint8_t in[YK_TF_PKG_HEADER], yk_tf_pkg_meta_t *out);

// ---------------------------------------------------------------- what every firmware runs

/** What a power cut left in `root`, any time but beside a writer (BUSY): half-written files, renames that stopped
 *  halfway, damaged headers, more than a slug's two. Reads every header. yk_tf_acquire() does it once a boot, for the
 *  system's own directories, before anyone can write; the store's `begin` does it for its own slug. */
yk_tf_pkg_err_t yk_tf_fat_cleanup(const char *root);

/** The space a directory's files take, all the way down, in whole clusters of `cluster` bytes. */
yk_tf_pkg_err_t yk_tf_fat_usage(const char *dir, uint32_t cluster, uint64_t *used);

// ---------------------------------------------------------------- what the store needs from below (yaykit_tf.c)

void *yk_tf_pkg_alloc(size_t size);  // PSRAM: malloc() of 4 KB and under is internal RAM
void yk_tf_pkg_free(void *p);
uint64_t yk_tf_pkg_card_free(void);
bool yk_tf_pkg_stop_asked(void);
void yk_tf_pkg_note_failure(void);   // the card holds something other than what was written
bool yk_tf_pkg_writer_take(void);    // true: nobody else writes or cleans up until yk_tf_pkg_writer_give()
void yk_tf_pkg_writer_give(void);

// ---------------------------------------------------------------- the store's own pieces, for yk_tf_pkg.c

#define YK_TF_FAT_PART ".PRT"
#define YK_TF_FAT_DONE ".PKG"
#define YK_TF_FAT_STEM 16                       // hex digits of the sha256 in a file's name
#define YK_TF_FAT_NAME (YK_TF_FAT_STEM + 4)
#define YK_TF_FAT_CHUNK 4096
#define YK_TF_FAT_SCAN 16  // files looked at in a slug's directory: two and what a power cut can leave beside them

/** What one operation works in, in PSRAM: a task's stack has no room for it. */
typedef struct {
    uint8_t buf[YK_TF_FAT_CHUNK];
    yk_tf_pkg_meta_t meta;
    char name[YK_TF_FAT_SCAN][YK_TF_FAT_NAME + 1];
    uint32_t seq[YK_TF_FAT_SCAN];
    char dir[YK_TF_PKG_PATH], path[YK_TF_PKG_PATH], other[YK_TF_PKG_PATH];
    uint32_t next_seq;
} yk_tf_scratch_t;

typedef enum {
    YK_TF_CHECK_OK,
    YK_TF_CHECK_BAD,    // not what it should be
    YK_TF_CHECK_IO,     // the card did not say
    YK_TF_CHECK_LATER,  // a format newer than this firmware's, after a roll-back: not ours to judge
} yk_tf_check_t;

static inline bool yk_tf_fat_terminated(const char *s, size_t size) { return memchr(s, 0, size) != NULL; }

static inline bool yk_tf_fat_version_ok(const char *v) {
    size_t n = 0;
    for (; v[n]; n++) {
        const char c = v[n];
        const bool plain = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!plain && c != '.' && c != '-' && c != '+') return false;
    }
    return n >= 1 && n < sizeof(((yk_tf_pkg_meta_t *)0)->version);
}

static inline bool yk_tf_fat_meta_ok(const yk_tf_pkg_meta_t *m) {
    return yk_tf_fat_terminated(m->slug, sizeof(m->slug)) && yk_tf_name_ok(m->slug) &&
           yk_tf_fat_terminated(m->version, sizeof(m->version)) && yk_tf_fat_version_ok(m->version) && m->size >= 1 &&
           m->size <= YK_TF_PKG_MAX_SIZE && yk_tf_fat_terminated(m->signature, sizeof(m->signature));
}

static inline void yk_tf_fat_stem_of(const uint8_t sha256[32], char out[YK_TF_FAT_STEM + 1]) {
    static const char HEX[] = "0123456789abcdef";
    for (int i = 0; i < YK_TF_FAT_STEM / 2; i++) {
        out[i * 2] = HEX[sha256[i] >> 4];
        out[i * 2 + 1] = HEX[sha256[i] & 15];
    }
    out[YK_TF_FAT_STEM] = 0;
}

static inline bool yk_tf_fat_join3(char *out, const char *a, const char *b, const char *c) {
    const int n = snprintf(out, YK_TF_PKG_PATH, "%s/%s%s", a, b, c);
    return n > 0 && n < YK_TF_PKG_PATH;
}

static inline bool yk_tf_fat_has_ext(const char *name, const char *ext) {
    return strlen(name) == YK_TF_FAT_NAME && !strcmp(name + YK_TF_FAT_STEM, ext);
}

/** The header of the file at `path`, and whether the file is as long as it says and, given a name, called what it
 *  should be. */
yk_tf_check_t yk_tf_fat_read_meta(const char *path, const char *name, uint8_t *buf, yk_tf_pkg_meta_t *out);
/** The body of the file at `path` against `meta`'s sha256. */
yk_tf_check_t yk_tf_fat_hash_body(const char *path, const yk_tf_pkg_meta_t *meta, uint8_t *buf);
/** The names in `dir` the store knows, into s->name. Anything else is left alone: a later format's, perhaps. */
int yk_tf_fat_list_dir(const char *dir, yk_tf_scratch_t *s);
/** Brings a slug's directory to what it should be: no .PRT, every .PKG with a good header, the last two. Leaves in
 *  s->next_seq the number the next package gets. */
void yk_tf_fat_clean_dir(const char *dir, yk_tf_scratch_t *s);
