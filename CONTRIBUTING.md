# 怎么给 Tikpal 开发包出力

这个仓库**不收 PR**，应用和改动都不收。开了会被自动关掉。

- **交应用**：在开发者平台 developer.tikpal.ai 上建应用、传提交包（`./kit submit` 打的），审核进度和意见也在那里看。代码放在你自己的 GitHub 仓库里（`./kit new` 会建好，开发包是 `devkit/` 子模块）。这个仓库不收「立项」单、「提交」单。
- **改开发包**：组件缺能力、工具有问题、文档不清楚，开一张「需求」单。开发包是从我们的主仓库导出的，这里合进去的改动会被下一次导出盖掉，所以要我们做进下一版。

完整流程见 [AGENTS.md](AGENTS.md)（写给帮你写应用的 AI，人也能照着走）。

---

# Contributing to the Tikpal Devkit

This repository **does not accept pull requests**, for apps or for changes. Pull requests are closed automatically.

- **To submit an app**: make it and upload its package (`./kit submit` makes one) on the developer platform,
  developer.tikpal.ai, where the review's progress and notes are too. Keep the code in your own GitHub repository
  (`./kit new` sets it up, with the kit as the `devkit/` submodule). This repository takes no proposal or submission
  issues.
- **To change the kit**: open a Request issue for a missing component feature, a tool problem or an unclear document.
  The kit is exported from our main repository, so a change merged here would be overwritten by the next export; we
  make it in the next version instead.

The whole process is in [AGENTS.md](AGENTS.md) (written for the AI that helps you build an app; people can follow it too).
