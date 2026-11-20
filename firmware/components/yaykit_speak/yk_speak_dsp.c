#include "yk_speak_dsp.h"

#include <math.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif

#ifndef M_PI  // not in strict C11 (glibc)
#define M_PI 3.14159265358979323846
#endif

#define YK_CHIME_AMP 1.0f  // before the envelope, whose top is about 0.72

static unsigned ring_cap_fixed;

void yk_ring_init(yk_ring_t *r, int16_t *buf, size_t cap) {
    if (!YK_RING_POW2(cap)) {
        size_t p = 1;
        while (p <= cap / 2) p *= 2;
        ring_cap_fixed++;
#ifdef ESP_PLATFORM
        ESP_LOGE("yk_ring", "cap %u is not a power of two: using %u", (unsigned)cap, (unsigned)(cap ? p : 0));
#endif
        cap = cap ? p : 0;
    }
    r->buf = buf;
    r->cap = cap;
    r->head = r->tail = 0;
}

unsigned yk_ring_cap_fixed(void) { return ring_cap_fixed; }

size_t yk_ring_avail(const yk_ring_t *r) { return r->head - r->tail; }
size_t yk_ring_space(const yk_ring_t *r) { return r->cap - (r->head - r->tail); }

size_t yk_ring_write(yk_ring_t *r, const int16_t *x, size_t n) {
    const size_t space = yk_ring_space(r);
    if (n > space) n = space;
    for (size_t i = 0; i < n; i++) r->buf[(r->head + i) & (r->cap - 1)] = x[i];
    r->head += n;
    return n;
}

size_t yk_ring_read(yk_ring_t *r, int16_t *out, size_t n) {
    const size_t avail = yk_ring_avail(r);
    if (n > avail) n = avail;
    for (size_t i = 0; i < n; i++) out[i] = r->buf[(r->tail + i) & (r->cap - 1)];
    r->tail += n;
    return n;
}

void yk_ring_clear(yk_ring_t *r) { r->tail = r->head; }

void yk_fade(int16_t *x, size_t n, float g0, float g1) {
    if (!n) return;
    for (size_t i = 0; i < n; i++) {
        const float g = g0 + (g1 - g0) * (float)i / (float)n;
        x[i] = (int16_t)lrintf((float)x[i] * g);
    }
}

uint8_t yk_level16(const int16_t *x, size_t n) {
    if (!n) return 0;
    double sum = 0;
    for (size_t i = 0; i < n; i++) sum += (double)x[i] * x[i];
    const double rms = sqrt(sum / n) / 32768.0;
    if (rms <= 0) return 0;
    const double db = 20 * log10(rms);
    if (db <= -60) return 0;
    if (db >= -6) return 100;
    return (uint8_t)lrint((db + 60) * 100.0 / 54.0);
}

/** One note with a 5 ms attack, an exponential decay to about -40 dB and a 2 ms release to silence, so it ends
 *  without a click at any level. */
static size_t note(int16_t *out, size_t max, int rate, float hz, int ms, float amp) {
    size_t n = (size_t)rate * ms / 1000;
    if (n > max) n = max;
    const size_t attack = (size_t)rate / 200, release = (size_t)rate / 500;
    for (size_t i = 0; i < n; i++) {
        const float t = (float)i / rate;
        const float tail = n - 1 - i < release ? (float)(n - 1 - i) / release : 1.0f;
        const float env = (i < attack ? (float)i / attack : 1.0f) * expf(-4.6f * (float)i / n) * tail;
        out[i] = (int16_t)lrintf(amp * env * sinf(2 * (float)M_PI * hz * t) * 32767);
    }
    return n;
}

size_t yk_chime_synth(yk_chime_t kind, int rate, int16_t *out, size_t max) {
    // E5 -> A5 to open the microphone, A5 -> E5 to close it, as loud as the notes go: the attack takes the peak to
    // about -3 dBFS, just under every sound's ceiling. These notes sit where the speaker is weak (1 kHz and below is
    // 20-30 dB down, yaykit/docs/architecture/audio.md), so this is still about 8 dB under the voice at step 6.
    const float first = kind == YK_CHIME_START ? 659.3f : 880.0f, second = kind == YK_CHIME_START ? 880.0f : 659.3f;
    size_t n = note(out, max, rate, first, 70, YK_CHIME_AMP);
    n += note(out + n, max - n, rate, second, 70, YK_CHIME_AMP);
    return n;
}

#define YK_FIND_AMP 0.78f  // -2.2 dBFS: under the -2 dBFS ceiling (26029), as loud as a sine goes without it

/** A raised-cosine edge: 0 at 0, 1 at 1. */
static float edge(float x) { return x <= 0 ? 0 : x >= 1 ? 1 : 0.5f - 0.5f * cosf((float)M_PI * x); }

/** One note of the whistle: `ms` long, its pitch going from `hz0` to `hz1` evenly in pitch (a glide; the same for a level
 *  note), `in_ms` up and `out_ms` down at its ends, and from full to `tail` across it (a whistle dying away). The phase
 *  runs on through the pitch change: no step, no click. */
static size_t whistle_note(int16_t *out, size_t max, int rate, float hz0, float hz1, int ms, int in_ms, int out_ms,
                           float tail) {
    size_t n = (size_t)rate * (size_t)ms / 1000;
    if (n > max) n = max;
    const float total = (float)rate * ms / 1000, in = (float)rate * in_ms / 1000, outn = (float)rate * out_ms / 1000;
    float phase = 0;
    for (size_t i = 0; i < n; i++) {
        const float t = (float)i / total, hz = hz0 * powf(hz1 / hz0, t);
        const float env = edge((float)i / in) * edge((total - 1 - (float)i) / outn) * (1 - (1 - tail) * t);
        out[i] = (int16_t)lrintf(YK_FIND_AMP * env * sinf(phase) * 32767);
        phase += 2 * (float)M_PI * hz / rate;
        if (phase > 2 * (float)M_PI) phase -= 2 * (float)M_PI;
    }
    return n;
}

static size_t quiet(int16_t *out, size_t max, size_t n) {
    if (n > max) n = max;
    memset(out, 0, n * sizeof(int16_t));
    return n;
}

size_t yk_find_whistle(int rate, int16_t *out, size_t max, const yk_whistle_t *w) {
    if (!out || rate <= 0 || !w || w->scale <= 0 || w->pace <= 0 || w->group_ms <= 0) return 0;
    const float k = w->scale, p = w->pace;
    size_t n = whistle_note(out, max, rate, YK_WHISTLE_UP_FROM * k, YK_WHISTLE_UP_TO * k, (int)lrintf(YK_WHISTLE_UP_MS * p), 12,
                            20, 1.0f);
    n += quiet(out + n, max - n, (size_t)rate * (size_t)lrintf(YK_WHISTLE_REST_MS * p) / 1000);
    n += whistle_note(out + n, max - n, rate, YK_WHISTLE_LEVEL * k, YK_WHISTLE_LEVEL * k, (int)lrintf(YK_WHISTLE_LEVEL_MS * p),
                      12, 60, YK_WHISTLE_TAIL);
    const size_t group = (size_t)rate * (size_t)w->group_ms / 1000;
    if (group > n) n += quiet(out + n, max - n, group - n);
    return n;
}

size_t yk_find_tone(int rate, int16_t *out, size_t max) {
    const yk_whistle_t w = {.scale = YK_FIND_SCALE, .pace = 1.0f, .group_ms = YK_FIND_TONE_MS};
    return yk_find_whistle(rate, out, max, &w);
}

bool yk_speak_muted(int volume, int codec_vol) { return volume <= 0 && codec_vol < 0; }

bool yk_speak_may_reclaim(bool codec_open, bool playing, bool mic_open) { return !codec_open && !playing && !mic_open; }

bool yk_speak_open_for_mic(bool codec_open, bool mic_open) { return mic_open && !codec_open; }

bool yk_speak_may_close(bool codec_open, bool woken, bool pa_on, bool mic_opening) {
    return codec_open && !woken && !pa_on && !mic_opening;
}

// ---------------------------------------------------------------- gaps in an utterance

void yk_gaps_reset(yk_gaps_t *g) {
    memset(g, 0, sizeof(*g));
    g->decoded = g->heard = g->gap_line = -1;
}

void yk_gaps_decoded(yk_gaps_t *g, int line, size_t pos) {
    if (line == g->decoded) return;
    g->decoded = (int16_t)line;
    if (g->n == YK_GAP_MARKS) {  // more lines ahead than kept: the oldest goes (it is still told when heard, a little late)
        g->head = (uint8_t)((g->head + 1) % YK_GAP_MARKS);
        g->n--;
    }
    const uint8_t at = (uint8_t)((g->head + g->n) % YK_GAP_MARKS);
    g->mark[at].pos = pos;
    g->mark[at].line = (int16_t)line;
    g->n++;
}

/** Lines that begin at or before `upto` are being heard (the last of them is): from heard_us at t0. */
static void heard_up_to(yk_gaps_t *g, size_t t0, size_t upto, int64_t heard_us, int rate) {
    while (g->n && g->mark[g->head].pos <= upto) {
        const size_t pos = g->mark[g->head].pos;
        g->heard = g->mark[g->head].line;
        g->line_us = heard_us + (pos > t0 ? (int64_t)(pos - t0) * 1000000 / rate : 0);
        g->head = (uint8_t)((g->head + 1) % YK_GAP_MARKS);
        g->n--;
    }
}

static void gap_said(yk_gaps_t *g, int64_t heard_us, int next, yk_gap_t *out) {
    out->at_ms = g->gap_at_ms;
    out->line_ms = g->gap_line_ms;
    out->gap_ms = (int32_t)((heard_us - g->gap_us) / 1000);
    out->line = g->gap_line;
    out->next = (int16_t)next;
    g->open = false;
    g->gaps++;
    if ((uint32_t)out->gap_ms > g->gap_max_ms) g->gap_max_ms = (uint32_t)out->gap_ms;
}

bool yk_gaps_block(yk_gaps_t *g, size_t t0, size_t n, bool short_, int64_t heard_us, int rate, yk_gap_t *out) {
    bool ended = false;
    if (n) {
        heard_up_to(g, t0, t0, heard_us, rate);  // the line of the first sample read
        if (!g->started) {
            g->started = true;
            g->utter_us = heard_us;
            if (g->line_us < heard_us) g->line_us = heard_us;
        }
        if (g->open) {
            gap_said(g, heard_us, g->heard, out);
            ended = true;
        }
        heard_up_to(g, t0, t0 + n - 1, heard_us, rate);  // a line that begins within this block
    }
    if (short_ && !g->open && g->started) {
        g->open = true;
        g->gap_us = heard_us + (int64_t)n * 1000000 / rate;  // silence after what came
        g->gap_line = g->heard;
        g->gap_at_ms = (int32_t)((g->gap_us - g->utter_us) / 1000);
        g->gap_line_ms = (int32_t)((g->gap_us - g->line_us) / 1000);
    }
    return ended;
}

bool yk_gaps_end(yk_gaps_t *g, int64_t heard_us, yk_gap_t *out) {
    if (!g->open) return false;
    gap_said(g, heard_us, -1, out);
    return true;
}
