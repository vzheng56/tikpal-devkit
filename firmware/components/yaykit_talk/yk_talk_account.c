// yaykit_talk and the device's account (yk_talk_account.h).
#include "yk_talk_account.h"

bool yk_talk_may_start(bool production, bool added) { return !production || added; }

esp_err_t yk_talk_account_token(const yk_talk_account_env_t *env, bool production, char *out, size_t size, bool fresh) {
    if (!production) {
        out[0] = 0;
        return ESP_OK;
    }
    if (fresh) env->expired(env->ctx);
    const esp_err_t err = env->token(out, size, env->ctx);
    // Asked of the account first, as before: for a device added it is the same call, and a device not added gets its
    // answer from RAM (no network). Then, only when there is no token: not added now, no connection.
    if (err != ESP_OK && !env->added(env->ctx)) env->not_added(env->ctx);
    return err;
}
