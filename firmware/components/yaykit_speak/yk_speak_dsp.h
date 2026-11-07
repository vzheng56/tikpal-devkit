// Pure C parts of yaykit_speak (host tested with test/run.sh): the playback ring, fades, level and the two cue
// tones. No ESP-IDF; the device wraps the ring in a critical section.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Mono int16 ring, one producer and one consumer. cap must be a power of two: positions wrap with & (cap - 1). */
typedef struct {
    int16_t *buf;
    size_t cap;
    size_t head, tail;  // free-running counts written / read
} yk_ring_t;

/** Whether n is a power of two (a ring's cap must be): `_Static_assert(YK_RING_POW2(RING), ...)` at the caller. */
#define YK_RING_POW2(n) ((n) > 0 && ((n) & ((n) - 1)) == 0)

/** A cap that is not a power of two (录音卡 0.1.0 had 32000: its sound came out in shuffled 256-sample blocks) is
 *  taken down to the power of two below it, so the ring stays in order, and said (yk_ring_cap_fixed; an error in
 *  the device's log). A power of two is used as it is. */
void yk_ring_init(yk_ring_t *r, int16_t *buf, size_t cap);
/** How many rings yk_ring_init had to take down since start-up (0 when every cap was a power of two). */
unsigned yk_ring_cap_fixed(void);
size_t yk_ring_avail(const yk_ring_t *r);
size_t yk_ring_space(const yk_ring_t *r);
/** Writes as much as fits; returns the samples written. */
size_t yk_ring_write(yk_ring_t *r, const int16_t *x, size_t n);
/** Reads up to n; returns the samples read. */
size_t yk_ring_read(yk_ring_t *r, int16_t *out, size_t n);
void yk_ring_clear(yk_ring_t *r);

/** Linear gain ramp across x, from g0 to g1 (0..1). */
void yk_fade(int16_t *x, size_t n, float g0, float g1);

/** Loudness of a chunk, 0..100: 0 below -60 dBFS RMS, 100 at -6 dBFS and above (for the mouth and the rim). */
uint8_t yk_level16(const int16_t *x, size_t n);

typedef enum { YK_CHIME_START = 1, YK_CHIME_END = 2 } yk_chime_t;

/** The short cue before the microphone opens (rising) or after it closes (falling): two notes, about 140 ms, at
 *  `rate`, peaking at about -3 dBFS. Returns the samples written (at most max). */
size_t yk_chime_synth(yk_chime_t kind, int rate, int16_t *out, size_t max);

/** Deprecated: use yk_speak_volume(). Volume step 0..10 to the codec's output volume (0..100, 0.5 dB a step, 100 = 0 dB),
 *  from the board's table (board_speaker.h). Device only: it is in yaykit_speak.c, not in this file's pure C. */
int yk_volume_to_codec(int level);

/** Is the codec to be muted outright? At step 0, on top of its volume 0 (-96 dB): the user asked for silence. A volume
 *  set by hand (`speak codec`, codec_vol >= 0; -1 = from the step) is as given. */
bool yk_speak_muted(int volume, int codec_vol);

/** May yk_speak_reclaim() open and close the codec now? Only with it closed (open, the speaker's format is already
 *  set), nothing to play, and the microphones not recording: opening the speaker then would put the shared I2S's
 *  clock (its transmit side, full duplex) back in the speaker's format under the recording. */
bool yk_speak_may_reclaim(bool codec_open, bool playing, bool mic_open);
