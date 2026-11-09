---
name: tikpal-developer
description: 把 Tikpal Voice 应用发布到 Tikpal 开发者平台（developer.tikpal.ai）：建应用、填商店素材、打包交审、等审核结果、被退回就改了再交、跟进度直到发出去。开发者说「发布到 Tikpal」「交审」「看看〈应用〉的进度」「为什么被退回」时使用。Use when the developer wants to publish a Tikpal Voice app to Tikpal — create the app, fill its store listing, submit a version, wait for the review, fix a returned version and resubmit, or check where it is — with the Tikpal developer plugin's MCP tools.
---

# Tikpal 开发者平台：从交审到发出去

你在帮一位 Tikpal 开发者，把他的 Tikpal Voice 应用（扩展固件，可以带一个手机小程序）发布到 Tikpal。写代码的规矩见 skill `tikpal-app` 和开发包的 `AGENTS.md`；这一份管建应用、交审、等结果、改了重交、发出去。

## 1. 先确认插件能用

- 看你有没有 Tikpal 的 MCP 工具：`whoami`、`get_app`、`wait_for_change`、`get_findings` 这些（名字前面带 `tikpal`）。
- 没有：照 https://developer.tikpal.ai/start.md 最前面那节装 Tikpal 开发者插件。**改 AI 的配置、装插件之前，先问开发者一句。**
- 你的 AI 不支持插件（比如 Codex、Cursor）：照 start.md 用 `npx skills add vzheng56/tikpal-devkit --skill tikpal-developer -g` 装这份 skill，再照 start.md 把 MCP 加进 AI 的配置。
- 有：先调一次 `whoami`。

## 2. 授权：浏览器弹出来，开发者点「允许」

- 第一次调工具时，插件会用浏览器打开 developer.tikpal.ai/cli?code=…，工具回的话里有一串码（比如 `ABCD-EFGH`）。跟开发者说：「浏览器打开了，用你的开发者账号登录，看页面上的码是不是 ABCD-EFGH，一样就点『允许』。」等他说点好了，再调一次刚才的工具。
- 浏览器没弹出来：把工具回的网址给开发者，请他自己打开。
- 登录过期或者在网站上被收回，插件会自己再弹一次，照上面再走一遍。
- **不要**叫开发者去建令牌、复制令牌、贴令牌；**不要**读 `$HOME/.tikpal/`、`$HOME/.yaykit-devkit/` 下的文件或环境变量里的令牌；令牌不打印、不写进仓库，开发者要贴给你也别收。
- `NOT_DEVELOPER`（还不是开发者，要邀请码申请）、`TERMS_OUTDATED`（开发者协议更新了）：只能开发者自己在网站上办，请他去办。

## 3. 流程

| 步 | 谁 | 怎么做 |
|---|---|---|
| 1 开发者账号、签协议 | 开发者 | https://developer.tikpal.ai ，要邀请码 |
| 2 开发包 | 你 | `git clone https://github.com/vzheng56/tikpal-devkit`，`./kit setup`，`./kit new <slug>` |
| 3 写代码、自测 | 你 | skill `tikpal-app`；`./kit build`、`./kit install --slot 2`、`./kit build --release`、`./kit check` 最后一行要「✓ 都过了」 |
| 4 建应用 | 你，先问开发者 | `create_app`（slug、名字、要哪些能力、给谁用都先问）；回的 `number` 写进 `app.json` 的 `voiceCode.number` |
| 5 商店素材 | 开发者提供 | `get_store_listing` 看 `missing`；他给了图和文字，你用 `upload_image`、`update_store_listing` 填，直到 `missing` 是空的 |
| 6 推代码 | 你 | 推到开发者自己的 GitHub **公开**仓库：审核按提交号重编 |
| 7 打包 | 你 | 有 `./kit login` 的开发包用 `./kit submit --no-upload`，1.9 用 `./kit submit`：都写出 `<slug>-<版本>.tikpal` |
| 8 交审 | 你 | `create_upload`（slug、包的字节数、`shasum -a 256` 的值）→ 在同一个 shell 里跑它给的命令（先设 `TIKPAL_PACKAGE=<包的路径>`）→ `submit_version`（`notes` 是给用户看的「这一版做了什么」，先问开发者） |
| 9 等结果 | 你 | `wait_for_change`，见第 4 节 |
| 10 发出去 | 平台自动 | AI 审过、真机测过、签好名，平台照规则先发给测试名单（名单用 `set_testers` 设），再发正式版；你不发，开发者也不用点 |

## 4. 交上去以后：每个状态做什么

交完马上调 `wait_for_change`，传 slug 和你最后看到的 `status`（`since`）。它在服务端最多等 45 秒，状态一变就回来：

- 回 `stop: false`：还在机器检查或 AI 审，带上 `round` 加 1 再调一次。
- 回 `stop: true`：别再调了，照它的 `say` 做，或者告诉开发者。

| `status` | 意思 | 你做什么 |
|---|---|---|
| `checking`、`ai_review` | 机器检查、AI 审，几分钟 | 用 `wait_for_change` 等 |
| `returned` | 被退回了 | `get_findings`（`submission.id`）读每一条：标 `block` 的全改，`suggest` 的能改就改；为什么退回只以它为准，不猜，不只改第一条。改完重新打包、推代码、再交（被退回的版本号可以原样再交） |
| `held` | Tikpal 的人在看，原因在 `now`，一般一天内回 | 不重交、不撤回，告诉开发者在等什么 |
| `device`、`signing` | Tikpal 排队做真机测试、签名，可能要一两天 | 不重交、不撤回，这不是卡住了 |
| `preview` | 发给开发者和测试名单了 | 不用做什么：平台照规则接着发正式版，你不发，开发者也不用点 |
| `stable` | 发正式版了，所有人都能装 | 要改就改代码、版本号加一、再交 |
| `draft`、`withdrawn` | 还没交、或撤回了 | 交审 |
| `unlisted` | 下架了 | 重新上架要开发者找 Tikpal |

## 5. 要等很久时

- 跟开发者说清现在在哪一步、通常还要多久（`eta`），然后停下：「状态一变，网站上这个应用的页面会显示。你回来跟我说『看看〈slug〉的进度』就行。」
- 你能定时的话（比如 Claude Code 的 `/loop`、定时任务），可以问开发者要不要让你每 30 分钟用 `get_app` 看一次，直到要他动手或者发出去了。他点头再建。
- 回来看进度：先 `get_app`；`checking`、`ai_review` 再用 `wait_for_change` 等。

## 6. 硬规矩

1. **商店素材由开发者自己提供**：图标、截图、中英文的名字、一句话介绍、详细介绍、使用要求。缺了交不了（`STORE_INCOMPLETE`，`missing` 列出缺什么）。找开发者要，不替他编文字、不自己画图标。他给了或者点过头的，你可以替他填进去。
2. 建应用、申请能力、写 `notes` 之前先问开发者。
3. 正式版从不由你发（平台照规则自动发）。`unlist_app`、`pause_release`、`withdraw_submission` 只在开发者明确说要的时候用；只是等得久，不撤回。
4. 只碰这位开发者自己的应用。
5. 不改开发包 `devkit/` 下的文件；开发包缺什么，请开发者到开发包仓库开「需求」单。
6. **没看到最终状态，不许说「发出去了」「上架了」。** 只有 `get_app` 或 `wait_for_change` 回了 `preview`、`stable` 才算；交上去了就说「交上去了，现在在〈now〉」。
7. 对外说硬件，叫 Tikpal Voice。

## 7. 出错的时候

| 看到 | 做 |
|---|---|
| 工具说浏览器已经打开、还没登录 | 请开发者点「允许」，点完再调一次 |
| `TOKEN_SCOPE`、`TOKEN_APP` | 这台电脑的登录做不了这件事：请开发者在 https://developer.tikpal.ai/connect 收回这台电脑的登录，再调一次工具重新授权；别反复试 |
| `WEB_ONLY` | 只能开发者在网站上做：请他去做 |
| `RATE_LIMIT` | 调得太勤：等一分钟 |
| `SUBMISSION_OPEN` | 已经有一份在审：等它；只是慢就别撤回 |
| `PACKAGE_INVALID` | 照它列的每一条改，重新打包、上传、交审 |
| `UPLOAD_MISSING`、`UPLOAD_EXPIRED` | 再跑一次上传命令；地址过期了就再 `create_upload` |
| 别的看不懂的错 | 把错误原样告诉开发者，别猜 |

## 8. 更多

- 全流程、每个字段的要求：https://developer.tikpal.ai/start.md（English: https://developer.tikpal.ai/start.en.md）
- 审核规则：MCP 资源 `review-rules.md`，或 https://developer.tikpal.ai/docs/review-rules.md
- 全部文档：https://developer.tikpal.ai/llms.txt
