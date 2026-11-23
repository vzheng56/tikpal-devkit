#include "yk_proto.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

// From this many bytes a message's cJSON tree goes to PSRAM (yk_proto_parse).
#define YK_PROTO_PSRAM_FROM 2048

// ---------------------------------------------------------------- small checks (the zod schemas in device.ts)

static bool alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

bool yk_proto_turn_ok(const char *t) {
    if (!t) return false;
    const size_t n = strlen(t);
    if (n != YK_PROTO_TURN_LEN) return false;
    const size_t from = (t[0] == 's' && t[1] == '-') ? 2 : 0;
    for (size_t i = from; i < n; i++)
        if (!alnum(t[i])) return false;
    return true;
}

bool yk_proto_server_turn(const char *t) { return t && t[0] == 's' && t[1] == '-'; }

void yk_proto_new_turn(char out[YK_PROTO_TURN_LEN + 1], const uint8_t random[16]) {
    static const char set[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < YK_PROTO_TURN_LEN; i++) out[i] = set[random[i] % 62];
    if (out[0] == 's' && out[1] == '-') out[1] = 'x';  // cannot happen ('-' is not in the set), kept explicit
    out[YK_PROTO_TURN_LEN] = 0;
}

bool yk_proto_slug_ok(const char *s) {
    if (!s) return false;
    const size_t n = strlen(s);
    if (n < 2 || n > 31 || s[0] < 'a' || s[0] > 'z') return false;
    for (size_t i = 1; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '-')) return false;
    return true;
}

/** Characters (code points; the same as UTF-16 units for Chinese) in UTF-8 text. */
static size_t chars(const char *s) {
    size_t n = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80) n++;
    return n;
}

static const cJSON *get(const cJSON *o, const char *key) { return cJSON_GetObjectItemCaseSensitive(o, key); }

static bool str_in(const cJSON *v, size_t min, size_t max) {
    if (!cJSON_IsString(v) || !v->valuestring) return false;
    const size_t n = chars(v->valuestring);
    return n >= min && n <= max;
}

static bool is_int(const cJSON *v, double min, double max) {
    return cJSON_IsNumber(v) && v->valuedouble >= min && v->valuedouble <= max && floor(v->valuedouble) == v->valuedouble;
}

static bool one_of(const char *s, const char *const *set) {
    for (; *set; set++)
        if (!strcmp(s, *set)) return true;
    return false;
}

static bool enum_str(const cJSON *v, const char *const *set) { return cJSON_IsString(v) && one_of(v->valuestring, set); }

/** z.iso.datetime(): 2026-10-18T16:00:00Z, fractions allowed, no offset. */
static bool datetime_ok(const cJSON *v) {
    if (!cJSON_IsString(v)) return false;
    const char *s = v->valuestring;
    static const char shape[] = "dddd-dd-ddTdd:dd:dd";
    for (int i = 0; shape[i]; i++) {
        if (shape[i] == 'd' ? !(s[i] >= '0' && s[i] <= '9') : s[i] != shape[i]) return false;
    }
    s += sizeof(shape) - 1;
    if (*s == '.') {
        s++;
        if (!(*s >= '0' && *s <= '9')) return false;
        while (*s >= '0' && *s <= '9') s++;
    }
    return s[0] == 'Z' && s[1] == 0;
}

/** z.iso.datetime({offset: true}): 2026-11-14T10:58:03.000+08:00, or Z; fractions allowed. */
static bool datetime_offset_ok(const char *s) {
    if (!s) return false;
    static const char shape[] = "dddd-dd-ddTdd:dd:dd";
    for (int i = 0; shape[i]; i++)
        if (shape[i] == 'd' ? !(s[i] >= '0' && s[i] <= '9') : s[i] != shape[i]) return false;
    s += sizeof(shape) - 1;
    if (*s == '.') {
        s++;
        if (!(*s >= '0' && *s <= '9')) return false;
        while (*s >= '0' && *s <= '9') s++;
    }
    if (s[0] == 'Z') return s[1] == 0;
    if (s[0] != '+' && s[0] != '-') return false;
    static const char off[] = "dd:dd";
    for (int i = 0; off[i]; i++)
        if (off[i] == 'd' ? !(s[1 + i] >= '0' && s[1 + i] <= '9') : s[1 + i] != off[i]) return false;
    return s[6] == 0;
}

/** z.uuid(): 8-4-4-4-12 hex digits. */
static bool uuid_ok(const char *s) {
    if (!s || strlen(s) != 36) return false;
    for (int i = 0; i < 36; i++) {
        const char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

/** hello.ok's binding: ^[A-Za-z0-9_-]{8,64}$. */
static bool binding_ok(const char *s) {
    if (!s) return false;
    const size_t n = strlen(s);
    if (n < 8 || n > 64) return false;
    for (size_t i = 0; i < n; i++)
        if (!(alnum(s[i]) || s[i] == '_' || s[i] == '-')) return false;
    return true;
}

/** A folderGrant (device-api.ts): app, state, folders (the four kinds, each once), rev >= 1. */
static bool folder_grant(const cJSON *g, yk_proto_folder_t *out) {
    static const char *const KINDS[] = {"music", "stories", "pictures", "documents"};
    const cJSON *app = get(g, "app"), *state = get(g, "state"), *folders = get(g, "folders"), *rev = get(g, "rev");
    if (!cJSON_IsObject(g) || !str_in(app, 2, 41) || !cJSON_IsString(state) || !cJSON_IsArray(folders) ||
        !is_int(rev, 1, 4294967295.0))
        return false;
    const char *a = app->valuestring;
    if (a[0] < 'a' || a[0] > 'z') return false;
    for (const char *c = a; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-')) return false;
    yk_proto_folder_t f = {.rev = (uint32_t)rev->valuedouble};
    if (!strcmp(state->valuestring, "granted")) f.state = 1;
    else if (!strcmp(state->valuestring, "denied")) f.state = 2;
    else return false;
    const cJSON *k;
    cJSON_ArrayForEach(k, folders) {
        int bit = -1;
        for (int i = 0; i < 4 && cJSON_IsString(k); i++)
            if (!strcmp(k->valuestring, KINDS[i])) bit = i;
        if (bit < 0 || f.folders & 1u << bit) return false;  // Recordings, or one twice
        f.folders |= (uint8_t)(1u << bit);
    }
    snprintf(f.app, sizeof(f.app), "%s", a);
    *out = f;
    return true;
}

static void *folders_alloc(size_t size) {
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return calloc(1, size);
#endif
}

/** hello.ok's folders: the whole list, or nothing of it (folders_given false). */
static void hello_folders(const cJSON *list, yk_srv_msg_t *m) {
    const int n = cJSON_IsArray(list) ? cJSON_GetArraySize(list) : -1;
    if (n < 0 || n > YK_PROTO_FOLDERS_MAX) return;  // left out, null, or more than the platform sends
    yk_proto_folder_t *at = n ? folders_alloc((size_t)n * sizeof(*at)) : NULL;
    if (n && !at) return;
    int i = 0;
    const cJSON *g;
    cJSON_ArrayForEach(g, list) {
        bool ok = folder_grant(g, &at[i]);
        for (int j = 0; ok && j < i; j++) ok = strcmp(at[j].app, at[i].app) != 0;  // one grant an app
        if (!ok) {
            free(at);
            return;
        }
        i++;
    }
    m->folders_given = true;
    m->folder_count = n;
    m->folders = at;
}

static void copy(char *out, size_t size, const cJSON *v) {
    snprintf(out, size, "%s", cJSON_IsString(v) && v->valuestring ? v->valuestring : "");
}

static char *dup(const cJSON *v) {
    const char *s = cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
    const size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

static const char *const YAY_STATES[] = {"ready", "listening", "thinking", "working", "approval", "done",
                                         "reminder", "help", "resting", "speaking", "ack", NULL};
static const char *const SLOT_STATES[] = {"empty", "valid", "pending", "invalid", "aborted", "new", "unknown", NULL};
static const char *const MODES[] = {"chat", "practice", "translate", NULL};
static const char *const TALK_LANGUAGES[] = {"zh", "en", "ja", "es", NULL};
static const char *const LEVELS[] = {"beginner", "everyday", "fluent", NULL};

bool yk_proto_talk_language_ok(const char *lang) { return lang && one_of(lang, TALK_LANGUAGES); }

bool yk_proto_pair_ok(const char *pair) {
    if (!pair || strlen(pair) != 5 || pair[2] != '-' || !strncmp(pair, pair + 3, 2)) return false;
    const char mine[3] = {pair[0], pair[1], 0};
    return yk_proto_talk_language_ok(mine) && yk_proto_talk_language_ok(pair + 3);
}

/** ^[A-Za-z0-9._:-]{4,80}$ (a record id) or ^[a-z][a-z0-9.]{2,40}$ (a record or action kind). */
static bool record_word(const char *s, bool id) {
    const size_t n = s ? strlen(s) : 0;
    if (id ? (n < 4 || n > 80) : (n < 3 || n > 41 || s[0] < 'a' || s[0] > 'z')) return false;
    for (size_t i = 0; i < n; i++) {
        const char c = s[i];
        const bool ok = id ? alnum(c) || c == '.' || c == '_' || c == ':' || c == '-'
                           : (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.';
        if (!ok) return false;
    }
    return true;
}

// ---------------------------------------------------------------- device to cloud

static char *finish(cJSON *o) {
    char *s = o ? cJSON_PrintUnformatted(o) : NULL;
    cJSON_Delete(o);
    return s;
}

static bool text_ok(const char *s, size_t min, size_t max) {
    if (!s) return false;
    const size_t n = chars(s);
    return n >= min && n <= max;
}

static bool screen_off_ok(int s) { return s == 0 || (s >= 15 && s <= 3600); }
static bool prefs_ok(int volume, int brightness, int screen_off_s) {
    return volume >= 0 && volume <= 10 && brightness >= 0 && brightness <= 10 && screen_off_ok(screen_off_s);
}
static void add_prefs(cJSON *o, int volume, int brightness, int screen_off_s) {
    cJSON_AddNumberToObject(o, "volume", volume);
    cJSON_AddNumberToObject(o, "brightness", brightness);
    cJSON_AddNumberToObject(o, "screenOff", screen_off_s);
}

static bool hex_digits(const char *s, size_t n, bool upper) {
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (upper && s[i] >= 'A' && s[i] <= 'F')))
            return false;
    return true;
}
/** ^[0-9a-f]{64}$ */
static bool sha256_ok(const char *s) { return s && strlen(s) == 64 && hex_digits(s, 64, false); }

bool yk_proto_saver_image_ok(const char *id) {
    if (!id) return false;
    const size_t n = strlen(id);
    if (n == 10) return !strncmp(id, "default-", 8) && id[8] >= '0' && id[8] <= '9' && id[9] >= '0' && id[9] <= '9';
    if (n != 36) return false;
    for (size_t i = 0; i < n; i++) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? id[i] != '-' : !hex_digits(id + i, 1, true)) return false;
    }
    return true;
}

static bool saver_ok(const yk_proto_saver_t *v) {
    return v && v->after_s >= 15 && v->after_s <= 3600 && v->duration_s >= 60 && v->duration_s <= 7200 &&
           yk_proto_saver_image_ok(v->image_id) && sha256_ok(v->image_sha256);
}
static void add_saver(cJSON *o, const yk_proto_saver_t *v) {
    cJSON *s = cJSON_AddObjectToObject(o, "screensaver");
    cJSON_AddBoolToObject(s, "on", v->on);
    cJSON_AddNumberToObject(s, "after", v->after_s);
    cJSON_AddNumberToObject(s, "duration", v->duration_s);
    cJSON *image = cJSON_AddObjectToObject(s, "image");
    cJSON_AddStringToObject(image, "id", v->image_id);
    cJSON_AddStringToObject(image, "sha256", v->image_sha256);
}
bool yk_proto_buddy_id_ok(const char *id) {
    const size_t n = id ? strlen(id) : 0;
    if (n < 2 || n > 31 || id[0] < 'a' || id[0] > 'z') return false;
    for (size_t i = 1; i < n; i++)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '-')) return false;
    return true;
}
static bool buddy_ok(const yk_proto_buddy_t *v) {
    return v && yk_proto_buddy_id_ok(v->id) && (!v->sha256 || sha256_ok(v->sha256));
}
static void add_buddy(cJSON *o, const yk_proto_buddy_t *v) {
    cJSON *b = cJSON_AddObjectToObject(o, "buddy");
    cJSON_AddStringToObject(b, "id", v->id);
    if (v->sha256) cJSON_AddStringToObject(b, "sha256", v->sha256);
}
/** ^[a-z][a-z_.]{2,39}$ */
static bool cap_ok(const char *c) {
    const size_t n = c ? strlen(c) : 0;
    if (n < 3 || n > 40 || c[0] < 'a' || c[0] > 'z') return false;
    for (size_t i = 1; i < n; i++)
        if (!((c[i] >= 'a' && c[i] <= 'z') || c[i] == '_' || c[i] == '.')) return false;
    return true;
}

char *yk_proto_hello(const yk_proto_hello_t *h) {
    if (!h || !text_ok(h->fw, 1, 40) || !text_ok(h->board, 1, 40) || !text_ok(h->locale, 2, 20) ||
        !text_ok(h->tz, 1, 64) || h->slot_count < 0 || h->slot_count > 4 || h->battery_pct < 0 ||
        h->battery_pct > 100 || h->rssi < -127 || h->rssi > 0 ||
        (h->out_rate != 16000 && h->out_rate != 24000 && h->out_rate != 48000) ||
        (h->mode && !one_of(h->mode, MODES)) || (h->pair && !yk_proto_pair_ok(h->pair)) ||
        (!h->practice_language != !h->practice_level) ||
        (h->practice_language && (!yk_proto_talk_language_ok(h->practice_language) || !one_of(h->practice_level, LEVELS))))
        return NULL;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "hello");
    cJSON_AddStringToObject(o, "fw", h->fw);
    cJSON_AddStringToObject(o, "board", h->board);
    cJSON *slots = cJSON_AddArrayToObject(o, "slots");
    for (int i = 0; i < h->slot_count; i++) {
        const yk_proto_slot_t *s = &h->slots[i];
        const bool has_slug = s->slug && s->slug[0];
        if (s->slot < 1 || s->slot > 3 || !s->state || !one_of(s->state, SLOT_STATES) ||
            (has_slug && !yk_proto_slug_ok(s->slug)) || (s->version && strlen(s->version) > 31) || s->last_used_ms < 0) {
            cJSON_Delete(o);
            return NULL;
        }
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "slot", s->slot);
        if (has_slug) cJSON_AddStringToObject(e, "slug", s->slug);
        else cJSON_AddNullToObject(e, "slug");
        if (s->version && s->version[0]) cJSON_AddStringToObject(e, "version", s->version);
        else cJSON_AddNullToObject(e, "version");
        cJSON_AddStringToObject(e, "state", s->state);
        cJSON_AddNumberToObject(e, "lastUsed", (double)s->last_used_ms);
        cJSON_AddItemToArray(slots, e);
    }
    cJSON *battery = cJSON_AddObjectToObject(o, "battery");
    cJSON_AddNumberToObject(battery, "pct", h->battery_pct);
    cJSON_AddBoolToObject(battery, "charging", h->charging);
    cJSON_AddNumberToObject(cJSON_AddObjectToObject(o, "wifi"), "rssi", h->rssi);
    cJSON_AddStringToObject(o, "locale", h->locale);
    cJSON_AddStringToObject(o, "tz", h->tz);
    cJSON *audio = cJSON_AddObjectToObject(o, "audio");
    cJSON_AddNumberToObject(audio, "inRate", 16000);
    cJSON_AddNumberToObject(audio, "outRate", h->out_rate);
    cJSON_AddStringToObject(audio, "codec", "opus");
    if (h->mode) cJSON_AddStringToObject(o, "mode", h->mode);
    if (h->practice_language) {
        cJSON *practice = cJSON_AddObjectToObject(o, "practice");
        cJSON_AddStringToObject(practice, "language", h->practice_language);
        cJSON_AddStringToObject(practice, "level", h->practice_level);
    }
    if (h->pair) cJSON_AddStringToObject(o, "pair", h->pair);
    if (h->has_prefs) {
        if (!prefs_ok(h->volume, h->brightness, h->screen_off_s) || (h->saver && !saver_ok(h->saver)) ||
            (h->buddy && !buddy_ok(h->buddy))) {
            cJSON_Delete(o);
            return NULL;
        }
        cJSON *prefs = cJSON_AddObjectToObject(o, "prefs");
        add_prefs(prefs, h->volume, h->brightness, h->screen_off_s);
        if (h->saver) add_saver(prefs, h->saver);
        if (h->buddy) add_buddy(prefs, h->buddy);
    }
    if (h->onboarding_step) {
        if (h->onboarding_step < 1 || h->onboarding_step > 255) {
            cJSON_Delete(o);
            return NULL;
        }
        cJSON *onb = cJSON_AddObjectToObject(o, "onboarding");
        cJSON_AddNumberToObject(onb, "step", h->onboarding_step);
        cJSON_AddBoolToObject(onb, "finished", h->onboarding_finished);
    }
    if (h->caps) {
        if (h->cap_count < 0 || h->cap_count > 32) {
            cJSON_Delete(o);
            return NULL;
        }
        cJSON *caps = cJSON_AddArrayToObject(o, "caps");
        for (int i = 0; i < h->cap_count; i++) {
            if (!cap_ok(h->caps[i])) {
                cJSON_Delete(o);
                return NULL;
            }
            cJSON_AddItemToArray(caps, cJSON_CreateString(h->caps[i]));
        }
    }
    if (h->region) {
        if (!yk_proto_region_ok(h->region)) {
            cJSON_Delete(o);
            return NULL;
        }
        cJSON_AddStringToObject(o, "region", h->region);
    }
    return finish(o);
}

bool yk_proto_region_ok(const char *region) {
    const size_t n = region ? strlen(region) : 0;
    if (n < 2 || n > 16 || region[0] < 'a' || region[0] > 'z') return false;
    for (size_t i = 1; i < n; i++)
        if (!((region[i] >= 'a' && region[i] <= 'z') || (region[i] >= '0' && region[i] <= '9') || region[i] == '-'))
            return false;
    return true;
}

char *yk_proto_listen_start(const char *turn, int preroll_ms, const char *side) {
    return yk_proto_listen_start_ex(turn, preroll_ms, side, false);
}

char *yk_proto_listen_start_ex(const char *turn, int preroll_ms, const char *side, bool held) {
    return yk_proto_listen_start_room(turn, preroll_ms, side, held, YK_PROTO_ROOM_NONE);
}

char *yk_proto_listen_start_room(const char *turn, int preroll_ms, const char *side, bool held, int room) {
    static const char *const sides[] = {"mine", "theirs", NULL};
    if (!yk_proto_turn_ok(turn) || preroll_ms < 0 || preroll_ms > 1000 || (side && !one_of(side, sides)) ||
        (room != YK_PROTO_ROOM_NONE && (room < -1 || room > YK_PROTO_ROOM_MAX)))
        return NULL;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "listen.start");
    cJSON_AddStringToObject(o, "turn", turn);
    cJSON_AddNumberToObject(o, "preroll", preroll_ms);
    if (side) cJSON_AddStringToObject(o, "side", side);
    if (held) cJSON_AddTrueToObject(o, "held");
    if (room != YK_PROTO_ROOM_NONE) cJSON_AddNumberToObject(cJSON_AddObjectToObject(o, "record"), "room", room);
    return finish(o);
}

static bool mono_ok(const yk_proto_mono_t *m) { return m->boot <= 2147483648u && m->ms <= 1099511627776ull; }

static cJSON *mono_json(const yk_proto_mono_t *m) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "bootId", (double)m->boot);
    cJSON_AddNumberToObject(o, "monoMs", (double)m->ms);
    return o;
}

char *yk_proto_listen_start_note(const char *turn, int preroll_ms, const char *recorded_at, const yk_proto_clock_t *clock) {
    return yk_proto_listen_start_note_live(turn, preroll_ms, recorded_at, clock, NULL);
}

char *yk_proto_listen_start_note_live(const char *turn, int preroll_ms, const char *recorded_at,
                                      const yk_proto_clock_t *clock, const char *live_turn) {
    if (!yk_proto_turn_ok(turn) || preroll_ms < 0 || preroll_ms > 1000 || !clock || !mono_ok(&clock->at) ||
        (live_turn && !yk_proto_turn_ok(live_turn)) ||
        !mono_ok(&clock->now) || (clock->has_sync && (!mono_ok(&clock->sync) || !datetime_offset_ok(clock->sync_at))) ||
        (recorded_at && !datetime_offset_ok(recorded_at)))
        return NULL;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "listen.start");
    cJSON_AddStringToObject(o, "turn", turn);
    cJSON_AddNumberToObject(o, "preroll", preroll_ms);
    cJSON_AddTrueToObject(o, "held");
    if (recorded_at) cJSON_AddStringToObject(o, "recordedAt", recorded_at);
    cJSON *c = mono_json(&clock->at);
    if (clock->has_sync) {
        cJSON *sync = mono_json(&clock->sync);
        cJSON_AddStringToObject(sync, "at", clock->sync_at);
        cJSON_AddItemToObject(c, "lastSync", sync);
    }
    cJSON_AddItemToObject(c, "now", mono_json(&clock->now));
    cJSON_AddItemToObject(o, "clock", c);
    if (live_turn) cJSON_AddStringToObject(o, "liveTurn", live_turn);
    return finish(o);
}

const char *yk_proto_time_offset(int64_t unix_ms, int offset_min, char out[32]) {
    // Civil date from days since 1970 (Howard Hinnant's), so that it needs no time zone of the C library's.
    const int64_t local_ms = unix_ms + (int64_t)offset_min * 60000;
    int64_t days = local_ms >= 0 ? local_ms / 86400000 : -((-local_ms + 86399999) / 86400000);
    const int64_t ms_day = local_ms - days * 86400000;
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = (unsigned)(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    const int64_t y = (int64_t)yoe + era * 400 + (m <= 2);
    const int off = (offset_min < 0 ? -offset_min : offset_min) % (24 * 60);
    char buf[80];  // as wide as the compiler can think it (a year of nine digits); 29 characters in our years
    snprintf(buf, sizeof(buf), "%04d-%02u-%02uT%02d:%02d:%02d.%03d%c%02d:%02d", (int)y, m, d, (int)(ms_day / 3600000),
             (int)(ms_day / 60000 % 60), (int)(ms_day / 1000 % 60), (int)(ms_day % 1000), offset_min < 0 ? '-' : '+',
             off / 60, off % 60);
    snprintf(out, 32, "%.31s", buf);
    return out;
}

/** Days since 1970-01-01 of a civil date (Howard Hinnant's, as yk_proto_time_offset the other way). */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static int digits(const char *s, int n) {
    int v = 0;
    for (int i = 0; i < n; i++) v = v * 10 + (s[i] - '0');
    return v;
}

bool yk_proto_time_ms(const char *iso, int64_t *unix_ms) {
    if (!datetime_offset_ok(iso)) return false;
    const unsigned month = (unsigned)digits(iso + 5, 2), day = (unsigned)digits(iso + 8, 2);
    const int hour = digits(iso + 11, 2), minute = digits(iso + 14, 2), second = digits(iso + 17, 2);
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) return false;
    const char *s = iso + 19;
    int ms = 0;
    if (*s == '.') {
        int n = 0;
        for (s++; *s >= '0' && *s <= '9'; s++, n++)
            if (n < 3) ms = ms * 10 + (*s - '0');
        for (; n < 3; n++) ms *= 10;
    }
    int offset_min = 0;
    if (*s == '+' || *s == '-') {
        const int h = digits(s + 1, 2), m = digits(s + 4, 2);
        if (h > 23 || m > 59) return false;
        offset_min = (h * 60 + m) * (*s == '-' ? -1 : 1);
    }
    const int64_t days = days_from_civil(digits(iso, 4), month, day);
    *unix_ms = ((days * 24 + hour) * 60 + minute - offset_min) * 60000 + (int64_t)second * 1000 + ms;
    return true;
}

char *yk_proto_proactive_say(const char *id) {
    if (!uuid_ok(id)) return NULL;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "proactive.say");
    cJSON_AddStringToObject(o, "id", id);
    return finish(o);
}

char *yk_proto_offline_digest_say(const char *id) {
    if (!uuid_ok(id)) return NULL;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "offline.digest.say");
    cJSON_AddStringToObject(o, "id", id);
    return finish(o);
}

char *yk_proto_turn_message(const char *type, const char *turn) {
    static const char *const types[] = {"listen.stop", "listen.cancel", "playback.stop", NULL};
    if (!type || !one_of(type, types) || !yk_proto_turn_ok(turn)) return NULL;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", type);
    cJSON_AddStringToObject(o, "turn", turn);
    return finish(o);
}

// kind: sent again in a later session, where the id is not known; value: a setting's value after it (< 0: none).
static char *action_done(const char *id, bool ok, const char *error, const char *kind, int value) {
    if (!text_ok(id, 1, 40) || (error && !text_ok(error, 0, 60)) || (kind && !record_word(kind, false)) ||
        value > 3600)
        return NULL;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "action.done");
    cJSON_AddStringToObject(o, "id", id);
    cJSON_AddBoolToObject(o, "ok", ok);
    if (error) cJSON_AddStringToObject(o, "error", error);
    if (kind) cJSON_AddStringToObject(o, "kind", kind);
    if (value >= 0) cJSON_AddNumberToObject(o, "value", value);
    return finish(o);
}

char *yk_proto_action_done(const char *id, bool ok, const char *error) {
    return action_done(id, ok, error, NULL, -1);
}

char *yk_proto_action_done_kind(const char *id, bool ok, const char *error, const char *kind) {
    return action_done(id, ok, error, kind, -1);
}

char *yk_proto_action_done_value(const char *id, bool ok, const char *error, int value) {
    return action_done(id, ok, error, NULL, value);
}

char *yk_proto_action_done_saver(const char *id, bool ok, const char *error, int value, const yk_proto_saver_t *saver) {
    if (!saver) return action_done(id, ok, error, NULL, value);
    if (!saver_ok(saver)) return NULL;
    char *done = action_done(id, ok, error, NULL, value);
    cJSON *o = done ? cJSON_Parse(done) : NULL;
    cJSON_free(done);
    if (!o) return NULL;
    add_saver(o, saver);
    return finish(o);
}

char *yk_proto_action_done_buddy(const char *id, bool ok, const char *error, const yk_proto_buddy_t *buddy) {
    if (!buddy) return action_done(id, ok, error, NULL, -1);
    if (!buddy_ok(buddy)) return NULL;
    char *done = action_done(id, ok, error, NULL, -1);
    cJSON *o = done ? cJSON_Parse(done) : NULL;
    cJSON_free(done);
    if (!o) return NULL;
    add_buddy(o, buddy);
    return finish(o);
}

char *yk_proto_find_stopped(const char *id, const char *by) {
    static const char *const BY[] = {"tap", "key", "app", "timeout", "other", NULL};
    if (!text_ok(id, 1, 40) || !by || !one_of(by, BY)) return NULL;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "find.stopped");
    cJSON_AddStringToObject(o, "id", id);
    cJSON_AddStringToObject(o, "by", by);
    return finish(o);
}

char *yk_proto_prefs(int volume, int brightness, int screen_off_s) {
    return yk_proto_prefs_saver(volume, brightness, screen_off_s, NULL);
}

char *yk_proto_prefs_saver(int volume, int brightness, int screen_off_s, const yk_proto_saver_t *saver) {
    return yk_proto_prefs_all(volume, brightness, screen_off_s, saver, NULL);
}

char *yk_proto_prefs_all(int volume, int brightness, int screen_off_s, const yk_proto_saver_t *saver,
                         const yk_proto_buddy_t *buddy) {
    if (!prefs_ok(volume, brightness, screen_off_s) || (saver && !saver_ok(saver)) || (buddy && !buddy_ok(buddy)))
        return NULL;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "prefs");
    add_prefs(o, volume, brightness, screen_off_s);
    if (saver) add_saver(o, saver);
    if (buddy) add_buddy(o, buddy);
    return finish(o);
}

/** The languages an account has (packages/contracts locales). */
static const char *const ACCOUNT_LOCALES[] = {"en", "zh-Hans", "ja", "es-419", NULL};

char *yk_proto_onboarding_step(const yk_proto_onb_report_t *steps, int count, const char *locale) {
    static const char *const results[] = {"done", "skipped", "failed", NULL};
    if (!steps || count < 1 || count > YK_PROTO_ONB_REPORTS_MAX || (locale && !one_of(locale, ACCOUNT_LOCALES))) return NULL;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "onboarding.step");
    cJSON *list = cJSON_AddArrayToObject(o, "steps");
    for (int i = 0; i < count; i++) {
        const yk_proto_onb_report_t *r = &steps[i];
        if (r->step < 1 || r->step > 255 || r->item < 0 || r->item > 255 || !r->result || !one_of(r->result, results) ||
            r->seconds > YK_PROTO_ONB_SECONDS_MAX) {
            cJSON_Delete(o);
            return NULL;
        }
        cJSON *one = cJSON_CreateObject();
        cJSON_AddNumberToObject(one, "step", r->step);
        if (r->item) cJSON_AddNumberToObject(one, "item", r->item);
        cJSON_AddStringToObject(one, "result", r->result);
        cJSON_AddNumberToObject(one, "seconds", r->seconds);
        cJSON_AddItemToArray(list, one);
    }
    if (locale) cJSON_AddStringToObject(o, "locale", locale);
    return finish(o);
}

char *yk_proto_app_result(const char *slug, const char *data_json) {
    if (!yk_proto_slug_ok(slug)) return NULL;
    cJSON *data = data_json ? cJSON_Parse(data_json) : cJSON_CreateObject();
    if (!cJSON_IsObject(data)) {
        cJSON_Delete(data);
        return NULL;
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "app.result");
    cJSON_AddStringToObject(o, "slug", slug);
    cJSON_AddItemToObject(o, "data", data);
    return finish(o);
}

char *yk_proto_status(int battery_pct, bool charging, int rssi, uint32_t heap_min) {
    if (battery_pct < 0 || battery_pct > 100 || rssi < -127 || rssi > 0) return NULL;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "status");
    cJSON *battery = cJSON_AddObjectToObject(o, "battery");
    cJSON_AddNumberToObject(battery, "pct", battery_pct);
    cJSON_AddBoolToObject(battery, "charging", charging);
    cJSON_AddNumberToObject(cJSON_AddObjectToObject(o, "wifi"), "rssi", rssi);
    cJSON_AddNumberToObject(o, "heapMin", heap_min);
    return finish(o);
}

char *yk_proto_auth(const char *token) {
    if (!token || !token[0] || strlen(token) > 4096) return NULL;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "auth");
    cJSON_AddStringToObject(o, "token", token);
    return finish(o);
}

/** {"type":type, key:value}: the one-field messages of the modes. */
static char *one_field(const char *type, const char *key, const char *value) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", type);
    if (key) cJSON_AddStringToObject(o, key, value);
    return finish(o);
}

char *yk_proto_pair_set(const char *pair) { return yk_proto_pair_ok(pair) ? one_field("pair.set", "pair", pair) : NULL; }

char *yk_proto_translate_text(const char *turn, const char *text, const char *from) {
    if (!yk_proto_turn_ok(turn) || !text_ok(text, 1, 500) || !yk_proto_talk_language_ok(from)) return NULL;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "translate.text");
    cJSON_AddStringToObject(o, "turn", turn);
    cJSON_AddStringToObject(o, "text", text);
    cJSON_AddStringToObject(o, "from", from);
    return finish(o);
}

char *yk_proto_practice_open(const char *why) {
    static const char *const whys[] = {"start", "settings", "resume", NULL};
    return why && one_of(why, whys) ? one_field("practice.open", "why", why) : NULL;
}

char *yk_proto_practice_set(const char *language, const char *level) {
    if (!yk_proto_talk_language_ok(language) || !level || !one_of(level, LEVELS)) return NULL;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "practice.set");
    cJSON_AddStringToObject(o, "language", language);
    cJSON_AddStringToObject(o, "level", level);
    return finish(o);
}

char *yk_proto_practice_next(void) { return one_field("practice.next", NULL, NULL); }

char *yk_proto_practice_end(const char *why) {
    static const char *const whys[] = {"double", "facedown", "idle", "home", NULL};
    return why && one_of(why, whys) ? one_field("practice.end", "why", why) : NULL;
}

/** One record as POST /records takes it, from the outbox's JSON; NULL when it breaks the schema. */
static cJSON *carried_record(const char *json) {
    cJSON *in = json ? cJSON_Parse(json) : NULL;
    const cJSON *id = get(in, "id"), *kind = get(in, "kind"), *at = get(in, "occurredAt"),
                *trusted = get(in, "clockTrusted"), *data = get(in, "data");
    cJSON *out = NULL;
    if (cJSON_IsString(id) && record_word(id->valuestring, true) && cJSON_IsString(kind) &&
        record_word(kind->valuestring, false) && datetime_ok(at) && (!trusted || cJSON_IsBool(trusted)) &&
        cJSON_IsObject(data)) {
        out = cJSON_CreateObject();
        cJSON_AddStringToObject(out, "id", id->valuestring);
        cJSON_AddStringToObject(out, "kind", kind->valuestring);
        cJSON_AddStringToObject(out, "occurredAt", at->valuestring);
        if (trusted) cJSON_AddBoolToObject(out, "clockTrusted", cJSON_IsTrue(trusted));
        cJSON_AddItemToObject(out, "data", cJSON_Duplicate(data, true));
    }
    cJSON_Delete(in);
    return out;
}

char *yk_proto_app_records(const char *slug, const char *const *records, int count) {
    if (!yk_proto_slug_ok(slug) || !records || count < 1 || count > YK_PROTO_RECORDS_MAX) return NULL;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "app.records");
    cJSON_AddStringToObject(o, "slug", slug);
    cJSON *list = cJSON_AddArrayToObject(o, "records");
    for (int i = 0; i < count; i++) {
        cJSON *r = carried_record(records[i]);
        if (!r) {
            cJSON_Delete(o);
            return NULL;
        }
        cJSON_AddItemToArray(list, r);
    }
    return finish(o);
}

// ---------------------------------------------------------------- cloud to device

static bool turn_field(const cJSON *o, yk_srv_msg_t *m, bool required) {
    const cJSON *t = get(o, "turn");
    if (!t) return !required;
    if (!cJSON_IsString(t) || !yk_proto_turn_ok(t->valuestring)) return false;
    copy(m->turn, sizeof(m->turn), t);
    return true;
}

static bool credits_field(const cJSON *c, yk_srv_msg_t *m) {
    const cJSON *used = get(c, "used"), *left = get(c, "remaining");
    if (!cJSON_IsObject(c) || !cJSON_IsNumber(used) || used->valuedouble < 0 || !is_int(left, 0, 2147483647))
        return false;
    m->has_credits = true;
    m->credits_used = used->valuedouble;
    m->credits_remaining = (int)left->valuedouble;
    return true;
}

static bool nullable_str(const cJSON *v, size_t max, char *out, size_t size) {
    if (cJSON_IsNull(v)) {
        out[0] = 0;
        return true;
    }
    if (!str_in(v, 0, max)) return false;
    copy(out, size, v);
    return true;
}

/** stt.final, reply.line: a language this firmware does not know (a newer server) is left out, not refused. */
static void lang_field(const cJSON *v, yk_srv_msg_t *m) {
    if (cJSON_IsString(v) && (yk_proto_talk_language_ok(v->valuestring) || !strcmp(v->valuestring, "other")))
        copy(m->lang, sizeof(m->lang), v);
}

/** practice.turn fix {said, better, spans, note}. */
static bool practice_fix(const cJSON *fix, yk_srv_msg_t *m) {
    const cJSON *said = get(fix, "said"), *better = get(fix, "better"), *spans = get(fix, "spans"),
                *note = get(fix, "note");
    if (!cJSON_IsObject(fix) || !str_in(said, 1, 300) || !str_in(better, 1, 300) || !cJSON_IsArray(spans) ||
        cJSON_GetArraySize(spans) > 8 || !str_in(note, 0, 24))
        return false;
    const cJSON *span;
    cJSON_ArrayForEach(span, spans) {
        const cJSON *from = cJSON_GetArrayItem(span, 0), *to = cJSON_GetArrayItem(span, 1);
        if (!cJSON_IsArray(span) || cJSON_GetArraySize(span) != 2 || !is_int(from, 0, 65535) || !is_int(to, 1, 65535))
            return false;
        m->spans[m->span_count][0] = (uint16_t)from->valuedouble;
        m->spans[m->span_count][1] = (uint16_t)to->valuedouble;
        m->span_count++;
    }
    m->said = dup(said);
    m->better = dup(better);
    m->note = dup(note);
    return m->said && m->better && m->note;
}

/** practice.turn newWords [{word, meaning}], two at most. */
static bool new_words(const cJSON *words, yk_srv_msg_t *m) {
    if (!cJSON_IsArray(words) || cJSON_GetArraySize(words) > 2) return false;
    const cJSON *w;
    cJSON_ArrayForEach(w, words) {
        const cJSON *word = get(w, "word"), *meaning = get(w, "meaning");
        if (!cJSON_IsObject(w) || !str_in(word, 1, 40) || !str_in(meaning, 1, 40)) return false;
        const int i = m->word_count++;
        m->words[i] = dup(word);
        m->meanings[i] = dup(meaning);
        if (!m->words[i] || !m->meanings[i]) return false;
    }
    return true;
}

/** A file to download: url (https), sha256 and size (1..max) into m (url owned). */
static bool file_to_fetch(const cJSON *o, yk_srv_msg_t *m, int max) {
    const cJSON *url = get(o, "url"), *sha = get(o, "sha256"), *size = get(o, "size");
    if (!cJSON_IsString(url) || strncmp(url->valuestring, "https://", 8) || strlen(url->valuestring) < 9 ||
        !cJSON_IsString(sha) || !sha256_ok(sha->valuestring) || !is_int(size, 1, max))
        return false;
    copy(m->sha256, sizeof(m->sha256), sha);
    m->size = (uint32_t)size->valuedouble;
    free(m->url);
    m->url = dup(url);
    return m->url != NULL;
}

static bool saver_image(const cJSON *o, yk_srv_msg_t *m) { return file_to_fetch(o, m, YK_PROTO_SAVER_IMAGE_MAX); }

/** buddy.set: one of the four; every one but Pip with its pack {url, sha256, size}, Pip without. */
static bool parse_buddy_set(const cJSON *o, yk_srv_msg_t *m) {
    static const char *const BUDDIES[] = {"pip", "mote", "dial", "bloom", NULL};
    const cJSON *buddy = get(o, "buddy"), *pack = get(o, "pack");
    if (!cJSON_IsString(buddy) || !one_of(buddy->valuestring, BUDDIES)) return false;
    const bool pip = !strcmp(buddy->valuestring, "pip");
    if (pip != !pack) return false;
    if (pack && (!cJSON_IsObject(pack) || !file_to_fetch(pack, m, YK_PROTO_BUDDY_PACK_MAX))) return false;
    copy(m->buddy, sizeof(m->buddy), buddy);
    m->kind = YK_ACT_BUDDY_SET;
    return true;
}

/** screensaver.set: at least one of on, after (30, 60, 300), duration (300, 600, 1800) and image {id, url, sha256,
 *  size}. */
static bool parse_saver_set(const cJSON *o, yk_srv_msg_t *m) {
    const cJSON *on = get(o, "on"), *after = get(o, "after"), *duration = get(o, "duration"), *image = get(o, "image");
    m->saver_on = -1;
    if (on) {
        if (!cJSON_IsBool(on)) return false;
        m->saver_on = cJSON_IsTrue(on) ? 1 : 0;
    }
    if (after) {
        const int s = is_int(after, 1, 3600) ? (int)after->valuedouble : 0;
        if (s != 30 && s != 60 && s != 300) return false;
        m->saver_after = s;
    }
    if (duration) {
        const int s = is_int(duration, 1, 7200) ? (int)duration->valuedouble : 0;
        if (s != 300 && s != 600 && s != 1800) return false;
        m->saver_duration = s;
    }
    if (image) {
        const cJSON *id = get(image, "id");
        if (!cJSON_IsObject(image) || !cJSON_IsString(id) || !yk_proto_saver_image_ok(id->valuestring) ||
            !saver_image(image, m))
            return false;
        copy(m->image_id, sizeof(m->image_id), id);
    }
    if (!on && !after && !duration && !image) return false;  // something to change
    m->kind = YK_ACT_SAVER_SET;
    return true;
}

/** onboarding.set: `skip` always, 16 steps at most, each 1..255; `locale` an account's; and something to change. */
static bool parse_onboarding_set(const cJSON *o, yk_srv_msg_t *m) {
    const cJSON *skip = get(o, "skip"), *locale = get(o, "locale"), *step;
    if (!cJSON_IsArray(skip) || cJSON_GetArraySize(skip) > 16) return false;
    if (locale && (!cJSON_IsString(locale) || !one_of(locale->valuestring, ACCOUNT_LOCALES))) return false;
    if (!cJSON_GetArraySize(skip) && !locale) return false;
    m->onb_skip = 0;
    cJSON_ArrayForEach(step, skip) {
        if (!is_int(step, 1, 255)) return false;
        if (step->valuedouble < 16) m->onb_skip |= (uint16_t)(1u << (int)step->valuedouble);
    }
    if (locale) copy(m->onb_locale, sizeof(m->onb_locale), locale);
    m->kind = YK_ACT_ONBOARDING_SET;
    return true;
}

/** A colourway's name (packages/contracts buddy.ts COLORWAYS, only added to): 1 to 11 lower-case letters. Which of
 *  them this firmware knows is the firmware's to say. */
static bool colorway_ok(const char *s) {
    const size_t n = strlen(s);
    if (n < 1 || n > 11) return false;
    for (size_t i = 0; i < n; i++)
        if (s[i] < 'a' || s[i] > 'z') return false;
    return true;
}

static bool parse_action(const cJSON *o, yk_srv_msg_t *m) {
    const cJSON *id = get(o, "id"), *kind = get(o, "kind");
    if (!str_in(id, 1, 40) || !cJSON_IsString(kind)) return false;
    copy(m->action_id, sizeof(m->action_id), id);
    const char *k = kind->valuestring;
    if (!strcmp(k, "app.open")) {
        const cJSON *slug = get(o, "slug"), *params = get(o, "params");
        if (!cJSON_IsString(slug) || !yk_proto_slug_ok(slug->valuestring) || !cJSON_IsObject(params)) return false;
        m->kind = YK_ACT_APP_OPEN;
        copy(m->slug, sizeof(m->slug), slug);
        m->params = cJSON_PrintUnformatted(params);
        return m->params != NULL;
    }
    if (!strcmp(k, "app.install")) {
        const cJSON *slug = get(o, "slug"), *name = get(o, "name"), *version = get(o, "version"),
                    *size = get(o, "size"), *sha = get(o, "sha256"), *url = get(o, "url"), *sig = get(o, "signature"),
                    *replace = get(o, "replace");
        if (!cJSON_IsString(slug) || !yk_proto_slug_ok(slug->valuestring) || !str_in(name, 1, 40) ||
            !str_in(version, 1, 31) || !is_int(size, 1, 4294967295.0) || !cJSON_IsString(sha) ||
            strlen(sha->valuestring) != 64 || !cJSON_IsString(url) || strncmp(url->valuestring, "https://", 8) ||
            strlen(url->valuestring) < 9 || !str_in(sig, 1, 200) ||
            (replace && (!cJSON_IsString(replace) || !yk_proto_slug_ok(replace->valuestring))))
            return false;
        for (const char *c = sha->valuestring; *c; c++)
            if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f'))) return false;
        m->kind = YK_ACT_APP_INSTALL;
        copy(m->slug, sizeof(m->slug), slug);
        copy(m->name, sizeof(m->name), name);
        copy(m->version, sizeof(m->version), version);
        m->size = (uint32_t)size->valuedouble;
        copy(m->sha256, sizeof(m->sha256), sha);
        m->url = dup(url);
        m->signature = dup(sig);
        if (replace) copy(m->replace, sizeof(m->replace), replace);
        return m->url && m->signature;
    }
    const cJSON *level = get(o, "level"), *step = get(o, "step"), *mute = get(o, "mute");
    m->level = -1;
    m->step = 0;
    m->mute = -1;
    const bool setting = !strcmp(k, "volume.set") || !strcmp(k, "brightness.set");
    if (setting) {
        // Exactly one of level, step and (volume) mute.
        if (level && !is_int(level, 0, 10) && !(cJSON_IsString(level) && !strcmp(level->valuestring, "auto"))) return false;
        if (step && (!is_int(step, -3, 3) || step->valuedouble == 0)) return false;
        if (is_int(level, 0, 10)) m->level = (int)level->valuedouble;
        if (step) m->step = (int)step->valuedouble;
    }
    if (!strcmp(k, "volume.set")) {
        if (mute && !cJSON_IsBool(mute)) return false;
        if (mute) m->mute = cJSON_IsTrue(mute) ? 1 : 0;
        if (!!level + !!step + !!mute != 1 || cJSON_IsString(level)) return false;  // "auto" is brightness's
        m->kind = YK_ACT_VOLUME;
        return true;
    }
    if (!strcmp(k, "brightness.set")) {
        // "auto" (older servers) reads as no level and no step: answered UNSUPPORTED, there is no light sensor.
        if (!!level + !!step != 1) return false;
        m->kind = YK_ACT_BRIGHTNESS;
        return true;
    }
    if (!strcmp(k, "app.home")) {
        m->kind = YK_ACT_APP_HOME;
        return true;
    }
    if (!strcmp(k, "screen_off.set")) {
        const cJSON *seconds = get(o, "seconds");
        if (!is_int(seconds, 0, 3600) || !screen_off_ok((int)seconds->valuedouble)) return false;
        m->kind = YK_ACT_SCREEN_OFF;
        m->seconds = (int)seconds->valuedouble;
        return true;
    }
    if (!strcmp(k, "screensaver.set")) return parse_saver_set(o, m);
    if (!strcmp(k, "buddy.set")) return parse_buddy_set(o, m);
    if (!strcmp(k, "screensaver.preview")) {
        const cJSON *seconds = get(o, "seconds");
        if (!saver_image(o, m) || !is_int(seconds, 3, 30)) return false;
        m->kind = YK_ACT_SAVER_PREVIEW;
        m->seconds = (int)seconds->valuedouble;
        return true;
    }
    if (!strcmp(k, "onboarding.set")) return parse_onboarding_set(o, m);
    if (!strcmp(k, "find.ring")) {
        const cJSON *seconds = get(o, "seconds");
        if (!is_int(seconds, 5, 300)) return false;
        m->kind = YK_ACT_FIND_RING;
        m->seconds = (int)seconds->valuedouble;
        return true;
    }
    if (!strcmp(k, "find.stop")) {  // `find`: the find.ring it stops, always named
        const cJSON *find = get(o, "find");
        if (!str_in(find, 1, 40)) return false;
        m->kind = YK_ACT_FIND_STOP;
        copy(m->find, sizeof(m->find), find);
        return true;
    }
    if (!strcmp(k, "locale.set")) {  // as brightness.set, one field: the account's language, one of the four
        const cJSON *locale = get(o, "locale");
        if (!cJSON_IsString(locale) || !one_of(locale->valuestring, ACCOUNT_LOCALES)) return false;
        copy(m->locale, sizeof(m->locale), locale);
        m->kind = YK_ACT_LOCALE_SET;
        return true;
    }
    if (!strcmp(k, "colorway.set")) {
        const cJSON *colorway = get(o, "colorway");
        if (!cJSON_IsString(colorway) || !colorway_ok(colorway->valuestring)) return false;
        copy(m->colorway, sizeof(m->colorway), colorway);
        m->kind = YK_ACT_COLORWAY_SET;
        return true;
    }
    if (!strcmp(k, "folder.set")) {  // one app's grant after the App's switch (0.1.19, 方案 A)
        if (!folder_grant(get(o, "grant"), &m->folder)) return false;
        m->kind = YK_ACT_FOLDER_SET;
        return true;
    }
    if (!strcmp(k, "record.start")) {  // 「帮我录」 (Yay 0.2 with the record cap): no fields
        m->kind = YK_ACT_RECORD_START;
        return true;
    }
    m->kind = YK_ACT_UNKNOWN;  // a newer kind: the id is enough to say no
    return true;
}

/** A language the device keeps (as yaykit_cloud's yk_locale_ok): 2 to 7 characters, a letter first, then letters,
 *  digits and '-'. */
static bool locale_ok(const char *s) {
    const size_t n = strlen(s);
    if (n < 2 || n > 7) return false;
    for (size_t i = 0; i < n; i++) {
        const bool letter = (s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z');
        if (!letter && (i == 0 || !((s[i] >= '0' && s[i] <= '9') || s[i] == '-'))) return false;
    }
    return true;
}

/** today.ts todayItemId: ^[snp]:[A-Za-z0-9._-]{1,62}$ */
static bool today_id_ok(const char *s) {
    const size_t n = s ? strlen(s) : 0;
    if (n < 3 || n > 64 || (s[0] != 's' && s[0] != 'n' && s[0] != 'p') || s[1] != ':') return false;
    for (size_t i = 2; i < n; i++)
        if (!(alnum(s[i]) || s[i] == '.' || s[i] == '_' || s[i] == '-')) return false;
    return true;
}

/** A time or null (today: no time): at_ms 0 for null. */
static bool nullable_time(const cJSON *v, int64_t *ms) {
    if (cJSON_IsNull(v)) {
        *ms = 0;
        return true;
    }
    return cJSON_IsString(v) && yk_proto_time_ms(v->valuestring, ms);
}

/** todayScreenWeather: sky, night, tempC, text, place, at. A sky this firmware does not know is read (YK_SKY_OTHER) and
 *  the weather taken as none to show; a field it does not know is passed over. */
static bool today_weather(const cJSON *w, yk_proto_today_t *t) {
    static const char *const SKIES[] = {"clear", "cloudy", "rain", "snow", NULL};
    const cJSON *sky = get(w, "sky"), *night = get(w, "night"), *temp = get(w, "tempC"), *text = get(w, "text"),
                *place = get(w, "place"), *at = get(w, "at");
    if (!cJSON_IsObject(w) || !cJSON_IsString(sky) || !cJSON_IsBool(night) || !is_int(temp, -60, 60) ||
        !str_in(text, 1, YK_PROTO_TODAY_TEXT_CHARS) || !str_in(place, 1, YK_PROTO_TODAY_PLACE_CHARS) ||
        !cJSON_IsString(at) || !yk_proto_time_ms(at->valuestring, &t->weather_at_ms))
        return false;
    // A newer platform's sky (an icon this firmware has none for): lower-case letters, as the contract's are
    const char *k = sky->valuestring;
    if (!colorway_ok(k)) return false;
    t->sky = YK_SKY_OTHER;
    for (int i = 0; SKIES[i]; i++)
        if (!strcmp(k, SKIES[i])) t->sky = (uint8_t)i;
    t->weather = t->sky == YK_SKY_OTHER ? 0 : 1;
    t->night = cJSON_IsTrue(night);
    t->temp_c = (int8_t)temp->valuedouble;
    copy(t->text, sizeof(t->text), text);
    copy(t->place, sizeof(t->place), place);
    return true;
}

/** today (today.ts todayMessage): weather left out, null or given; next (5 at most), left, day, done. */
static bool parse_today(const cJSON *o, yk_srv_msg_t *m) {
    const cJSON *weather = get(o, "weather"), *next = get(o, "next"), *left = get(o, "left"), *day = get(o, "day"),
                *done = get(o, "done");
    if (!cJSON_IsArray(next) || cJSON_GetArraySize(next) > YK_PROTO_TODAY_NEXT_MAX || !is_int(left, 0, 99) ||
        !cJSON_IsString(day) || (done && (!cJSON_IsString(done) || !today_id_ok(done->valuestring))))
        return false;
    static const char shape[] = "dddd-dd-dd";
    const char *d = day->valuestring;
    if (strlen(d) != sizeof(shape) - 1) return false;
    for (int i = 0; shape[i]; i++)
        if (shape[i] == 'd' ? !(d[i] >= '0' && d[i] <= '9') : d[i] != shape[i]) return false;
    yk_proto_today_t *t = folders_alloc(sizeof(*t));  // PSRAM: the websocket task's stack stays as it was
    if (!t) return false;
    m->today = t;
    t->weather = -1;
    if (cJSON_IsNull(weather)) t->weather = 0;
    else if (weather && !today_weather(weather, t)) return false;
    const cJSON *item;
    cJSON_ArrayForEach(item, next) {
        yk_proto_today_item_t *n = &t->next[t->next_count];
        const cJSON *id = get(item, "id"), *at = get(item, "at"), *title = get(item, "title");
        if (!cJSON_IsObject(item) || !cJSON_IsString(id) || !today_id_ok(id->valuestring) || !at ||
            !nullable_time(at, &n->at_ms) || !title ||
            !(cJSON_IsNull(title) || str_in(title, 1, YK_PROTO_TODAY_TITLE_CHARS)))
            return false;
        copy(n->id, sizeof(n->id), id);
        n->has_title = !cJSON_IsNull(title);
        if (n->has_title) copy(n->title, sizeof(n->title), title);
        t->next_count++;
    }
    t->left = (int)left->valuedouble;
    copy(t->day, sizeof(t->day), day);
    if (done) copy(t->done, sizeof(t->done), done);
    m->type = YK_SRV_TODAY;
    return true;
}

static bool parse_into(const cJSON *o, yk_srv_msg_t *m) {
    const cJSON *type = get(o, "type");
    if (!cJSON_IsObject(o) || !cJSON_IsString(type)) return false;
    const char *t = type->valuestring;
    if (!strcmp(t, "hello.ok")) {
        const cJSON *session = get(o, "session"), *time = get(o, "time"), *owner = get(o, "owner"),
                    *credits = get(o, "credits"), *dot = get(o, "dot");
        const cJSON *nick = owner ? get(owner, "nickname") : NULL, *left = credits ? get(credits, "remaining") : NULL,
                    *reset = credits ? get(credits, "resetAt") : NULL;
        if (!str_in(session, 1, 64) || !is_int(time, 1, 9007199254740991.0) || !cJSON_IsObject(owner) ||
            !str_in(nick, 0, 40) || !cJSON_IsObject(credits) || !is_int(left, 0, 2147483647) || !datetime_ok(reset) ||
            !cJSON_IsBool(dot))
            return false;
        m->type = YK_SRV_HELLO_OK;
        copy(m->session, sizeof(m->session), session);
        m->time_ms = (int64_t)time->valuedouble;
        copy(m->nickname, sizeof(m->nickname), nick);
        // The account's language (#545): optional, and a value the device would not keep is left out, never the
        // hello.ok with it.
        const cJSON *loc = get(owner, "locale");
        if (cJSON_IsString(loc) && locale_ok(loc->valuestring)) copy(m->owner_locale, sizeof(m->owner_locale), loc);
        // The account's colour (0.1.19): optional too, and one that is not a name is left out.
        const cJSON *colorway = get(owner, "colorway");
        if (cJSON_IsString(colorway) && colorway_ok(colorway->valuestring))
            copy(m->owner_colorway, sizeof(m->owner_colorway), colorway);
        hello_folders(get(o, "folders"), m);  // the shared folders' grants (0.1.19): optional, never the hello.ok with it
        // offlineNotes and binding (0.1.20): optional; one that is not one is left out (kept / not checked), never the
        // hello.ok with it.
        const cJSON *notes = get(o, "offlineNotes"), *binding = get(o, "binding");
        m->offline_notes = cJSON_IsBool(notes) ? (int8_t)cJSON_IsTrue(notes) : -1;
        if (cJSON_IsString(binding) && binding_ok(binding->valuestring)) copy(m->binding, sizeof(m->binding), binding);
        m->has_credits = true;  // credits is required here: an app reading has_credits gets it (#580)
        m->credits_remaining = (int)left->valuedouble;
        copy(m->reset_at, sizeof(m->reset_at), reset);
        m->dot = cJSON_IsTrue(dot);
        // powerLog (0.1.20 4.10): optional; anything but true is off, never the hello.ok with it.
        m->power_log = cJSON_IsTrue(get(o, "powerLog"));
        // nextReminderAt: optional; a value that is not a time leaves it not known, never the hello.ok with it.
        const cJSON *next = get(o, "nextReminderAt");
        m->next_reminder = -1;
        if (cJSON_IsNull(next)) {
            m->next_reminder = 0;
        } else if (is_int(next, 1, 9007199254740991.0)) {
            m->next_reminder = 1;
            m->next_reminder_ms = (int64_t)next->valuedouble;
        }
        m->week_days = m->week_turns = -1;
        const cJSON *practice = get(o, "practice");
        if (practice) {
            const cJSON *points = get(practice, "turnPoints"), *week = get(practice, "week");
            const cJSON *days = week ? get(week, "days") : NULL, *turns = week ? get(week, "turns") : NULL;
            if (!cJSON_IsObject(practice) || !cJSON_IsNumber(points) || points->valuedouble < 0 ||
                (week && (!cJSON_IsObject(week) || !is_int(days, 0, 7) || !is_int(turns, 0, 2147483647))))
                return false;
            m->has_practice = true;
            m->turn_points = points->valuedouble;
            if (week) m->week_days = (int)days->valuedouble, m->week_turns = (int)turns->valuedouble;
        }
        return true;
    }
    if (!strcmp(t, "stt.partial") || !strcmp(t, "stt.final")) {
        const cJSON *text = get(o, "text");
        if (!turn_field(o, m, true) || !str_in(text, 0, 2000)) return false;
        m->type = t[4] == 'p' ? YK_SRV_STT_PARTIAL : YK_SRV_STT_FINAL;
        lang_field(get(o, "lang"), m);
        m->text = dup(text);
        return m->text != NULL;
    }
    if (!strcmp(t, "reply.line")) {
        const cJSON *line = get(o, "line"), *text = get(o, "text"), *kind = get(o, "kind");
        if (!turn_field(o, m, true) || !is_int(line, 0, 255) || !str_in(text, 1, 120) || (kind && !cJSON_IsString(kind)))
            return false;
        m->type = YK_SRV_REPLY_LINE;
        m->line = (int)line->valuedouble;
        // A kind this firmware does not know is a subtitle, as for the devices before kinds.
        m->fix_line = kind && !strcmp(kind->valuestring, "fix");
        lang_field(get(o, "lang"), m);
        m->text = dup(text);
        return m->text != NULL;
    }
    if (!strcmp(t, "offline.digest")) {
        const cJSON *id = get(o, "id"), *counts = get(o, "counts"), *line = get(o, "line");
        const cJSON *total = get(counts, "total"), *notes = get(counts, "notes"), *rem = get(counts, "reminders"),
                    *untimed = get(counts, "untimed");
        if (!cJSON_IsString(id) || !uuid_ok(id->valuestring) || !cJSON_IsObject(counts) || !is_int(total, 0, 1000) ||
            !is_int(notes, 0, 1000) || !is_int(rem, 0, 1000) || !is_int(untimed, 0, 1000) || !str_in(line, 1, 200))
            return false;
        m->type = YK_SRV_OFFLINE_DIGEST;
        copy(m->digest_id, sizeof(m->digest_id), id);
        m->digest_total = (int)total->valuedouble, m->digest_notes = (int)notes->valuedouble;
        m->digest_reminders = (int)rem->valuedouble, m->digest_untimed = (int)untimed->valuedouble;
        m->text = dup(line);
        return m->text != NULL;
    }
    if (!strcmp(t, "app.records.ok")) {
        const cJSON *slug = get(o, "slug"), *ids = get(o, "ids");
        if (!cJSON_IsString(slug) || !yk_proto_slug_ok(slug->valuestring) || !cJSON_IsArray(ids) ||
            cJSON_GetArraySize(ids) > YK_PROTO_RECORDS_MAX)
            return false;
        m->type = YK_SRV_APP_RECORDS_OK;
        copy(m->slug, sizeof(m->slug), slug);
        m->ids = calloc((size_t)cJSON_GetArraySize(ids) + 1, sizeof(char *));
        if (!m->ids) return false;
        const cJSON *id;
        cJSON_ArrayForEach(id, ids) {
            if (!cJSON_IsString(id) || !record_word(id->valuestring, true)) return false;
            if (!(m->ids[m->id_count++] = dup(id))) return false;
        }
        return true;
    }
    if (!strcmp(t, "practice.turn")) {
        static const char *const verdicts[] = {"fix", "good", "none", "blocked", NULL};
        const cJSON *verdict = get(o, "verdict"), *fix = get(o, "fix"), *hint = get(o, "hint"),
                    *words = get(o, "newWords");
        if (!turn_field(o, m, true) || !enum_str(verdict, verdicts) || (fix && !practice_fix(fix, m)) ||
            (hint && !str_in(hint, 0, 120)) || (words && !new_words(words, m)))
            return false;
        m->type = YK_SRV_PRACTICE_TURN;
        copy(m->verdict, sizeof(m->verdict), verdict);
        m->text = dup(hint);
        return m->text != NULL;
    }
    if (!strcmp(t, "reply.end")) {
        static const char *const muted[] = {"credits", "speech", NULL};
        const cJSON *mute = get(o, "muted");
        if (!turn_field(o, m, true) || !credits_field(get(o, "credits"), m) || (mute && !cJSON_IsString(mute)))
            return false;
        m->type = YK_SRV_REPLY_END;
        if (mute) snprintf(m->muted, sizeof(m->muted), "%s", enum_str(mute, muted) ? mute->valuestring : "speech");
        return true;
    }
    if (!strcmp(t, "ui.state")) {
        const cJSON *state = get(o, "state"), *label = get(o, "label"), *progress = get(o, "progress"),
                    *task = get(o, "taskId");
        if (!enum_str(state, YAY_STATES) || !str_in(label, 0, 40) || !progress ||
            !(cJSON_IsNull(progress) || (cJSON_IsNumber(progress) && progress->valuedouble >= 0 && progress->valuedouble <= 1)) ||
            !task || !nullable_str(task, 64, m->task_id, sizeof(m->task_id)))
            return false;
        m->type = YK_SRV_UI_STATE;
        copy(m->state, sizeof(m->state), state);
        m->text = dup(label);
        m->progress = cJSON_IsNull(progress) ? -1.0f : (float)progress->valuedouble;
        return m->text != NULL;
    }
    if (!strcmp(t, "action")) {
        if (!parse_action(o, m)) return false;
        m->type = YK_SRV_ACTION;
        return true;
    }
    if (!strcmp(t, "inbox")) {
        static const char *const kinds[] = {"dot", "approval", "reminder", "done", "proactive", NULL};
        const cJSON *kind = get(o, "kind"), *text = get(o, "text"), *task = get(o, "taskId"), *id = get(o, "id"),
                    *say = get(o, "say");
        if (!enum_str(kind, kinds) || !str_in(text, 0, 120) || !task ||
            !nullable_str(task, 64, m->task_id, sizeof(m->task_id)))
            return false;
        // proactive (Yay 0.2) must have its id and say. The other kinds have neither today; one that comes with them
        // (a later platform's) is taken as before and they are not read: never the whole message dropped for a
        // field this version does not know.
        const bool proactive = !strcmp(kind->valuestring, "proactive");
        if (proactive && (!cJSON_IsString(id) || !uuid_ok(id->valuestring) || !cJSON_IsBool(say))) return false;
        m->type = YK_SRV_INBOX;
        copy(m->inbox_kind, sizeof(m->inbox_kind), kind);
        if (proactive) {
            copy(m->inbox_id, sizeof(m->inbox_id), id);
            m->inbox_say = cJSON_IsTrue(say);
        }
        m->text = dup(text);
        return m->text != NULL;
    }
    if (!strcmp(t, "today")) return parse_today(o, m);
    if (!strcmp(t, "session.reauth")) {
        m->type = YK_SRV_REAUTH;
        return true;
    }
    if (!strcmp(t, "error")) {
        const cJSON *code = get(o, "code"), *retry = get(o, "retryAfter"), *reset = get(o, "resetAt"),
                    *credits = get(o, "credits");
        // A code this firmware does not know still ends the turn (shown as a general error).
        if (!turn_field(o, m, false) || !str_in(code, 1, 40) || (retry && !is_int(retry, 1, 2147483647)) ||
            (reset && !datetime_ok(reset)) || (credits && !credits_field(credits, m)))
            return false;
        m->type = YK_SRV_ERROR;
        copy(m->code, sizeof(m->code), code);
        if (retry) m->retry_after = (int)retry->valuedouble;
        if (reset) copy(m->reset_at, sizeof(m->reset_at), reset);
        // 两套部署 · 语音单独同意 (#2740): "voice" itself with OPERATION_NOT_ALLOWED, nothing else; any other value, type
        // or code is as none and never makes the message invalid (a firmware before it did not read the field at all).
        const cJSON *consent = get(o, "consent");
        if (!strcmp(code->valuestring, "OPERATION_NOT_ALLOWED") && cJSON_IsString(consent) && consent->valuestring &&
            !strcmp(consent->valuestring, "voice"))
            m->consent = YK_PROTO_CONSENT_VOICE;
        return true;
    }
    return false;
}

#ifdef ESP_PLATFORM
static void *psram_alloc(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
#endif

yk_srv_type_t yk_proto_parse(const char *json, size_t len, yk_srv_msg_t *out) {
    memset(out, 0, sizeof(*out));
    out->progress = -1.0f;
#ifdef ESP_PLATFORM
    // A long message's tree (a hello.ok with 64 grants: ~9 KB, hundreds of blocks) in PSRAM, not in internal RAM, where
    // blocks this small go otherwise (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL), at the moment the session's TLS is new. The
    // hooks are cJSON's one global, as Yay's store_parse sets them: free stays free(), which takes either heap's.
    const bool big = len > YK_PROTO_PSRAM_FROM;
    if (big) cJSON_InitHooks(&(cJSON_Hooks){.malloc_fn = psram_alloc});
#endif
    cJSON *o = json ? cJSON_ParseWithLength(json, len) : NULL;
    const bool ok = o && parse_into(o, out);
    cJSON_Delete(o);
#ifdef ESP_PLATFORM
    if (big) cJSON_InitHooks(NULL);
#endif
    if (!ok) {
        yk_proto_free(out);
        return YK_SRV_INVALID;
    }
    if (!out->text) {
        out->text = malloc(1);
        if (!out->text) {
            yk_proto_free(out);
            return YK_SRV_INVALID;
        }
        out->text[0] = 0;
    }
    return out->type;
}

void yk_proto_free(yk_srv_msg_t *m) {
    if (!m) return;
    free(m->folders);
    m->folders = NULL;
    free(m->today);
    free(m->text);
    cJSON_free(m->params);
    free(m->url);
    free(m->signature);
    free(m->said);
    free(m->better);
    free(m->note);
    for (int i = 0; i < 2; i++) free(m->words[i]), free(m->meanings[i]);
    for (int i = 0; m->ids && i < m->id_count; i++) free(m->ids[i]);
    free(m->ids);
    memset(m, 0, sizeof(*m));
    m->progress = -1.0f;
}

/** Takes *from into *to when *to has nothing (both owned strings). */
static void take(char **to, char **from) {
    if (*to && (*to)[0]) return;
    free(*to);
    *to = *from;
    *from = NULL;
}

bool yk_proto_practice_merge(yk_srv_msg_t *into, yk_srv_msg_t *more) {
    const bool same = into->type == YK_SRV_PRACTICE_TURN && more->type == YK_SRV_PRACTICE_TURN &&
                      !strcmp(into->turn, more->turn);
    if (same) {
        if (!into->verdict[0]) memcpy(into->verdict, more->verdict, sizeof(into->verdict));
        if (!into->better && more->better) {  // the correction comes whole: said, better, spans and note together
            take(&into->said, &more->said);
            take(&into->better, &more->better);
            take(&into->note, &more->note);
            memcpy(into->spans, more->spans, sizeof(into->spans));
            into->span_count = more->span_count;
        }
        if (!into->word_count) {
            for (int i = 0; i < more->word_count; i++) {
                into->words[i] = more->words[i], into->meanings[i] = more->meanings[i];
                more->words[i] = more->meanings[i] = NULL;
            }
            into->word_count = more->word_count;
        }
        take(&into->text, &more->text);  // the hint
    }
    yk_proto_free(more);
    return same;
}

const char *yk_proto_type_name(yk_srv_type_t type) {
    static const char *const names[] = {"invalid", "hello.ok", "stt.partial", "stt.final", "reply.line", "reply.end",
                                        "ui.state", "action", "inbox", "session.reauth", "error", "practice.turn",
                                        "app.records.ok", "offline.digest", "today"};
    return (unsigned)type < sizeof(names) / sizeof(names[0]) ? names[type] : "?";
}

// ---------------------------------------------------------------- reconnecting

int yk_proto_backoff_ms(int attempt, uint32_t rnd) {
    static const int steps_s[] = {1, 2, 4, 8, 15, 30};
    const int last = (int)(sizeof(steps_s) / sizeof(steps_s[0])) - 1;
    const int base = steps_s[attempt < 0 ? 0 : attempt > last ? last : attempt] * 1000;
    return base * 3 / 4 + (int)(rnd % (uint32_t)(base / 2 + 1));
}

// ---------------------------------------------------------------- audio frames

size_t yk_proto_frame(uint8_t *out, int stream, int line, int seq, const uint8_t *packet, size_t bytes) {
    out[0] = YK_PROTO_FRAME_VERSION;
    out[1] = (uint8_t)stream;
    out[2] = (uint8_t)(line & 0xff);
    out[3] = (uint8_t)(seq & 0xff);
    if (bytes) memcpy(out + YK_PROTO_HEADER, packet, bytes);
    return YK_PROTO_HEADER + bytes;
}

bool yk_proto_unframe(const uint8_t *f, size_t len, int *stream, int *line, int *seq, const uint8_t **packet,
                      size_t *bytes) {
    if (len <= YK_PROTO_HEADER || f[0] != YK_PROTO_FRAME_VERSION) return false;
    if (f[1] != YK_PROTO_STREAM_MIC && f[1] != YK_PROTO_STREAM_TTS) return false;
    *stream = f[1];
    *line = f[2];
    *seq = f[3];
    *packet = f + YK_PROTO_HEADER;
    *bytes = len - YK_PROTO_HEADER;
    return true;
}
