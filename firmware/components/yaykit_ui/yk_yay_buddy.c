// Which buddy a Yay is (yk_yay_buddy in yk_yay.c; development kit 1.10, YK_API_LEVEL 5; 开发包四个伙伴-需求确认.md §4, §8).
// The current buddy is the device's shared setting, read once a run: one chosen while an app is open shows the next
// time it opens. Each buddy's pack is read once a run, from the slots the main firmware keeps (the current buddy and the
// one before; the card too where a firmware registered a reader, yk_buddy_card_set, which no extension does), and kept
// for the run, as every Yay drawing it points into it. Nothing is read for Pip, nor for a firmware that draws no buddy.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "yk_buddy.h"

static const char *BUDDY_TAG = "yk_yay";

// Internal RAM is the scarce kind (40,000 bytes at the least): what is kept here is a few words, the packs and their
// records in PSRAM. By YK_PREF_BUDDY's value; 1 and 2 are unused (they read as Pip).
static int s_buddy_current = YK_BUDDY_CURRENT;              // not read yet
static uint8_t s_buddy_tried;                               // a bit for each buddy whose pack was looked for
static yk_buddy_pack_t *s_buddy_kept[YK_BUDDY_BLOOM + 1];  // the pack read whole, in PSRAM; NULL none

int yk_yay_buddy_resolve(int buddy) {
    if (buddy == YK_BUDDY_PIP || buddy == YK_BUDDY_MOTE || buddy == YK_BUDDY_DIAL || buddy == YK_BUDDY_BLOOM) return buddy;
    // YK_BUDDY_CURRENT, or one this firmware cannot name: the user's. Wow, Aha and what it does not know read as Pip.
    if (s_buddy_current == YK_BUDDY_CURRENT) s_buddy_current = yk_buddy_from_id(yk_buddy_id(board_pref_buddy()));
    return s_buddy_current;
}

/** The pack, the slots first. yk_buddy_load says no on LVGL's own task (a Yay made in an event callback), for the
 *  card's sake, whose second would stall the screen: there the slots alone, a flash read. */
static bool buddy_load(int buddy, yk_buddy_pack_t *out) {
    if (yk_buddy_load(buddy, out)) return true;
    if (!out->why || strcmp(out->why, "LVGL_TASK")) return false;
    yk_buddy_slots_t slots;
    const int slot = yk_buddy_slots_read(&slots) ? yk_buddy_slot_newest(&slots, yk_buddy_id(buddy)) : -1;
    if (slot >= 0) return yk_buddy_load_slot(&slots, slot, out);
    out->why = "NO_SLOT";
    return false;
}

const yk_figure_t *yk_yay_buddy_pack(int buddy) {
    if (buddy <= YK_BUDDY_PIP || buddy > YK_BUDDY_BLOOM) return NULL;
    if (!(s_buddy_tried & (1u << buddy))) {
        s_buddy_tried |= 1u << buddy;  // once a run, whatever came of it: a pack not there now is not there in a second
        const int64_t t0 = esp_timer_get_time();
        yk_buddy_pack_t *p = heap_caps_calloc(1, sizeof(*p), MALLOC_CAP_SPIRAM);
        const bool ok = p && buddy_load(buddy, p);
        const int ms = (int)((esp_timer_get_time() - t0) / 1000);
        if (ok)
            ESP_LOGI(BUDDY_TAG, "YK_BUDDY %s from the %s, %u bytes, %d ms", yk_buddy_id(buddy),
                     p->from == YK_BUDDY_FROM_CARD ? "card" : "slot", (unsigned)p->size, ms);
        else
            ESP_LOGI(BUDDY_TAG, "YK_BUDDY %s: none (%s), Pip instead", yk_buddy_id(buddy), p && p->why ? p->why : "NO_MEM");
        (void)ms;  // the host tests' log says nothing
        if (ok) {
            s_buddy_kept[buddy] = p;
        } else {
            heap_caps_free(p);
        }
    }
    return s_buddy_kept[buddy] ? &s_buddy_kept[buddy]->figure : NULL;
}

const yk_figure_t *yk_yay_buddy_kept(int buddy) {
    return buddy > YK_BUDDY_PIP && buddy <= YK_BUDDY_BLOOM && s_buddy_kept[buddy] ? &s_buddy_kept[buddy]->figure : NULL;
}
