// The card's own half of what yk_tf_sys.h names for the system (yaykit_tf.c): the system's functions are compiled only
// with CONFIG_YK_TF_SYSTEM (yk_tf_sys.c), what they do always. Not in include/, so no app gets it (Yay 0.1.19 开发文档
// 6.1 step 3). yk_app_power.c stops the card before deep sleep in any firmware that has yaykit_tf: it links
// yk_tf_card_suspend() weakly, by this name (test/run.sh checks the two agree).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "yk_tf_state.h"

const char *yk_tf_card_sys_dir(const char *name);  // yk_tf_sys_dir()
esp_err_t yk_tf_card_suspend(uint32_t wait_ms);    // yk_tf_sys_suspend()
void yk_tf_card_resume(void);                      // yk_tf_sys_resume()
void yk_tf_card_known(yk_tf_known_t *out);         // yk_tf_sys_known()

/** The mounted volume's FatFs drive ("0:") into out, for what reads through FatFs itself (yk_folder_read.c): only while
 *  the caller holds the card. False when it is not mounted. */
bool yk_tf_card_drive(char out[3]);

// ---------------------------------------------------------------- the card lent to a computer (Yay 0.1.19 开发文档 4.2)
// The USB disk (yaykit_usb) hands the computer the whole card, sector by sector. Lending pauses the card and waits for its
// holders to let go, so nothing on the device reads or writes it until it is back; the transfers keep the card's timeout
// and its restart after one.

/** ESP_OK: lent, `sectors` of 512 bytes; yk_tf_card_lend_io() until yk_tf_card_return(). ESP_ERR_INVALID_STATE: a holder
 *  did not let go in wait_ms, or it is lent already, or the chip is restarting. ESP_ERR_NOT_FOUND: the card rests after
 *  failures. Anything else: the card did not answer. */
esp_err_t yk_tf_card_lend(uint32_t wait_ms, uint32_t *sectors);
/** One transfer of `count` sectors from `sector`, `buf` in internal RAM DMA can reach. 0: done. */
int yk_tf_card_lend_io(bool write, uint32_t sector, void *buf, uint32_t count);
/** The card back to the device: the next mount checks it again (yk_tf_card_layout). Nothing when not lent. */
void yk_tf_card_return(void);
/** Lent now. The shared folders (yk_folder*) link it weakly, by this name. */
bool yk_tf_card_usb(void);
/** yk_tf_card_return()s that took it back from a lend, this start (yk_tf_sys_returns()). */
uint32_t yk_tf_card_returns(void);

// ---------------------------------------------------------------- FAT's own, for the system's check (yk_tf_layout.c)

/** A path under YK_TF_BASE, while the card is held. 0: done. */
int yk_tf_card_attr(const char *path, uint8_t *attr);
int yk_tf_card_set_attr(const char *path, uint8_t attr, uint8_t mask);
/** A new file of `size` bytes in one run of clusters, allocated, not written. 0: done; 1: no run that long; -1: failed. */
int yk_tf_card_expand(const char *path, uint64_t size);
/** The volume's label (yk_tf_logic.h YK_TF_LABEL): the root directory's into `dir`, the boot sector's into `boot`
 *  (YK_TF_LABEL_BUF each, "" for none), `boot_field` false when the boot sector has no field for one. 0: done. Needs
 *  CONFIG_FATFS_USE_LABEL (CONFIG_YK_TF_SYSTEM selects it). */
int yk_tf_card_label(char *dir, char *boot, bool *boot_field);
/** Writes `label` into the root directory (`dir`), and into the boot sector and FAT32's copy of it (`boot`). 0: done. */
int yk_tf_card_set_label(const char *label, bool dir, bool boot);

/** The system's check of the card (yk_tf_sys.c, CONFIG_YK_TF_SYSTEM): at the first mount of a start and the first after
 *  a lending, under the first holder. Weak: firmware without the system's side has none. */
void yk_tf_card_layout(void) __attribute__((weak));
