// Host tests only (test_yay.c): what yk_yay.c calls of LVGL, doing nothing but keep what an image shows and whether it
// is hidden, so the test can see what each Yay draws from.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct _lv_layer_t lv_layer_t;
typedef struct _lv_event_t lv_event_t;
typedef void (*lv_event_cb_t)(lv_event_t *e);
typedef uint8_t lv_opa_t;
typedef struct {
    uint8_t blue, green, red;
} lv_color_t;

#define LV_OPA_TRANSP 0
#define LV_OPA_COVER 255
#define LV_IMAGE_HEADER_MAGIC 0x19
#define LV_COLOR_FORMAT_RGB565 0x12
#define LV_COLOR_FORMAT_RGB565A8 0x14
#define LV_ANIM_REPEAT_INFINITE 0xFFFFFFFFu

typedef struct {
    uint32_t magic, cf, flags, w, h, stride;
} lv_image_header_t;
typedef struct _lv_image_dsc_t {
    lv_image_header_t header;
    uint32_t data_size;
    const uint8_t *data;
} lv_image_dsc_t;

typedef struct _lv_obj_t {
    bool hidden;
    const void *src;  // an image's
} lv_obj_t;

static inline lv_color_t lv_color_hex(uint32_t c) {
    return (lv_color_t){.blue = (uint8_t)c, .green = (uint8_t)(c >> 8), .red = (uint8_t)(c >> 16)};
}

static inline uint32_t lv_tick_get(void) {
    static uint32_t t;  // every call a tick later: the cache's oldest is plain
    return ++t;
}

static inline lv_obj_t *lv_obj_create(lv_obj_t *parent) {
    (void)parent;
    return calloc(1, sizeof(lv_obj_t));
}
static inline lv_obj_t *lv_image_create(lv_obj_t *parent) { return lv_obj_create(parent); }
static inline void lv_obj_delete(lv_obj_t *o) { free(o); }  // its children stay: a test's leak, not a dangling one
static inline void lv_obj_remove_style_all(lv_obj_t *o) { (void)o; }
static inline void lv_obj_set_clickable(lv_obj_t *o, bool on) { (void)o, (void)on; }
static inline void lv_obj_set_scrollable(lv_obj_t *o, bool on) { (void)o, (void)on; }
static inline void lv_obj_set_hidden(lv_obj_t *o, bool on) { o->hidden = on; }
static inline bool lv_obj_is_hidden(const lv_obj_t *o) { return o->hidden; }
static inline void lv_obj_set_pos(lv_obj_t *o, int32_t x, int32_t y) { (void)o, (void)x, (void)y; }
static inline void lv_obj_set_size(lv_obj_t *o, int32_t w, int32_t h) { (void)o, (void)w, (void)h; }
static inline void lv_obj_move_to_index(lv_obj_t *o, int32_t i) { (void)o, (void)i; }
static inline void lv_obj_set_style_bg_color(lv_obj_t *o, lv_color_t c, uint32_t s) { (void)o, (void)c, (void)s; }
static inline void lv_obj_set_style_bg_opa(lv_obj_t *o, lv_opa_t v, uint32_t s) { (void)o, (void)v, (void)s; }
static inline void lv_obj_set_style_opa(lv_obj_t *o, lv_opa_t v, uint32_t s) { (void)o, (void)v, (void)s; }
static inline void lv_obj_set_style_radius(lv_obj_t *o, int32_t v, uint32_t s) { (void)o, (void)v, (void)s; }
static inline void lv_obj_set_style_translate_y(lv_obj_t *o, int32_t v, uint32_t s) { (void)o, (void)v, (void)s; }
static inline void lv_image_set_src(lv_obj_t *o, const void *src) { o->src = src; }
static inline const void *lv_image_get_src(lv_obj_t *o) { return o->src; }
static inline void lv_image_set_pivot(lv_obj_t *o, int32_t x, int32_t y) { (void)o, (void)x, (void)y; }
static inline void lv_image_set_scale(lv_obj_t *o, uint32_t v) { (void)o, (void)v; }
static inline void lv_image_set_scale_x(lv_obj_t *o, uint32_t v) { (void)o, (void)v; }
static inline void lv_image_set_scale_y(lv_obj_t *o, uint32_t v) { (void)o, (void)v; }
static inline void lv_image_set_antialias(lv_obj_t *o, bool v) { (void)o, (void)v; }
static inline void lv_image_set_rotation(lv_obj_t *o, int32_t v) { (void)o, (void)v; }

typedef struct _lv_timer_t {
    void *user_data;
} lv_timer_t;
static inline lv_timer_t *lv_timer_create(void (*cb)(lv_timer_t *), uint32_t period, void *user_data) {
    (void)cb, (void)period;
    lv_timer_t *t = calloc(1, sizeof(*t));
    if (t) t->user_data = user_data;
    return t;
}
static inline void lv_timer_delete(lv_timer_t *t) { free(t); }
static inline void *lv_timer_get_user_data(lv_timer_t *t) { return t->user_data; }

typedef struct _lv_anim_t lv_anim_t;
typedef void (*lv_anim_exec_xcb_t)(void *var, int32_t v);
typedef int32_t (*lv_anim_path_cb_t)(const lv_anim_t *a);
struct _lv_anim_t {
    void *var, *user_data;
};
static inline int32_t lv_anim_path_ease_in_out(const lv_anim_t *a) {
    (void)a;
    return 0;
}
static inline void lv_anim_init(lv_anim_t *a) { a->var = a->user_data = NULL; }
static inline void lv_anim_set_var(lv_anim_t *a, void *var) { a->var = var; }
static inline void lv_anim_set_values(lv_anim_t *a, int32_t from, int32_t to) { (void)a, (void)from, (void)to; }
static inline void lv_anim_set_duration(lv_anim_t *a, uint32_t ms) { (void)a, (void)ms; }
static inline void lv_anim_set_reverse_duration(lv_anim_t *a, uint32_t ms) { (void)a, (void)ms; }
static inline void lv_anim_set_repeat_count(lv_anim_t *a, uint32_t n) { (void)a, (void)n; }
static inline void lv_anim_set_exec_cb(lv_anim_t *a, lv_anim_exec_xcb_t cb) { (void)a, (void)cb; }
static inline void lv_anim_set_path_cb(lv_anim_t *a, lv_anim_path_cb_t cb) { (void)a, (void)cb; }
static inline void lv_anim_set_user_data(lv_anim_t *a, void *user_data) { a->user_data = user_data; }
static inline void *lv_anim_get_user_data(const lv_anim_t *a) { return a->user_data; }
static inline void lv_anim_set_completed_cb(lv_anim_t *a, void (*cb)(lv_anim_t *)) { (void)a, (void)cb; }
static inline lv_anim_t *lv_anim_start(const lv_anim_t *a) {
    (void)a;
    return NULL;
}
static inline bool lv_anim_delete(void *var, lv_anim_exec_xcb_t cb) {
    (void)var, (void)cb;
    return false;
}
