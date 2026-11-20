#include "yk_buddy.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

#define LVGL_TASK "lvgl"  // esp_lvgl_adapter's worker (yaykit_board starts it)

_Static_assert(YK_BUDDY_ID == YK_FIGURE_NAME && YK_BUDDY_PACK_MAX == YK_FIGURE_MAX_BYTES, "a slot holds a figure pack");

static yk_buddy_card_fn s_card;

void yk_buddy_card_set(yk_buddy_card_fn read) { s_card = read; }

static const esp_partition_t *partition(void) {
    const esp_partition_t *p =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, YK_BUDDY_PARTITION);
    return p && p->size >= YK_BUDDY_AREA_AT + YK_BUDDY_SLOTS * YK_BUDDY_SLOT_BYTES ? p : NULL;
}

bool yk_buddy_slots_read(yk_buddy_slots_t *out) {
    memset(out, 0, sizeof(*out));
    const esp_partition_t *p = partition();
    if (!p) return false;
    uint8_t head[YK_BUDDY_HEAD];
    for (int i = 0; i < YK_BUDDY_SLOTS; i++)
        out->ok[i] = esp_partition_read(p, yk_buddy_slot_at(i), head, sizeof(head)) == ESP_OK &&
                     yk_buddy_head_read(head, sizeof(head), &out->head[i]);
    return true;
}

static void none(yk_buddy_pack_t *out, const char *why) {
    memset(out, 0, sizeof(*out));
    out->slot = -1;
    out->why = why;
}

/** `bytes` as `id`'s pack: its table reads and it is that buddy's. */
static bool usable(yk_buddy_pack_t *out, const char *id) {
    const char *why = NULL;
    if (!yk_figure_read(out->bytes, out->size, &out->figure, &why)) {
        out->why = why ? why : "BAD_PACK";
        return false;
    }
    if (strcmp(out->figure.figure, id)) {
        out->why = "OTHER_BUDDY";
        return false;
    }
    return true;
}

bool yk_buddy_load_slot(const yk_buddy_slots_t *slots, int slot, yk_buddy_pack_t *out) {
    none(out, "NO_SLOT");
    const esp_partition_t *p = partition();
    if (!p || slot < 0 || slot >= YK_BUDDY_SLOTS || !slots->ok[slot]) return false;
    const yk_buddy_head_t *h = &slots->head[slot];
    out->bytes = heap_caps_malloc(h->size, MALLOC_CAP_SPIRAM);
    if (!out->bytes) {
        out->why = "NO_MEM";
        return false;
    }
    out->size = h->size;
    // A sector at a time: a flash read into PSRAM goes through a bounce buffer in internal RAM as big as the read.
    esp_err_t err = ESP_OK;
    const uint32_t at = yk_buddy_slot_at(slot) + YK_BUDDY_PACK_AT;
    for (uint32_t i = 0; err == ESP_OK && i < h->size; i += YK_BUDDY_SECTOR)
        err = esp_partition_read(p, at + i, out->bytes + i, h->size - i < YK_BUDDY_SECTOR ? h->size - i : YK_BUDDY_SECTOR);
    if (err != ESP_OK) {
        out->why = "READ";
    } else if (mbedtls_sha256(out->bytes, h->size, out->sha, 0) || memcmp(out->sha, h->sha, 32)) {
        out->why = "SHA256";
    } else if (usable(out, h->id)) {
        out->from = YK_BUDDY_FROM_SLOT;
        out->slot = slot;
        out->why = NULL;
        return true;
    }
    const char *why = out->why;
    yk_buddy_pack_free(out);
    out->why = why;
    return false;
}

bool yk_buddy_load(int buddy, yk_buddy_pack_t *out) {
    none(out, "PIP");
    if (yk_buddy_from_id(yk_buddy_id(buddy)) == YK_BUDDY_PIP) return false;  // Pip, and what reads as him
    const char *name = pcTaskGetName(NULL);
    if (name && !strcmp(name, LVGL_TASK)) {
        out->why = "LVGL_TASK";
        return false;
    }
    const char *id = yk_buddy_id(buddy);
    yk_buddy_slots_t slots;
    const char *why = "NO_SLOT";
    if (yk_buddy_slots_read(&slots)) {
        // The newest first; an older one of the same buddy (a power cut left both) only when the newest does not read.
        const int newest = yk_buddy_slot_newest(&slots, id);
        for (int pass = 0; pass < YK_BUDDY_SLOTS && newest >= 0; pass++) {
            const int slot = pass ? !newest : newest;
            if (pass && (!slots.ok[slot] || strcmp(slots.head[slot].id, id))) break;
            if (yk_buddy_load_slot(&slots, slot, out)) return true;
            why = out->why;
        }
    }
    if (!s_card) {
        none(out, why);
        return false;
    }
    none(out, why);
    uint8_t *bytes = NULL;
    uint32_t size = 0;
    if (!s_card(id, &bytes, &size, out->sha) || !bytes) {
        heap_caps_free(bytes);
        none(out, "NOT_ON_CARD");  // or no card at all, Bluetooth on: the reader cannot tell them apart
        return false;
    }
    out->bytes = bytes;
    out->size = size;
    if (usable(out, id)) {
        out->from = YK_BUDDY_FROM_CARD;
        out->why = NULL;
        return true;
    }
    why = out->why;
    yk_buddy_pack_free(out);
    out->why = why;
    return false;
}

void yk_buddy_pack_free(yk_buddy_pack_t *p) {
    if (!p) return;
    heap_caps_free(p->bytes);
    none(p, NULL);
}
