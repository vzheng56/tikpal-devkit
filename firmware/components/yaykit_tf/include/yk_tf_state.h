// What the card is doing now: yk_tf_state(). Apart from yaykit_tf.h so that the pure parts need no ESP-IDF.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    YK_TF_OFF,      // not mounted, nothing known against it: the next yk_tf_acquire() mounts it
    YK_TF_READY,    // mounted: someone holds it
    YK_TF_PAUSED,   // the system wants the card free (Bluetooth, a restart): yk_tf_acquire() refuses; a holder with a long
                    // job should stop and release
    YK_TF_RESTING,  // too many failures in a row: not used for now, yk_tf_acquire() refuses until the retry time
} yk_tf_state_t;

/** What is known of the card without going to it: yk_tf_sys_known(). */
typedef struct {
    yk_tf_state_t state;   // yk_tf_state()
    uint8_t fails;         // in a row
    bool opened;           // a mount has worked since this start
    bool not_fat;          // the last mount found no file system FatFs reads: formatting would help, nothing else
    uint64_t used, total;  // bytes, the whole card, when it was last measured; total 0: not yet
} yk_tf_known_t;
