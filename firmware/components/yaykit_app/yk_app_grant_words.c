// The question's words (yk_app_grant.h): the four languages' table (yaykit_strings, packages/strings' yaykit.folders.*),
// in the screens' language (yk_words: an extension's is set at start from the account's, yk_app_power.c). One sentence
// for every set of kinds, in the fixed order music, stories, pictures, documents (开发文档 4.6: 「15 种组合各写一句，好审、
// 好量宽度」). Pure, host tested (test/test_grant.c: every one there, in order, and in its room on the screen).
#include <stddef.h>

#include "yk_app_grant.h"
#include "yk_words.h"

// The kinds as bits (music 1, stories 2, pictures 4, documents 8), 1..15.
static const yk_str_t QUESTION[15] = {
    YK_STR_YAYKIT_FOLDERS_ASK_MUSIC,
    YK_STR_YAYKIT_FOLDERS_ASK_STORIES,
    YK_STR_YAYKIT_FOLDERS_ASK_MUSIC_STORIES,
    YK_STR_YAYKIT_FOLDERS_ASK_PICTURES,
    YK_STR_YAYKIT_FOLDERS_ASK_MUSIC_PICTURES,
    YK_STR_YAYKIT_FOLDERS_ASK_STORIES_PICTURES,
    YK_STR_YAYKIT_FOLDERS_ASK_MUSIC_STORIES_PICTURES,
    YK_STR_YAYKIT_FOLDERS_ASK_DOCUMENTS,
    YK_STR_YAYKIT_FOLDERS_ASK_MUSIC_DOCUMENTS,
    YK_STR_YAYKIT_FOLDERS_ASK_STORIES_DOCUMENTS,
    YK_STR_YAYKIT_FOLDERS_ASK_MUSIC_STORIES_DOCUMENTS,
    YK_STR_YAYKIT_FOLDERS_ASK_PICTURES_DOCUMENTS,
    YK_STR_YAYKIT_FOLDERS_ASK_MUSIC_PICTURES_DOCUMENTS,
    YK_STR_YAYKIT_FOLDERS_ASK_STORIES_PICTURES_DOCUMENTS,
    YK_STR_YAYKIT_FOLDERS_ASK_MUSIC_STORIES_PICTURES_DOCUMENTS,
};

const char *yk_grant_font_tag(void) {
    switch (yk_words_locale()) {
    case YK_LOCALE_EN: return "en";
    case YK_LOCALE_JA: return "ja";
    case YK_LOCALE_ES_419: return "es";
    default: return "zh";
    }
}

const char *yk_grant_question(unsigned folders) { return folders >= 1 && folders <= 15 ? yk_word(QUESTION[folders - 1]) : NULL; }
const char *yk_grant_allow(void) { return yk_word(YK_STR_YAYKIT_FOLDERS_ALLOW); }
const char *yk_grant_deny(void) { return yk_word(YK_STR_YAYKIT_FOLDERS_DENY); }
const char *yk_grant_later(void) { return yk_word(YK_STR_YAYKIT_FOLDERS_LATER); }
