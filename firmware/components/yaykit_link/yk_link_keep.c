// Yaykit Link's long mode (yk_link_keep.h). Grown from the selfie lab's sl_ble.c (exp/selfie-lab, c527e06c), with
// what the lab could not do yet: keys in the app's own namespace (not NimBLE's nimble_bond), a static random
// address of its own, and the remembered phone re-pairing asked about like a new one.
//
// The store: NimBLE runs on its RAM store (CONFIG_BT_NIMBLE_NVS_PERSIST=n). Its write and delete callbacks are
// wrapped: what yk_link_bond.c keeps goes through and is mirrored into one NVS blob in the app's namespace ("bond");
// a second phone's writes wait in `s_held`. At start the blob is written back into the RAM store before the host
// runs (so the phone's IRK is in the resolving list from the first advertisement).
//
// The unpaired mode (yk_link_keep_start_unpaired(), 设备通道 device.link) runs the same GAP and GATT code with
// `s_unpaired` set: no store at all (NimBLE's store callbacks stay NULL, so nothing of a phone is kept or mirrored), no
// bonding, no HID, battery or device information services, characteristics without _ENC, none of the one-phone rule.
// Its service and characteristics are the link's (device-link.ts LINK.ble), the handshake and the envelopes
// yk_link_seal.c's, under s_link_lock (NimBLE's host task opens and takes, yk_link_keep_send()'s task seals). Firmware
// that never calls it (Yay) keeps none of it: the linker drops what nothing reaches.
#include "yk_link_keep.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "services/gap/ble_svc_gap.h"
#include "sdkconfig.h"
#include "services/gatt/ble_svc_gatt.h"
#include "yaykit_link.h"
#include "yk_link_bond.h"
#include "yk_link_priv.h"
#include "yk_link_rules.h"
#include "yk_link_seal.h"

_Static_assert(YK_RULES_F_READ == BLE_GATT_CHR_F_READ && YK_RULES_F_WRITE == BLE_GATT_CHR_F_WRITE &&
                   YK_RULES_F_WRITE_NO_RSP == BLE_GATT_CHR_F_WRITE_NO_RSP && YK_RULES_F_NOTIFY == BLE_GATT_CHR_F_NOTIFY &&
                   YK_RULES_F_READ_ENC == BLE_GATT_CHR_F_READ_ENC && YK_RULES_F_WRITE_ENC == BLE_GATT_CHR_F_WRITE_ENC,
               "yk_link_rules.h's flags are NimBLE's");

void ble_store_config_init(void);

static const char *TAG = "yk_keep";

#define APPEARANCE 0x03C0  // generic HID
#define CCCD_MAX 8
#define HELD_MAX 8
#define MIRROR_MAGIC 0x31424B59u  // "YKB1"

// ---------------------------------------------------------------- state (changed on the host task unless noted)

static yk_link_keep_config_t s_cfg;
static volatile bool s_active;
static volatile uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile bool s_enc, s_trusted, s_sub[YK_KEEP_CHR_MAX], s_sub_report;
static uint8_t s_addr[6];      // our static random address
static bool s_addr_ok;
static yk_bond_t s_bond;
static char s_peer_name[48];
static uint8_t s_report;       // HID report 1: bit 0 volume up
static uint8_t s_battery = 100;
static esp_timer_handle_t s_release_timer, s_ask_timer;
static ble_store_write_fn *s_write;    // NimBLE's own (RAM)
static ble_store_delete_fn *s_delete;
static bool s_bypass;                  // the store's own changes (restoring, forgetting): no mirror update
static volatile uint16_t s_mtu;        // the connection's ATT MTU (BLE_ATT_MTU_DFLT until the phone agrees one)

// The unpaired mode (set at start, before the host runs).
static bool s_unpaired;
static bool s_adv_fast;                // advertise fast: after the start, after a disconnection
static uint16_t s_app;                 // the app's number
static void (*s_on_frame)(const uint8_t *frame, size_t len, void *ctx);
static uint8_t *s_value, *s_frame;     // the phone's write, the frame opened from it (PSRAM; host task)
static uint8_t *s_tx;                  // a sealed frame on its way out (PSRAM; under s_link_lock)
static yk_seal_link_t *s_link;         // keys and counters (PSRAM; under s_link_lock)
static SemaphoreHandle_t s_link_lock;
static uint32_t s_mark;
static uint8_t s_svc_data[16 + YK_SEAL_SERVICE_DATA];  // the scan response's service data: the UUID, then the data
static uint32_t s_link_drops;          // phones let go for envelopes that did not open
static bool s_dropping;                // the connected phone is being let go for them
enum { L_FRAMES, L_NOTIFY, L_SESSION };  // the link's characteristics, in s_cfg.chrs
#define LINK_UUID(x) {0xa9, 0xd4, 0x12, 0x98, 0x44, 0x25, 0x37, 0x8f, 0x65, 0x47, 0x66, 0x02, (x), 0x52, 0x8d, 0xe1}
#define LINK_NAME "Tikpal"             // LINK.ble.name: the scan response is full with it (23 + 8 bytes)

typedef struct {
    int type;
    union ble_store_value v;
} held_t;
static held_t s_held[HELD_MAX];
static int s_held_n;

/** The remembered phone, as kept in NVS. */
typedef struct {
    uint32_t magic;
    uint16_t sec_size, cccd_size;
    uint8_t have_our, have_peer, n_cccd, pad;
    struct ble_store_value_sec our, peer;
    struct ble_store_value_cccd cccd[CCCD_MAX];
} mirror_t;
static mirror_t s_mirror;

// GATT
static ble_uuid128_t s_svc_uuid, s_chr_uuid[YK_KEEP_CHR_MAX];
static uint16_t s_chr_handle[YK_KEEP_CHR_MAX], s_report_handle;
static struct ble_gatt_chr_def s_chr_defs[YK_KEEP_CHR_MAX + 1];
static struct ble_gatt_svc_def s_svcs[5];

enum { H_INFO = 100, H_MAP, H_CTRL, H_REPORT, H_PROTO, B_LEVEL, D_PNP, D_MANUF };

// Consumer Control, report 1: one bit, Volume Increment (0xE9), seven bits padding.
static const uint8_t REPORT_MAP[] = {
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x01, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
    0x95, 0x01, 0x09, 0xE9, 0x81, 0x02, 0x95, 0x07, 0x81, 0x03, 0xC0,
};
static const uint8_t HID_INFO[] = {0x11, 0x01, 0x00, 0x02};  // HID 1.11, no country, normally connectable
static const uint8_t PNP_ID[] = {0x02, 0x3A, 0x30, 0x01, 0x80, 0x01, 0x00};  // USB vendor 0x303A (Espressif)

bool yk_link_keep_active(void) { return s_active; }
bool yk_link_keep_connected(void) { return s_conn != BLE_HS_CONN_HANDLE_NONE; }
bool yk_link_keep_trusted(void) { return s_trusted && yk_link_keep_connected(); }
bool yk_link_keep_remembered(void) { return s_bond.have; }
bool yk_link_keep_asking(void) { return s_bond.asking; }
bool yk_link_keep_subscribed(int chr) {
    if (s_unpaired) return s_sub[L_NOTIFY];
    return chr >= 0 && chr < YK_KEEP_CHR_MAX && s_sub[chr];
}
uint16_t yk_link_keep_mtu(void) { return yk_link_keep_connected() ? s_mtu : 0; }

void yk_link_keep_disconnect(void) {
    const uint16_t conn = s_conn;
    if (s_active && conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
}

void yk_link_keep_peer_name(char *out, size_t size) {
    if (size) snprintf(out, size, "%s", s_peer_name);
}

void yk_link_keep_battery(int percent) { s_battery = percent < 0 ? 100 : percent > 100 ? 100 : (uint8_t)percent; }

// The unpaired mode's callbacks run in its own task (yk_link_app), never on NimBLE's host task: that stack is for the
// component's own connection, handshake and crypto, measured (BOSS-2 11-13). Events and frames go through one queue, in
// the order they came. A frame that finds the queue full is dropped and counted (the app fell behind by
// APP_QUEUE_LEN); an event waits for room a little (they are few and the app must see them).
#define APP_QUEUE_LEN 16
// Internal RAM: an app's callback may write NVS. 11-14 at 3 KB, nav-lab's callbacks (a printf each) left 728 bytes
// untouched at the least; 4 KB leaves an app's logging or snprintf room, as yk_ring did (3 KB, 456 left, raised 11-02).
#define APP_TASK_STACK 4096
typedef struct {
    uint8_t frame;  // 1: a frame (data, len); 0: an event
    uint8_t ev;
    uint16_t len;
    uint8_t data[YK_SEAL_FRAME_MAX];
} app_msg_t;
static QueueHandle_t s_app_q;
static TaskHandle_t s_app_task;
static uint32_t s_app_dropped;

static void app_task(void *arg) {
    (void)arg;
    app_msg_t m;
    for (;;) {
        if (xQueueReceive(s_app_q, &m, portMAX_DELAY) != pdTRUE) continue;
        if (m.frame) {
            if (s_on_frame) s_on_frame(m.data, m.len, s_cfg.ctx);
        } else if (s_cfg.on_event) {
            s_cfg.on_event((yk_keep_event_t)m.ev, s_cfg.ctx);
        }
    }
}

static void to_app(bool frame, yk_keep_event_t ev, const uint8_t *data, size_t len) {
    app_msg_t m = {.frame = frame, .ev = (uint8_t)ev, .len = (uint16_t)len};
    if (frame) memcpy(m.data, data, len);
    if (xQueueSend(s_app_q, &m, frame ? 0 : pdMS_TO_TICKS(50)) != pdTRUE) {
        s_app_dropped++;
        printf("YK_KEEP app behind dropped=%u what=%s\n", (unsigned)s_app_dropped, frame ? "frame" : "event");
    }
}

static void emit(yk_keep_event_t ev) {
    if (s_unpaired) to_app(false, ev, NULL, 0);
    else if (s_cfg.on_event) s_cfg.on_event(ev, s_cfg.ctx);
}

static void addr_text(const ble_addr_t *a, char out[18]) {
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", a->val[5], a->val[4], a->val[3], a->val[2], a->val[1], a->val[0]);
}

static yk_bond_addr_t bond_addr(const ble_addr_t *a) {
    yk_bond_addr_t b = {.type = a->type};
    memcpy(b.val, a->val, 6);
    return b;
}

static ble_addr_t nimble_addr(const yk_bond_addr_t *b) {
    ble_addr_t a = {.type = b->type};
    memcpy(a.val, b->val, 6);
    return a;
}

// ---------------------------------------------------------------- the mirror in NVS

static void mirror_save(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(s_cfg.nvs_ns, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        if (s_mirror.have_peer) err = nvs_set_blob(h, "bond", &s_mirror, sizeof(s_mirror));
        else if ((err = nvs_erase_key(h, "bond")) == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGW(TAG, "phone not kept: %s", esp_err_to_name(err));
}

static void mirror_clear(void) {
    memset(&s_mirror, 0, sizeof(s_mirror));
    s_mirror.magic = MIRROR_MAGIC;
    s_mirror.sec_size = sizeof(struct ble_store_value_sec);
    s_mirror.cccd_size = sizeof(struct ble_store_value_cccd);
}

static void mirror_load(void) {
    mirror_clear();
    nvs_handle_t h;
    if (nvs_open(s_cfg.nvs_ns, NVS_READONLY, &h) != ESP_OK) return;
    mirror_t m;
    size_t n = sizeof(m);
    const bool ok = nvs_get_blob(h, "bond", &m, &n) == ESP_OK && n == sizeof(m) && m.magic == MIRROR_MAGIC &&
                    m.sec_size == sizeof(struct ble_store_value_sec) &&
                    m.cccd_size == sizeof(struct ble_store_value_cccd) && m.have_peer && m.n_cccd <= CCCD_MAX;
    nvs_close(h);
    if (ok) s_mirror = m;  // else (another NimBLE's layout): the phone pairs again
}

/** Mirror one write the store took. */
static void mirror_write(int type, const union ble_store_value *v) {
    if (type == BLE_STORE_OBJ_TYPE_OUR_SEC) {
        s_mirror.our = v->sec;
        s_mirror.have_our = 1;
    } else if (type == BLE_STORE_OBJ_TYPE_PEER_SEC) {
        s_mirror.peer = v->sec;
        s_mirror.have_peer = 1;
    } else if (type == BLE_STORE_OBJ_TYPE_CCCD) {
        int i = 0;
        while (i < s_mirror.n_cccd && s_mirror.cccd[i].chr_val_handle != v->cccd.chr_val_handle) i++;
        if (i == s_mirror.n_cccd) {
            if (i == CCCD_MAX) return;
            s_mirror.n_cccd++;
        }
        s_mirror.cccd[i] = v->cccd;
    } else {
        return;
    }
    mirror_save();
}

/** The remembered phone's keys back into NimBLE's RAM store (at start, and when a re-pairing was refused). */
static void mirror_restore(void) {
    if (!s_mirror.have_peer) return;
    s_bypass = true;
    if (s_mirror.have_our) s_write(BLE_STORE_OBJ_TYPE_OUR_SEC, (const union ble_store_value *)&s_mirror.our);
    s_write(BLE_STORE_OBJ_TYPE_PEER_SEC, (const union ble_store_value *)&s_mirror.peer);
    for (int i = 0; i < s_mirror.n_cccd; i++)
        s_write(BLE_STORE_OBJ_TYPE_CCCD, (const union ble_store_value *)&s_mirror.cccd[i]);
    s_bypass = false;
}

// ---------------------------------------------------------------- the store's callbacks

static const ble_addr_t *peer_of(int type, const union ble_store_value *v) {
    if (type == BLE_STORE_OBJ_TYPE_OUR_SEC || type == BLE_STORE_OBJ_TYPE_PEER_SEC) return &v->sec.peer_addr;
    if (type == BLE_STORE_OBJ_TYPE_CCCD) return &v->cccd.peer_addr;
    return NULL;
}

static void ask_timeout(void *arg);

static int store_write(int type, const union ble_store_value *val) {
    const ble_addr_t *peer = peer_of(type, val);
    if (s_bypass || !peer) return s_write(type, val);
    const yk_bond_addr_t p = bond_addr(peer);
    bool first = false;
    char a[18];
    addr_text(peer, a);
    switch (yk_bond_write(&s_bond, &p, &first)) {
    case YK_BOND_KEEP: {
        const int rc = s_write(type, val);
        if (rc == 0) mirror_write(type, val);
        if (first) {
            printf("YK_KEEP bond first phone=%s\n", a);
            emit(YK_KEEP_EV_PAIRED);
        }
        return rc;
    }
    case YK_BOND_ASK:
        printf("YK_KEEP bond ask new=%s\n", a);
        esp_timer_start_once(s_ask_timer, (uint64_t)(s_cfg.ask_seconds > 0 ? s_cfg.ask_seconds : 30) * 1000000ULL);
        emit(YK_KEEP_EV_ASK);
        __attribute__((fallthrough));  // held like the rest of its writes
    case YK_BOND_HOLD:
        if (s_held_n < HELD_MAX) s_held[s_held_n++] = (held_t){.type = type, .v = *val};
        return 0;  // NimBLE goes on as if kept; nothing is until the answer says yes
    case YK_BOND_DROP:
    default:
        printf("YK_KEEP bond drop third=%s\n", a);
        return 0;
    }
}

static int store_delete(int type, const union ble_store_key *key) {
    const int rc = s_delete(type, key);
    if (s_bypass || rc != 0) return rc;
    if (type == BLE_STORE_OBJ_TYPE_CCCD) {
        for (int i = 0; i < s_mirror.n_cccd; i++) {
            if (s_mirror.cccd[i].chr_val_handle != key->cccd.chr_val_handle) continue;
            s_mirror.cccd[i] = s_mirror.cccd[--s_mirror.n_cccd];
            mirror_save();
            break;
        }
    } else if (type == BLE_STORE_OBJ_TYPE_PEER_SEC && s_mirror.have_peer &&
               (ble_addr_cmp(&key->sec.peer_addr, BLE_ADDR_ANY) == 0 ||
                ble_addr_cmp(&key->sec.peer_addr, &s_mirror.peer.peer_addr) == 0)) {
        mirror_clear();
        mirror_save();
        yk_bond_forget(&s_bond);
    }
    return rc;
}

// ---------------------------------------------------------------- the answer, on the host task

static struct ble_npl_event s_answer_ev;
static volatile bool s_answer_yes;

static void answer_on_host(struct ble_npl_event *ev) {
    (void)ev;
    if (!s_bond.asking) return;
    esp_timer_stop(s_ask_timer);
    const bool yes = s_answer_yes;
    const bool repairing = s_bond.repairing;
    const yk_bond_addr_t old = s_bond.phone, asked = s_bond.asked;
    yk_bond_answer(&s_bond, yes);
    if (yes) {
        // The old phone out of the store (and the mirror), then the new one's held writes in.
        const ble_addr_t o = nimble_addr(&old);
        s_bypass = true;
        ble_store_util_delete_peer(&o);
        s_bypass = false;
        mirror_clear();
        for (int i = 0; i < s_held_n; i++) {
            if (s_write(s_held[i].type, &s_held[i].v) == 0) mirror_write(s_held[i].type, &s_held[i].v);
        }
        mirror_save();
        const ble_addr_t n = nimble_addr(&asked);
        char a[18], b[18];
        addr_text(&o, a);
        addr_text(&n, b);
        printf("YK_KEEP bond replace old=%s new=%s\n", a, b);
        s_trusted = s_enc && yk_link_keep_connected();
        emit(YK_KEEP_EV_REPLACED);
        if (s_trusted) emit(YK_KEEP_EV_TRUSTED);
    } else {
        if (repairing) mirror_restore();  // its old keys were taken out for the re-pairing
        printf("YK_KEEP bond refuse\n");
        emit(YK_KEEP_EV_REFUSED);
        const uint16_t conn = s_conn;
        if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    memset(s_held, 0, sizeof(s_held));
    s_held_n = 0;
}

void yk_link_keep_answer(bool yes) {
    if (!s_active || s_unpaired || !s_bond.asking) return;
    s_answer_yes = yes;
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_answer_ev);
}

static void ask_timeout(void *arg) {
    (void)arg;
    printf("YK_KEEP bond ask_timeout\n");
    yk_link_keep_answer(false);
}

static struct ble_npl_event s_forget_ev;

static void forget_on_host(struct ble_npl_event *ev) {
    (void)ev;
    s_bypass = true;
    ble_store_clear();
    s_bypass = false;
    mirror_clear();
    mirror_save();
    yk_bond_forget(&s_bond);
    printf("YK_KEEP bond forget\n");
    const uint16_t conn = s_conn;
    if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
}

void yk_link_keep_forget(void) {
    if (s_active && !s_unpaired) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_forget_ev);
}

// ---------------------------------------------------------------- notifications and the key

static void notify_handle(uint16_t handle, const void *data, size_t len) {
    const uint16_t conn = s_conn;
    if (conn == BLE_HS_CONN_HANDLE_NONE) return;
    const uint16_t mtu = ble_att_mtu(conn);
    if (mtu > 3 && len > (size_t)mtu - 3) len = mtu - 3;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
    if (om) ble_gatts_notify_custom(conn, handle, om);
}

esp_err_t yk_link_keep_notify(int chr, const void *data, size_t len) {
    if (chr < 0 || chr >= s_cfg.n_chrs) return ESP_ERR_INVALID_ARG;
    if (s_unpaired) return ESP_ERR_NOT_SUPPORTED;  // frames go sealed: yk_link_keep_send()
    if (!yk_link_keep_trusted() || !s_sub[chr]) return ESP_ERR_INVALID_STATE;
    notify_handle(s_chr_handle[chr], data, len);
    return ESP_OK;
}

static void release(void *arg) {
    (void)arg;
    s_report = 0;
    if (yk_link_keep_trusted()) notify_handle(s_report_handle, &s_report, 1);
}

esp_err_t yk_link_keep_press(void) {
    if (!s_cfg.hid || !yk_link_keep_trusted()) return ESP_ERR_INVALID_STATE;
    s_report = 0x01;
    notify_handle(s_report_handle, &s_report, 1);
    esp_timer_stop(s_release_timer);
    esp_timer_start_once(s_release_timer, 40000);  // 40 ms: no long press (an iPhone would start a video)
    return ESP_OK;
}

// ---------------------------------------------------------------- GATT

static int append(struct ble_gatt_access_ctxt *ctxt, const void *data, size_t len) {
    return os_mbuf_append(ctxt->om, data, len) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}

/** The unpaired mode: the session's bytes to anyone; LinkOpen, then the phone's envelopes. A failed open answers with
 *  its ATT error (KEY_MISMATCH, BUSY); an envelope that does not open is dropped, and after YK_SEAL_FAILS_MAX of them
 *  in a row the phone is let go. */
static int unpaired_access(int chr, struct ble_gatt_access_ctxt *ctxt) {
    if (chr == L_SESSION && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t session[YK_SEAL_SESSION];
        xSemaphoreTake(s_link_lock, portMAX_DELAY);
        yk_seal_link_session(s_link, session);
        xSemaphoreGive(s_link_lock);
        return append(ctxt, session, sizeof(session));
    }
    if (chr != L_FRAMES || ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint16_t n = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, s_value, YK_RULES_VALUE_MAX, &n)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    xSemaphoreTake(s_link_lock, portMAX_DELAY);
    if (!s_link->open) {
        const bool was_mismatch = s_link->mismatch;
        const int rc = yk_seal_link_open(s_link, s_value, n);
        const bool mismatch = s_link->mismatch, have = s_link->have_key;
        xSemaphoreGive(s_link_lock);
        if (rc) {
            printf("YK_KEEP link open refused err=0x%02x key=%s\n", rc, have ? "other" : "none");
            if (mismatch != was_mismatch) {
                yk_link_keys_mismatch(s_app, mismatch);
                emit(YK_KEEP_EV_KEY);
            }
            return rc;
        }
        s_trusted = true;
        printf("YK_KEEP link open\n");
        yk_link_keys_mismatch(s_app, false);
        if (was_mismatch) emit(YK_KEEP_EV_KEY);
        emit(YK_KEEP_EV_TRUSTED);
        return 0;
    }
    const int len = yk_seal_link_receive(s_link, s_value, n, s_frame);
    const int fails = s_link->fails;
    xSemaphoreGive(s_link_lock);
    if (len < 0) {
        if (fails >= YK_SEAL_FAILS_MAX) {
            s_link_drops++;
            s_dropping = true;
            printf("YK_KEEP link drop why=envelopes fails=%d drops=%u\n", fails, (unsigned)s_link_drops);
            const uint16_t conn = s_conn;
            if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_AUTH_FAIL);
        }
        return 0;  // without response it says nothing anyway; with one, nothing to tell a stranger
    }
    if (len >= 2 && s_frame[1] == YK_SEAL_KIND_OPEN) return 0;  // LinkOpen only ever comes in the clear
    if (s_on_frame) to_app(true, (yk_keep_event_t)0, s_frame, (size_t)len);  // the app's task calls on_frame
    return 0;
}

esp_err_t yk_link_keep_send(const void *frame, size_t len) {
    if (!s_active || !s_unpaired) return ESP_ERR_INVALID_STATE;
    const uint16_t conn = s_conn;
    if (conn == BLE_HS_CONN_HANDLE_NONE || !s_trusted || !s_sub[L_NOTIFY]) return ESP_ERR_INVALID_STATE;
    if (!frame || !len || len > yk_rules_frame_room(ble_att_mtu(conn))) return ESP_ERR_INVALID_SIZE;
    xSemaphoreTake(s_link_lock, portMAX_DELAY);
    const int n = yk_seal_link_send(s_link, frame, len, s_tx);
    struct os_mbuf *om = n > 0 ? ble_hs_mbuf_from_flat(s_tx, (uint16_t)n) : NULL;
    xSemaphoreGive(s_link_lock);
    if (n <= 0) return ESP_ERR_INVALID_STATE;  // the link closed meanwhile
    if (!om) return ESP_ERR_NO_MEM;
    return ble_gatts_notify_custom(conn, s_chr_handle[L_NOTIFY], om) ? ESP_FAIL : ESP_OK;
}

uint32_t yk_link_keep_mark(void) {
    if (!s_mark) s_mark = yk_seal_mark(yk_link_device_id());
    return s_mark;
}

static int app_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn, (void)attr;
    const int chr = (int)(intptr_t)arg;
    if (s_unpaired) return unpaired_access(chr, ctxt);
    if (!yk_link_keep_trusted()) return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;  // a phone being asked about
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char buf[256];
        const int n = s_cfg.on_read ? s_cfg.on_read(chr, buf, sizeof(buf), s_cfg.ctx) : -1;
        const int rc = n < 0 ? BLE_ATT_ERR_UNLIKELY : append(ctxt, buf, (size_t)n);
        memset(buf, 0, sizeof(buf));  // it may have held a password
        return rc;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t buf[256];
        uint16_t n = 0;
        if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &n)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (s_cfg.on_write) s_cfg.on_write(chr, buf, n, s_cfg.ctx);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int std_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn, (void)attr;
    const bool read = ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR;
    switch ((int)(intptr_t)arg) {
    case H_INFO: return append(ctxt, HID_INFO, sizeof(HID_INFO));
    case H_MAP: return append(ctxt, REPORT_MAP, sizeof(REPORT_MAP));
    case H_CTRL: return 0;  // suspend / exit suspend: nothing to do
    case H_REPORT: return read ? append(ctxt, &s_report, 1) : 0;
    case H_PROTO: {
        const uint8_t report_protocol = 1;
        return read ? append(ctxt, &report_protocol, 1) : 0;
    }
    case B_LEVEL: return append(ctxt, &s_battery, 1);
    case D_PNP: return append(ctxt, PNP_ID, sizeof(PNP_ID));
    case D_MANUF: return append(ctxt, "Yaykit", 6);
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int report_ref(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn, (void)attr, (void)arg;
    static const uint8_t ref[] = {0x01, 0x01};  // report 1, input
    return append(ctxt, ref, sizeof(ref));
}

#define ENC_R (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC)
static const struct ble_gatt_chr_def HID_CHRS[] = {
    {.uuid = BLE_UUID16_DECLARE(0x2A4A), .access_cb = std_access, .arg = (void *)(intptr_t)H_INFO, .flags = ENC_R},
    {.uuid = BLE_UUID16_DECLARE(0x2A4B), .access_cb = std_access, .arg = (void *)(intptr_t)H_MAP, .flags = ENC_R},
    {.uuid = BLE_UUID16_DECLARE(0x2A4C), .access_cb = std_access, .arg = (void *)(intptr_t)H_CTRL,
     .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC},
    {.uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = std_access, .arg = (void *)(intptr_t)H_REPORT,
     .flags = ENC_R | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_report_handle,
     .descriptors = (struct ble_gatt_dsc_def[]){
         {.uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC, .access_cb = report_ref},
         {0}}},
    {.uuid = BLE_UUID16_DECLARE(0x2A4E), .access_cb = std_access, .arg = (void *)(intptr_t)H_PROTO,
     .flags = ENC_R | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC},
    {0},
};
static const struct ble_gatt_chr_def BATTERY_CHRS[] = {
    {.uuid = BLE_UUID16_DECLARE(0x2A19), .access_cb = std_access, .arg = (void *)(intptr_t)B_LEVEL, .flags = BLE_GATT_CHR_F_READ},
    {0},
};
static const struct ble_gatt_chr_def INFO_CHRS[] = {
    {.uuid = BLE_UUID16_DECLARE(0x2A50), .access_cb = std_access, .arg = (void *)(intptr_t)D_PNP, .flags = BLE_GATT_CHR_F_READ},
    {.uuid = BLE_UUID16_DECLARE(0x2A29), .access_cb = std_access, .arg = (void *)(intptr_t)D_MANUF, .flags = BLE_GATT_CHR_F_READ},
    {0},
};

static void build_gatt(void) {
    memset(s_chr_defs, 0, sizeof(s_chr_defs));
    memset(s_svcs, 0, sizeof(s_svcs));
    s_svc_uuid.u.type = BLE_UUID_TYPE_128;
    memcpy(s_svc_uuid.value, s_cfg.service, 16);
    for (int i = 0; i < s_cfg.n_chrs; i++) {
        s_chr_uuid[i].u.type = BLE_UUID_TYPE_128;
        memcpy(s_chr_uuid[i].value, s_cfg.chrs[i].uuid, 16);
        const uint16_t f = yk_rules_chr_flags(s_cfg.chrs[i].flags, s_unpaired);
        s_chr_defs[i] = (struct ble_gatt_chr_def){.uuid = &s_chr_uuid[i].u, .access_cb = app_access,
                                                  .arg = (void *)(intptr_t)i, .flags = f, .val_handle = &s_chr_handle[i]};
    }
    int k = 0;
    s_svcs[k++] = (struct ble_gatt_svc_def){.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &s_svc_uuid.u,
                                            .characteristics = s_chr_defs};
    if (s_unpaired) return;  // only the link's own service
    if (s_cfg.hid)
        s_svcs[k++] = (struct ble_gatt_svc_def){.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x1812),
                                                .characteristics = HID_CHRS};
    s_svcs[k++] = (struct ble_gatt_svc_def){.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x180F),
                                            .characteristics = BATTERY_CHRS};
    s_svcs[k++] = (struct ble_gatt_svc_def){.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x180A),
                                            .characteristics = INFO_CHRS};
}

// ---------------------------------------------------------------- GAP

static int gap_event(struct ble_gap_event *ev, void *arg);

/** The unpaired mode: the service in the advertisement, the name in the scan response; fast, then slow. */
static void advertise_unpaired(void) {
    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128 = &s_svc_uuid;
    f.num_uuids128 = 1;
    f.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);
    struct ble_hs_adv_fields r = {0};
    r.svc_data_uuid128 = s_svc_data;  // the mark and the app's number: the phone picks this device by them
    r.svc_data_uuid128_len = sizeof(s_svc_data);
    r.name = (const uint8_t *)LINK_NAME;
    r.name_len = strlen(LINK_NAME);
    r.name_is_complete = 1;
    if (!rc) rc = ble_gap_adv_rsp_set_fields(&r);
    const yk_rules_adv_t a = yk_rules_adv(s_adv_fast);
    struct ble_gap_adv_params p = {.conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN,
                                   .itvl_min = a.itvl_min, .itvl_max = a.itvl_max};
    const uint8_t own = s_addr_ok ? BLE_OWN_ADDR_RANDOM : BLE_OWN_ADDR_PUBLIC;
    if (!rc) rc = ble_gap_adv_start(own, NULL, a.duration_ms ? a.duration_ms : BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc && rc != BLE_HS_EALREADY) ESP_LOGW(TAG, "advertising: %d", rc);
    else if (!rc) printf("YK_KEEP advertising fast=%d\n", s_adv_fast);
}

static void advertise(void) {
    if (!s_active || yk_link_keep_connected()) return;
    if (s_unpaired) {
        advertise_unpaired();
        return;
    }
    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128 = &s_svc_uuid;  // the phone finds it by this
    f.num_uuids128 = 1;
    f.uuids128_is_complete = 1;
    f.appearance = APPEARANCE;
    f.appearance_is_present = 1;
    int rc = ble_gap_adv_set_fields(&f);
    struct ble_hs_adv_fields r = {0};
    r.name = (const uint8_t *)s_cfg.name;
    r.name_len = strlen(s_cfg.name);
    r.name_is_complete = 1;
    ble_uuid16_t hid = BLE_UUID16_INIT(0x1812);
    if (s_cfg.hid) {
        r.uuids16 = &hid;
        r.num_uuids16 = 1;
        r.uuids16_is_complete = 0;
    }
    if (!rc) rc = ble_gap_adv_rsp_set_fields(&r);
    struct ble_gap_adv_params p = {.conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN,
                                   .itvl_min = 160, .itvl_max = 240};  // 100-150 ms
    const uint8_t own = s_addr_ok ? BLE_OWN_ADDR_RANDOM : BLE_OWN_ADDR_PUBLIC;
    if (!rc) rc = ble_gap_adv_start(own, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc && rc != BLE_HS_EALREADY) ESP_LOGW(TAG, "advertising: %d", rc);
}

#if MYNEWT_VAL(BLE_GATTC)
static int on_name(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg) {
    (void)conn, (void)arg;
    if (err->status != 0 || !attr || s_peer_name[0]) return 0;
    char name[sizeof(s_peer_name)];
    uint16_t n = 0;
    ble_hs_mbuf_to_flat(attr->om, name, sizeof(name) - 1, &n);
    name[n] = 0;
    for (char *p = name; *p; p++)
        if ((unsigned char)*p < 0x20) *p = ' ';
    // A UTF-8 character cut at the end by the buffer: drop its leading bytes.
    size_t len = strlen(name);
    size_t back = 0;
    while (back < len && back < 4 && ((unsigned char)name[len - 1 - back] & 0xC0) == 0x80) back++;
    if (back < len && ((unsigned char)name[len - 1 - back] & 0xC0) == 0xC0) {
        const unsigned char lead = (unsigned char)name[len - 1 - back];
        const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
        if (back + 1 < need) name[len - 1 - back] = 0;
    }
    snprintf(s_peer_name, sizeof(s_peer_name), "%s", name);
    printf("YK_KEEP peer name_len=%u\n", (unsigned)strlen(s_peer_name));  // the name is the user's: not logged
    emit(YK_KEEP_EV_NAME);
    return 0;
}
#endif

static void set_untrusted(void) {
    s_enc = s_trusted = s_sub_report = false;
    memset((void *)s_sub, 0, sizeof(s_sub));
}

static int gap_event(struct ble_gap_event *ev, void *arg) {
    (void)arg;
    struct ble_gap_conn_desc d;
    char a[18];
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status != 0) {
            advertise();
            return 0;
        }
        s_conn = ev->connect.conn_handle;
        s_mtu = ble_att_mtu(s_conn);
        set_untrusted();
        s_peer_name[0] = 0;
        printf("YK_KEEP connected\n");
        emit(YK_KEEP_EV_CONNECTED);
        if (s_unpaired) {
            uint8_t nonce[YK_SEAL_NONCE];
            esp_fill_random(nonce, sizeof(nonce));
            xSemaphoreTake(s_link_lock, portMAX_DELAY);
            yk_seal_link_connect(s_link, nonce);  // new random bytes: a new connection key, counters from 1
            xSemaphoreGive(s_link_lock);
            // 45-75 ms: a write a second has room to spare, and the radio sleeps more than at the phone's 15-30 ms.
            // Within Apple's accessory rules (min >= 15 ms, max >= min + 15, max * 3 < the 4 s timeout).
            const struct ble_gap_upd_params up = {.itvl_min = 36, .itvl_max = 60, .latency = 0, .supervision_timeout = 400};
            ble_gap_update_params(s_conn, &up);
#if MYNEWT_VAL(BLE_GATTC)
            ble_gattc_exchange_mtu(s_conn, NULL, NULL);  // phones mostly ask first; Android only when told to
#endif
            return 0;
        }
        {
            // 30-50 ms: key presses stay quick and Wi-Fi keeps most of the air while the viewfinder streams.
            const struct ble_gap_upd_params up = {.itvl_min = 24, .itvl_max = 40, .latency = 0, .supervision_timeout = 400};
            ble_gap_update_params(s_conn, &up);
        }
#if MYNEWT_VAL(BLE_GATTC)
        ble_gattc_read_by_uuid(s_conn, 1, 0xFFFF, BLE_UUID16_DECLARE(0x2A00), on_name, NULL);
#endif
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        if (s_unpaired) {
            xSemaphoreTake(s_link_lock, portMAX_DELAY);
            s_link->open = false;  // nothing more is sealed for this phone
            xSemaphoreGive(s_link_lock);
            s_conn = BLE_HS_CONN_HANDLE_NONE;
            set_untrusted();
            s_mtu = BLE_ATT_MTU_DFLT;
            // The phone is likely to come back: be quick to find for the next 30 s; not after one let go for envelopes
            // that did not open (someone else, or noise: no hurry).
            s_adv_fast = !s_dropping;
            s_dropping = false;
            printf("YK_KEEP disconnected reason=0x%x\n", ev->disconnect.reason);
            emit(YK_KEEP_EV_DISCONNECTED);
            advertise();
            return 0;
        }
        if (s_bond.asking) {  // the new phone went away before an answer: nothing kept
            printf("YK_KEEP bond refuse why=gone\n");
            esp_timer_stop(s_ask_timer);
            const bool repairing = s_bond.repairing;
            yk_bond_answer(&s_bond, false);
            if (repairing) mirror_restore();
            memset(s_held, 0, sizeof(s_held));
            s_held_n = 0;
            emit(YK_KEEP_EV_REFUSED);
        } else if (s_bond.repairing) {  // re-pairing that never got to its keys: the old ones back
            s_bond.repairing = false;
            mirror_restore();
        }
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        set_untrusted();
        printf("YK_KEEP disconnected reason=0x%x\n", ev->disconnect.reason);
        emit(YK_KEEP_EV_DISCONNECTED);
        advertise();
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (s_unpaired) {  // a phone that paired anyway (nothing kept), or tried keys from elsewhere: no matter
            printf("YK_KEEP unpaired enc status=%d\n", ev->enc_change.status);
            return 0;
        }
        s_enc = ev->enc_change.status == 0;
        if (!s_enc) {
            if (s_bond.have) {
                // Keys this device no longer has (a replaced or forgotten phone): let it go, never wait on it.
                printf("YK_KEEP bond unknown status=%d\n", ev->enc_change.status);
                emit(YK_KEEP_EV_UNKNOWN);
            } else {
                printf("YK_KEEP pair failed status=%d\n", ev->enc_change.status);
                emit(YK_KEEP_EV_PAIR_FAILED);
            }
            ble_gap_terminate(ev->enc_change.conn_handle, BLE_ERR_AUTH_FAIL);
            return 0;
        }
        if (ble_gap_conn_find(ev->enc_change.conn_handle, &d) == 0) {
            const yk_bond_addr_t id = bond_addr(&d.peer_id_addr);
            s_trusted = !s_bond.asking && s_bond.have && yk_bond_same(&id, &s_bond.phone);
            addr_text(&d.peer_id_addr, a);
            printf("YK_KEEP encrypted bonded=%d trusted=%d id=%s\n", d.sec_state.bonded, s_trusted, a);
        }
        if (s_trusted) emit(YK_KEEP_EV_TRUSTED);
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE: {
        const uint16_t h = ev->subscribe.attr_handle;
        const bool on = ev->subscribe.cur_notify;
        if (h == s_report_handle) s_sub_report = on;
        for (int i = 0; i < s_cfg.n_chrs; i++)
            if (h == s_chr_handle[i]) s_sub[i] = on;
        emit(YK_KEEP_EV_SUBSCRIBED);
        return 0;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        if (s_unpaired) return BLE_GAP_REPEAT_PAIRING_IGNORE;
        // The remembered phone forgot this device and pairs again. Its old keys come out of the store (NimBLE needs
        // that to go on) but stay remembered until the screen's answer; anything else is let go.
        if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &d) == 0) {
            const yk_bond_addr_t id = bond_addr(&d.peer_id_addr);
            if (yk_bond_repair(&s_bond, &id)) {
                s_bypass = true;
                ble_store_util_delete_peer(&d.peer_id_addr);
                s_bypass = false;
                printf("YK_KEEP bond repairing\n");
                return BLE_GAP_REPEAT_PAIRING_RETRY;
            }
        }
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_unpaired && ev->adv_complete.reason == BLE_HS_ETIMEOUT) s_adv_fast = false;  // the 30 fast seconds
        advertise();
        return 0;
    case BLE_GAP_EVENT_MTU:
        s_mtu = ev->mtu.value;
        printf("YK_KEEP mtu=%u\n", ev->mtu.value);
        emit(YK_KEEP_EV_MTU);
        return 0;
    case BLE_GAP_EVENT_CONN_UPDATE:
        if (s_unpaired && ev->conn_update.status == 0 && ble_gap_conn_find(ev->conn_update.conn_handle, &d) == 0)
            printf("YK_KEEP conn itvl_ms=%u latency=%u timeout_ms=%u\n", d.conn_itvl * 5 / 4, d.conn_latency,
                   d.supervision_timeout * 10);
        return 0;
    default:
        return 0;
    }
}

static void on_sync(void) {
    if (s_addr_ok && ble_hs_id_set_rnd(s_addr) != 0) s_addr_ok = false;  // then the public address
    ble_hs_util_ensure_addr(0);
    advertise();
}

static void on_reset(int reason) { ESP_LOGW(TAG, "host reset %d", reason); }

static void host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/** The long mode's static random address: made once (the two top bits set, not all ones), kept in the app's
 *  namespace. The unpaired mode's is the binding's (yk_link_keys_addr()). */
static void address_load(void) {
    nvs_handle_t h;
    size_t n = sizeof(s_addr);
    if (nvs_open(s_cfg.nvs_ns, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_get_blob(h, "bt_addr", s_addr, &n) == ESP_OK && n == sizeof(s_addr) && (s_addr[5] & 0xC0) == 0xC0) {
        s_addr_ok = true;
    } else {
        do {
            esp_fill_random(s_addr, sizeof(s_addr));
            s_addr[5] |= 0xC0;
        } while (s_addr[0] == 0xFF && s_addr[1] == 0xFF && s_addr[2] == 0xFF && s_addr[3] == 0xFF && s_addr[4] == 0xFF);
        s_addr_ok = nvs_set_blob(h, "bt_addr", s_addr, sizeof(s_addr)) == ESP_OK && nvs_commit(h) == ESP_OK;
    }
    nvs_close(h);
}

esp_err_t yk_link_keep_start(const yk_link_keep_config_t *cfg) {
    if (s_active) return ESP_OK;
    if (!cfg || !cfg->nvs_ns || !cfg->name || cfg->n_chrs < 1 || cfg->n_chrs > YK_KEEP_CHR_MAX ||
        strlen(cfg->name) > YK_KEEP_NAME_MAX)
        return ESP_ERR_INVALID_ARG;
    if (yk_link_ble_active()) return ESP_ERR_INVALID_STATE;
    s_cfg = *cfg;
    yk_link_keep_running = true;
    yk_bond_init(&s_bond);
    address_load();
    mirror_load();
    if (s_mirror.have_peer) {
        s_bond.have = true;
        s_bond.phone = bond_addr(&s_mirror.peer.peer_addr);
    }
    const esp_timer_create_args_t rel = {.callback = release, .name = "yk_keep_hid"};
    const esp_timer_create_args_t ask = {.callback = ask_timeout, .name = "yk_keep_ask"};
    if (!s_release_timer && esp_timer_create(&rel, &s_release_timer) != ESP_OK) goto fail;
    if (!s_ask_timer && esp_timer_create(&ask, &s_ask_timer) != ESP_OK) goto fail;
    if (nimble_port_init() != ESP_OK) goto fail;
    ble_npl_event_init(&s_answer_ev, answer_on_host, NULL);
    ble_npl_event_init(&s_forget_ev, forget_on_host, NULL);
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;  // Just Works, as a shutter remote
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    build_gatt();
    if (ble_gatts_count_cfg(s_svcs) || ble_gatts_add_svcs(s_svcs)) {
        ESP_LOGE(TAG, "GATT table refused");
        goto fail;
    }
    ble_svc_gap_device_name_set(s_cfg.name);
    ble_svc_gap_device_appearance_set(APPEARANCE);
    ble_store_config_init();
    s_write = ble_hs_cfg.store_write_cb;
    s_delete = ble_hs_cfg.store_delete_cb;
    ble_hs_cfg.store_write_cb = store_write;
    ble_hs_cfg.store_delete_cb = store_delete;
    mirror_restore();  // before the host runs: the phone's IRK is resolved from the first connection
    s_active = true;
    nimble_port_freertos_init(host_task);
    printf("YK_KEEP start remembered=%d addr=%s\n", s_bond.have, s_addr_ok ? "random" : "public");
    return ESP_OK;
fail:
    yk_link_keep_running = false;
    return ESP_FAIL;
}

esp_err_t yk_link_keep_start_unpaired(const yk_link_keep_unpaired_t *cfg) {
    if (s_active) return s_unpaired ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (!yk_rules_unpaired_ok(cfg)) return ESP_ERR_INVALID_ARG;
    if (!yk_link_keys_app()) return ESP_ERR_NOT_SUPPORTED;  // app.json has no voiceCode number
    // After yk_link_init(); the address may be written to NVS: not from a PSRAM stack.
    if (yk_link_ble_active() || !yk_link_device_id()[0] || !yk_link_keys_stack_ok()) return ESP_ERR_INVALID_STATE;
#if CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE < YK_LINK_HOST_STACK_MIN
    // Measured 11-13: the component's connection, handshake and crypto take ~3.1 KB of NimBLE's host stack; at 4096 a
    // later NimBLE, compiler or one more call deep could overflow it on the road. Refused at run time, not with #error:
    // Yay builds this file too and never starts the mode.
    printf("YK_KEEP start refused: CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=%d, the unpaired mode needs %d (sdkconfig.defaults)\n",
           CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE, YK_LINK_HOST_STACK_MIN);
    return ESP_ERR_INVALID_STATE;
#endif
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.nvs_ns = cfg->nvs_ns;
    s_cfg.name = LINK_NAME;
    s_cfg.on_event = cfg->on_event;
    s_cfg.ctx = cfg->ctx;
    s_cfg.n_chrs = 3;
    const uint8_t svc[16] = LINK_UUID(0x50), frames[16] = LINK_UUID(0x51), notify[16] = LINK_UUID(0x52),
                  session[16] = LINK_UUID(0x53);
    memcpy(s_cfg.service, svc, 16);
    memcpy(s_cfg.chrs[L_FRAMES].uuid, frames, 16);
    s_cfg.chrs[L_FRAMES].flags = YK_KEEP_WRITE | YK_RULES_WRITE_NR;  // LinkOpen with response, envelopes without
    memcpy(s_cfg.chrs[L_NOTIFY].uuid, notify, 16);
    s_cfg.chrs[L_NOTIFY].flags = YK_KEEP_NOTIFY;
    memcpy(s_cfg.chrs[L_SESSION].uuid, session, 16);
    s_cfg.chrs[L_SESSION].flags = YK_KEEP_READ;
    s_app = yk_link_keys_app();  // never the app's word for it: another number would open another app's key
    s_on_frame = cfg->on_frame;
    s_unpaired = true;
    s_adv_fast = true;
    s_mtu = BLE_ATT_MTU_DFLT;
    yk_link_keep_running = true;
    yk_bond_init(&s_bond);
    // PSRAM: the phone's write and the frame opened from it (host task), a sealed frame out, keys and counters.
    if (!s_value) s_value = heap_caps_malloc(2 * YK_RULES_VALUE_MAX + YK_SEAL_FRAME_MAX + YK_SEAL_OVERHEAD,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_link) s_link = heap_caps_calloc(1, sizeof(*s_link), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_link_lock) s_link_lock = xSemaphoreCreateMutex();
    // The app's queue in PSRAM (never touched from an ISR); its task's stack internal (a callback may write NVS).
    if (!s_app_q) s_app_q = xQueueCreateWithCaps(APP_QUEUE_LEN, sizeof(app_msg_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_app_q && !s_app_task && xTaskCreate(app_task, "yk_link_app", APP_TASK_STACK, NULL, 5, &s_app_task) != pdPASS)
        goto fail;
    if (!s_value || !s_link || !s_link_lock || !s_app_q) goto fail;
    s_frame = s_value + YK_RULES_VALUE_MAX;
    s_tx = s_frame + YK_RULES_VALUE_MAX;
    {
        uint8_t key[YK_SEAL_KEY];
        uint32_t kid = 0;
        const bool have = yk_link_keys_load(s_app, key, &kid);
        yk_seal_link_init(s_link, s_app, have ? key : NULL, kid);
        memset(key, 0, sizeof(key));
    }
    memcpy(s_svc_data, svc, 16);
    yk_seal_service_data(yk_link_keep_mark(), s_app, s_svc_data + 16);
    // The address is the binding's (yk_link_keys.c), not the app's namespace's as in the long mode: claimed again or
    // unbound, it changes with the keys. Not kept (NVS full), it is still a static random one, for this time only.
    yk_link_keys_addr(s_app, s_addr);
    s_addr_ok = true;
    if (nimble_port_init() != ESP_OK) goto fail;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    // No store: nothing of a phone is kept (keys, subscriptions); no bonding, no key handed out. A phone may still
    // pair Just Works if it insists; nothing needs it and nothing is kept.
    ble_hs_cfg.store_read_cb = NULL;
    ble_hs_cfg.store_write_cb = NULL;
    ble_hs_cfg.store_delete_cb = NULL;
    ble_hs_cfg.store_status_cb = NULL;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;
    ble_hs_cfg.sm_our_key_dist = 0;
    ble_hs_cfg.sm_their_key_dist = 0;
    ble_att_set_preferred_mtu(BLE_ATT_MTU_MAX);  // 527; phones settle on 185 (iOS) to 517 (Android)
    ble_svc_gap_init();
    ble_svc_gatt_init();
    build_gatt();
    if (ble_gatts_count_cfg(s_svcs) || ble_gatts_add_svcs(s_svcs)) {
        ESP_LOGE(TAG, "GATT table refused");
        goto fail;
    }
    ble_svc_gap_device_name_set(LINK_NAME);
    s_active = true;
    nimble_port_freertos_init(host_task);
    printf("YK_KEEP start mode=unpaired app=%u key=%s addr=%s\n", (unsigned)s_app,
           s_link->have_key ? "yes" : "none", s_addr_ok ? "random" : "public");
    return ESP_OK;
fail:
    s_unpaired = false;
    yk_link_keep_running = false;
    return ESP_FAIL;
}

void yk_link_keep_end(int wait_ms) {
    if (!s_active) return;
    s_active = false;  // no advertising from here on
    ble_gap_adv_stop();
    const uint16_t conn = s_conn;
    if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    for (int t = 0; t < wait_ms && yk_link_keep_connected(); t += 10) vTaskDelay(pdMS_TO_TICKS(10));
    printf("YK_KEEP end connected=%d\n", yk_link_keep_connected());
}

// ---------------------------------------------------------------- console

static const char *key_state_name(yk_link_key_state_t k) {
    return k == YK_LINK_KEY_OK ? "ok" : k == YK_LINK_KEY_MISMATCH ? "mismatch" : "none";
}

#if CONFIG_YAY_DEV
static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
#endif

static int keep_cmd(int argc, char **argv) {
    const char *sub = argc > 1 ? argv[1] : "status";
    if (!strcmp(sub, "status")) {
        printf("YK_KEEP status active=%d mode=%s connected=%d trusted=%d mtu=%u", s_active, s_unpaired ? "unpaired" : "long",
               yk_link_keep_connected(), yk_link_keep_trusted(), yk_link_keep_mtu());
        if (s_unpaired)
            printf(" app=%u key=%s mark=%lu subscribed=%d drops=%u", (unsigned)s_app,
                   key_state_name(yk_link_unpaired_key_state()), (unsigned long)yk_link_keep_mark(),
                   s_sub[L_NOTIFY], (unsigned)s_link_drops);
        printf("\n");
        return 0;
    }
#if CONFIG_YAY_DEV
    if (!strcmp(sub, "key") && argc == 3 && !strcmp(argv[2], "clear")) {
        yk_link_keys_forget();
        return 0;
    }
    if (!strcmp(sub, "key") && argc == 4 && strlen(argv[3]) == 2 * YK_SEAL_KEY) {
        const unsigned long kid = strtoul(argv[2], NULL, 10);
        uint8_t key[YK_SEAL_KEY];
        bool ok = kid > 0 && kid <= 0xFFFFFFFFul;
        for (int i = 0; ok && i < YK_SEAL_KEY; i++) {
            const int a = hex_value(argv[3][2 * i]), b = hex_value(argv[3][2 * i + 1]);
            ok = a >= 0 && b >= 0;
            key[i] = (uint8_t)(a << 4 | b);
        }
        ok = ok && yk_link_keys_put((uint32_t)kid, key);
        memset(key, 0, sizeof(key));
        memset(argv[3], 0, strlen(argv[3]));
        printf(ok ? "YK_OK keep key\n" : "YK_ERR keep key\n");  // a restart takes it into a running link
        return ok ? 0 : 1;
    }
    if (!strcmp(sub, "refresh")) {
        const esp_err_t err = yk_link_unpaired_refresh(argc > 2 && !strcmp(argv[2], "force"));
        printf("YK_KEEP refresh err=%s key=%s\n", esp_err_to_name(err), key_state_name(yk_link_unpaired_key_state()));
        return err == ESP_OK ? 0 : 1;
    }
#endif
#if CONFIG_YAY_DEV
    printf("keep [status] | key <kid> <64 hex> | key clear | refresh [force]\n");
#else
    printf("keep [status]\n");
#endif
    return 1;
}

esp_err_t yk_link_keep_register_console(void) {
    const esp_console_cmd_t cmd = {.command = "keep", .help = "Bluetooth's long and unpaired modes", .func = keep_cmd};
    return esp_console_cmd_register(&cmd);
}
