// Host tests for yaykit_wifi.c itself, on stubs (stub_run/): a simulated radio with access points that come and go,
// esp_wifi's RAM and stored configurations, the event loop, the timers and NVS, all in simulated time. Checks what
// docs/delivery/2026-11-07-wifi-multi-network.md 7 asks of the device side: the list moved in, a new network added and
// not replacing, one that does not join leaving everything as it was (and the phone told so), the order, the next
// network when one cannot be joined, a hotspot that comes and goes, how long between rounds, no password in anything
// printed, forget. And how it is done: no esp_wifi call from the event task or under `lock`.
// Run: test/run.sh (needs IDF_PATH for ESP-IDF's cJSON).
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char out_buf[1 << 16];  // everything printed and logged, to look for passwords in
static size_t out_len;
static int test_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define printf test_printf
#include "../yaykit_wifi.c"
#undef printf

static int failures, checks;
#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

static bool verbose;
static bool str_is(const char *a, const char *b) { return a && !strcmp(a, b); }

static void out_add(const char *s) {
    const size_t n = strlen(s);
    if (out_len + n + 1 < sizeof(out_buf)) memcpy(out_buf + out_len, s, n + 1), out_len += n;
}

static int test_printf(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    out_add(line);
    if (verbose) fputs(line, stdout);
    return n;
}

void stub_log(char level, const char *tag, const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    char all[600];
    snprintf(all, sizeof(all), "%c %s: %s\n", level, tag, line);
    out_add(all);
    if (verbose) fputs(all, stdout);
}

const char *esp_err_to_name(esp_err_t err) { return err == ESP_OK ? "ESP_OK" : "ERR"; }
void *heap_caps_calloc(size_t n, size_t size, unsigned caps) {
    (void)caps;
    return calloc(n, size);
}

// ---- time, timers, events

static int64_t now_us;
int64_t esp_timer_get_time(void) { return now_us; }

struct stub_timer {
    esp_timer_cb_t cb;
    void *arg;
    int64_t due;
    bool armed;
};
static struct stub_timer timer_pool[4];
static int timer_count;

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out) {
    struct stub_timer *t = &timer_pool[timer_count++];
    *t = (struct stub_timer){.cb = args->callback, .arg = args->arg};
    *out = t;
    return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t us) {
    if (t->armed) return ESP_ERR_INVALID_STATE;  // as ESP-IDF: start_once on a running timer fails
    t->armed = true;
    t->due = now_us + (int64_t)us;
    return ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t t) {
    t->armed = false;
    return ESP_OK;
}

enum { EV_WIFI, EV_IP };
typedef struct {
    int64_t due;
    int base, id;
    uint8_t reason;
    int ap;      // the access point an attempt is at (-1 none)
    bool attempt;
} ev_t;
static ev_t queue[64];
static int queued;
static esp_event_handler_t wifi_handler, ip_handler;
esp_event_base_t const WIFI_EVENT = "WIFI_EVENT", IP_EVENT = "IP_EVENT";

static void post(int64_t after_us, int base, int id, uint8_t reason, int ap, bool attempt) {
    if (queued < 64) queue[queued++] = (ev_t){now_us + after_us, base, id, reason, ap, attempt};
}

esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *arg) {
    (void)id, (void)arg;
    if (base == WIFI_EVENT) wifi_handler = handler;
    else ip_handler = handler;
    return ESP_OK;
}

// ---- mutexes: one thread here, so a wait on one held would be a deadlock on the device

struct stub_mutex {
    int held;
};
static struct stub_mutex mutex_pool[4];
static int mutex_count, deadlocks;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &mutex_pool[mutex_count++]; }
SemaphoreHandle_t xSemaphoreCreateMutexWithCaps(unsigned caps) {
    (void)caps;
    return xSemaphoreCreateMutex();
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait) {
    if (s->held) {
        if (!wait) return pdFALSE;
        deadlocks++;
        return pdFALSE;
    }
    s->held++;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
    s->held--;
    return pdTRUE;
}

struct stub_group {
    EventBits_t bits;
};
static struct stub_group group;
EventGroupHandle_t xEventGroupCreate(void) { return &group; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t bits) { return g->bits |= bits; }
EventBits_t xEventGroupClearBits(EventGroupHandle_t g, EventBits_t bits) { return g->bits &= ~bits; }
static void run_ms(int ms);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t g, EventBits_t bits, BaseType_t clear, BaseType_t all,
                                TickType_t wait) {
    (void)clear, (void)all;
    for (TickType_t t = 0; (g->bits & bits) != bits && t < wait; t += 100) run_ms(100);
    return g->bits;
}

// ---- NVS

typedef struct {
    char key[16];
    uint8_t data[sizeof(yk_wifi_list_t)];
    size_t len;
    bool there;
} nvs_item_t;
static nvs_item_t nvs_chan, nvs_nets;
static int nets_writes;

static nvs_item_t *item(const char *key) {
    if (!strcmp(key, "chan")) return &nvs_chan;
    if (!strcmp(key, "nets")) return &nvs_nets;
    return NULL;
}
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *out) {
    (void)mode;
    *out = 1;
    return strcmp(ns, "yk_wifi") ? ESP_FAIL : ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_commit(nvs_handle_t h) {
    (void)h;
    return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *out) {
    (void)h;
    nvs_item_t *i = item(key);
    if (!i || !i->there) return ESP_ERR_NOT_FOUND;
    *out = i->data[0];
    return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value) {
    (void)h;
    nvs_item_t *i = item(key);
    i->data[0] = value, i->len = 1, i->there = true;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len) {
    (void)h;
    nvs_item_t *i = item(key);
    if (!i || !i->there) return ESP_ERR_NOT_FOUND;
    if (*len < i->len) return ESP_FAIL;
    memcpy(out, i->data, i->len);
    *len = i->len;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t len) {
    (void)h;
    nvs_item_t *i = item(key);
    memcpy(i->data, value, len);
    i->len = len, i->there = true;
    if (i == &nvs_nets) nets_writes++;
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key) {
    (void)h;
    nvs_item_t *i = item(key);
    if (!i->there) return ESP_ERR_NOT_FOUND;
    i->there = false;
    if (i == &nvs_nets) nets_writes++;
    return ESP_OK;
}

// ---- the air and esp_wifi

typedef struct {
    const char *ssid, *pass;
    uint8_t channel;
    bool on, broadcast;
    int8_t rssi;
} ap_t;
static ap_t air[8];
static int air_count;

static int add_ap(const char *ssid, const char *pass, uint8_t channel) {
    air[air_count] = (ap_t){ssid, pass, channel, true, true, -50};
    return air_count++;
}

static wifi_config_t ram_cfg, flash_cfg;
static wifi_storage_t storage;
static bool linked, attempting, drv_scanning, drop_on_store, in_event, flash_odd;
static int linked_ap = -1, connects, scans, flash_writes, misuse;
static int64_t scan_started_us[32];
static char tried[32][33];  // the name of each attempt, in order
static uint8_t tried_channel[32];
static wifi_ap_record_t scan_list[16];
static uint16_t scan_n;

/** esp_wifi called from the event task, or with `lock` held: not allowed. */
static void use_radio(void) {
    if (in_event || (lock && lock->held)) misuse++;
}

esp_netif_t *esp_netif_create_default_wifi_sta(void) { return (esp_netif_t *)1; }
esp_err_t esp_netif_init(void) { return ESP_OK; }
esp_err_t esp_netif_set_hostname(esp_netif_t *netif, const char *name) {
    (void)netif, (void)name;
    return ESP_OK;
}
esp_err_t esp_wifi_init(const wifi_init_config_t *config) {
    (void)config;
    ram_cfg = flash_cfg;
    return ESP_OK;
}
esp_err_t esp_wifi_set_storage(wifi_storage_t s) {
    storage = s;
    return ESP_OK;
}
esp_err_t esp_wifi_set_mode(wifi_mode_t mode) {
    (void)mode;
    return ESP_OK;
}
esp_err_t esp_wifi_get_config(wifi_interface_t interface, wifi_config_t *conf) {
    (void)interface;
    *conf = ram_cfg;
    return ESP_OK;
}
esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf) {
    (void)interface;
    use_radio();
    if (attempting) return ESP_ERR_WIFI_STATE;  // "sta is connecting, cannot set config"
    // esp_wifi writes nothing when the configuration is the one it holds in RAM (11-08 device run); here, stricter, a
    // field reaches flash only where it differs from the RAM configuration (what the device showed was all of it).
    bool wrote = false;
#define FIELD(f)                                                                                                        \
    if (storage == WIFI_STORAGE_FLASH && memcmp(&ram_cfg.sta.f, &conf->sta.f, sizeof(conf->sta.f))) {                \
        memcpy(&flash_cfg.sta.f, &conf->sta.f, sizeof(conf->sta.f));                                                  \
        wrote = true;                                                                                                   \
    }
    FIELD(ssid) FIELD(password) FIELD(scan_method) FIELD(bssid_set) FIELD(bssid) FIELD(channel) FIELD(listen_interval)
    FIELD(sort_method)
#undef FIELD
    ram_cfg = *conf;
    if (wrote) {
        flash_writes++;
        // flash_store's stand-in for the RAM configuration (no name, or a password of dashes) never reaches flash
        if ((flash_cfg.sta.ssid[0] == '-' && !flash_cfg.sta.ssid[1]) || !memcmp(flash_cfg.sta.password, "--------", 8))
            flash_odd = true;
        if (drop_on_store && linked) {
            linked = false;
            post(10000, EV_WIFI, WIFI_EVENT_STA_DISCONNECTED, WIFI_REASON_ASSOC_LEAVE, -1, false);
        }
    }
    return ESP_OK;
}
esp_err_t esp_wifi_start(void) {
    post(5000, EV_WIFI, WIFI_EVENT_STA_START, 0, -1, false);
    return ESP_OK;
}
esp_err_t esp_wifi_set_ps(wifi_ps_type_t type) {
    (void)type;
    return ESP_OK;
}

static bool cfg_is(const wifi_config_t *c, const char *ssid) {
    return strnlen((const char *)c->sta.ssid, 32) == strlen(ssid) && !memcmp(c->sta.ssid, ssid, strlen(ssid));
}

/** esp_wifi's stored copy: this name and this password, nothing after it. */
static bool flash_is(const char *ssid, const char *pass) {
    uint8_t want[64] = {0};
    memcpy(want, pass, strlen(pass));
    return cfg_is(&flash_cfg, ssid) && !memcmp(flash_cfg.sta.password, want, sizeof(want));
}

esp_err_t esp_wifi_connect(void) {
    use_radio();
    if (attempting || linked || drv_scanning) return ESP_ERR_WIFI_STATE;
    if (connects < 32) {
        snprintf(tried[connects], sizeof(tried[0]), "%.*s", (int)strnlen((const char *)ram_cfg.sta.ssid, 32),
                 (const char *)ram_cfg.sta.ssid);
        tried_channel[connects] = ram_cfg.sta.scan_method == WIFI_FAST_SCAN ? ram_cfg.sta.channel : 0;
    }
    connects++;
    attempting = true;
    int ap = -1;
    for (int i = 0; i < air_count; i++)
        if (air[i].on && cfg_is(&ram_cfg, air[i].ssid)) ap = i;  // a directed probe finds a hidden one too
    if (ap < 0) {
        post(2500000, EV_WIFI, WIFI_EVENT_STA_DISCONNECTED, WIFI_REASON_NO_AP_FOUND, -1, true);
        return ESP_OK;
    }
    const bool on_channel = ram_cfg.sta.scan_method == WIFI_FAST_SCAN && ram_cfg.sta.channel == air[ap].channel;
    const int64_t found = on_channel ? 150000 : 2000000;
    const char *pass = air[ap].pass;
    if (strnlen((const char *)ram_cfg.sta.password, 64) != strlen(pass) ||
        memcmp(ram_cfg.sta.password, pass, strlen(pass))) {
        post(found + 1000000, EV_WIFI, WIFI_EVENT_STA_DISCONNECTED, WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT, ap, true);
        return ESP_OK;
    }
    post(found + 300000, EV_WIFI, WIFI_EVENT_STA_CONNECTED, 0, ap, true);
    post(found + 600000, EV_IP, IP_EVENT_STA_GOT_IP, 0, ap, true);
    return ESP_OK;
}

static void cancel_attempt(void) {
    for (int i = 0; i < queued; i++)
        if (queue[i].attempt) queue[i--] = queue[--queued];
    attempting = false;
}

esp_err_t esp_wifi_disconnect(void) {
    use_radio();
    if (linked || attempting) {
        cancel_attempt();
        linked = false;
        post(5000, EV_WIFI, WIFI_EVENT_STA_DISCONNECTED, WIFI_REASON_ASSOC_LEAVE, -1, false);
    }
    return ESP_OK;
}

esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block) {
    (void)config, (void)block;
    use_radio();
    if (attempting || drv_scanning) return ESP_ERR_WIFI_STATE;
    drv_scanning = true;
    if (scans < 32) scan_started_us[scans] = now_us;
    scans++;
    post(3000000, EV_WIFI, WIFI_EVENT_SCAN_DONE, 0, -1, false);
    return ESP_OK;
}
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number) {
    *number = scan_n;
    return ESP_OK;
}
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *records) {
    if (*number > scan_n) *number = scan_n;
    if (records) memcpy(records, scan_list, *number * sizeof(*records));
    scan_n = 0;
    return ESP_OK;
}
esp_err_t esp_wifi_clear_ap_list(void) {
    scan_n = 0;
    return ESP_OK;
}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *info) {
    if (!linked) return ESP_FAIL;
    memset(info, 0, sizeof(*info));
    memcpy(info->ssid, air[linked_ap].ssid, strlen(air[linked_ap].ssid));
    info->primary = air[linked_ap].channel;
    info->rssi = air[linked_ap].rssi;
    return ESP_OK;
}
esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd) {
    (void)cmd;
    return ESP_OK;
}

static int failed_seen;  // times `state` was FAILED after a step

static void deliver(ev_t e) {
    if (e.base == EV_WIFI) {
        if (e.id == WIFI_EVENT_STA_CONNECTED) linked = true, linked_ap = e.ap, attempting = false;
        if (e.id == WIFI_EVENT_STA_DISCONNECTED) {
            if (e.attempt) attempting = false;
            linked = false;
        }
        if (e.id == WIFI_EVENT_SCAN_DONE) {
            drv_scanning = false;
            scan_n = 0;
            for (int i = 0; i < air_count && scan_n < 16; i++) {
                if (!air[i].on || !air[i].broadcast) continue;
                wifi_ap_record_t *r = &scan_list[scan_n++];
                memset(r, 0, sizeof(*r));
                memcpy(r->ssid, air[i].ssid, strlen(air[i].ssid));
                r->primary = air[i].channel;
                r->rssi = air[i].rssi;
                r->authmode = air[i].pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
            }
        }
        wifi_event_sta_disconnected_t d = {.reason = e.reason};
        in_event = true;
        wifi_handler(NULL, WIFI_EVENT, e.id, &d);
        in_event = false;
    } else {
        if (!linked) return;  // the link went before the address
        ip_event_got_ip_t got = {.ip_info.ip.addr = 0x0a01a8c0};
        in_event = true;
        ip_handler(NULL, IP_EVENT, e.id, &got);
        in_event = false;
    }
}

/** Runs the simulated time `ms` on: timers and radio events, in order. */
static void run_ms(int ms) {
    const int64_t end = now_us + (int64_t)ms * 1000;
    for (;;) {
        int64_t due = end + 1;
        int which = -1;
        bool is_timer = false;
        for (int i = 0; i < timer_count; i++)
            if (timer_pool[i].armed && timer_pool[i].due < due) due = timer_pool[i].due, which = i, is_timer = true;
        for (int i = 0; i < queued; i++)
            if (queue[i].due < due) due = queue[i].due, which = i, is_timer = false;
        if (which < 0 || due > end) break;
        now_us = due > now_us ? due : now_us;
        if (is_timer) {
            timer_pool[which].armed = false;
            timer_pool[which].cb(timer_pool[which].arg);
        } else {
            const ev_t e = queue[which];
            queue[which] = queue[--queued];
            deliver(e);
        }
        if (state == YK_WIFI_FAILED) failed_seen++;
    }
    now_us = end;
}

/** Waits up to `ms` for the state; returns the ms it took, or -1. */
static int run_until(yk_wifi_state_t want, int ms) {
    for (int t = 0; t <= ms; t += 50) {
        if (state == want) return t;
        run_ms(50);
    }
    return -1;
}

// ---- a start, again: yaykit_wifi.c's statics and the radio afresh; NVS and esp_wifi's stored copy kept

static void power_on(void) {
    if (nets) free(nets);
    nets = NULL;
    events = NULL, lock = NULL, radio = NULL, sta = NULL, connect_timer = NULL, joined_timer = NULL;
    state = YK_WIFI_OFF, fail_reason = 0, backoff_s = 0, scanning = false;
    memset(results, 0, sizeof(results)), result_count = 0, ip[0] = 0;
    memset(&timing, 0, sizeof(timing));
    phase = PH_IDLE, target = YK_WIFI_LIST_NONE, tries = 0, refused = gone = false, last_reason = 0;
    fresh_scan = trial_seen = false, join_seen = 0, flash_key = 0, commit_us = 0, joined_pending = false;
    memset(&pending, 0, sizeof(pending)), pending_set = false;
    timer_count = mutex_count = queued = 0;
    memset(timer_pool, 0, sizeof(timer_pool));
    memset(mutex_pool, 0, sizeof(mutex_pool));
    group.bits = 0;
    linked = attempting = drv_scanning = false, linked_ap = -1, scan_n = 0;
    connects = scans = flash_writes = nets_writes = failed_seen = 0;
    storage = WIFI_STORAGE_FLASH;
    memset(&ram_cfg, 0, sizeof(ram_cfg));
    CHECK(yk_wifi_start() == ESP_OK);
}

static void factory(void) {
    memset(&flash_cfg, 0, sizeof(flash_cfg));
    memset(&nvs_chan, 0, sizeof(nvs_chan));
    memset(&nvs_nets, 0, sizeof(nvs_nets));
    air_count = 0;
    drop_on_store = false;
}

static void flash_set(const char *ssid, const char *pass) {
    memset(&flash_cfg, 0, sizeof(flash_cfg));
    memcpy(flash_cfg.sta.ssid, ssid, strlen(ssid));
    memcpy(flash_cfg.sta.password, pass, strlen(pass));
    flash_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
}

static yk_wifi_list_t stored_list(void) {
    yk_wifi_list_t l;
    if (!nvs_nets.there || !yk_wifi_list_load(&l, nvs_nets.data, nvs_nets.len)) yk_wifi_list_clear(&l);
    return l;
}

static bool order_is(const yk_wifi_list_t *l, const char *a, const char *b, const char *c) {
    const char *want[] = {a, b, c};
    uint8_t o[YK_WIFI_LIST_MAX];
    const int n = yk_wifi_list_order(l, o);
    int expect = 0;
    while (expect < 3 && want[expect]) expect++;
    if (n != expect) return false;
    for (int k = 0; k < n; k++)
        if (l->nets[o[k]].ssid_len != strlen(want[k]) || memcmp(l->nets[o[k]].ssid, want[k], strlen(want[k])))
            return false;
    return l->cur == o[0];
}

static bool stored_is(const char *a, const char *b, const char *c) {
    const yk_wifi_list_t l = stored_list();
    return order_is(&l, a, b, c);
}

static bool joined_to(const char *ssid) { return state == YK_WIFI_CONNECTED && linked && !strcmp(air[linked_ap].ssid, ssid); }

static void phone_sends(const char *ssid, const char *pass) {
    wifi_prov_config_set_data_t in = {0};
    snprintf(in.ssid, sizeof(in.ssid), "%s", ssid);
    snprintf(in.password, sizeof(in.password), "%s", pass);
    wifi_prov_config_handlers_t *h = yk_wifi_config_handlers();
    CHECK(h->set_config_handler(&in, NULL) == ESP_OK);
    CHECK(h->apply_config_handler(NULL) == ESP_OK);
}

static void phone_scans(void) {
    wifi_prov_scan_handlers_t *h = yk_wifi_scan_handlers();
    CHECK(h->scan_start(false, false, 0, 0, NULL) == ESP_OK);
    run_ms(3500);
}

static wifi_prov_config_get_data_t phone_asks(void) {
    wifi_prov_config_get_data_t s;
    ((wifi_prov_config_handlers_t *)yk_wifi_config_handlers())->get_status_handler(&s, NULL);
    return s;
}

static void usb_join(const char *ssid, const char *pass) {
    cJSON *args = cJSON_CreateObject(), *reply = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "ssid", ssid);
    if (pass[0]) cJSON_AddStringToObject(args, "password", pass);
    const char *code = NULL;
    CHECK(yk_wifi_command("wifi.join", args, reply, &code) == ESP_OK);
    cJSON_Delete(args);
    cJSON_Delete(reply);
}

static void console(const char *sub) {
    char *argv[] = {"wifi", (char *)sub};
    wifi_cmd(2, argv);
}

// ---- the scenarios

static void test_moved_in(void) {
    // A device on 0.1.16: esp_wifi's stored copy and yk_wifi/chan, no list.
    factory();
    add_ap("Home", "homepass", 6);
    flash_set("Home", "homepass");
    nvs_set_u8(1, "chan", 6);
    power_on();
    CHECK(state == YK_WIFI_CONNECTING);
    CHECK(stored_is("Home", NULL, NULL));  // moved in at once, written once
    CHECK(nets_writes == 1);
    const int took = run_until(YK_WIFI_CONNECTED, 10000);
    CHECK(took >= 0 && took < 1500 && joined_to("Home"));
    CHECK(connects == 1 && tried_channel[0] == 6);  // the quick join as before: on yk_wifi/chan
    run_ms(2000);
    CHECK(flash_writes == 0 && nets_writes == 1);  // nothing else to write

    // The next start: the same, and nothing written.
    power_on();
    CHECK(run_until(YK_WIFI_CONNECTED, 10000) >= 0 && connects == 1 && tried_channel[0] == 6);
    run_ms(2000);
    CHECK(flash_writes == 0 && nets_writes == 0 && failed_seen == 0);

    // Nothing at all: unconfigured, nothing tried, nothing written.
    factory();
    power_on();
    run_ms(10000);
    CHECK(state == YK_WIFI_UNCONFIGURED && connects == 0 && scans == 0 && !nvs_nets.there);
}

static void test_new_network_adds(void) {
    factory();
    add_ap("Home", "homepass", 6);
    add_ap("Office", "officepass", 1);
    flash_set("Home", "homepass");
    power_on();
    CHECK(run_until(YK_WIFI_CONNECTED, 10000) >= 0);
    run_ms(2000);

    // The phone sends one (prov-config): added first, Home kept; esp_wifi's stored copy is the new one now.
    phone_scans();
    phone_sends("Office", "officepass");
    CHECK(state == YK_WIFI_CONNECTING);
    CHECK(run_until(YK_WIFI_CONNECTED, 15000) >= 0 && joined_to("Office"));
    CHECK(phone_asks().wifi_state == WIFI_PROV_STA_CONNECTED);
    run_ms(2000);
    yk_wifi_list_t l = stored_list();
    CHECK(order_is(&l, "Office", "Home", NULL));
    CHECK(!(l.nets[l.cur].flags & YK_WIFI_NET_BY_NAME) && l.nets[l.cur].channel == 1);  // the scan saw it
    CHECK(flash_is("Office", "officepass") && nvs_chan.data[0] == 1);

    // Over USB (wifi.join), the same: added, not replacing.
    add_ap("Cafe", "", 11);
    usb_join("Cafe", "");
    CHECK(run_until(YK_WIFI_CONNECTED, 15000) >= 0 && joined_to("Cafe"));
    run_ms(2000);
    l = stored_list();
    CHECK(order_is(&l, "Cafe", "Office", "Home"));
    CHECK(l.nets[l.cur].flags & YK_WIFI_NET_BY_NAME);  // no scan before it: tried by name, to be safe
    CHECK(flash_is("Cafe", ""));  // the open one after one with a password: none kept

    // The next start goes to the last joined, on its channel.
    power_on();
    CHECK(run_until(YK_WIFI_CONNECTED, 10000) >= 0 && joined_to("Cafe") && connects == 1 && tried_channel[0] == 11);
}

static void test_wrong_password(void) {
    factory();
    add_ap("Home", "homepass", 6);
    add_ap("Office", "officepass", 1);
    flash_set("Home", "homepass");
    power_on();
    CHECK(run_until(YK_WIFI_CONNECTED, 10000) >= 0);
    run_ms(2000);
    const int writes = nets_writes, stored = flash_writes;

    // A wrong password: not remembered; the phone is told so and it stays told while it is there.
    phone_sends("Office", "wrong-password");
    CHECK(run_until(YK_WIFI_FAILED, 30000) >= 0);
    wifi_prov_config_get_data_t s = phone_asks();
    CHECK(s.wifi_state == WIFI_PROV_STA_DISCONNECTED && s.fail_reason == WIFI_PROV_STA_AUTH_ERROR);
    const int attempts = connects;
    run_ms(40000);
    s = phone_asks();
    CHECK(state == YK_WIFI_FAILED && s.wifi_state == WIFI_PROV_STA_DISCONNECTED && connects == attempts);
    cJSON *info = cJSON_CreateObject();
    yk_wifi_describe(info);
    CHECK(str_is(cJSON_GetStringValue(cJSON_GetObjectItem(info, "state")), "failed"));
    CHECK(str_is(cJSON_GetStringValue(cJSON_GetObjectItem(info, "error")), "WRONG_PASSWORD"));
    cJSON_Delete(info);
    CHECK(nets_writes == writes && flash_writes == stored && flash_is("Home", "homepass"));
    CHECK(stored_is("Home", NULL, NULL));

    // The phone goes (its link off): back to Home, and the wrong password leaves esp_wifi.
    yk_wifi_drop_pending();
    CHECK(run_until(YK_WIFI_CONNECTED, 15000) >= 0 && joined_to("Home"));
    CHECK(cfg_is(&ram_cfg, "Home") && !memcmp(ram_cfg.sta.password, "homepass", 9));
    run_ms(2000);
    CHECK(nets_writes == writes && flash_writes == stored);

    // No phone to go (over USB, or the recovery): back after a minute.
    usb_join("Office", "wrong-password");
    CHECK(run_until(YK_WIFI_FAILED, 30000) >= 0);
    const int64_t failed_at = now_us;
    CHECK(run_until(YK_WIFI_CONNECTED, 80000) >= 0 && joined_to("Home"));
    CHECK(now_us - failed_at >= HOLD_MS * 1000LL);

    // A scan the phone asks for while it chooses again keeps it waiting.
    phone_sends("Office", "wrong-password");
    CHECK(run_until(YK_WIFI_FAILED, 30000) >= 0);
    run_ms(50000);
    phone_scans();
    run_ms(40000);
    CHECK(state == YK_WIFI_FAILED);
    // ... and the right password then joins, added first.
    phone_sends("Office", "officepass");
    CHECK(run_until(YK_WIFI_CONNECTED, 15000) >= 0 && joined_to("Office"));
    run_ms(2000);
    CHECK(stored_is("Office", "Home", NULL));

    // A network not there at all: NOT_FOUND, the same way.
    phone_sends("Nowhere", "12345678");
    CHECK(run_until(YK_WIFI_FAILED, 30000) >= 0);
    s = phone_asks();
    CHECK(s.wifi_state == WIFI_PROV_STA_DISCONNECTED && s.fail_reason == WIFI_PROV_STA_AP_NOT_FOUND);
    yk_wifi_drop_pending();
    CHECK(run_until(YK_WIFI_CONNECTED, 15000) >= 0 && joined_to("Office"));
    run_ms(2000);
    CHECK(stored_is("Office", "Home", NULL));
}

static void test_first_setup(void) {
    factory();
    add_ap("Home", "homepass", 6);
    power_on();
    CHECK(state == YK_WIFI_UNCONFIGURED);
    // A wrong password at the first setup: failed, and nothing to go back to.
    phone_scans();
    phone_sends("Home", "wrong-password");
    CHECK(run_until(YK_WIFI_FAILED, 30000) >= 0);
    yk_wifi_drop_pending();
    run_ms(5000);
    const int attempts = connects;
    run_ms(120000);
    CHECK(state == YK_WIFI_FAILED && connects == attempts && phase == PH_IDLE);
    static const wifi_config_t none;
    CHECK(!memcmp(ram_cfg.sta.password, none.sta.password, 64));  // the password is gone from esp_wifi
    CHECK(!nvs_nets.there && !flash_cfg.sta.ssid[0]);
    CHECK(yk_wifi_retry_now("test") == ESP_ERR_INVALID_STATE);
    // The right one.
    phone_sends("Home", "homepass");
    CHECK(run_until(YK_WIFI_CONNECTED, 15000) >= 0 && joined_to("Home"));
    run_ms(2000);
    CHECK(stored_is("Home", NULL, NULL) && flash_is("Home", "homepass") && nvs_chan.data[0] == 6);
    // A restart before any of that was stored would have been unconfigured: here it joins.
    power_on();
    CHECK(run_until(YK_WIFI_CONNECTED, 10000) >= 0);
}

static void test_last_one_gone(void) {
    // The last joined was the phone's hotspot, now off; at home: Home after a few seconds, not a minute.
    factory();
    add_ap("Home", "homepass", 6);
    const int hotspot = add_ap("Alex\xe2\x80\x99s iPhone", "hotspot1", 11);
    flash_set("Home", "homepass");
    nvs_set_u8(1, "chan", 6);
    power_on();
    CHECK(run_until(YK_WIFI_CONNECTED, 10000) >= 0);
    run_ms(2000);
    phone_scans();
    phone_sends("Alex\xe2\x80\x99s iPhone", "hotspot1");
    CHECK(run_until(YK_WIFI_CONNECTED, 15000) >= 0);
    run_ms(2000);
    air[hotspot].on = false;
    power_on();
    const int took = run_until(YK_WIFI_CONNECTED, 60000);
    CHECK(took >= 0 && took < 10000 && joined_to("Home"));
    CHECK(failed_seen == 0);  // 连接中, not 没网, while it looks
    CHECK(connects == 2 && !strcmp(tried[0], "Alex\xe2\x80\x99s iPhone") && !strcmp(tried[1], "Home"));
    CHECK(tried_channel[1] == 6);  // on the channel the scan saw it
    run_ms(2000);
    CHECK(stored_is("Home", "Alex\xe2\x80\x99s iPhone", NULL));
    CHECK(flash_is("Home", "homepass") && nvs_chan.data[0] == 6);
    // ... so the next start goes to Home at once, not to the hotspot again first.
    power_on();
    CHECK(run_until(YK_WIFI_CONNECTED, 10000) >= 0 && joined_to("Home") && connects == 1);
    CHECK(stored_is("Home", "Alex\xe2\x80\x99s iPhone", NULL));

    // With one network only, the quick retries stay as they were (a router that blinked).
    factory();
    const int home = add_ap("Home", "homepass", 6);
    flash_set("Home", "homepass");
    power_on();
    CHECK(run_until(YK_WIFI_CONNECTED, 10000) >= 0);
    air[home].on = false;
    power_on();
    run_ms(25000);
    CHECK(connects >= QUICK_TRIES && !strcmp(tried[QUICK_TRIES - 1], "Home"));
}

static void test_order_and_waits(void) {
    factory();
    const int home = add_ap("Home", "homepass", 6), office = add_ap("Office", "officepw1", 1),
              phone = add_ap("Phone", "phonepw1", 11);
    flash_set("Office", "officepw1");
    power_on();
    CHECK(run_until(YK_WIFI_CONNECTED, 10000) >= 0);
    phone_scans();
    phone_sends("Home", "homepass");
    CHECK(run_until(YK_WIFI_CONNECTED, 15000) >= 0);
    phone_scans();
    phone_sends("Phone", "phonepw1");
    CHECK(run_until(YK_WIFI_CONNECTED, 15000) >= 0);
    run_ms(2000);
    CHECK(stored_is("Phone", "Home", "Office"));

    // Out: nothing there. One round, then 15 s, 30 s, 60 s, 60 s between them; 没网 after the first.
    air[home].on = air[office].on = air[phone].on = false;
    power_on();
    run_ms(5000);
    run_ms(200000);
    CHECK(state == YK_WIFI_FAILED && scans >= 5);
    const int64_t gap1 = scan_started_us[1] - scan_started_us[0], gap2 = scan_started_us[2] - scan_started_us[1],
                  gap3 = scan_started_us[3] - scan_started_us[2], gap4 = scan_started_us[4] - scan_started_us[3];
    // each gap: the scan, Office by name (moved in from esp_wifi's copy: not known to be seen), and the wait
    CHECK(gap2 - gap1 == 15000000 && gap3 - gap2 == 30000000 && gap4 == gap3);
    CHECK(gap1 == 3000000 + 3510000 + 15000000);
    if (verbose) printf("gaps %lld %lld %lld %lld\n", (long long)gap1, (long long)gap2, (long long)gap3, (long long)gap4);

    // The key wakes the screen: a round now, not at the end of the wait.
    const int before = scans;
    CHECK(yk_wifi_retry_now("the key woke the screen") == ESP_OK);
    run_ms(100);
    CHECK(scans == before + 1);
    CHECK(yk_wifi_retry_now("again") == ESP_ERR_INVALID_STATE);  // looking already

    // The phone's hotspot comes and goes: joined in the first round whose scan sees it.
    run_ms(10000);
    air[phone].on = true;
    CHECK(run_until(YK_WIFI_CONNECTED, 70000) >= 0 && joined_to("Phone"));
    CHECK(yk_wifi_retry_now("connected") == ESP_ERR_INVALID_STATE);

    // Two seen: the most recently used first. Home's password changed: refused once, then Office.
    air[phone].on = false;
    air[home].on = air[office].on = true;
    air[home].pass = "changed!";
    power_on();
    CHECK(run_until(YK_WIFI_CONNECTED, 60000) >= 0 && joined_to("Office"));
    // Phone (gone: one try), the round: Home (refused: once), Office.
    CHECK(connects == 3 && !strcmp(tried[0], "Phone") && !strcmp(tried[1], "Home") && !strcmp(tried[2], "Office"));
    run_ms(2000);
    CHECK(stored_is("Office", "Phone", "Home"));
}

static void test_by_name(void) {
    // A network that hides its name, set up by typing it: tried by name in every round, seen or not.
    factory();
    const int home = add_ap("Home", "homepass", 6);
    const int hidden = add_ap("Hidden", "hiddenpw", 3);
    air[hidden].broadcast = false;
    flash_set("Home", "homepass");
    power_on();
    CHECK(run_until(YK_WIFI_CONNECTED, 10000) >= 0);
    phone_scans();
    phone_sends("Hidden", "hiddenpw");
    CHECK(run_until(YK_WIFI_CONNECTED, 15000) >= 0 && joined_to("Hidden"));
    run_ms(2000);
    yk_wifi_list_t l = stored_list();
    CHECK(l.nets[l.cur].flags & YK_WIFI_NET_BY_NAME);
    // Back home and the last joined is Home; Home goes; the hidden one is found by name.
    phone_sends("Home", "homepass");
    CHECK(run_until(YK_WIFI_CONNECTED, 15000) >= 0 && joined_to("Home"));
    run_ms(2000);
    air[home].on = false;
    power_on();
    CHECK(run_until(YK_WIFI_CONNECTED, 60000) >= 0 && joined_to("Hidden"));
}

static void test_dropped(void) {
    factory();
    const int home = add_ap("Home", "homepass", 6);
    add_ap("Office", "officepw1", 1);
    flash_set("Home", "homepass");
    power_on();
    CHECK(run_until(YK_WIFI_CONNECTED, 10000) >= 0);
    // The router blinks: the same network again, quickly.
    linked = false;
    post(0, EV_WIFI, WIFI_EVENT_STA_DISCONNECTED, WIFI_REASON_BEACON_TIMEOUT, -1, false);
    run_ms(10);
    CHECK(state == YK_WIFI_CONNECTING);
    CHECK(run_until(YK_WIFI_CONNECTED, 5000) >= 0 && joined_to("Home"));
    // Home goes for good, Office is remembered too: Office.
    phone_scans();
    phone_sends("Office", "officepw1");
    CHECK(run_until(YK_WIFI_CONNECTED, 15000) >= 0);
    phone_sends("Home", "homepass");
    CHECK(run_until(YK_WIFI_CONNECTED, 15000) >= 0 && joined_to("Home"));
    run_ms(2000);
    air[home].on = false;
    linked = false;
    post(0, EV_WIFI, WIFI_EVENT_STA_DISCONNECTED, WIFI_REASON_BEACON_TIMEOUT, -1, false);
    run_ms(10);
    const int took = run_until(YK_WIFI_CONNECTED, 30000);
    CHECK(took >= 0 && took < 10000 && joined_to("Office"));
    // Dropped while the phone has the device scan (connected scans leave the link up): joined again after the scan,
    // not left saying connected with no link (before 0.1.17 it was).
    run_ms(2000);
    CHECK(((wifi_prov_scan_handlers_t *)yk_wifi_scan_handlers())->scan_start(false, false, 0, 0, NULL) == ESP_OK);
    run_ms(500);
    linked = false;
    post(0, EV_WIFI, WIFI_EVENT_STA_DISCONNECTED, WIFI_REASON_BEACON_TIMEOUT, -1, false);
    run_ms(10);
    CHECK(state == YK_WIFI_CONNECTING);
    CHECK(run_until(YK_WIFI_CONNECTED, 10000) >= 0 && joined_to("Office"));
    // yk_wifi_reconnect, unchanged: the same network again.
    run_ms(2000);
    CHECK(yk_wifi_reconnect("test") == ESP_OK && state == YK_WIFI_CONNECTING);
    CHECK(run_until(YK_WIFI_CONNECTED, 5000) >= 0 && joined_to("Office"));
}

static void test_store_drops_link(void) {
    // Should writing esp_wifi's stored copy ever drop the link, it is joined again.
    factory();
    add_ap("Home", "homepass", 6);
    add_ap("Office", "officepw1", 1);
    flash_set("Home", "homepass");
    power_on();
    CHECK(run_until(YK_WIFI_CONNECTED, 10000) >= 0);
    drop_on_store = true;
    phone_scans();
    phone_sends("Office", "officepw1");
    CHECK(run_until(YK_WIFI_CONNECTED, 15000) >= 0);
    run_ms(1500);  // the store, and the link it dropped
    CHECK(flash_is("Office", "officepw1"));
    CHECK(run_until(YK_WIFI_CONNECTED, 10000) >= 0 && joined_to("Office"));
    run_ms(5000);
    CHECK(joined_to("Office"));
}

static void test_full_and_list(void) {
    factory();
    const char *names[] = {"A-net", "B-net", "C-net", "D-net", "E-net", "F-net"};
    const char *pass[] = {"secretA1", "secretB2", "secretC3", "secretD4", "secretE5", "secretF6"};
    for (int i = 0; i < 6; i++) add_ap(names[i], pass[i], (uint8_t)(1 + i));
    power_on();
    for (int i = 0; i < 6; i++) {
        usb_join(names[i], pass[i]);
        CHECK(run_until(YK_WIFI_CONNECTED, 15000) >= 0 && joined_to(names[i]));
        run_ms(2000);
    }
    yk_wifi_list_t l = stored_list();
    CHECK(yk_wifi_list_count(&l) == 5 && yk_wifi_list_find(&l, (const uint8_t *)"A-net", 5) < 0);
    uint8_t o[YK_WIFI_LIST_MAX];
    yk_wifi_list_order(&l, o);
    CHECK(!memcmp(l.nets[o[0]].ssid, "F-net", 5) && !memcmp(l.nets[o[4]].ssid, "B-net", 5) && l.cur == o[0]);

    out_len = 0, out_buf[0] = 0;
    console("list");
    CHECK(strstr(out_buf, "YK_WIFI_NET order=1 last=1 channel=6 by_name=1 ssid=F-net\n") != NULL);
    CHECK(strstr(out_buf, "YK_WIFI_NET order=5 last=0 channel=2 by_name=1 ssid=B-net\n") != NULL);
    CHECK(strstr(out_buf, "YK_WIFI_LIST count=5 max=5\n") != NULL);
    CHECK(strstr(out_buf, "YK_WIFI {\"state\":\"connected\",\"ssid\":\"F-net\"") != NULL);

    // Forget: every one, esp_wifi's stored copy and the channel; unconfigured, also after a restart.
    console("forget");
    CHECK(state == YK_WIFI_UNCONFIGURED && !nvs_nets.there && !nvs_chan.there && !flash_cfg.sta.ssid[0]);
    run_ms(30000);
    CHECK(state == YK_WIFI_UNCONFIGURED);
    power_on();
    run_ms(10000);
    CHECK(state == YK_WIFI_UNCONFIGURED && connects == 0);
}

static void test_built_before(void) {
    // An app built before the list set a network over USB (esp_wifi's stored copy only): taken in, first.
    factory();
    add_ap("Home", "homepass", 6);
    add_ap("Office", "officepw1", 1);
    flash_set("Home", "homepass");
    power_on();
    CHECK(run_until(YK_WIFI_CONNECTED, 10000) >= 0);
    run_ms(2000);
    flash_set("Office", "officepw1");
    power_on();
    CHECK(run_until(YK_WIFI_CONNECTED, 15000) >= 0 && joined_to("Office"));
    run_ms(2000);
    CHECK(stored_is("Office", "Home", NULL));
}

int main(int argc, char **argv) {
    verbose = argc > 1 && !strcmp(argv[1], "-v");
    test_moved_in();
    test_new_network_adds();
    test_wrong_password();
    test_first_setup();
    test_last_one_gone();
    test_order_and_waits();
    test_by_name();
    test_dropped();
    test_store_drops_link();
    test_full_and_list();
    test_built_before();
    // Through all of it: no password printed or logged; no esp_wifi call from the event task or under `lock`;
    // no wait on a mutex already held (a deadlock on the device); flash never held flash_store's stand-in.
    static const char *const secrets[] = {"homepass", "officepass", "officepw1", "wrong-password", "hotspot1",
                                          "phonepw1", "hiddenpw", "changed!", "secretA1", "secretF6"};
    for (size_t i = 0; i < sizeof(secrets) / sizeof(secrets[0]); i++) CHECK(!strstr(out_buf, secrets[i]));
    CHECK(misuse == 0);
    CHECK(deadlocks == 0);
    CHECK(!flash_odd);
    printf("yaykit_wifi on stubs: %d checks, %d failures\n", checks, failures);
    fputs(out_buf + (out_len > 0 && failures ? 0 : out_len), stdout);
    return failures ? 1 : 0;
}
