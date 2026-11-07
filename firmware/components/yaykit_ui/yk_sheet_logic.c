// The words of the system pull-down's rows (yk_sheet.h), pure: host tested with test/run.sh.
#include <stdio.h>
#include <time.h>

#include "yk_sheet.h"

#define T(zh, en) (english ? (en) : (zh))

// yaykit_wifi's yk_wifi_state_t, yaykit_cloud's yk_cloud_reach_t and yk_account_state_t, by number (this stays free of
// those components: yaykit_app links them weakly).
enum { WIFI_OFF, WIFI_UNCONFIGURED, WIFI_CONNECTING, WIFI_CONNECTED, WIFI_FAILED };
enum { REACH_UNKNOWN, REACH_OK, REACH_FAILED };
enum { ACCOUNT_UNBOUND, ACCOUNT_ACTIVATING, ACCOUNT_BOUND, ACCOUNT_FAILED };

const char *yk_sheet_battery_text(const yk_sheet_battery_t *b, bool named, bool english, char *buf, size_t size) {
    if (!size) return buf;
    const char *name = named ? T("电量 ", "Battery ") : "";
    // Very low and the fault say so instead of a number (it would read 0 % for a long while); full and stopped as
    // the power chip says.
    if (b->percent < 0) snprintf(buf, size, "%s", named ? T("电量还读不到", "Battery not read yet") : T("还读不到", "Not read yet"));
    else if (b->usb && b->fault) snprintf(buf, size, "%s", T("电池可能有问题", "Battery may be faulty"));
    else if (b->usb && b->low)
        snprintf(buf, size, "%s", named ? T("电量很低，正在充电", "Very low, charging") : T("很低，正在充电", "Very low, charging"));
    else if (b->usb && b->full) snprintf(buf, size, T("%s%d%% · 充满了", "%s%d%% · fully charged"), name, b->percent);
    else if (b->usb && b->stopped) snprintf(buf, size, T("%s%d%% · 没在充电", "%s%d%% · not charging"), name, b->percent);
    else if (b->charging) snprintf(buf, size, T("%s%d%% · 充电中", "%s%d%% · charging"), name, b->percent);
    else snprintf(buf, size, "%s%d%%", name, b->percent);
    return buf;
}

const char *yk_sheet_wifi_text(int wifi, const char *ssid, int rssi, int reach, bool english, bool *problem, char *buf,
                               size_t size) {
    bool wrong = false;
    if (!size) return buf;
    if (wifi == WIFI_CONNECTED && reach == REACH_FAILED) {
        wrong = true;
        snprintf(buf, size, "%s", T("连不上服务器", "Can't reach server"));
    } else if (wifi == WIFI_CONNECTED) {
        const char *signal = rssi > -60 ? T("信号好", "strong") : rssi > -72 ? T("信号一般", "fair") : T("信号弱", "weak");
        if (ssid && ssid[0]) snprintf(buf, size, "%s · %s", ssid, signal);  // one line, cut with … at a character
        else snprintf(buf, size, "%s", signal);
    } else if (wifi == WIFI_CONNECTING) {
        snprintf(buf, size, "%s", T("正在连接", "Connecting"));  // connecting is never 没网
    } else if (wifi == WIFI_OFF) {
        // The app has not started it (yaykit_wifi has no stop: off means not started since this boot): its choice, not
        // a fault, so not 没打开 (user 11-02).
        snprintf(buf, size, "%s", T("不用时关着", "Off when not in use"));
    } else {
        wrong = true;
        snprintf(buf, size, "%s", T("没连上", "Not connected"));
    }
    if (problem) *problem = wrong;
    return buf;
}

const char *yk_sheet_clock_text(int64_t utc_s, int offset_min, char *buf, size_t size) {
    if (!size) return buf;
    const time_t local = (time_t)(utc_s + (int64_t)offset_min * 60);
    struct tm tm;
    if (gmtime_r(&local, &tm)) snprintf(buf, size, "%02d:%02d", tm.tm_hour, tm.tm_min);
    else buf[0] = 0;
    return buf;
}

const char *yk_sheet_phone_text(int account, const char *nickname, bool english, bool *problem, char *buf, size_t size) {
    if (problem) *problem = account != ACCOUNT_BOUND && account != ACCOUNT_ACTIVATING;
    if (!size) return buf;
    if (account == ACCOUNT_BOUND && nickname && nickname[0])
        snprintf(buf, size, T("已添加 · %s的手机", "Added · %s's phone"), nickname);
    else if (account == ACCOUNT_BOUND) snprintf(buf, size, "%s", T("已添加", "Added"));
    else if (account == ACCOUNT_ACTIVATING) snprintf(buf, size, "%s", T("正在添加", "Adding"));
    else snprintf(buf, size, "%s", T("还没添加", "Not added"));
    return buf;
}
