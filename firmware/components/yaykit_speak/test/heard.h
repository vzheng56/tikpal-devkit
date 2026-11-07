// "Heard": the level of a sound as far as the Tikpal R1's speaker plays it, for loud_cli.c and test_steps.c. A speaker
// that gives little below ~1.2 kHz (a 4th-order high-pass there stands in for it) is all this measures; the comparison
// of the volume steps is made in it (docs/delivery/2026-11-06-speaker-loudness.md 5).
#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    float b0, b1, b2, a1, a2, x1, x2, y1, y2;
} heard_bq_t;

static void heard_hp(heard_bq_t *f, float hz, float rate) {
    const float w = 2 * (float)M_PI * hz / rate, c = cosf(w), al = sinf(w) / (2 * 0.7071f), a0 = 1 + al;
    *f = (heard_bq_t){(1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0, -2 * c / a0, (1 - al) / a0, 0, 0, 0, 0};
}

static float heard_run(heard_bq_t *f, float x) {
    const float y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1, f->x1 = x, f->y2 = f->y1, f->y1 = y;
    return y;
}

/** peak dBFS, RMS of sounding 20 ms blocks (> -50 dBFS), and the same RMS through the "speaker", at a codec gain. */
static void heard_stats(const int16_t *x, size_t n, int rate, float gain_db, double *pk, double *rms, double *heard) {
    const float g = powf(10, gain_db / 20);
    heard_bq_t a, b;
    heard_hp(&a, 1200, (float)rate);
    heard_hp(&b, 1200, (float)rate);
    double p = 0, s = 0, sh = 0;
    size_t cnt = 0;
    const size_t blk = (size_t)rate / 50;
    for (size_t i = 0; i + blk <= n; i += blk) {
        double bs = 0, bh = 0;
        for (size_t k = 0; k < blk; k++) {
            const double v = x[i + k] / 32768.0 * g;
            if (fabs(v) > p) p = fabs(v);
            bs += v * v;
            const double h = heard_run(&b, heard_run(&a, (float)v));
            bh += h * h;
        }
        if (bs / blk > 1e-5 * g * g) s += bs, sh += bh, cnt += blk;
    }
    *pk = 20 * log10(p + 1e-12);
    *rms = 10 * log10(s / (cnt ? cnt : 1) + 1e-20);
    *heard = 10 * log10(sh / (cnt ? cnt : 1) + 1e-20);
}

/** A 16-bit mono WAV with a 44-byte header (kept in `header`, may be NULL): malloc'd samples, or NULL. */
static int16_t *heard_read_wav(const char *path, unsigned char *header, size_t *n, int *rate) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char h[44];
    int16_t *x = NULL;
    if (fread(h, 1, 44, f) == 44) {
        *rate = h[24] | h[25] << 8 | h[26] << 16 | h[27] << 24;
        fseek(f, 0, SEEK_END);
        *n = (size_t)(ftell(f) - 44) / 2;
        fseek(f, 44, SEEK_SET);
        x = malloc(*n * 2);
        if (x && fread(x, 2, *n, f) != *n) {
            free(x);
            x = NULL;
        }
        if (header) {
            for (int i = 0; i < 44; i++) header[i] = h[i];
        }
    }
    fclose(f);
    return x;
}
