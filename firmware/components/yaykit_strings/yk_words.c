// The screens' language and their words (yk_words.h).
#include "yk_words.h"

#include <stdarg.h>
#include <stdio.h>

static volatile yk_locale_t s_locale = YK_LOCALE_ZH_HANS;

void yk_words_set(yk_locale_t locale) { s_locale = (unsigned)locale < YK_LOCALE_COUNT ? locale : YK_LOCALE_ZH_HANS; }

yk_locale_t yk_words_locale(void) { return s_locale; }

bool yk_words_chinese(void) { return s_locale == YK_LOCALE_ZH_HANS; }

const char *yk_word(yk_str_t id) { return yk_str(s_locale, id); }

yk_locale_t yk_words_pick(bool english) {
    if (!english) return YK_LOCALE_ZH_HANS;
    return s_locale == YK_LOCALE_ZH_HANS ? YK_LOCALE_EN : s_locale;
}

const char *yk_word_in(bool english, yk_str_t id) { return yk_str(yk_words_pick(english), id); }

static const char *fill(char *out, size_t size, const char *format, va_list ap) {
    if (!size) return out;
    // The table's lines are formats of %s and %% only (packages/strings checks it), so the arguments are strings.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    vsnprintf(out, size, format, ap);
#pragma GCC diagnostic pop
    return out;
}

const char *yk_wordf(char *out, size_t size, yk_str_t id, ...) {
    va_list ap;
    va_start(ap, id);
    fill(out, size, yk_word(id), ap);
    va_end(ap);
    return out;
}

const char *yk_wordf_in(bool english, char *out, size_t size, yk_str_t id, ...) {
    va_list ap;
    va_start(ap, id);
    fill(out, size, yk_word_in(english, id), ap);
    va_end(ap);
    return out;
}

const char *yk_words_num(char out[12], int n) {
    snprintf(out, 12, "%d", n);
    return out;
}
