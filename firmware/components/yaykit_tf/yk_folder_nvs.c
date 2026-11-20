// yk_folder_nvs.h: this device's records of the answers, one per app (Yay 0.1.19 开发文档 3.1 step 5).
#include "yk_folder_nvs.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "nvs.h"

// Not "yk_folder": an app's own namespace is "yk_" and its slug without hyphens (submissions.ts ownNamespace), so a slug
// `folder` would own that one and could write every app's answer. A slug has only a-z, 0-9 and '-': none of them gives
// an underscore after "yk_" (test/run.sh holds it to that rule).
#define NVS_NS "yk_sys_folder"

yk_grant_t yk_folder_record_load(const char *slug, char key[YK_GRANT_KEY]) {
    yk_grant_t g = {0};
    key[0] = 0;
    nvs_handle_t h;
    const bool open = nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK;
    for (int slot = 0; slot < 4; slot++) {
        char k[YK_GRANT_KEY];
        yk_grant_key(slug, slot, k);
        uint8_t rec[YK_GRANT_RECORD];
        size_t n = sizeof(rec);
        const esp_err_t err = open ? nvs_get_blob(h, k, rec, &n) : ESP_ERR_NVS_NOT_FOUND;
        if (err == ESP_OK && yk_grant_decode(rec, n, slug, &g)) {
            strcpy(key, k);
            break;
        }
        if (err == ESP_ERR_NVS_NOT_FOUND && !key[0]) strcpy(key, k);  // the first free one, should none be ours
    }
    if (open) nvs_close(h);
    if (!key[0]) yk_grant_key(slug, 0, key);  // four slugs on one hash: the first slot, someone's record goes
    return g;
}

void yk_folder_record_save(const char *slug, const char *key, const yk_grant_t *g) {
    uint8_t rec[YK_GRANT_RECORD];
    yk_grant_encode(slug, g, rec);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, key, rec, sizeof(rec));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) printf("YK_FOLDER record not saved: %s\n", esp_err_to_name(err));
}

bool yk_folder_record_same(const yk_grant_t *a, const yk_grant_t *b) {
    return a->state == b->state && a->folders == b->folders && a->known == b->known && a->pending == b->pending &&
           a->full == b->full && a->rev == b->rev;
}

bool yk_folder_record_known(const char *slug) {
    char key[YK_GRANT_KEY];
    return yk_folder_record_load(slug, key).state != YK_GRANT_NONE;
}

int yk_folder_record_each(yk_folder_record_fn fn, void *ctx) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return 0;  // none written yet
    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find(NVS_DEFAULT_PART_NAME, NVS_NS, NVS_TYPE_BLOB, &it);
    int n = 0;
    while (err == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        uint8_t rec[YK_GRANT_RECORD];
        size_t len = sizeof(rec);
        char slug[YK_GRANT_SLUG];
        yk_grant_t g;
        if (nvs_get_blob(h, info.key, rec, &len) == ESP_OK && yk_grant_record_slug(rec, len, slug) &&
            yk_grant_decode(rec, len, slug, &g)) {
            fn(slug, info.key, &g, ctx);
            n++;
        }
        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    nvs_close(h);
    return n;
}

void yk_folder_records_forget(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return;  // none was ever written
    if (err == ESP_OK) {
        err = nvs_erase_all(h);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    printf("YK_FOLDER records forgotten err=%s\n", esp_err_to_name(err));
}
