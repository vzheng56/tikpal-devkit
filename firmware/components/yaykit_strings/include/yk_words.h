// The screens' language and their words, for every firmware (Yay 0.1.19 开发文档 4.7, C 方案 5.3): the four languages'
// table (packages/strings, yaykit.* keys, generated into generated/ here) and the one language every screen goes by.
// Yay sets it from the owner's language, or the one chosen on the device before it has an owner (yay_locale.h); an
// extension from what it was started with (yaykit_app). Chinese until set. Pure C, host tested (test/run.sh).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "yk_strings.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The screens' language from now on. A value out of range is Chinese. */
void yk_words_set(yk_locale_t locale);
yk_locale_t yk_words_locale(void);
/** The screens are in Chinese. */
bool yk_words_chinese(void);

/** A line of the table in the screens' language: UTF-8, printf-style when it has placeholders (each a %s). */
const char *yk_word(yk_str_t id);

/** For an interface that says only English or not (yk_ui_set_english, the `bool english` parameters from before):
 *  Chinese when not `english`; else the screens' language when that is not Chinese (Japanese and Spanish stay), else
 *  English. */
yk_locale_t yk_words_pick(bool english);
/** yk_str(yk_words_pick(english), id). */
const char *yk_word_in(bool english, yk_str_t id);

/** The line with its placeholders filled (strings, in English's order: printf has no positions) into `out`; "" when
 *  `size` is 0. Returns `out`. */
const char *yk_wordf(char *out, size_t size, yk_str_t id, ...);
/** As yk_wordf, in yk_words_pick(english). */
const char *yk_wordf_in(bool english, char *out, size_t size, yk_str_t id, ...);

/** `n` as the table's placeholders take a number: decimal digits into `out` (12 bytes hold any int). Returns `out`. */
const char *yk_words_num(char out[12], int n);

#ifdef __cplusplus
}
#endif
