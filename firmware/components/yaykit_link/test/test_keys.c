// Host tests for the unpaired mode's keys and addresses (yk_link_keys.c), with NVS, the random bytes, the app's number
// and the platform's fetch answered here: the address is made once and then the same, one for each app, a bad one kept
// is made again, it goes with the keys when the device leaves its account or is claimed (yk_link_keys_forget()), and
// a key is fetched only when there is none, it is old or a phone said otherwise.
#include <stdio.h>
#include <string.h>

#include "nvs.h"
#include "esp_random.h"
#include "../yk_link_priv.h"
#include "../yk_link_rules.h"
#include "../yk_link_seal.h"
#include "yaykit_link.h"
#include "yk_link_keep.h"

static int failures, checks;
#define CHECK(c)                                                         \
    do {                                                                 \
        checks++;                                                        \
        if (!(c)) {                                                      \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); \
            failures++;                                                  \
        }                                                                \
    } while (0)

// ---- what yk_link_keys.c takes from elsewhere

volatile bool yk_link_keep_running;
const char *esp_err_to_name(esp_err_t code) { return code == ESP_OK ? "ESP_OK" : "ERR"; }

static uint32_t s_app = 65280;
uint32_t yk_vc_app_number(void) { return s_app; }

static bool s_stale;
bool yk_seal_key_stale(int64_t fetched_s, int64_t now_s) {
    (void)fetched_s;
    (void)now_s;
    return s_stale;
}

static int s_fetches;
static uint32_t s_fetch_kid = 7;
esp_err_t yk_api_link_key_fetch(uint32_t *kid, uint8_t key[YK_SEAL_KEY]) {
    s_fetches++;
    *kid = s_fetch_kid;
    memset(key, (int)s_fetch_kid, YK_SEAL_KEY);
    return ESP_OK;
}

// The random bytes: from a script when one is set, else a counter (never the same twice).
static const uint8_t *s_script;
static size_t s_script_left;
static uint8_t s_counter;
void esp_fill_random(void *buf, size_t len) {
    uint8_t *b = buf;
    for (size_t i = 0; i < len; i++) {
        if (s_script_left) {
            *b++ = *s_script++;
            s_script_left--;
        } else {
            *b++ = (uint8_t)(0x11 * (i + 1) + ++s_counter);
        }
    }
}

// NVS in memory: one namespace is all yk_link_keys.c opens.
#define NVS_MAX 8
static struct {
    char ns[16], key[16];
    uint8_t value[64];
    size_t len;
} s_nvs[NVS_MAX];
static int s_nvs_n;
static char s_open_ns[16];
static bool s_nvs_broken;  // nvs_open fails (NVS full or not initialised)

static int nvs_find(const char *key) {
    for (int i = 0; i < s_nvs_n; i++)
        if (!strcmp(s_nvs[i].ns, s_open_ns) && !strcmp(s_nvs[i].key, key)) return i;
    return -1;
}
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *handle) {
    (void)mode;
    if (s_nvs_broken) return ESP_FAIL;
    snprintf(s_open_ns, sizeof(s_open_ns), "%s", ns);
    *handle = 1;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { (void)handle; }
esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle;
    return ESP_OK;
}
esp_err_t nvs_erase_all(nvs_handle_t handle) {
    (void)handle;
    for (int i = 0; i < s_nvs_n;)
        if (!strcmp(s_nvs[i].ns, s_open_ns)) s_nvs[i] = s_nvs[--s_nvs_n];
        else i++;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length) {
    (void)handle;
    int i = nvs_find(key);
    if (i < 0 && s_nvs_n < NVS_MAX) i = s_nvs_n++;
    if (i < 0 || length > sizeof(s_nvs[i].value)) return ESP_FAIL;
    snprintf(s_nvs[i].ns, sizeof(s_nvs[i].ns), "%s", s_open_ns);
    snprintf(s_nvs[i].key, sizeof(s_nvs[i].key), "%s", key);
    memcpy(s_nvs[i].value, value, length);
    s_nvs[i].len = length;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length) {
    (void)handle;
    const int i = nvs_find(key);
    if (i < 0) return ESP_ERR_NVS_NOT_FOUND;
    if (*length < s_nvs[i].len) return ESP_FAIL;
    memcpy(out, s_nvs[i].value, s_nvs[i].len);
    *length = s_nvs[i].len;
    return ESP_OK;
}
static const uint8_t *stored(const char *ns, const char *key, size_t *len) {
    for (int i = 0; i < s_nvs_n; i++)
        if (!strcmp(s_nvs[i].ns, ns) && !strcmp(s_nvs[i].key, key)) {
            if (len) *len = s_nvs[i].len;
            return s_nvs[i].value;
        }
    return NULL;
}

int main(void) {
    uint8_t a[6], again[6], other[6];

    // The first time: a static random address, kept under "a<app number>" in the component's own namespace.
    CHECK(yk_link_keys_addr(65280, a) && yk_rules_static_addr_ok(a));
    size_t len = 0;
    const uint8_t *kept = stored("yk_link_keys", "a65280", &len);
    CHECK(kept && len == 6 && !memcmp(kept, a, 6));
    // Then the same, every start.
    CHECK(yk_link_keys_addr(65280, again) && !memcmp(again, a, 6));
    CHECK(yk_link_keys_addr(65280, again) && !memcmp(again, a, 6));
    // Another app on this device has its own.
    CHECK(yk_link_keys_addr(267, other) && yk_rules_static_addr_ok(other) && memcmp(other, a, 6));
    CHECK(yk_link_keys_addr(65280, again) && !memcmp(again, a, 6));

    // Drawn all 0s (after the top bits) or all 1s: drawn again.
    {
        static const uint8_t script[] = {0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3F, 1, 2, 3, 4, 5, 6};
        s_script = script;
        s_script_left = sizeof(script);
        uint8_t fresh[6];
        CHECK(yk_link_keys_addr(300, fresh));
        const uint8_t want[6] = {1, 2, 3, 4, 5, 0xC6};
        CHECK(!memcmp(fresh, want, 6) && s_script_left == 0);
    }

    // One kept that is not a static random address (written by something else): made again and kept.
    {
        const uint8_t bad[6] = {1, 2, 3, 4, 5, 0x06};
        nvs_handle_t h;
        nvs_open("yk_link_keys", NVS_READWRITE, &h);
        nvs_set_blob(h, "a301", bad, 6);
        uint8_t fresh[6];
        CHECK(yk_link_keys_addr(301, fresh) && yk_rules_static_addr_ok(fresh) && memcmp(fresh, bad, 6));
        CHECK(!memcmp(stored("yk_link_keys", "a301", NULL), fresh, 6));
    }

    // Keys: none, then fetched; not again while fresh; again when old, when a phone said otherwise, or forced.
    s_app = 65280;
    CHECK(yk_link_unpaired_key_state() == YK_LINK_KEY_NONE);
    CHECK(yk_link_unpaired_refresh(false) == ESP_OK && s_fetches == 1 && yk_link_unpaired_key_state() == YK_LINK_KEY_OK);
    CHECK(yk_link_unpaired_refresh(false) == ESP_OK && s_fetches == 1);
    s_stale = true;
    CHECK(yk_link_unpaired_refresh(false) == ESP_OK && s_fetches == 2);
    s_stale = false;
    yk_link_keys_mismatch(65280, true);
    CHECK(yk_link_unpaired_key_state() == YK_LINK_KEY_MISMATCH);
    s_fetch_kid = 8;
    CHECK(yk_link_unpaired_refresh(false) == ESP_OK && s_fetches == 3 && yk_link_unpaired_key_state() == YK_LINK_KEY_OK);
    CHECK(yk_link_unpaired_refresh(true) == ESP_OK && s_fetches == 4);
    {
        uint8_t key[YK_SEAL_KEY];
        uint32_t kid = 0;
        CHECK(yk_link_keys_load(65280, key, &kid) && kid == 8 && key[0] == 8 && key[31] == 8);
    }
    // Not while Bluetooth is on (one thing online at a time).
    yk_link_keep_running = true;
    CHECK(yk_link_unpaired_refresh(true) == ESP_ERR_INVALID_STATE && s_fetches == 4);
    yk_link_keep_running = false;
    // A bench's key (development builds).
    {
        uint8_t key[YK_SEAL_KEY];
        memset(key, 0x5A, sizeof(key));
        CHECK(yk_link_keys_put(0x01020304, key));
        uint32_t kid = 0;
        CHECK(yk_link_keys_load(65280, key, &kid) && kid == 0x01020304);
    }

    // Unbound or claimed (forget()): every key and every address goes; the next start makes new addresses.
    yk_link_keys_forget();
    CHECK(!stored("yk_link_keys", "a65280", NULL) && !stored("yk_link_keys", "k65280", NULL) &&
          !stored("yk_link_keys", "a267", NULL));
    CHECK(yk_link_unpaired_key_state() == YK_LINK_KEY_NONE);
    CHECK(yk_link_keys_addr(65280, again) && yk_rules_static_addr_ok(again) && memcmp(again, a, 6));
    CHECK(yk_link_keys_addr(267, again) && memcmp(again, other, 6));

    // NVS will not open: still a static random address, for this time only (not kept: false).
    s_nvs_broken = true;
    uint8_t once[6];
    CHECK(!yk_link_keys_addr(65280, once) && yk_rules_static_addr_ok(once));
    s_nvs_broken = false;

    if (failures) {
        fprintf(stderr, "test_keys: %d of %d failed\n", failures, checks);
        return 1;
    }
    printf("test_keys: %d checks, 0 failures\n", checks);
    return 0;
}
