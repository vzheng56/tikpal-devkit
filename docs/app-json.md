# app.json：应用清单

每个应用的根目录有一份 `app.json`，告诉商城、Tikpal 主固件和平台：它叫什么、怎么打开、能用哪些云能力。版本号跟着固件一起改。我们审核时用 Tikpal 的正式契约逐字段核对；不认识的字段一律不收。`./kit check` 核其中主要的几条，`records` 一段照契约逐条核。

## 最小的一份（模板）

```json
{
  "slug": "my-timer",
  "kind": "extension",
  "version": "0.1.0",
  "name": {"zh": "计时器", "en": "Timer"},
  "boards": ["tikpal-r1"],
  "layout": "yaykit-v1",
  "minYay": "0.1.22",
  "apiLevel": 6,
  "capabilities": {},
  "launch": {
    "summary": {"zh": "计时器：说多久就计多久，到点提醒。", "en": "Timer: counts down as long as you say."},
    "examples": {"zh": ["打开计时器", "帮我计时十分钟"], "en": ["Open the timer", "Set a ten minute timer"]}
  },
  "aliases": {"zh": ["定时器"], "en": ["Countdown"]},
  "scenes": ["tools"],
  "audience": "all",
  "pricing": {"kind": "free"},
  "voiceCode": {"number": 65280}
}
```

## 字段

| 字段 | 必填 | 说明 |
|---|---|---|
| `slug` | 是 | 小写字母、数字、连字符，字母开头，2–24 个字符（项目名 `yaykit_<slug>` 最长 31 个字符，Tikpal Voice 从它读回 slug）。和项目名对得上。定了就不改。`yay`、`launcher`、`recovery`、`template`、`system` 是保留的 |
| `kind` | 是 | 扩展应用都写 `extension` |
| `version` | 是 | `主.次.修`，和 `CMakeLists.txt` 的 `PROJECT_VER` 一样。比发出去过的版本都大（被退回的那一版可以原样再交） |
| `name` | 是 | 名字，`zh` 和 `en` 必填，`ja`、`es-419` 可选；每种不超过 24 个字符 |
| `boards` | 是 | 写 `["tikpal-r1"]` |
| `layout` | 是 | 写 `"yaykit-v1"` |
| `minYay` | 是 | 最低要哪一版 Tikpal 主固件。至少写开发包对应的那一版（`./kit version` 的 `yay`；开发包 1.11 是 0.1.22） |
| `apiLevel` | 建议 | 开发包的接口级别（`./kit version` 的 `apiLevel`） |
| `capabilities` | 是 | 要用的云能力和限额，最小权限：设备令牌只许这些。不要云能力就写 `{}`。要什么，先在开发者平台这个应用的「能力和权限」页申请；这里写的只能比批的少，不能多 |
| `launch` | 要让伙伴用说话打开就要 | `summary`：一句话说它能做什么（≤ 300 字）。`examples`：用户会说的话，每种语言最多 12 句、每句 ≤ 80 字，两类都要有：点了名的（「打开计时器」）和没点名、只说想做什么的（「帮我计时十分钟」），伙伴找应用主要靠后一类。`params`：主固件能带给你的启动参数（见下） |
| `aliases` | 是 | 别的叫法，每种语言最多 8 个；没有就写空数组 `{"zh": [], "en": []}` |
| `scenes` | 是 | 在商城哪一类里，1–3 个：`party` 聚会、`learning` 学习、`relax` 放松、`language` 语言、`focus` 专注、`tools` 工具 |
| `audience` | 是 | `kids`、`adults` 或 `all` |
| `pricing` | 是 | 现在只有 `{"kind": "free"}` |
| `settings` 等 | 否 | 可以在手机 App 里调的设置（`settings`、`settingOptions`、`settingLabels`、`settingRules`）。要的话跟我们一起定 |
| `records` | 用了 `yk_app_record()` 就要 | 记录进「动态」的格式，和 `capabilities.records` 一起写，见下面的[记录 `records`](#记录-records)。跟我们一起定 |
| `ephemeral` | 否 | `true`：什么都不留（不写记录、不存数据和文件），比如翻译 |
| `voiceCode` | 是（开发包 1.9 起） | 建应用时给的应用编号；亮自己的动作（8–15）的，再登记每个动作的意思。见下面的[应用码 `voiceCode`](#应用码-voicecode) |
| `permissions` | 用到就写 | 用到的 Tikpal Voice 硬件、小程序要的手机能力、要读的共享文件夹，审核拿它对代码。`device`：用到的每样写一次：`microphone` 麦克风、`speaker` 喇叭、`motion` 姿态、`tf_card` TF 卡上你自己的目录（`yk_tf_*`）、`bluetooth` 蓝牙、`bluetooth_unpaired` 不配对的蓝牙口（开发包 1.11 起，`apiLevel` 至少 6，`yk_link_keep_start_unpaired()`；它含蓝牙，不用再写 `bluetooth`；要申请，暂未开放，见[应用指南](app-guide.md)第 9 节「1.11」）、`vibration` 振动（开发包 1.11 起，`yk_haptic_*`；申请就批）。`phone`：小程序要的手机能力，一样一句为什么用（每种语言 120 字以内）。`folders`（开发包 1.10 起）：见下面的[共享文件夹 `permissions.folders`](#共享文件夹-permissionsfolders) |
| `privacy` | 收用户数据就写 | `collects`：收用户的什么、为什么，一种一条，什么都不收写空列表 `{"collects": []}`。六种：`voice` 声音、`text` 文字、`photos` 照片、`location` 位置、`usage` 使用记录、`files` 用户拷进 Tikpal Voice 的文件（开发包 1.10 起：读了共享文件夹、又把内容往外传的才写）。`why` 中文、英文必填，每种语言 200 字以内，商店页上照登 |

### 启动参数 `launch.params`

主固件打开你时能带的参数，键名是参数名，值说明它：

```json
"params": {
  "minutes": {"type": "number", "description": "要计时多少分钟"},
  "sound": {"type": "string", "description": "到点用什么声音", "enum": ["bell", "chime"]}
}
```

- `type`：`string`、`number` 或 `boolean`；`description`：给伙伴的模型看，它按这个决定传什么；`enum`：只能是这几个值。
- 只写固件真读的参数。读法见[应用开发指南](app-guide.md)第 5 节。

## 记录 `records`

固件用 `yk_app_record(kind, data)` 写记录（[应用开发指南](app-guide.md)第 5 节），平台按这一段收进「动态」。要写两处，种类要对上：

- `capabilities.records.kinds`：Tikpal Voice 上能写哪几种（设备令牌只许这几种）；
- `records.kinds`：每一种的 `kind` 和 `schema`（`data` 长什么样）。

两处是同样几种，多一种少一种都不过。伙伴问过用户（`ask`）、用户答应了以后，平台才收这个应用的记录。

这两段跟我们一起定：记哪几种、每种记什么、多久整理一次、「动态」用哪个模板。先在开发者平台「能力和权限」页申请「记录」能力，在「用来做什么」里写清要记什么。写全了是这样（计时器每计完一次记一条 `{"minutes": 10, "completed": true}`）：

```json
"capabilities": {
  "records": {"kinds": ["mytimer.session"]}
},
"records": {
  "ask": {
    "zh": "计时器计过的时间，要我记下来吗？只记哪天、计了几分钟、有没有计完，不录音；每周一在「动态」里整理一次。要的话回我一声。",
    "en": "Shall I keep the time you count with Timer? Only the day, the minutes and whether it finished, no recordings; it is summed up in Feed every Monday. Just reply if you'd like that."
  },
  "kinds": [
    {
      "kind": "mytimer.session",
      "schema": {
        "type": "object",
        "properties": {
          "minutes": {"type": "number", "minimum": 0, "maximum": 600},
          "completed": {"type": "boolean"},
          "sound": {"type": "string", "enum": ["bell", "chime"]}
        },
        "required": ["minutes", "completed"],
        "additionalProperties": false
      }
    }
  ],
  "milestones": [
    {"type": "streak", "days": 7},
    {"type": "total", "kind": "mytimer.session", "field": "minutes", "reach": 1000}
  ],
  "feed": {"cadence": "weekly", "feedDays": 60},
  "pages": {"template": "mytimer@1"}
}
```

| 字段 | 必填 | 说明 |
|---|---|---|
| `ask` | 是 | 伙伴问用户要不要记的那句话：记什么、不记什么、多久整理一次。`zh`、`en` 必填，`ja`、`es-419` 可选，每种不超过 400 个字符 |
| `kinds` | 是 | 1–16 种记录。每种有 `kind`（和 `yk_app_record()` 的第一个参数一字不差：小写字母、数字和点，字母开头，3–41 个字符）和 `schema`（见下）；可选 `noMemory: true`（伙伴的记忆不读这种记录），`collect`（收进集合，见下） |
| `collections` | 否 | 最多 8 个集合，比如「查过的字」：`name`（写法同字段名）、`title` 和 `unit`（`zh`、`en` 必填，每种不超过 20 个字符）、`every`（每满多少个算一个里程碑） |
| `milestones` | 否 | 最多 16 个：`{"type": "streak", "days": 7}` 连着几天有记录（2–3650）；`{"type": "total", "kind": …, "field": …, "reach": …}` 这种记录的这个字段加起来到多少；`{"type": "first", "kind": …, "by": …}` 这个字段每个值第一次出现；`{"type": "first", "kind": …, "name": …, "when": {…}}` 第一次出现 `when` 里这些值。`by` 和 `name` 加 `when` 只写一种 |
| `feed` | 是 | 「动态」多久整理一次：`cadence` 是 `daily`（每天早上）、`weekly`（每周一）或 `session`（用完一次）；`feedDays` 读最近几天的记录，1–366 |
| `pages` | 是 | `{"template": "名字@版本"}`：「动态」和应用页用哪个模板画。模板在平台上，不在你的仓库里，名字我们给 |

### 记录的 `schema`

`data` 长什么样，用 JSON Schema（2020-12）的一小部分写，只能用这些词：`type`、`description`（不超过 200 个字符）、`properties`、`required`、`additionalProperties`、`items`、`minItems`、`maxItems`、`minLength`、`maxLength`、`minimum`、`maximum`、`enum`、`anyOf`、`default`。`type` 是 `object`、`array`、`string`、`number`、`integer`、`boolean`、`null` 之一。

- 最外层是 `"type": "object"`：一条记录是一个对象。
- 每个 `"type": "object"` 都列出 `properties`，并写 `"additionalProperties": false`（只能写 `false`）：没列的字段不收。
- 每个 `"type": "array"` 都写 `items`。
- `required` 里的字段要在 `properties` 里，每个只写一次。
- `enum` 里的值每个只写一次。
- 字段名小写字母开头，只用字母、数字和下划线，1–41 个字符：`bestCombo`、`new_words` 可以，`best-combo` 不行。
- 可以为空的写 `{"anyOf": [{"type": "integer"}, {"type": "null"}]}`。
- `data` 本身不超过 512 字节（应用开发指南第 5 节）。

### 收进集合 `collect`

```json
"collect": [{"collection": "words", "each": "newWords", "key": "word", "keyAs": "lower", "fields": ["word", "meaning"], "with": {"lang": "language"}}]
```

把记录（写了 `each` 就是记录里这个列表的每一项）按字段 `key` 收进集合 `collection`（要在 `collections` 里）。`keyAs`：`lower` 转小写；`phrase` 转小写、只留字母和数字，什么都不剩的不收。`fields`：每项留哪些字段，不写就全留。`with`：再从记录本身带上几个字段，`{"lang": "language"}` 是每项多一个 `lang`，取记录的 `language`。

### `./kit check` 怎么核

- `records` 和 `capabilities.records` 照 Tikpal 的契约逐条核，不过就不能提交。
- `voiceCode` 开发包 1.9 起必填（系统下拉里的「在手机上打开」靠它），也照契约核：编号是 1–65535 的整数；写的是测试号会打一行「提示」。
- 第一版加的 `permissions`、`privacy`、`ageRating`、`miniapp` 也照契约核：小程序要的手机能力（摄像头、麦克风、姿态、定位）都要在 `permissions.phone` 里写为什么用，`miniapp.capabilityLevel` 不能低于要的能力。
- 开发包 1.10 起，再照应用自己的代码核三样：代码里用了 `YK_FOLDER_*`，`permissions.folders` 却没写这一类，不过；写了却从没用到，打一行「提示」（审核时 AI 会再看一眼）；调了自己目录的那几个函数（`yk_tf_acquire`、`yk_tf_dir`、`yk_tf_path`、`yk_tf_space`），`permissions.device` 却没有 `tf_card`，不过。代码里写了以 `/sdcard` 开头的路径，也不过（[审核规则 F-08](review-rules.md)）。主机测试（`test/` 下面）不算。
- 开发包 1.11 起，再核振动的三样：调了 `yaykit_haptic` 给应用的函数（`yk_haptic_init`、`yk_haptic_play`、`yk_haptic_pattern` 这些），`permissions.device` 却没有 `vibration`，不过；写了却一个都没调，打一行提示。自己用 `gpio_*` 或 LEDC 碰 46 号脚（振动马达），不过；调了主固件专用的 `yk_haptic_set_settings`、`yk_haptic_set_battery_low`、`yk_haptic_flash`、`yk_haptic_metering`，不过（[审核规则 F-08](review-rules.md)）。主机测试不算。
- `capabilities["voice.session"]` 的 `modes` 要写，只写归自己的，见下面的[语音对话 `voice.session`](#语音对话-voicesession)。
- 固件里 `yk_app_record("…")` 写的种类，`records` 里没有的，会打一行「提示」（不算不过）：服务端不收这种记录。模板（`./kit new`）在 `on_leave` 里写了一条 `<简称>.count`，没有 `records`：要留进「动态」就照上面补上，不要就删掉那一句。

## 共享文件夹 `permissions.folders`

开发包 1.10 起，Tikpal 主固件 0.1.19 起。用户插上电脑，能往 Tikpal Voice 的 TF 卡里拷东西，放在四个文件夹里：Music、Stories、Pictures、Documents。你的应用要读哪几类，就在这里写哪几类，每类一句用途：

```json
"permissions": {
  "folders": {
    "music": {"zh": "放你拷进 Music 的歌", "en": "Plays the songs you put in Music"}
  }
}
```

- **四类**：`music`、`stories`、`pictures`、`documents`，对应那四个文件夹。**只能读**，不能写、删、新建。
- **用途**：中文、英文必填，日文、西班牙文可选，每种 60 字以内，说清读来做什么。只在商店页上给用户看：Tikpal Voice 上问用户时用固定的字，手机 App 的「应用权限」只列几类，都不显示用途。
- **没写的那类读不到**：`yk_folder_*` 一律当没申请，不弹问话、不给读。写了的几类，Tikpal Voice 第一次真要读时问用户一次，一次列出全部几类。
- **也是一项能力**：每类对应开发者平台「能力和权限」里的一项（「读音乐文件夹」……），要申请。申请后先是「待定」，等这一版交审时由 AI 审核定，不用人点。AI 按类核三样：代码里真读这一类；用途那句是实话；读到的内容往外传的，`privacy.collects` 写了 `files`。
  - 这一版过了：申请的几类一起批。
  - 这一版退回：对不上的那几类记成没批，退回意见写清哪里对不上；对得上的几类还是「待定」，不算批过，等下一版一起定。改了再交，没批的那类回到「待定」重新审。版本因为别的原因退回（比如崩了、离开太慢），这几类都留「待定」。
  - 批过的，以后的版本不用再批，但每一版照样要过 [F-08](review-rules.md)；多写一类，只审新的那类。
- **不用 `tf_card`**：只读共享文件夹不用写 `permissions.device` 的 `tf_card`；用 TF 卡上你自己的目录（`yk_tf_acquire`、`yk_tf_dir`、`yk_tf_path`、`yk_tf_space`）才要写。
- **往外传要写隐私**：读到的内容经 `yk_cloud` 上传、或者给小程序看，`privacy.collects` 写上 `files` 和为什么。只在 Tikpal Voice 上读、不往外传的不用写。

怎么读，见[应用开发指南](app-guide.md#存储卡和共享文件夹)的「存储卡和共享文件夹」，例子在 `firmware/examples/shared-folders`。

## 语音对话 `voice.session`

`capabilities["voice.session"]` 是 Tikpal Voice 上的语音会话：按住说话、听回答、能打断。`modes` 写这个会话做什么，每一种只归一个应用：

| `modes` 里写 | 归谁 | 是什么 |
|---|---|---|
| `chat` | Tikpal 主固件 | 伙伴自己的对话：带用户的记忆，能替用户办提醒、日历、连接的服务 |
| `practice` | 口语陪练（`language-buddy`） | 口语练习，带它自己的练习记录 |
| `translate` | 面对面翻译（`translator`） | 面对面翻译，什么都不留 |

扩展要写 `modes`，只能写归自己的。不写（不写就是 `chat`）、写 `chat`、写别的应用的，`./kit check` 和上传都不过，Tikpal Voice 上也开不了这个应用的会话。`voice.session` 目前只给 Tikpal 自己的应用，开发包的对话能力在设计中：第三方的扩展先别写，也先别申请「语音对话」。

## 应用码 `voiceCode`

Tikpal Voice 上亮出 Tikpal 的声波码，手机 App 一扫就打开你这个应用的页面。码里带三样：应用编号、一个动作、一个值。不经过服务器。

```json
"voiceCode": {
  "number": 256
}
```

- **`number`**：在开发者平台建应用时当场给你的应用编号（「我的应用」里也看得到）。
  - 一旦给了永远不改，也不会再给别的应用，应用下架了也占着。
  - 打包时会核它和商城里登记的是不是同一个。
  - 还没拿到编号时，可以先写本机测试号（65280–65535 里随便一个），在自己的 Tikpal Voice 上试。App 扫到测试号只打开一个调试页，不进商城。**提交前要换成正式编号**，审核不收测试号，`./kit check` 也会提示。

三个通用动作，不用登记：

| 动作 | 值 | 扫了打开什么 |
|---|---|---|
| 0 | 0 | 应用页 |
| 1 | 0 | 整页服务页 |
| 2 | 1970-01-01 起的天数（Tikpal Voice 本地日期） | 那一天的记录 |

3–7 还没开：App 扫到只打开应用页，值也不带（3 问伙伴要等那一套做好、带它的那版 App 出来）。用户没装你这个应用的，打开商城里它的页面。

**自己的动作 `own`**（开发包 1.11 起）：8–15 归你的应用，扫了打开你的小程序，动作和值一起交给它。要亮哪个，先在 `own` 里登记它和值的意思：

```json
"voiceCode": {
  "number": 256,
  "own": {
    "8": {"zh": "开始导航：值的低 4 位是目的地（0 回家、1 去公司）", "en": "Start navigating: the value's low 4 bits are the destination (0 home, 1 work)"}
  }
}
```

- 键是动作号 `"8"`–`"15"`；每个写一句，中文、英文必填，日文、西班牙文可选，每种 200 字以内。给审核和文档看，用户看不到。
- 只加不改：发出去过的动作，它和值的意思不能再改，也不能删。
- 固件里写死亮了 8–15、`own` 里却没有的，`./kit check` 不过；3–7 也不过。

怎么在固件里亮码、小程序里怎么读，见 [voice-code.md](voice-code.md) 第 4 节。

## 常见的错

- `version` 忘了跟 `PROJECT_VER` 一起改。
- `examples` 只写了「打开 xxx」：伙伴只有在用户点名时才找得到你。
- `capabilities` 里写了没用到的能力：审核会请你删掉。
- 写了 `launch.params`，固件却不读：用户说了参数也没用。
- `records` 写成 `{"<种类>": {"fields": {…}}}`：不是这个写法，照上面「记录 `records`」的例子写。
- `capabilities.records.kinds` 和 `records.kinds` 没对上，或者 `kind` 和固件里 `yk_app_record()` 写的差一个字。
- 提交时 `voiceCode.number` 还是测试号。
- 亮了自己的动作（8–15），`voiceCode.own` 里却没登记；或者登记过的动作，下一版改了意思。
- 写了 `"voice.session": {}`：不写 `modes` 就是主固件自己的对话（`chat`），扩展开不了。
- 只读共享文件夹，却写了 `tf_card`；或者用自己的目录，却没写 `tf_card`。
- `permissions.folders` 写了代码用不到的几类：Tikpal Voice 问用户时会把写了的都列出来，用户看到「想读你的音乐和图片」，却只用了音乐。
