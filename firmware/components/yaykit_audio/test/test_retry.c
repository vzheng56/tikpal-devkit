// yk_retry: the microphones' open tried again (0.1.20).
#include <assert.h>
#include <stdio.h>

#include "../yk_retry.h"

static int fails_left, attempts, undos, paused_ms, stop_after;
static int attempt(void *ctx) {
    (void)ctx;
    attempts++;
    return fails_left-- > 0 ? -1 : 0;
}
static void undo(void *ctx) { (void)ctx, undos++; }
static void pause(void *ctx, int ms) { (void)ctx, paused_ms += ms; }
static bool go_on(void *ctx) { return (void)ctx, !stop_after || attempts < stop_after; }

static int run(int fail, int tries, int stop, int *failed) {
    fails_left = fail, attempts = undos = paused_ms = 0, stop_after = stop;
    const yk_retry_t r = {attempt, undo, pause, go_on, NULL};
    return yk_retry(&r, tries, 20, failed);
}

int main(void) {
    int failed;
    assert(run(0, 3, 0, &failed) == 1 && failed == 0 && attempts == 1 && !undos && !paused_ms);  // as ever: once
    assert(run(1, 3, 0, &failed) == 2 && failed == 1 && undos == 1 && paused_ms == 20);  // the I2C read held up once
    assert(run(2, 3, 0, &failed) == 3 && failed == 2 && undos == 2 && paused_ms == 40);
    assert(run(5, 3, 0, &failed) == 0 && failed == 3 && attempts == 3 && undos == 3 && paused_ms == 40);  // gives up
    assert(run(5, 3, 1, &failed) == 0 && failed == 1 && attempts == 1 && !paused_ms);  // the key went up: no more
    assert(run(5, 3, 2, &failed) == 0 && attempts == 2 && paused_ms == 20);
    const yk_retry_t bare = {attempt, NULL, pause, NULL, NULL};  // no undo, no go_on
    fails_left = 1, attempts = paused_ms = 0;
    assert(yk_retry(&bare, 3, 20, NULL) == 2 && paused_ms == 20);
    puts("yk_retry host tests: all passed");
    return 0;
}
