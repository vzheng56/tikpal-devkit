// Pure C part of 「我的应用」 (yk_more.h), host tested with test/run.sh: where each app sits on the honeycomb, how big
// and how bright it is by how far it is from the one in the middle, which are shown, what a touch hits, how far a drag
// may take it, the motion to an app, and the words below it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "yk_more.h"

// Yay 0.1.18 (the meeting's item 20, the interaction draft's 「我的应用」 board): the middle one's name and where it is
// went up, where a thumb sliding from below never covers them, and the title went; the honeycomb came down and grew.
#define YK_MORE_CX       233    // the honeycomb's middle: the screen's centre line ...
#define YK_MORE_CY       270    // ... lowered, the two lines of words above it (0.1.17: 196, the lines below)
#define YK_MORE_STEP     96.0f  // between neighbours' centres (hexagons of side 55.4: 55.4 x sqrt 3; 0.1.17: 90.07)
#define YK_MORE_ROW      83.14f // between rows (55.4 x 1.5; 0.1.17: 78)
#define YK_MORE_SIZE_0   116    // the middle one (0.1.17: 100)
#define YK_MORE_SIZE_1   80     // one step away (72)
#define YK_MORE_SIZE_2   60     // two rows away (sqrt 3 steps; 54)
#define YK_MORE_SIZE_FAR 46     // two steps and farther (40)
#define YK_MORE_DIM_FROM 150.0f // farther than this from the middle, darker ...
#define YK_MORE_DIM_TO   190.0f // ... down to YK_MORE_DIM_OPA here and beyond
#define YK_MORE_DIM_OPA  140    // 55 %
#define YK_MORE_CLOUD_OPA 107   // 42 %: to download
#define YK_MORE_TOP      128    // centres above this are not drawn (the two lines are there; 0.1.17: 84, the title)
// ... nor one reaching into the rim band (YK_R_BAND, 214: its disc at its size there, yk_more_size): no app covers the
// rim (0.1.17: centres within 196 of the screen's; the interaction draft drew some to y 446, over it).
#define YK_MORE_HIT_MIN  28     // a small app's touch target: 56 across at least
#define YK_MORE_MOVE_MS  260    // to the middle
#define YK_MORE_NAME_Y   64     // 24 px: the middle one's name (0.1.17: 344, 22 px)
#define YK_MORE_STATE_Y  100    // 22 px: where it is (0.1.17: 378, 16 px)
#define YK_MORE_LINE_W   300    // the lines are cut to this, or to yk_more_line_room()
#define YK_MORE_FADE_MS  200    // a new name fades in this long; so do both lines back to full once a drag lets go ...
#define YK_MORE_DRAG_NAME_OPA  140  // ... from these while a finger drags (55 %) ...
#define YK_MORE_DRAG_STATE_OPA 102  // ... and 40 %
#define YK_MORE_SHAKE_MS 240    // a coming-soon app tapped in the middle: shaken this long ...
#define YK_MORE_SHAKE_PX 6      // ... this far each way, twice
#define YK_MORE_LIT_R    48.0f  // autofocus: an app nearer the middle than this (half a step) is lit, fully there ...
#define YK_MORE_HALO_W   5      // ... with a halo this wide round its disc ...
#define YK_MORE_HALO_OPA 77     // ... in ink at 30 % (BOSS-2 10-26: the user picks from 22 % and 30 % pictures; this one)
#define YK_MORE_NEAR     (YK_MORE_STEP * 1.5f)  // the in-between sizes' masks are made for apps this near the middle
#define YK_MORE_ICON_MAX 84     // the biggest an icon is drawn: the store's 64 x 64 at most 1.31 times (blurred past it)

typedef struct {
    int16_t q, r;  // axial coordinates: q to the right, r down and to the right
} yk_hex_t;

/** The i-th place: 0 the middle, then ring by ring outwards, each from the right going anticlockwise (right, upper
 *  right, upper left, left, lower left, lower right: the prototype's order). */
yk_hex_t yk_more_place(int i);
/** Where place h is on the screen with the honeycomb's origin (place 0) at the middle plus (ox, oy). */
void yk_more_at(yk_hex_t h, float ox, float oy, float *x, float *y);
/** How far apart places i and j are on the honeycomb, in px (yk_more_place). */
float yk_more_apart(int i, int j);
/** Diameter at distance d (yk_more_apart) from the one in the middle: 116 for it, 80 a step away, 60 at two rows, 46
 *  from two steps. Sizes go with the places, not with the screen: a drag moves them as they are; letting go puts
 *  another one in the middle, and they change to its sizes as it moves there (the delivery 1.2, A: no fisheye). */
float yk_more_size(float d);
/** Opacity at distance d from the one in the middle (255: none taken away). */
uint8_t yk_more_opa(float d);
/** Autofocus (docs/delivery/2026-10-26-more-autofocus.md): how far (x, y) is from the honeycomb's middle on the
 *  screen. Sizes and opacity go by it (yk_more_size, yk_more_opa) as the honeycomb moves, so they change with every
 *  frame of a drag, Apple Watch's way; at rest it is each app's distance from the one in the middle, as before. */
float yk_more_from_middle(float x, float y);
/** How lit an app is at distance d from the middle: 1 there, down to 0 at YK_MORE_LIT_R, eased (smoothstep). */
float yk_more_lit(float d);
/** Whether an app centred at (x, y) is drawn: below YK_MORE_TOP, and its disc (yk_more_size by its distance from the
 *  middle) inside the rim band. */
bool yk_more_shown(float x, float y);
/** The offset that puts item i in the middle. */
void yk_more_offset_for(int i, float *ox, float *oy);
/** The item nearest the middle at this offset (0 when n is 0). */
int yk_more_nearest(int n, float ox, float oy);
/** Keeps a drag from taking the honeycomb away: the middle stays within the apps' reach (half a step past the
 *  farthest). */
void yk_more_clamp(int n, float *ox, float *oy);
/** The shown item whose disc (its size with `focus` in the middle; YK_MORE_HIT_MIN at least) holds (x, y), the
 *  nearest when two do; -1 for none. */
int yk_more_hit(int n, int focus, float ox, float oy, int x, int y);
/** How far a move is t_ms into YK_MORE_MOVE_MS: 0 to 1, eased in and out. */
float yk_more_ease(int t_ms);
/** Eased (yk_more_ease) from a to b, t_ms into YK_MORE_MOVE_MS; true once there. */
bool yk_more_move(float ax, float ay, float bx, float by, int t_ms, float *x, float *y);
/** The size class for a disc of this diameter: the nearest of eight (autofocus, the delivery 2.4 B) between 116 (0) and
 *  46 (7). At rest the honeycomb's sizes (116, 80, 60, 46) are classes 0, 2, 4 and 7, as before. */
int yk_more_size_class(float size);
/** Classes 0, 2, 4 and 7: the sizes at rest, made for every app; the others only for apps near the middle. */
bool yk_more_class_at_rest(int size_class);
/** The first character's size for a disc of this diameter: 36, 32, 28, 24, 22, 20, 18 or 16 by its class (sizes Yay's
 *  pages use already: dropping one from the font cache would crash). */
int yk_more_glyph_px(float size);
/** How wide a centred line of text at y (its box's top), line_h tall, may be with its glyphs (the box less 4 px above
 *  and 1 below, as on the ring) inside the content circle (r 200); YK_MORE_LINE_W at most. */
int yk_more_line_room(int y, int line_h);
/** What the line below says of the middle one (app-ecosystem.md 2). A preview says the same as a stable release
 *  (Yay 0.1.18: the screen never says 预览版; `preview` is kept for the callers and no longer changes the words). */
const char *yk_more_state_text(yk_more_where_t where, bool current, bool preview, bool english);
/** The line below as drawn (Yay 0.1.4's delivery 2.3): for an app to download, how long installing it would take
 *  (eta_s > 0: 约 40 秒) or, not measured, its size (bytes > 0: 要等一会儿 · 3.1 MB), else 要等一会儿. One in the
 *  package cache (Yay 0.1.5's delivery 3): the copying's seconds (约 20 秒, ~20 s), or 很快 when eta_s is 0. Any other:
 *  yk_more_state_text(). A preview's line is the same (Yay 0.1.18: no 预览版 on the screen). Returns out. */
const char *yk_more_state_line(yk_more_where_t where, bool current, bool preview, uint32_t bytes, int eta_s,
                               bool english, char *out, size_t size);
/** A size by 1000: whole KB under 1 MB (860 KB), else MB with one decimal (3.1 MB). Returns out. */
const char *yk_more_size_text(uint32_t bytes, char *out, size_t size);
/** An app's icon at each size class (#531 4; eight since autofocus): disc x 64 / 76, as the ring's 64 in its 76, but
 *  YK_MORE_ICON_MAX at most: 84, 83, 67, 59, 51, 46, 42 and 39 px in discs of 116, 98, 80, 70, 60, 55, 50 and 46,
 *  biggest first. */
#define YK_MORE_GLYPHS 8
int yk_more_icon_px(int size_class);
/** A tap on an app that is not the middle one opens it at once (the delivery 2.6 C) when it is on the device (in a
 *  slot, in the package cache); one to download or coming soon comes to the middle first, its line below seen. */
bool yk_more_opens_at_once(yk_more_where_t where);
/** A 64 x 64 A8 icon as n x n (1..128) into dst (n * n bytes): smaller by the area each pixel covers, bigger
 *  bilinearly (84: 1.31 times, its edges a little soft). tmp: YK_MORE_ICON_TMP floats of scratch (nothing big on the
 *  caller's stack: LVGL's task). Pure. */
#define YK_MORE_ICON_TMP (64 * 128 + 64 + 2 * 128)
void yk_more_icon_scale(const uint8_t *src, int n, uint8_t *dst, float *tmp);

/** An opacity `v` dt_ms later on its way to `target`: the whole way (0 to 255) takes YK_MORE_FADE_MS; there, it
 *  stays. (The two lines: a new name from 0; down to YK_MORE_DRAG_*_OPA while a finger drags, back up once it lets
 *  go.) */
uint8_t yk_more_fade(uint8_t v, uint8_t target, int dt_ms);

/** How far a shaken app is to the side, t_ms into YK_MORE_SHAKE_MS (0 before and after). */
float yk_more_shake(int t_ms);
