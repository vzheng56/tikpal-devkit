// The card itself: everything that needs ESP-IDF. The decisions are in yk_tf_logic.c, the store in yk_tf_fat.c (every
// firmware) and yk_tf_pkg.c (the system's, CONFIG_YK_TF_SYSTEM), all host tested. The system's names for what is done
// here are yk_tf_sys.c's (yk_tf_card.h). How the card is opened, the timeout and the restart were worked out on the device with `st tf`
// (shared-test/main/tf_look.c).
#include "yaykit_tf.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "diskio.h"
#include "diskio_impl.h"
#include "driver/sdmmc_host.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/sdmmc_ll.h"
#include "sdkconfig.h"
#include "sdmmc_cmd.h"
#include "soc/sdmmc_struct.h"
#include "yk_tf_card.h"
#include "yk_tf_fat.h"
#include "yk_tf_logic.h"

#if !CONFIG_FATFS_ALLOC_PREFER_EXTRAM || !CONFIG_FATFS_LFN_HEAP
#error "yaykit_tf: set CONFIG_FATFS_LFN_HEAP and CONFIG_FATFS_ALLOC_PREFER_EXTRAM, FatFs's buffers belong in PSRAM"
#endif

#define TF_PIN_CMD 1
#define TF_PIN_CLK 2
#define TF_PIN_D0 3
#define TF_MAX_FILES 4
#define TF_SYS_DIRS 4
#define TF_SHUTDOWN_WAIT_MS 100
#define TF_POLL_MS 10

// Linked by the firmware that has Bluetooth; one without it cannot have it on.
bool yk_link_ble_active(void) __attribute__((weak));

/** In PSRAM, made at the first use. */
typedef struct {
    SemaphoreHandle_t lock;  // recursive: mounting reads the card
    sdmmc_card_t card;
    FATFS *fs;
    BYTE pdrv;
    bool host_on;
    bool shutdown_hooked;
    bool cleaned;  // what a power cut left is gone, this boot
    bool writer;   // yk_tf_pkg_writer_take()
    bool lent;     // yk_tf_card_lend(): the computer has it
    bool layout_due;  // the system's check runs at the next mount (yk_tf_card_layout)
    uint32_t total_kb, free_kb;  // the card when it was last measured; words, so that yk_tf_card_known() reads them as they are
    char app_dir[YK_TF_DIR_SIZE];
    char sys_dir[TF_SYS_DIRS][YK_TF_DIR_SIZE];
} tf_t;

static tf_t *s;
static uint32_t s_returns;  // yk_tf_card_returns(): a word, read in any task
// Internal RAM, not in tf_t: yk_tf_core_pause() is an atomic, and the chip's atomics do not work on PSRAM.
static yk_tf_core_t s_core;

static uint64_t now_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }
static size_t internal_free(void) { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }
static void lock(void) { xSemaphoreTakeRecursive(s->lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGiveRecursive(s->lock); }

/** Each time someone measures the card anyway: nothing goes to the card for a report. */
static void note_space(uint64_t total, uint64_t free_bytes) {
    s->total_kb = (uint32_t)(total / 1024);
    s->free_kb = (uint32_t)(free_bytes / 1024);
}

static bool ready(void) {
    if (__atomic_load_n(&s, __ATOMIC_ACQUIRE)) return true;
    tf_t *t = heap_caps_calloc(1, sizeof(*t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!t) return false;
    t->lock = xSemaphoreCreateRecursiveMutex();
    if (!t->lock) {
        free(t);
        return false;
    }
    t->pdrv = FF_DRV_NOT_USED;
    t->layout_due = true;
    char slug[YK_TF_NAME_MAX + 1];
    if (yk_tf_slug_from_project(esp_app_get_description()->project_name, slug, sizeof(slug)))
        snprintf(t->app_dir, sizeof(t->app_dir), YK_TF_APPS_ROOT "/%s", slug);
    tf_t *none = NULL;
    if (!__atomic_compare_exchange_n(&s, &none, t, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        vSemaphoreDelete(t->lock);
        free(t);
    }
    return true;
}

// ---------------------------------------------------------------- the card

/** The card ready to use, 1 bit at 20 MHz, with the wide data timeout: ESP-IDF sets its 100 ms on every clock change,
 *  so this goes after sdmmc_card_init(), each time. */
static esp_err_t card_open(void) {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    host.unaligned_multi_block_rw_max_chunk_size = YK_TF_BOUNCE_SECTOR;  // until the transfer that decides (xfer_run)
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = TF_PIN_CLK;
    slot.cmd = TF_PIN_CMD;
    slot.d0 = TF_PIN_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;  // CMD has an external pull-up, D0 may not (old Tikpal, storage.c)
    memset(&s->card, 0, sizeof(s->card));
    esp_err_t err = sdmmc_host_init();
    if (err == ESP_OK) err = sdmmc_host_init_slot(host.slot, &slot);
    if (err == ESP_OK) err = sdmmc_card_init(&host, &s->card);
    if (err != ESP_OK) {
        sdmmc_host_deinit();
        return err;
    }
    s->host_on = true;
    sdmmc_ll_set_data_timeout(&SDMMC, yk_tf_timeout_cycles((uint32_t)s->card.real_freq_khz));
    return ESP_OK;
}

static void card_close(void) {
    if (s->host_on) sdmmc_host_deinit();
    s->host_on = false;
}

/** After a timeout the card sits in its data state and nothing but starting it again gets it out (R1 state 5, 11-06). */
static int card_restart(void *ctx) {
    (void)ctx;
    card_close();
    const esp_err_t err = card_open();
    printf("YK_TF restart err=%s\n", esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
}

typedef struct {
    void *buf;
    uint32_t sector;
    unsigned count;
    bool write;
} xfer_t;

static int xfer_run(void *ctx) {
    const xfer_t *x = ctx;
    // Read on every transfer by ESP-IDF, so it can follow internal RAM (3.8); a restart clears it. Without
    // SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF: that buffer is not freed at unmount, 512 bytes lost each time.
    s->card.host.unaligned_multi_block_rw_max_chunk_size = (size_t)yk_tf_bounce_sectors(internal_free());
    esp_err_t err;
#if CONFIG_YK_TF_READ_ONLY
    err = sdmmc_read_sectors(&s->card, x->buf, x->sector, x->count);
#else
    err = x->write ? sdmmc_write_sectors(&s->card, x->buf, x->sector, x->count)
                   : sdmmc_read_sectors(&s->card, x->buf, x->sector, x->count);
#endif
    return err == ESP_OK ? 0 : 1;
}

/** Everything FatFs asks of the card, one at a time: FatFs's own lock for a volume, and this one against a release. */
static DRESULT disk_transfer(void *buf, uint32_t sector, unsigned count, bool write) {
    xfer_t x = {.buf = buf, .sector = sector, .count = count, .write = write};
    const yk_tf_io_t io = {.transfer = xfer_run, .restart = card_restart, .ctx = &x, .now_ms = now_ms()};
    lock();
    const int r = yk_tf_core_io(&s_core, &io);
    unlock();
    return r ? RES_ERROR : RES_OK;
}

// ---------------------------------------------------------------- the disk under FatFs

static DSTATUS tf_disk_status(BYTE pdrv) {
    (void)pdrv;
#if CONFIG_YK_TF_READ_ONLY
    return STA_PROTECT;
#else
    return 0;
#endif
}

static DRESULT tf_disk_read(BYTE pdrv, BYTE *buff, uint32_t sector, unsigned count) {
    (void)pdrv;
    return disk_transfer(buff, sector, count, false);
}

static DRESULT tf_disk_write(BYTE pdrv, const BYTE *buff, uint32_t sector, unsigned count) {
    (void)pdrv;
#if CONFIG_YK_TF_READ_ONLY
    (void)buff, (void)sector, (void)count;
    return RES_WRPRT;
#else
    return disk_transfer((void *)buff, sector, count, true);
#endif
}

static DRESULT tf_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    (void)pdrv;
    switch (cmd) {
    case CTRL_SYNC: return RES_OK;  // sdmmc_write_sectors() waits for the card
    case GET_SECTOR_COUNT: *(DWORD *)buff = (DWORD)s->card.csd.capacity; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buff = (WORD)s->card.csd.sector_size; return RES_OK;
    default: return RES_ERROR;  // no trim
    }
}

static const ff_diskio_impl_t DISK = {
    .init = tf_disk_status, .status = tf_disk_status, .read = tf_disk_read, .write = tf_disk_write,
    .ioctl = tf_disk_ioctl,
};

static void drive_name(char out[3]) {
    out[0] = (char)('0' + s->pdrv);
    out[1] = ':';
    out[2] = 0;
}

static void unmount_now(void) {
    char drv[3];
    drive_name(drv);
    f_mount(NULL, drv, 0);
    ff_diskio_unregister(s->pdrv);
    esp_vfs_fat_unregister_path(YK_TF_BASE);
    s->pdrv = FF_DRV_NOT_USED;
    s->fs = NULL;
    card_close();
}

/** The card, its volume at /sdcard. Never formats: what is not FAT stays as it is, and is refused. */
static esp_err_t mount_now(void) {
    esp_err_t err = card_open();
    if (err != ESP_OK) return err;
    err = ff_diskio_get_drive(&s->pdrv);
    if (err != ESP_OK) {
        s->pdrv = FF_DRV_NOT_USED;
        card_close();
        return err;
    }
    ff_diskio_register(s->pdrv, &DISK);
    char drv[3];
    drive_name(drv);
    const esp_vfs_fat_conf_t conf = {.base_path = YK_TF_BASE, .fat_drive = drv, .max_files = TF_MAX_FILES};
    err = esp_vfs_fat_register_cfg(&conf, &s->fs);
    if (err != ESP_OK) {
        ff_diskio_unregister(s->pdrv);
        s->pdrv = FF_DRV_NOT_USED;
        card_close();
        return err;
    }
    const FRESULT res = f_mount(s->fs, drv, 1);
    s_core.not_fat = res == FR_NO_FILESYSTEM;
    if (res == FR_OK) return ESP_OK;
    printf("YK_TF mount_error=%d\n", res);
    unmount_now();
    return res == FR_NO_FILESYSTEM ? ESP_ERR_NOT_SUPPORTED : ESP_FAIL;
}

/** An app finds its directory there. Writes the card only the first time. */
static void make_app_dir(void) {
#if !CONFIG_YK_TF_READ_ONLY
    struct stat st;
    if (!s->app_dir[0] || !stat(s->app_dir, &st)) return;
    mkdir(YK_TF_APPS_ROOT, 0775);
    mkdir(s->app_dir, 0775);
#endif
}

/** What a power cut left, before anyone writes: a short name left over may share a chain, and cleared later it could take
 *  clusters that have gone to someone else since (yk_tf_fat.h). Once a boot, under the lock of the first acquire: in every
 *  firmware with the card, the system's or not, for whoever writes first. */
static void clean_once(void) {
#if !CONFIG_YK_TF_READ_ONLY
    if (s->cleaned) return;
    s->cleaned = true;
    const int64_t t0 = esp_timer_get_time();
    const yk_tf_pkg_err_t apps = yk_tf_fat_cleanup(YK_TF_SYS_ROOT "/" YK_TF_SYS_APPS);
    const yk_tf_pkg_err_t buddy = yk_tf_fat_cleanup(YK_TF_SYS_ROOT "/" YK_TF_SYS_BUDDY);
    printf("YK_TF cleaned apps=%d buddy=%d ms=%d\n", apps, buddy, (int)((esp_timer_get_time() - t0) / 1000));
#endif
}

/** The system's check (yk_tf_card_layout), after the clean-up: at the first mount of a start, and the first after the card
 *  was lent to a computer, which may have taken anything away. */
static void layout_once(void) {
#if !CONFIG_YK_TF_READ_ONLY
    if (!s->layout_due || !yk_tf_card_layout) return;
    s->layout_due = false;
    yk_tf_card_layout();
#endif
}

/** esp_restart() is on its way. Holders may still be in FatFs: their structures stay as they are, and the refusals of
 *  yk_tf_core_io() keep them off the card while it stops. No transfer is left halfway unless one outlasts the wait. */
static void on_shutdown(void) {
    if (!s) return;
    yk_tf_core_end(&s_core);
    if (xSemaphoreTakeRecursive(s->lock, pdMS_TO_TICKS(TF_SHUTDOWN_WAIT_MS)) != pdTRUE) return;
    card_close();
    unlock();
}

// ---------------------------------------------------------------- yaykit_tf.h

esp_err_t yk_tf_acquire(void) {
    if (!ready()) return ESP_ERR_NO_MEM;
    lock();
    const bool bluetooth = yk_link_ble_active && yk_link_ble_active();
    const size_t free_before = internal_free();
    esp_err_t err;
    switch (yk_tf_core_acquire(&s_core, now_ms(), bluetooth, free_before)) {
    case YK_TF_GO_HOLD: err = ESP_OK; break;
    case YK_TF_GO_MOUNT: {
        const uint64_t t0 = now_ms();
        err = mount_now();
        yk_tf_core_mounted(&s_core, err == ESP_OK, now_ms());
        printf("YK_TF mount err=%s ms=%" PRIu64 " internal_free=%u fails=%u\n", esp_err_to_name(err), now_ms() - t0,
               (unsigned)internal_free(), s_core.fails);
        if (err == ESP_OK) {
            clean_once();
            make_app_dir();
            layout_once();
            if (!s->shutdown_hooked) s->shutdown_hooked = esp_register_shutdown_handler(on_shutdown) == ESP_OK;
        }
        break;
    }
    case YK_TF_NO_RAM: err = ESP_ERR_NO_MEM; break;
    case YK_TF_NO_RESTING: err = ESP_ERR_NOT_FOUND; break;
    default: err = ESP_ERR_INVALID_STATE; break;
    }
    unlock();
    return err;
}

void yk_tf_release(void) {
    if (!__atomic_load_n(&s, __ATOMIC_ACQUIRE)) return;
    lock();
    if (yk_tf_core_release(&s_core)) unmount_now();
    unlock();
}

const char *yk_tf_dir(void) { return ready() && s->app_dir[0] ? s->app_dir : NULL; }

esp_err_t yk_tf_path(const char *rel, char *out, size_t n) {
    const char *dir = yk_tf_dir();
    if (!dir) {
        if (out && n) out[0] = 0;
        return ESP_ERR_INVALID_STATE;
    }
    switch (yk_tf_join(dir, rel, out, n)) {
    case YK_TF_JOIN_OK: return ESP_OK;
    case YK_TF_JOIN_SMALL: return ESP_ERR_INVALID_SIZE;
    default: return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t yk_tf_space(uint64_t *used, uint64_t *quota, uint64_t *card_free) {
    if (!__atomic_load_n(&s, __ATOMIC_ACQUIRE) || !s_core.mounted) return ESP_ERR_INVALID_STATE;
    uint64_t total = 0, free_bytes = 0;
    esp_err_t err = esp_vfs_fat_info(YK_TF_BASE, &total, &free_bytes);
    if (err != ESP_OK) return err;
    note_space(total, free_bytes);
    if (quota) *quota = yk_tf_quota_for(total);
    if (card_free) *card_free = free_bytes;
    if (!used) return ESP_OK;
    if (!s->app_dir[0]) return ESP_ERR_INVALID_STATE;
    const uint32_t cluster = (uint32_t)s->fs->csize * (uint32_t)s->card.csd.sector_size;
    return yk_tf_fat_usage(s->app_dir, cluster, used) == YK_TF_PKG_OK ? ESP_OK : ESP_FAIL;
}

yk_tf_state_t yk_tf_state(void) {
    return __atomic_load_n(&s, __ATOMIC_ACQUIRE) ? yk_tf_core_state(&s_core, now_ms()) : YK_TF_OFF;
}

// ---------------------------------------------------------------- yk_tf_card.h: what yk_tf_sys.h names for the system

void yk_tf_card_known(yk_tf_known_t *out) {
    const tf_t *t = __atomic_load_n(&s, __ATOMIC_ACQUIRE);
    yk_tf_core_known(&s_core, now_ms(), t ? t->total_kb : 0, t ? t->free_kb : 0, out);
}

const char *yk_tf_card_sys_dir(const char *name) {
    if (!yk_tf_name_ok(name) || !ready()) return NULL;
    char want[YK_TF_DIR_SIZE];
    snprintf(want, sizeof(want), YK_TF_SYS_ROOT "/%s", name);
    const char *found = NULL;
    lock();
    for (int i = 0; i < TF_SYS_DIRS && !found; i++) {
        if (!s->sys_dir[i][0]) strcpy(s->sys_dir[i], want);
        if (!strcmp(s->sys_dir[i], want)) found = s->sys_dir[i];
    }
    unlock();
    return found;
}

bool yk_tf_card_drive(char out[3]) {
    if (!__atomic_load_n(&s, __ATOMIC_ACQUIRE)) return false;
    lock();
    const bool on = s_core.mounted && s->pdrv != FF_DRV_NOT_USED;
    if (on) drive_name(out);
    unlock();
    return on;
}

esp_err_t yk_tf_card_suspend(uint32_t wait_ms) {
    if (!ready()) return ESP_ERR_NO_MEM;
    yk_tf_core_pause(&s_core);  // first, without the lock: a transfer may hold that for a while
    const int64_t until = esp_timer_get_time() + (int64_t)wait_ms * 1000;
    for (;;) {
        const int64_t left_us = until - esp_timer_get_time();
        const TickType_t ticks = left_us > 0 ? pdMS_TO_TICKS(left_us / 1000) : 0;
        if (xSemaphoreTakeRecursive(s->lock, ticks) == pdTRUE) {
            // Under the lock: an acquire that got past the pause has finished, an unmount too.
            const bool free_now = yk_tf_core_free(&s_core);
            unlock();
            if (free_now) return ESP_OK;
        }
        if (esp_timer_get_time() >= until) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(TF_POLL_MS));
    }
}

void yk_tf_card_resume(void) {
    if (__atomic_load_n(&s, __ATOMIC_ACQUIRE)) yk_tf_core_resume(&s_core);
}

// ---------------------------------------------------------------- yk_tf_card.h: the card lent to a computer

esp_err_t yk_tf_card_lend(uint32_t wait_ms, uint32_t *sectors) {
    if (!ready()) return ESP_ERR_NO_MEM;
    // Paused first, the holders gone: from here nobody new takes it, and the pause stays until the card is back.
    esp_err_t err = yk_tf_card_suspend(wait_ms);
    if (err != ESP_OK) {
        yk_tf_card_resume();
        return err == ESP_ERR_TIMEOUT ? ESP_ERR_INVALID_STATE : err;
    }
    lock();
    const bool resting = s_core.resting && now_ms() < s_core.rest_until_ms;
    if (!yk_tf_core_lend(&s_core, now_ms())) {
        err = resting ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_STATE;
    } else {
        err = card_open();
        if (err == ESP_OK) {
            s->lent = true;
            s->layout_due = true;
            if (sectors) *sectors = (uint32_t)s->card.csd.capacity;
            if (!s->shutdown_hooked) s->shutdown_hooked = esp_register_shutdown_handler(on_shutdown) == ESP_OK;
        } else {
            yk_tf_core_fail(&s_core, now_ms());
            yk_tf_core_lend_end(&s_core);
        }
    }
    unlock();
    printf("YK_TF lend err=%s sectors=%u internal_free=%u\n", esp_err_to_name(err),
           err == ESP_OK ? (unsigned)s->card.csd.capacity : 0, (unsigned)internal_free());
    if (err != ESP_OK) yk_tf_card_resume();
    return err;
}

int yk_tf_card_lend_io(bool write, uint32_t sector, void *buf, uint32_t count) {
    if (!__atomic_load_n(&s, __ATOMIC_ACQUIRE) || !count) return -1;
    xfer_t x = {.buf = buf, .sector = sector, .count = count, .write = write};
    const yk_tf_io_t io = {.transfer = xfer_run, .restart = card_restart, .ctx = &x, .now_ms = now_ms()};
    lock();
    const int r = s->lent ? yk_tf_core_io(&s_core, &io) : -1;
    unlock();
    return r;
}

void yk_tf_card_return(void) {
    if (!__atomic_load_n(&s, __ATOMIC_ACQUIRE)) return;
    lock();
    const bool was = s->lent;
    if (was) {
        card_close();
        s->lent = false;
        yk_tf_core_lend_end(&s_core);
        __atomic_add_fetch(&s_returns, 1, __ATOMIC_RELEASE);
    }
    unlock();
    if (!was) return;
    printf("YK_TF returned fails=%u\n", s_core.fails);
    yk_tf_card_resume();
}

uint32_t yk_tf_card_returns(void) { return __atomic_load_n(&s_returns, __ATOMIC_ACQUIRE); }

bool yk_tf_card_usb(void) {
    const tf_t *t = __atomic_load_n(&s, __ATOMIC_ACQUIRE);
    return t && t->lent;
}

// ---------------------------------------------------------------- yk_tf_card.h: FAT's own, for the system's check

/** "/sdcard/x" as FatFs names it on the mounted drive, "0:/x". */
static bool fat_path(const char *path, char *out, size_t n) {
    const size_t b = strlen(YK_TF_BASE);
    if (!path || strncmp(path, YK_TF_BASE, b) || path[b] != '/' || s->pdrv == FF_DRV_NOT_USED) return false;
    const int w = snprintf(out, n, "%c:%s", '0' + s->pdrv, path + b);
    return w > 0 && (size_t)w < n;
}

int yk_tf_card_attr(const char *path, uint8_t *attr) {
    if (!__atomic_load_n(&s, __ATOMIC_ACQUIRE) || !s_core.mounted) return -1;
    char p[YK_TF_PKG_PATH + 4];
    if (!fat_path(path, p, sizeof(p))) return -1;
    FILINFO *fi = heap_caps_malloc(sizeof(*fi), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);  // a long name: 260 bytes
    if (!fi) return -1;
    const FRESULT r = f_stat(p, fi);
    if (r == FR_OK) *attr = fi->fattrib;
    free(fi);
    return r == FR_OK ? 0 : -1;
}

int yk_tf_card_set_attr(const char *path, uint8_t attr, uint8_t mask) {
#if CONFIG_YK_TF_READ_ONLY
    (void)path, (void)attr, (void)mask;
    return -1;
#else
    if (!__atomic_load_n(&s, __ATOMIC_ACQUIRE) || !s_core.mounted) return -1;
    char p[YK_TF_PKG_PATH + 4];
    if (!fat_path(path, p, sizeof(p))) return -1;
    return f_chmod(p, attr, mask) == FR_OK ? 0 : -1;
#endif
}

#if FF_USE_LABEL
/** The boot sector, read (`write` false) or written, through the disk under FatFs: 0 done. FatFs keeps no copy of it
 *  that it would write back (its window holds a FAT or a directory sector by now; let go of it anyway). Only the label
 *  calls use it (an extension's build has no labels: CONFIG_YK_TF_SYSTEM). */
static int boot_sector(bool write, LBA_t sector, uint8_t *buf) {
    if (s->fs->winsect == sector) s->fs->winsect = (LBA_t)0 - 1;
    return (write ? disk_write(s->fs->pdrv, buf, sector, 1) : disk_read(s->fs->pdrv, buf, sector, 1)) == RES_OK ? 0 : -1;
}
#endif

int yk_tf_card_label(char *dir, char *boot, bool *boot_field) {
    dir[0] = boot[0] = 0;
    *boot_field = false;
#if !FF_USE_LABEL
    return -1;
#else
    if (!__atomic_load_n(&s, __ATOMIC_ACQUIRE) || !s_core.mounted || !s->fs || s->fs->ssize != 512) return -1;
    char drv[3];
    drive_name(drv);
    DWORD vsn;
    if (f_getlabel(drv, dir, &vsn) != FR_OK) return -1;
    uint8_t *sec = heap_caps_malloc(512, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);  // the card's DMA, half a moment
    if (!sec) return -1;
    const int r = boot_sector(false, s->fs->volbase, sec);
    if (!r) *boot_field = yk_tf_boot_label(sec, boot);
    free(sec);
    return r;
#endif
}

int yk_tf_card_set_label(const char *label, bool dir, bool boot) {
#if CONFIG_YK_TF_READ_ONLY || !FF_USE_LABEL
    (void)label, (void)dir, (void)boot;
    return -1;
#else
    if (!__atomic_load_n(&s, __ATOMIC_ACQUIRE) || !s_core.mounted || !s->fs || s->fs->ssize != 512) return -1;
    if (dir) {
        char l[3 + 12];
        drive_name(l);
        snprintf(l + 2, sizeof(l) - 2, "%s", label);  // "0:TIKPAL": the volume's, as f_setlabel takes it
        if (f_setlabel(l) != FR_OK) return -1;
    }
    if (!boot) return 0;
    uint8_t *sec = heap_caps_malloc(512, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!sec) return -1;
    uint32_t backup = 0;
    int r = boot_sector(false, s->fs->volbase, sec);
    if (!r) r = yk_tf_boot_label_set(sec, label, &backup) ? boot_sector(true, s->fs->volbase, sec) : -1;
    if (!r && backup && !boot_sector(false, s->fs->volbase + backup, sec)) {
        // FAT32's copy, as a formatter keeps it; a copy that is no boot sector is left as it is.
        if (yk_tf_boot_label_set(sec, label, NULL)) r = boot_sector(true, s->fs->volbase + backup, sec);
    }
    if (!r && disk_ioctl(s->fs->pdrv, CTRL_SYNC, NULL) != RES_OK) r = -1;
    free(sec);
    return r;
#endif
}

int yk_tf_card_expand(const char *path, uint64_t size) {
#if CONFIG_YK_TF_READ_ONLY
    (void)path, (void)size;
    return -1;
#else
    if (!__atomic_load_n(&s, __ATOMIC_ACQUIRE) || !s_core.mounted || size > 0xFFFFFFFFull) return -1;
    char p[YK_TF_PKG_PATH + 4];
    if (!fat_path(path, p, sizeof(p))) return -1;
    FIL *f = heap_caps_calloc(1, sizeof(*f), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);  // a sector's buffer in it
    if (!f) return -1;
    FRESULT r = f_open(f, p, FA_CREATE_NEW | FA_WRITE);
    if (r == FR_OK) {
        // In one run (f_expand reads the FAT until it finds one and writes the chain; nothing written to the clusters).
        // None: FR_DENIED. Not grown a cluster at a time instead (a seek past the end): on a card whose free room is in
        // pieces that writes both FATs for every cluster, a thousand and more, seconds at a start.
        r = f_expand(f, (FSIZE_t)size, 1);
        const FRESULT c = f_close(f);
        if (r == FR_OK) r = c;
        if (r != FR_OK) f_unlink(p);
    }
    free(f);
    return r == FR_OK ? 0 : r == FR_DENIED ? 1 : -1;
#endif
}

// ---------------------------------------------------------------- what the store needs (yk_tf_fat.h)

void *yk_tf_pkg_alloc(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

void yk_tf_pkg_free(void *p) { free(p); }

uint64_t yk_tf_pkg_card_free(void) {
    uint64_t total = 0, free_bytes = 0;
    if (esp_vfs_fat_info(YK_TF_BASE, &total, &free_bytes) != ESP_OK) return 0;
    note_space(total, free_bytes);
    return free_bytes;
}

bool yk_tf_pkg_stop_asked(void) { return s && yk_tf_core_stop_asked(&s_core); }

void yk_tf_pkg_note_failure(void) {
    if (!s) return;
    lock();
    yk_tf_core_fail(&s_core, now_ms());
    unlock();
}

bool yk_tf_pkg_writer_take(void) {
    if (!ready()) return false;
    lock();
    const bool free_now = !s->writer;
    s->writer = true;
    unlock();
    return free_now;
}

void yk_tf_pkg_writer_give(void) {
    if (!__atomic_load_n(&s, __ATOMIC_ACQUIRE)) return;
    lock();
    s->writer = false;
    unlock();
}
