// Host test for the device's region (yk_region.c itself, on test/stub) and the two addresses it gives: yk_cloud_origin()
// and yaykit_session's yk_session_url() (yk_session_url.c). run.sh builds it CONFIG_YK_REGIONS off and on, each as a
// release build and a development one (CONFIG_YAY_DEV), and on with a development build's own CONFIG_YK_CLOUD_ORIGIN
// (TEST_OWN_ORIGIN). Off, every device is global whatever NVS holds and nothing reads NVS: today's addresses. On, yk_acct
// `rg` picks the row, read once; no `rg`, or a name not in the table, is global. A development build with them on may
// have a test deployment stand in for a row (yk_region_test.h, yk_acct `rgdev_<id>`); no other build reads those keys.
#include "../yk_region.c"

#include <stdio.h>
#include <stdlib.h>

#include "yaykit_session.h"

static int checks, failures;
#define CHECK(c)                                                    \
    do {                                                            \
        checks++;                                                   \
        if (!(c)) {                                                 \
            failures++;                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
        }                                                           \
    } while (0)
#define SAME(a, b) (!strcmp((a), (b)))

// ---- NVS in memory: yk_acct, its `rg` and a development build's test deployments (`rgdev_<id>`)

#define NVS_NOT_INITIALIZED 0x1101  // ESP-IDF's ESP_ERR_NVS_NOT_INITIALIZED
#define NVS_INVALID_LENGTH 0x110c   // ESP_ERR_NVS_INVALID_LENGTH: the value is longer than the buffer

static bool s_ready, s_acct;  // nvs_flash_init() has run; yk_acct exists
static char s_rg[64];         // "": no `rg`
static char s_dev[2][128];    // rgdev_global, rgdev_cn as stored ("": none)
static int s_opens, s_writes;
static int s_dev_reads;       // reads of an rgdev_* key: none in a build without CONFIG_YAY_DEV and CONFIG_YK_REGIONS
static bool s_full;           // NVS takes no write (full, or broken)

/** The string `key` is kept in, NULL for a key this test does not keep. */
static char *slot(const char *key, size_t *size) {
    *size = sizeof(s_dev[0]);
    if (!strcmp(key, "rg")) {
        *size = sizeof(s_rg);
        return s_rg;
    }
    if (!strcmp(key, "rgdev_global")) return s_dev[0];
    if (!strcmp(key, "rgdev_cn")) return s_dev[1];
    return NULL;
}

esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *handle) {
    s_opens++;
    if (!s_ready) return NVS_NOT_INITIALIZED;
    if (strcmp(ns, "yk_acct")) return ESP_ERR_NVS_NOT_FOUND;
    if (mode == NVS_READWRITE) s_acct = true;  // opening it to write makes it
    else if (!s_acct) return ESP_ERR_NVS_NOT_FOUND;
    *handle = mode == NVS_READWRITE ? 2 : 1;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { (void)handle; }
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value) {
    size_t size;
    char *at = slot(key, &size);
    if (handle != 2 || !at) return ESP_FAIL;  // only the keys kept here, and only opened to write
    if (s_full) return ESP_FAIL;
    s_writes++;
    snprintf(at, size, "%s", value);
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    size_t size;
    char *at = slot(key, &size);
    if (handle != 2 || !at) return ESP_FAIL;
    if (s_full) return ESP_FAIL;
    if (!at[0]) return ESP_ERR_NVS_NOT_FOUND;
    s_writes++;
    at[0] = 0;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) { return handle == 2 ? ESP_OK : ESP_FAIL; }
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out, size_t *length) {
    (void)handle;
    if (!strncmp(key, "rgdev_", 6)) s_dev_reads++;
    size_t size;
    const char *at = slot(key, &size);
    if (!at || !at[0]) return ESP_ERR_NVS_NOT_FOUND;
    const size_t n = strlen(at) + 1;
    if (n > *length) return NVS_INVALID_LENGTH;
    memcpy(out, at, n);
    *length = n;
    return ESP_OK;
}

/** A device just started: NVS set up, yk_acct as given (NULL: none), no test deployment, nothing read yet. */
static void start(const char *rg) {
    s_ready = true;
    s_acct = rg != NULL;
    snprintf(s_rg, sizeof(s_rg), "%s", rg ? rg : "");
    s_dev[0][0] = s_dev[1][0] = 0;
    s_opens = s_writes = s_dev_reads = 0;
    s_full = false;
#if CONFIG_YK_REGIONS
    s_row = -1;
#endif
#if CONFIG_YAY_DEV && CONFIG_YK_REGIONS
    free(s_test);  // what the start before read
    s_test = NULL;
#endif
}

/** start(), with these test deployments stored (NULL: none) for global and cn: what `region set` left before the restart. */
static void start_tested(const char *rg, const char *global, const char *cn) {
    start(rg);
    if (global || cn) s_acct = true;  // `region set` made yk_acct
    snprintf(s_dev[0], sizeof(s_dev[0]), "%s", global ? global : "");
    snprintf(s_dev[1], sizeof(s_dev[1]), "%s", cn ? cn : "");
}

static int row_named(const char *id) {
    for (int i = 0; i < yk_region_count(); i++) {
        yk_region_row_t r;
        if (yk_region_row(i, &r) && SAME(r.id, id)) return i;
    }
    return -1;
}

/** Global's addresses as this build has them: the setting's origin, YK_SESSION_URL. */
static bool global_addresses(void) {
    return SAME(yk_cloud_origin(), CONFIG_YK_CLOUD_ORIGIN) && SAME(yk_session_url(), YK_SESSION_URL);
}

static void test_table(void) {
    // Row 0 is global, and the same as what the firmware had built in before the table: the setting's default (run.sh
    // gives this build the Kconfig's, except TEST_OWN_ORIGIN) and YK_SESSION_URL.
    yk_region_row_t g, r;
    CHECK(yk_region_count() == YK_REGION_COUNT && yk_region_count() >= 2);
    CHECK(yk_region_row(YK_REGION_GLOBAL, &g) && SAME(g.id, "global") && SAME(g.claim_prefix, "ykc_"));
    CHECK(SAME(g.device_origin, "https://device.yaykit.io") && SAME(g.session_url, YK_SESSION_URL));
#ifndef TEST_OWN_ORIGIN
    CHECK(SAME(g.device_origin, CONFIG_YK_CLOUD_ORIGIN));
#endif
    CHECK(SAME(YK_SESSION_URL, "wss://device.yaykit.io/device/v1/session"));
    // Every row whole, ids and prefixes their own
    for (int i = 0; i < yk_region_count(); i++) {
        CHECK(yk_region_row(i, &r) && *r.id && *r.claim_prefix && *r.device_origin && *r.session_url &&
              *r.onboarding_url && *r.computer_url);
        CHECK(yk_region_find(r.id) == i && row_named(r.id) == i);
        CHECK(!strncmp(r.device_origin, "https://", 8) && !strncmp(r.session_url, "wss://", 6));
        CHECK(strlen(r.onboarding_url) <= YK_REGION_ONBOARDING_URL_MAX);
    }
    const int cn = yk_region_find("cn");
    CHECK(cn > 0 && yk_region_row(cn, &r) && SAME(r.claim_prefix, "ykcn_"));
    CHECK(SAME(r.device_origin, "https://device.tikpal.com.cn"));
    CHECK(SAME(r.session_url, "wss://device.tikpal.com.cn/device/v1/session"));
    // Not rows
    memset(&r, 0, sizeof(r));
    CHECK(!yk_region_row(-1, &r) && !yk_region_row(yk_region_count(), &r) && !r.id);
    CHECK(!yk_region_row(0, NULL));
    CHECK(yk_region_find(NULL) == -1 && yk_region_find("") == -1 && yk_region_find("CN") == -1 &&
          yk_region_find("mars") == -1);
}

static void test_claims(void) {
    // The prefix says the row, in either setting: no NVS
    start("cn");
    const int cn = yk_region_find("cn");
    CHECK(yk_region_for_claim("ykc_AbC-123_xyz") == YK_REGION_GLOBAL);
    CHECK(yk_region_for_claim("ykcn_AbC-123_xyz") == cn);
    CHECK(yk_region_for_claim("ykc_cn_abc") == YK_REGION_GLOBAL);  // global's, whatever its random part
    CHECK(yk_region_for_claim("ykc_") == YK_REGION_GLOBAL && yk_region_for_claim("ykcn_") == cn);  // the format is yk_claim_valid()'s
    CHECK(yk_region_for_claim(NULL) == -1 && yk_region_for_claim("") == -1);
    CHECK(yk_region_for_claim("ykc") == -1 && yk_region_for_claim("ykcn") == -1 && yk_region_for_claim("yk_abc") == -1);
    CHECK(yk_region_for_claim("YKC_abc") == -1 && yk_region_for_claim("xykc_abc") == -1 && yk_region_for_claim(" ykc_a") == -1);
    CHECK(yk_region_for_claim("ykus_abc") == -1);
    for (int i = 0; i < yk_region_count(); i++) {
        yk_region_row_t r;
        char claim[64];
        CHECK(yk_region_row(i, &r));
        snprintf(claim, sizeof(claim), "%sabcdefghijklmnop", r.claim_prefix);
        CHECK(yk_region_for_claim(claim) == i);
    }
    CHECK(s_opens == 0);
}

#if !CONFIG_YK_REGIONS
static void test_off(void) {
    // Whatever NVS holds, global and today's addresses, and NVS is never opened
    const char *stored[] = {NULL, "", "global", "cn", "mars", "a-name-longer-than-any-buffer-here-0123456789"};
    for (size_t i = 0; i < sizeof(stored) / sizeof(stored[0]); i++) {
        start(stored[i]);
        CHECK(yk_region_current() == YK_REGION_GLOBAL && SAME(yk_region_name(), "global"));
        CHECK(SAME(yk_cloud_origin(), "https://device.yaykit.io"));
        CHECK(SAME(yk_session_url(), YK_SESSION_URL));
        CHECK(global_addresses());
        CHECK(s_opens == 0 && s_writes == 0);
    }
    // Test deployments a development build with the regions stored (`region set`): not read, development build or not
    start_tested("cn", "https://global-test.example", "https://cn-test.example");
    CHECK(yk_region_current() == YK_REGION_GLOBAL && global_addresses() && s_opens == 0 && s_dev_reads == 0);
}
#else
static void test_on(void) {
    const int cn = yk_region_find("cn");
    yk_region_row_t r;
    CHECK(yk_region_row(cn, &r));

    // Never activated: no yk_acct at all. Global, read once.
    start(NULL);
    CHECK(yk_region_current() == YK_REGION_GLOBAL && SAME(yk_region_name(), "global") && global_addresses());
    CHECK(s_opens == 1);
#ifdef TEST_OWN_ORIGIN
    // A development build's own device API stays global's; the other rows are the table's (below)
    CHECK(SAME(yk_cloud_origin(), "https://device.test") && SAME(yk_session_url(), YK_SESSION_URL));
#else
    CHECK(SAME(yk_cloud_origin(), "https://device.yaykit.io"));
#endif
    // yk_acct without `rg` (every device before the region was stored): global
    start("");
    CHECK(yk_region_current() == YK_REGION_GLOBAL && global_addresses());
    start("global");
    CHECK(yk_region_current() == YK_REGION_GLOBAL && SAME(yk_region_name(), "global") && global_addresses());

    // rg=cn: cn's row, both addresses
    start("cn");
    CHECK(yk_region_current() == cn && SAME(yk_region_name(), "cn"));
    CHECK(SAME(yk_cloud_origin(), r.device_origin) && SAME(yk_cloud_origin(), "https://device.tikpal.com.cn"));
    CHECK(SAME(yk_session_url(), r.session_url) && SAME(yk_session_url(), "wss://device.tikpal.com.cn/device/v1/session"));
    // Read once and kept: NVS changing under it is not read again (writing `rg` comes with activation, later)
    const int opened = s_opens;
    CHECK(opened == 1);
    snprintf(s_rg, sizeof(s_rg), "global");
    for (int i = 0; i < 5; i++) CHECK(yk_region_current() == cn && SAME(yk_cloud_origin(), r.device_origin));
    CHECK(s_opens == opened);

    // A name this firmware does not know (a newer one wrote it), or no name at all: global, not a crash; said once
    const char *unknown[] = {"mars", "CN", "cn ", "global2", "a-name-longer-than-any-buffer-here-0123456789"};
    for (size_t i = 0; i < sizeof(unknown) / sizeof(unknown[0]); i++) {
        start(unknown[i]);
        CHECK(yk_region_current() == YK_REGION_GLOBAL && SAME(yk_region_name(), "global") && global_addresses());
        CHECK(yk_region_current() == YK_REGION_GLOBAL && s_opens == 1);
    }

    // Asked before nvs_flash_init(): global this once, read again the next time
    start("cn");
    s_ready = false;
    CHECK(yk_region_current() == YK_REGION_GLOBAL && global_addresses());
    s_ready = true;
    CHECK(yk_region_current() == cn && SAME(yk_session_url(), r.session_url));
}

/** yk_region_store(): what an activation does with the claim's row, before its request. */
static void test_store(void) {
    const int cn = yk_region_find("cn");
    yk_region_row_t r;
    CHECK(yk_region_row(cn, &r));
    bool changed = true;

    // Never activated (no yk_acct): a cn claim. Stored by name, kept at once: both addresses cn's, no restart, NVS
    // not read again for it.
    start(NULL);
    CHECK(yk_region_store(cn, &changed) && changed && SAME(s_rg, "cn") && s_writes == 1);
    const int opened = s_opens;
    CHECK(yk_region_current() == cn && SAME(yk_cloud_origin(), r.device_origin) && SAME(yk_session_url(), r.session_url));
    CHECK(s_opens == opened);
    // ... and back to global: global's name written too (not the key left out), changed
    CHECK(yk_region_store(YK_REGION_GLOBAL, &changed) && changed && SAME(s_rg, "global") && global_addresses());
    // The same side again: written, not a change
    CHECK(yk_region_store(YK_REGION_GLOBAL, &changed) && !changed && SAME(s_rg, "global"));
    // No `rg` (a device from before) and a global claim: its name now, not a change (its tokens were global's)
    start("");
    CHECK(yk_region_store(YK_REGION_GLOBAL, &changed) && !changed && SAME(s_rg, "global") && global_addresses());
    start("cn");
    CHECK(yk_region_store(cn, &changed) && !changed && yk_region_current() == cn);
    // A name this firmware does not know is read as global, but it was another side: a change
    start("mars");
    CHECK(yk_region_current() == YK_REGION_GLOBAL);
    CHECK(yk_region_store(YK_REGION_GLOBAL, &changed) && changed && SAME(s_rg, "global"));
    // Not a row: nothing written, nothing kept
    start("cn");
    CHECK(!yk_region_store(-1, &changed) && !changed && !yk_region_store(yk_region_count(), NULL));
    CHECK(SAME(s_rg, "cn") && s_writes == 0 && yk_region_current() == cn);
    // NVS takes no write: false, the region as it was (an activation does not go)
    start("cn");
    s_full = true;
    CHECK(!yk_region_store(YK_REGION_GLOBAL, &changed) && !changed);
    CHECK(SAME(s_rg, "cn") && yk_region_current() == cn && SAME(yk_cloud_origin(), r.device_origin));
    s_full = false;
    // NVS not set up at all: false, nothing kept
    start(NULL);
    s_ready = false;
    CHECK(!yk_region_store(cn, &changed));
    s_ready = true;
    CHECK(yk_region_current() == YK_REGION_GLOBAL && global_addresses());
}

#if !CONFIG_YAY_DEV
/** A release build with the regions: test deployments left in NVS by a development build are never read. */
static void test_release(void) {
    const int cn = yk_region_find("cn");
    yk_region_row_t r;
    CHECK(yk_region_row(cn, &r));
    start_tested("cn", "https://global-test.example", "https://cn-test.example");
    CHECK(yk_region_current() == cn && SAME(yk_cloud_origin(), r.device_origin) && SAME(yk_session_url(), r.session_url));
    start_tested(NULL, "https://global-test.example", "https://cn-test.example");
    CHECK(yk_region_current() == YK_REGION_GLOBAL && global_addresses());
    CHECK(yk_region_store(cn, NULL) && SAME(yk_cloud_origin(), r.device_origin) && SAME(yk_session_url(), r.session_url));
    CHECK(s_dev_reads == 0);
}
#endif
#endif

#if CONFIG_YAY_DEV && CONFIG_YK_REGIONS
#define GLOBAL_TEST "https://global-test.example"
#define CN_TEST "https://cn-test.example"

/** A development build without a test deployment: the addresses as a release's, and nothing allocated. */
static void test_dev_none(void) {
    const int cn = yk_region_find("cn");
    yk_region_row_t r;
    CHECK(yk_region_row(cn, &r));
    start("cn");
    CHECK(yk_region_current() == cn && SAME(yk_cloud_origin(), r.device_origin) && SAME(yk_session_url(), r.session_url));
    CHECK(s_opens == 1 && s_dev_reads == 2 && !s_test);  // both rows looked for in the open `rg` was read in
    CHECK(!yk_region_test_origin(YK_REGION_GLOBAL) && !yk_region_test_session(YK_REGION_GLOBAL));
    CHECK(!yk_region_test_origin(cn) && !yk_region_test_session(cn));
    CHECK(!yk_region_test_origin(-1) && !yk_region_test_origin(yk_region_count()));
    start(NULL);  // never activated, nothing set: no yk_acct at all
    CHECK(yk_region_current() == YK_REGION_GLOBAL && global_addresses() && !s_test && s_dev_reads == 0);
}

/** cn's row on a test deployment: both of cn's addresses its, global's untouched; the way step 3 goes. */
static void test_dev_cn(void) {
    const int cn = yk_region_find("cn");
    start_tested("cn", NULL, CN_TEST);
    CHECK(yk_region_current() == cn && SAME(yk_region_name(), "cn"));
    CHECK(SAME(yk_cloud_origin(), CN_TEST) && SAME(yk_session_url(), "wss://cn-test.example/device/v1/session"));
    CHECK(SAME(yk_region_test_origin(cn), CN_TEST) && !yk_region_test_origin(YK_REGION_GLOBAL));
    CHECK(s_opens == 1);  // read with `rg`
    // A port goes with the host
    start_tested("cn", NULL, "https://cn-test.example:8443");
    CHECK(SAME(yk_cloud_origin(), "https://cn-test.example:8443"));
    CHECK(SAME(yk_session_url(), "wss://cn-test.example:8443/device/v1/session"));
    // In global (never activated, or a ykc_ claim): global's as ever; a ykcn_ claim's yk_region_store() moves both to the
    // test deployment at once, as it moves them to the table's cn without one
    start_tested(NULL, NULL, CN_TEST);
    CHECK(yk_region_current() == YK_REGION_GLOBAL && global_addresses());
    bool changed = false;
    CHECK(yk_region_store(cn, &changed) && changed && SAME(s_rg, "cn"));
    CHECK(SAME(yk_cloud_origin(), CN_TEST) && SAME(yk_session_url(), "wss://cn-test.example/device/v1/session"));
    // ... and back to global (the factory reset and a ykc_ claim: step 3's way back): global's again
    CHECK(yk_region_store(YK_REGION_GLOBAL, &changed) && changed && global_addresses());
    // Read once and kept: set or cleared after the start, the addresses stay until the next
    start_tested("cn", NULL, CN_TEST);
    CHECK(SAME(yk_cloud_origin(), CN_TEST));
    s_dev[1][0] = 0;
    snprintf(s_dev[1], sizeof(s_dev[1]), "https://other.example");
    for (int i = 0; i < 3; i++) CHECK(SAME(yk_cloud_origin(), CN_TEST) && SAME(yk_session_url(), "wss://cn-test.example/device/v1/session"));
    char next[80];
    CHECK(yk_region_test_stored(cn, next, sizeof(next)) && SAME(next, "https://other.example"));
}

/** Global's row on a test deployment: over the setting's origin (a development build's own one too), cn's untouched. */
static void test_dev_global(void) {
    const int cn = yk_region_find("cn");
    yk_region_row_t r;
    CHECK(yk_region_row(cn, &r));
    const char *stored[] = {NULL, "", "global", "mars"};  // never activated, no `rg`, global, a name not in the table
    for (size_t i = 0; i < sizeof(stored) / sizeof(stored[0]); i++) {
        start_tested(stored[i], GLOBAL_TEST, NULL);
        CHECK(yk_region_current() == YK_REGION_GLOBAL);
        CHECK(SAME(yk_cloud_origin(), GLOBAL_TEST) && SAME(yk_session_url(), "wss://global-test.example/device/v1/session"));
        CHECK(!SAME(yk_cloud_origin(), CONFIG_YK_CLOUD_ORIGIN) && !SAME(yk_session_url(), YK_SESSION_URL));
        CHECK(!yk_region_test_origin(cn));
    }
    // Both rows on test deployments: each its own, the region says which
    start_tested("cn", GLOBAL_TEST, CN_TEST);
    CHECK(SAME(yk_cloud_origin(), CN_TEST) && SAME(yk_region_test_origin(YK_REGION_GLOBAL), GLOBAL_TEST));
    CHECK(yk_region_store(YK_REGION_GLOBAL, NULL) && SAME(yk_cloud_origin(), GLOBAL_TEST));
    CHECK(SAME(yk_session_url(), "wss://global-test.example/device/v1/session"));
    // Global's on one, cn on the table's
    start_tested("cn", GLOBAL_TEST, NULL);
    CHECK(SAME(yk_cloud_origin(), r.device_origin) && SAME(yk_session_url(), r.session_url));
}

/** What is not a test origin: refused by `region set` with why, nothing written; and stored by hand all the same, left
 *  out at start (the table's, said in the log). */
static void test_dev_bad(void) {
    const int cn = yk_region_find("cn");
    yk_region_row_t r;
    CHECK(yk_region_row(cn, &r));
    char longest[YK_REGION_TEST_ORIGIN_MAX + 2];
    snprintf(longest, sizeof(longest), "https://%0*d.example", YK_REGION_TEST_ORIGIN_MAX - 16, 0);
    CHECK(strlen(longest) == YK_REGION_TEST_ORIGIN_MAX && yk_region_test_valid(longest, NULL));
    char too_long[YK_REGION_TEST_ORIGIN_MAX + 2];
    snprintf(too_long, sizeof(too_long), "https://%0*d.example", YK_REGION_TEST_ORIGIN_MAX - 15, 0);
    CHECK(strlen(too_long) == YK_REGION_TEST_ORIGIN_MAX + 1);
    const struct {
        const char *origin, *why;
    } bad[] = {
        {"http://cn-test.example", "not https://"},  // https only (the PR says why)
        {"ws://cn-test.example", "not https://"},
        {"wss://cn-test.example", "not https://"},
        {"HTTPS://cn-test.example", "not https://"},
        {"cn-test.example", "not https://"},
        {"", "empty"},
        {too_long, "too long"},
        {"https://", "no host"},
        {"https://.example", "no host"},
        {"https://-x.example", "no host"},
        {"https://CN-test.example", "no host"},  // lowercase, the host as the table writes it
        {"https://cn-Test.example", "only"},
        {"https://cn-test.example/", "only"},
        {"https://cn-test.example/device", "only"},
        {"https://cn-test.example?x=1", "only"},
        {"https://cn-test.example#x", "only"},
        {"https://user@example.com", "only"},  // the kit's check takes example.com's for no one's address
        {"https://cn test.example", "only"},
        {"https://cn-test.example ", "only"},
        {"https://cn-test.example:", "not a port"},
        {"https://cn-test.example:0", "not a port"},
        {"https://cn-test.example:65536", "not a port"},
        {"https://cn-test.example:123456", "not a port"},
        {"https://cn-test.example:8443x", "only"},
        {"https://cn-test.example:84:43", "only"},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        const char *why = NULL;
        CHECK(!yk_region_test_valid(bad[i].origin, &why) && why && strstr(why, bad[i].why));
        if (!(why && strstr(why, bad[i].why))) printf("  %s: %s\n", bad[i].origin, why ? why : "(valid)");
        // `region set` refuses it, nothing written
        start("cn");
        bool changed = true;
        why = NULL;
        CHECK(!yk_region_test_store(cn, bad[i].origin, &changed, &why) && !changed && why && s_writes == 0);
        CHECK(!s_dev[1][0] && SAME(yk_cloud_origin(), r.device_origin));
        // Written by hand all the same (or by a firmware that took it): left out, cn's the table's
        start_tested("cn", NULL, bad[i].origin);
        CHECK(SAME(yk_cloud_origin(), r.device_origin) && SAME(yk_session_url(), r.session_url) && !s_test);
    }
    CHECK(!yk_region_test_valid(NULL, NULL));
    // Valid: a port at the ends of its range, digits and dots and hyphens, the longest there is
    const char *good[] = {"https://a", "https://cn-test.example:1", "https://cn-test.example:65535",
                          "https://10.0.0.7:8443", "https://device-staging.tikpal.com.cn", longest};
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        const char *why = "x";
        CHECK(yk_region_test_valid(good[i], &why) && !why);
        start_tested("cn", NULL, good[i]);
        CHECK(SAME(yk_cloud_origin(), good[i]) && !strncmp(yk_session_url(), "wss://", 6));
        CHECK(strlen(yk_session_url()) < 96);  // Yay's s_server[96], yaykit_talk's url[96]
    }
    // A region not in the table: refused, nothing written
    start("cn");
    const char *why = NULL;
    CHECK(!yk_region_test_store(yk_region_find("mars"), CN_TEST, NULL, &why) && why && strstr(why, "not a region"));
    CHECK(!yk_region_test_store(yk_region_count(), CN_TEST, NULL, NULL) && !yk_region_test_store(-1, NULL, NULL, NULL));
    CHECK(s_writes == 0);
    char out[80] = "x";
    CHECK(!yk_region_test_stored(-1, out, sizeof(out)) && !out[0]);
    // One with only one row bad: that one left out, the other kept
    start_tested("cn", GLOBAL_TEST, "http://cn-test.example");
    CHECK(SAME(yk_cloud_origin(), r.device_origin) && SAME(yk_region_test_origin(YK_REGION_GLOBAL), GLOBAL_TEST));
}

/** `region set` and `clear`'s storing: by name, read again only at the next start. */
static void test_dev_store(void) {
    const int cn = yk_region_find("cn");
    yk_region_row_t r;
    CHECK(yk_region_row(cn, &r));
    bool changed = false;
    const char *why = "x";
    start(NULL);  // never activated: yk_acct made by the write
    CHECK(yk_region_current() == YK_REGION_GLOBAL);  // read at start (yk_account_init()), before any command
    CHECK(yk_region_test_store(cn, CN_TEST, &changed, &why) && changed && !why && SAME(s_dev[1], CN_TEST) && s_writes == 1);
    CHECK(SAME(yk_cloud_origin(), CONFIG_YK_CLOUD_ORIGIN) && !yk_region_test_origin(cn));  // not before the restart
    char next[80];
    CHECK(yk_region_test_stored(cn, next, sizeof(next)) && SAME(next, CN_TEST));
    CHECK(yk_region_test_stored(YK_REGION_GLOBAL, next, sizeof(next)) && !next[0]);
    // The same again: written, not a change
    CHECK(yk_region_test_store(cn, CN_TEST, &changed, NULL) && !changed && SAME(s_dev[1], CN_TEST));
    // Another: a change
    CHECK(yk_region_test_store(cn, "https://cn-test2.example", &changed, NULL) && changed);
    // Cleared: erased, a change; cleared again: nothing written, not a change
    const int writes = s_writes;
    CHECK(yk_region_test_store(cn, NULL, &changed, NULL) && changed && !s_dev[1][0] && s_writes == writes + 1);
    CHECK(yk_region_test_store(cn, NULL, &changed, NULL) && !changed && s_writes == writes + 1);
    // Global's beside it, each its own key
    CHECK(yk_region_test_store(YK_REGION_GLOBAL, GLOBAL_TEST, &changed, NULL) && changed && SAME(s_dev[0], GLOBAL_TEST));
    CHECK(!s_dev[1][0]);
    // NVS takes no write: false, as it was
    s_full = true;
    CHECK(!yk_region_test_store(YK_REGION_GLOBAL, CN_TEST, &changed, &why) && !changed && why && SAME(s_dev[0], GLOBAL_TEST));
    CHECK(!yk_region_test_store(YK_REGION_GLOBAL, NULL, &changed, &why) && !changed && SAME(s_dev[0], GLOBAL_TEST));
    s_full = false;
    // NVS not set up: nothing read, nothing written
    s_ready = false;
    CHECK(!yk_region_test_stored(cn, next, sizeof(next)) && !yk_region_test_store(cn, CN_TEST, NULL, NULL));
    s_ready = true;
    // At the next start, both in use
    snprintf(s_dev[1], sizeof(s_dev[1]), "%s", CN_TEST);
    const char was0[] = GLOBAL_TEST;
    start_tested("cn", was0, CN_TEST);
    CHECK(SAME(yk_cloud_origin(), CN_TEST) && SAME(yk_region_test_origin(YK_REGION_GLOBAL), GLOBAL_TEST));
    // Asked before nvs_flash_init(): global this once, the table's; read again (with the test deployments) next time
    start_tested("cn", NULL, CN_TEST);
    s_ready = false;
    CHECK(yk_region_current() == YK_REGION_GLOBAL && global_addresses() && !s_test);
    s_ready = true;
    CHECK(yk_region_current() == cn && SAME(yk_cloud_origin(), CN_TEST));
}
#endif

int main(void) {
    test_table();
    test_claims();
#if CONFIG_YK_REGIONS
    test_on();
    test_store();
#if CONFIG_YAY_DEV
    test_dev_none();
    test_dev_cn();
    test_dev_global();
    test_dev_bad();
    test_dev_store();
#else
    test_release();
#endif
#else
    test_off();
#endif
#if CONFIG_YK_REGIONS
    const char *setting = "on";
#else
    const char *setting = "off";
#endif
#ifdef TEST_OWN_ORIGIN
    const char *own = ", a development build's own origin";
#else
    const char *own = "";
#endif
#if CONFIG_YAY_DEV
    const char *build = "development";
#else
    const char *build = "release";
#endif
    printf("yk_region (CONFIG_YK_REGIONS %s, %s build%s): %d checks, %d failures\n", setting, build, own, checks, failures);
    return failures ? 1 : 0;
}
