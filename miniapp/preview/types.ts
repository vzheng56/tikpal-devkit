/**
 * A project's own pretend answers for the preview (preview/fake.ts in the project, or `dev --fake <file>`):
 *
 *   import type { FakeHandlers, Switches } from "@tikpal/miniapp/preview";
 *   export const switches: Switches = { credits: ["57", "0"] };      // shown in the preview's panel, first is the default
 *   export const handlers: FakeHandlers = {
 *     "recordings.list": (params, ctx) => ({ items: [], next: null, transcribeMinutes: Number(ctx.switches.credits) }),
 *   };
 *
 * The preview checks every call before a handler sees it, as the app would (method, params, declared capability,
 * level, the panel's 断网 / 未登录 / 拒绝), and checks what the handler answers against the contract.
 */
import type { BridgeErrorCode, Events, MiniappEnv, MiniappMethod, Params, Result } from "../src/index.js";

export type FakeError = { code: BridgeErrorCode; status?: number; serverCode?: string; message?: string };

export interface FakeContext {
  /** Where the page runs now, as the panel sets it. */
  env(): MiniappEnv;
  /** Sends an event to the page (checked against the contract). */
  emit<E extends keyof Events>(event: E, data: Events[E]): void;
  /** The project's switches, as the panel has them now. */
  switches: Record<string, string>;
  /** Whether the panel says 断网 right now (for something that fails halfway, like playing). */
  offline(): boolean;
  /** Shows a short note over the phone, as the app would (复制了). */
  toast(text: string): void;
  /** The contract's fixtures: miniapps.json and recordings.json. */
  fixtures: { miniapps: unknown; recordings: unknown };
}

export type FakeHandler<M extends MiniappMethod> = (params: Params<M>, ctx: FakeContext) => Result<M> | Promise<Result<M>>;
export type FakeHandlers = { [M in MiniappMethod]?: FakeHandler<M> };
/** Extra choices for the panel: name → its values, the first the default. Changing one opens the page again. */
export type Switches = Record<string, readonly [string, ...string[]]>;
export type FakeModule = { handlers?: FakeHandlers; switches?: Switches; default?: FakeHandlers };

/** What the panel sets (the shell keeps it, the pretend bridge reads it). */
export type PreviewSettings = {
  /** The capability level the pretend app offers (env.capabilityLevel): a method above it answers UNSUPPORTED. */
  level: number;
  locale: MiniappEnv["locale"];
  scheme: "light" | "dark";
  platform: MiniappEnv["platform"];
  fontScale: number;
  accent: "sage" | "honey";
  offline: boolean;
  signedOut: boolean;
  slow: boolean;
  denied: string[];
  switches: Record<string, string>;
};

/** One call as the panel lists it. */
export type CallRecord = { id: number; method: string; params: unknown; ms: number; ok: boolean; result?: unknown; error?: FakeError; note?: string };

/** What the shell puts on its window for the page's frame. */
export interface PreviewHost {
  settings: PreviewSettings;
  record(call: CallRecord): void;
  toast(text: string): void;
  closed(): void;
}
