/**
 * The preview's shell: a phone-sized frame with the capsule over the page, and the panel that sets where the page
 * runs (language, look, platform, width, text size, colour) and what goes wrong (断网, 未登录, 慢网, a capability
 * refused, the project's own switches). Look changes reach the page as env.changed, as in the app; the rest apply to
 * the next call, and switches open the page again. Every bridge call is listed. The dev server reloads the page when a
 * file changes and shows what failed to build.
 */
import { MINIAPP_CAPABILITY } from "../src/contract/miniapp-capabilities.js";
import { miniappPermissionsPolicy } from "../src/contract/miniapps.js";
import { KIT_LEVEL } from "../src/methods.js";
import { DEFAULTS, INSETS } from "./settings.js";
import type { CallRecord, PreviewHost, PreviewSettings } from "./types.js";

type State = { manifest: { slug: string; version: string; entry: string; name: Record<string, string>; capabilities: string[] } | null; error: string | null };

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const phone = $<HTMLDivElement>("phone");
const frame = $<HTMLIFrameElement>("page");
const log = $<HTMLOListElement>("log");
const KEY = "tikpal-preview";

const saved = (() => {
  try {
    return JSON.parse(localStorage.getItem(KEY) ?? "{}") as Partial<PreviewSettings & { width: number }>;
  } catch {
    return {};
  }
})();
const settings: PreviewSettings = { ...DEFAULTS, ...saved, denied: saved.denied ?? [], switches: saved.switches ?? {} };
let width = saved.width ?? 393;
let state: State = { manifest: null, error: null };
let toastTimer: ReturnType<typeof setTimeout> | undefined;

function save() {
  try {
    localStorage.setItem(KEY, JSON.stringify({ ...settings, width }));
  } catch {
    // A private window: the panel just starts from the defaults next time.
  }
}

const host: PreviewHost = {
  settings,
  record(call: CallRecord) {
    const li = document.createElement("li");
    const details = document.createElement("details");
    const summary = document.createElement("summary");
    const b = document.createElement("b");
    b.textContent = call.method;
    const status = document.createElement("span");
    status.className = call.ok ? "ok" : "code";
    status.textContent = call.ok ? "ok" : call.error!.code + (call.error!.serverCode ? ` ${call.error!.status} ${call.error!.serverCode}` : "");
    const ms = document.createElement("small");
    ms.textContent = `${call.ms} ms`;
    summary.append(b, status, ms);
    if (call.note) {
      const note = document.createElement("span");
      note.className = "note";
      note.textContent = "回答不合合同";
      summary.append(note);
    }
    const pre = document.createElement("pre");
    pre.textContent = JSON.stringify({ params: call.params, ...(call.ok ? { result: call.result } : { error: call.error }), ...(call.note ? { note: call.note } : {}) }, null, 2);
    details.append(summary, pre);
    li.append(details);
    log.prepend(li);
    while (log.children.length > 200) log.lastChild!.remove();
  },
  toast(text) {
    const toast = $("toast");
    toast.textContent = text;
    toast.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => (toast.hidden = true), 1800);
  },
  closed() {
    $("closed").hidden = false;
  },
};
(window as unknown as { __tikpalPreview: PreviewHost }).__tikpalPreview = host;

function chrome() {
  const insets = INSETS[settings.platform];
  phone.style.width = `${width}px`;
  phone.classList.toggle("dark", settings.scheme === "dark");
  phone.classList.toggle("android", settings.platform === "android");
  const statusHeight = settings.platform === "ios" ? 54 : 28;
  $("status").style.height = `${statusHeight}px`;
  $("capsule").style.top = `${statusHeight + (insets.top - statusHeight - 32) / 2}px`;
}

/** Tells the page where it runs now (env.changed), as the app does when its language or look changes. */
function envChanged() {
  save();
  chrome();
  (frame.contentWindow as unknown as { __tikpalFake?: { envChanged(): void } } | null)?.__tikpalFake?.envChanged();
}

/**
 * What the WebView itself may use (camera, microphone, motion, location): the kind 1 capabilities the manifest names
 * and the panel does not refuse, as the container allows them (the contract's Permissions-Policy, as the frame's allow).
 */
function allowed() {
  const kind1 = (state.manifest?.capabilities ?? []).filter((c) => (MINIAPP_CAPABILITY as Record<string, { kind: number }>)[c]?.kind === 1 && !settings.denied.includes(c));
  return miniappPermissionsPolicy(kind1)
    .split(", ")
    .filter((p) => p.endsWith("=(self)"))
    .map((p) => p.slice(0, p.indexOf("=")))
    .join("; ");
}

function open() {
  $("closed").hidden = true;
  if (!state.manifest) return;
  frame.setAttribute("allow", allowed());
  const url = `/app/${state.manifest.entry}`;
  if (frame.getAttribute("src") === url) frame.contentWindow?.location.reload();
  else frame.setAttribute("src", url);
}

function seg<T extends string | number>(values: [T, string][], get: () => T, set: (v: T) => void) {
  const box = document.createElement("div");
  box.className = "seg";
  const paint = () => box.querySelectorAll("button").forEach((b, i) => b.setAttribute("aria-pressed", String(values[i][0] === get())));
  for (const [value, label] of values) {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = label;
    button.onclick = () => {
      set(value);
      paint();
    };
    box.append(button);
  }
  paint();
  return box;
}

function check(label: string, get: () => boolean, set: (v: boolean) => void, title = "") {
  const box = document.createElement("label");
  box.className = "chk";
  box.title = title;
  const input = document.createElement("input");
  input.type = "checkbox";
  input.checked = get();
  input.onchange = () => set(input.checked);
  box.append(input, label);
  return box;
}

function row(label: string, ...items: HTMLElement[]) {
  const k = document.createElement("label");
  k.className = "k";
  k.textContent = label;
  const r = document.createElement("div");
  r.className = "row";
  r.append(...items);
  $("controls").append(k, r);
}

function controls() {
  $("controls").replaceChildren();
  row(
    "语言",
    seg(
      [["zh-Hans", "中文"], ["en", "English"], ["ja", "日本語"], ["es-419", "Español"]],
      () => settings.locale,
      (v) => ((settings.locale = v), envChanged()),
    ),
  );
  row("外观", seg([["light", "亮"], ["dark", "暗"]], () => settings.scheme, (v) => ((settings.scheme = v), envChanged())));
  row("配色", seg([["sage", "默认"], ["honey", "蜂蜜"]], () => settings.accent, (v) => ((settings.accent = v), envChanged())));
  row("平台", seg([["ios", "iPhone"], ["android", "安卓"]], () => settings.platform, (v) => ((settings.platform = v), envChanged())));
  row(
    "能力套装",
    seg(
      Array.from({ length: KIT_LEVEL }, (_, i) => [i + 1, String(i + 1)] as [number, string]),
      () => settings.level,
      (v) => ((settings.level = v), envChanged()),
    ),
  );
  row("宽度", seg([[360, "360"], [375, "375"], [393, "393"], [430, "430"]], () => width, (v) => ((width = v), save(), chrome())));
  row("字号", seg([[1, "1"], [1.15, "1.15"], [1.3, "1.3"], [1.6, "1.6"]], () => settings.fontScale, (v) => ((settings.fontScale = v), envChanged())));
  row(
    "出问题",
    check("断网", () => settings.offline, (v) => ((settings.offline = v), save()), "要连服务器的方法回 OFFLINE；正在放的音频停在 failed"),
    check("未登录", () => settings.signedOut, (v) => ((settings.signedOut = v), save()), "要连服务器的方法回 SIGNED_OUT"),
    check("慢网", () => settings.slow, (v) => ((settings.slow = v), save()), "每次调用等 1.5 秒"),
  );
  const capabilities = state.manifest?.capabilities ?? [];
  if (capabilities.length)
    row(
      "拒绝能力",
      ...capabilities.map((c) =>
        check(
          c,
          () => settings.denied.includes(c),
          (v) => {
            settings.denied = v ? [...settings.denied, c] : settings.denied.filter((d) => d !== c);
            save();
            // The WebView's own (camera, location…) are allowed when the page opens: open it again.
            if ((MINIAPP_CAPABILITY as Record<string, { kind: number }>)[c]?.kind === 1) open();
          },
          `App 不给「${c}」：这组方法回 NOT_DECLARED；摄像头、定位这类关掉`,
        ),
      ),
    );
  const switches = (frame.contentWindow as unknown as { __tikpalFake?: { switches: Record<string, string[]> } } | null)?.__tikpalFake?.switches ?? {};
  for (const [name, values] of Object.entries(switches)) {
    const select = document.createElement("select");
    for (const v of values) select.append(new Option(v, v, false, (settings.switches[name] ?? values[0]) === v));
    select.onchange = () => {
      settings.switches = { ...settings.switches, [name]: select.value };
      save();
      open();
    };
    row(name, select);
  }
  const again = document.createElement("button");
  again.className = "act";
  again.type = "button";
  again.textContent = "重新打开";
  again.onclick = open;
  row("", again);
}

async function refresh() {
  state = (await fetch("/__tikpal/state").then((r) => r.json())) as State;
  const problems = $("problems");
  problems.hidden = !state.error;
  problems.textContent = state.error ?? "";
  if (state.manifest) {
    $("title").textContent = state.manifest.name[settings.locale] ?? state.manifest.slug;
    $("version").textContent = `${state.manifest.slug}@${state.manifest.version} · ${state.manifest.capabilities.join("、") || "没声明能力"}`;
    document.title = `${$("title").textContent} · 预览`;
  }
  controls();
}

frame.addEventListener("load", controls);
$("close").onclick = () => host.closed();
$("more").onclick = () => host.toast("胶囊菜单：关于、固定（App 里才有）");
$("reopen").onclick = open;
$("clear").onclick = () => log.replaceChildren();

chrome();
await refresh();
open();
const events = new EventSource("/__tikpal/events");
events.addEventListener("reload", async () => {
  await refresh();
  if (!state.error) open();
});
