// yaykit_talk and the device's account (两套部署 E, 11-21): the production session only for a device added to an
// account, with CONFIG_YK_REGIONS on or off. A device added in cn keeps its binding as `bound_cn` (yk_acct, 两套部署
// 4.3), which a build with the setting off never reads: there it is not added, and its session would go to global's
// server without a token, to be turned away (401). A device not added makes no connection at all. Plain C, host tested
// (test/test_talk.c), as yk_talk_close.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/** yk_talk_start: a development server always (no token is asked for, nothing touches the cloud); the production
 *  session only for a device added to an account (`added`: yk_account_state() is YK_ACCOUNT_BOUND). */
bool yk_talk_may_start(bool production, bool added);

/** The account, for the session's token. */
typedef struct {
    esp_err_t (*token)(char *out, size_t size, void *ctx);  // yk_account_token
    void (*expired)(void *ctx);                             // yk_account_token_expired
    bool (*added)(void *ctx);                               // yk_account_state() is YK_ACCOUNT_BOUND, now
    void (*not_added)(void *ctx);                           // holds the session: the connection that asked is not made
    void *ctx;
} yk_talk_account_env_t;

/** The session's token (yk_session_config_t.token): "" for a development server; in production the app's (`fresh`:
 *  not the cached one). None because the device is not added (any more: the cloud forgot it while the app ran,
 *  yk_account_unbound_by_server()): env->not_added() before it returns, so that the session does not connect without
 *  a token. A token not got for another reason (the network) goes as before. */
esp_err_t yk_talk_account_token(const yk_talk_account_env_t *env, bool production, char *out, size_t size, bool fresh);
