// The owner's language (yk_locale.h).
#include "yk_locale.h"

#include <string.h>

bool yk_locale_ok(const char *locale) {
    const size_t n = locale ? strlen(locale) : 0;
    if (n < 2 || n >= YK_LOCALE_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        const char c = locale[i];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!letter && (i == 0 || !((c >= '0' && c <= '9') || c == '-'))) return false;
    }
    return true;
}

bool yk_locale_english(const char *locale) {
    if (!locale || !locale[0]) return false;
    return !((locale[0] == 'z' || locale[0] == 'Z') && (locale[1] == 'h' || locale[1] == 'H'));
}

/** The language subtag is `language` (two lower-case letters), whatever the case: "ja", "JA-jp", not "jam". */
static bool is_language(const char *locale, const char *language) {
    for (int i = 0; i < 2; i++) {
        const char c = locale[i];
        if ((c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c) != language[i]) return false;
    }
    return locale[2] == 0 || locale[2] == '-' || locale[2] == '_';
}

yk_lang_t yk_locale_lang(const char *locale) {
    if (!yk_locale_english(locale)) return YK_LANG_ZH;
    if (is_language(locale, "ja")) return YK_LANG_JA;
    if (is_language(locale, "es")) return YK_LANG_ES;
    return YK_LANG_EN;
}

const char *yk_locale_tag(yk_lang_t lang) {
    static const char *const TAGS[YK_LANG_COUNT] = {"zh-Hans", "en", "ja", "es-419"};
    return (unsigned)lang < YK_LANG_COUNT ? TAGS[lang] : TAGS[YK_LANG_EN];
}

const char *yk_locale_catalog(const char *locale) {
    static const char *const LANGS[YK_LANG_COUNT] = {"zh", "en", "ja", "es-419"};
    return LANGS[yk_locale_lang(locale)];
}

bool yk_locale_take(char *kept, const char *seen) {
    if (!yk_locale_ok(seen) || !strcmp(kept, seen)) return false;
    memcpy(kept, seen, strlen(seen) + 1);
    return true;
}
