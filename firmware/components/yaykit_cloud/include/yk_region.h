// Which deployment this device belongs to (两套部署: docs/delivery/2026-11-20-两套部署-总纲.md, the firmware's plan
// 4.1-4.5): a row of the contract's region table, include/yk_regions_gen.h. Only yk_region.c reads the table; everything
// else asks here, or calls yk_cloud_origin() (yaykit_cloud.h) and yk_session_url() (yaykit_session.h).
//
// With CONFIG_YK_REGIONS off (the default) the device is always in row 0, global, and nothing here reads or writes NVS:
// the addresses are today's. On, the region is yk_acct `rg` as stored, read once (yk_account_init() asks first) and
// kept: no `rg`, or a name not in the table, is global. Only an activation writes `rg` (yk_region_store(), which keeps
// the new row at once, declared in the component's own yk_region_store.h: no app moves the region), before its request:
// the claim's prefix says the row.
//
// A development build (CONFIG_YAY_DEV) with them on may have a test deployment stand in for a row, its origin read with
// `rg` (yk_region_test.h, the component's own; Yay's `region set`): yk_cloud_origin() and yk_session_url() are then that
// deployment's for that row. yk_region_row() still gives the table's. A release build reads none of it.
//
// A name not in the table is global (11-21, the firmware PM): today that is the only other address this firmware has.
// When a third region comes, look at this again: a newer firmware's `rg` read by an older one would then be a device
// bound elsewhere talking to global (附录 C 2.3 suggested 「未知地区」, no server at all).
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Row 0, always global (YK_REGION_DEFAULT in yk_regions_gen.h): the device that never stored a region is in it. */
#define YK_REGION_GLOBAL 0

/** A row of the table, as generated: pointers into it (never NULL, never freed). */
typedef struct {
    const char *id;              // "global", "cn": DEPLOY_REGION's, and what `rg`, hello and yk-info carry
    const char *claim_prefix;    // "ykc_", "ykcn_": the claims that deployment hands out
    const char *device_origin;   // its device API, as in the table (yk_cloud_origin() is what this device uses)
    const char *session_url;     // its voice session, as in the table (yk_session_url() is what this device uses)
    const char *onboarding_url;  // first setup's QR code
    const char *computer_url;    // the update page's 「在电脑上打开」 line
} yk_region_row_t;

/** How many rows the table has (at least one: global). */
int yk_region_count(void);
/** Row `row` into *out; false, *out untouched, for a row not in the table. */
bool yk_region_row(int row, yk_region_row_t *out);
/** The row named `id` ("cn"), -1 for NULL or a name not in the table. */
int yk_region_find(const char *id);
/** The row whose claim prefix `claim` starts with (the longest, should one prefix ever start another), -1 for NULL or
 *  none. Pure (no NVS, either setting): the claim's format is still yk_claim_valid()'s. */
int yk_region_for_claim(const char *claim);

/** This device's row. Off: YK_REGION_GLOBAL. On: yk_acct `rg`'s row, read on the first call and kept (an NVS read:
 *  that first call from a task whose stack is in internal RAM; yk_account_init() makes it); no `rg` or a name not in
 *  the table (said once in the log) is YK_REGION_GLOBAL. NVS not ready yet answers global and reads again next time. */
int yk_region_current(void);
/** This device's row's id: "global" with CONFIG_YK_REGIONS off. */
const char *yk_region_name(void);

#ifdef __cplusplus
}
#endif
