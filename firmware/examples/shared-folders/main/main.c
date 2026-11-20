// Shared folders example (development kit 1.10; docs/app-guide.md 「存储卡和共享文件夹」, docs/app-json.md 「共享文件夹」).
// What an app does to read the person's own files, and to keep one of its own:
//  - asks when the person wants it, a tap here, never at the start or on a timer (yk_folder_ask);
//  - reads in a task of its own with 8 KB of stack, never on the screen's task: the card can take a second or more;
//  - lists Music and reads the first 4 KB of the first song (decoding it is the app's own: bring your MP3 decoder);
//  - writes one file into its own directory on the card (yk_tf_acquire, yk_tf_path, fopen, yk_tf_release);
//  - closes its files when the person leaves (on_leave): nothing new starts on the card, the step under way is waited for.
// app.json asks for `music`, with what for, and `tf_card` for its own directory. The startup order is the template's
// (firmware/_template/main/main.c), without its counter and 应用码 button.
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "cjk_font.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "yaykit_app.h"
#include "yaykit_key.h"
#include "yaykit_link.h"
#include "yaykit_tf.h"
#include "yaykit_ui.h"
#include "yk_folder.h"

#define APP_SLUG "shared-folders"
static const char *TAG = "shared-folders";

#define LIST_MAX 8        // things listed from Music at a time
#define HEAD_BYTES 4096   // what is read of the first song
#define WORKER_STACK 8192 // the card needs 4 KB of the caller's stack; the rest is this example's own
#define LEAVE_WAIT_MS 300 // on_leave waits this long at most for the step on the card; YK_APP_LEAVE_MS is 400

static lv_obj_t *s_status;
static TaskHandle_t s_worker;
static QueueHandle_t s_answers;  // yk_folder_ask's answer, from yaykit_tf's task to the worker
static volatile bool s_tapped, s_busy;

// Leaving and the card: a step on the card (a list, an open to its close, keep_last) starts only while the app is not
// leaving, and on_leave waits for the one under way. Both flags under one lock, so no step starts between on_leave's
// flag and its wait.
static portMUX_TYPE s_card_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_leaving, s_in_card;

/** Before a step on the card: false once leaving has begun, and then the step is not started. */
static bool card_begin(void) {
    portENTER_CRITICAL(&s_card_lock);
    bool ok = !s_leaving;
    if (ok) s_in_card = true;
    portEXIT_CRITICAL(&s_card_lock);
    return ok;
}

/** After the step: its files are closed. */
static void card_end(void) {
    portENTER_CRITICAL(&s_card_lock);
    s_in_card = false;
    portEXIT_CRITICAL(&s_card_lock);
}

// What the screen says next: written by the worker, shown by the LVGL timer.
static portMUX_TYPE s_text_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_text[128];
static bool s_text_new;

static void say(const char *text) {
    portENTER_CRITICAL(&s_text_lock);
    strlcpy(s_text, text, sizeof(s_text));
    s_text_new = true;
    portEXIT_CRITICAL(&s_text_lock);
}

/** What to tell the person for an answer other than YK_FOLDER_ALLOWED. */
static const char *answer_text(yk_folder_answer_t answer) {
    switch (answer) {
    // Right after the person taps 「不允许」 the system says this itself for 3 s; later asks answer DENIED at once, the
    // app's to say. The same words as the system's.
    case YK_FOLDER_DENIED: return "想用了，到手机的「应用权限」里打开";
    case YK_FOLDER_NOT_NOW: return "这次没读。想听的时候再点一下";  // not a no: no word of the phone
    case YK_FOLDER_NOT_DECLARED: return "app.json 的 permissions.folders 没写 music";
    case YK_FOLDER_NO_CARD: return "没插存储卡，或者卡读不了";
    case YK_FOLDER_USB: return "先在电脑上推出 U 盘";
    default: return "这次没读成";
    }
}

/** What to tell the person when a read did not work. */
static const char *read_text(esp_err_t err) {
    switch (err) {
    case ESP_ERR_NOT_FOUND: return "卡上没有 Music 文件夹";
    case ESP_ERR_INVALID_STATE: return "存储卡现在用不了，过一会儿再点";  // Bluetooth on, a computer's USB disk
    case ESP_ERR_NO_MEM: return "内存不够，这次没读";
    default: return "存储卡没回应，再点一下试试";
    }
}

/** On yaykit_tf's task, once for each yk_folder_ask() that returned ESP_OK. */
static void answered(yk_folder_answer_t answer, void *ctx) {
    (void)ctx;
    xQueueOverwrite(s_answers, &answer);
}

/** The app's own file: the first song's name, in its own directory. The card is never needed: anything but ESP_OK
 *  means carry on without it. One short write, never started once leaving has begun: a write cut by the restart can
 *  leave a broken file on the card. */
static void keep_last(const char *name) {
    if (!card_begin()) return;
    if (yk_tf_acquire() == ESP_OK) {
        char path[128];
        if (yk_tf_path("last.txt", path, sizeof(path)) == ESP_OK) {
            FILE *fp = fopen(path, "w");
            if (fp) {
                fprintf(fp, "%s\n", name);
                fclose(fp);  // before yk_tf_release()
                printf("FOLDERS_KEPT %s\n", path);
            }
        }
        yk_tf_release();
    }
    card_end();
}

/** After YK_FOLDER_ALLOWED: what is in Music, then the first 4 KB of the first song, each step between card_begin()
 *  and card_end(). Once leaving has begun no step starts. */
static void read_music(void) {
    yk_folder_entry_t *list = heap_caps_malloc(LIST_MAX * sizeof(*list), MALLOC_CAP_SPIRAM);
    uint8_t *head = heap_caps_malloc(HEAD_BYTES, MALLOC_CAP_SPIRAM);
    size_t n = 0, songs = 0, got = 0;
    const yk_folder_entry_t *first = NULL;
    esp_err_t err = ESP_ERR_NO_MEM;
    if (list && head) {
        if (!card_begin()) goto out;
        err = yk_folder_list(YK_FOLDER_MUSIC, "", 0, list, LIST_MAX, &n);
        card_end();
    }
    if (err == ESP_OK) {
        for (size_t i = 0; i < n; i++) {
            printf("FOLDERS_ENTRY %s%s size=%llu\n", list[i].name, list[i].dir ? "/" : "", (unsigned long long)list[i].size);
            if (list[i].dir) continue;
            songs++;
            if (!first) first = &list[i];
        }
        printf("FOLDERS_LIST n=%u songs=%u\n", (unsigned)n, (unsigned)songs);
    }
    if (err == ESP_OK && first) {
        if (!card_begin()) goto out;
        yk_folder_file_t *f = NULL;
        uint64_t size = 0;
        // One small read (4 KB): the step has to be done well within LEAVE_WAIT_MS. A player reads a piece at a time,
        // each between card_begin() and card_end(), and keeps its place to open the file again.
        err = yk_folder_open(YK_FOLDER_MUSIC, first->name, &f, &size);
        if (err == ESP_OK) err = yk_folder_read(f, head, HEAD_BYTES, &got);
        yk_folder_close(f);  // NULL does nothing
        card_end();
        if (err == ESP_OK) {
            // The bytes are the app's to decode; here only their first four, enough to tell an MP3's "ID3".
            printf("FOLDERS_READ name=%s size=%llu got=%u head=%02x%02x%02x%02x\n", first->name,
                   (unsigned long long)size, (unsigned)got, got > 0 ? head[0] : 0, got > 1 ? head[1] : 0,
                   got > 2 ? head[2] : 0, got > 3 ? head[3] : 0);
            keep_last(first->name);
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Music: %s", esp_err_to_name(err));
        say(read_text(err));
    } else if (!first) {
        say("Music 里还没有歌：插上电脑，拷几首进去");
    } else {
        char text[128];
        snprintf(text, sizeof(text), "Music 里有 %u 首，第一首读了 %u 字节", (unsigned)songs, (unsigned)got);
        say(text);
    }
out:
    free(list);
    free(head);
}

/** The card can block for a second or more: everything of it runs here, one tap at a time. */
static void worker(void *arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        say("在问…");
        xQueueReset(s_answers);
        yk_folder_answer_t answer = YK_FOLDER_NOT_NOW;
        // The question, when the person has not answered yet, covers the screen until they do (or 30 s).
        if (yk_folder_ask(YK_FOLDER_MUSIC, answered, NULL) == ESP_OK) xQueueReceive(s_answers, &answer, portMAX_DELAY);
        if (answer == YK_FOLDER_ALLOWED) {
            say("在读…");
            read_music();
        } else {
            say(answer_text(answer));
        }
        s_busy = false;
    }
}

/** Through yaykit_app (yk_app_on_key): a tap reads; a hold this app does not use (the system asks 「回到主屏幕说？」),
 *  and the double click is the system's app ring. */
static bool on_key(const yk_key_event_t *ev, void *ctx) {
    (void)ctx;
    if (ev->type != YK_KEY_TAP) return false;
    s_tapped = true;
    return true;
}

static void on_read(lv_event_t *e) {
    (void)e;
    s_tapped = true;
}

/** LVGL timer: runs with the board lock held, so it may touch the screen. */
static void tick(lv_timer_t *timer) {
    (void)timer;
    if (s_tapped) {
        s_tapped = false;
        if (!s_busy && !s_leaving) {
            s_busy = true;
            xTaskNotifyGive(s_worker);
        }
    }
    char text[sizeof(s_text)];
    bool show = false;
    portENTER_CRITICAL(&s_text_lock);
    if (s_text_new) {
        memcpy(text, s_text, sizeof(text));
        s_text_new = false;
        show = true;
    }
    portEXIT_CRITICAL(&s_text_lock);
    if (show) lv_label_set_text(s_status, text);
}

/** Leaving for the main firmware (a double click, `app exit`). Runs in a task of yaykit_app's, and the chip restarts
 *  after YK_APP_LEAVE_MS (400 ms) whatever is under way. From here on no step on the card starts; the one under way
 *  is waited for, up to LEAVE_WAIT_MS, to close its file. One that takes longer is not waited for and is cut: so a
 *  read here is one small piece, and the write to the app's own directory is short and never starts after this. */
static void on_leave(void *ctx) {
    (void)ctx;
    portENTER_CRITICAL(&s_card_lock);
    s_leaving = true;
    portEXIT_CRITICAL(&s_card_lock);
    for (int waited = 0; s_in_card && waited < LEAVE_WAIT_MS; waited += 10) vTaskDelay(pdMS_TO_TICKS(10));
    if (s_in_card) ESP_LOGW(TAG, "left with the card still in use");
}

static esp_err_t on_command(const char *name, const cJSON *args, cJSON *reply, const char **code, void *ctx) {
    (void)ctx;
    yk_app_input();
    return yk_app_command(name, args, reply, code);  // app.exit, app.recovery, app.status
}

static int heap_cmd(int argc, char **argv) {
    (void)argc, (void)argv;
    printf("HEAP internal=%u internal_min=%u psram=%u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return 0;
}

static void start_console(void) {
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    cfg.prompt = APP_SLUG ">";
    cfg.task_stack_size = 8192;
    esp_console_dev_usb_serial_jtag_config_t usb = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usb, &cfg, &repl));
    esp_console_register_help_command();
    ESP_ERROR_CHECK(yk_link_register_console());
    ESP_ERROR_CHECK(yk_app_register_console());
    yk_ui_register_snapshot();
    const esp_console_cmd_t heap = {.command = "heap", .help = "available memory", .func = heap_cmd};
    ESP_ERROR_CHECK(esp_console_cmd_register(&heap));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

void app_main(void) {
    // 1. NVS: never erase it.
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) ESP_LOGE(TAG, "NVS unavailable (%s); running without saved settings", esp_err_to_name(err));
    // 2. yaykit_app first (the crash loop guard).
    ESP_ERROR_CHECK(yk_app_start(&(yk_app_config_t){.slug = APP_SLUG, .version = APP_VERSION}));
    yk_app_on_leave(on_leave, NULL);
    // The worker before anything can tap.
    s_answers = xQueueCreate(1, sizeof(yk_folder_answer_t));
    if (!s_answers || xTaskCreate(worker, "folders", WORKER_STACK, NULL, tskIDLE_PRIORITY + 2, &s_worker) != pdPASS) {
        ESP_LOGE(TAG, "no memory for the worker");
        return;
    }
    // 3. Board and screen (dark until the first frame), then the side key.
    if (!board_display_start_opts(&(board_display_opts_t){.start_dark = true})) {
        ESP_LOGE(TAG, "display failed");
        return;
    }
    ESP_ERROR_CHECK(yk_key_init(&(yk_key_config_t){.delay_tap = true}));
    ESP_ERROR_CHECK(yk_app_on_key(on_key, NULL));
    // 4. Yaykit Link: `yk info` and `yk cmd` over USB.
    const yk_link_config_t link = {.board = "tikpal-r1", .app_slug = APP_SLUG, .app_version = APP_VERSION,
                                   .layout = "yaykit-v1", .idle_seconds = 90, .on_command = on_command};
    ESP_ERROR_CHECK(yk_link_init(&link));

    // 5. The screen; then confirm the image.
    board_lock(-1);
    ESP_ERROR_CHECK(cjk_font_init());
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_style_bg_color(screen, lv_color_hex(YK_PAPER), 0);
    yk_ui_label(screen, "共享文件夹", 0, 100, 466, YK_SIZE_STATUS, YK_SAGE_TEXT);
    s_status = yk_ui_label(screen, "点一下，读 Music 里的歌", 83, 180, 300, YK_SIZE_MIN, YK_INK);
    yk_ui_button(screen, "读音乐", 148, 300, 170, 44, on_read, NULL, YK_PANEL);
    lv_screen_load(screen);
    lv_refr_now(NULL);
    board_brightness_set(yk_app_brightness());
    lv_timer_create(tick, 50, NULL);
    board_unlock();
    // 6. The console, then yk_app_ready().
    start_console();
    yk_app_ready();
}
