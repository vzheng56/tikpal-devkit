// Yaykit Wi-Fi: one station connection shared by every app in the yaykit-v1 layout.
//
// Credentials are set from the phone over Yaykit Link (standard prov-scan / prov-config endpoints), or from the
// desktop client and web installer over USB with the `wifi.scan` / `wifi.join` commands. Up to 5 networks are
// remembered (NVS yk_wifi/nets, yk_wifi_list.h): one sent is tried first and remembered only once it has joined, so
// it adds to the list instead of replacing what was there; at a start the last joined goes first, and when it cannot
// be joined the others the device can see, the most recently used first. The last joined is also kept where esp_wifi
// keeps it (nvs.net80211), so a firmware built before the list joins it as it always did.
// The password is never logged, echoed or reported; over USB it travels only on the hidden argument line of
// `yk cmd wifi.join -` (see yaykit_link.h).
// Console: `wifi status`, `wifi scan`, `wifi list` (names only), `wifi forget` (all of them).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YK_WIFI_OFF,
    YK_WIFI_UNCONFIGURED,
    YK_WIFI_CONNECTING,
    YK_WIFI_CONNECTED,
    YK_WIFI_FAILED,  // retries continue in the background with a longer delay
} yk_wifi_state_t;

/** Starts the station and connects with stored credentials, if any. */
esp_err_t yk_wifi_start(void);
yk_wifi_state_t yk_wifi_state(void);
/** ESP-IDF's wifi_err_reason_t of the last time the router went (0: none yet this start). For the report's
 *  net.last.wifiReason (Yay 0.1.20, yaykit_session links it weakly). Any task. */
uint8_t yk_wifi_last_reason(void);
const char *yk_wifi_state_name(void);
/** Waits until the station has an IP address. */
bool yk_wifi_wait_connected(uint32_t timeout_ms);
/** Fills a yk-info "wifi" object: state; ssid / rssi / ip when connected, with what the last join took in joinMs and
 *  its two parts linkMs and dhcpMs (yk_wifi_timing.h); error (WRONG_PASSWORD or NOT_FOUND) when failed. */
void yk_wifi_describe(cJSON *wifi);
/** For a status line (the system pull-down, docs/delivery/2026-11-02-system-pulldown.md): the state as yk_wifi_state()
 *  (a yk_wifi_state_t, as an int), and when connected the network's name in `ssid` ("" otherwise) and its signal in
 *  `rssi` (dBm; 0 otherwise). Plain types, so that yaykit_app can link it weakly (an app without Wi-Fi has none).
 *  `ssid` and `rssi` may be NULL. */
int yk_wifi_status(char *ssid, size_t size, int *rssi);
/**
 * Yaykit Link commands; ESP_ERR_NOT_FOUND for other names.
 *   wifi.scan                         -> {"networks":[{"ssid","rssi","auth":"open|password|unsupported"}]}, strongest
 *                                        first, one entry per name, hidden networks left out; blocks up to 15 s.
 *   wifi.join {"ssid","password"}     -> {"state":"connecting","ssid"}; then follow yk-info "wifi" to connected or
 *                                        failed. Errors: BAD_SSID, BAD_PASSWORD, WIFI_OFF, JOIN_FAILED.
 */
esp_err_t yk_wifi_command(const char *name, const cJSON *args, cJSON *reply, const char **code);
/** Forgets a network the phone sent but never applied (prov-config without apply): call when the phone link goes
 *  off, so the password does not stay in memory. */
void yk_wifi_drop_pending(void);
/** wifi_prov_scan_handlers_t / wifi_prov_config_handlers_t for yk_link_config_t. */
void *yk_wifi_scan_handlers(void);
void *yk_wifi_config_handlers(void);
/** Connected, but nothing gets through (the caller looked: a name that does not resolve, say): leaves the network
 *  and joins it again, which also renews the address. ESP_ERR_INVALID_STATE when not connected. `why` is logged. */
esp_err_t yk_wifi_reconnect(const char *why);
/** Not connected, and waiting to look again for a remembered network (yk_wifi_list.h: 15 s, 30 s, then every
 *  60 s): looks now. For a moment the owner is there to wait for it (the screen woken by the key). Changes nothing
 *  else: ESP_ERR_INVALID_STATE when connected, joining, or with nothing remembered. `why` is logged. */
esp_err_t yk_wifi_retry_now(const char *why);
/** Not connected: wait at least `s` seconds between rounds that joined nothing, from the next wait on (0: as ever, 15 s,
 *  30 s, then 60 s). The main firmware's 极简模式 (Yay 0.1.20 开发文档 4.2: 5 minutes, 15 on a low battery), so that a
 *  recorder left without a network does not spend its battery looking. yk_wifi_retry_now() still looks at once. */
void yk_wifi_set_slow(int s);
/** Registers the `wifi` console command. */
esp_err_t yk_wifi_register_console(void);

#ifdef __cplusplus
}
#endif
