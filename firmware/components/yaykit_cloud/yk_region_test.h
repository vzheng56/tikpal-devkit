// A development build's test addresses for a row of the region table (两套部署 第 3 步: the firmware's plan 4.5, step 1's
// C): yaykit_cloud's own, not in include/, and its names are in the kit's review's internal list (devkit/review/
// symbols.json), so no app sets or reads them. Built only with CONFIG_YAY_DEV and CONFIG_YK_REGIONS both on: a release
// build, and any build without the regions, has none of this and reads none of it, whatever NVS holds.
//
// A row (global or cn) can be pointed at a test deployment by its origin, https://<host>[:port]. That row's
// yk_cloud_origin() is then the origin, and its yk_session_url() (yaykit_session) wss://<the same host>[:port] and the
// table's path (/device/v1/session): the contract puts a deployment's session on its device host. Kept in yk_acct as
// `rgdev_<id>` ("rgdev_cn"), read once at start with `rg` (yk_region_current()): a change takes a restart, as `yay
// server` does. Every firmware with yaykit_cloud reads it, so Yay, Language Buddy, Face to Face, Homework Buddy and the
// rest all go there: set once, with Yay's `region` command (yaykit_account.c, beside `account`). Yay's factory reset keeps
// it (yay_reset.c: the reset and the way back to global are step 3's walk); a full USB install erases it with the rest.
//
// The session commands of before stay as they are, for the mock voice server, the USB bridge and the acceptance
// scripts (`yay server`, `lb server`, `tr server`): with one of them set too, the session is that one's, as today, and
// HTTPS still this row's test origin.
#pragma once

#include <stdbool.h>
#include <stddef.h>

/** The longest test origin, in characters: its session (16 more) fits Yay's s_server[96] and yaykit_talk's url[96], and
 *  an origin and a path the 160 bytes yaykit_cloud.c builds a kept connection's URL in. */
#define YK_REGION_TEST_ORIGIN_MAX 64

/** Whether `origin` can be a test origin: "https://", a host of lowercase letters, digits, dots and hyphens beginning with
 *  a letter or digit, maybe ":<port>" (1-65535), and nothing after (no path, no query, no user), at most
 *  YK_REGION_TEST_ORIGIN_MAX characters. https:// only: a release talks only https, and the test deployment's certificate
 *  is part of what step 3 tries. *why (may be NULL): what is wrong, for the console. Pure. */
bool yk_region_test_valid(const char *origin, const char **why);

/** Row `row`'s test origin and session in use since this start (what yk_cloud_origin() and yk_session_url() give when
 *  the device is in that row), NULL for none or not a row: then the table's (global's, the setting's). Strings that stay
 *  as they are until the restart. */
const char *yk_region_test_origin(int row);
const char *yk_region_test_session(int row);

/** Row `row`'s test origin as stored now, what the next start will use, into `out` ("" none). False: not a row, or NVS
 *  could not be read. NVS reads: an internal-RAM stack. */
bool yk_region_test_stored(int row, char *out, size_t size);

/** Stores `origin` as row `row`'s test origin for the next start, or with `origin` NULL erases it (the table's again).
 *  *changed (may be NULL): what is stored now is not what was. False, nothing written: not a row, not a valid origin
 *  (*why says, may be NULL), or NVS would not take it. Nothing in use changes before the restart. NVS writes: an
 *  internal-RAM stack. */
bool yk_region_test_store(int row, const char *origin, bool *changed, const char **why);
