/**
 * The kit and developer.tikpal.ai (`tikpal-miniapp login`, `logout`, `preview`; the contract's DEVELOPER_CLI): the kit
 * signs in by a code the developer allows on the site, keeps its token in $HOME/.yaykit-devkit/developer.json (only this
 * user can read it), and puts a built package up as the developer's own dev version, which only their account gets:
 * the Tikpal App on their phone then opens that version. No review, no points; nobody else sees it.
 */
import { spawn } from "node:child_process";
import { chmod, mkdir, readFile, rename, rm, writeFile } from "node:fs/promises";
import { homedir, hostname } from "node:os";
import path from "node:path";
import { cliIssued, cliMe, cliStart } from "../src/contract/developer.js";
import { MINIAPP_UPLOAD_PATH, miniappUploaded, miniappUploadProblem } from "../src/contract/miniapps.js";

export const DEFAULT_URL = "https://developer.tikpal.ai";
const LOCAL = ["localhost", "127.0.0.1", "[::1]", "::1"];
/** The developer site to talk to: https, or plain http on this computer only (a token never goes out in the clear). */
export function siteUrl(raw: string) {
  let url: URL;
  try {
    url = new URL(raw);
  } catch {
    throw new Error(`开发者网站的地址不对：${raw}`);
  }
  if (url.protocol !== "https:" && !(url.protocol === "http:" && LOCAL.includes(url.hostname))) throw new Error(`开发者网站要用 https（本机测试除外）：${raw}`);
  return url.origin;
}
/** Where the kit keeps its token: the devkit's own folder, as the kit's other state (kit setup). */
export const tokenFile = () => path.join(process.env.YAYKIT_DEVKIT_HOME ?? path.join(homedir(), ".yaykit-devkit"), "developer.json");
export type Signed = { url: string; token: string; expiresAt: string; name: string };

/** The platform said no: its code, and what was wrong with a package when it says. */
export class PlatformError extends Error {
  constructor(
    public code: string,
    public status: number,
    public problems: { where: string; code: string }[] = [],
  ) {
    super(code);
  }
}

async function call(url: string, init: RequestInit & { token?: string } = {}) {
  const { token, ...rest } = init;
  let response: Response;
  try {
    response = await fetch(url, { ...rest, headers: { ...(token ? { Authorization: `Bearer ${token}` } : {}), ...(rest.headers ?? {}) } });
  } catch {
    throw new PlatformError("OFFLINE", 0);
  }
  const text = await response.text();
  let body: Record<string, unknown> = {};
  try {
    body = text ? (JSON.parse(text) as Record<string, unknown>) : {};
  } catch {
    // not the platform's answer (an edge's 502 page): its status says it
    if (response.ok) throw new PlatformError(`HTTP_${response.status}`, response.status);
  }
  if (!response.ok) {
    const error = (body.error ?? {}) as { code?: string; details?: { problems?: unknown[] } };
    const problems = (error.details?.problems ?? []).flatMap((p) => {
      const parsed = miniappUploadProblem.safeParse(p);
      return parsed.success ? [parsed.data] : [];
    });
    throw new PlatformError(error.code ?? `HTTP_${response.status}`, response.status, problems);
  }
  return body;
}
const json = (value: unknown) => ({ method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(value) });

/** The token kept for `url`, if it has not run out. */
export async function signedIn(url: string): Promise<Signed | null> {
  const kept = await readFile(tokenFile(), "utf8").then((text) => JSON.parse(text) as Signed, () => null);
  if (!kept || kept.url !== url || Date.parse(kept.expiresAt) <= Date.now()) return null;
  return kept;
}

/** Opens the address in the browser (macOS, Linux or Windows); says it when that does not work. */
function openBrowser(address: string) {
  const [command, ...args] = process.platform === "darwin" ? ["open", address] : process.platform === "win32" ? ["cmd", "/c", "start", "", address] : ["xdg-open", address];
  try {
    spawn(command, args, { stdio: "ignore", detached: true }).on("error", () => {}).unref();
  } catch {
    // the address is printed anyway
  }
}

/**
 * Signs the kit in: a code, shown with the page to allow it on (opened in the browser unless `open` is false), then
 * asking every `interval` seconds until the developer allows it, says no, or the code runs out.
 */
export async function login(url: string, options: { open?: boolean; say?: (line: string) => void; sleep?: (ms: number) => Promise<void> } = {}): Promise<Signed> {
  const say = options.say ?? console.log;
  const sleep = options.sleep ?? ((ms: number) => new Promise((resolve) => setTimeout(resolve, ms)));
  const asked = cliStart.parse(await call(`${url}/v1/developer/cli/start`, json({ name: hostname().slice(0, 60) || "computer" })));
  const page = `${url}/cli?code=${asked.userCode}`;
  say(`在浏览器里打开 ${page}`);
  say(`用你的开发者账号登录，核对码是 ${asked.userCode}，点「允许」（${Math.round(asked.expiresIn / 60)} 分钟内有效）`);
  if (options.open !== false) openBrowser(page);
  const until = Date.now() + asked.expiresIn * 1000;
  while (Date.now() < until) {
    await sleep(asked.interval * 1000);
    try {
      const issued = cliIssued.parse(await call(`${url}/v1/developer/cli/token`, json({ deviceCode: asked.deviceCode })));
      const signed: Signed = { url, token: issued.token, expiresAt: issued.expiresAt, name: issued.developer.name };
      // written whole or not at all: a file of its own (0600) first, then put in place
      await mkdir(path.dirname(tokenFile()), { recursive: true, mode: 0o700 });
      const next = `${tokenFile()}.${process.pid}.tmp`;
      await writeFile(next, JSON.stringify(signed, null, 2) + "\n", { mode: 0o600 });
      await chmod(next, 0o600);
      await rename(next, tokenFile());
      say(`登录好了：${signed.name}（${signed.expiresAt.slice(0, 10)} 到期）`);
      return signed;
    } catch (error) {
      // not allowed yet, asked too soon, or the network or the platform in trouble: wait and ask again
      if (error instanceof PlatformError && (error.code === "CLI_PENDING" || error.code === "RATE_LIMIT" || error.code === "OFFLINE" || error.status >= 500)) continue;
      if (error instanceof PlatformError && error.code === "CLI_DENIED") throw new Error("网页上没有允许，没登录");
      if (error instanceof PlatformError && error.code === "CLI_EXPIRED") throw new Error("码过期了，再来一次：./kit miniapp login");
      throw error;
    }
  }
  throw new Error("码过期了，再来一次：./kit miniapp login");
}

/**
 * Signs the kit out of `url`: the platform forgets the token, and so does this computer. A sign-in kept for another site
 * is left as it is (`other`), so a token is never forgotten here while it still works there.
 */
export async function logout(url: string): Promise<{ name: string } | { other: string } | null> {
  const kept = await readFile(tokenFile(), "utf8").then((text) => JSON.parse(text) as Signed, () => null);
  if (!kept) return null;
  if (kept.url !== url) return { other: kept.url };
  if (Date.parse(kept.expiresAt) > Date.now()) await call(`${url}/v1/developer/cli/me`, { method: "DELETE", token: kept.token }).catch(() => undefined);
  await rm(tokenFile(), { force: true });
  return { name: kept.name };
}

/** Whose token it is, as the platform says. */
export async function whoami(signed: Signed) {
  return cliMe.parse(await call(`${signed.url}/v1/developer/cli/me`, { token: signed.token }));
}

/** Puts a built package up as the developer's own dev version. */
export async function putDev(signed: Signed, slug: string, zip: Buffer) {
  const answer = await call(`${signed.url}${MINIAPP_UPLOAD_PATH.replace(":slug", encodeURIComponent(slug))}`, {
    method: "POST",
    token: signed.token,
    headers: { "Content-Type": "application/zip" },
    body: new Uint8Array(zip),
  });
  return miniappUploaded.parse(answer);
}

/** What the platform's refusal means for the developer, in the kit's words. */
export const PROBLEM_WORDS: Record<string, string> = {
  path: "路径不允许",
  extra_file: "清单没列的文件",
  missing_file: "清单列了、包里没有",
  file_hash: "哈希对不上",
  too_many_files: "文件太多",
  too_large: "太大了",
  manifest: "manifest.json 不对",
  spec_mismatch: "跟这个版本的 app.json 对不上（app.json 要先登记上平台）",
  not_granted: "要了没批的能力，先到开发者网站的「能力和权限」申请",
  version_taken: "这个版本号已经发到预览版或正式版了，改版本号",
};
export const ERROR_WORDS: Record<string, string> = {
  APP_NOT_FOUND: "开发者网站上没有这个应用，或者不是你的",
  NOT_DEVELOPER: "这个账号还不是开发者：先在开发者网站用邀请码申请",
  TERMS_OUTDATED: "开发者协议更新了：先到开发者网站读完签了",
  DEVELOPER_UNAVAILABLE: "开发者平台暂时连不上，过一会儿再试",
  MINIAPP_STORAGE_UNAVAILABLE: "平台暂时存不了小程序，过一会儿再试",
  MINIAPP_TOO_LARGE: "包压缩后超过 5 MB",
  OFFLINE: "连不上开发者网站：看看网络",
};
