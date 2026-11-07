// Runs yk_loud on a 16-bit mono WAV and reports what it does, also as heard through a speaker that gives little
// below ~1.2 kHz (a 2nd-order high-pass there stands in for the Tikpal R1's speaker). A host tool, not a test:
//   cc -std=c11 -O2 -I.. -o /tmp/loud_cli loud_cli.c ../yk_loud.c -lm      (in test/)
//   loud_cli <in.wav> <out.wav> <boost_db> <ceiling_dbfs> <hp_hz> [in_gain_db] [out_gain_db]
// in_gain_db / out_gain_db: the codec volume before / after (-10.5 is the old level 6, the reference of the
// 2026-11-06 measurements). replies/ has four of Yay's replies as they arrived (speak keep / speak dump).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "heard.h"
#include "yk_loud.h"

int main(int argc, char **argv) {
    if (argc < 6) return 2;
    unsigned char h[44];
    size_t n;
    int rate;
    int16_t *x = heard_read_wav(argv[1], h, &n, &rate);
    if (!x) return 1;
    int16_t *y = malloc(n * 2);
    const float in_g = argc > 6 ? (float)atof(argv[6]) : 0, out_g = argc > 7 ? (float)atof(argv[7]) : in_g;
    static yk_loud_t L;
    yk_loud_init(&L, rate, (float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5]));
    for (size_t i = 0; i < n; i += 320) yk_loud_process(&L, x + i, y + i, n - i < 320 ? n - i : 320);
    double p0, r0, h0, p1, r1, h1;
    heard_stats(x, n, rate, in_g, &p0, &r0, &h0);
    heard_stats(y, n, rate, out_g, &p1, &r1, &h1);
    printf("before (codec %+.1f dB): peak %6.1f  rms %6.1f  heard %6.1f dBFS\n", in_g, p0, r0, h0);
    printf("after  (codec %+.1f dB): peak %6.1f  rms %6.1f  heard %6.1f dBFS   -> heard %+.1f dB, limited %.0f%%\n",
           out_g, p1, r1, h1, h1 - h0, 100.0 * L.limited / n);
    FILE *o = fopen(argv[2], "wb");
    fwrite(h, 1, 44, o);
    fwrite(y, 2, n, o);
    fclose(o);
    return 0;
}
