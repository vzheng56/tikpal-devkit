// yk_folder_logic.c: what a path in a shared folder may be (the person's names, Chinese and all), and what the question
// decides from this device's record and the service's answer (Yay 0.1.19 开发文档 3.1, 4.3, 4.6, 6.2).
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yk_folder_logic.h"
#include "yk_tf_logic.h"

static int checks, failures;
#define CHECK(c)                                                         \
    do {                                                                 \
        checks++;                                                        \
        if (!(c)) {                                                      \
            failures++;                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
        }                                                                \
    } while (0)

static char *repeat(const char *unit, int n, const char *tail) {
    const size_t u = strlen(unit), t = strlen(tail);
    char *s = malloc(u * (size_t)n + t + 1);
    for (int i = 0; i < n; i++) memcpy(s + u * (size_t)i, unit, u);
    memcpy(s + u * (size_t)n, tail, t + 1);
    return s;
}

static void test_names(void) {
    // The person's own names: Chinese, Japanese, accents, spaces inside, emoji, the ~ of a short name, brackets.
    const char *good[] = {
        "三只小猪.ogg", "故事/睡前/三只小猪.ogg", "Música/canción ñ.opus", "あいうえお/ドラえもん.jpg", "My Song (live).wav",
        "说明.txt", "a", "PROGRA~1.TXT", "😀 smile.png", "1. 第一章/02 - 开头.ogg", "白噪音 #3 [雨].ogg", "a.b.c",
        "x/y/z/w/v/u/t/s",  // 8 levels
    };
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        if (!yk_folder_path_ok(good[i], false)) printf("  refused 「%s」\n", good[i]);
        CHECK(yk_folder_path_ok(good[i], false));
    }
    const char *bad[] = {
        "/三只小猪.ogg", "故事//小猪.ogg", "故事/", "..", "../.tikpal/apps", "故事/../../.tikpal", ".", "./a", "a/./b",
        ".hidden", "故事/._小猪.ogg", "a./b", "a /b", "a.", "a ", "a\\b", "C:x", "0:/apps", "a*b", "a?b",
        "a<b", "a>b", "a|b", "a\"b", "a\tb", "a\nb", "a\177b", "x/y/z/w/v/u/t/s/r",  // 9 levels
        "\xc0\xaf", "\xe0\x80\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xff", "\xe4\xb8", "abc\xe4", "\x80",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        if (yk_folder_path_ok(bad[i], false)) printf("  took 「%s」\n", bad[i]);
        CHECK(!yk_folder_path_ok(bad[i], false));
    }
    // "名字。" ends in an ideographic full stop, which is not FAT's dot: it is a name like any other.
    CHECK(yk_folder_path_ok("名字。", false));
    CHECK(!yk_folder_path_ok("", false) && yk_folder_path_ok("", true) && !yk_folder_path_ok(NULL, true));
    // 255 UTF-16 units a part: 255 Han characters (765 bytes) pass, 256 do not; an emoji is two.
    char *han255 = repeat("故", 255, ""), *han256 = repeat("故", 256, "");
    CHECK(strlen(han255) == 765 && yk_folder_path_ok(han255, false) && !yk_folder_path_ok(han256, false));
    char *emoji128 = repeat("😀", 128, ""), *emoji127 = repeat("😀", 127, "a");
    CHECK(!yk_folder_path_ok(emoji128, false) && yk_folder_path_ok(emoji127, false));
    // The longest path the rule lets through fits YK_FOLDER_PATH_BYTES.
    char *deep = malloc(YK_FOLDER_PATH_BYTES + 1);
    deep[0] = 0;
    for (int i = 0; i < 8; i++) strcat(strcat(deep, i ? "/" : ""), han255);
    CHECK(strlen(deep) == YK_FOLDER_PATH_BYTES && yk_folder_path_ok(deep, false));
    free(han255), free(han256), free(emoji128), free(emoji127), free(deep);
    // Two rules, apart: the system's slug rule and an app's own ASCII paths take none of the person's names.
    CHECK(!yk_tf_name_ok("三只小猪") && !yk_tf_rel_ok("三只小猪.ogg") && yk_folder_path_ok("三只小猪.ogg", false));
    // A listing leaves out a computer's own files.
    CHECK(yk_folder_name_hidden("._三只小猪.ogg", false) && yk_folder_name_hidden(".Spotlight-V100", false));
    CHECK(yk_folder_name_hidden("System Volume Information", true) && !yk_folder_name_hidden("三只小猪.ogg", false));
    CHECK(!strcmp(yk_folder_dir_name(0), "Music") && !strcmp(yk_folder_dir_name(3), "Documents") && !yk_folder_dir_name(4));
    CHECK(!strcmp(yk_folder_kind_word(1), "stories") && !yk_folder_kind_word(-1));
}

static yk_grant_t grant(int state, unsigned folders, bool known, bool pending, unsigned rev) {
    return (yk_grant_t){.state = (uint8_t)state, .folders = (uint8_t)folders, .known = known, .pending = pending,
                        .rev = rev};
}

static bool eq(yk_grant_t a, yk_grant_t b) {
    return a.state == b.state && a.folders == b.folders && a.known == b.known && a.pending == b.pending &&
           a.full == b.full && a.rev == b.rev;
}

static yk_grant_t full(yk_grant_t g) {
    g.full = true;
    return g;
}

static void test_decide(void) {
    const unsigned M = 1, S = 2, P = 4, D = 8;
    // Never asked: the question. Granted with every kind: no question. Denied: no.
    yk_grant_t none = {0};
    CHECK(yk_grant_decide(&none, M) == YK_ASK_QUESTION);
    yk_grant_t g = grant(YK_GRANT_GRANTED, M | S, true, false, 3);
    CHECK(yk_grant_decide(&g, M | S) == YK_ASK_ALLOWED && yk_grant_decide(&g, M) == YK_ASK_ALLOWED);
    // An update that asks for more: the question again, listing every kind; a no then closes the whole app.
    CHECK(yk_grant_decide(&g, M | S | P) == YK_ASK_QUESTION);
    yk_grant_t no = grant(YK_GRANT_DENIED, M, true, false, 5);
    CHECK(yk_grant_decide(&no, M) == YK_ASK_DENIED && yk_grant_decide(&no, M | S | P | D) == YK_ASK_DENIED);
    // An answer covers what the app declares, waits to be told, on the rev it was made on.
    yk_grant_t yes = yk_grant_answered(&g, true, M | S | P);
    CHECK(eq(yes, grant(YK_GRANT_GRANTED, M | S | P, true, true, 3)));
    yk_grant_t nay = yk_grant_answered(&none, false, M | D);
    CHECK(eq(nay, grant(YK_GRANT_DENIED, M | D, false, true, 0)) && yk_grant_decide(&nay, M) == YK_ASK_DENIED);
    // Fewer kinds after an update: the grant shrinks, its state stays.
    yk_grant_t shrink = grant(YK_GRANT_GRANTED, M | S | P, true, false, 4);
    CHECK(yk_grant_shrink(&shrink, M | P) && eq(shrink, grant(YK_GRANT_GRANTED, M | P, true, false, 4)));
    CHECK(!yk_grant_shrink(&shrink, M | P | D) && yk_grant_decide(&shrink, M | P) == YK_ASK_ALLOWED);
}

static void test_merge(void) {
    const unsigned M = 1, S = 2;
    yk_grant_t here = grant(YK_GRANT_GRANTED, M, true, false, 2), service;
    // No answer in 2 s: what is here stands.
    CHECK(eq(yk_grant_merge(here, YK_NET_SILENT, NULL), here));
    // The App switched it off (rev 3): the service's, nothing pending.
    service = grant(YK_GRANT_DENIED, M, false, false, 3);
    CHECK(eq(yk_grant_merge(here, YK_NET_ANSWERED, &service), grant(YK_GRANT_DENIED, M, true, false, 3)));
    // Gone at the service (unbound, a new owner, deleted): gone here too, and asked again.
    service = grant(YK_GRANT_NONE, 0, false, false, 0);
    yk_grant_t gone = yk_grant_merge(here, YK_NET_ANSWERED, &service);
    CHECK(eq(gone, grant(YK_GRANT_NONE, 0, false, false, 0)) && yk_grant_decide(&gone, M) == YK_ASK_QUESTION);
    // Answered here offline, never told: the service has none, the answer stands.
    yk_grant_t offline = grant(YK_GRANT_GRANTED, M | S, false, true, 0);
    CHECK(eq(yk_grant_merge(offline, YK_NET_ANSWERED, &service), offline));
    // Answered here on rev 2, not told yet: the service still at 2 keeps it; at 3 (the App, meanwhile) wins.
    yk_grant_t made = grant(YK_GRANT_DENIED, M, true, true, 2);
    service = grant(YK_GRANT_GRANTED, M, false, false, 2);
    CHECK(eq(yk_grant_merge(made, YK_NET_ANSWERED, &service), made));
    service = grant(YK_GRANT_GRANTED, M | S, false, false, 3);
    CHECK(eq(yk_grant_merge(made, YK_NET_ANSWERED, &service), grant(YK_GRANT_GRANTED, M | S, true, false, 3)));
    // Told: OK takes the service's rev; 409 takes the table's, no question; silence keeps it pending.
    yk_grant_t told = grant(YK_GRANT_GRANTED, M, true, false, 3);
    CHECK(eq(yk_grant_reported(made, YK_PUT_OK, &told), told));
    yk_grant_t table = grant(YK_GRANT_DENIED, M | S, false, false, 7);
    CHECK(eq(yk_grant_reported(made, YK_PUT_CHANGED, &table), grant(YK_GRANT_DENIED, M | S, true, false, 7)));
    CHECK(eq(yk_grant_reported(made, YK_PUT_SILENT, &table), made));
}

// 409 GRANTS_FULL (the contract, #1845): 64 apps on this device at the service already, nothing written there. The
// answer here stands and is acted on, no question; marked full, still pending: the next ask tells it once more.
static void test_full(void) {
    const unsigned M = 1, S = 2;
    yk_grant_t made = grant(YK_GRANT_GRANTED, M | S, false, true, 0);  // the 65th app, answered yes, never told
    yk_grant_t back = yk_grant_reported(made, YK_PUT_FULL, NULL);
    CHECK(eq(back, full(made)) && back.pending && back.full);
    CHECK(yk_grant_decide(&back, M | S) == YK_ASK_ALLOWED);  // acted on: no question
    yk_grant_t nay = yk_grant_reported(grant(YK_GRANT_DENIED, M, false, true, 0), YK_PUT_FULL, NULL);
    CHECK(nay.full && nay.pending && yk_grant_decide(&nay, M) == YK_ASK_DENIED);
    // Told again at the next ask: still full stays full; silence keeps it; told at last, it is the service's.
    CHECK(eq(yk_grant_reported(back, YK_PUT_FULL, NULL), back));
    CHECK(eq(yk_grant_reported(back, YK_PUT_SILENT, NULL), back));
    yk_grant_t told = grant(YK_GRANT_GRANTED, M | S, false, false, 1);
    CHECK(eq(yk_grant_reported(back, YK_PUT_OK, &told), grant(YK_GRANT_GRANTED, M | S, true, false, 1)));
    yk_grant_t table = grant(YK_GRANT_DENIED, M, false, false, 2);
    CHECK(eq(yk_grant_reported(back, YK_PUT_CHANGED, &table), grant(YK_GRANT_DENIED, M, true, false, 2)));
    // The service lists none for it (it is full): the answer here stands, still full.
    yk_grant_t none = grant(YK_GRANT_NONE, 0, false, false, 0);
    CHECK(eq(yk_grant_merge(back, YK_NET_ANSWERED, &none), back));
    // The service has one for it after all (the App, meanwhile): the service's, not full.
    yk_grant_t app = grant(YK_GRANT_DENIED, M | S, false, false, 1);
    CHECK(eq(yk_grant_merge(back, YK_NET_ANSWERED, &app), grant(YK_GRANT_DENIED, M | S, true, false, 1)));
    // A new answer (an update asked for one more kind): told once more, whatever the last time said.
    yk_grant_t again = yk_grant_answered(&back, true, M | S | 4);
    CHECK(eq(again, grant(YK_GRANT_GRANTED, M | S | 4, false, true, 0)));
    // Kept across a restart.
    uint8_t rec[YK_GRANT_RECORD];
    yk_grant_encode("photo-frame", &back, rec);
    yk_grant_t read;
    CHECK(yk_grant_decode(rec, sizeof(rec), "photo-frame", &read) && eq(read, back));
}

static void test_record(void) {
    uint8_t rec[YK_GRANT_RECORD];
    const yk_grant_t g = grant(YK_GRANT_GRANTED, 5, true, true, 123456789);
    yk_grant_encode("story-box", &g, rec);
    yk_grant_t back;
    CHECK(yk_grant_decode(rec, sizeof(rec), "story-box", &back) && eq(back, g));
    CHECK(!yk_grant_decode(rec, sizeof(rec), "story-bo", &back));     // another slug on the same hash: not ours
    CHECK(!yk_grant_decode(rec, sizeof(rec) - 1, "story-box", &back));
    // A walk over every record reads each one's own slug (Yay's check, yk_folder_sys.c).
    char slug[YK_GRANT_SLUG];
    CHECK(yk_grant_record_slug(rec, sizeof(rec), slug) && !strcmp(slug, "story-box"));
    CHECK(!yk_grant_record_slug(rec, sizeof(rec) - 1, slug));
    rec[2] ^= 1;
    CHECK(!yk_grant_decode(rec, sizeof(rec), "story-box", &back));    // a byte changed
    char k0[YK_GRANT_KEY], k1[YK_GRANT_KEY], k2[YK_GRANT_KEY];
    yk_grant_key("story-box", 0, k0);
    yk_grant_key("story-box", 1, k1);
    yk_grant_key("a-slug-of-the-longest-length-24", 0, k2);
    CHECK(strlen(k0) == 10 && strlen(k2) == 10 && k0[0] == 'g' && strcmp(k0, k1) && !strncmp(k0, k1, 9));
}

// Yay's check of a record against the service's list, and what it tells (yk_folder_sys.c); the hand-over of the calls.
static void test_sync_rules(void) {
    const unsigned M = 1;
    yk_grant_t g = grant(YK_GRANT_GRANTED, M, true, false, 3), off = grant(YK_GRANT_DENIED, M, false, false, 4);
    CHECK(eq(yk_grant_synced(g, &off), grant(YK_GRANT_DENIED, M, true, false, 4)));
    CHECK(eq(yk_grant_synced(g, NULL), grant(YK_GRANT_NONE, 0, false, false, 0)));  // listed no more: cleared
    yk_grant_t offline = grant(YK_GRANT_GRANTED, M, false, true, 0);
    CHECK(eq(yk_grant_synced(offline, NULL), offline));  // an answer here that waits stays
    CHECK(yk_grant_to_tell(&offline) && !yk_grant_to_tell(&g));
    yk_grant_t waits_full = full(offline);
    CHECK(!yk_grant_to_tell(&waits_full));  // a full one waits for its own app's ask
    yk_grant_t odd = grant(YK_GRANT_NONE, 0, false, true, 0);
    CHECK(!yk_grant_to_tell(&odd));
    // The platform's push: newer rev only.
    yk_grant_t here = grant(YK_GRANT_GRANTED, M, true, false, 3), pushed = grant(YK_GRANT_DENIED, M, false, false, 4);
    CHECK(eq(yk_grant_pushed(here, &pushed), grant(YK_GRANT_DENIED, M, true, false, 4)));
    pushed.rev = 3;
    CHECK(eq(yk_grant_pushed(here, &pushed), here));
    CHECK(eq(yk_grant_pushed(offline, &pushed), grant(YK_GRANT_DENIED, M, true, false, 3)));  // never told: the App's
    CHECK(eq(yk_grant_pushed(here, NULL), here));
    // The whole list (hello.ok's, or the GET's): listed is taken whatever its rev (a new owner's start from 1).
    yk_grant_t high = grant(YK_GRANT_GRANTED, M, true, false, 40), reset = grant(YK_GRANT_DENIED, M, false, false, 1);
    CHECK(eq(yk_grant_synced(high, &reset), grant(YK_GRANT_DENIED, M, true, false, 1)));
    yk_grant_t waits = grant(YK_GRANT_DENIED, M, true, true, 3);  // answered here on rev 3, the App's rev 5 since
    yk_grant_t app5 = grant(YK_GRANT_GRANTED, M, false, false, 5);
    CHECK(eq(yk_grant_synced(waits, &app5), waits));  // stays, to be told: the 409 then says the App's
    CHECK(!yk_folder_net());
    static const yk_folder_net_t calls = {0};
    yk_folder_net_set(&calls);
    CHECK(yk_folder_net() == &calls);
    yk_folder_net_set(NULL);
    CHECK(!yk_folder_net());
}

int main(void) {
    test_names();
    test_decide();
    test_merge();
    test_full();
    test_sync_rules();
    test_record();
    printf("yk_folder logic host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
