// The settings ring and its dial (docs/delivery/2026-10-31-round-screen-gestures.md 6, 9): one look for every setting on the
// device, the main firmware's and later an app's own. The ring stands as the app ring does (yk_apps): paper over everything,
// a middle, up to 8 places clockwise from 12 o'clock, opening from the centre, closing towards the finger. Colours (Yay
// 0.1.19 4.7): icons in ink; a level as the accent's thin arc round its place; coral only solid (the update dot, a
// middle filled with it). Since 0.1.18 it is
// the app ring's size (Yay 0.1.18 dev doc 4.7): places of 108 on a circle of r 150, a middle of 150, 6 places for Yay's own
// settings (7 keep 12 px apart; 8 are drawn closer). A place is a dark disc with a line icon (YK_RING_ICON), a coral dot on
// its upper right when there is something new; its name shows in the middle while a finger is down on it. The middle is
// words otherwise: what the ring is about (76% · Home-5G), or an action (全部更新), and the line `below`.
//
// The dial is the page of a value: a scale along the edge, 7:30 clockwise to 4:30, the value in the middle, the setting's
// name at the top (0.1.18), and if the caller asks, a button under the note that leads on (声音和振动). A finger that
// goes down on the edge band and moves turns it, following the finger; one that only taps a stop there goes to that stop as
// it lets go (0.1.18); one that swipes from further in goes back; a tap on the middle is done. Every step is reported as it
// happens, and there is no cancel.
//
// A tap, on the ring and the dial alike (0.1.18): the finger goes down on a place and lets go on the same place, without
// moving more than a tap may (10 px) and in under 1.5 s. Down on one and up on another, a slow drag: nothing.
//
// Objects exist only while open: one drawing object each, the state in PSRAM. The caller's icons stay the caller's and must
// outlive the ring. What a tap does is the caller's: this only reports it. Call under board_lock() or from LVGL callbacks,
// from LVGL's task when the words change: measuring them takes TinyTTF more stack than a small task has.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YK_RING_MAX 8
#define YK_RING_ICON 80  // a place's icon, in px: it is drawn at this size (0.1.18: 40 before, on a smaller disc)

typedef struct {
    const char *name;
    const lv_image_dsc_t *icon;  // YK_RING_ICON square in any format LVGL draws; NULL: the name's first character
    bool dot;                    // a coral dot on the upper right: something new behind it
    bool more;                   // 「更多」: three dots for an icon
    bool busy;                   // a sage ring round the disc: at work on it now
    bool stays;                  // a tap only reports it, the ring stays: a switch, or a place the caller redraws
    // Yay 0.1.19 (开发文档 4.7): a setting with a level (its dial's) shows it as an arc round the disc, the accent's thin
    // line (yk_accent_text) from 7:30 clockwise as far as `level`, 0..1000 of the way to 4:30. Not `leveled`: none
    // (what an item from before gets).
    bool leveled;
    int level;
} yk_ring_item_t;

typedef struct {
    const yk_ring_item_t *items;
    int n;                 // at most YK_RING_MAX; fewer share the circle evenly from 12 o'clock
    const char *title;     // the middle, one line, cut to fit
    const char *sub;       // under it; NULL or "": none
    uint32_t fill;         // the middle's colour, 0xRRGGBB; 0: the panel's
    bool action;           // a tap on the middle reports it and the ring stays
    bool hold;             // no swipe, no tap beside the places, no 8 s: something is under way
    bool keep;             // no 8 s: it is a level down, with a way back of its own
    const char *below;     // a line under the middle's words (0.1.18: in the middle, below the places before); NULL, "": none
    int permille;          // a sage progress along the edge, 0..1000; < 0: none
} yk_ring_view_t;

typedef enum {
    YK_RING_GONE,    // a swipe, the paper, the middle, 8 s: it closed, nothing chosen
    YK_RING_PLACE,   // `index` tapped; it closed, unless the place `stays`
    YK_RING_ACTION,  // the middle of a view with `action`; it is still there
} yk_ring_what_t;

/** Called when the motion is over: the ring is gone by then (not for YK_RING_ACTION or a place that `stays`), and the
 *  callback may open it again or draw a page. Not called after yk_ring_close(). */
typedef void (*yk_ring_cb_t)(yk_ring_what_t what, int index, void *ctx);

/** Opens it over `parent` (the active screen: it stays its top child). The view is copied, its icons are not. Already open:
 *  that one goes at once, no callback. False with no memory. */
bool yk_ring_open(lv_obj_t *parent, const yk_ring_view_t *view, yk_ring_cb_t cb, void *ctx);
/** New words or marks on the open ring, without any motion: the places stay where they are for the same `n`. */
void yk_ring_show(const yk_ring_view_t *view);
/** Closes it without a callback: folding back in 180 ms (`animated`) or at once. Nothing when it is closed. */
void yk_ring_close(bool animated);
/** Open, or still folding back. */
bool yk_ring_is_open(void);

typedef struct {
    int stops;  // how many values, 2 or more
    int stop;   // the value now, 0..stops - 1
    int last;   // the last it may go to (a cap: the scale ends there); < 0: stops - 1
    const char *note;  // the line under the value
    // The setting's name, at the top (0.1.18); NULL or "": none. One line when it fits; else two at the same size about
    // the middle of that one line, broken at a space or between Han and kana (声音和振动, 交互稿第 4 版: never smaller);
    // cut with "…" only when two lines do not hold it either.
    const char *title;
    // A pill under the note that leads on (声音和振动's 「振动 · 轻」): its words; NULL or "": none, the dial as before.
    // A tap on it reports YK_DIAL_BUTTON and the dial stays: what it leads to is the caller's, and so is closing the dial.
    const char *button;
} yk_dial_view_t;

typedef enum {
    YK_DIAL_TURN,    // `stop` is the value now; the dial stays
    YK_DIAL_DONE,    // the middle tapped, or a swipe from inside: it closed, keeping `stop`
    YK_DIAL_BUTTON,  // the button under the note (`button`) tapped: the dial stays, `stop` as it is
} yk_dial_what_t;

typedef void (*yk_dial_cb_t)(yk_dial_what_t what, int stop, void *ctx);
/** The words of `stop`, into `out`: called on LVGL's task whenever the value changes. */
typedef void (*yk_dial_label_t)(int stop, char *out, size_t size, void *ctx);

/** Opens the dial over `parent`, as yk_ring_open(). False with no memory. */
bool yk_dial_open(lv_obj_t *parent, const yk_dial_view_t *view, yk_dial_label_t label, yk_dial_cb_t cb, void *ctx);
/** The value changed elsewhere (the phone, the voice): moves it without a callback. */
void yk_dial_set(int stop);
/** Closes it at once, without a callback. Nothing when it is closed. */
void yk_dial_close(void);
bool yk_dial_is_open(void);

#ifdef __cplusplus
}
#endif
