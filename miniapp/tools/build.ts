/**
 * Builds a 小程序 project into its package (`tikpal-miniapp build`): the folder (dist/) and the zip to hand the
 * platform, which signs it (the kit never does).
 *
 * The project: manifest.json (the contract's, without `files`), the entry page it names (index.html, its CSP <meta>
 * written in from the contract's MINIAPP_CSP), the code from
 * src/main.(tsx|ts|jsx|js) bundled into app.js and app.css, and public/ copied as it is. The package: those files,
 * and manifest.json with every file's SHA-256 (`files`), checked (tools/check.ts) before anything is said to be done;
 * in an app made by `kit new` (the project its miniapp/), against app.json's `miniapp` part too.
 *
 * The same sources write the same bytes on any machine: esbuild as pinned, minified, no source map, nothing from the
 * clock or the paths, licences gathered at the end of app.js. `verify` builds again elsewhere and compares, which is
 * how a review knows the package came from these sources.
 */
import { build as esbuild, type BuildOptions } from "esbuild";
import { createHash } from "node:crypto";
import { cp, mkdir, mkdtemp, readdir, readFile, rm, stat, writeFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { miniappManifest, type MiniappManifest } from "../src/contract/miniapps.js";
import { allowedMethods } from "../src/methods.js";
import { checkAppJson, checkPackage, type CheckResult } from "./check.js";
import { withCsp } from "./csp.js";
import { zip } from "./zip.js";

export const ENTRIES = ["src/main.tsx", "src/main.ts", "src/main.jsx", "src/main.js"];
/** The WebViews a page must run in (#1123 §3.1 补充 2). */
export const TARGET = ["safari16", "chrome87"];

/** manifest.json as the project has it: the contract's, `files` left to the build. */
export async function readManifest(project: string): Promise<Omit<MiniappManifest, "files">> {
  const file = path.join(project, "manifest.json");
  const json = JSON.parse(await readFile(file, "utf8").catch(() => {
    throw new Error(`${file}: 没有 manifest.json（tikpal-miniapp new 起的项目里有）`);
  }));
  if (json && typeof json === "object" && "files" in json) throw new Error("manifest.json：files 由打包工具写，项目里的清单别写");
  const parsed = miniappManifest.safeParse(json);
  if (!parsed.success) throw new Error(`manifest.json 不合合同：\n  ${parsed.error.issues.map((i) => `${i.path.join(".") || "(整个)"}：${i.message}`).join("\n  ")}`);
  return parsed.data;
}

export function scriptEntry(project: string, entry?: string): string | null {
  if (entry) return entry;
  return ENTRIES.find((e) => existsSync(path.join(project, e))) ?? null;
}

/** esbuild's settings for the page, shared with the preview (which adds a source map and leaves it readable). */
export function pageOptions(project: string, entry: string, manifest: Pick<MiniappManifest, "capabilities">): BuildOptions {
  return {
    absWorkingDir: project,
    entryPoints: { app: entry },
    bundle: true,
    format: "esm",
    // iOS 17's WKWebView, and Android system WebViews from Chrome 87 (a Redmi 8A on Android 10 has 87 and no Play to
    // update it): esbuild stops on syntax 87 cannot run, top-level await among it; check.ts stops the APIs it lacks.
    target: TARGET,
    jsx: "automatic",
    charset: "utf8",
    loader: { ".png": "dataurl", ".jpg": "dataurl", ".jpeg": "dataurl", ".gif": "dataurl", ".webp": "dataurl", ".svg": "dataurl", ".woff2": "file", ".woff": "file", ".ttf": "file", ".mp3": "file", ".m4a": "file", ".wav": "file" },
    assetNames: "assets/[name]",
    define: { __TIKPAL_ALLOWED__: JSON.stringify(allowedMethods(manifest.capabilities)) },
    logLevel: "warning",
  };
}

async function walk(dir: string, base = dir): Promise<string[]> {
  const out: string[] = [];
  for (const entry of await readdir(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) out.push(...(await walk(full, base)));
    else out.push(path.relative(base, full).split(path.sep).join("/"));
  }
  return out.sort();
}

/** Every file of a built folder, and what in it is not a plain file. */
export async function readFolder(dir: string): Promise<{ files: Map<string, Buffer>; odd: string[] }> {
  const files = new Map<string, Buffer>();
  const odd: string[] = [];
  const visit = async (sub: string) => {
    for (const entry of await readdir(path.join(dir, sub), { withFileTypes: true })) {
      const name = sub ? `${sub}/${entry.name}` : entry.name;
      if (entry.isDirectory()) await visit(name);
      else if (entry.isFile()) files.set(name, await readFile(path.join(dir, name)));
      else odd.push(name);
    }
  };
  await visit("");
  return { files, odd };
}

export type Built = { out: string; zip: string | null; manifest: MiniappManifest; result: CheckResult; sizes: [string, number, string][] };

/**
 * Builds `project` into `out` (emptied first), and the zip unless `zipFile` is false (default
 * `<project>/<slug>-<version>.zip`). Throws when the project cannot be built; check problems come back in `result`.
 */
export async function build(project: string, options: { out?: string; entry?: string; zipFile?: string | false; appJson?: false } = {}): Promise<Built> {
  const source = await readManifest(project);
  const out = path.resolve(project, options.out ?? "dist");
  if (!existsSync(path.join(project, source.entry))) throw new Error(`manifest.json 的入口 ${source.entry} 在项目里没有`);
  await rm(out, { recursive: true, force: true });
  await mkdir(out, { recursive: true });

  const publicDir = path.join(project, "public");
  if (existsSync(publicDir)) await cp(publicDir, out, { recursive: true, verbatimSymlinks: true });
  const entry = scriptEntry(project, options.entry);
  if (entry) {
    await esbuild({
      ...pageOptions(project, entry, source),
      outdir: out,
      minify: true,
      sourcemap: false,
      legalComments: "eof",
      define: { ...pageOptions(project, entry, source).define, "process.env.NODE_ENV": '"production"' },
    });
  }
  await mkdir(path.dirname(path.join(out, source.entry)), { recursive: true });
  // The CSP is the contract's, written in here: never copied by hand into a page.
  await writeFile(path.join(out, source.entry), withCsp(await readFile(path.join(project, source.entry), "utf8")));

  const names = (await walk(out)).filter((n) => n !== "manifest.json");
  const files: Record<string, string> = {};
  for (const name of names) files[name] = createHash("sha256").update(await readFile(path.join(out, name))).digest("hex");
  const manifest: MiniappManifest = { ...source, files };
  await writeFile(path.join(out, "manifest.json"), JSON.stringify(manifest, null, 2) + "\n");

  const folder = await readFolder(out);
  let zipPath: string | null = null;
  let zipBytes: number | undefined;
  const data = options.zipFile !== false ? zip([...folder.files].map(([name, body]) => ({ name, data: body }))) : null;
  if (data) zipBytes = data.length;
  const sizes: [string, number, string][] = [...folder.files].map(([name, body]) => [name, body.length, createHash("sha256").update(body).digest("hex").slice(0, 12)]);
  const result = checkPackage(folder.files, { zipBytes, odd: folder.odd });
  // In an app made by `kit new` (the project its miniapp/), app.json's miniapp part must agree.
  const app = await readFile(path.join(project, "..", "app.json"), "utf8").then((t) => JSON.parse(t) as Record<string, unknown>, () => null);
  if (app && options.appJson !== false) result.problems.push(...checkAppJson(app, source));
  // The zip is what goes to the platform: only one that passed. A failed build leaves dist/ to look at, and takes away
  // an older zip of this version, which these sources no longer make.
  if (data) {
    const file = path.resolve(project, options.zipFile || `${source.slug}-${source.version}.zip`);
    if (result.problems.length) await rm(file, { force: true });
    else {
      await writeFile(file, data);
      zipPath = file;
    }
  }
  return { out, zip: zipPath, manifest, result, sizes };
}

/** What differs between two built folders: nothing when they are the same. */
export async function compare(a: string, b: string): Promise<string[]> {
  const [left, right] = [await readFolder(a).catch(() => ({ files: new Map<string, Buffer>(), odd: [] })), await readFolder(b)];
  const problems: string[] = [];
  const names = [...new Set([...left.files.keys(), ...right.files.keys()])].sort();
  const hash = (data?: Buffer) => (data ? createHash("sha256").update(data).digest("hex").slice(0, 12) : "missing");
  for (const name of names) {
    const [x, y] = [hash(left.files.get(name)), hash(right.files.get(name))];
    if (x !== y) problems.push(`${name}: ${x} ≠ ${y}`);
  }
  return problems;
}

/** Builds again in a folder of its own and compares with `dist`: nothing when `dist` is what these sources write. */
export async function verify(project: string, options: { out?: string; entry?: string } = {}): Promise<string[]> {
  const fresh = await mkdtemp(path.join(tmpdir(), "tikpal-miniapp-"));
  try {
    await build(project, { out: fresh, entry: options.entry, zipFile: false, appJson: false });
    return await compare(path.resolve(project, options.out ?? "dist"), fresh);
  } finally {
    await rm(fresh, { recursive: true, force: true });
  }
}

export async function isFile(file: string) {
  return (await stat(file).catch(() => null))?.isFile() ?? false;
}
