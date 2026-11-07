// Host tests for the system pull-down's words (yk_sheet_logic.c): the battery, Wi-Fi and phone rows, in Chinese and
// English, and which of them are coral. Run: bash test/run.sh
#include <stdio.h>
#include <string.h>

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

int main(void) {
    test_clock();
    test_battery();
    test_wifi();
    test_phone();
    printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "ok", checks, failures);
    return failures ? 1 : 0;
}
