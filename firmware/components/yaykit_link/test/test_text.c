// Host tests for yk info's text a part at a time (yk_link_text.c): the same text as the whole tree printed at once,
// PSRAM's failure falling back to internal RAM, both failing, a text past the first buffer, nothing leaked.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../yk_link_text.h"

static int failures;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); \
            failures++;                                                  \
        }                                                                \
    } while (0)

static int psram_ok = 1, internal_ok = 1, from_psram, from_internal, live;
static void *psram(size_t n) {
    if (!psram_ok) return NULL;
    from_psram++, live++;
    return malloc(n);
}
static void *internal(size_t n) {
    if (!internal_ok) return NULL;
    from_internal++, live++;
    return malloc(n);
}
static void release(void *p) {
    live--;
    free(p);
}

static cJSON *slot(int i, const char *slug) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "index", i);
    cJSON_AddStringToObject(o, "state", "valid");
    cJSON_AddStringToObject(o, "slug", slug);
    cJSON_AddStringToObject(o, "version", "0.1.13");
    cJSON_AddBoolToObject(o, "running", i == 0);
    cJSON_AddBoolToObject(o, "boot", i == 0);
    return o;
}

/** yk info's shape, with `note` as the device's board (escaping, length). */
static cJSON *part(const char *name, const char *note) {
    cJSON *o = cJSON_CreateObject();
    if (!strcmp(name, "device")) {
        cJSON_AddStringToObject(o, "id", "yk_0123456789ab");
        cJSON_AddStringToObject(o, "board", note);
        cJSON_AddNumberToObject(o, "flash", 33554432);
    } else if (!strcmp(name, "heap")) {
        cJSON_AddNumberToObject(o, "internalFree", 60147);
        cJSON_AddNumberToObject(o, "internalMinSinceReady", 40895);
        cJSON_AddItemToObject(o, "stackMinFree", cJSON_CreateObject());
        cJSON_AddNumberToObject(cJSON_GetObjectItem(o, "stackMinFree"), "yay_run", 2836);
    } else {
        cJSON_AddStringToObject(o, "state", "off");
    }
    return o;
}

static cJSON *whole(const char *note, int slots) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "proto", 1);
    cJSON_AddItemToObject(root, "device", part("device", note));
    cJSON *arr = cJSON_AddArrayToObject(root, "slots");
    for (int i = 0; i < slots; i++) cJSON_AddItemToArray(arr, slot(i, i ? "translator" : "yay"));
    cJSON_AddItemToObject(root, "heap", part("heap", note));
    cJSON_AddItemToObject(root, "ble", part("ble", note));
    cJSON *caps = cJSON_AddArrayToObject(root, "capabilities");
    cJSON_AddItemToArray(caps, cJSON_CreateString("signed_install"));
    cJSON_AddNumberToObject(root, "uptime", 241);
    cJSON_AddStringToObject(root, "resetReason", "usb");
    return root;
}

static char *in_parts(const char *note, int slots) {
    yk_text_t t;
    yk_text_init(&t, psram, internal, release);
    yk_text_raw(&t, "{");
    yk_text_add(&t, "proto", cJSON_CreateNumber(1));
    yk_text_add(&t, "device", part("device", note));
    yk_text_key(&t, "slots");
    yk_text_raw(&t, "[");
    for (int i = 0; i < slots; i++) yk_text_add(&t, NULL, slot(i, i ? "translator" : "yay"));
    yk_text_raw(&t, "]");
    yk_text_add(&t, "heap", part("heap", note));
    yk_text_add(&t, "ble", part("ble", note));
    cJSON *caps = cJSON_CreateArray();
    cJSON_AddItemToArray(caps, cJSON_CreateString("signed_install"));
    yk_text_add(&t, "capabilities", caps);
    yk_text_add(&t, "uptime", cJSON_CreateNumber(241));
    yk_text_add(&t, "resetReason", cJSON_CreateString("usb"));
    yk_text_raw(&t, "}");
    return yk_text_take(&t);
}

/** In parts == the whole tree printed at once, byte for byte; parses back. */
static void same_as_whole(const char *note, int slots) {
    cJSON *root = whole(note, slots);
    char *want = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    char *got = in_parts(note, slots);
    CHECK(got && want && !strcmp(got, want));
    if (got && want && strcmp(got, want)) fprintf(stderr, "  want %s\n  got  %s\n", want, got);
    cJSON *back = got ? cJSON_Parse(got) : NULL;
    CHECK(back != NULL);
    cJSON_Delete(back);
    cJSON_free(want);
    if (got) release(got);
}

int main(void) {
    // The same text as before, empty slots too; a board with quotes and a backslash escaped by cJSON as before.
    same_as_whole("tikpal-r1", 3);
    same_as_whole("tikpal-r1", 0);
    same_as_whole("say \"hi\" \\ \t", 1);
    CHECK(from_psram == 3 && from_internal == 0 && live == 0);

    // Past the first buffer (1,536 bytes): grown, still one text, the smaller buffer freed.
    char big[4000];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    from_psram = 0;
    same_as_whole(big, 3);
    CHECK(from_psram >= 2 && live == 0);

    // PSRAM refused: the text from internal RAM, yk info as ever.
    psram_ok = 0, from_psram = from_internal = 0;
    same_as_whole("tikpal-r1", 3);
    CHECK(from_psram == 0 && from_internal == 1 && live == 0);

    // Both refused: NULL, as cJSON_PrintUnformatted's NULL before; nothing left over.
    internal_ok = 0;
    char *none = in_parts("tikpal-r1", 3);
    CHECK(none == NULL && live == 0);

    // A part that could not be made (NULL): NULL, nothing left over.
    psram_ok = internal_ok = 1;
    yk_text_t t;
    yk_text_init(&t, psram, internal, release);
    yk_text_raw(&t, "{");
    yk_text_add(&t, "proto", cJSON_CreateNumber(1));
    yk_text_add(&t, "device", NULL);
    yk_text_raw(&t, "}");
    CHECK(yk_text_take(&t) == NULL && live == 0);

    if (failures) {
        fprintf(stderr, "test_text: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_text: ok\n");
    return 0;
}
