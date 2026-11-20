// The vibration's scheduling, pure C: see yk_haptic_core.h.
#include "yk_haptic_core.h"

#include <string.h>

#define NEVER (INT64_MIN / 2)

ykh_settings_t ykh_settings_read(int stored_on, int stored_level) {
    const ykh_settings_t s = {.on = stored_on != 0, .level = ykh_level_clamp(stored_level)};  // missing (-1): on
    return s;
}

int ykh_level_clamp(int level) { return level >= YKH_LEVEL_LIGHT && level <= YKH_LEVEL_STRONG ? level : YKH_LEVEL_LIGHT; }

int ykh_level_duty(int level) {
    switch (ykh_level_clamp(level)) {
    case YKH_LEVEL_STRONG: return YKH_DUTY_FULL;
    case YKH_LEVEL_MEDIUM: return YKH_DUTY_FULL * 3 / 4;  // 768
    default: return YKH_DUTY_FULL / 2;                     // 512
    }
}

int ykh_builtin(int kind, uint16_t seg[YKH_SEG_MAX]) {
    static const struct {
        int n;
        uint16_t seg[5];
    } KINDS[] = {
        {1, {40}},                           // 1 tick: a pick, one step of a dial
        {3, {40, 160, 40}},                  // 2 double: confirmed, done
        {1, {120}},                          // 3 short: a hint
        {1, {500}},                          // 4 long: a timer's end, an alarm's beat
        {5, {200, 250, 200, 250, 200}},      // 5 notify: a message, a reminder (200 / 250, three times)
    };
    if (kind < 1 || kind > (int)(sizeof(KINDS) / sizeof(KINDS[0]))) return 0;
    memcpy(seg, KINDS[kind - 1].seg, sizeof(uint16_t) * (size_t)KINDS[kind - 1].n);
    return KINDS[kind - 1].n;
}

ykh_check_t ykh_check(const uint16_t *seg, int n) {
    if (!seg || n < 1 || n > YKH_SEG_MAX) return YKH_BAD_COUNT;
    int on = 0;
    for (int i = 0; i < n; i++) {
        if (seg[i] < YKH_SEG_MIN_MS) return YKH_BAD_SEGMENT;
        if (i % 2 == 0) on += seg[i];
    }
    return on > YKH_ON_MAX_MS ? YKH_TOO_LONG : YKH_OK;
}

void ykh_init(ykh_core_t *c, ykh_settings_t set) {
    memset(c, 0, sizeof(*c));
    c->set = set;
    c->i = -1;
    c->on_since_ms = NEVER;
    c->off_at_ms = NEVER;
    c->rest_until_ms = NEVER;
}

static void set_out(ykh_core_t *c, int duty, int64_t now) {
    if (duty > 0 && c->out == 0) c->on_since_ms = now;
    if (duty == 0 && c->out > 0) c->off_at_ms = now;
    c->out = duty;
}

static void end_play(ykh_core_t *c, int64_t now) {
    c->active = false;
    c->n = 0;
    c->i = -1;
    c->kick_end_ms = 0;
    set_out(c, 0, now);
}

/** Segment c->i begins now (on at even indexes). An on from rest kicks; an on that goes on from a pulse already
 *  running keeps that pulse's kick to its end, or takes the play's duty at once. */
static void begin_segment(ykh_core_t *c, int64_t now) {
    c->seg_end_ms = now + c->seg[c->i];
    if (c->i % 2) {
        c->kick_end_ms = 0;
        set_out(c, 0, now);
        return;
    }
    const bool kick = c->kick && c->duty < YKH_DUTY_FULL;
    if (c->out == 0 && kick) {
        c->kick_end_ms = now + YKH_KICK_MS;
        set_out(c, YKH_DUTY_FULL, now);
    } else if (!(c->out > 0 && kick && c->kick_end_ms > now)) {
        c->kick_end_ms = 0;
        set_out(c, c->duty, now);
    }
}

static void advance(ykh_core_t *c, int64_t now, unsigned quiet) {
    if (!c->active) return;
    if (quiet || c->shut || !c->set.on) {
        end_play(c, now);
        return;
    }
    if (c->i < 0) {
        if (now < c->start_ms) return;
        c->i = 0;
        begin_segment(c, now);
    } else if (now >= c->seg_end_ms) {  // a late wake: the next segment gets its whole length from now
        if (++c->i >= c->n) {
            end_play(c, now);
            return;
        }
        begin_segment(c, now);
    }
    if (c->kick_end_ms && now >= c->kick_end_ms) {
        c->kick_end_ms = 0;
        if (c->out > 0) set_out(c, c->duty, now);
    }
    // On too long in one go: plays that replaced plays while the motor ran (one play alone stays within it, ykh_check).
    // After the segment's own end, so that a pulse of exactly YKH_ON_MAX_MS simply ends.
    if (c->out > 0 && now - c->on_since_ms >= YKH_ON_MAX_MS) {
        end_play(c, now);
        c->rest_until_ms = now + YKH_REST_MS;
    }
}

static void fill(const ykh_core_t *c, int64_t now, ykh_out_t *out) {
    out->duty = c->out;
    if (!c->active) {
        out->wake_ms = -1;
        return;
    }
    int64_t w = c->i < 0 ? c->start_ms : c->seg_end_ms;
    if (c->i >= 0 && c->kick_end_ms && c->kick_end_ms < w) w = c->kick_end_ms;
    if (c->out > 0) {
        if (now + YKH_POLL_MS < w) w = now + YKH_POLL_MS;
        if (c->on_since_ms + YKH_ON_MAX_MS < w) w = c->on_since_ms + YKH_ON_MAX_MS;
    }
    out->wake_ms = w;
}

void ykh_set_settings(ykh_core_t *c, ykh_settings_t set, int64_t now_ms, ykh_out_t *out) {
    c->set.on = set.on;
    c->set.level = ykh_level_clamp(set.level);
    if (!c->set.on) end_play(c, now_ms);
    fill(c, now_ms, out);
}

ykh_result_t ykh_play(ykh_core_t *c, int64_t now_ms, const uint16_t *seg, int n, int duty, bool kick, unsigned quiet,
                      ykh_out_t *out) {
    ykh_result_t r = YKH_PLAYED;
    if (c->shut) r = YKH_SHUT;
    else if (ykh_check(seg, n) != YKH_OK || duty < 1 || duty > YKH_DUTY_FULL) r = YKH_BAD;
    else if (!c->set.on) r = YKH_OFF;
    else if (quiet) r = YKH_QUIET;
    if (r != YKH_PLAYED) {
        advance(c, now_ms, quiet);  // a bad one leaves what plays as it was; a quiet one stops it
        fill(c, now_ms, out);
        return r;
    }
    memcpy(c->seg, seg, sizeof(uint16_t) * (size_t)n);
    c->n = n;
    c->duty = duty;
    c->kick = kick;
    c->active = true;
    if (c->out > 0) {  // the motor runs: the new play's first on goes on from here (a cap still counts from its start)
        c->i = 0;
        begin_segment(c, now_ms);
    } else {
        int64_t start = c->off_at_ms + YKH_GAP_MS;
        if (c->rest_until_ms > start) start = c->rest_until_ms;
        c->i = -1;
        c->start_ms = start > now_ms ? start : now_ms;
        c->kick_end_ms = 0;
    }
    advance(c, now_ms, 0);
    fill(c, now_ms, out);
    return YKH_PLAYED;
}

void ykh_step(ykh_core_t *c, int64_t now_ms, unsigned quiet, ykh_out_t *out) {
    advance(c, now_ms, quiet);
    fill(c, now_ms, out);
}

void ykh_stop(ykh_core_t *c, int64_t now_ms, ykh_out_t *out) {
    end_play(c, now_ms);
    fill(c, now_ms, out);
}

void ykh_shut(ykh_core_t *c, int64_t now_ms, ykh_out_t *out) {
    c->shut = true;
    end_play(c, now_ms);
    fill(c, now_ms, out);
}

bool ykh_busy(const ykh_core_t *c) { return c->active; }
