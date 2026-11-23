// Pure C (cJSON only) parts of the device API v1 client: reading answers and building requests, shared by the
// device and the host tests, which check them against packages/contracts/fixtures/device-api.json.
// Every reader ignores fields it does not know (docs/design/yay-device.md §13.3).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "yk_cloud_types.h"

/** Fills `out` from a GET /device/v1/me answer; `*capabilities` gets a copy of its capabilities (free it with
 *  cJSON_Delete) and `*settings` a copy of its settings. False when the answer is not a JSON object. */
bool yk_api_me(const char *json, yk_cloud_me_t *out, cJSON **capabilities, cJSON **settings);
/** Whether `capability` is among `capabilities`. */
bool yk_api_has(const cJSON *capabilities, const char *capability);
/** A number in a capability's limits, or `fallback`. */
int yk_api_limit(const cJSON *capabilities, const char *capability, const char *key, int fallback);
/** Whether a list in a capability's limits (asr "languages", files "contentTypes"…) holds `value`. */
bool yk_api_allows(const cJSON *capabilities, const char *capability, const char *list, const char *value);
/** A `credits` object of a metered answer (charged is 0 when absent). */
void yk_api_charge(const cJSON *credits, yk_cloud_charge_t *out);
/** A POST /asr answer. */
bool yk_api_asr(const char *json, yk_cloud_asr_t *out);

/** The body of POST /llm/chat (free with cJSON_free). */
char *yk_api_chat_body(const yk_cloud_chat_t *request);
/** One /llm/chat event: "delta" calls `on_delta`; "done" fills `done`. Returns false for "error" (with `*code`). */
bool yk_api_chat_event(const char *event, const cJSON *data, yk_cloud_delta_fn on_delta, void *ctx,
                       yk_cloud_chat_done_t *done, const char **code);

/** The body of POST /tts (free with cJSON_free). */
char *yk_api_tts_body(const char *text, const yk_cloud_tts_t *options);
/**
 * One /tts event: "audio" decoded and handed to `on_audio` (in place: the event's "data" string is overwritten);
 * "done" fills `credits`. False for "error" (`*code`).
 */
bool yk_api_tts_event(const char *event, const cJSON *data, yk_cloud_audio_fn on_audio, void *ctx,
                      yk_cloud_charge_t *credits, const char **code);
/**
 * Standard base64 (with padding) into `out`, which may be `in` itself (each byte is written behind what was read).
 * Returns the length, or 0 when it is not base64 or does not fit.
 */
size_t yk_api_b64_decode(const char *in, uint8_t *out, size_t size);
/** How a /llm/chat or /tts stream ended: NULL with its done event, the error event's code, else NETWORK. */
const char *yk_api_stream_end(bool done, const char *error);

/** The body of POST /files (free with cJSON_free). */
char *yk_api_file_body(const yk_cloud_file_info_t *info);
/** A {file, upload?} answer (register, complete, status, transcribe). Part URLs are allocated; yk_cloud_file_free. */
bool yk_api_file(const char *json, yk_cloud_file_t *out);

/** The body of POST /deliver (free with cJSON_free). */
char *yk_api_deliver_body(const yk_cloud_delivery_info_t *info);
/** A {delivery} answer. */
bool yk_api_delivery(const char *json, yk_cloud_delivery_t *out);

/** A data item; `out->value` is a copy (free with yk_cloud_data_free). */
bool yk_api_data_item(const cJSON *item, yk_cloud_data_item_t *out);
/** The body of PUT /data/:collection/:key: the value and, when `version` >= 0, the version it replaces. */
char *yk_api_data_body(const cJSON *value, int version);

/** A GET /inbox answer: `on_item` for each item, then `now` for the next call's since. */
bool yk_api_inbox(const char *json, yk_cloud_inbox_fn on_item, void *ctx, char *now, size_t now_size);

/** One app's grant to read the shared folders (device-api.ts folderGrant): `state` 1 granted, 2 denied (0: none);
 *  `folders` the kinds as bits in yk_folder.h's order, music 1, stories 2, pictures 4, documents 8; `rev` from 1. */
typedef struct {
    uint8_t state;
    uint8_t folders;
    uint32_t rev;
} yk_api_grant_t;
/** A folderGrant object, if it is `slug`'s and one (a state and kinds the contract lists, each kind once, rev >= 1). */
bool yk_api_folder_grant(const cJSON *grant, const char *slug, yk_api_grant_t *out);
/** A GET /folders?app= answer: 1 with `slug`'s grant in *out, 0 when it lists none of `slug` (*out zeroed), -1 when it
 *  is not a folderGrantsAnswer, or lists `slug` twice or not as a grant. Other apps' grants are not looked at. */
int yk_api_folder_grants(const char *json, const char *slug, yk_api_grant_t *out);
/** Every grant of a GET /folders answer (Yay's: all apps) to `each`, each one a folderGrant of its own slug; how many,
 *  -1 when it is not a folderGrantsAnswer. A grant that is not one, or an app listed twice, is left out. */
typedef void (*yk_api_grant_fn)(const char *slug, const yk_api_grant_t *grant, void *ctx);
int yk_api_folder_each(const char *json, yk_api_grant_fn each, void *ctx);
/** The body of PUT /folders/:app (free with cJSON_free): {state, folders (in the kinds' order), base: `rev`, or null
 *  when not `known`}; NULL for a state that is neither granted nor denied. */
char *yk_api_folder_put_body(uint8_t state, uint8_t folders, bool known, uint32_t rev);
/** What a PUT /folders/:app answered, by its status and its error code (never the status alone): 1 200 with `slug`'s
 *  grant, 2 409 GRANT_CHANGED with it (both into *out), 3 409 GRANTS_FULL, 0 anything else. */
int yk_api_folder_put_answer(int status, const char *json, const char *slug, yk_api_grant_t *out);

/** One of the device API's error codes as a static string ("SERVER" for one this firmware does not know). */
const char *yk_api_code(const char *error);
/** The code of a refusal's body ({"error":"FILE_GONE",…}). */
const char *yk_api_error_code(const char *json);
/** A number among a refusal's details ({"error":"VERSION_CONFLICT","version":4}), or `fallback`. */
int yk_api_error_number(const char *json, const char *key, int fallback);
/** What a refusal's body says it was held for want of (yk_cloud_consent_t, readRefusalConsent): VOICE only for
 *  {"error":"OPERATION_NOT_ALLOWED","consent":"voice"}, the key and both values exactly so; NONE otherwise. The code is
 *  read as ever (yk_api_error_code, yk_api_stream_code): `consent` changes nothing of it. */
yk_cloud_consent_t yk_api_error_consent(const char *json);
/** Whether `url` is an https:// URL with a host (part URLs must be). */
bool yk_api_https(const char *url);
/** `value` percent-encoded for a query string. Returns the length, or 0 when `out` is too small. */
size_t yk_api_escape(const char *value, char *out, size_t size);
