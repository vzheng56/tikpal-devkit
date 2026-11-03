/**
 * A 小程序 page's one way out: `window.tikpal`, which the container puts in before the page's own scripts (the
 * preview puts in a pretend one). Typed from the bridge contract (contract/miniapps.ts MINIAPP_BRIDGE); the app checks
 * every call again on its side.
 *
 *   const env = await watchEnv(render);              // language, look, room to keep clear; again when they change
 *   const page = await call("recordings.list");
 *   await call("clipboard.write", { text });
 *   on("audio.state", (s) => …);
 *
 * Only types come from the contract here, never its schemas: a page carries none of zod. A page built by
 * `tikpal-miniapp build` also knows its manifest's methods (__TIKPAL_ALLOWED__), so calling one it did not declare
 * fails at once with NOT_DECLARED, as the container would answer, and says so in the console.
 */
import type { z } from "zod";
import type { MINIAPP_BRIDGE, MiniappEnv, MiniappMethod, miniappErrors } from "./contract/miniapps.js";

export type { MiniappCapability, MiniappEnv, MiniappManifest, MiniappMethod } from "./contract/miniapps.js";
export type { Recording, RecordingAnswer, RecordingList, RecordingText } from "./contract/recordings.js";

type Bridge = typeof MINIAPP_BRIDGE;
/** What a method takes. */
export type Params<M extends MiniappMethod> = z.input<Bridge[M]["params"]>;
/** What it answers. */
export type Result<M extends MiniappMethod> = z.output<Bridge[M]["result"]>;
/** What the app is playing (audio.play, audio.control, the audio.state event). */
export type AudioState = Result<"audio.play">;
/** What the app says unasked. */
export type Events = { "audio.state": AudioState; "env.changed": MiniappEnv };
export type BridgeErrorCode = (typeof miniappErrors)[number];

/** `window.tikpal` as the container gives it. */
export interface TikpalBridge {
  call(method: string, params: unknown): Promise<unknown>;
  on(event: string, listener: (data: unknown) => void): () => void;
}

declare global {
  interface Window {
    tikpal?: TikpalBridge;
  }
}

/** The methods the manifest allows, written in by the build; undefined when something else built the page. */
declare const __TIKPAL_ALLOWED__: readonly string[] | undefined;

/**
 * Why a call failed (contract bridgeError): NOT_DECLARED, UNSUPPORTED, BAD_PARAMS, SIGNED_OUT, OFFLINE, TIMEOUT,
 * SERVER (with the server's `status` and its own `serverCode`, such as CREDITS_EXHAUSTED) or FAILED.
 */
export class BridgeError extends Error {
  constructor(
    readonly code: BridgeErrorCode,
    readonly status?: number,
    readonly serverCode?: string,
    message?: string,
  ) {
    super(message ?? serverCode ?? code);
    this.name = "BridgeError";
  }
}

function bridge(): TikpalBridge {
  if (typeof window === "undefined" || !window.tikpal) throw new BridgeError("UNSUPPORTED", undefined, undefined, "no window.tikpal: not in the Tikpal app or the preview");
  return window.tikpal;
}

/** The params a method takes, left out where it takes none. */
type Args<M extends MiniappMethod> = {} extends Params<M> ? [params?: Params<M>] : [params: Params<M>];

/** A bridge call. A failed one throws BridgeError, whatever shape the app's error came in. */
export async function call<M extends MiniappMethod>(method: M, ...[params]: Args<M>): Promise<Result<M>> {
  if (typeof __TIKPAL_ALLOWED__ !== "undefined" && !__TIKPAL_ALLOWED__.includes(method)) {
    console.error(`[tikpal] ${method}: its capability is not in manifest.json "capabilities", so the app answers NOT_DECLARED`);
    throw new BridgeError("NOT_DECLARED");
  }
  try {
    return (await bridge().call(method, params ?? {})) as Result<M>;
  } catch (error) {
    if (error instanceof BridgeError) throw error;
    const e = (error ?? {}) as { code?: BridgeErrorCode; status?: number; serverCode?: string; message?: string };
    throw new BridgeError(e.code ?? "FAILED", e.status, e.serverCode, e.message);
  }
}

/** What the app says unasked; returns the way to stop listening. */
export function on<E extends keyof Events>(event: E, listener: (data: Events[E]) => void): () => void {
  return bridge().on(event, (data) => listener(data as Events[E]));
}

/**
 * The app around the page, as CSS on the root: its colours as `--paper`, `--ink`, `--accent`… (the contract's names;
 * accent… follow the colour the user picked, coral only for what waits and for errors), `--inset-top` and
 * `--inset-bottom` (the capsule's row and the system's bar: draw from `--inset-top` down), `--font-scale`, and
 * `color-scheme`, `lang` and `data-scheme`. iOS's WebView does not follow the system's text size, so on iOS
 * `--font-scale` is the user's; Android's WebView already scales text, so there it is 1.
 */
export function applyEnv(env: MiniappEnv, root: HTMLElement = document.documentElement): void {
  for (const [name, value] of Object.entries(env.theme.colors)) root.style.setProperty(`--${name}`, value);
  root.style.setProperty("--inset-top", `${env.insets.top}px`);
  root.style.setProperty("--inset-bottom", `${env.insets.bottom}px`);
  root.style.setProperty("--font-scale", String(env.platform === "ios" ? env.fontScale : 1));
  root.style.colorScheme = env.theme.scheme;
  root.lang = env.locale;
  root.setAttribute("data-scheme", env.theme.scheme);
}

/**
 * Where the page runs, applied (applyEnv); and every change after, applied and then handed to `changed` (the user
 * switched the app's language, look or colour while the page was open). Returns the first.
 */
export async function watchEnv(changed?: (env: MiniappEnv) => void): Promise<MiniappEnv> {
  const env = await call("env.get");
  applyEnv(env);
  on("env.changed", (next) => {
    applyEnv(next);
    changed?.(next);
  });
  return env;
}
