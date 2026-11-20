// The networks Tikpal Voice remembers, and which one it tries when (docs/delivery/2026-11-07-wifi-multi-network.md 4).
// Pure C, host-tested (test/run.sh); yaykit_wifi.c keeps the list in NVS and does the joining.
//
// Up to 5 networks. One joined goes first; with 5 already, the one used longest ago makes room (never the one in use).
// A network is remembered only once it was joined: one that never connected leaves the list as it was.
//
// At a start the last network goes first, on its last channel (yaykit_wifi.c's quick join, as before). When it cannot be
// joined, a round: one scan, then the remembered networks the scan saw, the most recently used first, each tried twice
// (once when the password is refused), then the ones marked "by name" (hidden, or a phone's hotspot that did not show
// when it was set up) once each, seen or not. A round that joins none waits 15 s, then 30 s, then 60 s between the next.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YK_WIFI_LIST_MAX 5
#define YK_WIFI_LIST_VER 1
#define YK_WIFI_LIST_NONE 0xff

#define YK_WIFI_NET_BY_NAME 0x01  // not in the scan when it was joined: a round tries it even when no scan sees it

/** One network. The NVS blob yk_wifi/nets is yk_wifi_list_t as it is: fixed widths, no pointers. */
typedef struct {
    uint8_t ssid[32];  // the name's bytes as the network gives them (GBK too); ssid_len of them count
    uint8_t pass[64];  // as esp_wifi takes it: zero-padded, all 64 bytes for a raw key; empty for an open network
    uint8_t ssid_len;  // 1..32; 0: a free slot
    uint8_t channel;   // the channel it was last joined on, 1..14; 0 not known
    uint8_t flags;     // YK_WIFI_NET_BY_NAME
    uint8_t reserved;
    uint32_t used;     // when it was last joined, as a count: higher is more recent (no clock is needed at a start)
} yk_wifi_net_t;

typedef struct {
    uint8_t ver;  // YK_WIFI_LIST_VER
    uint8_t cur;  // the slot last joined, or YK_WIFI_LIST_NONE
    uint8_t reserved[2];
    yk_wifi_net_t nets[YK_WIFI_LIST_MAX];
} yk_wifi_list_t;

/** An empty list. */
void yk_wifi_list_clear(yk_wifi_list_t *l);
/** The list from an NVS blob (which may be `l` itself). False (and an empty list) for one of another size or version, or with a slot that cannot
 *  be (a name longer than 32 bytes); a free slot's leftovers are wiped, a channel out of range becomes 0, a name that
 *  is there twice keeps its more recent one, and `cur` is made to point at the most recently used. */
bool yk_wifi_list_load(yk_wifi_list_t *l, const void *blob, size_t len);
int yk_wifi_list_count(const yk_wifi_list_t *l);
/** The slot holding the name, or -1. */
int yk_wifi_list_find(const yk_wifi_list_t *l, const uint8_t *ssid, size_t ssid_len);
/** The slots in use, the most recently used first; returns how many. */
int yk_wifi_list_order(const yk_wifi_list_t *l, uint8_t out[YK_WIFI_LIST_MAX]);

/**
 * A network was just joined: its slot (or a new one first in line) gets the name, the password and the channel
 * (0 keeps the one known), becomes the most recent and `cur`. `seen`: 1 the scan before saw it, 0 it did not (it
 * becomes "by name"), -1 not known (the flag stays; a new network is "by name"). With 5 already, the least recently
 * used other than `cur` goes. Returns the slot, -1 for a name of 0 or more than 32 bytes. `*changed` (may be NULL):
 * anything to write.
 */
int yk_wifi_list_remember(yk_wifi_list_t *l, const uint8_t *ssid, size_t ssid_len, const uint8_t pass[64],
                          uint8_t channel, int seen, bool *changed);
/**
 * esp_wifi's own stored network at a start (nvs.net80211, which a firmware built before the list may have set): when
 * it is not `cur` with that password it is remembered first, as one just joined, "by name" until a scan says
 * otherwise. `channel` is yk_wifi/chan, which belongs to it. Returns whether anything changed.
 */
bool yk_wifi_list_adopt(yk_wifi_list_t *l, const uint8_t *ssid, size_t ssid_len, const uint8_t pass[64],
                        uint8_t channel);

/**
 * One line of `wifi list` for the `order`th network (0 the most recent): its place, whether it is the last joined,
 * its channel, whether it is tried by name, and its name. Never the password. Returns snprintf's count, -1 past the
 * end of the list.
 */
int yk_wifi_list_line(const yk_wifi_list_t *l, int order, char *out, size_t size);

/** A round (see the top): the slots to try, in order, and how many times each. */
typedef struct {
    uint8_t slot[YK_WIFI_LIST_MAX];
    uint8_t times[YK_WIFI_LIST_MAX];
    uint8_t n, pos, tries;
    bool started;
} yk_wifi_round_t;

/** Plans a round from what the scan saw: `seen[slot]` the channel the slot's network was seen on, 0 not seen. */
void yk_wifi_round_plan(yk_wifi_round_t *r, const yk_wifi_list_t *l, const uint8_t seen[YK_WIFI_LIST_MAX]);
/** The slot to try now, or -1 when the round is over. Called first after the plan, then after each attempt that
 *  failed: `refused` when that attempt's password was refused (its network is not tried again in this round). */
int yk_wifi_round_next(yk_wifi_round_t *r, bool refused);

#define YK_WIFI_WAIT_FIRST_S 15
#define YK_WIFI_WAIT_MAX_S 60
/** How long to wait after a round that joined nothing, given the wait before it (0 for the first). */
int yk_wifi_list_wait_s(int before_s);

#ifdef __cplusplus
}
#endif
