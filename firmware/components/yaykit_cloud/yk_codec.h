// Pure C helpers shared by the device and the host tests: base64url, the signed messages, claim checks and the
// kept token.
// The message formats must match packages/cloud/src/device-auth.ts.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** base64url without padding. Returns the length written (NUL-terminated), or 0 when `out` is too small. */
size_t yk_b64url_encode(const uint8_t *in, size_t len, char *out, size_t out_size);

/** "yaykit-activate:v1:<device>:<claim>". Returns the length, or 0 when `out` is too small. */
size_t yk_activation_message(const char *device_id, const char *claim, char *out, size_t size);

/** "yaykit-token:v1:<device>:<app>:<nonce>". Returns the length, or 0 when `out` is too small. */
size_t yk_token_message(const char *device_id, const char *app, const char *nonce, char *out, size_t size);

/** A claim from /v1/me/device-claims: "ykc_" followed by base64url, 20–100 characters in total. */
bool yk_claim_valid(const char *claim);

/** The NVS key an app's kept token goes under: "tk" and 8 hex digits of the slug's FNV-1a (keys are at most 15). */
void yk_token_key(const char *app, char out[11]);

/** Seconds a kept token can still be used, or 0: the clock is not known yet (before 2025), or it ends within two
 *  minutes (a token is asked for again a minute before its end anyway). */
int64_t yk_token_left_s(int64_t now_unix, int64_t expires_unix);

/** How far a lookup's POST got before it failed: not open (the request line and headers did not go), part of the
 *  recording out, or all of it out (then the cloud may be charging). */
typedef enum { YK_SENT_NONE, YK_SENT_PART, YK_SENT_ALL } yk_sent_t;
/** Send it all again on a new connection? Only when it went on a kept connection (the server or its proxy may have
 *  closed it while idle) and before the whole recording was out: after that it never goes twice, so it is never
 *  paid twice. */
bool yk_send_again(bool reused, yk_sent_t sent);
