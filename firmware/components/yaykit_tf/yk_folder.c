// yk_folder.h on the device: the question's way (yk_folder_logic.c decides), this device's record of the answer in NVS,
// the device service, the card held while reading, and the reading itself (yk_folder_read.c, FatFs).
//
// The answer is the person's for the whole app, every kind it declares or none (开发文档 3.1 step 5). This device keeps
// one record per app in NVS namespace `yk_sys_folder` (no app's own can be called that), written by the system's component in whichever firmware asks
// (keys: yk_grant_key(), a hash of the slug). The device service has the same (Yaykit's device_folder_grants), and the
// App changes it there: the first ask of a run asks the service for this app's (2 s at most; without an answer the
// record here stands), and an answer made here waits in the record (`pending`) until the service has heard it. The
// calls to the service are yaykit_cloud's, handed over as it starts (yk_folder_net(), yk_folder_logic.h): an app that
// uses the cloud asks with its own token; one that does not goes by the record, which Yay keeps in line (its check at
// connecting, and before it opens an app; 固件总策划 11-13). The service full (409 GRANTS_FULL, 64 apps on this
// device): the answer here stands, marked full, told again at the next ask only.
//
// The question is the screen's (yaykit_app's yk_app_folder_question, linked weakly: a firmware that cannot draw it
// gets NOT_NOW). An ask runs on a task of its own, made for it and gone after: internal RAM for its stack (NVS writes
// stop the flash cache, and a stack in PSRAM goes with it), ASK_STACK bytes, only while an ask is under way.
#include "yk_folder.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "yaykit_tf.h"
#include "yk_folder_logic.h"
#include "yk_folder_nvs.h"
#include "yk_folder_read.h"
#include "yk_tf_card.h"
#include "yk_tf_logic.h"

// The kinds the project's app.json asks for (folders.cmake, at build time): bits of YK_FOLDER_ALL.
#ifndef YK_FOLDER_DECLARED
#define YK_FOLDER_DECLARED 0
#endif
_Static_assert((YK_FOLDER_DECLARED & ~YK_FOLDER_ALL) == 0, "YK_FOLDER_DECLARED: the four kinds' bits only");
_Static_assert(YK_FOLDER_COUNT == YK_FOLDER_KINDS, "yk_folder.h and yk_folder_logic.h count the same kinds");

#define NET_WAIT_MS 2000        // 开发文档 3.1: the service, at the first ask of a run
#define QUESTION_WAIT_MS 30000  // 4.6: untouched this long is NOT_NOW
#define ASK_STACK 6144          // the card's mount (about 3 KB, yaykit_tf.h), NVS, and the app's callback on top
#define ASK_PRIORITY 4

// The question (yaykit_app, yk_app_grant.c): blocks until it is answered; 1 yes, 2 no, 0 not now (the side key, 30 s,
// the app leaving, or no screen to ask on).
int yk_app_folder_question(uint8_t folders, uint32_t wait_ms) __attribute__((weak));
// The card as a computer's USB disk (the U 盘 work, 开发文档 4.2): until it says otherwise, never.
bool yk_tf_card_usb(void) __attribute__((weak));

typedef struct {
    yk_folder_t folder;
    yk_folder_cb_t cb;
    void *ctx;
} ask_t;

static SemaphoreHandle_t s_lock;     // the reading below, one at a time
static char s_slug[YK_TF_NAME_MAX + 1];
static volatile bool s_asking;       // one ask at a time
static volatile uint8_t s_allowed;   // the kinds this run may read: set by an ask that answered ALLOWED
static bool s_synced;                // the service was asked this run (only the ask task touches it)
static int s_held;                   // holds on the card for open files and a listing (under s_lock)

static const char *answer_word(yk_folder_answer_t a) {
    static const char *const W[] = {"allowed", "denied", "not_now", "not_declared", "no_card", "usb"};
    return (unsigned)a < sizeof(W) / sizeof(W[0]) ? W[a] : "?";
}

static bool ready(void) {
    if (__atomic_load_n(&s_lock, __ATOMIC_ACQUIRE)) return true;
    SemaphoreHandle_t m = xSemaphoreCreateMutex();
    if (!m) return false;
    SemaphoreHandle_t none = NULL;
    if (!__atomic_compare_exchange_n(&s_lock, &none, m, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) vSemaphoreDelete(m);
    if (!s_slug[0] && !yk_tf_slug_from_project(esp_app_get_description()->project_name, s_slug, sizeof(s_slug)))
        s_slug[0] = 0;
    return true;
}

/** Tells the service an answer made here, if it waits to be told and the service's calls are here; `*put` what came
 *  back (-1: nothing to tell, or no calls). */
static void report(yk_grant_t *g, int *put) {
    const yk_folder_net_t *net = yk_folder_net();
    if (!g->pending || g->state == YK_GRANT_NONE || !net || !net->put) return;
    yk_grant_t back = {0};
    *put = net->put(s_slug, g, &back, NET_WAIT_MS);
    const yk_put_t said = *put == 1 ? YK_PUT_OK : *put == 2 ? YK_PUT_CHANGED : *put == 3 ? YK_PUT_FULL : YK_PUT_SILENT;
    *g = yk_grant_reported(*g, said, &back);
}

// ---------------------------------------------------------------- the ask

static yk_folder_answer_t decide(yk_folder_t folder, int *question, int *net, int *put) {
    const uint8_t declared = YK_FOLDER_DECLARED;
    if (!(declared & 1u << folder) || !s_slug[0]) return YK_FOLDER_NOT_DECLARED;
    if (yk_tf_card_usb && yk_tf_card_usb()) return YK_FOLDER_USB;
    if (yk_tf_acquire() != ESP_OK) return YK_FOLDER_NO_CARD;  // none, not FAT, failing, resting, Bluetooth
    yk_tf_release();
    char key[YK_GRANT_KEY];
    const yk_grant_t stored = yk_folder_record_load(s_slug, key);
    yk_grant_t g = stored;
    const yk_folder_net_t *calls = yk_folder_net();
    if (!s_synced && calls && calls->get) {
        yk_grant_t service = {0};
        *net = calls->get(s_slug, &service, NET_WAIT_MS);
        s_synced = *net == 1;
        g = yk_grant_merge(g, *net == 1 ? YK_NET_ANSWERED : YK_NET_SILENT, &service);
    }
    yk_grant_shrink(&g, declared);
    report(&g, put);
    if (!yk_folder_record_same(&g, &stored)) yk_folder_record_save(s_slug, key, &g);
    switch (yk_grant_decide(&g, declared)) {
    case YK_ASK_ALLOWED: s_allowed = declared; return YK_FOLDER_ALLOWED;
    case YK_ASK_DENIED: s_allowed = 0; return YK_FOLDER_DENIED;
    case YK_ASK_QUESTION: break;
    }
    *question = yk_app_folder_question ? yk_app_folder_question(declared, QUESTION_WAIT_MS) : 0;
    if (*question != 1 && *question != 2) return YK_FOLDER_NOT_NOW;  // not a no: nothing is kept
    g = yk_grant_answered(&g, *question == 1, declared);
    yk_folder_record_save(s_slug, key, &g);  // first: the answer stands, network or not
    const yk_grant_t answered = g;
    report(&g, put);
    if (!yk_folder_record_same(&g, &answered)) yk_folder_record_save(s_slug, key, &g);
    const bool yes = g.state == YK_GRANT_GRANTED && yk_grant_decide(&g, declared) == YK_ASK_ALLOWED;
    s_allowed = yes ? declared : 0;  // a 409 says what the App made of it meanwhile
    return yes ? YK_FOLDER_ALLOWED : YK_FOLDER_DENIED;
}

static void ask_task(void *arg) {
    ask_t a = *(ask_t *)arg;
    heap_caps_free(arg);
    const int64_t t0 = esp_timer_get_time();
    int question = -1, net = -1, put = -1;
    const yk_folder_answer_t answer = decide(a.folder, &question, &net, &put);
    printf("YK_FOLDER ask kind=%s answer=%s question=%d net=%d put=%d ms=%d stack_left=%u\n",
           yk_folder_kind_word(a.folder), answer_word(answer), question, net, put,
           (int)((esp_timer_get_time() - t0) / 1000), (unsigned)uxTaskGetStackHighWaterMark(NULL));
    a.cb(answer, a.ctx);
    s_asking = false;
    vTaskDelete(NULL);
}

esp_err_t yk_folder_ask(yk_folder_t folder, yk_folder_cb_t cb, void *ctx) {
    if ((unsigned)folder >= YK_FOLDER_COUNT || !cb) return ESP_ERR_INVALID_ARG;
    if (!ready()) return ESP_ERR_NO_MEM;
    if (__atomic_exchange_n(&s_asking, true, __ATOMIC_ACQ_REL)) return ESP_ERR_INVALID_STATE;
    ask_t *a = heap_caps_malloc(sizeof(*a), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (a) *a = (ask_t){.folder = folder, .cb = cb, .ctx = ctx};
    if (!a || xTaskCreatePinnedToCore(ask_task, "yk_folder", ASK_STACK, a, ASK_PRIORITY, NULL, tskNO_AFFINITY) != pdPASS) {
        heap_caps_free(a);
        s_asking = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// ---------------------------------------------------------------- reading

void *yk_folder_fs_alloc(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
void yk_folder_fs_free(void *p) { heap_caps_free(p); }

static esp_err_t to_esp(yk_fs_err_t e) {
    switch (e) {
    case YK_FS_OK: return ESP_OK;
    case YK_FS_ARG: return ESP_ERR_INVALID_ARG;
    case YK_FS_NOT_FOUND: return ESP_ERR_NOT_FOUND;
    case YK_FS_NO_MEM: return ESP_ERR_NO_MEM;
    default: return ESP_FAIL;
    }
}

/** May this run read `folder` now: allowed (an ask said so), and the card free of a computer. */
static esp_err_t may(yk_folder_t folder) {
    if ((unsigned)folder >= YK_FOLDER_COUNT) return ESP_ERR_INVALID_ARG;
    if (!(s_allowed & 1u << folder)) return ESP_ERR_NOT_ALLOWED;
    if (yk_tf_card_usb && yk_tf_card_usb()) return ESP_ERR_INVALID_STATE;
    return ready() ? ESP_OK : ESP_ERR_NO_MEM;
}

/** One more hold on the card (under s_lock), and its drive. */
static esp_err_t hold(char drive[3]) {
    const esp_err_t err = yk_tf_acquire();
    if (err != ESP_OK) return err == ESP_ERR_NO_MEM ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_STATE;
    if (!yk_tf_card_drive(drive)) {
        yk_tf_release();
        return ESP_ERR_INVALID_STATE;
    }
    s_held++;
    return ESP_OK;
}

static void let_go(void) {
    if (s_held <= 0) return;
    s_held--;
    yk_tf_release();
}

esp_err_t yk_folder_list(yk_folder_t folder, const char *sub, size_t from, yk_folder_entry_t *out, size_t max,
                         size_t *got) {
    if (got) *got = 0;
    esp_err_t err = may(folder);
    if (err != ESP_OK) return err;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    char drive[3];
    err = hold(drive);
    if (err == ESP_OK) {
        err = to_esp(yk_folder_fs_list(drive, folder, sub ? sub : "", from, out, max, got));
        let_go();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t yk_folder_open(yk_folder_t folder, const char *path, yk_folder_file_t **out, uint64_t *size) {
    if (out) *out = NULL;
    esp_err_t err = may(folder);
    if (err != ESP_OK) return err;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    char drive[3];
    err = hold(drive);  // kept while the file is open
    if (err == ESP_OK) {
        err = to_esp(yk_folder_fs_open(drive, folder, path, out, size));
        if (err != ESP_OK) let_go();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t yk_folder_read(yk_folder_file_t *f, void *buf, size_t size, size_t *got) {
    if (!ready()) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const esp_err_t err = to_esp(yk_folder_fs_read(f, buf, size, got));
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t yk_folder_seek(yk_folder_file_t *f, uint64_t offset) {
    if (!ready()) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const esp_err_t err = to_esp(yk_folder_fs_seek(f, offset));
    xSemaphoreGive(s_lock);
    return err;
}

void yk_folder_close(yk_folder_file_t *f) {
    if (!f || !ready()) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int before = yk_folder_fs_open_count();
    yk_folder_fs_close(f);
    if (yk_folder_fs_open_count() < before) let_go();  // a file of ours: its hold goes with it
    xSemaphoreGive(s_lock);
}
