// The public keys this build trusts for slot images (firmware/signing/README.md): the release keys, the same as the
// store's RELEASE_KEYS (packages/cloud/src/firmware-signature.ts); development builds (CONFIG_YAY_DEV) add the
// developer key, so images built and signed on the bench install on the test device. The store never offers what the
// developer key signs. A key can be for extensions only (the automation key, 0.1.17): it never signs the system's own
// firmware (yay, recovery, launcher), like the store's `scope: 'extensions'`.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "yaykit_sig.h"

/** The keys, and their number in `count`. */
const uint8_t (*yk_install_keys(size_t *count))[YK_SIG_KEY_BYTES];

/** Whether the key with this id signs extensions only. */
bool yk_install_key_extensions_only(const char *key_id);

/** Whether `signature` is one of the keys' over `image`, as yk_sig_verify judges it, with a key that may sign this app:
 *  for the system's own firmware a key for extensions only is not one of the keys ("unknown-key", what the store
 *  answers too). Needs about 3 KB of stack, like yk_sig_verify. */
yk_sig_result_t yk_install_verify(const yk_sig_image_t *image, const char *signature);
