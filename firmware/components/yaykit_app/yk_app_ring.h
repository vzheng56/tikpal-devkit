// yaykit_app's side key in an extension (P23): the app ring and 「回到 Yay 说？」 (yk_app_ring.c); and what yaykit_app.c
// and yk_app_power.c (0.1.13) ask of each other. Private.
#pragma once

#include <stdbool.h>

#include "yaykit_app.h"

/** From yk_app_start(). `active`: an extension in a slot (Yay has its own ring; the recovery has nowhere to go):
 *  the double click opens the ring and a hold may ask. Otherwise the key only goes on to the app's yk_app_on_key. */
void yk_app_ring_start(const char *slug, bool talks, bool own_double, bool active);
/** Every key event (yaykit_app's subscriber, in the key task). */
void yk_app_ring_key(const yk_key_event_t *ev);
void yk_app_ring_on_key(yk_app_key_fn fn, void *ctx);
/** Development (`yay ring <slug>|yay|paper`): a tap on the open ring or 「回到 Yay 说？」. -1: not an extension's. */
int yk_app_ring_dev_tap(const char *what);
/** Going for good (the battery's sleep, yk_app_power.c): from now on the key does nothing, in the app or here. The ring
 *  and 「回到 Yay 说？」 are closed by the caller, under the board lock (yk_apps_close, yk_apps_ask_close). */
void yk_app_ring_quit(void);

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

// ---------------------------------------------------------------- yk_app_power.c, for yaykit_app.c

/** yk_app_start(), after the config is kept: the settings read (NVS); in an extension the battery looked at before
 *  anything else, and when it is too low, deep sleep from here (does not return). */
void yk_app_power_start(void);
/** yk_app_ready(): in an extension, the battery watched from now on. */
void yk_app_power_ready(void);
/** `yay power ...` in an extension (development builds). */
int yk_app_power_dev(int argc, char **argv);
