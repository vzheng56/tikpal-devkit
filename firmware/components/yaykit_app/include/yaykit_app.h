// The life of every Yay firmware (docs/design/yay-device.md 6.8–6.10, firmware guide 3–4):
//   yk_app_start()      right after NVS: the crash loop guard; records this slot's last use; the side key's
//                       double click opens the app ring (unless the app takes it, or it is the main firmware); the
//                       hint arrow's counts for this slug (yk_hint_init), and in an extension any press takes a
//                       hint away before the app hears of it (P24)
//   yk_app_on_key()     the app's side key through yaykit_app: a hold it does not use asks 「回到主屏幕说？」
//   yk_app_ready()      the screen is up: confirms the image so the bootloader keeps it; boot banner every 3 s
//                       until the first input
//   yk_app_launch()     launch parameters the main firmware left for this app (read once)
//   yk_app_set_result() a sentence and data for the main firmware, read when it starts again
//   yk_app_set_result_summary() the same with the summary Yay draws in its ring (yk_app_summary.h)
//   yk_app_on_leave()   the app's wrap-up (fade the sound, write the result), 400 ms at most, before leaving
//   yk_app_record()     a record for the platform, kept in the outbox until someone online delivers it
//   yk_app_open()       the main firmware opens another slot with parameters
//   yk_return_home()    back to Yay: slot 1 when it holds a usable main firmware, otherwise the recovery (factory)
//   yk_return_home_with() the same with a launch record for Yay ({"then":"install","slug":…} from the ring)
//   yk_app_recovery_read() the recovery's version from its description (yk_app_recovery.h); _hash() its image's sha256;
//                       _verify() whether it is whole, before Yay hands it an update
//   yk_app_settings()   (0.1.13) Yay's volume, brightness, screen-off time and language as the app should use them
//                       now, with the battery's caps (yk_app_power.h); and the battery guard of an extension, which
//                       yk_app_start() and yk_app_ready() run by themselves
// Records live in NVS namespace yk_sys (launch, result, update, update_auto, update_bad, update_err, recovery,
// used_<slot>, boot_fail, crashed), shared by all firmware.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"
#include "yk_app_power.h"
#include "yk_app_recovery.h"
#include "yk_app_summary.h"
#include "yk_key_types.h"
#include "yk_app_update.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Interface level of the shared components towards apps: only goes up, by one for each incompatible change.
 *  1: yaykit_key, yaykit_app (F1-a). 2: yaykit_cloud v2, the device API v1 (YK_CLOUD_API_LEVEL 2).
 *  3: the double click opens the app ring instead of going straight back to Yay (P23; built against level 2, an app
 *  still goes home on a double click); yk_app_on_key() and 「回到主屏幕说？」; yk_return_home_with().
 *  0.1.13 adds at level 3 (additions only, nothing changed for an app not rebuilt, and nothing Yay must tell apart): the
 *  device's settings through yaykit_app (yk_app_settings(), yk_app_brightness(), yk_app_screen_off_s(), yk_app_volume()
 *  / yk_app_set_volume(), yk_app_english(), yk_app_keep_screen_on()), and the battery guard of an extension (battery
 *  over-discharge step 2): rebuilt, an extension sleeps at once in yk_app_start() on a battery too low, and from
 *  yk_app_ready() shows 「电量用完了，请充电」 at the threshold and sleeps with the boot partition back on Yay. That is in
 *  the extension's own image: one not rebuilt reads the shared NVS itself and runs on until the brownout, as before.
 *  4 (声波码 0.1, Yay 0.1.14, kit 1.9): an app shows its own 应用码 (components/yaykit_voicecode, yk_vc_show(): its number
 *  from app.json's voiceCode at build time; devkit docs/voice-code.md); a swipe down from the top edge is the
 *  system's pull-down (yk_sheet: its 「在手机上打开」 shows the app's page code), never the app's; the app ring holds apps
 *  only. Built against 3, an app keeps what it had: Yay tells nothing apart.
 *  5 (Yay 0.1.19, kit 1.10; Yay 0.1.19 开发文档 6.4): additions only. The TF card through yaykit_tf (the app's own
 *  folder, yaykit_tf.h) and the four shared folders read only (yk_folder.h, with app.json's permissions.folders); the
 *  buddies (yk_buddy_t, yk_yay_buddy(): every yk_yay is the user's buddy unless the app names one, Pip when that pack is
 *  not in a buddy slot). An app built against 5 asks minYay 0.1.19 at least, which keeps it off an older main firmware
 *  (the store's filter and Yay's install read minYay; Yay reads no apiLevel). Built against 4, an app keeps Pip and its
 *  card as they were: Yay tells nothing apart.
 *  6 (Yay 0.1.21, kit 1.11; 0.1.20 went out as a test build only): additions only. yaykit_link's unpaired mode (yk_link_keep_start_unpaired(), 设备通道
 *  device.link, permissions.device bluetooth_unpaired: the kit check holds an app that declares it to apiLevel 6 and a
 *  5120-byte NimBLE host stack), and the vibration motor (yaykit_haptic, permission vibration) going out with the same
 *  kit: one level for both. An app built against 6 asks minYay 0.1.21 at least (the kit's own Yay). Built against 5, an app keeps what it
 *  had. With the same kit, an extension's objects get LV_EVENT_CLICKED only for a tap (F-1018: yk_ui_taps_strict),
 *  unless yk_app_config_t.loose_taps: the rule is in the app's own image, so built against 5 it keeps LVGL's. And
 *  yk_app_config_t.double_tap_ring, off unless the app turns it on: a double tap on the screen opens the app ring. */
#define YK_API_LEVEL 6

#define YK_APP_HOME_SLUG "yay"

typedef struct {
    const char *slug;     // "party-play"
    const char *version;  // "0.2.0"
    const char *banner;   // "YAYKIT_PARTY_PLAY_READY"
    bool is_home;         // the Yay main firmware: no app ring of yaykit_app's (Yay has its own). Only with the slug
                          // YK_APP_HOME_SLUG: any other app that sets it stays an extension (the ring, the pull-down)
    bool own_double;      // the app takes the double click itself and offers another way back to Yay
    bool double_tap_ring; // kit 1.11 (Yay 0.1.21; the 0.1.20 dev doc's 4.7: 0.1.20's test build had it without the
                          // hold): a double tap on the screen opens the app ring, as the side key's double click
                          // does. Off (the default): every tap is the app's, as before.
                          // A double tap is Yay's own (yay_touch.h): two taps (down and up, let go within 10 px of
                          // where the finger went down, no swipe, under 1.5 s: F-1018's rule), the second's finger
                          // down within 450 ms of the first's release and within 40 px of where the first went down,
                          // nothing pressed between. With it on, a tap's LV_EVENT_SHORT_CLICKED and LV_EVENT_CLICKED
                          // wait about a window (450 ms) before they reach the app (Yay 0.1.21): a second tap in it
                          // opens the ring and the app gets neither tap's; none, and the first's come late, inside a
                          // read of the touch screen as LVGL's own do (lv_indev_active() and lv_indev_get_point() as
                          // at the tap's release, unless a new press came first). Only those: PRESSED, RELEASED, a drag,
                          // and
                          // LVGL's SINGLE_CLICKED and DOUBLE_CLICKED still come at once. An app that wants every tap
                          // at once (a quiz buzzer, a game) leaves it off and has the double tap to itself. With the
                          // ring up, a double tap on its paper (no app there) closes it and the app gets nothing.
                          // Not while the ring, 「回到主屏幕说？」, the pull-down, the 应用码 or the folders'
                          // question is up, nor while the app's own press of the side key goes on. On wherever LVGL
                          // sees a press: on the app's screen or anything clickable on it. Only in an extension, as
                          // the ring is; independent of own_double (the key's double click stays as own_double says)
    bool quiet;           // the app prints its own banner (apps from before yaykit_app)
    bool talks;           // the app has a conversation of its own (API level 3): a hold is always its, never
                          // 「回到主屏幕说？」
    bool loose_taps;      // kit 1.11 (Yay 0.1.20, F-1018): LVGL's own CLICKED for the app's objects, any release as
                          // before. Without it an extension's objects get CLICKED only for a tap (yk_ui_taps_strict:
                          // down and up on the object, within 10 px, under 1.5 s), as Yay's pages since 0.1.18
} yk_app_config_t;

/** Call first, right after nvs_flash_init() and before the board, so a crash anywhere in start-up counts.
 *  In an app slot: after a panic, watchdog or brownout reset it counts a failed start, and on the third in a row
 *  (YK_CRASHLOOP_LIMIT) leaves instead of starting: an app goes back to Yay, the main firmware to the recovery,
 *  and `crashed` names it for whoever shows the message. Ten seconds after yk_app_ready() the count is cleared.
 *  Also: last-use record and double-click home (yaykit_key may start before or after). */
esp_err_t yk_app_start(const yk_app_config_t *cfg);

/** The app's side key through yaykit_app (API level 3), instead of yk_key_subscribe(): each event yaykit_key reports
 *  (from its task: keep it short), except the presses that open or close the app ring and 「回到主屏幕说？」. Return
 *  true when the app used it. A HOLD the app did not use, in an app without a conversation of its own (cfg.talks),
 *  asks 「回到主屏幕说？」 (device-controls.md 1.3): a tap on the screen goes back to Yay, a press stays. An app still
 *  on yk_key_subscribe() gets the ring all the same, never the question. After yk_app_start(). */
typedef bool (*yk_app_key_fn)(const yk_key_event_t *event, void *ctx);
esp_err_t yk_app_on_key(yk_app_key_fn fn, void *ctx);

/** Registers `app [status|exit|recovery|mem]` (mem: yk_mem.h) and, in development builds, `yay ...`. After esp_console_init(). */
esp_err_t yk_app_register_console(void);

/** The app started correctly (screen up): cancels the bootloader's pending rollback. Idempotent. */
esp_err_t yk_app_ready(void);

/** Something happened (input, a command): the banner stops repeating. Key events count by themselves. */
void yk_app_input(void);

typedef struct {
    bool present;
    char from[16];      // "yay"
    char intent[32];    // "practice"
    char nonce[17];
    int64_t at;         // Unix seconds when it was written
    char params[768];   // JSON object, "{}" when none
} yk_launch_t;

/** Reads and deletes this app's launch record. present = false when there is none, it is for another app, or
 *  it is older than 60 s. */
esp_err_t yk_app_launch(yk_launch_t *out);

/** Leaves a result for the main firmware. summary: one sentence for people (may be NULL); data_json: an
 *  object (may be NULL). At most 1 KB in all. */
esp_err_t yk_app_set_result(const char *summary, const char *data_json);

/** Leaves a result with the summary Yay draws when it is back (P3): data gets "summary" (yk_summary_format) next to
 *  the app's own fields in `data` (may be NULL). The sentence is what Yay says; it is not said when Yay comes back
 *  more than 30 minutes later (yk_result_fresh). Saying it spends none of the user's credits: the platform pays
 *  (2026-11-11, 总策划: what Yay says unasked is never the user's to pay), up to its limit; past it, it is not said. */
esp_err_t yk_app_set_result_summary(const char *sentence, const yk_summary_t *summary, const cJSON *data);

/** The app's wrap-up before it leaves for Yay (a double click, `app exit`, yk_return_home, yk_app_recovery,
 *  yk_app_open): fade the sound out, write the result. It runs in a task of its own and gets YK_APP_LEAVE_MS; the
 *  restart does not wait longer, so it never cuts a sound mid-way for long (P3). NULL removes it. */
#define YK_APP_LEAVE_MS 400
typedef void (*yk_app_leave_fn)(void *ctx);
void yk_app_on_leave(yk_app_leave_fn fn, void *ctx);
/** The same with more time, up to YK_APP_LEAVE_MAX_MS, for a wrap-up that waits on the network: Language Buddy closes
 *  its connection after practice.end (#580, 10-25). Going back to Yay takes that much longer when it needs it all;
 *  less is never waited for. */
#define YK_APP_LEAVE_MAX_MS 1000
void yk_app_on_leave_ms(yk_app_leave_fn fn, void *ctx, uint32_t budget_ms);

/** The record outbox (P19): a record for the platform's records (POST /records), kept in the shared NVS until
 *  someone online delivers it (Yay over its session, or the app itself); the last YK_RECORD_SLOTS, a new one
 *  replacing the oldest. kind: "breathe.session"; data_json: an object of at most YK_RECORD_DATA_MAX bytes. */
esp_err_t yk_app_record(const char *kind, const char *data_json);
/** The records in the outbox, oldest first, from any app: fn gets each one's sequence number and its JSON
 *  (yk_record_format: the records API's fields plus "app"), until it returns false. Returns how many it got. */
typedef bool (*yk_app_record_fn)(uint32_t seq, const char *json, void *ctx);
int yk_app_records_each(yk_app_record_fn fn, void *ctx);
/** Removes the records up to and including `seq` (delivered). */
esp_err_t yk_app_records_drop(uint32_t seq);

/** Main firmware: takes the result an app left (JSON, deleted after reading). ESP_ERR_NOT_FOUND when none. */
esp_err_t yk_app_take_result(char *json, size_t size);

/** Main firmware: writes the launch record for `slug`, waits for the side key to be released, boots `slot`. */
esp_err_t yk_app_open(int slot, const char *slug, const char *intent, const char *params_json);

/** Back to Yay. Does not return when it works. */
esp_err_t yk_return_home(void);

/** Back to Yay with a launch record for it (yk_launch_t: `intent`, `params_json` an object): what Yay should do once it
 *  is up, e.g. {"then":"install","slug":"language-buddy"} from the app ring (P23). With no usable Yay (the recovery
 *  instead) the record is not read and goes stale. Does not return when it works. */
esp_err_t yk_return_home_with(const char *intent, const char *params_json);

/** Restarts into the recovery (factory) and keeps it there for this boot instead of forwarding to Yay. */
esp_err_t yk_app_recovery(void);

/** While held, leaving is refused: yk_app_open, yk_return_home(_with) and yk_app_recovery return ESP_ERR_INVALID_STATE,
 *  `yk cmd app.exit` / `app.recovery` and the console's `app exit` / `app recovery` answer BUSY. yaykit_install holds
 *  it while it writes a slot over the app it held (safe-replace ③); the firmware's own `reboot` should ask too. */
void yk_app_hold_leave(bool on);
bool yk_app_leave_held(void);

/** Recovery (factory) at boot: true when it should go straight on to slot 1. Clears the recovery flag. */
bool yk_app_should_forward(void);
/** Slot 1 holds a Yay the bootloader will start (not empty, not rolled back). */
bool yk_app_home_usable(void);

/** The firmware that last left because it kept crashing (its slug), read once: the main firmware and the
 *  recovery show "<name> 上次没有正常启动". ESP_ERR_NOT_FOUND when none. */
esp_err_t yk_app_take_crashed(char *slug, size_t size);

// ---------------------------------------------------------------- the main firmware's update (yk_app_update.h)

/** Yay: the new image (`version`, `sha256`) is in the package cache; the recovery writes it after the next restart
 *  (then yk_app_recovery()). `from`: the version running now. */
esp_err_t yk_app_update_request(const char *version, const char *sha256, const char *from);
/** The update record. ESP_ERR_NOT_FOUND when there is none (one not well formed is dropped). */
esp_err_t yk_app_update_get(yk_update_t *out);
/** The recovery's steps. */
esp_err_t yk_app_update_put(const yk_update_t *u);
void yk_app_update_clear(void);
/** The new Yay keeps the update: its record goes. yaykit_app calls it once the main firmware on trial has run
 *  YK_UPDATE_CONFIRM_MS after yk_app_ready(); Yay sooner, after hello.ok. Nothing when no update is on trial. */
void yk_app_update_confirm(void);
/** The recovery refused an update before slot 1 was touched (NO_SPACE: the previous version does not fit beside the
 *  new image): the record goes, and Yay hears the error code once (yk_app_update_take_error). */
void yk_app_update_fail(const char *code);
/** Why the last update was refused, read once. ESP_ERR_NOT_FOUND when none was. */
esp_err_t yk_app_update_take_error(char *code, size_t size);
/** The same without taking it (the recovery's `update status`). */
esp_err_t yk_app_update_peek_error(char *code, size_t size);
/** yk_app_update_request(), saying whether Yay asks for it on its own at night (automatic): NVS yk_sys "update_auto"
 *  then holds the record's sha256, and the recovery keeps the screen at its lowest (yk_app_update_is_auto). The
 *  record itself stays seven fields: a recovery before 0.1.6 drops one with more. yk_app_update_request() is this
 *  with automatic = false; clear, confirm and fail drop "update_auto" too. */
esp_err_t yk_app_update_request_ex(const char *version, const char *sha256, const char *from, bool automatic);
/** The record was asked for automatically ("update_auto" holds its sha256). */
bool yk_app_update_is_auto(const yk_update_t *u);
/** The recovery went back from this version: not offered again automatically (Yay's update check asks). */
esp_err_t yk_app_update_reject(const char *version);
bool yk_app_update_rejected(const char *version);
/** Forgets the version the recovery went back from (F, docs/delivery/2026-10-26-yay-update-nf.md 3.3): after a first
 *  failure Yay tries it again the next night; after the second it stays. */
void yk_app_update_unreject(void);

// ---------------------------------------------------------------- the device's settings and battery (0.1.13, level 3)
// docs/delivery/2026-10-27-connecting-and-fast-reconnect.md 4 (共享设置) and 2026-10-27-battery-overdischarge.md 5 (第 2
// 步); the rules are pure and host tested (yk_app_power.h). An app follows Yay with these instead of reading the shared
// NVS itself (board_pref_*, yk_account_locale): they stay, and still work, but miss the battery's caps.
//
// Read from NVS in yk_app_start() and again at every look at the battery (every 10–30 s in an extension): calling the
// getters costs nothing, so an app's tick may ask each time. After yk_app_start().

/** All of them at once (yk_settings_merge): volume, the brightness the screen comes on at, the screen-off time
 *  (0 never) and whether screens are in English. */
void yk_app_settings(yk_settings_t *out);
/** The shared brightness (board_pref_brightness), at most YK_POWER_BRIGHTNESS_MAX while the battery is low. For
 *  board_brightness_set() at the start and board_display_wake() (instead of -1). */
int yk_app_brightness(void);
/** The shared screen-off time in seconds, 0 never (board_pref_screen_off_s); YK_POWER_SCREEN_OFF_LOW_S while the
 *  battery is low; 0 while the app keeps the screen on (yk_app_keep_screen_on). Ask it each time the idle is compared. */
int yk_app_screen_off_s(void);
/** The shared volume, 0..10 (board_pref_volume; yaykit_speak starts with it). */
int yk_app_volume(void);
/** The app changed the volume (its own dial): kept for every firmware, Yay too (board_pref_set_volume). NVS: from a task
 *  whose stack is in internal RAM. Sets the store only: the app sets its speaker (yk_speak_volume) itself. */
esp_err_t yk_app_set_volume(int level);
/** Screens in English: the owner's language (yaykit_cloud's, kept in NVS) is not Chinese. Also for firmware without
 *  yaykit_cloud. */
bool yk_app_english(void);
/** The owner's language as kept ("zh-Hans", "en", "ja", "es-419"; "zh" when none). */
void yk_app_locale(char *out, size_t size);
/** Something changed them while the app runs (yaykit_talk's SETTING from the phone): read again now instead of at the
 *  next look at the battery. NVS: from a task whose stack is in internal RAM. */
void yk_app_settings_reload(void);
/** The app is doing something the screen must stay on for (a breathing session, a round, a recording): while on,
 *  yk_app_screen_off_s() says 0. Off again when it is done. */
void yk_app_keep_screen_on(bool on);
/** An extension's battery is low this discharge (YK_POWER_LOW reached on battery; caps on). Always false in Yay, which
 *  has its own battery service (yay_power.h). */
bool yk_app_battery_low(void);

/** Chinese name of a built-in firmware ("聚会玩" for "party-play"); the slug itself otherwise. */
const char *yk_app_display_name(const char *slug);
/** A built-in firmware's name in Chinese or English (each app.json's), NULL when the table does not know it. */
const char *yk_app_table_name(const char *slug, bool english);
/** What to call an app on screen: `catalog_name` (the store's, in the device's language) when there is one, else the
 *  built-in name, else the slug ("" for none). The app ring, 「我的应用」 and Yay's install pages all name apps so. */
const char *yk_app_label(const char *slug, const char *catalog_name, bool english);

// ---------------------------------------------------------------- the recovery's version (yk_app_recovery.h)

/** The recovery (factory): its header and app description only (a few ms; no image read), looked up in the table of
 *  released recoveries (yk_recovery_identify). ESP_ERR_NOT_FOUND without a factory partition; an error when it holds
 *  no image. */
esp_err_t yk_app_recovery_read(yk_recovery_t *out);
/** The sha256 of the recovery's first `image_len` bytes (yk_recovery_t.size), 64 lowercase hex: the package .bin's.
 *  4 KB at a time, a 1 ms pause after each (1.6 MB: about 400 pauses), in a 4 KB internal buffer freed at the end.
 *  Blocks: call it from a low-priority task of its own, when nothing else is going on (Yay: once, when idle). */
esp_err_t yk_app_recovery_hash(uint32_t image_len, char sha256[65]);
/** The recovery is whole, before Yay hands it an update (docs/delivery/2026-10-26-yay-update-e.md 3): its `r->size`
 *  bytes read as yk_app_recovery_hash() does (4 KB, a 1 ms pause after each; never esp_image_verify's 1.6 MB in one
 *  go), the part before the SHA-256 the build appends matched against that SHA, and the whole image against
 *  `r->sha256` when it is known (the table, NVS). ESP_OK, ESP_ERR_INVALID_CRC (not whole: no SHA appended, or a
 *  mismatch), or a read error. The whole image's sha256 into `sha256` either way once read ("" when not read).
 *  Blocks as _hash() does. */
esp_err_t yk_app_recovery_verify(const yk_recovery_t *r, char sha256[65]);

/** When a slot was last opened, Unix ms; 0 when not known. */
int64_t yk_app_last_used(int slot);

/** `yk cmd` handling shared by all apps: app.exit, app.recovery, app.status. Returns ESP_ERR_NOT_SUPPORTED for
 *  other names so the app can go on with its own. */
esp_err_t yk_app_command(const char *name, const cJSON *args, cJSON *reply, const char **code);

/** Development builds (CONFIG_YAY_DEV): adds `yay <name> ...` (the main firmware adds state, action, inbox,
 *  server). Built-in: tap, double, hold <ms>, say <s> [text], records [drop <seq>], launch <slug> [json],
 *  crash [boots] (panics now and at the start of the next `boots` boots, to try the crash loop guard), record,
 *  noup [ms] (a press whose release edge is lost: yk_key_simulate_lost_up).
 *  Release builds keep only tap, double, hold, say and records (CONFIG_YAY_ACCEPT); what is registered here is not
 *  reachable there. */
typedef int (*yk_app_dev_fn)(int argc, char **argv);
esp_err_t yk_app_dev_register(const char *name, const char *help, yk_app_dev_fn fn);
/** Development builds: `yay say <s> <text>` passes the text here (the main firmware hands it to its session). */
typedef void (*yk_app_say_fn)(int seconds, const char *text);
void yk_app_dev_on_say(yk_app_say_fn fn);

#ifdef __cplusplus
}
#endif
