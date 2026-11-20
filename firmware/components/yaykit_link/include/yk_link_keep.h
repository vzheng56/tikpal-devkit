// Yaykit Link's long mode (docs/design/selfie.md 3, 自拍屏): Bluetooth on for as long as the app runs, one phone
// remembered, the round screen a HID key (Consumer Control: volume up, which the phone's camera takes as its
// shutter) and the app's own service on the same peripheral. Pairing happens here, in the app, as the exception the
// design agreed to (selfie.md 8): only in this app, one phone, gone when the app is left.
//
// Added beside yaykit_link.h; the provisioning link (yk_link_ble_start) is untouched. The two never run together:
// while the long mode runs, yk_link_ble_start() returns ESP_ERR_INVALID_STATE (and the other way round). Built only
// with CONFIG_BT_NIMBLE_ENABLED: firmware without NimBLE (Party Play) has none of these functions.
//
// - The phone's keys and subscriptions are kept in the app's own NVS namespace (`nvs_ns`), never NimBLE's own
//   (`nimble_bond`) nor any other firmware's: set CONFIG_BT_NIMBLE_NVS_PERSIST=n; NimBLE's RAM store is filled from
//   it at start. Yay and the other apps never see them.
// - A static random address of its own, made once and kept in `nvs_ns`: phones that paired with another firmware
//   at the chip's public address (the lab saw a tablet retry every 7-10 s) do not take it for theirs.
// - One phone (yk_link_bond.h): a second phone that pairs is held back and YK_KEEP_EV_ASK says so; the app asks on
//   the screen and calls yk_link_keep_answer(). While asked about, the new phone reads and writes nothing of the
//   app's service (insufficient authorisation) and gets no key presses.
// - Undirected advertising (100-150 ms) whenever nobody is connected; one connection, so nobody else finds it while
//   the phone is connected.
// - The phone's name (its GAP Device Name, read once connected) for the screen; it needs the GATT client
//   (CONFIG_BT_NIMBLE_ROLE_CENTRAL, CONFIG_BT_NIMBLE_GATT_CLIENT), else it stays "".
//
// Every characteristic of the app's service needs an encrypted link: the first read brings up the phone's pairing
// alert. Callbacks run on NimBLE's host task: keep them short, do not block, and write NVS elsewhere.
//
// The unpaired mode (不配对模式, yk_link_keep_start_unpaired(); Yay 0.1.20's components, 开发包 1.11; 导航 first):
// 设备通道 (device.link, packages/contracts/src/device-link.ts). The app's 小程序 on the phone talks to this app on
// this device through the Tikpal app, over Bluetooth without pairing: no system dialog, nothing of the phone kept. The
// component owns all of it, so every app that uses it is the same on the air:
// - the advertisement: the link's service; the scan response its data (this device's mark, the app's number) and the
//   name "Tikpal". Only while the app runs it; one phone at a time (connected, it stops advertising);
// - the GATT service: `session` (read), `frames` (the phone's writes), `notify` (the device's); no security manager,
//   no bonding, no _ENC, no HID, battery or device information service;
// - the link key: the platform's, one for each device and app, fetched with the app's token
//   (yk_link_unpaired_refresh()) and kept in the component's own NVS namespace `yk_link_keys` ("k<app number>");
//   Yay clears it on unbinding. An app never sees it, only its state (yk_link_unpaired_key_state());
// - the address: a static random one for each device and app, made the first time and kept beside the key
//   ("a<app number>"), so a phone reconnects by it; it goes with the keys on unbinding and claiming, and the owner
//   before cannot pick the device out by the address their phone kept;
// - the app's number: the 应用码 number built in from app.json's voiceCode.number (yk_vc_app_number()), never one the
//   app passes (it would open another app's key). An app without one cannot use the link;
// - the handshake and AES-128-GCM both ways: the app gets the phone's frames opened (on_frame) and sends its own with
//   yk_link_keep_send(); YK_KEEP_EV_TRUSTED once a phone opened the link with this device's key.
// NVS is the whole device's: another app on the same device could read the namespace; review keeps them out of it.
// Its own entry function, so the review (devkit/review/symbols.json `device`) finds an app using it by name: app.json
// must declare `bluetooth_unpaired` (permissions.device), asked for, and looked at by a person on every version.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YK_KEEP_CHR_MAX 4
#define YK_KEEP_NAME_MAX 20  // the Bluetooth name, UTF-8 bytes: it goes in the 31-byte scan response

enum { YK_KEEP_READ = 1, YK_KEEP_WRITE = 2, YK_KEEP_NOTIFY = 4 };

typedef struct {
    uint8_t uuid[16];  // 128-bit, least significant byte first (as NimBLE's BLE_UUID128_INIT)
    uint8_t flags;     // YK_KEEP_READ | YK_KEEP_WRITE | YK_KEEP_NOTIFY; always encrypted
} yk_keep_chr_t;

typedef enum {
    YK_KEEP_EV_CONNECTED,     // a phone connected (nothing trusted yet)
    YK_KEEP_EV_TRUSTED,       // the remembered phone is in, encrypted: reads, writes, notifications, key presses
    YK_KEEP_EV_PAIRED,        // the first phone paired and is remembered now (TRUSTED follows)
    YK_KEEP_EV_ASK,           // another phone paired: ask 「换成这台手机？」, then yk_link_keep_answer()
    YK_KEEP_EV_REPLACED,      // the answer was yes: the new phone is the remembered one (TRUSTED follows)
    YK_KEEP_EV_REFUSED,       // no, the time ran out, or the new phone left: the old one stays
    YK_KEEP_EV_PAIR_FAILED,   // no phone remembered yet and pairing did not finish (cancelled, timed out)
    YK_KEEP_EV_UNKNOWN,       // a phone with keys this device no longer has (replaced, forgotten): let go
    YK_KEEP_EV_NAME,          // the connected phone's name was read (yk_link_keep_peer_name())
    YK_KEEP_EV_SUBSCRIBED,    // notifications on a characteristic turned on or off (yk_link_keep_subscribed())
    YK_KEEP_EV_DISCONNECTED,
    YK_KEEP_EV_MTU,           // the phone agreed an MTU (yk_link_keep_mtu()); both modes. Only added at the end.
    YK_KEEP_EV_KEY,           // the unpaired mode: yk_link_unpaired_key_state() changed
} yk_keep_event_t;

typedef struct {
    const char *nvs_ns;  // the app's namespace: the phone's keys ("bond"), this device's address ("bt_addr")
    const char *name;    // the Bluetooth name (in the owner's language), at most YK_KEEP_NAME_MAX bytes
    uint8_t service[16]; // the app's service, in the advertisement (phones find the device by it)
    yk_keep_chr_t chrs[YK_KEEP_CHR_MAX];
    int n_chrs;
    /** A read of chrs[chr]: up to `size` bytes into `out`; returns the length (-1: refuse). */
    int (*on_read)(int chr, char *out, size_t size, void *ctx);
    /** A write to chrs[chr] (only from the trusted phone). */
    void (*on_write)(int chr, const uint8_t *data, size_t len, void *ctx);
    void (*on_event)(yk_keep_event_t ev, void *ctx);
    void *ctx;
    bool hid;            // the HID key (volume up) beside the app's service
    int ask_seconds;     // 「换成这台手机？」 times out as a no after this (0: 30)
} yk_link_keep_config_t;

#define YK_LINK_FRAME_MAX 160  // the unpaired mode: a frame, either way (LINK.frame.maxBytes less the envelope)
/** The unpaired mode refuses to start below this CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE (set it in the app's
 *  sdkconfig.defaults; about 1 KB more internal RAM): the component's own handshake and crypto take ~3.1 KB of it. */
#define YK_LINK_HOST_STACK_MIN 5120

/** The unpaired mode (yk_link_keep_start_unpaired()). */
typedef struct {
    const char *nvs_ns;  // the app's namespace: nothing of the mode's is kept there (the address went beside the key,
                         // 11-14: changed with it); still asked for
    /** A frame from the phone, opened: version, kind, the rest (the kinds are the app's, device-link.ts; never
     *  LinkOpen), at most YK_LINK_FRAME_MAX bytes, only valid during the call. */
    void (*on_frame)(const uint8_t *frame, size_t len, void *ctx);
    /** CONNECTED, TRUSTED (the phone opened the link with this device's key), MTU, SUBSCRIBED, KEY, DISCONNECTED.
     *  Both callbacks run in the component's own task `yk_link_app` (4 KB stack in internal RAM), never on NimBLE's
     *  host task, one at a time and in the order things came; they may call yk_link_keep_send(). Keep them short: 16
     *  waiting is the most, and a frame that finds no room is dropped (YK_KEEP app behind …). */
    void (*on_event)(yk_keep_event_t ev, void *ctx);
    void *ctx;
} yk_link_keep_unpaired_t;

typedef enum {
    YK_LINK_KEY_NONE,      // none on this device for this app yet: 「到有 Wi-Fi 的地方打开一次」
    YK_LINK_KEY_OK,        // one, and no phone said otherwise
    YK_LINK_KEY_MISMATCH,  // a phone came with another: this one is likely old (unbound and bound again); fetch again
} yk_link_key_state_t;

/** Starts NimBLE in the long mode and advertises. From a task with an internal-RAM stack (it reads and may write
 *  NVS). ESP_ERR_INVALID_STATE while the provisioning link runs. */
esp_err_t yk_link_keep_start(const yk_link_keep_config_t *cfg);
/** Starts NimBLE in the unpaired mode and advertises (CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE ≥ YK_LINK_HOST_STACK_MIN,
 *  else ESP_ERR_INVALID_STATE and a log line saying so): every 20-30 ms for the first 30 s (and again for 30 s after
 *  each disconnection, so a phone in the background finds it back quickly), then every 152.5 ms. Without a key it
 *  still advertises (the phone says why it cannot open). From a task with an internal-RAM stack, after yk_link_init()
 *  (it reads and may write NVS: a PSRAM stack gets ESP_ERR_INVALID_STATE rather than an assert). ESP_ERR_INVALID_STATE
 *  also while the provisioning link or the long mode runs; ESP_ERR_NOT_SUPPORTED: app.json has no voiceCode number;
 *  ESP_ERR_INVALID_ARG for a bad `cfg`. Ended by yk_link_keep_end(). Of the functions below, the unpaired mode uses end, active, connected, trusted
 *  (the link is open), subscribed (to the link's notifications, whatever `chr`), mtu and disconnect; notify and press
 *  refuse (frames go sealed: yk_link_keep_send()), remembered and asking stay false, the others do nothing. */
esp_err_t yk_link_keep_start_unpaired(const yk_link_keep_unpaired_t *cfg);
/** A frame to the phone (version, kind, the rest), sealed and notified: at most YK_LINK_FRAME_MAX bytes and the MTU's
 *  room (MTU − 23). ESP_ERR_INVALID_STATE: the link is not open or the phone takes no notifications;
 *  ESP_ERR_INVALID_SIZE. Any task. */
esp_err_t yk_link_keep_send(const void *frame, size_t len);
/** This app's link key's state on this device; any time (before the start too). */
yk_link_key_state_t yk_link_unpaired_key_state(void);
/** Asks the platform for this device's link key for this app (yaykit_cloud, the app's token) and keeps it, when there
 *  is none, a phone came with another, it is older than 7 days, or `force`. Never hands the key out: see
 *  yk_link_unpaired_key_state() after. When: online (Wi-Fi up), before yk_link_keep_start_unpaired(): the app's home
 *  page as it opens is the place. HTTPS for a few seconds and a TLS connection's internal RAM, so never while the
 *  unpaired mode runs (one network thing at a time). From a task with an internal-RAM stack (the main task, a console
 *  command; it writes NVS: a PSRAM stack gets ESP_ERR_INVALID_STATE). ESP_OK (fetched, or nothing to do);
 *  ESP_ERR_INVALID_STATE (Bluetooth on, or a PSRAM stack); ESP_ERR_NOT_SUPPORTED (no voiceCode number, or no
 *  yaykit_cloud); ESP_ERR_INVALID_RESPONSE (the platform said no or answered nonsense); ESP_FAIL (no network). */
esp_err_t yk_link_unpaired_refresh(bool force);
/** This device's mark (20 bits, device-link.ts): what an app's 声波码 carries to pick this device among others near.
 *  Not a secret. */
uint32_t yk_link_keep_mark(void);
/** Leaving the app (a restart follows): stops advertising and lets the phone go, waiting up to `wait_ms` for it to
 *  be gone, so it keeps no Bluetooth keyboard. NimBLE itself stays up. Any task. */
void yk_link_keep_end(int wait_ms);
bool yk_link_keep_active(void);
bool yk_link_keep_connected(void);
/** The connection's ATT MTU: 23 until the phone agrees a bigger one (YK_KEEP_EV_MTU), 0 when nobody is connected. A
 *  write or a notification holds at most MTU − 3 bytes. Both modes. */
uint16_t yk_link_keep_mtu(void);
/** Lets the connected phone go (advertising resumes); nothing when nobody is connected. Both modes, any task. */
void yk_link_keep_disconnect(void);
/** The remembered phone is connected and encrypted (and not being asked about). */
bool yk_link_keep_trusted(void);
/** A phone is remembered. */
bool yk_link_keep_remembered(void);
bool yk_link_keep_asking(void);
/** The connected (or asked-about) phone's name, "" when not read. */
void yk_link_keep_peer_name(char *out, size_t size);
bool yk_link_keep_subscribed(int chr);

/** A notification on chrs[chr] to the trusted phone, when it subscribed. Cut at the link's MTU - 3. The long mode
 *  only (the unpaired mode: yk_link_keep_send()). */
esp_err_t yk_link_keep_notify(int chr, const void *data, size_t len);
/** The HID key: volume up pressed, released after 40 ms (a longer press makes an iPhone record). Trusted only. */
esp_err_t yk_link_keep_press(void);
/** The battery service's level (0..100). */
void yk_link_keep_battery(int percent);
/** 「换成这台手机？」's answer (any task). */
void yk_link_keep_answer(bool yes);
/** Forgets the remembered phone (development: `keep forget`). */
void yk_link_keep_forget(void);
/** Console `keep`: status (either mode: connected, MTU; the unpaired mode: open, the key's state, the mark, phones let
 *  go); development builds (CONFIG_YAY_DEV) also `keep key <kid> <64 hex>` (a bench's key for this app, never a real
 *  one), `keep key clear` and `keep refresh [force]`. Register it after esp_console's REPL is made. */
esp_err_t yk_link_keep_register_console(void);

#ifdef __cplusplus
}
#endif
