// Host tests: a loop body once in each of the four languages (Yay 0.1.19 4.7). The screens' language is set to it
// (yk_words_set) and `en` says English or not, as the `bool english` interfaces take it: zh-Hans false, the others true,
// so that what reads yk_words_pick shows Japanese and Spanish too. Chinese again after the last.
#pragma once

#include <stdbool.h>

#include "yk_words.h"

static inline int yk_each_lang_set(int i) {
    yk_words_set(i < YK_LOCALE_COUNT ? (yk_locale_t)i : YK_LOCALE_ZH_HANS);
    return i < YK_LOCALE_COUNT;
}

#define YK_EACH_LANG(en)                                                \
    for (int yk_lang_ = 0; yk_each_lang_set(yk_lang_); yk_lang_++)      \
        for (bool en = yk_lang_ != YK_LOCALE_ZH_HANS, yk_once_ = true; yk_once_; yk_once_ = false)

/** The language the loop is in, for a message. */
#define YK_EACH_LANG_NAME() ((const char *[]){"en", "zh-Hans", "ja", "es-419"}[yk_words_locale()])
