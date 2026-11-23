// Writing the device's region (两套部署): yaykit_cloud's own, not in include/. Only the activation (yaykit_account.c)
// changes which side a device is on; an app sees the region (yk_region.h) and never moves it (the kit's review blocks
// the name too: devkit/review/symbols.json).
#pragma once

#include <stdbool.h>

/** CONFIG_YK_REGIONS only (not built with it off: nothing writes `rg` then). Row `row` becomes this device's region:
 *  yk_acct `rg` set to its name (global's too) and committed, and kept as yk_region_current() from now on, so that
 *  yk_cloud_origin() and yk_session_url() are its addresses at once, without a restart. *changed (may be NULL): `rg` as
 *  stored named another side (no `rg` is global's; a name not in the table is another side too, though read as global).
 *  False, nothing kept and the region as it was: not a row, or NVS would not take it. yaykit_account's activation calls
 *  it, before its request. NVS reads and a write: a task whose stack is in internal RAM. */
bool yk_region_store(int row, bool *changed);
