// Host tests for yk_proto: every example in packages/contracts/fixtures/device.json, the platform's own.
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "yk_proto.h"

static int failures, checks;
#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)
#define CHECK_CASE(cond, name)                                             \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #cond, name); \
        }                                                                  \
    } while (0)

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = malloc((size_t)n + 1);
    if (s && fread(s, 1, (size_t)n, f) != (size_t)n) {
        free(s);
        s = NULL;
    }
    if (s) s[n] = 0;
    fclose(f);
    return s;
}

static const char *str(const cJSON *o, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static double num(const cJSON *o, const char *key, double fallback) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

/** The message as the schema reads it: hello's audio rates and the slots' lastUsed take their defaults; a slot's
 *  missing slug or version is null. */
static void normalize(cJSON *m) {
    if (strcmp(str(m, "type"), "hello")) return;
    cJSON *audio = cJSON_GetObjectItemCaseSensitive(m, "audio");
    if (!cJSON_GetObjectItemCaseSensitive(audio, "inRate")) cJSON_AddNumberToObject(audio, "inRate", 16000);
    if (!cJSON_GetObjectItemCaseSensitive(audio, "outRate")) cJSON_AddNumberToObject(audio, "outRate", 16000);
    cJSON *slot;
    cJSON_ArrayForEach(slot, cJSON_GetObjectItemCaseSensitive(m, "slots")) {
        if (!cJSON_GetObjectItemCaseSensitive(slot, "lastUsed")) cJSON_AddNumberToObject(slot, "lastUsed", 0);
        if (!cJSON_GetObjectItemCaseSensitive(slot, "slug")) cJSON_AddNullToObject(slot, "slug");
        if (!cJSON_GetObjectItemCaseSensitive(slot, "version")) cJSON_AddNullToObject(slot, "version");
    }
}

/** A screensaver object (prefs.screensaver, action.done's) as the firmware holds it; NULL when there is none. */
static const yk_proto_saver_t *saver_of(const cJSON *v, yk_proto_saver_t *out) {
    if (!v) return NULL;
    const cJSON *image = cJSON_GetObjectItemCaseSensitive(v, "image");
    *out = (yk_proto_saver_t){.on = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(v, "on")),
                              .after_s = (int)num(v, "after", -1),
                              .duration_s = (int)num(v, "duration", -1),
                              .image_id = str(image, "id"),
                              .image_sha256 = str(image, "sha256")};
    return out;
}

/** A buddy object (prefs.buddy, action.done's) as the firmware holds it; NULL when there is none. */
static const yk_proto_buddy_t *buddy_of(const cJSON *v, yk_proto_buddy_t *out) {
    if (!v) return NULL;
    *out = (yk_proto_buddy_t){.id = str(v, "id"), .sha256 = str(v, "sha256")};
    return out;
}

/** Builds the device message the way the firmware does, from the fixture's fields. NULL when the builder refuses. */
static char *build(const cJSON *m) {
    yk_proto_saver_t saver;
    yk_proto_buddy_t buddy;
    const char *type = str(m, "type");
    if (!strcmp(type, "hello")) {
        const cJSON *practice = cJSON_GetObjectItemCaseSensitive(m, "practice");
        yk_proto_hello_t h = {.fw = str(m, "fw"), .board = str(m, "board"), .locale = str(m, "locale"),
                              .tz = str(m, "tz"), .mode = str(m, "mode"), .pair = str(m, "pair"),
                              .practice_language = str(practice, "language"), .practice_level = str(practice, "level"),
                              .region = str(m, "region")};
        const cJSON *battery = cJSON_GetObjectItemCaseSensitive(m, "battery"), *audio = cJSON_GetObjectItemCaseSensitive(m, "audio");
        h.battery_pct = (int)num(battery, "pct", -1);
        h.charging = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(battery, "charging"));
        h.rssi = (int)num(cJSON_GetObjectItemCaseSensitive(m, "wifi"), "rssi", 1);
        h.out_rate = (int)num(audio, "outRate", 16000);
        const char *codec = str(audio, "codec");
        if (!codec || strcmp(codec, "opus")) return NULL;  // the firmware only speaks Opus
        const cJSON *slot;
        cJSON_ArrayForEach(slot, cJSON_GetObjectItemCaseSensitive(m, "slots")) {
            yk_proto_slot_t *s = &h.slots[h.slot_count++];
            s->slot = (int)num(slot, "slot", -1);
            s->slug = str(slot, "slug");
            s->version = str(slot, "version");
            s->state = str(slot, "state");
            s->last_used_ms = (int64_t)num(slot, "lastUsed", 0);
        }
        const cJSON *prefs = cJSON_GetObjectItemCaseSensitive(m, "prefs"), *caps = cJSON_GetObjectItemCaseSensitive(m, "caps");
        if (prefs) {
            h.has_prefs = true;
            h.volume = (int)num(prefs, "volume", -1);
            h.brightness = (int)num(prefs, "brightness", -1);
            h.screen_off_s = (int)num(prefs, "screenOff", -1);
            h.saver = saver_of(cJSON_GetObjectItemCaseSensitive(prefs, "screensaver"), &saver);
            h.buddy = buddy_of(cJSON_GetObjectItemCaseSensitive(prefs, "buddy"), &buddy);
        }
        const cJSON *onb = cJSON_GetObjectItemCaseSensitive(m, "onboarding");
        if (onb) {
            h.onboarding_step = (int)num(onb, "step", -1);
            if (!h.onboarding_step) h.onboarding_step = -1;  // the field's 0 is no onboarding at all: this is a bad one
            h.onboarding_finished = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(onb, "finished"));
            if (!cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(onb, "finished"))) return NULL;  // Yay always says it
        }
        const char *cap_list[33];
        if (caps) {
            const cJSON *cap;
            cJSON_ArrayForEach(cap, caps) if (h.cap_count < 33) cap_list[h.cap_count++] = cJSON_IsString(cap) ? cap->valuestring : NULL;
            h.caps = cap_list;
        }
        return yk_proto_hello(&h);
    }
    const cJSON *clock = cJSON_GetObjectItemCaseSensitive(m, "clock");
    if (!strcmp(type, "listen.start") && clock) {  // a turn said offline (0.1.20): as yay_run.c builds it
        const cJSON *sync = cJSON_GetObjectItemCaseSensitive(clock, "lastSync"), *now = cJSON_GetObjectItemCaseSensitive(clock, "now");
        if (!now || !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(m, "held"))) return NULL;  // the firmware always has both
        yk_proto_clock_t c = {.at = {(uint32_t)num(clock, "bootId", -1), (uint64_t)num(clock, "monoMs", -1)},
                              .now = {(uint32_t)num(now, "bootId", -1), (uint64_t)num(now, "monoMs", -1)}};
        if (num(clock, "bootId", -1) > 4294967295.0 || num(now, "bootId", -1) > 4294967295.0) return NULL;  // past uint32
        if (sync) {
            c.has_sync = true;
            c.sync = (yk_proto_mono_t){(uint32_t)num(sync, "bootId", -1), (uint64_t)num(sync, "monoMs", -1)};
            c.sync_at = str(sync, "at");
        }
        return yk_proto_listen_start_note_live(str(m, "turn"), (int)num(m, "preroll", -1), str(m, "recordedAt"), &c,
                                               str(m, "liveTurn"));
    }
    const cJSON *record = cJSON_GetObjectItemCaseSensitive(m, "record");
    if (!strcmp(type, "listen.start") && record) {  // held on the home (Yay 0.2 with the record cap): its room
        if (cJSON_GetArraySize(record) != 1) return NULL;  // the firmware sends room alone
        return yk_proto_listen_start_room(str(m, "turn"), (int)num(m, "preroll", -1), str(m, "side"),
                                          cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(m, "held")),
                                          (int)num(record, "room", YK_PROTO_ROOM_NONE));
    }
    if (!strcmp(type, "listen.start"))
        return yk_proto_listen_start_ex(str(m, "turn"), (int)num(m, "preroll", -1), str(m, "side"),
                                        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(m, "held")));
    if (!strcmp(type, "offline.digest.say")) return yk_proto_offline_digest_say(str(m, "id"));
    if (!strcmp(type, "proactive.say")) return yk_proto_proactive_say(str(m, "id"));
    if (!strcmp(type, "onboarding.step")) {
        yk_proto_onb_report_t reports[YK_PROTO_ONB_REPORTS_MAX + 2] = {0};
        int n = 0;
        const cJSON *r;
        cJSON_ArrayForEach(r, cJSON_GetObjectItemCaseSensitive(m, "steps")) {
            if (n > YK_PROTO_ONB_REPORTS_MAX) break;
            const cJSON *step = cJSON_GetObjectItemCaseSensitive(r, "step"), *seconds = cJSON_GetObjectItemCaseSensitive(r, "seconds");
            const cJSON *item = cJSON_GetObjectItemCaseSensitive(r, "item");
            if (!cJSON_IsNumber(step) || !cJSON_IsNumber(seconds) || seconds->valuedouble < 0 || (item && !item->valuedouble))
                return NULL;  // as the firmware holds them: a number, never below 0, an item from 1 (0 is none)
            reports[n++] = (yk_proto_onb_report_t){.step = step->valueint, .item = (int)num(r, "item", 0),
                                                   .result = str(r, "result"), .seconds = (uint32_t)seconds->valuedouble};
        }
        return yk_proto_onboarding_step(reports, n, str(m, "locale"));
    }
    if (!strcmp(type, "pair.set")) return yk_proto_pair_set(str(m, "pair"));
    if (!strcmp(type, "translate.text")) return yk_proto_translate_text(str(m, "turn"), str(m, "text"), str(m, "from"));
    if (!strcmp(type, "practice.open")) return yk_proto_practice_open(str(m, "why"));
    if (!strcmp(type, "practice.set")) return yk_proto_practice_set(str(m, "language"), str(m, "level"));
    if (!strcmp(type, "practice.next")) return yk_proto_practice_next();
    if (!strcmp(type, "practice.end")) return yk_proto_practice_end(str(m, "why"));
    if (!strcmp(type, "app.records")) {
        // As Yay passes them on: each record as the outbox keeps it (yk_record_format, with "app").
        const char *records[YK_PROTO_RECORDS_MAX + 1];
        int n = 0;
        const cJSON *r;
        cJSON_ArrayForEach(r, cJSON_GetObjectItemCaseSensitive(m, "records")) {
            if (n > YK_PROTO_RECORDS_MAX) break;
            cJSON *kept = cJSON_Duplicate(r, 1);
            cJSON_AddStringToObject(kept, "app", str(m, "slug") ? str(m, "slug") : "");
            records[n++] = cJSON_PrintUnformatted(kept);
            cJSON_Delete(kept);
        }
        char *out = yk_proto_app_records(str(m, "slug"), records, n);
        for (int i = 0; i < n; i++) cJSON_free((void *)records[i]);
        return out;
    }
    if (!strcmp(type, "action.done")) {
        const bool ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(m, "ok"));
        // kind is only sent again for an install from an earlier session, value only for a setting: never both.
        if (str(m, "kind")) return yk_proto_action_done_kind(str(m, "id"), ok, str(m, "error"), str(m, "kind"));
        // screensaver.set's: the screensaver Yay uses after it
        const cJSON *shows = cJSON_GetObjectItemCaseSensitive(m, "buddy");
        if (shows) return yk_proto_action_done_buddy(str(m, "id"), ok, str(m, "error"), buddy_of(shows, &buddy));
        const cJSON *uses = cJSON_GetObjectItemCaseSensitive(m, "screensaver");
        if (uses)
            return yk_proto_action_done_saver(str(m, "id"), ok, str(m, "error"), (int)num(m, "value", -1),
                                              saver_of(uses, &saver));
        return yk_proto_action_done_value(str(m, "id"), ok, str(m, "error"), (int)num(m, "value", -1));
    }
    if (!strcmp(type, "prefs"))
        return yk_proto_prefs_all((int)num(m, "volume", -1), (int)num(m, "brightness", -1), (int)num(m, "screenOff", -1),
                                  saver_of(cJSON_GetObjectItemCaseSensitive(m, "screensaver"), &saver),
                                  buddy_of(cJSON_GetObjectItemCaseSensitive(m, "buddy"), &buddy));
    if (!strcmp(type, "app.result")) {
        char *data = cJSON_PrintUnformatted(cJSON_GetObjectItemCaseSensitive(m, "data"));
        char *out = yk_proto_app_result(str(m, "slug"), data);
        cJSON_free(data);
        return out;
    }
    if (!strcmp(type, "status")) {
        const cJSON *battery = cJSON_GetObjectItemCaseSensitive(m, "battery");
        return yk_proto_status((int)num(battery, "pct", -1), cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(battery, "charging")),
                               (int)num(cJSON_GetObjectItemCaseSensitive(m, "wifi"), "rssi", 1),
                               (uint32_t)num(m, "heapMin", 0));
    }
    if (!strcmp(type, "auth")) return yk_proto_auth(str(m, "token"));
    if (!strcmp(type, "find.stopped")) return yk_proto_find_stopped(str(m, "id"), str(m, "by"));
    return yk_proto_turn_message(type, str(m, "turn"));  // listen.stop, listen.cancel, playback.stop (or refused)
}

static void test_device(const cJSON *cases, bool valid) {
    const cJSON *c;
    cJSON_ArrayForEach(c, cases) {
        if (strcmp(str(c, "from"), "device")) continue;
        const cJSON *m = cJSON_GetObjectItemCaseSensitive(c, "message");
        const char *name = str(c, "name") ? str(c, "name") : str(m, "type");
        char *built = build(m);
        if (!valid) {
            CHECK_CASE(built == NULL, name);
        } else {
            CHECK_CASE(built != NULL, name);
            cJSON *want = cJSON_Duplicate(m, 1), *got = built ? cJSON_Parse(built) : NULL;
            normalize(want);
            CHECK_CASE(got && cJSON_Compare(got, want, 1), name);
            if (got && !cJSON_Compare(got, want, 1)) {
                char *w = cJSON_PrintUnformatted(want);
                printf("  want %s\n  got  %s\n", w, built);
                cJSON_free(w);
            }
            cJSON_Delete(want);
            cJSON_Delete(got);
        }
        cJSON_free(built);
    }
}

/** The firmware's grant says what the fixture's does (app, state, the kinds as bits, rev). */
static bool folder_is(const yk_proto_folder_t *f, const cJSON *g) {
    static const char *const KINDS[] = {"music", "stories", "pictures", "documents"};
    unsigned bits = 0;
    const cJSON *k;
    cJSON_ArrayForEach(k, cJSON_GetObjectItemCaseSensitive(g, "folders"))
        for (int i = 0; i < 4; i++)
            if (cJSON_IsString(k) && !strcmp(k->valuestring, KINDS[i])) bits |= 1u << i;
    const cJSON *rev = cJSON_GetObjectItemCaseSensitive(g, "rev");
    return f && g && !strcmp(f->app, str(g, "app")) && f->state == (!strcmp(str(g, "state"), "granted") ? 1 : 2) &&
           f->folders == bits && cJSON_IsNumber(rev) && f->rev == (uint32_t)rev->valuedouble;
}

static int64_t time_of(const cJSON *v) {
    int64_t ms = -1;
    if (cJSON_IsNull(v)) return 0;
    if (!cJSON_IsString(v) || !yk_proto_time_ms(v->valuestring, &ms)) return -1;
    return ms;
}

/** `today` as the firmware holds it says what the fixture's does (Yay 0.2). */
static bool today_is(const yk_proto_today_t *t, const cJSON *m) {
    static const char *const SKIES[] = {"clear", "cloudy", "rain", "snow"};
    if (!t) return false;
    const cJSON *w = cJSON_GetObjectItemCaseSensitive(m, "weather");
    bool ok = t->weather == (!w ? -1 : cJSON_IsNull(w) ? 0 : 1);
    if (t->weather == 1) {
        ok = ok && !strcmp(SKIES[t->sky], str(w, "sky")) && t->night == cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(w, "night")) &&
             t->temp_c == (int)num(w, "tempC", 99) && !strcmp(t->text, str(w, "text")) && !strcmp(t->place, str(w, "place")) &&
             t->weather_at_ms == time_of(cJSON_GetObjectItemCaseSensitive(w, "at"));
    }
    const cJSON *next = cJSON_GetObjectItemCaseSensitive(m, "next");
    ok = ok && t->next_count == cJSON_GetArraySize(next);
    for (int i = 0; ok && i < t->next_count; i++) {
        const cJSON *n = cJSON_GetArrayItem(next, i), *title = cJSON_GetObjectItemCaseSensitive(n, "title");
        ok = !strcmp(t->next[i].id, str(n, "id")) && t->next[i].at_ms == time_of(cJSON_GetObjectItemCaseSensitive(n, "at")) &&
             t->next[i].has_title == !cJSON_IsNull(title) && !strcmp(t->next[i].title, cJSON_IsString(title) ? title->valuestring : "");
    }
    const char *done = str(m, "done");
    return ok && t->left == (int)num(m, "left", -1) && !strcmp(t->day, str(m, "day")) && !strcmp(t->done, done ? done : "");
}

static void test_server(const cJSON *cases, bool valid) {
    const cJSON *c;
    cJSON_ArrayForEach(c, cases) {
        if (strcmp(str(c, "from"), "server")) continue;
        const cJSON *m = cJSON_GetObjectItemCaseSensitive(c, "message");
        const char *name = str(c, "name") ? str(c, "name") : str(m, "type");
        char *json = cJSON_PrintUnformatted(m);
        yk_srv_msg_t msg;
        const yk_srv_type_t t = yk_proto_parse(json, strlen(json), &msg);
        // Device policy (§13.3): what a newer server adds is taken, not dropped; the schema calls an unknown error
        // code invalid, the device still ends the turn with it.
        const bool lenient = !strcmp(name, "an unknown error code");
        // Older servers sent brightness "auto": read, and answered UNSUPPORTED (no light sensor).
        const bool auto_brightness = !strcmp(name, "automatic brightness (no light sensor)");
        // A colour COLORWAYS does not have yet (0.1.19): the schema calls it invalid, the device reads it and keeps its
        // own accent (colorway.set answered UNSUPPORTED; hello.ok kept), so that a later colour never costs a session.
        const bool later_colour = !strcmp(name, "follow: a colour not in COLORWAYS"),
                   later_colour_hello = !strcmp(name, "follow: hello.ok with a colour not in COLORWAYS");
        // The shared folders' (0.1.19, the folder group): a grant with a field more is a newer server's, and read; a
        // hello.ok whose folders are no list the device takes (null, an app twice, more than 64) is the hello.ok still,
        // its folders not taken (the check goes by GET), never the session with it.
        const bool folder_more = !valid && !strcmp(name, "folder: a grant with more than it has"),
                   folder_hello = !valid && !strncmp(name, "folder: hello.ok", 16);
        // hello.ok's binding (0.1.20) that is not one: the hello.ok still, the binding left out (the check skipped).
        const bool binding_bad = !valid && !strncmp(name, "offline: a binding", 18);
        // A sky the round screen has no icon for (Yay 0.2): the schema calls it invalid, the device reads the `today` and
        // draws no weather (设计 A7), so that a newer platform's sky never costs the day's things.
        const bool later_sky = !valid && !strcmp(name, "y02: today with a sky not listed");
        // An older inbox kind with proactive's id and say (Yay 0.2): the schema calls it invalid, the device takes it as
        // before and does not read them, so that a newer platform giving reminders an id never costs the reminder.
        const bool kind_with_id = !valid && !strcmp(name, "y02: done with an id");
        if (kind_with_id) {
            CHECK_CASE(t == YK_SRV_INBOX && !strcmp(msg.inbox_kind, str(m, "kind")) && !msg.inbox_id[0] && !msg.inbox_say &&
                           msg.text && !strcmp(msg.text, str(m, "text")),
                       name);
        } else if (later_sky) {
            CHECK_CASE(t == YK_SRV_TODAY && msg.today && msg.today->weather == 0 && msg.today->sky == YK_SKY_OTHER &&
                           msg.today->next_count == cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(m, "next")),
                       name);
        } else if (lenient) {
            CHECK_CASE(t == YK_SRV_ERROR && !strcmp(msg.code, "OOPS"), name);
        } else if (later_colour) {
            CHECK_CASE(t == YK_SRV_ACTION && msg.kind == YK_ACT_COLORWAY_SET && !strcmp(msg.colorway, str(m, "colorway")), name);
        } else if (later_colour_hello) {
            CHECK_CASE(t == YK_SRV_HELLO_OK, name);
        } else if (folder_more) {
            CHECK_CASE(t == YK_SRV_ACTION && msg.kind == YK_ACT_FOLDER_SET && msg.folder.rev > 0, name);
        } else if (folder_hello) {
            CHECK_CASE(t == YK_SRV_HELLO_OK && !msg.folders_given && !msg.folders, name);
        } else if (binding_bad) {
            CHECK_CASE(t == YK_SRV_HELLO_OK && !msg.binding[0], name);
        } else if (auto_brightness) {
            CHECK_CASE(t == YK_SRV_ACTION && msg.kind == YK_ACT_BRIGHTNESS && msg.level == -1 && msg.step == 0, name);
        } else if (valid) {
            CHECK_CASE(t != YK_SRV_INVALID && !strcmp(yk_proto_type_name(t), str(m, "type")), name);
            CHECK_CASE(msg.text != NULL, name);
            const char *turn = str(m, "turn");
            CHECK_CASE(!turn || !strcmp(msg.turn, turn), name);
            if (t == YK_SRV_ACTION && !strcmp(str(m, "kind"), "onboarding.set")) {  // as the fixture has it
                unsigned want = 0;
                const cJSON *step;
                cJSON_ArrayForEach(step, cJSON_GetObjectItemCaseSensitive(m, "skip")) want |= 1u << step->valueint;
                const char *loc = str(m, "locale");
                CHECK_CASE(msg.kind == YK_ACT_ONBOARDING_SET && msg.onb_skip == want, name);
                CHECK_CASE(!strcmp(msg.onb_locale, loc ? loc : ""), name);
            }
            if (t == YK_SRV_HELLO_OK) {  // the account's language as the fixture has it (#545), "" without one
                const char *loc = str(cJSON_GetObjectItemCaseSensitive(m, "owner"), "locale");
                CHECK_CASE(!strcmp(msg.owner_locale, loc ? loc : ""), name);
                const char *colorway = str(cJSON_GetObjectItemCaseSensitive(m, "owner"), "colorway");  // 0.1.19
                CHECK_CASE(!strcmp(msg.owner_colorway, colorway ? colorway : ""), name);
            }
            if (t == YK_SRV_ACTION && !strcmp(str(m, "kind"), "locale.set"))  // 0.1.19, the follow group
                CHECK_CASE(msg.kind == YK_ACT_LOCALE_SET && !strcmp(msg.locale, str(m, "locale")), name);
            if (t == YK_SRV_ACTION && !strcmp(str(m, "kind"), "colorway.set"))
                CHECK_CASE(msg.kind == YK_ACT_COLORWAY_SET && !strcmp(msg.colorway, str(m, "colorway")), name);
            if (t == YK_SRV_ACTION && !strcmp(str(m, "kind"), "folder.set"))  // 0.1.19, the folder group
                CHECK_CASE(msg.kind == YK_ACT_FOLDER_SET && folder_is(&msg.folder, cJSON_GetObjectItemCaseSensitive(m, "grant")), name);
            if (t == YK_SRV_HELLO_OK) {  // the shared folders' whole list as the fixture has it, or none given
                const cJSON *list = cJSON_GetObjectItemCaseSensitive(m, "folders");
                CHECK_CASE(msg.folders_given == cJSON_IsArray(list), name);
                CHECK_CASE(!list || msg.folder_count == cJSON_GetArraySize(list), name);
                for (int i = 0; list && i < msg.folder_count; i++)
                    CHECK_CASE(folder_is(&msg.folders[i], cJSON_GetArrayItem(list, i)), name);
                // offlineNotes and binding (0.1.20): as the fixture has them, -1 / "" when left out
                const cJSON *notes = cJSON_GetObjectItemCaseSensitive(m, "offlineNotes");
                const char *binding = str(m, "binding");
                CHECK_CASE(msg.offline_notes == (cJSON_IsBool(notes) ? cJSON_IsTrue(notes) : -1), name);
                CHECK_CASE(!strcmp(msg.binding, binding ? binding : ""), name);
            }
            if (t == YK_SRV_TODAY) CHECK_CASE(today_is(msg.today, m), name);  // Yay 0.2: every field as the fixture's
            if (t != YK_SRV_TODAY) CHECK_CASE(!msg.today, name);
            // 两套部署 · 语音单独同意 (#2740): voice only with OPERATION_NOT_ALLOWED and "voice" itself; none otherwise
            const char *consent = str(m, "consent");
            const bool voice = t == YK_SRV_ERROR && !strcmp(str(m, "code"), "OPERATION_NOT_ALLOWED") && consent &&
                               !strcmp(consent, "voice");
            CHECK_CASE(msg.consent == (voice ? YK_PROTO_CONSENT_VOICE : YK_PROTO_CONSENT_NONE), name);
            if (t == YK_SRV_ACTION && !strcmp(str(m, "kind"), "record.start")) CHECK_CASE(msg.kind == YK_ACT_RECORD_START, name);
            if (t == YK_SRV_INBOX) {  // proactive (0.2): its id and say; the other kinds have neither
                const char *id = str(m, "id");
                CHECK_CASE(!strcmp(msg.inbox_kind, str(m, "kind")) && !strcmp(msg.inbox_id, id ? id : ""), name);
                CHECK_CASE(msg.inbox_say == cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(m, "say")), name);
                CHECK_CASE(!strcmp(msg.text, str(m, "text")), name);
            }
            if (t == YK_SRV_OFFLINE_DIGEST) {  // 0.1.20: its id, what it made, its line
                const cJSON *counts = cJSON_GetObjectItemCaseSensitive(m, "counts");
                CHECK_CASE(!strcmp(msg.digest_id, str(m, "id")) && !strcmp(msg.text, str(m, "line")), name);
                CHECK_CASE(msg.digest_total == (int)num(counts, "total", -1) && msg.digest_notes == (int)num(counts, "notes", -1) &&
                               msg.digest_reminders == (int)num(counts, "reminders", -1) &&
                               msg.digest_untimed == (int)num(counts, "untimed", -1),
                           name);
            }
        } else {
            CHECK_CASE(t == YK_SRV_INVALID, name);
        }
        yk_proto_free(&msg);
        cJSON_free(json);
    }
}

static yk_srv_type_t parse(const char *json, yk_srv_msg_t *m) { return yk_proto_parse(json, strlen(json), m); }

static void test_fields(void) {
    yk_srv_msg_t m;
    CHECK(parse("{\"type\":\"hello.ok\",\"session\":\"s1\",\"time\":1792310400000,\"owner\":{\"nickname\":\"小鱼\"},"
                "\"credits\":{\"remaining\":846,\"resetAt\":\"2026-10-18T16:00:00.000Z\"},\"dot\":true}", &m) == YK_SRV_HELLO_OK);
    CHECK(m.time_ms == 1792310400000LL && !strcmp(m.nickname, "小鱼") && m.has_credits && m.credits_remaining == 846 &&
          m.dot);
    CHECK(m.next_reminder == -1);  // no nextReminderAt (platforms before it): not known, never "none"
    yk_proto_free(&m);
    CHECK(!m.owner_locale[0]);  // no owner.locale (servers before #545): none
    // nextReminderAt (N): a time, null (none), or anything else: not known, the hello.ok kept all the same
    static const struct {
        const char *json;
        int8_t want;
        int64_t ms;
    } reminders[] = {
        {"1792330200000", 1, 1792330200000LL}, {"null", 0, 0}, {"0", -1, 0}, {"-5", -1, 0}, {"1.5", -1, 0},
        {"\"soon\"", -1, 0}, {"{}", -1, 0},
    };
    for (size_t i = 0; i < sizeof(reminders) / sizeof(reminders[0]); i++) {
        char hello[400];
        snprintf(hello, sizeof(hello),
                 "{\"type\":\"hello.ok\",\"session\":\"s1\",\"time\":1792310400000,\"owner\":{\"nickname\":\"小鱼\"},"
                 "\"credits\":{\"remaining\":846,\"resetAt\":\"2026-10-18T16:00:00.000Z\"},\"dot\":false,"
                 "\"nextReminderAt\":%s}",
                 reminders[i].json);
        CHECK(parse(hello, &m) == YK_SRV_HELLO_OK);
        CHECK(m.next_reminder == reminders[i].want && (reminders[i].want != 1 || m.next_reminder_ms == reminders[i].ms));
        yk_proto_free(&m);
    }
    // powerLog (0.1.20 4.10, 测试员计划): true turns the report's extra fields on; left out, false or anything else is off,
    // the hello.ok kept all the same.
    static const struct {
        const char *json;
        bool want;
    } power[] = {{",\"powerLog\":true", true}, {",\"powerLog\":false", false}, {"", false},
                 {",\"powerLog\":\"true\"", false}, {",\"powerLog\":1", false}, {",\"powerLog\":null", false}};
    for (size_t i = 0; i < sizeof(power) / sizeof(power[0]); i++) {
        char hello[320];
        snprintf(hello, sizeof(hello),
                 "{\"type\":\"hello.ok\",\"session\":\"s1\",\"time\":1792310400000,\"owner\":{\"nickname\":\"n\"},"
                 "\"credits\":{\"remaining\":10,\"resetAt\":\"2026-11-15T16:00:00.000Z\"},\"dot\":false%s}",
                 power[i].json);
        yk_srv_msg_t m;
        CHECK(parse(hello, &m) == YK_SRV_HELLO_OK && m.power_log == power[i].want);
        yk_proto_free(&m);
    }
    // owner.locale (#545): kept as said; a value a device would not keep is left out, the hello.ok kept all the same
    static const struct {
        const char *json, *want;
    } locales[] = {
        {"\"en\"", "en"},       {"\"zh-Hans\"", "zh-Hans"}, {"\"ja\"", "ja"},   {"\"es-419\"", "es-419"},
        {"\"x\"", ""},          {"\"zh-Hans-CN\"", ""},    {"\"\"", ""},      {"5", ""},
        {"null", ""},             {"{\"a\":1}", ""},          {"\"e n\"", ""},
    };
    for (size_t i = 0; i < sizeof(locales) / sizeof(locales[0]); i++) {
        char hello[400];
        snprintf(hello, sizeof(hello),
                 "{\"type\":\"hello.ok\",\"session\":\"s1\",\"time\":1792310400000,\"owner\":{\"nickname\":\"小鱼\","
                 "\"locale\":%s},\"credits\":{\"remaining\":846,\"resetAt\":\"2026-10-18T16:00:00.000Z\"},\"dot\":false}",
                 locales[i].json);
        CHECK(parse(hello, &m) == YK_SRV_HELLO_OK);
        CHECK(!strcmp(m.owner_locale, locales[i].want) && !strcmp(m.nickname, "小鱼"));
        yk_proto_free(&m);
    }
    CHECK(parse("{\"type\":\"stt.final\",\"turn\":\"t7f3a9c21d0e4b55\",\"text\":\"\"}", &m) == YK_SRV_STT_FINAL);
    CHECK(m.text && m.text[0] == 0);  // not understood
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"reply.end\",\"turn\":\"t7f3a9c21d0e4b55\",\"credits\":{\"used\":3.1,\"remaining\":0},"
                "\"muted\":\"credits\"}", &m) == YK_SRV_REPLY_END);
    CHECK(m.has_credits && m.credits_used > 3.09 && m.credits_used < 3.11 && m.credits_remaining == 0 &&
          !strcmp(m.muted, "credits"));
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"reply.line\",\"turn\":\"s-k2m8q4w9z1x7c3\",\"line\":3,\"text\":\"好\"}", &m) == YK_SRV_REPLY_LINE);
    CHECK(m.line == 3 && yk_proto_server_turn(m.turn) && !strcmp(m.text, "好"));
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"a1\",\"kind\":\"app.open\",\"slug\":\"language-buddy\","
                "\"params\":{\"level\":\"beginner\"}}", &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_APP_OPEN && !strcmp(m.slug, "language-buddy") && !strcmp(m.params, "{\"level\":\"beginner\"}"));
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"a4\",\"kind\":\"brightness.set\",\"level\":\"auto\"}", &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_BRIGHTNESS && m.level == -1 && m.step == 0);
    yk_proto_free(&m);
    // Internal commands: a level, a step, mute, the screen-off time.
    CHECK(parse("{\"type\":\"action\",\"id\":\"a5\",\"kind\":\"volume.set\",\"level\":6}", &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_VOLUME && m.level == 6 && m.step == 0 && m.mute == -1);
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"a5\",\"kind\":\"volume.set\",\"step\":-2}", &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_VOLUME && m.level == -1 && m.step == -2 && m.mute == -1);
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"a5\",\"kind\":\"volume.set\",\"mute\":false}", &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_VOLUME && m.level == -1 && m.step == 0 && m.mute == 0);
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"a5\",\"kind\":\"brightness.set\",\"step\":3}", &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_BRIGHTNESS && m.level == -1 && m.step == 3);
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"a5\",\"kind\":\"screen_off.set\",\"seconds\":0}", &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_SCREEN_OFF && m.seconds == 0);
    yk_proto_free(&m);
    // Back to Yay, asked from the phone: a command's id (a UUID) is the action's.
    CHECK(parse("{\"type\":\"action\",\"id\":\"7d4c2f9e-3b1a-4c8d-9e2f-1a3b5c7d9e0f\",\"kind\":\"app.home\"}", &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_APP_HOME && !strcmp(m.action_id, "7d4c2f9e-3b1a-4c8d-9e2f-1a3b5c7d9e0f"));
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"a5\",\"kind\":\"volume.set\",\"level\":\"auto\"}", &m) == YK_SRV_INVALID);
    CHECK(parse("{\"type\":\"action\",\"id\":\"a5\",\"kind\":\"volume.set\",\"mute\":1}", &m) == YK_SRV_INVALID);
    CHECK(parse("{\"type\":\"action\",\"id\":\"a5\",\"kind\":\"volume.set\",\"step\":1,\"mute\":true}", &m) == YK_SRV_INVALID);
    // A newer kind with fields of its own is still answered.
    CHECK(parse("{\"type\":\"action\",\"id\":\"a5\",\"kind\":\"night.set\",\"level\":\"dim\"}", &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_UNKNOWN);
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"ui.state\",\"state\":\"working\",\"label\":\"在看邮件\",\"progress\":0.33,\"taskId\":\"t2\"}", &m) ==
          YK_SRV_UI_STATE);
    CHECK(!strcmp(m.state, "working") && m.progress > 0.32f && m.progress < 0.34f && !strcmp(m.task_id, "t2"));
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"ui.state\",\"state\":\"done\",\"label\":\"做好了\",\"progress\":null,\"taskId\":null}", &m) ==
          YK_SRV_UI_STATE);
    CHECK(m.progress < 0 && m.task_id[0] == 0);
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"error\",\"turn\":\"t7f3a9c21d0e4b55\",\"code\":\"RATE_LIMITED\",\"retryAfter\":30}", &m) ==
          YK_SRV_ERROR);
    CHECK(!strcmp(m.code, "RATE_LIMITED") && m.retry_after == 30);
    yk_proto_free(&m);
    // Not messages at all, or broken ones.
    CHECK(parse("not json", &m) == YK_SRV_INVALID);
    CHECK(parse("{\"type\":\"reply.line\",\"turn\":\"t7f3a9c21d0e4b55\",\"line\":256,\"text\":\"x\"}", &m) == YK_SRV_INVALID);
    CHECK(parse("{\"type\":\"reply.line\",\"turn\":\"t7f3a9c21d0e4b55\",\"line\":0,\"text\":\"\"}", &m) == YK_SRV_INVALID);
    CHECK(parse("{\"type\":\"ui.state\",\"state\":\"working\",\"label\":\"x\",\"progress\":0.5}", &m) == YK_SRV_INVALID);
    CHECK(parse("{\"type\":\"hello.ok\",\"session\":\"s\",\"time\":1,\"owner\":{\"nickname\":\"\"},"
                "\"credits\":{\"remaining\":1,\"resetAt\":\"2026-10-18 16:00\"},\"dot\":false}", &m) == YK_SRV_INVALID);
    CHECK(parse("{\"type\":\"session.reauth\"}", &m) == YK_SRV_REAUTH);
    yk_proto_free(&m);
    // A newer server: a new muted value reads as speech, a new action kind is still answered.
    CHECK(parse("{\"type\":\"reply.end\",\"turn\":\"t7f3a9c21d0e4b55\",\"credits\":{\"used\":1,\"remaining\":5},"
                "\"muted\":\"quiet-hours\"}", &m) == YK_SRV_REPLY_END);
    CHECK(!strcmp(m.muted, "speech"));
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"a7\",\"kind\":\"camera.snap\"}", &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_UNKNOWN && !strcmp(m.action_id, "a7"));
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"kind\":\"camera.snap\"}", &m) == YK_SRV_INVALID);  // no id: nothing to answer
    CHECK(parse("{\"type\":\"error\",\"turn\":\"t7f3a9c21d0e4b55\",\"code\":\"\"}", &m) == YK_SRV_INVALID);
}

static void test_frames(const cJSON *frames) {
    const cJSON *c;
    cJSON_ArrayForEach(c, frames) {
        const cJSON *f = cJSON_GetObjectItemCaseSensitive(c, "frame");
        const char *hex = str(f, "packet"), *want = str(c, "bytes");
        uint8_t packet[64], out[80];
        size_t n = strlen(hex) / 2;
        for (size_t i = 0; i < n; i++) sscanf(hex + 2 * i, "%2hhx", &packet[i]);
        const size_t len = yk_proto_frame(out, (int)num(f, "stream", 0), (int)num(f, "line", 0), (int)num(f, "seq", 0), packet, n);
        char got[200] = "";
        for (size_t i = 0; i < len; i++) sprintf(got + 2 * i, "%02x", out[i]);
        CHECK_CASE(!strcmp(got, want), str(c, "name"));
        int stream, line, seq;
        const uint8_t *p;
        size_t bytes;
        CHECK(yk_proto_unframe(out, len, &stream, &line, &seq, &p, &bytes) && stream == (int)num(f, "stream", 0) &&
              line == (int)num(f, "line", 0) && seq == (int)num(f, "seq", 0) && bytes == n && !memcmp(p, packet, n));
    }
    const uint8_t bad_version[] = {2, 1, 0, 0, 9}, bad_stream[] = {1, 3, 0, 0, 9}, empty[] = {1, 1, 0, 0};
    int s, l, q;
    const uint8_t *p;
    size_t b;
    CHECK(!yk_proto_unframe(bad_version, 5, &s, &l, &q, &p, &b));
    CHECK(!yk_proto_unframe(bad_stream, 5, &s, &l, &q, &p, &b));
    CHECK(!yk_proto_unframe(empty, 4, &s, &l, &q, &p, &b));
}

/** The modes (extension-platform.md P1) and the records Yay carries (P19), beyond the fixtures. */
static void test_modes(void) {
    CHECK(yk_proto_pair_ok("zh-ja") && !yk_proto_pair_ok("zh-zh") && !yk_proto_pair_ok("zh-ko") &&
          !yk_proto_pair_ok("zhja") && !yk_proto_pair_ok("zh-jap") && !yk_proto_pair_ok(NULL));
    yk_proto_hello_t h = {.fw = "language-buddy/0.1.0", .board = "tikpal-r1", .locale = "zh", .tz = "Asia/Shanghai",
                          .battery_pct = 80, .rssi = -50, .out_rate = 16000, .mode = "practice",
                          .practice_language = "en"};
    CHECK(yk_proto_hello(&h) == NULL);  // a language without a level
    h.practice_level = "everyday";
    char *s = yk_proto_hello(&h);
    CHECK(s && strstr(s, "\"mode\":\"practice\",\"practice\":{\"language\":\"en\",\"level\":\"everyday\"}"));
    cJSON_free(s);
    h.mode = "karaoke";
    CHECK(yk_proto_hello(&h) == NULL);
    h.mode = NULL, h.practice_language = h.practice_level = NULL;
    s = yk_proto_hello(&h);
    CHECK(s && !strstr(s, "mode"));  // chat, as before the modes
    cJSON_free(s);
    CHECK(yk_proto_listen_start("t7f3a9c21d0e4b55", 300, "left") == NULL);
    s = yk_proto_listen_start_ex("t7f3a9c21d0e4b55", 300, NULL, true);  // recorded while connecting (#834's example)
    CHECK(s && !strcmp(s, "{\"type\":\"listen.start\",\"turn\":\"t7f3a9c21d0e4b55\",\"preroll\":300,\"held\":true}"));
    cJSON_free(s);
    s = yk_proto_listen_start("t7f3a9c21d0e4b55", 300, NULL);
    CHECK(s && !strstr(s, "held"));  // live, as before
    cJSON_free(s);
    CHECK(yk_proto_translate_text("t7f3a9c21d0e4b55", "", "zh") == NULL);
    CHECK(yk_proto_practice_end("bored") == NULL);
    s = yk_proto_practice_end("home");  // the phone asked it back to Yay (app.home)
    CHECK(s && strstr(s, "\"why\":\"home\""));
    cJSON_free(s);

    // Records as the outbox keeps them: "app" is left behind, the rest passes as it is.
    const char *rec[2] = {"{\"app\":\"breathe\",\"id\":\"yk_0a1b2c3d4e5f:breathe:7\",\"kind\":\"breathe.session\","
                          "\"occurredAt\":\"2026-10-19T08:53:20.123Z\",\"clockTrusted\":true,\"data\":{\"minutes\":5}}",
                          "{\"app\":\"breathe\",\"id\":\"yk_0a1b2c3d4e5f:breathe:8\",\"kind\":\"breathe.session\","
                          "\"occurredAt\":\"1970-01-01T00:00:12.000Z\",\"clockTrusted\":false,\"data\":{}}"};
    s = yk_proto_app_records("breathe", rec, 2);
    CHECK(s && !strcmp(s, "{\"type\":\"app.records\",\"slug\":\"breathe\",\"records\":[{\"id\":\"yk_0a1b2c3d4e5f:breathe:7\","
                          "\"kind\":\"breathe.session\",\"occurredAt\":\"2026-10-19T08:53:20.123Z\",\"clockTrusted\":true,"
                          "\"data\":{\"minutes\":5}},{\"id\":\"yk_0a1b2c3d4e5f:breathe:8\",\"kind\":\"breathe.session\","
                          "\"occurredAt\":\"1970-01-01T00:00:12.000Z\",\"clockTrusted\":false,\"data\":{}}]}"));
    cJSON_free(s);
    const char *bad[] = {"{\"id\":\"a b c d\",\"kind\":\"breathe.session\",\"occurredAt\":\"2026-10-19T08:53:20Z\",\"data\":{}}",
                         "{\"id\":\"abcd\",\"kind\":\"Breathe\",\"occurredAt\":\"2026-10-19T08:53:20Z\",\"data\":{}}",
                         "{\"id\":\"abcd\",\"kind\":\"breathe.session\",\"occurredAt\":\"2026-10-19 08:53\",\"data\":{}}",
                         "{\"id\":\"abcd\",\"kind\":\"breathe.session\",\"occurredAt\":\"2026-10-19T08:53:20Z\",\"data\":[]}",
                         "not json"};
    for (int i = 0; i < 5; i++) CHECK(yk_proto_app_records("breathe", &bad[i], 1) == NULL);
    CHECK(yk_proto_app_records("breathe", rec, 0) == NULL && yk_proto_app_records("breathe", rec, 51) == NULL);

    yk_srv_msg_t m;
    CHECK(parse("{\"type\":\"hello.ok\",\"session\":\"s1\",\"time\":1792310400000,\"owner\":{\"nickname\":\"\"},\"credits\":"
                "{\"remaining\":846,\"resetAt\":\"2026-10-18T16:00:00.000Z\"},\"dot\":false,\"practice\":{\"turnPoints\":11.5}}",
                &m) == YK_SRV_HELLO_OK);
    CHECK(m.has_practice && m.turn_points == 11.5 && m.week_days == -1 && m.week_turns == -1);
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"practice.turn\",\"turn\":\"t7f3a9c21d0e4b55\",\"verdict\":\"fix\",\"fix\":{\"said\":\"たこ焼きが二つください\","
                "\"better\":\"たこ焼きを二つください\",\"spans\":[[4,5],[6,8]],\"note\":\"要东西用「を」\"},\"hint\":\"お茶をください。\","
                "\"newWords\":[{\"word\":\"お茶\",\"meaning\":\"茶\"}]}", &m) == YK_SRV_PRACTICE_TURN);
    CHECK(!strcmp(m.verdict, "fix") && !strcmp(m.better, "たこ焼きを二つください") && !strcmp(m.note, "要东西用「を」") &&
          m.span_count == 2 && m.spans[1][0] == 6 && m.spans[1][1] == 8 && !strcmp(m.text, "お茶をください。") &&
          m.word_count == 1 && !strcmp(m.words[0], "お茶") && !strcmp(m.meanings[0], "茶"));
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"practice.turn\",\"turn\":\"t7f3a9c21d0e4b55\",\"verdict\":\"good\"}", &m) == YK_SRV_PRACTICE_TURN);
    CHECK(!m.said && !m.better && m.span_count == 0 && m.word_count == 0 && m.text && !m.text[0]);
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"practice.turn\",\"turn\":\"t7f3a9c21d0e4b55\",\"verdict\":\"fix\",\"fix\":{\"said\":\"a\",\"better\":\"b\","
                "\"spans\":[[0]],\"note\":\"\"}}", &m) == YK_SRV_INVALID);
    CHECK(parse("{\"type\":\"practice.turn\",\"turn\":\"t7f3a9c21d0e4b55\",\"verdict\":\"good\",\"newWords\":[{\"word\":\"a\","
                "\"meaning\":\"b\"},{\"word\":\"c\",\"meaning\":\"d\"},{\"word\":\"e\",\"meaning\":\"f\"}]}", &m) == YK_SRV_INVALID);
    // reply.line: 120 characters for a translated sentence; a kind or language this firmware does not know is not
    // refused (a subtitle; no language).
    char line[400];
    snprintf(line, sizeof(line), "{\"type\":\"reply.line\",\"turn\":\"t7f3a9c21d0e4b55\",\"line\":0,\"text\":\"%0120d\","
             "\"kind\":\"aside\",\"lang\":\"ko\"}", 0);
    CHECK(parse(line, &m) == YK_SRV_REPLY_LINE && !m.fix_line && !m.lang[0] && strlen(m.text) == 120);
    yk_proto_free(&m);
    snprintf(line, sizeof(line), "{\"type\":\"reply.line\",\"turn\":\"t7f3a9c21d0e4b55\",\"line\":0,\"text\":\"%0121d\"}", 0);
    CHECK(parse(line, &m) == YK_SRV_INVALID);
    CHECK(parse("{\"type\":\"reply.line\",\"turn\":\"t7f3a9c21d0e4b55\",\"line\":1,\"kind\":\"fix\",\"lang\":\"en\",\"text\":\"Two, please.\"}",
                &m) == YK_SRV_REPLY_LINE && m.fix_line && !strcmp(m.lang, "en"));
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"stt.final\",\"turn\":\"t7f3a9c21d0e4b55\",\"text\":\"안녕\",\"lang\":\"other\"}", &m) == YK_SRV_STT_FINAL &&
          !strcmp(m.lang, "other"));
    yk_proto_free(&m);
    // app.records.ok: the batch's ids the platform is done with.
    CHECK(parse("{\"type\":\"app.records.ok\",\"slug\":\"breathe\",\"ids\":[\"yk_0a1b2c3d4e5f:breathe:7\","
                "\"yk_0a1b2c3d4e5f:breathe:8\"]}", &m) == YK_SRV_APP_RECORDS_OK);
    CHECK(!strcmp(m.slug, "breathe") && m.id_count == 2 && !strcmp(m.ids[1], "yk_0a1b2c3d4e5f:breathe:8"));
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"app.records.ok\",\"slug\":\"breathe\",\"ids\":[]}", &m) == YK_SRV_APP_RECORDS_OK &&
          m.id_count == 0);
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"app.records.ok\",\"slug\":\"breathe\",\"ids\":[\"a b\"]}", &m) == YK_SRV_INVALID);
    CHECK(parse("{\"type\":\"app.records.ok\",\"slug\":\"Breathe\",\"ids\":[\"abcd\"]}", &m) == YK_SRV_INVALID);
    CHECK(parse("{\"type\":\"app.records.ok\",\"slug\":\"breathe\"}", &m) == YK_SRV_INVALID);
}

/** practice.turn in two parts (the contract's fixtures): merged by turn, the later fields added, none replaced. */
static void test_practice_parts(const cJSON *valid) {
    const char *first = NULL, *second = NULL;
    const cJSON *c;
    cJSON_ArrayForEach(c, valid) {
        const char *name = str(c, "name");
        if (!name || strncmp(name, "practice: in two parts", 22)) continue;
        char *json = cJSON_PrintUnformatted(cJSON_GetObjectItemCaseSensitive(c, "message"));
        if (strstr(name, "1 of 2")) first = json;
        else second = json;
    }
    CHECK(first && second);
    if (!first || !second) return;
    yk_srv_msg_t a, b;
    CHECK(parse(first, &a) == YK_SRV_PRACTICE_TURN && parse(second, &b) == YK_SRV_PRACTICE_TURN);
    CHECK(yk_proto_practice_merge(&a, &b));
    CHECK(!strcmp(a.verdict, "fix") && !strcmp(a.better, "I went to school yesterday.") && a.span_count == 1 &&
          a.spans[0][0] == 2 && a.spans[0][1] == 6 && !strcmp(a.note, "过去的事用过去式") && a.word_count == 1 &&
          !strcmp(a.words[0], "photosynthesis") && !strcmp(a.text, "I learned about plants."));
    // The other way round (the hint first): nothing that came first is replaced.
    yk_srv_msg_t x, y;
    parse(second, &x);
    parse(first, &y);
    CHECK(yk_proto_practice_merge(&x, &y) && !strcmp(x.text, "I learned about plants.") && x.better && x.word_count == 1);
    // Another turn's part is not merged; it is freed all the same.
    yk_srv_msg_t z;
    CHECK(parse("{\"type\":\"practice.turn\",\"turn\":\"aaaaaaaaaaaaaaaa\",\"verdict\":\"good\",\"hint\":\"Hi\"}", &z) ==
          YK_SRV_PRACTICE_TURN);
    CHECK(!yk_proto_practice_merge(&a, &z) && !strcmp(a.text, "I learned about plants.") && !z.text);
    yk_proto_free(&a);
    yk_proto_free(&x);
    cJSON_free((void *)first);
    cJSON_free((void *)second);
}

static void test_buddy(void) {
    yk_srv_msg_t m;
    const char *sha = "9e3b7f1d5a0c8e2f6b4d1a7c3e9f5b0d2c8a6e4f1b7d3a9c5e0f2b8d6a4c1e7f";
    // What the loop takes: the buddy, and its pack in the fields a download has.
    CHECK(parse("{\"type\":\"action\",\"id\":\"a21\",\"kind\":\"buddy.set\",\"buddy\":\"mote\",\"pack\":{\"url\":"
                "\"https://x.dev/mote-sage-1.ykfig\",\"sha256\":\"9e3b7f1d5a0c8e2f6b4d1a7c3e9f5b0d2c8a6e4f1b7d3a9c5e0f2b8d6a4c1e7f\","
                "\"size\":643072}}", &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_BUDDY_SET && !strcmp(m.buddy, "mote") && !strcmp(m.action_id, "a21"));
    CHECK(m.size == 643072 && !strcmp(m.sha256, sha) && !strcmp(m.url, "https://x.dev/mote-sage-1.ykfig"));
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"a22\",\"kind\":\"buddy.set\",\"buddy\":\"pip\"}", &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_BUDDY_SET && !strcmp(m.buddy, "pip") && !m.url && !m.size);
    yk_proto_free(&m);
    // A newer field in the pack is read past.
    CHECK(parse("{\"type\":\"action\",\"id\":\"a21\",\"kind\":\"buddy.set\",\"buddy\":\"dial\",\"pack\":{\"url\":"
                "\"https://x.dev/d\",\"sha256\":\"9e3b7f1d5a0c8e2f6b4d1a7c3e9f5b0d2c8a6e4f1b7d3a9c5e0f2b8d6a4c1e7f\","
                "\"size\":1,\"art\":2}}", &m) == YK_SRV_ACTION);
    yk_proto_free(&m);
    // A pack that is null is no pack; the buddy is not a number.
    CHECK(parse("{\"type\":\"action\",\"id\":\"a21\",\"kind\":\"buddy.set\",\"buddy\":\"pip\",\"pack\":null}", &m) == YK_SRV_INVALID);
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"a21\",\"kind\":\"buddy.set\",\"buddy\":4}", &m) == YK_SRV_INVALID);
    yk_proto_free(&m);

    // What Yay says it shows: Pip without a sha256, a pack with its own, hello, prefs and action.done alike.
    const yk_proto_buddy_t pip = {.id = "pip"}, dial = {.id = "dial", .sha256 = sha};
    char *p = yk_proto_prefs_all(6, 8, 30, NULL, &pip);
    CHECK(p && strstr(p, "\"buddy\":{\"id\":\"pip\"}"));
    cJSON_free(p);
    p = yk_proto_prefs_all(6, 8, 30, NULL, &dial);
    CHECK(p && strstr(p, "\"buddy\":{\"id\":\"dial\",\"sha256\":\"9e3b7f"));
    cJSON_free(p);
    p = yk_proto_prefs_all(6, 8, 30, NULL, NULL);  // a firmware without the cap: as before
    CHECK(p && !strstr(p, "buddy"));
    cJSON_free(p);
    p = yk_proto_action_done_buddy("a21", false, "BUSY", &dial);
    CHECK(p && strstr(p, "\"error\":\"BUSY\"") && strstr(p, "\"buddy\":{\"id\":\"dial\"") && !strstr(p, "value"));
    cJSON_free(p);
    p = yk_proto_action_done_buddy("a21", true, NULL, NULL);
    CHECK(p && !strstr(p, "buddy"));
    cJSON_free(p);
    const yk_proto_buddy_t later = {.id = "new-friend-2"}, odd = {.id = "Dial"}, short_sha = {.id = "dial", .sha256 = "abc"};
    p = yk_proto_prefs_all(6, 8, 30, NULL, &later);  // any id of the contract's shape
    CHECK(p != NULL);
    cJSON_free(p);
    CHECK(!yk_proto_prefs_all(6, 8, 30, NULL, &odd) && !yk_proto_action_done_buddy("a1", true, NULL, &short_sha));
    CHECK(yk_proto_buddy_id_ok("pip") && !yk_proto_buddy_id_ok("p") && !yk_proto_buddy_id_ok(NULL) &&
          !yk_proto_buddy_id_ok("9lives") && !yk_proto_buddy_id_ok("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
}

static void test_saver(void) {
    yk_srv_msg_t m;
    // Everything, a catalog image: what to change, and the image to fetch.
    CHECK(parse("{\"type\":\"action\",\"id\":\"a11\",\"kind\":\"screensaver.set\",\"on\":true,\"after\":60,"
                "\"duration\":600,\"image\":{\"id\":\"default-03\",\"url\":\"https://x.dev/1/default-03-466.jpg\","
                "\"sha256\":\"3a9f1c7e5b2d8a4f0c6e9b1d7a3f5c8e2b0d4a6f9c1e7b3d5a8f0c2e6b4d9a1f\",\"size\":61210}}",
                &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_SAVER_SET && m.saver_on == 1 && m.saver_after == 60 && m.saver_duration == 600 &&
          !strcmp(m.image_id, "default-03") && !strcmp(m.url, "https://x.dev/1/default-03-466.jpg") && m.size == 61210 &&
          !strcmp(m.sha256, "3a9f1c7e5b2d8a4f0c6e9b1d7a3f5c8e2b0d4a6f9c1e7b3d5a8f0c2e6b4d9a1f"));
    yk_proto_free(&m);
    // Only the times: nothing said of on or the image.
    CHECK(parse("{\"type\":\"action\",\"id\":\"a14\",\"kind\":\"screensaver.set\",\"after\":300,\"duration\":1800}",
                &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_SAVER_SET && m.saver_on == -1 && m.saver_after == 300 && m.saver_duration == 1800 &&
          !m.image_id[0] && !m.url);
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"a15\",\"kind\":\"screensaver.set\",\"on\":false}", &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_SAVER_SET && m.saver_on == 0 && !m.saver_after && !m.saver_duration);
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"a13\",\"kind\":\"screensaver.preview\",\"url\":\"https://x.dev/p.jpg\","
                "\"sha256\":\"3a9f1c7e5b2d8a4f0c6e9b1d7a3f5c8e2b0d4a6f9c1e7b3d5a8f0c2e6b4d9a1f\",\"size\":98304,\"seconds\":10}",
                &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_SAVER_PREVIEW && m.seconds == 10 && m.size == 98304 && !strcmp(m.url, "https://x.dev/p.jpg"));
    yk_proto_free(&m);
    // A photo's id in capitals is still a UUID; an upper-case sha256, an on that is not a boolean, are not.
    CHECK(yk_proto_saver_image_ok("5D0C1B7E-8A3F-4E2B-9C61-2F7A4B8D3E90") && yk_proto_saver_image_ok("default-07"));
    CHECK(!yk_proto_saver_image_ok("default-7") && !yk_proto_saver_image_ok("default-ab") && !yk_proto_saver_image_ok(NULL) &&
          !yk_proto_saver_image_ok("5d0c1b7e-8a3f-4e2b-9c61-2f7a4b8d3e9") && !yk_proto_saver_image_ok("5d0c1b7e08a3f-4e2b-9c61-2f7a4b8d3e90"));
    CHECK(parse("{\"type\":\"action\",\"id\":\"a13\",\"kind\":\"screensaver.preview\",\"url\":\"https://x.dev/p.jpg\","
                "\"sha256\":\"3A9F1C7E5B2D8A4F0C6E9B1D7A3F5C8E2B0D4A6F9C1E7B3D5A8F0C2E6B4D9A1F\",\"size\":1,\"seconds\":10}",
                &m) == YK_SRV_INVALID);
    CHECK(parse("{\"type\":\"action\",\"id\":\"a15\",\"kind\":\"screensaver.set\",\"on\":1}", &m) == YK_SRV_INVALID);
    CHECK(parse("{\"type\":\"action\",\"id\":\"a15\",\"kind\":\"screensaver.preview\",\"url\":\"https://x.dev/p.jpg\","
                "\"sha256\":\"3a9f1c7e5b2d8a4f0c6e9b1d7a3f5c8e2b0d4a6f9c1e7b3d5a8f0c2e6b4d9a1f\",\"size\":0,\"seconds\":10}",
                &m) == YK_SRV_INVALID);
    // What Yay says it uses: a hello without the screensaver stays as before; a broken one is refused.
    const yk_proto_saver_t uses = {.on = true, .after_s = 60, .duration_s = 600, .image_id = "default-07",
                                   .image_sha256 = "834370e26ca77ce574e970b30ace0a6ce67a475061196bd389e0fbc7f04a7129"};
    char *p = yk_proto_prefs_saver(6, 8, 30, &uses);
    CHECK(p && strstr(p, "\"screensaver\":{\"on\":true,\"after\":60,\"duration\":600,\"image\":{\"id\":\"default-07\""));
    cJSON_free(p);
    p = yk_proto_prefs(6, 8, 30);
    CHECK(p && !strstr(p, "screensaver"));
    cJSON_free(p);
    yk_proto_saver_t bad = uses;
    bad.image_sha256 = "834370E26CA77CE574E970B30ACE0A6CE67A475061196BD389E0FBC7F04A7129";
    CHECK(!yk_proto_prefs_saver(6, 8, 30, &bad) && !yk_proto_action_done_saver("a1", true, NULL, -1, &bad));
    bad = uses;
    bad.after_s = 10;
    CHECK(!yk_proto_prefs_saver(6, 8, 30, &bad));
    p = yk_proto_action_done_saver("a1", false, "BAD_IMAGE", -1, &uses);
    CHECK(p && strstr(p, "\"error\":\"BAD_IMAGE\"") && strstr(p, "\"screensaver\":{") && !strstr(p, "value"));
    cJSON_free(p);
    // screen_off.set while the screensaver is on: the new time before it, and the screensaver as it is now.
    p = yk_proto_action_done_saver("a5", true, NULL, 300, &uses);
    CHECK(p && strstr(p, "\"value\":300") && strstr(p, "\"screensaver\":{\"on\":true"));
    cJSON_free(p);
    p = yk_proto_action_done_saver("a1", false, "BUSY", -1, NULL);  // a preview's: no screensaver
    CHECK(p && !strstr(p, "screensaver"));
    cJSON_free(p);
}

static void test_turns(void) {
    uint8_t r[16];
    char t[YK_PROTO_TURN_LEN + 1];
    for (int round = 0; round < 50; round++) {
        for (int i = 0; i < 16; i++) r[i] = (uint8_t)(round * 37 + i * 101);
        yk_proto_new_turn(t, r);
        CHECK(yk_proto_turn_ok(t) && !yk_proto_server_turn(t));
    }
    CHECK(yk_proto_turn_ok("s-k2m8q4w9z1x7c3") && yk_proto_server_turn("s-k2m8q4w9z1x7c3"));
    CHECK(!yk_proto_turn_ok("t7f3a9c21d0e4b5") && !yk_proto_turn_ok("t7f3a9c21d0e4b5!") && !yk_proto_turn_ok(NULL));
    CHECK(yk_proto_slug_ok("language-buddy") && !yk_proto_slug_ok("Yay") && !yk_proto_slug_ok("a") &&
          !yk_proto_slug_ok("1abc"));
}

static void test_backoff(void) {
    static const int steps[] = {1000, 2000, 4000, 8000, 15000, 30000, 30000, 30000};
    for (int a = 0; a < 8; a++) {
        int lo = 1 << 30, hi = 0;
        for (uint32_t r = 0; r < 4000; r += 7) {
            const int ms = yk_proto_backoff_ms(a, r * 2654435761u);
            lo = ms < lo ? ms : lo;
            hi = ms > hi ? ms : hi;
        }
        // Within ±25 % of the step, and spread over most of it (not one fixed value).
        CHECK(lo >= steps[a] * 3 / 4 && hi <= steps[a] * 5 / 4);
        CHECK(hi - lo >= steps[a] * 2 / 5);
    }
    CHECK(yk_proto_backoff_ms(-1, 0) == 750 && yk_proto_backoff_ms(1000, 0) == 22500);
}

/** Yay 0.1.19 (开发文档 2.5, 4.7): locale.set and colorway.set, and hello.ok's owner.colorway. Until the contract has
 *  their samples (the contract session's PR), these are the shapes the 开发文档 gives: as brightness.set, one field. */
static void test_follow(void) {
    yk_srv_msg_t m;
    static const struct {
        const char *json;
        bool ok;
        const char *want;
    } locales[] = {
        {"\"ja\"", true, "ja"},   {"\"es-419\"", true, "es-419"}, {"\"zh-Hans\"", true, "zh-Hans"}, {"\"en\"", true, "en"},
        {"\"zh\"", false, ""},    {"\"fr\"", false, ""},          {"5", false, ""},                {"null", false, ""},
    };
    for (size_t i = 0; i < sizeof(locales) / sizeof(locales[0]); i++) {
        char json[160];
        snprintf(json, sizeof(json), "{\"type\":\"action\",\"id\":\"l1\",\"kind\":\"locale.set\",\"locale\":%s}",
                 locales[i].json);
        const yk_srv_type_t t = parse(json, &m);
        CHECK(locales[i].ok ? t == YK_SRV_ACTION && m.kind == YK_ACT_LOCALE_SET && !strcmp(m.locale, locales[i].want) &&
                                  !strcmp(m.action_id, "l1")
                            : t == YK_SRV_INVALID);
        yk_proto_free(&m);
    }
    CHECK(parse("{\"type\":\"action\",\"id\":\"l2\",\"kind\":\"locale.set\"}", &m) == YK_SRV_INVALID);
    yk_proto_free(&m);
    // A colour's name: the five, and a later one too (the firmware answers it UNSUPPORTED); not a name, invalid.
    static const struct {
        const char *json;
        bool ok;
        const char *want;
    } colors[] = {
        {"\"original\"", true, "original"}, {"\"honey\"", true, "honey"},  {"\"cocoa\"", true, "cocoa"},
        {"\"lavender\"", true, "lavender"}, {"\"Sage\"", false, ""},       {"\"\"", false, ""},
        {"\"twelveletter\"", false, ""},    {"\"sage-2\"", false, ""},     {"3", false, ""},
    };
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        char json[160];
        snprintf(json, sizeof(json), "{\"type\":\"action\",\"id\":\"c1\",\"kind\":\"colorway.set\",\"colorway\":%s}",
                 colors[i].json);
        const yk_srv_type_t t = parse(json, &m);
        CHECK(colors[i].ok ? t == YK_SRV_ACTION && m.kind == YK_ACT_COLORWAY_SET && !strcmp(m.colorway, colors[i].want)
                           : t == YK_SRV_INVALID);
        yk_proto_free(&m);
    }
    // hello.ok's owner.colorway: kept as said; not a name, left out, the hello.ok kept all the same (as owner.locale).
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        char hello[400];
        snprintf(hello, sizeof(hello),
                 "{\"type\":\"hello.ok\",\"session\":\"s1\",\"time\":1792310400000,\"owner\":{\"nickname\":\"小鱼\","
                 "\"locale\":\"ja\",\"colorway\":%s},\"credits\":{\"remaining\":846,\"resetAt\":\"2026-10-18T16:00:00.000Z\"},"
                 "\"dot\":false}",
                 colors[i].json);
        CHECK(parse(hello, &m) == YK_SRV_HELLO_OK);
        CHECK(!strcmp(m.owner_colorway, colors[i].want) && !strcmp(m.owner_locale, "ja"));
        yk_proto_free(&m);
    }
    CHECK(parse("{\"type\":\"hello.ok\",\"session\":\"s1\",\"time\":1792310400000,\"owner\":{\"nickname\":\"\"},"
                "\"credits\":{\"remaining\":1,\"resetAt\":\"2026-10-18T16:00:00.000Z\"},\"dot\":false}",
                &m) == YK_SRV_HELLO_OK);
    CHECK(!m.owner_colorway[0]);  // a server from before: none
    yk_proto_free(&m);
    // The caps a firmware says: "locale" and "colorway" go in hello as any other.
    static const char *const caps[] = {"buddy", "locale", "colorway"};
    yk_proto_hello_t h = {.fw = "yay/0.1.19", .board = "tikpal-r1", .locale = "ja", .tz = "Asia/Tokyo", .battery_pct = 50,
                          .rssi = -50, .out_rate = 16000, .caps = caps, .cap_count = 3};
    char *json = yk_proto_hello(&h);
    CHECK(json && strstr(json, "\"caps\":[\"buddy\",\"locale\",\"colorway\"]"));
    cJSON_free(json);
}

/** 两套部署 (device.ts helloMessage.region): the contract's own hello with `region` (fixtures/deployments.json `hello`)
 *  built as the firmware builds it; without a region not a byte more than before (every firmware with CONFIG_YK_REGIONS
 *  off); a name the schema would refuse refused here. */
static void test_region(const char *deployments) {
    char *text = deployments ? read_file(deployments) : NULL;
    cJSON *doc = text ? cJSON_Parse(text) : NULL;
    const cJSON *hello = cJSON_GetObjectItemCaseSensitive(doc, "hello");
    if (!hello) {
        printf("yaykit_session: the deployments' hello left out (no fixtures/deployments.json)\n");
    } else {
        CHECK(str(hello, "region") && !strcmp(str(hello, "region"), "cn"));
        char *built = build(hello);
        cJSON *want = cJSON_Duplicate(hello, 1), *got = built ? cJSON_Parse(built) : NULL;
        normalize(want);
        CHECK(got && cJSON_Compare(got, want, 1));
        if (got && !cJSON_Compare(got, want, 1)) printf("  got %s\n", built);
        cJSON_Delete(want);
        cJSON_Delete(got);
        cJSON_free(built);
    }
    cJSON_Delete(doc);
    free(text);

    static const char *const caps[] = {"buddy", "locale"};
    yk_proto_hello_t h = {.fw = "yay/0.2.0", .board = "tikpal-r1", .locale = "zh", .tz = "Asia/Shanghai", .battery_pct = 50,
                          .rssi = -50, .out_rate = 16000, .caps = caps, .cap_count = 2};
    char *before = yk_proto_hello(&h);  // no region: as every hello before it
    CHECK(before && !strstr(before, "region"));
    static const char *const good[] = {"global", "cn", "eu-west", "ab", "a0123456789abcde"};
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        h.region = good[i];
        char *json = yk_proto_hello(&h), want[64];
        snprintf(want, sizeof(want), ",\"region\":\"%s\"}", good[i]);
        // The same hello with the one field at its end
        CHECK(json && before && strlen(json) == strlen(before) + strlen(want) - 1 && !strncmp(json, before, strlen(before) - 1) &&
              !strcmp(json + strlen(before) - 1, want));
        CHECK(yk_proto_region_ok(good[i]));
        cJSON_free(json);
    }
    static const char *const bad[] = {"", "c", "CN", "Cn", "1cn", "-cn", "cn_x", "cn!", "a0123456789abcdef", " cn"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        h.region = bad[i];
        CHECK(!yk_proto_hello(&h) && !yk_proto_region_ok(bad[i]));
    }
    CHECK(!yk_proto_region_ok(NULL));
    cJSON_free(before);
}

/** Two parsed `error`s are the same but for `consent`: every other field, byte for byte (an error owns only `text`). */
static bool same_but_consent(const yk_srv_msg_t *a, const yk_srv_msg_t *b) {
    if (!a->text || !b->text || strcmp(a->text, b->text)) return false;
    yk_srv_msg_t x, y;
    memcpy(&x, a, sizeof(x));
    memcpy(&y, b, sizeof(y));
    x.text = y.text = NULL;
    x.consent = y.consent = YK_PROTO_CONSENT_NONE;
    return !memcmp(&x, &y, sizeof(x));
}

/** Parses `m` as it is and with `consent` set to `value` (NULL: left out); true when both are errors alike but for it,
 *  the second's reason in *got. */
static bool parse_with_consent(const cJSON *m, const cJSON *value, yk_proto_consent_t *got) {
    cJSON *plain = cJSON_Duplicate(m, 1), *with = cJSON_Duplicate(m, 1);
    cJSON_DeleteItemFromObjectCaseSensitive(plain, "consent");
    cJSON_DeleteItemFromObjectCaseSensitive(with, "consent");
    if (value) cJSON_AddItemToObject(with, "consent", cJSON_Duplicate(value, 1));
    char *a = cJSON_PrintUnformatted(plain), *b = cJSON_PrintUnformatted(with);
    yk_srv_msg_t ma, mb;
    const yk_srv_type_t ta = yk_proto_parse(a, strlen(a), &ma), tb = yk_proto_parse(b, strlen(b), &mb);
    const bool ok = ta == YK_SRV_ERROR && tb == YK_SRV_ERROR && ma.consent == YK_PROTO_CONSENT_NONE && same_but_consent(&ma, &mb);
    *got = (yk_proto_consent_t)mb.consent;
    yk_proto_free(&ma);
    yk_proto_free(&mb);
    cJSON_free(a);
    cJSON_free(b);
    cJSON_Delete(plain);
    cJSON_Delete(with);
    return ok;
}

/** 两套部署 · 语音单独同意, V1 (device.ts error.consent, consent.ts VOICE_CONSENT_GATES, #2740): the contract's turn held
 *  for want of voice consent reads as voice; without the field, every error parses as before, field for field; a value
 *  not exactly "voice", or "voice" with another code, is as none and never costs the message (the turn still ends). */
static void test_consent(const cJSON *valid) {
    CHECK(offsetof(yk_srv_msg_t, consent) >= offsetof(yk_srv_msg_t, retry_after) + sizeof(int));  // only added, at the end
    const cJSON *c, *held = NULL;
    int errors = 0;
    cJSON *voice = cJSON_CreateString("voice");
    cJSON_ArrayForEach(c, valid) {
        const cJSON *m = cJSON_GetObjectItemCaseSensitive(c, "message");
        if (strcmp(str(c, "from"), "server") || strcmp(str(m, "type"), "error")) continue;
        const char *name = str(c, "name") ? str(c, "name") : str(m, "code");
        if (str(c, "name") && !strcmp(str(c, "name"), "a turn held for want of voice consent")) held = m;
        errors++;
        // Every one of the contract's errors: left out, as today; with "voice", voice only for OPERATION_NOT_ALLOWED
        yk_proto_consent_t got;
        CHECK_CASE(parse_with_consent(m, NULL, &got) && got == YK_PROTO_CONSENT_NONE, name);
        const bool refusal = !strcmp(str(m, "code"), "OPERATION_NOT_ALLOWED");
        CHECK_CASE(parse_with_consent(m, voice, &got) && got == (refusal ? YK_PROTO_CONSENT_VOICE : YK_PROTO_CONSENT_NONE), name);
    }
    CHECK(errors >= 4);  // the contract's: CREDITS_EXHAUSTED, SESSION_REPLACED, SERVICE_BUSY with credits, the held one
    CHECK(held != NULL);  // the contract's own example (fixtures/device.json)
    if (held) {
        char *json = cJSON_PrintUnformatted(held);
        yk_srv_msg_t m;
        CHECK(yk_proto_parse(json, strlen(json), &m) == YK_SRV_ERROR);
        CHECK(m.consent == YK_PROTO_CONSENT_VOICE && !strcmp(m.code, "OPERATION_NOT_ALLOWED") && !strcmp(m.turn, str(held, "turn")));
        CHECK(m.retry_after == 0 && !m.has_credits && !m.reset_at[0] && m.text && !m.text[0]);
        yk_proto_free(&m);
        cJSON_free(json);
        // Not "voice" itself: as none, the message read as without it
        static const char *const not_voice[] = {"Voice", "VOICE", "voice ", " voice", "voic", "voices", "", "apns", "apple",
                                                "operation"};
        for (size_t i = 0; i < sizeof(not_voice) / sizeof(not_voice[0]); i++) {
            cJSON *v = cJSON_CreateString(not_voice[i]);
            yk_proto_consent_t got;
            CHECK_CASE(parse_with_consent(held, v, &got) && got == YK_PROTO_CONSENT_NONE, not_voice[i]);
            cJSON_Delete(v);
        }
        static const char *const not_strings[] = {"1", "true", "false", "null", "[\"voice\"]", "{\"kind\":\"voice\"}"};
        for (size_t i = 0; i < sizeof(not_strings) / sizeof(not_strings[0]); i++) {
            cJSON *v = cJSON_Parse(not_strings[i]);
            yk_proto_consent_t got;
            CHECK_CASE(v && parse_with_consent(held, v, &got) && got == YK_PROTO_CONSENT_NONE, not_strings[i]);
            cJSON_Delete(v);
        }
        // "voice" with another code (or one only like it): as none
        static const char *const codes[] = {"SERVICE_BUSY", "CREDITS_EXHAUSTED", "RATE_LIMITED", "DEVICE_NOT_ACTIVATED",
                                            "CONSENT_ON_USE_REQUIRED", "operation_not_allowed", "OPERATION_NOT_ALLOWED_X",
                                            "OPERATION_NOT_ALLOWE", "OOPS"};
        for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
            cJSON *m2 = cJSON_Duplicate(held, 1);
            cJSON_ReplaceItemInObjectCaseSensitive(m2, "code", cJSON_CreateString(codes[i]));
            yk_proto_consent_t got;
            CHECK_CASE(parse_with_consent(m2, voice, &got) && got == YK_PROTO_CONSENT_NONE, codes[i]);
            cJSON_Delete(m2);
        }
        // With the other fields an error may carry, and with no turn: still voice, the rest as without it
        cJSON *more = cJSON_Duplicate(held, 1);
        cJSON_AddNumberToObject(more, "retryAfter", 30);
        cJSON_AddStringToObject(more, "resetAt", "2026-10-18T16:00:00.000Z");
        cJSON *credits = cJSON_AddObjectToObject(more, "credits");
        cJSON_AddNumberToObject(credits, "used", 3.2);
        cJSON_AddNumberToObject(credits, "remaining", 843);
        yk_proto_consent_t got;
        CHECK(parse_with_consent(more, voice, &got) && got == YK_PROTO_CONSENT_VOICE);
        cJSON_DeleteItemFromObjectCaseSensitive(more, "turn");
        CHECK(parse_with_consent(more, voice, &got) && got == YK_PROTO_CONSENT_VOICE);
        cJSON_Delete(more);
        // The key as it is written, nothing like it
        CHECK(parse("{\"type\":\"error\",\"turn\":\"t7f3a9c21d0e4b55\",\"code\":\"OPERATION_NOT_ALLOWED\",\"Consent\":\"voice\"}",
                    &m) == YK_SRV_ERROR);
        CHECK(m.consent == YK_PROTO_CONSENT_NONE);
        yk_proto_free(&m);
        // A message that breaks the schema otherwise is still not one, consent or not
        CHECK(parse("{\"type\":\"error\",\"turn\":\"t7f3a9c21d0e4b55\",\"code\":\"\",\"consent\":\"voice\"}", &m) == YK_SRV_INVALID);
        CHECK(m.consent == YK_PROTO_CONSENT_NONE);
        // Another message's consent is not read
        CHECK(parse("{\"type\":\"session.reauth\",\"consent\":\"voice\"}", &m) == YK_SRV_REAUTH);
        CHECK(m.consent == YK_PROTO_CONSENT_NONE);
        yk_proto_free(&m);
    }
    cJSON_Delete(voice);
}

/** One of the contract's reader cases: `msg` read as an error, its consent what `expect` says (readRefusalConsent:
 *  "voice", or null; a later value this firmware does not know yet is as none), the rest as without it. */
static void consent_case(const cJSON *msg, const cJSON *expect, const char *why, int *voice, int *none) {
    const char *want = str(expect, "consent");
    const bool is_voice = want && !strcmp(want, "voice");
    const cJSON *given = cJSON_GetObjectItemCaseSensitive(msg, "consent");
    yk_proto_consent_t got;
    CHECK_CASE(parse_with_consent(msg, given, &got), why);
    CHECK_CASE(got == (is_voice ? YK_PROTO_CONSENT_VOICE : YK_PROTO_CONSENT_NONE), why);
    if (is_voice) (*voice)++;
    else if (!want) (*none)++;
}

/** The contract's reader answers (#2778: fixtures/deployments.json voiceConsentSession and voiceConsentHttp, device-api.ts
 *  readRefusalConsent): voice only with OPERATION_NOT_ALLOWED and "voice" itself, none otherwise. Every one is still
 *  read as an error, those the platform never sends too (expect.valid false): the turn ends as with any other. An HTTP
 *  body says its code in `error` (yaykit_cloud reads those, V4); here the same rule on the session's error, its `error`
 *  as `code`. */
static void test_consent_cases(const char *deployments) {
    char *text = deployments ? read_file(deployments) : NULL;
    cJSON *doc = text ? cJSON_Parse(text) : NULL;
    const cJSON *session = cJSON_GetObjectItemCaseSensitive(doc, "voiceConsentSession"),
                *http = cJSON_GetObjectItemCaseSensitive(doc, "voiceConsentHttp"),
                *kinds = cJSON_GetObjectItemCaseSensitive(doc, "REFUSAL_CONSENTS"), *c, *k;
    if (!session || !http) {
        printf("yaykit_session: the voice consent cases left out (no fixtures/deployments.json)\n");
    } else {
        bool listed = false;  // the value this firmware reads is one the contract has
        cJSON_ArrayForEach(k, kinds) listed = listed || (cJSON_IsString(k) && !strcmp(k->valuestring, "voice"));
        CHECK(listed);
        int voice = 0, none = 0;
        const char *turn = NULL;
        cJSON_ArrayForEach(c, session) {
            const cJSON *m = cJSON_GetObjectItemCaseSensitive(c, "message");
            CHECK_CASE(!strcmp(str(m, "type"), "error"), str(c, "why"));
            if (!turn) turn = str(m, "turn");
            consent_case(m, cJSON_GetObjectItemCaseSensitive(c, "expect"), str(c, "why"), &voice, &none);
        }
        CHECK(voice >= 1 && none >= 3);
        voice = none = 0;
        cJSON_ArrayForEach(c, http) {
            const cJSON *body = cJSON_GetObjectItemCaseSensitive(c, "body");
            cJSON *m = cJSON_Duplicate(body, 1);
            cJSON_DeleteItemFromObjectCaseSensitive(m, "error");
            cJSON_AddStringToObject(m, "type", "error");
            if (turn) cJSON_AddStringToObject(m, "turn", turn);
            cJSON_AddStringToObject(m, "code", str(body, "error"));
            consent_case(m, cJSON_GetObjectItemCaseSensitive(c, "expect"), str(c, "why"), &voice, &none);
            cJSON_Delete(m);
        }
        CHECK(voice >= 1 && none >= 4);
    }
    cJSON_Delete(doc);
    free(text);
}

/** Beyond the fixtures: a step no firmware has yet is left out of the bits, and a hello without it says nothing. */
static void test_onboarding(void) {
    yk_srv_msg_t m;
    const char *far = "{\"type\":\"action\",\"id\":\"a1\",\"kind\":\"onboarding.set\",\"skip\":[7,16,200]}";
    CHECK(parse(far, &m) == YK_SRV_ACTION && m.kind == YK_ACT_ONBOARDING_SET && m.onb_skip == 1u << 7);
    yk_proto_free(&m);
    yk_proto_hello_t h = {.fw = "yay/0.2.0", .board = "tikpal-r1", .locale = "zh", .tz = "Asia/Shanghai", .battery_pct = 50,
                          .rssi = -50, .out_rate = 16000};
    char *json = yk_proto_hello(&h);
    CHECK(json && !strstr(json, "onboarding"));
    cJSON_free(json);
    h.onboarding_step = 256;
    CHECK(!yk_proto_hello(&h));
    const yk_proto_onb_report_t bad = {.step = 5, .result = "done", .seconds = YK_PROTO_ONB_SECONDS_MAX + 1};
    CHECK(!yk_proto_onboarding_step(&bad, 1, NULL));
    const yk_proto_onb_report_t most = {.step = 5, .result = "done", .seconds = YK_PROTO_ONB_SECONDS_MAX};
    json = yk_proto_onboarding_step(&most, 1, NULL);
    CHECK(json && strstr(json, "\"seconds\":2592000"));
    cJSON_free(json);
    CHECK(!yk_proto_onboarding_step(&most, 1, "zh"));  // hello's old word for it, not an account's language
    const yk_proto_onb_report_t none = {.step = 0, .result = "done"};
    CHECK(!yk_proto_onboarding_step(&none, 1, NULL));
}

/** Find my Tikpal (docs/design/find-my-tikpal.md 2.3), before the contract's own examples: the two actions and find.stopped. */
static void test_find(void) {
    yk_srv_msg_t m;
    CHECK(parse("{\"type\":\"action\",\"id\":\"f1\",\"kind\":\"find.ring\",\"seconds\":30}", &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_FIND_RING && m.seconds == 30 && !strcmp(m.action_id, "f1"));
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"f1\",\"kind\":\"find.ring\",\"seconds\":5}", &m) == YK_SRV_ACTION &&
          m.seconds == 5);
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"f1\",\"kind\":\"find.ring\",\"seconds\":300}", &m) == YK_SRV_ACTION &&
          m.seconds == 300);
    yk_proto_free(&m);
    // seconds out of 5..300, not whole, not a number, or missing: not a find.ring (ignored, no answer)
    static const char *const bad_ring[] = {"4", "301", "30.5", "\"30\"", "null"};
    for (size_t i = 0; i < sizeof(bad_ring) / sizeof(bad_ring[0]); i++) {
        char json[160];
        snprintf(json, sizeof(json), "{\"type\":\"action\",\"id\":\"f1\",\"kind\":\"find.ring\",\"seconds\":%s}",
                 bad_ring[i]);
        CHECK(parse(json, &m) == YK_SRV_INVALID);
        yk_proto_free(&m);
    }
    CHECK(parse("{\"type\":\"action\",\"id\":\"f1\",\"kind\":\"find.ring\"}", &m) == YK_SRV_INVALID);
    yk_proto_free(&m);
    // find.stop: the ring it stops, always named (the contract's FIND, #1845)
    CHECK(parse("{\"type\":\"action\",\"id\":\"s1\",\"kind\":\"find.stop\",\"find\":\"f1\"}", &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_FIND_STOP && !strcmp(m.find, "f1") && !strcmp(m.action_id, "s1"));
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"s1\",\"kind\":\"find.stop\"}", &m) == YK_SRV_INVALID);
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"s1\",\"kind\":\"find.stop\",\"find\":\"\"}", &m) == YK_SRV_INVALID);
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"s1\",\"kind\":\"find.stop\",\"find\":7}", &m) == YK_SRV_INVALID);
    yk_proto_free(&m);
    char id41[42];
    memset(id41, 'a', 41);
    id41[41] = 0;
    char json[200];
    snprintf(json, sizeof(json), "{\"type\":\"action\",\"id\":\"s1\",\"kind\":\"find.stop\",\"find\":\"%s\"}", id41);
    CHECK(parse(json, &m) == YK_SRV_INVALID);
    yk_proto_free(&m);
    // find.stopped: each way it stops; nothing else is a way
    static const char *const by[] = {"tap", "key", "app", "timeout", "other"};
    for (size_t i = 0; i < sizeof(by) / sizeof(by[0]); i++) {
        char *s = yk_proto_find_stopped("f1", by[i]);
        char want[80];
        snprintf(want, sizeof(want), "{\"type\":\"find.stopped\",\"id\":\"f1\",\"by\":\"%s\"}", by[i]);
        CHECK(s && !strcmp(s, want));
        cJSON_free(s);
    }
    CHECK(!yk_proto_find_stopped("f1", "unknown") && !yk_proto_find_stopped("f1", NULL) &&
          !yk_proto_find_stopped("", "tap") && !yk_proto_find_stopped(NULL, "tap") && !yk_proto_find_stopped(id41, "tap"));
    id41[40] = 0;
    char *s = yk_proto_find_stopped(id41, "key");
    CHECK(s != NULL);
    cJSON_free(s);
}

// The shared folders' grants (0.1.19, 方案 A): hello.ok's whole list, or nothing of it; folder.set's one.
static void test_folders(void) {
    yk_srv_msg_t m;
    char json[12000];
    const char *head = "{\"type\":\"hello.ok\",\"session\":\"s1\",\"time\":1792310400000,\"owner\":{\"nickname\":\"小鱼\"},"
                       "\"credits\":{\"remaining\":846,\"resetAt\":\"2026-10-18T16:00:00.000Z\"},\"dot\":true";
    // Two grants: taken, the kinds as bits, in their order whatever the list's.
    snprintf(json, sizeof(json), "%s,\"folders\":[{\"app\":\"bedtime-stories\",\"state\":\"granted\",\"folders\":"
             "[\"stories\",\"music\"],\"rev\":1},{\"app\":\"photo-frame\",\"state\":\"denied\",\"folders\":[\"pictures\"],"
             "\"rev\":3}]}", head);
    CHECK(parse(json, &m) == YK_SRV_HELLO_OK && m.folders_given && m.folder_count == 2);
    CHECK(m.folders && !strcmp(m.folders[0].app, "bedtime-stories") && m.folders[0].state == 1 &&
          m.folders[0].folders == 3 && m.folders[0].rev == 1);
    CHECK(m.folders && !strcmp(m.folders[1].app, "photo-frame") && m.folders[1].state == 2 && m.folders[1].folders == 4 &&
          m.folders[1].rev == 3);
    yk_proto_free(&m);
    CHECK(!m.folders);
    // None asked yet: an empty list, given.
    snprintf(json, sizeof(json), "%s,\"folders\":[]}", head);
    CHECK(parse(json, &m) == YK_SRV_HELLO_OK && m.folders_given && m.folder_count == 0 && !m.folders);
    yk_proto_free(&m);
    // Left out, or null (the platform waited on Yaykit too long): not given, the hello.ok all the same.
    snprintf(json, sizeof(json), "%s}", head);
    CHECK(parse(json, &m) == YK_SRV_HELLO_OK && !m.folders_given && !m.folders);
    yk_proto_free(&m);
    snprintf(json, sizeof(json), "%s,\"folders\":null}", head);
    CHECK(parse(json, &m) == YK_SRV_HELLO_OK && !m.folders_given);
    yk_proto_free(&m);
    // One that is not a grant, or an app twice: nothing of the list is taken (a part would clear the others).
    static const char *const bad[] = {
        "{\"app\":\"voice-notes\",\"state\":\"granted\",\"folders\":[\"recordings\"],\"rev\":1}",
        "{\"app\":\"a\",\"state\":\"granted\",\"folders\":[],\"rev\":1}",
        "{\"app\":\"Bad-App\",\"state\":\"granted\",\"folders\":[],\"rev\":1}",
        "{\"app\":\"photo-frame\",\"state\":\"ask\",\"folders\":[],\"rev\":1}",
        "{\"app\":\"photo-frame\",\"state\":\"granted\",\"folders\":[\"music\",\"music\"],\"rev\":1}",
        "{\"app\":\"photo-frame\",\"state\":\"granted\",\"folders\":[\"music\"],\"rev\":0}",
        "{\"app\":\"bedtime-stories\",\"state\":\"denied\",\"folders\":[],\"rev\":2}",  // the first one's app again
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        snprintf(json, sizeof(json), "%s,\"folders\":[{\"app\":\"bedtime-stories\",\"state\":\"granted\",\"folders\":"
                 "[\"music\"],\"rev\":1},%s]}", head, bad[i]);
        CHECK(parse(json, &m) == YK_SRV_HELLO_OK && !m.folders_given && !m.folders);
        yk_proto_free(&m);
    }
    // 64 with the longest slug: taken; 65: not.
    int at = snprintf(json, sizeof(json), "%s,\"folders\":[", head);
    for (int i = 0; i < 65; i++)
        at += snprintf(json + at, sizeof(json) - (size_t)at, "%s{\"app\":\"a%02dxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\","
                       "\"state\":\"granted\",\"folders\":[\"music\",\"stories\",\"pictures\",\"documents\"],\"rev\":%d}",
                       i ? "," : "", i, 1000000 + i);
    snprintf(json + at, sizeof(json) - (size_t)at, "]}");
    CHECK(parse(json, &m) == YK_SRV_HELLO_OK && !m.folders_given);
    yk_proto_free(&m);
    char *last = strrchr(json, '{');  // drop the 65th
    snprintf(last - 1, sizeof(json) - (size_t)(last - 1 - json), "]}");
    CHECK(parse(json, &m) == YK_SRV_HELLO_OK && m.folders_given && m.folder_count == 64 && m.folders[63].folders == 15 &&
          m.folders[63].rev == 1000063 && strlen(m.folders[63].app) == 41);
    yk_proto_free(&m);
    // folder.set: one grant, as the App left it.
    CHECK(parse("{\"type\":\"action\",\"id\":\"g1\",\"kind\":\"folder.set\",\"grant\":{\"app\":\"photo-frame\","
                "\"state\":\"denied\",\"folders\":[\"pictures\"],\"rev\":4}}", &m) == YK_SRV_ACTION);
    CHECK(m.kind == YK_ACT_FOLDER_SET && !strcmp(m.action_id, "g1") && !strcmp(m.folder.app, "photo-frame") &&
          m.folder.state == 2 && m.folder.folders == 4 && m.folder.rev == 4);
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"g1\",\"kind\":\"folder.set\"}", &m) == YK_SRV_INVALID);
    yk_proto_free(&m);
    CHECK(parse("{\"type\":\"action\",\"id\":\"g1\",\"kind\":\"folder.set\",\"grant\":{\"app\":\"photo-frame\","
                "\"state\":\"denied\",\"folders\":[\"recordings\"],\"rev\":4}}", &m) == YK_SRV_INVALID);
    yk_proto_free(&m);
}

/** yk_proto_time_offset (0.1.20): the local time at the offset, as z.iso.datetime({offset: true}) takes it. */
static void test_time_offset(void) {
    static const struct {
        int64_t ms;
        int off;
        const char *want;
    } cases[] = {
        {1794625083000LL, 480, "2026-11-14T10:58:03.000+08:00"},
        {1835499599999LL, -300, "2028-02-29T23:59:59.999-05:00"},
        {1798759800250LL, 0, "2026-12-31T23:30:00.250+00:00"},
        {1798761600000LL, 345, "2027-01-01T05:45:00.000+05:45"},
        {0LL, 0, "1970-01-01T00:00:00.000+00:00"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char out[32];
        yk_proto_time_offset(cases[i].ms, cases[i].off, out);
        CHECK_CASE(!strcmp(out, cases[i].want), cases[i].want);
        if (strcmp(out, cases[i].want)) printf("  got %s\n", out);
    }
    // ... and what it makes is what listen.start takes; a time without its offset is not.
    const yk_proto_clock_t c = {.at = {3, 52000}, .now = {3, 290000}};
    char at[32];
    char *ok = yk_proto_listen_start_note("t7f3a9c21d0e4b55", 300, yk_proto_time_offset(1763089083000LL, 480, at), &c);
    CHECK(ok != NULL && strstr(ok, "\"recordedAt\":\"2025-11-14T10:58:03.000+08:00\""));
    cJSON_free(ok);
    CHECK(yk_proto_listen_start_note("t7f3a9c21d0e4b55", 300, "2026-11-14T10:58:03", &c) == NULL);
    CHECK(yk_proto_offline_digest_say("not-a-uuid") == NULL);
}

/** Yay 0.2 (0.2 开发文档 3.2): the numbers as the contract has them, the times, and what the fixtures do not show. */
static void test_y02(const cJSON *y02, const cJSON *today_json) {
    // PROACTIVE_SAY and TODAY as the contract writes them out (device.json y02, today.json)
    const cJSON *say = cJSON_GetObjectItemCaseSensitive(y02, "proactiveSay");
    CHECK(say != NULL);
    CHECK(num(say, "minutes", -1) == YK_PROTO_PROACTIVE_MINUTES && num(say, "keepMinutes", -1) == YK_PROTO_PROACTIVE_KEEP_MINUTES);
    CHECK(num(say, "recordingHours", -1) == YK_PROTO_PROACTIVE_RECORDING_HOURS && num(say, "missedMax", -1) == YK_PROTO_PROACTIVE_MISSED_MAX);
    const cJSON *limits = cJSON_GetObjectItemCaseSensitive(today_json, "limits"), *skies = cJSON_GetObjectItemCaseSensitive(today_json, "skies");
    CHECK(limits != NULL && skies != NULL);
    CHECK(num(limits, "screenItems", -1) == YK_PROTO_TODAY_NEXT_MAX && num(limits, "titleChars", -1) == YK_PROTO_TODAY_TITLE_CHARS);
    CHECK(num(limits, "textChars", -1) == YK_PROTO_TODAY_TEXT_CHARS && num(limits, "placeChars", -1) == YK_PROTO_TODAY_PLACE_CHARS);
    CHECK(num(limits, "soonMinutes", -1) == YK_PROTO_TODAY_SOON_MINUTES && num(limits, "weatherStaleMinutes", -1) == YK_PROTO_TODAY_STALE_MINUTES);
    static const char *const SKIES[] = {"clear", "cloudy", "rain", "snow"};  // in yk_sky_t's order
    CHECK(cJSON_GetArraySize(skies) == YK_SKY_OTHER);
    for (int i = 0; i < YK_SKY_OTHER; i++) CHECK(!strcmp(cJSON_GetStringValue(cJSON_GetArrayItem(skies, i)), SKIES[i]));

    // Times both ways, offsets and fractions; not one z.iso.datetime({offset: true}) takes: false
    int64_t ms = 0;
    CHECK(yk_proto_time_ms("2026-11-18T15:00:00.000+08:00", &ms) && ms == 1794985200000LL);
    CHECK(yk_proto_time_ms("2026-11-18T07:00:00Z", &ms) && ms == 1794985200000LL);
    CHECK(yk_proto_time_ms("2026-11-18T02:00:00.5-05:00", &ms) && ms == 1794985200500LL);
    CHECK(yk_proto_time_ms("2027-01-01T05:45:00.0009+05:45", &ms) && ms == 1798761600000LL);
    CHECK(yk_proto_time_ms("2028-02-29T23:59:59.999-05:00", &ms) && ms == 1835499599999LL);
    for (int64_t t = 1795000000123LL; t < 1795000000123LL + 400LL * 86400000; t += 86400000LL * 7 + 3600123) {
        char out[32];
        int64_t back = 0;
        CHECK(yk_proto_time_ms(yk_proto_time_offset(t, 480, out), &back) && back == t);
        CHECK(yk_proto_time_ms(yk_proto_time_offset(t, -210, out), &back) && back == t);
    }
    static const char *const bad[] = {"2026-11-18T15:00:00", "2026-11-18 15:00:00Z", "2026-13-18T15:00:00Z",
                                      "2026-11-18T24:00:00Z", "2026-11-18T15:00:00+08", "2026-11-18T15:00:00.Z", "", NULL};
    for (int i = 0; bad[i]; i++) CHECK_CASE(!yk_proto_time_ms(bad[i], &ms), bad[i]);
    CHECK(!yk_proto_time_ms(NULL, &ms));

    // today: fields a later version adds are passed over, in the weather and the things too; the weather left out is -1
    yk_srv_msg_t m;
    const char *later = "{\"type\":\"today\",\"weather\":{\"sky\":\"rain\",\"night\":true,\"tempC\":-3,\"text\":\"小雨\","
                        "\"place\":\"朝阳区\",\"at\":\"2026-11-18T21:30:00+08:00\",\"wind\":3},\"next\":[{\"id\":\"s:a1\","
                        "\"at\":\"2026-11-18T22:00:00+08:00\",\"title\":\"吃药\",\"icon\":\"pill\"}],\"left\":0,"
                        "\"day\":\"2026-11-18\",\"hourly\":[]}";
    CHECK(yk_proto_parse(later, strlen(later), &m) == YK_SRV_TODAY && m.today && m.today->weather == 1 &&
          m.today->sky == YK_SKY_RAIN && m.today->night && m.today->temp_c == -3 && m.today->next_count == 1 &&
          m.today->next[0].has_title && !strcmp(m.today->next[0].title, "吃药") && m.today->weather_at_ms == 1795008600000LL);
    CHECK(m.text && !m.text[0]);
    yk_proto_free(&m);
    CHECK(!m.today);
    const char *bare = "{\"type\":\"today\",\"next\":[],\"left\":0,\"day\":\"2026-11-18\"}";
    CHECK(yk_proto_parse(bare, strlen(bare), &m) == YK_SRV_TODAY && m.today->weather == -1 && !m.today->next_count && !m.today->done[0]);
    yk_proto_free(&m);
    // ... but a thing without its at or title, a weather without its place, `next` not a list: not a today
    static const char *const broken[] = {
        "{\"type\":\"today\",\"next\":[{\"id\":\"s:a1\",\"title\":\"x\"}],\"left\":0,\"day\":\"2026-11-18\"}",
        "{\"type\":\"today\",\"next\":[{\"id\":\"s:a1\",\"at\":null}],\"left\":0,\"day\":\"2026-11-18\"}",
        "{\"type\":\"today\",\"weather\":{\"sky\":\"clear\",\"night\":false,\"tempC\":1,\"text\":\"晴\",\"at\":\"2026-11-18T09:30:00Z\"},"
        "\"next\":[],\"left\":0,\"day\":\"2026-11-18\"}",
        "{\"type\":\"today\",\"next\":{},\"left\":0,\"day\":\"2026-11-18\"}",
        "{\"type\":\"today\",\"next\":[],\"left\":100,\"day\":\"2026-11-18\"}",
        "{\"type\":\"today\",\"next\":[],\"left\":0,\"day\":\"2026-11-18\",\"done\":\"q:1\"}",
        NULL};
    for (int i = 0; broken[i]; i++) {
        CHECK_CASE(yk_proto_parse(broken[i], strlen(broken[i]), &m) == YK_SRV_INVALID && !m.today, broken[i]);
        yk_proto_free(&m);
    }

    // record.start; proactive.say; listen.start's room at its ends, and left out
    const char *rec = "{\"type\":\"action\",\"id\":\"a-rec-1\",\"kind\":\"record.start\",\"later\":1}";
    CHECK(yk_proto_parse(rec, strlen(rec), &m) == YK_SRV_ACTION && m.kind == YK_ACT_RECORD_START && !strcmp(m.action_id, "a-rec-1"));
    yk_proto_free(&m);
    char *out = yk_proto_proactive_say("7d2e4a9c-13f6-4b5c-8a48-f00000000010");
    CHECK(out && !strcmp(out, "{\"type\":\"proactive.say\",\"id\":\"7d2e4a9c-13f6-4b5c-8a48-f00000000010\"}"));
    cJSON_free(out);
    CHECK(yk_proto_proactive_say(NULL) == NULL && yk_proto_proactive_say("") == NULL);
    out = yk_proto_listen_start_room("t8a4b0d32e1f5c66", 300, NULL, false, YK_PROTO_ROOM_MAX);
    CHECK(out && strstr(out, "\"record\":{\"room\":1000}"));
    cJSON_free(out);
    out = yk_proto_listen_start_room("t8a4b0d32e1f5c66", 300, NULL, true, -1);
    CHECK(out && strstr(out, "\"held\":true") && strstr(out, "\"record\":{\"room\":-1}"));
    cJSON_free(out);
    out = yk_proto_listen_start_room("t8a4b0d32e1f5c66", 300, NULL, false, YK_PROTO_ROOM_NONE);
    char *as_before = yk_proto_listen_start("t8a4b0d32e1f5c66", 300, NULL);
    CHECK(out && as_before && !strcmp(out, as_before) && !strstr(out, "record"));
    cJSON_free(out);
    cJSON_free(as_before);
    CHECK(yk_proto_listen_start_room("t8a4b0d32e1f5c66", 300, NULL, false, YK_PROTO_ROOM_MAX + 1) == NULL);
    CHECK(yk_proto_listen_start_room("t8a4b0d32e1f5c66", 300, NULL, false, -3) == NULL);

    // inbox: proactive keeps its id and say; an older kind is read as before, with neither
    const char *line = "{\"type\":\"inbox\",\"kind\":\"proactive\",\"text\":\"三点了\",\"taskId\":null,"
                       "\"id\":\"7d2e4a9c-13f6-4b5c-8a48-f00000000011\",\"say\":false}";
    CHECK(yk_proto_parse(line, strlen(line), &m) == YK_SRV_INBOX && !strcmp(m.inbox_kind, "proactive") && !m.inbox_say &&
          !strcmp(m.inbox_id, "7d2e4a9c-13f6-4b5c-8a48-f00000000011") && !strcmp(m.text, "三点了"));
    yk_proto_free(&m);
    const char *done = "{\"type\":\"inbox\",\"kind\":\"done\",\"text\":\"做好了\",\"taskId\":\"t1\"}";
    CHECK(yk_proto_parse(done, strlen(done), &m) == YK_SRV_INBOX && !m.inbox_id[0] && !m.inbox_say);
    yk_proto_free(&m);
    // a reminder from a later platform with an id and say: received as before, neither read
    const char *later_reminder = "{\"type\":\"inbox\",\"kind\":\"reminder\",\"text\":\"三点了\",\"taskId\":null,"
                                 "\"id\":\"7d2e4a9c-13f6-4b5c-8a48-f00000000012\",\"say\":true}";
    CHECK(yk_proto_parse(later_reminder, strlen(later_reminder), &m) == YK_SRV_INBOX &&
          !strcmp(m.inbox_kind, "reminder") && !m.inbox_id[0] && !m.inbox_say && !strcmp(m.text, "三点了"));
    yk_proto_free(&m);
    // the same with ones that are no id or say at all: still the reminder
    const char *odd = "{\"type\":\"inbox\",\"kind\":\"dot\",\"text\":\"x\",\"taskId\":null,\"id\":7,\"say\":\"yes\"}";
    CHECK(yk_proto_parse(odd, strlen(odd), &m) == YK_SRV_INBOX && !m.inbox_id[0] && !m.inbox_say);
    yk_proto_free(&m);
    // proactive is still held to them: an id that is no UUID, or say that is no boolean, drops it
    const char *bad_id = "{\"type\":\"inbox\",\"kind\":\"proactive\",\"text\":\"x\",\"taskId\":null,\"id\":\"7\",\"say\":true}";
    CHECK(yk_proto_parse(bad_id, strlen(bad_id), &m) == YK_SRV_INVALID);
    yk_proto_free(&m);
    const char *bad_say = "{\"type\":\"inbox\",\"kind\":\"proactive\",\"text\":\"x\",\"taskId\":null,"
                          "\"id\":\"7d2e4a9c-13f6-4b5c-8a48-f00000000013\",\"say\":1}";
    CHECK(yk_proto_parse(bad_say, strlen(bad_say), &m) == YK_SRV_INVALID);
    yk_proto_free(&m);
    const char *newer = "{\"type\":\"inbox\",\"kind\":\"nudge\",\"text\":\"x\",\"taskId\":null}";
    CHECK(yk_proto_parse(newer, strlen(newer), &m) == YK_SRV_INVALID);  // a kind not known: dropped, as before
    yk_proto_free(&m);

    // Yay's three caps go into a hello as any other
    const char *caps[] = {"offline", YK_PROTO_CAP_TODAY, YK_PROTO_CAP_RECORD, YK_PROTO_CAP_PROACTIVE};
    yk_proto_hello_t h = {.fw = "yay/0.2.0", .board = "tikpal-r1", .locale = "zh", .tz = "Asia/Shanghai", .battery_pct = 72,
                          .rssi = -55, .out_rate = 16000, .caps = caps, .cap_count = 4};
    out = yk_proto_hello(&h);
    CHECK(out && strstr(out, "\"caps\":[\"offline\",\"today\",\"record\",\"proactive\"]"));
    cJSON_free(out);
    CHECK(!strcmp(yk_proto_type_name(YK_SRV_TODAY), "today"));
}

int main(int argc, char **argv) {
    char *text = argc > 1 ? read_file(argv[1]) : NULL;
    cJSON *fixtures = text ? cJSON_Parse(text) : NULL;
    CHECK(fixtures != NULL);
    if (fixtures) {
        test_device(cJSON_GetObjectItemCaseSensitive(fixtures, "valid"), true);
        test_device(cJSON_GetObjectItemCaseSensitive(fixtures, "invalid"), false);
        test_server(cJSON_GetObjectItemCaseSensitive(fixtures, "valid"), true);
        test_server(cJSON_GetObjectItemCaseSensitive(fixtures, "invalid"), false);
        test_frames(cJSON_GetObjectItemCaseSensitive(fixtures, "frames"));
        test_time_offset();
        test_practice_parts(cJSON_GetObjectItemCaseSensitive(fixtures, "valid"));
        test_consent(cJSON_GetObjectItemCaseSensitive(fixtures, "valid"));
        // The screensaver's own (docs/design/screensaver.md): Yay's hello and prefs with it, its actions, the answers.
        const cJSON *saver = cJSON_GetObjectItemCaseSensitive(fixtures, "screensaver");
        CHECK(saver != NULL);
        test_device(cJSON_GetObjectItemCaseSensitive(saver, "valid"), true);
        test_device(cJSON_GetObjectItemCaseSensitive(saver, "invalid"), false);
        test_server(cJSON_GetObjectItemCaseSensitive(saver, "valid"), true);
        test_server(cJSON_GetObjectItemCaseSensitive(saver, "invalid"), false);
        // The buddy's own: hello and prefs with it, buddy.set, the answers.
        const cJSON *buddy = cJSON_GetObjectItemCaseSensitive(fixtures, "buddy");
        CHECK(buddy != NULL);
        test_device(cJSON_GetObjectItemCaseSensitive(buddy, "valid"), true);
        test_device(cJSON_GetObjectItemCaseSensitive(buddy, "invalid"), false);
        test_server(cJSON_GetObjectItemCaseSensitive(buddy, "valid"), true);
        test_server(cJSON_GetObjectItemCaseSensitive(buddy, "invalid"), false);
        // Yay's first setup (登录与开箱 5.3): hello.onboarding, onboarding.step, onboarding.set.
        const cJSON *onboarding = cJSON_GetObjectItemCaseSensitive(fixtures, "onboarding");
        CHECK(onboarding != NULL);
        test_device(cJSON_GetObjectItemCaseSensitive(onboarding, "valid"), true);
        test_device(cJSON_GetObjectItemCaseSensitive(onboarding, "invalid"), false);
        test_server(cJSON_GetObjectItemCaseSensitive(onboarding, "valid"), true);
        test_server(cJSON_GetObjectItemCaseSensitive(onboarding, "invalid"), false);
        // Find my Tikpal's, once the contract has them (find-my-tikpal.md 2.3): find.ring, find.stop, find.stopped.
        const cJSON *find = cJSON_GetObjectItemCaseSensitive(fixtures, "find");
        if (find) {
            test_device(cJSON_GetObjectItemCaseSensitive(find, "valid"), true);
            test_device(cJSON_GetObjectItemCaseSensitive(find, "invalid"), false);
            test_server(cJSON_GetObjectItemCaseSensitive(find, "valid"), true);
            test_server(cJSON_GetObjectItemCaseSensitive(find, "invalid"), false);
        }
        // Yay 0.1.19's shared folders (#1936): Yay's hello with `folder`, folder.set, action.done, hello.ok's folders.
        const cJSON *folder = cJSON_GetObjectItemCaseSensitive(fixtures, "folder");
        CHECK(folder != NULL);
        test_device(cJSON_GetObjectItemCaseSensitive(folder, "valid"), true);
        test_device(cJSON_GetObjectItemCaseSensitive(folder, "invalid"), false);
        test_server(cJSON_GetObjectItemCaseSensitive(folder, "valid"), true);
        test_server(cJSON_GetObjectItemCaseSensitive(folder, "invalid"), false);
        // Yay 0.1.20's offline group (offlineNotes, binding, listen.start's clock, offline.digest): once the contract has it
        // (#2239).
        const cJSON *offline = cJSON_GetObjectItemCaseSensitive(fixtures, "offline");
        if (offline) {
            test_device(cJSON_GetObjectItemCaseSensitive(offline, "valid"), true);
            test_device(cJSON_GetObjectItemCaseSensitive(offline, "invalid"), false);
            test_server(cJSON_GetObjectItemCaseSensitive(offline, "valid"), true);
            test_server(cJSON_GetObjectItemCaseSensitive(offline, "invalid"), false);
        }
        // ... and listen.start's liveTurn (用户 11-18: kept after it went out live, its connection gone before stt.final).
        const cJSON *live = cJSON_GetObjectItemCaseSensitive(fixtures, "liveTurn");
        if (live) {
            test_device(cJSON_GetObjectItemCaseSensitive(live, "valid"), true);
            test_device(cJSON_GetObjectItemCaseSensitive(live, "invalid"), false);
        }
        // Yay 0.2's group (today, record.start, listen.start's record, inbox proactive, proactive.say), and today.json
        // beside it for TODAY's numbers.
        const cJSON *y02 = cJSON_GetObjectItemCaseSensitive(fixtures, "y02");
        CHECK(y02 != NULL);
        if (y02) {
            test_device(cJSON_GetObjectItemCaseSensitive(y02, "valid"), true);
            test_device(cJSON_GetObjectItemCaseSensitive(y02, "invalid"), false);
            test_server(cJSON_GetObjectItemCaseSensitive(y02, "valid"), true);
            test_server(cJSON_GetObjectItemCaseSensitive(y02, "invalid"), false);
            char path[1024];
            const char *slash = strrchr(argv[1], '/');
            snprintf(path, sizeof(path), "%.*stoday.json", slash ? (int)(slash - argv[1] + 1) : 0, argv[1]);
            char *today_text = read_file(path);
            cJSON *today_json = today_text ? cJSON_Parse(today_text) : NULL;
            CHECK(today_json != NULL);
            test_y02(y02, today_json);
            cJSON_Delete(today_json);
            free(today_text);
        }
        // Yay 0.1.19's follow group (locale.set, colorway.set, owner.colorway): once the contract has it (#1845).
        const cJSON *follow = cJSON_GetObjectItemCaseSensitive(fixtures, "follow");
        if (follow) {
            test_device(cJSON_GetObjectItemCaseSensitive(follow, "valid"), true);
            test_device(cJSON_GetObjectItemCaseSensitive(follow, "invalid"), false);
            test_server(cJSON_GetObjectItemCaseSensitive(follow, "valid"), true);
            test_server(cJSON_GetObjectItemCaseSensitive(follow, "invalid"), false);
        } else {
            printf("yaykit_session: the contract has no follow group yet (#1845): test_follow's own examples only\n");
        }
    }
    test_folders();
    test_find();
    test_onboarding();
    test_region(argc > 2 ? argv[2] : NULL);
    test_consent_cases(argc > 2 ? argv[2] : NULL);
    test_follow();
    test_saver();
    test_buddy();
    test_fields();
    test_modes();
    test_turns();
    test_backoff();
    cJSON_Delete(fixtures);
    free(text);
    printf("yaykit_session host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
