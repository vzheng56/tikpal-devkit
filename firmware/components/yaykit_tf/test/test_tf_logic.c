// yk_tf_logic.c: paths, quota, memory, holders, pauses, failures, and the restart after a timeout, on a stand-in for the
// card that behaves as the sample card did on 11-06.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "yk_tf_logic.h"

#define RAM_PLENTY 80000

// ---------------------------------------------------------------- paths

static const char *const BAD_RELS[] = {
    // up and out
    "..", "../", "../x", "../../etc", "a/..", "a/../b", "a/../../b", "a/b/..", "./..",
    // same place, other spellings
    ".", "./", "./x", "a/.", "a/./b", "...", "..../x", "a/...", ". .", ".. ", " ..", "..\t", "a/.. /b",
    // absolute, drives, the other separator
    "/", "/x", "//x", "/sdcard/x", "/sdcard/.tikpal/apps", "0:", "0:/x", "1:x", "C:\\x", "\\x", "a\\b", "..\\x", "a\\..\\b",
    // empty parts
    "", "a//b", "a/", "a/b/", "/a", "//", "///",
    // percent encoding, once and twice
    "%2e%2e", "%2E%2E/x", "..%2f", "..%2Fx", "%2e%2e%2fx", "a%2f..%2fb", "%252e%252e", "..%5cx", "%00", "a%00b", "%2f",
    // overlong UTF-8 for . and / , and full-width ones
    "\xC0\xAE\xC0\xAE", "\xC0\xAE\xC0\xAE/x", "\xC0\xAF", "a\xC0\xAF..", "\xE0\x80\xAE\xE0\x80\xAE", "\xF0\x80\x80\xAE",
    "\xEF\xBC\x8E\xEF\xBC\x8E", "\xEF\xBC\x8Fx", "\xEF\xBC\xBCx", "\xE2\x80\xA4\xE2\x80\xA4", "\xE3\x80\x82\xE3\x80\x82",
    // bytes that are not text, controls, a name in another script
    "a\x01", "a\x7F", "\xFF", "\x80", "a\tb", "a\nb", "a\rb", "雨声", "a/雨.pcm",
    // what FAT folds: trailing dots and spaces, short names, wildcards
    "x.", "x..", "a./b", "x ", "x /y", " x", "a b", "TIKPAL~1", "PROGRA~1/x", "a*", "a?", "a<b", "a>b", "a|b", "a\"b", "a:b",
    // leading dots: nothing an app needs, and `.tikpal` looks like the system's
    ".tikpal", ".x/y", "a/.tikpal", "..x", "..x/y",
};

static const char *const GOOD_RELS[] = {
    "a", "a.pcm", "A.PCM", "rain-01.pcm", "a_b", "sounds/rain.pcm", "a/b/c", "x.y.z", "a..b", "a..b/c", "1", "0-0", "a-",
    "a_", "-a", "_a", "a/b/c/d/e/f/g/h",
};

static void test_rel(void) {
    for (size_t i = 0; i < sizeof(BAD_RELS) / sizeof(BAD_RELS[0]); i++) {
        if (yk_tf_rel_ok(BAD_RELS[i])) {
            printf("should be refused: [%s]\n", BAD_RELS[i]);
            assert(0);
        }
    }
    for (size_t i = 0; i < sizeof(GOOD_RELS) / sizeof(GOOD_RELS[0]); i++) {
        if (!yk_tf_rel_ok(GOOD_RELS[i])) {
            printf("should pass: [%s]\n", GOOD_RELS[i]);
            assert(0);
        }
    }
    assert(!yk_tf_rel_ok(NULL));
    assert(!yk_tf_rel_ok("a/b/c/d/e/f/g/h/i"));  // nine deep
    char long_rel[YK_TF_REL_MAX + 2];
    memset(long_rel, 'a', sizeof(long_rel));
    long_rel[YK_TF_REL_MAX] = 0;
    assert(yk_tf_rel_ok(long_rel));
    long_rel[YK_TF_REL_MAX] = 'a';
    long_rel[YK_TF_REL_MAX + 1] = 0;
    assert(!yk_tf_rel_ok(long_rel));
}

/** Every string of up to 6 tokens of the dangerous pieces: none may pass and still hold a `..` part. */
static void test_rel_exhaustive(void) {
    static const char *const T[] = {".", "..", "/", "a", "\\", "%2e", " "};
    enum { N = sizeof(T) / sizeof(T[0]), LEN = 6 };
    int pick[LEN];
    int passed = 0;
    for (int len = 1; len <= LEN; len++) {
        long total = 1;
        for (int i = 0; i < len; i++) total *= N;
        for (long v = 0; v < total; v++) {
            char rel[32] = "";
            long x = v;
            for (int i = 0; i < len; i++, x /= N) pick[i] = (int)(x % N), strcat(rel, T[pick[i]]);
            if (!yk_tf_rel_ok(rel)) continue;
            passed++;
            char copy[32];
            strcpy(copy, rel);
            assert(copy[0] != '/');
            for (const char *part = strtok(copy, "/"); part; part = strtok(NULL, "/")) {
                assert(strcmp(part, "..") && strcmp(part, "."));
                assert(part[0] != '.' && part[strlen(part) - 1] != '.');
            }
            assert(!strchr(rel, '\\') && !strchr(rel, '%') && !strchr(rel, ' '));
        }
    }
    assert(passed > 0);
}

static void test_join(void) {
    char out[32];
    assert(yk_tf_join("/sdcard/apps/x", "a/b.pcm", out, sizeof(out)) == YK_TF_JOIN_OK);
    assert(!strcmp(out, "/sdcard/apps/x/a/b.pcm"));
    strcpy(out, "stale");
    assert(yk_tf_join("/sdcard/apps/x", "../y", out, sizeof(out)) == YK_TF_JOIN_BAD && !out[0]);
    strcpy(out, "stale");
    assert(yk_tf_join("/sdcard/apps/x", NULL, out, sizeof(out)) == YK_TF_JOIN_BAD && !out[0]);
    assert(yk_tf_join(NULL, "a", out, sizeof(out)) == YK_TF_JOIN_BAD && !out[0]);
    assert(yk_tf_join("", "a", out, sizeof(out)) == YK_TF_JOIN_BAD && !out[0]);
    // Exactly enough, and one byte short: never a shorter path that exists for something else.
    const char *full = "/sdcard/apps/x/abc";
    char fit[32];
    assert(yk_tf_join("/sdcard/apps/x", "abc", fit, strlen(full) + 1) == YK_TF_JOIN_OK && !strcmp(fit, full));
    strcpy(fit, "stale");
    assert(yk_tf_join("/sdcard/apps/x", "abc", fit, strlen(full)) == YK_TF_JOIN_SMALL && !fit[0]);
    assert(yk_tf_join("/sdcard/apps/x", "abc", NULL, 8) == YK_TF_JOIN_SMALL);
    assert(yk_tf_join("/sdcard/apps/x", "abc", fit, 0) == YK_TF_JOIN_SMALL);
}

static void test_names(void) {
    assert(yk_tf_name_ok("language-buddy") && yk_tf_name_ok("yay") && yk_tf_name_ok("a") && yk_tf_name_ok("0"));
    assert(yk_tf_name_ok("abcdefghijklmnopqrstuvwxyz01234"));  // 31
    assert(!yk_tf_name_ok("abcdefghijklmnopqrstuvwxyz012345"));
    assert(!yk_tf_name_ok(NULL) && !yk_tf_name_ok("") && !yk_tf_name_ok("-a") && !yk_tf_name_ok("A") && !yk_tf_name_ok("a_b"));
    assert(!yk_tf_name_ok("..") && !yk_tf_name_ok("a/b") && !yk_tf_name_ok(".tikpal") && !yk_tf_name_ok("a b"));

    char slug[32];
    assert(yk_tf_slug_from_project("yaykit_language_buddy", slug, sizeof(slug)) && !strcmp(slug, "language-buddy"));
    assert(yk_tf_slug_from_project("yaykit_yay", slug, sizeof(slug)) && !strcmp(slug, "yay"));
    assert(yk_tf_slug_from_project("yaykit_shared_test", slug, sizeof(slug)) && !strcmp(slug, "shared-test"));
    static const char *const NOT_APPS[] = {"yay", "yaykit", "yaykit_", "other_app", "yaykit_Yay", "yaykit_a.b", "yaykit_a/b",
                                           "yaykit_..", "yaykit__a", "xyaykit_a"};
    for (size_t i = 0; i < sizeof(NOT_APPS) / sizeof(NOT_APPS[0]); i++) {
        strcpy(slug, "stale");
        assert(!yk_tf_slug_from_project(NOT_APPS[i], slug, sizeof(slug)) && !slug[0]);
    }
    assert(!yk_tf_slug_from_project(NULL, slug, sizeof(slug)));
    char small[4];
    assert(!yk_tf_slug_from_project("yaykit_long_one", small, sizeof(small)) && !small[0]);
    assert(yk_tf_slug_from_project("yaykit_abc", small, sizeof(small)) && !strcmp(small, "abc"));
}

// ---------------------------------------------------------------- how much

static void test_quota(void) {
    assert(yk_tf_quota_for(15634268160ULL) == 1ULL << 30);  // the sample card
    assert(yk_tf_quota_for(8ULL << 30) == 1ULL << 30);
    assert(yk_tf_quota_for(4ULL << 30) == 512ULL << 20);
    assert(yk_tf_quota_for(0) == 0 && yk_tf_quota_for(7) == 0);
    assert(yk_tf_quota_for(UINT64_MAX) == 1ULL << 30);

    const uint32_t cluster = 32 * 1024;
    assert(yk_tf_used_add(0, 0, cluster) == 0);
    assert(yk_tf_used_add(0, 1, cluster) == cluster);
    assert(yk_tf_used_add(0, cluster, cluster) == cluster);
    assert(yk_tf_used_add(0, cluster + 1, cluster) == 2 * cluster);
    assert(yk_tf_used_add(100, 1024, cluster) == 100 + cluster);
    assert(yk_tf_used_add(0, 1000, 0) == 1000);  // no cluster size known: the bytes
    assert(yk_tf_used_add(UINT64_MAX - 10, 1000, 0) == UINT64_MAX);
    assert(yk_tf_used_add(UINT64_MAX, 1, cluster) == UINT64_MAX);
    assert(yk_tf_used_add(0, UINT64_MAX, cluster) == UINT64_MAX);
}

// ---------------------------------------------------------------- memory and the controller

/** What the development doc and the 11-06 measurements say, as numbers: these do not move by editing a #define. */
static void test_agreed_numbers(void) {
    assert(YK_TF_FAILS_MAX == 3);
    assert(YK_TF_REST_MS == 86400000ULL);
    assert(YK_TF_RAM_BATCH == 50000);
    assert(YK_TF_RAM_MOUNT >= 41276);
    assert(YK_TF_BOUNCE_BATCH * 512 == 4096 && YK_TF_BOUNCE_SECTOR * 512 == 512);
    assert(YK_TF_DATA_TIMEOUT_MS > 656 && YK_TF_DATA_TIMEOUT_MS <= 838);
    assert(!strcmp(YK_TF_BASE, "/sdcard") && !strcmp(YK_TF_APPS_ROOT, "/sdcard/apps"));
    assert(!strcmp(YK_TF_SYS_ROOT, "/sdcard/.tikpal"));
    assert(sizeof(YK_TF_SYS_ROOT "/") + YK_TF_NAME_MAX <= YK_TF_DIR_SIZE);
}

static void test_memory(void) {
    assert(yk_tf_bounce_sectors(RAM_PLENTY) == 8);
    assert(yk_tf_bounce_sectors(50000) == 8);
    assert(yk_tf_bounce_sectors(49999) == 1);
    assert(yk_tf_bounce_sectors(0) == 1);

    // The line holds at the peak of either mode (measured: 4,860 and 1,276).
    assert(YK_TF_RAM_BATCH - 4860 >= 40000);
    assert(YK_TF_RAM_MOUNT - 1276 >= 40000);

    yk_tf_core_t c = {0};
    assert(yk_tf_core_acquire(&c, 0, false, YK_TF_RAM_MOUNT - 1) == YK_TF_NO_RAM);
    assert(!c.mounting && !c.refs && !c.fails);  // not the card's fault
    assert(yk_tf_core_acquire(&c, 0, false, YK_TF_RAM_MOUNT) == YK_TF_GO_MOUNT);
    yk_tf_core_mounted(&c, true, 0);
    assert(yk_tf_core_acquire(&c, 0, false, 100) == YK_TF_GO_HOLD);  // a second holder costs nothing
}

static void test_timeout(void) {
    assert(yk_tf_timeout_cycles(20000) == 838u * 20000u);
    assert(yk_tf_timeout_cycles(20000) <= YK_TF_TIMEOUT_REG_MAX);
    assert(yk_tf_timeout_cycles(20000) / 20000 > 656);       // the slowest read seen
    assert(yk_tf_timeout_cycles(40000) == YK_TF_TIMEOUT_REG_MAX);
    assert(yk_tf_timeout_cycles(400) == 838u * 400u);
    assert(yk_tf_timeout_cycles(0xFFFFFFFF) == YK_TF_TIMEOUT_REG_MAX);  // no wrap in 32 bits
}

// ---------------------------------------------------------------- holders

static void hold(yk_tf_core_t *c) {
    assert(yk_tf_core_acquire(c, 0, false, RAM_PLENTY) == YK_TF_GO_MOUNT);
    yk_tf_core_mounted(c, true, 0);
}

static void test_refs(void) {
    yk_tf_core_t c = {0};
    assert(yk_tf_core_state(&c, 0) == YK_TF_OFF && yk_tf_core_free(&c));
    assert(!yk_tf_core_release(&c));  // nothing to release
    assert(c.refs == 0);
    assert(yk_tf_core_acquire(&c, 0, false, RAM_PLENTY) == YK_TF_GO_MOUNT);
    assert(!yk_tf_core_free(&c) && !c.refs);  // mounting: nobody holds it yet, but it is not free
    yk_tf_core_mounted(&c, true, 0);
    assert(c.refs == 1 && yk_tf_core_state(&c, 0) == YK_TF_READY);
    assert(yk_tf_core_acquire(&c, 0, false, RAM_PLENTY) == YK_TF_GO_HOLD);
    assert(yk_tf_core_acquire(&c, 0, false, RAM_PLENTY) == YK_TF_GO_HOLD);
    assert(c.refs == 3);
    assert(!yk_tf_core_release(&c) && !yk_tf_core_release(&c));
    assert(c.mounted && c.refs == 1);
    assert(yk_tf_core_release(&c));  // the last one unmounts
    assert(!c.mounted && yk_tf_core_state(&c, 0) == YK_TF_OFF);
    assert(!yk_tf_core_release(&c));  // one too many
    assert(c.refs == 0);
    assert(yk_tf_core_acquire(&c, 0, false, RAM_PLENTY) == YK_TF_GO_MOUNT);  // mounts again
    yk_tf_core_mounted(&c, false, 0);
    assert(c.refs == 0 && !c.mounted && yk_tf_core_free(&c));  // failed: no holder to release
}

static void test_pause(void) {
    yk_tf_core_t c = {0};
    hold(&c);
    yk_tf_core_pause(&c);
    assert(!yk_tf_core_free(&c) && yk_tf_core_stop_asked(&c));
    assert(yk_tf_core_state(&c, 0) == YK_TF_PAUSED);
    assert(yk_tf_core_acquire(&c, 0, false, RAM_PLENTY) == YK_TF_NO_PAUSED);  // no new holders, even with it mounted
    assert(c.refs == 1);
    assert(yk_tf_core_release(&c));  // the holder lets go: it unmounts
    assert(yk_tf_core_free(&c) && yk_tf_core_state(&c, 0) == YK_TF_PAUSED);
    assert(yk_tf_core_acquire(&c, 0, false, RAM_PLENTY) == YK_TF_NO_PAUSED);
    yk_tf_core_pause(&c);  // two callers
    yk_tf_core_resume(&c);
    assert(yk_tf_core_acquire(&c, 0, false, RAM_PLENTY) == YK_TF_NO_PAUSED);
    yk_tf_core_resume(&c);
    assert(!yk_tf_core_stop_asked(&c));
    yk_tf_core_resume(&c);  // one too many
    assert(c.pauses == 0);
    assert(yk_tf_core_acquire(&c, 0, false, RAM_PLENTY) == YK_TF_GO_MOUNT);
    yk_tf_core_mounted(&c, true, 0);
    yk_tf_core_release(&c);

    // Not the card's fault: pausing and Bluetooth count against nothing.
    assert(yk_tf_core_acquire(&c, 0, true, RAM_PLENTY) == YK_TF_NO_BLUETOOTH);
    assert(!c.fails && !c.mounted && !c.mounting);
    hold(&c);
    assert(yk_tf_core_acquire(&c, 0, true, RAM_PLENTY) == YK_TF_NO_BLUETOOTH);  // Bluetooth on: not even a second holder
    assert(c.refs == 1);
}

// ---------------------------------------------------------------- failures

static void test_backoff(void) {
    const uint64_t day = YK_TF_REST_MS;
    assert(day == 86400000ULL);
    yk_tf_core_t c = {0};
    uint64_t now = 1000;
    for (int i = 0; i < YK_TF_FAILS_MAX; i++) {
        assert(yk_tf_core_state(&c, now) == YK_TF_OFF);
        assert(yk_tf_core_acquire(&c, now, false, RAM_PLENTY) == YK_TF_GO_MOUNT);
        yk_tf_core_mounted(&c, false, now);
    }
    assert(yk_tf_core_state(&c, now) == YK_TF_RESTING);
    assert(yk_tf_core_acquire(&c, now, false, RAM_PLENTY) == YK_TF_NO_RESTING);
    assert(yk_tf_core_acquire(&c, now + day - 1, false, RAM_PLENTY) == YK_TF_NO_RESTING);
    assert(yk_tf_core_state(&c, now + day - 1) == YK_TF_RESTING);
    assert(yk_tf_core_state(&c, now + day) == YK_TF_OFF);
    // A day later: one try, and failing it rests the card for another day.
    now += day;
    assert(yk_tf_core_acquire(&c, now, false, RAM_PLENTY) == YK_TF_GO_MOUNT);
    yk_tf_core_mounted(&c, false, now);
    assert(yk_tf_core_acquire(&c, now, false, RAM_PLENTY) == YK_TF_NO_RESTING);
    assert(yk_tf_core_acquire(&c, now + day - 1, false, RAM_PLENTY) == YK_TF_NO_RESTING);
    // The next try works: everything forgiven.
    now += day;
    assert(yk_tf_core_acquire(&c, now, false, RAM_PLENTY) == YK_TF_GO_MOUNT);
    yk_tf_core_mounted(&c, true, now);
    assert(yk_tf_core_release(&c));
    assert(!c.fails && !c.resting);
    for (int i = 0; i < YK_TF_FAILS_MAX - 1; i++) {  // the full three again
        assert(yk_tf_core_acquire(&c, now, false, RAM_PLENTY) == YK_TF_GO_MOUNT);
        yk_tf_core_mounted(&c, false, now);
    }
    assert(!c.resting);
}

/** What a report says of the card: read from the core alone, so nothing here needs the card. */
static void test_known(void) {
    yk_tf_core_t c = {0};
    yk_tf_known_t k;
    yk_tf_core_known(&c, 0, 0, 0, &k);
    assert(k.state == YK_TF_OFF && !k.opened && !k.fails && !k.not_fat && !k.used && !k.total);

    // In use, and after: it worked, whether or not someone holds it now.
    assert(yk_tf_core_acquire(&c, 0, false, RAM_PLENTY) == YK_TF_GO_MOUNT);
    yk_tf_core_known(&c, 0, 0, 0, &k);
    assert(!k.opened);
    yk_tf_core_mounted(&c, true, 0);
    yk_tf_core_known(&c, 0, 0, 0, &k);
    assert(k.state == YK_TF_READY && k.opened);
    assert(yk_tf_core_release(&c));
    yk_tf_core_known(&c, 0, 0, 0, &k);
    assert(k.state == YK_TF_OFF && k.opened && !k.fails);

    // Failing, then resting: the same state word yk_tf_state() gives, and the count.
    yk_tf_core_t bad = {0};
    for (int i = 1; i <= YK_TF_FAILS_MAX; i++) {
        assert(yk_tf_core_acquire(&bad, 0, false, RAM_PLENTY) == YK_TF_GO_MOUNT);
        yk_tf_core_mounted(&bad, false, 0);
        yk_tf_core_known(&bad, 0, 0, 0, &k);
        assert(k.fails == i && !k.opened && k.state == yk_tf_core_state(&bad, 0));
    }
    assert(k.state == YK_TF_RESTING);

    // No file system: until the next mount says otherwise.
    yk_tf_core_t blank = {0};
    assert(yk_tf_core_acquire(&blank, 0, false, RAM_PLENTY) == YK_TF_GO_MOUNT);
    blank.not_fat = true;  // mount_now()
    yk_tf_core_mounted(&blank, false, 0);
    yk_tf_core_known(&blank, 0, 0, 0, &k);
    assert(k.not_fat && k.fails == 1);
    assert(yk_tf_core_acquire(&blank, 0, false, RAM_PLENTY) == YK_TF_GO_MOUNT);
    yk_tf_core_mounted(&blank, true, 0);
    yk_tf_core_known(&blank, 0, 0, 0, &k);
    assert(!k.not_fat && k.opened);

    // How full: past 32 bits on a 64 GB card; not measured, or a pair that does not add up, is left out.
    yk_tf_core_known(&c, 0, 62367744, 60061696, &k);
    assert(k.total == 63864569856ULL && k.used == 2361393152ULL);
    yk_tf_core_known(&c, 0, 1000, 1000, &k);
    assert(k.total == 1024000 && !k.used);
    yk_tf_core_known(&c, 0, 1000, 1001, &k);
    assert(!k.total && !k.used);
    yk_tf_core_known(&c, 0, 0, 5, &k);
    assert(!k.total && !k.used);
}

/** "In a row": two failures, a clean session, two more: no rest. A session with a failure in it clears nothing. */
static void test_in_a_row(void) {
    yk_tf_core_t c = {0};
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < YK_TF_FAILS_MAX - 1; i++) {
            assert(yk_tf_core_acquire(&c, 0, false, RAM_PLENTY) == YK_TF_GO_MOUNT);
            yk_tf_core_mounted(&c, false, 0);
        }
        hold(&c);
        assert(c.fails == YK_TF_FAILS_MAX - 1);  // a mount alone is not a clean session
        assert(yk_tf_core_release(&c));
        assert(!c.fails);
    }

    // A mount works and a check on what it read fails, three sessions running: the card rests.
    memset(&c, 0, sizeof(c));
    for (int i = 0; i < YK_TF_FAILS_MAX; i++) {
        assert(yk_tf_core_acquire(&c, 0, false, RAM_PLENTY) == YK_TF_GO_MOUNT);
        yk_tf_core_mounted(&c, true, 0);
        yk_tf_core_fail(&c, 0);
        assert(yk_tf_core_release(&c));
    }
    assert(c.fails == YK_TF_FAILS_MAX && yk_tf_core_state(&c, 0) == YK_TF_RESTING);

    // Three within one session rest it at once, and a holder still there is turned away from then on.
    memset(&c, 0, sizeof(c));
    hold(&c);
    for (int i = 0; i < YK_TF_FAILS_MAX; i++) yk_tf_core_fail(&c, 5);
    assert(c.resting && c.rest_until_ms == 5 + YK_TF_REST_MS);
    assert(yk_tf_core_acquire(&c, 5, false, RAM_PLENTY) == YK_TF_NO_RESTING);
    assert(c.refs == 1);
    yk_tf_core_fail(&c, 900);
    assert(c.rest_until_ms == 5 + YK_TF_REST_MS);  // a late failure does not move the retry

    // Low RAM at the end of a rest does not use up the one try.
    yk_tf_core_t low = c;
    low.mounted = false;
    low.refs = 0;
    assert(yk_tf_core_acquire(&low, 5 + YK_TF_REST_MS, false, 100) == YK_TF_NO_RAM);
    assert(low.resting && low.fails == YK_TF_FAILS_MAX);

    // A holder that sat through the whole rest: the next holder ends it for both, the card is tried again.
    yk_tf_core_t held = c;
    assert(yk_tf_core_acquire(&held, 5 + YK_TF_REST_MS, false, RAM_PLENTY) == YK_TF_GO_HOLD);
    assert(!held.resting && held.fails == YK_TF_FAILS_MAX - 1 && held.refs == 2);

    assert(yk_tf_core_release(&c));
    assert(c.fails == YK_TF_FAILS_MAX);  // that session was not clean
}

// ---------------------------------------------------------------- the card, as it was on 11-06

typedef struct {
    uint32_t timeout_ms;
    int delays[64];  // how long each read takes the card, in order; beyond them 1 ms
    int next;
    bool stuck;      // in its data state after a timeout: everything fails at once until it is started again
    bool dead;       // does not come back from a restart
    bool write_fails;
    int restarts, transfers, instant_fails;
} card_t;

static int card_transfer(void *ctx) {
    card_t *k = ctx;
    k->transfers++;
    if (k->stuck) {
        k->instant_fails++;
        return 1;
    }
    const int ms = k->next < 64 && k->delays[k->next] ? k->delays[k->next] : 1;
    k->next++;
    if ((uint32_t)ms > k->timeout_ms) {
        k->stuck = true;
        return 1;
    }
    return k->write_fails ? 1 : 0;
}

static int card_start_again(void *ctx) {
    card_t *k = ctx;
    k->restarts++;
    if (k->dead) return 1;
    k->stuck = false;
    return 0;
}

static int run_io(yk_tf_core_t *c, card_t *k) {
    const yk_tf_io_t io = {.transfer = card_transfer, .restart = card_start_again, .ctx = k, .now_ms = 100};
    return yk_tf_core_io(c, &io);
}

static void test_wide_timeout_is_enough_for_a_slow_read(void) {
    yk_tf_core_t c = {0};
    hold(&c);
    card_t k = {.timeout_ms = YK_TF_DATA_TIMEOUT_MS};
    k.delays[3] = 656;
    for (int i = 0; i < 16; i++) assert(!run_io(&c, &k));
    assert(!k.restarts && !c.fails);
    // ESP-IDF's 100 ms would have stopped there: every read after it fails, and without a restart none comes back.
    card_t idf = {.timeout_ms = 100};
    idf.delays[3] = 656;
    yk_tf_core_t c2 = {0};
    hold(&c2);
    for (int i = 0; i < 16; i++) assert(!run_io(&c2, &idf));
    assert(idf.restarts == 1 && idf.transfers == 17 && !c2.fails);
}

static void test_restart_after_timeout(void) {
    yk_tf_core_t c = {0};
    hold(&c);
    card_t k = {.timeout_ms = YK_TF_DATA_TIMEOUT_MS};
    k.delays[2] = 900;  // past even the widest timeout
    for (int i = 0; i < 8; i++) assert(!run_io(&c, &k));
    assert(k.restarts == 1 && !k.stuck);
    assert(k.transfers == 9);   // the read that stalled is the only one done twice
    assert(!k.instant_fails);   // the retry came after the restart, not into the stuck card
    assert(!c.fails && !c.session_bad);
    assert(yk_tf_core_release(&c) && !c.fails);
}

/** Restart works, the read stalls again. One failure, and the next read starts the card again by itself. */
static void test_second_stall(void) {
    yk_tf_core_t c = {0};
    hold(&c);
    card_t k = {.timeout_ms = YK_TF_DATA_TIMEOUT_MS};
    k.delays[0] = k.delays[1] = 900;
    assert(run_io(&c, &k));
    assert(k.restarts == 1 && k.stuck && c.fails == 1);
    assert(!run_io(&c, &k));
    assert(k.restarts == 2 && !k.stuck && c.fails == 1 && k.instant_fails == 1);
}

static void test_card_lost(void) {
    yk_tf_core_t c = {0};
    hold(&c);
    card_t k = {.timeout_ms = YK_TF_DATA_TIMEOUT_MS, .dead = true};
    k.delays[0] = 900;
    assert(run_io(&c, &k));
    assert(c.lost && c.fails == 1 && k.restarts == 1);
    // The host is down now: nothing may reach it, and the holder hears at once.
    const int transfers = k.transfers;
    assert(run_io(&c, &k) && run_io(&c, &k));
    assert(k.transfers == transfers && k.restarts == 1 && c.fails == 1);
    assert(yk_tf_core_release(&c));
    assert(c.fails == 1);  // not clean
    assert(yk_tf_core_acquire(&c, 200, false, RAM_PLENTY) == YK_TF_GO_MOUNT);
    assert(!c.lost);  // a new mount opens the card again
    yk_tf_core_mounted(&c, false, 200);
    assert(c.fails == 2);
}

/** A failure counted by a transfer inside the mount is the mount's failure: once, not twice. */
static void test_mount_fails_once(void) {
    yk_tf_core_t c = {0};
    assert(yk_tf_core_acquire(&c, 0, false, RAM_PLENTY) == YK_TF_GO_MOUNT);
    card_t k = {.timeout_ms = YK_TF_DATA_TIMEOUT_MS, .dead = true};
    k.delays[0] = 900;
    assert(run_io(&c, &k));  // reads work during the mount
    assert(c.fails == 1);
    yk_tf_core_mounted(&c, false, 0);
    assert(c.fails == 1);
}

static void test_restart_budget(void) {
    yk_tf_core_t c = {0};
    hold(&c);
    card_t k = {.timeout_ms = YK_TF_DATA_TIMEOUT_MS};
    for (int i = 0; i < YK_TF_RESTARTS_MAX; i++) {
        k.delays[k.next] = 900;
        assert(!run_io(&c, &k));
    }
    assert(k.restarts == YK_TF_RESTARTS_MAX && !c.fails);
    // Stalling on every read: enough restarts, this card is not coming right.
    k.delays[k.next] = 900;
    assert(run_io(&c, &k));
    assert(k.restarts == YK_TF_RESTARTS_MAX && c.fails == 1);
    for (int i = 1; i < YK_TF_FAILS_MAX; i++) assert(run_io(&c, &k));  // stuck, one failure each
    assert(c.resting && k.restarts == YK_TF_RESTARTS_MAX);
    const int transfers = k.transfers;
    assert(run_io(&c, &k));  // resting: not even tried
    assert(k.transfers == transfers);

    // Stalls far apart are no reason to give up: a run of good transfers gives the restarts back.
    memset(&c, 0, sizeof(c));
    hold(&c);
    for (int round = 0; round < YK_TF_RESTARTS_MAX * 3; round++) {
        card_t far = {.timeout_ms = YK_TF_DATA_TIMEOUT_MS};
        far.delays[0] = 900;
        for (int i = 0; i < YK_TF_GOOD_RUN; i++) assert(!run_io(&c, &far));
        assert(far.restarts == 1);
    }
    assert(!c.fails);
}

static void test_refused(void) {
    yk_tf_core_t c = {0};
    card_t k = {.timeout_ms = YK_TF_DATA_TIMEOUT_MS};
    assert(run_io(&c, &k) && !k.transfers);  // not mounted
    hold(&c);
    assert(!run_io(&c, &k) && k.transfers == 1);
    yk_tf_core_end(&c);
    assert(run_io(&c, &k) && k.transfers == 1);  // the chip is restarting
    assert(yk_tf_core_acquire(&c, 0, false, RAM_PLENTY) == YK_TF_NO_ENDING);
    assert(yk_tf_core_stop_asked(&c) && yk_tf_core_state(&c, 0) == YK_TF_PAUSED);

    // Mounted while the pause comes: what is under way finishes, the pause only stops new holders.
    memset(&c, 0, sizeof(c));
    hold(&c);
    yk_tf_core_pause(&c);
    assert(!run_io(&c, &k));
}

/** A write that fails is started again and written once more, like a read. */
static void test_write_failure(void) {
    yk_tf_core_t c = {0};
    hold(&c);
    card_t k = {.timeout_ms = YK_TF_DATA_TIMEOUT_MS, .write_fails = true};
    assert(run_io(&c, &k));
    assert(k.transfers == 2 && k.restarts == 1 && c.fails == 1);
}

/** Lent to a computer (the USB disk): only from a free, paused card; transfers go on without a mount, with the restart
 *  after a stall; nobody takes it meanwhile; back, a clean lending clears the failures. */
static void test_lend(void) {
    yk_tf_core_t c = {0};
    card_t k = {.timeout_ms = YK_TF_DATA_TIMEOUT_MS};
    hold(&c);
    assert(!yk_tf_core_lend(&c, 0));  // held: not until it is let go
    assert(yk_tf_core_release(&c));
    yk_tf_core_pause(&c);
    c.fails = 2;
    assert(yk_tf_core_lend(&c, 0) && c.lent);
    assert(!yk_tf_core_lend(&c, 0));  // once
    assert(!yk_tf_core_free(&c));     // a deep sleep's suspend waits for it, as for a holder
    assert(yk_tf_core_acquire(&c, 0, false, RAM_PLENTY) == YK_TF_NO_PAUSED);
    assert(yk_tf_core_state(&c, 0) == YK_TF_PAUSED);
    k.delays[1] = 900;
    for (int i = 0; i < 4; i++) assert(!run_io(&c, &k));
    assert(k.restarts == 1 && c.fails == 2 && !c.session_bad);
    yk_tf_core_lend_end(&c);
    assert(!c.lent && !c.fails && yk_tf_core_free(&c));
    assert(run_io(&c, &k));  // back: no transfer without a mount
    yk_tf_core_resume(&c);
    assert(yk_tf_core_acquire(&c, 0, false, RAM_PLENTY) == YK_TF_GO_MOUNT);

    // A lending that failed keeps its failure; a resting card, an ending chip are not lent.
    memset(&c, 0, sizeof(c));
    yk_tf_core_pause(&c);
    assert(yk_tf_core_lend(&c, 0));
    card_t bad = {.timeout_ms = YK_TF_DATA_TIMEOUT_MS, .write_fails = true};
    assert(run_io(&c, &bad) && c.fails == 1);
    yk_tf_core_lend_end(&c);
    assert(c.fails == 1);
    for (int i = 0; i < YK_TF_FAILS_MAX; i++) yk_tf_core_fail(&c, 10);
    assert(c.resting && !yk_tf_core_lend(&c, 20));
    assert(yk_tf_core_lend(&c, 10 + YK_TF_REST_MS));  // the rest over: one more try
    yk_tf_core_lend_end(&c);
    yk_tf_core_end(&c);
    assert(!yk_tf_core_lend(&c, 10 + YK_TF_REST_MS));
}

static void test_crc(void) {
    assert(yk_tf_crc32("123456789", 9) == 0xCBF43926u);  // the standard check value
    assert(yk_tf_crc32("", 0) == 0);
}

/** A boot sector as a formatter writes it: FAT32 (its backup at 6 of 32 reserved sectors) or FAT16, NO NAME. */
static void boot_sector(uint8_t *sec, bool fat32) {
    memset(sec, 0, 512);
    memcpy(sec + 3, "MSDOS5.0", 8);
    sec[14] = 32;  // reserved sectors
    if (fat32) {
        sec[50] = 6;  // the backup boot sector
        sec[66] = 0x29;
        memcpy(sec + 71, "NO NAME    FAT32   ", 19);
    } else {
        sec[22] = 0x40;  // its 16-bit FAT size
        sec[38] = 0x29;
        memcpy(sec + 43, "NO NAME    FAT16   ", 19);
    }
    sec[510] = 0x55;
    sec[511] = 0xAA;
}

static void test_label(void) {
    uint8_t sec[512];
    char l[YK_TF_LABEL_BUF];
    uint32_t backup = 99;
    for (int fat32 = 0; fat32 < 2; fat32++) {
        boot_sector(sec, fat32);
        assert(yk_tf_boot_label(sec, l) && !strcmp(l, "NO NAME"));
        assert(yk_tf_boot_label_set(sec, YK_TF_LABEL, &backup) && backup == (fat32 ? 6u : 0u));
        assert(yk_tf_boot_label(sec, l) && !strcmp(l, YK_TF_LABEL));
        assert(!memcmp(sec + (fat32 ? 71 : 43), "TIKPAL     FAT", 14));  // padded, the type after it untouched
        assert(!yk_tf_boot_label_set(sec, "TWELVE CHARS", NULL));     // FAT's 11 at most
    }
    boot_sector(sec, true);
    memset(sec + 71, ' ', 11);  // a blank field
    assert(yk_tf_boot_label(sec, l) && !l[0]);
    sec[66] = 0;  // no extended boot signature: no field
    assert(!yk_tf_boot_label(sec, l) && !yk_tf_boot_label_set(sec, YK_TF_LABEL, &backup) && !backup);
    boot_sector(sec, true);
    memcpy(sec + 3, "EXFAT   ", 8);
    assert(!yk_tf_boot_label(sec, l));
    boot_sector(sec, true);
    sec[511] = 0;
    assert(!yk_tf_boot_label(sec, l));
    sec[14] = 4;  // a backup past the reserved sectors is none
    sec[511] = 0xAA;
    assert(yk_tf_boot_label_set(sec, YK_TF_LABEL, &backup) && !backup);

    // Which copies: none and NO NAME are no name; the owner's own name stays, in either place; ours is made whole.
    bool d, b;
    yk_tf_label_plan("", "NO NAME", &d, &b);  // a fresh card, as FatFs and most formatters leave it
    assert(d && b);
    yk_tf_label_plan("NO NAME", "NO NAME", &d, &b);
    assert(d && b);
    yk_tf_label_plan("", "", &d, &b);
    assert(d && b);
    yk_tf_label_plan("", NULL, &d, &b);  // no field in the boot sector: the root directory's only
    assert(d && !b);
    yk_tf_label_plan("MY CARD", "NO NAME", &d, &b);  // renamed on Windows (the root directory's)
    assert(!d && !b);
    yk_tf_label_plan("MY CARD", "MY CARD", &d, &b);  // renamed on a Mac
    assert(!d && !b);
    yk_tf_label_plan("", "PHOTOS", &d, &b);  // a formatter's name, in the boot sector only
    assert(!d && !b);
    yk_tf_label_plan("TIKPAL", "NO NAME", &d, &b);  // ours, its boot sector's copy not written yet
    assert(!d && b);
    yk_tf_label_plan("TIKPAL", "TIKPAL", &d, &b);
    assert(!d && !b);
    yk_tf_label_plan("NO NAME", "PHOTOS", &d, &b);  // the root directory's no name is what a computer shows
    assert(d && b);
    // Renamed on a Mac after we named it (真机验收 11-14): macOS wrote the boot sector's copy only, and shows the root
    // directory's on the next mount. Neither copy becomes TIKPAL again; the owner's name goes into the root directory.
    yk_tf_label_plan("TIKPAL", "YAYTEST", &d, &b);
    assert(!d && !b);
    assert(yk_tf_label_follow("TIKPAL", "YAYTEST"));
    assert(!yk_tf_label_follow("TIKPAL", "TIKPAL") && !yk_tf_label_follow("TIKPAL", "NO NAME"));
    assert(!yk_tf_label_follow("TIKPAL", "") && !yk_tf_label_follow("TIKPAL", NULL));
    assert(!yk_tf_label_follow("NO NAME", "PHOTOS") && !yk_tf_label_follow("", "PHOTOS"));  // never ours: as above
    assert(!yk_tf_label_follow("MY CARD", "OTHER"));  // the owner's in the root directory already: left

    // 0.1.20 4.8: what moved since the two kept on the card is the rename; the other copy follows, TIKPAL too.
    char to[YK_TF_LABEL_BUF];
    // A Mac's second rename: the root directory has the first, the boot sector the new one.
    assert(yk_tf_label_sync("YAYTEST", "SECOND", "YAYTEST", "YAYTEST", to, &d, &b) == YK_TF_LABEL_RENAMED);
    assert(!strcmp(to, "SECOND") && d && !b);
    // ... and back to TIKPAL on a Mac: it stays TIKPAL, not the old name back.
    assert(yk_tf_label_sync("SECOND", "TIKPAL", "SECOND", "SECOND", to, &d, &b) == YK_TF_LABEL_RENAMED);
    assert(!strcmp(to, "TIKPAL") && d && !b);
    // Windows writes the root directory's: the boot sector follows (as yk_tf_label_follow alone would have got wrong).
    assert(yk_tf_label_sync("TIKPAL", "SECOND", "SECOND", "SECOND", to, &d, &b) == YK_TF_LABEL_RENAMED);
    assert(!strcmp(to, "TIKPAL") && !d && b);
    assert(yk_tf_label_sync("PHOTOS", "TIKPAL", "TIKPAL", "TIKPAL", to, &d, &b) == YK_TF_LABEL_RENAMED);
    assert(!strcmp(to, "PHOTOS") && !d && b);
    // A name the boot sector cannot hold (more than 11, lower case, not ASCII): the root directory's only.
    assert(yk_tf_label_sync("相册", "TIKPAL", "TIKPAL", "TIKPAL", to, &d, &b) == YK_TF_LABEL_KEEP && !d && !b);
    assert(yk_tf_label_sync("Photos", "TIKPAL", "TIKPAL", "TIKPAL", to, &d, &b) == YK_TF_LABEL_KEEP);
    // The name taken away on a computer: no name is TIKPAL, both copies.
    assert(yk_tf_label_sync("SECOND", "NO NAME", "SECOND", "SECOND", to, &d, &b) == YK_TF_LABEL_NONE);
    assert(!strcmp(to, "TIKPAL") && d && b);
    assert(yk_tf_label_sync("", "SECOND", "SECOND", "SECOND", to, &d, &b) == YK_TF_LABEL_NONE && d && b);
    // Both moved (another tool, or the power went between our two writes): the root directory's, what a computer shows.
    assert(yk_tf_label_sync("ONE", "TWO", "TIKPAL", "TIKPAL", to, &d, &b) == YK_TF_LABEL_RENAMED);
    assert(!strcmp(to, "ONE") && !d && b);
    assert(yk_tf_label_sync("", "TWO", "TIKPAL", "TIKPAL", to, &d, &b) == YK_TF_LABEL_RENAMED);  // the root's none
    assert(!strcmp(to, "TWO") && d && !b);
    assert(yk_tf_label_sync("ONE", "ONE", "TIKPAL", "TIKPAL", to, &d, &b) == YK_TF_LABEL_KEEP);  // already alike
    // Nothing moved: as before, the owner's left as it is, none made TIKPAL.
    assert(yk_tf_label_sync("MY CARD", "NO NAME", "MY CARD", "NO NAME", to, &d, &b) == YK_TF_LABEL_KEEP && !d && !b);
    assert(yk_tf_label_sync("", "NO NAME", "", "", to, &d, &b) == YK_TF_LABEL_NONE && d && b);  // NO NAME is none
    // Nothing kept (a card from before 0.1.20, or formatted since) or no field: yk_tf_label_follow, then the plan.
    assert(yk_tf_label_sync("TIKPAL", "YAYTEST", NULL, NULL, to, &d, &b) == YK_TF_LABEL_RENAMED);
    assert(!strcmp(to, "YAYTEST") && d && !b);
    assert(yk_tf_label_sync("", "NO NAME", NULL, NULL, to, &d, &b) == YK_TF_LABEL_NONE && !strcmp(to, "TIKPAL") && d && b);
    assert(yk_tf_label_sync("MY CARD", "OTHER", NULL, NULL, to, &d, &b) == YK_TF_LABEL_KEEP);
    assert(yk_tf_label_sync("", NULL, "X", "X", to, &d, &b) == YK_TF_LABEL_NONE && d && !b);
    assert(yk_tf_label_sync("SECOND", NULL, "FIRST", "FIRST", to, &d, &b) == YK_TF_LABEL_KEEP);

    // What is kept: two lines, each a label; anything else is not known.
    char kd[YK_TF_LABEL_BUF], kb[YK_TF_LABEL_BUF];
    assert(yk_tf_label_parse("MY CARD\nTIKPAL\n", 15, kd, kb) && !strcmp(kd, "MY CARD") && !strcmp(kb, "TIKPAL"));
    assert(yk_tf_label_parse("\nNO NAME\n", 9, kd, kb) && !kd[0] && !strcmp(kb, "NO NAME"));
    assert(!yk_tf_label_parse("MY CARD\nTIKPAL", 14, kd, kb));  // cut short (the power went)
    assert(!yk_tf_label_parse("MY CARD\n", 8, kd, kb));
    assert(!yk_tf_label_parse("", 0, kd, kb));
    assert(!yk_tf_label_parse("0123456789012345678901234567890123456789\nX\n", 43, kd, kb));  // too long for a label
}

int main(void) {
    test_rel();
    test_rel_exhaustive();
    test_join();
    test_names();
    test_quota();
    test_agreed_numbers();
    test_memory();
    test_timeout();
    test_refs();
    test_pause();
    test_backoff();
    test_known();
    test_in_a_row();
    test_wide_timeout_is_enough_for_a_slow_read();
    test_restart_after_timeout();
    test_second_stall();
    test_card_lost();
    test_mount_fails_once();
    test_restart_budget();
    test_refused();
    test_write_failure();
    test_lend();
    test_crc();
    test_label();
    puts("yaykit_tf logic: all passed");
    return 0;
}
