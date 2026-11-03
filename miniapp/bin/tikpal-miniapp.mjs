#!/usr/bin/env node
// tikpal-miniapp: the 小程序 kit's one command (README.md). `new` needs nothing installed; the rest run tools/cli.ts
// through tsx, which comes with the kit.
const [command] = process.argv.slice(2);
if (command === "new") {
  const { create } = await import("../tools/new.mjs");
  await create(process.argv.slice(3));
} else {
  const { tsImport } = await import("tsx/esm/api");
  await tsImport("../tools/cli.ts", import.meta.url);
}
