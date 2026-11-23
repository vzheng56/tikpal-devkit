// Host tests only (test_account.c, test_region.c): NVS in memory, in each test.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define NVS_DEFAULT_PART_NAME "nvs"
#define NVS_KEY_NAME_MAX_SIZE 16
#define NVS_NS_NAME_MAX_SIZE NVS_KEY_NAME_MAX_SIZE
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
typedef enum { NVS_TYPE_U8 = 0x01, NVS_TYPE_STR = 0x21, NVS_TYPE_BLOB = 0x42, NVS_TYPE_ANY = 0xff } nvs_type_t;
typedef struct {
    char namespace_name[NVS_NS_NAME_MAX_SIZE];
    char key[NVS_KEY_NAME_MAX_SIZE];
    nvs_type_t type;
} nvs_entry_info_t;
typedef struct nvs_opaque_iterator_t *nvs_iterator_t;
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value);
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value);
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value);
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out, size_t *length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length);
esp_err_t nvs_entry_find(const char *part_name, const char *namespace_name, nvs_type_t type, nvs_iterator_t *output_iterator);
esp_err_t nvs_entry_next(nvs_iterator_t *iterator);
esp_err_t nvs_entry_info(const nvs_iterator_t iterator, nvs_entry_info_t *out_info);
void nvs_release_iterator(nvs_iterator_t iterator);
