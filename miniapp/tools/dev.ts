/**
 * `tikpal-miniapp dev`: the page in a browser, in the preview's shell, with the pretend bridge (preview/), rebuilt
 * and reopened on every change. Only on this computer: it listens on 127.0.0.1, answers only a Host of 127.0.0.1 or
 * localhost (a page elsewhere cannot rebind its name to it), and serves only its own files, the page's bundle and
 * public/, never a path outside them. Nothing reaches a server.
 *
 *   /                  the shell: the phone-sized frame and the panel
 *   /app/              the entry page (manifest.json's entry), as /app/index.html
 *   /app/<file>        the page, under the container's CSP: the entry with the pretend bridge put in before its
 *                      scripts, the bundle, public/
 *   /__tikpal/…        the shell's and the bridge's scripts, the manifest, and the reload events
 */
import { context, type BuildContext, type Plugin } from "esbuild";
import { existsSync, statSync, watch } from "node:fs";
import { mkdtemp, readFile } from "node:fs/promises";
import { createServer, type ServerResponse } from "node:http";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { pageOptions, readManifest, scriptEntry, TARGET } from "./build.js";
import { MINIAPP_CAPABILITY } from "../src/contract/miniapp-capabilities.js";
import { MINIAPP_CSP, miniappFeaturePolicy, miniappPermissionsPolicy } from "../src/contract/miniapps.js";
import { withCsp } from "./csp.js";

const SDK = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const TYPES: Record<string, string> = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".gif": "image/gif",
  ".webp": "image/webp",
  ".woff2": "font/woff2",
  ".woff": "font/woff",
  ".ttf": "font/ttf",
  ".mp3": "audio/mpeg",
  ".m4a": "audio/mp4",
  ".wav": "audio/wav",
};

export async function dev(project: string, options: { port?: number; fake?: string; entry?: string } = {}) {
  const out = await mkdtemp(path.join(tmpdir(), "tikpal-preview-"));
  const clients = new Set<ServerResponse>();
  let error: string | null = null;
  const reload = () => {
    for (const c of clients) c.write("event: reload\ndata: {}\n\n");
  };
  const reporter = (what: string): Plugin => ({
    name: "report",
    setup(b) {
      b.onEnd((result) => {
        const failed = result.errors.map((e) => `${e.location ? `${e.location.file}:${e.location.line}  ` : ""}${e.text}`);
        error = failed.length ? `${what} 编不过：\n${failed.join("\n")}` : null;
        reload();
      });
    },
  });

  const fake = options.fake ? path.resolve(project, options.fake) : ["preview/fake.ts", "preview/fake.tsx", "preview/fake.js"].map((f) => path.join(project, f)).find(existsSync);
  const bridge = await context({
    stdin: {
      contents: [
        `import { install } from ${JSON.stringify(path.join(SDK, "preview/fake-bridge.ts"))};`,
        fake ? `import * as custom from ${JSON.stringify(fake)};` : "const custom = {};",
        "install(custom);",
      ].join("\n"),
      resolveDir: project,
      loader: "ts",
      sourcefile: "preview-bridge.ts",
    },
    bundle: true,
    format: "iife",
    target: TARGET,
    sourcemap: "inline",
    outfile: path.join(out, "__tikpal/fake-bridge.js"),
    logLevel: "warning",
    plugins: [reporter("preview/fake.ts")],
  });
  await bridge.rebuild().catch(() => undefined);
  await bridge.watch();
  const shell = await context({
    entryPoints: [path.join(SDK, "preview/shell.ts")],
    bundle: true,
    format: "esm",
    // The shell runs in the desktop browser, not in the app.
    target: ["safari17", "chrome120"],
    outfile: path.join(out, "__tikpal/shell.js"),
    logLevel: "warning",
  });
  await shell.rebuild();
  await shell.dispose();

  // The page's bundle knows its manifest's methods, so a change to the manifest makes a new one.
  let page: BuildContext | null = null;
  let manifestText = "";
  async function startPage() {
    const text = await readFile(path.join(project, "manifest.json"), "utf8").catch(() => "");
    if (page && text === manifestText) return;
    manifestText = text;
    await page?.dispose();
    page = null;
    let manifest;
    try {
      manifest = await readManifest(project);
    } catch (e) {
      error = (e as Error).message;
      reload();
      return;
    }
    const entry = scriptEntry(project, options.entry);
    if (!entry) {
      error = null;
      reload();
      return;
    }
    page = await context({
      ...pageOptions(project, entry, manifest),
      outdir: path.join(out, "app"),
      sourcemap: "inline",
      // Not __TIKPAL_ALLOWED__: in the preview the pretend bridge answers an undeclared method itself, as the app
      // would, so the call shows in the panel's list too.
      define: { "process.env.NODE_ENV": '"development"' },
      plugins: [reporter("页面")],
    });
    // The first build before anything is served, then on every change.
    await page.rebuild().catch(() => undefined);
    await page.watch();
  }
  await startPage();

  // What esbuild does not watch: the entry page, the manifest, public/.
  const watcher = watch(project, { recursive: true }, (_event, file) => {
    if (!file) return;
    const name = file.split(path.sep).join("/");
    if (name.startsWith("node_modules/") || name.startsWith("dist/") || name.startsWith(".git/")) return;
    if (name === "manifest.json") void startPage().then(reload);
    else if (name.startsWith("public/") || name.endsWith(".html")) reload();
  });

  // The page gets the container's CSP, Permissions-Policy and Feature-Policy (the older header, which a WebView 87
  // reads) as headers, as in the app (the container doc §3.1), and its CSP <meta> as the build writes it.
  let allowed: string[] = [];
  const send = (res: ServerResponse, status: number, type: string, body: string | Buffer, page = false) => {
    const headers = page
      ? { "content-security-policy": MINIAPP_CSP, "permissions-policy": miniappPermissionsPolicy(allowed), "feature-policy": miniappFeaturePolicy(allowed) }
      : {};
    res.writeHead(status, { "content-type": type, "cache-control": "no-store", "x-content-type-options": "nosniff", ...headers });
    res.end(body);
  };
  /** The file under `dir` that `relative` names, or null: never anything outside it, however the path is written. */
  const inside = (dir: string, relative: string) => {
    const full = path.resolve(dir, relative);
    const rel = path.relative(dir, full);
    return rel && !rel.startsWith("..") && !path.isAbsolute(rel) && statSync(full, { throwIfNoEntry: false })?.isFile() ? full : null;
  };
  /** The preview's own files: these, and nothing else under /__tikpal/. */
  const own = new Map([
    ["/__tikpal/shell.js", path.join(out, "__tikpal/shell.js")],
    ["/__tikpal/fake-bridge.js", path.join(out, "__tikpal/fake-bridge.js")],
  ]);
  let port = 0;
  const server = createServer(async (req, res) => {
    // Only pages of this computer's own: a page elsewhere that points its name at 127.0.0.1 (DNS rebinding) gets nothing.
    const host = String(req.headers.host ?? "");
    if (host !== `127.0.0.1:${port}` && host !== `localhost:${port}`) return send(res, 403, "text/plain; charset=utf-8", "预览只认 127.0.0.1 和 localhost");
    let route: string;
    try {
      route = decodeURIComponent(new URL(req.url ?? "/", "http://127.0.0.1").pathname);
    } catch {
      return send(res, 400, "text/plain; charset=utf-8", "看不懂的地址");
    }
    if (route.includes("\0")) return send(res, 400, "text/plain; charset=utf-8", "看不懂的地址");
    try {
      if (route === "/") return send(res, 200, TYPES[".html"], await readFile(path.join(SDK, "preview/shell.html")));
      if (route === "/__tikpal/events") {
        res.writeHead(200, { "content-type": "text/event-stream", "cache-control": "no-store", connection: "keep-alive" });
        res.write(": hello\n\n");
        clients.add(res);
        req.on("close", () => clients.delete(res));
        return;
      }
      if (route === "/__tikpal/state") {
        const manifest = await readManifest(project).catch(() => null);
        return send(res, 200, TYPES[".json"], JSON.stringify({ manifest, error }));
      }
      const mine = own.get(route);
      if (mine) return send(res, 200, TYPES[".js"], await readFile(mine));
      if (route === "/app") return res.writeHead(302, { location: "/app/" }).end();
      if (route.startsWith("/app/")) {
        const manifest = await readManifest(project).catch(() => null);
        allowed = (manifest?.capabilities ?? []).filter((c) => MINIAPP_CAPABILITY[c].kind === 1);
        const asked = route.slice("/app/".length);
        const file = asked === "" ? (manifest?.entry ?? "index.html") : asked;
        const entry = manifest && file === manifest.entry ? inside(project, file) : null;
        if (manifest && entry) {
          const lite = { slug: manifest.slug, version: manifest.version, capabilities: manifest.capabilities, capabilityLevel: manifest.capabilityLevel };
          const tag = `<script src="/__tikpal/fake-bridge.js" data-manifest='${JSON.stringify(lite).replace(/'/g, "&#39;")}'></script>`;
          const html = withCsp(await readFile(entry, "utf8"));
          const at = html.search(/<script\b/i);
          const body = at >= 0 ? html.slice(0, at) + tag + "\n    " + html.slice(at) : html.replace(/<\/head>/i, `${tag}</head>`);
          return send(res, 200, TYPES[".html"], body, true);
        }
        for (const dir of [path.join(out, "app"), path.join(project, "public")]) {
          const full = inside(dir, file);
          if (full) return send(res, 200, TYPES[path.extname(full)] ?? "application/octet-stream", await readFile(full), true);
        }
      }
      send(res, 404, "text/plain; charset=utf-8", `没有 ${route}`);
    } catch (e) {
      console.error(e);
      send(res, 500, "text/plain; charset=utf-8", "预览出错了，看终端");
    }
  });
  await new Promise<void>((resolve, reject) => {
    const asked = options.port ?? Number(process.env.PORT ?? 5180);
    server.once("error", (e: NodeJS.ErrnoException) =>
      reject(e.code === "EADDRINUSE" ? new Error(`端口 ${asked} 被占了：换一个，npm run dev -- --port 5199`) : e),
    );
    server.listen(asked, "127.0.0.1", () => {
      port = (server.address() as { port: number }).port;
      resolve();
    });
  });
  // 127.0.0.1, not localhost: localhost may go to ::1 first, where something else can sit on the same port.
  return {
    url: `http://127.0.0.1:${port}/`,
    port,
    close: async () => {
      watcher.close();
      for (const c of clients) c.end();
      await new Promise((r) => server.close(r));
      await page?.dispose();
      await bridge.dispose();
    },
  };
}
