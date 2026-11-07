// Host tests for yk_speak_dsp: the playback ring, fades, level, cue tones.
#include <math.h>
#include <stdio.h>
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

/** Strongest frequency in x by a coarse DFT scan, Hz. */
static float peak_hz(const int16_t *x, size_t n, int rate) {
    float best = 0, best_hz = 0;
    for (float hz = 400; hz < 1200; hz += 5) {
        double re = 0, im = 0;
        for (size_t i = 0; i < n; i++) re += x[i] * cos(2 * M_PI * hz * i / rate), im += x[i] * sin(2 * M_PI * hz * i / rate);
        const float p = (float)(re * re + im * im);
        if (p > best) best = p, best_hz = hz;
    }
    return best_hz;
}

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
}

int main(void) {
    test_ring();
    test_ring_cap();
    test_fade();
    test_level();
    test_chime();
    test_muted();
    test_reclaim();
    printf("yaykit_speak host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
