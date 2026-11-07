#include "yk_loud.h"

#include <math.h>
#include <string.h>

#ifndef M_PI  // not in strict C11 (glibc)
#define M_PI 3.14159265358979323846
#endif

#define LOOK_MS 2.0f      // look-ahead: the limiter starts turning down this long before a peak
#define RELEASE_MS 60.0f  // and lets go this slowly after it

void yk_loud_init(yk_loud_t *l, uint32_t rate, float boost_db, float ceiling_dbfs, float hp_hz) {
    memset(l, 0, sizeof(*l));
    l->on = boost_db > 0 || hp_hz > 0;
    const float boost = powf(10, boost_db / 20);
    l->ceiling = powf(10, ceiling_dbfs / 20);
    if (l->ceiling > 1) l->ceiling = 1;
    l->look = (uint32_t)(rate * LOOK_MS / 1000);
    if (l->look < 1) l->look = 1;
    if (l->look > YK_LOUD_LOOK_MAX) l->look = YK_LOUD_LOOK_MAX;
    // Attack: within about 1 % of the gain a peak needs by the time it leaves the delay (5 time constants).
    l->att = 1 - expf(-5.0f / (float)l->look);
    l->rel = 1 - expf(-1.0f / (rate * RELEASE_MS / 1000));
    if (hp_hz > 0) {
        // RBJ cookbook high-pass, Q = 1/sqrt(2)
        const float w = 2 * (float)M_PI * hp_hz / (float)rate, c = cosf(w), alpha = sinf(w) / (2 * 0.7071f);
        const float a0 = 1 + alpha;
        l->b0 = (1 + c) / 2 / a0;
        l->b1 = -(1 + c) / a0;
        l->b2 = (1 + c) / 2 / a0;
        l->a1 = -2 * c / a0;
        l->a2 = (1 - alpha) / a0;
    } else {
        l->b0 = 1;
    }
    // The filter is linear: the boost rides in its numerator.
    l->b0 *= boost, l->b1 *= boost, l->b2 *= boost;
    yk_loud_reset(l);
}

void yk_loud_reset(yk_loud_t *l) {
    l->s1 = l->s2 = 0;
    l->gain = 1;
    l->at = 0;
    l->qhead = l->qn = l->t = 0;
    for (uint32_t i = 0; i < YK_LOUD_LOOK_MAX; i++) l->dly[i] = 0;
    l->held = false;
}

/** 1 / x for x > 0, to about float precision. The ESP32-S3 has no float divide: `/` is a call into the ROM, and the
 *  loop would keep every float register in memory around it. */
static inline float recip(float x) {
    union {
        float f;
        uint32_t u;
    } v = {.f = x};
    v.u = 0x7EF127EAu - v.u;  // a guess within about 12 %, then Newton-Raphson
    float r = v.f;
    for (int i = 0; i < 3; i++) r *= 2 - x * r;
    return r;
}

void yk_loud_process(yk_loud_t *l, const int16_t *in, int16_t *out, size_t n) {
    if (!l->on) {
        if (out != in) memcpy(out, in, n * sizeof(int16_t));
        return;
    }
    // Every sample of every utterance, with the state in PSRAM: what can be, is in registers.
    const float b0 = l->b0, b1 = l->b1, b2 = l->b2, a1 = l->a1, a2 = l->a2;
    const float ceiling = l->ceiling, att = l->att, rel = l->rel;
    const uint32_t look = l->look;
    float s1 = l->s1, s2 = l->s2, gain = l->gain;
    uint32_t at = l->at, qhead = l->qhead, qn = l->qn, t = l->t, limited = l->limited;
    float peak = 0, target = 1;  // the loudest in the delay, and the gain it needs
    for (size_t i = 0; i < n; i++) {
        const float x = in[i] / 32768.0f;
        const float y = b0 * x + s1;  // transposed direct form II: two states to keep, not four
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        // The sample entering the delay, and the level it counts for when it leaves it.
        const float a = fabsf(y) > ceiling ? fabsf(y) : ceiling;
        const float leaving = l->dly[at];
        l->dly[at] = y;
        if (++at == look) at = 0;
        // Aim for the gain the loudest sample in the delay needs: it is reached before that sample comes out.
        // Queue: drop from the back what is no louder than this sample (this one outlasts it), then drop from the
        // front what has left the delay; the front is the loudest.
        while (qn && l->qa[(qhead + qn - 1) % YK_LOUD_LOOK_MAX] <= a) qn--;
        const uint32_t back = (qhead + qn) % YK_LOUD_LOOK_MAX;
        l->qa[back] = a;
        l->qt[back] = t;
        qn++;
        while (t - l->qt[qhead] >= look) qhead = (qhead + 1) % YK_LOUD_LOOK_MAX, qn--;
        t++;
        if (l->qa[qhead] != peak) {
            peak = l->qa[qhead];
            target = peak > ceiling ? ceiling * recip(peak) : 1;
        }
        gain += (target - gain) * (target < gain ? att : rel);
        float v = leaving * gain;
        if (gain < 0.9886f) limited++;  // -0.1 dB
        // The attack is a smooth curve, so a sudden peak can still be a hair over: never past the ceiling.
        if (v > ceiling) v = ceiling;
        else if (v < -ceiling) v = -ceiling;
        out[i] = (int16_t)((int32_t)(v * 32767.0f + 32768.5f) - 32768);  // rounds: always positive before the cut
    }
    l->s1 = s1, l->s2 = s2, l->gain = gain;
    l->at = at, l->qhead = qhead, l->qn = qn, l->t = t, l->limited = limited;
}

bool yk_loud_block(yk_loud_t *l, int16_t *x, size_t n, bool voice) {
    if (!voice) {
        if (!l->held) return false;
        memset(x, 0, n * sizeof(int16_t));
    }
    yk_loud_process(l, x, x, n);
    l->held = voice;
    return true;
}
