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

/** The owner's whistle (11-13), by its shape, measured from five takes of it: a glide up from 970 to 1590 Hz over
 *  240 ms, 250 ms quiet, a level 1190 Hz for 380 ms dying away. */
#define YK_WHISTLE_UP_FROM 970.0f
#define YK_WHISTLE_UP_TO 1590.0f
#define YK_WHISTLE_UP_MS 240
#define YK_WHISTLE_REST_MS 250
#define YK_WHISTLE_LEVEL 1190.0f
#define YK_WHISTLE_LEVEL_MS 380
#define YK_WHISTLE_TAIL 0.35f  // the level note ends at this much of where it began, then fades out
typedef struct {
    float scale;   // every pitch times this (1: as whistled)
    float pace;    // every length times this (1: as whistled)
    int group_ms;  // the group with its quiet after: the whistle, then silence to here
} yk_whistle_t;
/** One group of the whistle at `rate`, shaped by `w`, for yk_speak_cue_pcm(): same peak as yk_find_tone (just under
 *  -2 dBFS), soft edges (no click). Returns the samples written (cut to fit max; 0 for a bad argument). */
size_t yk_find_whistle(int rate, int16_t *out, size_t max, const yk_whistle_t *w);

/** Find my Tikpal's sound (docs/design/find-my-tikpal.md 4.3): the owner's whistle 1.68 times higher, so that all of it is
 *  where this speaker is loudest (1.4-2.8 kHz: the glide 1630 -> 2670 Hz, the level note 2.0 kHz), a group in 1.5 s.
 *  The owner chose it by ear on the device (11-13) over the whistle as whistled and over a tighter one a second. */
#define YK_FIND_SCALE 1.68f
#define YK_FIND_TONE_MS 1500  // one group: the whistle (870 ms), quiet to 1.5 s
/** One group of it at `rate` (yk_speak_rate()), for yk_speak_cue_pcm(): YK_FIND_TONE_MS of samples (fewer when max is
 *  smaller), soft edges (no click at any volume), peaks just under the -2 dBFS every sound stays under (board_speaker.h),
 *  so the ceiling does not touch it; no voice loudness. */
size_t yk_find_tone(int rate, int16_t *out, size_t max);

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

/** Speaker first, then the microphones (0.1.17): the codec is to be opened for microphones about to record. Open, the
 *  shared I2S's transmit side runs in the speaker's format (2 channels, 16-bit data) and the microphones only widen its
 *  slots: its DMA buffers keep their size. Closed, esp_codec_dev puts it in theirs (4 channels: 32-bit data), and the
 *  buffers are made again twice as big, about 10 KB more internal RAM at a key press (docs/design/yay-device.md). */
bool yk_speak_open_for_mic(bool codec_open, bool mic_open);

/** May the idle speaker close its codec? Not while it was just woken, the amplifier is on, or microphones are opening
 *  (the first second after yk_speak_mic(true)): closed then, they would open with the speaker closed, and the buffers
 *  big. Once they are open it may: under a recording the transmit side keeps running in its format (esp_codec_dev
 *  only marks it to stop with the microphones), its DMA unchanged, and the codec's DAC is off for the rest of it. */
bool yk_speak_may_close(bool codec_open, bool woken, bool pa_on, bool mic_opening);

// ---------------------------------------------------------------- gaps in an utterance (0.1.18 acceptance)

#define YK_GAP_MARKS 8  // line starts not heard yet: a reply has a few lines, and the ring holds a second or two

/** Where an utterance's lines begin in the ring, which one is heard, and the gap under way: what an underrun is to say
 *  (yaykit_speak's speak task). Lines are the session's audio frames' (yk_speak_line); -1 not known (a clip, a cue). */
typedef struct {
    struct {
        size_t pos;  // the ring's write count at the line's first sample
        int16_t line;
    } mark[YK_GAP_MARKS];
    uint8_t head, n;
    int16_t decoded;          // the line of the last packet decoded
    int16_t heard;            // the line being heard
    bool started;             // the utterance's first sample has been read
    int64_t utter_us, line_us;  // when the utterance and the line heard began to be heard
    bool open;                // a gap under way
    int64_t gap_us;           // when it began to be heard
    int16_t gap_line;
    int32_t gap_at_ms, gap_line_ms;  // where in the utterance and in its line
    uint32_t gaps, gap_max_ms;       // this utterance's
} yk_gaps_t;

/** What a gap was, once it is over. next: the line heard after it; -1 the utterance ended in it. */
typedef struct {
    int32_t at_ms;    // from the utterance's first sample heard to the gap
    int32_t line_ms;  // from its line's first sample heard
    int32_t gap_ms;   // silence heard
    int16_t line, next;
} yk_gap_t;

void yk_gaps_reset(yk_gaps_t *g);
/** A packet of `line` decoded, its samples written into the ring from `pos` (the ring's write count before it). */
void yk_gaps_decoded(yk_gaps_t *g, int line, size_t pos);
/** A block read: n samples of the ring from its read count t0, heard from heard_us; `short_` when fewer than the block
 *  came and the utterance is not all in (an underrun). True when this block ended a gap, said in *out. */
bool yk_gaps_block(yk_gaps_t *g, size_t t0, size_t n, bool short_, int64_t heard_us, int rate, yk_gap_t *out);
/** The utterance is all in and played out with a gap open (the end came late): true, said in *out with next -1. */
bool yk_gaps_end(yk_gaps_t *g, int64_t heard_us, yk_gap_t *out);
