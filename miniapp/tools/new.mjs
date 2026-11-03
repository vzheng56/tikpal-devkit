/**
 * `tikpal-miniapp new <slug>`: a new 小程序 project from template/, needing nothing installed. Inside an app made by
 * `kit new` (an app.json here) it goes into miniapp/, else into <slug>/; --dir puts it elsewhere. The project uses
 * this kit through a file: dependency (installed as a copy: .npmrc install-links), so it builds the same wherever it
 * is cloned next to the kit.
 */
import { existsSync } from "node:fs";
import { cp, readdir, readFile, rename, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const SDK = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const SLUG = /^[a-z][a-z0-9-]{1,40}$/;
const TEXT = [".json", ".html", ".ts", ".md", ".css", "npmrc", "gitignore"];

function fail(message) {
  console.error(`tikpal-miniapp new: ${message}`);
  process.exit(1);
}

export async function create(args) {
  const slug = args.find((a, i) => !a.startsWith("--") && args[i - 1] !== "--dir");
  const dirAt = args.indexOf("--dir");
  if (!slug || !SLUG.test(slug)) fail("用法：tikpal-miniapp new <slug> [--dir <目录>]。slug 跟圆屏应用一样：小写字母开头，只有小写字母、数字和 -，2–41 个字");
  const app = existsSync("app.json") ? JSON.parse(await readFile("app.json", "utf8")) : null;
  if (app?.slug && app.slug !== slug) fail(`这里的 app.json 是 ${app.slug}：小程序跟应用用同一个 slug（tikpal-miniapp new ${app.slug}）`);
  const target = path.resolve(dirAt >= 0 ? args[dirAt + 1] ?? fail("--dir 后面要跟目录") : existsSync("app.json") ? "miniapp" : slug);
  if (existsSync(target) && (await readdir(target)).length) fail(`${target} 已经有东西了，换一个 --dir`);
  await cp(path.join(SDK, "template"), target, { recursive: true });
  // npm drops .gitignore and .npmrc from what it installs, so the template keeps them without the dot.
  for (const name of ["gitignore", "npmrc"]) if (existsSync(path.join(target, name))) await rename(path.join(target, name), path.join(target, `.${name}`));
  const title = slug.split("-").map((w) => (w ? w[0].toUpperCase() + w.slice(1) : "")).join(" ").trim().slice(0, 40);
  const kit = path.relative(target, SDK).split(path.sep).join("/") || ".";
  const walk = async (dir) => {
    for (const entry of await readdir(dir, { withFileTypes: true })) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) await walk(full);
      else if (TEXT.some((t) => entry.name.endsWith(t))) {
        const text = await readFile(full, "utf8");
        await writeFile(full, text.replaceAll("__SLUG__", slug).replaceAll("__TITLE__", title).replaceAll("__KIT__", kit));
      }
    }
  };
  await walk(target);
  const shown = path.relative(process.cwd(), target) || ".";
  const inApp = existsSync(path.join(target, "..", "app.json"));
  const manifest = JSON.parse(await readFile(path.join(target, "manifest.json"), "utf8"));
  const part = { entry: manifest.entry, capabilities: manifest.capabilities, capabilityLevel: manifest.capabilityLevel };
  console.log(`建好了：${shown}/

  cd ${shown}
  npm install          # 在国内慢的话加 --registry=https://registry.npmmirror.com
  npm run dev          # 浏览器里预览：http://localhost:5180/
  npm run build        # 打包：dist/ 和 ${slug}-0.1.0.zip

先把 manifest.json 的 name 四种语言写好。文档：开发包的 docs/miniapp.md。${
    inApp
      ? `

应用的 app.json 里加上这一段（跟 manifest.json 一致，打包时会核）：
  "miniapp": ${JSON.stringify(part)}`
      : ""
  }`);
}
