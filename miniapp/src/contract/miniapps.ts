// 生成的，别手改：packages/contracts/src/miniapps.ts 里开发包用得到的那些（scripts/sync.ts KEPT），别的不进公开包。改那份，再跑 pnpm miniapp:sync。
/**
 * 小程序: pages written as web pages (HTML, JS, CSS) that run in the apps' container and reach Tikpal only through its
 * bridge (docs/delivery/2026-11-01-developer-platform.md §4, the plan's 1.1; the container's own rules are BOSS-3's,
 * docs/delivery/2026-11-01-miniapp-container.md §3). The first, 录音卡's page, is built into the apps; later ones are
 * signed and downloaded (the plan's 2.4), in this same package format, so a page never has to change for it.
 *
 * A package is a folder: manifest.json (miniappManifest) at its root, the entry page and its files beside it, paths as
 * MINIAPP.path allows. The page never gets the account's sign-in: everything it does goes through the bridge, and only
 * what its manifest declares (`capabilities`) answers; anything else fails with NOT_DECLARED before it reaches the
 * server. Every bridge call the account makes is made by the app as the signed-in user, for that 小程序 only.
 *
 * The bridge (MINIAPP_BRIDGE): the page calls `window.tikpal.call(method, params)` and gets a promise. On the wire each
 * call is a bridgeRequest, answered by one bridgeResponse with the same id; the app sends bridgeEvent for what happens
 * on its side (audio playing, the language or look changing). A method belongs to a capability, and to the capability
 * level it came in with: the container offers every method up to its own level (MINIAPP.capabilityLevel), and a
 * package names the lowest it needs (`capabilityLevel`). Methods are only ever added, never changed or taken away.
 */
import { z } from "zod";
import { recordingAnswer, recordingDeleted, recordingId, recordingList, recordingText } from "./recordings.js";
import { MINIAPP_PATH, type MiniappCapabilityName, capabilityLevelOf } from "./miniapp-capabilities.js";
import { LINK } from "./device-link.js";
import { gcjPoint } from "./navigation.js";
import { MAP_ERRORS, mapGeocodeRequest, mapGeocodeResult, mapRegeoResult, mapRoute, mapRoutesRequest, mapRoutesResult, mapSavedResult, mapSearchRequest, mapSearchResult, mapStaticRequest, mapStaticResult, mapTipsRequest, mapTipsResult } from "./map.js";
import { tripEnded, tripProgress, tripStart, tripStarted, tripStatus } from "./trip.js";

export const MINIAPP = {
  /**
   * The level the apps' container offers: every method up to it. 2 since 「小程序容器 · 两端」 offers the first version's
   * methods (the plan's 2.5); 3 since App 1.1.0 (31), build 31 on, both apps (and 1.2.0), offers 能力套装 3's (用户
   * 11-17: still named 1.1.0, 1.2 kept for 0.2; no background trip in it, and the map's and 设备通道's switches stay off
   * on the platform). The kit says level 3 from 开发包 1.11, out with Yay 0.1.22's full release (项目总策划 11-19). 1.1.0
   * (30) and before are level 2 (or 1), however they are named. An
   * app says its own level with every download (GET /v1/miniapps?capabilityLevel=<N>) and
   * gets the highest release its container runs (miniapps.ts choose), so an older app keeps the release before a level-3
   * one, and an app none of whose releases it runs is not in its list. This is the level the kit checks a package against.
   */
  capabilityLevel: 3,
  /** A package's files at most, and a path's length. */
  maxFiles: 200,
  maxPathLength: 120,
  /** Lowercase letters, digits, `.`, `_`, `-` and `/`; never a `.` or `..` segment, an empty one, or a leading or trailing `/`. */
  path: MINIAPP_PATH,
  /** How long the app waits for its own side of a call (the server's answer included) before TIMEOUT. */
  callSeconds: 30,
  /** photos.pick: the longer side of a photo the app hands over in pixels, its base64 in characters, and how many at once, at most. */
  photoSide: 1600,
  photoChars: 1_500_000,
  photos: 4,
  /** storage.*: keys of a 小程序 on this phone, a key's length, a value's length, all its values together (characters). */
  storage: { keys: 100, keyChars: 64, valueChars: 4096, totalChars: 65_536 },
} as const;

export const miniappSlug = z.string().regex(/^[a-z][a-z0-9-]{1,40}$/);
const semver = z.string().regex(/^(0|[1-9]\d{0,3})\.(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,5})$/);
const path = z.string().max(MINIAPP.maxPathLength).regex(MINIAPP.path);
const localized = z
  .object({ en: z.string().min(1).max(40), "zh-Hans": z.string().min(1).max(40), ja: z.string().min(1).max(40), "es-419": z.string().min(1).max(40) })
  .strict();

/**
 * What a page may ask for (miniapp-capabilities.ts: their kind, level and grant). Kind 2 and 3 are groups of bridge
 * methods (MINIAPP_BRIDGE); kind 1 are the WebView's own, which the container allows only to a page that names them.
 * `env` and `app` every page has.
 */
export const miniappCapabilities = [
  "recordings",
  "clipboard",
  "audio",
  "camera",
  "microphone",
  "motion",
  "location",
  "scan",
  "share",
  "photos",
  "records",
  "settings",
  "user",
  "device.link",
  "trip",
  "map",
  "places.saved",
  "storage",
] as const satisfies readonly MiniappCapabilityName[];
export type MiniappCapability = (typeof miniappCapabilities)[number];

/** manifest.json at the package's root. */
export const miniappManifest = z
  .object({
    slug: miniappSlug,
    version: semver,
    /** The page that opens, a path in the package. */
    entry: path,
    name: localized,
    /** What it calls through the bridge; anything else fails with NOT_DECLARED. */
    capabilities: z.array(z.enum(miniappCapabilities)).max(miniappCapabilities.length),
    /** The lowest capability level (能力套装版本) it needs; a container below it does not open it. */
    capabilityLevel: z.number().int().min(1),
    /** When it talks to its app on the round screen: that app's lowest version ("recording-card@0.2.0"); else null. */
    minFirmware: z.string().regex(/^[a-z][a-z0-9-]{1,40}@(0|[1-9]\d{0,3})\.(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,5})$/).nullable(),
    /**
     * Each file's SHA-256 by path. A built-in package may leave it out (it ships inside the signed app); a downloaded
     * one (2.4) always has it, and the container checks every file against it.
     */
    files: z.record(path, z.string().regex(/^[0-9a-f]{64}$/)).optional(),
  })
  .strict()
  .refine((m) => new Set(m.capabilities).size === m.capabilities.length, "a capability named twice")
  .refine((m) => !m.files || Object.keys(m.files).length <= MINIAPP.maxFiles, "too many files")
  // Object.hasOwn, not `in`: `in` also finds the object prototype's names, so an entry named `constructor` would pass
  // with no such file in the package.
  .refine((m) => !m.files || Object.hasOwn(m.files, m.entry), "the entry is not among the files")
  .refine((m) => m.capabilityLevel >= capabilityLevelOf(m.capabilities), "capabilityLevel is below what a capability needs");
export type MiniappManifest = z.infer<typeof miniappManifest>;

/** What the page knows of where it runs (env.get, and env.changed when any of it changes). */
export const miniappEnv = z
  .object({
    locale: z.enum(["en", "zh-Hans", "ja", "es-419"]),
    /**
     * The app's look now, and its colours as CSS colours, for the page to match the app around it. `accent…` follow the
     * colour the user picked for the app (#863, #869: buttons and what is selected); coral is only for what waits for the
     * user and for errors, as in the apps.
     */
    theme: z
      .object({
        scheme: z.enum(["light", "dark"]),
        colors: z
          .object({
            paper: z.string(),
            card: z.string(),
            ink: z.string(),
            ink2: z.string(),
            ink3: z.string(),
            line: z.string(),
            coral: z.string(),
            coralText: z.string(),
            sage: z.string(),
            sageStrong: z.string(),
            danger: z.string(),
            accent: z.string(),
            accentStrong: z.string(),
            onAccent: z.string(),
            accentText: z.string(),
          })
          .strict(),
      })
      .strict(),
    /**
     * The text size the user picked, 1 the standard. Android's WebView already scales text by it; iOS's does not follow
     * Dynamic Type, so the page multiplies its own sizes by it there.
     */
    fontScale: z.number().min(0.5).max(4),
    platform: z.enum(["ios", "android"]),
    capabilityLevel: z.number().int().min(1),
    /**
     * CSS pixels the page keeps clear of: `top` is the lower edge of the row the capsule sits in (the page draws from
     * there down, its title under the capsule, never beside it); `bottom` the system's own bar.
     */
    insets: z.object({ top: z.number().nonnegative(), bottom: z.number().nonnegative() }).strict(),
    /**
     * Level 3: how the page was opened. A 声波码 of this app's own action (voice-code.ts, actions 8–15: app.json
     * voiceCode.own says what each means) gives its action and value; anything else (the app page, the drawer, actions
     * 0–3) null. A page opened by a container below level 3 has no such field. While the page is open, another code of
     * the same app comes as the voiceCode.scanned event instead.
     */
    launch: z.object({ from: z.literal("voiceCode"), action: z.number().int().min(8).max(15), value: z.number().int().min(0).max(0xffffff) }).strict().nullable().optional(),
  })
  .strict();
export type MiniappEnv = z.infer<typeof miniappEnv>;

const none = z.object({}).strict();
const byId = z.object({ id: recordingId }).strict();
/**
 * What the app is playing: which recording, how far, how long. loading until it plays; failed with why (the audio gone,
 * no network, anything else), say halfway through when the network goes.
 */
const audioState = z
  .object({
    id: recordingId,
    state: z.enum(["loading", "playing", "paused", "ended", "stopped", "failed"]),
    positionMs: z.number().int().nonnegative(),
    durationMs: z.number().int().nonnegative(),
    error: z.enum(["AUDIO_GONE", "OFFLINE", "FAILED"]).optional(),
  })
  .strict()
  .refine((a) => (a.state === "failed") === (a.error !== undefined), "an error with failed, and only then");

// Level 2 (the first version). The records, settings and user methods are the app asking the server's
// /v1/miniapps/:slug/… (MINIAPP_CAPABILITY's paths) as the user, the answer handed back as it came; the others are the
// app's own screens, the page getting only what the user picked.
const recordKindName = z.string().regex(/^[a-z][a-z0-9.]{2,40}$/);
const settingValue = z.union([z.string().max(200), z.number(), z.boolean()]);
/** One record the 小程序 kept: its kind (app.json's records), when, and its fields as that kind's schema has them. */
export const miniappRecord = z
  .object({ id: z.string().min(1).max(64), kind: recordKindName, at: z.string().datetime(), data: z.record(z.string(), z.unknown()) })
  .strict();
/** A page of them, newest first: GET /v1/miniapps/:slug/records?kind=&cursor= (at most 50 a page). */
export const miniappRecordList = z.object({ items: z.array(miniappRecord).max(50), next: z.string().max(400).nullable() }).strict();
/** POST /v1/miniapps/:slug/records: one record; the server checks it against its kind's schema (400 SCHEMA_INVALID). */
export const miniappRecordInput = z.object({ kind: recordKindName, data: z.record(z.string(), z.unknown()) }).strict();
export const miniappRecordAdded = z.object({ id: miniappRecord.shape.id, at: z.string().datetime() }).strict();
/** GET and PATCH /v1/miniapps/:slug/settings: the app's settings as the round screen's app reads them (app.json's settings). */
export const miniappSettings = z.object({ values: z.record(z.string(), settingValue) }).strict();
export const miniappSettingsInput = z
  .object({ values: z.record(z.string(), settingValue) })
  .strict()
  .refine((s) => Object.keys(s.values).length >= 1 && Object.keys(s.values).length <= 20, "1 to 20 settings at a time");
/** GET /v1/miniapps/:slug/user-id: the user's id in this 小程序, 16 bytes base64url (the container doc §1.1). */
export const miniappUserId = z.object({ id: z.string().regex(/^[A-Za-z0-9_-]{22}$/) }).strict();
/** What a user may decline: the app's own screens answer it as a result, not an error. */
const cancelled = z.object({ cancelled: z.literal(true) }).strict();
/**
 * A photo the user picked, as the app hands it over: a JPEG whose longer side the app brought down to at most
 * MINIAPP.photoSide, at quality 0.8 and lower until it fits MINIAPP.photoChars, in base64 for an
 * `<img src="data:image/jpeg;base64,…">` (CSP allows data: images). At most MINIAPP.photos at a time: a Redmi 8A's
 * WebView and WKWebView both choke on tens of megabytes in one message (BOSS-3).
 */
const pickedPhoto = z
  .object({ type: z.literal("image/jpeg"), width: z.number().int().positive(), height: z.number().int().positive(), data: z.string().max(1_500_000) })
  .strict();
/**
 * Server errors a 小程序's own calls may answer with (bridgeError serverCode), besides the general ones.
 * MINIAPP_UNAVAILABLE (503): the server cannot answer it now (its keys are not set). RECORDS_OFF (409): records.add
 * before the user agreed to keep this app's records in 动态 (Yay asks in the chat; the page says 「先在对话里同意」),
 * nothing kept. DEVICE_REQUIRED (400): settings kept per round screen (settingsScope device) on an account with more
 * than one; with one, that one.
 */
export const miniappServerErrors = [
  "MINIAPP_NOT_FOUND",
  "MINIAPP_SCOPE",
  "UNKNOWN_KIND",
  "SCHEMA_INVALID",
  "UNKNOWN_SETTING",
  "BAD_SETTING",
  "MINIAPP_UNAVAILABLE",
  "RECORDS_OFF",
  "DEVICE_REQUIRED",
  ...MAP_ERRORS,
] as const;

// Level 3 (能力套装 3). The map methods are the map module's operations (map.ts), the app asking the platform for them
// as the user, on this page's behalf; leaving out the start (`from`) or `near` (`point` for regeo) is 「我这里」:
// the app finds the position once, which needs `location` declared and its card agreed, else NOT_DECLARED.
const mapRoutesParams = z.object({ ...mapRoutesRequest.shape, from: gcjPoint.optional() }).strict();
const mapTipsParams = mapTipsRequest;
const mapSearchParams = mapSearchRequest;
const mapRegeoParams = z.object({ point: gcjPoint.optional() }).strict();
/** An app message on the device link: the app's own bytes, base64 (1 to LINK.frame.appMaxBytes). */
const linkData = z.string().regex(/^[A-Za-z0-9+/]+={0,2}$/).max(Math.ceil(LINK.frame.appMaxBytes / 3) * 4);
/**
 * device.link.open: the device to talk to. `mark` from the 声波码 (env.launch's value, as the app's registration says);
 * without it, the account's device with this app that the phone knows or finds nearby. Not connected: why. no_device:
 * none of the account's devices with this app is near or open; several: more than one and no mark (the page asks the
 * user to scan); no_key: the platform gave no key (offline the first time); key_mismatch: the device holds another key
 * (「让 Tikpal Voice 连一下网再试」); bluetooth_off; denied: the user said no to the card or the system's Bluetooth.
 */
const linkOpened = z.union([
  z.object({ connected: z.literal(true), name: z.string().min(1).max(32), mark: z.number().int().min(0).max(2 ** LINK.mark.bits - 1) }).strict(),
  z.object({ connected: z.literal(false), reason: z.enum(["no_device", "several", "no_key", "key_mismatch", "bluetooth_off", "denied", "busy"]) }).strict(),
]);
/** The link as the page sees it: connecting, connected, lost (out of reach: the app reconnects by itself; or the device
 * took the key no more and fetching again did not help: then it stays lost until the page opens it again), closed. */
const linkState = z.object({ state: z.enum(["connecting", "connected", "lost", "closed"]) }).strict();
const storageKey = z.string().regex(/^[A-Za-z0-9._-]{1,64}$/);

/**
 * Every bridge method: its capability (null: every page has it), the level it came in with, what it takes and what it
 * answers. The recordings methods are 录音卡's (recordings.ts) as the app asks the server for them, as the user; where to
 * play one from stays with the app (audio.play).
 */
export const MINIAPP_BRIDGE = {
  "env.get": { capability: null, level: 1, params: none, result: miniappEnv },
  "app.close": { capability: null, level: 1, params: none, result: none },
  "recordings.list": { capability: "recordings", level: 1, params: z.object({ cursor: z.string().max(400).optional() }).strict(), result: recordingList },
  "recordings.get": { capability: "recordings", level: 1, params: byId, result: recordingText },
  "recordings.transcribe": { capability: "recordings", level: 1, params: byId, result: recordingAnswer },
  "recordings.delete": { capability: "recordings", level: 1, params: byId, result: recordingDeleted },
  "clipboard.write": { capability: "clipboard", level: 1, params: z.object({ text: z.string().min(1).max(200_000) }).strict(), result: none },
  /**
   * Plays a recording from `startMs`, its files one after another. The app asks GET /v1/recordings/:id/audio itself and
   * asks again when the links run out (a long pause, a drag far ahead): the links never reach the page, and the app plays
   * Ogg Opus, which WebViews may not. Whether there is audio the list says (recording.audio).
   */
  "audio.play": { capability: "audio", level: 1, params: z.object({ id: recordingId, startMs: z.number().int().nonnegative().optional() }).strict(), result: audioState },
  "audio.control": {
    capability: "audio",
    level: 1,
    params: z.union([
      z.object({ action: z.enum(["pause", "resume", "stop"]) }).strict(),
      z.object({ action: z.literal("seek"), positionMs: z.number().int().nonnegative() }).strict(),
    ]),
    result: audioState,
  },
  "user.id": { capability: "user", level: 2, params: none, result: miniappUserId },
  "records.list": {
    capability: "records",
    level: 2,
    params: z.object({ kind: recordKindName.optional(), cursor: z.string().max(400).optional() }).strict(),
    result: miniappRecordList,
  },
  "records.add": { capability: "records", level: 2, params: miniappRecordInput, result: miniappRecordAdded },
  "settings.get": { capability: "settings", level: 2, params: none, result: miniappSettings },
  "settings.set": { capability: "settings", level: 2, params: miniappSettingsInput, result: miniappSettings },
  /**
   * The app's scanner: the text of the one code the user points it at. Tikpal's own codes never reach a page: the
   * round screen's pairing code, 自拍屏's hotspot code (its Wi-Fi password), a sound code. For those the app says
   * 「这个码不能给小程序」 and goes on scanning; closed by the user, it answers {cancelled: true}.
   */
  "scan.code": { capability: "scan", level: 2, params: none, result: z.union([z.object({ text: z.string().min(1).max(4096) }).strict(), cancelled]) },
  /** The system's share sheet with this text; `shared` false when the user closed it without sharing. */
  "share.text": { capability: "share", level: 2, params: z.object({ text: z.string().min(1).max(2000) }).strict(), result: z.object({ shared: z.boolean() }).strict() },
  "photos.pick": {
    capability: "photos",
    level: 2,
    params: z.object({ max: z.number().int().min(1).max(4).optional() }).strict(),
    result: z.union([z.object({ photos: z.array(pickedPhoto).min(1).max(4) }).strict(), cancelled]),
  },
  /** Saves an image the page made (PNG or JPEG, base64) to the photo library; the system asks the user once. */
  "photos.save": {
    capability: "photos",
    level: 2,
    params: z.object({ type: z.enum(["image/png", "image/jpeg"]), data: z.string().min(1).max(6_000_000) }).strict(),
    result: z.object({ saved: z.boolean() }).strict(),
  },
  "device.link.open": { capability: "device.link", level: 3, params: z.object({ mark: z.number().int().min(0).max(2 ** LINK.mark.bits - 1).optional() }).strict(), result: linkOpened },
  /** One app message to the device (at most LINK.frame.appPerSecond a second; more is BAD_PARAMS). Not connected: FAILED. */
  "device.link.send": { capability: "device.link", level: 3, params: z.object({ data: linkData }).strict(), result: none },
  "device.link.close": { capability: "device.link", level: 3, params: none, result: none },
  "trip.start": { capability: "trip", level: 3, params: tripStart, result: tripStarted },
  "trip.status": { capability: "trip", level: 3, params: none, result: tripStatus },
  "trip.stop": { capability: "trip", level: 3, params: none, result: none },
  "map.routes": { capability: "map", level: 3, params: mapRoutesParams, result: mapRoutesResult },
  "map.tips": { capability: "map", level: 3, params: mapTipsParams, result: mapTipsResult },
  "map.search": { capability: "map", level: 3, params: mapSearchParams, result: mapSearchResult },
  "map.geocode": { capability: "map", level: 3, params: mapGeocodeRequest, result: mapGeocodeResult },
  "map.regeo": { capability: "map", level: 3, params: mapRegeoParams, result: mapRegeoResult },
  "map.static": { capability: "map", level: 3, params: mapStaticRequest, result: mapStaticResult },
  "places.saved": { capability: "places.saved", level: 3, params: none, result: mapSavedResult },
  /** The page's own value for a key, or null (MINIAPP.storage: past its limits storage.set is BAD_PARAMS). */
  "storage.get": { capability: "storage", level: 3, params: z.object({ key: storageKey }).strict(), result: z.object({ value: z.string().max(MINIAPP.storage.valueChars).nullable() }).strict() },
  "storage.set": { capability: "storage", level: 3, params: z.object({ key: storageKey, value: z.string().max(MINIAPP.storage.valueChars) }).strict(), result: none },
  "storage.remove": { capability: "storage", level: 3, params: z.object({ key: storageKey }).strict(), result: none },
  "storage.keys": { capability: "storage", level: 3, params: none, result: z.object({ keys: z.array(storageKey).max(MINIAPP.storage.keys) }).strict() },
} as const;
export type MiniappMethod = keyof typeof MINIAPP_BRIDGE;
export const miniappMethods = Object.keys(MINIAPP_BRIDGE) as MiniappMethod[];

/**
 * Why a call failed. NOT_DECLARED: its capability is not in the manifest; UNSUPPORTED: the container's level is
 * below the method's; BAD_PARAMS: the params are not what the method takes; SIGNED_OUT: the app has no sign-in;
 * OFFLINE: no network; TIMEOUT: no answer in MINIAPP.callSeconds; SERVER: the server said no (`status`, and its own
 * `code`, such as AUDIO_GONE or CREDITS_EXHAUSTED: recordings.ts recordingErrors); FAILED: anything else on the app's side.
 */
export const miniappErrors = ["NOT_DECLARED", "UNSUPPORTED", "BAD_PARAMS", "SIGNED_OUT", "OFFLINE", "TIMEOUT", "SERVER", "FAILED"] as const;
export const bridgeError = z
  .object({
    code: z.enum(miniappErrors),
    status: z.number().int().min(400).max(599).optional(),
    serverCode: z.string().regex(/^[A-Z][A-Z0-9_]{2,60}$/).optional(),
    message: z.string().max(300).optional(),
  })
  .strict()
  .refine((e) => (e.code === "SERVER") === (e.status !== undefined), "a status with SERVER, and only then");

/** One call from the page; the id is the page's own, unique among its calls in flight. */
export const bridgeRequest = z
  .object({ id: z.string().regex(/^[A-Za-z0-9_-]{1,40}$/), method: z.enum(miniappMethods as [MiniappMethod, ...MiniappMethod[]]), params: z.unknown() })
  .strict();
/** Its answer: the result as the method's `result`, or why not. */
export const bridgeResponse = z.union([
  z.object({ id: bridgeRequest.shape.id, ok: z.literal(true), result: z.unknown() }).strict(),
  z.object({ id: bridgeRequest.shape.id, ok: z.literal(false), error: bridgeError }).strict(),
]);
/**
 * What the app says unasked: the audio's progress (about every second while playing, and when it changes) and env
 * changes; level 3: the link's state and the device's app messages, a trip's progress (once a second while the page is
 * in front), its new route and its end, and a 声波码 of this app's own action scanned while the page is open.
 */
export const bridgeEvent = z.union([
  z.object({ event: z.literal("audio.state"), data: audioState }).strict(),
  z.object({ event: z.literal("env.changed"), data: miniappEnv }).strict(),
  z.object({ event: z.literal("device.link.state"), data: linkState }).strict(),
  z.object({ event: z.literal("device.link.message"), data: z.object({ data: linkData }).strict() }).strict(),
  z.object({ event: z.literal("trip.progress"), data: tripProgress }).strict(),
  z.object({ event: z.literal("trip.rerouted"), data: z.object({ route: mapRoute }).strict() }).strict(),
  z.object({ event: z.literal("trip.ended"), data: tripEnded }).strict(),
  z.object({ event: z.literal("voiceCode.scanned"), data: z.object({ action: z.number().int().min(8).max(15), value: z.number().int().min(0).max(0xffffff) }).strict() }).strict(),
]);

/**
 * A package at most: zipped, unpacked, and one file unpacked (5, 20 and 10 MiB; the doc's §2.1, §3.3); and the app.json
 * a developer's upload comes with (64 KiB; the biggest first-party one is about 8 KB).
 */
export const MINIAPP_LIMITS = { zipBytes: 5 * 1024 * 1024, unpackedBytes: 20 * 1024 * 1024, fileBytes: 10 * 1024 * 1024, specBytes: 64 * 1024 } as const;

/**
 * The page's Content-Security-Policy (the container doc §3.1): sent as a header and written into index.html too. Only
 * the package's own scripts, styles, fonts and sounds, its own images and data: ones; it connects nowhere (the bridge is
 * its only way out) and frames nothing. The containers, 录音卡's page, the developer kit's template and its check all
 * use this string and no copy of their own.
 */
export const MINIAPP_CSP =
  "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; font-src 'self'; media-src 'self'; connect-src 'none'; frame-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'";

/**
 * The page's Permissions-Policy by the kind 1 capabilities the user has allowed it (the container doc §3.1): what is not
 * allowed is off, so a page cannot read motion that Android's WebView would hand over unasked. The containers send it
 * with miniappFeaturePolicy beside it (the older header, which a Redmi 8A's WebView 87 reads instead); the developer
 * kit's preview uses the same.
 */
const POLICY_FEATURES = {
  camera: ["camera"],
  microphone: ["microphone"],
  motion: ["accelerometer", "gyroscope", "magnetometer"],
  location: ["geolocation"],
} as const;
export function miniappPermissionsPolicy(allowed: readonly string[]) {
  return Object.entries(POLICY_FEATURES)
    .flatMap(([capability, features]) => features.map((f) => `${f}=${allowed.includes(capability) ? "(self)" : "()"}`))
    .join(", ");
}
/** The same as Feature-Policy, written its own way: `camera 'none'; microphone 'self'`. */
export function miniappFeaturePolicy(allowed: readonly string[]) {
  return Object.entries(POLICY_FEATURES)
    .flatMap(([capability, features]) => features.map((f) => `${f} ${allowed.includes(capability) ? "'self'" : "'none'"}`))
    .join("; ");
}

const sha256Hex = z.string().regex(/^[0-9a-f]{64}$/);

// ——— The developer's side (C4, C5; the doc's §2.5): uploading a package and seeing its releases. ———

/**
 * POST /v1/developer/apps/:slug/miniapp as multipart/form-data with exactly the two MINIAPP_UPLOAD_PARTS, each once (用户
 * 11-04, BOSS-1): `app`, the app's app.json (at most MINIAPP_LIMITS.specBytes), and `package`, the zip (at most
 * MINIAPP_LIMITS.zipBytes). The package is held to that app.json's 小程序 part, as a submission is held to its own
 * (the doc's §2.4), and what it may ask for is still what C8 approved: app.json adds nothing to that. Any other part, one
 * missing or one twice, or the zip alone as the body, is MINIAPP_BAD_UPLOAD. A refusal's `error.message` says what to
 * do in Chinese, for the kit to show as it is.
 */
export const MINIAPP_UPLOAD_PATH = "/v1/developer/apps/:slug/miniapp";
/** The parts of an upload, by name. */
export const MINIAPP_UPLOAD_PARTS = ["app", "package"] as const;
/** Why an upload is refused (400 unless said), each with what was wrong in `problems`. */
export const miniappUploadErrors = [
  /** Not a package as the doc's §2.1 has it: a bad path, an extra or missing file, a file's hash, a size. */
  "MINIAPP_BAD_PACKAGE",
  /** manifest.json does not match the app.json's 小程序 part (`where`: the field). */
  "MINIAPP_SPEC_MISMATCH",
  /** It asks for a capability the app has not been granted (C8). 403. */
  "MINIAPP_NOT_GRANTED",
  /** That version is out already on preview or stable with other bytes. 409. */
  "MINIAPP_VERSION_TAKEN",
  /** Over MINIAPP_LIMITS.zipBytes (`where`: package) or specBytes (`where`: app). 413. */
  "MINIAPP_TOO_LARGE",
  /** Not the two parts, each once (`missing_part`, `extra_part`; `where`: the part's name). */
  "MINIAPP_BAD_UPLOAD",
  /**
   * The app.json is not one as the contract has it (app-spec.ts), or not this app's, or has no 小程序 part (`app_json`;
   * `where`: its field, `app.json` when it cannot be read at all).
   */
  "MINIAPP_BAD_SPEC",
] as const;
/** What was wrong, for the page to show next to the upload: where (a path or field), and a code. */
export const miniappUploadProblem = z
  .object({
    where: z.string().max(200),
    code: z.enum([
      "path",
      "extra_file",
      "missing_file",
      "file_hash",
      "too_many_files",
      "too_large",
      "manifest",
      "spec_mismatch",
      "not_granted",
      "version_taken",
      "missing_part",
      "extra_part",
      "app_json",
    ]),
  })
  .strict();
/** An upload taken: stored as a draft, and on dev once the machine checks pass (`dev` then). */
export const miniappUploaded = z
  .object({ slug: miniappSlug, version: semver, size: z.number().int().positive(), sha256: sha256Hex, state: z.enum(["draft", "dev"]) })
  .strict();
