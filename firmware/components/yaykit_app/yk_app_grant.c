// The folders' question in an extension (yk_app_grant.h): yaykit_tf's yk_folder_ask() calls yk_app_folder_question()
// from its own task, which waits here while the page is up. The page is built and taken down in the LVGL task (labels
// measure their words with TinyTTF, which wants that task's stack), on LVGL's top layer over the app, the pull-down's
// edge and all; the key is the system's while it is up (yk_app_ring_grant_opening: a press is 「not now」). Everything
// the page holds is in PSRAM, and only while it or its after-image is up.
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "cjk_font.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "yaykit_app.h"
#include "yaykit_ui.h"
#include "yk_app_grant.h"
#include "yk_app_ring.h"
#include "yk_catalog.h"

static const char *TAG = "yk_grant";

enum { NOT_NOW = 0, YES = 1, NO = 2 };

typedef struct {
    lv_obj_t *root;     // the page
    lv_obj_t *after;    // the sage ring after yes, or the line after no
    lv_timer_t *timer;  // 30 s untouched; then the after-image's time
    char name[48];      // the catalog's, or the built-in one
    char glyph[8];      // its first character, on the disc
    uint32_t color;     // the disc's
    unsigned folders;
    int result;
} page_t;

static page_t *s_page;           // PSRAM; made at the first question, kept (a small struct)
static SemaphoreHandle_t s_done;  // given when the page has an answer

static const char *const WHY[] = {"not_now", "yes", "no"};

// ---------------------------------------------------------------- in the LVGL task

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w, int px, uint32_t color, bool wrap) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_label_set_long_mode(l, wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_line_space(l, (int)(px * 0.45) - px / 5, 0);  // the draft's 1.45 line height
    const lv_font_t *font = cjk_font_get_lang(px, yk_grant_font_tag());
    if (font) lv_obj_set_style_text_font(l, font, 0);
    return l;
}

static void after_gone(lv_timer_t *t) {
    (void)t;
    if (s_page->after) lv_obj_delete(s_page->after);
    s_page->after = NULL;
    s_page->timer = NULL;
}

static void fade_opa(void *obj, int32_t v) { lv_obj_set_style_opa(obj, (lv_opa_t)v, 0); }

/** The page goes with its answer; yes leaves the rim's sage ring a moment, no one line for 3 s. */
static void finish(int result, const char *by) {
    page_t *p = s_page;
    if (!p || !p->root) return;
    if (p->timer) lv_timer_delete(p->timer);
    p->timer = NULL;
    lv_obj_delete(p->root);
    p->root = NULL;
    if (p->after) lv_obj_delete(p->after);
    p->after = NULL;
    lv_obj_t *top = lv_layer_top();
    if (result == YES) {
        // The round-screen spec's 「done」: the sage ring all round (round-screen-design.md), fading out.
        lv_obj_t *ring = lv_obj_create(top);
        lv_obj_remove_style_all(ring);
        lv_obj_set_size(ring, 452, 452);
        lv_obj_set_pos(ring, YK_CX - 226, YK_CY - 226);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(ring, 6, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(YK_SAGE), 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_clickable(ring, false);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, ring);
        lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_duration(&a, YK_GRANT_RING_MS);
        lv_anim_set_exec_cb(&a, fade_opa);
        lv_anim_start(&a);
        p->after = ring;
        p->timer = lv_timer_create(after_gone, YK_GRANT_RING_MS + 50, NULL);
    } else if (result == NO) {
        p->after = label(top, yk_grant_later(), 83, YK_GRANT_LATER_Y, 300, YK_GRANT_LATER_PX, YK_INK3, false);
        lv_obj_set_clickable(p->after, false);
        p->timer = lv_timer_create(after_gone, YK_GRANT_LATER_MS, NULL);
    }
    if (p->timer) lv_timer_set_repeat_count(p->timer, 1);
    p->result = result;
    printf("YAY_APP grant answer=%s by=%s\n", WHY[result], by);
    yk_app_ring_grant_closed();  // the key back to the app
    xSemaphoreGive(s_done);
}

static void on_allow(lv_event_t *e) { (void)e, finish(YES, "tap"); }
static void on_deny(lv_event_t *e) { (void)e, finish(NO, "tap"); }
static void on_idle(lv_timer_t *t) {
    (void)t;
    s_page->timer = NULL;  // one shot: LVGL deletes it
    finish(NOT_NOW, "timeout");
}

void yk_app_grant_let_go_async(void *arg) {
    (void)arg;
    finish(NOT_NOW, "key");
}

static void open_async(void *arg) {
    page_t *p = s_page;
    const uint32_t wait_ms = (uint32_t)(uintptr_t)arg;
    if (p->after) lv_obj_delete(p->after);  // what the last answer left, still up
    p->after = NULL;
    if (p->timer) lv_timer_delete(p->timer);
    lv_obj_t *root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, YK_CX * 2, YK_CY * 2);
    lv_obj_set_style_bg_color(root, lv_color_hex(YK_PAPER), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_gesture_bubble(root, false);
    lv_obj_set_clickable(root, true);  // a tap off the buttons does nothing, and reaches nothing under it
    lv_obj_t *disc = lv_obj_create(root);
    lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, YK_GRANT_DISC, YK_GRANT_DISC);
    lv_obj_set_pos(disc, YK_CX - YK_GRANT_DISC / 2, YK_GRANT_DISC_CY - YK_GRANT_DISC / 2);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(p->color), 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_clickable(disc, false);
    lv_obj_t *g = label(disc, p->glyph, 0, 0, YK_GRANT_DISC, YK_GRANT_NAME_PX, YK_INK, false);
    lv_obj_align(g, LV_ALIGN_CENTER, 0, 0);
    label(root, p->name, 33, YK_GRANT_NAME_Y, 400, YK_GRANT_NAME_PX, YK_INK, false);
    label(root, yk_grant_question(p->folders), YK_GRANT_ASK_X, YK_GRANT_ASK_Y, YK_GRANT_ASK_W,
          YK_GRANT_ASK_PX, YK_INK2, true);
    // yk_ui_button(): a tap is down and up on it (yaykit_ui.h); coral with dark words, the panel with ink.
    yk_ui_button(root, yk_grant_allow(), YK_GRANT_ALLOW_X, YK_GRANT_BUTTON_Y, YK_GRANT_BUTTON_W,
                 YK_GRANT_BUTTON_H, on_allow, NULL, YK_CORAL);
    yk_ui_button(root, yk_grant_deny(), YK_GRANT_DENY_X, YK_GRANT_BUTTON_Y, YK_GRANT_BUTTON_W,
                 YK_GRANT_BUTTON_H, on_deny, NULL, YK_PANEL);
    p->root = root;
    p->timer = lv_timer_create(on_idle, wait_ms, NULL);
    lv_timer_set_repeat_count(p->timer, 1);
    printf("YAY_APP grant shown kinds=%u lang=%s app=%s\n", p->folders, yk_grant_font_tag(), yk_app_ring_slug());
}

// ---------------------------------------------------------------- yaykit_tf's task

/** The app's name and disc: the catalog's (Yay writes it in the account's language), else the built-in name. */
static void who(page_t *p) {
    const char *slug = yk_app_ring_slug();
    yk_catalog_t *cat = heap_caps_calloc(1, sizeof(*cat), MALLOC_CAP_SPIRAM);
    const yk_catalog_entry_t *e = cat && yk_catalog_read(cat) == ESP_OK ? yk_catalog_find(cat, slug) : NULL;
    snprintf(p->name, sizeof(p->name), "%s", e && e->name[0] ? e->name : yk_app_display_name(slug));
    p->color = e && e->icon_format == YK_ICON_A8 && e->icon_color ? e->icon_color : YK_PANEL;
    heap_caps_free(cat);
    // The first character, whole: UTF-8's lead byte says how long it is.
    const unsigned char c = (unsigned char)p->name[0];
    const size_t n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
    memcpy(p->glyph, p->name, n <= strlen(p->name) ? n : 0);
    p->glyph[n <= strlen(p->name) ? n : 0] = 0;
}

int yk_app_folder_question(uint8_t folders, uint32_t wait_ms) {
    if (!folders || folders > 15 || yk_app_leaving()) return NOT_NOW;
    if (!s_page) s_page = heap_caps_calloc(1, sizeof(*s_page), MALLOC_CAP_SPIRAM);
    if (!s_done) s_done = xSemaphoreCreateBinary();
    if (!s_page || !s_done) return NOT_NOW;
    who(s_page);
    s_page->folders = folders;
    s_page->result = NOT_NOW;
    xSemaphoreTake(s_done, 0);  // nothing left over
    if (!yk_app_ring_grant_opening()) {
        printf("YAY_APP grant not_now by=busy\n");  // the ring, the pull-down or the code is up: no question over them
        return NOT_NOW;
    }
    if (board_lock(1000) != ESP_OK) {
        ESP_LOGW(TAG, "no board lock");
        yk_app_ring_grant_closed();
        return NOT_NOW;
    }
    lv_async_call(open_async, (void *)(uintptr_t)wait_ms);
    board_unlock();
    // The page's own timer answers after wait_ms; this wait is only against a page that never came up.
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(wait_ms + 5000)) != pdTRUE) {
        ESP_LOGW(TAG, "no answer from the page");
        if (board_lock(1000) == ESP_OK) {
            lv_async_call(yk_app_grant_let_go_async, NULL);
            board_unlock();
        }
        xSemaphoreTake(s_done, pdMS_TO_TICKS(1000));
        yk_app_ring_grant_closed();
        return NOT_NOW;
    }
    return s_page->result;
}
