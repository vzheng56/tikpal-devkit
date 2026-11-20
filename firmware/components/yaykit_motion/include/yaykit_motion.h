// The device's posture and movement from the QMI8658 (I2C 0x6B): extension platform P12, round-screen spec 6.
// Grown from Breathe's motion.c (10 Hz accelerometer, face down and pick-up) into a shared component: accelerometer
// and gyroscope at ~224 Hz, gravity and linear acceleration in screen coordinates, and the events every app uses.
//
//   Party Play      yk_motion_take() + yk_motion_state() + yk_motion_holder(500) every frame (33 ms, LVGL task):
//                   pick-up or shake rolls the dice, still lands them, gravity less linear acceleration pushes them,
//                   moving for the bomb, face down quiets, the holder's direction
//   Homework Buddy  take() at 56 Hz: face down dims the screen, face up or pick-up lights it
//   Language Buddy  on_event: shake for another one, face down / up
//   Selfie          on_event at 56 Hz: which way up while upright turns the screen
// (Translator does not link this.) Waking on pick-up and going quiet face down are each app's own, and each app's
// rules differ: this component only reports the events.
//
// Screen coordinates: x to the right, y down (as pixels), z out of the glass towards the viewer. Gravity points
// down: flat with the glass up it is (0, 0, -1) g. Angles on the screen are degrees clockwise from the top
// (12 o'clock), the way the screen is drawn: 90 = the right edge.
//
// The sensor's axes differ between boards; the mapping (and the accelerometer's bias) is the device's calibration,
// kept in the shared NVS (namespace yk_pref, key imu_cal) so every firmware reads the same. Without one the
// test device's measured mapping applies (sensor z faces the back).
//
// Pure C parts (the estimate, the events, CSV) are in yk_motion_core.c and host tested with recorded data
// (test/motion/*.csv). The device part runs one task that polls the sensor's registers every 4 ms at 224 Hz (every
// 16 ms at 56 Hz), a little faster than it samples, and skips a read whose sample counter has not moved (no FIFO).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YK_MOTION_NONE = 0,
    YK_MOTION_FLAT,             // lies flat, glass up: within flat_deg of level for orient_ms
    YK_MOTION_UPRIGHT,          // stands or is held upright: past upright_deg for orient_ms; `angle` = which screen
                                // direction is up (0: the top edge, 180: upside down)
    YK_MOTION_LIFT,             // picked up from rest: only once it has been still since yk_motion_start() (started
                                // in a hand, the first is after it was put down and picked up again)
    YK_MOTION_PUT_DOWN,         // at rest again after moving; `angle` / `confidence`: where the holder was in the
                                // half second before it came to rest (yk_motion_holder())
    YK_MOTION_SHAKE_START,      // `strength` 0..1 when it was recognised
    YK_MOTION_SHAKE_END,        // no strong peak for shake_end_ms
    YK_MOTION_FACE_DOWN_PROBE,  // glass down for down_probe_ms: soften (Breathe turns its sound down to 40 %).
                                // Without the gyroscope (an accelerometer-only feed) face down / up read the
                                // accelerometer's up component (50 ms low-pass) instead of the gravity estimate
    YK_MOTION_FACE_DOWN,        // glass down for down_ms: quiet (stop sounds, screen off; practice apps end)
    YK_MOTION_FACE_UP,          // turned back over after a probe or FACE_DOWN, for up_ms
    YK_MOTION_MOVING,           // started moving (anything in the last moving_window_ms)
    YK_MOTION_STILL,            // nothing for moving_window_ms
    YK_MOTION_KIND_COUNT,
} yk_motion_kind_t;

typedef struct {
    yk_motion_kind_t kind;
    int64_t t_ms;      // esp_timer milliseconds of the sample that decided it
    float strength;    // SHAKE_START: 0..1
    float angle;       // UPRIGHT: up; PUT_DOWN: the holder; degrees 0..360 clockwise from the top
    float confidence;  // PUT_DOWN: 0..1, as yk_motion_holder() (nobody tilted it in that half second: the last
                       // known direction, half as sure; 0 and angle 0 when there never was one)
} yk_motion_event_t;

/** One processed sample, screen coordinates. */
typedef struct {
    int64_t t_us;
    float lin[3];      // linear acceleration, g (gravity removed)
    float gravity[3];  // gravity, g, unit length, pointing down
    float gyro[3];     // rotation, degrees per second, bias removed
} yk_motion_sample_t;

/** Where the device is now. */
typedef struct {
    yk_motion_sample_t now;
    float tilt_deg;        // between the glass's normal and up: 0 flat glass up, 90 upright, 180 glass down
    float up_angle;        // which screen direction is up (meaningful when tilted, e.g. upright)
    bool flat, upright, face_down;
    bool still;            // at rest (still_g, still_dps for still_ms)
    bool moving;           // anything within moving_window_ms
    bool shaking;
    float shake_strength;  // 0..1: RMS of the linear acceleration over the last 250 ms, 0.5..3 g
    float holder_angle, holder_confidence;  // the holder direction as last worked out: by the latest PUT_DOWN or
                                            // yk_motion_holder() call (not updated on its own); call that for now
} yk_motion_state_t;

/** Thresholds (Party Play's design, "姿态检测", and Breathe's dev doc 6); apps may override any. */
typedef struct {
    float flat_deg;         // 25
    float upright_deg;      // 60
    int orient_ms;          // 500
    float still_g;          // 0.04: |a| within this of what it reads at rest on this device (1 g, 0.04 wider, until
                            // a steady second has shown it: yk_motion_core.h)
    float still_dps;        // 4 (the gyroscope less its bias, learned in steady seconds)
    int still_ms;           // 300
    float lift_g;           // 0.15 linear acceleration ...
    float lift_dps;         // ... or 25 dps rotation ...
    int lift_ms;            // ... for 80 ms, from rest
    float lift_tilt_deg;    // or the tilt changing by 20 degrees from rest
    int lift_gap_ms;        // 2000 between two pick-ups
    float shake_peak_g;     // 0.8: a peak of linear acceleration
    int shake_peaks;        // 3 peaks, each opposite to the one before ...
    int shake_window_ms;    // ... within 1000 ms
    int shake_end_ms;       // 400 without a peak ends it
    float down_g;           // 0.75: the glass faces down when the up component is below -0.75 g
    int down_probe_ms;      // 250
    int down_ms;            // 600 (Party Play's 1000 is within "1 s to quiet")
    float up_g;             // 0.30: turned back when above -0.30 g ...
    int up_ms;              // ... for 400 ms
    float moving_dps;       // 15
    float moving_g;         // 0.08
    int moving_window_ms;   // 1000
    float holder_min_deg;   // 17: the holder's direction counts between these tilts
    float holder_max_deg;   // 70
} yk_motion_thresholds_t;

void yk_motion_default_thresholds(yk_motion_thresholds_t *th);

/** The sensor-to-screen mapping and bias: the device's, shared by every firmware. */
typedef struct {
    int8_t axis[3];        // screen x, y, z from sensor axes: 1, 2, 3 = sensor x, y, z; negative = reversed
    float acc_bias[3];     // g, screen axes, subtracted
    float gyro_bias[3];    // dps, screen axes: where it starts; the component learns it again in steady seconds
} yk_motion_cal_t;

/** The test device's mapping, measured 2026-10 (sensor z faces the back; x, y as the factory firmware had them). */
void yk_motion_default_cal(yk_motion_cal_t *cal);

// ---------------------------------------------------------------- device

typedef void (*yk_motion_event_fn)(const yk_motion_event_t *ev, void *ctx);
/** Samples since the last call (every ~20 ms), oldest first. From the motion task: be quick (no LVGL). */
typedef void (*yk_motion_stream_fn)(const yk_motion_sample_t *s, size_t n, void *ctx);

typedef struct {
    int odr_hz;                              // 224 (0, default) or 56 (only events, less power)
    const yk_motion_thresholds_t *thresholds;  // NULL: defaults
    yk_motion_event_fn on_event;             // NULL: events are queued for yk_motion_take()
    void *ctx;
    bool stack_in_psram;                     // the task's 4 KB stack in PSRAM, not internal RAM (Yay's badge, 0.1.20:
                                             // internal RAM is short there); false as ever
} yk_motion_config_t;

/** Starts the sensor and its task (after board_init(): it is on the shared I2C bus). cfg may be NULL. Reads the
 *  calibration from NVS. ESP_ERR_NOT_FOUND when the sensor does not answer (the app works without motion). */
esp_err_t yk_motion_start(const yk_motion_config_t *cfg);
/** Stops the task and puts the sensor to sleep. */
void yk_motion_stop(void);
bool yk_motion_running(void);

/** Next queued event (when no on_event callback), oldest first; false when none. Queue of 16, oldest dropped. */
bool yk_motion_take(yk_motion_event_t *ev);
/** The state now (any task). */
void yk_motion_state(yk_motion_state_t *out);
/** Samples as they come, for what wants every one (a knock between two frames): every ~20 ms, a batch, from the motion
 *  task. NULL stops. One listener. (Nothing uses it yet: Party Play reads yk_motion_state() once a frame.) */
void yk_motion_stream(yk_motion_stream_fn fn, void *ctx);
/** The holder's direction: the circular mean of the screen tilt's direction over the last window_ms (Party Play
 *  asks for the half second before the bomb went off), and how sure (0..1). Only tilts between holder_min_deg and
 *  holder_max_deg count. When none did in that time (flat, upright, face down): the last known direction with its
 *  confidence halved, once for the whole stretch (asking again, even every frame, gives the same until a new
 *  direction is worked out); false when there never was one. To know whether someone holds it towards themselves
 *  now, look at yk_motion_state()'s tilt_deg as well. */
bool yk_motion_holder(int window_ms, float *angle, float *confidence);

/** New thresholds while running. */
esp_err_t yk_motion_set_thresholds(const yk_motion_thresholds_t *th);

/** The calibration in use; and to keep a new one for every firmware (NVS yk_pref/imu_cal). */
void yk_motion_calibration(yk_motion_cal_t *out);
esp_err_t yk_motion_save_calibration(const yk_motion_cal_t *cal);

/** The latest raw reading, sensor axes: g and dps. False before the first. */
bool yk_motion_raw(float acc[3], float gyr[3]);

/** `motion` (state and raw reading), `motion log <seconds>` (raw samples as CSV between MOTION_CSV_BEGIN and
 *  MOTION_CSV_END, numbered lines "MC <seq> <t_us,ax,ay,az,gx,gy,gz>": tools/yay_dev.py motion-log saves them),
 *  `motion events` (prints events as they come, for a minute), `motion cal flat|upright|save|reset` (the
 *  calibration: lay it flat glass up, then stand it so the screen reads upright). Development builds only. */
esp_err_t yk_motion_register_console(void);

#ifdef __cplusplus
}
#endif
