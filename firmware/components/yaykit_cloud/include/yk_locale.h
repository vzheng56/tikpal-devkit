// The owner's language as the firmware uses it (docs/delivery/2026-10-24-yay-locale-sync.md 3): kept as the platform
// says it ("zh-Hans", "en", "ja", "es-419"; "zh" from before), read by its prefix. Most screens have Chinese and
// English: "zh…" is Chinese, anything else (en, ja, es-419, one not known yet) English, which is what 10-20 chose as the
// main language. Yay's first setup and pairing page have all four (0.1.17, yk_locale_lang). Pure C, host tested
// (test/run.sh).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YK_LOCALE_MAX 8  // with its terminator: "zh-Hans" just fits (NVS yk_acct "loc")

/** The platform's four languages (packages/contracts locales), for the screens that have them all. */
typedef enum { YK_LANG_ZH, YK_LANG_EN, YK_LANG_JA, YK_LANG_ES, YK_LANG_COUNT } yk_lang_t;

/** A value to keep: 2 to 7 characters, a letter first, then letters, digits and '-' ("zh-Hans", "es-419"). */
bool yk_locale_ok(const char *locale);
/** Whether screens are in English: not "zh…". NULL or "" (nothing kept: the device's default) is Chinese. */
bool yk_locale_english(const char *locale);
/** Which of the four a kept value is, by its language ("zh-Hans", "ja-JP", "es-419"). NULL or "" is Chinese and one not
 *  known yet English, as for yk_locale_english: it is YK_LANG_ZH exactly when that says false. */
yk_lang_t yk_locale_lang(const char *locale);
/** The platform's tag for it ("zh-Hans", "en", "ja", "es-419"): what yk_pref's `lang` keeps and onboarding.step's
 *  `locale` says. English for a value out of range. */
const char *yk_locale_tag(yk_lang_t lang);
/** The store's list and the catalog in that language: "zh", "en", "ja" or "es-419" (Yay 0.1.19 4.7: the device API's
 *  catalogLang folds any other tag to English, so a service that names apps in two languages still answers). */
const char *yk_locale_catalog(const char *locale);
/** `seen` (hello.ok's owner.locale) into `kept` (YK_LOCALE_MAX) when it is one to keep and another than kept. True
 *  when kept changed. */
bool yk_locale_take(char *kept, const char *seen);

#ifdef __cplusplus
}
#endif
