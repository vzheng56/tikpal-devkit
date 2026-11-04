/**
 * tikpal-miniapp dev | build | check | verify | preview, run in the 小程序 project, and login | logout
 * (bin/tikpal-miniapp.mjs; `new` is there). `preview` puts the package up as the developer's own dev version
 * (tools/platform.ts).
 */
import { existsSync } from "node:fs";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { build, readFolder, verify } from "./build.js";
import { checkPackage, type CheckResult } from "./check.js";
import { dev } from "./dev.js";
import { DEFAULT_URL, login, logout, PlatformError, PROBLEM_WORDS, putDev, readAppJson, signedIn, siteUrl, type Signed } from "./platform.js";
import { unzip } from "./zip.js";

const USAGE = `tikpal-miniapp：Tikpal 小程序开发包

  tikpal-miniapp new <slug> [--dir <目录>]   从模板起一个小程序项目
  tikpal-miniapp dev [--port 5180] [--fake preview/fake.ts]
                                              浏览器里预览（假桥），改了就重开
  tikpal-miniapp build [--out dist] [--zip <文件> | --no-zip]
                                              打包：dist/ 和 <slug>-<版本>.zip，打完照合同检查
  tikpal-miniapp check [dist | <文件>.zip]   照合同检查一个打好的包
  tikpal-miniapp verify [--out dist]          在别处再打一遍，跟 dist/ 逐字节比
  tikpal-miniapp preview [--no-open] [--app <app.json>]
                                              打包、检查，连同应用目录里的 app.json 传到开发者平台当你的开发版：
                                              只有你的账号拿得到，手机上的 Tikpal App 打开它就是这一版。不审核、不花点数
  tikpal-miniapp login [--no-open]            用你的开发者账号登录（浏览器里点「允许」），preview 第一次会自己来
  tikpal-miniapp logout                       退出登录

  --entry <文件>  代码入口（默认 src/main.tsx、main.ts、main.jsx、main.js 里先找到的）
  --url <地址>    开发者网站（默认 ${DEFAULT_URL}，也可以设 TIKPAL_DEVELOPER_URL）
  文档：开发包的 docs/miniapp.md`;

const args = process.argv.slice(3);
const flag = (name: string) => {
  const at = args.indexOf(`--${name}`);
  return at >= 0 ? args[at + 1] : undefined;
};
const positional = args.filter((a, i) => !a.startsWith("--") && !args[i - 1]?.startsWith("--"));
const project = process.cwd();
const site = () => siteUrl(flag("url") ?? process.env.TIKPAL_DEVELOPER_URL ?? DEFAULT_URL);
const open = !args.includes("--no-open");

function report(result: CheckResult): boolean {
  for (const w of result.warnings) console.warn(`  提醒：${w}`);
  for (const p of result.problems) console.error(`  ✗ ${p}`);
  return result.problems.length === 0;
}

async function main(command: string | undefined) {
  switch (command) {
    case "dev": {
      const { url } = await dev(project, { port: flag("port") ? Number(flag("port")) : undefined, fake: flag("fake"), entry: flag("entry") });
      console.log(`预览：${url}  （Ctrl-C 停）`);
      return;
    }
    case "build": {
      const built = await build(project, { out: flag("out"), entry: flag("entry"), zipFile: args.includes("--no-zip") ? false : flag("zip") });
      for (const [name, bytes, hash] of built.sizes) console.log(`  ${path.relative(project, built.out)}/${name}  ${bytes} bytes  ${hash}`);
      if (built.zip) console.log(`  ${path.relative(project, built.zip)}`);
      if (!report(built.result)) {
        console.error(`${built.manifest.slug}@${built.manifest.version}：包不合规矩，改好再打`);
        process.exit(1);
      }
      console.log(`${built.manifest.slug}@${built.manifest.version}：打好了，过了检查`);
      return;
    }
    case "check": {
      const target = path.resolve(project, positional[0] ?? "dist");
      let result: CheckResult;
      if (target.endsWith(".zip")) {
        const data = await readFile(target);
        const entries = unzip(data);
        const files = new Map(entries.filter((e) => !e.directory && !e.symlink).map((e) => [e.name, e.data]));
        result = checkPackage(files, { zipBytes: data.length, odd: entries.filter((e) => e.directory || e.symlink).map((e) => e.name) });
      } else {
        const folder = await readFolder(target);
        result = checkPackage(folder.files, { odd: folder.odd });
      }
      if (!report(result)) process.exit(1);
      console.log(`${result.manifest!.slug}@${result.manifest!.version}：过了检查`);
      return;
    }
    case "verify": {
      const problems = await verify(project, { out: flag("out"), entry: flag("entry") });
      if (problems.length) {
        console.error(`${flag("out") ?? "dist"}/ 不是这份源码现在打出来的（重新 build）：\n  ${problems.join("\n  ")}`);
        process.exit(1);
      }
      console.log(`${flag("out") ?? "dist"}/ 跟这份源码打出来的一样`);
      return;
    }
    case "login": {
      await login(site(), { open });
      return;
    }
    case "logout": {
      const was = await logout(site());
      if (!was) console.log("本来就没登录");
      else if ("other" in was) console.log(`这台电脑登录的是 ${was.other}，没动它；要退出那个：./kit miniapp logout --url ${was.other}`);
      else console.log(`退出了（${was.name}）`);
      return;
    }
    case "preview": {
      // run in an app's folder (./kit miniapp preview), the 小程序 is its miniapp/
      const here = existsSync(path.join(project, "manifest.json")) || !existsSync(path.join(project, "miniapp", "manifest.json")) ? project : path.join(project, "miniapp");
      // the app's app.json goes with it (the app's folder holds it and miniapp/): none here, and nothing is built or sent
      const appFile = path.resolve(flag("app") ?? path.join(here, "..", "app.json"));
      await readAppJson(appFile);
      const built = await build(here, { out: flag("out"), entry: flag("entry") });
      if (!report(built.result) || !built.zip) {
        console.error(`${built.manifest.slug}@${built.manifest.version}：包不合规矩，改好再传`);
        process.exit(1);
      }
      const zip = await readFile(built.zip);
      const app = await readAppJson(appFile, built.manifest.slug);
      let signed: Signed = (await signedIn(site())) ?? (await login(site(), { open }));
      let up;
      try {
        up = await putDev(signed, built.manifest.slug, zip, app);
      } catch (error) {
        // signed out from the site, or run out: sign in again, once
        if (!(error instanceof PlatformError && error.code === "CLI_TOKEN_INVALID")) throw error;
        signed = await login(site(), { open });
        up = await putDev(signed, built.manifest.slug, zip, app);
      }
      console.log(
        up.state === "dev"
          ? `${up.slug}@${up.version}：传上去了，是你的开发版。打开手机上的 Tikpal App，侧栏「小程序」里就有它；只有你的账号拿得到。`
          : `${up.slug}@${up.version}：存好了，平台检查过就是你的开发版。`,
      );
      console.log("要给测试账号或者所有人用，./kit submit 交审核。");
      return;
    }
    default:
      console.log(USAGE);
      if (command && command !== "help" && command !== "--help") process.exit(1);
  }
}

await main(process.argv[2]).catch((error: Error) => {
  if (error instanceof PlatformError) {
    console.error(`tikpal-miniapp: ${error.words}`);
    for (const p of error.problems) console.error(`  ✗ ${p.where}：${PROBLEM_WORDS[p.code] ?? p.code}`);
  } else console.error(`tikpal-miniapp: ${error.message}`);
  process.exit(1);
});
