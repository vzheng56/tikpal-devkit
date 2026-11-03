# AGENTS.md：给帮人写 Tikpal 应用的 AI

你在 **Tikpal 开发包**（Tikpal Devkit）里。它用来给 Tikpal Voice（一台带圆形屏幕的语音设备）做**应用**：扩展固件，装在设备的位置 2 或 3，由主固件打开，用户双击侧键开应用环回到主固件。

照这份做，能把一个应用从想法走到上架。七步的顺序是固定的，不要跳步。

## 1. 先知道的四件事

1. **这个仓库不收 PR。** 应用和改动都不收。
   - 应用放在开发者自己的 GitHub 仓库里（第 2 步 `./kit new` 会建好，开发包是它的 `devkit/` 子模块），只通过「提交」单交给我们。
   - 要改开发包（组件缺能力、工具有问题、文档不清楚），开「需求」单。开发包是从我们的主仓库导出的，这里合进去的改动会被下一次导出盖掉。
   - 开了 PR 会被自动关掉。
2. **单子是公开的。** 立项单、提交单、需求单谁都看得见。账号这类东西要私下给我们。
3. **我们回复都在单子上。** 状态看单子上的标签（第 9 节）。
4. **开发者协议定稿以前，只收 Tikpal 邀请的开发者。** 不在邀请名单里的立项单，我们会回「协议定稿后开放」，单子留着，协议公布时在单子上通知。

## 2. 你不能做的事

1. 不改 `devkit/` 下的任何文件，包括公共组件；缺什么开「需求」单。
2. 不碰签名：不找、不要、不生成发布密钥，不自己打签名包。签名只在 Tikpal 的发布机上做。
3. 不在公开单子、提交说明、代码里写账号、邮箱、手机号、设备编号、令牌。日志用 `./kit logs`、`./kit info` 出来的，设备编号已经抹掉了。
4. 版本号只往上加：每次提交都比上一次大；USB 写进测试机的版本也不能比应用市场上的低。不然圆屏查更新时会把它当成旧版，一点更新就换成应用市场上的版本。
5. 立项单回复以前，不开始要云能力的开发。不要云能力的可以先写。
6. 不给这个仓库开 PR。
7. 不擦整片 Flash，不写位置 1（主固件）和恢复器，只用 `./kit install --slot 2|3`。
8. 不写死服务器地址和密钥；云能力只走开发包的接口。

## 3. 七步

| 步 | 谁做 | 交接 |
|---|---|---|
| 1 立项 | 你 | 「立项」单 |
| 2 开发 | 你 | — |
| 3 自测 | 你 | — |
| 4 提交 | 你 | 「提交」单 |
| 5 审核 | 我们 | 回在提交单上 |
| 6 真机测 | 我们 | 回在提交单上 |
| 7 签名上架 | 我们 | 回在提交单上，单子关掉 |

### 第 1 步：立项

- **条件**：想好了做什么。
- **做**：在这个仓库开一张「立项」单（模板 `proposal.yml`）：`https://github.com/vzheng56/tikpal-devkit/issues/new?template=proposal.yml`。
  - 用 `gh` 也行，正文照第 10 节的格式，**不要加标签**，标签我们打。
- **字段**：
  - `slug`：小写字母、数字、连字符，字母开头，2–24 个字符，比如 `my-timer`。定了就不改。
  - 中文名 / 英文名；
  - 一句话：它能做什么；
  - 用户会说的话，3–5 句：点了名的（「打开计时器」）和没点名的（「帮我计时十分钟」）都要；
  - 要哪些云能力：不要 / AI 对话 / 语音识别 / 语音合成 / 存数据或文件 / 别的；
  - 要哪些硬件：网络、麦克风、喇叭、姿态、TF 卡、蓝牙；
  - 勾选「我已读并同意开发者协议」（仓库里的 `DEVELOPER-TERMS.md`，协议定稿以后才有这一项）。
- **等我们回**（通常 1 个工作日内）：
  - slug 能不能用；
  - **应用编号**：一个数字，应用码开放前在这张单上补发，拿到后写进 `app.json`；
  - 要云能力的，开通要我们部署一次，回复里会说要等多久。
- **没过怎么办**：
  - slug 重名就换一个，在同一张单上回复新的 slug；
  - 云能力我们不给的，照回复改方案。
- **过关的标准**：单子上有 `number-issued` 标签，回复里说 slug 能用。

### 第 2 步：开发

- **条件**：
  - 立项单回了；
  - 要云能力的，等我们说开通了再写云能力那部分；
  - 一台 Mac（Linux 也行），一台测试用的 Tikpal Voice，一根 USB-C 数据线。
- **命令**：

  ```bash
  git clone https://github.com/vzheng56/tikpal-devkit.git
  cd tikpal-devkit
  ./kit setup                 # 装 ESP-IDF（只装一次）；在中国内地加 --cn
  ./kit new my-timer          # 建 my-timer/：一个应用一个 git 仓库，开发包是 devkit/ 子模块
  cd my-timer
  ./kit build                 # 编译，再跑主机测试
  ./kit install --slot 2      # USB 写进测试机的位置 2，写完直接打开
  ```

- **写代码照这些**：
  - `devkit/docs/app-guide.md`：资源归谁、启动顺序、启动参数和结果、侧键、组件、内存；
  - `devkit/docs/round-screen-design.md`：界面；
  - `devkit/docs/app-json.md`：应用清单；
  - 应用里的 skill `.claude/skills/tikpal-app`：写代码的硬规则。
  - 应用里的 skill `.claude/skills/tikpal-review`：交审之前照 `docs/review-rules.md` 自审，写出 `ai-selfreview.json`。
- **拿到应用编号以后写进 `app.json`**（字段名以 `devkit/docs/app-json.md` 为准；编号和亮码接口都随应用码开放，在那之前不用管）。
- **建自己的 GitHub 仓库并推上去**：

  ```bash
  gh repo create <你的账号>/my-timer --public --source . --push
  ```

  私有仓库也行，要把 `vzheng56` 加成协作者，我们才能重编。
- **产出**：一个能编过、能写进测试机、能打开的应用仓库。
- **过关的标准**：`./kit build` 和 `./kit test` 都过；`./kit install --slot 2` 以后应用能打开，双击侧键能回主固件。

### 第 3 步：自测

- **条件**：第 2 步过了。
- **照检查清单** `devkit/docs/checklist.md` 一项项走，每项留证据：

  ```bash
  ./kit snap main.png         # 截图，对照设计规范
  ./kit heap                  # 内存；底线读 ./kit info 的 heap.internalMinSinceReady：不低于 40,000 字节
  ./kit logs 60               # 看日志
  ./kit info                  # 设备上现在跑的是什么
  ```

  - 双击侧键开应用环，点中间回主固件；从应用环换到别的应用再回来；
  - 没网、低电量下的样子；
  - 中文、英文两套界面文字都放得下。
- **产出**：截图、内存日志、清单打勾。
- **过关的标准**：清单每一项都打得了勾。

### 第 4 步：提交

- **条件**：第 3 步过了；所有改动都提交、推上去了。
- **命令**：

  ```bash
  ./kit build --release       # 提交用的那份（不带开发命令）
  ./kit check                 # 要全过，最后一行是「✓ 都过了」
  git push
  ```

  推上去以后，应用仓库的 GitHub Actions 会跑 Check 工作流（模板自带），里面也跑 `./kit check`。**镜像 SHA-256 用 CI 里打出来的**：Mac 上编的和 Linux 编的永远差 60 几个字节（工具链调试信息里的路径不同），代码一样。

  ```bash
  gh run list --workflow Check --limit 1       # 找到这次推送的运行
  gh run view <运行编号> --log | grep -E "提交|SHA-256|都过了"
  ```

- **开「提交」单**（模板 `submission.yml`）：`https://github.com/vzheng56/tikpal-devkit/issues/new?template=submission.yml`。
- **字段**：
  - 应用 slug；
  - 版本号：和 `app.json`、`PROJECT_VER` 一样；
  - 这一版做了什么；
  - 应用仓库地址；
  - 提交号：完整 40 位；
  - 开发包版本：`./kit version` 的 version；
  - 镜像 SHA-256：CI 里 `./kit check` 打的；
  - `./kit check` 的输出：CI 里那一次的整段输出和运行链接；
  - 内存：最重的那一刻以后 `./kit info` 里 heap 那一段；
  - 截图；
  - 兼容检查清单五项打勾；
  - 还想说的。
- **等我们回**：通常 2 个工作日内回第一次意见。
- **过关的标准**：单子上出现 `queued`。

### 第 5 步：审核（我们做）

- 我们按你的提交号，在跟你 CI 一样的容器里重编。字节要和你填的 SHA-256 一样。
- 然后按检查清单和设计规范看代码，核 `app.json` 要的云能力跟立项开通的一样。
- **你要做的**：等。单子上会出现 `in-review`。
- **没过怎么办**：单子上会出现 `changes-requested`，意见写明哪一条、看到什么、怎么改。
  1. 改代码；
  2. **版本号加一**（比如 0.1.0 → 0.1.1，`app.json` 和 `PROJECT_VER` 一起改）；
  3. 走第 3、4 步的命令；
  4. 在**同一张提交单上回复**新的版本号、提交号、SHA-256、`./kit check` 输出。不要开新单子。

### 第 6 步：真机测（我们做）

- 我们用 USB 把重编出来的那份写进我们的测试机，走一遍。单子上是 `device-test`。
- **没过怎么办**：同第 5 步。

### 第 7 步：签名上架（我们做）

- 通过以后单子上是 `approved`。上不上架、什么时候上由 Tikpal 决定。
- 上架时我们用发布密钥签名、打包，先上**预览版**，以后转正式版。签名只对审过、重编对上的那份镜像做，密钥不经过你。
- 上架以后单子上是 `published`，单子关掉。
- **上架以后要改**：改代码、版本号加一，从第 3 步再走一遍，开一张新的提交单。

## 4. 要改开发包怎么办

开「需求」单（模板 `request.yml`）：`https://github.com/vzheng56/tikpal-devkit/issues/new?template=request.yml`。

- 写开发包版本（`./kit version`）、要什么或者哪里不对、日志。
- 我们通常 2 个工作日内回会不会做、放哪一版。
- 做进新版本以后，用 `./kit update <版本>` 换过去，编译、自测过再提交。

## 5. 应用码（在手机上打开你的应用）

- Tikpal 有自己的「应用码」：圆屏上亮一圈声波一样的码，用户用 Tikpal App 一扫，App 就打开你这个应用的页面。
- 码里装三样：
  - **应用编号**：立项单上我们给的（应用码开放前补发）；
  - **动作**：0–15；
  - **值**。
- 通用动作：
  - 0 打开应用页；
  - 1 整页服务页；
  - 2 某一天的记录（值是 1970-01-01 起的天数）；
  - 3 带一句话问伙伴（值是你登记的第几句，从 0 数）。
- 8–15 留给各应用自己用，**现在还不开放**：先只用 0–3。
- 应用环里的「在手机上打开」，所有应用自动都有，你不用写。
- 亮码的接口随之后的一版开发包开放，接口级别 4，最低主固件版本写在那一版的 `devkit.json` 里。编号也在那之前补发。
- 应用还可以带一个**小程序**：手机 App 里的网页，只经 App 的桥做事。扫到应用码时，这个应用有小程序就开小程序。怎么写、预览、打包见 [docs/miniapp.md](docs/miniapp.md)（`./kit miniapp`）；第一版只开给受邀的开发者。

## 6. 云能力

- AI 对话、语音识别、语音合成、存数据由我们提供，用的是用户账户的点数，开发者不付费。每个应用有限额，立项时定。以后可能调整，会提前通知。
- 你在 `app.json` 的 `capabilities` 里写要什么，只能比开通的少，不能多。
- 能力或限额要改，在立项单上回复说明，我们要再开通一次。

## 7. 规矩速查

- 项目名 `yaykit_<slug，连字符换下划线>`。前缀 `yaykit_` 是内部名字，换牌后不变，别改。
- 只写自己的 NVS 命名空间；不碰别人的资源；离开前的收尾不超过 400 ms。
- 镜像不超过 3.5 MB；内部 RAM 最低（`./kit info` 的 `heap.internalMinSinceReady`）不低于 40,000 字节，按字节比。
- 名字、启动横幅、控制台命令里的 `yaykit`、`yk`、`yay` 都是内部名字，照模板写。

## 8. 出错怎么办

| 看到 | 做 |
|---|---|
| `./kit setup` 装不上 | 在中国内地加 `--cn`；还不行开「需求」单，贴 `./kit version` 和报错 |
| `./kit check` 不过 | 照它最后几行的提示改；多半是有没提交的改动，或者版本号没加 |
| 写不进测试机 | 换一根能传数据的 USB-C 线；插着不止一台时用 `YAYKIT_PORT=/dev/cu.usbmodem… ./kit install --slot 2` |
| 审核说重编对不上 | 确认都推上去了、`./kit version` 和单子一致，版本号加一，重新提交 |
| 组件缺能力 | 开「需求」单，不要改 `devkit/` |

## 9. 单子上的标签

| 标签 | 意思 |
|---|---|
| `queued` | 收到了，等审核 |
| `in-review` | 在重编、看代码 |
| `changes-requested` | 要改，意见在单子上 |
| `device-test` | 在真机上测 |
| `approved` | 通过，等上架 |
| `published` | 上架了 |
| `declined` | 不收，理由在单子上 |
| `needs-cloud` | 等我们开通云能力 |
| `number-issued` | 立项通过；应用编号在回复里，或者应用码开放前补发 |
| `waiting-terms` | 协议定稿以后再接着办 |

## 10. 从零到提交：一个完整例子

做一个不要云能力的计时器 `my-timer`。

**① 立项单**（用网页上的模板最稳；用 `gh` 的话照下面的正文，不加标签）

```bash
gh issue create -R vzheng56/tikpal-devkit --title "立项：my-timer" --body-file proposal.md
```

`proposal.md`：

```markdown
### slug

my-timer

### 中文名 / 英文名

计时器 / Timer

### 一句话：它能做什么

在圆屏上倒计时，到点响一声。

### 用户会说的话（3–5 句）

- 打开计时器
- 帮我计时十分钟
- 五分钟以后提醒我
- 还剩多久

### 要哪些云能力

- [x] 不要（只在圆屏上跑）

### 要哪些硬件

- [x] 喇叭

### 还想说的

无
```

等回复：slug 可用（应用编号在应用码开放前补发），单子上出现 `number-issued`。

**② 开发**

```bash
git clone https://github.com/vzheng56/tikpal-devkit.git && cd tikpal-devkit
./kit setup
./kit new my-timer && cd my-timer
# 写代码……（拿到应用编号以后写进 app.json，字段名见 devkit/docs/app-json.md）
./kit build
./kit install --slot 2
gh repo create <你的账号>/my-timer --public --source . --push
```

**③ 自测**

```bash
./kit snap main.png
./kit heap
./kit logs 60
```

照 `devkit/docs/checklist.md` 走一遍，留截图和内存日志。

**④ 提交**

```bash
./kit build --release
./kit check                  # 最后一行「✓ 都过了」
git push
gh run list --workflow Check --limit 1
gh run view <运行编号> --log | grep -E "提交|SHA-256|都过了"
gh issue create -R vzheng56/tikpal-devkit --title "提交：my-timer 0.1.0" --body-file submission.md
```

`submission.md`：

```markdown
### 应用 slug

my-timer

### 版本号（和 app.json、PROJECT_VER 一样）

0.1.0

### 这一版做了什么

第一次提交：圆屏上倒计时，到点响一声；说「帮我计时十分钟」能直接开始。

### 应用仓库地址

https://github.com/<你的账号>/my-timer

### 提交号（完整 40 位）

<40 位提交号>

### 开发包版本（./kit version 的 version）

<x.y>

### 镜像 SHA-256（应用仓库 CI 里 ./kit check 打的）

<64 位 SHA-256>

### ./kit check 的输出

<CI 里那一次的整段输出>
<运行链接>

### 内存

<./kit info 里 heap 那一段>

### 截图

<主界面、计时中、到点、没网时的截图>

### 兼容检查清单（docs/checklist.md）

- [x] 构建
- [x] 生命周期
- [x] 规矩
- [x] 界面
- [x] 内存和性能

### 还想说的

无
```

然后等：`queued` → `in-review` → `device-test` → `approved` → `published`。中间出现 `changes-requested` 就照第 5 步改。
