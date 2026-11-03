/**
 * The page. It starts once the app has said where it runs (watchEnv: language, look, the room under the capsule) and
 * draws again when that changes. Everything outside the page goes through the bridge (call); a call can fail, so
 * every one has its message.
 */
import { BridgeError, call, watchEnv, type MiniappEnv } from "@tikpal/miniapp";
import { chart, ring } from "@tikpal/miniapp/components";
import { words } from "./i18n.js";
import "./app.css";

const root = document.getElementById("root")!;
// Sample numbers. When the capability kit gives this app its own records, read them through the bridge instead.
const week = [12, 30, 8, 0, 21, 40, 16];
const parts = [38, 52, 37];
let env: MiniappEnv;

function el<K extends keyof HTMLElementTagNameMap>(tag: K, text?: string, className?: string) {
  const node = document.createElement(tag);
  if (text !== undefined) node.textContent = text;
  if (className) node.className = className;
  return node;
}

function render() {
  const w = words(env.locale);
  const total = week.reduce((a, b) => a + b, 0);
  const status = el("p", "", "status");
  const say = (text: string, tone: "done" | "error") => {
    status.textContent = text;
    status.dataset.tone = tone;
  };
  const failed = (error: unknown) => {
    const code = error instanceof BridgeError ? error.code : "FAILED";
    say(code === "NOT_DECLARED" ? w.denied : code === "UNSUPPORTED" ? w.update : w.failed, "error");
  };
  const copy = el("button", w.copy, "primary");
  copy.addEventListener("click", () => {
    call("clipboard.write", { text: w.summary(total) }).then(() => say(w.copied, "done"), failed);
  });
  // share.text is a capability level 2 method: manifest.json says "share" and capabilityLevel 2.
  const share = el("button", w.share, "secondary");
  share.addEventListener("click", () => {
    call("share.text", { text: w.summary(total) }).then((r) => r.shared && say(w.shared, "done"), failed);
  });
  const actions = el("div", undefined, "actions");
  actions.append(copy, share);
  root.replaceChildren(
    el("h1", w.title),
    el("p", w.total(total), "lead"),
    chart({ kind: "bar", points: week.map((value, i) => ({ label: w.days[i], value })) }),
    ring({ title: w.when, items: parts.map((value, i) => ({ label: w.parts[i], value })) }),
    actions,
    status,
  );
}

// No top-level await: Android's system WebView can be as old as Chrome 87, which has none.
void watchEnv((next) => {
  env = next;
  render();
}).then((first) => {
  env = first;
  render();
});
