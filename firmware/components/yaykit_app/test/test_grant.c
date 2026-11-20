// The question's words (yk_app_grant_words.c, the table's yaykit.folders.*): four languages, the 15 sets of kinds each, the kinds in their fixed
// order, and every line in its room: the question in two lines of 360 at 22, the buttons inside their round ends, the
// line after 「不允许」 within the safe circle at y 350 (Yay 0.1.19 开发文档 4.6). Widths as yay/test/text_width.h counts
// them, a little wider than the font: the acceptance holds to that.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "text_width.h"
#include "yk_app_grant.h"
#include "yk_words.h"

static int checks, failures;
#define CHECK(c)                                                         \
    do {                                                                 \
        checks++;                                                        \
        if (!(c)) {                                                      \
            failures++;                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
        }                                                                \
    } while (0)

#define CX 233
#define SAFE_R 200

/** The safe circle's width for a line of `px` at `y` (as yay/test/test_onboard.c's fits()). */
static double room_at(int y, int px) {
    const double far = fmax(fabs(y - (double)CX), fabs(y + px * 1.2 - CX));
    return far < SAFE_R ? 2 * sqrt((double)SAFE_R * SAFE_R - far * far) : 0;
}

/** Lines a label of `w` breaks `text` into, as LVGL does: at a space, or between any two characters of CJK. */
static int lines(const char *text, int px, double w) {
    int n = 1;
    double line = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        // The next piece: a word up to a space, or one CJK character.
        char piece[128];
        size_t len = 0;
        if (*p >= 0xE3) {  // U+3000 and up, as the three-byte leads of CJK: one character a piece
            len = 3;
        } else {
            while (p[len] && p[len] != ' ' && p[len] < 0xE3) len++;
            if (p[len] == ' ') len++;
        }
        memcpy(piece, p, len);
        piece[len] = 0;
        const double pw = text_width(piece, px);
        if (line > 0 && line + pw > w + 0.5) {
            n++;
            line = 0;
        }
        line += pw;
        p += len;
    }
    return n;
}

static void test_words(void) {
    // In yk_locale_t's order: en, zh-Hans, ja, es-419.
    const char *kinds[YK_LOCALE_COUNT][4] = {
        {"music", "stories", "pictures", "documents"},
        {"音乐", "故事", "图片", "文档"},
        {"音楽", "お話", "画像", "書類"},  // as the App's 「アプリの権限」 names them (device.permissions.foldersSub)
        {"música", "cuentos", "imágenes", "documentos"},
    };
    const char *tags[YK_LOCALE_COUNT] = {"en", "zh", "ja", "es"};
    for (int l = 0; l < YK_LOCALE_COUNT; l++) {
        yk_words_set((yk_locale_t)l);
        CHECK(!strcmp(yk_grant_font_tag(), tags[l]));
        CHECK(!yk_grant_question(0) && !yk_grant_question(16));
        for (unsigned m = 1; m <= 15; m++) {
            const char *q = yk_grant_question(m);
            CHECK(q && q[0]);
            if (!q) continue;
            // Each kind asked for is named, none other; and in the fixed order.
            const char *last = q;
            for (int k = 0; k < 4; k++) {
                const char *at = strstr(q, kinds[l][k]);
                CHECK(!!at == !!(m >> k & 1));
                if (at) {
                    CHECK(at >= last);
                    last = at;
                }
            }
            for (unsigned o = 1; o < m; o++) CHECK(strcmp(q, yk_grant_question(o)));  // 15 different ones
            // Two lines of 360 at 22, and the second inside the circle too.
            const int n = lines(q, YK_GRANT_ASK_PX, YK_GRANT_ASK_W);
            if (n > YK_GRANT_ASK_LINES) printf("  %s: 「%s」 takes %d lines\n", yk_grant_font_tag(), q, n);
            CHECK(n <= YK_GRANT_ASK_LINES);
        }
        // The buttons' words inside their round ends.
        const double inside = YK_GRANT_BUTTON_W - 2 * 12;
        CHECK(text_width(yk_grant_allow(), YK_GRANT_BUTTON_PX) <= inside);
        CHECK(text_width(yk_grant_deny(), YK_GRANT_BUTTON_PX) <= inside);
        // The line after no, one line inside the circle at y 350 (开发文档 4.6's table: 272, 271, 272, 282 of 293).
        const double later = text_width(yk_grant_later(), YK_GRANT_LATER_PX);
        printf("  %s: 「%s」 %.0f of %.0f px\n", yk_grant_font_tag(), yk_grant_later(), later,
               room_at(YK_GRANT_LATER_Y, YK_GRANT_LATER_PX));
        CHECK(later <= room_at(YK_GRANT_LATER_Y, YK_GRANT_LATER_PX));
    }
    // The question's two lines sit inside the circle: 360 is less than its width there.
    const int second = YK_GRANT_ASK_Y + (int)lround(YK_GRANT_ASK_PX * 1.45);
    CHECK(room_at(YK_GRANT_ASK_Y, YK_GRANT_ASK_PX) >= YK_GRANT_ASK_W && room_at(second, YK_GRANT_ASK_PX) >= YK_GRANT_ASK_W);
    CHECK(second + YK_GRANT_ASK_PX * 1.45 < YK_GRANT_BUTTON_Y);  // clear of the buttons
    // The buttons inside the circle: their outer corners' round ends.
    CHECK(YK_GRANT_ALLOW_X + YK_GRANT_BUTTON_W <= YK_GRANT_DENY_X && YK_GRANT_DENY_X + YK_GRANT_BUTTON_W <= 2 * CX - 63 + 1);
    // The deny hint's words as the App's page is called (the draft: 「应用权限」 / App permissions / アプリの権限 /
    // Permisos de apps).
    const char *page[YK_LOCALE_COUNT] = {"App permissions", "「应用权限」", "「アプリの権限」", "Permisos de apps"};
    for (int l = 0; l < YK_LOCALE_COUNT; l++) {
        yk_words_set((yk_locale_t)l);
        CHECK(strstr(yk_grant_later(), page[l]));
    }
    // The longest Chinese: 「想读你的音乐、故事、图片和文档」 in one line (the draft: 330 px).
    yk_words_set(YK_LOCALE_ZH_HANS);
    CHECK(lines(yk_grant_question(15), YK_GRANT_ASK_PX, YK_GRANT_ASK_W) == 1);
}

int main(void) {
    test_words();
    printf("yk_app grant words host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
