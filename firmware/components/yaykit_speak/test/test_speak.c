// Host tests for yk_speak_dsp: the playback ring, fades, level, cue tones.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI  // not in strict C11 (glibc)
#define M_PI 3.14159265358979323846
#endif

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

static void test_ring(void) {
    int16_t buf[8], out[8];
    yk_ring_t r;
    yk_ring_init(&r, buf, 8);
    CHECK(yk_ring_avail(&r) == 0 && yk_ring_space(&r) == 8);
    const int16_t a[] = {1, 2, 3, 4, 5, 6};
    CHECK(yk_ring_write(&r, a, 6) == 6);
    CHECK(yk_ring_read(&r, out, 4) == 4 && out[0] == 1 && out[3] == 4);
    // Wraps around the end of the buffer, keeps order.
    const int16_t b[] = {7, 8, 9, 10, 11, 12};
    CHECK(yk_ring_write(&r, b, 6) == 6 && yk_ring_avail(&r) == 8);
    CHECK(yk_ring_write(&r, b, 1) == 0);  // full: nothing written
    CHECK(yk_ring_read(&r, out, 8) == 8 && out[0] == 5 && out[1] == 6 && out[2] == 7 && out[7] == 12);
    CHECK(yk_ring_read(&r, out, 1) == 0);  // empty
    // Partial writes and reads.
    yk_ring_write(&r, a, 6);
    CHECK(yk_ring_write(&r, b, 6) == 2);
    CHECK(yk_ring_read(&r, out, 20) == 8 && out[6] == 7 && out[7] == 8);
    yk_ring_write(&r, a, 3);
    yk_ring_clear(&r);
    CHECK(yk_ring_avail(&r) == 0 && yk_ring_space(&r) == 8);
    // The free-running counts can pass SIZE_MAX without trouble.
    r.head = r.tail = (size_t)-3;
    CHECK(yk_ring_write(&r, a, 6) == 6 && yk_ring_avail(&r) == 6);
    CHECK(yk_ring_read(&r, out, 6) == 6 && out[0] == 1 && out[5] == 6);
    CHECK(yk_ring_cap_fixed() == 0);  // every cap so far a power of two
}

/** Pushes `total` counting samples through a ring of `cap` in uneven bites (as the microphones' 20 ms frames meet
 *  the encoder's 60 ms packets), and says whether every one came out where it went in. */
static bool ring_in_order(size_t cap, size_t total) {
    static int16_t buf[40000], in[512], out[2048];
    yk_ring_t r;
    yk_ring_init(&r, buf, cap);
    size_t wrote = 0, read = 0;
    bool ok = true;
    while (read < total) {
        size_t n = 320 + (wrote % 7) * 13;  // 320..398 at a time
        if (n > total - wrote) n = total - wrote;
        for (size_t i = 0; i < n; i++) in[i] = (int16_t)(wrote + i);
        wrote += yk_ring_write(&r, in, n);
        if (yk_ring_avail(&r) >= 960 || wrote == total) {
            const size_t got = yk_ring_read(&r, out, 960);
            for (size_t i = 0; i < got; i++) ok = ok && out[i] == (int16_t)(read + i);
            read += got;
        }
    }
    return ok;
}

static void test_ring_cap(void) {
    CHECK(YK_RING_POW2(32768) && YK_RING_POW2(8) && YK_RING_POW2(1));
    CHECK(!YK_RING_POW2(32000) && !YK_RING_POW2(0) && !YK_RING_POW2(24));
    // A power of two: as it always was, every sample in place.
    CHECK(ring_in_order(32768, 200000));
    CHECK(yk_ring_cap_fixed() == 0);
    // 32000 (录音卡 0.1.0): taken down to 16384, said, and in order.
    int16_t buf[32000];
    yk_ring_t r;
    yk_ring_init(&r, buf, 32000);
    CHECK(r.cap == 16384 && yk_ring_space(&r) == 16384);
    CHECK(yk_ring_cap_fixed() == 1);
    CHECK(ring_in_order(32000, 200000));
    CHECK(yk_ring_cap_fixed() == 2);
    // What 32000 did before the guard: positions & 31999 land in 256-sample blocks out of order.
    size_t clash = 0;
    for (size_t i = 0; i < 32000; i++) clash += (i & 31999) != i;
    CHECK(clash > 0);
    // The smallest cases.
    yk_ring_init(&r, buf, 3);
    CHECK(r.cap == 2);
    yk_ring_init(&r, buf, 0);
    CHECK(r.cap == 0 && yk_ring_space(&r) == 0);
    CHECK(yk_ring_cap_fixed() == 4);
}

static void test_fade(void) {
    int16_t x[100];
    for (int i = 0; i < 100; i++) x[i] = 10000;
    yk_fade(x, 100, 0, 1);
    CHECK(x[0] == 0 && x[50] == 5000 && x[99] > 9800 && x[99] < 10000);
    for (int i = 0; i < 100; i++) x[i] = -10000;
    yk_fade(x, 100, 1, 0);
    CHECK(x[0] == -10000 && x[99] > -200 && x[99] <= 0);
    yk_fade(x, 0, 0, 1);  // nothing to do
}

static void test_level(void) {
    int16_t x[320];
    memset(x, 0, sizeof(x));
    CHECK(yk_level16(x, 320) == 0);
    CHECK(yk_level16(x, 0) == 0);
    for (int i = 0; i < 320; i++) x[i] = (int16_t)(32767 * 0.5 * sin(i * 0.3));  // about -9 dBFS RMS
    const uint8_t loud = yk_level16(x, 320);
    CHECK(loud > 90 && loud < 100);
    for (int i = 0; i < 320; i++) x[i] = (int16_t)(32767 * 0.005 * sin(i * 0.3));  // about -49 dBFS
    const uint8_t quiet = yk_level16(x, 320);
    CHECK(quiet > 10 && quiet < 30);
    for (int i = 0; i < 320; i++) x[i] = i % 2 ? 32767 : -32768;
    CHECK(yk_level16(x, 320) == 100);
}

/** Strongest frequency in x by a coarse DFT scan from lo to hi, Hz. */
static float peak_hz_in(const int16_t *x, size_t n, int rate, float lo, float hi) {
    float best = 0, best_hz = 0;
    for (float hz = lo; hz < hi; hz += 5) {
        double re = 0, im = 0;
        for (size_t i = 0; i < n; i++) re += x[i] * cos(2 * M_PI * hz * i / rate), im += x[i] * sin(2 * M_PI * hz * i / rate);
        const float p = (float)(re * re + im * im);
        if (p > best) best = p, best_hz = hz;
    }
    return best_hz;
}

static float peak_hz(const int16_t *x, size_t n, int rate) { return peak_hz_in(x, n, rate, 400, 1200); }

static void test_chime(void) {
    static int16_t x[4000];
    const size_t n = yk_chime_synth(YK_CHIME_START, 16000, x, 4000);
    CHECK(n == 2240);  // two 70 ms notes
    int16_t peak = 0;
    for (size_t i = 0; i < n; i++) peak = (int16_t)(x[i] > peak ? x[i] : -x[i] > peak ? -x[i] : peak);
    CHECK(peak > 20000 && peak <= 26029);  // under the board's -2 dBFS by itself, so it passes the mixer as it is
    CHECK(x[0] == 0);                      // starts from silence (5 ms attack)
    CHECK(fabsf(peak_hz(x, 1120, 16000) - 659) < 10 && fabsf(peak_hz(x + 1120, 1120, 16000) - 880) < 10);  // rising
    yk_chime_synth(YK_CHIME_END, 16000, x, 4000);
    CHECK(fabsf(peak_hz(x, 1120, 16000) - 880) < 10 && fabsf(peak_hz(x + 1120, 1120, 16000) - 659) < 10);  // falling
    CHECK(yk_chime_synth(YK_CHIME_START, 16000, x, 1000) == 1000);  // cut to fit
    // Each note decays to near silence and ends on it: no click between them, none at the end.
    yk_chime_synth(YK_CHIME_START, 16000, x, 4000);
    int16_t tail = 0;
    for (size_t i = 1100; i < 1120; i++) tail = (int16_t)(x[i] > tail ? x[i] : -x[i] > tail ? -x[i] : tail);
    CHECK(tail < 300);
    CHECK(x[1119] == 0 && x[2239] == 0);
}

/** The owner's whistle (11-13) by its shape: a glide up, a rest, a level note dying away, quiet to the group's end. */
static void test_find_whistle(void) {
    static int16_t x[32000];
    const yk_whistle_t as_whistled = {1.0f, 1.0f, 1500}, higher = {1.68f, 1.0f, 1500}, tighter = {1.68f, 0.75f, 1000};
    CHECK(yk_find_whistle(16000, x, 32000, &as_whistled) == 24000);  // 1.5 s, whatever room there is
    CHECK(yk_find_whistle(16000, x, 32000, &tighter) == 16000);
    CHECK(yk_find_whistle(16000, x, 1000, &as_whistled) == 1000);  // cut to fit
    CHECK(yk_find_whistle(16000, NULL, 100, &as_whistled) == 0 && yk_find_whistle(0, x, 100, &as_whistled) == 0 &&
          yk_find_whistle(16000, x, 100, NULL) == 0);
    for (int v = 0; v < 3; v++) {
        const yk_whistle_t *w = v == 0 ? &as_whistled : v == 1 ? &higher : &tighter;
        const size_t n = yk_find_whistle(16000, x, 32000, w);
        // Each length to the whole ms, as the synthesis rounds it
        const size_t up = 16 * (size_t)lrintf(YK_WHISTLE_UP_MS * w->pace), rest = 16 * (size_t)lrintf(YK_WHISTLE_REST_MS * w->pace),
                     level = 16 * (size_t)lrintf(YK_WHISTLE_LEVEL_MS * w->pace);
        int16_t peak = 0;
        for (size_t i = 0; i < n; i++) peak = (int16_t)(x[i] > peak ? x[i] : -x[i] > peak ? -x[i] : peak);
        CHECK(peak > 25000 && peak <= 26029);  // as loud as the beeps, under the board's -2 dBFS
        // The glide: low at its start, high at its end; the level note where it should be
        const float k = w->scale;
        CHECK(fabsf(peak_hz_in(x + up / 8, up / 4, 16000, 600, 4000) - YK_WHISTLE_UP_FROM * k * 1.12f) < 120 * k);
        CHECK(fabsf(peak_hz_in(x + up * 5 / 8, up / 4, 16000, 600, 4000) - YK_WHISTLE_UP_TO * k * 0.92f) < 150 * k);
        CHECK(fabsf(peak_hz_in(x + up + rest + level / 4, level / 2, 16000, 600, 4000) - YK_WHISTLE_LEVEL * k) < 15);
        // Silence at both ends of each note, in the rest and after the level note: no click anywhere
        CHECK(x[0] == 0 && abs(x[up - 1]) < 300 && abs(x[up + rest]) < 300 && abs(x[up + rest + level - 1]) < 300);
        bool quiet = true;
        for (size_t i = up; i < up + rest; i++) quiet &= x[i] == 0;
        for (size_t i = up + rest + level; i < n; i++) quiet &= x[i] == 0;
        CHECK(quiet);
        // Dying away: the level note's last third quieter than its first
        double head = 0, tail = 0;
        for (size_t i = 0; i < level / 3; i++) {
            const double a = x[up + rest + level / 12 + i], b = x[up + rest + level / 2 + i];
            head += a * a, tail += b * b;
        }
        CHECK(tail < head * 0.6);
    }
}

/** Find my Tikpal's group (find-my-tikpal.md 4.3): the owner's whistle 1.68 times higher, a group in 1.5 s, all of it
 *  where the speaker is loudest (1.4-2.8 kHz). */
static void test_find_tone(void) {
    static int16_t x[40000], y[40000];
    CHECK(yk_find_tone(16000, x, 40000) == 24000);  // 1.5 s, whatever room there is
    CHECK(yk_find_tone(24000, x, 40000) == 36000);
    CHECK(yk_find_tone(16000, x, 1000) == 1000);    // cut to fit
    CHECK(yk_find_tone(16000, NULL, 100) == 0 && yk_find_tone(0, x, 100) == 0);
    const size_t n = yk_find_tone(16000, x, 40000);
    const yk_whistle_t b = {YK_FIND_SCALE, 1.0f, YK_FIND_TONE_MS};
    CHECK(yk_find_whistle(16000, y, 40000, &b) == n && !memcmp(x, y, n * sizeof(int16_t)));  // the whistle, as chosen
    const size_t up = 16 * YK_WHISTLE_UP_MS, rest = 16 * YK_WHISTLE_REST_MS, level = 16 * YK_WHISTLE_LEVEL_MS;
    // Every part between 1.4 and 2.8 kHz: the glide's start and end, the level note at 2.0 kHz
    CHECK(YK_WHISTLE_UP_FROM * YK_FIND_SCALE > 1400 && YK_WHISTLE_UP_TO * YK_FIND_SCALE < 2800);
    CHECK(fabsf(peak_hz_in(x + up + rest + level / 4, level / 2, 16000, 1000, 4000) - 2000) < 15);
    CHECK(peak_hz_in(x, up / 4, 16000, 1000, 4000) > 1400 && peak_hz_in(x + up * 3 / 4, up / 4, 16000, 1000, 4000) < 2800);
    int16_t peak = 0;
    for (size_t i = 0; i < n; i++) peak = (int16_t)(x[i] > peak ? x[i] : -x[i] > peak ? -x[i] : peak);
    CHECK(peak > 25000 && peak <= 26029);  // as loud as it goes under the board's -2 dBFS: the ceiling never touches it
    // Starts and ends on silence, quiet after the whistle to the end of the group: no click between groups
    CHECK(x[0] == 0 && x[n - 1] == 0);
    bool quiet = true;
    for (size_t i = up + rest + level; i < n; i++) quiet &= x[i] == 0;
    CHECK(quiet);
    // 12 ms in (192 samples): from silence, halfway up at most in the first 6 ms
    int16_t edge = 0;
    for (size_t i = 0; i < 96; i++) edge = (int16_t)(abs(x[i]) > edge ? abs(x[i]) : edge);
    CHECK(edge < 26029 / 2 + 400);
}

static void test_muted(void) {
    CHECK(yk_speak_muted(0, -1));                                  // step 0: silent, not just -50 dB
    for (int v = 1; v <= 10; v++) CHECK(!yk_speak_muted(v, -1));   // every other step sounds
    CHECK(!yk_speak_muted(0, 0) && !yk_speak_muted(0, 90));        // `speak codec` is as given, 0 too
    CHECK(!yk_speak_muted(6, 90));
}

static void test_reclaim(void) {
    CHECK(yk_speak_may_reclaim(false, false, false));  // a tap's microphones closed, the speaker idle
    CHECK(!yk_speak_may_reclaim(false, false, true));  // the microphones open again (a double click, a quick hold)
    CHECK(!yk_speak_may_reclaim(true, false, false));  // the codec open: its format is the speaker's already
    CHECK(!yk_speak_may_reclaim(false, true, false));  // something to play: its own open does it
    CHECK(!yk_speak_may_reclaim(true, true, true));
    // Speaker first, then the microphones (0.1.17): opened for them only when closed and they are about to record.
    CHECK(yk_speak_open_for_mic(false, true));
    CHECK(!yk_speak_open_for_mic(true, true));   // open already (playing, or not closed yet): nothing to do
    CHECK(!yk_speak_open_for_mic(false, false)); // no microphones: it stays closed
    CHECK(!yk_speak_open_for_mic(true, false));
    // ... and not closed while they open: idle, not just woken, the amplifier off, no microphones opening.
    CHECK(yk_speak_may_close(true, false, false, false));   // idle, or recording for over a second: the DAC goes off
    CHECK(!yk_speak_may_close(true, false, false, true));   // they are opening: it stays open
    CHECK(!yk_speak_may_close(true, true, false, false));  // woken for something: the next idle wait first
    CHECK(!yk_speak_may_close(true, false, true, false));  // the amplifier lingers
    CHECK(!yk_speak_may_close(false, false, false, false));  // closed already
}

/** Gaps in an utterance (0.1.18 acceptance): where each falls, between two lines or inside one, and how long. */
static void test_gaps(void) {
    const int rate = 16000, block = 320;  // 20 ms
    yk_gaps_t g;
    yk_gap_t gap;
    yk_gaps_reset(&g);
    CHECK(g.heard == -1 && !g.open && !g.gaps);
    // Lines 0 (0.3 s) and 1 decoded ahead, 2 later.
    for (size_t pos = 0; pos < 4800; pos += 960) yk_gaps_decoded(&g, 0, pos);
    for (size_t pos = 4800; pos < 9600; pos += 960) yk_gaps_decoded(&g, 1, pos);
    CHECK(g.n == 2 && g.decoded == 1);
    int64_t t = 1000000;  // heard_us
    size_t at = 0;
    // Line 0 plays whole, to its last sample.
    for (; at < 4800; at += block, t += 20000) CHECK(!yk_gaps_block(&g, at, block, false, t, rate, &gap));
    CHECK(g.started && g.heard == 0 && g.utter_us == 1000000);
    // Empty where line 1 should begin (its packets late): a gap, after line 0. 300 ms later it comes.
    // (Here line 1 was decoded already, as a test can: in a real one it would be late; the mark is what is read.)
    yk_gaps_t late = g;
    late.n = 0;  // line 1 not decoded yet
    late.decoded = 0;
    CHECK(!yk_gaps_block(&late, at, 0, true, t, rate, &gap) && late.open && late.gap_line == 0);
    CHECK(late.gap_at_ms == 300 && late.gap_line_ms == 300);
    yk_gaps_decoded(&late, 1, at);
    CHECK(yk_gaps_block(&late, at, block, false, t + 300000, rate, &gap));
    CHECK(gap.line == 0 && gap.next == 1 && gap.gap_ms == 300 && gap.at_ms == 300 && gap.line_ms == 300);  // between lines
    CHECK(late.heard == 1 && late.line_us == t + 300000 && late.gaps == 1 && late.gap_max_ms == 300);
    // Inside line 1: 100 samples come, then nothing; 140 ms of silence from there, then the rest of line 1.
    for (; at < 6000; at += block, t += 20000) yk_gaps_block(&g, at, block, false, t, rate, &gap);
    CHECK(g.heard == 1);
    CHECK(!yk_gaps_block(&g, at, 100, true, t, rate, &gap) && g.open && g.gap_line == 1);
    const int64_t silent = t + 100 * 1000000 / rate;
    CHECK(g.gap_line_ms == (int32_t)((silent - (1000000 + 300000)) / 1000));  // line 1 began 0.3 s in
    at += 100;
    CHECK(yk_gaps_block(&g, at, block, false, silent + 140000, rate, &gap));
    CHECK(gap.line == 1 && gap.next == 1 && gap.gap_ms == 140);  // inside a line
    CHECK(g.gaps == 1 && g.gap_max_ms == 140);
    // A short block that ends one gap opens the next (more came, then nothing again).
    at += block;
    CHECK(!yk_gaps_block(&g, at, 0, true, silent + 160000, rate, &gap) && g.open);
    CHECK(yk_gaps_block(&g, at, 50, true, silent + 200000, rate, &gap) && gap.gap_ms == 40 && g.open);
    // The end came late, with a gap open: said, next -1; with none open, nothing.
    CHECK(yk_gaps_end(&g, silent + 500000, &gap) && gap.next == -1 && !g.open && g.gaps == 3 && g.gap_max_ms == 296);
    CHECK(!yk_gaps_end(&g, silent + 600000, &gap));
    // Before the first sample is heard, a short block is the start, not a gap.
    yk_gaps_reset(&g);
    yk_gaps_decoded(&g, 3, 0);
    CHECK(!yk_gaps_block(&g, 0, 0, true, t, rate, &gap) && !g.open);
    // More lines ahead than marks: the oldest dropped, the newest still found.
    yk_gaps_reset(&g);
    for (int line = 0; line < YK_GAP_MARKS + 3; line++) yk_gaps_decoded(&g, line, (size_t)line * 1000);
    CHECK(g.n == YK_GAP_MARKS && g.mark[g.head].line == 3);
    yk_gaps_block(&g, 10000, block, false, t, rate, &gap);
    CHECK(g.heard == 10);
    // Not known (a clip): -1 throughout.
    yk_gaps_reset(&g);
    yk_gaps_decoded(&g, -1, 0);
    yk_gaps_block(&g, 0, block, false, t, rate, &gap);
    yk_gaps_block(&g, block, 10, true, t + 20000, rate, &gap);
    CHECK(yk_gaps_block(&g, block + 10, block, false, t + 80000, rate, &gap) && gap.line == -1 && gap.next == -1);
}

int main(void) {
    test_ring();
    test_ring_cap();
    test_fade();
    test_level();
    test_chime();
    test_find_tone();
    test_find_whistle();
    test_muted();
    test_reclaim();
    test_gaps();
    printf("yaykit_speak host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
