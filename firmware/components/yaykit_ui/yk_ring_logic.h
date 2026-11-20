// Pure C parts of the settings ring and its dial (yk_ring.h; host tested with test/run.sh): where everything goes and how it
// moves, what a touch hits, a turn apart from a swipe.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yk_apps_logic.h"
#include "yk_ring.h"

// ---------------------------------------------------------------- the ring (round-screen-gestures.md 6.1)

// The app ring's size (yk_rings.h, 0.1.18): its places on the same circle, discs and targets as big, the middle as big.
#define YK_RING_STAGGER_MS 16

typedef struct {
    int n;
    int16_t cx[YK_RING_MAX], cy[YK_RING_MAX];
} yk_ring_layout_t;

/** Where place i of n stands: clockwise from 12 o'clock, evenly, on YK_RINGS_R. */
void yk_ring_place(int i, int n, int16_t *cx, int16_t *cy);
/** Lays out n places. */
void yk_ring_layout(int n, yk_ring_layout_t *out);

/** The ring t_ms after it started opening or closing. pose[0] the middle, pose[1..n] the places, as yk_apps_frame(). True
 *  once the motion is over. */
bool yk_ring_frame(const yk_ring_layout_t *L, int t_ms, bool closing, yk_apps_pose_t pose[YK_RING_MAX + 1]);
/** The same after a swipe (dx, dy: the finger's way): as yk_apps_frame_away(). */
bool yk_ring_frame_away(const yk_ring_layout_t *L, int t_ms, int dx, int dy, yk_apps_pose_t pose[YK_RING_MAX + 1]);

/** What a touch at (x, y) hits: 0 the middle, 1..n a place (the nearest whose target it is on), -1 the paper. */
int yk_ring_hit(const yk_ring_layout_t *L, int x, int y);
/** LV_EVENT_CLICKED at (x, y), at now_ms, with the app ring's rule: a tap (yk_tap) on what it hits, pressed there too
 *  (yk_apps_touch_press() with yk_ring_hit()); a press that swiped taps nothing. */
yk_apps_go_t yk_ring_touch_click(yk_apps_touch_t *t, const yk_ring_layout_t *L, int x, int y, uint32_t now_ms);

// ---------------------------------------------------------------- the dial (round-screen-gestures.md 6.4)

#define YK_DIAL_START     225   // the scale's first stop, degrees clockwise from 12 o'clock: 7:30
#define YK_DIAL_SWEEP     270   // ... to 4:30
#define YK_DIAL_BAND      65    // the edge band a turn starts in: px in from the screen's edge (the round-screen spec's dial)
#define YK_DIAL_TICK_OUT  228   // the scale, from the centre (0.1.18, 方案一: 206..228; 204..222 before)
#define YK_DIAL_TICK_IN   206
#define YK_DIAL_STOP_R    3     // a stop's mark on it: 6 across
#define YK_DIAL_MIDDLE_R  90    // a tap this close to the centre is 「好了」
#define YK_DIAL_VALUE_PX  72    // the value (48 before)
#define YK_DIAL_TITLE_Y   96    // the setting's name, YK_RINGS_NAME_PX, at the top (0.1.18)
#define YK_DIAL_TITLE_W   200
#define YK_DIAL_NOTE_GAP  6     // between the value and the note under it: the two about the centre
#define YK_DIAL_NOTE_W    300
// A title too wide for one line (声音和振动, 交互稿第 4 版): two lines at the same size, this far apart, the two about the
// middle of the one line (y 81-141 for a 30 px line).
#define YK_DIAL_TITLE_LINE_H 30
// The button under the note (yk_dial_view_t `button`): a pill, its corners half its height, its words 16 px with 12 px
// clear at either end (156 px for them). Below the middle's 90 (its top 93 px from the centre), inside the edge band's 168
// (its corners 166 px from the centre), under the note.
#define YK_DIAL_BUTTON_X   143
#define YK_DIAL_BUTTON_Y   326
#define YK_DIAL_BUTTON_W   180
#define YK_DIAL_BUTTON_H   46
#define YK_DIAL_BUTTON_PX  16
#define YK_DIAL_BUTTON_PAD 12
#define YK_DIAL_HIT_BUTTON (-2)  // what a touch on it hits (yk_dial_touch_t's yk_tap)

typedef struct {
    yk_box_t title, value, note;
    // Since 声音和振动 (appended): the title's second line (h 0: one line), the button (w 0: none) and its words.
    yk_box_t title2, button, button_words;
} yk_dial_layout_t;

/** Where the dial's words go for lines title_h, value_h and note_h px tall: the name at the top, the value and the note
 *  under it as one block about the centre. One title line, no button. */
void yk_dial_layout(int title_h, int value_h, int note_h, yk_dial_layout_t *out);
/** The same with the title on `title_lines` lines (1 or 2: YK_DIAL_TITLE_LINE_H apart, the two about the middle of the one
 *  line, each box title_h tall) and, when button_h > 0, the button with its words' line button_h tall in its middle.
 *  The value and the note are where yk_dial_layout() puts them, whatever the title and the button. */
void yk_dial_layout_full(int title_lines, int title_h, int value_h, int note_h, int button_h, yk_dial_layout_t *out);
/** The title's lines (glyph widths from `width`): `line1` alone when it fits max_w; else two, each within max_w, broken at
 *  the last place that lets the first fit: before a space between words (the spaces go), or between two characters either
 *  of which is Han or kana (never before closing punctuation or ー); when the second does not fit either, `line1` cut with
 *  "…" as before (yk_text_fit). Returns how many lines (0 for ""). */
int yk_dial_title_lines(const char *title, yk_glyph_width_fn width, void *ctx, int max_w, char *line1, size_t size1,
                        char *line2, size_t size2);

/** Where stop i of `stops` stands on the scale, degrees clockwise from 12 o'clock. */
float yk_dial_angle(int stop, int stops);
/** A point at radius r on that line. */
void yk_dial_point(int stop, int stops, int r, int *x, int *y);
/** The press at (x, y) can turn it. */
bool yk_dial_in_band(int x, int y);
/** The stop under a finger at (x, y), `last` the last it may reach; below the scale, the end nearer the finger. */
int yk_dial_stop_at(int x, int y, int stops, int last);

/** One press on the dial, from LVGL's events: what started on the edge is a turn to the end, whatever LVGL makes of its speed;
 *  what started further in is a swipe or a tap. A press on the edge that stays a tap (yk_tap: let go on the stop it went
 *  down on, the finger still) goes to that stop as it lets go (0.1.18); once it moves to another stop, or further than a
 *  tap may, the dial follows the finger from then on. */
typedef struct {
    bool turning;    // this press went down on the edge band
    bool following;  // ... and the dial follows it
    bool over;       // the dial is going: nothing more counts
    int down_stop;   // the stop it went down on
    yk_tap_t tap;    // hits: 0 the middle, 1 + its stop on the band, YK_DIAL_HIT_BUTTON the button, -1 elsewhere
    bool button;     // the dial has the button under its note (set by whoever opens it; kept across presses)
} yk_dial_touch_t;

typedef enum {
    YK_DIAL_GO_NONE,
    YK_DIAL_GO_TURN,    // the finger is at `stop` now
    YK_DIAL_GO_DONE,    // the middle tapped, or swiped from inside: back
    YK_DIAL_GO_BUTTON,  // the button tapped (yk_tap: down and up on it, still, not held): the dial stays
} yk_dial_go_t;

/** LV_EVENT_PRESSED at (x, y), at now_ms. Nothing turns yet: *stop is left as it is. */
yk_dial_go_t yk_dial_touch_press(yk_dial_touch_t *t, int x, int y, int stops, int last, uint32_t now_ms, int *stop);
/** LV_EVENT_PRESSING at (x, y): once it is a turn, the stop under the finger. */
yk_dial_go_t yk_dial_touch_move(yk_dial_touch_t *t, int x, int y, int stops, int last, int *stop);
/** LV_EVENT_GESTURE. */
yk_dial_go_t yk_dial_touch_swipe(yk_dial_touch_t *t);
/** LV_EVENT_CLICKED at (x, y), at now_ms: a tap on a stop goes there; a tap on the middle is 「好了」; a tap on the button
 *  (with `button`) is YK_DIAL_GO_BUTTON; a turn lets go without one mattering. */
yk_dial_go_t yk_dial_touch_click(yk_dial_touch_t *t, int x, int y, int stops, int last, uint32_t now_ms, int *stop);
/** The button looks pressed (CARD, as big_button's): a finger down on it that may still be a tap there (not moved off,
 *  the dial not going). */
bool yk_dial_button_pressed(const yk_dial_touch_t *t);
