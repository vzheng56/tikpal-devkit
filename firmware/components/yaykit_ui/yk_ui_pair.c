// The "Connect phone" pairing content (yaykit_ui.h yk_ui_pair_content): in a file of its own so that only firmware
// that pairs (the launcher) links Yaykit Link's Bluetooth side; Party Play and Breathe use yaykit_ui without it.
// The code (handoff docs/handoff/2026-11-01-voice-code.md 2.1③): 声波码 in a firmware that lists
// components/yaykit_voicecode, linked weakly (yaykit_ui does not depend on it); otherwise the QR code as before, which
// needs CONFIG_LV_USE_QRCODE (without it the page shows the 8-digit code alone).
#include <stdio.h>
#include <string.h>

#include "yaykit_link.h"
#include "yaykit_ui.h"
#include "yk_round.h"
#include "yk_ui_priv.h"

lv_obj_t *yk_vc_pair_layer(lv_obj_t *screen, const char *suffix, const char *digits) __attribute__((weak));

void yk_ui_pair_content(lv_obj_t *screen, bool starting, lv_event_cb_t on_turn_on, lv_event_cb_t on_turn_off) {
    if (!yk_link_ble_active()) {
        const char *state = yk_ui_is_english() ? (starting ? "Turning on Bluetooth…" : "Bluetooth is off") : (starting ? "正在打开蓝牙…" : "蓝牙已关闭");
        yk_ui_label(screen, state, 35, 214, 396, 20, YK_INK2);
        if (!starting) yk_ui_button(screen, yk_ui_is_english() ? "Turn on" : "打开蓝牙", 143, 276, 180, 50, on_turn_on, NULL, YK_CORAL);
        return;
    }
    const bool connected = yk_link_ble_connected();
    if (yk_vc_pair_layer) {
        // Waiting for a phone: the pairing code over the whole screen, as large as the app reads it (R 184), the 8
        // digits in its middle for an app that types them. A tap on it turns Bluetooth off: the caller's page shows
        // again (its way back, 「打开蓝牙」). A phone on the link: the page, without the code.
        const char *name = yk_link_ble_name();  // "YK-" and the device id's last 4, lower-case hex
        lv_obj_t *layer =
            !connected && strlen(name) == 7 ? yk_vc_pair_layer(screen, name + 3, yk_link_pairing_code()) : NULL;
        if (layer) {
            lv_obj_add_event_cb(layer, on_turn_off, LV_EVENT_CLICKED, NULL);
            return;
        }
        if (connected) {
            const bool en = yk_ui_is_english();
            yk_ui_label(screen, en ? "Phone connected" : "已连接手机", 35, 196, 396, 30, YK_SAGE);
            yk_ui_label(screen, en ? "Carry on in the Tikpal app" : "在 Tikpal App 里接着操作", 35, 244, 396, 20, YK_INK2);
            yk_ui_button(screen, en ? "Turn off" : "关闭蓝牙", 143, 366, 180, 44, on_turn_off, NULL, YK_PANEL);
            return;
        }
    }
#if LV_USE_QRCODE
    lv_obj_t *qr = lv_qrcode_create(screen);
    lv_qrcode_set_size(qr, 140);
    lv_qrcode_set_dark_color(qr, lv_color_hex(YK_PAPER));
    lv_qrcode_set_light_color(qr, lv_color_hex(YK_INK));
    lv_obj_set_pos(qr, 157, 128);
    lv_obj_set_style_border_color(qr, lv_color_hex(YK_INK), 0);
    lv_obj_set_style_border_width(qr, 6, 0);
    const char *payload = yk_link_pairing_qr();
    if (lv_qrcode_update(qr, payload, strlen(payload)) != LV_RESULT_OK) printf("YK_UI_QR error len=%u\n", (unsigned)strlen(payload));
#endif
    const char *pin = yk_link_pairing_code();
    char text[16];
    snprintf(text, sizeof(text), "%.4s %.4s", pin, pin + 4);
    yk_ui_label(screen, text, 0, 286, 466, 30, YK_INK);
    const char *hint = yk_ui_is_english() ? (connected ? "Phone connected" : "Tikpal app: scan or enter the code")
                               : (connected ? "已连接手机" : "用 Tikpal App 扫码或输入配对码");
    yk_ui_label(screen, hint, 35, 326, 396, 20, connected ? YK_SAGE : YK_INK2);
    yk_ui_button(screen, yk_ui_is_english() ? "Turn off" : "关闭蓝牙", 143, 366, 180, 44, on_turn_off, NULL, YK_PANEL);
}
