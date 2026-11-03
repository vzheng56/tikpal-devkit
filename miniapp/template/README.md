# __TITLE__ · 小程序

用 Tikpal 小程序开发包写的。从零到打出包的完整说明在开发包的 `docs/miniapp.md`（英文 `docs/miniapp.en.md`）。

| 命令 | 做什么 |
|---|---|
| `npm install` | 装开发包（第一次；在国内慢加 `--registry=https://registry.npmmirror.com`） |
| `npm run dev` | 浏览器里预览：<http://localhost:5180/>，可以切语言、亮暗、手机宽度，模拟断网和能力被拒 |
| `npm run build` | 打包：`dist/` 和 `__SLUG__-<版本>.zip`，打完照合同检查 |
| `npm run check` | 只检查 `dist/`（或 `npm run check -- 文件.zip`） |
| `npm run typecheck` | 类型检查 |

| 文件 | 是什么 |
|---|---|
| `manifest.json` | 小程序清单：slug、版本、名字（四种语言）、要的能力 |
| `index.html` | 入口页，带内容安全策略（CSP），别放开 |
| `src/main.ts` | 页面代码，从这里开始 |
| `src/i18n.ts` | 四种语言的字 |
| `src/app.css` | 样式：颜色、边距、字号都跟 App 走 |
