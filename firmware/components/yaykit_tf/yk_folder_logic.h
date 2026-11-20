// The pure parts of yk_folder.h, host tested (test/run.sh): what a path in a shared folder may be, and what the
// question decides, from what this device remembers (NVS) and what the device service says (Yay 0.1.19 开发文档 3.1,
// 4.6, 6.2). No ESP-IDF here.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define YK_FOLDER_KINDS 4
#define YK_FOLDER_ALL 0x0Fu  // the four kinds as bits: music 1, stories 2, pictures 4, documents 8

/** The folder's name on the card (Music, Stories, Pictures, Documents), and its word in the device API (music...).
 *  NULL out of range. */
const char *yk_folder_dir_name(int kind);
const char *yk_folder_kind_word(int kind);

// ---------------------------------------------------------------- the person's names (开发文档 4.3)

#define YK_FOLDER_PATH_LEVELS 8
#define YK_FOLDER_PATH_PART 255  // UTF-16 units of a part: FatFs's long names
// The longest path the rule lets through, in bytes: 8 parts of 255 characters of three bytes, and the slashes.
#define YK_FOLDER_PATH_BYTES (YK_FOLDER_PATH_LEVELS * YK_FOLDER_PATH_PART * 3 + YK_FOLDER_PATH_LEVELS - 1)

/** A path the person's files may have, relative to a shared folder: parts split by single slashes, at most 8, none
 *  empty, `.` or `..`, none starting with a dot (a computer's own files, never listed) or ending with a dot or a space
 *  (FAT drops those: the name would be another's); no control characters, no backslash, none of FAT's `"*:<>?|`
 *  (`:` would be a drive); well-formed UTF-8 (no overlong forms, no surrogates, nothing past U+10FFFF); each part at
 *  most 255 UTF-16 units. `empty_ok`: "" is the folder itself (a listing). This is not yk_tf_name_ok() nor
 *  yk_tf_rel_ok(), the system's and an app's own: those stay ASCII. */
bool yk_folder_path_ok(const char *path, bool empty_ok);

/** A name as listed: it starts with a dot (Mac's ._ files, .Spotlight-V100) or is hidden or a system file. */
bool yk_folder_name_hidden(const char *name, bool hidden_or_system);

// ---------------------------------------------------------------- the person's answer, per app (开发文档 3.1)

typedef enum {
    YK_GRANT_NONE = 0,  // never asked on this device (or the record is gone)
    YK_GRANT_GRANTED = 1,
    YK_GRANT_DENIED = 2,
} yk_grant_state_t;

/** One app's answer as this device keeps it (NVS namespace yk_sys_folder), and as the service has it. */
typedef struct {
    uint8_t state;    // yk_grant_state_t
    uint8_t folders;  // the kinds it covers, YK_FOLDER_ALL bits: GRANTED, the kinds the person said yes to
    bool known;       // `rev` is the service's: false until the service has told this device (a PUT's base is null)
    bool pending;     // the person answered here and the service has not heard yet: tell it when online
    bool full;        // the service said 409 GRANTS_FULL (64 apps on this device already): pending all the same, told
                      // again at this app's next ask only; Yay's check at connecting leaves it (合同 #1845)
    uint32_t rev;     // the service's count of changes to it
} yk_grant_t;

/** What the service answered to GET /device/v1/folders?app=: ANSWERED with its grant (state NONE: it lists none), or
 *  not in time / not at all (2 s, no network, no account: what is kept here stands). */
typedef enum { YK_NET_SILENT = 0, YK_NET_ANSWERED } yk_net_t;

/** This device's record after the service's answer: the service's wins when it is newer (App changed it), or when
 *  nothing here waits to be told. A pending answer made here is kept over a service that has not seen it (its rev
 *  is not newer than the one this answer was made on). */
yk_grant_t yk_grant_merge(yk_grant_t here, yk_net_t net, const yk_grant_t *service);

typedef enum {
    YK_ASK_ALLOWED,      // read
    YK_ASK_DENIED,       // no, and no question
    YK_ASK_QUESTION,     // show the question, with every kind the app declares
} yk_ask_t;

/** What yk_folder_ask() does for an app declaring `declared` (bits) that wants `kind`, `kind` among them: granted
 *  and covering every kind it declares: allowed; granted with fewer kinds than it declares now (an update asked for
 *  more): the question again; denied: denied; never asked: the question. */
yk_ask_t yk_grant_decide(const yk_grant_t *g, uint8_t declared);

/** The person answered the question: yes covers every kind the app declares, no closes the whole app; both wait to be
 *  told to the service, on the rev they were made on (a new answer, so tried again even when the service was full). */
yk_grant_t yk_grant_answered(const yk_grant_t *before, bool yes, uint8_t declared);

/** The app declares fewer kinds than its grant covers (an update dropped one): the grant shrinks with it, its state
 *  as it was (3.1 step 5). True when it changed. */
bool yk_grant_shrink(yk_grant_t *g, uint8_t declared);

/** The service's answer to the PUT: OK with its grant (now known, nothing pending), CHANGED (409 GRANT_CHANGED: the
 *  App changed it meanwhile; the table's grant stands, no question), FULL (409 GRANTS_FULL: nothing written there;
 *  the answer here stands and is acted on, pending, marked full), or nothing (offline: still pending). The two 409s
 *  are told apart by their error code, never by the status. */
typedef enum { YK_PUT_SILENT = 0, YK_PUT_OK, YK_PUT_CHANGED, YK_PUT_FULL } yk_put_t;
yk_grant_t yk_grant_reported(yk_grant_t here, yk_put_t put, const yk_grant_t *service);

// ---------------------------------------------------------------- the record in NVS

#define YK_GRANT_RECORD 44  // bytes: format, state, folders, flags (known, pending, full), rev, the slug (the key is a hash of it)
#define YK_GRANT_KEY 16     // an NVS key: "g" and 8 hex digits, and the slot
#define YK_GRANT_SLUG 32    // a slug as a record keeps it, its NUL in

/** The record of `slug` and `g` into out[YK_GRANT_RECORD]. */
void yk_grant_encode(const char *slug, const yk_grant_t *g, uint8_t out[YK_GRANT_RECORD]);
/** Reads a record back: false when it is not one, or another slug's (a hash shared). */
bool yk_grant_decode(const uint8_t in[YK_GRANT_RECORD], size_t size, const char *slug, yk_grant_t *out);
/** The NVS key of `slug`'s record in slot `slot` (0..3: a slug whose hash another took looks further). */
void yk_grant_key(const char *slug, int slot, char out[YK_GRANT_KEY]);

// ---------------------------------------------------------------- the device service (开发文档 2.1, 3.1)

/** The device service's calls for the grants, yaykit_cloud's (yk_folder_net.c): it hands them over as it starts
 *  (yk_account_init calls yk_folder_net_offer, which calls yk_folder_net_set when this component is linked). A firmware
 *  without yaykit_cloud never has them and goes by the records here alone, Yay's check at connecting and before an
 *  app opens bringing them in line (固件总策划 11-13: an app that does not use the cloud carries no HTTPS for this). */
typedef void (*yk_folder_net_each_fn)(const char *slug, const yk_grant_t *grant, void *ctx);
typedef struct {
    // On a task of their own, waiting `wait_ms` at most (an extension's first ask; Yay before it opens an app).
    // get: 1 answered, `out` its grant (state NONE: it lists none for the app); 0 no answer (not in time, no Wi-Fi up,
    // Bluetooth on, no account, a refusal). put: with `grant`'s state and kinds, base its `rev` when `known` else
    // null: 1 OK and 2 409 GRANT_CHANGED (`out` the service's grant), 3 409 GRANTS_FULL, 0 no answer.
    int (*get)(const char *slug, yk_grant_t *out, uint32_t wait_ms);
    int (*put)(const char *slug, const yk_grant_t *grant, yk_grant_t *out, uint32_t wait_ms);
    // On the caller's task, which holds the TLS (Yay's check at connecting, after the store's list): every app's
    // grant to `each` (1 answered, 0 not); a PUT with Yay's token, answered as `put`.
    int (*list)(yk_folder_net_each_fn each, void *ctx);
    int (*put_now)(const char *slug, const yk_grant_t *grant, yk_grant_t *out);
} yk_folder_net_t;

/** yaykit_cloud's hand-over (`net` lives on); NULL takes them back. */
void yk_folder_net_set(const yk_folder_net_t *net);
/** What was handed over, or NULL. */
const yk_folder_net_t *yk_folder_net(void);
/** Every record of this device goes (yk_folder_nvs.c): the account it was bound to has let it go, and a new owner's
 *  service has none of them (device-api.ts: cleared on release and a new owner). yaykit_cloud's yk_account forget()
 *  calls it, linked weakly (a firmware without the shared folders has none to clear). */
void yk_folder_records_forget(void);

/** Yay's check of one record against the service's whole list (`listed`: the app's grant, or NULL when it lists none;
 *  固件总策划 11-13): an answer made here that waits (or is marked full) stays, to be told (a 409 then says what the App
 *  made of it); anything else is the service's as listed, whatever its rev (a new owner's start again from 1), and
 *  goes when it is not listed. */
yk_grant_t yk_grant_synced(yk_grant_t here, const yk_grant_t *listed);
/** The platform's push of one grant after the App's switch (方案 A, 11-13): taken only when its `rev` is newer than the
 *  one here (or this device was never told one); an older or the same rev changes nothing. */
yk_grant_t yk_grant_pushed(yk_grant_t here, const yk_grant_t *pushed);
/** Yay tells the service the answers that wait: pending and not marked full (a full one waits for its app's ask). */
bool yk_grant_to_tell(const yk_grant_t *g);
/** A record's own slug, for a walk over all of them (yk_folder_sys_sync); false when it is not a record. */
bool yk_grant_record_slug(const uint8_t in[YK_GRANT_RECORD], size_t size, char slug[YK_GRANT_SLUG]);
