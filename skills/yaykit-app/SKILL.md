---
name: yaykit-app
description: tikpal-app 的旧名字，内容一样，开发包 1.4 起留一版。用 Tikpal 开发包做 Tikpal Voice 应用（扩展固件）的规矩和做法。在这个应用仓库里写代码、改界面、加功能、编译、写进测试圆屏、看日志和内存、自测、交审核、看审核进度和拦下的问题（./kit submit、./kit status，或者开发者平台的 MCP）时使用。Use for any work in a Tikpal app repository made by `kit new`: the round screen extension's code, UI, side key, launch parameters and results, cloud calls, building with ./kit, installing over USB, self-testing, submitting and following the review (./kit submit, ./kit status, or the developer platform's MCP).
---

# Tikpal 应用

这个仓库是一个 Tikpal Voice 应用（扩展固件）：装在圆屏的位置 2 或 3，由 Tikpal 主固件打开；用户双击侧键开应用环、点中间回到主固件。同一时间圆屏上只跑一个固件，打开和回去都是重启。

## 先读（按需要）

| 要做的 | 读 |
|---|---|
| 任何开发 | `devkit/docs/app-guide.md`（资源归谁、启动顺序、启动参数和结果、侧键、组件速查、内存） |
| 界面 | `devkit/docs/round-screen-design.md`（颜色、字号、安全圆、提示、文案） |
| 应用清单 | `devkit/docs/app-json.md` |
| 流程、自测、提交 | `devkit/AGENTS.md`（先读）、`devkit/docs/checklist.md` |
| 某个组件怎么用 | `devkit/firmware/components/<组件>/include/*.h`（头文件是准的）和组件的 README |
| 照着改的例子 | 本仓库一开始就是模板：`main/main.c` 的启动顺序、侧键、收尾都是对的 |

## 流程和不能做的事

从建应用到上架的七步、每步交什么、提交包怎么打、怎么交给开发者平台 developer.tikpal.ai、怎么看进度，见 `devkit/AGENTS.md`，照它做。开发包仓库不收 PR，也不收「立项」单、「提交」单。

### 不能做的事

从 `devkit/AGENTS.md` 第 2 节原样抄来，两边一字不差（导出开发包时会核）：

1. 不改 `devkit/` 下的任何文件，包括公共组件；缺什么开「需求」单。
2. 不碰签名：不找、不要、不生成发布密钥，不自己打签名包。签名只在 Tikpal 的发布机上做。
3. 不在公开单子、提交说明、代码里写账号、邮箱、手机号、设备编号、令牌。日志用 `./kit logs`、`./kit info` 出来的，设备编号已经抹掉了。
4. 版本号只往上加：每次交的都比发出去过的大（被退回的那一版可以原样再交）；USB 写进测试机的版本也不能比应用市场上的低。不然 Tikpal Voice 查更新时会把它当成旧版，一点更新就换成应用市场上的版本。
5. 开发者平台「能力和权限」页上批下来以前，不开始用到那项能力的开发。不用它的部分可以先写。读共享文件夹的几项（「读音乐文件夹」……，开发包 1.10 起）例外：申请了就照用，交审时由 AI 审核批（`app-json.md`「共享文件夹」）。
6. 不给这个仓库开 PR。
7. 不擦整片 Flash，不写位置 1（主固件）和恢复器，只用 `./kit install --slot 2|3`。
8. 不写死服务器地址和密钥；云能力只走开发包的接口。

## 硬规则

1. **`devkit/` 只读。** 它是开发包（git 子模块），钉在一个版本上。不改、不复制一份改。组件缺什么，记下来告诉用户，在开发包仓库开「需求」单，由 Tikpal 做进下一版开发包。
2. **不擦圆屏。** 只用 `./kit install --slot 2|3` 写一个位置；不用 esptool 整片写、不擦 Flash，不碰位置 1（Tikpal 主固件）和恢复器。
3. **`yk_app_start()` 是 NVS 之后第一个调用**；画好主界面、控制台起来以后 `yk_app_ready()`。
4. **侧键**：用 `yk_app_on_key()`，`yk_key_init` 开 `delay_tap`；双击留给系统（应用环）；语音对话（`voice.session`）目前只给 Tikpal 自己的应用；开发包的对话能力在设计中，`talks` 先不用设。不轮询 GPIO0。
5. **资源各有其主**：屏幕先 `board_lock()`；录音只走 `yaykit_audio`，放音只走 `yaykit_speak`（不碰 GPIO43）；Wi-Fi 只 `yk_wifi_start()`（不配网）；令牌只用 `yk_account_token()`；回主固件用 `yk_return_home()`，不直接改启动分区；TF 卡只经 `yaykit_tf`（不自己挂卡、不写 `/sdcard` 路径），四个共享文件夹只读，要在 `app.json` 写 `permissions.folders` 并申请。
6. **NVS**：只写自己的命名空间 `yk_<简称>`（简称 = slug 去掉连字符，`night-light` 就是 `yk_nightlight`；≤ 15 个字符，超了截到 15）；绝不 `nvs_flash_erase()`。
7. **名字**：项目名 `yaykit_<slug，连字符换下划线>`；记录的 kind、`yk cmd` 的命令名用去掉连字符的简称做前缀（`mytimer.session`），只用小写字母、数字和点。
8. **界面**：只用 `yk_round.h` 的主题色和字号（同一目录的 `yk_theme.h` 是生成它的底层令牌，数值和圆屏规范不完全一样，应用里别直接用），不写死颜色，不用蓝色；文字在半径 200 的安全圆里；正文 22–24（三行放不下可以用 20），说明不小于 16，不为塞字缩字；说人话，不显示错误码。开发包 1.10 起，点一下要按下和抬起在同一个按钮上、手指没挪出 10 px、按着不到 1.5 秒（`yk_ui_button()` 已经是）；自己用 `LV_EVENT_CLICKED` 的照 app-guide 第 6 节核；慢慢拖既不算滑也不算点。
9. **内存**：内部 RAM 从 ready 起的最低（`./kit info` 的 `heap.internalMinSinceReady`）不低于 40,000 字节，低于 60,000 字节在应用仓库的 README 里写原因；大东西放 PSRAM。
10. **云**：调之前 `yk_cloud_has()`；限额从 `/me.capabilities` 读；提示词和模型不写进固件；忽略不认识的字段。
11. **安全**：代码、日志、提交包里不出现密钥、令牌、账号、设备编号（`kit logs`、`kit info` 已经把设备编号抹掉了）。
12. **版本**：`app.json` 的 `version` 和 `CMakeLists.txt` 的 `PROJECT_VER` 一起改；`minYay`、`apiLevel` 照开发包（`./kit version`）。

## 常用

```bash
./kit build              # 编译 + 主机测试 + 镜像不超过 3.5 MB
./kit test               # 只跑主机测试
./kit install --slot 2   # USB 写进位置 2 并打开
./kit logs 60            # 日志；YAY_APP 开头的是 yaykit_app 打的
./kit snap               # 截图 snap.png，对照设计规范
./kit heap               # 内存
./kit info               # 设备、三个位置、heap.internalMinSinceReady
./kit build --release    # 提交用的那份
./kit login              # 开发包登录开发者账号（开发者在浏览器里点「允许」，30 天一次）
./kit submit --notes "…" # 打包、上传、交审，打出提交编号（--notes 给用户看）
./kit status             # 审核到哪了、下一步谁做；被退回时按文件和行列出拦下的问题
./kit mcp                # 打出把你接到开发者平台 MCP 的配置
```

## 交审、看进度

- 交审、看进度都走 `./kit` 或开发者平台的 MCP，不用让开发者去网页上点。MCP 能做的更多（建应用、改商店素材、传图标截图、改测试名单），令牌由开发者在网站上建，`./kit mcp` 打出接法；接上先调 `whoami`。
- 签开发者协议、建和收回令牌、注销账号、改登录邮箱只能开发者自己在网页上做。
- 商店素材（图标、截图、名字、一句话、介绍、使用要求）由开发者自己提供，平台不代做；缺了交不了，会列出缺什么。替他填可以，内容要是他给的或者他点过头的。
- 挂起、真机测、签名是 Tikpal 排队做的，可能要一两天，不是卡住了：别重交、别撤回，隔半小时以上看一次进度就行。
- 出错去哪看：
  - 包交不了：`./kit submit` 当场一条一条列出来，照着改再交。
  - 被退回、被挂起：`./kit status`（MCP 用 `get_findings`）列出的每一条，文件、行、规则、怎么改，以它为准，不要猜，不要只改第一条。
  - 没登录、登录过期、旧的登录：`./kit login`。MCP 说令牌不行、没权限：请开发者在网站上建一个新令牌。

## 做一个功能的顺序

1. 逻辑先写成不依赖 ESP-IDF 的纯 C（像 `main/counter.c`），在 `test/` 加主机测试。
2. 界面在 LVGL 任务里改（LVGL 定时器或回调里），或者拿着 `board_lock()`。侧键回调在侧键任务里跑，只设标志，别在里面画。
3. `./kit build`，`./kit install --slot 2`，用起来，`./kit snap` 对设计，`./kit heap` 看内存。
4. 双击开应用环回主固件，再从主固件打开回来，确认收尾（`yk_app_on_leave`）写的结果伙伴念得对。

## 汇报

用中文。说清改了什么、在哪测的（主机测试、真机、截图）、哪些没测。没上过真机就直说。
