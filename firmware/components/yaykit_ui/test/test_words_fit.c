// The shared notices' words in each of the four languages (yk_notice.h; 两套部署 · 语音单独同意 V3), held to the widths
// the shared font draws (yay/test/text_width.h, as Yay's own test_words_fit.c and yaykit_app's test_grant.c): one line
// of 18 px inside the content circle (r 200) at the line's widest row, at yk_notice_create()'s default height (y 360),
// and at the language buddy's and Face to Face's (y 404, lb_ui.c LINE_Y, tr_ui.c NOTICE_Y). There a line is 195 px wide
// and today's 「先在 Tikpal App 里添加」 is already wider in English, Japanese and Spanish: a notice added since may be no
// wider than that one in the same language. test_ui.c checks the eight characters and the rougher 0.5 em widths.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "text_width.h"
#include "yk_each_lang.h"
#include "yk_notice.h"
#include "yk_round.h"

static int checks, failures;
#define CHECK(c)                                                         \
    do {                                                                 \
        checks++;                                                        \
        if (!(c)) {                                                      \
            failures++;                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
        }                                                                \
    } while (0)

/** The line's width inside the content circle at its widest row: yk_notice_create() puts its 18 px label's top at
 *  y - 18, its line 1.2 em (as test_ui.c counts it). */
static double room(int y) {
    const double far = y - YK_SIZE_STATUS + YK_SIZE_STATUS * 1.2 - YK_CY;
    return 2 * sqrt((double)YK_R_CONTENT * YK_R_CONTENT - far * far);
}

#define EXTENSION_Y 404  // lb_ui.c LINE_Y, tr_ui.c NOTICE_Y

int main(void) {
    const double line = room(360), low = room(EXTENSION_Y);
    printf("yk_notice words: %.0f px at y 360, %.0f px at y %d\n", line, low, EXTENSION_Y);
    // The notices before V3 are as they ship: at y 360 not added's Spanish 「Primero agrégame en la app Tikpal」 is
    // already 310 px, listed (held); at y 404 not added is the widest. One added since fits at y 360 and is no wider
    // than not added at y 404.
    int held = 0;
    YK_EACH_LANG(en) {
        const double not_added = text_width(yk_notice_text(YK_NOTICE_NOT_ADDED, en), YK_SIZE_STATUS);
        for (int k = 0; k < YK_NOTICE_COUNT; k++) {
            const char *text = yk_notice_text((yk_notice_t)k, en);
            if (!text) continue;
            const double w = text_width(text, YK_SIZE_STATUS);
            if (w > line && k < YK_NOTICE_VOICE_CONSENT) {  // as it ships: listed, not this one's to change
                held++;
                printf("  held: notice %d in %s %.0f px of %.0f at y 360\n", k, YK_EACH_LANG_NAME(), w, line);
            } else {
                CHECK(w <= line);
            }
            if (k >= YK_NOTICE_VOICE_CONSENT) {
                CHECK(w <= low || w <= not_added);
                printf("  notice %d in %s: %.0f px (y %d: %.0f; not added there: %.0f)\n", k, YK_EACH_LANG_NAME(), w,
                       EXTENSION_Y, low, not_added);
            }
        }
    }
    CHECK(held <= 1);  // not added in Spanish, no more
    // 两套部署 · 语音单独同意 V3: yaykit.notice.voiceConsent, the table's own words in the four languages.
    YK_EACH_LANG(en) CHECK(yk_notice_text(YK_NOTICE_VOICE_CONSENT, en) && yk_notice_text(YK_NOTICE_VOICE_CONSENT, en)[0]);
    printf("yk_notice words: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
