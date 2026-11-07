# 应用开发指南

写给做 Tikpal Voice 应用（扩展固件）的人和 AI。完整流程（七步、交什么、不能做的事）见 [AGENTS.md](../AGENTS.md)。照这份做，你的应用才能和 Tikpal 主固件、和别的应用配合好。

组件的头文件（`devkit/firmware/components/*/include/`）是准的：这份文档和头文件对不上时，以头文件为准，并告诉我们改文档。

## 1. 圆屏和位置

**圆屏**：ESP32-S3（双核 240 MHz）、32 MB Flash、8 MB PSRAM、1.75 寸 466×466 圆形 AMOLED 带触摸、两路麦克风、扬声器、六轴姿态（QMI8658）、一个侧键、TF 卡、电池。

**位置**：Flash 上有三个放应用的位置。

| 位置 | 放什么 | 大小 |
|---|---|---|
| 1 | Tikpal 主固件，只放它 | 3.5 MB |
| 2、3 | 扩展应用，你的应用就装在这里 | 各 3.5 MB |

另外还有恢复器（出厂位置）、共享中文字体（所有应用只读）、语音模型、安装包缓存。你的应用只占一个位置，别的都不碰。

**同一时间只有一个固件在跑。** 打开你的应用就是重启进你的位置，回到 Tikpal 主固件就是重启进位置 1。所以主固件和你的应用之间只靠三样东西说话：启动参数、返回结果、给平台的记录（第 5 节）。

## 2. 用户的一天：你的应用要跟上的地方

1. 用户对伙伴 Pip 说「帮我计时十分钟」→ Pip 说「好，打开计时器」→ 倒计时 3 秒 → 黑底「正在打开」→ 你的应用起来，按启动参数直接开始。
2. 在你的应用里，用户按你的方式用。第一次进来时，提示箭头指向侧键「双击：应用和回家」。
3. 回去：双击侧键开应用环 → 点中间（下面写着「回到 Tikpal」）→ 「正在回到 Tikpal」→ Pip 接着说一句（「计时完成了吗？」），用的是你留下的结果。

**你的应用必须和 Tikpal 主固件一致的地方**（细节见[圆屏设计规范](round-screen-design.md)）：

| 方面 | 要求 |
|---|---|
| 颜色 | 只用 `yk_round.h` 的主题色（同一目录的 `yk_theme.h` 是底层的设计令牌，数值和圆屏规范不完全一样，别直接用）（底 `#171C19`、珊瑚 `#DF795D`、鼠尾草 `#9FB38C`、文字 `#FAF7EF` / `#A5AA9E` / `#737A70`），不写死颜色；要沉浸感可以纯黑底。**不用蓝色** |
| 字 | 共享字体（`cjk_font_get(px)`）；字号用 `yk_round.h` 的常量：正文和字幕 22–24（三行放不下时可以用 20，`YK_SIZE_SUBTITLE_SMALL`），状态行 18，提示和说明 16（`YK_SIZE_MIN`，不再更小）；不为塞字缩字 |
| 布局 | 文字和能点的东西放在半径 200 的安全圆里（`YK_R_CONTENT`）；顶部状态 y 46–70，底部提示 y 390 |
| 侧键 | 双击归系统（应用环）；按住、短按归你（要说话就用按住说话，和主固件一样） |
| 说话的样子 | 在听用鼠尾草外圈，等用户用珊瑚色；说人话，不显示错误码 |
| 伙伴这个角色 | 要出现就用 `yk_yay` 画同一个 Pip，不自己画别的角色；界面上要说名字用 `yk_ui_buddy_name()` |
| 网络 | 不做配网；没网时提示「回 Tikpal 设置网络」（`yk_notice_net()`） |
| 语言 | 跟着账号语言：界面文字做中文、英文两套（`yk_locale.h`：`zh` 开头的是中文，别的都按英文）；日文、西班牙文账号现在显示英文 |

## 3. 资源归谁

一个固件里，每样资源只能有一个组件去碰：

| 资源 | 归谁 | 你怎么用 |
|---|---|---|
| 屏幕、触摸、LVGL | `yaykit_board` | 改界面前 `board_lock()`，改完 `board_unlock()`；或者在 LVGL 回调里改 |
| 录音 | `yaykit_audio` | 不碰 I2S、编解码芯片 |
| 放音、功放（GPIO43） | `yaykit_speak` | 不直接拉 GPIO43。响度不用管，什么都不用设：说话的声音（`yk_speak_opus` / `yk_speak_pcm` / `yk_speak_clip`、`yk_cloud_tts`、`yaykit_talk` 的回答）由组件按圆屏的喇叭和功放自动处理，跟着音量档走，不要自己再放大。这几个接口只放说话，音乐别走它们，会被削薄、压扁。音效和音乐（`yk_sfx_play_*`）不经这一步，用 `gain` 调你自己的混音；短提示音用 `yk_speak_chime`，或者 `yk_speak_cue_pcm` 放你自己的。这些都有限幅保护，峰值不会超过喇叭的上限 |
| 侧键（GPIO0） | `yaykit_key`、`yaykit_app` | 订阅事件（`yk_app_on_key`），不轮询 GPIO0 |
| Wi-Fi | `yaykit_wifi` | `yk_wifi_start()` 用已有的网络，不配网 |
| 蓝牙 | `yaykit_link` | 平时关着；配对、安装、对话不同时进行（内部 RAM 不够） |
| 账号、令牌 | `yaykit_cloud` | 只用 `yk_account_token()`，不读别人的 NVS |
| 启动分区 | `yaykit_app` | 回主固件用 `yk_return_home()`，不直接调 `esp_ota_set_boot_partition` |
| 姿态（QMI8658） | `yaykit_motion` | 订阅事件、读数据流，不自己读传感器 |
| 电量、USB 供电 | `yaykit_board` | `board_battery()`；电量太低时睡下去归 `yaykit_app`（1.6 起，见第 9 节），你不用自己做 |
| 音量、亮度、息屏时间、伙伴 | `yaykit_board`（全设备共用，存在 `yk_pref`） | 经 `yaykit_app` 用（1.6 起）：`yk_app_brightness()`、`yk_app_screen_off_s()`、`yk_app_volume()` / `yk_app_set_volume()`，跟主固件的设置走，低电量的上限也算在里面；应用里调了音量用 `yk_app_set_volume()` 写回。`board_pref_*()` 照旧能用，只是不带低电量的上限。伙伴只读（`yk_ui_buddy_name()`） |
| TF 卡 | 你自己 | 只碰自己的目录 |

**NVS**：你的设置放在自己的命名空间 `yk_<简称>`。简称就是 slug 去掉连字符（`night-light` 的简称是 `nightlight`，命名空间 `yk_nightlight`），记录种类、命令名前面的也是它；NVS 最长 15 个字符，超了就截到 15。`./kit check` 和审核都照这个核，`nvs_open` 打开别的名字会被拦下。别的命名空间只读或者不碰（`yk_sys` 归 `yaykit_app`，`yk_pref` 用 `yk_app_*()` 或 `board_pref_*()`）。**绝不调用 `nvs_flash_erase()`**：NVS 出错时记日志、照常跑。

## 4. 启动顺序

照模板（`main/main.c`）的顺序，按需取用，顺序不变：

1. NVS 初始化（出错不擦）。
2. `yk_app_start(&cfg)`：一定是 NVS 之后的第一个调用。它管开机崩溃保护（连续 3 次起不来就回主固件）、记下使用时间、接上双击开应用环、读共享设置。1.6 起它还先看一眼电池：没插电又快没电了，就不往下走，直接睡（第 9 节）。所以它要在屏、Wi-Fi 之前。
3. 板子：`board_display_start_opts(&(board_display_opts_t){.start_dark = true})`（屏幕先黑着，画完第一帧再亮）、`board_rtc_init()`（恢复时间）、要声音的再 `audio_init_rate(16000)`。
4. `yk_key_init(&(yk_key_config_t){.delay_tap = true})`，`yk_app_on_key(on_key, NULL)`。
5. `yk_app_launch(&launch)` 读启动参数；画第一帧，再 `board_brightness_set(yk_app_brightness())` 按主固件的亮度亮起来。
6. `yk_link_init()` 和控制台；画好主界面、控制台也起来以后 `yk_app_ready()`（确认这一版镜像，开始打启动横幅 `YAYKIT_<SLUG>_READY`）。
7. 要网络的：`yk_wifi_start()`；要云的：`yk_account_init()`。

**回滚**：新装的应用第一次启动是「待确认」。`yk_app_ready()` 之前崩了或重启了，圆屏自动回到上一个能用的（也就是 Tikpal 主固件），Pip 会说「上次的应用没有正常启动」。

## 5. 和主固件说话：启动参数、结果、记录

### 启动参数

主固件打开你时可以带参数（`launch.params` 的格式由你的 `app.json` 定义）。启动时读一次：

```c
yk_launch_t launch;
if (yk_app_launch(&launch) == ESP_OK && launch.present) {
    // launch.intent、launch.params（JSON）：直接进对应的页面
}
```

不是给你的、超过 60 秒的都不算。没有参数就正常进首页。

### 回到主固件

- 双击侧键 → 应用环 → 点中间：系统替你做，不用写代码。
- 你自己想回去（比如一局结束）：`yk_return_home()`。
- 回去并让主固件做点事：`yk_return_home_with(intent, params_json)`。

离开之前系统会调你登记的收尾（`yk_app_on_leave(fn, ctx)`，最多 400 ms；要等网络的用 `yk_app_on_leave_ms`，最多 1 秒）。在收尾里写结果和记录：

### 结果（给伙伴念的那一句）

```c
yk_summary_t s;
yk_summary_init(&s);
yk_summary_stat(&s, "10", "分钟");
yk_app_set_result_summary("计时 10 分钟，完成了", &s, NULL);
```

- 一句人话，回到主固件时伙伴念出来或用在下一句话里；`stats` 最多 3 个，画在主屏的圆环上。
- 写下超过 30 分钟才回到主固件的，这句不念。
- 不留东西的应用（比如翻译）不写摘要。

### 记录（给平台的）

`yk_app_record(kind, data_json)`：一条记录，先放进圆屏上的发件箱，谁在线谁送，你不用自己联网。

- `kind` 只用小写字母、数字和点，3–41 个字符，前缀是你的简称：`mytimer.session`。
- `data` 是不超过 512 字节的 JSON 对象。
- 最多留 16 条，满了顶掉最旧的。

### 你自己的命令

`yk_link_init` 的 `on_command` 处理 `yk cmd <名字>`，名字也用你的简称：`mytimer.status`。先调 `yk_app_command()`，它处理 `app.exit`、`app.recovery`、`app.status`。

## 6. 侧键

```c
static bool on_key(const yk_key_event_t *e, void *ctx) {
    switch (e->type) {
    case YK_KEY_TAP:  /* 短按 */ return true;
    case YK_KEY_HOLD: /* 按住 */ return true;
    default: return false;
    }
}
```

- 返回 `true` 表示「这一下我用了」。
- 双击留给系统（应用环）。真要用双击，在 `yk_app_config_t` 里设 `own_double`，并且另给一个回到主固件的办法。
- 你没用掉的按住，又没有自己的对话，系统会问「回到 Pip 说？」（伙伴名照共享设置）。息屏、倒计时这类时候按住有别的意思，就返回 `true`。
- 有自己的语音对话（`yaykit_talk`），在 `yk_app_config_t` 里设 `talks`：按住永远归你。
- `yk_key_init` 要开 `delay_tap`：不开的话，开应用环的那次双击，第一下会先当单击给你。

## 7. 组件速查

只列做应用常用的；完整的接口看头文件。

| 组件 | 做什么 | 常用 |
|---|---|---|
| `yaykit_app` | 生命周期、启动参数、结果、记录、应用环、共享设置、电量保护 | `yk_app_start`、`yk_app_ready`、`yk_app_launch`、`yk_app_on_key`、`yk_app_on_leave`、`yk_app_set_result_summary`、`yk_app_record`、`yk_return_home`、`yk_app_brightness`、`yk_app_screen_off_s`、`yk_app_english` |
| `yaykit_board` | 屏幕、触摸、时钟、音频芯片、电量、共享设置、字体 | `board_display_start_opts`、`board_lock`、`board_battery`、`board_pref_*`、`board_display_sleep/wake`、`cjk_font_get` |
| `yaykit_key` | 侧键状态机 | `yk_key_init`（用 `yk_app_on_key` 订阅） |
| `yaykit_ui` | 主题、边缘环、伙伴、字幕、提示、进出、截图 | `yk_round.h`、`yk_rim_*`、`yk_yay_*`、`yk_caption_*`、`yk_notice_*`、`yk_hint_*`、`yk_ui_buddy_name`、`yk_ui_register_snapshot` |
| `yaykit_link` | 设备 ID、USB 控制台、`yk cmd` | `yk_link_init`、`yk_link_register_console`、`yk_link_device_id` |
| `yaykit_wifi` | 用已有的 Wi-Fi | `yk_wifi_start`、`yk_wifi_wait_connected` |
| `yaykit_cloud` | 账号、令牌、设备公共 API | `yk_account_init`、`yk_cloud_me`、`yk_cloud_has`、`yk_cloud_asr`、`yk_cloud_chat`、`yk_cloud_tts`、`yk_cloud_data_*` |
| `yaykit_talk` | 扩展用的语音对话（按住说话 → 回答 → 播放 → 打断） | `yk_talk_start`、`yk_talk_key`、`yk_talk_close` |
| `yaykit_audio` | 录音，16 kHz，降噪、人声检测 | `yk_audio_init`、`yk_audio_start`、`yk_audio_rec_*` |
| `yaykit_speak` | 放音、Opus、音效混音 | `yk_speak_init`、`yk_speak_opus`、`yk_sfx_play_*`、`yk_speak_chime`、`yk_speak_cue_pcm` |
| `yaykit_motion` | 姿态：拿起、放下、摇、扣下 | `yk_motion_start`、`yk_motion_state`、`yk_motion_stream` |
| `yaykit_recorder` | TF 卡上断电安全的录音 | `yk_rec_open/write/close` |

**云能力的规矩**：

- 调之前先 `yk_cloud_has("<能力>")`；时长、字数、语言这些限额从 `/me.capabilities` 读，不写死。
- 提示词和模型不写在固件里：用服务端的模板名或场景接口。
- 解析返回时忽略不认识的字段和事件（接口只做加法）。
- 你的应用要哪些能力，在开发者平台这个应用的「能力和权限」页申请：花用户点数的（AI 对话、语音识别、语音合成……）、麦克风这类要人工批，我们通常 1 个工作日内回，别的建应用时就开通；要加能力也在那一页申请。

## 8. 内存

- **内部 RAM 最低不能低于 40,000 字节**（低于 60,000 字节要在应用仓库的 README 里写明原因，审核看代码时会读）。看 `./kit info` 的 `heap.internalMinSinceReady`：从 `yk_app_ready()` 起算的最低，拿原数和 40,000 比，不换算成 KB。`./kit heap` 的 `internal_min` 是开机以来的老读数，偏低，不拿它比。
- 大东西放 PSRAM：图、音频缓冲、大任务的栈（`xTaskCreatePinnedToCoreWithCaps(..., MALLOC_CAP_SPIRAM)`）。
- 用网络、TLS 的应用，`sdkconfig.defaults` 照主固件省内部 RAM：
  - `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`、`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`（TLS 和 Wi-Fi 缓冲进 PSRAM）；
  - `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`（4 KB 以下才放内部 RAM）；
  - 用蓝牙的：`CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y`。

## 9. 兼容和版本

- **`YK_API_LEVEL`**（`yaykit_app.h`）：公共组件给应用的接口版本，只增不改。开发包 1.0 到 1.8 都是 3；1.9 起是 4：应用能亮自己的应用码（[voice-code.md](voice-code.md)），从顶边往下拉归系统的下拉，应用环只放应用。按 3 编的应用不重编照旧跑。
- **`app.json` 的 `minYay`**：你的应用要求的最低 Tikpal 主固件版本，至少是开发包对应的那一版（`./kit version` 的 `yay`，`./kit check` 会核）。主固件太旧的圆屏，商城不给它列出你的应用。
- **`apiLevel`**：写开发包的 `apiLevel`。
- 开发包每次 Tikpal 主固件发版出一版。换开发包版本（`./kit update`）以后重编、重新自测，再提交。
- 你的 `main/main.c` 是建应用时从模板抄的，换开发包版本不会跟着变：模板改了哪里，下面的更新记录会写，要用就自己照着改。

### 更新记录

只记组件（`firmware/`）的变化和换版本要做的事；别的文档和工具的改动看开发包仓库的提交。

**1.9**（对应 Tikpal 主固件 0.1.14）：组件换成主固件 0.1.14 的那份，`YK_API_LEVEL` 升到 4。接口只加不改，按 3 编的应用不重编照旧跑。

- **换过来：一般升级开发包版本、改 `app.json`、重编就行。** `./kit update 1.9`，再改下面三处 `app.json`，重编、自测、提交。这一版的新东西都是重编就有，一般的应用 C 代码和 CMake 不用动，要改代码的两种见下一条。
  - `minYay` 至少写 `"0.1.14"`：`./kit version` 的 `yay` 是 0.1.14，`./kit check` 要 `minYay` 不比它旧。圆屏上的主固件比 `minYay` 旧，商城不给它列出你的应用。
  - `apiLevel` 写 4。
  - `voiceCode` 必填：写建应用时给的编号（[app-json.md](app-json.md#应用码-voicecode)）。下拉里的「在手机上打开」靠它。
- **这两种应用要改代码**：
  - 读 `yk_acct` 的：`./kit check` 和审核都不过。语言换成 `yk_app_english()`（要语言代码用 `yk_app_locale()`），昵称用 `yk_account_nickname()`。
  - 自带下拉页的（自己做了一份状态页，往下滑打开）：删掉，系统下拉已经有这些，留着用户会看到两个下拉。自带页里读 `yk_acct` 的代码跟着删。
- **接口级别 4，亮码**：应用能亮自己的应用码，用 `yk_vc_show()`，用法见 [voice-code.md](voice-code.md)。这一版亮动作 0–2。`yaykit_voicecode` 默认链接：`yaykit_app` 依赖它，`firmware/reproducible.cmake` 加组件目录，CMake 不用改。自己调 `yk_vc_show()` 才要在 `main/CMakeLists.txt` 的 `REQUIRES` 加它。
- **系统下拉不能不用**：圆屏从顶边往下拉是系统的下拉（Wi-Fi、电量、手机连接这些状态），重编就有，不用写代码，下拉里的「在手机上打开」亮你的应用码。绕开它的写法，1.9 起过不了：
  - `./kit check`（审核也跑这份）卡：构建里没有 `yaykit_app`、`yaykit_ui`、`yaykit_voicecode`，链接表里没有 `yk_app_sheet_attach`、`yk_sheet_create`、`yk_vc_show_ring`；`app.json` 没写 `voiceCode`；应用代码不调 `yk_app_start()`、`yk_app_ready()`；写了 `is_home`、`YK_APP_HOME_SLUG`，或者 `.slug` 是 `yay`。
  - 机器审卡：项目里放一份和开发包同名的组件，自己定义这些函数，用链接参数 `--wrap`、`--defsym`、`--allow-multiple-definition`、`-z muldefs` 换函数。`lv_layer_top()`、`lv_layer_sys()`、`lv_indev_delete()` 这类调用标出来由人看。
  - 绕开系统下拉不允许，真机审核也会看。规则全文见[审核规则 F-05](review-rules.md)。
  - 组件里也收了口：只有 slug 是 `yay` 才算主屏，别的应用写了 `.is_home` 也还是扩展。
- **响度不用你设**：喇叭的调音归板子（`board_speaker.h`），开发者什么都不用设定，重编就有。
  - 说话的声音（`yk_speak_opus`、`yk_speak_pcm`、`yk_speak_clip`、`yk_cloud_tts`、`yaykit_talk` 的回答）由组件自动处理，跟着音量档走：1 档比原来的 6 档还响 3–4 dB，10 档响约 25 dB。不要自己再放大。
  - 音乐和音效走 `yk_sfx_*`，不经这一步，用 `gain` 调你自己的混音；音量档本身每档比原来响 2.5–4 dB。音乐别走说话的接口，会被削薄、压扁。
  - 提示音：`yk_speak_chime()` 不走人声增强，音色不变，比原来响。放自己的短提示音用新接口 `yk_speak_cue_pcm()`。
  - 所有声音的峰值都不超过喇叭的上限。音量 0 档是真静音，提示音也听不到。
  - `yk_volume_to_codec()` 还在，已经废弃，用 `yk_speak_volume()`。
- **伙伴叫 Pip**：`yk_ui_buddy_name()` 默认回 Pip（原来是 Yay），「回到 Pip 说？」跟着变。`BOARD_BUDDY_YAY` 这个常量名不变，值还是 0。
- **应用环只放应用**：环上没有「设置」了，只有应用和「更多」，不亮码。在环上滑一下是收起，不算点。
- **Tikpal 标记改画「伴环」标**：`yk_ui_mark()` 画一段圆头珊瑚弧加一个鼠尾草圆点，用主题色 `YK_CORAL`、`YK_SAGE`，接口和 `size` 的意思不变（环的外径）。点伸出环的圆，往右上最远到中心外 0.64·`size`：版面用 `yk_mark_within()` 查（它已经把点算进去），`yk_mark_reach(size)` 给出最远多少。只加不改：新加 `yk_ui_mark_pose()`、纯函数 `yk_mark_arrival(ms)`（到场动画，900 ms）和 `yk_mark_ring_cover()`、`yk_mark_disc_cover()`（每个像素的覆盖率，要每帧动大标时自己画进带里用，`lv_arc` 太慢），`YK_MARK_CORAL`、`YK_MARK_TRACK` 留着、不再用。重编才变成新标。
- **电量**（`board.h`）：`board_battery()` 的 `percent` 现在是平滑过的显示值，满之前最多 99，`charging` 和满电来自电源芯片，`status` 改成 SW6106 的 0x11。新加 `level`（没平滑的读数）、`state`、`low`、`fault`、`chip_mv`、`charge_ma`。
- **其他只加不改的**：时区 `board_pref_tz()`、`board_pref_tz_offset()`、`board_pref_tz_apply()`（只读，主固件写；要显示本地时间的应用开机调一次 `board_pref_tz_apply()`）；`yk_account_status()`、`yk_account_token_cached()`；`yk_cloud_reach()`；录音分段上传 `yk_cloud_stream.h`；`yk_talk_mic_speech_ms()`、`yk_talk_mic_speech_span()`。
- **工具**：
  - `./kit check` 核 `main/` 里写死的 `nvs_open`：命名空间要是 `yk_<简称>`（第 3 节）。
  - `./kit new` 建的 `.gitignore` 多了 `shots/`、`heap.json`、`ai-selfreview.json`、`*.tikpal`。老应用自己加上，不然 `./kit check` 算没提交的改动。
- **模板**：多了一个按钮「在手机上打开」亮动作 0 的码（`main/main.c` 的 `on_code`），`on_key` 第一行接 `yk_vc_on_key()`，`app.json` 写了 `voiceCode`，`CMakeLists.txt` 和 `main/CMakeLists.txt` 加了 `yaykit_voicecode`。老应用不用抄，下拉里已经有这个码。
- **没变的**：分区表、`yay_dev.py`、ESP-IDF 版本。没重编的老应用照旧跑：没有系统下拉，环上还有「设置」，声音和原来一样。

**1.6**（对应 Tikpal 主固件 0.1.12）：组件换成主固件 0.1.12 的那份（1.3 到 1.5 是同一份，主固件 0.1.10 时的）。接口只加不改，`YK_API_LEVEL` 还是 3。

- **换过来要改 `minYay`**：`./kit version` 的 `yay` 是 0.1.12，`./kit check` 要 `app.json` 的 `minYay` 至少是它，改成 `"0.1.12"`。`apiLevel` 照旧写 3。
- **跟主固件的设置走**（`yaykit_app.h`；规则在新头文件 `yk_app_power.h`，纯 C，有主机测试）：`yk_app_settings(&s)` 一次拿全（音量、亮度、息屏时间、是不是英文），也可以单拿 `yk_app_brightness()`、`yk_app_screen_off_s()`（0 = 不息屏）、`yk_app_volume()`、`yk_app_english()`、`yk_app_locale()`。在主固件里改的，进你的应用就是这个值；低电量（没插电、这次放电到过 15%）时亮度最多 60%、15 秒息屏，和主固件一样。都在 `yk_app_start()` 之后调。
  - 开屏和 `board_display_wake()` 用 `yk_app_brightness()`，不再传 -1；比空闲时间时每次问 `yk_app_screen_off_s()`（调用本身不读 Flash，每帧问也行）。
  - 应用里调了音量：`yk_app_set_volume(level)` 写回共享设置，回到主固件也是这个值（只改存的值，扬声器还是自己 `yk_speak_volume()`）。
  - 正在做不能息屏的事（一局、一段录音）：`yk_app_keep_screen_on(true)`，做完关掉；开着时 `yk_app_screen_off_s()` 回 0。
  - 手机经 `yaykit_talk` 改了设置（`YK_TALK_EV_SETTING`）：调 `yk_app_settings_reload()` 马上生效。
  - `yk_app_set_volume()`、`yk_app_settings_reload()` 要读写 NVS：在栈放在内部 RAM 的任务里调。
  - `yk_app_battery_low()`：这次放电到过 15%，上限正在生效。
  - `yk_app_english()` 跟账号语言走，没链 `yaykit_cloud` 的应用也能用。
  - `board_pref_*()`、`yk_account_locale()` 都还在，照旧能用，只是不带低电量的上限。
  - 模板改了一行：画完第一帧 `board_brightness_set(yk_app_brightness())`（原来是 `board_pref_brightness()`）。
- **电量保护：重编就有，不用写代码**（只在位置 2、3 里生效）：
  - 开机：`yk_app_start()` 在屏、Wi-Fi 之前看一次电量。没插电，电量 ≤ 5% 或上次是掉电复位，就不亮屏、直接深睡。
  - 运行中：`yk_app_ready()` 以后每 30 秒看一次电量（到 15% 以后，或读到过一次 ≤ 5%，每 10 秒）。没插电时连续两次 ≤ 5%：显示「电量用完了，请充电 / Out of battery. Please charge me.」2.5 秒，其间跑你登记的收尾（`yk_app_on_leave`），把启动分区设回主固件（位置 1 没有可用的主固件就设恢复器），再深睡。侧键、插电或每 30 分钟的定时器叫醒，进的是主固件，不回你的应用。
  - 插着 USB 不睡。门限是 `yk_app_power.h` 里的常量（`YK_POWER_EMPTY` 5、`YK_POWER_EMPTY_READS` 2、`YK_POWER_LOW` 15、`YK_POWER_BRIGHTNESS_MAX` 60、`YK_POWER_SCREEN_OFF_LOW_S` 15）。
  - 内部 RAM 静态多占约 2.4–2.7 KB（深度睡眠必须放在 IRAM 里的那段代码）：重编以后再看一次 `./kit info` 的 `heap.internalMinSinceReady`（第 8 节）。
- **伙伴的名字**（`board.h`、`yaykit_ui.h`）：共享设置 `yk_pref` 加了伙伴（键 `buddy`，`YK_PREF_BUDDY`）。`board_pref_buddy()` 读出 `BOARD_BUDDY_YAY`、`BOARD_BUDDY_WOW`、`BOARD_BUDDY_AHA`（0、1、2），没有或不认识的值都当 Yay；`yk_ui_buddy_name()` 给名字（Yay、Wow、Aha，各语言一样），「回到 Yay 说？」那张卡片就用它。只由主固件写（`board_pref_set_buddy()` 不归你调），主固件 0.1.12 还不写，所以那时读到的都是 Yay（1.9 起叫 Pip，见上面）。界面上要说伙伴的名字，用 `yk_ui_buddy_name()`。
- **应用环和「更多」页的更新小点**（`yk_apps.h`、`yk_more.h`）：`yk_apps_item_t`、`yk_more_item_t` 末尾各加 `bool update`，为 true 时圆盘右上角画珊瑚色小点。只有主固件填；扩展里的应用环（`yaykit_app` 替你开的）一直是 false。字段加在末尾，原来的写法不用改，没写的就是 false。
- **Tikpal 标记和共享界面上的字**：
  - 新头文件 `yk_mark.h`：`yk_ui_mark(parent, cx, cy, size)` 用两段 LVGL 圆弧画 Tikpal 标记（不占 Flash），页面上的大小是 `YK_MARK_PX`（`yk_mark_fit.h`，默认 64）。主固件的配对页、设置里的关于和更新页、恢复器的更新和安装页用它；开机动画还是伙伴。
  - 重编以后，你的应用里这些字跟着变，截图对照以这些为准：应用环中间「回到 Tikpal / Back to Tikpal」，回去时「正在回到 Tikpal」；没网「回 Tikpal 设置网络 / Set up Wi-Fi in Tikpal」，没添加「先在 Tikpal App 里添加 / Add me in the Tikpal app first」；`yk_app_display_name("yay")` 回「Tikpal」。
- **其他**：`yaykit_link` 能不带蓝牙编（`CONFIG_BT_ENABLED=n`），这时 `yk_link_ble_start()` 回 `ESP_ERR_NOT_SUPPORTED`；`yaykit_lan` 的 `yk_lan_config_t` 末尾加 `any_hello`（默认 false，照旧查令牌），文字帧的帧头只读一次，hello 不再被随机拒掉。
- **没变的**：`YK_API_LEVEL`（3）、分区表、`yay_dev.py`、ESP-IDF 版本。亮码（应用码）的接口不在这一版（[AGENTS.md](../AGENTS.md) 第 5 节）。没重编的老应用照旧跑：没有电量保护，设置自己读。
- 头文件注释里，共享设置和电量保护标的是「0.1.13」：以开发包为准，1.6 已经带上。它们在你自己的镜像里，跟圆屏上装的主固件是哪一版无关。

## 10. 测试

- **主机测试**：逻辑写成不依赖 ESP-IDF 的纯 C，`test/run.sh` 在电脑上跑。`./kit build` 每次都跑。
- **真机**：`./kit install --slot 2` → 用起来 → 双击开应用环、点中间回到主固件 → 主固件里再双击开环、点你的应用回来。
- **截图**：`./kit snap`，对照设计规范。
- **日志**：`./kit logs`。`YAY_APP` 开头的是 `yaykit_app` 打的（打开、离开、结果、记录）。
- 做完照[兼容检查清单](checklist.md)走一遍。
