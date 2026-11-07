// The speaker's tuning for this board: how far the amplifier goes, what the speaker cannot play, and what each step
// of the volume setting does. It belongs to the board, like the microphones' declaration, not to an app: a new board
// changes this table and nothing else. yaykit_speak reads it; apps never need it (they only set the volume).
// tikpal-r1 was measured on 2026-11-06 (yaykit/docs/architecture/audio.md, 放音的响度). Pure C (test/run.sh).
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float ceiling_dbfs;  // every sound's peak limit: under where the amplifier clips (tikpal-r1: -2)
    float voice_hp_hz;   // the voice's high-pass: the lows the speaker cannot play (tikpal-r1: 800)
    struct {
        uint8_t codec_vol;     // ES8311 output volume 0-100 (0.5 dB a step, 100 = 0 dB; 0 = -96 dB, and yaykit_speak mutes)
        float voice_boost_db;  // the voice's boost (yk_loud)
    } steps[11];               // the volume setting, 0-10
} board_speaker_t;

const board_speaker_t *board_speaker(void);

/** A volume step's codec volume and voice boost; a step out of range is taken to 0 or 10. */
int board_speaker_codec_vol(int step);
float board_speaker_voice_boost_db(int step);

#ifdef __cplusplus
}
#endif
