// yk_folder_read.c on ESP-IDF's own FatFs over a RAM disk: the person's files as a computer leaves them (Chinese,
// Japanese and Spanish names, a name over 255 bytes, eight levels of 255-character folders, a Mac's ._ files, a hidden
// and a system one), listed, opened, read, sought, closed. And only read: the disk is byte for byte what it was before.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diskio.h"
#include "ff.h"
#include "yk_folder_logic.h"
#include "yk_folder_read.h"

static int checks, failures;
#define CHECK(c)                                                         \
    do {                                                                 \
        checks++;                                                        \
        if (!(c)) {                                                      \
            failures++;                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
        }                                                                \
    } while (0)

// ---------------------------------------------------------------- the disk

#define SS 512
#define SECTORS (128u * 1024)  // 64 MB: FAT32 with 512-byte clusters (it needs 65,526 of them), as a card is FAT32
static uint8_t *disk;
static long writes;

DSTATUS ff_disk_initialize(BYTE p) { return (void)p, 0; }
DSTATUS ff_disk_status(BYTE p) { return (void)p, 0; }
DRESULT ff_disk_read(BYTE p, BYTE *b, LBA_t s, UINT n) {
    (void)p;
    memcpy(b, disk + (size_t)s * SS, (size_t)n * SS);
    return RES_OK;
}
DRESULT ff_disk_write(BYTE p, const BYTE *b, LBA_t s, UINT n) {
    (void)p;
    writes += n;
    memcpy(disk + (size_t)s * SS, b, (size_t)n * SS);
    return RES_OK;
}
DRESULT ff_disk_ioctl(BYTE p, BYTE cmd, void *buf) {
    (void)p;
    switch (cmd) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_COUNT: *(LBA_t *)buf = SECTORS; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buf = SS; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buf = 1; return RES_OK;
    default: return RES_ERROR;
    }
}
PARTITION VolToPart[FF_VOLUMES] = {{0, 0}, {1, 0}};
void *ff_memalloc(unsigned n) { return malloc(n); }
void ff_memfree(void *p) { free(p); }
int ff_mutex_create(int v) { return v >= 0; }
void ff_mutex_delete(int v) { (void)v; }
int ff_mutex_take(int v) { return v >= 0; }
void ff_mutex_give(int v) { (void)v; }
DWORD get_fattime(void) { return 0x5A210000; }

static int live;
void *yk_folder_fs_alloc(size_t size) {
    live++;
    return malloc(size);
}
void yk_folder_fs_free(void *p) {
    if (p) live--;
    free(p);
}

// ---------------------------------------------------------------- the person's files, as a computer writes them

static char *repeat(const char *unit, int n) {
    const size_t u = strlen(unit);
    char *s = malloc(u * (size_t)n + 1);
    for (int i = 0; i < n; i++) memcpy(s + u * (size_t)i, unit, u);
    s[u * (size_t)n] = 0;
    return s;
}

static void put(const char *path, const char *text) {
    FIL f;
    UINT n;
    if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) || f_write(&f, text, (UINT)strlen(text), &n) || f_close(&f)) {
        printf("setup: could not write %s\n", path);
        exit(2);
    }
}

static char *s_deep_dir, *s_long_name;

static void make_card(void) {
    static FATFS fs;
    static uint8_t work[4096];
    const MKFS_PARM opt = {FM_FAT32 | FM_SFD, 0, 0, 0, SS};
    if (f_mkfs("0:", &opt, work, sizeof(work)) || f_mount(&fs, "0:", 1)) exit(2);
    const char *dirs[] = {"0:/Music", "0:/Stories", "0:/Pictures", "0:/Documents", "0:/.tikpal", "0:/.tikpal/apps",
                          "0:/apps", "0:/apps/recording-card", "0:/Stories/睡前", "0:/Music/Música"};
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) f_mkdir(dirs[i]);
    put("0:/.tikpal/apps/secret.PKG", "system");
    put("0:/apps/recording-card/rec.OGG", "someone's recording");
    put("0:/Stories/三只小猪.ogg", "从前有三只小猪。");
    put("0:/Stories/睡前/小红帽.ogg", "red riding hood");
    put("0:/Stories/ドラえもん.ogg", "doraemon");
    put("0:/Stories/._三只小猪.ogg", "a Mac's resource fork");
    put("0:/Stories/.DS_Store", "a Mac's own");
    put("0:/Stories/藏起来的.ogg", "hidden");
    f_chmod("0:/Stories/藏起来的.ogg", AM_HID, AM_HID);
    put("0:/Stories/系统的.ogg", "system");
    f_chmod("0:/Stories/系统的.ogg", AM_SYS, AM_SYS);
    put("0:/Music/Música/canción ñ.opus", "una canción");
    put("0:/Music/雨声 😀.ogg", "rain");
    // A name of 100 Han characters: 300 bytes of UTF-8, more than FatFs lists (255): it comes as its short name.
    s_long_name = repeat("长", 100);
    char path[1024];
    snprintf(path, sizeof(path), "0:/Documents/%s.txt", s_long_name);
    put(path, "a long name");
    // Eight levels of 255 Han characters: 6,127 bytes of path, far past ESP-IDF's VFS (255).
    char *part = repeat("深", 255);
    char *deep = calloc(1, YK_FOLDER_PATH_BYTES + 32);
    strcpy(deep, "0:/Pictures");
    for (int i = 0; i < 7; i++) {
        strcat(strcat(deep, "/"), part);
        if (f_mkdir(deep)) exit(3);
    }
    s_deep_dir = strdup(deep + strlen("0:/Pictures/"));
    strcat(strcat(deep, "/"), part);  // the eighth part: a file of 255 characters
    put(deep, "deep down");
    free(part);
    free(deep);
}

// ---------------------------------------------------------------- the tests

static int find(const yk_folder_entry_t *e, size_t n, const char *name) {
    for (size_t i = 0; i < n; i++)
        if (!strcmp(e[i].name, name)) return (int)i;
    return -1;
}

static void test_list(void) {
    yk_folder_entry_t *e = calloc(16, sizeof(*e));
    size_t n = 0;
    CHECK(yk_folder_fs_list("0:", 1, "", 0, e, 16, &n) == YK_FS_OK);
    // The Stories folder: two stories and a folder; a Mac's files, the hidden and the system one left out.
    CHECK(n == 3 && find(e, n, "三只小猪.ogg") >= 0 && find(e, n, "ドラえもん.ogg") >= 0 && find(e, n, "睡前") >= 0);
    const int pigs = find(e, n, "三只小猪.ogg"), night = find(e, n, "睡前");
    CHECK(pigs >= 0 && e[pigs].size == strlen("从前有三只小猪。") && !e[pigs].dir);
    CHECK(night >= 0 && e[night].dir && e[night].size == 0);
    CHECK(find(e, n, "._三只小猪.ogg") < 0 && find(e, n, ".DS_Store") < 0 && find(e, n, "藏起来的.ogg") < 0 &&
          find(e, n, "系统的.ogg") < 0);
    // Page by page, the hidden ones not counted.
    size_t a = 0, b = 0;
    yk_folder_entry_t one[2];
    CHECK(yk_folder_fs_list("0:", 1, "", 0, one, 2, &a) == YK_FS_OK && a == 2);
    CHECK(yk_folder_fs_list("0:", 1, "", 2, one, 2, &b) == YK_FS_OK && b == 1);
    CHECK(yk_folder_fs_list("0:", 1, "", 3, one, 2, &b) == YK_FS_OK && b == 0);
    // A folder inside, in Spanish.
    CHECK(yk_folder_fs_list("0:", 0, "Música", 0, e, 16, &n) == YK_FS_OK && n == 1 && !strcmp(e[0].name, "canción ñ.opus"));
    CHECK(yk_folder_fs_list("0:", 0, "", 0, e, 16, &n) == YK_FS_OK && find(e, n, "雨声 😀.ogg") >= 0);
    // A name over 255 bytes: listed by its short name, which opens it.
    CHECK(yk_folder_fs_list("0:", 3, "", 0, e, 16, &n) == YK_FS_OK && n == 1);
    CHECK(n == 1 && strlen(e[0].name) <= 12 && strchr(e[0].name, '~'));
    yk_folder_file_t *f = NULL;
    char buf[64] = "";
    size_t got = 0;
    CHECK(n == 1 && yk_folder_fs_open("0:", 3, e[0].name, &f, NULL) == YK_FS_OK);
    CHECK(f && yk_folder_fs_read(f, buf, sizeof(buf), &got) == YK_FS_OK && got == 11 && !memcmp(buf, "a long name", 11));
    yk_folder_fs_close(f);
    // ... and by its long name too.
    const size_t named_size = strlen(s_long_name) + 8;
    char *named = malloc(named_size);
    snprintf(named, named_size, "%s.txt", s_long_name);
    CHECK(yk_folder_fs_open("0:", 3, named, &f, NULL) == YK_FS_OK);
    yk_folder_fs_close(f);
    free(named);
    // Eight levels deep, each part 255 characters: listed and read, the path never cut short.
    CHECK(yk_folder_fs_list("0:", 2, s_deep_dir, 0, e, 16, &n) == YK_FS_OK && n == 1 && !e[0].dir);
    // Wrong ways in: none reach the system's area or another app's.
    CHECK(yk_folder_fs_list("0:", 1, "..", 0, e, 16, &n) == YK_FS_ARG && n == 0);
    CHECK(yk_folder_fs_list("0:", 1, "../.tikpal/apps", 0, e, 16, &n) == YK_FS_ARG);
    CHECK(yk_folder_fs_list("0:", 1, "/apps", 0, e, 16, &n) == YK_FS_ARG);
    CHECK(yk_folder_fs_list("0:", 1, "没有这个", 0, e, 16, &n) == YK_FS_NOT_FOUND);
    CHECK(yk_folder_fs_list("0:", 4, "", 0, e, 16, &n) == YK_FS_ARG);
    CHECK(yk_folder_fs_list("0:", 1, "", 0, NULL, 0, &n) == YK_FS_OK && n == 0);  // nothing asked for
    free(e);
}

static void test_read(void) {
    yk_folder_file_t *f = NULL;
    uint64_t size = 0;
    char buf[64];
    size_t got = 0;
    CHECK(yk_folder_fs_open("0:", 1, "三只小猪.ogg", &f, &size) == YK_FS_OK && size == strlen("从前有三只小猪。"));
    CHECK(yk_folder_fs_read(f, buf, 6, &got) == YK_FS_OK && got == 6 && !memcmp(buf, "从前", 6));
    CHECK(yk_folder_fs_seek(f, 9) == YK_FS_OK && yk_folder_fs_read(f, buf, sizeof(buf), &got) == YK_FS_OK &&
          got == size - 9 && !memcmp(buf, "三只小猪。", got));
    CHECK(yk_folder_fs_read(f, buf, sizeof(buf), &got) == YK_FS_OK && got == 0);  // the end
    CHECK(yk_folder_fs_seek(f, 1u << 30) == YK_FS_OK && yk_folder_fs_read(f, buf, 4, &got) == YK_FS_OK && got == 0);
    CHECK(yk_folder_fs_seek(f, 0) == YK_FS_OK && yk_folder_fs_read(f, buf, 3, &got) == YK_FS_OK && got == 3);
    yk_folder_fs_close(f);
    CHECK(yk_folder_fs_read(f, buf, 3, &got) == YK_FS_ARG);  // closed: not a file any more
    yk_folder_fs_close(f);                                    // twice: nothing
    // Inside a folder, in Japanese; deep down.
    CHECK(yk_folder_fs_open("0:", 1, "睡前/小红帽.ogg", &f, &size) == YK_FS_OK && size == 15);
    yk_folder_fs_close(f);
    char *deep = malloc(YK_FOLDER_PATH_BYTES + 1), *part = repeat("深", 255);
    snprintf(deep, YK_FOLDER_PATH_BYTES + 1, "%s/%s", s_deep_dir, part);
    CHECK(strlen(deep) == YK_FOLDER_PATH_BYTES);
    CHECK(yk_folder_fs_open("0:", 2, deep, &f, &size) == YK_FS_OK && size == 9);
    CHECK(yk_folder_fs_read(f, buf, sizeof(buf), &got) == YK_FS_OK && got == 9 && !memcmp(buf, "deep down", 9));
    yk_folder_fs_close(f);
    free(deep), free(part);
    // Not a file, not there, not allowed.
    CHECK(yk_folder_fs_open("0:", 1, "睡前", &f, NULL) == YK_FS_NOT_FOUND && !f);
    CHECK(yk_folder_fs_open("0:", 1, "没有.ogg", &f, NULL) == YK_FS_NOT_FOUND);
    CHECK(yk_folder_fs_open("0:", 1, "._三只小猪.ogg", &f, NULL) == YK_FS_ARG);
    CHECK(yk_folder_fs_open("0:", 1, "../.tikpal/apps/secret.PKG", &f, NULL) == YK_FS_ARG);
    CHECK(yk_folder_fs_open("0:", 1, "../apps/recording-card/rec.OGG", &f, NULL) == YK_FS_ARG);
    CHECK(yk_folder_fs_open("0:", 1, "", &f, NULL) == YK_FS_ARG);
    // Four open at a time.
    yk_folder_file_t *open[5] = {0};
    for (int i = 0; i < 4; i++) CHECK(yk_folder_fs_open("0:", 1, "三只小猪.ogg", &open[i], NULL) == YK_FS_OK);
    CHECK(yk_folder_fs_open_count() == 4 && yk_folder_fs_open("0:", 1, "ドラえもん.ogg", &open[4], NULL) == YK_FS_NO_MEM);
    for (int i = 0; i < 4; i++) yk_folder_fs_close(open[i]);
    CHECK(yk_folder_fs_open_count() == 0);
}

int main(void) {
    disk = calloc(SECTORS, SS);
    make_card();
    uint8_t *before = malloc((size_t)SECTORS * SS);
    memcpy(before, disk, (size_t)SECTORS * SS);
    writes = 0;
    test_list();
    test_read();
    // Only read: not one sector written, and the disk as it was.
    CHECK(writes == 0 && !memcmp(before, disk, (size_t)SECTORS * SS));
    CHECK(live == 0);  // every buffer given back
    f_mount(NULL, "0:", 0);
    free(before), free(disk), free(s_deep_dir), free(s_long_name);
    printf("yk_folder on FatFs host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
