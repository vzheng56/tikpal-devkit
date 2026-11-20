// The pure parts of yaykit_tf, host tested (test/run.sh): what a path may be, how much an app may use, which bounce
// buffer, and the card's state: holders, pauses, failures and the restart after a timeout.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "yk_tf_state.h"

#define YK_TF_BASE "/sdcard"
#define YK_TF_APPS_ROOT YK_TF_BASE "/apps"
// Not `tikpal`: old Tikpal firmware keeps /sd/tikpal/rec, and FAT does not tell cases apart (development doc 3.4).
#define YK_TF_SYS_ROOT YK_TF_BASE "/.tikpal"
// The system's stores in it, for yk_tf_sys_dir(): app images, and the 形象 packs not in the two built-in places. Every
// firmware cleans them up once a boot (yk_tf_fat_cleanup()), the system's or not.
#define YK_TF_SYS_APPS "apps"
#define YK_TF_SYS_BUDDY "buddy"

#define YK_TF_NAME_MAX 31   // a slug, or a system directory's name
#define YK_TF_REL_MAX 200   // what yk_tf_path() takes
#define YK_TF_REL_LEVELS 8  // and how deep: what yk_tf_fat_usage() walks
#define YK_TF_DIR_SIZE 48   // "/sdcard/.tikpal/" and a name, and the NUL

// ---------------------------------------------------------------- paths

/** a-z, 0-9 and '-', 1–31 characters, starting with a letter or digit: the slug rule of yaykit_install. */
bool yk_tf_name_ok(const char *name);

/** A path an app may give below its own directory, in the one spelling that means what it says: parts of A-Z a-z 0-9
 *  . _ -, split by single slashes, none starting or ending with a dot, at most 8 deep. So no `..`, no absolute path, no drive (`0:`),
 *  no backslash, no %xx, no bytes above ASCII (overlong or full-width dots and slashes), no space, and none of the
 *  spellings FAT folds into another name: it drops trailing dots and spaces, and `~` reaches a short name. */
bool yk_tf_rel_ok(const char *rel);

typedef enum { YK_TF_JOIN_OK, YK_TF_JOIN_BAD, YK_TF_JOIN_SMALL } yk_tf_join_t;

/** dir + "/" + rel into out; out is "" unless OK, never a cut-off path that would point somewhere else. */
yk_tf_join_t yk_tf_join(const char *dir, const char *rel, char *out, size_t size);

/** The slug out of an image's project name, `yaykit_<slug with underscores>` (firmware rule 4, yk_inst_project_matches):
 *  what an app is comes from its image, not from anything it says. False when it is no such name. */
bool yk_tf_slug_from_project(const char *project, char *out, size_t size);

// ---------------------------------------------------------------- how much

#define YK_TF_QUOTA_MAX (1ULL << 30)   // one app: 1 GB, or an eighth of a smaller card
#define YK_TF_QUOTA_SHARE 8

/** The space one app may use on a card of `total` bytes. */
uint64_t yk_tf_quota_for(uint64_t total);

/** What a file of `size` takes on the card: whole clusters (a 1 KB file takes 32 KB on the sample card). `used` plus
 *  that; stays at UINT64_MAX rather than wrapping. */
uint64_t yk_tf_used_add(uint64_t used, uint64_t size, uint32_t cluster);

// ---------------------------------------------------------------- memory and the controller

// Internal RAM (development doc 3.8; measured 11-06 on one card, the peak of mounting and reading a file, in bytes):
// ESP-IDF's default 8 KB bounce 8,956 (not used: it is what 3.8 rules out), 4 KB bounce 4,860, one sector 1,276.
// Reads and writes that go through PSRAM (FatFs's own buffers do) bounce through an internal buffer of this many sectors.
#define YK_TF_BOUNCE_BATCH 8
#define YK_TF_BOUNCE_SECTOR 1
// Below this much free, one sector: 4 KB would still leave 45,140 at the peak, above the 40,000 line with room for
// what runs beside it. Speed there: 4 KB about 1.9 MB/s, one sector about 0.85 MB/s.
#define YK_TF_RAM_BATCH 50000
// Below this much free no mount at all: 40,000 + 1,276 = 41,276, and a little for what is not measured.
#define YK_TF_RAM_MOUNT 42000

/** Sectors of bounce buffer for a transfer with this much internal RAM free. */
int yk_tf_bounce_sectors(size_t internal_free);

// ESP-IDF writes 100 ms and has no option. A read has been seen to take 656 ms (one in five or six thousand, 11-06).
// The controller counts in card clocks in 24 bits: 838 ms at 20 MHz is as far as it goes.
#define YK_TF_DATA_TIMEOUT_MS 838
#define YK_TF_TIMEOUT_REG_MAX 0xFFFFFFu

/** The data timeout register's value for a card clock in kHz. */
uint32_t yk_tf_timeout_cycles(uint32_t card_khz);

// ---------------------------------------------------------------- the card's state

#define YK_TF_FAILS_MAX 3                           // in a row, then the card rests (development doc 3.7)
#define YK_TF_REST_MS (24ULL * 60 * 60 * 1000)
#define YK_TF_RESTARTS_MAX 4                        // card restarts in a row with few good transfers between them
#define YK_TF_GOOD_RUN 64                           // this many good transfers give the restarts back

typedef struct {
    uint32_t refs;           // holders
    uint32_t pauses;         // yk_tf_sys_suspend() not resumed yet
    bool mounted;
    bool mounting;
    bool ending;             // the chip is restarting: nothing more
    bool lost;               // a restart did not bring the card back: no transfer until the next mount
    bool session_bad;        // a failure since the mount
    bool resting;
    bool opened;             // a mount has worked since this start
    bool not_fat;            // the last mount found no file system FatFs reads
    bool lent;               // lent to a computer by sector (the USB disk): transfers without a mount
    uint8_t fails;           // in a row: a clean session clears them
    uint8_t restarts;
    uint8_t good;
    uint64_t rest_until_ms;  // since boot: "this boot" of 3.7 is the uptime clock
} yk_tf_core_t;

typedef enum {
    YK_TF_GO_HOLD,        // it is mounted: one more holder, counted
    YK_TF_GO_MOUNT,       // the first holder: mount, then yk_tf_core_mounted()
    YK_TF_NO_PAUSED,
    YK_TF_NO_BLUETOOTH,   // with Bluetooth on there are about 3,000 bytes to spare (3.8)
    YK_TF_NO_RESTING,
    YK_TF_NO_RAM,
    YK_TF_NO_ENDING,
} yk_tf_go_t;

/** May someone take the card now? Only the GO answers change anything (and the end of a rest: one more try, and one
 *  more failure rests it again). */
yk_tf_go_t yk_tf_core_acquire(yk_tf_core_t *c, uint64_t now_ms, bool bluetooth, size_t internal_free);

/** After GO_MOUNT. ok: the first holder is counted. Not ok: one failure, unless a transfer already counted it. */
void yk_tf_core_mounted(yk_tf_core_t *c, bool ok, uint64_t now_ms);

/** One holder less. True when that was the last: unmount now. One too many does nothing. */
bool yk_tf_core_release(yk_tf_core_t *c);

/** A failure: a transfer that did not come back, a mount, a check that did not pass. */
void yk_tf_core_fail(yk_tf_core_t *c, uint64_t now_ms);

/** The card lent out by sector (yk_tf_card_lend(): the USB disk), after a pause has emptied it: transfers go on without a
 *  mount, with the restart after a timeout as ever. False: not now (the chip is ending, the card rests, it is lent or
 *  held already). */
bool yk_tf_core_lend(yk_tf_core_t *c, uint64_t now_ms);
/** Back from the computer: a clean lending clears the failures, as a clean session does. */
void yk_tf_core_lend_end(yk_tf_core_t *c);

/** No new holders. Needs no lock: a transfer can hold that for a second. */
void yk_tf_core_pause(yk_tf_core_t *c);
void yk_tf_core_resume(yk_tf_core_t *c);
/** Nobody holds the card, is mounting it or has it lent. */
bool yk_tf_core_free(const yk_tf_core_t *c);

/** The chip is restarting: no new holders and no transfers, whatever is mounted. */
void yk_tf_core_end(yk_tf_core_t *c);

/** A long job should stop: the system wants the card. */
bool yk_tf_core_stop_asked(const yk_tf_core_t *c);

yk_tf_state_t yk_tf_core_state(const yk_tf_core_t *c, uint64_t now_ms);

/** What yk_tf_sys_known() says. `total_kb`, `free_kb`: the card when it was last measured; total_kb 0 not yet. */
void yk_tf_core_known(const yk_tf_core_t *c, uint64_t now_ms, uint32_t total_kb, uint32_t free_kb, yk_tf_known_t *out);

/** One transfer. 0 is done; anything else from transfer() or restart() is failed. */
typedef struct {
    int (*transfer)(void *ctx);
    int (*restart)(void *ctx);
    void *ctx;
    uint64_t now_ms;
} yk_tf_io_t;

/** Does the transfer. After a timeout the card stays in its data state and every read after it fails at once, however
 *  long the timeout (11-06): when one fails the card is started again and the transfer tried once more, so FatFs and
 *  the app see one slow read. Refused at once when the card is resting, lost or the chip is ending. */
int yk_tf_core_io(yk_tf_core_t *c, const yk_tf_io_t *io);

/** CRC-32 (the zlib one) of a buffer: the headers of yk_tf_pkg.h. */
uint32_t yk_tf_crc32(const void *data, size_t size);

// ---------------------------------------------------------------- the card's name on a computer (Yay 0.1.19 开发文档 4.2)
// The user, 11-13: NO NAME on the Mac is no name for it. A card with none, or the formatter's NO NAME, is named TIKPAL
// (FAT: 11 characters of the code page at most, upper case, so not TIKPAL VOICE nor Chinese); a name the owner gave it
// on a computer stays. FAT keeps it twice, as an entry in the root directory (what FatFs's f_setlabel writes, and what
// Windows reads) and in the boot sector (what a formatter writes): both are put right.

#define YK_TF_LABEL "TIKPAL"
#define YK_TF_LABEL_BUF 34  // a label as FatFs gives it in UTF-8: 11 characters of the code page, 3 bytes each at most

/** The label field of a FAT volume's boot sector (`sec`, 512 bytes) into `out` (YK_TF_LABEL_BUF), the spaces at its end
 *  dropped ("" when blank). False: the sector has no such field (exFAT, no 55 AA, no extended boot signature 0x29). */
bool yk_tf_boot_label(const uint8_t *sec, char *out);

/** Writes `label` (upper case ASCII, 11 at most) into that field, padded with spaces. `backup`: where FAT32 keeps a copy
 *  of the sector, counted from it (0: none). False: no field, or a label that does not fit. */
bool yk_tf_boot_label_set(uint8_t *sec, const char *label, uint32_t *backup);

/** Which copies of the label to write (`dir`: the root directory's, `boot`: the boot sector's, "" for none, NULL when
 *  the sector has no field for one): when the card has no name (none or NO NAME) or is TIKPAL already, those not TIKPAL
 *  yet; when the owner named it (in the root directory, or only in the boot sector), none. */
void yk_tf_label_plan(const char *dir, const char *boot, bool *set_dir, bool *set_boot);

/** A Mac's rename of a card we named (真机验收 11-14, then a FAT32 image renamed with `diskutil rename`): macOS writes
 *  the boot sector's copy only when the root directory's label is not its first entry (f_setlabel puts ours in the
 *  first free one), and mounting it again it shows the root directory's, TIKPAL. Ours in the root directory and a name
 *  of the owner's in the boot sector: true, and that name goes into the root directory too, so that it holds on every
 *  computer (yk_tf_label_plan leaves both copies then). `boot` NULL (no field), no name or ours: false. */
bool yk_tf_label_follow(const char *dir, const char *boot);

/** Under .tikpal: both copies as the system last saw them (0.1.20 4.8), so that a rename is told by which one moved. */
#define YK_TF_LABEL_FILE "label"

typedef enum {
    YK_TF_LABEL_KEEP,     // nothing to write
    YK_TF_LABEL_NONE,     // no name: TIKPAL
    YK_TF_LABEL_RENAMED,  // renamed on a computer, which wrote one copy: the other one follows
} yk_tf_label_do_t;

/** What to write now (0.1.20 4.8: a Mac's second rename, and back to TIKPAL). `dir`, `boot` as yk_tf_label_plan has
 *  them; `was_dir`, `was_boot`: the two the last time (YK_TF_LABEL_FILE), NULL when not known. A Mac writes the boot
 *  sector's copy only, Windows the root directory's: the one that moved since is the owner's new name and the other is
 *  written to match, whatever it is (TIKPAL too: a card renamed back to it stays so); no name is TIKPAL. Both moved:
 *  the root directory's, what a computer shows. The boot sector takes plain upper case ASCII, 11 at most: a name it
 *  cannot hold stays in the root directory only. Not known (a card from before, or formatted since), or no field in the
 *  boot sector (`boot` NULL): as before, yk_tf_label_follow, then yk_tf_label_plan. `to` (YK_TF_LABEL_BUF): the name. */
yk_tf_label_do_t yk_tf_label_sync(const char *dir, const char *boot, const char *was_dir, const char *was_boot, char *to,
                                  bool *set_dir, bool *set_boot);

/** YK_TF_LABEL_FILE's text: "<dir>\n<boot>\n". */
#define YK_TF_LABEL_TEXT (2 * YK_TF_LABEL_BUF + 2)
/** Its two names from `text` (`n` bytes) into `dir`, `boot` (YK_TF_LABEL_BUF each): false when it is not whole. */
bool yk_tf_label_parse(const char *text, size_t n, char *dir, char *boot);
