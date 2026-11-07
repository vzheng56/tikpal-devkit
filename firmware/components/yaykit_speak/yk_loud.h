// Loudness of the voice on the round screen's small speaker (yaykit_speak), pure C (host tested with test/run.sh).
//
// The Tikpal R1's amplifier (NS4150B) has its input resistors at 150 kΩ: a gain of 240k / 150k = 1.6, where the
// datasheet's circuit has 30 kΩ (8x). So the speaker is only loud when the codec plays near full scale, and speech
// from the cloud (about -9 dBFS peak, -22 dBFS RMS) uses little of that; the speaker also gives 20-30 dB less below
// 1 kHz (yaykit/docs/architecture/audio.md, 放音的响度). This stage makes the voice louder without clipping: a
// high-pass that keeps the lows the speaker cannot play from using up the swing, a fixed boost, and a look-ahead
// limiter that turns peaks down smoothly before they reach the ceiling.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define YK_LOUD_LOOK_MAX 64  // samples of look-ahead at most (2.7 ms at 24 kHz)

typedef struct {
    bool on;
    float b0, b1, b2, a1, a2;  // the high-pass (biquad, Q 0.707), the boost in b0-b2
    float s1, s2;
    float ceiling;             // linear
    float gain;                // the limiter's gain now
    float att, rel;            // per-sample smoothing towards a lower / higher gain
    uint32_t look;             // samples of look-ahead (= the delay this stage adds)
    float dly[YK_LOUD_LOOK_MAX];
    uint32_t at;
    // The loudest sample in the delay: a monotonic queue of (sample number, level, at least the ceiling), levels falling
    // from the front, so finding it costs the same however long the look-ahead is.
    float qa[YK_LOUD_LOOK_MAX];
    uint32_t qt[YK_LOUD_LOOK_MAX];
    uint32_t qhead, qn, t;
    uint32_t limited;          // samples the limiter turned down by more than 0.1 dB
    bool held;                 // voice went in and has not been run out of the delay (yk_loud_block)
} yk_loud_t;

/** boost_db: added to the voice (0 = off: the stage passes samples through untouched, no delay); ceiling_dbfs:
 *  the loudest sample out; hp_hz: the high-pass corner (0 = none). */
void yk_loud_init(yk_loud_t *l, uint32_t rate, float boost_db, float ceiling_dbfs, float hp_hz);
/** Between utterances: filters, delay and limiter start from silence. */
void yk_loud_reset(yk_loud_t *l);
/** In place is fine (in == out). */
void yk_loud_process(yk_loud_t *l, const int16_t *in, int16_t *out, size_t n);
/** One of the speaker's blocks, in place, n at least the look-ahead. `voice`: x is the voice, processed; true. Not voice,
 *  for the first block after some: zeros go through, so the delay's last samples come out now, behind what they
 *  followed, and not raised and with no fade in front of the voice when it comes back after a gap in the stream; true,
 *  and x is to be played. Otherwise false and x is left alone. */
bool yk_loud_block(yk_loud_t *l, int16_t *x, size_t n, bool voice);
