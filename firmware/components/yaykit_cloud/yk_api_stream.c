#include "yk_api_stream.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "yk_api.h"

// packages/contracts/src/device-api.ts: FILE_STREAM, fileStreamRegister, fileStreamState, fileStreamComplete,
// fileStreamErrors. Shared by the device and test/test_stream.c.

/** {bootId, monoMs} into o (numbers: monoMs stays under 2^40, exact in a double). */
static void mono(cJSON *o, const yk_cloud_mono_t *m) {
    cJSON_AddNumberToObject(o, "bootId", (double)m->boot_id);
    cJSON_AddNumberToObject(o, "monoMs", (double)m->mono_ms);
}

char *yk_api_stream_body(const yk_cloud_stream_t *file) {
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "kind", "recording");
    cJSON_AddStringToObject(body, "contentType", "audio/ogg");
    if (file->recorded_at) cJSON_AddStringToObject(body, "recordedAt", file->recorded_at);
    cJSON_AddBoolToObject(body, "clockTrusted", file->recorded_at && file->clock_trusted);
    if (file->retention) cJSON_AddStringToObject(body, "retention", file->retention);
    cJSON *r = cJSON_AddObjectToObject(body, "recording");
    cJSON_AddStringToObject(r, "id", file->recording);
    cJSON_AddNumberToObject(r, "segment", file->segment > 0 ? file->segment : 1);
    cJSON *t = cJSON_AddObjectToObject(body, "transcribe");
    cJSON_AddBoolToObject(t, "enabled", file->transcribe);
    if (file->transcribe && file->language) cJSON_AddStringToObject(t, "language", file->language);
    // Yay 0.1.20's, after everything 0.4.0 says and only when given.
    if (file->purpose) cJSON_AddStringToObject(body, "purpose", file->purpose);
    if (file->capture) cJSON_AddStringToObject(body, "capture", file->capture);
    if (file->clock) {
        const yk_cloud_clock_t *k = file->clock;
        cJSON *c = cJSON_AddObjectToObject(body, "clock");
        mono(c, &k->at);
        if (k->synced && k->last_sync_at) {
            cJSON *l = cJSON_AddObjectToObject(c, "lastSync");
            mono(l, &k->last_sync);
            cJSON_AddStringToObject(l, "at", k->last_sync_at);
        }
        mono(cJSON_AddObjectToObject(c, "now"), &k->now);
    }
    if (file->batch && file->batch->id) {
        cJSON *b = cJSON_AddObjectToObject(body, "batch");
        cJSON_AddStringToObject(b, "id", file->batch->id);
        cJSON_AddNumberToObject(b, "index", file->batch->index);
        cJSON_AddNumberToObject(b, "count", file->batch->count);
    }
    char *out = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    return out;
}

bool yk_api_stream_state(const char *json, yk_cloud_stream_state_t *out) {
    memset(out, 0, sizeof(*out));
    cJSON *answer = cJSON_Parse(json ? json : "");
    const cJSON *chunks = cJSON_GetObjectItemCaseSensitive(answer, "chunks");
    const bool ok = cJSON_IsObject(answer) && cJSON_IsArray(chunks);
    if (ok) {
        const cJSON *c;
        cJSON_ArrayForEach(c, chunks) {
            if (cJSON_IsNumber(c) && c->valuedouble >= 1 && c->valuedouble <= 65535 &&
                out->nkept < YK_CLOUD_STREAM_KEPT_MAX)
                out->kept[out->nkept++] = (uint16_t)c->valuedouble;
        }
        const cJSON *received = cJSON_GetObjectItemCaseSensitive(answer, "receivedBytes");
        if (cJSON_IsNumber(received)) out->received_bytes = (int64_t)received->valuedouble;
        out->complete = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(answer, "complete"));
    }
    cJSON_Delete(answer);
    return ok;
}

int yk_api_stream_complete_ms(int64_t duration_ms) {
    const int64_t ms = 15000 + (duration_ms > 0 ? duration_ms / 30 : 0);
    return ms > 130000 ? 130000 : (int)ms;
}

char *yk_api_stream_end_body(const yk_cloud_stream_end_t *end) {
    cJSON *body = cJSON_CreateObject();
    cJSON_AddNumberToObject(body, "lastSeq", end->last_seq);
    cJSON_AddNumberToObject(body, "size", (double)end->size);
    cJSON_AddStringToObject(body, "sha256", end->sha256);
    cJSON_AddNumberToObject(body, "durationMs", (double)end->duration_ms);
    if (end->speech_ms >= 0) cJSON_AddNumberToObject(body, "speechMs", (double)end->speech_ms);
    char *out = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    return out;
}

const char *yk_api_stream_code(int status, const char *json) {
    // The contract's stream codes, and the device API's that rc_up acts on but yk_api_code does not know.
    static const char *const stream[] = {"CHUNK_SHA256_MISMATCH", "CHUNK_CONFLICT",  "FILE_COMPLETE",
                                         "FILE_INCOMPLETE",       "CHUNK_TOO_LARGE", "STORAGE_LIMIT",
                                         "APP_NOT_ENABLED"};
    cJSON *refusal = cJSON_Parse(json ? json : "");
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(refusal, "error");
    const char *code = NULL;
    if (cJSON_IsString(error))
        for (size_t i = 0; i < sizeof(stream) / sizeof(stream[0]) && !code; i++)
            if (!strcmp(error->valuestring, stream[i])) code = stream[i];
    if (!code) code = yk_api_code(cJSON_IsString(error) ? error->valuestring : NULL);
    if (!strcmp(code, "SERVER") && status >= 400 && status < 500) code = "REFUSED";
    cJSON_Delete(refusal);
    return code;
}

/** device-api.ts fileId: [A-Za-z0-9][A-Za-z0-9._-]{2,79}. */
static bool file_id(const char *id) {
    const size_t n = id ? strlen(id) : 0;
    if (n < 3 || n > 80) return false;
    for (size_t i = 0; i < n; i++) {
        const char c = id[i];
        const bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!alnum && (i == 0 || (c != '.' && c != '_' && c != '-'))) return false;
    }
    return true;
}

bool yk_api_stream_path(const char *id, int seq, char *out, size_t size) {
    if (!file_id(id)) return false;
    const int n = seq > 0   ? snprintf(out, size, "/device/v1/files/%s/chunks/%d", id, seq)
                  : seq < 0 ? snprintf(out, size, "/device/v1/files/%s/stream/complete", id)
                            : snprintf(out, size, "/device/v1/files/%s/stream", id);
    return n > 0 && (size_t)n < size;
}
