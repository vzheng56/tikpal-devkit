// A step tried again after a failure (0.1.20: the microphones' open, whose ES7210 I2C read a flash erase can hold past
// its timeout). Pure C, host tested (test/test_retry.c).
#pragma once

#include <stdbool.h>

typedef struct {
    int (*attempt)(void *ctx);         // 0: it worked
    void (*undo)(void *ctx);           // after a failed attempt, before the next (may be NULL)
    void (*pause)(void *ctx, int ms);  // between two attempts
    bool (*go_on)(void *ctx);          // false: no more attempts, it is not wanted any more (may be NULL)
    void *ctx;
} yk_retry_t;

/** `attempt` up to `tries` times, `undo` after each failure and `pause_ms` before the next, while `go_on` says so.
 *  Returns the attempt that worked (1..tries), or 0; `failed` (may be NULL): how many failed. */
int yk_retry(const yk_retry_t *r, int tries, int pause_ms, int *failed);
