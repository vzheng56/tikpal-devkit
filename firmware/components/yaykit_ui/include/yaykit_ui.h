#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "yk_theme.h"
// v2 (extension platform P11): the round-screen spec's pieces, each in its own header.
#include "yk_caption.h"     // subtitles laid out by the circle
#include "yk_notice.h"      // the same notices everywhere
#include "yk_rim.h"         // the rim, eight boxes
#include "yk_round.h"       // the spec as constants: colours, sizes, geometry
#include "yk_transition.h"  // the splash, into and out of an app
#include "yk_yay.h"         // Yay, alive
// P23/P24 (device-controls.md): the double click's app ring, the catalog it draws from, the arrow to the side key.
#include "yk_apps.h"        // the app ring
#include "yk_catalog.h"     // the app catalog cache (names, icons, sizes)
#include "yk_hint.h"        // the hint arrow and when it shows
// The system pull-down (docs/delivery/2026-11-02-system-pulldown.md): the status sheet every firmware shares.
#include "yk_sheet.h"       // the status sheet

#ifdef __cplusplus
extern "C" {
#endif

/** Centered CJK label (fonts from the shared `fonts` partition). */
lv_obj_t *yk_ui_label(lv_obj_t *parent, const char *text, int x, int y, int width, int size, uint32_t color);
/** Pill button with a centered label. `cb` gets LV_EVENT_CLICKED only for a tap (yk_ui_tap_only). */
lv_obj_t *yk_ui_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h, lv_event_cb_t cb,
                       void *user_data, uint32_t color);
/** From now on `o`'s LV_EVENT_CLICKED reaches its callbacks only for a tap (Yay 0.1.18, the dev doc's 4.8). The rule,
 *  the same as the rings' and 「我的应用」's (yk_tap in yk_apps_logic.h: YK_TAP_SLOP, YK_TAP_HOLD_MS):
 *   - the finger went down on `o` and let go on `o` itself: not on something clickable on top of it, nor off it;
 *   - it never went more than 10 px either way from where it went down (a slow drag is no tap, and no swipe either);
 *   - it was held under 1.5 s (a slow press taps; a thumb resting on the edge does not);
 *   - a fast swipe is LVGL's LV_EVENT_GESTURE, as before: never a tap (it moves past 10 px).
 *  Down on it and let go off it, down elsewhere and let go on it, a slow drag, a swipe, a long hold: no CLICKED. (LVGL
 *  keeps the press with what it went down on, and sends CLICKED wherever the finger lets go.) A tap anywhere on a
 *  full-screen layer: yk_ui_tap_only() on that layer (first setup's does; Yay's home and install confirmation have it on
 *  the screen, yay_run.c).
 *  yk_ui_button() and the system pull-down already do this. Once per object is enough; more does no harm. Yay's own
 *  pages only, this version: not in the kit's documents and not for apps yet (their buttons get the rule from
 *  yk_ui_button(); opening this is the kit 1.10's to decide).
 *  The one exception (Yay 0.1.19, Find my Tikpal, the dev doc's 4.8): while the device rings to be found, the finger going
 *  down anywhere on the screen stops it, at once, as an alarm clock does, and so does the side key going down (no wait for
 *  a double click). That press is the ringing layer's alone: nothing under it gets it, nor its release, nor a click;
 *  the key's press is not a tap (the screen off, back) nor half of a double. Stopping is safe, so it is the quicker the
 *  better. Yay's yay_find_view.c (LV_EVENT_PRESSED on a layer over everything) and yay_find.c (the key). */
void yk_ui_tap_only(lv_obj_t *o);
/** The last tap yk_ui_tap_only() let through (Yay 0.1.20, a double tap on Yay's screen: yay_touch.h): where the
 *  finger went down, when it went down and let go (lv_tick_get()'s ms), and the number of its press. Every press on
 *  an object that has yk_ui_tap_only() counts one, a tap or not (a swipe, a drag, a long hold), so two taps whose
 *  numbers follow each other had nothing pressed between them. Read it in the object's own CLICKED callback, which
 *  comes only for a tap. False before the first tap. */
typedef struct {
    int16_t x, y;
    uint32_t down_ms, up_ms;
    uint32_t press;
} yk_ui_tap_t;
bool yk_ui_last_tap(yk_ui_tap_t *out);
/** Two taps in a row, for the log (Yay 0.1.22: the double tap's window is to be set from real taps, 项目总策划 11-18):
 *  "YAY_TAP_PAIR where=<where> gap_ms=<the second down after the first let go> dist_px=<between where they went down>
 *  hold1_ms= hold2_ms=<how long each was held> double=<0|1> why=<double | between (a press between them) | slow
 *  (gap over window_ms) | far (over px) | `busy` (the caller's: something of the system's up) | third (the first was
 *  a double's second)>". 0, nothing written, when the second came YK_UI_TAP_PAIR_MS or more after the first (no pair).
 *  Pure: host tested. */
#define YK_UI_TAP_PAIR_MS 1000
int yk_ui_tap_pair_line(char *out, size_t size, const char *where, const yk_ui_tap_t *first, const yk_ui_tap_t *second,
                        bool twice, const char *busy, int window_ms, int px);
/** The same rule for every object on the screen (0.1.20, F-1018; yaykit_app turns it on in an extension built with
 *  kit 1.11, unless yk_app_config_t.loose_taps): `indev`'s LV_EVENT_CLICKED and LV_EVENT_SHORT_CLICKED reach an
 *  object only for a tap (yk_tap_strict: down and up on it, the finger let go within 10 px of where it went down and
 *  never jumping further, no swipe, held under 1.5 s). LVGL's own CLICKED comes for any release, after a drag too: a
 *  finger dragged across a button and lifted pressed it. Not for what judges its own taps (yk_ui_own_taps: the app
 *  ring, its dial, 「我的应用」) nor while a scroll goes (LVGL sends no CLICKED then anyway). Once; in the LVGL task. */
void yk_ui_taps_strict(lv_indev_t *indev);
/** `root` and everything in it judge their own taps (yk_tap on their CLICKED, which also settles a drag): left out by
 *  yk_ui_taps_strict. Up to 8 at a time; each goes from the list with its object. */
void yk_ui_own_taps(lv_obj_t *root);
/** Every press on `indev`'s screen, whatever it went down on (kit 1.11: yaykit_app's double tap in an extension,
 *  yk_app_config_t.double_tap_ring). `fn` hears of each: YK_UI_PRESS_DOWN at the read that saw the finger come down,
 *  before LVGL processes it (x, y, down_ms = up_ms, its number); at the release, before anything gets that release,
 *  YK_UI_PRESS_TAP for a tap, judged as yk_ui_taps_strict judges one (let go within 10 px of where it went down, no
 *  swipe, held under 1.5 s) but on any object, or YK_UI_PRESS_OTHER for a press that was none. Times are
 *  lv_tick_get()'s ms. Every press counts one, a tap or not, on something clickable or not, so two taps whose numbers
 *  follow each other had nothing pressed between them. For a tap, `fn`'s answer says what becomes of that press's
 *  SHORT_CLICKED and CLICKED (only those: LVGL's SINGLE/DOUBLE_CLICKED still reach the object at once, and its PRESSED
 *  and RELEASED always do); for the others it is not read. A press on nothing clickable has no release for `fn`. One
 *  `fn` (a second call replaces it; NULL stops); in the LVGL task. The device's read callback is wrapped (set no other
 *  after this). yk_ui_finger's finger too. */
typedef enum {
    YK_UI_TAP_GIVE,  // the object's, at once, as any tap
    YK_UI_TAP_TAKE,  // nobody's: they reach no object
    YK_UI_TAP_HOLD,  // held back (Yay 0.1.21): they reach the object after yk_ui_screen_tap_give(), late, or never at
                     // yk_ui_screen_tap_drop(). One at a time: a tap held while another is gives that one first. A held
                     // tap yk_ui_taps_strict would have kept from its object stays kept; an object gone meanwhile, or
                     // disabled, gets nothing.
} yk_ui_tap_do_t;
typedef enum { YK_UI_PRESS_DOWN, YK_UI_PRESS_TAP, YK_UI_PRESS_OTHER } yk_ui_press_t;
typedef yk_ui_tap_do_t (*yk_ui_screen_tap_fn)(yk_ui_press_t what, const yk_ui_tap_t *tap, void *ctx);
void yk_ui_screen_taps(lv_indev_t *indev, yk_ui_screen_tap_fn fn, void *ctx);
/** The held tap's SHORT_CLICKED (when it had one: a tap held past LVGL's long-press time has none) and CLICKED go to
 *  the object it went down on at its input device's next read, before LVGL processes what that read saw: inside it,
 *  lv_indev_active() is that device, and its point the tap's own release when no press came since (a new press asked
 *  for from YK_UI_PRESS_DOWN: before that press reaches anything). The event's parameter is the device
 *  (lv_event_get_indev()). With no read in 100 ms (the screen asleep), from a timer: lv_indev_active() is NULL then.
 *  Nothing when none is held. In the LVGL task. */
void yk_ui_screen_tap_give(void);
/** The held tap's clicks go to nobody. Nothing when none is held. In the LVGL task. */
void yk_ui_screen_tap_drop(void);
/**
 * Draws the pairing content on `screen` below a title: QR code, 8-digit code and status
 * while BLE is on, or a button to turn it on. Caller owns the title and back button.
 * Only this function needs Yaykit Link's Bluetooth (yk_link_init with BLE) and CONFIG_LV_USE_QRCODE (without it: the
 * code alone); firmware that never calls it links neither.
 */
void yk_ui_pair_content(lv_obj_t *screen, bool starting, lv_event_cb_t on_turn_on, lv_event_cb_t on_turn_off);
/** English text in the shared pages (default Chinese). Call with the board lock held, before drawing. */
void yk_ui_set_english(bool on);
/** The buddy's name, who talks with you: Pip, Mote, Dial or Bloom, the same in every language (0.1.12; Pip since
 *  0.1.14). From the device's shared setting (yaykit_board's board_pref_buddy(), YK_PREF_BUDDY), read each call; Pip
 *  while the main firmware has not set another (no version does yet) and for a value this build does not know. Only
 *  an addition: YK_API_LEVEL stays, firmware built before keeps its Yay. Needs NVS initialised. */
const char *yk_ui_buddy_name(void);
/** Registers `snap`: prints the real LVGL frame as base64 RGB565 between SNAP_BEGIN/SNAP_END. */
void yk_ui_register_snapshot(void);
/** True while `snap` is streaming, so periodic banners can stay quiet. */
bool yk_ui_capturing(void);
/** Development (`yay finger`): a finger LVGL reads like the touch screen's, through its own input device: down at
 *  (x0, y0), along to (x1, y1) in `ms`, let go there. The same point is a tap; 40 px or more in 150 ms is a swipe to
 *  LVGL (gestures, then the release's CLICKED, as with a real finger). False while one is still moving. Under
 *  board_lock(). */
bool yk_ui_finger(int x0, int y0, int x1, int y1, int ms);

// ---------------------------------------------------------------- v2 helpers (P11)

/** A short label (at most 8 characters) along the circle, centred at `angle_deg` (clockwise from the top) with its
 *  baseline on `radius`, reading clockwise (or counter-clockwise when `ccw`, for the bottom half to read upright).
 *  One small label per character: for short labels only (spec 2). */
lv_obj_t *yk_ui_arc_label(lv_obj_t *parent, const char *text, int size, uint32_t color, float angle_deg, int radius,
                          bool ccw);

/** A full-screen container whose content turns about the screen's centre (Translator's far half, Party Play's
 *  results facing a player). Its children are laid out unturned in 466 x 466; yk_ui_turn() turns them (ms 0: at
 *  once; 300 is the spec's). Turning redraws the box's content area: keep what is inside small. */
lv_obj_t *yk_ui_turn_box(lv_obj_t *parent);
void yk_ui_turn(lv_obj_t *box, float deg, int ms);

/** The pull-down status sheet (spec 6): what the app shows of the device. -1 / NULL hides an item (Party Play
 *  shows only the battery and the volume; nothing about the network in an offline app). */
typedef struct {
    const char *clock;  // "12:30"
    int wifi;           // -1 hidden, 0 off, 1 connecting, 2 connected
    int battery;        // percent, -1 hidden
    bool charging;
    int volume;         // 0..10, -1 hidden
    int credits;        // today's points left, -1 hidden
} yk_status_t;
/** Creates the sheet in `parent` (hidden). */
lv_obj_t *yk_ui_status_sheet(lv_obj_t *parent);
/** Fills it and slides it down (show) or up (hide) over YK_PAGE_MS. */
void yk_ui_status_show(lv_obj_t *sheet, const yk_status_t *st, bool show);

#ifdef __cplusplus
}
#endif
