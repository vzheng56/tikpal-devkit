# 扩展固件模板

新做一个扩展固件就从这里开始（[公共固件开发指南](../../docs/development/firmware-guide.md)第 6 节，开发文档第 6.10 节）。它是一个能直接装进应用位置的小应用：点屏幕加一，按住侧键清零，短按侧键息屏、再短按亮屏，**双击侧键开应用环、点中间回到 Tikpal 主固件**。

## 照着改

1. 复制这个目录到 `firmware/<slug>`。
2. `CMakeLists.txt`：`project(yaykit_template)` 改成 `project(yaykit_<slug，连字符换下划线>)`，位置和商店都靠这个名字认应用。
3. `main/main.c`：`APP_SLUG` 改成你的 slug；启动横幅自动是 `YAYKIT_<SLUG>_READY`。
4. `app.json`（应用清单，契约是仓库根目录的 `packages/contracts/src/app-spec.ts`）：改 `slug`、`version`（和 `PROJECT_VER` 一样）、名字、能力、打开方式、场景；版本号跟固件一起改。只写这一版固件真做得到的：固件不读的启动参数不写进 `launch.params`。`launch.examples` 每种语言都要有两类：点了名的（「打开计数器」），和没点名、只说想做什么的（「帮我数数」），伙伴找应用主要靠后一类。
5. 把 `counter.c` 换成你的逻辑。逻辑写成不依赖 ESP-IDF 的纯 C，`test/run.sh` 在电脑上测。
6. 构建：`bash scripts/build-template.sh` 照着改一份。只编不刷。

## 已经替你做好的

- 启动顺序：NVS（出错不擦）→ 板子 → 侧键 → `yaykit_app` → Yaykit Link → 界面 → `yk_app_ready()` → 控制台。
- `yaykit_key`：按住、短按交给你的代码处理，但意思照圆屏规范（圆屏设计规范第 6 节，开发包里是 `docs/round-screen-design.md`）：短按停下或返回上一层，主界面息屏；按住用来说话，或者结束练习。双击归系统（应用环）。模板只有主界面，所以短按息屏、再短按亮屏（息屏后只有侧键叫得亮）；按住清零，算结束这一轮；加一放在点屏幕上。要自己用双击，就在 `yk_app_config_t` 里设 `own_double`，并另外给一个回到主固件的办法。
- `yaykit_app`：启动横幅、确认镜像、读主固件带来的启动参数（`yk_app_launch`）、退出前留一句结果（`yk_app_set_result`）、`yk cmd app.exit` / `app.recovery` / `app.status`、控制台 `app`，开发版还有 `yay tap|double|hold|say|launch`。
- 点一下的规矩（开发包 1.10 起，重编就有；Tikpal 主固件自己从 0.1.18 起）：手指在按钮上按下、在同一个按钮上抬起，中间没挪出 10 px、按着不到 1.5 秒，才算点一下；按下后挪出去再抬起、在别处按下挪进来抬起、慢慢拖，都不算。模板的按钮用 `yk_ui_button()`，已经照这条判，代码不用改。自己用 LVGL 的 `LV_EVENT_CLICKED` 判点击的，也照这条核（LVGL 的 CLICKED 只认按下的那个东西，手指在哪儿抬起都发）：模板「点屏幕加一」就是这样做的，见 `main/main.c` 的 `on_screen()`。
- 应用码（接口级别 4）：屏幕下面「在手机上打开」亮这个应用的页面码（`yk_vc_show()`，动作 0），Tikpal App 一扫就打开它的应用页；码亮着时屏幕不熄，短按侧键或点码收起。这个码从顶边往下拉的系统下拉里也有，模板放这个按钮是给你看怎么亮码，用不上就删掉。编号由构建从 `app.json` 的 `voiceCode.number` 读进镜像：模板写的是本机测试号 65280，拿到正式编号换掉。用法见开发包 `docs/voice-code.md`。
- `yaykit-v1` 分区、带回滚；开机不做 PSRAM 自检（每次切换都快 0.4 秒）；蓝牙默认关。
- 联网快一点：开了 `CONFIG_LWIP_DHCP_RESTORE_LAST_IP`，每次切换回来直接要上次的地址，不走发现、提供那一轮。模板自己不联网，加了 `yaykit_wifi` 才用得上。
- 颜色用圆屏的深色那组；`yaykit_ui` 的主题做好后改用主题。

发布前过一遍指南第 6 节的兼容检查清单。
