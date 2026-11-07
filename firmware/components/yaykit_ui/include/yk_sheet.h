// The system pull-down (docs/delivery/2026-11-02-system-pulldown.md): one status sheet for every firmware, like a
// phone's status bar: the system's, never an app's to take or restyle. A panel over the upper half (only its lower
// corners show rounded) and the rest of the screen dimmed under it; at the top the time, under it one row a state
// (「Wi-Fi」 on the left, 「Home-5G · 信号好」 on the right), and a button under them if there is one (an extension's
// 「在手机上打开」). Or, in place of the rows, one line, a line under it and a button: the main firmware's offline sheet
// (「没有连上网络」 … 「换个网络」). A hint at the bottom. The words are the
// round-screen spec's sizes since 0.1.14 (system-pulldown.md 8.2): the rows and the button 18, the line and the hint
// 16 (the main firmware's sheet had 16, 14 and 13 before).
//
// What it says is the caller's: this only draws it (a label is set again only when its words change) and reports a
// tap on the dimmed part below the panel or on the button; the panel takes the taps on itself. Swipes bubble up to
// the parent (LVGL's GESTURE events), whose they are, unless yk_sheet_take_swipes() keeps them: any way closes it
// (round-screen-gestures.md 5.2). The main firmware builds it once with its pages and shows it as their layer
// (yay_pages.c); yaykit_app opens it over an extension when a finger pulls down from the top edge (yk_app_sheet.c).
// The words of the rows every firmware has are here too (yk_sheet_*_text, pure: yk_sheet_logic.c, host tested).
// Call under board_lock().
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YK_SHEET_ROWS 4

typedef struct {
    const char *name;   // 「Wi-Fi」; NULL: no row (the rows under it move up)
    const char *value;  // one line, cut with … at a character
    bool problem;       // the value in coral: it is wrong (not added, not connected); ink otherwise
} yk_sheet_row_t;

typedef struct {
    const char *clock;                  // 「09:41」; NULL or "": nothing there
    yk_sheet_row_t row[YK_SHEET_ROWS];  // shown while `title` is NULL
    const char *title;                  // the line in place of the rows (「没有连上网络」); NULL: the rows
    bool title_calm;                    // ... in ink, not coral: nothing is wrong (「正在连网络」)
    const char *line;                   // ... the line under it
    const char *button;                 // the button's words, under the rows or the line; NULL: no button
    const char *hint;                   // the bottom line (「上滑收起」); NULL: none
} yk_sheet_view_t;

typedef struct yk_sheet yk_sheet_t;

/** Builds it on `parent`, hidden, covering it from (0, 0) (466 x 466: the screen). A tap on the dimmed part sends
 *  `on_dim` LV_EVENT_CLICKED with `dim_ctx` as the event's user data, one on the button `on_button` with `button_ctx`
 *  (either NULL: nothing). Deleted with its parent, or by yk_sheet_delete(). NULL without memory. */
yk_sheet_t *yk_sheet_create(lv_obj_t *parent, lv_event_cb_t on_dim, void *dim_ctx, lv_event_cb_t on_button,
                            void *button_ctx);
/** Its layer, to show, hide or move (lv_obj_set_hidden). */
lv_obj_t *yk_sheet_obj(const yk_sheet_t *sheet);
/** Says `view`; only what changed is drawn again. The strings are copied (LVGL's labels). */
void yk_sheet_fill(yk_sheet_t *sheet, const yk_sheet_view_t *view);
/** Keeps the swipes on it: they no longer bubble up to the parent (nor scroll it), and each goes to `on_swipe`
 *  (LV_EVENT_GESTURE, `ctx` its user data; lv_indev_get_gesture_dir() says which way). For a parent that is an app's
 *  screen, whose own swipes are its own (yaykit_app). */
void yk_sheet_take_swipes(yk_sheet_t *sheet, lv_event_cb_t on_swipe, void *ctx);
/** Deletes its objects and itself (by the next LVGL tick: safe from its own events). */
void yk_sheet_delete(yk_sheet_t *sheet);

// ---------------------------------------------------------------- the rows' words (pure, host tested)

/** The battery as board_battery() reads it (yaykit_board), for its row (the main firmware's words since 0.1.14,
 *  docs/delivery/2026-10-31-battery-display.md 4.4). */
typedef struct {
    int percent;    // the number to show (board_battery_t.percent, smoothed); -1 not read
    bool usb;       // plugged in
    bool charging;  // the chip charging
    bool full;      // BOARD_BATTERY_FULL
    bool stopped;   // BOARD_BATTERY_STOPPED: plugged in, not charging, not full
    bool low;       // charging below 3.3 V: words, not a number
    bool fault;     // plugged in 30 minutes, still below 3.3 V
} yk_sheet_battery_t;

/** 「76%」, 「76% · 充电中」, 「很低，正在充电」, 「电池可能有问题」, 「还读不到」…; `named`: with 「电量 」 in front (a line of its own,
 *  not a row). Into `buf`, which it returns. */
const char *yk_sheet_battery_text(const yk_sheet_battery_t *b, bool named, bool english, char *buf, size_t size);
/** The Wi-Fi row (system-pulldown.md 2.3, 8.1). `wifi`: yk_wifi_state_t as an int (0 off, 1 not set up, 2 connecting,
 *  3 connected, 4 failed); `reach`: yk_cloud_reach_t as an int (0 not known, 1 through, 2 failed), -1 when the app has
 *  no cloud. Connected: 「Home-5G · 信号好」 (-60 dBm and up; 一般 from -72; 弱 below), or 「连不上服务器」 when the cloud's
 *  last connection failed; 「正在连接」 while it connects (never 没网); 「没连上」 not set up or failed; 「不用时关着」 off (the
 *  app has not started it this boot: the recording card only when something waits to upload). `*problem` (may be
 *  NULL): the coral ones. Into `buf`, which it returns. */
const char *yk_sheet_wifi_text(int wifi, const char *ssid, int rssi, int reach, bool english, bool *problem, char *buf,
                               size_t size);
/** The clock at the top in an extension (system-pulldown.md 8.1, BOSS-2 11-02): 「09:41」 for `utc_s` (Unix seconds)
 *  `offset_min` east of UTC (the shared settings' tz_off, Yay's), worked out here: the firmware's own zone is never
 *  set. Into `buf`, which it returns. */
const char *yk_sheet_clock_text(int64_t utc_s, int offset_min, char *buf, size_t size);
/** The phone row in an extension (system-pulldown.md 8.1: whether the device is added, and to whose phone; no
 *  「正在连手机」, Bluetooth is off there). `account`: yk_account_state_t as an int (0 not added, 1 being added, 2 added,
 *  3 failed). 「已添加 · 昵称的手机」, 「已添加」, 「正在添加」, 「还没添加」 (coral). Into `buf`, which it returns. */
const char *yk_sheet_phone_text(int account, const char *nickname, bool english, bool *problem, char *buf, size_t size);

#ifdef __cplusplus
}
#endif
