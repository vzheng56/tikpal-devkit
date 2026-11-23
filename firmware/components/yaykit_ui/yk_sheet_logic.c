// The words of the system pull-down's rows (yk_sheet.h), pure: host tested with test/run.sh.
#include <stdio.h>
#include <time.h>

#include "yk_sheet.h"
#include "yk_words.h"


// yaykit_wifi's yk_wifi_state_t, yaykit_cloud's yk_cloud_reach_t and yk_account_state_t, by number (this stays free of
// those components: yaykit_app links them weakly).
enum { WIFI_OFF, WIFI_UNCONFIGURED, WIFI_CONNECTING, WIFI_CONNECTED, WIFI_FAILED };
enum { REACH_UNKNOWN, REACH_OK, REACH_FAILED };
enum { ACCOUNT_UNBOUND, ACCOUNT_ACTIVATING, ACCOUNT_BOUND, ACCOUNT_FAILED };

const char *yk_sheet_battery_text(const yk_sheet_battery_t *b, bool named, bool english, char *buf, size_t size) {
    if (!size) return buf;
    // Very low and the fault say so instead of a number (it would read 0 % for a long while); full and stopped as
    // the power chip says.
    char pct[12], value[48];
    yk_words_num(pct, b->percent);
    if (b->percent < 0) {
        snprintf(buf, size, "%s", yk_word_in(english, named ? YK_STR_YAYKIT_SHEET_BATTERY_NOT_READ : YK_STR_YAYKIT_SHEET_NOT_READ));
        return buf;
    }
    if (b->usb && b->fault) {
        snprintf(buf, size, "%s", yk_word_in(english, YK_STR_YAYKIT_BATTERY_FAULTY));
        return buf;
    }
    if (b->usb && b->low) {
        snprintf(buf, size, "%s", yk_word_in(english, named ? YK_STR_YAYKIT_BATTERY_LOW_CHARGING : YK_STR_YAYKIT_SHEET_LOW_CHARGING));
        return buf;
    }
    const yk_str_t id = b->usb && b->full      ? YK_STR_YAYKIT_SHEET_BATTERY_FULL
                        : b->usb && b->stopped ? YK_STR_YAYKIT_SHEET_BATTERY_STOPPED
                        : b->charging          ? YK_STR_YAYKIT_SHEET_BATTERY_CHARGING
                                               : YK_STR_YAYKIT_SHEET_PERCENT;
    yk_wordf_in(english, value, sizeof(value), id, pct);
    if (named) yk_wordf_in(english, buf, size, YK_STR_YAYKIT_SHEET_BATTERY_NAMED, value);
    else snprintf(buf, size, "%s", value);
    return buf;
}

const char *yk_sheet_wifi_text(int wifi, const char *ssid, int rssi, int reach, bool english, bool *problem, char *buf,
                               size_t size) {
    bool wrong = false;
    if (!size) return buf;
    if (wifi == WIFI_CONNECTED && reach == REACH_FAILED) {
        wrong = true;
        snprintf(buf, size, "%s", yk_word_in(english, YK_STR_YAYKIT_WIFI_NO_SERVER));
    } else if (wifi == WIFI_CONNECTED) {
        const char *signal = rssi > -60 ? yk_word_in(english, YK_STR_YAYKIT_WIFI_STRONG) : rssi > -72 ? yk_word_in(english, YK_STR_YAYKIT_WIFI_FAIR) : yk_word_in(english, YK_STR_YAYKIT_WIFI_WEAK);
        if (ssid && ssid[0]) snprintf(buf, size, "%s · %s", ssid, signal);  // one line, cut with … at a character
        else snprintf(buf, size, "%s", signal);
    } else if (wifi == WIFI_CONNECTING) {
        snprintf(buf, size, "%s", yk_word_in(english, YK_STR_YAYKIT_WIFI_CONNECTING));  // connecting is never 没网
    } else if (wifi == WIFI_OFF) {
        // The app has not started it (yaykit_wifi has no stop: off means not started since this boot): its choice, not
        // a fault, so not 没打开 (user 11-02).
        snprintf(buf, size, "%s", yk_word_in(english, YK_STR_YAYKIT_WIFI_IDLE_OFF));
    } else {
        wrong = true;
        snprintf(buf, size, "%s", yk_word_in(english, YK_STR_YAYKIT_WIFI_NOT_CONNECTED));
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

const char *yk_sheet_wifi_value(int wifi, const char *ssid, int rssi, int reach, bool english, bool *problem,
                                yk_sheet_icon_t *icon, char *buf, size_t size) {
    yk_sheet_icon_t none = {0};
    if (wifi == WIFI_CONNECTED && reach != REACH_FAILED) {
        if (problem) *problem = false;
        if (icon) *icon = (yk_sheet_icon_t){.kind = YK_SHEET_ICON_WIFI, .level = rssi > -60 ? 3 : rssi > -72 ? 2 : 1};
        if (size) snprintf(buf, size, "%s", ssid ? ssid : "");
        return buf;
    }
    if (icon) *icon = none;
    return yk_sheet_wifi_text(wifi, ssid, rssi, reach, english, problem, buf, size);
}

const char *yk_sheet_battery_value(const yk_sheet_battery_t *b, bool english, yk_sheet_icon_t *icon, char *buf,
                                   size_t size) {
    yk_sheet_icon_t none = {0};
    // The words alone: not read, very low and charging (it would read 0 % for a long while), a fault.
    if (b->percent < 0 || (b->usb && (b->fault || b->low))) {
        if (icon) *icon = none;
        return yk_sheet_battery_text(b, false, english, buf, size);
    }
    const int pct = b->percent > 100 ? 100 : b->percent;
    const bool full = b->usb && b->full, stopped = b->usb && b->stopped;
    const bool charging = b->charging && !full && !stopped;
    if (icon)
        *icon = (yk_sheet_icon_t){.kind = YK_SHEET_ICON_BATTERY, .level = (uint8_t)pct, .charging = charging,
                                  .low = !charging && pct <= YK_SHEET_BATTERY_LOW};
    if (!size) return buf;
    if (stopped) return yk_sheet_battery_text(b, false, english, buf, size);  // 「76% · 没在充」: worth reading
    char n[12];
    yk_wordf_in(english, buf, size, YK_STR_YAYKIT_SHEET_PERCENT, yk_words_num(n, pct));
    return buf;
}

const char *yk_sheet_phone_value(int account, const char *nickname, bool english, bool *problem, yk_sheet_icon_t *icon,
                                 char *buf, size_t size) {
    yk_sheet_icon_t none = {0};
    if (account == ACCOUNT_BOUND) {
        if (problem) *problem = false;
        if (icon) *icon = (yk_sheet_icon_t){.kind = YK_SHEET_ICON_CHECK};
        if (size) snprintf(buf, size, "%s", nickname ? nickname : "");
        return buf;
    }
    if (icon) *icon = none;
    return yk_sheet_phone_text(account, nickname, english, problem, buf, size);
}

const char *yk_sheet_phone_text(int account, const char *nickname, bool english, bool *problem, char *buf, size_t size) {
    if (problem) *problem = account != ACCOUNT_BOUND && account != ACCOUNT_ACTIVATING;
    if (!size) return buf;
    if (account == ACCOUNT_BOUND && nickname && nickname[0])
        yk_wordf_in(english, buf, size, YK_STR_YAYKIT_PHONE_ADDED_TO, nickname);
    else if (account == ACCOUNT_BOUND) snprintf(buf, size, "%s", yk_word_in(english, YK_STR_YAYKIT_PHONE_ADDED));
    else if (account == ACCOUNT_ACTIVATING) snprintf(buf, size, "%s", yk_word_in(english, YK_STR_YAYKIT_PAIR_ADDING));
    else snprintf(buf, size, "%s", yk_word_in(english, YK_STR_YAYKIT_ACCOUNT_NOT_ADDED));
    return buf;
}
