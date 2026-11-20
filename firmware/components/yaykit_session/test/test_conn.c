// Host tests for the connection's timing line (yk_conn_timing) and the kept TLS ticket's record (yk_ticket).
#include <stdio.h>
#include <string.h>

#include "yk_conn_drop.h"
#include "yk_conn_timing.h"
#include "yk_ticket.h"

static int failures, checks;
#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

static void test_line(void) {
    char out[200];
    // The device service's field names; at is hello.ok's time in UTC, to the millisecond.
    yk_conn_timing_t t = {.dial_us = 2000000, .upgrade_us = 2812400, .hello_us = 2815900, .ok_us = 3456000,
                          .at_ms = 1793178753456LL, .firmware = "yay/0.1.10", .tls = YK_CONN_TLS_RESUMED};
    yk_conn_timing_line(out, sizeof(out), &t);
    CHECK(!strcmp(out, "YAY_CONN at=2026-10-28T09:12:33.456Z fw=0.1.10 app=yay tls=resumed upgradeMs=812 helloMs=3 "
                       "helloOkMs=640"));
    // An extension's, a whole handshake.
    t.firmware = "language-buddy/1.2.0", t.tls = YK_CONN_TLS_FULL, t.at_ms = 1793232000000LL;  // 2026-10-29 00:00 UTC
    yk_conn_timing_line(out, sizeof(out), &t);
    CHECK(strstr(out, "at=2026-10-29T00:00:00.000Z fw=1.2.0 app=language-buddy tls=full ") != NULL);
    // Leap day, and the epoch.
    t.at_ms = 1709210096007LL;  // 2024-02-29 12:34:56.007
    yk_conn_timing_line(out, sizeof(out), &t);
    CHECK(strstr(out, "at=2024-02-29T12:34:56.007Z ") != NULL);
    t.at_ms = 1000;
    yk_conn_timing_line(out, sizeof(out), &t);
    CHECK(strstr(out, "at=1970-01-01T00:00:01.000Z ") != NULL);
    // Not known: "-"; a moment not seen or out of order: "-".
    t.at_ms = 0, t.upgrade_us = 0, t.tls = YK_CONN_TLS_NONE, t.firmware = "yay";
    yk_conn_timing_line(out, sizeof(out), &t);
    CHECK(!strcmp(out, "YAY_CONN at=- fw=- app=yay tls=none upgradeMs=- helloMs=- helloOkMs=640"));
    t.firmware = NULL, t.ok_us = t.hello_us - 1;
    yk_conn_timing_line(out, sizeof(out), &t);
    CHECK(strstr(out, "fw=- app=- ") && strstr(out, "helloOkMs=-"));
    // Short buffers are cut, never overrun.
    char small[16];
    memset(small, 'x', sizeof(small));
    yk_conn_timing_line(small, 10, &t);
    CHECK(strlen(small) == 9 && small[10] == 'x');
    CHECK(!strcmp(yk_conn_tls_name(YK_CONN_TLS_RESUMED), "resumed") && !strcmp(yk_conn_tls_name(99), "none"));
}

static void test_ticket(void) {
    uint8_t area[YK_TICKET_AREA];
    memset(area, 0xA5, sizeof(area));  // RTC memory after a power cut: anything
    const uint8_t *s;
    size_t n;
    uint32_t age;
    CHECK(yk_ticket_get(area, sizeof(area), "device.yaykit.io", 5000000, &s, &n, &age) == YK_TICKET_DAMAGED && !s);
    yk_ticket_clear(area, sizeof(area));
    CHECK(yk_ticket_get(area, sizeof(area), "device.yaykit.io", 5000000, &s, &n, &age) == YK_TICKET_NONE);

    uint8_t session[300];
    for (size_t i = 0; i < sizeof(session); i++) session[i] = (uint8_t)(i * 7);
    // Kept at hello.ok, 1.5 s old; read back 4 s later (a restart): 5.5 s old, the same bytes.
    CHECK(yk_ticket_put(area, sizeof(area), "device.yaykit.io", 10000000, 1500, 86400, session, sizeof(session)));
    CHECK(yk_ticket_get(area, sizeof(area), "device.yaykit.io", 14000000, &s, &n, &age) == YK_TICKET_OK);
    CHECK(n == sizeof(session) && !memcmp(s, session, n) && age == 5500 && s == area + 32);
    // Another server's (a development one): not offered.
    CHECK(yk_ticket_get(area, sizeof(area), "192.168.1.20", 14000000, &s, &n, &age) == YK_TICKET_OTHER_HOST && !s);
    // A day: still; a day and a millisecond: old. The RTC clock behind it: a power cut, old.
    const uint64_t day_us = (uint64_t)YK_TICKET_MAX_AGE_MS * 1000;
    CHECK(yk_ticket_get(area, sizeof(area), "device.yaykit.io", 10000000 + day_us - 1500000, &s, &n, &age) == YK_TICKET_OK);
    CHECK(yk_ticket_get(area, sizeof(area), "device.yaykit.io", 10000000 + day_us - 1500000 + 1000, &s, &n, &age) ==
          YK_TICKET_OLD);
    CHECK(yk_ticket_get(area, sizeof(area), "device.yaykit.io", 9000000, &s, &n, &age) == YK_TICKET_OLD);
    // A flipped bit anywhere: damaged.
    area[40] ^= 1;
    CHECK(yk_ticket_get(area, sizeof(area), "device.yaykit.io", 14000000, &s, &n, &age) == YK_TICKET_DAMAGED);
    area[40] ^= 1;
    area[12] ^= 0x80;  // the age in the header
    CHECK(yk_ticket_get(area, sizeof(area), "device.yaykit.io", 14000000, &s, &n, &age) == YK_TICKET_DAMAGED);
    area[12] ^= 0x80;
    CHECK(yk_ticket_get(area, sizeof(area), "device.yaykit.io", 14000000, &s, &n, &age) == YK_TICKET_OK);
    // Used once: cleared, nothing more.
    yk_ticket_clear(area, sizeof(area));
    CHECK(yk_ticket_get(area, sizeof(area), "device.yaykit.io", 14000000, &s, &n, &age) == YK_TICKET_NONE);
    // Too big for the area (a TLS 1.2 session carries the server's certificate): not kept, and the old one goes.
    CHECK(yk_ticket_put(area, sizeof(area), "device.yaykit.io", 10000000, 0, 0, session, sizeof(session)));
    static uint8_t big[YK_TICKET_AREA];
    CHECK(!yk_ticket_put(area, sizeof(area), "device.yaykit.io", 10000000, 0, 0, big, sizeof(big)));
    CHECK(yk_ticket_get(area, sizeof(area), "device.yaykit.io", 10000000, &s, &n, &age) == YK_TICKET_NONE);
    CHECK(!yk_ticket_put(area, sizeof(area), "device.yaykit.io", 10000000, 0, 0, session, 0));
    CHECK(!yk_ticket_put(NULL, 0, "device.yaykit.io", 0, 0, 0, session, 1));
    CHECK(yk_ticket_get(NULL, 0, "x", 0, &s, &n, &age) == YK_TICKET_NONE);
    // Exactly full fits.
    CHECK(yk_ticket_put(area, sizeof(area), "h", 1, 0, 0, big, sizeof(big) - 32));
    CHECK(yk_ticket_get(area, sizeof(area), "h", 1, &s, &n, &age) == YK_TICKET_OK && n == sizeof(big) - 32);
    // Its own lifetime when shorter than a day (an hour here); none said: a day; longer than a day: a day.
    CHECK(yk_ticket_put(area, sizeof(area), "h", 0, 0, 3600, session, 10));
    CHECK(yk_ticket_get(area, sizeof(area), "h", 3600ULL * 1000000, &s, &n, &age) == YK_TICKET_OK);
    CHECK(yk_ticket_get(area, sizeof(area), "h", 3600ULL * 1000000 + 1000, &s, &n, &age) == YK_TICKET_OLD);
    CHECK(yk_ticket_put(area, sizeof(area), "h", 0, 0, 0, session, 10));
    CHECK(yk_ticket_get(area, sizeof(area), "h", day_us, &s, &n, &age) == YK_TICKET_OK);
    CHECK(yk_ticket_get(area, sizeof(area), "h", day_us + 1000, &s, &n, &age) == YK_TICKET_OLD);
    CHECK(yk_ticket_put(area, sizeof(area), "h", 0, 0, 7 * 86400, session, 10));
    CHECK(yk_ticket_get(area, sizeof(area), "h", day_us + 1000, &s, &n, &age) == YK_TICKET_OLD);
    CHECK(!strcmp(yk_ticket_why_name(YK_TICKET_OTHER_HOST), "other_host"));
}

static void test_url(void) {
    char host[64];
    const char *path;
    CHECK(yk_ticket_url("wss://device.yaykit.io/device/v1/session", host, sizeof(host), &path));
    CHECK(!strcmp(host, "device.yaykit.io") && !strcmp(path, "/device/v1/session"));
    CHECK(yk_ticket_url("wss://staging.example.test:8443/device/v1/session?x=1", host, sizeof(host), &path));
    CHECK(!strcmp(host, "staging.example.test") && !strcmp(path, "/device/v1/session?x=1"));
    CHECK(yk_ticket_url("wss://h", host, sizeof(host), &path) && !strcmp(host, "h") && !strcmp(path, "/"));
    CHECK(!yk_ticket_url("ws://192.168.1.20:8787/device/v1/session", host, sizeof(host), &path) && !host[0]);
    CHECK(!yk_ticket_url("usb", host, sizeof(host), &path) && !strcmp(path, "/"));
    CHECK(!yk_ticket_url("wss:///x", host, sizeof(host), &path));
    CHECK(!yk_ticket_url("wss://a-very-long-host-name.example", host, 8, &path));
    CHECK(!yk_ticket_url(NULL, host, sizeof(host), &path));
}

/** How the session went away and came back (0.1.20 4.8; the report's net): the cause, why it stayed down, how long. */
static void test_drop(void) {
    // The cause, from what was seen.
    yk_drop_seen_t s = {.ip = false, .close_code = 4001};
    CHECK(yk_drop_classify(&s) == YK_DROP_WIFI);  // off the router: nothing else can be told
    s = (yk_drop_seen_t){.ip = true, .close_code = 4001};
    CHECK(yk_drop_classify(&s) == YK_DROP_CLOSED);
    s = (yk_drop_seen_t){.ip = true, .tls_error = YK_DROP_TLS_CANNOT_RESOLVE};
    CHECK(yk_drop_classify(&s) == YK_DROP_NO_INTERNET);
    s.tls_error = YK_DROP_TLS_FAILED_CONNECT;
    CHECK(yk_drop_classify(&s) == YK_DROP_NO_INTERNET);
    s.tls_error = YK_DROP_TLS_CONNECTION_TIMEOUT;
    CHECK(yk_drop_classify(&s) == YK_DROP_TIMEOUT);
    s.tls_error = YK_DROP_TLS_BASE + 0x0E;  // a handshake that failed
    CHECK(yk_drop_classify(&s) == YK_DROP_SERVER);
    s = (yk_drop_seen_t){.ip = true, .handshake = true};
    CHECK(yk_drop_classify(&s) == YK_DROP_SERVER);
    s = (yk_drop_seen_t){.ip = true, .pong_timeout = true};
    CHECK(yk_drop_classify(&s) == YK_DROP_TIMEOUT);
    s = (yk_drop_seen_t){.ip = true};
    CHECK(yk_drop_classify(&s) == YK_DROP_OTHER);
    CHECK(!strcmp(yk_drop_cause_word(YK_DROP_NO_INTERNET), "no_internet") && !strcmp(yk_drop_cause_word(YK_DROP_WIFI), "wifi"));
    // Pings stop (the router went): the cause is told by the tries that fail while down; back after three.
    yk_drop_t d = {0};
    CHECK(!d.have);
    yk_drop_went(&d, YK_DROP_TIMEOUT, 15, 0, 10000, 1763100000);
    CHECK(d.have && d.down && d.drops == 1 && d.cause == YK_DROP_TIMEOUT && d.wifi_reason == -1 && d.down_s == -1);
    yk_drop_try(&d, YK_DROP_NONE, -1);
    yk_drop_try(&d, YK_DROP_WIFI, 200);  // no address: the router is gone
    CHECK(d.cause == YK_DROP_WIFI && d.wifi_reason == 200 && d.tries == 1);
    yk_drop_try(&d, YK_DROP_NONE, -1);
    yk_drop_try(&d, YK_DROP_NO_INTERNET, -1);  // a plainer one already told: kept
    CHECK(d.cause == YK_DROP_WIFI);
    yk_drop_try(&d, YK_DROP_NONE, -1);
    yk_drop_back(&d, 48400);
    CHECK(!d.down && d.down_s == 38 && d.tries == 3 && d.at_unix_s == 1763100000);
    yk_drop_back(&d, 90000);  // once
    CHECK(d.down_s == 38);
    // Said in a report: the count goes, the latest stays; a newer drop replaces it.
    yk_drop_taken(&d, 1);
    CHECK(d.drops == 0 && d.have && d.cause == YK_DROP_WIFI);
    yk_drop_went(&d, YK_DROP_CLOSED, -1, 4001, 100000, 0);
    CHECK(d.drops == 1 && d.cause == YK_DROP_CLOSED && d.close_code == 4001 && d.at_unix_s == 0);
    yk_drop_try(&d, YK_DROP_NONE, -1);
    yk_drop_try(&d, YK_DROP_WIFI, 2);  // the server closed it: that is why, whatever came after
    CHECK(d.cause == YK_DROP_CLOSED && d.wifi_reason == -1);
    yk_drop_back(&d, 100200);  // under a second: 0 s, one try
    CHECK(d.down_s == 0 && d.tries == 1);
    // Two drops before the report went: both counted; a report that went before the second leaves one.
    yk_drop_went(&d, YK_DROP_OTHER, -1, 0, 200000, 0);
    CHECK(d.drops == 2);
    yk_drop_taken(&d, 1);
    CHECK(d.drops == 1);
    yk_drop_taken(&d, 5);
    CHECK(d.drops == 0);
    // Tries and back without a drop known: nothing.
    yk_drop_t none = {0};
    yk_drop_try(&none, YK_DROP_WIFI, 3);
    yk_drop_back(&none, 5000);
    CHECK(!none.have && none.cause == YK_DROP_NONE);
}

int main(void) {
    test_drop();
    test_line();
    test_ticket();
    test_url();
    printf("yaykit_session timing and ticket host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
