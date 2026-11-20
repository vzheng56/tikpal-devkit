// How long the last join took: yk info's wifi.joinMs, linkMs and dhcpMs. Pure C, host-tested (test/run.sh).
//
// A join starts at yk_wifi_start(), at a connection that dropped or was left to join again, or at new credentials,
// and ends with an address; the retries and the waits between them are part of it. It splits at
// WIFI_EVENT_STA_CONNECTED, the one mark the driver gives on the way: the scan inside esp_wifi_connect() raises no
// event, so it cannot be timed apart from what follows.
//   link: the start to the link up (scan, authentication, association, 4-way handshake);
//   dhcp: the link up to the address (DHCP, its ARP probe included).
// link + dhcp = join, exactly.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool running;  // a join is under way: begin_us and link_us count
    bool done;     // a join has ended: join_ms, link_ms and dhcp_ms count, until the next one ends
    int64_t begin_us, link_us;
    int64_t join_ms, link_ms, dhcp_ms;
} yk_wifi_timing_t;

/** A join starts. One under way is dropped: new credentials start over. */
void yk_wifi_timing_begin(yk_wifi_timing_t *t, int64_t now_us);
/** The link is up. When it came and went before the address, the last time counts. */
void yk_wifi_timing_linked(yk_wifi_timing_t *t, int64_t now_us);
/** The address is there: the join ends. Does nothing with none under way (a lease renewed on a new address). */
void yk_wifi_timing_got_ip(yk_wifi_timing_t *t, int64_t now_us);
