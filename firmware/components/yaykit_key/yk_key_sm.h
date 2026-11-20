// The side key's state machine, pure C (no ESP-IDF), host tested with test/run.sh. The driver feeds it debounced
// edges, and at yk_key_sm_deadline() reads the key's level and calls yk_key_sm_align(); it never reads the clock itself.
// While the key is down the level is read every YK_KEY_SM_POLL_MS (YK_KEY_SM_POLL_HELD_MS after HOLD), so a release
// whose edge never came still ends the press (11-01: a lost release left 口语陪练 listening with no one on the key).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yk_key_types.h"

#define YK_KEY_SM_MAX_EVENTS   4
#define YK_KEY_SM_POLL_MS      25   // the key down, before HOLD: its level read at least this often
#define YK_KEY_SM_POLL_HELD_MS 100  // ... and after HOLD
#define YK_KEY_SM_CONFIRM_MS   25   // read up while down here: read again this much later before it counts

typedef struct {
    yk_key_config_t cfg;
    bool pressed;
    bool hold_sent;
    bool second;           // this press may complete a double click
    bool tap_pending;      // delay_tap: a tap waits for the double-click window to close
    int64_t press_ms;
    int64_t tap_release_ms;  // release of the last tap, -1 when no double click can follow
    int32_t tap_held_ms;     // how long the pending tap was held
    int64_t level_ms;        // the key down: when its level was last read (its DOWN, then yk_key_sm_align)
    int64_t up_seen_ms;      // the key down here but read up (its release edge lost?): when, -1 when not
} yk_key_sm_t;

/** Defaults filled in for zero fields: hold 300 ms, double 450 ms. */
void yk_key_sm_init(yk_key_sm_t *sm, const yk_key_config_t *cfg);

/** A debounced change of the key at `now_ms`. Writes up to YK_KEY_SM_MAX_EVENTS events; returns how many. A release
 *  after yk_key_sm_align() read the key up before any HOLD is no hold: at hold_ms - 1 at the latest. */
int yk_key_sm_edge(yk_key_sm_t *sm, bool pressed, int64_t now_ms, yk_key_event_t *out);

/** Time passing: emits HOLD once the key has been down hold_ms (not while a reading of it up waits to be confirmed),
 *  and a delayed TAP. Returns the event count. */
int yk_key_sm_tick(yk_key_sm_t *sm, int64_t now_ms, yk_key_event_t *out);

/** The driver read the key's level (`pressed`) at `now_ms`: aligns the state machine with it, then does what
 *  yk_key_sm_tick() does. Down here and read down: as it should be (or it bounced back). Down here but read up: read
 *  again YK_KEY_SM_CONFIRM_MS later, HOLD waiting for that; up again then, its release edge was lost, and it is
 *  released at that second reading (UP, then TAP or DOUBLE as for an edge), or, with no HOLD sent yet, at hold_ms - 1
 *  at the latest: a tap, never a hold. Up here and read down: nothing (presses come from edges only). Returns the
 *  event count. */
int yk_key_sm_align(yk_key_sm_t *sm, bool pressed, int64_t now_ms, yk_key_event_t *out);

/** When the driver must next read the level and call yk_key_sm_align(), or -1 when nothing is due. Never -1 while
 *  the key is down. */
int64_t yk_key_sm_deadline(const yk_key_sm_t *sm);
