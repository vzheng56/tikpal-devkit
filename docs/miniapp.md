# 小程序：从零到打出包

小程序是跑在 Tikpal App 里的网页（HTML、CSS、JS）。它只能经 App 给的「桥」做事：记录、设置、扫码、分享、复制……没有别的出口，不连网，也不要任何密钥。用户在手机上从 App 的小程序弹窗打开它，或者扫你的应用在圆屏上亮的码。

这份文档从零讲到打出一个合规的包：起项目、在浏览器里预览、写页面、打包、过检查。英文版：[miniapp.en.md](miniapp.en.md)。

> **第一版只开给受邀的开发者。** 先在浏览器里做好、打好包、过检查；再用 `./kit miniapp preview` 传成你自己的开发版，在自己手机上看（第 6 节）。

## 要准备的

- Node.js 20 或更新（`node -v` 看版本）。
- 这个开发包。已经有用 `kit new` 建的应用仓库，开发包就在它的 `devkit/` 里；`devkit/miniapp/` 不在的话，先 `./kit update <版本>` 换到带小程序的那一版。

## 1. 起一个小程序

在应用仓库里（有 `app.json` 的那个目录）：

```bash
./kit miniapp new my-timer     # 建在 miniapp/；slug 跟圆屏上的应用一样
cd miniapp
npm install                    # 在国内慢的话加 --registry=https://registry.npmmirror.com
```

在应用仓库里 slug 要跟 `app.json` 的一样，不一样会拒。不在应用仓库里也行：在开发包目录里跑 `./kit miniapp new my-timer`，建在当前目录的 `my-timer/`，`--dir <目录>` 放别处。`slug` 的规矩：小写字母开头，只有小写字母、数字和 `-`，2–41 个字。

建好的项目：

| 文件 | 是什么 |
|---|---|
| `manifest.json` | 小程序清单：slug、版本、入口、名字（四种语言）、要的能力（第 4.3 节） |
| `index.html` | 入口页。内容安全策略（CSP）不用写，打包时从合同写进去（第 4.1 节） |
| `src/main.ts` | 页面代码，从这里开始。也可以是 `main.tsx`、`main.js`，用 React 之类的库也行 |
| `src/i18n.ts` | 四种语言的字 |
| `src/app.css` | 样式：颜色、边距、字号都跟 App 走（第 3.4 节） |
| `public/` | 没有就自己建：放进去的文件原样进包（图片、mp3 音效） |
| `preview/fake.ts` | 没有就自己建：预览用的假数据（第 2 节） |
| `package.json`、`.npmrc` | 开发包作为 `@tikpal/miniapp` 装进来（`.npmrc` 让它装成一份拷贝） |

先把 `manifest.json` 的 `name` 四种语言写好（`en`、`zh-Hans`、`ja`、`es-419`，每个最多 40 个字）。

在应用仓库里，应用的 `app.json` 还要加一段 `miniapp`，跟 `manifest.json` 说的一样（`kit miniapp new` 建好时会把这一段打出来，照抄进去）：

```json
"miniapp": { "entry": "index.html", "capabilities": ["clipboard", "share"], "capabilityLevel": 2 }
```

以后改了 `manifest.json` 的 `capabilities`、`capabilityLevel`，这一段跟着改；`npm run build` 会核两边一不一致。要跟圆屏上的应用通信时，`manifest.json` 写 `"minFirmware": "my-timer@0.1.3"`，这一段写 `"minFirmware": "0.1.3"`。

## 2. 在浏览器里预览

```bash
npm run dev                    # 打开 http://127.0.0.1:5180/
npm run dev -- --port 5199     # 5180 被占了就换一个
```

页面本身在 `http://127.0.0.1:5180/app/`（不带面板，假桥用默认设置）。

左边是手机大小的框，右上角画着 App 的胶囊；右边是面板：

| 面板 | 做什么 |
|---|---|
| 语言、外观、配色、平台、字号 | 照 App 那样发 `env.changed`，页面当场换（第 3.1 节） |
| 能力套装 | App 给到第几级（第 3.3 节）：选 1 看老 App 上会怎样（第 2 级的方法回 `UNSUPPORTED`） |
| 宽度 | 360、375、393、430，看窄屏、宽屏 |
| 断网 | 要连服务器的方法回 `OFFLINE`；正在放的录音停在 `failed` |
| 未登录 | 要连服务器的方法回 `SIGNED_OUT` |
| 慢网 | 每次调用等 1.5 秒，看加载中的样子 |
| 拒绝能力 | App 不给这项能力（用户在胶囊「···」里收回了）：这组方法回 `NOT_DECLARED`；摄像头、定位这类直接拿不到 |
| 调用记录 | 每一次调用：方法、参数、回答或出错，用了多久；回答不合合同的会标出来 |
| 胶囊 | 「✕」当关闭；`ctx.toast`、复制、分享的提示在手机框下方弹一下 |

- 改了代码、`index.html`、`manifest.json` 或 `public/`，页面自己重开。编不过的话，面板上有红框写着哪一行。
- 预览用的是假桥：照合同核每一次调用（方法有没有、参数对不对、清单里声明了没有），然后用合同里的样例数据回答。它不连任何服务器。
- 预览给页面发的 CSP、权限策略跟 App 一样（第 4.1 节），在预览里连不出去的，在 App 里也连不出去。
- 预览只在这台电脑上：只认 `127.0.0.1` 和 `localhost` 的地址，只给开发包自己的文件、你的代码和 `public/`。
- 边距（胶囊那一行、系统的条）是近似值；真机上以 `env.insets` 为准。
- 扫码、选照片要用户自己操作：预览里用浏览器的对话框代替，你来扮演用户（取消就是用户关掉了）。记录和设置存在页面内存里，重开就没了；种类和设置项预览不核（服务器照 app.json 核）。

**自己的假数据**：建 `preview/fake.ts`，回答你想要的，再加几个面板上的开关：

```ts
import type { FakeHandlers, Switches } from "@tikpal/miniapp/preview";

// 面板上多两个选择，第一个是默认；换了就重开页面。
export const switches: Switches = { 记录: ["有", "空"], 存记录: ["能", "不合 schema"] };

export const handlers: FakeHandlers = {
  "records.list": (params, ctx) => ({
    items: ctx.switches.记录 === "空" ? [] : [{ id: "r1", kind: "knock.session", at: "2026-11-02T08:30:00.000Z", data: { count: 108 } }],
    next: null,
  }),
  "records.add": (params, ctx) => {
    if (ctx.switches.存记录 !== "能") throw { code: "SERVER", status: 400, serverCode: "SCHEMA_INVALID" }; // 出错就 throw
    return { id: "r2", at: new Date().toISOString() };
  },
};
```

`ctx` 里还有 `ctx.emit("env.changed", …)`（发事件）、`ctx.offline()`（面板上是不是断网）、`ctx.toast("…")`、`ctx.env()`。没写的方法用开发包自带的样例。预览先核参数、能力、断网这些，过了才轮到你的函数；你回的东西也照合同核，不对的在调用记录里标出来。

## 3. 写页面

### 3.1 起步：等 App 说完它在哪

```ts
import { watchEnv, type MiniappEnv } from "@tikpal/miniapp";

let env: MiniappEnv;
void watchEnv((next) => {        // 之后每次变了：语言、外观、配色、字号
  env = next;
  render();
}).then((first) => {             // 第一次
  env = first;
  render();
});
```

`watchEnv` 调 `env.get`，把颜色、边距、字号写成 CSS 变量（`applyEnv`），以后每次 `env.changed` 再写一遍，然后叫你的函数。**不要用顶层 `await`**：最老的安卓 WebView（Chrome 87）不支持，打包会直接报错（第 4.5 节）。

`env` 里有：

| 字段 | 是什么 |
|---|---|
| `locale` | `en`、`zh-Hans`、`ja`、`es-419`：App 的语言 |
| `theme.scheme`、`theme.colors` | 亮还是暗，和 App 的颜色（第 3.4 节） |
| `fontScale` | 用户选的字号，1 是标准 |
| `platform` | `ios` 或 `android` |
| `capabilityLevel` | App 的能力套装版本 |
| `insets.top`、`insets.bottom` | 要让出的 CSS 像素：胶囊那一行的下沿、系统的条 |

### 3.2 调桥

```ts
import { BridgeError, call, on } from "@tikpal/miniapp";

call("records.add", { kind: "knock.session", data: { count: 108 } }).then(
  (added) => showSaved(added.at),
  (error) => {
    if (error instanceof BridgeError && error.code === "OFFLINE") showOffline();
    else showFailed();
  },
);

const stop = on("env.changed", (env) => console.log(env.locale)); // App 主动说的事；watchEnv 已经替你听了这一个
```

- `call(方法, 参数)` 回一个 Promise；不要参数的方法可以不写参数。方法名、参数、回答都有类型，写错了编辑器和 `npm run typecheck` 会说。
- 出错一律抛 `BridgeError`，看 `code`：

| `code` | 什么时候 |
|---|---|
| `NOT_DECLARED` | 清单的 `capabilities` 没声明这组方法，或者 App 不给（第 4.3 节） |
| `UNSUPPORTED` | App 的能力套装版本比这个方法低（让用户更新 App） |
| `BAD_PARAMS` | 参数不对 |
| `SIGNED_OUT` | App 没登录 |
| `OFFLINE` | 没网 |
| `TIMEOUT` | 30 秒没回 |
| `SERVER` | 服务器说不行：`status` 是 HTTP 状态，`serverCode` 是它的原因（比如 `CREDITS_EXHAUSTED` 点数不够） |
| `FAILED` | 别的 |

- 每个会出错的调用都要有给用户看的一句话。网络的事用户常碰到：断网、慢网都在预览里试一遍。

### 3.3 能力和方法（能力套装 2）

能力套装是 App 给小程序的那一套能力，有版本（第 4.4 节）。第 2 级是第一版，新写的小程序都写 `"capabilityLevel": 2`。

**桥上的方法**：

| 方法 | 能力 | 级 | 参数 | 回什么 |
|---|---|---|---|---|
| `env.get` | 都有 | 1 | — | 上面那个 `env` |
| `app.close` | 都有 | 1 | — | 关掉小程序 |
| `clipboard.write` | `clipboard` | 1 | `{ text }` | 复制 |
| `user.id` | `user` | 2 | — | `{ id }`：用户在这个小程序里的编号，每个小程序不同，拿不到账号本身 |
| `records.list` | `records` | 2 | `{ kind?, cursor? }` | 自己应用的记录（进「动态」的那些），一页最多 50 条，新的在前 |
| `records.add` | `records` | 2 | `{ kind, data }` | 记一条；`kind` 和 `data` 的样子照 app.json 的 `records`，不合回 `SERVER` 400 `SCHEMA_INVALID` |
| `settings.get` | `settings` | 2 | — | `{ values }`：应用的设置，跟圆屏上那个应用读的是同一份 |
| `settings.set` | `settings` | 2 | `{ values }` | 改 1–20 项，回改完的全部；设置项照 app.json 的 `settings` |
| `scan.code` | `scan` | 2 | — | App 的扫一扫：`{ text }`，用户关掉了回 `{ cancelled: true }`。Tikpal 自己的码（配对码、声波码等）不交给小程序 |
| `share.text` | `share` | 2 | `{ text }` | 系统的分享：`{ shared }` |
| `photos.pick` | `photos` | 2 | `{ max? }` | 用户选照片（最多 4 张）：`{ photos: [{ type, width, height, data }] }`，JPEG 的 base64，长边最多 1600；没选回 `{ cancelled: true }` |
| `photos.save` | `photos` | 2 | `{ type, data }` | 把页面画的图（PNG 或 JPEG 的 base64）存进相册：`{ saved }` |

`app.close`、`clipboard.write` 回空对象 `{}`。事件：`env.changed`（都有）。

**WebView 自己的能力**：不走桥，用网页的标准接口，但要在 `capabilities` 里声明，第一次用时 App 问用户（用户能在胶囊「···」里收回），没声明的直接拿不到：

| 能力 | 接口 |
|---|---|
| `camera` | `navigator.mediaDevices.getUserMedia({ video })` |
| `microphone` | `getUserMedia({ audio })`：录音，审核时要人工批 |
| `motion` | `devicemotion`、`deviceorientation` |
| `location` | `navigator.geolocation`，页面开着时 |

用了这四个，在应用的 `app.json` 里写 `permissions.phone`，每个一句为什么（App 问用户时照它说），比如 `"permissions": { "phone": { "camera": "拍下你的木鱼当封面" } }`。

**只给录音卡的**：`recordings`、`audio`（读这个账号圆屏录的音、由 App 放录音）是录音卡自己的，别的小程序不能声明。

**计划中，第一版不给**：选文件、存文件、问伙伴、AI 对话、语音识别和合成、发消息给圆屏上的应用。哪一级开了，开发包跟着升版本，这张表跟着补。

每个字段的意思、出错时回什么，以开发包里的合同为准：`miniapp/src/contract/miniapps.ts`（桥）、`miniapp-capabilities.ts`（能力套装）。

### 3.4 跟 App 一个样子

`applyEnv`（`watchEnv` 已经替你调了）在页面的根上写：

| CSS | 是什么 |
|---|---|
| `--paper`、`--card`、`--line` | 底色、卡片、分隔线 |
| `--ink`、`--ink2`、`--ink3` | 字：主要、次要、最淡 |
| `--accent`、`--accentStrong`、`--onAccent`、`--accentText` | 用户给 App 选的颜色：按钮、选中的东西 |
| `--sage`、`--sageStrong` | 自己在进行的、转好了的 |
| `--coral`、`--coralText`、`--danger` | 只给要用户处理的、出错的 |
| `--inset-top`、`--inset-bottom` | 要让出的边距（px） |
| `--font-scale` | 字号倍数：iOS 上是用户选的；安卓的 WebView 自己会放大，所以是 1 |
| `color-scheme`、`lang`、`data-scheme` | 亮暗、语言 |

- **从 `--inset-top` 往下画**：标题在胶囊那一行的下面，不跟胶囊并排。底下让出 `--inset-bottom`。
- 字号写成 `calc(15px * var(--font-scale, 1))` 这样。
- **不用蓝色。** coral 只用在等用户处理的、出错的地方。
- 四种语言都要写，换语言时（`env.changed`）当场换。

### 3.5 组件：图表、日历、环

跟 App 里服务页一样的画法（同一个渲染器），颜色跟 App 走：

```ts
import { calendar, chart, ring } from "@tikpal/miniapp/components";

root.append(
  chart({ kind: "bar", title: "这周", points: [{ label: "一", value: 3 }, { label: "二", value: 5 }] }),
  ring({ title: "什么时候", items: [{ label: "上午", value: 38 }, { label: "晚上", value: 37 }], center: { value: "75", label: "次" } }),
  calendar({ start: "2026-10-01", values: [/* 每天一个数，7–371 个 */] }, { less: "少", more: "多" }),
);
```

- `chart`：`kind` 是 `bar` 或 `line`，2–31 个点，按顺序画。**最后一个点画成 coral**，跟 App 服务页一样表示「最新的那一个」：把今天放在最后（比如「近 7 天」，不是「周一到周日」）。
  - 堆叠的柱子：写 `series`（2–6 个，每个可带 `tone`），每个点的 `parts` 按 `series` 的顺序；`value` 照样写总数（老版 App 只画总数）。
- `calendar`：从 `start` 那天起每天一个数，越多越深；`tone` 写在顶层（`coral`、`sage`、`sun`、`ink`）；第二个参数是图例的两个字，跟页面的语言走。
- `ring`：1–8 项各占多少，每项可带 `tone`；`center` 是中间写的字。
- `tone` 只能是 App 的颜色：`coral`、`sage`、`sun`、`ink`，和呼吸之间四个场景的 `wake`、`focus`、`sleep`、`mine`。没写的按顺序配。
- 要 HTML 文字（比如放进 React 的 `dangerouslySetInnerHTML`）用 `chartHtml`、`calendarHtml`、`ringHtml`。字都会转义。

### 3.6 图片、字体、声音

- 放进 `public/` 就原样进包，用相对路径引用（`<img src="images/logo.png">`）。在代码里 `import` 的图片会直接编进 `app.js`。
- 声音用 mp3、m4a 或 wav 的短音效。**Ogg Opus 不行**（iOS 的 WebView 放不了）。照手机的默认，用户先点一下页面才出声。
- 文件名只能用小写字母、数字和 `. _ - /`。

### 3.7 存东西

App 里的小程序不留浏览器的存储：`localStorage`、`IndexedDB`、cookie 关了小程序就没了（安卓上可能直接用不了）。要留下来的，用桥：记录用 `records`，设置用 `settings`（第 3.3 节）。

`index.html` 里的 `<title>` App 不用（App 显示清单的 `name`），`<html lang>` 由 `applyEnv` 换成 App 的语言。

## 4. 规矩

打包时都会查（第 5 节）：过不了就不出 zip，`dist/` 留着给你看。检查是照代码里的写法查的，绕开写法查不到，但 App 的 CSP 照样拦，在手机上就是坏的。

### 4.1 不连外部网址

- 小程序不直接连网：不许 `fetch`、`XMLHttpRequest`、`WebSocket`、`EventSource`、`sendBeacon`，一切走桥。
- 不许从别处加载东西：`<script src="https://…">`、外面的图片、字体、样式都不行，只用包里的文件，写相对路径。
- 不许 `eval`、`new Function`，也不许写在页面里的 `<script>` 和 `onclick="…"`：App 不让它们跑，写进 `src/` 用 `addEventListener`。
- 外链、跳转、新窗口、下载，App 一律拦下。
- 内容安全策略（CSP）是合同定的这一串，打包时写进 `index.html`（你自己写了也会换成它），App 也会给页面发同一串：

  ```
  default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; font-src 'self'; media-src 'self'; connect-src 'none'; frame-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'
  ```

### 4.2 不要 API key

App 用用户自己的登录替小程序调我们的服务，小程序拿不到登录，也用不着任何密钥。包里像是有 API key、令牌、私钥的，检查会拦下。

### 4.3 能力要声明

- 要用哪组方法、哪项 WebView 的能力，先在 `manifest.json` 的 `capabilities` 里写上（`env`、`app` 每个小程序都有，不用写），`app.json` 的 `miniapp` 一段跟着写：

  ```json
  "capabilities": ["clipboard", "share"]
  ```

- 没声明就调：回 `NOT_DECLARED`，控制台说一声（打包后的页面当场拒，不问 App；预览里由假桥回，进调用记录）；打包检查也会指出哪个方法要哪项能力。代码里用了 `getUserMedia`、`devicemotion`、`geolocation` 却没声明对应的能力，检查也会说。App 和服务器那边各再拦一道。
- 只声明真用到的。审核会看每项能力为什么要。

### 4.4 能力套装版本

- 能力套装加能力就升一级，只加不改。第 1 级是录音卡那一套；第 2 级是第一版（第 3.3 节）。
- `manifest.json` 的 `capabilityLevel` 写你要的最低一级，不能低于你声明的能力要的那一级（合同的 `capabilityLevelOf`；写低了清单就不合合同）。新写的小程序写 2。
- 用了更高一级才有的方法，打包检查会说；App 低于你要的那一级，就打不开这个包，App 会提示更新。
- App 的小程序容器从第一版起是第 2 级；还没升级的老 App 只到第 1 级（录音卡用的那一套），后台只发它装得了的版本。要的级别比现在的 App 高时，打包会提醒一句，不算错。

### 4.5 旧手机

- 安卓的系统 WebView 最老按 Chrome 87 算，iOS 按 Safari 16。开发包照这个打包：**不能用顶层 `await`**，打包会报错。
- 87 没有的接口也不能用：`structuredClone`、`Object.hasOwn`、`.at()`、`findLast`、`toSorted`、`crypto.randomUUID`、`Object.groupBy`、`Promise.withResolvers`（还有 `toReversed`、`toSpliced`）。打包检查会查打出来的 `app.js`，用的库里有也算。

### 4.6 包的样子

- 最多 200 个文件；zip 最大 5 MB，解开最多 20 MB，一个文件最大 10 MB。
- 路径只能用小写字母、数字和 `. _ - /`，最长 120 个字；不许有符号链接。

## 5. 打包

```bash
npm run build
```

打出两样：

- `dist/`：包本身。`manifest.json`、`index.html`、`app.js`、`app.css`，加上 `public/` 里的文件。
- `my-timer-0.1.0.zip`：交给平台的那个 zip，里面就是 `dist/` 的文件。

打完自动照合同检查（第 4 节），有问题就列出来、退出码不是 0，**不出 zip**（这个版本以前打的 zip 也删掉，免得交错）。`dist/manifest.json` 是打包工具写的，比你的清单多一个 `files`：每个文件的 SHA-256。

| 命令 | 做什么 |
|---|---|
| `npm run build` | 打包并检查。`-- --no-zip` 不打 zip，`-- --zip <文件>` 换个名字 |
| `npm run check` | 只检查 `dist/`；`npm run check -- my-timer-0.1.0.zip` 检查 zip |
| `npm run verify` | 在别处再打一遍，跟 `dist/` 逐字节比。同一份源码在哪台电脑上打都一样，审核就是这么核的 |
| `npm run typecheck` | 类型检查 |

- **改版本号**：改 `manifest.json` 的 `version`（三段式，比如 0.1.1），`package.json` 的 `version` 一起改。
- **签名由平台做**。开发包不签名，你也不需要任何密钥：包传上去、审过以后，由 Tikpal 签名再发出去。

## 6. 交给平台

先在浏览器里做到：预览里四种语言、亮暗、窄屏都看过，断网和能力被拒都有提示，`npm run build` 过了检查。

### 6.1 在自己手机上看（开发版）

在应用目录里：

```
./kit miniapp preview
```

（在小程序目录里也行：`npx tikpal-miniapp preview`。）它先打包、过检查，再连同应用目录里的 `app.json` 一起传到开发者平台，当成你的**开发版**：只有你自己的账号拿得到，手机上的 Tikpal App 打开这个小程序就是这一版。不审核、不花点数，别人看不到。

`app.json` 要有 `miniapp` 一段，`entry`、`capabilities`、`capabilityLevel` 跟小程序的 `manifest.json` 写成一样，`slug` 也一样（写法见 `docs/app-json.md`）。平台照这份核包；能用哪些能力照旧看开发者网站「能力和权限」批了哪些，`app.json` 多写了也不算。小程序要的手机能力在 `permissions.phone` 里写的为什么用，就是手机上弹窗问用户时显示的那句。`app.json` 不在应用目录里，用 `--app <路径>` 指给它。

第一次会先让你登录：终端里给一个码和一个网址（developer.tikpal.ai/cli），在浏览器里用你的开发者账号登录、核对码、点「允许」，终端就自己接着往下走。也可以先单独登录：`./kit miniapp login`。登录 30 天有效，到期再来一次；`./kit miniapp logout` 退出。开发者网站的那一页能看到哪些电脑登录着，不认得的点「退出」。

- 平台拒了会说清楚怎么改，再逐条列出哪里不对，照着改了再传。
- 同一个版本号还在开发版上，可以反复传，后传的替换先传的；已经发到预览版或正式版的版本号不能再传，要改版本号。

### 6.2 给别人用

要给测试账号或者所有人用，就交审核：`./kit submit` 打提交包，在开发者网站应用的「版本」里上传。

## 7. 出问题了

| 看到 | 怎么办 |
|---|---|
| 预览里一片空白 | 看面板上有没有红框（编不过）；再看浏览器的控制台 |
| `NOT_DECLARED` | `manifest.json` 的 `capabilities` 加上那组能力；或者是面板上勾了「拒绝能力」 |
| `Top-level await is not available` | 改成 `watchEnv(...).then(...)` 这样（第 3.1 节） |
| `用了 .at()：最老的安卓 WebView（Chrome 87）没有` | 换写法，比如 `a[a.length - 1]`；是库带进来的就换个版本或换个库 |
| `CSP：… 要写 …` | 包不是这个开发包打的：用 `npm run build` 打，它会写进第 4.1 节那一串 |
| 提醒：`capabilityLevel 是 3：现在的 App 只到第 2 级` | 不是错：现在的 App 是第 2 级的容器，要的级别更高的包，等容器再升级才打得开。先在预览里做 |
| `capabilityLevel is below what a capability needs` | 声明了第 2 级的能力，`capabilityLevel` 要写 2 |
| `app.json 要有 miniapp 一段` 之类 | 照提示改应用的 `app.json`，跟 `manifest.json` 一致（第 1 节） |
| `引用的 … 不在包里` | 引用写相对路径；打包后的代码叫 `app.js`、样式叫 `app.css` |
| `端口 5180 被占了` | `npm run dev -- --port 5199` |
| `… ：文件名和路径只能用小写字母…` | 把 `public/` 里那个文件改成小写、不带空格 |
| `这里的 app.json 是 …` | 小程序的 slug 要跟应用的一样，照提示的 slug 重来 |
| `npm install` 卡住 | 在国内加 `--registry=https://registry.npmmirror.com` |
| npm 说 esbuild 的 install script 没跑（`allowScripts`） | 不用管，打包照常；想让它跑就照提示 `npm install-scripts approve esbuild` |
| 要 React | `npm install react react-dom`，入口改成 `src/main.tsx`，照常写 |

Tikpal 自己的录音卡小程序就是用这个开发包写、预览、打包的，跟这里讲的一样。

## 更新记录

- **0.1.0**（2026-11-02）：第一版。桥的接口和类型（能力套装 2）、模板、图表日历环、浏览器预览、打包和检查。
