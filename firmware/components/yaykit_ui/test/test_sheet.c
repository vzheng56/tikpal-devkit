// Host tests for the system pull-down's words (yk_sheet_logic.c): the battery, Wi-Fi and phone rows, in Chinese and
// English, and which of them are coral; and its taps (0.1.18), which are yk_tap's (yk_ui_tap_only on the dimmed layer
// and the button). Run: bash test/run.sh
#include <stdio.h>
#include <string.h>

#include "yk_apps_logic.h"
#include "yk_sheet.h"

static int failures, checks;
#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

/** The main firmware's words since 0.1.14 (battery-display.md 4.4), now shared: as a row and as a named line. */
static void test_battery(void) {
    char b[64];
    yk_sheet_battery_t s = {.percent = 76};
    CHECK(!strcmp(yk_sheet_battery_text(&s, false, false, b, sizeof(b)), "76%"));
    CHECK(!strcmp(yk_sheet_battery_text(&s, true, false, b, sizeof(b)), "电量 76%"));
    CHECK(!strcmp(yk_sheet_battery_text(&s, true, true, b, sizeof(b)), "Battery 76%"));
    s.charging = s.usb = true;
    CHECK(!strcmp(yk_sheet_battery_text(&s, false, false, b, sizeof(b)), "76% · 充电中"));
    CHECK(!strcmp(yk_sheet_battery_text(&s, false, true, b, sizeof(b)), "76% · charging"));
    s.charging = false, s.full = true, s.percent = 100;
    CHECK(!strcmp(yk_sheet_battery_text(&s, false, false, b, sizeof(b)), "100% · 充满了"));
    s.full = false, s.stopped = true, s.percent = 80;
    CHECK(!strcmp(yk_sheet_battery_text(&s, true, false, b, sizeof(b)), "电量 80% · 没在充电"));
    s.stopped = false, s.low = true, s.charging = true, s.percent = 0;
    CHECK(!strcmp(yk_sheet_battery_text(&s, false, false, b, sizeof(b)), "很低，正在充电"));
    CHECK(!strcmp(yk_sheet_battery_text(&s, true, false, b, sizeof(b)), "电量很低，正在充电"));
    s.fault = true;  // the fault before the rest
    CHECK(!strcmp(yk_sheet_battery_text(&s, false, false, b, sizeof(b)), "电池可能有问题"));
    s = (yk_sheet_battery_t){.percent = 40, .fault = true, .low = true};  // unplugged: neither shows, the number does
    CHECK(!strcmp(yk_sheet_battery_text(&s, false, false, b, sizeof(b)), "40%"));
    s.percent = -1;
    CHECK(!strcmp(yk_sheet_battery_text(&s, false, false, b, sizeof(b)), "还读不到"));
    CHECK(!strcmp(yk_sheet_battery_text(&s, false, true, b, sizeof(b)), "Not read yet"));
    char small[4];
    yk_sheet_battery_text(&(yk_sheet_battery_t){.percent = 76, .charging = true}, false, false, small, sizeof(small));
    CHECK(strlen(small) < sizeof(small));  // cut, never past the end
}

/** The Wi-Fi row (system-pulldown.md 2.3): connected with the signal, the server not reached, connecting (never 没网),
 *  not connected, off. */
static void test_wifi(void) {
    char b[64];
    bool p = true;
    CHECK(!strcmp(yk_sheet_wifi_text(3, "Home-5G", -50, 1, false, &p, b, sizeof(b)), "Home-5G · 信号好") && !p);
    CHECK(!strcmp(yk_sheet_wifi_text(3, "Home-5G", -60, -1, false, &p, b, sizeof(b)), "Home-5G · 信号一般") && !p);
    CHECK(!strcmp(yk_sheet_wifi_text(3, "Home-5G", -72, 0, false, &p, b, sizeof(b)), "Home-5G · 信号弱") && !p);
    CHECK(!strcmp(yk_sheet_wifi_text(3, "Home-5G", -50, 1, true, &p, b, sizeof(b)), "Home-5G · strong"));
    CHECK(!strcmp(yk_sheet_wifi_text(3, "", -50, 1, false, &p, b, sizeof(b)), "信号好"));
    CHECK(!strcmp(yk_sheet_wifi_text(3, "Home-5G", -50, 2, false, &p, b, sizeof(b)), "连不上服务器") && p);
    CHECK(!strcmp(yk_sheet_wifi_text(3, "Home-5G", -50, 2, true, &p, b, sizeof(b)), "Can't reach server") && p);
    CHECK(!strcmp(yk_sheet_wifi_text(2, "", 0, 2, false, &p, b, sizeof(b)), "正在连接") && !p);  // an old failure: still connecting
    CHECK(!strcmp(yk_sheet_wifi_text(2, "", 0, 0, true, &p, b, sizeof(b)), "Connecting") && !p);
    CHECK(!strcmp(yk_sheet_wifi_text(4, "", 0, 0, false, &p, b, sizeof(b)), "没连上") && p);
    CHECK(!strcmp(yk_sheet_wifi_text(1, "", 0, 0, false, &p, b, sizeof(b)), "没连上") && p);
    CHECK(!strcmp(yk_sheet_wifi_text(1, "", 0, 0, true, &p, b, sizeof(b)), "Not connected") && p);
    CHECK(!strcmp(yk_sheet_wifi_text(0, "", 0, 0, false, &p, b, sizeof(b)), "不用时关着") && !p);
    CHECK(!strcmp(yk_sheet_wifi_text(0, "", 0, 0, true, NULL, b, sizeof(b)), "Off when not in use"));
}

/** The phone row in an extension (8.1): added (to whose phone), being added, not added (coral). */
static void test_phone(void) {
    char b[64];
    bool p = true;
    CHECK(!strcmp(yk_sheet_phone_text(2, "昵称", false, &p, b, sizeof(b)), "已添加 · 昵称的手机") && !p);
    CHECK(!strcmp(yk_sheet_phone_text(2, "Nickname", true, &p, b, sizeof(b)), "Added · Nickname's phone") && !p);
    CHECK(!strcmp(yk_sheet_phone_text(2, "", false, &p, b, sizeof(b)), "已添加") && !p);
    CHECK(!strcmp(yk_sheet_phone_text(2, NULL, false, &p, b, sizeof(b)), "已添加") && !p);
    CHECK(!strcmp(yk_sheet_phone_text(1, "昵称", false, &p, b, sizeof(b)), "正在添加") && !p);
    CHECK(!strcmp(yk_sheet_phone_text(0, "昵称", false, &p, b, sizeof(b)), "还没添加") && p);
    CHECK(!strcmp(yk_sheet_phone_text(3, "", true, &p, b, sizeof(b)), "Not added") && p);
}

/** The clock (8.1, BOSS-2 11-02): UTC plus the shared offset, the firmware's zone untouched. */
static void test_clock(void) {
    char b[8];
    const int64_t t = 1793590200;  // 2026-11-02 03:30:00 UTC
    CHECK(!strcmp(yk_sheet_clock_text(t, 480, b, sizeof(b)), "11:30"));   // Shanghai
    CHECK(!strcmp(yk_sheet_clock_text(t, 0, b, sizeof(b)), "03:30"));
    CHECK(!strcmp(yk_sheet_clock_text(t, -300, b, sizeof(b)), "22:30"));  // New York (winter): the day before
    CHECK(!strcmp(yk_sheet_clock_text(t, 345, b, sizeof(b)), "09:15"));   // Kathmandu
    CHECK(!strcmp(yk_sheet_clock_text(t + 21 * 3600, 480, b, sizeof(b)), "08:30"));  // past midnight there
}

/** Its taps: yk_ui_tap_only() keeps a press on the dimmed layer (or the button) as a yk_tap, 0 on it, -1 off it (the
 *  topmost clickable thing under the finger is another: the panel over the layer). */
static void test_taps(void) {
    yk_tap_t t = {0};
    // The dimmed part below the panel, down and up there: it closes.
    yk_tap_press(&t, 0, 233, 400, 1000);
    CHECK(yk_tap_release(&t, 0, 236, 398, 1090));
    // Down on the dimmed part, let go on the panel just above (a few px: still within the slop): no tap.
    yk_tap_press(&t, 0, 233, 322, 1000);
    CHECK(!yk_tap_release(&t, -1, 233, 315, 1090));
    // The button: down on it and slid off before letting go, no tap; a swipe up across it, no tap.
    yk_tap_press(&t, 0, 233, 260, 1000);
    yk_tap_move(&t, 233, 300);
    CHECK(!yk_tap_release(&t, -1, 233, 300, 1200));
    yk_tap_press(&t, 0, 233, 260, 1000);
    yk_tap_move(&t, 233, 200);
    CHECK(!yk_tap_release(&t, 0, 233, 255, 1150));
    yk_tap_press(&t, 0, 233, 260, 1000);
    CHECK(yk_tap_release(&t, 0, 233, 262, 1100));
}

/** 0.2 (user 11-20): a state that is fine as its picture and the least words; one to read stays the words, no picture.
 *  The bars as 信号好 / 一般 / 弱 are; a battery filled to its percent, a bolt charging, coral low; a tick when added. */
static void test_icons(void) {
    char b[64];
    bool p = true;
    yk_sheet_icon_t ic;
    // Wi-Fi: connected and through, the name and the bars (their thresholds the words'); else the words alone.
    CHECK(!strcmp(yk_sheet_wifi_value(3, "Home-5G", -50, 1, false, &p, &ic, b, sizeof(b)), "Home-5G") && !p);
    CHECK(ic.kind == YK_SHEET_ICON_WIFI && ic.level == 3);
    CHECK(!strcmp(yk_sheet_wifi_value(3, "Home-5G", -60, -1, true, &p, &ic, b, sizeof(b)), "Home-5G") && ic.level == 2);
    CHECK(yk_sheet_wifi_value(3, "Home-5G", -72, 0, false, &p, &ic, b, sizeof(b)) && ic.level == 1);
    CHECK(yk_sheet_wifi_value(3, "Home-5G", -95, 1, false, &p, &ic, b, sizeof(b)) && ic.level == 1);
    CHECK(!strcmp(yk_sheet_wifi_value(3, "", -50, 1, false, &p, &ic, b, sizeof(b)), "") && ic.kind == YK_SHEET_ICON_WIFI);
    CHECK(!strcmp(yk_sheet_wifi_value(3, NULL, -50, 1, false, &p, &ic, b, sizeof(b)), ""));
    for (int rssi = -100; rssi <= -30; rssi++) {  // the bars always the words' strength
        char words[64];
        yk_sheet_wifi_text(3, "", rssi, 1, false, NULL, words, sizeof(words));
        yk_sheet_wifi_value(3, "", rssi, 1, false, NULL, &ic, b, sizeof(b));
        CHECK(ic.level == (!strcmp(words, "信号好") ? 3 : !strcmp(words, "信号一般") ? 2 : 1));
    }
    const struct {
        int wifi, reach;
        const char *words;
        bool problem;
    } wifi_words[] = {{3, 2, "连不上服务器", true}, {2, 0, "正在连接", false}, {0, -1, "不用时关着", false},
                      {1, -1, "没连上", true}, {4, 1, "没连上", true}};
    for (size_t i = 0; i < sizeof(wifi_words) / sizeof(wifi_words[0]); i++) {
        ic.kind = YK_SHEET_ICON_CHECK;
        CHECK(!strcmp(yk_sheet_wifi_value(wifi_words[i].wifi, "Home-5G", -50, wifi_words[i].reach, false, &p, &ic, b,
                                          sizeof(b)),
                      wifi_words[i].words));
        CHECK(ic.kind == YK_SHEET_ICON_NONE && p == wifi_words[i].problem);
    }
    // The battery: a number and its picture; the words alone where a number would mislead.
    yk_sheet_battery_t s = {.percent = 76};
    CHECK(!strcmp(yk_sheet_battery_value(&s, false, &ic, b, sizeof(b)), "76%"));
    CHECK(ic.kind == YK_SHEET_ICON_BATTERY && ic.level == 76 && !ic.charging && !ic.low);
    s.usb = s.charging = true;
    CHECK(!strcmp(yk_sheet_battery_value(&s, true, &ic, b, sizeof(b)), "76%") && ic.charging && !ic.low);
    s.percent = 9;  // charging at 9 %: the bolt, not coral
    CHECK(yk_sheet_battery_value(&s, false, &ic, b, sizeof(b)) && ic.charging && !ic.low && ic.level == 9);
    s.usb = s.charging = false;
    s.percent = YK_SHEET_BATTERY_LOW;  // 15 % and under, not charging: coral
    CHECK(yk_sheet_battery_value(&s, false, &ic, b, sizeof(b)) && ic.low);
    s.percent = YK_SHEET_BATTERY_LOW + 1;
    CHECK(yk_sheet_battery_value(&s, false, &ic, b, sizeof(b)) && !ic.low);
    s.percent = 0;
    CHECK(!strcmp(yk_sheet_battery_value(&s, false, &ic, b, sizeof(b)), "0%") && ic.level == 0 && ic.low);
    s.percent = 120;  // never past full
    CHECK(!strcmp(yk_sheet_battery_value(&s, false, &ic, b, sizeof(b)), "100%") && ic.level == 100);
    s = (yk_sheet_battery_t){.percent = 100, .usb = true, .charging = true, .full = true};  // full: no bolt
    CHECK(!strcmp(yk_sheet_battery_value(&s, false, &ic, b, sizeof(b)), "100%") && !ic.charging && ic.level == 100);
    s = (yk_sheet_battery_t){.percent = 64, .usb = true, .stopped = true};  // plugged in, not charging: worth the words
    yk_sheet_battery_text(&s, false, false, b, sizeof(b));
    char words[64];
    snprintf(words, sizeof(words), "%s", b);
    CHECK(!strcmp(yk_sheet_battery_value(&s, false, &ic, b, sizeof(b)), words) && strcmp(words, "64%"));
    CHECK(ic.kind == YK_SHEET_ICON_BATTERY && !ic.charging);
    const yk_sheet_battery_t worded[] = {{.percent = -1}, {.percent = 3, .usb = true, .low = true, .charging = true},
                                         {.percent = 40, .usb = true, .fault = true}};
    for (size_t i = 0; i < sizeof(worded) / sizeof(worded[0]); i++) {
        ic.kind = YK_SHEET_ICON_WIFI;
        yk_sheet_battery_text(&worded[i], false, false, words, sizeof(words));
        CHECK(!strcmp(yk_sheet_battery_value(&worded[i], false, &ic, b, sizeof(b)), words) && ic.kind == YK_SHEET_ICON_NONE);
    }
    // The phone: added, the nickname and a tick (no 「已添加到」); the rest the words.
    CHECK(!strcmp(yk_sheet_phone_value(2, "小明", false, &p, &ic, b, sizeof(b)), "小明") && !p);
    CHECK(ic.kind == YK_SHEET_ICON_CHECK);
    CHECK(!strcmp(yk_sheet_phone_value(2, "", true, &p, &ic, b, sizeof(b)), "") && ic.kind == YK_SHEET_ICON_CHECK);
    CHECK(!strcmp(yk_sheet_phone_value(2, NULL, false, &p, &ic, b, sizeof(b)), ""));
    for (int account = 0; account <= 3; account++) {
        if (account == 2) continue;
        bool want;
        yk_sheet_phone_text(account, "小明", false, &want, words, sizeof(words));
        CHECK(!strcmp(yk_sheet_phone_value(account, "小明", false, &p, &ic, b, sizeof(b)), words));
        CHECK(ic.kind == YK_SHEET_ICON_NONE && p == want);
    }
    // No picture asked for (NULL): the words all the same.
    CHECK(!strcmp(yk_sheet_wifi_value(3, "Home-5G", -50, 1, false, NULL, NULL, b, sizeof(b)), "Home-5G"));
    CHECK(!strcmp(yk_sheet_battery_value(&(yk_sheet_battery_t){.percent = 50}, false, NULL, b, sizeof(b)), "50%"));
    CHECK(!strcmp(yk_sheet_phone_value(2, "小明", false, NULL, NULL, b, sizeof(b)), "小明"));
    // A row without a picture is as before: zero is none.
    const yk_sheet_row_t old = {.name = "Wi-Fi", .value = "Home-5G · 信号好"};
    CHECK(old.icon.kind == YK_SHEET_ICON_NONE);
}

int main(void) {
    test_taps();
    test_clock();
    test_battery();
    test_wifi();
    test_phone();
    test_icons();
    printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "ok", checks, failures);
    return failures ? 1 : 0;
}
