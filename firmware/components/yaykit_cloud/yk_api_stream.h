// Pure C (cJSON only) parts of the streamed recordings (yk_cloud_stream.h): the requests' bodies and paths, the
// answers, the refusals. Shared by the device and test/test_stream.c, which checks them against
// packages/contracts/fixtures/device-api.json. Readers ignore fields they do not know.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "yk_cloud_stream.h"

/** The body of POST /files/:id/stream (free with cJSON_free). */
char *yk_api_stream_body(const yk_cloud_stream_t *file);
/** A fileStreamState answer. False when it is not one. */
bool yk_api_stream_state(const char *json, yk_cloud_stream_state_t *out);
/** How long POST …/stream/complete may take, in ms, for a recording `duration_ms` long: the platform joins the chunks,
 *  an hour's in 120 s at most, a short one's at once; so 15 s and a second more for each 30 s of it, at most 130 s.
 *  A request that long without an answer lets go and is tried again later (yk_rs_up): an upload whose network went
 *  mid-request held the recordings' flash task 51 s (新四号机 11-18, a 4 s note's completion). */
int yk_api_stream_complete_ms(int64_t duration_ms);
/** The body of POST /files/:id/stream/complete (free with cJSON_free). */
char *yk_api_stream_end_body(const yk_cloud_stream_end_t *end);
/** A refusal (HTTP `status`, body {"error": CODE}): the contract's stream codes as they are, else as yk_api_code says;
 *  a code it does not know is SERVER for a 5xx (everything waits) and REFUSED for a 4xx (the file waits alone). */
const char *yk_api_stream_code(int status, const char *json);
/** "/device/v1/files/<id>/stream" (seq 0), "…/chunks/<seq>" (seq > 0), "…/stream/complete" (seq < 0). False when
 *  the id is not a file id (device-api.ts fileId) or it does not fit. */
bool yk_api_stream_path(const char *id, int seq, char *out, size_t size);
