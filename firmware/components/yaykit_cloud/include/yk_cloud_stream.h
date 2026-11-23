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
// CHUNK_TOO_LARGE, STORAGE_LIMIT, FILE_BUSY (the platform is joining it: not a refusal, ask again in a few seconds);
// STOPPED and TIMEOUT (yk_cloud_stream_set_stop); the device API's (FILE_NOT_FOUND, FILE_GONE, DEVICE_NOT_ACTIVATED, APP_NOT_ENABLED,
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
#include "yk_cloud_types.h"  // yk_cloud_consent_t

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
    const char *capture;            // "hold" | "button" | "note" (0.2)
    const yk_cloud_clock_t *clock;
    const yk_cloud_batch_t *batch;
    // Yay 0.2 (开发文档 3.3), each left out when 0 / NULL, as 0.1.20's are: a service before its contract refuses them.
    uint8_t offline;                // 0 left out; 1 offline true, 2 false (recorded online)
    uint32_t expected_bytes;        // the whole file's size, known before it is sent (it has ended); 0 left out
    const uint8_t *peaks;           // its waveform, one level a second 0..100 (FILE_PEAKS_MAX at most); NULL left out
    int npeaks;
} yk_cloud_stream_t;

#define YK_CLOUD_STREAM_PEAKS_MAX 7200  // device-api.ts FILE_PEAKS_MAX: two hours of one a second

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

/** Yay 0.2 (F6c): a step that can be stopped. `stop` is asked before a call opens its connection (none opens: no TLS
 *  handshake begins), between the body's pieces of 4 KB, and every half second at most while the answer is awaited;
 *  saying true, the call ends there, its connection closed, with code STOPPED. What the cloud may have kept by then is
 *  as it is: the same chunk again is 200 and changes nothing (its bytes are the file's at that seq), a completion is
 *  learned by registering again (fileStreamState.complete). With it set, a completion's answer is awaited
 *  YK_CLOUD_STREAM_COMPLETE_STOPPABLE_MS at most, then TIMEOUT (the platform joins the chunks all the same). NULL (as
 *  ever, the recording card's): the calls and their waits as 0.4.0's. From the task that makes the calls. */
#define YK_CLOUD_STREAM_COMPLETE_STOPPABLE_MS 20000
void yk_cloud_stream_set_stop(bool (*stop)(void));

esp_err_t yk_cloud_stream_register(const yk_cloud_stream_t *file, yk_cloud_stream_state_t *out, const char **code);
esp_err_t yk_cloud_stream_chunk(const char *id, int seq, const uint8_t *data, size_t len, const char sha256[65],
                                const char **code);
esp_err_t yk_cloud_stream_complete(const char *id, const yk_cloud_stream_end_t *end, const char **code);
/** Closes the kept connection (leaving the app, going offline). */
void yk_cloud_stream_close(void);
/** 两套部署 · 语音单独同意 V4: what the last of the three calls' refusal said it was held for want of (yk_api_error_consent
 *  on the answer's body): YK_CLOUD_CONSENT_VOICE when it was 403 OPERATION_NOT_ALLOWED with consent "voice" (the account
 *  has not agreed to voice there: yaykit_recstore's upload waits as for any OPERATION_NOT_ALLOWED and says why), NONE
 *  for anything else: a success, no answer, another refusal, one without it. Each call's *code is as before; this only
 *  adds why. From the task that makes the calls, right after one. */
yk_cloud_consent_t yk_cloud_stream_consent(void);
