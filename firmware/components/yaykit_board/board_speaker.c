#include "board_speaker.h"

// tikpal-r1, decided by ear on 2026-11-06 (docs/delivery/2026-11-06-speaker-loudness.md 3): step 10 is the loudest the
// speaker goes cleanly, step 1 is a third of the way up that ladder, and every step is the same 2.4 dB. The boost runs
// evenly from 10 to 20 dB; each step's codec volume is the 0.5 dB mark that brings the replies in
// yaykit_speak/test/replies nearest that step's target. yaykit_speak/test/test_steps.c holds it to that: a change to
// the ceiling or the high-pass means solving this table again.
static const board_speaker_t TIKPAL_R1 = {
    .ceiling_dbfs = -2.0f,
    .voice_hp_hz = 800.0f,
    .steps =
        {
            {0, 0.0f},  // muted
            {68, 10.0f},
            {71, 11.1f},
            {74, 12.2f},
            {78, 13.3f},
            {81, 14.4f},
            {85, 15.6f},
            {88, 16.7f},
            {92, 17.8f},
            {96, 18.9f},
            {100, 20.0f},
        },
};

const board_speaker_t *board_speaker(void) { return &TIKPAL_R1; }

static int clamped(int step) { return step < 0 ? 0 : step > 10 ? 10 : step; }

int board_speaker_codec_vol(int step) { return TIKPAL_R1.steps[clamped(step)].codec_vol; }

float board_speaker_voice_boost_db(int step) { return TIKPAL_R1.steps[clamped(step)].voice_boost_db; }
