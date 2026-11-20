// Pure C parts of the app ring, the app catalog and the hint arrow (host tested with test/run.sh, LVGL stubbed):
// where everything on the ring goes and how it moves, what a touch hits, the catalog's bytes on flash, icons to
// RGB565, the hint's geometry and when it may show.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "yk_apps.h"
#include "yk_catalog.h"
#include "yk_hint.h"
#include "yk_rings.h"
#include "yk_ui_logic.h"

// ---------------------------------------------------------------- the ring's geometry (device-controls.md 4)

// The sizes it shares with the settings ring are yk_rings.h's (0.1.18, dev doc 4.7). The marks keep the sizes they were
// made with for a disc of YK_MARK_DISC_R (the ring's until 0.1.17): 「我的应用」 scales them to its discs by k = r /
// YK_MARK_DISC_R; the rings keep them as they are, on their bigger disc's edge (yk_rings_mark()).
#define YK_MARK_DISC_R     38   // the disc the marks are made for (76 across)
#define YK_APPS_RING_W     YK_RINGS_IN_USE_W  // the sage ring of the app in use
#define YK_APPS_BADGE_D    27   // the download mark's centre, right and down from the disc's (a YK_MARK_DISC_R disc's)
#define YK_APPS_BADGE_R    12
#define YK_APPS_HIT_R      YK_RINGS_HIT_R      // an app's touch target
#define YK_APPS_CENTER_R   YK_RINGS_CENTER_R   // the middle's disc
#define YK_APPS_CENTER_HIT_R YK_RINGS_CENTER_HIT_R
#define YK_APPS_DIM_OPA    107  // 42 % of 255: an app not installed
#define YK_APPS_CACHE_D    29   // the cached mark's centre, right and down from the disc's (a YK_MARK_DISC_R disc's)
#define YK_APPS_CACHE_R    7    // ... its outer radius, a hollow ring ...
#define YK_APPS_CACHE_W    3    // ... this wide, filled with paper
#define YK_APPS_UPDATE_D   27   // the update dot's centre (`update`, 0.1.12), right and up from the disc's: on a
#define YK_APPS_UPDATE_R   6    // ... YK_MARK_DISC_R disc's edge at 45 degrees, coral, 12 across (the settings tile's
#define YK_APPS_UPDATE_EDGE 2   // ... dot), in a paper ring this wide, apart from the disc
#define YK_APPS_HOME_DOT_D 53   // the middle's update dot (0.1.14): on its disc's edge (r 75) at 45 degrees, upper right
#define YK_APPS_PHONE_D    27   // the phone mark's centre (`phone`, voice-code handoff 2.1②), right and up from the
#define YK_APPS_PHONE_R    14   // ... disc's, where the update dot would be (an extension's ring has none); sage, 28
#define YK_APPS_PHONE_EDGE 2    // ... across, in a paper ring this wide (apart from the in-use ring under it)
#define YK_APPS_MORE_PLACE 4    // 「更多」 stands at the fifth place, upper left (288 degrees)
#define YK_APPS_DOT_R      7    // 「更多」's three dots ...
#define YK_APPS_DOT_GAP    20   // ... this far apart
// The ring with 「设置」 (settings-u.md 5, BOSS-2 10-25; nothing gives it since 0.1.14): 6 places every 60 degrees from
// 12 o'clock, on the same circle since 0.1.18 (it was r 128, for the name under the bottom one). Its places are numbered
// YK_APPS_SIX + 0..5, apart from the 5-place ring's 0..4.
#define YK_APPS_SIX        8
#define YK_APPS_MORE_PLACE6     (YK_APPS_SIX + 4)  // 「更多」: lower left (240 degrees)
#define YK_APPS_SETTINGS_PLACE  (YK_APPS_SIX + 5)  // 「设置」: upper left (300 degrees)
#define YK_APPS_GEAR_UNIT  2.0f // 「设置」's gear: the canvas's 24-unit icon at 48 px

typedef struct {
    int16_t x, y, w, h;
} yk_box_t;

typedef struct {
    int slot;           // 0..4, clockwise from 12 o'clock, every 72 degrees; YK_APPS_SIX + 0..5 every 60
    int16_t cx, cy;     // the disc's centre, screen coordinates
    bool phone;         // it carries the phone mark (the item's `phone`; set after the layout): a touch on it is its
} yk_apps_sat_t;

typedef struct {
    int n;
    yk_apps_sat_t sat[YK_APPS_MAX];
} yk_apps_layout_t;

/** Which of the 5 places n apps take: all for 5; fewer stand symmetrically (1: the top; 2: upper right then upper
 *  left; 3: top, lower right, lower left; 4: all but the top). */
void yk_apps_slots(int n, int slots[YK_APPS_MAX]);
/** The places of these items: with 「设置」 last (YK_APPS_SETTINGS), the 6-place ring: the apps from the top clockwise,
 *  「更多」 (before it) at YK_APPS_MORE_PLACE6, 「设置」 at YK_APPS_SETTINGS_PLACE; with 「更多」 last (YK_APPS_MORE), the
 *  apps from the top clockwise and 「更多」 at YK_APPS_MORE_PLACE; otherwise yk_apps_slots(n). */
void yk_apps_item_slots(const yk_apps_item_t *items, int n, int slots[YK_APPS_MAX]);
/** Lays out n apps at yk_apps_slots(n): every disc on the rings' circle (YK_RINGS_R). */
void yk_apps_layout(int n, yk_apps_layout_t *out);
/** The same at the places given (yk_apps_item_slots()). */
void yk_apps_layout_at(int n, const int slots[], yk_apps_layout_t *out);
/** The update dot (`update`, 0.1.12) of an app's disc centred at (cx, cy) drawn at scale k (1: a YK_MARK_DISC_R disc;
 *  「我的应用」 scales it with its disc): its centre, its coral radius and its paper ring's outer one. The rings put it
 *  on their own disc: yk_rings_mark(). */
void yk_apps_update_dot(int cx, int cy, float k, int *x, int *y, float *r, float *edge_r);
/** The middle's update dot (yk_apps_home_update, 0.1.14) when the middle is centred at (cx, cy) at scale k: the apps'
 *  dot (the same size) on the middle disc's edge, upper right. */
void yk_apps_home_dot(int cx, int cy, float k, int *x, int *y, float *r, float *edge_r);
/** The phone mark (`phone`, voice-code handoff 2.1②) of a YK_MARK_DISC_R disc centred at (cx, cy) at scale k: its
 *  centre, its sage radius and its paper ring's outer one. */
void yk_apps_phone_mark(int cx, int cy, float k, int *x, int *y, float *r, float *edge_r);

// ---------------------------------------------------------------- both rings (yk_rings.h)

typedef enum {
    YK_MARK_UPDATE,    // a newer version: a coral dot in a paper ring, upper right
    YK_MARK_PHONE,     // its 应用码: a sage dot in a paper ring with a paper phone, upper right
    YK_MARK_CACHED,    // in the package cache: a hollow sage ring, lower right
    YK_MARK_DOWNLOAD,  // to download: a sage dot with a paper arrow, lower right
} yk_mark_kind_t;

/** A mark of a ring's place whose disc (YK_RINGS_SAT_R) is centred at (cx, cy), at scale k (the ring's motion; a press
 *  leaves the marks where they are): its centre, where a YK_MARK_DISC_R disc has it moved out to the bigger disc's edge,
 *  and its outer radius, the size it was made with (times k). */
void yk_rings_mark(yk_mark_kind_t kind, int cx, int cy, float k, int *x, int *y, float *outer);

/** The middle's words: `n` lines (at most 3) line_h[i] px tall, stacked about the centre, into out[0..n-1]: each
 *  YK_RINGS_WORDS_W wide, centred. */
void yk_rings_words(int n, const int line_h[], yk_box_t out[]);
/** A pressed place's name for the middle (glyph widths from `width`, at YK_RINGS_NAME_PX): `line1` alone when it fits
 *  max_w; else two lines, broken after the last space that lets the first fit (a word too long for a line, or Han:
 *  where the line is full), the second cut with "…". Returns how many lines (0 for ""). */
int yk_rings_name_lines(const char *name, yk_glyph_width_fn width, void *ctx, int max_w, char *line1, size_t size1,
                        char *line2, size_t size2);

// ---------------------------------------------------------------- motion

#define YK_APPS_OPEN_MS      300   // the last app in place
#define YK_APPS_CENTER_IN_MS 240   // the middle grows from 0.6
#define YK_APPS_STAGGER_MS   28    // each app leaves the centre this much after the one before
#define YK_APPS_FLY_MS       (YK_APPS_OPEN_MS - (YK_APPS_MAX - 1) * YK_APPS_STAGGER_MS)  // 188
#define YK_APPS_CLOSE_MS     180
#define YK_APPS_IDLE_MS      8000  // untouched this long: it closes by itself

typedef struct {
    int16_t cx, cy;   // where the element's centre is now
    uint16_t scale;   // 256 = full size
    uint8_t opa;
} yk_apps_pose_t;

/** The ring t_ms after it started opening (closing: after it started closing). pose[0] is the middle, pose[1..n]
 *  the apps. Opening: the middle from 0.6 and clear, the apps from the centre at 0.2 and clear, eased out; closing:
 *  all back towards the centre (the middle to 0.8), eased in. Returns true once the motion is over. */
bool yk_apps_frame(const yk_apps_layout_t *L, int t_ms, bool closing, yk_apps_pose_t pose[YK_APPS_MAX + 1]);

// A swipe closes the ring towards the finger (round-screen-gestures.md 5.2, 7): everything slides that way and fades.
#define YK_APPS_AWAY_MS 200
#define YK_APPS_AWAY_PX 160

/** The ring t_ms after a swipe (dx, dy: the finger's way, one of them +-1, the other 0) began closing it: every element
 *  from its place YK_APPS_AWAY_PX that way, full size, clear by the end, eased in. Returns true once it is over. */
bool yk_apps_frame_away(const yk_apps_layout_t *L, int t_ms, int dx, int dy, yk_apps_pose_t pose[YK_APPS_MAX + 1]);

/** One frame of a press on a ring at rest (both rings, yk_rings.h). pose[0] the middle and pose[1..n] the places come in
 *  as the end of its opening left them (full size, in place, clear) and go out as they look this frame, `pressed`
 *  (yk_rings_pressed(): 1..n a place) having been down t_ms: that place grown from YK_RINGS_SAT_R towards
 *  YK_RINGS_PRESS_R, eased out over YK_RINGS_PRESS_MS. `dim` (in and out, bit i-1 for place i) is which places look
 *  dimmed (YK_RINGS_DIM_OPA): towards every other place while a place is pressed, none otherwise (0 the middle, -1), at
 *  most one changing a frame, clockwise from the pressed one (or the one that was), so that no frame repaints more than
 *  the pressed place and one other (a frame that repainted all five took about 60 ms on the device, 0.1.18). True once
 *  nothing moves. */
bool yk_rings_press(int n, int pressed, int t_ms, uint32_t *dim, yk_apps_pose_t pose[]);

/** What a touch at (x, y) hits: 0 the middle, 1..n an app (the nearest whose target or phone mark it is on), -1 the
 *  paper. */
int yk_apps_hit(const yk_apps_layout_t *L, int x, int y);

// ---------------------------------------------------------------- a tap (0.1.18, round-screen-gestures.md 7)

// A tap is a finger that goes down on something and lets go on the same thing (Yay 0.1.18 dev doc 4.8): what it went
// down on and what it let go on are the caller's numbers (a place on a ring, a button: 0, off it: -1; equal is all
// that counts). Down on one thing and let go on another is no tap, either way round; nor is a finger that went more
// than YK_TAP_SLOP from where it went down (a slow drag: no swipe, and no tap either), nor one held YK_TAP_HOLD_MS or
// more. LVGL's CLICKED knows none of this: it comes for whatever was pressed, wherever the finger lets go.
#define YK_TAP_SLOP    10   // px each way: LVGL's scroll limit, LV_INDEV_DEF_SCROLL_LIMIT (yaykit_ui.c asserts they are
                            // the same): 「我的应用」's drag starts there, and so does Yay's whole-screen tap (#1713)
#define YK_TAP_HOLD_MS 1500 // held this long it is no tap: nothing on the device is a long press, so a slow press by a
                            // child or an old hand still taps; a thumb resting on the edge while it holds the device
                            // does not (the firmware lead, 11-11; 600 before, as Party Play's own taps)

typedef enum {
    YK_TAP_YES,      // a tap
    YK_TAP_OTHER,    // let go on something else than it went down on
    YK_TAP_MOVED,    // went more than YK_TAP_SLOP from where it went down (a slow drag)
    YK_TAP_HELD,     // held YK_TAP_HOLD_MS or more
    YK_TAP_SWIPED,   // a swipe, a press lost (yk_tap_cancel)
} yk_tap_why_t;

typedef struct {
    bool down;       // a press is under way
    bool off;        // it can no longer be a tap: it went too far, or swiped
    uint8_t why;     // yk_tap_why_t: why not, once it is not (YK_TAP_YES while it still may be)
    int hit;         // what it went down on
    int up_hit;      // ... and let go on (yk_tap_release)
    int16_t x0, y0;  // where
    uint32_t t0;     // when (ms)
} yk_tap_t;

/** The finger went down at (x, y) on `hit`, at now_ms. */
void yk_tap_press(yk_tap_t *t, int hit, int x, int y, uint32_t now_ms);
/** It is at (x, y) now: false once it has gone more than YK_TAP_SLOP from where it went down (and from then on). */
bool yk_tap_move(yk_tap_t *t, int x, int y);
/** No tap any more this press: a swipe, a press lost. */
void yk_tap_cancel(yk_tap_t *t);
/** It let go at (x, y) on `hit`, at now_ms: true when that was a tap, on what it went down on. The press is over; `why`
 *  says why it was none. */
bool yk_tap_release(yk_tap_t *t, int hit, int x, int y, uint32_t now_ms);
/** A release as the input device sees it (yk_ui_taps_strict, 0.1.20 F-1018: an app's own objects): `same`: the press
 *  went down on the object the click is for; `over`: the finger lets go over it (or something inside it); `swiped`: a
 *  swipe was recognised, or two readings jumped more than YK_TAP_SLOP apart (lv_indev_get_press_moved). The press is
 *  over. True when it was a tap: no further than YK_TAP_SLOP from where it went down, held under YK_TAP_HOLD_MS. */
bool yk_tap_strict(yk_tap_t *t, bool same, bool over, bool swiped, int x, int y, uint32_t now_ms);
/** The word for a yk_tap_why_t in the YK_TAP log line: tap, other, moved, held, swiped. */
const char *yk_tap_why_name(int why);
/** The log line every release on the rings, 「我的应用」 and yk_ui_tap_only's objects prints (for the acceptance's
 *  scripts: `yay finger` plays the finger, this says what came of it): "YK_TAP <where> <why> down=<hit> up=<hit>".
 *  Into out; returns it. */
const char *yk_tap_line(const yk_tap_t *t, const char *where, char *out, size_t size);

// ---------------------------------------------------------------- touches (round-screen-gestures.md 7)

/** One press on the ring, from LVGL's events. A swipe closes it towards the finger and is never a tap: LVGL still
 *  sends CLICKED when a swipe lets go (the ring's paper does not scroll), and wherever the finger lands then, on an
 *  app or on the app in use, nothing is opened, installed or shown. A tap is yk_tap's: down and up on the same app
 *  (or the middle, or the paper), the finger still, not held. */
typedef struct {
    bool swiped;  // this press has swiped
    bool closing; // the ring is going: nothing more counts
    yk_tap_t tap;
} yk_apps_touch_t;

typedef enum {
    YK_APPS_GO_NONE,   // nothing
    YK_APPS_GO_TAP,    // close folding back, reporting `hit`
    YK_APPS_GO_AWAY,   // close towards (dx, dy), reporting nothing
} yk_apps_go_kind_t;

typedef struct {
    yk_apps_go_kind_t kind;
    int hit;         // GO_TAP: yk_apps_hit()'s
    int8_t dx, dy;   // GO_AWAY: the finger's way
} yk_apps_go_t;

/** LV_EVENT_PRESSED at (x, y) on `hit` (yk_apps_hit(), yk_ring_hit()), at now_ms: a new press. */
void yk_apps_touch_press(yk_apps_touch_t *t, int hit, int x, int y, uint32_t now_ms);
/** LV_EVENT_PRESSING at (x, y). */
void yk_apps_touch_move(yk_apps_touch_t *t, int x, int y);
/** LV_EVENT_GESTURE, the finger's way (dx, dy; one of them +-1): the ring goes that way. */
yk_apps_go_t yk_apps_touch_swipe(yk_apps_touch_t *t, int dx, int dy);
/** LV_EVENT_CLICKED at (x, y) on `hit`, at now_ms: a tap on it when the press was one (yk_tap) on the same and did not
 *  swipe; otherwise nothing. */
yk_apps_go_t yk_apps_touch_up(yk_apps_touch_t *t, int hit, int x, int y, uint32_t now_ms);
/** The same on the app ring: `hit` is yk_apps_hit()'s at (x, y). */
yk_apps_go_t yk_apps_touch_click(yk_apps_touch_t *t, const yk_apps_layout_t *L, int x, int y, uint32_t now_ms);
/** The ring began closing some other way (8 s, yk_apps_tap(), yk_apps_close()): touches no longer count. */
void yk_apps_touch_closing(yk_apps_touch_t *t);
/** yk_apps_touch_click() said TAP and the guard kept it (yk_apps_set_guard): the ring stays, and takes the next finger. */
void yk_apps_touch_stays(yk_apps_touch_t *t);
/** What the press shows now, at now_ms (yk_rings.h): what it went down on (1..n a place, 0 the middle) while it may
 *  still be a tap there (yk_tap: not moved off, not held YK_TAP_HOLD_MS, not swiped, the ring not going); -1 otherwise,
 *  and for the paper. */
int yk_rings_pressed(const yk_apps_touch_t *t, uint32_t now_ms);

/** The ask card's line: 「回到主屏幕说？」 / "Talk on the home screen?" (kit 1.10), in yk_words_pick(english). No buddy's
 *  name: the user's may be any of four (开发包四个伙伴需求 4), and the home screen is where it talks. */
const char *yk_apps_ask_text(bool english);

/** `text` cut to max_w px (glyph widths from `width`), ending in "…" when it was longer. Returns its width. */
int yk_text_fit(const char *text, yk_glyph_width_fn width, void *ctx, int max_w, char *out, size_t size);
/** The first character of a name (what stands in for its icon); "" for none. */
void yk_first_char(const char *name, char out[5]);

// ---------------------------------------------------------------- the catalog's bytes (yk_catalog.h)

#define YK_CATALOG_MAGIC   0x43414B59u  // "YKAC"
#define YK_CATALOG_VERSION 1
#define YK_CATALOG_HEADER  64
#define YK_CATALOG_TABLE_AT 4096        // the table and the icons start in the second sector
#define YK_CATALOG_ENTRY   128

/** CRC-32 (IEEE), continuing from `crc` (0 to start). (yaykit_board has a one-shot yk_crc32 of its own.) */
uint32_t yk_catalog_crc32(uint32_t crc, const void *data, size_t len);
/** The region's bytes for this catalog: what to write at offset 0 (header) and at YK_CATALOG_TABLE_AT (the
 *  table, then the icons). out must hold YK_CATALOG_REGION bytes; returns the bytes used from 0 (0 when it does
 *  not fit or an entry is bad). The icon offsets and CRCs are filled into the copy written. */
size_t yk_catalog_encode(const yk_catalog_t *cat, const uint8_t *const icons[], uint8_t *out, size_t size);
/** The header's fields; false when it is not a catalog this reader knows. *table_len: bytes of the table. */
bool yk_catalog_decode_header(const uint8_t *h, yk_catalog_t *out, uint32_t *table_len, uint32_t *table_crc);
/** The table into out->app (after decode_header); false when its CRC or an entry is wrong. */
bool yk_catalog_decode_table(const uint8_t *t, uint32_t len, uint32_t crc, yk_catalog_t *out);

#define YK_ICON_RGB565A8_BYTES (YK_CATALOG_ICON * YK_CATALOG_ICON * 3)
/** A 64 x 64 catalog icon as LVGL's RGB565A8 (the colours, then the alpha plane): the icon's own alpha times opa
 *  (255: as it is; YK_APPS_DIM_OPA: an app not installed), so it sits on the disc with its corners clear. A8 is
 *  drawn in `color`. */
void yk_icon_rgb565a8(uint8_t format, const uint8_t *src, uint32_t color, uint8_t opa, uint8_t *dst);

// ---------------------------------------------------------------- the hint arrow (device-controls.md 2)

#define YK_HINT_TIP_R     201   // the V's point from the centre (the prototype's 434 - 233)
#define YK_HINT_V_DEPTH   16    // from the point back to the arms' ends
#define YK_HINT_V_HALF    14    // half the V's opening
#define YK_HINT_STROKE    5
#define YK_HINT_NUDGE     8     // towards the key and back
#define YK_HINT_NUDGE_MS  700   // each way
#define YK_HINT_PILL_R    171   // the pill reaches this far towards the key
#define YK_HINT_PILL_H    40
#define YK_HINT_PILL_PAD  16    // left and right of the text
#define YK_HINT_TEXT_SIZE 18    // YK_SIZE_STATUS
#define YK_HINT_SHOW_MS   4000
#define YK_HINT_FADE_MS   200

typedef struct {
    float ux, uy;              // unit vector towards the key (screen: x right, y down)
    float tip_x, tip_y;        // the V: arm, point, arm (screen coordinates, before the nudge)
    float a1_x, a1_y, a2_x, a2_y;
    yk_box_t pill;             // the pill's box
} yk_hint_geo_t;

/** The arrow and the pill for a key at key_deg (clockwise from 12) and a line text_w px wide: the pill as close to
 *  the key as it can be with its corners inside the content circle. */
void yk_hint_geometry(float key_deg, int text_w, yk_hint_geo_t *g);

#define YK_HINT_CAP 3  // counts stop here (the rules only need "fewer than 3")

typedef struct {
    // saved in NVS
    uint8_t talked;      // turns spoken
    uint8_t ring;        // times the app ring opened
    uint8_t talk_shown;  // times TALK showed
    uint32_t seen;       // extensions whose APPS hint showed (yk_hint_app_bit)
    // this boot only
    bool tap_shown;      // TAP showed since the screen woke
    uint32_t app_bit;    // this firmware's bit (0: Yay, or not known)
} yk_hint_state_t;

/** An extension's bit in `seen` (a hash of its slug; never 0); 0 for Yay and NULL. */
uint32_t yk_hint_app_bit(const char *slug);
/** The rules (yk_hint.h) without the "ring open" one. */
bool yk_hint_rule(const yk_hint_state_t *s, yk_hint_kind_t kind);
/** An event; returns true when a saved count changed. */
bool yk_hint_apply(yk_hint_state_t *s, yk_hint_event_t ev);
/** The hint of this kind was shown; returns true when a saved count changed. */
bool yk_hint_shown(yk_hint_state_t *s, yk_hint_kind_t kind);
/** The kind's own line. */
const char *yk_hint_text(yk_hint_kind_t kind, bool english);
