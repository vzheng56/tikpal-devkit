// Host tests of yk_api_stream.c (streamed recordings, yk_cloud_stream.h) against the platform's contract fixtures
// (packages/contracts/fixtures/device-api.json): the bodies the device builds equal the fixtures' requests, the
// answers it reads come out right, the refusals keep their names, the paths take file ids only.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    };
    char *body = yk_api_stream_body(&f);
    cJSON *got = cJSON_Parse(body);
    // Everything the fixture says, the body says alike.
    const cJSON *field;
    cJSON_ArrayForEach(field, v) CHECK(cJSON_Compare(field, at(got, field->string), true));
    // And nothing the contract does not take: transcribe is always said (enabled false when not wanted), no size.
    cJSON_ArrayForEach(field, got) {
        const bool known = at(v, field->string) || !strcmp(field->string, "transcribe");
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
    test_card_body_unchanged();
    test_answers();
    test_codes();
    test_paths();
    // The completion's time by the recording's length (新四号机 11-18: a 4 s note's held the flash task 51 s).
    CHECK(yk_api_stream_complete_ms(0) == 15000 && yk_api_stream_complete_ms(-1) == 15000);
    CHECK(yk_api_stream_complete_ms(4000) == 15133);
    CHECK(yk_api_stream_complete_ms(60000) == 17000);
    CHECK(yk_api_stream_complete_ms(3600000) == 130000 && yk_api_stream_complete_ms(36000000) == 130000);
    printf("yaykit_cloud stream host tests: %d checks over %d fixtures, %d failures\n", checks, read, failures);
    cJSON_Delete(fixtures);
    free(text);
    return failures ? 1 : 0;
}
