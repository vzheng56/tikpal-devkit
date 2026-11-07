// The system pull-down in an extension (docs/delivery/2026-11-02-system-pulldown.md): a finger that comes down on the
// screen's top edge and pulls down opens yaykit_ui's yk_sheet over the app, the same one as Yay's (the system's: apps
// cannot take it or change it). Elsewhere a swipe down stays the app's.
//
// The edge: an invisible strip (466 x YK_PULL_EDGE_PX) on LVGL's top layer, over the app's screen, made once the app is
// ready (yk_app_ready). A press that comes down there is the strip's, not the app's; once it has gone YK_PULL_PX down
// (more down than sideways, yk_pull_move) the sheet opens and the rest of that press goes nowhere. A tap on the strip
// does nothing (the safe circle starts below it: nothing of an app's is there). Not while the app ring, 「回到 Yay 说？」 or
// the 应用码 is up, nor while leaving (yk_app_ring_sheet_opening).
//
// The sheet: on the app's screen, its top child (so `snap` has it), moved to the top again if the app adds objects;
// its swipes are its own (yk_sheet_take_swipes), not the app's. The rows the extension can know (8.1): Wi-Fi when the
// firmware has yaykit_wifi (「连不上服务器」 when yaykit_cloud's last connection failed), the battery always, the phone
// when it has an account (yaykit_account), the time when Yay has written its UTC offset into the shared settings (UTC
// plus that offset, worked out here: the firmware's own zone is never set);
// 「在手机上打开」 when the app has a 应用码 (8.5: a tap shows it full screen, back to the app when it goes). Every second
// it says them again while it is up. Any swipe, a tap on the dimmed part, the button or a press of the key closes it
// (round-screen-gestures.md 5.2; the key through yk_app_ring.c's yk_keys). Objects exist only while it is up.
//
// yaykit_wifi, yaykit_cloud and yaykit_account are linked weakly: an app without them has no such row. Everything here
// runs in the LVGL task, holding the board lock.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "yaykit_app.h"
#include "yaykit_ui.h"
#include "yk_app_logic.h"
#include "yk_app_ring.h"
#include "yk_sheet.h"

int yk_wifi_status(char *ssid, size_t size, int *rssi) __attribute__((weak));
int yk_account_status(char *nickname, size_t size) __attribute__((weak));
int yk_cloud_reach(void) __attribute__((weak));

#define REFRESH_MS 1000
#define TIME_SET_S 1767225600  // 2026: a clock before this was never set

static const char *s_slug = "";
static lv_obj_t *s_edge;          // the strip on the top layer
static yk_sheet_t *s_sheet;       // up
static lv_timer_t *s_timer;
static yk_pull_t s_pull;
static int s_edge_px = YK_PULL_EDGE_PX, s_pull_px = YK_PULL_PX;  // `yay sheet edge|pull` changes them (measuring)
static int64_t s_open_ms;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void fill(void) {
    if (!s_sheet) return;
    const bool en = yk_app_english();
    // The time: UTC plus the offset Yay keeps in the shared settings, worked out here; the firmware's own zone (setenv,
    // the app's localtime) is never touched. No offset written (a Yay before 0.1.14): no time, rather than a guess.
    char clock[8] = "";
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int offset = 0;
    if (tv.tv_sec > TIME_SET_S && board_pref_tz_offset(&offset)) yk_sheet_clock_text(tv.tv_sec, offset, clock, sizeof(clock));
    yk_sheet_view_t v = {.clock = clock, .hint = en ? "Swipe up to close" : "上滑收起"};
    int k = 0;
    char wifi[64], battery[64], phone[96], ssid[33], nickname[64];
    if (yk_wifi_status) {
        int rssi = 0;
        bool problem;
        const int state = yk_wifi_status(ssid, sizeof(ssid), &rssi);
        yk_sheet_wifi_text(state, ssid, rssi, yk_cloud_reach ? yk_cloud_reach() : -1, en, &problem, wifi, sizeof(wifi));
        v.row[k++] = (yk_sheet_row_t){.name = "Wi-Fi", .value = wifi, .problem = problem};
    }
    board_battery_t b = {.percent = -1};
    const esp_err_t err = board_battery(&b);
    const bool known = err == ESP_OK;  // as Yay reads it (yay_run.c read_battery)
    const yk_sheet_battery_t batt = {
        .percent = known ? b.percent : -1,
        .usb = (known || err == ESP_ERR_INVALID_RESPONSE) && b.usb,
        .charging = known && b.charging,
        .full = b.state == BOARD_BATTERY_FULL,
        .stopped = b.state == BOARD_BATTERY_STOPPED,
        .low = known && b.low,
        .fault = b.fault != BOARD_BATTERY_FAULT_NONE,
    };
    yk_sheet_battery_text(&batt, false, en, battery, sizeof(battery));
    v.row[k++] = (yk_sheet_row_t){.name = en ? "Battery" : "电量", .value = battery};
    if (yk_account_status) {
        bool problem;
        const int account = yk_account_status(nickname, sizeof(nickname));
        yk_sheet_phone_text(account, nickname, en, &problem, phone, sizeof(phone));
        v.row[k++] = (yk_sheet_row_t){.name = en ? "Phone" : "手机", .value = phone, .problem = problem};
    }
    if (yk_app_ring_has_code()) v.button = en ? "Open on phone" : "在手机上打开";
    yk_sheet_fill(s_sheet, &v);
}

static void close_sheet(const char *by) {
    if (!s_sheet) return;
    yk_sheet_t *s = s_sheet;
    s_sheet = NULL;  // first: its delete event (on_gone) is ours then
    if (s_timer) lv_timer_delete(s_timer);
    s_timer = NULL;
    yk_sheet_delete(s);
    if (s_edge) lv_obj_set_hidden(s_edge, false);
    yk_app_ring_sheet_closed();
    printf("YAY_SHEET closed from=%s by=%s shown_ms=%lld\n", s_slug, by, (long long)(now_ms() - s_open_ms));
}

/** Its screen went (the app loaded another and deleted this one): it went with it. */
static void on_gone(lv_event_t *e) {
    (void)e;
    if (!s_sheet) return;
    s_sheet = NULL;
    if (s_timer) lv_timer_delete(s_timer);
    s_timer = NULL;
    if (s_edge) lv_obj_set_hidden(s_edge, false);
    yk_app_ring_sheet_closed();
    printf("YAY_SHEET closed from=%s by=gone shown_ms=%lld\n", s_slug, (long long)(now_ms() - s_open_ms));
}

static void on_refresh(lv_timer_t *t) {
    (void)t;
    if (!s_sheet) return;
    lv_obj_t *o = yk_sheet_obj(s_sheet);
    if (lv_obj_get_screen(o) != lv_screen_active()) {
        close_sheet("screen");  // the app loaded another screen (and kept this one)
        return;
    }
    lv_obj_t *parent = lv_obj_get_parent(o);
    if (lv_obj_get_index(o) != (int32_t)lv_obj_get_child_count(parent) - 1) lv_obj_move_to_index(o, -1);
    fill();
}

static void on_dim(lv_event_t *e) {
    (void)e;
    close_sheet("dim");
}

static void on_swipe(lv_event_t *e) {
    (void)e;
    close_sheet("swipe");
}

static void on_button(lv_event_t *e) {
    (void)e;
    close_sheet("button");
    yk_app_ring_show_code();  // its own task shows it (and takes the key)
}

/** Opens it, `via` for the log: false when something of the system's is up or there is no memory. */
static bool open_sheet(const char *via) {
    if (s_sheet) return true;
    if (!yk_app_ring_sheet_opening()) return false;
    lv_obj_t *scr = lv_screen_active();
    s_sheet = yk_sheet_create(scr, on_dim, NULL, on_button, NULL);
    if (!s_sheet) {
        yk_app_ring_sheet_closed();
        printf("YAY_SHEET no memory from=%s\n", s_slug);
        return false;
    }
    yk_sheet_take_swipes(s_sheet, on_swipe, NULL);
    lv_obj_t *o = yk_sheet_obj(s_sheet);
    lv_obj_add_event_cb(o, on_gone, LV_EVENT_DELETE, NULL);
    fill();
    lv_obj_set_hidden(o, false);
    if (s_edge) lv_obj_set_hidden(s_edge, true);  // over the sheet it would take a swipe up from its top
    s_timer = lv_timer_create(on_refresh, REFRESH_MS, NULL);
    s_open_ms = now_ms();
    yk_hint_hide();  // the arrow to the key would sit over it
    printf("YAY_SHEET open from=%s via=%s y0=%d dy=%d x0=%d edge=%d pull=%d internal=%u internal_min=%u\n", s_slug, via,
           s_pull.y0, s_pull.dy_max, s_pull.x0, s_edge_px, s_pull_px,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    return true;
}

static void on_edge(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = {0, 0};
    if (indev) lv_indev_get_point(indev, &p);
    if (code == LV_EVENT_PRESSED) {
        yk_pull_start(&s_pull, p.x, p.y, s_edge_px);
    } else if (code == LV_EVENT_PRESSING) {
        if (yk_pull_move(&s_pull, p.x, p.y, s_pull_px) && open_sheet("pull") && indev)
            lv_indev_wait_release(indev);  // the rest of this press goes nowhere
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && s_pull.armed && !s_pull.pulled) {
        // How far a press on the edge went without opening it: what the edge's numbers are measured by (8.4).
        printf("YAY_SHEET pull none from=%s y0=%d dy=%d x0=%d edge=%d pull=%d\n", s_slug, s_pull.y0, s_pull.dy_max,
               s_pull.x0, s_edge_px, s_pull_px);
        s_pull.armed = false;
    }
}

void yk_app_sheet_attach(const char *slug) {
    if (s_edge) return;
    s_slug = slug ? slug : "";
    lv_obj_t *o = s_edge = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);  // nothing drawn
    lv_obj_set_pos(o, 0, 0);
    lv_obj_set_size(o, 466, s_edge_px);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, true);
    lv_obj_set_press_lock(o, true);  // a finger that leaves the strip going down is still its
    lv_obj_set_gesture_bubble(o, false);
    lv_obj_set_scroll_chain(o, false);
    lv_obj_add_event_cb(o, on_edge, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(o, on_edge, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(o, on_edge, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(o, on_edge, LV_EVENT_PRESS_LOST, NULL);
    printf("YAY_SHEET edge from=%s edge=%d pull=%d\n", s_slug, s_edge_px, s_pull_px);
}

void yk_app_sheet_close(const char *by) { close_sheet(by); }

bool yk_app_sheet_is_open(void) { return s_sheet != NULL; }

int yk_app_sheet_dev(int argc, char **argv) {
    // argv[0] "sheet": `yay sheet` the state; open | close; edge <px> | pull <px> (measuring the way in)
    int r = 0;
    if (argc > 2 && !strcmp(argv[1], "edge")) {
        s_edge_px = atoi(argv[2]) > 0 ? atoi(argv[2]) : YK_PULL_EDGE_PX;
        if (s_edge) lv_obj_set_height(s_edge, s_edge_px);
    } else if (argc > 2 && !strcmp(argv[1], "pull")) {
        s_pull_px = atoi(argv[2]) > 0 ? atoi(argv[2]) : YK_PULL_PX;
    } else if (argc > 1 && !strcmp(argv[1], "open")) {
        s_pull = (yk_pull_t){0};
        r = open_sheet("dev") ? 0 : 1;
    } else if (argc > 1 && !strcmp(argv[1], "close")) {
        close_sheet("dev");
    }
    printf("YAY_SHEET state from=%s attached=%d open=%d edge=%d pull=%d code=%d wifi=%d account=%d cloud=%d\n", s_slug,
           s_edge != NULL, s_sheet != NULL, s_edge_px, s_pull_px, yk_app_ring_has_code(), yk_wifi_status != NULL,
           yk_account_status != NULL, yk_cloud_reach != NULL);
    return r;
}
