// yaykit_app's side key in an extension (P23): the app ring and 「回到主屏幕说？」 (yk_app_ring.c); and what yaykit_app.c
// and yk_app_power.c (0.1.13) ask of each other. Private.
#pragma once

#include <stdbool.h>

#include "yaykit_app.h"

/** From yk_app_start(). `active`: an extension in a slot (Yay has its own ring; the recovery has nowhere to go):
 *  the double click opens the ring and a hold may ask. Otherwise the key only goes on to the app's yk_app_on_key.
 *  `double_tap`: yk_app_config_t.double_tap_ring (kit 1.11): with `active`, a double tap on the screen opens it too. */
void yk_app_ring_start(const char *slug, bool talks, bool own_double, bool double_tap, bool active);
/** Every key event (yaykit_app's subscriber, in the key task). */
void yk_app_ring_key(const yk_key_event_t *ev);
void yk_app_ring_on_key(yk_app_key_fn fn, void *ctx);
/** Development (`yay ring <slug>|yay|paper`): a tap on the open ring or 「回到主屏幕说？」. -1: not an extension's. */
int yk_app_ring_dev_tap(const char *what);
/** This extension has a 应用码 to show (its app.json's voiceCode, yaykit_voicecode linked, a number): the system
 *  pull-down offers 「在手机上打开」 (docs/delivery/2026-11-02-system-pulldown.md 8). */
bool yk_app_ring_has_code(void);
/** Its page code full screen (yk_vc_show_ring: action 0, value 0), the key ours and the screen on while it is up; a
 *  press, a tap, a swipe or 60 s hide it, back to the app. Nothing without a code or while leaving. Any task. */
void yk_app_ring_show_code(void);
/** yk_app_ready(): the app's screen is up; in an extension, the system pull-down's top edge is made (yk_app_sheet.c). */
void yk_app_ring_ready(void);
/** Going for good (the battery's sleep, yk_app_power.c): from now on the key does nothing, in the app or here. The ring
 *  and 「回到主屏幕说？」 are closed by the caller, under the board lock (yk_apps_close, yk_apps_ask_close). */
void yk_app_ring_quit(void);

// ---------------------------------------------------------------- the system pull-down (yk_app_sheet.c)
// docs/delivery/2026-11-02-system-pulldown.md: from an extension's top edge, yaykit_ui's yk_sheet over the app.

/** In the LVGL task (board lock held): the top edge's strip on LVGL's top layer, once. */
void yk_app_sheet_attach(const char *slug);
/** In the LVGL task: closes it if it is up (a press of the key, leaving, the battery's sleep), `by` for the log. */
void yk_app_sheet_close(const char *by);
bool yk_app_sheet_is_open(void);
/** Development (`yay sheet [open|close|edge <px>|pull <px>]`), in the LVGL task's lock (the caller takes it). */
int yk_app_sheet_dev(int argc, char **argv);
/** yk_app_ring.c, for yk_app_sheet.c (any task): may it open now (nothing of the system's up, not leaving)? When true
 *  the key is the system's from now on, and a press closes it (YK_KEYS_SHEET_CLOSE). */
bool yk_app_ring_sheet_opening(void);
/** ... it went by a touch or with its screen: the key back to the app. */
void yk_app_ring_sheet_closed(void);
/** The volume dial (yk_app_sheet.c, 0.1.20 F-1077) turned to `level` (0..10), any task: the speaker set and the level
 *  kept for every firmware (yk_app_set_volume: NVS, so in our task, whose stack is in internal RAM). Turns faster than
 *  the task are folded into the last. */
void yk_app_ring_volume(int level);
/** The folders' question (yk_app_grant.c) is to go up: true when it may (nothing of the system's up), and the key is
 *  ours until yk_app_ring_grant_closed(): its next press lets the question go (yk_app_grant_let_go_async). */
bool yk_app_ring_grant_opening(void);
void yk_app_ring_grant_closed(void);
/** In the LVGL task (lv_async_call): the side key let the question go, 「not now」. */
void yk_app_grant_let_go_async(void *arg);
/** yaykit_app.c: the app asked for LVGL's own CLICKED (yk_app_config_t.loose_taps); else yk_app_sheet_attach() turns
 *  yk_ui_taps_strict on for the app's objects. */
bool yk_app_loose_taps(void);
/** This app's slug, as yk_app_ring_start() had it ("" before). */
const char *yk_app_ring_slug(void);

// ---------------------------------------------------------------- yaykit_app.c, for yk_app_power.c

/** The app's wrap-up (yk_app_on_leave) before going, `to` for the log: once, the budget it asked for at most. */
void yk_app_run_leave(const char *to);
/** Leaving has begun (the wrap-up ran or is running): a restart is on its way. */
bool yk_app_leaving(void);
/** The boot partition on Yay (slot 1), or on the recovery when slot 1 holds no usable Yay, without a restart: the next
 *  start, whatever wakes the chip, is that one's. `home`: "yay" or "recovery". Reads the whole image (esp_ota's check):
 *  a few KB of internal stack. */
esp_err_t yk_app_boot_home_next(const char **home);
/** This firmware is an extension in an app slot (not Yay, not the recovery). */
bool yk_app_is_extension(void);

// ---------------------------------------------------------------- yk_app_power.c, for yk_app_ring.c

/** The 应用码 the app ring showed is up (voice-code handoff 2.1②): the screen stays on, apart from the app's own
 *  yk_app_keep_screen_on() (yk_keep_on_t). Logs YAY_APP screen keep_on_code=0|1. */
void yk_app_keep_screen_on_code(bool on);

// ---------------------------------------------------------------- yk_app_power.c, for yaykit_app.c

/** yk_app_start(), after the config is kept: the settings read (NVS); in an extension the battery looked at before
 *  anything else, and when it is too low, deep sleep from here (does not return). */
void yk_app_power_start(void);
/** yk_app_ready(): in an extension, the battery watched from now on. */
void yk_app_power_ready(void);
/** `yay power ...` in an extension (development builds). */
int yk_app_power_dev(int argc, char **argv);
