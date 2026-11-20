// The shared folders read through FatFs itself (f_opendir, f_open...), not ESP-IDF's VFS: the VFS copies a whole path
// into 255 bytes and cuts a longer one short without a word (vfs_fat.c, FILENAME_MAX), and the person's Chinese names
// reach that quickly (three bytes a character). FatFs takes a path of any length, a part of up to 255 UTF-16 units.
// What a name in a listing can be is still FatFs's: a long name over 255 bytes of UTF-8 comes as its short name
// (FILINFO.fname, FF_LFN_BUF), which opens the same file. Only reading: nothing here writes, makes, renames or deletes.
// Plain FatFs, host tested on ESP-IDF's FatFs over a RAM disk (test/test_folder_fat.c). The card must be held
// (yk_tf_acquire) around every call; `drive` is the mounted volume's, "0:".
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "yk_folder.h"

typedef enum {
    YK_FS_OK = 0,
    YK_FS_ARG,        // not a path the rule lets through (yk_folder_path_ok), or a bad argument
    YK_FS_NOT_FOUND,  // no such file or folder (a folder where a file was asked for, too)
    YK_FS_NO_MEM,     // no PSRAM, or YK_FS_FILES open already
    YK_FS_IO,         // the card did not answer
} yk_fs_err_t;

#define YK_FS_FILES 4  // open at a time

/** `sub` in the folder `kind` (yk_folder_list()). */
yk_fs_err_t yk_folder_fs_list(const char *drive, int kind, const char *sub, size_t from, yk_folder_entry_t *out,
                              size_t max, size_t *got);
yk_fs_err_t yk_folder_fs_open(const char *drive, int kind, const char *path, yk_folder_file_t **out, uint64_t *size);
yk_fs_err_t yk_folder_fs_read(yk_folder_file_t *f, void *buf, size_t size, size_t *got);
yk_fs_err_t yk_folder_fs_seek(yk_folder_file_t *f, uint64_t offset);
void yk_folder_fs_close(yk_folder_file_t *f);
/** Files open now. */
int yk_folder_fs_open_count(void);

/** PSRAM (yk_folder.c), or the host's malloc (the test). */
void *yk_folder_fs_alloc(size_t size);
void yk_folder_fs_free(void *p);
