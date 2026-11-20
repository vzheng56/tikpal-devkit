// The system's question before an app reads the shared folders (Yay 0.1.19 开发文档 4.6, 交互稿「应用第一次读共享文件夹」),
// in an extension: yaykit_tf's yk_folder_ask() asks it through yk_app_folder_question() (yk_app_grant.c). Not in include/:
// no app draws it or anything like it; its words are the system's, four languages, never the app's.
//
// The page (466 x 466, over everything on LVGL's top layer): the app's disc (96, its first character) centred at y 134,
// its name 24 at y 196, 「想读你的音乐和故事」 22 at y 236 (360 wide, two lines at most), 「允许」 (coral, dark words) and
// 「不允许」 (panel grey) 164 x 60 at y 322, as first setup's pair. A tap on a button answers; the side key or 30 s
// untouched is not now. After yes the rim's sage ring for 0.8 s; after no one line for 3 s, 16 at y 350, pointing to
// the phone's 「应用权限」.
#pragma once

#include <stdint.h>

// The words are the four languages' table's (yaykit_strings, yaykit.folders.*), in the screens' language (yk_words).

/** The screens' language as the fonts take it (cjk_font_get_lang): "zh", "en", "ja", "es". */
const char *yk_grant_font_tag(void);
/** 「想读你的音乐和故事」: the kinds (bits: music 1, stories 2, pictures 4, documents 8) always in that order; NULL for 0
 *  or more than the four. */
const char *yk_grant_question(unsigned folders);
const char *yk_grant_allow(void);
const char *yk_grant_deny(void);
/** The line after 「不允许」: where to turn it on later. Its words name the App's page exactly (packages/strings). */
const char *yk_grant_later(void);

// Where things are (the interaction draft's numbers; test_grant.c holds the words to them).
#define YK_GRANT_DISC 96
#define YK_GRANT_DISC_CY 134
#define YK_GRANT_NAME_Y 196
#define YK_GRANT_NAME_PX 24
#define YK_GRANT_ASK_X 53
#define YK_GRANT_ASK_Y 236
#define YK_GRANT_ASK_W 360
#define YK_GRANT_ASK_PX 22
#define YK_GRANT_ASK_LINES 2
#define YK_GRANT_BUTTON_W 164
#define YK_GRANT_BUTTON_H 60
#define YK_GRANT_BUTTON_Y 322
#define YK_GRANT_ALLOW_X 63
#define YK_GRANT_DENY_X 239
#define YK_GRANT_BUTTON_PX 22
#define YK_GRANT_LATER_Y 350
#define YK_GRANT_LATER_PX 16
#define YK_GRANT_LATER_MS 3000
#define YK_GRANT_RING_MS 800
