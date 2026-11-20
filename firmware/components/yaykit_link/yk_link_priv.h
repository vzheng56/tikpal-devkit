// Inside yaykit_link: what its files share with each other (not for apps).
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** The long mode (yk_link_keep.c) runs NimBLE: the provisioning link must not start meanwhile. A flag here rather
 *  than a call, so firmware without the long mode never links yk_link_keep.c. */
extern volatile bool yk_link_keep_running;

/** The unpaired mode's link key for `app` (yk_link_keys.c): false when there is none. */
bool yk_link_keys_load(uint16_t app, uint8_t key[32], uint32_t *kid);
/** A phone came to `app` with another kid (on), or opened with this one (off): yk_link_unpaired_key_state(). */
void yk_link_keys_mismatch(uint16_t app, bool on);
/** This app's 应用码 number (yaykit_voicecode's, built in from app.json; 0: none): the unpaired mode's app. */
uint16_t yk_link_keys_app(void);
/** The unpaired mode's static random address for `app` on this device (NimBLE's byte order): the one kept, or a new
 *  one made and kept (false: not kept, used this time only). New after yk_link_keys_forget(). From an internal-RAM
 *  stack (it may write NVS). */
bool yk_link_keys_addr(uint16_t app, uint8_t addr[6]);
/** The calling task's stack is in internal RAM (NVS writes from a PSRAM stack assert). */
bool yk_link_keys_stack_ok(void);
/** Development (CONFIG_YAY_DEV): a bench's own key for this app, as if fetched (`keep key`). */
bool yk_link_keys_put(uint32_t kid, const uint8_t key[32]);
