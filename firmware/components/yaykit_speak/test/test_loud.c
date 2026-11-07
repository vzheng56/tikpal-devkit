// Host tests for yk_loud: the voice's high-pass, boost and look-ahead limiter. Run: bash test/run.sh
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "yk_loud.h"

static int failures, checks;
#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

#define RATE 16000
#define N (RATE * 2)
static int16_t in[N], out[N];
static yk_loud_t L;

static double rms_db(const int16_t *x, size_t n) {
    double s = 0;
    for (size_t i = 0; i < n; i++) s += (double)x[i] * x[i];
    return 10 * log10(s / n / (32768.0 * 32768.0) + 1e-20);
}

static int peak(const int16_t *x, size_t n) {
    int p = 0;
    for (size_t i = 0; i < n; i++) p = abs(x[i]) > p ? abs(x[i]) : p;
    return p;
}

/** Amplitude of hz in x (Goertzel), relative to full scale. */
static double tone_amp(const int16_t *x, size_t n, double hz) {
    const double k = 2 * cos(2 * M_PI * hz / RATE);
    double s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; i++) {
        const double s0 = x[i] / 32768.0 + k * s1 - s2;
        s2 = s1, s1 = s0;
    }
    return 2 * sqrt(s1 * s1 + s2 * s2 - k * s1 * s2) / n;
}

static void sine(double hz, double dbfs) {
    const double a = pow(10, dbfs / 20) * 32767;
    for (int i = 0; i < N; i++) in[i] = (int16_t)lrint(a * sin(2 * M_PI * hz * i / RATE));
}

/** In 20 ms blocks, as the speaker task calls it. */
static void run(void) {
    for (int i = 0; i < N; i += 320) yk_loud_process(&L, in + i, out + i, 320);
}

static void test_off(void) {
    yk_loud_init(&L, RATE, 0, -1, 0);
    sine(1000, -6);
    run();
    CHECK(!memcmp(in, out, sizeof(in)));  // untouched, no delay
}

static void test_boost(void) {
    // A quiet tone gets the boost and nothing else: no limiting, no distortion.
    yk_loud_init(&L, RATE, 9, -1, 150);
    sine(1000, -24);
    run();
    const size_t from = RATE / 2;  // past the filter's settling
    const double gain = rms_db(out + from, N - from) - rms_db(in + from, N - from);
    printf("  loud: -24 dBFS tone +%.2f dB (boost 9, high-pass 150 Hz)\n", gain);
    CHECK(fabs(gain - 9) < 0.2);
    CHECK(L.limited == 0);
    // The delay is the look-ahead (2 ms).
    CHECK(L.look == 32);
}

static void test_limit(void) {
    // A loud tone is held at the ceiling with a steady gain: no clipping, the harmonics stay low.
    yk_loud_init(&L, RATE, 9, -1, 150);
    sine(1000, -3);
    run();
    const size_t from = RATE / 2, cycles = (N - from) / 16;  // 1 kHz: 16 samples a cycle
    const int16_t *y = out + from;
    const size_t n = cycles * 16;
    const double ceiling = pow(10, -1 / 20.0) * 32767;
    const double f0 = tone_amp(y, n, 1000);
    double h = 0;
    for (int k = 2; k <= 5; k++) h += pow(tone_amp(y, n, 1000.0 * k), 2);
    const double thd = sqrt(h) / f0;
    printf("  loud: -3 dBFS tone +9 dB -> peak %.2f dBFS, THD %.3f %%\n", 20 * log10(peak(y, n) / 32768.0),
           thd * 100);
    CHECK(peak(out, N) <= ceiling + 1);
    CHECK(peak(y, n) > ceiling - 200);  // and right up to it
    CHECK(thd < 0.005);
    CHECK(L.limited > 0);
}

static void test_bursts(void) {
    // Speech-like: noise bursts with sharp onsets at random levels. Never past the ceiling; much louder on average.
    yk_loud_init(&L, RATE, 9, -1, 150);
    unsigned seed = 1;
    for (int i = 0; i < N; i++) {
        const int burst = i / 1600;  // 100 ms each
        const double level = burst % 3 == 2 ? 0 : pow(10, (-20 + (burst * 7) % 18) / 20.0);
        seed = seed * 1103515245 + 12345;
        const double r = ((seed >> 8) & 0xFFFF) / 32768.0 - 1;
        in[i] = (int16_t)lrint(level * 0.9 * r * 32767);
    }
    run();
    const double ceiling = pow(10, -1 / 20.0) * 32767;
    printf("  loud: bursts peak %.2f -> %.2f dBFS, rms %.1f -> %.1f dBFS\n", 20 * log10(peak(in, N) / 32768.0),
           20 * log10(peak(out, N) / 32768.0), rms_db(in, N), rms_db(out, N));
    CHECK(peak(out, N) <= ceiling + 1);
    CHECK(rms_db(out, N) - rms_db(in, N) > 5);
}

static void test_highpass(void) {
    // 80 Hz is turned well down, 1 kHz is not.
    yk_loud_init(&L, RATE, 0.01f, 0, 150);
    sine(80, -20);
    run();
    const double low = rms_db(out + RATE / 2, N - RATE / 2) - rms_db(in + RATE / 2, N - RATE / 2);
    yk_loud_reset(&L);
    sine(1000, -20);
    run();
    const double mid = rms_db(out + RATE / 2, N - RATE / 2) - rms_db(in + RATE / 2, N - RATE / 2);
    printf("  loud: high-pass 150 Hz: 80 Hz %.1f dB, 1 kHz %.2f dB\n", low, mid);
    CHECK(low < -9);
    CHECK(fabs(mid) < 0.1);
}

/** Peak of the first `n` samples of a voice coming back with its 10 ms fade-in, in `block`-sample blocks. */
static int back_peak(size_t block, size_t n) {
    sine(1000, -9);
    for (size_t i = 0; i < 160; i++) in[i] = (int16_t)(in[i] * (double)i / 160);
    for (size_t i = 0; i < n; i += block) {
        memcpy(out + i, in + i, block * sizeof(int16_t));
        CHECK(yk_loud_block(&L, out + i, block, true));
    }
    return peak(out, n);
}

static void test_gap(void) {
    // The stream stalls in the middle of a sentence or a press cuts it off, then the voice comes back. What was in the
    // delay comes out in the first block after the voice, behind what it followed, and not in front of the voice's fade
    // when it comes back. At 20 ms and at 5 ms blocks (low latency).
    static const size_t blocks[] = {320, 80};
    for (int b = 0; b < 2; b++) {
        const size_t block = blocks[b];
        yk_loud_init(&L, RATE, 20, -2, 800);
        sine(1000, -9);
        for (size_t i = 0; i < 1600; i += block) {
            memcpy(out + i, in + i, block * sizeof(int16_t));
            CHECK(yk_loud_block(&L, out + i, block, true));
        }
        int16_t gap[320];
        for (size_t i = 0; i < block; i++) gap[i] = 1234;
        CHECK(yk_loud_block(&L, gap, block, false));          // the first block after the voice
        CHECK(peak(gap, L.look) > 20000);                     // its tail, at full level
        CHECK(peak(gap + block - 16, 16) < 400);              // and the filter dead by the end of the block
        gap[0] = 1234;
        CHECK(!yk_loud_block(&L, gap, block, false) && gap[0] == 1234);  // only once: nothing to do, x left alone
        const int after = back_peak(block, 320);
        const int first = peak(out, L.look);
        printf("  loud: back after a gap, %u-sample blocks: first 2 ms peak %d (voice %d)\n", (unsigned)block, first, after);
        CHECK(first < 400);  // what the filter had left, nothing of the old voice
        CHECK(after > 20000);
    }
    // The same without that block is what it is there for: the old voice, raised, at the front.
    yk_loud_init(&L, RATE, 20, -2, 800);
    sine(1000, -9);
    for (int i = 0; i < 1600; i += 320) yk_loud_process(&L, in + i, out + i, 320);
    sine(1000, -9);
    for (size_t i = 0; i < 160; i++) in[i] = (int16_t)(in[i] * (double)i / 160);
    yk_loud_process(&L, in, out, 320);
    CHECK(peak(out, L.look) > 20000);
    // Not the voice while a reset is waiting: an utterance's start finds nothing held.
    yk_loud_block(&L, out, 320, true);
    yk_loud_reset(&L);
    CHECK(!yk_loud_block(&L, out, 320, false));
}

static void test_defaults(void) {
    // The settings tikpal-r1 has at volume 10 (board_speaker.h), on bursts at the cloud's level (-9 dBFS peak):
    // never past -2 dBFS, and louder on average.
    yk_loud_init(&L, RATE, 20, -2, 800);
    unsigned seed = 7;
    for (int i = 0; i < N; i++) {
        const int burst = i / 1600;
        const double level = burst % 3 == 2 ? 0 : pow(10, (-14 + (burst * 5) % 10) / 20.0);
        seed = seed * 1103515245 + 12345;
        in[i] = (int16_t)lrint(level * 0.9 * (((seed >> 8) & 0xFFFF) / 32768.0 - 1) * 32767);
    }
    run();
    const double ceiling = pow(10, -2 / 20.0) * 32767;
    CHECK(peak(out, N) <= ceiling + 1);
    CHECK(rms_db(out, N) > rms_db(in, N));
}

int main(void) {
    test_off();
    test_boost();
    test_limit();
    test_bursts();
    test_highpass();
    test_gap();
    test_defaults();
    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "ok", checks, failures);
    return failures ? 1 : 0;
}
