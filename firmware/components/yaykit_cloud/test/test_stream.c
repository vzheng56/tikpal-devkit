// Host tests of yk_api_stream.c (streamed recordings, yk_cloud_stream.h) against the platform's contract fixtures
// (packages/contracts/fixtures/device-api.json): the bodies the device builds equal the fixtures' requests, the
// answers it reads come out right, the refusals keep their names, the paths take file ids only; a refusal's consent
// (两套部署 · 语音单独同意 V4) against those and fixtures/deployments.json's voiceConsentHttp (the second argument).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../yk_api.h"  // yk_api_error_consent, yk_api_error_code
#include "../yk_api_stream.h"
#include "cJSON.h"

static int failures, checks;
static const char *current = "";
#define CHECK(cond)                                                             \
    do {                                                                        \
        checks++;                                                               \
        if (!(cond)) {                                                          \
            failures++;                                                         \
            printf("FAIL %s:%d %s (%s)\n", __FILE__, __LINE__, #cond, current); \
        }                                                                       \
    } while (0)

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) n = 0;
    if (buf) buf[n] = 0;
    fclose(f);
    return buf;
}

static const cJSON *at(const cJSON *o, const char *name) { return cJSON_GetObjectItemCaseSensitive(o, name); }
static const char *str(const cJSON *o, const char *name) {
    const cJSON *s = at(o, name);
    return cJSON_IsString(s) ? s->valuestring : NULL;
}

/** A fileStreamRegister fixture: the device's body for the same file says the same. */
static yk_cloud_mono_t mono_of(const cJSON *o) {
    return (yk_cloud_mono_t){(uint32_t)at(o, "bootId")->valuedouble, (uint64_t)at(o, "monoMs")->valuedouble};
}

static void stream_register(const cJSON *v) {
    const cJSON *rec = at(v, "recording"), *t = at(v, "transcribe");
    // Yay 0.1.20's fields, when the fixture has them (packages/contracts after the 0.1.20 contract).
    const cJSON *ck = at(v, "clock"), *ls = ck ? at(ck, "lastSync") : NULL, *bt = at(v, "batch");
    yk_cloud_clock_t clock = {0};
    if (ck) {
        clock.at = mono_of(ck);
        clock.synced = ls != NULL;
        if (ls) {
            clock.last_sync = mono_of(ls);
            clock.last_sync_at = str(ls, "at");
        }
        clock.now = mono_of(at(ck, "now"));
    }
    const yk_cloud_batch_t batch = {bt ? str(bt, "id") : NULL, bt ? (int)at(bt, "index")->valuedouble : 0,
                                    bt ? (int)at(bt, "count")->valuedouble : 0};
    // Yay 0.2's, when the fixture has them (y02).
    const cJSON *off = at(v, "offline"), *eb = at(v, "expectedBytes"), *pk = at(v, "peaks");
    static uint8_t peaks[YK_CLOUD_STREAM_PEAKS_MAX];
    int npeaks = 0;
    const cJSON *p;
    if (pk) cJSON_ArrayForEach(p, pk) peaks[npeaks++] = (uint8_t)p->valuedouble;
    const yk_cloud_stream_t f = {
        .id = "rec-1",
        .recording = str(rec, "id"),
        .segment = (int)at(rec, "segment")->valuedouble,
        .recorded_at = str(v, "recordedAt"),
        .clock_trusted = cJSON_IsTrue(at(v, "clockTrusted")),
        .transcribe = t && cJSON_IsTrue(at(t, "enabled")),
        .language = t ? str(t, "language") : NULL,
        .retention = str(v, "retention"),
        .purpose = str(v, "purpose"),
        .capture = str(v, "capture"),
        .clock = ck ? &clock : NULL,
        .batch = bt ? &batch : NULL,
        .offline = off ? (cJSON_IsTrue(off) ? 1 : 2) : 0,
        .expected_bytes = eb ? (uint32_t)eb->valuedouble : 0,
        .peaks = pk ? peaks : NULL,
        .npeaks = npeaks,
    };
    char *body = yk_api_stream_body(&f);
    cJSON *got = cJSON_Parse(body);
    // Everything the fixture says, the body says alike.
    const cJSON *field;
    cJSON_ArrayForEach(field, v) CHECK(cJSON_Compare(field, at(got, field->string), true));
    // And nothing the contract does not take: transcribe is always said (enabled false when not wanted), and
    // clockTrusted (false without recordedAt: 0.4.0's body), no size.
    cJSON_ArrayForEach(field, got) {
        const bool known = at(v, field->string) || !strcmp(field->string, "transcribe") ||
                           (!strcmp(field->string, "clockTrusted") && !at(v, "recordedAt") && cJSON_IsFalse(field));
        CHECK(known);
        if (!known) printf("  extra field %s\n", field->string);
    }
    CHECK(!at(got, "size") && !at(got, "sha256") && !strcmp(str(got, "kind"), "recording") &&
          !strcmp(str(got, "contentType"), "audio/ogg"));
    if (!t) CHECK(cJSON_IsFalse(at(at(got, "transcribe"), "enabled")));
    cJSON_Delete(got);
    cJSON_free(body);
}

/** The recording card's request is 0.4.0's, byte for byte (the device service is strict: a field more is a 400 before
 *  its contract has it): what 0.4.0's yk_api_stream_body made for these two files, saved before 0.1.20's fields. */
/** Yay 0.2's waveform: an hour's, printed whole and parsed back as it went, in one block (not 3,600 of cJSON's); a level
 *  over 100 said as 100; the rest of the body as without it. */
static void test_peaks(void) {
    current = "an hour's waveform";
    static uint8_t lv[3600];
    for (int k = 0; k < 3600; k++) lv[k] = (uint8_t)(k % 101);
    lv[7] = 255;
    const yk_cloud_stream_t f = {.id = "t0-cafe-1", .recording = "t0-cafe", .segment = 1, .purpose = "offline_note",
                                 .capture = "button", .offline = 2, .expected_bytes = 5062000, .peaks = lv,
                                 .npeaks = 3600};
    char *body = yk_api_stream_body(&f);
    cJSON *got = cJSON_Parse(body);
    const cJSON *pk = at(got, "peaks");
    CHECK(cJSON_IsArray(pk) && cJSON_GetArraySize(pk) == 3600);
    int k = 0, wrong = 0;
    const cJSON *p;
    cJSON_ArrayForEach(p, pk) wrong += p->valuedouble != (k == 7 ? 100 : k % 101), k++;
    CHECK(!wrong);
    CHECK(cJSON_IsFalse(at(got, "offline")) && at(got, "expectedBytes")->valuedouble == 5062000);
    CHECK(!strcmp(str(got, "capture"), "button") && !strcmp(str(at(got, "recording"), "id"), "t0-cafe"));
    CHECK(strstr(body, "\"peaks\":[0,1,2,3,4,5,6,100,8,") != NULL && body[strlen(body) - 1] == '}');
    cJSON_Delete(got);
    cJSON_free(body);
    // None of them: the body as 0.1.20's (offline 0, no size, no levels).
    const yk_cloud_stream_t g = {.id = "t0-cafe-1", .recording = "t0-cafe", .segment = 1, .purpose = "offline_note",
                                 .peaks = lv, .npeaks = 0};
    body = yk_api_stream_body(&g);
    CHECK(!strstr(body, "peaks") && !strstr(body, "offline\"") && !strstr(body, "expectedBytes"));
    cJSON_free(body);
    // More than the contract takes: cut to FILE_PEAKS_MAX.
    static uint8_t many[YK_CLOUD_STREAM_PEAKS_MAX + 5];
    const yk_cloud_stream_t h = {.id = "t0-cafe-1", .recording = "t0-cafe", .segment = 1, .peaks = many,
                                 .npeaks = YK_CLOUD_STREAM_PEAKS_MAX + 5, .offline = 1};
    body = yk_api_stream_body(&h);
    got = cJSON_Parse(body);
    CHECK(cJSON_GetArraySize(at(got, "peaks")) == YK_CLOUD_STREAM_PEAKS_MAX && cJSON_IsTrue(at(got, "offline")));
    cJSON_Delete(got);
    cJSON_free(body);
}

static void test_card_body_unchanged(void) {
    current = "the card's body as 0.4.0's";
    const yk_cloud_stream_t a = {.id = "20261115-101500-a1b2-1", .recording = "20261115-101500-a1b2", .segment = 1,
                                 .recorded_at = "2026-11-15T10:15:00Z", .clock_trusted = true, .transcribe = true};
    const yk_cloud_stream_t b = {.id = "t0-0badf00d-2", .recording = "t0-0badf00d", .segment = 2, .transcribe = false};
    char *x = yk_api_stream_body(&a), *y = yk_api_stream_body(&b);
    CHECK(!strcmp(x, "{\"kind\":\"recording\",\"contentType\":\"audio/ogg\",\"recordedAt\":\"2026-11-15T10:15:00Z\","
                     "\"clockTrusted\":true,\"recording\":{\"id\":\"20261115-101500-a1b2\",\"segment\":1},"
                     "\"transcribe\":{\"enabled\":true}}"));
    CHECK(!strcmp(y, "{\"kind\":\"recording\",\"contentType\":\"audio/ogg\",\"clockTrusted\":false,"
                     "\"recording\":{\"id\":\"t0-0badf00d\",\"segment\":2},\"transcribe\":{\"enabled\":false}}"));
    cJSON_free(x);
    cJSON_free(y);
    // Yay's: every field, the numbers whole at their limits.
    current = "Yay's body";
    const yk_cloud_clock_t k = {.at = {7, 1099511627775ull}, .synced = true, .last_sync = {6, 42},
                                .last_sync_at = "2026-11-15T09:00:00Z", .now = {2147483647u, 0}};
    const yk_cloud_batch_t bt = {"b-1", 2, 5};
    const yk_cloud_stream_t c = {.id = "t0-cafe-1", .recording = "t0-cafe", .segment = 1, .purpose = "offline_note",
                                 .capture = "hold", .clock = &k, .batch = &bt};
    char *z = yk_api_stream_body(&c);
    cJSON *got = cJSON_Parse(z);
    CHECK(!strcmp(str(got, "purpose"), "offline_note") && !strcmp(str(got, "capture"), "hold"));
    const cJSON *gc = at(got, "clock"), *gl = at(gc, "lastSync"), *gn = at(gc, "now"), *gb = at(got, "batch");
    CHECK(at(gc, "bootId")->valuedouble == 7 && at(gc, "monoMs")->valuedouble == 1099511627775.0);
    CHECK(strstr(z, "\"monoMs\":1099511627775") != NULL);  // printed whole
    CHECK(at(gl, "bootId")->valuedouble == 6 && at(gl, "monoMs")->valuedouble == 42 &&
          !strcmp(str(gl, "at"), "2026-11-15T09:00:00Z"));
    CHECK(at(gn, "bootId")->valuedouble == 2147483647.0 && at(gn, "monoMs")->valuedouble == 0);
    CHECK(!strcmp(str(gb, "id"), "b-1") && at(gb, "index")->valuedouble == 2 && at(gb, "count")->valuedouble == 5);
    cJSON_Delete(got);
    cJSON_free(z);
    const yk_cloud_clock_t k2 = {.at = {1, 5}, .now = {1, 9}};  // never set: no lastSync
    const yk_cloud_stream_t d = {.id = "t0-cafe-1", .recording = "t0-cafe", .segment = 1, .clock = &k2};
    z = yk_api_stream_body(&d);
    got = cJSON_Parse(z);
    CHECK(at(got, "clock") && !at(at(got, "clock"), "lastSync") && !at(got, "purpose") && !at(got, "batch"));
    cJSON_Delete(got);
    cJSON_free(z);
}

static void stream_state(const cJSON *v) {
    char *json = cJSON_PrintUnformatted(v);
    yk_cloud_stream_state_t s;
    CHECK(yk_api_stream_state(json, &s));
    const cJSON *chunks = at(v, "chunks");
    CHECK(s.nkept == cJSON_GetArraySize(chunks));
    for (int k = 0; k < s.nkept; k++) CHECK(s.kept[k] == (uint16_t)cJSON_GetArrayItem(chunks, k)->valuedouble);
    CHECK(s.received_bytes == (int64_t)at(v, "receivedBytes")->valuedouble);
    CHECK(s.complete == cJSON_IsTrue(at(v, "complete")));
    free(json);
}

/** A fileStreamComplete fixture: the device's body for the same end is that body. */
static void stream_complete(const cJSON *v) {
    yk_cloud_stream_end_t e = {
        .last_seq = (int)at(v, "lastSeq")->valuedouble,
        .size = (int64_t)at(v, "size")->valuedouble,
        .duration_ms = (int64_t)at(v, "durationMs")->valuedouble,
        .speech_ms = at(v, "speechMs") ? (int64_t)at(v, "speechMs")->valuedouble : -1,
    };
    snprintf(e.sha256, sizeof(e.sha256), "%s", str(v, "sha256"));
    char *body = yk_api_stream_end_body(&e);
    cJSON *got = cJSON_Parse(body);
    CHECK(cJSON_Compare(got, v, true));
    cJSON_Delete(got);
    cJSON_free(body);
}

static void test_answers(void) {
    current = "answers";
    yk_cloud_stream_state_t s;
    CHECK(!yk_api_stream_state("not json", &s));
    CHECK(!yk_api_stream_state("{\"id\":\"a-1\"}", &s));  // no chunks: not a state
    CHECK(!yk_api_stream_state(NULL, &s));
    // Unknown fields ignored; nonsense chunks skipped; more chunks than kept are cut to the first ones.
    char big[8192];
    int n = snprintf(big, sizeof(big), "{\"new\":1,\"chunks\":[0,-3,1.5e9");
    for (int k = 1; k <= 100; k++) n += snprintf(big + n, sizeof(big) - (size_t)n, ",%d", k);
    snprintf(big + n, sizeof(big) - (size_t)n, "],\"receivedBytes\":12,\"complete\":true}");
    CHECK(yk_api_stream_state(big, &s));
    static char many[16384];  // more than YK_CLOUD_STREAM_KEPT_MAX: the first ones
    n = snprintf(many, sizeof(many), "{\"chunks\":[1");
    for (int k = 2; k <= 600; k++) n += snprintf(many + n, sizeof(many) - (size_t)n, ",%d", k);
    snprintf(many + n, sizeof(many) - (size_t)n, "],\"receivedBytes\":1,\"complete\":false}");
    yk_cloud_stream_state_t m;
    CHECK(yk_api_stream_state(many, &m) && m.nkept == YK_CLOUD_STREAM_KEPT_MAX && m.kept[511] == 512);
    CHECK(s.nkept == 100 && s.kept[0] == 1 && s.kept[99] == 100 && s.complete && s.received_bytes == 12);
}

static void test_codes(void) {
    current = "codes";
    static const char *const stream[] = {"CHUNK_SHA256_MISMATCH", "CHUNK_CONFLICT", "FILE_COMPLETE",
                                         "CHUNK_TOO_LARGE", "STORAGE_LIMIT"};
    for (size_t i = 0; i < sizeof(stream) / sizeof(stream[0]); i++) {
        char json[96];
        snprintf(json, sizeof(json), "{\"error\":\"%s\",\"message\":\"x\"}", stream[i]);
        CHECK(!strcmp(yk_api_stream_code(409, json), stream[i]));
    }
    CHECK(!strcmp(yk_api_stream_code(404, "{\"error\":\"FILE_NOT_FOUND\"}"), "FILE_NOT_FOUND"));
    CHECK(!strcmp(yk_api_stream_code(410, "{\"error\":\"FILE_GONE\"}"), "FILE_GONE"));
    CHECK(!strcmp(yk_api_stream_code(401, "{\"error\":\"DEVICE_NOT_ACTIVATED\"}"), "DEVICE_NOT_ACTIVATED"));
    CHECK(!strcmp(yk_api_stream_code(503, "{\"error\":\"SERVICE_BUSY\"}"), "SERVICE_BUSY"));
    CHECK(!strcmp(yk_api_stream_code(403, "{\"error\":\"APP_NOT_ENABLED\"}"), "APP_NOT_ENABLED"));
    CHECK(!strcmp(yk_api_stream_code(409, "{\"error\":\"FILE_INCOMPLETE\",\"missingChunks\":[3]}"),
                  "FILE_INCOMPLETE"));
    // The platform joining it (Yay 0.2 F6c, contract #2448): its own name, not REFUSED (a few seconds and ask again).
    CHECK(!strcmp(yk_api_stream_code(409, "{\"error\":\"FILE_BUSY\"}"), "FILE_BUSY"));
    // A code it does not know: the server's trouble (5xx) or this file's (4xx).
    CHECK(!strcmp(yk_api_stream_code(400, "{\"error\":\"SOMETHING_NEW\"}"), "REFUSED"));
    CHECK(!strcmp(yk_api_stream_code(500, "{\"error\":\"SOMETHING_NEW\"}"), "SERVER"));
    CHECK(!strcmp(yk_api_stream_code(502, "<html>bad gateway</html>"), "SERVER"));
    CHECK(!strcmp(yk_api_stream_code(422, NULL), "REFUSED"));
    CHECK(!strcmp(yk_api_stream_code(500, NULL), "SERVER"));
}

static void test_paths(void) {
    current = "paths";
    char p[128];
    CHECK(yk_api_stream_path("20261101-091530-a1b2-1", 0, p, sizeof(p)) &&
          !strcmp(p, "/device/v1/files/20261101-091530-a1b2-1/stream"));
    CHECK(yk_api_stream_path("20261101-091530-a1b2-1", 7, p, sizeof(p)) &&
          !strcmp(p, "/device/v1/files/20261101-091530-a1b2-1/chunks/7"));
    CHECK(yk_api_stream_path("t0-deadbeef-2", -1, p, sizeof(p)) &&
          !strcmp(p, "/device/v1/files/t0-deadbeef-2/stream/complete"));
    CHECK(!yk_api_stream_path("../x", 0, p, sizeof(p)));
    CHECK(!yk_api_stream_path("a/b-1", 0, p, sizeof(p)));
    CHECK(!yk_api_stream_path("ab", 0, p, sizeof(p)));  // at least three characters
    CHECK(!yk_api_stream_path("-ab", 0, p, sizeof(p)));
    CHECK(!yk_api_stream_path("abc?x=1", 0, p, sizeof(p)));
    CHECK(!yk_api_stream_path(NULL, 0, p, sizeof(p)));
    char tiny[20];
    CHECK(!yk_api_stream_path("20261101-091530-a1b2-1", 0, tiny, sizeof(tiny)));  // does not fit
}

// ---------------------------------------------------------------- 两套部署 · 语音单独同意 V4: a refusal's consent

/** `body` printed as it is, or with its `consent` left out. */
static char *printed(const cJSON *body, bool without) {
    cJSON *b = cJSON_Duplicate(body, 1);
    if (without) cJSON_DeleteItemFromObjectCaseSensitive(b, "consent");
    char *out = cJSON_PrintUnformatted(b);
    cJSON_Delete(b);
    return out;
}

/** One refusal's body (an object): its consent what `want` says (readRefusalConsent), none with it left out; and its
 *  code, at any status, the same with it and without, byte for byte (an upload so refused waits as before:
 *  OPERATION_NOT_ALLOWED stays itself, in yk_rs_up's GLOBAL). */
static void consent_is(const cJSON *body, yk_cloud_consent_t want) {
    char *with = printed(body, false), *plain = printed(body, true);
    CHECK(yk_api_error_consent(with) == want);
    CHECK(yk_api_error_consent(plain) == YK_CLOUD_CONSENT_NONE);
    static const int statuses[] = {400, 401, 403, 404, 409, 410, 422, 500, 503};
    for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); i++)
        CHECK(!strcmp(yk_api_stream_code(statuses[i], with), yk_api_stream_code(statuses[i], plain)));
    CHECK(!strcmp(yk_api_error_code(with), yk_api_error_code(plain)));
    cJSON_free(with);
    cJSON_free(plain);
}

/** The body {"error":`code`,"consent":<value>} (`value` JSON text; NULL: no consent at all). */
static yk_cloud_consent_t consent_of(const char *code, const char *value) {
    char json[160];
    if (value) snprintf(json, sizeof(json), "{\"error\":\"%s\",\"consent\":%s}", code, value);
    else snprintf(json, sizeof(json), "{\"error\":\"%s\"}", code);
    return yk_api_error_consent(json);
}

/** A refusal's consent (device-api.ts deviceError, consent.ts VOICE_CONSENT_GATES, #2740), as readRefusalConsent reads it
 *  (#2778): the contract's own examples (device-api.json, every deviceError valid or not; deployments.json
 *  voiceConsentHttp), then beyond them. */
static void test_consent(const cJSON *fixtures, const char *deployments) {
    int voice = 0, none = 0;
    const char *const sets[] = {"valid", "invalid"};
    for (int s = 0; s < 2; s++) {
        const cJSON *c;
        cJSON_ArrayForEach(c, at(fixtures, sets[s])) {
            if (!str(c, "schema") || strcmp(str(c, "schema"), "deviceError")) continue;
            const cJSON *v = at(c, "value");
            current = str(c, "name") ? str(c, "name") : str(v, "error") ? str(v, "error") : "deviceError";
            if (!cJSON_IsObject(v)) {
                char *json = cJSON_PrintUnformatted(v);
                CHECK(yk_api_error_consent(json) == YK_CLOUD_CONSENT_NONE);
                cJSON_free(json);
                continue;
            }
            const bool is_voice = str(v, "error") && !strcmp(str(v, "error"), "OPERATION_NOT_ALLOWED") &&
                                  str(v, "consent") && !strcmp(str(v, "consent"), "voice");
            consent_is(v, is_voice ? YK_CLOUD_CONSENT_VOICE : YK_CLOUD_CONSENT_NONE);
            CHECK(!is_voice || s == 0);  // the platform's own: only a valid one says voice
            if (is_voice) {
                char *json = cJSON_PrintUnformatted(v);
                CHECK(!strcmp(yk_api_stream_code(403, json), "OPERATION_NOT_ALLOWED"));  // the upload's GLOBAL: all wait
                cJSON_free(json);
                voice++;
            } else if (at(v, "consent")) {
                none++;  // a consent there that is not voice beside OPERATION_NOT_ALLOWED: none
            }
        }
    }
    current = "device-api.json consent";
    CHECK(voice >= 1 && none >= 2);  // "held for want of voice consent"; "consent other than voice", "… another code"
    // deployments.json: the HTTP refusals the readers answer alike (readRefusalConsent), the platform's or not
    char *text = deployments ? slurp(deployments) : NULL;
    cJSON *doc = text ? cJSON_Parse(text) : NULL;
    const cJSON *http = at(doc, "voiceConsentHttp"), *kinds = at(doc, "REFUSAL_CONSENTS"), *c;
    if (!http) {
        printf("yaykit_cloud stream: the voiceConsentHttp cases left out (no fixtures/deployments.json)\n");
    } else {
        bool listed = false;  // the value this firmware reads is one the contract has
        cJSON_ArrayForEach(c, kinds) listed = listed || (cJSON_IsString(c) && !strcmp(c->valuestring, "voice"));
        current = "REFUSAL_CONSENTS";
        CHECK(listed);
        voice = none = 0;
        cJSON_ArrayForEach(c, http) {
            current = str(c, "why") ? str(c, "why") : "voiceConsentHttp";
            const cJSON *body = at(c, "body"), *expect = at(c, "expect");
            const bool want = str(expect, "consent") && !strcmp(str(expect, "consent"), "voice");
            CHECK(cJSON_IsObject(body) && at(expect, "consent"));
            if (!cJSON_IsObject(body)) continue;
            consent_is(body, want ? YK_CLOUD_CONSENT_VOICE : YK_CLOUD_CONSENT_NONE);
            if (want) voice++;
            else none++;
        }
        current = "voiceConsentHttp";
        CHECK(voice >= 1 && none >= 4);
    }
    cJSON_Delete(doc);
    free(text);
    // Beyond the fixtures. "voice" itself only: as none otherwise, the code as ever.
    current = "consent beyond the fixtures";
    CHECK(consent_of("OPERATION_NOT_ALLOWED", "\"voice\"") == YK_CLOUD_CONSENT_VOICE);
    static const char *const not_voice[] = {"\"Voice\"", "\"VOICE\"", "\"voice \"", "\" voice\"", "\"voic\"", "\"voices\"",
                                            "\"\"", "\"apns\"", "\"apple\"", "\"camera\"", "1", "true", "false", "null",
                                            "[\"voice\"]", "{\"kind\":\"voice\"}"};
    for (size_t i = 0; i < sizeof(not_voice) / sizeof(not_voice[0]); i++) {
        current = not_voice[i];
        CHECK(consent_of("OPERATION_NOT_ALLOWED", not_voice[i]) == YK_CLOUD_CONSENT_NONE);
    }
    static const char *const codes[] = {"SERVICE_BUSY", "CREDITS_EXHAUSTED", "ACCOUNT_FROZEN", "DEVICE_NOT_ACTIVATED",
                                        "APP_NOT_ENABLED", "OFFLINE_NOTES_OFF", "PURPOSE_NOT_ALLOWED", "STORAGE_LIMIT",
                                        "CONSENT_ON_USE_REQUIRED", "operation_not_allowed", "OPERATION_NOT_ALLOWED_X",
                                        "OPERATION_NOT_ALLOWE", "OOPS"};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        current = codes[i];
        CHECK(consent_of(codes[i], "\"voice\"") == YK_CLOUD_CONSENT_NONE);
    }
    current = "consent: the body's shape";
    CHECK(consent_of("OPERATION_NOT_ALLOWED", NULL) == YK_CLOUD_CONSENT_NONE);
    CHECK(yk_api_error_consent("{\"error\":\"OPERATION_NOT_ALLOWED\",\"Consent\":\"voice\"}") == YK_CLOUD_CONSENT_NONE);
    CHECK(yk_api_error_consent("{\"Error\":\"OPERATION_NOT_ALLOWED\",\"consent\":\"voice\"}") == YK_CLOUD_CONSENT_NONE);
    CHECK(yk_api_error_consent("{\"consent\":\"voice\"}") == YK_CLOUD_CONSENT_NONE);
    CHECK(yk_api_error_consent("{\"error\":1,\"consent\":\"voice\"}") == YK_CLOUD_CONSENT_NONE);
    CHECK(yk_api_error_consent("[{\"error\":\"OPERATION_NOT_ALLOWED\",\"consent\":\"voice\"}]") == YK_CLOUD_CONSENT_NONE);
    CHECK(yk_api_error_consent("\"voice\"") == YK_CLOUD_CONSENT_NONE);
    CHECK(yk_api_error_consent("{\"error\":\"OPERATION_NOT_ALLOWED\",\"consent\":\"voice\"") == YK_CLOUD_CONSENT_NONE);
    CHECK(yk_api_error_consent("<html>forbidden</html>") == YK_CLOUD_CONSENT_NONE);
    CHECK(yk_api_error_consent("") == YK_CLOUD_CONSENT_NONE && yk_api_error_consent(NULL) == YK_CLOUD_CONSENT_NONE);
    // With the details another refusal of it may carry, and a message: still voice, the code as without it.
    const char *more = "{\"error\":\"OPERATION_NOT_ALLOWED\",\"message\":\"x\",\"app\":\"yay\",\"operation\":\"files\","
                       "\"consent\":\"voice\",\"later\":{\"a\":1}}";
    CHECK(yk_api_error_consent(more) == YK_CLOUD_CONSENT_VOICE);
    CHECK(!strcmp(yk_api_stream_code(403, more), "OPERATION_NOT_ALLOWED"));
    CHECK(!strcmp(yk_api_error_code(more), "OPERATION_NOT_ALLOWED"));
}

int main(int argc, char **argv) {
    char *text = slurp(argc > 1 ? argv[1] : "../../../../../packages/contracts/fixtures/device-api.json");
    if (!text) {
        printf("no fixtures\n");
        return 1;
    }
    cJSON *fixtures = cJSON_Parse(text);
    const cJSON *c;
    int read = 0;
    cJSON_ArrayForEach(c, at(fixtures, "valid")) {
        const char *schema = str(c, "schema");
        const cJSON *v = at(c, "value");
        current = str(c, "name") ? str(c, "name") : schema;
        if (!strcmp(schema, "fileStreamRegister")) stream_register(v);
        else if (!strcmp(schema, "fileStreamState")) stream_state(v);
        else if (!strcmp(schema, "fileStreamComplete")) stream_complete(v);
        else continue;
        read++;
    }
    CHECK(read >= 6);  // two of each, as the contract has them
    // Yay 0.2's (y02): 记一句 online with its size and waveform, a long one recorded first, offline said.
    int y02 = 0;
    cJSON_ArrayForEach(c, at(at(fixtures, "y02"), "valid")) {
        if (strcmp(str(c, "schema"), "fileStreamRegister")) continue;
        current = str(c, "name");
        stream_register(at(c, "value"));
        y02++;
    }
    CHECK(y02 >= 3);
    read += y02;
    test_peaks();
    test_card_body_unchanged();
    test_answers();
    test_codes();
    test_paths();
    test_consent(fixtures, argc > 2 ? argv[2] : NULL);  // deployments.json's voiceConsentHttp (两套部署, V4)
    // The completion's time by the recording's length (新四号机 11-18: a 4 s note's held the flash task 51 s).
    CHECK(yk_api_stream_complete_ms(0) == 15000 && yk_api_stream_complete_ms(-1) == 15000);
    CHECK(yk_api_stream_complete_ms(4000) == 15133);
    CHECK(yk_api_stream_complete_ms(60000) == 17000);
    CHECK(yk_api_stream_complete_ms(3600000) == 130000 && yk_api_stream_complete_ms(36000000) == 130000);
    // A step that can be stopped (F6c): 20 s at most, the answer awaited in half seconds, the stop first.
    CHECK(yk_api_stream_complete_stoppable_ms(4000) == 15133 && yk_api_stream_complete_stoppable_ms(3600000) == 20000);
    CHECK(yk_api_stream_wait(0, 20000, false) == YK_API_WAIT_ON && yk_api_stream_wait(19999, 20000, false) == YK_API_WAIT_ON);
    CHECK(yk_api_stream_wait(20000, 20000, false) == YK_API_WAIT_TIMEOUT);
    CHECK(yk_api_stream_wait(0, 20000, true) == YK_API_WAIT_STOP && yk_api_stream_wait(25000, 20000, true) == YK_API_WAIT_STOP);
    printf("yaykit_cloud stream host tests: %d checks over %d fixtures, %d failures\n", checks, read, failures);
    cJSON_Delete(fixtures);
    free(text);
    return failures ? 1 : 0;
}
