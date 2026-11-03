/**
 * The preview's pretend `window.tikpal` (never in a package). It answers as the app's bridge does: the method must be
 * the contract's, the params what it takes (BAD_PARAMS), its capability declared in manifest.json (NOT_DECLARED), its
 * level one the container has (UNSUPPORTED); then the panel's 拒绝 (NOT_DECLARED, as the app answers a capability it
 * does not give), 未登录 (SIGNED_OUT) and 断网 (OFFLINE) for what needs the server. Answers come from the project's
 * preview/fake.ts where it has one, else from the contract's fixtures; every answer and event is checked against the
 * contract, and the panel lists each call.
 *
 * Level 2's own screens (scan, photos) are the browser's prompt and confirm: the person previewing plays the user.
 *
 * The dev server puts it in before the page's own scripts, with the manifest on the script tag. In the preview's
 * shell it reads the panel's settings from the parent window; opened on its own it uses the defaults.
 */
import { bridgeEvent, MINIAPP_BRIDGE, miniappEnv, type MiniappEnv, type MiniappMethod } from "../src/contract/miniapps.js";
import type { AudioState, Result } from "../src/index.js";
import { MINIAPP_CAPABILITY } from "../src/contract/miniapp-capabilities.js";
import { METHODS } from "../src/methods.js";
import miniapps from "./fixtures/miniapps.json";
import recordings from "./fixtures/recordings.json";
import { DEFAULTS, INSETS } from "./settings.js";
import type { CallRecord, FakeContext, FakeError, FakeHandlers, FakeModule, PreviewHost, PreviewSettings } from "./types.js";

type Colors = MiniappEnv["theme"]["colors"];
const fixtureEnv = (miniapps.calls.valid.find((c) => c.request.method === "env.get")!.response as { result: MiniappEnv }).result;
const LIGHT: Colors = fixtureEnv.theme.colors;
/** The apps' dark look, near enough for a preview. */
const DARK: Colors = {
  ...LIGHT,
  paper: "#171c19",
  card: "#1f2521",
  ink: "#faf7ef",
  ink2: "#a5aa9e",
  ink3: "#737a70",
  line: "#2c332e",
  coral: "#df795d",
  coralText: "#ee9a80",
  sage: "#9fb38c",
  sageStrong: "#b9caa8",
  danger: "#e07a72",
  accent: "#9fb38c",
  accentStrong: "#b9caa8",
  onAccent: "#171c19",
  accentText: "#b9caa8",
};
/** Two of the colours a user can pick for the app (accent…): the default, and honey (the contract's example). */
const ACCENTS: Record<PreviewSettings["accent"], Record<"light" | "dark", Partial<Colors>>> = {
  sage: { light: {}, dark: {} },
  honey: {
    light: { accent: "#c99a3c", accentStrong: "#a87d28", accentText: "#8a6520", onAccent: "#ffffff" },
    dark: { accent: "#e0b45e", accentStrong: "#ecc77f", accentText: "#ecc77f", onAccent: "#171c19" },
  },
};
export function envOf(s: PreviewSettings): MiniappEnv {
  const colors = { ...(s.scheme === "dark" ? DARK : LIGHT), ...ACCENTS[s.accent][s.scheme] };
  return miniappEnv.parse({
    locale: s.locale,
    theme: { scheme: s.scheme, colors },
    fontScale: s.fontScale,
    platform: s.platform,
    capabilityLevel: s.level,
    insets: INSETS[s.platform],
  });
}

/** What reaches the server: these fail with 断网 and 未登录. */
const SERVER = (method: string) => /^(recordings|records|settings|user)\./.test(method) || method === "audio.play";

class Failure {
  constructor(readonly error: FakeError) {}
}
const fail = (code: FakeError["code"], extra: Omit<FakeError, "code"> = {}) => new Failure({ code, ...extra });

// ------------------------------------------------------------------ the fixtures' answers

type Recording = Result<"recordings.list">["items"][number];
type Fixture = { schema: string; name: string; value: unknown };
const named = <T>(schema: string, name?: string) => (recordings.valid as Fixture[]).find((f) => f.schema === schema && (!name || f.name === name))!.value as T;
type Call = { name: string; request: { method: string }; response: { ok: boolean; result?: unknown } };
/** The contract's level 2 example answer for a method (the first that went through). */
const example = <T>(method: string, name?: string) =>
  JSON.parse(JSON.stringify((miniapps.level2.calls.valid as Call[]).find((c) => c.request.method === method && c.response.ok && (!name || c.name.includes(name)))!.response.result)) as T;

function defaults(ctx: FakeContext): FakeHandlers {
  const first = named<Result<"recordings.list">>("recordingList", "a page: done in two files, still uploading, failed for points");
  let items: Recording[] = first.items.map((r) => ({ ...r }));
  let minutes = first.transcribeMinutes;
  const long = named<Result<"recordings.get">>("recordingText");
  let audio: AudioState | null = null;
  let timer: ReturnType<typeof setInterval> | undefined;

  const tick = () => {
    if (!audio || audio.state !== "playing") return;
    if (ctx.offline()) audio = { ...audio, state: "failed", error: "OFFLINE" };
    else {
      const positionMs = Math.min(audio.durationMs, audio.positionMs + 1000);
      audio = { ...audio, positionMs, state: positionMs >= audio.durationMs ? "ended" : "playing" };
    }
    ctx.emit("audio.state", audio);
  };
  const find = (id: string) => {
    const r = items.find((i) => i.id === id);
    if (!r) throw fail("SERVER", { status: 404, serverCode: "RECORDING_NOT_FOUND" });
    return r;
  };

  let records = example<Result<"records.list">>("records.list").items;
  let settings = example<Result<"settings.get">>("settings.get").values;

  return {
    "env.get": () => ctx.env(),
    "app.close": () => ({}),
    "recordings.list": (p) => (p.cursor ? { items: [], next: null, transcribeMinutes: minutes } : { items, next: null, transcribeMinutes: minutes }),
    "recordings.get": (p) => {
      if (p.id === long.id) return long;
      const r = find(p.id);
      const text = r.state === "done" && !r.noSpeech ? (r.preview ?? "") : null;
      return { id: r.id, title: r.title, text: text ?? "", parts: [{ segment: 1, durationMs: r.durationMs, text }] };
    },
    "recordings.transcribe": (p) => {
      const r = find(p.id);
      if (r.state !== "failed") throw fail("SERVER", { status: 409, serverCode: "NOT_FAILED" });
      if (!r.audio) throw fail("SERVER", { status: 409, serverCode: "AUDIO_GONE" });
      if (minutes === 0) throw fail("SERVER", { status: 402, serverCode: "CREDITS_EXHAUSTED" });
      const next: Recording = { ...r, state: "transcribing", failure: null, retry: false };
      items = items.map((i) => (i.id === r.id ? next : i));
      minutes = Math.max(0, minutes - Math.ceil(r.durationMs / 60_000));
      return { recording: next, transcribeMinutes: minutes };
    },
    "recordings.delete": (p) => {
      find(p.id);
      items = items.filter((i) => i.id !== p.id);
      return { deleted: true };
    },
    "clipboard.write": (p) => {
      ctx.toast(`已复制 ${p.text.length} 个字`);
      return {};
    },
    "audio.play": (p) => {
      const r = find(p.id);
      if (!r.audio) throw fail("SERVER", { status: 409, serverCode: "AUDIO_GONE" });
      audio = { id: r.id, state: "playing", positionMs: Math.min(p.startMs ?? 0, r.durationMs), durationMs: r.durationMs };
      clearInterval(timer);
      timer = setInterval(tick, 1000);
      return audio;
    },
    "audio.control": (p) => {
      if (!audio) throw fail("BAD_PARAMS", { message: "nothing is playing" });
      if (p.action === "seek") audio = { ...audio, positionMs: Math.min(p.positionMs, audio.durationMs) };
      else audio = { ...audio, state: p.action === "pause" ? "paused" : p.action === "resume" ? "playing" : "stopped" };
      return audio;
    },
    // Level 2. Records and settings live in the page's memory here: the kinds and settings app.json declares are the
    // server's to check, so the preview takes any.
    "user.id": () => example("user.id"),
    "records.list": (p) => {
      const mine = records.filter((r) => !p.kind || r.kind === p.kind);
      return { items: p.cursor ? [] : mine, next: null };
    },
    "records.add": (p) => {
      const added = { id: `r-preview-${records.length + 1}`, at: new Date().toISOString() };
      records = [{ ...added, kind: p.kind, data: p.data }, ...records];
      return added;
    },
    "settings.get": () => ({ values: settings }),
    "settings.set": (p) => {
      settings = { ...settings, ...p.values };
      return { values: settings };
    },
    // The app's own screens: the browser's dialogs stand in for the user picking.
    "scan.code": () => {
      const text = window.prompt("预览：假装扫到了哪个码？（取消 = 用户关掉了扫码）", example<{ text: string }>("scan.code", "scanned").text);
      return text ? { text } : { cancelled: true };
    },
    "share.text": (p) => {
      ctx.toast(`分享：${p.text.slice(0, 40)}`);
      return { shared: true };
    },
    "photos.pick": () => (window.confirm("预览：选一张照片？（取消 = 没选）") ? example("photos.pick", "one photo") : { cancelled: true }),
    "photos.save": () => {
      ctx.toast("存进相册了");
      return { saved: true };
    },
  };
}

// ------------------------------------------------------------------ the bridge

type Manifest = { slug: string; version: string; capabilities: string[]; capabilityLevel: number };

function host(): PreviewHost | null {
  try {
    const parent = window.parent as unknown as { __tikpalPreview?: PreviewHost };
    return parent !== (window as unknown) ? (parent.__tikpalPreview ?? null) : null;
  } catch {
    return null;
  }
}

export function install(custom: FakeModule = {}) {
  const script = document.currentScript as HTMLScriptElement | null;
  const manifest: Manifest = JSON.parse(script?.dataset.manifest ?? '{"slug":"preview","version":"0.0.0","capabilities":[],"capabilityLevel":1}');
  const settings = () => host()?.settings ?? DEFAULTS;
  const listeners = new Map<string, Set<(data: unknown) => void>>();
  let count = 0;

  const ctx: FakeContext = {
    env: () => envOf(settings()),
    emit(event, data) {
      const checked = bridgeEvent.safeParse({ event, data });
      if (!checked.success) console.warn(`[preview] ${event} is not what the contract says:`, checked.error.issues);
      listeners.get(event)?.forEach((l) => l(data));
    },
    get switches() {
      const chosen = settings().switches;
      return Object.fromEntries(Object.entries(custom.switches ?? {}).map(([name, values]) => [name, chosen[name] ?? values[0]]));
    },
    offline: () => settings().offline,
    toast: (text) => (host() ? host()!.toast(text) : console.info(`[preview] ${text}`)),
    fixtures: { miniapps, recordings },
  };
  const own = custom.handlers ?? custom.default ?? {};
  const fallback = defaults(ctx);
  const handlers: FakeHandlers = { ...fallback, ...own };

  async function answer(method: string, params: unknown): Promise<{ result: unknown; note?: string }> {
    const spec = (MINIAPP_BRIDGE as Record<string, (typeof MINIAPP_BRIDGE)[MiniappMethod]>)[method];
    if (!spec) throw fail("UNSUPPORTED", { message: `the bridge has no ${method}` });
    const checked = spec.params.safeParse(params);
    if (!checked.success) throw fail("BAD_PARAMS", { message: checked.error.issues.map((i) => `${i.path.join(".") || "params"}: ${i.message}`).join("; ").slice(0, 300) });
    const capability = METHODS[method as MiniappMethod].capability;
    if (capability && !manifest.capabilities.includes(capability)) {
      console.error(`[tikpal] ${method}：manifest.json 的 capabilities 没声明 "${capability}"，App 会回 NOT_DECLARED`);
      throw fail("NOT_DECLARED", { message: `manifest.json 没声明 "${capability}"` });
    }
    const only = capability ? (MINIAPP_CAPABILITY[capability] as { only?: readonly string[] }).only : undefined;
    if (only && !only.includes(manifest.slug)) throw fail("NOT_DECLARED", { message: `"${capability}" 只给 ${only.join("、")}` });
    const s = settings();
    if (spec.level > s.level) throw fail("UNSUPPORTED", { message: `${method} 要能力套装 ${spec.level}，预览的 App 是 ${s.level}` });
    if (capability && s.denied.includes(capability)) throw fail("NOT_DECLARED", { message: `预览：拒绝了「${capability}」` });
    if (SERVER(method) && s.signedOut) throw fail("SIGNED_OUT");
    await new Promise((r) => setTimeout(r, s.slow ? 1500 : 200));
    if (SERVER(method) && settings().offline) throw fail("OFFLINE", { message: "没有网络" });
    if (method === "app.close") host()?.closed();
    const handler = handlers[method as MiniappMethod] as ((p: unknown, c: FakeContext) => unknown) | undefined;
    const result = await handler!(checked.data, ctx);
    const valid = spec.result.safeParse(result);
    if (valid.success) return { result };
    const note = `preview/fake.ts 的回答不合合同：${valid.error.issues.map((i) => `${i.path.join(".")}: ${i.message}`).join("; ")}`;
    console.warn(`[preview] ${method}: ${note}`);
    return { result, note };
  }

  window.tikpal = {
    async call(method, params) {
      const id = ++count;
      const started = performance.now();
      const record = (r: Omit<CallRecord, "id" | "method" | "params" | "ms">) =>
        host()?.record({ id, method, params, ms: Math.round(performance.now() - started), ...r });
      try {
        const { result, note } = await answer(String(method), params);
        record({ ok: true, result, note });
        return result;
      } catch (thrown) {
        const error: FakeError =
          thrown instanceof Failure ? thrown.error : thrown && typeof thrown === "object" && "code" in thrown ? (thrown as FakeError) : { code: "FAILED", message: String(thrown) };
        record({ ok: false, error });
        throw error;
      }
    },
    on(event, listener) {
      if (!listeners.has(event)) listeners.set(event, new Set());
      listeners.get(event)!.add(listener);
      return () => listeners.get(event)?.delete(listener);
    },
  };
  (window as unknown as { __tikpalFake: unknown }).__tikpalFake = {
    switches: custom.switches ?? {},
    envChanged: () => ctx.emit("env.changed", ctx.env()),
  };
}

