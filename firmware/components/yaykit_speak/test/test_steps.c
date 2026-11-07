// Holds board_speaker()'s table to what it was solved for (docs/delivery/2026-11-06-speaker-loudness.md 3, 5): on four
// of Yay's real replies (replies/), step 1 is about 3.7 dB over the old step 6, step 10 about 25.1, and every step the
// same 2.4 dB, in "heard" (heard.h). A failure here means the ceiling, the high-pass or yk_loud changed: solve the
// table again (the target is 3.6 + 21.5 x (step - 1) / 9). Also where the cue tones stand against the voice.
#include <stdio.h>

#include "board_speaker.h"
#include "heard.h"
#include "yk_loud.h"
#include "yk_speak_dsp.h"

static int failures, checks;
#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

#define OLD_STEP_6_DB (-10.5f)  // the codec's volume at the old default, the reference of the measurements
#define REPLIES 4

static yk_loud_t L;

/** The codec's gain at a step, dB. */
static float codec_db(int step) { return (float)(board_speaker_codec_vol(step) - 100) * 0.5f; }

int main(void) {
    const board_speaker_t *b = board_speaker();
    double over_old[11] = {0}, voice[11] = {0}, peak = -100;
    for (int r = 1; r <= REPLIES; r++) {
        char path[32];
        snprintf(path, sizeof(path), "replies/t%d.wav", r);
        size_t n = 0;
        int rate = 0;
        int16_t *x = heard_read_wav(path, NULL, &n, &rate);
        CHECK(x != NULL);
        if (!x) return 1;
        int16_t *y = malloc(n * 2);
        double p0, r0, h0;
        heard_stats(x, n, rate, OLD_STEP_6_DB, &p0, &r0, &h0);
        for (int s = 1; s <= 10; s++) {
            yk_loud_init(&L, (uint32_t)rate, board_speaker_voice_boost_db(s), b->ceiling_dbfs, b->voice_hp_hz);
            for (size_t i = 0; i < n; i += 320) yk_loud_process(&L, x + i, y + i, n - i < 320 ? n - i : 320);
            double p1, r1, h1;
            heard_stats(y, n, rate, 0, &p1, &r1, &h1);  // the peak before the codec: what the ceiling holds
            if (p1 > peak) peak = p1;
            heard_stats(y, n, rate, codec_db(s), &p1, &r1, &h1);
            over_old[s] += (h1 - h0) / REPLIES;
            voice[s] += h1 / REPLIES;
        }
        free(x);
        free(y);
    }
    for (int s = 1; s <= 10; s++)
        printf("  step %2d: codec %3d, boost %4.1f dB, heard %+5.1f dB over the old step 6\n", s,
               board_speaker_codec_vol(s), board_speaker_voice_boost_db(s), over_old[s]);
    CHECK(fabs(over_old[1] - 3.7) <= 0.5);
    CHECK(fabs(over_old[10] - 25.1) <= 0.5);
    for (int s = 1; s < 10; s++) {
        const double d = over_old[s + 1] - over_old[s];
        if (fabs(d - 2.4) > 0.5) printf("  step %d to %d: %+.2f dB\n", s, s + 1, d);
        CHECK(fabs(d - 2.4) <= 0.5);
    }
    printf("  loudest sample at any step: %.2f dBFS (ceiling %.1f)\n", peak, b->ceiling_dbfs);
    CHECK(peak <= b->ceiling_dbfs + 0.01);

    // The cue tones, which skip yk_loud, against the voice at the default step. They cannot come closer than this:
    // they are already at the ceiling and sit where the speaker is weak (docs: 放音的响度). Never over the voice.
    static int16_t cue[4000];
    const size_t n = yk_chime_synth(YK_CHIME_END, 16000, cue, 4000);
    double p, r, h;
    heard_stats(cue, n, 16000, codec_db(6), &p, &r, &h);
    printf("  cue at step 6: heard %+.1f dB against the voice\n", h - voice[6]);
    CHECK(h - voice[6] > -9 && h - voice[6] < 3);

    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "ok", checks, failures);
    return failures ? 1 : 0;
}
