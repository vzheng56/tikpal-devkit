// Host tests for the pure C parts of yaykit_wifi. Run: bash firmware/components/yaykit_wifi/test/run.sh
#include <stdio.h>
#include <string.h>

#include "../yk_wifi_creds.h"
#include "../yk_wifi_list.h"
#include "../yk_wifi_timing.h"

static int failures, checks;
#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                  \
    } while (0)

#define OK(ssid, password) CHECK(yk_wifi_join_error(ssid, password) == NULL)
#define BAD(ssid, password, code) CHECK(yk_wifi_join_error(ssid, password) && !strcmp(yk_wifi_join_error(ssid, password), code))

static void test_join_arguments(void) {
    OK("Home", "12345678");
    OK("Home", NULL);  // open network
    OK("Home", "");
    OK("\xe5\xb0\x8f\xe9\xb1\xbc\xe5\xae\xb6", "pass word with spaces");  // 小鱼家
    OK("12345678901234567890123456789012", "123456789012345678901234567890123456789012345678901234567890123");
    OK("Home", "0123456789abcdefABCDEF0123456789abcdef0123456789abcdef0123456789");  // 64 hex: a raw key
    BAD(NULL, "12345678", "BAD_SSID");
    BAD("", "12345678", "BAD_SSID");
    BAD("123456789012345678901234567890123", "12345678", "BAD_SSID");
    BAD("Home", "1234567", "BAD_PASSWORD");
    BAD("Home", "0123456789abcdefABCDEF0123456789abcdef0123456789abcdef012345678g", "BAD_PASSWORD");
    BAD("Home", "line\nbreak", "BAD_PASSWORD");
    BAD("Home", "1234567890123456789012345678901234567890123456789012345678901234567", "BAD_PASSWORD");
}

#define TOOK(t, join, link, dhcp) \
    CHECK((t).done && (t).join_ms == (join) && (t).link_ms == (link) && (t).dhcp_ms == (dhcp))

static void test_join_timing(void) {
    yk_wifi_timing_t t = {0};

    // An address before any join: nothing to report.
    yk_wifi_timing_got_ip(&t, 500000);
    CHECK(!t.done);

    // A start: scan and handshake, then DHCP.
    yk_wifi_timing_begin(&t, 1000000);
    CHECK(t.running && !t.done);
    yk_wifi_timing_linked(&t, 3400000);
    CHECK(!t.done);
    yk_wifi_timing_got_ip(&t, 3900000);
    CHECK(!t.running);
    TOOK(t, 2900, 2400, 500);

    // A lease renewed on a new address is no join: what the last one took stays.
    yk_wifi_timing_got_ip(&t, 3600000000);
    TOOK(t, 2900, 2400, 500);

    // A dropped connection: what the last join took stays until this one ends.
    yk_wifi_timing_begin(&t, 10000000);
    TOOK(t, 2900, 2400, 500);
    yk_wifi_timing_linked(&t, 10800000);
    yk_wifi_timing_got_ip(&t, 11000000);
    TOOK(t, 1000, 800, 200);

    // Attempts that failed and the waits after them are part of link: they leave no mark.
    yk_wifi_timing_begin(&t, 20000000);
    yk_wifi_timing_linked(&t, 95000000);
    yk_wifi_timing_got_ip(&t, 95300000);
    TOOK(t, 75300, 75000, 300);

    // The link comes, goes before the address, and comes back: the last time counts.
    yk_wifi_timing_begin(&t, 100000000);
    yk_wifi_timing_linked(&t, 101000000);
    yk_wifi_timing_linked(&t, 104000000);
    yk_wifi_timing_got_ip(&t, 104500000);
    TOOK(t, 4500, 4000, 500);

    // New credentials while a join is under way start over: its start and its link do not count.
    yk_wifi_timing_begin(&t, 200000000);
    yk_wifi_timing_linked(&t, 201000000);
    yk_wifi_timing_begin(&t, 202000000);
    yk_wifi_timing_linked(&t, 205000000);
    yk_wifi_timing_got_ip(&t, 205250000);
    TOOK(t, 3250, 3000, 250);

    // An address with no link mark (the driver gives one, but if not): the parts still add up.
    yk_wifi_timing_begin(&t, 300000000);
    yk_wifi_timing_got_ip(&t, 300700000);
    TOOK(t, 700, 0, 700);

    // Parts cut to whole milliseconds add up to the whole.
    yk_wifi_timing_begin(&t, 400000000);
    yk_wifi_timing_linked(&t, 400001999);
    yk_wifi_timing_got_ip(&t, 400003000);
    TOOK(t, 3, 1, 2);

    // A month off the network fits.
    yk_wifi_timing_begin(&t, 1000000);
    yk_wifi_timing_linked(&t, 3000000000000);
    yk_wifi_timing_got_ip(&t, 3000000500000);
    TOOK(t, 2999999500, 2999999000, 500);
}


// ---- the remembered networks (yk_wifi_list.c)

static const uint8_t *U(const char *s) { return (const uint8_t *)s; }
static uint8_t pw[64];
static const uint8_t *P(const char *s) {
    memset(pw, 0, sizeof(pw));
    memcpy(pw, s, strlen(s) > 64 ? 64 : strlen(s));
    return pw;
}
#define JOIN(l, name, pass, ch, seen) yk_wifi_list_remember(l, U(name), strlen(name), P(pass), ch, seen, NULL)
/** The slot of the `k`th most recent, 0xff past the end. */
static int AT(const yk_wifi_list_t *l, int k) {
    uint8_t o[YK_WIFI_LIST_MAX];
    return k < yk_wifi_list_order(l, o) ? o[k] : 0xff;
}

static bool named(const yk_wifi_list_t *l, int order, const char *name) {
    uint8_t o[YK_WIFI_LIST_MAX];
    const int n = yk_wifi_list_order(l, o);
    if (order >= n) return false;
    const yk_wifi_net_t *net = &l->nets[o[order]];
    return net->ssid_len == strlen(name) && !memcmp(net->ssid, name, net->ssid_len);
}

static bool pass_is(const yk_wifi_list_t *l, const char *name, const char *pass) {
    const int slot = yk_wifi_list_find(l, U(name), strlen(name));
    return slot >= 0 && !memcmp(l->nets[slot].pass, P(pass), 64);
}

static void test_list_blob(void) {
    yk_wifi_list_t l, back;
    yk_wifi_list_clear(&l);
    CHECK(l.ver == YK_WIFI_LIST_VER && l.cur == YK_WIFI_LIST_NONE && yk_wifi_list_count(&l) == 0);
    CHECK(sizeof(l) == 524);  // 五六百字节: one NVS blob

    // Written, read back.
    JOIN(&l, "Home", "12345678", 6, 1);
    JOIN(&l, "\xd0\xa1\xd3\xe3\xbc\xd2", "gbk-name", 11, 1);  // 小鱼家 in GBK: its own bytes
    JOIN(&l, "Alex\xe2\x80\x99s iPhone", "hotspot1", 0, 0);
    CHECK(yk_wifi_list_load(&back, &l, sizeof(l)));
    CHECK(!memcmp(&back, &l, sizeof(l)));
    CHECK(yk_wifi_list_count(&back) == 3 && named(&back, 0, "Alex\xe2\x80\x99s iPhone") && named(&back, 2, "Home"));
    CHECK(pass_is(&back, "\xd0\xa1\xd3\xe3\xbc\xd2", "gbk-name"));

    // A bad blob is none, and nothing breaks.
    CHECK(!yk_wifi_list_load(&back, &l, sizeof(l) - 1) && yk_wifi_list_count(&back) == 0);
    CHECK(!yk_wifi_list_load(&back, &l, sizeof(l) + 1) && yk_wifi_list_count(&back) == 0);
    CHECK(!yk_wifi_list_load(&back, NULL, sizeof(l)) && back.cur == YK_WIFI_LIST_NONE);
    yk_wifi_list_t bad = l;
    bad.ver = 2;
    CHECK(!yk_wifi_list_load(&back, &bad, sizeof(bad)) && yk_wifi_list_count(&back) == 0);
    bad = l;
    bad.nets[1].ssid_len = 33;
    CHECK(!yk_wifi_list_load(&back, &bad, sizeof(bad)) && yk_wifi_list_count(&back) == 0);
    uint8_t junk[sizeof(l)];
    for (size_t i = 0; i < sizeof(junk); i++) junk[i] = (uint8_t)(i * 37 + 11);
    junk[0] = YK_WIFI_LIST_VER;
    yk_wifi_list_load(&back, junk, sizeof(junk));  // whatever it makes of it, it stays within the list
    CHECK(yk_wifi_list_count(&back) <= YK_WIFI_LIST_MAX);
    CHECK(back.cur == YK_WIFI_LIST_NONE || back.cur < YK_WIFI_LIST_MAX);

    // What load puts right: a channel out of range, a free slot's leftovers, a name twice, cur.
    bad = l;
    bad.nets[0].channel = 40;
    bad.cur = 4;
    memset(&bad.nets[4], 0, sizeof(bad.nets[4]));
    memcpy(bad.nets[4].pass, "leftover", 8);
    CHECK(yk_wifi_list_load(&back, &bad, sizeof(bad)));
    CHECK(back.nets[0].channel == 0 && back.nets[4].pass[0] == 0);
    CHECK(named(&back, 0, "Alex\xe2\x80\x99s iPhone") && back.cur == AT(&back, 0));
    bad = l;
    bad.nets[3] = bad.nets[0];  // Home twice: the copy is the older
    bad.nets[3].used = 0;
    CHECK(yk_wifi_list_load(&back, &bad, sizeof(bad)) && yk_wifi_list_count(&back) == 3);
}

static void test_list_add(void) {
    yk_wifi_list_t l;
    yk_wifi_list_clear(&l);
    bool changed = false;

    // A new one goes first and is the last joined.
    CHECK(yk_wifi_list_remember(&l, U("Home"), 4, P("12345678"), 6, 1, &changed) >= 0 && changed);
    CHECK(named(&l, 0, "Home") && l.cur == AT(&l, 0) && l.nets[l.cur].channel == 6);
    JOIN(&l, "Office", "officepass", 1, 1);
    CHECK(named(&l, 0, "Office") && named(&l, 1, "Home") && l.cur == AT(&l, 0));

    // The same one again, nothing new: nothing to write (a start at home writes nothing).
    yk_wifi_list_remember(&l, U("Office"), 6, P("officepass"), 1, -1, &changed);
    CHECK(!changed);
    yk_wifi_list_remember(&l, U("Office"), 6, P("officepass"), 0, -1, &changed);  // channel not known: kept
    CHECK(!changed && l.nets[l.cur].channel == 1);
    yk_wifi_list_remember(&l, U("Office"), 6, P("officepass"), 11, -1, &changed);  // moved: written
    CHECK(changed && l.nets[l.cur].channel == 11);

    // The same name: the new password over the old, first again, still one of it.
    yk_wifi_list_remember(&l, U("Home"), 4, P("newpassword"), 0, 1, &changed);
    CHECK(changed && named(&l, 0, "Home") && named(&l, 1, "Office") && yk_wifi_list_count(&l) == 2);
    CHECK(pass_is(&l, "Home", "newpassword") && l.nets[l.cur].channel == 6);
    // A shorter password leaves nothing of the longer behind.
    JOIN(&l, "Home", "short123", 0, 1);
    CHECK(pass_is(&l, "Home", "short123"));
    // A raw key: all 64 bytes, no terminator.
    const char *raw = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    JOIN(&l, "Raw", raw, 3, 1);
    CHECK(!memcmp(l.nets[l.cur].pass, raw, 64));
    // An open network.
    JOIN(&l, "Cafe", "", 0, 1);
    CHECK(pass_is(&l, "Cafe", "") && l.nets[l.cur].pass[0] == 0);

    // Full (5): the one used longest ago goes; the new one is first.
    JOIN(&l, "Fifth", "fifthpass", 0, 1);  // Office, Home, Raw, Cafe, Fifth: Office is the oldest
    CHECK(yk_wifi_list_count(&l) == 5);
    JOIN(&l, "Sixth", "sixthpass", 0, 1);
    CHECK(yk_wifi_list_count(&l) == 5 && yk_wifi_list_find(&l, U("Office"), 6) < 0 && named(&l, 0, "Sixth"));
    CHECK(named(&l, 4, "Home"));  // Home is now the oldest
    // Joining an old one again saves it from going next.
    JOIN(&l, "Home", "short123", 0, -1);
    JOIN(&l, "Seventh", "seventhp", 0, 1);
    CHECK(yk_wifi_list_find(&l, U("Home"), 4) >= 0 && yk_wifi_list_find(&l, U("Raw"), 3) < 0);

    // The one in use never goes, even when the counts say it is the oldest (a list from elsewhere).
    yk_wifi_list_t odd = l;
    for (int i = 0; i < YK_WIFI_LIST_MAX; i++) odd.nets[i].used = 100 + (uint32_t)i;
    odd.cur = 0;
    odd.nets[0].used = 1;
    char kept[33] = {0};
    memcpy(kept, odd.nets[0].ssid, odd.nets[0].ssid_len);
    const int slot = yk_wifi_list_remember(&odd, U("Eighth"), 6, P("eighthpw"), 0, 1, NULL);
    CHECK(slot == 1 && yk_wifi_list_find(&odd, U(kept), strlen(kept)) == 0);

    // A name that cannot be: nothing happens.
    yk_wifi_list_t before = l;
    CHECK(yk_wifi_list_remember(&l, U(""), 0, P("12345678"), 0, 1, &changed) == -1 && !changed);
    CHECK(yk_wifi_list_remember(&l, U("123456789012345678901234567890123"), 33, P("12345678"), 0, 1, &changed) == -1);
    CHECK(!memcmp(&before, &l, sizeof(l)));
    // A 32-byte name fits.
    CHECK(JOIN(&l, "12345678901234567890123456789012", "12345678", 0, 1) >= 0 && l.nets[l.cur].ssid_len == 32);

    // "By name": not in the scan when joined. A later join with a scan that saw it clears it, one without says nothing.
    yk_wifi_list_clear(&l);
    JOIN(&l, "Hidden", "hiddenpw", 0, 0);
    CHECK(l.nets[l.cur].flags & YK_WIFI_NET_BY_NAME);
    JOIN(&l, "Hidden", "hiddenpw", 0, -1);
    CHECK(l.nets[l.cur].flags & YK_WIFI_NET_BY_NAME);
    yk_wifi_list_remember(&l, U("Hidden"), 6, P("hiddenpw"), 0, 1, &changed);
    CHECK(changed && !(l.nets[l.cur].flags & YK_WIFI_NET_BY_NAME));
    JOIN(&l, "New", "newpass1", 0, -1);  // not known whether the scan saw it: by name, to be safe
    CHECK(l.nets[l.cur].flags & YK_WIFI_NET_BY_NAME);

    // The count never runs out: near the top it is renumbered, the order kept.
    yk_wifi_list_clear(&l);
    JOIN(&l, "A", "aaaaaaaa", 0, 1);
    JOIN(&l, "B", "bbbbbbbb", 0, 1);
    JOIN(&l, "C", "cccccccc", 0, 1);
    l.nets[yk_wifi_list_find(&l, U("C"), 1)].used = UINT32_MAX - 1;
    l.nets[yk_wifi_list_find(&l, U("B"), 1)].used = UINT32_MAX - 2;
    JOIN(&l, "A", "aaaaaaaa", 0, 1);
    CHECK(named(&l, 0, "A") && named(&l, 1, "C") && named(&l, 2, "B") && l.nets[l.cur].used < 10);
}

static void test_list_adopt(void) {
    yk_wifi_list_t l;
    uint8_t blob[sizeof(l)];

    // Only esp_wifi's own (a firmware from before the list): it becomes the first, on yk_wifi/chan, by name.
    CHECK(!yk_wifi_list_load(&l, NULL, 0));
    CHECK(yk_wifi_list_adopt(&l, U("Home"), 4, P("12345678"), 6));
    CHECK(yk_wifi_list_count(&l) == 1 && named(&l, 0, "Home") && l.cur == AT(&l, 0) && l.nets[l.cur].channel == 6);
    CHECK(pass_is(&l, "Home", "12345678") && (l.nets[l.cur].flags & YK_WIFI_NET_BY_NAME));
    // The next start: the same, nothing to write.
    memcpy(blob, &l, sizeof(l));
    CHECK(yk_wifi_list_load(&l, blob, sizeof(blob)) && !yk_wifi_list_adopt(&l, U("Home"), 4, P("12345678"), 6));
    CHECK(!yk_wifi_list_adopt(&l, U("Home"), 4, P("12345678"), 0));  // no yk_wifi/chan: the known one stays
    CHECK(l.nets[l.cur].channel == 6);

    // Neither: an empty list.
    CHECK(!yk_wifi_list_load(&l, NULL, 0) && !yk_wifi_list_adopt(&l, U(""), 0, P(""), 0));
    CHECK(yk_wifi_list_count(&l) == 0 && l.cur == YK_WIFI_LIST_NONE);

    // A list, and esp_wifi's not in it (set over USB by an app built before the list): taken in, first.
    yk_wifi_list_clear(&l);
    JOIN(&l, "Home", "12345678", 6, 1);
    JOIN(&l, "Hotspot", "hotspot1", 1, 1);
    CHECK(yk_wifi_list_adopt(&l, U("Office"), 6, P("officepass"), 11));
    CHECK(named(&l, 0, "Office") && named(&l, 1, "Hotspot") && named(&l, 2, "Home") && l.nets[l.cur].channel == 11);
    // In it, but not the last joined: it becomes the last joined.
    CHECK(yk_wifi_list_adopt(&l, U("Home"), 4, P("12345678"), 6));
    CHECK(named(&l, 0, "Home") && l.cur == AT(&l, 0) && yk_wifi_list_count(&l) == 3);
    CHECK(!(l.nets[l.cur].flags & YK_WIFI_NET_BY_NAME));  // a scan had seen it: that stays
    // In it with another password: the new one.
    CHECK(yk_wifi_list_adopt(&l, U("Home"), 4, P("changed!"), 6) && pass_is(&l, "Home", "changed!"));
}

static void test_round(void) {
    yk_wifi_list_t l;
    yk_wifi_round_t r;
    yk_wifi_list_clear(&l);
    // Most recent first: Hotspot (by name: hid when set up), Home, Office, Hidden (by name).
    JOIN(&l, "Hidden", "hiddenpw", 0, 0);
    JOIN(&l, "Office", "officepw", 1, 1);
    JOIN(&l, "Home", "homepass", 6, 1);
    JOIN(&l, "Hotspot", "hotspot1", 0, 0);
    const int hidden = yk_wifi_list_find(&l, U("Hidden"), 6), office = yk_wifi_list_find(&l, U("Office"), 6),
              home = yk_wifi_list_find(&l, U("Home"), 4), hotspot = yk_wifi_list_find(&l, U("Hotspot"), 7);
    uint8_t seen[YK_WIFI_LIST_MAX] = {0};

    // At home, the hotspot off (the last joined): Home, the only one seen, twice; then the by-name ones once each.
    seen[home] = 6;
    yk_wifi_round_plan(&r, &l, seen);
    CHECK(yk_wifi_round_next(&r, false) == home);
    CHECK(yk_wifi_round_next(&r, false) == home);
    CHECK(yk_wifi_round_next(&r, false) == hotspot);
    CHECK(yk_wifi_round_next(&r, false) == hidden);
    CHECK(yk_wifi_round_next(&r, false) == -1);
    CHECK(yk_wifi_round_next(&r, false) == -1);  // over stays over

    // Seen ones by when they were used, the last joined first when it shows.
    memset(seen, 0, sizeof(seen));
    seen[office] = 1, seen[home] = 6, seen[hotspot] = 11;
    yk_wifi_round_plan(&r, &l, seen);
    int got[8], n = 0;
    for (int s; n < 8 && (s = yk_wifi_round_next(&r, false)) >= 0;) got[n++] = s;
    CHECK(n == 7 && got[0] == hotspot && got[1] == hotspot && got[2] == home && got[3] == home && got[4] == office &&
          got[5] == office && got[6] == hidden);

    // A refused password: that network is not tried again this round.
    yk_wifi_round_plan(&r, &l, seen);
    CHECK(yk_wifi_round_next(&r, false) == hotspot);
    CHECK(yk_wifi_round_next(&r, true) == home);   // hotspot's password refused: on to Home
    CHECK(yk_wifi_round_next(&r, false) == home);
    CHECK(yk_wifi_round_next(&r, true) == office); // the first call's `refused` means nothing; this one skips nothing more
    yk_wifi_round_plan(&r, &l, seen);
    CHECK(yk_wifi_round_next(&r, true) == hotspot);  // the first call: nothing failed yet

    // Out, nothing seen: only the by-name ones, once each.
    memset(seen, 0, sizeof(seen));
    yk_wifi_round_plan(&r, &l, seen);
    CHECK(yk_wifi_round_next(&r, false) == hotspot && yk_wifi_round_next(&r, false) == hidden);
    CHECK(yk_wifi_round_next(&r, false) == -1);
    yk_wifi_round_plan(&r, &l, NULL);  // no scan at all: the same
    CHECK(yk_wifi_round_next(&r, false) == hotspot && yk_wifi_round_next(&r, false) == hidden);

    // A hotspot that comes and goes (a phone's, not by name): tried in the rounds where the scan sees it.
    yk_wifi_list_clear(&l);
    JOIN(&l, "Home", "homepass", 6, 1);
    JOIN(&l, "Phone", "phonepw1", 6, 1);
    const int phone = yk_wifi_list_find(&l, U("Phone"), 5);
    for (int round = 0; round < 6; round++) {
        memset(seen, 0, sizeof(seen));
        const bool shows = round % 3 == 2;
        if (shows) seen[phone] = 6;
        yk_wifi_round_plan(&r, &l, seen);
        const int first = yk_wifi_round_next(&r, false);
        CHECK(shows ? first == phone : first == -1);
    }

    // An empty list: an empty round.
    yk_wifi_list_clear(&l);
    yk_wifi_round_plan(&r, &l, seen);
    CHECK(yk_wifi_round_next(&r, false) == -1);
}

static void test_wait(void) {
    // 15 s, then doubling, at most 60 s (5 minutes before 11-07).
    int w = yk_wifi_list_wait_s(0);
    CHECK(w == 15);
    w = yk_wifi_list_wait_s(w);
    CHECK(w == 30);
    w = yk_wifi_list_wait_s(w);
    CHECK(w == 60);
    w = yk_wifi_list_wait_s(w);
    CHECK(w == 60);
    CHECK(yk_wifi_list_wait_s(-1) == 15 && yk_wifi_list_wait_s(45) == 60);
}

static void test_list_lines(void) {
    yk_wifi_list_t l;
    yk_wifi_list_clear(&l);
    char line[160];
    CHECK(yk_wifi_list_line(&l, 0, line, sizeof(line)) == -1);
    JOIN(&l, "Home", "secret-home-pass", 6, 1);
    JOIN(&l, "Hidden", "secret-hidden-pw", 0, 0);
    const char *raw = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    JOIN(&l, "Raw", raw, 11, 1);
    CHECK(yk_wifi_list_line(&l, 0, line, sizeof(line)) > 0);
    CHECK(!strcmp(line, "YK_WIFI_NET order=1 last=1 channel=11 by_name=0 ssid=Raw"));
    CHECK(yk_wifi_list_line(&l, 1, line, sizeof(line)) > 0);
    CHECK(!strcmp(line, "YK_WIFI_NET order=2 last=0 channel=0 by_name=1 ssid=Hidden"));
    CHECK(yk_wifi_list_line(&l, 2, line, sizeof(line)) > 0);
    CHECK(!strcmp(line, "YK_WIFI_NET order=3 last=0 channel=6 by_name=0 ssid=Home"));
    CHECK(yk_wifi_list_line(&l, 3, line, sizeof(line)) == -1 && yk_wifi_list_line(&l, -1, line, sizeof(line)) == -1);
    // No password, nor any piece of one.
    for (int k = 0; k < 3; k++) {
        yk_wifi_list_line(&l, k, line, sizeof(line));
        CHECK(!strstr(line, "secret") && !strstr(line, "0123456789abcdef") && !strstr(line, "pass"));
    }
    // A 32-byte name is whole, and a short buffer is cut, not overrun.
    JOIN(&l, "12345678901234567890123456789012", "12345678", 0, 1);
    yk_wifi_list_line(&l, 0, line, sizeof(line));
    CHECK(strstr(line, "ssid=12345678901234567890123456789012") != NULL);
    char small[12];
    memset(small, 'x', sizeof(small));
    CHECK(yk_wifi_list_line(&l, 0, small, 8) > 8 && small[7] == 0 && small[8] == 'x');
}

int main(void) {
    test_join_arguments();
    test_join_timing();
    test_list_blob();
    test_list_add();
    test_list_adopt();
    test_round();
    test_wait();
    test_list_lines();
    printf("yaykit_wifi host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
