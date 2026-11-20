#include "yk_retry.h"

int yk_retry(const yk_retry_t *r, int tries, int pause_ms, int *failed) {
    int bad = 0, worked = 0;
    for (int i = 1; i <= tries; i++) {
        if (i > 1 && r->go_on && !r->go_on(r->ctx)) break;
        if (!r->attempt(r->ctx)) {
            worked = i;
            break;
        }
        bad++;
        if (r->undo) r->undo(r->ctx);
        if (i < tries && (!r->go_on || r->go_on(r->ctx))) r->pause(r->ctx, pause_ms);
    }
    if (failed) *failed = bad;
    return worked;
}
