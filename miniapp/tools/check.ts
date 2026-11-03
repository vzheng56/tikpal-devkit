/**
 * What a package must be before the platform takes it (`tikpal-miniapp check`, and the end of every build): the
 * contract's manifest with every file's SHA-256 and nothing more or less in the package; paths, counts and the zip's
 * size as the package format allows; no way out but the bridge (no network APIs, nothing loaded from elsewhere, a CSP
 * that keeps it so); every bridge method it names declared, at a capability level the manifest asks for; no keys.
 * Problems stop the build; warnings only say.
 */
import { createHash } from "node:crypto";
import path from "node:path";
import { MINIAPP_CAPABILITY, phoneAbilities } from "../src/contract/miniapp-capabilities.js";
import { MINIAPP, MINIAPP_LIMITS, miniappManifest, type MiniappManifest } from "../src/contract/miniapps.js";
import { METHODS } from "../src/methods.js";
import { looserThan, MINIAPP_CSP } from "./csp.js";

const MB = (bytes: number) => `${(bytes / 1048576).toFixed(bytes % 1048576 ? 2 : 0)} MB`;

/**
 * What the WebView has of its own (kind 1 capabilities): code that reaches for it without the manifest naming the
 * capability gets nothing in the app (the container's Permissions-Policy turns it off).
 */
const WEBVIEW_OWN: [RegExp, string[], string][] = [
  [/\bgetUserMedia\b/, ["camera", "microphone"], "getUserMedia"],
  [/\b(?:devicemotion|deviceorientation|DeviceMotionEvent|DeviceOrientationEvent)\b/, ["motion"], "姿态（devicemotion、deviceorientation）"],
  [/\bgeolocation\b/, ["location"], "定位（navigator.geolocation）"],
];

const TEXT = /\.(html?|m?js|css|json|svg|txt|md)$/;
const sha256 = (data: Buffer) => createHash("sha256").update(data).digest("hex");

/** Ways out of the page the container's CSP shuts anyway; a package that uses them is broken there. */
const BANNED: [RegExp, string][] = [
  [/(?<![\w$])fetch\s*\(/, "fetch()"],
  [/\bXMLHttpRequest\b/, "XMLHttpRequest"],
  [/\bWebSocket\b/, "WebSocket"],
  [/\bEventSource\b/, "EventSource"],
  [/\bsendBeacon\b/, "navigator.sendBeacon"],
  [/\bRTCPeerConnection\b/, "RTCPeerConnection"],
  [/\bimportScripts\s*\(/, "importScripts()"],
  [/(?<![\w$.])eval\s*\(/, "eval()"],
  [/\bnew\s+Function\s*\(/, "new Function()"],
];

/**
 * What Android's oldest system WebView (Chrome 87: a Redmi 8A on Android 10, no Play to update it) does not have and
 * esbuild does not rewrite: it only stops syntax, such as top-level await (#1123 §3.1 补充 2).
 */
const NOT_IN_CHROME_87: [RegExp, string][] = [
  [/\bstructuredClone\b/, "structuredClone"],
  [/\bObject\.hasOwn\b/, "Object.hasOwn"],
  [/\.at\(/, ".at()"],
  [/\.findLast(?:Index)?\(/, "findLast()"],
  [/\.toSorted\(/, "toSorted()"],
  [/\.toReversed\(/, "toReversed()"],
  [/\.toSpliced\(/, "toSpliced()"],
  [/\brandomUUID\s*\(/, "crypto.randomUUID()"],
  [/\bObject\.groupBy\b|\bMap\.groupBy\b/, "Object.groupBy()"],
  [/\bPromise\.withResolvers\b/, "Promise.withResolvers()"],
];

/** Sounds a WebView can play from the package; Ogg Opus it cannot (iOS's not at all). */
const OGG = /\.(ogg|oga|opus)$/;

/** Keys and secrets that must never be in a package (the app signs in for it; a 小程序 needs no key of its own). */
const SECRETS: [RegExp, string][] = [
  [/-----BEGIN [A-Z ]*PRIVATE KEY-----/, "a private key"],
  [/\bMIG[EH]AgEAMBMGByqGSM49|\bMII[EJ][A-Za-z0-9+/]{2}AgEAMA0GCSqGSIb3DQEB/, "a private key (base64 PKCS#8)"],
  [/\bsk-[A-Za-z0-9_-]{20,}/, "an API key (sk-…)"],
  [/\bAKIA[0-9A-Z]{16}\b/, "an AWS access key"],
  [/\bAIza[0-9A-Za-z_-]{35}\b/, "a Google API key"],
  [/\bgh[pousr]_[A-Za-z0-9]{36}\b/, "a GitHub token"],
  [/\bxox[abposr]-[A-Za-z0-9-]{10,}/, "a Slack token"],
  [/\b(?:api[_-]?key|secret|access[_-]?token|auth[_-]?token)["']?\s*[:=]\s*["'][A-Za-z0-9_\-+/=.]{16,}["']/i, "a key in the code"],
  [/\bBearer\s+[A-Za-z0-9_\-.=]{20,}/, "a bearer token"],
];

export type CheckResult = { problems: string[]; warnings: string[]; manifest: MiniappManifest | null };

/** A reference in a page or stylesheet: fine, or why not. Local ones must be files of the package. */
function reference(value: string, from: string, files: Set<string>, kind: "html" | "css"): string | null {
  const v = value.trim();
  if (!v || v.startsWith("#")) return null;
  if (/^data:/i.test(v)) return null;
  if (/^[a-z][a-z0-9+.-]*:/i.test(v) || v.startsWith("//")) return `${from} 引用了外面的地址 ${v.slice(0, 80)}：小程序只能用包里的文件`;
  if (v.startsWith("/")) return `${from} 用了绝对路径 ${v}：改成相对路径（包不在网站的根上）`;
  const target = path.posix.normalize(path.posix.join(path.posix.dirname(from), v.replace(/[?#].*$/, "")));
  if (target.startsWith("..") || !files.has(target)) return `${from} 引用的 ${v} 不在包里${kind === "css" ? "" : "（打包后的文件名是 app.js、app.css）"}`;
  return null;
}

/** The entry page's own CSP (the <meta http-equiv> one), or null. */
function contentSecurityPolicy(html: string): string | null {
  for (const tag of html.match(/<meta\b[^>]*>/gi) ?? []) {
    if (!/http-equiv\s*=\s*["']?content-security-policy/i.test(tag)) continue;
    const content = /content\s*=\s*(?:"([^"]*)"|'([^']*)')/i.exec(tag);
    if (content) return content[1] ?? content[2];
  }
  return null;
}

function checkHtml(name: string, html: string, files: Set<string>, problems: string[], entry: boolean) {
  if (entry) {
    const csp = contentSecurityPolicy(html);
    if (csp === null) problems.push(`${name} 没有 Content-Security-Policy：照模板的 index.html 加上那一行 <meta>（${MINIAPP_CSP}）`);
    else for (const why of looserThan(csp)) problems.push(`${name} 的 CSP：${why}`);
  }
  if (/<base\b/i.test(html)) problems.push(`${name} 有 <base>：去掉，用相对路径`);
  for (const script of html.matchAll(/<script\b([^>]*)>([\s\S]*?)<\/script>/gi))
    if (!/\bsrc\s*=/.test(script[1]) && script[2].trim()) problems.push(`${name} 有写在页面里的 <script>：CSP 不让它跑，挪进 src/`);
  for (const tag of html.match(/<[a-z][^>]*>/gi) ?? []) {
    if (/\son[a-z]+\s*=/i.test(tag)) problems.push(`${name} 的 ${tag.slice(0, 40)}… 写了 on…= 事件：CSP 不让它跑，在代码里 addEventListener`);
    for (const attr of tag.matchAll(/\s(src|href|action|formaction|poster|data|srcset)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+))/gi)) {
      const value = attr[2] ?? attr[3] ?? attr[4] ?? "";
      const values = attr[1].toLowerCase() === "srcset" ? value.split(",").map((v) => v.trim().split(/\s+/)[0]) : [value];
      for (const v of values) {
        const why = reference(v, name, files, "html");
        if (why) problems.push(why);
      }
    }
  }
}

/**
 * Checks a package: `files` every file in it by path (manifest.json included), `zipBytes` the zip's size when there
 * is one, `odd` entries that are not plain files (folders in a zip, links).
 */
export function checkPackage(files: Map<string, Buffer>, options: { zipBytes?: number; odd?: string[] } = {}): CheckResult {
  const problems: string[] = [];
  const warnings: string[] = [];
  for (const odd of options.odd ?? []) problems.push(`${odd} 不是普通文件：包里不许有符号链接`);
  const raw = files.get("manifest.json");
  if (!raw) return { problems: [...problems, "包的根目录没有 manifest.json"], warnings, manifest: null };
  let json: unknown;
  try {
    json = JSON.parse(raw.toString("utf8"));
  } catch (error) {
    return { problems: [...problems, `manifest.json 不是 JSON：${(error as Error).message}`], warnings, manifest: null };
  }
  const names = new Set([...files.keys()].filter((n) => n !== "manifest.json"));
  for (const file of names) {
    if (OGG.test(file)) problems.push(`${file}：Ogg Opus 在 WebView 里放不了，音效用 mp3、m4a 或 wav`);
    if (file.length > MINIAPP.maxPathLength || !MINIAPP.path.test(file))
      problems.push(`${file}：文件名和路径只能用小写字母、数字和 . _ - /，最长 ${MINIAPP.maxPathLength} 个字`);
  }
  const parsed = miniappManifest.safeParse(json);
  if (!parsed.success) {
    for (const issue of parsed.error.issues)
      if (!(issue.path[0] === "files" && problems.length)) problems.push(`manifest.json ${issue.path.join(".") || "(整个)"}：${issue.message}`);
    return { problems, warnings, manifest: null };
  }
  const manifest = parsed.data;

  if (!manifest.files) problems.push("manifest.json 没有 files（每个文件的 SHA-256）：用 tikpal-miniapp build 打包");
  else {
    for (const [file, hash] of Object.entries(manifest.files)) {
      const data = files.get(file);
      if (!data) problems.push(`manifest.json 列了 ${file}，包里没有`);
      else if (sha256(data) !== hash) problems.push(`${file} 的 SHA-256 跟 manifest.json 里的不一样：重新打包`);
    }
    for (const file of names) if (!(file in manifest.files)) problems.push(`${file} 在包里，manifest.json 的 files 没列：重新打包`);
  }
  if (names.size > MINIAPP.maxFiles) problems.push(`包里有 ${names.size} 个文件，最多 ${MINIAPP.maxFiles} 个`);
  if (options.zipBytes !== undefined && options.zipBytes > MINIAPP_LIMITS.zipBytes)
    problems.push(`zip 有 ${MB(options.zipBytes)}，最大 ${MB(MINIAPP_LIMITS.zipBytes)}`);
  let unpacked = 0;
  for (const [file, data] of files) {
    unpacked += data.length;
    if (data.length > MINIAPP_LIMITS.fileBytes) problems.push(`${file} 有 ${MB(data.length)}，一个文件最大 ${MB(MINIAPP_LIMITS.fileBytes)}`);
  }
  if (unpacked > MINIAPP_LIMITS.unpackedBytes) problems.push(`包解开有 ${MB(unpacked)}，最大 ${MB(MINIAPP_LIMITS.unpackedBytes)}`);
  if (manifest.capabilityLevel > MINIAPP.capabilityLevel)
    warnings.push(`capabilityLevel 是 ${manifest.capabilityLevel}：现在的 App 只到第 ${MINIAPP.capabilityLevel} 级，打不开这个包，要等容器做到第 ${manifest.capabilityLevel} 级`);
  for (const capability of manifest.capabilities) {
    const only = (MINIAPP_CAPABILITY[capability] as { only?: readonly string[] }).only;
    if (only && !only.includes(manifest.slug)) problems.push(`能力 "${capability}" 只给 ${only.join("、")}，${manifest.slug} 不能声明`);
  }
  if (!files.has(manifest.entry)) problems.push(`入口 ${manifest.entry} 不在包里`);

  const code: string[] = [];
  for (const [file, data] of files) {
    if (!TEXT.test(file)) continue;
    const text = data.toString("utf8");
    for (const [pattern, what] of SECRETS) if (pattern.test(text)) problems.push(`${file} 里像是有 ${what}：小程序不要 API key，也不许带密钥`);
    if (/\.html?$/.test(file)) {
      checkHtml(file, text, names, problems, file === manifest.entry);
      code.push(text);
    } else if (/\.m?js$/.test(file)) {
      for (const [pattern, what] of BANNED) if (pattern.test(text)) problems.push(`${file} 用了 ${what}：小程序不直接连网，也不许动态执行代码，一切走桥`);
      for (const [pattern, what] of NOT_IN_CHROME_87) if (pattern.test(text)) problems.push(`${file} 用了 ${what}：最老的安卓 WebView（Chrome 87）没有，换个写法`);
      code.push(text);
    } else if (file.endsWith(".css")) {
      for (const url of text.matchAll(/url\(\s*(["']?)([^"')]+)\1\s*\)/g)) {
        const why = reference(url[2], file, names, "css");
        if (why) problems.push(why);
      }
      for (const imported of text.matchAll(/@import\s+(?:url\()?\s*["']?([^"');\s]+)/g)) {
        const why = reference(imported[1], file, names, "css");
        if (why) problems.push(why);
      }
    }
  }

  const all = code.join("\n");
  for (const [pattern, capabilities, what] of WEBVIEW_OWN)
    if (pattern.test(all) && !capabilities.some((c) => (manifest.capabilities as readonly string[]).includes(c)))
      problems.push(`用了${what}，manifest.json 的 capabilities 要声明 ${capabilities.map((c) => `"${c}"`).join(" 或 ")}`);
  for (const [method, spec] of Object.entries(METHODS)) {
    const named = new RegExp(`["'\`]${method.replace(".", "\\.")}["'\`]`).test(all);
    if (!named) continue;
    if (spec.capability && !manifest.capabilities.includes(spec.capability))
      problems.push(`用了 ${method}，manifest.json 的 capabilities 没声明 "${spec.capability}"`);
    if (spec.level > manifest.capabilityLevel)
      problems.push(`用了 ${method}（能力套装 ${spec.level} 才有），manifest.json 的 capabilityLevel 是 ${manifest.capabilityLevel}`);
  }
  return { problems, warnings, manifest };
}

/**
 * The app's app.json beside the project (an app made by `kit new`, the 小程序 in its miniapp/): its `miniapp` part must
 * say what manifest.json says, as the platform checks at upload (app-spec.ts); a phone ability the page uses needs its
 * why in `permissions.phone`, and records and settings need app.json's own. Nothing when they agree.
 */
export function checkAppJson(app: Record<string, unknown>, manifest: Omit<MiniappManifest, "files">): string[] {
  const problems: string[] = [];
  const part = app.miniapp as { entry?: unknown; capabilities?: unknown; capabilityLevel?: unknown; minFirmware?: unknown } | undefined;
  const wanted = {
    entry: manifest.entry,
    capabilities: manifest.capabilities,
    capabilityLevel: manifest.capabilityLevel,
    ...(manifest.minFirmware ? { minFirmware: manifest.minFirmware.split("@")[1] } : {}),
  };
  if (app.slug !== manifest.slug) problems.push(`app.json 的 slug 是 ${String(app.slug)}，manifest.json 是 ${manifest.slug}：小程序跟应用用同一个 slug`);
  if (!part || typeof part !== "object") {
    problems.push(`app.json 要有 miniapp 一段，跟 manifest.json 一致：\n    "miniapp": ${JSON.stringify(wanted)}`);
  } else {
    if (part.entry !== wanted.entry) problems.push(`app.json 的 miniapp.entry 要是 ${wanted.entry}`);
    const listed = Array.isArray(part.capabilities) ? [...part.capabilities].sort().join(",") : null;
    if (listed !== [...wanted.capabilities].sort().join(",")) problems.push(`app.json 的 miniapp.capabilities 要跟 manifest.json 一样：${JSON.stringify(wanted.capabilities)}`);
    if (part.capabilityLevel !== wanted.capabilityLevel) problems.push(`app.json 的 miniapp.capabilityLevel 要是 ${wanted.capabilityLevel}`);
    if ((part.minFirmware ?? undefined) !== wanted.minFirmware)
      problems.push(wanted.minFirmware ? `app.json 的 miniapp.minFirmware 要是 "${wanted.minFirmware}"` : "app.json 的 miniapp.minFirmware 去掉（manifest.json 的 minFirmware 是 null）");
  }
  const phone = Object.keys(((app.permissions as { phone?: object } | undefined)?.phone ?? {}) as object);
  const asked = manifest.capabilities.filter((c) => (phoneAbilities as readonly string[]).includes(c));
  for (const ability of asked) if (!phone.includes(ability)) problems.push(`用 "${ability}" 要在 app.json 的 permissions.phone.${ability} 写为什么（App 问用户时照它说）`);
  for (const ability of phone) if (!(asked as string[]).includes(ability)) problems.push(`app.json 的 permissions.phone.${ability} 写了，小程序却没声明 "${ability}"`);
  if (manifest.capabilities.includes("records") && !app.records) problems.push('用 "records" 要在 app.json 写 records（记录的种类和 schema，见 app-json.md）');
  const settings = app.settings && typeof app.settings === "object" ? Object.keys(app.settings) : [];
  if (manifest.capabilities.includes("settings") && !settings.length) problems.push('用 "settings" 要在 app.json 写 settings');
  return problems;
}
