// The device's region and its addresses (yk_region.h). The one source file that reads the generated table
// (yk_regions_gen.h; the host tests read it too). With CONFIG_YAY_DEV and CONFIG_YK_REGIONS both on, also a development
// build's test deployment for a row (yk_region_test.h), read with `rg`.
#include "yk_region.h"

#include <stddef.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "yaykit_cloud.h"
#include "yk_region_store.h"
#include "yk_regions_gen.h"
#if CONFIG_YAY_DEV && CONFIG_YK_REGIONS
#include <stdio.h>

#include "esp_heap_caps.h"
#include "yk_region_test.h"
#endif

_Static_assert(YK_REGION_DEFAULT == YK_REGION_GLOBAL, "row 0 of the table is global");
_Static_assert(YK_REGION_COUNT >= 1, "the table has global's row");

int yk_region_count(void) { return YK_REGION_COUNT; }

bool yk_region_row(int row, yk_region_row_t *out) {
    if (row < 0 || row >= YK_REGION_COUNT || !out) return false;
    const yk_region_t *r = &YK_REGIONS[row];
    *out = (yk_region_row_t){.id = r->id, .claim_prefix = r->claim_prefix, .device_origin = r->device_origin,
                             .session_url = r->session_url, .onboarding_url = r->onboarding_url,
                             .computer_url = r->computer_url};
    return true;
}

int yk_region_find(const char *id) {
    if (!id) return -1;
    for (int i = 0; i < YK_REGION_COUNT; i++)
        if (!strcmp(id, YK_REGIONS[i].id)) return i;
    return -1;
}

int yk_region_for_claim(const char *claim) {
    if (!claim) return -1;
    int best = -1;
    size_t best_len = 0;
    for (int i = 0; i < YK_REGION_COUNT; i++) {
        const size_t n = strlen(YK_REGIONS[i].claim_prefix);
        if (n > best_len && !strncmp(claim, YK_REGIONS[i].claim_prefix, n)) {
            best = i;
            best_len = n;
        }
    }
    return best;
}

#if CONFIG_YK_REGIONS
static const char *TAG = "yk_region";
static int s_row = -1;  // yk_acct `rg`'s row once read; -1 not yet

#if CONFIG_YAY_DEV
// A development build's test addresses (yk_region_test.h): yk_acct `rgdev_<id>`, read with `rg` in the same NVS open and
// kept until the restart. In PSRAM, one entry per row, and only once a row has one: without, nothing is allocated.
typedef struct {
    char origin[YK_REGION_TEST_ORIGIN_MAX + 1];  // "": none for this row
    char session[96];                           // wss://, the origin's host and port, the table's path
} test_row_t;
static test_row_t *s_test;  // NULL: no row has one (or not read yet)

/** Row `row`'s key: "rgdev_" and its id (at most 9 characters, as for `bound_<id>`: test_account.c checks every row). */
static void test_key(int row, char key[NVS_KEY_NAME_MAX_SIZE]) {
    snprintf(key, NVS_KEY_NAME_MAX_SIZE, "rgdev_%s", YK_REGIONS[row].id);
}

/** Row `row`'s session for the test `origin` (a valid one): wss:// and its host and port, then the table's path (the
 *  contract puts a deployment's session on its device host). False: it does not fit `size`. */
static bool test_session(int row, const char *origin, char *out, size_t size) {
    const char *table = YK_REGIONS[row].session_url;  // wss://<host>/<path>: test_region.c holds every row so
    const char *path = strchr(table + strlen("wss://"), '/');
    const int n = snprintf(out, size, "wss://%s%s", origin + strlen("https://"), path ? path : "");
    return n > 0 && (size_t)n < size;
}

/** Every row's test origin, from the yk_acct `h` is open on, into s_test. A value that is not one is left out, said in
 *  the log: that row's addresses are the table's. Two tasks at once (not at start: yk_account_init() reads first): the
 *  same answer, one copy left over. */
static void test_load(nvs_handle_t h) {
    test_row_t *rows = NULL;
    for (int i = 0; i < YK_REGION_COUNT; i++) {
        char key[NVS_KEY_NAME_MAX_SIZE], origin[YK_REGION_TEST_ORIGIN_MAX + 2], session[sizeof(rows->session)];
        size_t len = sizeof(origin);
        test_key(i, key);
        const esp_err_t got = nvs_get_str(h, key, origin, &len);
        if (got == ESP_ERR_NVS_NOT_FOUND) continue;
        const char *why = "not a string, or too long";
        if (got != ESP_OK || !yk_region_test_valid(origin, &why) || !test_session(i, origin, session, sizeof(session))) {
            ESP_LOGW(TAG, "development: %s left out (%s): %s's addresses are the table's", key, why ? why : "too long",
                     YK_REGIONS[i].id);
            continue;
        }
        if (!rows) rows = heap_caps_calloc(YK_REGION_COUNT, sizeof(*rows), MALLOC_CAP_SPIRAM);
        if (!rows) {
            ESP_LOGW(TAG, "development: no memory for the test addresses: the table's");
            break;
        }
        strcpy(rows[i].origin, origin);
        strcpy(rows[i].session, session);
        ESP_LOGW(TAG, "development: %s is a test deployment's: %s and %s", YK_REGIONS[i].id, origin, session);
    }
    s_test = rows;
}
#endif

/** `rg`'s row, global without one. *sure false: NVS could not be read at all (not set up yet), so read again later. */
static int load(bool *sure) {
    *sure = true;
    nvs_handle_t h;
    const esp_err_t opened = nvs_open("yk_acct", NVS_READONLY, &h);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return YK_REGION_DEFAULT;  // no yk_acct: never activated
    if (opened != ESP_OK) {
        *sure = false;
        return YK_REGION_DEFAULT;
    }
    char id[24];
    size_t len = sizeof(id);
    const esp_err_t got = nvs_get_str(h, "rg", id, &len);
#if CONFIG_YAY_DEV
    test_load(h);  // a development build's test addresses, with `rg` (none without yk_acct: never set)
#endif
    nvs_close(h);
    if (got == ESP_ERR_NVS_NOT_FOUND) return YK_REGION_DEFAULT;  // every device before the region was stored
    const int row = got == ESP_OK ? yk_region_find(id) : -1;
    if (row >= 0) return row;
    // A name a newer firmware wrote, or a value that is not one: global, decided 11-21 (yk_region.h says why, and when
    // to look at it again: a third region).
    ESP_LOGW(TAG, "rg %s is not in this firmware's table: global", got == ESP_OK ? id : "(not a name)");
    return YK_REGION_DEFAULT;
}

bool yk_region_store(int row, bool *changed) {
    if (changed) *changed = false;
    if (row < 0 || row >= YK_REGION_COUNT) return false;
    const int before = yk_region_current();  // first: read after the write, it would be the new one
    nvs_handle_t h;
    if (nvs_open("yk_acct", NVS_READWRITE, &h) != ESP_OK) return false;
    // Changed: `rg` as stored names another side than this row (none is global). As stored, not the row it was read as:
    // a name this firmware does not know is global here, but what was kept for it (its tokens) is not global's.
    char was[24];
    size_t len = sizeof(was);
    const esp_err_t got = nvs_get_str(h, "rg", was, &len);
    const bool other = got == ESP_ERR_NVS_NOT_FOUND ? row != YK_REGION_DEFAULT : got != ESP_OK || strcmp(was, YK_REGIONS[row].id);
    const bool stored = nvs_set_str(h, "rg", YK_REGIONS[row].id) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (!stored) {
        ESP_LOGW(TAG, "rg %s not stored: the region stays %s", YK_REGIONS[row].id, YK_REGIONS[before].id);
        return false;
    }
    s_row = row;
    if (changed) *changed = other;
    ESP_LOGI(TAG, "rg %s%s", YK_REGIONS[row].id, other ? " (another side than before)" : "");
    return true;
}
#endif

int yk_region_current(void) {
#if CONFIG_YK_REGIONS
    int row = s_row;
    if (row < 0) {  // two tasks at once both read it: the same answer
        bool sure;
        row = load(&sure);
        if (sure) s_row = row;
    }
    return row;
#else
    return YK_REGION_DEFAULT;
#endif
}

const char *yk_region_name(void) { return YK_REGIONS[yk_region_current()].id; }

const char *yk_cloud_origin(void) {
#if CONFIG_YK_REGIONS
    // Global's stays the setting's: the table's in every build that does not set it (yaykit/tests/deployments.test.ts
    // and test/test_region.c hold the two equal), a development build's own where it does.
    const int row = yk_region_current();
#if CONFIG_YAY_DEV
    const char *test = yk_region_test_origin(row);  // `region set`: a test deployment stands in for this row
    if (test) return test;
#endif
    if (row != YK_REGION_DEFAULT) return YK_REGIONS[row].device_origin;
#endif
    return CONFIG_YK_CLOUD_ORIGIN;
}

#if CONFIG_YAY_DEV && CONFIG_YK_REGIONS
static bool lower_or_digit(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

bool yk_region_test_valid(const char *origin, const char **why) {
    static const char scheme[] = "https://";
    const char *bad = NULL;
    if (!origin || !*origin) bad = "empty";
    else if (strlen(origin) > YK_REGION_TEST_ORIGIN_MAX) bad = "too long";
    else if (strncmp(origin, scheme, sizeof(scheme) - 1)) bad = "not https://";
    else {
        const char *p = origin + sizeof(scheme) - 1;
        if (!lower_or_digit(*p)) bad = "no host (lowercase letters, digits, dots and hyphens)";
        while (lower_or_digit(*p) || *p == '.' || *p == '-') p++;
        if (!bad && *p == ':') {
            unsigned long port = 0;
            int digits = 0;
            for (p++; *p >= '0' && *p <= '9' && digits <= 5; p++, digits++) port = port * 10 + (unsigned long)(*p - '0');
            if (!digits || digits > 5 || !port || port > 65535) bad = "not a port (1-65535)";
        }
        if (!bad && *p) bad = "https://<host>[:port] only: lowercase, no path, query or user";
    }
    if (why) *why = bad;
    return !bad;
}

const char *yk_region_test_origin(int row) {
    (void)yk_region_current();  // read with `rg`, once
    return s_test && row >= 0 && row < YK_REGION_COUNT && s_test[row].origin[0] ? s_test[row].origin : NULL;
}

const char *yk_region_test_session(int row) { return yk_region_test_origin(row) ? s_test[row].session : NULL; }

bool yk_region_test_stored(int row, char *out, size_t size) {
    if (!out || !size) return false;
    out[0] = 0;
    if (row < 0 || row >= YK_REGION_COUNT) return false;
    nvs_handle_t h;
    const esp_err_t opened = nvs_open("yk_acct", NVS_READONLY, &h);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return true;  // no yk_acct: nothing stored
    if (opened != ESP_OK) return false;
    char key[NVS_KEY_NAME_MAX_SIZE];
    test_key(row, key);
    size_t len = size;
    const esp_err_t got = nvs_get_str(h, key, out, &len);
    nvs_close(h);
    if (got != ESP_OK) out[0] = 0;
    return got == ESP_OK || got == ESP_ERR_NVS_NOT_FOUND;
}

bool yk_region_test_store(int row, const char *origin, bool *changed, const char **why) {
    if (changed) *changed = false;
    if (why) *why = NULL;
    const char *bad = NULL;
    char session[sizeof(s_test->session)];
    if (row < 0 || row >= YK_REGION_COUNT) bad = "not a region of this firmware's table";
    else if (origin && yk_region_test_valid(origin, &bad) && !test_session(row, origin, session, sizeof(session)))
        bad = "too long";
    if (bad) {
        if (why) *why = bad;
        return false;
    }
    char was[YK_REGION_TEST_ORIGIN_MAX + 2];
    const bool read = yk_region_test_stored(row, was, sizeof(was));  // not read (too long, say): another than this one
    if (!origin && read && !was[0]) return true;                      // nothing to clear: nothing written
    char key[NVS_KEY_NAME_MAX_SIZE];
    test_key(row, key);
    nvs_handle_t h;
    esp_err_t err = nvs_open("yk_acct", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = origin ? nvs_set_str(h, key, origin) : nvs_erase_key(h, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;  // erasing what was not there
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) {
        if (why) *why = "NVS would not take it";
        return false;
    }
    if (changed) *changed = !read || strcmp(was, origin ? origin : "");
    ESP_LOGW(TAG, "development: %s %s%s, from the next start", key, origin ? "set to " : "cleared", origin ? origin : "");
    return true;
}
#endif
