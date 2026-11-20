// Host tests only (test_yay.c): LVGL's image cache, as yk_yay.c includes it (9.6.0 keeps it out of its public headers).
// Nothing is cached here; the test counts what yk_yay.c tells it to forget.
#pragma once
static int s_lv_cache_drops;         // lv_image_cache_drop calls
static const void *s_lv_cache_drop;  // the last one's source
static inline void lv_image_cache_drop(const void *src) {
    s_lv_cache_drops++;
    s_lv_cache_drop = src;
}
