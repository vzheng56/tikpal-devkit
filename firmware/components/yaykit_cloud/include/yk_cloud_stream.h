// Streamed recordings (packages/contracts/src/device-api.ts FILE_STREAM; 录音卡 0.2): a file sent a chunk at a time
// through the device API while it is being recorded, never straight to storage, so the device lets each chunk go once
// the cloud keeps it.
//   1. yk_cloud_stream_register: POST /device/v1/files/:id/stream. Again after a restart it resumes: `kept` lists the
//      chunks the cloud has, `complete` says the cloud finished the file already.
//   2. yk_cloud_stream_chunk: PUT /device/v1/files/:id/chunks/:seq, the chunk's SHA-256 in x-yay-sha256. ESP_OK (200)
//      means it is kept. The same chunk again is 200.
//   3. yk_cloud_stream_complete: POST /device/v1/files/:id/stream/complete once its last chunk is kept.
// Refusals come in *code as the contract names them: CHUNK_SHA256_MISMATCH (send it again), CHUNK_CONFLICT,
// FILE_COMPLETE, FILE_INCOMPLETE (a chunk up to the last is missing: register again, send it, complete again),
// CHUNK_TOO_LARGE, STORAGE_LIMIT; the device API's (FILE_NOT_FOUND, FILE_GONE, DEVICE_NOT_ACTIVATED, APP_NOT_ENABLED,
// RATE_LIMITED…); a code it does not know: SERVER for a 5xx, REFUSED for a 4xx; NO_WIFI, NETWORK.
// The three share one HTTPS connection to the device API, kept open between calls (a new TLS handshake costs seconds
// and a burst of memory); it closes after 45 s unused, on an error, before a new token is fetched, or with
// yk_cloud_stream_close() (call it before anything else of yaykit_cloud's, /me say, so that two TLS sessions are never
// open at once). The completion waits up to 130 s for its answer (the platform joins the chunks first).
// Call them from one task. Added for the recording card; yaykit_account gains yk_account_token_cached(), nothing else
// in yaykit_cloud changes.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define YK_CLOUD_STREAM_KEPT_MAX 512  // an hour is about 130 chunks

/** A moment as the device counts it (Yay 0.1.20 开发文档 4.5): the start's number (bootId, one more each start) and
 *  the ms since that start. */
typedef struct {
    uint32_t boot_id;  // 0 … 2^31
    uint64_t mono_ms;  // 0 … 2^40
} yk_cloud_mono_t;

/** fileStreamRegister.clock (Yay 0.1.20 开发文档 2.1): when the recording began, the last time the clock was set (as
 *  the device counted it then, and the time it was set to), and now (at this request), so the platform can work out
 *  when it was recorded. */
typedef struct {
    yk_cloud_mono_t at;
    bool synced;                // last_sync and last_sync_at hold
    yk_cloud_mono_t last_sync;
    const char *last_sync_at;   // ISO 8601
    yk_cloud_mono_t now;
} yk_cloud_clock_t;

/** fileStreamRegister.batch: the files that waited when the network came back, so the platform sorts them out together
 *  once they are all in. */
typedef struct {
    const char *id;
    int index;  // 1 … count
    int count;
} yk_cloud_batch_t;

/** What POST …/stream says of a file (fileStreamRegister). */
typedef struct {
    const char *id;           // the file: "<recording.id>-<segment>"
    const char *recording;    // recording.id
    int segment;              // 1, 2… (a new file every 60 minutes)
    const char *recorded_at;  // ISO 8601 with its offset, or NULL when the clock was not set
    bool clock_trusted;
    bool transcribe;
    const char *language;     // NULL: the service's default
    const char *retention;    // NULL: the app's first
    // Yay 0.1.20 (开发文档 2.1), each left out of the body when NULL, so the recording card's request is 0.4.0's byte
    // for byte (test/test_stream.c); the device service takes them once its contract has them (strict before).
    const char *purpose;            // "offline_note" (Yay's); NULL: not said, transcribe
    const char *capture;            // "hold" | "button"
    const yk_cloud_clock_t *clock;
    const yk_cloud_batch_t *batch;
} yk_cloud_stream_t;

/** Its answer (fileStreamState). */
typedef struct {
    uint16_t kept[YK_CLOUD_STREAM_KEPT_MAX];  // the chunks the cloud keeps, in order (the first ones, when more)
    int nkept;
    int64_t received_bytes;
    bool complete;
} yk_cloud_stream_state_t;

/** POST …/stream/complete (fileStreamComplete). */
typedef struct {
    int last_seq;
    int64_t size;
    char sha256[65];  // lowercase hex
    int64_t duration_ms;
    int64_t speech_ms;  // -1: not known
} yk_cloud_stream_end_t;

esp_err_t yk_cloud_stream_register(const yk_cloud_stream_t *file, yk_cloud_stream_state_t *out, const char **code);
esp_err_t yk_cloud_stream_chunk(const char *id, int seq, const uint8_t *data, size_t len, const char sha256[65],
                                const char **code);
esp_err_t yk_cloud_stream_complete(const char *id, const yk_cloud_stream_end_t *end, const char **code);
/** Closes the kept connection (leaving the app, going offline). */
void yk_cloud_stream_close(void);
