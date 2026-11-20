// Host tests for the pure parts of yaykit_app. Run: bash firmware/components/yaykit_app/test/run.sh
#include <stdio.h>
#include <string.h>

#include "../yk_app_logic.h"
#include "yk_app_power.h"
#include "yk_app_recovery.h"

static int failures, checks;
#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                  \
    } while (0)

static void test_home(void) {
    CHECK(yk_home_slot("yay", "valid") == 0);
    CHECK(yk_home_slot("yay", "new") == 0);
    CHECK(yk_home_slot("yay", "pending") == 0);
    CHECK(yk_home_slot("yay", "unknown") == 0);
    CHECK(yk_home_slot("yay", "invalid") == -1);  // rolled back: the recovery has to fix it
    CHECK(yk_home_slot("yay", "aborted") == -1);
    CHECK(yk_home_slot("", "empty") == -1);
    CHECK(yk_home_slot("party-play", "valid") == -1);  // slot 1 holds something else: not home
    CHECK(yk_home_slot(NULL, NULL) == -1);

    CHECK(yk_recovery_forward(false, "yay", "valid"));
    CHECK(!yk_recovery_forward(true, "yay", "valid"));  // someone asked for the recovery
    CHECK(!yk_recovery_forward(false, "party-play", "valid"));
}

static void test_is_home(void) {
    CHECK(yk_app_is_home("yay", true, 0));  // the main firmware in slot 1, as yay/main/main.c writes it
    CHECK(!yk_app_is_home("yay", false, 0));
    // An app cannot make itself home: it would lose the app ring and the system pull-down.
    CHECK(!yk_app_is_home("party-play", true, 0));
    CHECK(!yk_app_is_home("party-play", true, 1));
    CHECK(!yk_app_is_home("yay-clone", true, 0));
    CHECK(!yk_app_is_home("Yay", true, 0));
    CHECK(!yk_app_is_home("", true, 0));
    CHECK(!yk_app_is_home(NULL, true, 0));
    CHECK(!yk_app_is_home("party-play", false, 0));
    // Nor by making up "yay" and is_home at run time (0.1.17): only slot 1 is home. Slots 2 and 3, factory (-1) and
    // anything else are not, whatever the slug says.
    CHECK(!yk_app_is_home("yay", true, 1));
    CHECK(!yk_app_is_home("yay", true, 2));
    CHECK(!yk_app_is_home("yay", true, -1));
    CHECK(!yk_app_is_home("yay", true, 3));
    CHECK(!yk_app_is_home("yay", false, 1));
}

static void test_launch_accept(void) {
    const long long t = 1792310400;  // 2026-10-17
    CHECK(yk_launch_accept("language-buddy", t, "language-buddy", t + 3));
    CHECK(yk_launch_accept("language-buddy", t, "language-buddy", t + 60));
    CHECK(!yk_launch_accept("language-buddy", t, "language-buddy", t + 61));  // stale: ignore it
    CHECK(!yk_launch_accept("breathe", t, "language-buddy", t + 1));         // for another app
    CHECK(yk_launch_accept("breathe", t, "breathe", t - 3));                 // small skew
    CHECK(!yk_launch_accept("breathe", t, "breathe", t - 30));
    CHECK(yk_launch_accept("breathe", 0, "breathe", t));  // unknown times are not judged
    CHECK(yk_launch_accept("breathe", t, "breathe", 100));
    CHECK(!yk_launch_accept(NULL, t, "breathe", t));
}

static void test_escape(void) {
    char out[64];
    CHECK(yk_json_escape(out, sizeof(out), "a\"b\\c\nd\x01") == 16 && !strcmp(out, "a\\\"b\\\\c\\nd\\u0001"));
    CHECK(yk_json_escape(out, sizeof(out), "练得怎么样？") == strlen("练得怎么样？") && !strcmp(out, "练得怎么样？"));
    char small[5];
    CHECK(yk_json_escape(small, sizeof(small), "abcdefg") == 7 && !strcmp(small, "abcd"));  // cut, terminated
    CHECK(yk_json_escape(out, sizeof(out), NULL) == 0 && out[0] == 0);
}

static void test_records(void) {
    char out[1100];
    int n = yk_launch_format(out, sizeof(out), "yay", "language-buddy", "practice", "{\"language\":\"en\"}",
                             1792310400, "9f2c4a1e");
    CHECK(n > 0 && !strcmp(out, "{\"v\":1,\"from\":\"yay\",\"slug\":\"language-buddy\",\"intent\":\"practice\","
                                "\"params\":{\"language\":\"en\"},\"at\":1792310400,\"nonce\":\"9f2c4a1e\"}"));
    CHECK((size_t)n == strlen(out));
    n = yk_launch_format(out, sizeof(out), "yay", "breathe", NULL, NULL, 1, "n");
    CHECK(n > 0 && strstr(out, "\"params\":{}") && strstr(out, "\"intent\":\"\""));
    CHECK(yk_launch_format(out, sizeof(out), "yay", "breathe", "", "[1,2]", 1, "n") == -1);  // not an object
    CHECK(yk_launch_format(out, sizeof(out), "yay", NULL, "", NULL, 1, "n") == -1);

    // Over 1 KB in all is refused, even when the buffer is bigger.
    char big[1200];
    memset(big, 'a', sizeof(big));
    big[0] = '{', big[1] = '"', big[1000] = '"', big[1001] = ':', big[1002] = '1', big[1003] = '}', big[1004] = 0;
    CHECK(yk_launch_format(out, sizeof(out), "yay", "breathe", "", big, 1, "n") == -1);
    char tiny[40];
    CHECK(yk_launch_format(tiny, sizeof(tiny), "yay", "language-buddy", "practice", NULL, 1, "n") == -1);

    n = yk_result_format(out, sizeof(out), "language-buddy", "练了 8 分钟\"英语\"", "{\"minutes\":8}", 1792310880);
    CHECK(n > 0 && !strcmp(out, "{\"v\":1,\"slug\":\"language-buddy\",\"summary\":\"练了 8 分钟\\\"英语\\\"\","
                                "\"data\":{\"minutes\":8},\"at\":1792310880}"));
    n = yk_result_format(out, sizeof(out), "breathe", NULL, NULL, 5);
    CHECK(n > 0 && !strcmp(out, "{\"v\":1,\"slug\":\"breathe\",\"data\":{},\"at\":5}"));
}

static void test_banner(void) {
    char out[40];
    yk_banner_name(out, sizeof(out), "party-play");
    CHECK(!strcmp(out, "YAYKIT_PARTY_PLAY_READY"));
    yk_banner_name(out, sizeof(out), "yay");
    CHECK(!strcmp(out, "YAYKIT_YAY_READY"));
}

static void test_guard(void) {
    uint8_t n = 99;
    // Normal resets (power on, esp_restart, USB) start the count again.
    CHECK(yk_boot_guard(0, YK_RESET_NORMAL, true, &n) == YK_GUARD_RUN && n == 0);
    CHECK(yk_boot_guard(2, YK_RESET_NORMAL, false, &n) == YK_GUARD_RUN && n == 0);
    // One or two crashes in a row: try again.
    CHECK(yk_boot_guard(0, YK_RESET_CRASH, true, &n) == YK_GUARD_RUN && n == 1);
    CHECK(yk_boot_guard(1, YK_RESET_CRASH, false, &n) == YK_GUARD_RUN && n == 2);
    // The third: leave. The main firmware goes to the recovery, an app back to Yay.
    CHECK(yk_boot_guard(2, YK_RESET_CRASH, true, &n) == YK_GUARD_RECOVERY && n == 3);
    CHECK(yk_boot_guard(2, YK_RESET_CRASH, false, &n) == YK_GUARD_HOME && n == 3);
    CHECK(yk_boot_guard(7, YK_RESET_CRASH, true, &n) == YK_GUARD_RECOVERY && n == 8);  // stale count from an old build
    CHECK(yk_boot_guard(255, YK_RESET_CRASH, false, &n) == YK_GUARD_HOME && n == 255);  // saturates
    CHECK(yk_boot_guard(1, YK_RESET_CRASH, true, NULL) == YK_GUARD_RUN);
    // A whole loop: crash, crash, leave; a good start in between clears it (the device writes 0).
    uint8_t stored = 0;
    int runs = 0;
    for (int boot = 0; boot < 5; boot++) {
        if (yk_boot_guard(stored, YK_RESET_CRASH, true, &stored) != YK_GUARD_RUN) break;
        runs++;
    }
    CHECK(runs == YK_CRASHLOOP_LIMIT - 1 && stored == YK_CRASHLOOP_LIMIT);
    // Brownouts (a battery running low) are not the app's crashes: however many, it keeps starting.
    stored = 0;
    for (int boot = 0; boot < 10; boot++) CHECK(yk_boot_guard(stored, YK_RESET_POWER, false, &stored) == YK_GUARD_RUN);
    CHECK(stored == 0);
    // Nor do they wipe out real crashes before them: crash, crash, brownout, crash still leaves.
    CHECK(yk_boot_guard(0, YK_RESET_CRASH, false, &n) == YK_GUARD_RUN && n == 1);
    CHECK(yk_boot_guard(n, YK_RESET_CRASH, false, &n) == YK_GUARD_RUN && n == 2);
    CHECK(yk_boot_guard(n, YK_RESET_POWER, false, &n) == YK_GUARD_RUN && n == 2);
    CHECK(yk_boot_guard(n, YK_RESET_CRASH, false, &n) == YK_GUARD_HOME && n == 3);
}

static void test_names(void) {
    CHECK(!strcmp(yk_app_display_name("party-play"), "聚会玩"));
    CHECK(!strcmp(yk_app_display_name("yay"), "Tikpal"));
    CHECK(!strcmp(yk_app_display_name("language-buddy"), "口语陪练"));
    CHECK(!strcmp(yk_app_display_name("weather-pal"), "weather-pal"));  // not known here: the slug
    CHECK(!strcmp(yk_app_display_name(NULL), ""));
    // Added rows (10-24, Yay 0.1.3's install page): the two the table lacked; the old ones as they were.
    CHECK(!strcmp(yk_app_display_name("recording-card"), "录音卡") && !strcmp(yk_app_display_name("translator"), "面对面翻译"));
    CHECK(!strcmp(yk_app_display_name("breathe"), "呼吸之间") && !strcmp(yk_app_display_name("template"), "扩展模板"));
    // In English (each app.json's name); NULL when the table does not know it.
    CHECK(!strcmp(yk_app_table_name("translator", true), "Face to Face") &&
          !strcmp(yk_app_table_name("recording-card", true), "Recording Card"));
    CHECK(!strcmp(yk_app_table_name("party-play", true), "Party Play") && !strcmp(yk_app_table_name("party-play", false), "聚会玩"));
    CHECK(!yk_app_table_name("weather-pal", false) && !yk_app_table_name("weather-pal", true) && !yk_app_table_name(NULL, true));
    // On screen: the store's name, else the table's, else the slug, in either language.
    CHECK(!strcmp(yk_app_label("recording-card", "录音卡片", false), "录音卡片"));          // the catalog has it
    CHECK(!strcmp(yk_app_label("recording-card", "Voice Notes", true), "Voice Notes"));
    CHECK(!strcmp(yk_app_label("recording-card", NULL, false), "录音卡"));               // not in the catalog: the table
    CHECK(!strcmp(yk_app_label("translator", "", true), "Face to Face"));
    CHECK(!strcmp(yk_app_label("weather-pal", NULL, false), "weather-pal"));           // neither: the slug
    CHECK(!strcmp(yk_app_label("weather-pal", "", true), "weather-pal"));
    CHECK(!strcmp(yk_app_label(NULL, NULL, false), ""));
}

static void test_summary(void) {
    yk_summary_t s;
    char out[512];
    // The design's example: 8 rounds, new phrases in rounds 2, 5 and 7, recorded.
    yk_summary_init(&s);
    CHECK(yk_summary_stat(&s, "8", "轮") && yk_summary_stat(&s, "3", "新说法"));
    const int rounds[] = {2, 5, 7};
    yk_summary_beads(&s, 8, rounds, 3);
    s.recorded = 1;
    CHECK(yk_summary_format(out, sizeof(out), &s) > 0);
    CHECK(!strcmp(out, "{\"stats\":[{\"v\":\"8\",\"l\":\"轮\"},{\"v\":\"3\",\"l\":\"新说法\"}],"
                       "\"beads\":{\"total\":8,\"marked\":[2,5,7]},\"recorded\":true}"));
    // No more than 3 numbers; a value is needed.
    CHECK(yk_summary_stat(&s, "12", "分钟") && !yk_summary_stat(&s, "1", "多余") && s.stat_count == 3);
    yk_summary_init(&s);
    CHECK(!yk_summary_stat(&s, "", "空") && !yk_summary_stat(&s, NULL, "空") && s.stat_count == 0);
    // Nothing set: an empty stats list, no beads, no recorded.
    CHECK(yk_summary_format(out, sizeof(out), &s) > 0 && !strcmp(out, "{\"stats\":[]}"));
    s.recorded = 0;
    CHECK(yk_summary_format(out, sizeof(out), &s) > 0 && !strcmp(out, "{\"stats\":[],\"recorded\":false}"));
    // Over 20: 20 beads, item i of n on floor(i * 20 / n).
    CHECK(yk_summary_bead(0, 30) == 0 && yk_summary_bead(29, 30) == 19 && yk_summary_bead(14, 30) == 9);
    CHECK(yk_summary_bead(19, 20) == 19 && yk_summary_bead(5, 8) == 5);
    const int many[] = {0, 1, 29, 30, -1};  // 0 and 1 share bead 0; 30 and -1 are not items
    yk_summary_beads(&s, 30, many, 5);
    CHECK(s.beads == 20 && s.marked == ((1u << 0) | (1u << 19)));
    yk_summary_beads(&s, 0, NULL, 0);
    CHECK(s.beads == 0 && s.marked == 0);
    // A label that does not fit is cut between characters, never inside one.
    yk_summary_init(&s);
    CHECK(yk_summary_stat(&s, "1", "一二三四五六七八九十"));  // 30 bytes into 24: 7 characters
    CHECK(!strcmp(s.stats[0].l, "一二三四五六七"));
    // Too small a buffer: -1.
    CHECK(yk_summary_format(out, 8, &s) == -1);
    // Fresh for 30 minutes; a clock not known at either end cannot tell.
    const int64_t now = 1792400000;
    CHECK(yk_result_fresh(now - 1800, now) && !yk_result_fresh(now - 1801, now));
    CHECK(yk_result_fresh(1000, now) && yk_result_fresh(now - 7200, 1000));
}

static void test_outbox(void) {
    CHECK(yk_record_kind_ok("breathe.session") && yk_record_kind_ok("abc"));
    CHECK(!yk_record_kind_ok("ab") && !yk_record_kind_ok("Breathe") && !yk_record_kind_ok("9a") &&
          !yk_record_kind_ok("a-b.c") && !yk_record_kind_ok(NULL));
    char key[8];
    yk_record_key(0, key);
    CHECK(!strcmp(key, "rec0"));
    yk_record_key(17, key);
    CHECK(!strcmp(key, "rec1"));  // the ring: 16 entries
    yk_record_key(4294967295u, key);
    CHECK(!strcmp(key, "rec15"));
    char out[700];
    const int64_t at = 1792400000123LL;  // 2026-10-19T08:53:20.123Z
    CHECK(yk_record_format(out, sizeof(out), "breathe", "yk_0a1b2c3d4e5f", 7, "breathe.session",
                           "{\"minutes\":5,\"breaths\":42}", at) > 0);
    CHECK(!strcmp(out, "{\"app\":\"breathe\",\"id\":\"yk_0a1b2c3d4e5f:breathe:7\",\"kind\":\"breathe.session\","
                       "\"occurredAt\":\"2026-10-19T08:53:20.123Z\",\"clockTrusted\":true,"
                       "\"data\":{\"minutes\":5,\"breaths\":42}}"));
    // A clock never set: still a time, not trusted.
    CHECK(yk_record_format(out, sizeof(out), "breathe", "yk_0a1b2c3d4e5f", 8, "breathe.session", NULL, 5000) > 0);
    CHECK(strstr(out, "\"occurredAt\":\"1970-01-01T00:00:05.000Z\",\"clockTrusted\":false") && strstr(out, "\"data\":{}"));
    // Refused: a bad kind, data that is not an object or too long, a buffer too small.
    CHECK(yk_record_format(out, sizeof(out), "breathe", "yk_0a1b2c3d4e5f", 9, "Bad", NULL, at) == -1);
    CHECK(yk_record_format(out, sizeof(out), "breathe", "yk_0a1b2c3d4e5f", 9, "breathe.session", "[1]", at) == -1);
    char big[600];
    memset(big, 'x', sizeof(big));
    memcpy(big, "{\"a\":\"", 6);
    big[sizeof(big) - 3] = '"', big[sizeof(big) - 2] = '}', big[sizeof(big) - 1] = 0;
    CHECK(yk_record_format(out, sizeof(out), "breathe", "yk_0a1b2c3d4e5f", 9, "breathe.session", big, at) == -1);
    CHECK(yk_record_format(out, 40, "breathe", "yk_0a1b2c3d4e5f", 9, "breathe.session", NULL, at) == -1);
}

enum { DOWN = 1, HOLD, UP, TAP, DOUBLE };

/** The side key with the app ring (P23): which presses are the app's. */
static void test_keys(void) {
    yk_keys_t k;
    yk_keys_init(&k);
    // Taps and holds are the app's; a double click opens the ring.
    CHECK(yk_keys_route(&k, DOWN, 1000, false) == YK_KEYS_APP);
    CHECK(yk_keys_route(&k, UP, 1080, false) == YK_KEYS_APP);
    CHECK(yk_keys_route(&k, TAP, 1080, false) == YK_KEYS_APP);
    CHECK(yk_keys_route(&k, DOWN, 1200, false) == YK_KEYS_APP);
    CHECK(yk_keys_route(&k, UP, 1280, false) == YK_KEYS_APP);
    CHECK(yk_keys_route(&k, DOUBLE, 1280, false) == YK_KEYS_RING_OPEN && k.ring && !yk_keys_idle(&k));
    // A tap while it is open: closes it, and nothing of that press reaches the app.
    CHECK(yk_keys_route(&k, DOWN, 3000, false) == YK_KEYS_RING_CLOSE && !k.ring);
    CHECK(yk_keys_route(&k, UP, 3080, false) == YK_KEYS_EAT);
    CHECK(!yk_keys_idle(&k));
    CHECK(yk_keys_route(&k, TAP, 3080, false) == YK_KEYS_EAT && yk_keys_idle(&k));
    // A double click while it is open: the first press closes it; without delay_tap its TAP came already, so the
    // second press is the app's, but the double click does not open it again.
    yk_keys_route(&k, DOUBLE, 5000, false);
    CHECK(k.ring);
    CHECK(yk_keys_route(&k, DOWN, 6000, false) == YK_KEYS_RING_CLOSE);
    yk_keys_route(&k, UP, 6080, false);
    yk_keys_route(&k, TAP, 6080, false);
    CHECK(yk_keys_route(&k, DOWN, 6200, false) == YK_KEYS_APP);
    CHECK(yk_keys_route(&k, UP, 6280, false) == YK_KEYS_APP);
    CHECK(yk_keys_route(&k, DOUBLE, 6280, false) == YK_KEYS_EAT && !k.ring);
    // ... with delay_tap no TAP comes between: the whole double click is eaten.
    yk_keys_route(&k, DOUBLE, 9000, false);
    CHECK(yk_keys_route(&k, DOWN, 10000, false) == YK_KEYS_RING_CLOSE);
    CHECK(yk_keys_route(&k, UP, 10080, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, DOWN, 10200, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, UP, 10280, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, DOUBLE, 10280, false) == YK_KEYS_EAT && yk_keys_idle(&k) && !k.ring);
    // A double click well after: the ring again.
    CHECK(yk_keys_route(&k, DOUBLE, 20000, false) == YK_KEYS_RING_OPEN);
    // Held while it is open: closes it; the hold and its release are the system's.
    CHECK(yk_keys_route(&k, DOWN, 21000, false) == YK_KEYS_RING_CLOSE);
    CHECK(yk_keys_route(&k, HOLD, 21300, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, UP, 22000, false) == YK_KEYS_EAT && yk_keys_idle(&k));
    // It closed by itself (a tap on it): the next press is the app's.
    yk_keys_route(&k, DOUBLE, 30000, false);
    yk_keys_closed(&k);
    CHECK(yk_keys_idle(&k) && yk_keys_route(&k, DOWN, 31000, false) == YK_KEYS_APP);
    yk_keys_route(&k, UP, 31080, false);
    yk_keys_route(&k, TAP, 31080, false);
    // An app that takes the double click itself: no ring.
    CHECK(yk_keys_route(&k, DOUBLE, 40000, true) == YK_KEYS_APP && !k.ring);
    // A hold the app did not use, in an app that does not talk: 「回到主屏幕说？」; one it used, or a talking app: no.
    yk_keys_init(&k);
    CHECK(yk_keys_route(&k, DOWN, 1000, false) == YK_KEYS_APP);
    CHECK(yk_keys_route(&k, HOLD, 1300, false) == YK_KEYS_APP);
    CHECK(!yk_keys_ask(&k, true, false) && !yk_keys_ask(&k, false, true) && !k.ask);
    CHECK(yk_keys_ask(&k, false, false) && k.ask);
    CHECK(yk_keys_route(&k, UP, 2000, false) == YK_KEYS_EAT);  // the release of the hold that asked
    // A press says stay: the card goes, that press is the system's.
    CHECK(yk_keys_route(&k, DOWN, 3000, false) == YK_KEYS_ASK_CLOSE && !k.ask);
    CHECK(yk_keys_route(&k, UP, 3080, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, TAP, 3080, false) == YK_KEYS_EAT && yk_keys_idle(&k));
    // A hold while the ring is up asks nothing (it closes the ring).
    yk_keys_route(&k, DOUBLE, 9000, false);
    yk_keys_route(&k, DOWN, 9500, false);
    yk_keys_route(&k, HOLD, 9800, false);
    CHECK(!yk_keys_ask(&k, false, false) && !k.ask);
    // The 应用码 the ring showed (voice-code handoff 2.1②): the key is the system's while it is up; a tap hides it and
    // all of that press goes nowhere.
    yk_keys_init(&k);
    yk_keys_code(&k, true);
    CHECK(!yk_keys_idle(&k));
    CHECK(yk_keys_route(&k, DOWN, 50000, false) == YK_KEYS_CODE_CLOSE && !k.code);
    CHECK(yk_keys_route(&k, UP, 50080, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, TAP, 50080, false) == YK_KEYS_EAT && yk_keys_idle(&k));
    CHECK(yk_keys_route(&k, DOWN, 52000, false) == YK_KEYS_APP);  // the app's again
    yk_keys_route(&k, UP, 52080, false);
    yk_keys_route(&k, TAP, 52080, false);
    // A double click while it is up: the first press hides it; the double click opens no ring.
    yk_keys_init(&k);
    yk_keys_code(&k, true);
    CHECK(yk_keys_route(&k, DOWN, 60000, false) == YK_KEYS_CODE_CLOSE);
    CHECK(yk_keys_route(&k, UP, 60080, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, DOWN, 60200, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, UP, 60280, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, DOUBLE, 60280, false) == YK_KEYS_EAT && !k.ring && yk_keys_idle(&k));
    // A hold hides it and asks nothing; its release ends it.
    yk_keys_init(&k);
    yk_keys_code(&k, true);
    CHECK(yk_keys_route(&k, DOWN, 70000, false) == YK_KEYS_CODE_CLOSE);
    CHECK(yk_keys_route(&k, HOLD, 70300, false) == YK_KEYS_EAT);
    CHECK(!yk_keys_ask(&k, false, false));
    CHECK(yk_keys_route(&k, UP, 71000, false) == YK_KEYS_EAT && yk_keys_idle(&k));
    // It went by itself (a tap on it, a swipe, 60 s): the key is the app's again at once.
    yk_keys_init(&k);
    yk_keys_code(&k, true);
    yk_keys_code(&k, false);
    CHECK(yk_keys_idle(&k) && yk_keys_route(&k, DOWN, 80000, false) == YK_KEYS_APP);
}

// ---------------------------------------------------------------- what the app gets (#371's review)

#include "yk_key_sm.h"

typedef struct {
    int64_t at;
    bool down;
} edge_t;

/** Presses (key edges at these times) through the real key state machine with `cfg`, then yk_keys_route: the events
 *  the app gets ("down up tap…") and what the system did ("ring", "close"…). */
static int64_t s_clock;  // each press() starts 10 s after the last one began

static void press(const yk_key_config_t *cfg, yk_keys_t *k, const edge_t *edges, int n, char *app, size_t app_size,
                  char *sys, size_t sys_size) {
    s_clock += 10000;
    static const char *const names[] = {"?", "down", "hold", "up", "tap", "double"};
    static const char *const did[] = {"", "", "ring", "close", "stay"};  // APP, EAT, RING_OPEN, RING_CLOSE, ASK_CLOSE
    yk_key_sm_t sm;
    yk_key_sm_init(&sm, cfg);
    app[0] = sys[0] = 0;
    for (int64_t now = s_clock + edges[0].at; now <= s_clock + edges[n - 1].at + 1500; now += 5) {
        yk_key_event_t ev[YK_KEY_SM_MAX_EVENTS];
        int c = 0;
        for (int i = 0; i < n; i++)
            if (s_clock + edges[i].at == now) c += yk_key_sm_edge(&sm, edges[i].down, now, ev + c);
        c += yk_key_sm_tick(&sm, now, ev + c);
        for (int i = 0; i < c; i++) {
            const yk_keys_do_t d = yk_keys_route(k, ev[i].type, ev[i].at_ms, false);
            if (d == YK_KEYS_APP) {
                strncat(app, names[ev[i].type], app_size - strlen(app) - 1);
                strncat(app, " ", app_size - strlen(app) - 1);
            } else if (did[d][0]) {
                strncat(sys, did[d], sys_size - strlen(sys) - 1);
                strncat(sys, " ", sys_size - strlen(sys) - 1);
            }
        }
    }
}

static void test_app_events(void) {
    const edge_t dbl[] = {{1000, true}, {1080, false}, {1200, true}, {1280, false}};
    const edge_t tap[] = {{1000, true}, {1080, false}};
    char app[64], sys[64];
    yk_keys_t k;
    // Without delay_tap the first press of a double click is a TAP the app acts on (Party Play back to its menu,
    // Breathe paused, the template counting) before the ring opens: why the apps on yk_app_on_key use delay_tap.
    const yk_key_config_t plain = {0}, delayed = {.delay_tap = true};
    yk_keys_init(&k);
    press(&plain, &k, dbl, 4, app, sizeof(app), sys, sizeof(sys));
    CHECK(!strcmp(app, "down up tap down up ") && !strcmp(sys, "ring "));
    // With delay_tap: the app sees the two presses go down and up, never a tap; the ring opens.
    yk_keys_init(&k);
    press(&delayed, &k, dbl, 4, app, sizeof(app), sys, sizeof(sys));
    CHECK(!strcmp(app, "down up down up ") && !strcmp(sys, "ring "));
    // The ring open: a tap closes it, the app gets nothing of it.
    press(&delayed, &k, tap, 2, app, sizeof(app), sys, sizeof(sys));
    CHECK(!strcmp(app, "") && !strcmp(sys, "close ") && yk_keys_idle(&k));
    // A tap with the ring closed: the app's, once no double click can follow.
    press(&delayed, &k, tap, 2, app, sizeof(app), sys, sizeof(sys));
    CHECK(!strcmp(app, "down up tap ") && !strcmp(sys, ""));
    // The ring open, a double click: closed by its first press, nothing for the app, not opened again.
    press(&delayed, &k, dbl, 4, app, sizeof(app), sys, sizeof(sys));
    CHECK(!strcmp(sys, "ring "));
    press(&delayed, &k, dbl, 4, app, sizeof(app), sys, sizeof(sys));
    CHECK(!strcmp(app, "") && !strcmp(sys, "close ") && !k.ring && yk_keys_idle(&k));
}

#define SHA_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define SHA_B "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"

static void test_update_record(void) {
    yk_update_t u = {.state = YK_UPDATE_PENDING, .version = "1.0.1", .sha256 = SHA_A, .from = "1.0.0"}, back;
    char buf[160];
    CHECK(yk_update_format(buf, sizeof(buf), &u) > 0);
    CHECK(!strcmp(buf, "1|yay|1.0.1|" SHA_A "|pending|0|1.0.0"));
    CHECK(yk_update_parse(buf, &back) && back.state == YK_UPDATE_PENDING && !strcmp(back.version, "1.0.1") &&
          !strcmp(back.sha256, SHA_A) && !strcmp(back.from, "1.0.0") && back.tries == 0);
    u.state = YK_UPDATE_TRIAL, u.tries = 2, u.from[0] = 0;  // `from` may be empty
    CHECK(yk_update_format(buf, sizeof(buf), &u) > 0 && yk_update_parse(buf, &back) && back.state == YK_UPDATE_TRIAL &&
          back.tries == 2 && !back.from[0]);
    CHECK(yk_update_format(buf, 20, &u) == -1);  // does not fit
    // Not written by this firmware: dropped.
    CHECK(!yk_update_parse(NULL, &back));
    CHECK(!yk_update_parse("", &back));
    CHECK(!yk_update_parse("2|yay|1.0.1|" SHA_A "|pending|0|", &back));          // another format
    CHECK(!yk_update_parse("1|breathe|1.0.1|" SHA_A "|pending|0|", &back));      // only Yay updates this way
    CHECK(!yk_update_parse("1|yay|1.0.1|" SHA_A "|done|0|", &back));             // unknown state
    CHECK(!yk_update_parse("1|yay|1.0.1|" SHA_A "|pending|256|", &back));        // tries out of range
    CHECK(!yk_update_parse("1|yay|1.0.1|" SHA_A "|pending||", &back));           // no tries
    CHECK(!yk_update_parse("1|yay||" SHA_A "|pending|0|", &back));               // no version
    CHECK(!yk_update_parse("1|yay|1.0.1|AAAA|pending|0|", &back));               // not a SHA-256
    CHECK(!yk_update_parse("1|yay|1.0.1|" SHA_A "|pending|0", &back));           // six fields
    CHECK(!yk_update_parse("1|yay|1.0.1|" SHA_A "|pending|0||x", &back));        // eight
    CHECK(!yk_update_parse("1|yay|1 0|" SHA_A "|pending|0|", &back));            // a space in the version
    // A version that would break the record is never written.
    yk_update_t bad = u;
    snprintf(bad.version, sizeof(bad.version), "1|0");
    CHECK(yk_update_format(buf, sizeof(buf), &bad) == -1);
    bad = u, bad.sha256[0] = 'G';
    CHECK(yk_update_format(buf, sizeof(buf), &bad) == -1);
    bad = u, bad.state = YK_UPDATE_NONE;
    CHECK(yk_update_format(buf, sizeof(buf), &bad) == -1);
}

static yk_update_in_t upd(yk_update_state_t state, uint8_t tries, bool home_usable, bool crashed, bool new_cached,
                          bool previous) {
    yk_update_in_t in = {.has = state != YK_UPDATE_NONE, .home_usable = home_usable, .home_crashed = crashed,
                         .new_cached = new_cached, .previous_cached = previous};
    in.rec.state = state;
    in.rec.tries = tries;
    return in;
}

static void test_update_next(void) {
    yk_update_in_t in;
    // No update: on as before; slot 1 broken or empty (a write cut short) with a previous version: it goes back.
    in = upd(YK_UPDATE_NONE, 0, true, false, false, true);
    CHECK(yk_update_next(&in) == YK_UPDATE_GO);
    in = upd(YK_UPDATE_NONE, 0, false, false, false, true);
    CHECK(yk_update_next(&in) == YK_UPDATE_RESTORE);
    in = upd(YK_UPDATE_NONE, 0, false, false, false, false);
    CHECK(yk_update_next(&in) == YK_UPDATE_GO);  // nothing to restore: the recovery page
    in = upd(YK_UPDATE_NONE, 0, true, true, false, true);
    CHECK(yk_update_next(&in) == YK_UPDATE_GO);  // Yay crashing with no update going: the crash page, as before
    // Asked for: back up the Yay that runs first, then write.
    in = upd(YK_UPDATE_PENDING, 0, true, false, true, false);
    CHECK(yk_update_next(&in) == YK_UPDATE_BACKUP);
    in = upd(YK_UPDATE_PENDING, 0, true, false, false, true);
    CHECK(yk_update_next(&in) == YK_UPDATE_DROP);  // the image is not in the cache: nothing changes
    in = upd(YK_UPDATE_PENDING, 0, false, false, true, false);
    CHECK(yk_update_next(&in) == YK_UPDATE_WRITE);  // no usable Yay to keep: straight in
    in = upd(YK_UPDATE_PENDING, YK_UPDATE_TRIES, false, false, true, true);
    CHECK(yk_update_next(&in) == YK_UPDATE_ROLLBACK);
    // Backed up: write; the power went while writing (slot 1 empty): again, up to YK_UPDATE_TRIES writes.
    in = upd(YK_UPDATE_BACKED_UP, 0, true, false, true, true);
    CHECK(yk_update_next(&in) == YK_UPDATE_WRITE);
    in = upd(YK_UPDATE_BACKED_UP, 1, false, false, true, true);
    CHECK(yk_update_next(&in) == YK_UPDATE_WRITE);
    in = upd(YK_UPDATE_BACKED_UP, YK_UPDATE_TRIES, false, false, true, true);
    CHECK(yk_update_next(&in) == YK_UPDATE_ROLLBACK);
    // Refused before slot 1 was touched (a bad signature), twice: the Yay there still works, nothing is rewritten.
    in = upd(YK_UPDATE_BACKED_UP, YK_UPDATE_TRIES, true, false, true, true);
    CHECK(yk_update_next(&in) == YK_UPDATE_REJECT);
    in = upd(YK_UPDATE_BACKED_UP, 1, true, false, false, true);
    CHECK(yk_update_next(&in) == YK_UPDATE_REJECT);
    // A bad hash or image halfway: the install dropped the cached copy and slot 1 is empty: back to the previous one.
    in = upd(YK_UPDATE_BACKED_UP, 1, false, false, false, true);
    CHECK(yk_update_next(&in) == YK_UPDATE_ROLLBACK);
    in = upd(YK_UPDATE_BACKED_UP, 1, false, false, false, false);
    CHECK(yk_update_next(&in) == YK_UPDATE_DROP);  // nothing to go back to: the recovery page
    // The new one starts: on to it. It crashed three times at start, or slot 1 broke: back to the previous one.
    in = upd(YK_UPDATE_TRIAL, 1, true, false, true, true);
    CHECK(yk_update_next(&in) == YK_UPDATE_GO);
    in = upd(YK_UPDATE_TRIAL, 1, true, true, true, true);
    CHECK(yk_update_next(&in) == YK_UPDATE_ROLLBACK);
    in = upd(YK_UPDATE_TRIAL, 1, false, false, true, true);
    CHECK(yk_update_next(&in) == YK_UPDATE_ROLLBACK);
    in = upd(YK_UPDATE_TRIAL, 1, true, true, true, false);
    CHECK(yk_update_next(&in) == YK_UPDATE_DROP);  // crashing, nothing to go back to: the crash page
    // Confirmed (record gone), it crashes three times much later: the crash page, not the previous version.
    in = upd(YK_UPDATE_NONE, 0, true, true, false, true);
    CHECK(yk_update_next(&in) == YK_UPDATE_GO);
    CHECK(!strcmp(yk_update_step_name(YK_UPDATE_REJECT), "reject"));
    CHECK(!strcmp(yk_update_step_name(YK_UPDATE_ROLLBACK), "rollback") &&
          !strcmp(yk_update_state_name(YK_UPDATE_BACKED_UP), "backed_up"));
}

static void test_update_confirm(void) {
    yk_update_t u = {.state = YK_UPDATE_TRIAL, .version = "1.0.1", .sha256 = SHA_A};
    // On trial, the main firmware in slot 1: confirmed a stable minute after yk_app_ready(), online or not (hello.ok
    // only sooner). A crash before then leaves it on trial: three of them and the recovery goes back.
    CHECK(yk_update_confirm_after(&u, true) == YK_UPDATE_CONFIRM_MS && YK_UPDATE_CONFIRM_MS == 60000);
    CHECK(YK_UPDATE_CONFIRM_MS > YK_CRASHLOOP_STABLE_MS);
    CHECK(yk_update_confirm_after(&u, false) == -1);  // an app, or Yay not in slot 1: not its update
    CHECK(yk_update_confirm_after(NULL, true) == -1);  // no record
    u.state = YK_UPDATE_PENDING;
    CHECK(yk_update_confirm_after(&u, true) == -1);  // the Yay that asked: the recovery has not written the new one
    u.state = YK_UPDATE_BACKED_UP;
    CHECK(yk_update_confirm_after(&u, true) == -1);
    // The cache keeps the new image and the previous version from the request until the update is kept.
    CHECK(yk_update_holds_cache(&u));
    u.state = YK_UPDATE_PENDING;
    CHECK(yk_update_holds_cache(&u));
    u.state = YK_UPDATE_TRIAL;
    CHECK(yk_update_holds_cache(&u));
    u.state = YK_UPDATE_NONE;
    CHECK(!yk_update_holds_cache(&u) && !yk_update_holds_cache(NULL));
}

/** A tap on an extension's app ring (app-ecosystem.md 4.5): 「更多」 goes home with {"then":"more"}. */
static void test_ring_tapped(void) {
    char p[96];
    CHECK(yk_ring_tapped(NULL, "breathe", false, p, sizeof(p)) == YK_RING_STAY && p[0] == 0);
    CHECK(yk_ring_tapped("", "breathe", false, p, sizeof(p)) == YK_RING_STAY);
    CHECK(yk_ring_tapped("breathe", "breathe", true, p, sizeof(p)) == YK_RING_STAY);  // the app in use
    CHECK(yk_ring_tapped("yay", "breathe", false, p, sizeof(p)) == YK_RING_HOME && p[0] == 0);
    CHECK(yk_ring_tapped(YK_RING_SETTINGS, "breathe", false, p, sizeof(p)) == YK_RING_HOME_WITH);
    CHECK(!strcmp(p, "{\"then\":\"settings\"}"));
    CHECK(yk_ring_tapped(YK_RING_MORE, "breathe", false, p, sizeof(p)) == YK_RING_HOME_WITH);
    CHECK(!strcmp(p, "{\"then\":\"more\"}"));
    CHECK(yk_ring_tapped("party-play", "breathe", true, p, sizeof(p)) == YK_RING_OPEN && p[0] == 0);
    CHECK(yk_ring_tapped("translator", "breathe", false, p, sizeof(p)) == YK_RING_HOME_WITH);
    CHECK(!strcmp(p, "{\"then\":\"install\",\"slug\":\"translator\"}"));
    CHECK(!strcmp(YK_RING_MORE, "+more"));
    CHECK(!strcmp(YK_RING_SETTINGS, "+settings"));  // yaykit_ui's YK_APPS_SETTINGS
    // The app in use, with a 应用码 or not: nothing since 0.1.14 (the ring only switches apps; the code is the system
    // pull-down's, system-pulldown.md 8).
    CHECK(yk_ring_tapped("recording-card", "recording-card", true, p, sizeof(p)) == YK_RING_STAY && p[0] == 0);
}

/** The new recovery (P3): the battery rule, which steps it guards, the automatic mark. */
static void test_update_p3(void) {
    // On battery under 30 %: no; at 30, on external power at any level, or not known: yes.
    CHECK(!yk_update_battery_ok(29, false));
    CHECK(!yk_update_battery_ok(0, false));
    CHECK(yk_update_battery_ok(30, false));
    CHECK(yk_update_battery_ok(5, true));
    CHECK(yk_update_battery_ok(-1, false));
    // Guarded: the backup, and the first write over a slot 1 that works; not a write after a power cut, nor going back.
    yk_update_in_t in = upd(YK_UPDATE_PENDING, 0, true, false, true, true);
    CHECK(yk_update_next(&in) == YK_UPDATE_BACKUP && yk_update_step_guarded(YK_UPDATE_BACKUP, &in));
    in = upd(YK_UPDATE_BACKED_UP, 0, true, false, true, true);
    CHECK(yk_update_next(&in) == YK_UPDATE_WRITE && yk_update_step_guarded(YK_UPDATE_WRITE, &in));
    in = upd(YK_UPDATE_BACKED_UP, 1, false, false, true, true);  // cut short halfway: slot 1 may be broken
    CHECK(yk_update_next(&in) == YK_UPDATE_WRITE && !yk_update_step_guarded(YK_UPDATE_WRITE, &in));
    in = upd(YK_UPDATE_BACKED_UP, 1, true, false, true, true);  // written once already
    CHECK(!yk_update_step_guarded(YK_UPDATE_WRITE, &in));
    in = upd(YK_UPDATE_PENDING, 0, false, false, true, true);  // no usable Yay to keep: straight in, not guarded
    CHECK(yk_update_next(&in) == YK_UPDATE_WRITE && !yk_update_step_guarded(YK_UPDATE_WRITE, &in));
    CHECK(!yk_update_step_guarded(YK_UPDATE_RESTORE, &in) && !yk_update_step_guarded(YK_UPDATE_ROLLBACK, &in));
    CHECK(!yk_update_step_guarded(YK_UPDATE_GO, &in) && !yk_update_step_guarded(YK_UPDATE_REJECT, &in));
    CHECK(!yk_update_step_guarded(YK_UPDATE_WRITE, NULL));
    // Automatic only when the mark holds this record's sha256.
    const yk_update_t u = {.state = YK_UPDATE_PENDING, .version = "1.0.1", .sha256 = SHA_A, .from = "1.0.0"};
    CHECK(yk_update_auto_matches(SHA_A, &u));
    CHECK(!yk_update_auto_matches(SHA_B, &u));  // left from another update
    CHECK(!yk_update_auto_matches("", &u) && !yk_update_auto_matches(NULL, &u) && !yk_update_auto_matches(SHA_A, NULL));
    CHECK(!yk_update_auto_matches("1", &u));
    // The record stays seven fields, which every recovery since 0.1.0 reads: the mark never goes into it.
    char buf[160];
    yk_update_t back;
    CHECK(yk_update_format(buf, sizeof(buf), &u) > 0 && yk_update_parse(buf, &back));
    int bars = 0;
    for (const char *p = buf; *p; p++) bars += *p == '|';
    CHECK(bars == 6);
}

static void test_recovery(void) {
    // The six released recoveries: each one's ELF key and length give its version, sha256 and bytes
    static const struct {
        uint8_t elf[YK_RECOVERY_ELF_KEY];
        uint32_t size;
        const char *version, *sha_start;
    } released[] = {
        {{0xa4, 0x6d, 0xf9, 0xf5, 0x5a, 0x54, 0x47, 0xce}, 1609904, "0.1.0", "e38d0934112584ad"},
        {{0x1c, 0x9b, 0x03, 0x2d, 0xfc, 0x9c, 0xf8, 0x09}, 1609680, "0.1.1", "0734ed73f13d0a81"},
        {{0x6a, 0x58, 0xc5, 0x49, 0x94, 0xcc, 0x75, 0x05}, 1609680, "0.1.2", "6f61d5a71a3f18f4"},
        {{0x95, 0x5e, 0x05, 0xde, 0x33, 0x6b, 0x16, 0xa8}, 1616384, "0.1.3", "e96787bd89c3bc2b"},
        {{0xbe, 0x93, 0x0f, 0x2d, 0xf9, 0x8a, 0x47, 0xed}, 1616592, "0.1.4", "51b7736e02ffcd7c"},
        {{0x29, 0x76, 0xa4, 0xb3, 0xf5, 0x47, 0xe7, 0xea}, 1616624, "0.1.5", "417b48262d11e981"},
    };
    yk_recovery_t r;
    for (size_t i = 0; i < sizeof(released) / sizeof(released[0]); i++) {
        yk_recovery_identify(released[i].elf, "0.1.0", released[i].size, &r);
        CHECK(r.from == YK_RECOVERY_TABLE);
        CHECK(!strcmp(r.version, released[i].version));
        CHECK(strlen(r.sha256) == 64 && !strncmp(r.sha256, released[i].sha_start, 16));
        CHECK(r.size == released[i].size);
    }
    // Same ELF key, another length: not that release (changed after the build), hashed
    yk_recovery_identify(released[3].elf, "0.1.0", 1616400, &r);
    CHECK(r.from == YK_RECOVERY_PENDING && !strcmp(r.version, YK_RECOVERY_UNKNOWN) && !r.sha256[0] && r.size == 1616400);
    // Not in the table: the description's version unless it is the "0.1.0" every release says
    const uint8_t other[YK_RECOVERY_ELF_KEY] = {1, 2, 3, 4, 5, 6, 7, 8};
    yk_recovery_identify(other, "0.1.0", 1700000, &r);
    CHECK(r.from == YK_RECOVERY_PENDING && !strcmp(r.version, YK_RECOVERY_UNKNOWN));
    yk_recovery_identify(other, "0.1.6", 1700000, &r);  // P3: the recovery's version follows Yay's package
    CHECK(r.from == YK_RECOVERY_PENDING && !strcmp(r.version, "0.1.6") && !r.sha256[0]);
    yk_recovery_identify(other, "", 1700000, &r);
    CHECK(!strcmp(r.version, YK_RECOVERY_UNKNOWN));
    yk_recovery_identify(other, NULL, 1700000, &r);
    CHECK(!strcmp(r.version, YK_RECOVERY_UNKNOWN));
    yk_recovery_identify(other, "0.1.6-a-very-long-tag", 1700000, &r);  // does not fit: unknown, not cut
    CHECK(!strcmp(r.version, YK_RECOVERY_UNKNOWN));
    CHECK(!strcmp(yk_recovery_from_name(YK_RECOVERY_TABLE), "table"));
    CHECK(!strcmp(yk_recovery_from_name(YK_RECOVERY_NONE), "none"));

    // Hashed, kept in NVS, read again on the next start
    const char *sha = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    char rec[128];
    yk_recovery_identify(other, "0.1.0", 1700000, &r);
    CHECK(yk_recovery_record(rec, sizeof(rec), &r) == -1);  // nothing hashed yet
    CHECK(!yk_recovery_hashed(&r, "0123"));
    CHECK(!yk_recovery_hashed(&r, "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef"));  // upper case
    CHECK(r.from == YK_RECOVERY_PENDING && !r.sha256[0]);
    CHECK(yk_recovery_hashed(&r, sha) && r.from == YK_RECOVERY_HASHED && !strcmp(r.sha256, sha));
    CHECK(yk_recovery_record(rec, sizeof(rec), &r) > 0);
    CHECK(!strcmp(rec, "1|0102030405060708|1700000|0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    CHECK(yk_recovery_record(rec, 40, &r) == -1);  // does not fit
    yk_recovery_t next;
    yk_recovery_identify(other, "0.1.0", 1700000, &next);
    CHECK(yk_recovery_record(rec, sizeof(rec), &r) > 0 && yk_recovery_from_record(rec, &next));
    CHECK(next.from == YK_RECOVERY_NVS && !strcmp(next.sha256, sha) && !strcmp(next.version, YK_RECOVERY_UNKNOWN));
    // A record of another recovery (replaced over the web USB): hashed again
    yk_recovery_identify(other, "0.1.0", 1700016, &next);
    CHECK(!yk_recovery_from_record(rec, &next) && next.from == YK_RECOVERY_PENDING && !next.sha256[0]);
    const uint8_t third[YK_RECOVERY_ELF_KEY] = {1, 2, 3, 4, 5, 6, 7, 9};
    yk_recovery_identify(third, "0.1.0", 1700000, &next);
    CHECK(!yk_recovery_from_record(rec, &next) && next.from == YK_RECOVERY_PENDING);
    // Not well formed
    yk_recovery_identify(other, "0.1.0", 1700000, &next);
    CHECK(!yk_recovery_from_record(NULL, &next));
    CHECK(!yk_recovery_from_record("", &next));
    CHECK(!yk_recovery_from_record("2|0102030405060708|1700000|" "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", &next));
    CHECK(!yk_recovery_from_record("1|0102030405060708|1700000|0123", &next));
    CHECK(!yk_recovery_from_record("1|0102030405060708||0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", &next));
    CHECK(!yk_recovery_from_record("1|0102030405060708|1700000x|0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", &next));
    CHECK(!yk_recovery_from_record("1|0102030405060708|1700000|0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0", &next));
    CHECK(next.from == YK_RECOVERY_PENDING && !next.sha256[0]);
}

// ---------------------------------------------------------------- the battery guard and the settings (0.1.13)

static void test_power_boot(void) {
    // On USB an app always starts, whatever the reading and the reset.
    CHECK(!yk_power_boot_sleep(false, 0, true));
    CHECK(!yk_power_boot_sleep(true, 3, true));
    // On battery: at YK_POWER_EMPTY (about 3.50 V) and below it sleeps before the screen; above, it starts.
    CHECK(yk_power_boot_sleep(false, YK_POWER_EMPTY, false));
    CHECK(yk_power_boot_sleep(false, 0, false));
    CHECK(!yk_power_boot_sleep(false, YK_POWER_EMPTY + 1, false));
    CHECK(!yk_power_boot_sleep(false, 80, false));
    // Not read (-1): it starts.
    CHECK(!yk_power_boot_sleep(false, -1, false));
    // A brownout on battery: it sleeps whatever the reading (the cell recovers at rest, then dips again lit).
    CHECK(yk_power_boot_sleep(true, 40, false));
    CHECK(yk_power_boot_sleep(true, -1, false));
}

static void test_power_watch(void) {
    yk_power_t p;
    yk_power_init(&p);
    CHECK(p.percent == -1 && !yk_power_low(&p) && yk_power_read_ms(&p) == YK_POWER_READ_MS);
    CHECK(yk_power_feed(&p, 60, false) == YK_POWER_NONE);
    CHECK(!yk_power_low(&p) && yk_power_read_ms(&p) == YK_POWER_READ_MS);
    // 15 %: low once, read more often, caps on.
    CHECK(yk_power_feed(&p, YK_POWER_LOW, false) == YK_POWER_LOW_NOW);
    CHECK(yk_power_low(&p) && yk_power_read_ms(&p) == YK_POWER_READ_LOW_MS);
    CHECK(yk_power_feed(&p, 12, false) == YK_POWER_NONE);
    // Not read: nothing changes, nothing said.
    CHECK(yk_power_feed(&p, -1, false) == YK_POWER_NONE && p.percent == 12 && yk_power_low(&p));
    // One reading at 5 % (the speaker pulled it down) is not a sleep; a better one in between starts the count again.
    CHECK(yk_power_feed(&p, YK_POWER_EMPTY, false) == YK_POWER_NONE && p.empty_reads == 1);
    CHECK(yk_power_feed(&p, 7, false) == YK_POWER_NONE && p.empty_reads == 0);
    CHECK(yk_power_feed(&p, 4, false) == YK_POWER_NONE);
    // The second in a row: sleep, said once.
    CHECK(yk_power_feed(&p, 3, false) == YK_POWER_SLEEP);
    CHECK(yk_power_feed(&p, 3, false) == YK_POWER_NONE);
}

static void test_power_usb(void) {
    yk_power_t p;
    yk_power_init(&p);
    // Started on USB: no news; a reading at 3 % on USB (charging) is never low nor a sleep.
    CHECK(yk_power_feed(&p, 50, true) == YK_POWER_NONE);
    CHECK(yk_power_feed(&p, 3, true) == YK_POWER_NONE && !yk_power_low(&p));
    CHECK(yk_power_feed(&p, 3, true) == YK_POWER_NONE);
    // Unplugged at 14 %: low; plugged in again: caps lifted, the count gone.
    CHECK(yk_power_feed(&p, 14, false) == YK_POWER_LOW_NOW);
    CHECK(yk_power_feed(&p, 5, false) == YK_POWER_NONE && p.empty_reads == 1);
    CHECK(yk_power_feed(&p, 5, true) == YK_POWER_PLUGGED);
    CHECK(!yk_power_low(&p) && p.empty_reads == 0 && yk_power_read_ms(&p) == YK_POWER_READ_MS);
    // Out again: a new discharge, low said again.
    CHECK(yk_power_feed(&p, 15, false) == YK_POWER_LOW_NOW);
    CHECK(!strcmp(yk_power_ev_name(YK_POWER_SLEEP), "sleep") && !strcmp(yk_power_ev_name((yk_power_ev_t)9), "?"));
}

static void test_settings(void) {
    yk_settings_t s;
    // As stored, Chinese for "zh", "zh-Hans", none.
    yk_settings_merge(4, 30, 15, "zh-Hans", false, false, &s);
    CHECK(s.volume == 4 && s.brightness == 30 && s.screen_off_s == 15 && !s.english);
    yk_settings_merge(6, 80, 0, "zh", false, false, &s);
    CHECK(s.screen_off_s == 0 && !s.english);
    yk_settings_merge(6, 80, 30, NULL, false, false, &s);
    CHECK(!s.english);
    yk_settings_merge(6, 80, 30, "", false, false, &s);
    CHECK(!s.english);
    // Any other language: English screens (as yaykit_cloud's yk_locale_english).
    CHECK(yk_settings_english("en") && yk_settings_english("ja") && yk_settings_english("es-419"));
    CHECK(!yk_settings_english("ZH-hant") && !yk_settings_english("zh"));
    // Low battery: brightness at most 60, screen off after 15 s even when set to never; a dimmer setting stays.
    yk_settings_merge(6, 100, 0, "en", true, false, &s);
    CHECK(s.brightness == YK_POWER_BRIGHTNESS_MAX && s.screen_off_s == YK_POWER_SCREEN_OFF_LOW_S && s.english);
    yk_settings_merge(6, 40, 300, "en", true, false, &s);
    CHECK(s.brightness == 40 && s.screen_off_s == YK_POWER_SCREEN_OFF_LOW_S);
    // The app keeps the screen on (a session, a round, a recording): never off, low battery or not.
    yk_settings_merge(6, 80, 30, "zh", false, true, &s);
    CHECK(s.screen_off_s == 0);
    yk_settings_merge(6, 80, 30, "zh", true, true, &s);
    CHECK(s.screen_off_s == 0 && s.brightness == YK_POWER_BRIGHTNESS_MAX);
    // The 应用码 the app ring showed keeps the screen on with a switch of its own (BOSS-2 11-02): the app's keep_on is
    // still there once the code goes; with the app's off, the screen goes off after the set time again.
    yk_keep_on_t keep = {.app = true};
    keep.code = true;
    yk_settings_merge(6, 80, 30, "zh", false, yk_keep_on(&keep), &s);
    CHECK(s.screen_off_s == 0);
    keep.code = false;  // the code went
    yk_settings_merge(6, 80, 30, "zh", false, yk_keep_on(&keep), &s);
    CHECK(keep.app && s.screen_off_s == 0);
    keep = (yk_keep_on_t){.code = true};
    yk_settings_merge(6, 80, 30, "zh", false, yk_keep_on(&keep), &s);
    CHECK(s.screen_off_s == 0);
    yk_settings_merge(6, 80, 30, "zh", true, yk_keep_on(&keep), &s);  // low battery: as the app's keep_on
    CHECK(s.screen_off_s == 0 && s.brightness == YK_POWER_BRIGHTNESS_MAX);
    keep.code = false;
    yk_settings_merge(6, 80, 30, "zh", false, yk_keep_on(&keep), &s);
    CHECK(!keep.app && s.screen_off_s == 30);
    yk_settings_merge(6, 80, 30, "zh", true, yk_keep_on(&keep), &s);
    CHECK(s.screen_off_s == YK_POWER_SCREEN_OFF_LOW_S);
    // Volume stays in yaykit_speak's steps.
    yk_settings_merge(12, 80, 30, "zh", false, false, &s);
    CHECK(s.volume == 10);
    yk_settings_merge(-1, 80, 30, "zh", false, false, &s);
    CHECK(s.volume == 0);
}

/** The folders' question (Yay 0.1.19 开发文档 4.6): it goes up only when nothing of the system's is; a press is 「这次先不
 *  给」 and is eaten whole, a double click opens no ring; the app's press going on ends with the app. */
static void test_keys_grant(void) {
    yk_keys_t k;
    yk_keys_init(&k);
    CHECK(yk_keys_grant_open(&k) && k.grant && !yk_keys_idle(&k));
    CHECK(!yk_keys_sheet_open(&k) && !yk_keys_ask(&k, false, false));  // no pull-down, no 「回到 Yay 说？」 over it
    CHECK(yk_keys_route(&k, DOWN, 1000, false) == YK_KEYS_GRANT_CLOSE && !k.grant);
    CHECK(yk_keys_route(&k, UP, 1080, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, TAP, 1080, false) == YK_KEYS_EAT && yk_keys_idle(&k));
    CHECK(yk_keys_route(&k, DOWN, 3000, false) == YK_KEYS_APP);  // the app's again
    yk_keys_route(&k, UP, 3080, false);
    yk_keys_route(&k, TAP, 3080, false);
    // A double click while it is up: let go, and no ring.
    CHECK(yk_keys_grant_open(&k));
    CHECK(yk_keys_route(&k, DOWN, 5000, false) == YK_KEYS_GRANT_CLOSE);
    CHECK(yk_keys_route(&k, UP, 5080, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, DOWN, 5200, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, UP, 5280, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, DOUBLE, 5280, false) == YK_KEYS_EAT && yk_keys_idle(&k) && !k.ring);
    // Held: let go, the hold and its release the system's.
    CHECK(yk_keys_grant_open(&k));
    CHECK(yk_keys_route(&k, DOWN, 8000, false) == YK_KEYS_GRANT_CLOSE);
    CHECK(yk_keys_route(&k, HOLD, 8300, false) == YK_KEYS_EAT && !yk_keys_ask(&k, false, false));
    CHECK(yk_keys_route(&k, UP, 9000, false) == YK_KEYS_EAT && yk_keys_idle(&k));
    // Answered on the screen: the key is the app's at once.
    CHECK(yk_keys_grant_open(&k));
    yk_keys_grant_closed(&k);
    CHECK(yk_keys_idle(&k) && yk_keys_route(&k, DOWN, 12000, false) == YK_KEYS_APP);
    yk_keys_route(&k, UP, 12080, false);
    yk_keys_route(&k, TAP, 12080, false);
    // Not over the ring, the pull-down, the code.
    yk_keys_route(&k, DOUBLE, 20000, false);
    CHECK(k.ring && !yk_keys_grant_open(&k) && !k.grant);
    yk_keys_init(&k);
    CHECK(yk_keys_sheet_open(&k) && !yk_keys_grant_open(&k));
    yk_keys_init(&k);
    yk_keys_code(&k, true);
    CHECK(!yk_keys_grant_open(&k));
    // Asked while the app's press goes on (a hold on 「播放」): up at once; that press's UP is the app's, the next
    // press lets it go.
    yk_keys_init(&k);
    CHECK(yk_keys_route(&k, DOWN, 30000, false) == YK_KEYS_APP);
    CHECK(yk_keys_grant_open(&k));
    CHECK(yk_keys_route(&k, UP, 30500, false) == YK_KEYS_APP && k.grant);
    CHECK(yk_keys_route(&k, DOWN, 32000, false) == YK_KEYS_GRANT_CLOSE);
    yk_keys_route(&k, UP, 32080, false);
    CHECK(yk_keys_route(&k, TAP, 32080, false) == YK_KEYS_EAT && yk_keys_idle(&k));
}

/** The system pull-down in an extension (docs/delivery/2026-11-02-system-pulldown.md): it opens only when nothing of the
 *  system's is up; a press closes it and all of that press is the system's; a double click to close it opens no ring. */
static void test_keys_sheet(void) {
    yk_keys_t k;
    yk_keys_init(&k);
    CHECK(yk_keys_sheet_open(&k) && k.sheet && !yk_keys_idle(&k));
    CHECK(!yk_keys_ask(&k, false, false));  // (a hold cannot be the app's while it is up anyway)
    CHECK(yk_keys_route(&k, DOWN, 1000, false) == YK_KEYS_SHEET_CLOSE && !k.sheet);
    CHECK(yk_keys_route(&k, UP, 1080, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, TAP, 1080, false) == YK_KEYS_EAT && yk_keys_idle(&k));
    CHECK(yk_keys_route(&k, DOWN, 3000, false) == YK_KEYS_APP);  // the app's again
    yk_keys_route(&k, UP, 3080, false);
    yk_keys_route(&k, TAP, 3080, false);
    // A double click while it is up: the first press closes it, the double click opens no ring (delay_tap or not).
    CHECK(yk_keys_sheet_open(&k));
    CHECK(yk_keys_route(&k, DOWN, 5000, false) == YK_KEYS_SHEET_CLOSE);
    CHECK(yk_keys_route(&k, UP, 5080, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, DOWN, 5200, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, UP, 5280, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, DOUBLE, 5280, false) == YK_KEYS_EAT && yk_keys_idle(&k) && !k.ring);
    // Held: closes it, the hold and its release are the system's (no 「回到主屏幕说？」).
    CHECK(yk_keys_sheet_open(&k));
    CHECK(yk_keys_route(&k, DOWN, 8000, false) == YK_KEYS_SHEET_CLOSE);
    CHECK(yk_keys_route(&k, HOLD, 8300, false) == YK_KEYS_EAT && !yk_keys_ask(&k, false, false));
    CHECK(yk_keys_route(&k, UP, 9000, false) == YK_KEYS_EAT && yk_keys_idle(&k));
    // Closed by a touch: the key is the app's at once.
    CHECK(yk_keys_sheet_open(&k));
    yk_keys_sheet_closed(&k);
    CHECK(yk_keys_idle(&k) && yk_keys_route(&k, DOWN, 12000, false) == YK_KEYS_APP);
    yk_keys_route(&k, UP, 12080, false);
    yk_keys_route(&k, TAP, 12080, false);
    // Not over the ring, 「回到主屏幕说？」, the code, nor the rest of a press that closed one of them.
    yk_keys_route(&k, DOUBLE, 20000, false);
    CHECK(k.ring && !yk_keys_sheet_open(&k) && !k.sheet);
    yk_keys_route(&k, DOWN, 21000, false);  // closes the ring; its press goes on
    CHECK(!yk_keys_sheet_open(&k));
    yk_keys_route(&k, UP, 21080, false);
    yk_keys_route(&k, TAP, 21080, false);
    CHECK(yk_keys_idle(&k) && yk_keys_sheet_open(&k));
    yk_keys_init(&k);
    yk_keys_code(&k, true);
    CHECK(!yk_keys_sheet_open(&k));
    yk_keys_init(&k);
    yk_keys_route(&k, DOWN, 1000, false);
    yk_keys_route(&k, HOLD, 1300, false);
    CHECK(yk_keys_ask(&k, false, false) && !yk_keys_sheet_open(&k));
    // The key down in the app (11-09): no pull-down until its UP, which is the app's (an app that opened the
    // microphone at DOWN closes it at UP; eaten, it would listen on with no one on the key).
    yk_keys_init(&k);
    CHECK(yk_keys_route(&k, DOWN, 30000, false) == YK_KEYS_APP);
    CHECK(!yk_keys_sheet_open(&k) && !k.sheet && yk_keys_idle(&k));
    CHECK(yk_keys_route(&k, HOLD, 30300, false) == YK_KEYS_APP && !yk_keys_sheet_open(&k));
    CHECK(yk_keys_route(&k, UP, 31000, false) == YK_KEYS_APP);
    CHECK(yk_keys_sheet_open(&k) && k.sheet);  // up: it opens
    CHECK(yk_keys_route(&k, DOWN, 32000, false) == YK_KEYS_SHEET_CLOSE);  // and a press closes it, as before
    CHECK(yk_keys_route(&k, UP, 32080, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, TAP, 32080, false) == YK_KEYS_EAT && yk_keys_idle(&k));
    // The 应用码 up while the app's press goes on (the pull-down's button tapped as the key went down): that press's
    // HOLD is the system's, its UP the app's; the code stays up, the next press hides it.
    yk_keys_init(&k);
    CHECK(yk_keys_route(&k, DOWN, 40000, false) == YK_KEYS_APP);
    yk_keys_code(&k, true);
    CHECK(yk_keys_route(&k, HOLD, 40300, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, UP, 41000, false) == YK_KEYS_APP && k.code);
    CHECK(yk_keys_route(&k, DOWN, 42000, false) == YK_KEYS_CODE_CLOSE);
    CHECK(yk_keys_route(&k, UP, 42080, false) == YK_KEYS_EAT);
    CHECK(yk_keys_route(&k, TAP, 42080, false) == YK_KEYS_EAT && yk_keys_idle(&k));
    // ... a short press: its UP the app's, its TAP (after the UP) the system's.
    yk_keys_init(&k);
    CHECK(yk_keys_route(&k, DOWN, 50000, false) == YK_KEYS_APP);
    yk_keys_code(&k, true);
    CHECK(yk_keys_route(&k, UP, 50080, false) == YK_KEYS_APP);
    CHECK(yk_keys_route(&k, TAP, 50080, false) == YK_KEYS_EAT && k.code);
    // 「回到主屏幕说？」 is as before: the release of the hold that asked is the card's (P23).
    yk_keys_init(&k);
    yk_keys_route(&k, DOWN, 60000, false);
    yk_keys_route(&k, HOLD, 60300, false);
    CHECK(yk_keys_ask(&k, false, false));
    CHECK(yk_keys_route(&k, UP, 61000, false) == YK_KEYS_EAT && !k.app_down);
}

/** The way in from the top edge (8.4): a press on the edge's strip that goes far enough down, more down than sideways;
 *  nothing else, and only once a press. */
static void test_pull(void) {
    yk_pull_t p;
    yk_pull_start(&p, 233, 10, YK_PULL_EDGE_PX);
    CHECK(p.armed && !yk_pull_move(&p, 236, 30, YK_PULL_PX) && p.dy_max == 20);
    CHECK(yk_pull_move(&p, 240, 50, YK_PULL_PX) && p.pulled);
    CHECK(!yk_pull_move(&p, 240, 120, YK_PULL_PX));  // once
    // Below the edge: the app's, however far it goes.
    yk_pull_start(&p, 233, YK_PULL_EDGE_PX, YK_PULL_EDGE_PX);
    CHECK(!p.armed && !yk_pull_move(&p, 233, 200, YK_PULL_PX));
    yk_pull_start(&p, 233, 120, YK_PULL_EDGE_PX);
    CHECK(!yk_pull_move(&p, 233, 300, YK_PULL_PX));
    // Sideways more than down: not a pull, until it goes down enough after all.
    yk_pull_start(&p, 150, 20, YK_PULL_EDGE_PX);
    CHECK(!yk_pull_move(&p, 210, 65, YK_PULL_PX));  // 60 across, 45 down
    CHECK(yk_pull_move(&p, 200, 90, YK_PULL_PX));   // 50 across, 70 down
    // Up: never.
    yk_pull_start(&p, 233, 30, YK_PULL_EDGE_PX);
    CHECK(!yk_pull_move(&p, 233, 0, YK_PULL_PX) && p.dy_max == 0);
    // Other numbers (the development command measures with them).
    yk_pull_start(&p, 233, 50, 60);
    CHECK(p.armed && !yk_pull_move(&p, 233, 70, 30) && yk_pull_move(&p, 233, 80, 30));
    yk_pull_start(&p, 233, -1, YK_PULL_EDGE_PX);
    CHECK(!p.armed);
}

/** A tap at (x, y), press number `press`, down at `down` ms and up 80 ms later. */
static yk_dtap_tap_t tap_at(int x, int y, uint32_t press, uint32_t down) {
    return (yk_dtap_tap_t){.x = (int16_t)x, .y = (int16_t)y, .down_ms = down, .up_ms = down + 80, .press = press};
}

/** The tap's whole press, as yk_app_ring.c hands it on: its finger down (true: the held tap given then), then its
 *  release. `*gave`: the held tap was given, at the press or at the release. */
static yk_dtap_do_t dtap(yk_dtap_t *d, yk_keys_t *k, bool on, const yk_dtap_tap_t *t, bool *gave) {
    bool at_press = false, at_release = false;
    if (on) at_press = yk_dtap_press(d, t->x, t->y, t->down_ms, t->press);
    const yk_dtap_do_t r = yk_dtap_route(d, k, on, t, &at_release);
    if (gave) *gave = at_press || at_release;
    return r;
}

/** The clock from `from` to `to` ms (one ms at a time): how many times the held tap was given. */
static int dtap_clock(yk_dtap_t *d, uint32_t from, uint32_t to) {
    int n = 0;
    for (uint32_t t = from; t != to + 1; t++) n += yk_dtap_tick(d, t);
    return n;
}

/** A double tap on the screen in an extension (kit 1.11, yk_app_config_t.double_tap_ring; Yay 0.1.20 dev doc 4.7): off,
 *  every tap the app's at once; on, a first tap's clicks held for the window and given late, or, the second of two
 *  quick near taps opening the ring, nobody's; with the ring up, a double tap on its paper closes it and gives the app
 *  nothing (Yay 0.1.21, 项目总策划 11-18). */
static void test_dtap(void) {
    yk_dtap_t d;
    yk_keys_t k;
    bool gave;
    // Off (the default): every tap the app's at once, however quick and near; nothing kept, the ring never.
    yk_dtap_init(&d);
    yk_keys_init(&k);
    yk_dtap_tap_t a = tap_at(233, 233, 1, 1000), b = tap_at(236, 230, 2, 1180);
    CHECK(dtap(&d, &k, false, &a, &gave) == YK_DTAP_APP && !gave);
    CHECK(dtap(&d, &k, false, &b, &gave) == YK_DTAP_APP && !gave && !k.ring && !yk_dtap_waiting(&d));
    CHECK(dtap_clock(&d, 1080, 3000) == 0);

    // 1. On, one tap: held, and the app's a window late. Its finger let go at 1080: up to 1530 a second could still
    // come (450 ms); at 1531 the clicks are the app's, once.
    yk_dtap_init(&d);
    CHECK(dtap(&d, &k, true, &a, &gave) == YK_DTAP_HOLD && !gave && yk_dtap_waiting(&d) && !k.ring);
    CHECK(dtap_clock(&d, 1080, 1080 + YK_DTAP_MS) == 0 && yk_dtap_waiting(&d));
    CHECK(yk_dtap_tick(&d, 1080 + YK_DTAP_MS + 1) && !yk_dtap_waiting(&d));
    CHECK(dtap_clock(&d, 1532, 5000) == 0);

    // 2. On, a double: the ring opens and the app gets neither tap's clicks (the first's held ones dropped, never given).
    yk_dtap_init(&d);
    yk_keys_init(&k);
    CHECK(dtap(&d, &k, true, &a, &gave) == YK_DTAP_HOLD && !gave);
    CHECK(!yk_dtap_press(&d, b.x, b.y, b.down_ms, b.press) && d.second);  // it may be the second: still held
    CHECK(dtap_clock(&d, 1180, 1200) == 0);  // held while the second's finger is down
    CHECK(yk_dtap_route(&d, &k, true, &b, &gave) == YK_DTAP_RING && !gave && k.ring && !yk_dtap_waiting(&d));
    CHECK(dtap_clock(&d, 1260, 5000) == 0);  // the first's never given
    // The key's next press closes it.
    CHECK(yk_keys_route(&k, DOWN, 2000, false) == YK_KEYS_RING_CLOSE && !k.ring);
    yk_keys_route(&k, UP, 2080, false);
    yk_keys_route(&k, TAP, 2080, false);
    CHECK(yk_keys_idle(&k));

    // 3. The ring up: a tap on its paper is the ring's (it closes it at once); the second of that double is nobody's.
    yk_dtap_init(&d);
    yk_keys_init(&k);
    k.ring = true;
    yk_dtap_tap_t p1 = tap_at(60, 233, 70, 60000), p2 = tap_at(64, 236, 71, 60250), p3 = tap_at(64, 236, 72, 60500);
    p1.paper = true;
    CHECK(dtap(&d, &k, true, &p1, &gave) == YK_DTAP_APP && !gave && d.armed && d.eat);
    yk_keys_closed(&k);  // its CLICKED closed the ring (folded back in 180 ms)
    CHECK(dtap(&d, &k, true, &p2, &gave) == YK_DTAP_EAT && !gave && !k.ring && !yk_dtap_waiting(&d));
    CHECK(dtap(&d, &k, true, &p3, &gave) == YK_DTAP_HOLD);  // a third is a first again
    // ... its second landing on the ring still folding back (still up, no paper then): the ring's, and over.
    yk_dtap_init(&d);
    yk_keys_init(&k);
    k.ring = true;
    CHECK(dtap(&d, &k, true, &p1, &gave) == YK_DTAP_APP && d.eat);
    CHECK(dtap(&d, &k, true, &p2, &gave) == YK_DTAP_APP && !gave && !yk_dtap_waiting(&d));
    yk_keys_closed(&k);
    CHECK(dtap(&d, &k, true, &p3, &gave) == YK_DTAP_HOLD);  // a first, not the paper's second
    // A single tap on the paper: the ring closes as ever, nothing is given late, the next tap (later) a first.
    yk_dtap_init(&d);
    yk_keys_init(&k);
    k.ring = true;
    CHECK(dtap(&d, &k, true, &p1, &gave) == YK_DTAP_APP);
    yk_keys_closed(&k);
    CHECK(dtap_clock(&d, 60080, 61000) == 0 && !yk_dtap_waiting(&d));
    yk_dtap_tap_t later = tap_at(64, 236, 71, 61200);
    CHECK(dtap(&d, &k, true, &later, &gave) == YK_DTAP_HOLD && !gave);
    // A tap on a place on the ring (no paper): the ring's at once, as ever; no double across it.
    yk_dtap_init(&d);
    yk_keys_init(&k);
    k.ring = true;
    yk_dtap_tap_t place = tap_at(233, 60, 80, 70000), after = tap_at(233, 62, 81, 70200);
    CHECK(dtap(&d, &k, true, &place, &gave) == YK_DTAP_APP && !d.armed);
    yk_keys_closed(&k);
    CHECK(dtap(&d, &k, true, &after, &gave) == YK_DTAP_HOLD && !k.ring);
    // The paper with the switch off: nothing of this (the hook is not even there).
    yk_dtap_init(&d);
    yk_keys_init(&k);
    k.ring = true;
    CHECK(dtap(&d, &k, false, &p1, &gave) == YK_DTAP_APP && !d.armed);

    // A press that cannot be the second gives the held tap before it reaches anything: too far, too late, after
    // another press. Its own tap is then a first, held.
    yk_dtap_tap_t f = tap_at(200, 200, 20, 10000);  // up at 10080
    yk_dtap_tap_t far = tap_at(200 + 41, 200, 21, 10200), near = tap_at(200 + 24, 200 + 32, 21, 10200);  // 41, 40 px
    yk_dtap_tap_t in_time = tap_at(200, 200, 21, 10080 + YK_DTAP_MS), late = tap_at(200, 200, 21, 10080 + YK_DTAP_MS + 1);
    yk_dtap_tap_t after_swipe = tap_at(200, 200, 22, 10200);
    yk_dtap_init(&d);
    yk_keys_init(&k);
    dtap(&d, &k, true, &f, NULL);
    CHECK(yk_dtap_press(&d, far.x, far.y, far.down_ms, far.press) && !d.armed);
    CHECK(yk_dtap_route(&d, &k, true, &far, &gave) == YK_DTAP_HOLD && !gave && d.first.press == 21);
    yk_dtap_init(&d);
    dtap(&d, &k, true, &f, NULL);
    CHECK(dtap(&d, &k, true, &near, &gave) == YK_DTAP_RING && !gave);
    yk_dtap_init(&d);
    yk_keys_init(&k);
    dtap(&d, &k, true, &f, NULL);
    CHECK(yk_dtap_press(&d, late.x, late.y, late.down_ms, late.press));  // 451 ms: given
    CHECK(yk_dtap_route(&d, &k, true, &late, &gave) == YK_DTAP_HOLD && !gave && !k.ring);
    yk_dtap_init(&d);
    dtap(&d, &k, true, &f, NULL);
    CHECK(dtap(&d, &k, true, &in_time, &gave) == YK_DTAP_RING && !gave);  // 450 ms: a double
    yk_dtap_init(&d);
    yk_keys_init(&k);
    dtap(&d, &k, true, &f, NULL);
    CHECK(dtap(&d, &k, true, &after_swipe, &gave) == YK_DTAP_HOLD && gave && !k.ring);  // a press came between
    // The route alone (no press seen) judges the same.
    yk_dtap_init(&d);
    yk_dtap_route(&d, &k, true, &f, &gave);
    CHECK(yk_dtap_route(&d, &k, true, &far, &gave) == YK_DTAP_HOLD && gave);

    // A second press that turns out no tap: a drag gives the held one at its release; one held down gives it at
    // YK_DTAP_SECOND_MS, and its release then gives nothing more.
    yk_dtap_init(&d);
    yk_keys_init(&k);
    dtap(&d, &k, true, &f, NULL);
    CHECK(!yk_dtap_press(&d, near.x, near.y, near.down_ms, near.press));
    CHECK(yk_dtap_other(&d) && !yk_dtap_waiting(&d) && !yk_dtap_other(&d));
    yk_dtap_init(&d);
    dtap(&d, &k, true, &f, NULL);
    CHECK(!yk_dtap_press(&d, near.x, near.y, near.down_ms, near.press));
    CHECK(dtap_clock(&d, 10200, 10200 + YK_DTAP_SECOND_MS - 1) == 0);
    CHECK(yk_dtap_tick(&d, 10200 + YK_DTAP_SECOND_MS) && !yk_dtap_waiting(&d) && !yk_dtap_other(&d));
    // A drag with nothing held: nothing.
    yk_dtap_init(&d);
    CHECK(!yk_dtap_press(&d, 10, 10, 100, 1) && !yk_dtap_other(&d));

    // A third tap is a first again (held).
    yk_dtap_init(&d);
    yk_keys_init(&k);
    yk_dtap_tap_t t1 = tap_at(100, 100, 10, 5000), t2 = tap_at(100, 100, 11, 5200), t3 = tap_at(100, 100, 12, 5400);
    dtap(&d, &k, true, &t1, NULL);
    CHECK(dtap(&d, &k, true, &t2, &gave) == YK_DTAP_RING);
    yk_keys_closed(&k);  // the ring went (a tap on it)
    CHECK(dtap(&d, &k, true, &t3, &gave) == YK_DTAP_HOLD && !gave && d.armed);
    // The clock wrapping between them, and under a held tap: still a double; still given a window late.
    yk_dtap_init(&d);
    yk_keys_init(&k);
    yk_dtap_tap_t w1 = tap_at(50, 50, 30, 0xFFFFFF00u), w2 = tap_at(52, 50, 31, 0xFFFFFF00u + 300u);
    dtap(&d, &k, true, &w1, NULL);
    CHECK(dtap(&d, &k, true, &w2, &gave) == YK_DTAP_RING && !gave);
    yk_dtap_init(&d);
    yk_keys_init(&k);
    dtap(&d, &k, true, &w1, NULL);  // up at 0xFFFFFF50
    CHECK(dtap_clock(&d, 0xFFFFFF50u, 0xFFFFFF50u + YK_DTAP_MS) == 0);
    CHECK(yk_dtap_tick(&d, 0xFFFFFF50u + YK_DTAP_MS + 1));

    // Something of the system's up: no half of a double, before or after, the tap the app's at once (a tap on it is
    // its own); a held one is given then. Once it goes, a pair again.
    yk_dtap_tap_t s1 = tap_at(233, 233, 40, 20000), s2 = tap_at(233, 233, 41, 20200), s3 = tap_at(233, 233, 42, 20400);
    yk_dtap_tap_t s4 = tap_at(233, 233, 43, 20600);
    for (int which = 0; which < 5; which++) {
        yk_dtap_init(&d);
        yk_keys_init(&k);
        if (which == 0) k.ring = true;
        if (which == 1) k.ask = true;
        if (which == 2) yk_keys_code(&k, true);
        if (which == 3) yk_keys_sheet_open(&k);
        if (which == 4) yk_keys_grant_open(&k);
        CHECK(dtap(&d, &k, true, &s1, &gave) == YK_DTAP_APP && !d.armed);
        CHECK(dtap(&d, &k, true, &s2, &gave) == YK_DTAP_APP && !d.armed);
        yk_keys_init(&k);  // it went
        CHECK(dtap(&d, &k, true, &s3, &gave) == YK_DTAP_HOLD && d.armed);  // a first, not a second
        CHECK(dtap(&d, &k, true, &s4, &gave) == YK_DTAP_RING && k.ring);
    }
    // A held tap, then the pull-down (say) up before its second: that second is the pull-down's; the held one is
    // the app's then.
    yk_dtap_init(&d);
    yk_keys_init(&k);
    dtap(&d, &k, true, &s1, NULL);
    CHECK(yk_keys_sheet_open(&k));
    CHECK(dtap(&d, &k, true, &s2, &gave) == YK_DTAP_APP && gave && !d.armed && !k.ring);
    // The rest of a press that closed one (eat), and the app's own press of the side key going on: no ring.
    yk_dtap_init(&d);
    yk_keys_init(&k);
    k.ring = true;
    yk_keys_route(&k, DOWN, 30000, false);  // closes the ring; its UP and TAP still to come
    CHECK(k.eat);
    yk_dtap_tap_t e1 = tap_at(233, 233, 50, 30010), e2 = tap_at(233, 233, 51, 30200);
    dtap(&d, &k, true, &e1, NULL);
    CHECK(dtap(&d, &k, true, &e2, &gave) == YK_DTAP_APP && !k.ring);
    yk_dtap_init(&d);
    yk_keys_init(&k);
    CHECK(yk_keys_route(&k, DOWN, 40000, false) == YK_KEYS_APP && k.app_down);
    yk_dtap_tap_t h1 = tap_at(233, 233, 60, 40100), h2 = tap_at(233, 233, 61, 40300);
    CHECK(dtap(&d, &k, true, &h1, &gave) == YK_DTAP_APP);
    CHECK(dtap(&d, &k, true, &h2, &gave) == YK_DTAP_APP && !k.ring);
    // own_double (the app takes the key's double click) does not touch it: the switch alone says.
    yk_dtap_init(&d);
    yk_keys_init(&k);
    dtap(&d, &k, true, &a, NULL);
    CHECK(dtap(&d, &k, true, &b, &gave) == YK_DTAP_RING);
    CHECK(yk_keys_route(&k, DOWN, 50000, true) == YK_KEYS_RING_CLOSE);
    // The app going (yk_dtap_forget): a held tap dropped, never given.
    yk_dtap_init(&d);
    yk_keys_init(&k);
    dtap(&d, &k, true, &t1, NULL);
    yk_dtap_forget(&d);
    CHECK(!yk_dtap_waiting(&d) && dtap_clock(&d, 5080, 7000) == 0);
    // The numbers are Yay's own (yay/main/yay_touch.h: YAY_TOUCH_DOUBLE_MS, YAY_TOUCH_DOUBLE_PX; yay/test/test_touch.c
    // checks they stay the same); YK_DTAP_SECOND_MS is yaykit_ui's YK_TAP_HOLD_MS (its test_apps.c checks 1500).
    CHECK(YK_DTAP_MS == 450 && YK_DTAP_PX == 40 && YK_DTAP_SECOND_MS == 1500);
}

int main(void) {
    test_keys();
    test_dtap();
    test_keys_sheet();
    test_keys_grant();
    test_pull();
    test_ring_tapped();
    test_app_events();
    test_summary();
    test_outbox();
    test_home();
    test_is_home();
    test_guard();
    test_names();
    test_launch_accept();
    test_escape();
    test_records();
    test_banner();
    test_update_record();
    test_update_next();
    test_update_p3();
    test_update_confirm();
    test_recovery();
    test_power_boot();
    test_power_watch();
    test_power_usb();
    test_settings();
    printf("yaykit_app host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
