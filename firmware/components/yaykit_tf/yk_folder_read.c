#include "yk_folder_read.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "yk_folder_logic.h"

#define PATH_SIZE (8 + 16 + YK_FOLDER_PATH_BYTES + 1)  // "0:/Documents/" and the longest path the rule lets through

struct yk_folder_file {
    FIL fil;  // its 512-byte buffer with it, in PSRAM
};

static yk_folder_file_t *s_open[YK_FS_FILES];  // yk_folder.c calls one at a time (its lock)

/** drive + "/" + the folder + "/" + rel into a buffer of PATH_SIZE, which it allocates; NULL: no memory. */
static char *full_path(const char *drive, int kind, const char *rel) {
    char *path = yk_folder_fs_alloc(PATH_SIZE);
    if (!path) return NULL;
    const int n = snprintf(path, PATH_SIZE, "%s/%s%s%s", drive, yk_folder_dir_name(kind), rel[0] ? "/" : "", rel);
    if (n < 0 || n >= PATH_SIZE) {  // cannot happen for a path the rule passed: never a cut-off path
        yk_folder_fs_free(path);
        return NULL;
    }
    return path;
}

static yk_fs_err_t from_fresult(FRESULT r) {
    switch (r) {
    case FR_OK: return YK_FS_OK;
    case FR_NO_FILE:
    case FR_NO_PATH:
    case FR_INVALID_NAME: return YK_FS_NOT_FOUND;  // a name FatFs cannot hold cannot be there
    case FR_NOT_ENOUGH_CORE:
    case FR_TOO_MANY_OPEN_FILES: return YK_FS_NO_MEM;
    default: return YK_FS_IO;
    }
}

yk_fs_err_t yk_folder_fs_list(const char *drive, int kind, const char *sub, size_t from, yk_folder_entry_t *out,
                              size_t max, size_t *got) {
    if (got) *got = 0;
    if (!drive || !yk_folder_dir_name(kind) || !yk_folder_path_ok(sub, true) || (max && !out) || !got) return YK_FS_ARG;
    char *path = full_path(drive, kind, sub);
    FF_DIR *dir = yk_folder_fs_alloc(sizeof(*dir));
    FILINFO *info = yk_folder_fs_alloc(sizeof(*info));
    yk_fs_err_t err = path && dir && info ? YK_FS_OK : YK_FS_NO_MEM;
    if (err == YK_FS_OK) err = from_fresult(f_opendir(dir, path));
    if (err == YK_FS_OK) {
        size_t seen = 0;
        while (*got < max) {
            const FRESULT r = f_readdir(dir, info);
            if (r != FR_OK) {
                err = from_fresult(r);
                break;
            }
            if (!info->fname[0]) break;  // the end
            if (yk_folder_name_hidden(info->fname, info->fattrib & (AM_HID | AM_SYS))) continue;
            if (seen++ < from) continue;
            yk_folder_entry_t *e = &out[(*got)++];
            snprintf(e->name, sizeof(e->name), "%s", info->fname);
            e->dir = info->fattrib & AM_DIR;
            e->size = e->dir ? 0 : (uint64_t)info->fsize;
        }
        f_closedir(dir);
    }
    yk_folder_fs_free(info);
    yk_folder_fs_free(dir);
    yk_folder_fs_free(path);
    return err;
}

yk_fs_err_t yk_folder_fs_open(const char *drive, int kind, const char *path, yk_folder_file_t **out, uint64_t *size) {
    if (out) *out = NULL;
    if (!drive || !yk_folder_dir_name(kind) || !yk_folder_path_ok(path, false) || !out) return YK_FS_ARG;
    int slot = -1;
    for (int i = 0; i < YK_FS_FILES && slot < 0; i++)
        if (!s_open[i]) slot = i;
    if (slot < 0) return YK_FS_NO_MEM;
    char *full = full_path(drive, kind, path);
    yk_folder_file_t *f = yk_folder_fs_alloc(sizeof(*f));
    yk_fs_err_t err = full && f ? YK_FS_OK : YK_FS_NO_MEM;
    if (err == YK_FS_OK) {
        memset(f, 0, sizeof(*f));
        err = from_fresult(f_open(&f->fil, full, FA_READ | FA_OPEN_EXISTING));  // FA_READ only: never a write
    }
    yk_folder_fs_free(full);
    if (err != YK_FS_OK) {
        yk_folder_fs_free(f);
        return err;
    }
    s_open[slot] = f;
    if (size) *size = (uint64_t)f_size(&f->fil);
    *out = f;
    return YK_FS_OK;
}

static bool is_open(const yk_folder_file_t *f) {
    for (int i = 0; f && i < YK_FS_FILES; i++)
        if (s_open[i] == f) return true;
    return false;
}

yk_fs_err_t yk_folder_fs_read(yk_folder_file_t *f, void *buf, size_t size, size_t *got) {
    if (got) *got = 0;
    if (!is_open(f) || (size && !buf) || !got) return YK_FS_ARG;
    UINT n = 0;
    const FRESULT r = f_read(&f->fil, buf, (UINT)size, &n);
    *got = n;
    return from_fresult(r);
}

yk_fs_err_t yk_folder_fs_seek(yk_folder_file_t *f, uint64_t offset) {
    if (!is_open(f)) return YK_FS_ARG;
    const FSIZE_t end = f_size(&f->fil);
    return from_fresult(f_lseek(&f->fil, offset > end ? end : (FSIZE_t)offset));  // past the end: the end, not growing
}

void yk_folder_fs_close(yk_folder_file_t *f) {
    for (int i = 0; f && i < YK_FS_FILES; i++) {
        if (s_open[i] != f) continue;
        f_close(&f->fil);
        s_open[i] = NULL;
        yk_folder_fs_free(f);
        return;
    }
}

int yk_folder_fs_open_count(void) {
    int n = 0;
    for (int i = 0; i < YK_FS_FILES; i++) n += s_open[i] != NULL;
    return n;
}
