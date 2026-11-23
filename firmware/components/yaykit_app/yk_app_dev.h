// Development builds' `yay <name>` commands, the ones yk_app_dev_register() adds (CONFIG_YAY_DEV; yk_app_dev.c): no
// fixed limit, kept in PSRAM. Private to yaykit_app (yaykit_app.c's `yay`). Other builds keep yaykit_app.c's table of
// 30 in internal RAM, as before; nothing of Yay's is registered there.
#pragma once

#include <stdio.h>

#include "yaykit_app.h"

/** The command registered as `name` (the first one, if two have it); NULL when there is none. */
yk_app_dev_fn yk_app_dev_find(const char *name);

/** Each registered command, in the order registered, as " | <help>" (its name when it has no help). Returns how many. */
int yk_app_dev_help(FILE *out);
