# 应用开发指南

写给做 Tikpal Voice 应用（扩展固件）的人和 AI。完整流程（七步、交什么、不能做的事）见 [AGENTS.md](../AGENTS.md)。照这份做，你的应用才能和 Tikpal 主固件、和别的应用配合好。

组件的头文件（`devkit/firmware/components/*/include/`）是准的：这份文档和头文件对不上时，以头文件为准，并告诉我们改文档。

## 1. Tikpal Voice 和位置

**Tikpal Voice**：ESP32-S3（双核 240 MHz）、32 MB Flash、8 MB PSRAM、1.75 寸 466×466 圆形 AMOLED 带触摸、两路麦克风、扬声器、六轴姿态（QMI8658）、一个侧键、TF 卡、电池。

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
3. 回去：双击侧键开应用环 → 点中间（开发包 1.9 是下面写着「回到 Tikpal」，1.10 起按下时中间写「回到 Tikpal」）→ 「正在回到 Tikpal」→ Pip 接着说一句（「计时完成了吗？」），用的是你留下的结果。

**你的应用必须和 Tikpal 主固件一致的地方**（细节见[Tikpal Voice 应用设计规范](round-screen-design.md)）：

| 方面 | 要求 |
|---|---|
| 颜色 | 只用 `yk_round.h` 的主题色（同一目录的 `yk_theme.h` 是底层的设计令牌，数值和设计规范不完全一样，别直接用）（底 `#171C19`、珊瑚 `#DF795D`、鼠尾草 `#9FB38C`、文字 `#FAF7EF` / `#A5AA9E` / `#737A70`），不写死颜色；要沉浸感可以纯黑底。**不用蓝色** |
| 字 | 共享字体（`cjk_font_get(px)`）；字号用 `yk_round.h` 的常量：正文和字幕 22–24（三行放不下时可以用 20，`YK_SIZE_SUBTITLE_SMALL`），状态行 18，提示和说明 16（`YK_SIZE_MIN`，不再更小）；不为塞字缩字 |
| 布局 | 文字和能点的东西放在半径 200 的安全圆里（`YK_R_CONTENT`）；顶部状态 y 46–70，底部提示 y 390 |
| 侧键 | 按住、短按交给你的代码处理，但意思照[设计规范](round-screen-design.md)：短按停下或返回上一层，主界面息屏；按住用来说话，或者结束练习。双击归系统。 |
| 说话的样子 | 在听用鼠尾草外圈，等用户用珊瑚色；说人话，不显示错误码 |
| 伙伴这个角色 | 要出现就用 `yk_yay` 画，不自己画别的角色。1.10 起 `yk_yay` 画的就是用户选的伙伴（Pip、Mote、Dial、Bloom），不用写代码；只有应用本身要展示某一个伙伴时，才用 `yk_yay_buddy()` 指定（`yk_yay.h`，第 9 节 1.10）。界面上要说名字用 `yk_ui_buddy_name()` |
| 网络 | 不做配网；没网时提示「回 Tikpal 设置网络」（`yk_notice_net()`） |
| 语言 | 跟着账号语言：界面文字做中文、英文两套（`yk_locale.h`：`zh` 开头的是中文，别的都按英文）；日文、西班牙文账号现在显示英文 |

## 3. 资源归谁

一个固件里，每样资源只能有一个组件去碰：

| 资源 | 归谁 | 你怎么用 |
|---|---|---|
| 屏幕、触摸、LVGL | `yaykit_board` | 改界面前 `board_lock()`，改完 `board_unlock()`；或者在 LVGL 回调里改 |
| 录音 | `yaykit_audio` | 不碰 I2S、编解码芯片 |
| 放音、功放（GPIO43） | `yaykit_speak` | 不直接拉 GPIO43。响度不用管，什么都不用设：说话的声音（`yk_speak_opus` / `yk_speak_pcm` / `yk_speak_clip`、`yk_cloud_tts`、`yaykit_talk` 的回答）由组件按 Tikpal Voice 的喇叭和功放自动处理，跟着音量档走，不要自己再放大。这几个接口只放说话，音乐别走它们，会被削薄、压扁。音效和音乐（`yk_sfx_play_*`）不经这一步，用 `gain` 调你自己的混音；短提示音用 `yk_speak_chime`，或者 `yk_speak_cue_pcm` 放你自己的。这些都有限幅保护，峰值不会超过喇叭的上限 |
| 侧键（GPIO0） | `yaykit_key`、`yaykit_app` | 订阅事件（`yk_app_on_key`），不轮询 GPIO0 |
| Wi-Fi | `yaykit_wifi` | `yk_wifi_start()` 用已有的网络，不配网 |
| 蓝牙 | `yaykit_link` | 平时关着；配对、安装、对话不同时进行（内部 RAM 不够）。例外：不配对的蓝牙口（1.11 起，要申请，暂未开放，第 9 节「1.11」）在你的应用开着时一直开，回 Tikpal 就断；它和联网也不同时开：有网时先取钥匙，再开蓝牙 |
| 账号、令牌 | `yaykit_cloud` | 只用 `yk_account_token()`，不读别人的 NVS。`yaykit_account.h` 里管绑定的那几个（`yk_account_command()`、`yk_account_register_console()`、`yk_account_recover()`、`yk_account_release*()`、`yk_account_binding_stored()`、`yk_account_unbound_by_server()`、`yk_account_owner_seen()`、`yk_account_locale_seen()`）只有系统用，应用调了审核不过，哪一版开发包编的都一样 |
| 启动分区 | `yaykit_app` | 回主固件用 `yk_return_home()`，不直接调 `esp_ota_set_boot_partition` |
| 姿态（QMI8658） | `yaykit_motion` | 取事件（`yk_motion_take` 或 `on_event`）、读状态（`yk_motion_state`），不自己读传感器；`permissions.device` 写 `motion`。见下面「姿态」 |
| 振动马达（GPIO46） | `yaykit_haptic`（1.11 起） | `yk_haptic_play()` 放五种振法，多强由用户的设置定；不自己用 `gpio_*`、LEDC 碰 GPIO46；`permissions.device` 写 `vibration`。见下面「振动」 |
| 电量、USB 供电 | `yaykit_board` | `board_battery()`；电量太低时睡下去归 `yaykit_app`（1.6 起，见第 9 节），你不用自己做 |
| 音量、亮度、息屏时间、伙伴 | `yaykit_board`（全设备共用，存在 `yk_pref`） | 经 `yaykit_app` 用（1.6 起）：`yk_app_brightness()`、`yk_app_screen_off_s()`、`yk_app_volume()` / `yk_app_set_volume()`，跟主固件的设置走，低电量的上限也算在里面；应用里调了音量用 `yk_app_set_volume()` 写回。`board_pref_*()` 照旧能用，只是不带低电量的上限。伙伴只读（`yk_ui_buddy_name()`）；伙伴图包在 `models` 分区里（伙伴区、Pip 格），只归主固件写，应用写它过不了审 |
| TF 卡 | `yaykit_tf`（1.10 起） | 自己的目录读写（要写 `tf_card`）；四个共享文件夹只读，要申请（`permissions.folders`）。见下面「存储卡和共享文件夹」 |

**NVS**：你的设置放在自己的命名空间 `yk_<简称>`。简称就是 slug 去掉连字符（`night-light` 的简称是 `nightlight`，命名空间 `yk_nightlight`），记录种类、命令名前面的也是它；NVS 最长 15 个字符，超了就截到 15。`./kit check` 和审核都照这个核，`nvs_open` 打开别的名字会被拦下。别的命名空间只读或者不碰（`yk_sys` 归 `yaykit_app`，`yk_pref` 用 `yk_app_*()` 或 `board_pref_*()`）。**绝不调用 `nvs_flash_erase()`**：NVS 出错时记日志、照常跑。

### 存储卡和共享文件夹

开发包 1.10 起，Tikpal 主固件 0.1.19 起。TF 卡上你能碰两样，都只经 `yaykit_tf`：自己的目录，和用户拷进来的四个共享文件夹。不自己挂卡，代码里不写 `/sdcard` 开头的路径（[审核规则 F-08](review-rules.md)）。

- **接进来**：`main/CMakeLists.txt` 的 `REQUIRES` 加 `yaykit_tf`，项目的 `EXTRA_COMPONENT_DIRS` 加它的目录；`sdkconfig.defaults` 加 `CONFIG_FATFS_LFN_HEAP=y`、`CONFIG_FATFS_ALLOC_PREFER_EXTRAM=y`（少了编不过），读用户的文件再加 `CONFIG_FATFS_MAX_LFN=255`、`CONFIG_FATFS_API_ENCODING_UTF_8=y`。
- **例子**：`firmware/examples/shared-folders` 能直接编：点一下问能不能读 Music，列出来、读第一首的前 4 KB，再往自己的目录写一个文件。照着抄，别从它起应用：起应用还是 `./kit new`。
- **都不在屏幕、音频任务上调**：一次可能要一秒以上。放在自己的任务里，那个任务留出 4 KB 栈给它。
- **离开时关文件**：双击离开时收尾（`yk_app_on_leave`）只有 400 ms，到点就重启，不管卡上做到哪；写到一半被切，卡上可能留下一个坏文件。收尾里先置一个标记，从此卡上的事不再起头，正在做的那一步等它关文件，最多等 300 ms 左右。所以一次只读一小块（几 KB），写也要短。例子里的 `card_begin()`、`card_end()` 和 `on_leave()` 就是这一套。
- **蓝牙开着用不了卡**；卡也不一定插着。拿不到就照没有卡做，不用另外告诉用户。

**你自己的目录**（`yaykit_tf.h`，`permissions.device` 写 `tf_card`）：

- 在 `yk_tf_dir()`，就是 `/sdcard/apps/<slug>`；路径用 `yk_tf_path("sub/a.txt", …)` 拼，名字只用 `A-Z a-z 0-9 . _ -` 和 `/`，最长 200 字、8 层。
- **用时挂、用完卸**：`yk_tf_acquire()` 挂上，`yk_tf_release()` 卸下。先关文件再卸，`yk_app_on_leave()` 里也要关。
- **配额**：1 GB 或卡容量的八分之一，取小的。`yk_tf_space()` 报占用，不替你拦，自己别超。
- **插电脑当 U 盘时**，你的目录跟系统区一样藏起来、设成只读，只防手滑：电脑上打开「显示隐藏文件」就看得到、拷得走。不想让人从电脑上拷走的（比如待传的录音），自己加密。
- 只认 FAT32，别的格式挂不上，也不会被格式化。

**四个共享文件夹**（`yk_folder.h`，`permissions.folders`）：

- 卡根目录下的 `Music`、`Stories`、`Pictures`、`Documents`，用户插上电脑拷进来的。**只能读**，不写、不删、不改名。
- **先在 `app.json` 写**要读哪类和用途，在开发者平台申请（[app-json.md](app-json.md#共享文件夹-permissionsfolders)）。没写的那类读不到（`YK_FOLDER_NOT_DECLARED`），不弹问话。只读共享文件夹不用写 `tf_card`；卡由组件自己挂卸，不用 `yk_tf_acquire()`。
- **读之前问**：`yk_folder_ask(YK_FOLDER_MUSIC, cb, ctx)`，在用户要用的时候（点了播放、选了一首），不在启动时、不定时、被拒以后不隔一会儿又问。用户没回答过的，Tikpal Voice 上弹出系统的问话，盖住你的界面：应用名、「想读你的〈几类〉」、「允许」「不允许」，四种语言，不显示你写的用途。`cb` 只来一次，在 `yaykit_tf` 的任务上。
- **六种回答**：
  - `YK_FOLDER_ALLOWED`：读。
  - `YK_FOLDER_DENIED`：用户点了不允许，或者在手机上关了；改回来之前哪类都读不到。刚点不允许时系统自己提示 3 秒去手机的「应用权限」；以后再问直接回这个，要提示就你说，用系统那句「想用了，到手机的「应用权限」里打开」。
  - `YK_FOLDER_NOT_NOW`：短按了侧键，或者 30 秒没点。不算不允许，下次用户要用时再问，别叫他去手机。
  - `YK_FOLDER_NOT_DECLARED`：`app.json` 没写这一类。
  - `YK_FOLDER_NO_CARD`：没插卡，或者卡读不了。
  - `YK_FOLDER_USB`：卡正在给电脑当 U 盘，推出以后才行。系统的说法是「先在电脑上推出 U 盘」。
- 用户在手机上改了主意，你下次 `yk_folder_ask()` 时才知道。
- **读**：`yk_folder_list()` 列出一个文件夹（以点开头的、隐藏的、系统的不列），`yk_folder_open()`、`yk_folder_read()`、`yk_folder_seek()`、`yk_folder_close()` 读文件，最多同时开 4 个，关了最后一个组件就把卡放了。列表和读的缓冲放 PSRAM。蓝牙开着、卡在当 U 盘时回 `ESP_ERR_INVALID_STATE`。
- **文件名是用户起的**：UTF-8，中文、日文、什么文字都有，每一级最长 255 个字，最多 8 层。照原样画，字库里没有的字屏上是「□」，文件照常读。
- **解码自己带**：MP3、PNG 这些组件不解，只给字节。
- **往外传要写隐私**：读到的内容经 `yk_cloud` 上传、或者给小程序看，`privacy.collects` 写 `files`。

### 姿态

Tikpal Voice 里有六轴传感器（QMI8658），只经 `yaykit_motion` 用，不自己读 I2C。组件报事件（拿起、放下、摇、扣下……）和现在的状态（重力、线加速度、倾角）；拿起亮屏、扣下变安静这类反应，组件不替你做，自己写（自家几个应用的规则也各不一样）。

- **接进来**，两行 CMake：项目 `CMakeLists.txt` 的 `EXTRA_COMPONENT_DIRS` 加 `"${YAYKIT_FIRMWARE}/components/yaykit_motion"`，`main/CMakeLists.txt` 的 `REQUIRES` 加 `yaykit_motion`。模板不带它。
- **`app.json` 的 `permissions.device` 写 `motion`**（[app-json.md](app-json.md)）。申请就批，不用等人看。
- **启动**：在 `board_display_start_opts()` 之后调 `yk_motion_start()`（传感器和触摸在同一条 I2C 上）。返回 `ESP_ERR_NOT_FOUND` 是没找到传感器：照常跑，只是没有姿态，别因此报错或退出。
- **56 Hz 还是 224 Hz**（`yk_motion_config_t.odr_hz`，0 就是 224）：
  - 224 Hz：要摇、要每帧拿状态推东西（骰子、滚珠、水平仪）、要拿的人方向跟得紧，用这一档。组件每 4 ms 读一次传感器（没用 FIFO，每次约 0.5 ms 的 I2C，跟触摸共用总线）。
  - 56 Hz：只要扣下、翻回、拿起、竖放哪边朝上，用这一档，每 16 ms 读一次，读得少四分之三。省多少电还没量过。
  - 判定的阈值只在 224 Hz 的录音上测过；56 Hz 下摇准不准还没核，要摇就用 224 Hz。

**取事件，两种办法二选一**。设了 `on_event`，事件就不进队列，`yk_motion_take()` 一直取不到。

```c
#include "yaykit_motion.h"

// 一、轮询：在界面的定时器里每帧取（LVGL 任务里，可以直接改界面）
static void tick(lv_timer_t *t) {
    yk_motion_event_t ev;
    while (yk_motion_take(&ev)) {  // 队列 16 条，满了丢最老的
        switch (ev.kind) {
        case YK_MOTION_FACE_DOWN: go_quiet(); break;
        case YK_MOTION_FACE_UP:
        case YK_MOTION_LIFT: wake_up(); break;
        default: break;            // 一定留着：以后还会加事件
        }
    }
    yk_motion_state_t st;
    yk_motion_state(&st);          // 现在的状态，哪个任务都能调
}
yk_motion_start(&(yk_motion_config_t){.odr_hz = 56});

// 二、回调：在姿态任务里调，只记下来，到 LVGL 任务里再用
static volatile float s_shake = -1;  // 姿态任务写，界面任务读
static void on_motion(const yk_motion_event_t *ev, void *ctx) {
    switch (ev->kind) {
    case YK_MOTION_SHAKE_START: s_shake = ev->strength; break;
    default: break;
    }
}
static void tick2(lv_timer_t *t) {
    const float s = s_shake;
    if (s >= 0) s_shake = -1, next_card(s);  // 改界面、放声音都在这里
}
yk_motion_start(&(yk_motion_config_t){.on_event = on_motion});
```

**回调的规矩**：`on_event` 和数据流 `yk_motion_stream()` 的回调跑在组件自己的姿态任务里（核 0，优先级 9，每 4 ms 就要读下一次传感器）。回调里只把要的东西记进变量就返回：不碰 LVGL、不 `board_lock()`、不放声音（`yk_sfx_play_*` 要等混音锁，`yk_speak_chime()` 要等声音放完）、不联网、不写 NVS。到 LVGL 的定时器里再用。

**事件**（默认阈值；`switch` 里留 `default`，以后还会加）：

| 事件 | 什么时候报 | 带什么 |
|---|---|---|
| `YK_MOTION_FLAT` 平放 | 玻璃法线跟竖直夹角 < 25°，持续 500 ms | — |
| `YK_MOTION_UPRIGHT` 竖放 | 夹角在 60°–120°，持续 500 ms；哪边朝上变了（按 90° 取整）再报一次 | `angle`：哪边朝上，0 是正着，180 是倒着 |
| `YK_MOTION_LIFT` 拿起 | 从静止开始，线加速度 > 0.15 g 或转速 > 25 dps 持续 80 ms，或者倾角变了 20° 以上；两次之间至少 2 秒；屏幕朝下时不算；`yk_motion_start()` 以后先要静止过一次（拿在手里打开应用不算拿起） | — |
| `YK_MOTION_PUT_DOWN` 放下 | 拿起或摇过之后，又静止满 300 ms | `angle`、`confidence`：开始静止前 0.5 秒拿的人方向（见下面） |
| `YK_MOTION_SHAKE_START` 摇开始 | 1 秒内至少 3 个 > 0.8 g 的线加速度峰，相邻两个方向相反 | `strength` 0–1：最近 250 ms 线加速度的均方根，0.5–3 g 对到 0–1 |
| `YK_MOTION_SHAKE_END` 摇结束 | 400 ms 没有峰 | 现在不带力度 |
| `YK_MOTION_FACE_DOWN_PROBE` 扣下·试探 | 屏幕朝下（向上的分量 < −0.75 g）满 250 ms | 适合先把声音放轻 |
| `YK_MOTION_FACE_DOWN` 扣下 | 朝下满 600 ms | 适合停声音、熄屏 |
| `YK_MOTION_FACE_UP` 翻回 | 试探或扣下以后，向上的分量 > −0.30 g 满 400 ms | — |
| `YK_MOTION_MOVING` / `YK_MOTION_STILL` 在动 / 不动了 | 最近 1 秒里有没有 > 15 dps 的转动或 > 0.08 g 的线加速度 | — |

每个事件都带 `t_ms`（`esp_timer` 的毫秒）。阈值每项都能改：`yk_motion_default_thresholds(&th)` 取默认，改了放进 `cfg.thresholds`，跑着的时候用 `yk_motion_set_thresholds()`；名字和默认值见头文件的 `yk_motion_thresholds_t`。

**状态**（`yk_motion_state()`）：

- 屏幕坐标：x 向右、y 向下（跟像素一样）、z 从玻璃朝外。重力朝下：玻璃朝上平放是 (0, 0, −1) g。屏幕上的角度从正上方（12 点）顺时针算，90 是右边。
- `now.gravity`、`now.lin`（线加速度，已经去掉重力）、`now.gyro`；`tilt_deg`（0 平放、90 竖着、180 扣着）、`up_angle`；`flat`、`upright`、`face_down`、`still`（静止满 300 ms）、`moving`、`shaking`、`shake_strength`。
- 读的是最新的一个样本。一帧 33 ms 里有七八个样本，两帧之间很短的一下（比如敲一下）会漏；要每个样本都看，用数据流 `yk_motion_stream()`（每 20 ms 一批，只能一个听者，回调规矩同上）。

**拿的人方向**（`yk_motion_holder(window_ms, &angle, &conf)`）：最近 `window_ms` 里倾角在 17°–70° 之间时，重力在屏幕平面上的分量指着斜向自己拿的那个人，取平均，可信度 0–1。这段时间里没人斜着拿（平放、竖着、扣着），给上一次的方向、可信度减半，**一段只减半一次**：接着问（每帧都问也一样）还是这个数，直到又算出新方向。从来没算出过返回 `false`。要的是「现在」谁拿着，自己再看 `tilt_deg`：比如记下最近一次倾角在 17°–70° 的时刻，超过 0.5 秒没有就不认。

### 振动

Tikpal Voice 里有一个振动马达（GPIO46），开发包 1.11 起只经 `yaykit_haptic` 用（`yaykit_haptic.h`）。先有声音，振动配在旁边：确认、拨动、出结果照样放声音，`yk_haptic_play()` 跟着振一下，不拿振动代替声音。

- **接进来**：项目 `CMakeLists.txt` 的 `EXTRA_COMPONENT_DIRS` 加 `"${YAYKIT_FIRMWARE}/components/yaykit_haptic"`，`main/CMakeLists.txt` 的 `REQUIRES` 加 `yaykit_haptic`。模板不带它。
- **`app.json` 的 `permissions.device` 写 `vibration`**，在开发者平台「能力和权限」申请「振动」，申请就批。代码调了下面这些函数却没写，`./kit check` 和审核都挡。
- **用**：`nvs_flash_init()` 之后，在栈放内部 RAM 的任务里（比如 `app_main`）调一次 `yk_haptic_init()`。`yk_haptic_play()` 放五种振法之一：`YK_HAPTIC_TICK` 点一下、`DOUBLE` 点两下、`SHORT` 短振、`LONG` 长振、`NOTIFY` 提醒；自己的振法用 `yk_haptic_pattern()`（开、关、开……，最多 16 段，开的加起来最多 2 秒）；`yk_haptic_stop()` 马上停。哪个任务都能调，中断里不行。
- **开不开、多强归用户**：用户在设置里定开关和三档强度，应用只说要哪一种。要看开没开、多强，用 `yk_haptic_available()`、`yk_haptic_get_settings()`。`yk_haptic_set_settings()` 是主固件设置页的，`yk_haptic_set_battery_low()`、`yk_haptic_flash()`、`yk_haptic_metering()` 是主固件专用的钩子：应用调了，`./kit check` 和审核都挡（[审核规则 F-08](review-rules.md)）。
- **这些时候不振**（`yk_haptic_play()` 回 `ESP_ERR_NOT_ALLOWED`，组件自己管，不用另做）：用户关了振动；麦克风开着（录音、按住说话：马达的嗡声会进麦克风）；正在装应用；电量低。马达关了还要转 150–250 ms，开麦克风的那一下别振。
- **GPIO46 不自己碰**：它是启动采样脚，复位时必须是低；离开、重启、电量低睡下前，组件都先停振。不用 `gpio_*`（`GPIO_NUM_46`、`BIT64(46)`、`1ULL << 46`）或 LEDC（`.gpio_num = 46`、`ledc_set_pin`）驱动它，`./kit check` 和审核挡。自己调 `esp_deep_sleep_start()` 的，先 `yk_haptic_stop()`。

## 4. 启动顺序

照模板（`main/main.c`）的顺序，按需取用，顺序不变：

1. NVS 初始化（出错不擦）。
2. `yk_app_start(&cfg)`：一定是 NVS 之后的第一个调用。它管开机崩溃保护（连续 3 次起不来就回主固件）、记下使用时间、接上双击开应用环、读共享设置。1.6 起它还先看一眼电池：没插电又快没电了，就不往下走，直接睡（第 9 节）。所以它要在屏、Wi-Fi 之前。
3. 板子：`board_display_start_opts(&(board_display_opts_t){.start_dark = true})`（屏幕先黑着，画完第一帧再亮）、`board_rtc_init()`（恢复时间）、要声音的再 `audio_init_rate(16000)`。
4. `yk_key_init(&(yk_key_config_t){.delay_tap = true})`，`yk_app_on_key(on_key, NULL)`。
5. `yk_app_launch(&launch)` 读启动参数；画第一帧，再 `board_brightness_set(yk_app_brightness())` 按主固件的亮度亮起来。
6. `yk_link_init()` 和控制台；画好主界面、控制台也起来以后 `yk_app_ready()`（确认这一版镜像，开始打启动横幅 `YAYKIT_<SLUG>_READY`）。
7. 要网络的：`yk_wifi_start()`；要云的：`yk_account_init()`。

**回滚**：新装的应用第一次启动是「待确认」。`yk_app_ready()` 之前崩了或重启了，Tikpal Voice 自动回到上一个能用的（也就是 Tikpal 主固件），Pip 会说「上次的应用没有正常启动」。

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
- 伙伴念这一句不扣用户的点数，费用平台出（2026-11-11 起）；平台有上限，用完就不念。所以商店里写「不用点数」的应用也可以留这句。
- 写下超过 30 分钟才回到主固件的，这句不念。
- 不留东西的应用（比如翻译）不写摘要。

### 记录（给平台的）

`yk_app_record(kind, data_json)`：一条记录，先放进 Tikpal Voice 上的发件箱，谁在线谁送，你不用自己联网。

- `kind` 只用小写字母、数字和点，3–41 个字符，前缀是你的简称：`mytimer.session`。
- `data` 是不超过 512 字节的 JSON 对象。
- 最多留 16 条，满了顶掉最旧的。

### 你自己的命令

`yk_link_init` 的 `on_command` 处理 `yk cmd <名字>`，名字也用你的简称：`mytimer.status`。先调 `yk_app_command()`，它处理 `app.exit`、`app.recovery`、`app.status`。

## 6. 侧键

```c
static bool on_key(const yk_key_event_t *e, void *ctx) {
    switch (e->type) {
    case YK_KEY_TAP:  /* 短按：停下或返回上一层，主界面息屏 */ return true;
    case YK_KEY_HOLD: /* 按住：说话，或者结束练习 */ return true;
    default: return false;
    }
}
```

- 返回 `true` 表示「这一下我用了」。
- 双击留给系统（应用环）。真要用双击，在 `yk_app_config_t` 里设 `own_double`，并且另给一个回到主固件的办法。
- **双击屏幕开应用环**（开发包 1.11 起，Tikpal 主固件 0.1.22 起，要自己打开）：在 `yk_app_config_t` 里设 `double_tap_ring = true`，双击屏幕就跟双击侧键一样开应用环，两下你都收不到。默认关：关着时屏幕上的每一下都是你的，双击也照常交给你自己处理，跟以前一样。
  - 怎么算双击：两下都是「点一下」（下面「屏幕上点一下」：抬起时离按下的地方 10 px 以内、没滑、按着不到 1.5 秒），第二下按下离第一下抬起不到 450 ms、离第一下按下的地方不到 40 px，中间没按过别的。
  - **开着时点一下会晚到**：系统要先等一下，看是不是双击，所以 `LV_EVENT_SHORT_CLICKED`、`LV_EVENT_CLICKED` 要晚约 450–500 ms 才到你的东西。是双击就开环，两下的这两个事件都不给你；不是，第一下的晚一点照常到，这时 `lv_indev_get_point()` 读到的还是抬起时的位置。按下（`LV_EVENT_PRESSED`）、抬起（`LV_EVENT_RELEASED`）、拖动，还有 LVGL 自己的 `LV_EVENT_SINGLE_CLICKED`、`LV_EVENT_DOUBLE_CLICKED`，照旧马上到。
  - 要即时点按的应用（抢答、节奏游戏、快速连点的小游戏这类）别开这个开关；关着时双击照常交给你自己处理。
  - 环开着时：点环上的某一项，马上打开它；点空处，马上关环，紧跟着的第二下谁也不给，所以再双击一下也是关环。侧键下一按也收起。
  - 这些时候不算双击：应用环、下拉、「回到主屏幕说？」、应用码、共享文件夹的询问在屏上；你自己按着侧键。点在 LVGL 认得的地方才算：你的屏幕本身，或者屏上能点的东西。
  - 跟 `own_double` 各管各的：`own_double` 只管侧键的双击。
- 你没用掉的按住，又没有自己的对话，系统会问「回到主屏幕说？」（不写伙伴的名字：用户的伙伴可能是四个里的任何一个）。息屏、倒计时这类时候按住有别的意思，就返回 `true`。
- 语音对话（`voice.session`）目前只给 Tikpal 自己的应用；开发包的对话能力在设计中，`yk_app_config_t` 的 `talks` 先不用设。
- `yk_key_init` 要开 `delay_tap`：不开的话，开应用环的那次双击，第一下会先当单击给你。

**屏幕上点一下**（开发包 1.10 起，重编就有；Tikpal 主固件自己从 0.1.18 起）：

- 手指在一个按钮（一格、一个图标）上按下，又在同一个上抬起，中间没挪出 10 px、按着不到 1.5 秒，才算点一下。
- 这几种都不算点：按下以后挪出去再抬起；在别处按下、挪进来再抬起；按下不抬；慢慢拖。慢慢拖也不算滑，滑要快。
- `yk_ui_button()`、应用环、系统下拉都照这条判，重编就有，不用写代码。
- 自己用 LVGL 的 `LV_EVENT_CLICKED` 判点击的：
  - **开发包 1.11 起，重编就有**：你自己的按钮、格子、图标也照这条判。系统在触摸设备那一层先看一眼，不是点一下的，`LV_EVENT_CLICKED`、`LV_EVENT_SHORT_CLICKED` 就不发给你的东西。LVGL 自己的 CLICKED 是手指在哪儿抬起都发，拖过一个按钮再抬手也算按了它，1.11 以后不再这样。
  - 系统自己的应用环、拨盘、「我的应用」不受这条管，它们自己判。
  - 你的东西靠「拖完松手」的 CLICKED 做事（比如自己写的滑块在 CLICKED 里收尾），改到 `LV_EVENT_RELEASED` 里做。或者在 `yk_app_config_t` 里设 `loose_taps`，照旧用 LVGL 自己的 CLICKED。
  - 1.10 及以前编的应用没有这条：在 `LV_EVENT_PRESSED` 记下按在哪个东西上、在哪、什么时候，抬起时再核一遍。

## 7. 组件速查

只列做应用常用的；完整的接口看头文件。

| 组件 | 做什么 | 常用 |
|---|---|---|
| `yaykit_app` | 生命周期、启动参数、结果、记录、应用环、共享设置、电量保护 | `yk_app_start`、`yk_app_ready`、`yk_app_launch`、`yk_app_on_key`、`yk_app_on_leave`、`yk_app_set_result_summary`、`yk_app_record`、`yk_return_home`、`yk_app_brightness`、`yk_app_screen_off_s`、`yk_app_english` |
| `yaykit_board` | 屏幕、触摸、时钟、音频芯片、电量、共享设置、字体 | `board_display_start_opts`、`board_lock`、`board_battery`、`board_pref_*`、`board_display_sleep/wake`、`cjk_font_get` |
| `yaykit_key` | 侧键状态机 | `yk_key_init`（用 `yk_app_on_key` 订阅） |
| `yaykit_ui` | 主题、边缘环、伙伴、字幕、提示、进出、截图 | `yk_round.h`、`yk_rim_*`、`yk_yay_*`（`yk_yay_buddy` 指定伙伴）、`yk_caption_*`、`yk_notice_*`、`yk_hint_*`、`yk_ui_buddy_name`、`yk_ui_register_snapshot` |
| `yaykit_link` | 设备 ID、USB 控制台、`yk cmd`；不配对的蓝牙口（1.11 起，要申请，暂未开放） | `yk_link_init`、`yk_link_register_console`、`yk_link_device_id`；`yk_link_keep_start_unpaired`、`yk_link_keep_send`、`yk_link_unpaired_key_state`、`yk_link_unpaired_refresh`、`yk_link_keep_mark` |
| `yaykit_wifi` | 用已有的 Wi-Fi | `yk_wifi_start`、`yk_wifi_wait_connected` |
| `yaykit_cloud` | 账号、令牌、设备公共 API | `yk_account_init`、`yk_cloud_me`、`yk_cloud_has`、`yk_cloud_asr`、`yk_cloud_chat`、`yk_cloud_tts`、`yk_cloud_data_*` |
| `yaykit_talk` | 语音对话：`voice.session` 目前只给 Tikpal 自己的应用，开发包的对话能力在设计中，先别用 | — |
| `yaykit_audio` | 录音，16 kHz，降噪、人声检测 | `yk_audio_init`、`yk_audio_start`、`yk_audio_rec_*` |
| `yaykit_speak` | 放音、Opus、音效混音 | `yk_speak_init`、`yk_speak_opus`、`yk_sfx_play_*`、`yk_speak_chime`、`yk_speak_cue_pcm` |
| `yaykit_motion` | 姿态：平放、竖放、拿起、放下、摇、扣下、在动，拿的人方向（见第 3 节「姿态」） | `yk_motion_start`、`yk_motion_take` 或 `on_event`、`yk_motion_state`、`yk_motion_holder` |
| `yaykit_haptic` | 振动：五种振法、自己的振法，强度跟用户的设置（1.11 起，见第 3 节「振动」） | `yk_haptic_init`、`yk_haptic_play`、`yk_haptic_pattern`、`yk_haptic_stop`、`yk_haptic_available` |
| `yaykit_recorder` | TF 卡上断电安全的录音 | `yk_rec_open/write/close` |
| `yaykit_tf` | TF 卡：自己的目录、四个共享文件夹（1.10 起，见第 3 节「存储卡和共享文件夹」） | `yk_tf_acquire/release`、`yk_tf_path`、`yk_tf_space`、`yk_folder_ask`、`yk_folder_list`、`yk_folder_open/read/close` |

**云能力的规矩**：

- 调之前先 `yk_cloud_has("<能力>")`；时长、字数、语言这些限额从 `/me.capabilities` 读，不写死。
- 提示词和模型不写在固件里：用服务端的模板名或场景接口。
- 解析返回时忽略不认识的字段和事件（接口只做加法）。
- 你的应用要哪些能力，在开发者平台这个应用的「能力和权限」页申请：花用户点数的（AI 对话、语音识别、语音合成……）、麦克风这类要人工批，我们通常 1 个工作日内回；读共享文件夹的几项（开发包 1.10 起）申请后是「待定」，交审时由 AI 审核批（[app-json.md](app-json.md#共享文件夹-permissionsfolders)），不用等着就能写；别的建应用时就开通。要加能力也在那一页申请。

## 8. 内存

- **内部 RAM 最低不能低于 40,000 字节**（低于 60,000 字节要在应用仓库的 README 里写明原因，审核看代码时会读）。看 `./kit info` 的 `heap.internalMinSinceReady`：从 `yk_app_ready()` 起算的最低，拿原数和 40,000 比，不换算成 KB。`./kit heap` 的 `internal_min` 是开机以来的老读数，偏低，不拿它比。
- 大东西放 PSRAM：图、音频缓冲、大任务的栈（`xTaskCreatePinnedToCoreWithCaps(..., MALLOC_CAP_SPIRAM)`）。
- 画伙伴（1.10 起）：用户选的不是 Pip 时，第一次画 `yk_yay` 读进它的图包，约 63 万字节 PSRAM，不占内部 RAM，这次运行一直留着；画 Pip、不画伙伴的应用一点不占。用 `yk_yay_buddy()` 指定别的伙伴时（比如图鉴），每读到一个伙伴的图包就各多留一份；伙伴区只存当前伙伴和上一个，所以最多两份，约 127 万字节 PSRAM。
- 用卡（1.10 起）：挂着约 760 字节内部 RAM，读写时再多约 4.9 KB（内部 RAM 紧时 1.3 KB）；FatFs 的缓冲在 PSRAM（`CONFIG_FATFS_ALLOC_PREFER_EXTRAM=y`）。不用时卸下，就不占。
- 用网络、TLS 的应用，`sdkconfig.defaults` 照主固件省内部 RAM：
  - `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`、`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`（TLS 和 Wi-Fi 缓冲进 PSRAM）；
  - `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`（4 KB 以下才放内部 RAM）；
  - 用蓝牙的：`CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y`。
- 用 Wi-Fi 的应用开 `CONFIG_LWIP_DHCP_RESTORE_LAST_IP=y`，切换回来直接要上次的地址；`kit new` 新建的已经开了。

## 9. 兼容和版本

- **`YK_API_LEVEL`**（`yaykit_app.h`）：公共组件给应用的接口版本，只增不改。开发包 1.0 到 1.8 都是 3；1.9 起是 4：应用能亮自己的应用码（[voice-code.md](voice-code.md)），从顶边往下拉归系统的下拉，应用环只放应用。按 3 编的应用不重编照旧跑。1.10 起是 5：`yk_yay` 画用户选的伙伴，能用 `yk_yay_buddy()` 指定；`yaykit_tf` 能读四个共享文件夹（`yk_folder.h`，第 3 节「存储卡和共享文件夹」）。1.11 起是 6：不配对的蓝牙口、振动。1.10 没有单独发，5 和 6 都随 1.11 给。按 4 编的应用不重编照旧跑，画的是 Pip。
- **`app.json` 的 `minYay`**：你的应用要求的最低 Tikpal 主固件版本，至少是开发包对应的那一版（`./kit version` 的 `yay`，`./kit check` 会核）。主固件比它旧的 Tikpal Voice，新装拿到的是你的上一版（它的 `minYay` 够得上的那一版），已经装着的照常用、不推新版；一版都够不上，就不给它列出你的应用。
- **`apiLevel`**：写开发包的 `apiLevel`。
- 开发包每次 Tikpal 主固件发版出一版。换开发包版本（`./kit update`）以后重编、重新自测，再提交。
- 你的 `main/main.c` 是建应用时从模板抄的，换开发包版本不会跟着变：模板改了哪里，下面的更新记录会写，要用就自己照着改。

### 更新记录

只记组件（`firmware/`）的变化和换版本要做的事；别的文档和工具的改动看开发包仓库的提交。

**1.11**（还没发；组件换成 Tikpal 主固件 0.1.22 的那份，含 0.1.18 到 0.1.22 里组件的改动。1.10 没有单独发，它的改动都在这一版里：文档里写「开发包 1.10 起」的，用 1.11 编就有）：

- **换过来要改 `app.json`**：`apiLevel` 写 6，`minYay` 至少写 `"0.1.22"`（`./kit version` 的 `yay`）。没改的 `./kit check` 不过，会告诉你该写多少。`YK_API_LEVEL` 是 6：1.10 的 5（伙伴、存储卡和共享文件夹）和 1.11 的 6（不配对的蓝牙口、振动 `yaykit_haptic`，权限 `vibration`）一起给，写了 `bluetooth_unpaired` 的应用 `apiLevel` 不到 6，`./kit check` 也不过。按 4 编的应用不重编照旧跑。
- **用 1.11 编的版本只给主固件 0.1.22 及以上的 Tikpal Voice**：主固件还没升上来的，新装拿到的是你的上一版（它的 `minYay` 够得上的那一版），已经装着的照常用、不推新版，等主固件升上来再更新。一版都够不上的（只用 1.11 编过的新应用），那几台先看不到它。
- **双击屏幕开应用环，要自己打开**（接口级别 6）：`yk_app_config_t` 的 `double_tap_ring`，默认关，关着跟以前一样；开了以后双击屏幕跟双击侧键一样开应用环，点一下要晚约半秒才到你的东西，要即时点按的应用别开。怎么判、什么时候不算，见第 6 节。
- **你自己的东西也只认点一下，重编就有**（F-1018）：你的按钮、格子、图标收到的 `LV_EVENT_CLICKED`、`LV_EVENT_SHORT_CLICKED`，也照第 6 节「屏幕上点一下」判：按下和抬起在同一个东西上、手指没挪出 10 px、按着不到 1.5 秒。拖过去再抬手、按下挪出去再抬手，都不再发 CLICKED。靠拖完松手的 CLICKED 做事的，改到 `LV_EVENT_RELEASED`，或者在 `yk_app_config_t` 里设 `loose_taps`，照旧用 LVGL 自己的。已经发出去的应用包不受影响：这条编在应用自己的镜像里，换 1.11 重编才生效。
- **不配对的蓝牙口（设备通道），要写代码才有，要申请，暂未开放**（接口级别 6；开放了这里会改，下面是开放以后的用法）：你的小程序经 Tikpal App 跟 Tikpal Voice 上你自己的应用说话，不用配对、不弹系统框。手机就在身边、Tikpal Voice 没网时用：导航每秒推下一步，手机当遥控、计分板。
  - 组件管全部：广播、蓝牙服务、钥匙、握手、加密。你的应用只收发「帧」：第 1 字节版本（1）、第 2 字节种类、后面是你的。种类 2 是组件自己的，别用；你自己的消息用种类 4，内容 1–86 字节（跟小程序的 `device.link.send` 对得上），每秒最多 10 条。一帧最多 160 字节，还要放得下当前的 MTU（MTU − 23）。
  - 有网时先取钥匙，再开蓝牙（`yaykit_link/include/yk_link_keep.h`）：
    ```c
    // 首页打开、有 Wi-Fi 时（内部 RAM 栈的任务，比如 app_main）：钥匙是平台按这台设备和你的应用发的，
    // 没有、过期、手机说对不上才真去取；钥匙本身你拿不到
    yk_link_unpaired_refresh(false);
    // 开始：同样从内部 RAM 栈的任务里调；你的应用编号组件自己从 app.json 的 voiceCode 读
    yk_link_keep_start_unpaired(&(yk_link_keep_unpaired_t){
        .nvs_ns = "yk_<简称>", .on_frame = on_frame, .on_event = on_event});
    // 发：任何任务；手机连上、开了通道（YK_KEEP_EV_TRUSTED）以后才发得出去
    uint8_t frame[] = {1, 4, 42};
    yk_link_keep_send(frame, sizeof(frame));
    // 离开：yk_app_on_leave 里
    yk_link_keep_end(300);
    ```
  - **`sdkconfig.defaults` 必须加 `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=5120`**（模板里有这一行，去掉 `#` 就行；多占约 1 KB 内部 RAM）：蓝牙主机任务的栈组件自己的握手和加解密就用掉约 3.4 KB，小于 5120 组件不开通道（回 `ESP_ERR_INVALID_STATE`，日志写原因），`./kit check` 也不过。
  - `on_frame`、`on_event` 在组件自己的任务 `yk_link_app` 里跑（栈 4 KB，在内部 RAM），一次一个、按先后；里面能调 `yk_link_keep_send()`。还是要短：最多攒 16 条，满了新来的帧丢掉（日志 `YK_KEEP app behind`）；画屏先 `board_lock()`，费时的交给你自己的任务。
  - 蓝牙地址组件自己管：你的应用在这台设备上有自己固定的一个，第一次开时生成，之后每次一样，手机记下后直接连回；设备解绑或换了主人时跟钥匙一起换。`nvs_ns` 照样填你的命名空间。
  - 手机靠「设备标记」在附近几台里挑出这一台：`yk_link_keep_mark()` 给你 20 位，放进你的应用码（比如导航的值是「标记 << 4 | 目的地」，见 [voice-code.md](voice-code.md)）。标记谁都看得见，不是钥匙。
  - 钥匙的状态 `yk_link_unpaired_key_state()`：没有（第一次没联过网）就提示「到有 Wi-Fi 的地方打开一次」；对不上（设备解绑又绑过）一样，下次有网时 `yk_link_unpaired_refresh()` 会自己重取。钥匙你拿不到，也不用存。
  - 手机那头钥匙对不上时：Tikpal Voice 回「钥匙不对」，或者手机解不开你的应用发出的通知，App 都当场断开、这把钥匙不再用，向平台重取一次（同一台设备一分钟最多一次），拿到不一样的钥匙才再连；拿不到就停，小程序看到 `key_mismatch`，或者停在 `lost` 等页面重新打开（[小程序指南](miniapp.md) 3.11）。所以你这边只会收到一次 `YK_KEEP_EV_DISCONNECTED`，手机不会拿旧钥匙一直重连。
  - 要有应用码：`app.json` 得写 `voiceCode`（[voice-code.md](voice-code.md)），组件用它的编号认你的应用、取你的钥匙；没写的 `start` 回 `ESP_ERR_NOT_SUPPORTED`。
  - `refresh` 和 `start` 都要在栈放内部 RAM 的任务里调（`app_main`、控制台命令都是）；栈在 PSRAM 的任务里调，回 `ESP_ERR_INVALID_STATE`，不会重启。
  - 规矩：`app.json` 的 `permissions.device` 写 `bluetooth_unpaired`，再到开发者平台「能力和权限」申请「不配对的蓝牙」，人看过才开通；用了它的应用**每一版**都有人看（[审核规则「需申请的能力：人看什么」](review-rules.md)）。代码调了 `yk_link_keep_start_unpaired()` 却没写 `bluetooth_unpaired`，`./kit check` 和审核都挡。组件里面的函数（`yk_link_keys_*`、`yk_seal_*`、`yk_rules_*`）应用不能调。
  - 内部 RAM：蓝牙开着要占一块（测试应用连着手机每秒一条时，从 ready 起最低还剩约 15 万字节），一次只做一件联网或配对的事；你的应用连着手机时照第 8 节量 `heap.internalMinSinceReady`。
  - 跟手机的连接（实测，红米 8A，和两台 Tikpal Voice 对测）：第一次约 2.4 秒能收发；手机锁屏、打盹也照常每秒一条，延迟中位一两百毫秒以内；手机断了再连回来不用重开你的应用；你的应用退出时组件马上断开，回来时手机 2–4 秒自己连回。
- **拿的人方向一段只减半一次，重编就有**：`yk_motion_holder()` 在没人斜着拿的那段时间里，原来每调一次可信度就减半一次（每帧都问的话 0.2 秒就到 0），现在一段只减半一次、之后一直是这个数，跟头文件写的一致。`PUT_DOWN` 事件的 `confidence` 照同一个规矩。拿可信度当「现在有人拿着」用的应用，照第 3 节「姿态」再看一下 `tilt_deg`。接口不变，`YK_API_LEVEL` 不因这条升。
- **点一下改了判法，重编就有**：`yk_ui_button()`、应用环、「我的应用」、设置环、调数值的环、系统下拉，都改成按下和抬起在同一个按钮或格子上、手指没挪出 10 px、按着不到 1.5 秒才算点一下（第 6 节）。按下后挪出去再抬起、在别处按下挪进来抬起、慢慢拖，都不再算点。调数值的环点一档，抬起时才跳过去；按着拖照旧跟手。接口不变，`YK_API_LEVEL` 不因这条升。自己判点击的应用照第 6 节核一遍。
- **四个伙伴，重编就有**（接口级别 5）：`yk_yay` 画的是用户选的伙伴（Pip、Mote、Dial、Bloom），不是写死的 Pip，应用的代码不用改。
  - 伙伴的图包由 Tikpal 主固件在后台下好，存在伙伴区。你的应用第一次画 `yk_yay` 时从伙伴区读一次（约十分之一秒、63 万字节 PSRAM），这次运行一直用它。
  - 用户在你的应用开着时换了伙伴，下次打开才换过来。
  - 这几种画 Pip：那个伙伴的图包不在 Tikpal Voice 上（主固件还没下好，或者主固件比 0.1.18 旧）；`yk_yay` 用的是 `YK_YAY_TRANSPARENT`（图包只画在不透明的底上）；PSRAM 不够。Pip 编在你的镜像里，一定在。
  - 图包里没有 `yk_yay_look_at()` 那种看人的眼睛，伙伴画出来时它不起作用；眨眼、嘴、换姿势、转角度照旧。
  - 只有应用本身要展示某一个伙伴时（比如图鉴、小游戏），才用 `yk_yay_buddy(yay, YK_BUDDY_MOTE)` 这样指定；`YK_BUDDY_CURRENT` 是用户选的那个，不认识的值也当它。名字是枚举 `yk_buddy_t`（`yk_buddy_slot.h`），以后有新伙伴只往后加。
  - 图包不在开发包里：你在自己电脑上、自己的 Tikpal Voice 上没下过别的伙伴时，看到的都是 Pip。
  - 伙伴区、Pip 格只归主固件写，应用写 `models` 分区过不了审。
- **存储卡和共享文件夹，要写代码才有**（接口级别 5）：`yaykit_tf` 进开发包，自己的目录读写（`yaykit_tf.h`），四个共享文件夹只读（`yk_folder.h`，要在 `app.json` 写 `permissions.folders` 并申请）。规矩和例子见第 3 节「存储卡和共享文件夹」、`firmware/examples/shared-folders`。用 1.11 编的应用自己挂卡、写 `/sdcard` 开头的路径、调系统的卡接口，`./kit check` 和审核都挡（[审核规则 F-08](review-rules.md)）。
- **用了姿态，`permissions.device` 要写 `motion`**：1.11 起 `./kit check` 和审核对着代码核 `permissions.device`。应用自己的代码调了 `yk_motion_start()`、`app.json` 的 `permissions.device` 却没写 `motion`，就挡，报 `调了 yk_motion_start()，用了动作感应，app.json 的 permissions.device 要写 motion，再到开发者平台「能力和权限」申请「动作感应」`；写了 `motion` 却没调的，只提一句，不挡。用 1.9 编的应用不受影响，已经上架的照旧；换到 1.11 编、交新版本以前补上两处：
  - `app.json` 的 `permissions.device` 加上 `motion`，比如 `"permissions": {"device": ["motion"]}`；原来写了别的硬件的，加进同一个列表。
  - 到开发者平台「能力和权限」申请「动作感应」，申请就批，不用等人看。写了没申请的，交审时平台会说这一项还没批。
- **系统问话不写伙伴的名字，重编就有**：你没用掉的按住，系统问「回到主屏幕说？」（英文 “Talk on the home screen?”），原来是「回到 Pip 说？」。四种语言跟着 Tikpal Voice 的语言走，`yk_ui_buddy_name()` 照旧给名字，你自己的界面要说名字还用它。
- **应用环和设置环换成大格子，重编就有**：两个环一样大，格子直径 108，只放图标，名字按下时报在中间；手指按住一格时它放大、别的变淡，抬起才打开。应用的图标照旧 64×64，目录格式不变。用了设置环（`yk_ring.h`）的应用：线条图标要按 `YK_RING_ICON`（80，以前 40）重画，还用 40 的图会画偏、不在格子正中；调数值的页 `yk_dial_view_t` 末尾多了 `title`，可以不填。别的接口不变，`YK_API_LEVEL` 不因这条升。

**1.9**（对应 Tikpal 主固件 0.1.14）：组件换成主固件 0.1.14 的那份，`YK_API_LEVEL` 升到 4。接口只加不改，按 3 编的应用不重编照旧跑。

- **换过来：一般升级开发包版本、改 `app.json`、重编就行。** `./kit update 1.9`，再改下面三处 `app.json`，重编、自测、提交。这一版的新东西都是重编就有，一般的应用 C 代码和 CMake 不用动，要改代码的两种见下一条。
  - `minYay` 至少写 `"0.1.14"`：`./kit version` 的 `yay` 是 0.1.14，`./kit check` 要 `minYay` 不比它旧。Tikpal Voice 上的主固件比 `minYay` 旧，商城不给它列出你的应用。
  - `apiLevel` 写 4。
  - `voiceCode` 必填：写建应用时给的编号（[app-json.md](app-json.md#应用码-voicecode)）。下拉里的「在手机上打开」靠它。
- **这两种应用要改代码**：
  - 读 `yk_acct` 的：`./kit check` 和审核都不过。语言换成 `yk_app_english()`（要语言代码用 `yk_app_locale()`），昵称用 `yk_account_nickname()`。
  - 自带下拉页的（自己做了一份状态页，往下滑打开）：删掉，系统下拉已经有这些，留着用户会看到两个下拉。自带页里读 `yk_acct` 的代码跟着删。
- **接口级别 4，亮码**：应用能亮自己的应用码，用 `yk_vc_show()`，用法见 [voice-code.md](voice-code.md)。通用动作是 0–2；开发包 1.11 起还能亮应用自己的动作 8–15，要先在 `app.json` 的 `voiceCode.own` 登记（voice-code.md 第 4 节）。`yaykit_voicecode` 默认链接：`yaykit_app` 依赖它，`firmware/reproducible.cmake` 加组件目录，CMake 不用改。自己调 `yk_vc_show()` 才要在 `main/CMakeLists.txt` 的 `REQUIRES` 加它。
- **系统下拉不能不用**：在 Tikpal Voice 上从顶边往下拉是系统的下拉（Wi-Fi、电量、手机连接这些状态），重编就有，不用写代码，下拉里的「在手机上打开」亮你的应用码。绕开它的写法，1.9 起过不了：
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
- 头文件注释里，共享设置和电量保护标的是「0.1.13」：以开发包为准，1.6 已经带上。它们在你自己的镜像里，跟 Tikpal Voice 上装的主固件是哪一版无关。

## 10. 测试

- **主机测试**：逻辑写成不依赖 ESP-IDF 的纯 C，`test/run.sh` 在电脑上跑。`./kit build` 每次都跑。
- **真机**：`./kit install --slot 2` → 用起来 → 双击开应用环、点中间回到主固件 → 主固件里再双击开环、点你的应用回来。
- **截图**：`./kit snap`，对照设计规范。
- **日志**：`./kit logs`。`YAY_APP` 开头的是 `yaykit_app` 打的（打开、离开、结果、记录）。
- 做完照[兼容检查清单](checklist.md)走一遍。
