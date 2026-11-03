#include "yk_conn_timing.h"

#include <stdio.h>
#include <string.h>

const char *yk_conn_tls_name(yk_conn_tls_t tls) {
    switch (tls) {
    case YK_CONN_TLS_FULL: return "full";
    case YK_CONN_TLS_RESUMED: return "resumed";
    default: return "none";
    }
}

/** Days since 1970-01-01 to a civil date (proleptic Gregorian; H. Hinnant's algorithm). */
static void civil(int64_t days, int *y, int *m, int *d) {
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t doe = days - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

/** The milliseconds between two moments, or "-" when either was not seen or they are out of order. */
static const char *span(char *buf, size_t size, int64_t from_us, int64_t to_us) {
    if (from_us <= 0 || to_us <= 0 || to_us < from_us) return "-";
    snprintf(buf, size, "%lld", (long long)((to_us - from_us) / 1000));
    return buf;
}

int yk_conn_timing_line(char *out, size_t size, const yk_conn_timing_t *t) {
    char at[32] = "-";
    if (t->at_ms > 0) {
        const int64_t s = t->at_ms / 1000;
        int y, mo, d;
        civil(s / 86400, &y, &mo, &d);
        const int sod = (int)(s % 86400);
        snprintf(at, sizeof(at), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", y, mo, d, sod / 3600, sod / 60 % 60, sod % 60,
                 (int)(t->at_ms % 1000));
    }
    // "yay/0.1.10": the app before the slash, its version after.
    const char *fw = t->firmware ? t->firmware : "";
    const char *slash = strchr(fw, '/');
    const int app_len = slash ? (int)(slash - fw) : (int)strlen(fw);
    const char *version = slash && slash[1] ? slash + 1 : "-";
    char up[24], hello[24], ok[24];
    return snprintf(out, size, "YAY_CONN at=%s fw=%s app=%.*s tls=%s upgradeMs=%s helloMs=%s helloOkMs=%s", at, version,
                    app_len ? app_len : 1, app_len ? fw : "-", yk_conn_tls_name(t->tls),
                    span(up, sizeof(up), t->dial_us, t->upgrade_us), span(hello, sizeof(hello), t->upgrade_us, t->hello_us),
                    span(ok, sizeof(ok), t->hello_us, t->ok_us));
}
