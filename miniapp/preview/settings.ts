/** The panel's starting settings, and the room the capsule's row and the system's bar take (about as on a phone). */
import { KIT_LEVEL } from "../src/methods.js";
import type { PreviewSettings } from "./types.js";

export const DEFAULTS: PreviewSettings = {
  level: KIT_LEVEL,
  locale: "zh-Hans",
  scheme: "light",
  platform: "ios",
  fontScale: 1,
  accent: "sage",
  offline: false,
  signedOut: false,
  slow: false,
  denied: [],
  switches: {},
};

export const INSETS = { ios: { top: 100, bottom: 34 }, android: { top: 80, bottom: 24 } } as const;
