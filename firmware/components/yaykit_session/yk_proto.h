// The session protocol v1 (docs/design/yay-device.md 4; packages/contracts/src/device.ts is the one source):
// building the device's messages, checking and reading the server's, and the 4-byte audio frame header. Pure C on
// cJSON, host tested against packages/contracts/fixtures/device.json (test/run.sh).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define YK_PROTO_FRAME_VERSION 1
#define YK_PROTO_STREAM_MIC 1
#define YK_PROTO_STREAM_TTS 2
#define YK_PROTO_HEADER 4
#define YK_PROTO_FRAME_MS 60
#define YK_PROTO_MAX_SECONDS 60
#define YK_PROTO_TURN_LEN 16  // "s-" + 14 for turns the server starts

/** A turn id: 16 letters and digits, or "s-" and 14 (the server's own turns). */
bool yk_proto_turn_ok(const char *turn);
bool yk_proto_server_turn(const char *turn);
/** A new device turn id from 16 random bytes. */
void yk_proto_new_turn(char out[YK_PROTO_TURN_LEN + 1], const uint8_t random[16]);
bool yk_proto_slug_ok(const char *slug);

// ---------------------------------------------------------------- device to cloud
// Each returns a JSON string to free() (cJSON_free), or NULL when an argument breaks the schema.

/** Yay's screensaver as it uses it now (docs/design/screensaver.md; the contract's deviceScreensaver): on or off, the
 *  seconds at rest before it (15..3600) and before the screen goes off after it (60..7200), and the image it checked:
 *  its id ("default-07", a photo's UUID) and sha256 (64 lowercase hex). */
typedef struct {
    bool on;
    int after_s, duration_s;
    const char *image_id;
    const char *image_sha256;
} yk_proto_saver_t;

#define YK_PROTO_SAVER_IMAGE_MAX 98304  // a screensaver image's bytes at most (SCREENSAVER_IMAGE_MAX_BYTES)
/** "default-01".."default-99" (the catalog's) or a UUID's 8-4-4-4-12 hex digits (a photo's). */
bool yk_proto_saver_image_ok(const char *id);

/** Who Yay's home screen shows now, as it checked it (the contract's deviceBuddy): the buddy, and the sha256 of the pack it
 *  uses; sha256 NULL for Pip, who is built in. */
typedef struct {
    const char *id;
    const char *sha256;
} yk_proto_buddy_t;

#define YK_PROTO_BUDDY_PACK_MAX 643072  // a buddy's pack's bytes at most (BUDDY_PACK_MAX_BYTES)
/** ^[a-z][a-z0-9-]{1,30}$ */
bool yk_proto_buddy_id_ok(const char *id);

typedef struct {
    int slot;              // 1..3 (yk_slots index + 1)
    const char *slug;      // NULL or "" when empty
    const char *version;   // NULL or "" when not known
    const char *state;     // "empty", "valid", "pending", "invalid", "aborted", "new", "unknown"
    int64_t last_used_ms;  // 0 = not known
} yk_proto_slot_t;

typedef struct {
    const char *fw;     // "yay/0.1.0"
    const char *board;  // "tikpal-r1"
    yk_proto_slot_t slots[4];
    int slot_count;
    int battery_pct;
    bool charging;
    int rssi;
    const char *locale;  // "zh"
    const char *tz;      // "Asia/Shanghai"
    int out_rate;        // 16000, 24000 or 48000
    // What the session is for (extension-platform.md P1): NULL is chat (left out, as before the modes); "practice"
    // (the language buddy) takes practice_language and practice_level, "translate" takes pair. The platform only
    // opens the modes the app's service manifest lists.
    const char *mode;               // NULL, "chat", "practice", "translate"
    const char *practice_language;  // "zh", "en", "ja", "es"
    const char *practice_level;     // "beginner", "everyday", "fluent"
    const char *pair;               // "zh-ja": the owner's language, then the other person's
    // The settings now (internal-commands.md 6.3): sent when has_prefs. Volume and brightness 0..10 as the actions
    // take them, the screen-off time 0 (never) or 15..3600 s.
    bool has_prefs;
    int volume, brightness, screen_off_s;
    const yk_proto_saver_t *saver;  // with has_prefs: prefs.screensaver (Yay with the screensaver cap); NULL none
    const yk_proto_buddy_t *buddy;  // with has_prefs: prefs.buddy (Yay with the buddy cap); NULL none
    // What this firmware can do beyond the four actions every one knows ("volume.step", "volume.mute",
    // "brightness.step", "screen_off.set", "prefs"); sent when caps is not NULL (an empty list too).
    const char *const *caps;
    int cap_count;
    // Yay with the onboarding cap (docs/delivery/2026-11-06-登录与开箱.md 5.3): where its first setup is, 1..255; 0: left out.
    int onboarding_step;
    bool onboarding_finished;
} yk_proto_hello_t;

/** "zh", "en", "ja", "es": what the practice and translate modes speak. */
bool yk_proto_talk_language_ok(const char *lang);
/** "zh-ja": two different talk languages. */
bool yk_proto_pair_ok(const char *pair);

char *yk_proto_hello(const yk_proto_hello_t *h);
/** side (translate): "mine" or "theirs", which half of the screen was held; NULL for the side key. */
char *yk_proto_listen_start(const char *turn, int preroll_ms, const char *side);
/** The same, `held`: the turn was recorded while connecting and what was kept comes at once ("held": true; the server
 *  passes its packets on to recognition as they are). */
char *yk_proto_listen_start_ex(const char *turn, int preroll_ms, const char *side, bool held);
/** A moment by the device's own count (Yay 0.1.20, device-api.ts recordingClock): which start (bootId, 0..2^31) and the
 *  ms into it (esp_timer, 0..2^40). */
typedef struct {
    uint32_t boot;
    uint64_t ms;
} yk_proto_mono_t;
/** recordingClock: when it was made (`at`), the last time the clock was set as the device noted it (`sync`, with the
 *  time it was set to, `sync_at`: an ISO time with its offset; has_sync false: never this device), and now. */
typedef struct {
    yk_proto_mono_t at;
    bool has_sync;
    yk_proto_mono_t sync;
    const char *sync_at;
    yk_proto_mono_t now;
} yk_proto_clock_t;
/** listen.start of a turn said offline and sent now (Yay with the offline cap, 0.1.20 开发文档 2.2): held, its clock, and
 *  `recorded_at` (an ISO time with its offset, NULL when the clock was not set: left out). NULL when one is out of range. */
char *yk_proto_listen_start_note(const char *turn, int preroll_ms, const char *recorded_at, const yk_proto_clock_t *clock);
/** The same, with `liveTurn` (Yay 0.1.21, 用户 11-18): the note first went live as `live_turn` and the connection went
 *  before the device heard stt.final; a server that heard it then takes it no more. NULL `live_turn`: as above; not a
 *  turn id: NULL. */
char *yk_proto_listen_start_note_live(const char *turn, int preroll_ms, const char *recorded_at,
                                      const yk_proto_clock_t *clock, const char *live_turn);
/** "2026-11-14T10:58:03.000+08:00": Unix ms at `offset_min` east of UTC (what z.iso.datetime({offset: true}) takes). */
const char *yk_proto_time_offset(int64_t unix_ms, int offset_min, char out[32]);
/** offline.digest.say {id} (0.1.20 开发文档 2.3): the screen on, nothing said or heard; `id` offline.digest's (a UUID). */
char *yk_proto_offline_digest_say(const char *id);
/** listen.stop, listen.cancel or playback.stop. */
char *yk_proto_turn_message(const char *type, const char *turn);
char *yk_proto_action_done(const char *id, bool ok, const char *error);
/** The same with the action's kind ("app.install"): sent again in a later session, where the id is not known. */
char *yk_proto_action_done_kind(const char *id, bool ok, const char *error, const char *kind);
/** action.done with a setting's value after it (volume, brightness 0..10; seconds 0..3600); value < 0: none. */
char *yk_proto_action_done_value(const char *id, bool ok, const char *error, int value);
/** action.done with the screensaver Yay uses after it (`saver` NULL: none): screensaver.set's, done or not; and
 *  screen_off.set's while it is on, `value` the new time before it (< 0: none). */
char *yk_proto_action_done_saver(const char *id, bool ok, const char *error, int value, const yk_proto_saver_t *saver);
/** action.done with who Yay shows after it, done or not (`buddy` NULL: none): buddy.set's. */
char *yk_proto_action_done_buddy(const char *id, bool ok, const char *error, const yk_proto_buddy_t *buddy);
/** prefs: the settings changed on the device itself. */
char *yk_proto_prefs(int volume, int brightness, int screen_off_s);
/** The same with Yay's screensaver (`saver` NULL: as yk_proto_prefs). */
char *yk_proto_prefs_saver(int volume, int brightness, int screen_off_s, const yk_proto_saver_t *saver);
/** The same with Yay's buddy too (`buddy` NULL: as yk_proto_prefs_saver). */
char *yk_proto_prefs_all(int volume, int brightness, int screen_off_s, const yk_proto_saver_t *saver,
                         const yk_proto_buddy_t *buddy);
/** One report of onboarding.step: `item` 0 for the whole step; `result` "done", "skipped" or "failed". */
typedef struct {
    int step, item;
    const char *result;
    uint32_t seconds;  // at most YK_PROTO_ONB_SECONDS_MAX
} yk_proto_onb_report_t;
#define YK_PROTO_ONB_REPORTS_MAX 16        // ONBOARDING.reportsMax
#define YK_PROTO_ONB_SECONDS_MAX 2592000   // ONBOARDING.secondsMax
/** onboarding.step: 1 to 16 reports, and `locale` the language chosen on the device ("zh-Hans", "en", "ja", "es-419";
 *  NULL none). NULL when one breaks the schema. */
char *yk_proto_onboarding_step(const yk_proto_onb_report_t *steps, int count, const char *locale);
/** find.stopped (find-my-tikpal.md 2.3): the find.ring `id` stopped, `by` "tap" (the screen), "key" (the side key), "app"
 *  (find.stop), "timeout" (its seconds were up) or "other" (something more urgent: sleep, an install, a restart). */
char *yk_proto_find_stopped(const char *id, const char *by);
/** data_json: an object (NULL = {}). */
char *yk_proto_app_result(const char *slug, const char *data_json);
char *yk_proto_status(int battery_pct, bool charging, int rssi, uint32_t heap_min);
char *yk_proto_auth(const char *token);
/** translate: the languages changed (this session only). */
char *yk_proto_pair_set(const char *pair);
/** translate: the last sentence again (1..500 characters) from the device's memory, said in `from`. */
char *yk_proto_translate_text(const char *turn, const char *text, const char *from);
/** practice: the screen is ready, Yay speaks first. why: "start", "settings", "resume". */
char *yk_proto_practice_open(const char *why);
/** practice: the language or level changed in the settings. */
char *yk_proto_practice_set(const char *language, const char *level);
/** practice: shaken, another topic. */
char *yk_proto_practice_next(void);
/** practice: leaving (no answer is waited for). why: "double" (the app ring), "facedown", "idle", "home" (the phone
 *  asked it back to Yay: app.home). */
char *yk_proto_practice_end(const char *why);
/** app.records: up to 50 records an extension left in the outbox (yk_app_records_each), each a JSON object as
 *  POST /records takes it; only id, kind, occurredAt, clockTrusted and data are passed on (the outbox's "app" is
 *  the slug). NULL when one breaks the schema. */
#define YK_PROTO_RECORDS_MAX 50
char *yk_proto_app_records(const char *slug, const char *const *records, int count);

// ---------------------------------------------------------------- cloud to device

typedef enum {
    YK_SRV_INVALID = 0,  // not JSON, or breaks the schema: ignored (and logged)
    YK_SRV_HELLO_OK,
    YK_SRV_STT_PARTIAL,
    YK_SRV_STT_FINAL,
    YK_SRV_REPLY_LINE,
    YK_SRV_REPLY_END,
    YK_SRV_UI_STATE,
    YK_SRV_ACTION,
    YK_SRV_INBOX,
    YK_SRV_REAUTH,
    YK_SRV_ERROR,
    YK_SRV_PRACTICE_TURN,
    YK_SRV_APP_RECORDS_OK,
    YK_SRV_OFFLINE_DIGEST,  // Yay with the offline cap (0.1.20): digest_id, digest_*, its line in `text`
} yk_srv_type_t;

/** One app's grant to read the shared folders (Yay 0.1.19, 方案 A; device-api.ts folderGrant): hello.ok's folders and
 *  folder.set's grant. */
#define YK_PROTO_FOLDERS_MAX 64  // device-api.ts FOLDER_GRANTS_MAX
typedef struct {
    char app[42];     // its slug (^[a-z][a-z0-9-]{1,40}$)
    uint8_t state;    // 1 granted, 2 denied
    uint8_t folders;  // the kinds as bits: music 1, stories 2, pictures 4, documents 8
    uint32_t rev;     // the service's count, from 1
} yk_proto_folder_t;

/** YK_ACT_UNKNOWN: a kind this firmware does not know (a newer server); answer action.done ok=false UNSUPPORTED. */
typedef enum {
    YK_ACT_UNKNOWN = 0,
    YK_ACT_APP_OPEN,
    YK_ACT_APP_INSTALL,
    YK_ACT_VOLUME,
    YK_ACT_BRIGHTNESS,
    YK_ACT_SCREEN_OFF,
    YK_ACT_APP_HOME,       // back to Yay (an extension on yaykit_talk; internal commands, step 2): no fields
    YK_ACT_SAVER_SET,      // screensaver.set (Yay with the screensaver cap): saver_on, saver_after, saver_duration, image
    YK_ACT_SAVER_PREVIEW,  // screensaver.preview: url, sha256, size, seconds
    YK_ACT_BUDDY_SET,      // buddy.set (Yay with the buddy cap): buddy; every buddy but Pip url, sha256, size (its pack)
    YK_ACT_ONBOARDING_SET, // onboarding.set (Yay with the onboarding cap): onb_skip, onb_locale
    // Find my Tikpal (docs/design/find-my-tikpal.md 2.3; Yay with the find cap): find.ring rings `seconds` (5..300), lit,
    // loud; find.stop stops the ring whose id is `find`.
    YK_ACT_FIND_RING,
    YK_ACT_FIND_STOP,
    YK_ACT_LOCALE_SET,     // locale.set (Yay with the locale cap, 0.1.19): locale, the account's language now
    YK_ACT_COLORWAY_SET,   // colorway.set (Yay with the colorway cap, 0.1.19): colorway, the account's colour now
    YK_ACT_FOLDER_SET,     // folder.set (Yay with the folder cap, 0.1.19): folder, one app's grant after the App's switch
} yk_act_kind_t;

typedef struct {
    yk_srv_type_t type;
    char turn[YK_PROTO_TURN_LEN + 1];  // "" when the message has none
    char *text;                        // stt, reply.line, inbox, ui.state label, practice.turn hint; "" when none
                                       // (owned)
    int line;                          // reply.line
    char lang[8];                      // stt.final (also "other"), reply.line: the language; "" when not given
    bool fix_line;                     // reply.line kind "fix" (practice): the card, not a subtitle
    bool has_credits;                  // reply.end, error: when they carry credits; hello.ok: always
    double credits_used;               // not in hello.ok (0)
    int credits_remaining;
    char muted[12];                    // reply.end: "", "credits" or "speech"
    // hello.ok
    char session[65];
    int64_t time_ms;
    char nickname[48];
    char owner_locale[8];              // hello.ok owner.locale, the account's language ("zh-Hans", "en", "ja", "es-419");
                                       // "" when left out, or not one a device keeps (yaykit_cloud's yk_locale_ok)
    char owner_colorway[12];           // hello.ok owner.colorway (0.1.19), the account's colour as the buddy resolves it
                                       // ("original", "sage", "honey", "blossom", "cocoa", or a later one's name: 1..11
                                       // lower-case letters); "" when left out or not a name
    char reset_at[32];                 // hello.ok credits, error
    bool dot;
    // hello.ok nextReminderAt (docs/delivery/2026-10-26-yay-update-nf.md 2.6): the account's earliest reminder in the
    // next 24 hours. -1 not known (left out, or not a time: the device takes it that there is one), 0 none (null),
    // 1 at next_reminder_ms (Unix ms).
    int8_t next_reminder;
    int64_t next_reminder_ms;
    bool power_log;                    // hello.ok powerLog (Yay 0.1.20 4.10, 测试员计划): the report's extra fields; left
                                       // out or anything but true: false
    bool has_practice;                 // hello.ok practice
    double turn_points;                // what a practice turn costs on average
    int week_days, week_turns;         // this week so far; -1 when not given
    // hello.ok folders (0.1.19, 方案 A): every app's grant on this device, the platform's whole list (0..64, PSRAM,
    // owned; NULL with none). folders_given false when it is left out or null (the platform waited too long on Yaykit:
    // the device asks by GET), or when one in it is not a grant (the list is not taken: a part would clear the rest).
    bool folders_given;
    int folder_count;
    yk_proto_folder_t *folders;
    // hello.ok offlineNotes and binding (Yay with the offline cap, 0.1.20 开发文档 2.3; BOSS-2 11-15): -1 left out (keep
    // the one kept), 0 or 1; the platform's opaque mark of whose it is ("" left out, or not one: the check is skipped).
    int8_t offline_notes;
    char binding[65];
    // offline.digest: its id (a UUID), what it made, its line in `text`
    char digest_id[37];
    int digest_total, digest_notes, digest_reminders, digest_untimed;
    // practice.turn
    char verdict[8];                   // "fix", "good", "none", "blocked"
    char *said, *better, *note;        // fix (owned; NULL without one)
    uint16_t spans[8][2];              // the changed parts of `better`: code points, [start, end)
    int span_count;
    char *words[2], *meanings[2];      // newWords (owned)
    int word_count;
    // app.records.ok: the ids of a batch the platform is done with (kept, already there or dropped), in `slug`
    char **ids;                        // (owned)
    int id_count;
    // ui.state
    char state[16];
    float progress;                    // < 0 when null
    char task_id[65];
    // action
    char action_id[41];
    yk_act_kind_t kind;
    char slug[32];
    char *params;                      // app.open: the params object as JSON (owned)
    char name[48];
    char version[32];
    uint32_t size;                     // app.install; screensaver.set's image, screensaver.preview: 1..98304;
                                       // buddy.set's pack: 1..643072
    char sha256[65];                   // the same three
    char *url;                         // app.install, screensaver.set's image, screensaver.preview, buddy.set's pack
                                       // (owned)
    char buddy[8];                     // buddy.set: "pip", "mote", "dial", "bloom"
    char *signature;                   // app.install (owned)
    char replace[32];
    // volume.set, brightness.set: exactly one of level (0..10; -1 when not given), step (-3..3, 0 when not given)
    // and, for volume, mute (1 mute, 0 back from mute, -1 when not given). brightness "auto" reads as level -1 and
    // no step: there is no light sensor, it is answered UNSUPPORTED.
    int level;
    int step;
    int mute;
    int seconds;                       // screen_off.set: 0 (never off) or 15..3600; screensaver.preview: 3..30;
                                       // find.ring: 5..300
    char find[41];                     // find.stop: the find.ring it stops (1..40 characters)
    // screensaver.set: what changes. The image (when image_id is not "") comes in url, sha256 and size, as for
    // screensaver.preview.
    int saver_on;                      // 1 on, 0 off, -1 not given
    int saver_after, saver_duration;   // seconds (30, 60, 300; 300, 600, 1800); 0 not given
    char image_id[41];
    // onboarding.set: the steps it need not walk, bit n for step n (steps past 15 no firmware has yet are left out), and
    // the account's language to follow ("" none).
    uint16_t onb_skip;
    char onb_locale[8];
    // locale.set (0.1.19): the account's language, one of the four. colorway.set: the colour's name (as owner_colorway:
    // one this firmware does not know is still read, and answered UNSUPPORTED by it).
    char locale[8];
    char colorway[12];
    yk_proto_folder_t folder;          // folder.set (0.1.19): the app's grant as the App left it
    // inbox
    char inbox_kind[12];               // "dot", "approval", "reminder", "done"
    // error
    char code[32];
    int retry_after;                   // seconds, 0 when none
} yk_srv_msg_t;

/** Reads one text frame. Returns its type, YK_SRV_INVALID when it is not a valid server message (out is then
 *  empty). Always call yk_proto_free() afterwards. What a newer server may add is taken, not dropped (§13.3): an
 *  error code this firmware does not know (the turn still ends), a new `muted` value (read as "speech"), an
 *  action of an unknown kind (YK_ACT_UNKNOWN, to be answered UNSUPPORTED). */
yk_srv_type_t yk_proto_parse(const char *json, size_t len, yk_srv_msg_t *out);
void yk_proto_free(yk_srv_msg_t *m);
const char *yk_proto_type_name(yk_srv_type_t type);

/** practice.turn may come in two parts for one turn: the verdict, the correction and new words before the reply,
 *  then the same verdict and the hint before reply.end. Merges `more` into `into` when both are the same turn's:
 *  what `into` lacks is taken from `more`, nothing `into` has is replaced. `more` is freed either way. False when
 *  they are not two parts of one turn (`into` is left as it was). */
bool yk_proto_practice_merge(yk_srv_msg_t *into, yk_srv_msg_t *more);

// ---------------------------------------------------------------- reconnecting

/** The wait before reconnect attempt `attempt` (0 = the first after a drop): 1, 2, 4, 8, 15, then 30 s, each
 *  spread ±25 % by `rnd` (any random number) so that devices dropped together do not come back together. */
int yk_proto_backoff_ms(int attempt, uint32_t rnd);

// ---------------------------------------------------------------- audio frames

/** Header + packet into out (at least YK_PROTO_HEADER + bytes). Returns the frame length. */
size_t yk_proto_frame(uint8_t *out, int stream, int line, int seq, const uint8_t *packet, size_t bytes);
/** False for another version, an unknown stream or a frame without a packet. */
bool yk_proto_unframe(const uint8_t *frame, size_t len, int *stream, int *line, int *seq, const uint8_t **packet,
                      size_t *bytes);
