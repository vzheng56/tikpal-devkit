// The part of the store every firmware with yaykit_tf compiles (yk_tf_fat.h): the file format, reading and checking a
// stored file, the clean-up after a power cut, and the space a directory takes. Plain POSIX, host tested (test/run.sh).
// Writing, finding and the index are the system's, yk_tf_pkg.c (CONFIG_YK_TF_SYSTEM); how a package is stored on the
// card is told in sys_include/yk_tf_pkg.h.
#include "yk_tf_fat.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mbedtls/sha256.h"

#define PKG_FORMAT 1
#define USAGE_PATH 256  // the longest a path below an app's directory can be (YK_TF_REL_MAX)

// The header, little-endian.
enum {
    H_MAGIC = 0,
    H_FORMAT = 4,
    H_HEADER = 6,
    H_SIZE = 8,
    H_SEQ = 12,
    H_SLUG = 16,
    H_VERSION = 48,
    H_SHA = 80,
    H_SIGNATURE = 112,
    H_CRC = 508,
};

static const uint8_t PKG_MAGIC[4] = {'Y', 'K', 'T', 'F'};

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v), put16(p + 2, v >> 16); }
static uint32_t get16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | get16(p + 2) << 16; }

void yk_tf_pkg_header_encode(const yk_tf_pkg_meta_t *meta, uint8_t out[YK_TF_PKG_HEADER]) {
    memset(out, 0, YK_TF_PKG_HEADER);
    memcpy(out + H_MAGIC, PKG_MAGIC, sizeof(PKG_MAGIC));
    put16(out + H_FORMAT, PKG_FORMAT);
    put16(out + H_HEADER, YK_TF_PKG_HEADER);
    put32(out + H_SIZE, meta->size);
    put32(out + H_SEQ, meta->seq);
    memcpy(out + H_SLUG, meta->slug, sizeof(meta->slug));
    memcpy(out + H_VERSION, meta->version, sizeof(meta->version));
    memcpy(out + H_SHA, meta->sha256, sizeof(meta->sha256));
    memcpy(out + H_SIGNATURE, meta->signature, sizeof(meta->signature));
    put32(out + H_CRC, yk_tf_crc32(out, H_CRC));
}

bool yk_tf_pkg_header_decode(const uint8_t in[YK_TF_PKG_HEADER], yk_tf_pkg_meta_t *out) {
    if (memcmp(in + H_MAGIC, PKG_MAGIC, sizeof(PKG_MAGIC)) || get16(in + H_FORMAT) != PKG_FORMAT ||
        get16(in + H_HEADER) != YK_TF_PKG_HEADER || get32(in + H_CRC) != yk_tf_crc32(in, H_CRC))
        return false;
    memset(out, 0, sizeof(*out));
    out->size = get32(in + H_SIZE);
    out->seq = get32(in + H_SEQ);
    memcpy(out->slug, in + H_SLUG, sizeof(out->slug));
    memcpy(out->version, in + H_VERSION, sizeof(out->version));
    memcpy(out->sha256, in + H_SHA, sizeof(out->sha256));
    memcpy(out->signature, in + H_SIGNATURE, sizeof(out->signature));
    return yk_tf_fat_meta_ok(out);
}

// ---------------------------------------------------------------- files

/** What is left of a name when the power goes while it is being deleted and the entries span two sectors: the long
 *  part is gone, the short one (F7EFC6~1.PRT) is still there, with the chain. */
static bool is_orphan(const char *name) {
    const size_t n = strlen(name);
    return n > 4 && n <= 12 && strchr(name, '~') &&
           (!strcmp(name + n - 4, YK_TF_FAT_PART) || !strcmp(name + n - 4, YK_TF_FAT_DONE));
}

/** `size` bytes unless the file ends first (BAD) or the card fails (IO). */
static yk_tf_check_t read_all(int fd, uint8_t *buf, size_t size) {
    for (size_t got = 0; got < size;) {
        const ssize_t n = read(fd, buf + got, size - got);
        if (n < 0) return YK_TF_CHECK_IO;
        if (n == 0) return YK_TF_CHECK_BAD;
        got += (size_t)n;
    }
    return YK_TF_CHECK_OK;
}

yk_tf_check_t yk_tf_fat_read_meta(const char *path, const char *name, uint8_t *buf, yk_tf_pkg_meta_t *out) {
    const int fd = open(path, O_RDONLY);
    if (fd < 0) return errno == ENOENT ? YK_TF_CHECK_BAD : YK_TF_CHECK_IO;
    struct stat st;
    yk_tf_check_t c = fstat(fd, &st) ? YK_TF_CHECK_IO : read_all(fd, buf, YK_TF_PKG_HEADER);
    close(fd);
    if (c != YK_TF_CHECK_OK) return c;
    if (!memcmp(buf + H_MAGIC, PKG_MAGIC, sizeof(PKG_MAGIC)) && get16(buf + H_FORMAT) > PKG_FORMAT) return YK_TF_CHECK_LATER;
    if (!yk_tf_pkg_header_decode(buf, out) || (uint64_t)st.st_size != (uint64_t)out->size + YK_TF_PKG_HEADER)
        return YK_TF_CHECK_BAD;
    if (name) {
        char stem[YK_TF_FAT_STEM + 1];
        yk_tf_fat_stem_of(out->sha256, stem);
        if (strncmp(name, stem, YK_TF_FAT_STEM)) return YK_TF_CHECK_BAD;
    }
    return YK_TF_CHECK_OK;
}

yk_tf_check_t yk_tf_fat_hash_body(const char *path, const yk_tf_pkg_meta_t *meta, uint8_t *buf) {
    const int fd = open(path, O_RDONLY);
    if (fd < 0) return YK_TF_CHECK_IO;
    yk_tf_check_t c = lseek(fd, YK_TF_PKG_HEADER, SEEK_SET) == YK_TF_PKG_HEADER ? YK_TF_CHECK_OK : YK_TF_CHECK_IO;
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    for (uint32_t left = meta->size; c == YK_TF_CHECK_OK && left;) {
        const uint32_t n = left < YK_TF_FAT_CHUNK ? left : YK_TF_FAT_CHUNK;
        c = read_all(fd, buf, n);
        if (c == YK_TF_CHECK_OK) mbedtls_sha256_update(&sha, buf, n);
        left -= n;
    }
    close(fd);
    uint8_t sum[32];
    mbedtls_sha256_finish(&sha, sum);
    mbedtls_sha256_free(&sha);
    if (c == YK_TF_CHECK_OK && memcmp(sum, meta->sha256, sizeof(sum))) c = YK_TF_CHECK_BAD;
    return c;
}

int yk_tf_fat_list_dir(const char *dir, yk_tf_scratch_t *s) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    for (struct dirent *e; n < YK_TF_FAT_SCAN && (e = readdir(d));) {
        if (!yk_tf_fat_has_ext(e->d_name, YK_TF_FAT_PART) && !yk_tf_fat_has_ext(e->d_name, YK_TF_FAT_DONE) &&
            !is_orphan(e->d_name))
            continue;
        strcpy(s->name[n++], e->d_name);
    }
    closedir(d);
    return n;
}

static int name_index(const yk_tf_scratch_t *s, int n, const char *stem, const char *ext) {
    for (int i = 0; i < n; i++)
        if (!strncmp(s->name[i], stem, YK_TF_FAT_STEM) && !strcmp(s->name[i] + YK_TF_FAT_STEM, ext)) return i;
    return -1;
}

/** Deletes the name at i, if there is one, and forgets it. */
static void drop(const char *dir, yk_tf_scratch_t *s, int i) {
    if (i < 0 || !s->name[i][0]) return;
    if (yk_tf_fat_join3(s->other, dir, s->name[i], "")) unlink(s->other);
    s->name[i][0] = 0;
}

/** The short name left of a delete (is_orphan()). It may be all that remains of a package, or the old name of a rename
 *  the power cut short, on the same chain as the .PKG: its own header says which package, and every name of that
 *  package goes, in the order of settle_part(). One that cannot be read stays: deleting it might free someone's chain. */
static void settle_orphan(const char *dir, yk_tf_scratch_t *s, int n, int i) {
    if (!yk_tf_fat_join3(s->path, dir, s->name[i], "")) return;
    const yk_tf_check_t c = yk_tf_fat_read_meta(s->path, NULL, s->buf, &s->meta);
    if (c == YK_TF_CHECK_IO || c == YK_TF_CHECK_LATER) return;
    int done = -1, part = -1;
    if (c == YK_TF_CHECK_OK) {
        char stem[YK_TF_FAT_STEM + 1];
        yk_tf_fat_stem_of(s->meta.sha256, stem);
        done = name_index(s, n, stem, YK_TF_FAT_DONE);
        part = name_index(s, n, stem, YK_TF_FAT_PART);
    }
    drop(dir, s, done);
    drop(dir, s, i);
    drop(dir, s, part);
}

/** A .PRT: whole and right, the rename it did not get. Anything else goes, unreadable too: it is not worth keeping, and
 *  it is how the name left of a rename that stopped halfway goes (below). */
static void settle_part(const char *dir, yk_tf_scratch_t *s, int n, int i) {
    if (!yk_tf_fat_join3(s->path, dir, s->name[i], "")) return;
    const int done = name_index(s, n, s->name[i], YK_TF_FAT_DONE);
    if (done >= 0) {
        // One chain, two names (yk_tf_pkg.h). The .PKG first: should the power go between the two, what is left points
        // at free clusters, and a .PRT is never taken at its word. A .PKG would be, its header still reads.
        drop(dir, s, done);
        drop(dir, s, i);
        return;
    }
    yk_tf_check_t c = yk_tf_fat_read_meta(s->path, s->name[i], s->buf, &s->meta);
    if (c == YK_TF_CHECK_LATER) return;
    if (c == YK_TF_CHECK_OK) c = yk_tf_fat_hash_body(s->path, &s->meta, s->buf);
    if (c == YK_TF_CHECK_OK) {
        memcpy(s->other, s->path, sizeof(s->other));
        memcpy(s->other + strlen(s->other) - 4, YK_TF_FAT_DONE, 4);
        if (!rename(s->path, s->other)) memcpy(s->name[i] + YK_TF_FAT_STEM, YK_TF_FAT_DONE, 4);
        else s->name[i][0] = 0;  // not deleted: a rename that failed may have added the new name all the same
        return;
    }
    drop(dir, s, i);
}

void yk_tf_fat_clean_dir(const char *dir, yk_tf_scratch_t *s) {
    const int n = yk_tf_fat_list_dir(dir, s);
    for (int i = 0; i < n; i++)
        if (s->name[i][0] && is_orphan(s->name[i])) settle_orphan(dir, s, n, i);
    for (int i = 0; i < n; i++)
        if (s->name[i][0] && yk_tf_fat_has_ext(s->name[i], YK_TF_FAT_PART)) settle_part(dir, s, n, i);
    int kept = 0;
    s->next_seq = 0;
    for (int i = 0; i < n; i++) {
        if (!s->name[i][0] || !yk_tf_fat_has_ext(s->name[i], YK_TF_FAT_DONE) ||
            !yk_tf_fat_join3(s->path, dir, s->name[i], ""))
            continue;
        const yk_tf_check_t c = yk_tf_fat_read_meta(s->path, s->name[i], s->buf, &s->meta);
        if (c == YK_TF_CHECK_IO || c == YK_TF_CHECK_LATER) {
            s->name[i][0] = 0;  // not counted, not touched
        } else if (c == YK_TF_CHECK_BAD) {
            unlink(s->path);
            s->name[i][0] = 0;
            yk_tf_pkg_note_failure();
        } else {
            s->seq[i] = s->meta.seq;
            if (s->meta.seq >= s->next_seq) s->next_seq = s->meta.seq + 1;
            kept++;
        }
    }
    for (; kept > YK_TF_PKG_KEEP; kept--) {
        int oldest = -1;
        for (int i = 0; i < n; i++)
            if (s->name[i][0] && yk_tf_fat_has_ext(s->name[i], YK_TF_FAT_DONE) &&
                (oldest < 0 || s->seq[i] < s->seq[oldest]))
                oldest = i;
        if (yk_tf_fat_join3(s->path, dir, s->name[oldest], "")) unlink(s->path);
        s->name[oldest][0] = 0;
    }
}

// ---------------------------------------------------------------- cleaning up

yk_tf_pkg_err_t yk_tf_fat_cleanup(const char *root) {
    if (!root) return YK_TF_PKG_ARG;
    if (!yk_tf_pkg_writer_take()) return YK_TF_PKG_BUSY;
    DIR *d = opendir(root);
    yk_tf_pkg_err_t err = !d && errno != ENOENT ? YK_TF_PKG_IO : YK_TF_PKG_OK;
    yk_tf_scratch_t *s = d ? yk_tf_pkg_alloc(sizeof(*s)) : NULL;
    if (d && !s) err = YK_TF_PKG_NO_MEM;
    for (struct dirent *e; s && (e = readdir(d));)
        if (yk_tf_name_ok(e->d_name) && yk_tf_fat_join3(s->dir, root, e->d_name, "")) yk_tf_fat_clean_dir(s->dir, s);
    if (d) closedir(d);
    yk_tf_pkg_free(s);
    yk_tf_pkg_writer_give();
    return err;
}

// ---------------------------------------------------------------- how much

static bool usage_walk(char *path, int level, uint32_t cluster, uint64_t *used) {
    DIR *d = opendir(path);
    if (!d) return false;
    const size_t len = strlen(path);
    bool ok = true;
    for (struct dirent *e; ok && (e = readdir(d));) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        const int n = snprintf(path + len, USAGE_PATH - len, "/%s", e->d_name);
        if (n < 0 || (size_t)n >= USAGE_PATH - len) continue;  // not made through yk_tf_path()
        struct stat st;
        ok = !stat(path, &st);
        if (ok && S_ISDIR(st.st_mode)) ok = level >= YK_TF_REL_LEVELS || usage_walk(path, level + 1, cluster, used);
        else if (ok) *used = yk_tf_used_add(*used, (uint64_t)st.st_size, cluster);
        path[len] = 0;
    }
    closedir(d);
    return ok;
}

yk_tf_pkg_err_t yk_tf_fat_usage(const char *dir, uint32_t cluster, uint64_t *used) {
    if (!dir || !used || strlen(dir) >= USAGE_PATH) return YK_TF_PKG_ARG;
    *used = 0;
    struct stat st;
    if (stat(dir, &st)) return errno == ENOENT ? YK_TF_PKG_OK : YK_TF_PKG_IO;
    char *path = yk_tf_pkg_alloc(USAGE_PATH);
    if (!path) return YK_TF_PKG_NO_MEM;
    strcpy(path, dir);
    const bool ok = usage_walk(path, 1, cluster, used);
    yk_tf_pkg_free(path);
    return ok ? YK_TF_PKG_OK : YK_TF_PKG_IO;
}
