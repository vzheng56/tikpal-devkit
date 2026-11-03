# Tikpal 小程序开发包（`@tikpal/miniapp`）

写小程序用的：小程序是跑在 Tikpal App 里的网页，只经 App 的桥做事。这个包给：

| 目录 | 是什么 |
|---|---|
| `src/index.ts` | 桥的接口和类型：`call`、`on`、`BridgeError`、`applyEnv`、`watchEnv`（类型照合同 `src/contract/miniapps.ts`） |
| `src/components/` | 图表、日历、环：App 服务页的同一个渲染器（`src/components/render.js`），颜色跟 App 走 |
| `preview/` | 浏览器预览：手机框、面板（语言、亮暗、宽度、断网、未登录、拒绝能力）、照合同核调用的假桥 |
| `tools/` | `tikpal-miniapp` 的 `dev`、`build`、`check`、`verify`；`new` 在 `bin/` 里，不用先装依赖 |
| `template/` | `tikpal-miniapp new` 起的项目 |
| `src/contract/`、`preview/fixtures/`、`src/components/render.js`、`components.css` | 生成的拷贝，别手改：合同里开发者用得到的那些（保留名单）、合同的调用样例、page-runtime 的渲染器和样式 |

用法从零到打出包，见开发包的 `docs/miniapp.md`（英文 `docs/miniapp.en.md`）。

## 在主仓库里

源码在 `apps/miniapps/sdk`，开发包导出时放进 `miniapp/`（`yaykit/scripts/devkit-export.ts`，`test/`、`scripts/`、`tsconfig.json` 不出去）。录音卡（`apps/miniapps/recording-card`）就是用它写和打包的。

| 命令 | 做什么 |
|---|---|
| `pnpm miniapp:sync` | 合同、fixtures、page-runtime 改了以后，重新生成上面那几份拷贝（`--check` 只核不写；`devkit:export` 导出前按那个提交核一遍） |
| `pnpm miniapp:sdk:test` | 单元测试（接口和合同对得上、打包和检查、拷贝是最新的、组件）加类型检查（`test/types.check.ts`：该报错的写法确实报错） |

- **开发包是公开的**：合同拷贝只带 `scripts/sync.ts` 的 `KEPT` 名单上的导出和它们用到的声明，合同新加的导出默认不进；名单上的东西用到了名单外的导出，sync 会停下来，要么加进名单，要么两个都不带。服务器放行的路径（能力套装的 `paths`）照 `STRIPPED` 去掉。
- 桥加方法（能力套装升级）：先改合同，看要不要把新导出加进 `KEPT`，`pnpm miniapp:sync`，再在 `src/methods.ts` 加上那一行（测试会核对），改文档的方法表和更新记录，升 `package.json` 的版本。
- CSP 用合同的 `MINIAPP_CSP`：打包时写进入口页（`tools/csp.ts` 的 `withCsp`），预览当响应头发，页面源码里不手抄。
