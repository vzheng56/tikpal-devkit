#include "yk_wifi_timing.h"

void yk_wifi_timing_begin(yk_wifi_timing_t *t, int64_t now_us) {
    t->running = true;
    t->begin_us = t->link_us = now_us;
}

void yk_wifi_timing_linked(yk_wifi_timing_t *t, int64_t now_us) { t->link_us = now_us; }

void yk_wifi_timing_got_ip(yk_wifi_timing_t *t, int64_t now_us) {
    if (!t->running) return;
    t->running = false;
    t->done = true;
    t->join_ms = (now_us - t->begin_us) / 1000;
    t->link_ms = (t->link_us - t->begin_us) / 1000;
    t->dhcp_ms = t->join_ms - t->link_ms;
}
