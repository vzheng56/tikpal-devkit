// 声波码 on the round screen (docs/delivery/2026-10-30-voice-code.md 「码长什么样」「动画」; the handoff
// docs/handoff/2026-11-01-voice-code.md 2.1①): the code drawn light on pure black, its bars talking for 1.2 s before
// they settle on the code, and only the bars' ring redrawn while they do.
//
//   yk_vc_show()       an app shows its own 应用码: an action and a value; its number comes from its app.json
//                      (voiceCode.number), built in, never passed. Over everything the app draws, until it is hidden.
//   yk_vc_hide()       hides it; yk_vc_on_key() hides it on a tap of the side key.
//   yk_vc_view_*()     the code as a widget on a page of the firmware's own (a pairing page).
//   yk_vc_pair_layer() the pairing page's code over a whole screen, the 8 digits in its middle (yk_ui_pair_content).
//
// The screen stays on while a code shows only if the app keeps it on: this component never touches
// yk_app_keep_screen_on() (a switch, not a count: setting it here would undo the app's own). An app whose screen may go
// off before the code hides keeps it on itself while yk_vc_showing(). The code the app ring shows (yk_vc_show_ring())
// is yaykit_app's: it keeps the screen on with a switch of its own while that code is up.
//
// Every firmware with yaykit_app links it since 0.1.14: added beside the rest, nothing else changed. For apps
// from YK_API_LEVEL 4 (devkit docs/voice-code.md; the template's 「问 <buddy>」, action 3).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"
#include "yk_key_types.h"
#include "yk_voicecode.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YK_VC_HIDE_AFTER_S 60  // a shown code hides itself after this, unless the app says otherwise

typedef enum {
    YK_VC_HIDDEN_APP = 0,  // yk_vc_hide()
    YK_VC_HIDDEN_TIMEOUT,  // its time was up
    YK_VC_HIDDEN_TAP,      // a tap on it
    YK_VC_HIDDEN_KEY,      // a tap of the side key (yk_vc_on_key)
    YK_VC_HIDDEN_REPLACED, // another code took its place
    YK_VC_HIDDEN_SWIPE,    // a swipe on it, any way (round-screen-gestures.md 5.2; voice-code handoff 2.1②)
} yk_vc_hidden_t;

/** Called holding board_lock() once the code is gone: in the LVGL task (timeout, tap), or in the task that hid or
 *  replaced it (yk_vc_hide, yk_vc_on_key, yk_vc_show). */
typedef void (*yk_vc_hidden_fn)(yk_vc_hidden_t why, void *ctx);

typedef struct {
    int hide_after_s;      // 0: YK_VC_HIDE_AFTER_S; below 0: shown until yk_vc_hide()
    bool keep_on_tap;      // a tap or a swipe on the code leaves it shown (by default either hides it)
    yk_vc_hidden_fn on_hidden;
    void *ctx;
} yk_vc_show_opts_t;

/** This app's 应用码 number, built in from its app.json's voiceCode.number (or -D YK_VC_APP_NUMBER=… for a test
 *  build: the test numbers 65280..65535 too). 0: it has none. */
uint32_t yk_vc_app_number(void);

/** Shows this app's 应用码 over the whole screen: `action` 0..15 (YK_VC_ACTION_*), `value` below 2^24 (0 for page and
 *  service, days since 1970-01-01 for day, the sentence's index for ask). The bars talk YK_VC_ANIM_MS (1.2 s), then
 *  stand still on the code: the app reads it only then. A code already shown gives way to this one (REPLACED), its
 *  bars talking on from where they were. A swipe on it is its own, never the app's screen's (and hides it as a tap
 *  does). `opts` may be NULL (hides after 60 s or on a tap or a swipe). Takes board_lock()
 *  itself (it is recursive): call it from any task. ESP_ERR_NOT_SUPPORTED: app.json has no voiceCode;
 *  ESP_ERR_INVALID_ARG: action or value out of range; ESP_ERR_INVALID_STATE: no screen yet; ESP_ERR_NO_MEM. */
esp_err_t yk_vc_show(uint32_t action, uint32_t value, const yk_vc_show_opts_t *opts);

/** The same for any payload (a pairing code, a test code). Logs never carry a pairing code's 8 digits. */
esp_err_t yk_vc_show_payload(const yk_vc_payload_t *payload, const yk_vc_show_opts_t *opts);

/** Hides the code shown, if any (on_hidden gets YK_VC_HIDDEN_APP). Takes board_lock() itself. */
void yk_vc_hide(void);

/** A code is shown; settled: its bars stand still on it. */
bool yk_vc_showing(void);
bool yk_vc_settled(void);

/** For the app's yk_app_on_key() handler: while a code shows, a tap of the side key hides it (YK_VC_HIDDEN_KEY) and
 *  this returns true (return true too: the tap is used); anything else returns false. */
bool yk_vc_on_key(const yk_key_event_t *event);

/** The app ring's 「在手机上打开」 (docs/handoff/2026-11-01-voice-code.md 2.1②), for yaykit_app, which links it weakly:
 *  a firmware without this component, or an app.json without voiceCode, shows no code from the ring. This app's page
 *  code (action 0, value 0) as yk_vc_show() shows it (talks, settles, hides after 60 s, on a tap or a swipe), and
 *  `hidden` once it is gone for any reason (its yk_vc_hidden_t; the app's own code taking its place too), holding
 *  board_lock(). yk_vc_show()'s errors. The component's CMakeLists keeps it in the image (-u) even when the app
 *  never shows a code itself. */
esp_err_t yk_vc_show_ring(void (*hidden)(int why));

// ---------------------------------------------------------------- the widget

typedef struct yk_vc_view yk_vc_view_t;

/** The code centred on (cx, cy) of `parent`, its ring of radius `r` px (YK_VC_VIEW_R: 184), nothing shown yet. It
 *  draws the code's marks only: put it on pure black (YK_NIGHT). Deleted with its object (yk_vc_view_obj). Under
 *  board_lock(). NULL when out of memory. */
yk_vc_view_t *yk_vc_view_create(lv_obj_t *parent, int cx, int cy, int r);
/** Shows this payload: the bars talk, then settle (`talk` false: straight onto it). Returns 0, or -1 when the payload
 *  does not encode (nothing changes). Under board_lock(). */
int yk_vc_view_set(yk_vc_view_t *view, const yk_vc_payload_t *payload, bool talk);
/** Called in the LVGL task when the bars come to stand on the code. */
typedef void (*yk_vc_settled_fn)(yk_vc_view_t *view, void *ctx);
void yk_vc_view_on_settled(yk_vc_view_t *view, yk_vc_settled_fn fn, void *ctx);
bool yk_vc_view_settled(const yk_vc_view_t *view);
lv_obj_t *yk_vc_view_obj(yk_vc_view_t *view);
/** One short line in the code's middle in place of the Tikpal mark (the pairing page's 8 digits): 28 px
 *  (YK_VC_MIDDLE_PX), the theme's ink, kept inside the logo's circle (YK_VC_LOGO_MAX_R of the ring) where the code
 *  carries nothing and a reader looks past it. NULL: the mark again. Under board_lock(). */
void yk_vc_view_set_middle(yk_vc_view_t *view, const char *text);

// ---------------------------------------------------------------- the pairing page

/** The pairing page's code (docs/handoff/2026-11-01-voice-code.md 2.1③), for yaykit_ui's yk_ui_pair_content(), which
 *  links it weakly (a firmware without this component keeps the QR code): pure black over the whole of `screen`, the
 *  pairing code yk_vc_pair(suffix, digits) at YK_VC_VIEW_R, talking then settling, and the 8 digits in its middle as
 *  "1234 5678" for an app that does not read the code (they are typed). Clickable, with nothing bound to a tap: the
 *  caller adds that. Deleted with `screen`. NULL when `suffix` or `digits` is not what yk_vc_pair() takes, or out of
 *  memory. Logs never carry the digits. Under board_lock(). */
lv_obj_t *yk_vc_pair_layer(lv_obj_t *screen, const char *suffix, const char *digits);

#ifdef __cplusplus
}
#endif
