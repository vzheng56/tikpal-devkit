// Yay in an app (round-screen spec 7, extension platform P11): the character from yaykit/firmware/assets/yay (I8
// images with the face's boxes, yay_art.h), alive on its own: it blinks every 4..6 s, looks at whoever speaks or
// whose turn it is, opens its mouth with the voice (closed / slightly / open), changes pose with a short fade, and
// can be turned to any angle (Party Play peeks in from a player's edge; Translator faces the far side).
//
// Sizes: 264 hero, 176 companion, 132 corner. A pose that is not exported at a size is drawn from the nearest size
// that is, scaled (yk_yay_has() says whether it is native). Missing now: 132 for anything but greet, 264 for all but
// ready / resting / greet, thinking_glass at any size (the export script adds them; the names stay).
//
// Call under board_lock() (or from LVGL callbacks); blinking and breathing run on lv_timer / lv_anim.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "yk_buddy_slot.h"  // yk_buddy_t, for yk_yay_buddy

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YK_YAY_READY = 0,
    YK_YAY_ACK,
    YK_YAY_LISTENING,
    YK_YAY_THINKING,
    YK_YAY_WORKING,
    YK_YAY_APPROVAL,
    YK_YAY_SPEAKING,
    YK_YAY_DONE,
    YK_YAY_REMINDER,
    YK_YAY_HELP,
    YK_YAY_RESTING,
    YK_YAY_GREET,
    YK_YAY_WORKING_LAPTOP,
    YK_YAY_THINKING_GLASS,  // Homework Buddy's "looking it up": until exported, drawn as THINKING
    YK_YAY_POSE_COUNT,
} yk_yay_pose_t;

typedef enum {
    YK_YAY_MOUTH_POSE = -1,  // the pose's own mouth
    YK_YAY_MOUTH_CLOSED = 0,
    YK_YAY_MOUTH_SLIGHT,
    YK_YAY_MOUTH_OPEN,
} yk_yay_mouth_t;

typedef struct yk_yay yk_yay_t;

/** `paper` for a Yay drawn over anything (Party Play peeking in over the wheel and the dice): the art keeps its
 *  alpha (RGB565 + an alpha plane, 3 bytes a pixel) instead of being blended with one colour. Costs more to draw
 *  than an opaque pose: use it where Yay overlaps other things, not on a plain background. */
#define YK_YAY_TRANSPARENT 0xFF000000u

/** Yay at `size` (132, 176 or 264) in `parent`, centred on (cx, cy) in the parent's coordinates, pose READY,
 *  blinking. The background under it is `paper` (YK_PAPER, YK_NIGHT...): the art is blended with it once (opaque
 *  RGB565, cheaper to draw), so put it on that colour; YK_YAY_TRANSPARENT keeps it transparent around Yay. */
yk_yay_t *yk_yay_create(lv_obj_t *parent, int size, int cx, int cy, uint32_t paper);
void yk_yay_delete(yk_yay_t *yay);
lv_obj_t *yk_yay_obj(yk_yay_t *yay);

/** Changes the pose, crossfading over fade_ms (0: at once). */
void yk_yay_pose(yk_yay_t *yay, yk_yay_pose_t pose, int fade_ms);
yk_yay_pose_t yk_yay_get_pose(const yk_yay_t *yay);
/** Changes the size (keeps the centre). */
void yk_yay_size(yk_yay_t *yay, int size);
void yk_yay_move(yk_yay_t *yay, int cx, int cy);
/** Yay on another `paper` from now on (Yay 0.2's home at night: black, 22:00–7:00): the pose drawn again over it, and
 *  what the cache made over the old one that no Yay shows any more goes (PSRAM, as every picture). Nothing for the
 *  same paper, or from or to YK_YAY_TRANSPARENT (a transparent Yay stays one). Not called: as before, byte for byte. */
void yk_yay_paper(yk_yay_t *yay, uint32_t paper);

/** Blinks by itself every 4..6 s (on by default; off while a pose has its eyes shut). */
void yk_yay_auto_blink(yk_yay_t *yay, bool on);
void yk_yay_blink(yk_yay_t *yay);
/** Eyes towards a point on the screen (absolute coordinates); yk_yay_look_ahead() puts them back. */
void yk_yay_look_at(yk_yay_t *yay, int x, int y);
void yk_yay_look_ahead(yk_yay_t *yay);
/** The mouth: a fixed shape, or from what the speaker is playing now (yk_speak_level(), 0..100; call ~10 times a
 *  second while it speaks; a little hysteresis so it does not flutter). */
void yk_yay_mouth(yk_yay_t *yay, yk_yay_mouth_t mouth);
void yk_yay_mouth_level(yk_yay_t *yay, uint8_t level);
/** Breathing: 3 px up and down every 5 s (the idle screen's), off by default. */
void yk_yay_breathe(yk_yay_t *yay, bool on);

/** Turns Yay about its centre to `deg` (clockwise; 180 = upside down for the far side), over ms (0: at once).
 *  Rotated images cost more to draw: keep it still while turned, or small. */
void yk_yay_rotate(yk_yay_t *yay, float deg, int ms);

/** Squashed and stretched (Yay's badge, 0.1.20: a knock against the rim, a landing, home's wind-up before it jumps out):
 *  its width times sx and its height times sy about its middle, on top of its size; 1, 1: as drawn. Its own blinks and
 *  mouth wait while squashed; yk_yay_face says where the squashed eyes are. Drawing a scaled image costs more: squash it
 *  for moments. */
void yk_yay_squash(yk_yay_t *yay, float sx, float sy);

/** Drawn scaled or squashed quickly (nearest pixel, no smoothing): a picture scaled every frame for moments (Yay's
 *  badge jumping in, 264 ⇄ 176, a knock; home's wind-up) draws in about half the time, and moving that fast nobody sees
 *  the edges (新四号机 11-18: the jump in drew at 9 frames a second). Drawn at its own size it looks the same either way. */
void yk_yay_fast(yk_yay_t *yay, bool on);

/** Copies of `pose` (of what `yay` draws: its pack's picture, or Pip's) at each of `sizes`, opaque over `paper`, made in
 *  PSRAM by a task of their own at the lowest priority, the largest picture of the pose scaled once. Any Yay asked for
 *  one of those sizes, unsquashed or squashed, then draws the copy as it is instead of scaling the picture every frame
 *  (Yay's badge, 0.1.21: its jump in, growing back, popping out at 5–13 frames a second). The Yay keeps its own size as
 *  asked: what is not made yet, or never fits, is drawn scaled as before. At most 12 copies; a copy made already is not
 *  made again. The number asked to be made now (0: all there, being made, or no room). */
int yk_yay_prescale(yk_yay_t *yay, yk_yay_pose_t pose, uint32_t paper, const uint16_t *sizes, int n);
/** What is made: copies, their bytes, and how long the last job took (ms); true while one is being made. */
bool yk_yay_prescale_stats(uint32_t *ready, uint32_t *bytes, uint32_t *made_ms);
/** Every copy goes (a job going stops first), but one some Yay draws now (that one at the next drop). */
void yk_yay_prescale_drop(void);

/** A buddy's pack (yk_figure.h) instead of the built-in Pip, on this Yay only, from now on and at once, without a fade.
 *  It draws the same poses at the same sizes from the pack; blinking and the mouth paste the pack's small blocks at
 *  (x, y) and take its times and its breath. yk_yay_look_at does nothing on it. `figure` is copied, the pack's bytes are
 *  not: keep them until another figure took its place. NULL: Pip again. On an opaque paper only: false on
 *  YK_YAY_TRANSPARENT, and false with Pip back when the memory for a picture is not there. Several Yays may hang the
 *  same pack (Yay 0.1.18: the home screen's and the app ring's middle): what was drawn from it goes when the last of
 *  them takes another or is deleted, so its bytes may go once every Yay that hung it has. */
struct yk_figure;
bool yk_yay_figure(yk_yay_t *yay, const struct yk_figure *figure);

/** Which buddy this Yay is (development kit 1.10, YK_API_LEVEL 5; 开发包四个伙伴-需求确认.md §4): YK_BUDDY_CURRENT, the
 *  one the user chose, or one by name (YK_BUDDY_PIP, YK_BUDDY_MOTE, YK_BUDDY_DIAL, YK_BUDDY_BLOOM; yk_buddy_slot.h) for
 *  an app that shows a buddy of its own choosing, a gallery say. A value it does not know is YK_BUDDY_CURRENT. In an
 *  extension yk_yay_create() already made it the current buddy: an app that stands for the user's buddy writes nothing.
 *  The current buddy is read once a run: one chosen while the app is open shows the next time it opens.
 *  The pack is read from the buddy slots the main firmware keeps (the current buddy and the one before), once a run
 *  for each buddy and kept (about 630 KB of PSRAM each; nothing for Pip and none for an app that draws no buddy). The
 *  first read takes about a tenth of a second. Drawn as Pip, who is always in the image, when that buddy's pack is not
 *  in a slot (the main firmware has not fetched it, a main firmware before 0.1.18), on YK_YAY_TRANSPARENT (a pack draws
 *  on an opaque paper only), or with no memory for it. True: drawn as asked (Pip asked counts); false: Pip instead. */
bool yk_yay_buddy(yk_yay_t *yay, int buddy);

/** Pip's own pictures from Pip's pack, for a firmware built without them (CONFIG_YK_UI_PIP_BUILTIN off, Yay from 0.1.18:
 *  its image holds greet_132 only, the fallback every pose is drawn as until this). Pip is drawn as ever, the eyes and
 *  the mouth over the art; only where the pixels are changes. Every Yay drawing Pip takes them at its next tick, and
 *  every one made after. Once: a second call changes nothing (a Yay may be showing what the first one gave). The pack's
 *  bytes are the caller's and stay put for good (PSRAM). Returns how many of Pip's pictures it found in the pack (0:
 *  none, and nothing changed). Built with Pip's pictures in the image, it does nothing and returns 0. */
int yk_yay_pip(const struct yk_figure *pack);

/** Where its eyes are drawn now, in the screen's (the parent's) coordinates, and the skin around them: for a firmware that
 *  draws an expression over them (Yay's badge, 0.1.20: spirals when dizzy, arcs when happy). The art's boxes for Pip, the
 *  pack's eye blocks for a buddy, at the size drawn. False when the pose has its eyes shut, or Yay is turned. */
typedef struct {
    struct {
        int16_t x, y, w, h;
    } eye[2];       // left and right as seen
    uint32_t skin;  // 0xRRGGBB just beside them
} yk_yay_face_t;
bool yk_yay_face(const yk_yay_t *yay, yk_yay_face_t *out);

/** Whether the art has this pose at this size (otherwise it is scaled from another). */
bool yk_yay_has(yk_yay_pose_t pose, int size);
/** The pose's name as the art and the protocol spell it ("working-laptop"); NULL when out of range. And back:
 *  YK_YAY_POSE_COUNT for a name it does not know. */
const char *yk_yay_pose_name(yk_yay_pose_t pose);
yk_yay_pose_t yk_yay_pose_from_name(const char *name);

#ifdef __cplusplus
}
#endif
