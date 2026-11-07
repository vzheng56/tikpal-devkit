// The round screen's side of 声波码 without LVGL (yk_vc_view.c draws with it; test/run.sh tests it on the host): the
// code in pixels (the layout of README.md at a ring radius in px), the 24 boxes a frame redraws, the bars' talking
// before they settle on the code (docs/delivery/2026-10-30-voice-code.md 「动画」), and the app code an app shows,
// its number built in from app.json (voicecode_number.cmake).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yk_voicecode.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YK_VC_VIEW_R 184     // the ring's radius on the 466 x 466 screen: tried with the iPhone (#975), inside the safe circle
#define YK_VC_LOGO_D 0.40f   // the logo's size across, in units of the ring's radius (BOSS-2, 11-01: the code large,
                             // the middle small; the contract allows a radius of YK_VC_LOGO_MAX_R)
#define YK_VC_ANIM_MS 1200   // the bars talk this long when a code shows ...
#define YK_VC_SETTLE_MS 260  // ... the last of it easing onto the code; then they never change again
#define YK_VC_FRAME_MS 33    // about 30 frames a second at most ...
#define YK_VC_YIELD_MS 15    // ... and after each frame is on the screen, this long with LVGL asleep (its lock free) before
                             // the next: the firmware's own screen updates (a lock waited for 200 ms) get it during the
                             // talking too (BOSS-2, 11-03)
#define YK_VC_WAIT_MS 250    // a frame whose refresh never comes (the code covered): the next one this long after it
#define YK_VC_BOXES (YK_VC_BARS / 2)  // a frame redraws at most these (two bars each): LVGL keeps 32 areas a frame,
                                      // one a bar would fall back to the whole screen

// The pairing page (handoff 2026-11-01-voice-code.md 2.1③): the 8 digits in the code's middle in place of the mark,
// inside the logo's circle (YK_VC_LOGO_MAX_R of the ring), where the code carries nothing. Measured in the round
// screen's font (yaykit_board/fonts/cjk/yaykit_fonts.bin): a digit is 0.555 em across whatever it is, the space 0.224,
// so "1234 5678" at 28 px is 134 px; a digit stands 0.73 em.
#define YK_VC_MIDDLE_PX 28
#define YK_VC_DIGITS_W 134
#define YK_VC_DIGITS_H 21

/** Whether a box `w` x `h` centred on the code stays inside the logo's circle of a ring of radius `r`. */
bool yk_vc_middle_fits(int r, int w, int h);

/** The code in whole pixels for a ring of radius `r`, offsets from the code's centre (x right, y down). */
typedef struct {
    int r;       // the ring's radius (the middle of its stroke)
    int ring_w;  // the ring's stroke
    int bar_w;   // a bar's stroke; its round caps add half of it at each end
    int dot_y;   // the coral dot's centre, this far above the centre
    int dot_r;   // the coral dot's radius
    int logo;    // the logo's size across
    int reach;   // the farthest any of it reaches from the centre (the ring's outer edge, as LVGL draws it)
} yk_vc_px_t;
void yk_vc_px(int r, yk_vc_px_t *out);

/** Bar k at `len` (units of the ring's radius, cap centre to cap centre), as drawn: its two cap centres in whole
 *  pixels from the code's centre, the inner one first: {x1, y1, x2, y2}. */
void yk_vc_bar_px(const yk_vc_px_t *px, int k, float len, int16_t ends[4]);

/** The box bars 2j and 2j + 1 (j < YK_VC_BOXES) stay inside at any length, caps and `margin` px included: {x1, y1,
 *  x2, y2} from the code's centre, inclusive. */
void yk_vc_box_px(const yk_vc_px_t *px, int j, int margin, int16_t box[4]);

/** The level whose length is nearest `len` (the colour a bar is drawn in: level 3 may be coral). */
int yk_vc_nearest_level(float len);

/** The bars' talking, then the code. `lengths` (units of the ring's radius) stay within level 0..3's lengths. */
typedef struct {
    float lengths[YK_VC_BARS];
    float drive[YK_VC_BARS];  // each bar's loudness 0..1, after a random goal
    uint8_t target[YK_VC_BARS];
    uint32_t rng;
    float phase1, phase2;
    bool running;
} yk_vc_anim_t;

/** Every bar at its shortest, still. */
void yk_vc_anim_init(yk_vc_anim_t *a);
/** Talks from the bars' lengths now (the last code, or a frame cut short) and settles on `target` YK_VC_ANIM_MS later.
 *  `seed`: any number but 0. */
void yk_vc_anim_start(yk_vc_anim_t *a, const uint8_t target[YK_VC_BARS], uint32_t seed);
/** The frame `t_ms` after the start. From YK_VC_ANIM_MS on the lengths are the code's exactly and it stops: returns
 *  true that once, false otherwise (and changes nothing once stopped). */
bool yk_vc_anim_step(yk_vc_anim_t *a, int t_ms);

/** How long after a frame is on the screen the next one comes, `since_ms` after that frame began: YK_VC_FRAME_MS
 *  from its start, and YK_VC_YIELD_MS after its end at least. The talking takes YK_VC_ANIM_MS whatever the frames
 *  (the lengths are those of the time since it began), so fewer frames change nothing but their number. */
int yk_vc_next_frame_ms(int since_ms);

/** This app's 应用码 number, from its app.json's voiceCode.number at build time (or -D YK_VC_APP_NUMBER); 0 when it
 *  has none. */
uint32_t yk_vc_app_number(void);

#define YK_VC_NO_NUMBER (-2)
/** The 应用码 this app shows for `action` and `value`: number from yk_vc_app_number(). Returns 0, YK_VC_NO_NUMBER when
 *  the app has no number, or -1 for an action over 15 or a value of 2^24 or more. */
int yk_vc_app_code(uint32_t action, uint32_t value, yk_vc_payload_t *payload, uint8_t levels[YK_VC_BARS]);

#ifdef __cplusplus
}
#endif
