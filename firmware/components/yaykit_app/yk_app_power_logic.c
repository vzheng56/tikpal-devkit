// The device's settings and the battery guard, the pure parts (yk_app_power.h).
#include "yk_app_power.h"

#include <string.h>

void yk_power_init(yk_power_t *p) {
    memset(p, 0, sizeof(*p));
    p->percent = -1;
}

const char *yk_power_ev_name(yk_power_ev_t ev) {
    static const char *const names[] = {"none", "low", "plugged", "sleep"};
    return (unsigned)ev < sizeof(names) / sizeof(names[0]) ? names[ev] : "?";
}

yk_power_ev_t yk_power_feed(yk_power_t *p, int percent, bool usb) {
    const bool first = !p->known, was_usb = p->usb;
    p->known = true;
    p->usb = usb;
    if (percent >= 0) p->percent = percent > 100 ? 100 : percent;
    if (usb) {
        // Plugged in: whatever this discharge held back is lifted, and the next one starts from nothing.
        p->low = false;
        p->empty_reads = 0;
        return first || was_usb ? YK_POWER_NONE : YK_POWER_PLUGGED;
    }
    if (percent < 0) return YK_POWER_NONE;  // nothing read: what was is kept
    p->empty_reads = percent <= YK_POWER_EMPTY ? p->empty_reads + 1 : 0;
    if (p->empty_reads >= YK_POWER_EMPTY_READS && !p->sleep) {
        p->sleep = true;
        return YK_POWER_SLEEP;
    }
    if (percent <= YK_POWER_LOW && !p->low) {
        p->low = true;
        return YK_POWER_LOW_NOW;
    }
    return YK_POWER_NONE;
}

bool yk_power_low(const yk_power_t *p) { return !p->usb && p->low; }

int yk_power_read_ms(const yk_power_t *p) {
    const bool close = !p->usb && (p->low || p->empty_reads > 0);
    return close ? YK_POWER_READ_LOW_MS : YK_POWER_READ_MS;
}

bool yk_power_boot_sleep(bool brownout, int percent, bool usb) {
    if (usb) return false;
    if (brownout) return true;
    return percent >= 0 && percent <= YK_POWER_EMPTY;
}

bool yk_settings_english(const char *locale) {
    if (!locale || !locale[0]) return false;
    return !((locale[0] == 'z' || locale[0] == 'Z') && (locale[1] == 'h' || locale[1] == 'H'));
}

void yk_settings_merge(int volume, int brightness, int screen_off_s, const char *locale, bool low, bool keep_on,
                       yk_settings_t *out) {
    out->volume = volume < 0 ? 0 : volume > 10 ? 10 : volume;
    out->brightness = low && brightness > YK_POWER_BRIGHTNESS_MAX ? YK_POWER_BRIGHTNESS_MAX : brightness;
    out->screen_off_s = keep_on ? 0 : low ? YK_POWER_SCREEN_OFF_LOW_S : screen_off_s < 0 ? 0 : screen_off_s;
    out->english = yk_settings_english(locale);
}
