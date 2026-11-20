#include "yaykit_wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "wifi_provisioning/wifi_config.h"
#include "wifi_provisioning/wifi_scan.h"
#include "yk_wifi_creds.h"
#include "yk_wifi_list.h"
#include "yk_wifi_timing.h"

static const char *TAG = "yk_wifi";

#define CONNECTED_BIT BIT0
#define SCAN_DONE_BIT BIT1
#define MAX_RESULTS 16
#define QUICK_TRIES 5     // a start, new credentials or a dropped connection: the first attempt and 4 fast retries
#define HOLD_MS 60000     // a new network that did not join: said so until the phone goes or this long passes
#define TRIAL 0xfe        // `target`: the network the phone or the computer just sent, not remembered yet

static EventGroupHandle_t events;
static SemaphoreHandle_t lock;
static esp_netif_t *sta;
static esp_timer_handle_t connect_timer;
static volatile yk_wifi_state_t state = YK_WIFI_OFF;
static wifi_prov_sta_fail_reason_t fail_reason;
static int backoff_s;
static volatile int slow_s;  // yk_wifi_set_slow: the least wait between rounds (0: none)
static volatile bool scanning;
static wifi_ap_record_t results[MAX_RESULTS];
static uint16_t result_count;
static char ip[16];
static yk_wifi_timing_t timing;  // under `lock`: marked from several tasks, read by yk_wifi_describe

static const char *const STATE_NAMES[] = {"off", "unconfigured", "connecting", "connected", "failed"};

// ---- the remembered networks (yk_wifi_list.h) ----
//
// What to try next is decided in one place, step() in on_connect_timer (the esp_timer task), holding `radio` while it
// calls esp_wifi; the event handler only notes what happened and sets the timer (the system event task's stack is
// 2304 bytes). `lock` guards what the event task reads or writes, and is never held across an esp_wifi call.
//
// Every attempt runs with esp_wifi's configuration in RAM. Once one has joined, the list is written (yk_wifi/nets)
// and so is esp_wifi's own stored copy (nvs.net80211), which apps built before the list, the recovery and older Yay
// versions read: they join the network last joined, as they always did.

typedef enum {
    PH_IDLE,   // connected, or nothing to try
    PH_QUICK,  // `target` (the last joined, or TRIAL), up to QUICK_TRIES times
    PH_SCAN,   // a round's scan
    PH_ROUND,  // a round's networks, one after the other
    PH_WAIT,   // a round joined nothing: the next at the end of backoff_s
    PH_HOLD,   // a new network that did not join: the phone is told so; then back to the remembered ones
} phase_t;

typedef struct {  // in PSRAM: internal RAM keeps only the pointer
    yk_wifi_list_t list;
    yk_wifi_round_t round;
    uint8_t seen[YK_WIFI_LIST_MAX];  // the channel each slot was seen on in the last scan; 0 not seen
} nets_t;

static nets_t *nets;
static SemaphoreHandle_t radio;  // the radio's actions one at a time; never taken by the event task
static volatile uint8_t phase = PH_IDLE;
static volatile uint8_t target = YK_WIFI_LIST_NONE;  // a slot, or TRIAL
static uint8_t tries;                              // attempts at `target` in PH_QUICK
static volatile bool refused, gone;                // the last attempt: its password refused / its network not there
static volatile uint8_t last_reason;
static bool fresh_scan;      // under `lock`: a scan ended since the last round was planned
static bool trial_seen;      // the new network was in the last scan
static int8_t join_seen;     // the network being tried was in the scan before: 1, 0, or -1 not known
static uint32_t flash_key;   // which network esp_wifi's stored copy holds (a hash of name and password; 0 none)
static volatile int64_t commit_us;  // when that copy was last written while connected
static volatile bool joined_pending;  // an address came: what joined is still to be remembered (remember_joined)

/** Overwrites credentials in a way the compiler keeps, even for a buffer about to go out of scope. */
static void wipe(void *p, size_t len) {
    volatile uint8_t *bytes = p;
    while (len--) *bytes++ = 0;
}

static void timing_mark(void (*mark)(yk_wifi_timing_t *, int64_t)) {
    xSemaphoreTake(lock, portMAX_DELAY);
    mark(&timing, esp_timer_get_time());
    xSemaphoreGive(lock);
}

static bool known(void) { return nets && yk_wifi_list_count(&nets->list) > 0; }

static size_t ssid_len(const uint8_t ssid[32]) { return strnlen((const char *)ssid, 32); }

static uint32_t key_of(const wifi_config_t *c) {
    const size_t n = ssid_len(c->sta.ssid);
    if (!n) return 0;
    uint32_t h = 2166136261u;  // FNV-1a over the name, a separator and the password
    for (size_t i = 0; i < n; i++) h = (h ^ c->sta.ssid[i]) * 16777619u;
    h = (h ^ 0xff) * 16777619u;
    for (size_t i = 0; i < 64 && c->sta.password[i]; i++) h = (h ^ c->sta.password[i]) * 16777619u;
    return h ? h : 1;
}

/** Whether two configurations are the same network with the same password. */
static bool same_network(const wifi_config_t *a, const wifi_config_t *b) {
    const size_t n = ssid_len(a->sta.ssid);
    if (n != ssid_len(b->sta.ssid) || memcmp(a->sta.ssid, b->sta.ssid, n)) return false;
    for (size_t i = 0; i < 64; i++) {
        if (a->sta.password[i] != b->sta.password[i]) return false;
        if (!a->sta.password[i]) break;
    }
    return true;
}

static void connect_later(int ms) {
    esp_timer_stop(connect_timer);
    esp_timer_start_once(connect_timer, (uint64_t)ms * 1000);
}

// The channel the network in esp_wifi's stored copy was last joined on (NVS yk_wifi/chan): the next join scans that
// channel first and stops at the first match, instead of listening on all 13 for the strongest (about 2.5 s at every
// start, an app switch included). A network that moved is still found: the scan goes on to the other channels.
static esp_timer_handle_t joined_timer;

static uint8_t hint_load(void) {
    uint8_t ch = 0;
    nvs_handle_t h;
    if (nvs_open("yk_wifi", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "chan", &ch);
        nvs_close(h);
    }
    return ch >= 1 && ch <= 14 ? ch : 0;
}

static void hint_save(uint8_t ch) {
    if (!ch || ch == hint_load()) return;
    nvs_handle_t h;
    if (nvs_open("yk_wifi", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_u8(h, "chan", ch) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

/** No network, or one whose channel is not known. */
static void hint_drop(void) {
    nvs_handle_t h;
    if (nvs_open("yk_wifi", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_erase_key(h, "chan") == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

/** The list into NVS (yk_wifi/nets, one blob). Holding `radio`, as every change to the list does. */
static void nets_save(void) {
    nvs_handle_t h;
    if (nvs_open("yk_wifi", NVS_READWRITE, &h) != ESP_OK) return;
    if (known() ? nvs_set_blob(h, "nets", &nets->list, sizeof(nets->list)) == ESP_OK
                : nvs_erase_key(h, "nets") == ESP_OK)
        nvs_commit(h);
    nvs_close(h);
}

/** `c` into esp_wifi's stored copy (nvs.net80211); esp_wifi's RAM configuration becomes `c` too. Holding `radio`.
 *  esp_wifi writes nothing when the configuration is the one it holds in RAM, and a network that has just joined is
 *  that one already (it was tried from RAM): stored as it was, the old network stayed in flash (11-08 device run).
 *  So first, in RAM only, one that differs from `c` in every field set here, then `c` with storage FLASH. Flash never
 *  holds that other one: a power cut leaves the old network or the new one. Changing the RAM configuration leaves the
 *  link as it is (the same run). */
static void flash_store(wifi_config_t *c) {
    wifi_config_t other = {0};
    if (!c->sta.ssid[0]) other.sta.ssid[0] = '-';
    if (!c->sta.password[0]) memset(other.sta.password, '-', 8);
    other.sta.channel = c->sta.channel ? 0 : 1;
    other.sta.scan_method = c->sta.scan_method == WIFI_FAST_SCAN ? WIFI_ALL_CHANNEL_SCAN : WIFI_FAST_SCAN;
    other.sta.sort_method =
        c->sta.sort_method == WIFI_CONNECT_AP_BY_SIGNAL ? WIFI_CONNECT_AP_BY_SECURITY : WIFI_CONNECT_AP_BY_SIGNAL;
    commit_us = esp_timer_get_time();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &other);  // storage is RAM
    if (err == ESP_OK) {
        esp_wifi_set_storage(WIFI_STORAGE_FLASH);
        err = esp_wifi_set_config(WIFI_IF_STA, c);
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
    }
    if (err == ESP_OK) {
        flash_key = key_of(c);
    } else {
        esp_wifi_set_config(WIFI_IF_STA, c);  // RAM as it was; flash_key unchanged, so the next join tries again
        ESP_LOGW(TAG, "storing the network: %s", esp_err_to_name(err));
    }
}

/** At the start, before esp_wifi_start(): the list from NVS, with esp_wifi's stored network taken into it. */
static void load_networks(void) {
    wifi_config_t c = {0};
    esp_wifi_get_config(WIFI_IF_STA, &c);  // storage is still FLASH here: nvs.net80211's
    flash_key = key_of(&c);
    bool have = false;
    nvs_handle_t h;
    if (nvs_open("yk_wifi", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(nets->list);
        have = nvs_get_blob(h, "nets", &nets->list, &len) == ESP_OK && yk_wifi_list_load(&nets->list, &nets->list, len);
        nvs_close(h);
    }
    if (!have) yk_wifi_list_clear(&nets->list);
    if (yk_wifi_list_adopt(&nets->list, c.sta.ssid, ssid_len(c.sta.ssid), c.sta.password, hint_load())) {
        ESP_LOGI(TAG, "%s the stored network into the list", have ? "took" : "moved");
        nets_save();
    }
    wipe(&c, sizeof(c));
    target = nets->list.cur;
}

/** One attempt at `target`. Holding `radio`. */
static void attempt(void) {
    char name[33] = "";
    uint8_t ch = 0;
    if (target == TRIAL) {
        join_seen = trial_seen;
    } else {
        wifi_config_t c = {0}, now;
        xSemaphoreTake(lock, portMAX_DELAY);
        const yk_wifi_net_t *n = &nets->list.nets[target];
        const bool last = target == nets->list.cur;
        memcpy(name, n->ssid, n->ssid_len);
        memcpy(c.sta.ssid, n->ssid, n->ssid_len);
        memcpy(c.sta.password, n->pass, sizeof(c.sta.password));
        if (phase == PH_ROUND) ch = nets->seen[target], join_seen = ch != 0;
        else ch = last ? 0 : n->channel, join_seen = -1;
        xSemaphoreGive(lock);
        if (phase != PH_ROUND && last) ch = hint_load();
        if (esp_wifi_get_config(WIFI_IF_STA, &now) == ESP_OK && same_network(&now, &c)) {
            // The network esp_wifi holds already (a start, as before the list): only the channel to begin with.
            if (ch && (now.sta.channel != ch || now.sta.scan_method != WIFI_FAST_SCAN)) {
                now.sta.channel = ch;
                now.sta.scan_method = WIFI_FAST_SCAN;
                esp_wifi_set_config(WIFI_IF_STA, &now);
            }
        } else {
            c.sta.channel = ch;
            c.sta.scan_method = ch ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN;
            c.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
            const esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &c);
            if (err != ESP_OK) ESP_LOGW(TAG, "config: %s", esp_err_to_name(err));
        }
        wipe(&c, sizeof(c));
        wipe(&now, sizeof(now));
    }
    const char *who = target == TRIAL ? "the new network" : name;
    if (phase == PH_ROUND)
        ESP_LOGI(TAG, "round: %s (%s, try %u)", who, ch ? "seen" : "by name", (unsigned)nets->round.tries);
    else if (tries > 1)
        ESP_LOGI(TAG, "joining %s again (try %u, reason %u)", who, (unsigned)tries, (unsigned)last_reason);
    const esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {  // no event will come: on as if it had failed
        ESP_LOGW(TAG, "connect: %s", esp_err_to_name(err));
        refused = gone = false;
        connect_later(1000);
    }
}

static esp_err_t scan_start(bool blocking, bool passive, uint8_t group_channels, uint32_t period_ms,
                            wifi_prov_scan_ctx_t **ctx);

/** A round begins: one scan; its end plans the round (step). */
static void pick_scan(void) {
    phase = PH_SCAN;
    if (scan_start(false, false, 0, 0, NULL) != ESP_OK) {  // without a scan: the ones tried by name
        xSemaphoreTake(lock, portMAX_DELAY);
        memset(nets->seen, 0, sizeof(nets->seen));
        fresh_scan = true;
        xSemaphoreGive(lock);
        connect_later(1000);
    }
}

/** The new network did not join: not remembered. The phone (prov-config) and the computer (yk info) are told so,
 *  and the remembered networks wait until the phone goes or HOLD_MS passes, so the answer stays what it was. */
static void trial_failed(void) {
    ESP_LOGW(TAG, "the new network did not join (reason %u): not remembered", (unsigned)last_reason);
    state = YK_WIFI_FAILED;
    phase = PH_HOLD;
    connect_later(HOLD_MS);
}

/** Back from a new network that did not join: to the last joined, or nothing when there is none. */
static void go_back(void) {
    const uint8_t cur = nets->list.cur;
    if (cur == YK_WIFI_LIST_NONE) {
        wifi_config_t none = {0};  // its password leaves esp_wifi too
        esp_wifi_set_config(WIFI_IF_STA, &none);
        phase = PH_IDLE;
        return;
    }
    ESP_LOGI(TAG, "back to the remembered networks");
    target = cur;
    tries = 1;
    phase = PH_QUICK;
    attempt();
}

static void round_over(void) {
    backoff_s = yk_wifi_list_wait_s(backoff_s);
    const int slow = slow_s;
    const int wait_s = slow > backoff_s ? slow : backoff_s;
    if (state != YK_WIFI_FAILED)
        ESP_LOGW(TAG, "none of the %d remembered networks joined: looking again in %d s", yk_wifi_list_count(&nets->list),
                 wait_s);
    state = YK_WIFI_FAILED;
    phase = PH_WAIT;
    connect_later(wait_s * 1000);
}

void yk_wifi_set_slow(int s) { slow_s = s > 0 ? s : 0; }

/** A round's next network, or its end. Holding `radio`. */
static void round_step(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    const int slot = yk_wifi_round_next(&nets->round, refused);
    xSemaphoreGive(lock);
    refused = false;
    if (slot < 0) {
        round_over();
        return;
    }
    target = (uint8_t)slot;
    attempt();
}

/** What comes next. In the esp_timer task, holding `radio`. */
static void step(void) {
    switch (phase) {
    case PH_QUICK: {
        if (state == YK_WIFI_CONNECTED) break;
        if (target != TRIAL && target >= YK_WIFI_LIST_MAX) {
            phase = PH_IDLE;
            break;
        }
        // Not there at all, and others are remembered: a round now rather than 4 more tries at nothing (the round
        // tries this one first if its scan sees it).
        const bool skip = tries > 0 && gone && target != TRIAL && yk_wifi_list_count(&nets->list) > 1;
        if (tries < QUICK_TRIES && !skip) {
            tries++;
            attempt();
        } else if (target == TRIAL) {
            trial_failed();
        } else {
            pick_scan();
        }
        break;
    }
    case PH_WAIT:
    case PH_SCAN: {
        xSemaphoreTake(lock, portMAX_DELAY);
        const bool fresh = fresh_scan;
        if (fresh) {
            yk_wifi_round_plan(&nets->round, &nets->list, nets->seen);
            fresh_scan = false;
        }
        xSemaphoreGive(lock);
        if (!fresh) {  // the wait is over (a scan the phone asked for during it counts as this round's)
            pick_scan();
            break;
        }
        phase = PH_ROUND;
        refused = false;
        round_step();
        break;
    }
    case PH_ROUND:
        round_step();
        break;
    case PH_HOLD:
        go_back();
        break;
    default:
        break;
    }
}

static void on_connect_timer(void *arg) {
    (void)arg;
    if (scanning) return;  // its end comes back here
    if (xSemaphoreTake(radio, 0) != pdTRUE) {  // new credentials or a forget right now: after them
        connect_later(50);
        return;
    }
    step();
    xSemaphoreGive(radio);
}

/** A second after an address: the network joined goes first in the list, and into esp_wifi's stored copy when that
 *  holds another. Only what changed is written: a start at home writes nothing. Only while still joined to what
 *  esp_wifi holds: a network sent in that second (new credentials) is not the one that joined, and join() does this
 *  first. Holding `radio`. */
static void remember_joined(void) {
    joined_pending = false;
    wifi_config_t c;
    wifi_ap_record_t ap;
    const bool joined = state == YK_WIFI_CONNECTED && esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
                        esp_wifi_get_config(WIFI_IF_STA, &c) == ESP_OK && ssid_len(c.sta.ssid) &&
                        ssid_len(ap.ssid) == ssid_len(c.sta.ssid) && !memcmp(ap.ssid, c.sta.ssid, ssid_len(c.sta.ssid));
    if (joined) {
        const uint8_t ch = ap.primary;
        bool changed = false;
        const bool was_new = target == TRIAL;
        xSemaphoreTake(lock, portMAX_DELAY);
        const int slot = yk_wifi_list_remember(&nets->list, c.sta.ssid, ssid_len(c.sta.ssid), c.sta.password, ch,
                                               join_seen, &changed);
        xSemaphoreGive(lock);
        if (slot >= 0) target = (uint8_t)slot;
        if (changed) {
            nets_save();
            ESP_LOGI(TAG, "%s %.*s (%d remembered)", was_new ? "remembered" : "noted", (int)ssid_len(c.sta.ssid),
                     (const char *)c.sta.ssid, yk_wifi_list_count(&nets->list));
        }
        // esp_wifi's stored copy, for whatever runs next.
        if (key_of(&c) != flash_key) flash_store(&c);
        if (key_of(&c) == flash_key) hint_save(ch);
    }
    wipe(&c, sizeof(c));
}

static void on_joined_timer(void *arg) {
    (void)arg;
    if (xSemaphoreTake(radio, 0) != pdTRUE) {  // new credentials right now: join() has done it
        if (joined_pending) esp_timer_start_once(joined_timer, 100 * 1000);
        return;
    }
    if (joined_pending) remember_joined();
    xSemaphoreGive(radio);
}

static bool auth_failure(uint8_t reason) {
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return true;
    default:
        return false;
    }
}

static int by_signal(const void *a, const void *b) {
    return ((const wifi_ap_record_t *)b)->rssi - ((const wifi_ap_record_t *)a)->rssi;
}

/** Keeps the strongest access point per network name; hidden networks are dropped. Notes which remembered networks
 *  the scan saw, from all it found (not only the 16 kept), and on which channel. */
static void collect_results(void) {
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    // All of them, briefly, in PSRAM: internal RAM is the short one (a block of flats shows 30 and more).
    wifi_ap_record_t *all = n ? heap_caps_calloc(n, sizeof(*all), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    if (n && !all) all = calloc(n, sizeof(*all));
    if (n && !all) n = 0;
    if (esp_wifi_scan_get_ap_records(&n, all) != ESP_OK) n = 0;
    if (n) qsort(all, n, sizeof(*all), by_signal);
    xSemaphoreTake(lock, portMAX_DELAY);
    result_count = 0;
    for (uint16_t i = 0; i < n && result_count < MAX_RESULTS; i++) {
        if (!all[i].ssid[0]) continue;
        bool seen = false;
        for (uint16_t j = 0; j < result_count && !seen; j++)
            seen = !strcmp((const char *)results[j].ssid, (const char *)all[i].ssid);
        if (!seen) results[result_count++] = all[i];
    }
    for (int s = 0; s < YK_WIFI_LIST_MAX; s++) {
        const yk_wifi_net_t *net = &nets->list.nets[s];
        nets->seen[s] = 0;
        for (uint16_t i = 0; i < n && net->ssid_len && !nets->seen[s]; i++)
            if (ssid_len(all[i].ssid) == net->ssid_len && !memcmp(all[i].ssid, net->ssid, net->ssid_len))
                nets->seen[s] = all[i].primary ? all[i].primary : 1;
    }
    fresh_scan = true;
    xSemaphoreGive(lock);
    free(all);
    if (!n) esp_wifi_clear_ap_list();
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got = data;
        timing_mark(yk_wifi_timing_got_ip);
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&got->ip_info.ip));
        joined_pending = true;
        esp_timer_stop(joined_timer);
        esp_timer_start_once(joined_timer, 1000 * 1000);
        state = YK_WIFI_CONNECTED;
        phase = PH_IDLE;
        backoff_s = 0;
        xEventGroupSetBits(events, CONNECTED_BIT);
        ESP_LOGI(TAG, "connected joinMs=%ld linkMs=%ld dhcpMs=%ld", (long)timing.join_ms, (long)timing.link_ms,
                 (long)timing.dhcp_ms);
        return;
    }
    if (base != WIFI_EVENT) return;
    switch (id) {
    case WIFI_EVENT_STA_START:
        if (phase == PH_QUICK && !tries) connect_later(10);
        break;
    case WIFI_EVENT_STA_CONNECTED:
        timing_mark(yk_wifi_timing_linked);
        break;
    case WIFI_EVENT_SCAN_DONE:
        collect_results();
        scanning = false;
        xEventGroupSetBits(events, SCAN_DONE_BIT);
        if (phase == PH_HOLD) connect_later(HOLD_MS);  // the phone is still choosing: what it is told stays
        else if (phase != PH_IDLE) connect_later(10);
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *event = data;
        xEventGroupClearBits(events, CONNECTED_BIT);
        ip[0] = 0;
        // Writing esp_wifi's stored copy should leave the link alone; should it not, that is no leave of ours.
        const bool by_store = event->reason == WIFI_REASON_ASSOC_LEAVE && state == YK_WIFI_CONNECTED &&
                              esp_timer_get_time() - commit_us < 3000000;
        // Our own disconnect (new credentials, scan, forget) is not a failed attempt.
        if (event->reason == WIFI_REASON_ASSOC_LEAVE && !by_store) break;
        last_reason = event->reason;
        if (state == YK_WIFI_CONNECTED) {  // dropped (a scan beside it too): the same network again, quickly
            state = YK_WIFI_CONNECTING;
            phase = PH_QUICK;
            tries = 0;
            timing_mark(yk_wifi_timing_begin);
            connect_later(1000);  // after the scan, if one runs (on_connect_timer waits for its end)
            break;
        }
        // An attempt a scan cut short is not a failed one; with nothing being tried there is nothing to do.
        if (scanning || (phase != PH_QUICK && phase != PH_ROUND)) break;
        refused = auth_failure(event->reason);
        gone = event->reason == WIFI_REASON_NO_AP_FOUND;
        if (target == TRIAL || target == nets->list.cur)
            fail_reason = refused ? WIFI_PROV_STA_AUTH_ERROR : WIFI_PROV_STA_AP_NOT_FOUND;
        connect_later(1000);
        break;
    }
    default:
        break;
    }
}

esp_err_t yk_wifi_reconnect(const char *why) {
    if (state != YK_WIFI_CONNECTED || scanning) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(radio, portMAX_DELAY);
    ESP_LOGW(TAG, "connected but stuck (%s): joining again", why ? why : "?");
    state = YK_WIFI_CONNECTING;
    phase = PH_QUICK;
    tries = 0;
    timing_mark(yk_wifi_timing_begin);
    xEventGroupClearBits(events, CONNECTED_BIT);
    ip[0] = 0;
    esp_wifi_disconnect();  // our own leave: the event handler does not count it, the timer joins again
    connect_later(500);
    xSemaphoreGive(radio);
    return ESP_OK;
}

esp_err_t yk_wifi_retry_now(const char *why) {
    if (phase != PH_WAIT || scanning) return ESP_ERR_INVALID_STATE;
    ESP_LOGI(TAG, "looking for a remembered network now (%s)", why ? why : "?");
    connect_later(10);
    return ESP_OK;
}

esp_err_t yk_wifi_start(void) {
    if (state != YK_WIFI_OFF) return ESP_OK;
    yk_wifi_timing_begin(&timing, esp_timer_get_time());  // nothing else runs yet, and `lock` is still to come
    events = xEventGroupCreate();
    lock = xSemaphoreCreateMutex();
    radio = xSemaphoreCreateMutexWithCaps(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);  // tasks only: PSRAM will do
    if (!radio) radio = xSemaphoreCreateMutex();
    nets = heap_caps_calloc(1, sizeof(*nets), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!nets) nets = calloc(1, sizeof(*nets));
    if (!events || !lock || !radio || !nets) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    sta = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(sta, "yaykit");
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&init)) != ESP_OK) return err;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL));
    const esp_timer_create_args_t timer = {.callback = on_connect_timer, .name = "yk_wifi"};
    ESP_ERROR_CHECK(esp_timer_create(&timer, &connect_timer));
    const esp_timer_create_args_t joined = {.callback = on_joined_timer, .name = "yk_wifi_joined"};
    ESP_ERROR_CHECK(esp_timer_create(&joined, &joined_timer));
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    load_networks();
    // From here on a network reaches flash only once it has joined (flash_store).
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    phase = known() ? PH_QUICK : PH_IDLE;
    state = known() ? YK_WIFI_CONNECTING : YK_WIFI_UNCONFIGURED;
    // Modem sleep is required while BLE shares the radio.
    if ((err = esp_wifi_start()) != ESP_OK) return err;
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    return ESP_OK;
}

yk_wifi_state_t yk_wifi_state(void) { return state; }

uint8_t yk_wifi_last_reason(void) { return last_reason; }

const char *yk_wifi_state_name(void) { return STATE_NAMES[state]; }

bool yk_wifi_wait_connected(uint32_t timeout_ms) {
    if (!events) return false;
    return xEventGroupWaitBits(events, CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms)) & CONNECTED_BIT;
}

void yk_wifi_describe(cJSON *wifi) {
    cJSON_AddStringToObject(wifi, "state", yk_wifi_state_name());
    if (state == YK_WIFI_FAILED)
        cJSON_AddStringToObject(wifi, "error", fail_reason == WIFI_PROV_STA_AUTH_ERROR ? "WRONG_PASSWORD" : "NOT_FOUND");
    if (state != YK_WIFI_CONNECTED) return;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        cJSON_AddStringToObject(wifi, "ssid", (const char *)ap.ssid);
        cJSON_AddNumberToObject(wifi, "rssi", ap.rssi);
    }
    if (ip[0]) cJSON_AddStringToObject(wifi, "ip", ip);
    xSemaphoreTake(lock, portMAX_DELAY);
    const yk_wifi_timing_t last = timing;
    xSemaphoreGive(lock);
    if (last.done) {
        cJSON_AddNumberToObject(wifi, "joinMs", (double)last.join_ms);
        cJSON_AddNumberToObject(wifi, "linkMs", (double)last.link_ms);
        cJSON_AddNumberToObject(wifi, "dhcpMs", (double)last.dhcp_ms);
    }
}

int yk_wifi_status(char *ssid, size_t size, int *rssi) {
    const yk_wifi_state_t now = state;
    wifi_ap_record_t ap;
    const bool info = now == YK_WIFI_CONNECTED && esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    if (ssid && size) snprintf(ssid, size, "%s", info ? (const char *)ap.ssid : "");
    if (rssi) *rssi = info ? ap.rssi : 0;
    return (int)now;
}

// ---- prov-scan ----

static esp_err_t scan_start(bool blocking, bool passive, uint8_t group_channels, uint32_t period_ms,
                            wifi_prov_scan_ctx_t **ctx) {
    (void)group_channels, (void)ctx;
    if (state == YK_WIFI_OFF) return ESP_ERR_INVALID_STATE;
    if (scanning) return ESP_OK;
    // A connection attempt in progress blocks scanning; it resumes after the scan.
    scanning = true;
    if (state != YK_WIFI_CONNECTED) {
        esp_timer_stop(connect_timer);
        esp_wifi_disconnect();
    }
    xEventGroupClearBits(events, SCAN_DONE_BIT);
    wifi_scan_config_t cfg = {.show_hidden = false};
    if (passive) {
        cfg.scan_type = WIFI_SCAN_TYPE_PASSIVE;
        cfg.scan_time.passive = period_ms ? period_ms : 360;
    } else {
        // The driver's own active times (0/0): with Bluetooth on (the phone link, which is how the App asks for the
        // list) it shares the radio with BLE and wants them ("Should use default active scan time parameter…").
        // Measured with the link on: ~3.3 s for the 13 channels.
        cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
        (void)period_ms;
    }
    esp_err_t err = esp_wifi_scan_start(&cfg, false);
    if (err != ESP_OK) {
        scanning = false;
        ESP_LOGW(TAG, "scan: %s", esp_err_to_name(err));
        return err;
    }
    if (blocking) xEventGroupWaitBits(events, SCAN_DONE_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
    return ESP_OK;
}

static esp_err_t scan_status(bool *finished, uint16_t *count, wifi_prov_scan_ctx_t **ctx) {
    (void)ctx;
    *finished = !scanning;
    *count = scanning ? 0 : result_count;
    return ESP_OK;
}

static esp_err_t scan_result(uint16_t index, wifi_prov_scan_result_t *out, wifi_prov_scan_ctx_t **ctx) {
    (void)ctx;
    xSemaphoreTake(lock, portMAX_DELAY);
    esp_err_t err = ESP_FAIL;
    if (index < result_count) {
        const wifi_ap_record_t *r = &results[index];
        memcpy(out->ssid, r->ssid, sizeof(out->ssid));
        memcpy(out->bssid, r->bssid, sizeof(out->bssid));
        out->channel = r->primary;
        out->rssi = r->rssi;
        out->auth = r->authmode;
        err = ESP_OK;
    }
    xSemaphoreGive(lock);
    return err;
}

static wifi_prov_scan_handlers_t scan_handlers = {
    .scan_start = scan_start, .scan_status = scan_status, .scan_result = scan_result};

// ---- prov-config ----

static wifi_config_t pending;
static bool pending_set;

static esp_err_t get_status(wifi_prov_config_get_data_t *out, wifi_prov_ctx_t **ctx) {
    (void)ctx;
    memset(out, 0, sizeof(*out));
    switch (state) {
    case YK_WIFI_CONNECTED: {
        out->wifi_state = WIFI_PROV_STA_CONNECTED;
        snprintf(out->conn_info.ip_addr, sizeof(out->conn_info.ip_addr), "%s", ip);
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            memcpy(out->conn_info.bssid, ap.bssid, sizeof(ap.bssid));
            memcpy(out->conn_info.ssid, ap.ssid, sizeof(ap.ssid));
            out->conn_info.channel = ap.primary;
            out->conn_info.auth_mode = ap.authmode;
        }
        break;
    }
    case YK_WIFI_FAILED:
    case YK_WIFI_UNCONFIGURED:
        out->wifi_state = WIFI_PROV_STA_DISCONNECTED;
        out->fail_reason = fail_reason;
        break;
    default:
        out->wifi_state = WIFI_PROV_STA_CONNECTING;
        out->connecting_info.attempts_remaining = phase == PH_QUICK && tries < QUICK_TRIES ? QUICK_TRIES - tries : 0;
        break;
    }
    return ESP_OK;
}

static esp_err_t set_config(const wifi_prov_config_set_data_t *in, wifi_prov_ctx_t **ctx) {
    (void)ctx;
    memset(&pending, 0, sizeof(pending));
    memcpy(pending.sta.ssid, in->ssid, strnlen(in->ssid, sizeof(pending.sta.ssid)));
    strlcpy((char *)pending.sta.password, in->password, sizeof(pending.sta.password));
    pending.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    pending.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    pending_set = true;
    ESP_LOGI(TAG, "new network received");
    return ESP_OK;
}

/** Whether the last scan saw this name (results: the strongest 16, one per name). Under `lock`. */
static bool in_results(const uint8_t ssid[32]) {
    const size_t n = ssid_len(ssid);
    for (uint16_t i = 0; i < result_count; i++)
        if (ssid_len(results[i].ssid) == n && !memcmp(results[i].ssid, ssid, n)) return true;
    return false;
}

/** A network the phone or the computer sent: tried from RAM, remembered once it has joined (on_joined_timer); one
 *  that does not join leaves the list and esp_wifi's stored copy as they were (trial_failed). Wipes `config`. */
static esp_err_t join(wifi_config_t *config) {
    xSemaphoreTake(radio, portMAX_DELAY);
    if (joined_pending) remember_joined();  // one that joined a moment ago stays remembered
    timing_mark(yk_wifi_timing_begin);
    esp_timer_stop(connect_timer);
    esp_wifi_disconnect();
    xSemaphoreTake(lock, portMAX_DELAY);
    trial_seen = in_results(config->sta.ssid);
    xSemaphoreGive(lock);
    const esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, config);  // storage is RAM
    wipe(config, sizeof(*config));
    tries = 0;
    backoff_s = 0;
    if (err == ESP_OK) {
        target = TRIAL;
        phase = PH_QUICK;
        state = YK_WIFI_CONNECTING;
    } else if (known()) {  // the link was left above: back to the last joined
        target = nets->list.cur;
        phase = PH_QUICK;
        state = YK_WIFI_CONNECTING;
    } else {
        phase = PH_IDLE;
    }
    // Let the reply reach the phone before the radio retunes.
    connect_later(500);
    xSemaphoreGive(radio);
    return err;
}

static esp_err_t apply_config(wifi_prov_ctx_t **ctx) {
    (void)ctx;
    if (!pending_set || state == YK_WIFI_OFF) return ESP_ERR_INVALID_STATE;
    pending_set = false;
    const esp_err_t err = join(&pending);  // the driver keeps its own copy
    memset(&pending, 0, sizeof(pending));
    return err;
}

static wifi_prov_config_handlers_t config_handlers = {
    .get_status_handler = get_status, .set_config_handler = set_config, .apply_config_handler = apply_config};

void *yk_wifi_scan_handlers(void) { return &scan_handlers; }
void *yk_wifi_config_handlers(void) { return &config_handlers; }

// ---- Yaykit Link commands ----

/** What a client needs to offer a network: "open", "password", or "unsupported" (enterprise sign-in, DPP). */
static const char *auth_kind(wifi_auth_mode_t mode) {
    switch (mode) {
    case WIFI_AUTH_OPEN:
    case WIFI_AUTH_OWE:
        return "open";
    case WIFI_AUTH_WEP:
    case WIFI_AUTH_WPA_PSK:
    case WIFI_AUTH_WPA2_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK:
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA2_WPA3_PSK:
    case WIFI_AUTH_WAPI_PSK:
    case WIFI_AUTH_WPA3_EXT_PSK:
    case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE:
        return "password";
    default:
        return "unsupported";
    }
}

static esp_err_t scan_command(cJSON *reply, const char **code) {
    // A scan the phone started is still running: use its results.
    bool ok = true;
    if (scanning) xEventGroupWaitBits(events, SCAN_DONE_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
    else ok = scan_start(true, false, 0, 0, NULL) == ESP_OK;
    if (!ok || scanning) {
        *code = "SCAN_FAILED";
        return ESP_FAIL;
    }
    cJSON *networks = cJSON_AddArrayToObject(reply, "networks");
    xSemaphoreTake(lock, portMAX_DELAY);
    for (uint16_t i = 0; i < result_count; i++) {
        cJSON *network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "ssid", (const char *)results[i].ssid);
        cJSON_AddNumberToObject(network, "rssi", results[i].rssi);
        cJSON_AddStringToObject(network, "auth", auth_kind(results[i].authmode));
        cJSON_AddItemToArray(networks, network);
    }
    xSemaphoreGive(lock);
    return ESP_OK;
}

static esp_err_t join_command(const cJSON *args, cJSON *reply, const char **code) {
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(args, "ssid");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(args, "password");
    const char *error = yk_wifi_join_error(cJSON_IsString(ssid) ? ssid->valuestring : NULL,
                                           cJSON_IsString(password) ? password->valuestring : NULL);
    if (password && !cJSON_IsString(password)) error = "BAD_PASSWORD";
    if (error) {
        *code = error;
        return ESP_ERR_INVALID_ARG;
    }
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid->valuestring, strlen(ssid->valuestring));
    if (password) memcpy(config.sta.password, password->valuestring, strlen(password->valuestring));
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    ESP_LOGI(TAG, "new network received");
    esp_err_t err = join(&config);
    if (err != ESP_OK) {
        *code = "JOIN_FAILED";
        return err;
    }
    cJSON_AddStringToObject(reply, "state", yk_wifi_state_name());
    cJSON_AddStringToObject(reply, "ssid", ssid->valuestring);
    return ESP_OK;
}

esp_err_t yk_wifi_command(const char *name, const cJSON *args, cJSON *reply, const char **code) {
    bool scan = !strcmp(name, "wifi.scan"), join_cmd = !strcmp(name, "wifi.join");
    if (!scan && !join_cmd) return ESP_ERR_NOT_FOUND;
    if (state == YK_WIFI_OFF) {
        *code = "WIFI_OFF";
        return ESP_ERR_INVALID_STATE;
    }
    return scan ? scan_command(reply, code) : join_command(args, reply, code);
}

// ---- console ----

/** `wifi list`: the remembered networks, the most recent first; names only. Printed after `lock` is let go. */
static void list_command(void) {
    enum { LINE = 64 + 4 * 32 };  // the words, and a name of 32 bytes even if every byte were printed wide
    char *text = heap_caps_calloc(YK_WIFI_LIST_MAX, LINE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!text) text = calloc(YK_WIFI_LIST_MAX, LINE);
    int n = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    while (text && n < YK_WIFI_LIST_MAX && yk_wifi_list_line(&nets->list, n, text + n * LINE, LINE) >= 0) n++;
    xSemaphoreGive(lock);
    for (int i = 0; i < n; i++) printf("%s\n", text + i * LINE);
    printf("YK_WIFI_LIST count=%d max=%d\n", n, YK_WIFI_LIST_MAX);
    free(text);
}

static int wifi_cmd(int argc, char **argv) {
    const char *sub = argc > 1 ? argv[1] : "status";
    if (!strcmp(sub, "scan")) {
        if (scan_start(true, false, 0, 0, NULL) != ESP_OK) {
            printf("YK_WIFI_SCAN error\n");
            return 1;
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        for (uint16_t i = 0; i < result_count; i++)
            printf("YK_WIFI_AP rssi=%d channel=%d auth=%d ssid=%s\n", results[i].rssi, results[i].primary,
                   results[i].authmode, (const char *)results[i].ssid);
        printf("YK_WIFI_SCAN count=%u\n", (unsigned)result_count);
        xSemaphoreGive(lock);
        return 0;
    }
    if (!strcmp(sub, "list") && state != YK_WIFI_OFF) list_command();
    if (!strcmp(sub, "forget") && state != YK_WIFI_OFF) {  // every remembered network, and esp_wifi's stored copy
        xSemaphoreTake(radio, portMAX_DELAY);
        esp_timer_stop(connect_timer);
        esp_wifi_disconnect();
        hint_drop();
        xSemaphoreTake(lock, portMAX_DELAY);
        yk_wifi_list_clear(&nets->list);
        memset(nets->seen, 0, sizeof(nets->seen));
        xSemaphoreGive(lock);
        nets_save();
        wifi_config_t empty = {0};
        flash_store(&empty);
        state = YK_WIFI_UNCONFIGURED;
        phase = PH_IDLE;
        target = YK_WIFI_LIST_NONE;
        xSemaphoreGive(radio);
    }
    cJSON *wifi = cJSON_CreateObject();
    yk_wifi_describe(wifi);
    char *text = cJSON_PrintUnformatted(wifi);
    printf("YK_WIFI %s\n", text);
    cJSON_free(text);
    cJSON_Delete(wifi);
    return 0;
}

esp_err_t yk_wifi_register_console(void) {
    const esp_console_cmd_t cmd = {
        .command = "wifi", .help = "wifi status | scan | list | forget", .func = wifi_cmd};
    return esp_console_cmd_register(&cmd);
}

void yk_wifi_drop_pending(void) {
    memset(&pending, 0, sizeof(pending));
    pending_set = false;
    if (phase == PH_HOLD) connect_later(10);  // the phone has gone: back to the remembered networks now
}
