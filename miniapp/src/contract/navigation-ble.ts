// 生成的，别手改：packages/contracts/src/navigation-ble.ts 里开发包用得到的那些（scripts/sync.ts KEPT），别的不进公开包。改那份，再跑 pnpm miniapp:sync。
/**
 * What a trip (level 3, `trip`) sends to the page's app on the user's Tikpal Voice while the device link is
 * connected, whichever app it is: the tick once a second (encodeNavTick: the next action, how far, the road, what is
 * left, the route ahead relative to the rider), a tick's route apart when the write is too small, the overview (the
 * whole route, simplified) at the start, after a reroute and on reconnecting, the countdown (trip.start's
 * countdownSeconds) at its start and on reconnecting during it, and a last tick of state `ended` when the trip is
 * stopped from the phone; and from the device, the stop frame (encodeNavStop: its app asks the trip to stop). Then
 * 导航's own app messages (the payload of device.link.send / .message: hello, countdown, preview, back, cancel, state),
 * which any app may reuse, and 导航's 声波码 value (the device mark and a destination). Little-endian throughout.
 */
import { z } from "zod";
import { LINK } from "./device-link.js";
import { navigationActions, navigationModes } from "./navigation.js";

export const NAV_BLE = {
  slug: "navigation",
  frame: {
    headerBytes: 20,
    routeMaxPoints: 40,
    /** Each point is one metre per step on each axis (int8): longer steps are split by the phone. */
    routeStepMaxM: 127,
    roadMaxBytes: 48,
    placeMaxBytes: 48,
    fwMaxBytes: 24,
    /** The overview's points (after the start): 6 + 2 × 40 bytes, one write at LINK.frame.minWriteBytes. */
    overviewMaxPoints: 40,
  },
  /**
   * The trip's frame kinds: 0, 3, 6 and 7 to the device, 8 from it; 1 and 5 retired (the draft's hello and state), 2 and
   * 4 the link's.
   */
  kind: { tick: 0, route: 3, overview: 6, countdown: 7, stop: 8 },
  /** How often the phone sends a tick (the device dims the arrow after this many missed). */
  tickMs: 1000,
  staleTicks: 5,
  /** The 声波码: the app's own action, and how many seconds a scanned home/work/last counts down before it starts. */
  voiceCodeAction: 8,
  countdownSeconds: 3,
} as const;
const NAV_KIND = NAV_BLE.kind;

/** What the 声波码's low 4 bits say: 0 home, 1 work, 2 where the last trip went, 3 elsewhere (去哪儿). */
export const navDestinations = ["home", "work", "last", "elsewhere"] as const;
export type NavDestination = (typeof navDestinations)[number];
/** The value of 导航's 声波码: mark << 4 | destination. */
export function navVoiceCodeValue(mark: number, destination: NavDestination): number {
  if (!Number.isInteger(mark) || mark < 0 || mark >= 2 ** LINK.mark.bits) throw new Error("nav: a mark is 20 bits");
  return mark * 16 + navDestinations.indexOf(destination);
}
/** Its reading; null for a destination this version does not know (4–15: the app opens 去哪儿). */
export function readNavVoiceCodeValue(value: number): { mark: number; destination: NavDestination } | null {
  if (!Number.isInteger(value) || value < 0 || value >= 2 ** 24) return null;
  const destination = navDestinations[value % 16];
  return destination ? { mark: Math.floor(value / 16), destination } : null;
}

// ---------------------------------------------------------------- the words of a tick

/** The tick's state; its mode and action are the route's own (navigationModes, navigationActions above). Only ever
 *  added to at the end: the tick stores the index. */
export const navStates = ["idle", "navigating", "rerouting", "arrived", "ended"] as const;

const u16 = z.number().int().min(0).max(65535);

/** One point of the route ahead: x to the right, y ahead, metres, relative to the point before (the first to the rider). */
export const navStep = z.tuple([
  z.number().int().min(-NAV_BLE.frame.routeStepMaxM).max(NAV_BLE.frame.routeStepMaxM),
  z.number().int().min(-NAV_BLE.frame.routeStepMaxM).max(NAV_BLE.frame.routeStepMaxM),
]);

const utf8Bytes = (s: string) => new TextEncoder().encode(s).length;

export const navTick = z.object({
  /** Wraps at 2^16; the device only uses it to say which one it drew last (NavState.seq). */
  seq: u16,
  state: z.enum(navStates),
  mode: z.enum(navigationModes),
  /** The next step's action (navigationActions); `roundabout` takes its exit in `exit`. */
  action: z.enum(navigationActions),
  /** Roundabout exit, 1-based; 0 otherwise. */
  exit: z.number().int().min(0).max(255).default(0),
  /** To the next step, metres (65535 and more as 65535). */
  toActionM: u16,
  /** What is left, in tens of metres (the screen rounds to 10 m) and seconds. */
  remainingDam: u16,
  remainingS: u16,
  /** How far along, 0–1000. */
  progress: z.number().int().min(0).max(1000),
  /** How far off the route, metres (255 and more as 255): the phone decides rerouting, the device only shows it. */
  offRouteM: z.number().int().min(0).max(255).default(0),
  /** The route ahead, at most 40 steps. Empty: keep the last one drawn, or take the NavRoute with this seq when the
   *  phone sent the route apart (a write too small for both). */
  route: z.array(navStep).max(NAV_BLE.frame.routeMaxPoints).default([]),
  /** The road the next step goes onto ("" none). */
  road: z.string().refine((s) => utf8Bytes(s) <= NAV_BLE.frame.roadMaxBytes, "road too long").default(""),
  /** Only with state "arrived": the place, the trip's time and length. */
  arrival: z
    .object({
      place: z.string().refine((s) => utf8Bytes(s) <= NAV_BLE.frame.placeMaxBytes, "place too long"),
      tripS: u16,
      tripDam: u16,
    })
    .optional(),
});
export type NavTick = z.infer<typeof navTick>;
export type NavTickInput = z.input<typeof navTick>;

/**
 * Where the device is (state.page). home: the four buttons; code: a 声波码 shown; choose: 「在手机上选去哪」 (去别处);
 * routing: a route being worked out; countdown: 「3 秒后出发」; navigating; near: close to a turn; rerouting; arrived;
 * lost: no tick for NAV_BLE.staleTicks; overview: the whole route (a tap while navigating); preview: a route chosen
 * on the phone, 「在手机上点开始」 (design B5). Only ever added to at the end: the state stores the index.
 */
export const navPages = ["home", "code", "choose", "routing", "countdown", "navigating", "near", "rerouting", "arrived", "lost", "overview", "preview"] as const;
/**
 * Where a short press of the side key cancels (cancel.page): a route being worked out, the countdown, 去别处 (back
 * home), a route shown before the start (back home; the page stops offering it).
 */
export const navCancelPages = ["routing", "countdown", "choose", "preview"] as const;
/**
 * Why the page sends the device back before a trip (back.reason), and where to: cancelled on the phone, or no route:
 * home; choose: the user went back to choosing on the phone (from a route shown, or to pick another place), so the
 * device leaves the preview for 「在手机上选去哪」. Only ever added to at the end.
 */
export const navBackReasons = ["cancelled", "failed", "choose"] as const;

const bytesAtMost = (n: number, what: string) => z.string().refine((s) => utf8Bytes(s) <= n, `${what} too long`);

/**
 * 导航's app messages, the payload of a kind-4 frame (at most LINK.frame.appMaxBytes), its first byte the type
 * (NAV_APP). From the page: `hello` once connected (whether home and work are set: the device keeps it for its home
 * page, 「设个家」 when not); `preview` once a route is chosen in 去别处 (「去 图书馆 · 18 分钟 · 4.1 公里 · 在手机上点开始」:
 * the user starts it on the phone); `back` (go home: the phone cancelled, or no route; or back to 「在手机上选去哪」: the
 * user is choosing again). From the device: `cancel` (a short press on `page`: the page stops working out the route or
 * the preview, or leaves 去别处); `state`, on change and at most every 5 s (the last tick drawn, the battery, the
 * firmware). The last place arrived at the device keeps itself, from the arrived tick.
 * The countdown is the trip's since 11-14 (trip.ts): the app sends the countdown frame (encodeNavCountdown; the device
 * counts down `seconds` itself, and the first tick takes it out, 「现在出发」's at once), and in it the side key sends the
 * stop frame (encodeNavStop). So 导航 sends neither `countdown` nor `cancel` of page countdown as app messages any more:
 * both stay valid for any app that counts down on its own.
 */
const countdownShape = {
  seconds: z.number().int().min(1).max(9),
  mode: z.enum(navigationModes),
  /** The route's length in tens of metres and its time: 「回家 · 4.1 公里 · 18 分钟」. */
  remainingDam: u16,
  remainingS: u16,
  place: bytesAtMost(NAV_BLE.frame.placeMaxBytes, "place").refine((p) => p.length > 0, "place empty"),
};
export const navAppMessage = z.discriminatedUnion("type", [
  z.object({ type: z.literal("hello"), home: z.boolean(), work: z.boolean() }).strict(),
  z.object({ type: z.literal("countdown"), ...countdownShape }).strict(),
  z.object({ type: z.literal("back"), reason: z.enum(navBackReasons) }).strict(),
  z
    .object({
      type: z.literal("preview"),
      mode: z.enum(navigationModes),
      remainingDam: u16,
      remainingS: u16,
      place: bytesAtMost(NAV_BLE.frame.placeMaxBytes, "place").refine((p) => p.length > 0, "place empty"),
    })
    .strict(),
  z.object({ type: z.literal("cancel"), page: z.enum(navCancelPages) }).strict(),
  z
    .object({
      type: z.literal("state"),
      page: z.enum(navPages),
      battery: z.number().int().min(0).max(100).nullable(),
      charging: z.boolean(),
      /** 20% or under, not charging: the screen is only on 300 m before a turn (§2.2). */
      low: z.boolean(),
      /** The last tick drawn (NavTick.seq), null before the first. */
      seq: u16.nullable(),
      fw: bytesAtMost(NAV_BLE.frame.fwMaxBytes, "fw").refine((f) => f.length > 0, "fw empty"),
    })
    .strict(),
]);
export type NavAppMessage = z.infer<typeof navAppMessage>;
/** Each app message's first byte. */
export const NAV_APP = { hello: 1, countdown: 2, back: 3, cancel: 4, state: 5, preview: 6 } as const satisfies Record<NavAppMessage["type"], number>;

/** The overview: the route's length, the scale, and its points from the start (trip.ts rule 11). */
export const navOverview = z
  .object({
    lengthDam: u16,
    /** Metres per unit of the points. */
    scaleM: z.number().int().min(1).max(255),
    /** East (x) and north (y), in scaleM, each from the point before, the first from the start; the last the end. */
    points: z.array(navStep).min(1).max(NAV_BLE.frame.overviewMaxPoints),
  })
  .strict();
export type NavOverview = z.infer<typeof navOverview>;

// ---------------------------------------------------------------- the tick as bytes

const FLAG_ROUTE = 1,
  FLAG_ROAD = 2,
  FLAG_ARRIVAL = 4;

/**
 * Little-endian: version u8, kind u8 (0), seq u16, state u8, mode u8, action u8, exit u8, toActionM u16,
 * remainingDam u16, remainingS u16, progress u16, offRouteM u8, flags u8 (1 route, 2 road, 4 arrival), route count u8,
 * road bytes u8 — then the steps (int8 x, int8 y each), the road (UTF-8), and with arrival: place bytes u8, the place,
 * tripS u16, tripDam u16. Null when it would not fit LINK.frame.maxBytes with the envelope.
 */
export function encodeNavTick(input: NavTickInput): Uint8Array | null {
  const t = navTick.parse(input);
  const road = new TextEncoder().encode(t.road);
  const place = t.arrival ? new TextEncoder().encode(t.arrival.place) : new Uint8Array(0);
  const size =
    NAV_BLE.frame.headerBytes + t.route.length * 2 + road.length + (t.arrival ? 1 + place.length + 4 : 0);
  if (size + LINK.crypto.counterBytes + LINK.crypto.tagBytes > LINK.frame.maxBytes) return null;
  const b = new Uint8Array(size);
  const v = new DataView(b.buffer);
  v.setUint8(0, LINK.frame.version);
  v.setUint8(1, NAV_KIND.tick);
  v.setUint16(2, t.seq, true);
  v.setUint8(4, navStates.indexOf(t.state));
  v.setUint8(5, navigationModes.indexOf(t.mode));
  v.setUint8(6, navigationActions.indexOf(t.action));
  v.setUint8(7, t.exit);
  v.setUint16(8, t.toActionM, true);
  v.setUint16(10, t.remainingDam, true);
  v.setUint16(12, t.remainingS, true);
  v.setUint16(14, t.progress, true);
  v.setUint8(16, t.offRouteM);
  v.setUint8(17, (t.route.length ? FLAG_ROUTE : 0) | (road.length ? FLAG_ROAD : 0) | (t.arrival ? FLAG_ARRIVAL : 0));
  v.setUint8(18, t.route.length);
  v.setUint8(19, road.length);
  let at = NAV_BLE.frame.headerBytes;
  for (const [x, y] of t.route) {
    v.setInt8(at++, x);
    v.setInt8(at++, y);
  }
  b.set(road, at);
  at += road.length;
  if (t.arrival) {
    v.setUint8(at++, place.length);
    b.set(place, at);
    at += place.length;
    v.setUint16(at, t.arrival.tripS, true);
    v.setUint16(at + 2, t.arrival.tripDam, true);
  }
  return b;
}

/** Null when it is not a version-1 tick or anything in it is out of range (the device keeps the last one drawn). */
export function decodeNavTick(b: Uint8Array): NavTick | null {
  if (b.length < NAV_BLE.frame.headerBytes) return null;
  const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
  if (v.getUint8(0) !== LINK.frame.version || v.getUint8(1) !== NAV_KIND.tick) return null;
  const state = navStates[v.getUint8(4)];
  const mode = navigationModes[v.getUint8(5)];
  const action = navigationActions[v.getUint8(6)];
  const flags = v.getUint8(17);
  const count = v.getUint8(18);
  const roadLen = v.getUint8(19);
  if (!state || !mode || !action || count > NAV_BLE.frame.routeMaxPoints || roadLen > NAV_BLE.frame.roadMaxBytes) return null;
  if (!!(flags & FLAG_ROUTE) !== count > 0 || !!(flags & FLAG_ROAD) !== roadLen > 0) return null;
  let at = NAV_BLE.frame.headerBytes;
  if (b.length < at + count * 2 + roadLen) return null;
  const route: [number, number][] = [];
  for (let i = 0; i < count; i++, at += 2) route.push([v.getInt8(at), v.getInt8(at + 1)]);
  let road: string;
  try {
    road = new TextDecoder("utf-8", { fatal: true }).decode(b.subarray(at, at + roadLen));
  } catch {
    return null;
  }
  at += roadLen;
  let arrival: NavTick["arrival"];
  if (flags & FLAG_ARRIVAL) {
    if (b.length < at + 1) return null;
    const placeLen = v.getUint8(at++);
    if (placeLen > NAV_BLE.frame.placeMaxBytes || b.length < at + placeLen + 4) return null;
    let place: string;
    try {
      place = new TextDecoder("utf-8", { fatal: true }).decode(b.subarray(at, at + placeLen));
    } catch {
      return null;
    }
    at += placeLen;
    arrival = { place, tripS: v.getUint16(at, true), tripDam: v.getUint16(at + 2, true) };
    at += 4;
  }
  if (at !== b.length) return null;
  const parsed = navTick.safeParse({
    seq: v.getUint16(2, true),
    state,
    mode,
    action,
    exit: v.getUint8(7),
    toActionM: v.getUint16(8, true),
    remainingDam: v.getUint16(10, true),
    remainingS: v.getUint16(12, true),
    progress: v.getUint16(14, true),
    offRouteM: v.getUint8(16),
    route,
    road,
    ...(arrival && { arrival }),
  });
  return parsed.success && (parsed.data.state === "arrived" || !parsed.data.arrival) ? parsed.data : null;
}

/** version u8, kind u8 (3), seq u16, count u8, the steps: the route of the tick with this seq, when it did not fit. */
export function encodeNavRoute(seq: number, route: [number, number][]): Uint8Array {
  const steps = z.array(navStep).min(1).max(NAV_BLE.frame.routeMaxPoints).parse(route);
  const b = new Uint8Array(5 + steps.length * 2);
  const v = new DataView(b.buffer);
  v.setUint8(0, LINK.frame.version);
  v.setUint8(1, NAV_KIND.route);
  v.setUint16(2, u16.parse(seq), true);
  v.setUint8(4, steps.length);
  steps.forEach(([x, y], i) => {
    v.setInt8(5 + i * 2, x);
    v.setInt8(6 + i * 2, y);
  });
  return b;
}

export function decodeNavRoute(b: Uint8Array): { seq: number; route: [number, number][] } | null {
  if (b.length < 5 || b[0] !== LINK.frame.version || b[1] !== NAV_KIND.route) return null;
  const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const count = v.getUint8(4);
  if (count < 1 || count > NAV_BLE.frame.routeMaxPoints || b.length !== 5 + count * 2) return null;
  const route: [number, number][] = [];
  for (let i = 0; i < count; i++) route.push([v.getInt8(5 + i * 2), v.getInt8(6 + i * 2)]);
  return { seq: v.getUint16(2, true), route };
}

/**
 * version u8, kind u8 (6), count u8, scaleM u8, lengthDam u16 — then count × (int8 x, int8 y). At most 86 bytes, one
 * write at LINK.frame.minWriteBytes with the envelope.
 */
export function encodeNavOverview(input: NavOverview): Uint8Array {
  const o = navOverview.parse(input);
  const b = new Uint8Array(6 + o.points.length * 2);
  const v = new DataView(b.buffer);
  b.set([LINK.frame.version, NAV_KIND.overview, o.points.length, o.scaleM]);
  v.setUint16(4, o.lengthDam, true);
  o.points.forEach(([x, y], i) => {
    v.setInt8(6 + i * 2, x);
    v.setInt8(7 + i * 2, y);
  });
  return b;
}
export function decodeNavOverview(b: Uint8Array): NavOverview | null {
  if (b.length < 6 || b[0] !== LINK.frame.version || b[1] !== NAV_KIND.overview) return null;
  const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const count = b[2]!;
  if (b.length !== 6 + count * 2) return null;
  const points: [number, number][] = [];
  for (let i = 0; i < count; i++) points.push([v.getInt8(6 + i * 2), v.getInt8(7 + i * 2)]);
  const parsed = navOverview.safeParse({ lengthDam: v.getUint16(4, true), scaleM: b[3], points });
  return parsed.success ? parsed.data : null;
}

/**
 * The trip's countdown (trip.ts, 11-14): the seconds the device counts down itself, and the route's mode, length,
 * time and to.name (「回家 · 4.1 公里 · 18 分钟」), at the countdown's start and on reconnecting during it (the seconds
 * left, rounded up).
 */
export const navCountdown = z.object(countdownShape).strict();
export type NavCountdown = z.infer<typeof navCountdown>;
/**
 * version u8, kind u8 (7), then the bytes of the app message `countdown` after its type: seconds u8, mode u8,
 * remainingDam u16, remainingS u16, place bytes u8, the place (UTF-8). Little-endian.
 */
export function encodeNavCountdown(input: NavCountdown): Uint8Array {
  const payload = encodeNavApp({ type: "countdown", ...navCountdown.parse(input) });
  return Uint8Array.from([LINK.frame.version, NAV_KIND.countdown, ...payload.subarray(1)]);
}
export function decodeNavCountdown(b: Uint8Array): NavCountdown | null {
  if (b.length < 3 || b[0] !== LINK.frame.version || b[1] !== NAV_KIND.countdown) return null;
  const m = decodeNavApp(Uint8Array.from([NAV_APP.countdown, ...b.subarray(2)]));
  if (m?.type !== "countdown") return null;
  const { type: _type, ...countdown } = m;
  return countdown;
}
/**
 * version u8, kind u8 (8), nothing else: the device's app asks the trip to stop (its side key in the countdown, or an
 * end of its own). The trip ends as stopped (trip.ts tripEnded) and sends nothing back: the device has gone home.
 */
export function encodeNavStop(): Uint8Array {
  return Uint8Array.from([LINK.frame.version, NAV_KIND.stop]);
}
export function isNavStop(b: Uint8Array): boolean {
  return b.length === 2 && b[0] === LINK.frame.version && b[1] === NAV_KIND.stop;
}

/**
 * An app message's payload (what device.link.send takes, base64): type u8, then
 * - hello: flags u8 (1 home set, 2 work set);
 * - countdown: seconds u8, mode u8, remainingDam u16, remainingS u16, place bytes u8, the place (UTF-8);
 * - preview: as countdown without seconds: mode u8, remainingDam u16, remainingS u16, place bytes u8, the place;
 * - back: reason u8 (navBackReasons); cancel: page u8 (its index in navPages);
 * - state: page u8, battery u8 (255 not read yet), flags u8 (1 charging, 2 low, 4 seq), seq u16 (0 without), fw bytes
 *   u8, fw (UTF-8).
 * Little-endian.
 */
export function encodeNavApp(input: NavAppMessage): Uint8Array {
  const m = navAppMessage.parse(input);
  const text = (t: string) => {
    const u = new TextEncoder().encode(t);
    return [u.length, ...u];
  };
  const le16 = (n: number) => [n & 0xff, n >> 8];
  switch (m.type) {
    case "hello":
      return Uint8Array.from([NAV_APP.hello, (m.home ? 1 : 0) | (m.work ? 2 : 0)]);
    case "countdown":
      return Uint8Array.from([NAV_APP.countdown, m.seconds, navigationModes.indexOf(m.mode), ...le16(m.remainingDam), ...le16(m.remainingS), ...text(m.place)]);
    case "preview":
      return Uint8Array.from([NAV_APP.preview, navigationModes.indexOf(m.mode), ...le16(m.remainingDam), ...le16(m.remainingS), ...text(m.place)]);
    case "back":
      return Uint8Array.from([NAV_APP.back, navBackReasons.indexOf(m.reason)]);
    case "cancel":
      return Uint8Array.from([NAV_APP.cancel, navPages.indexOf(m.page)]);
    case "state":
      return Uint8Array.from([
        NAV_APP.state,
        navPages.indexOf(m.page),
        m.battery ?? 255,
        (m.charging ? 1 : 0) | (m.low ? 2 : 0) | (m.seq === null ? 0 : 4),
        ...le16(m.seq ?? 0),
        ...text(m.fw),
      ]);
  }
}

/** Null for anything else: an unknown type or flag, a wrong length, text that is not UTF-8, a value out of range. */
export function decodeNavApp(b: Uint8Array): NavAppMessage | null {
  if (b.length < 2 || b.length > LINK.frame.appMaxBytes) return null;
  const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const text = (at: number): string | null => {
    if (b.length !== at + 1 + b[at]!) return null;
    try {
      return new TextDecoder("utf-8", { fatal: true }).decode(b.subarray(at + 1));
    } catch {
      return null;
    }
  };
  let m: unknown = null;
  switch (b[0]) {
    case NAV_APP.hello:
      if (b.length === 2 && b[1]! <= 3) m = { type: "hello", home: !!(b[1]! & 1), work: !!(b[1]! & 2) };
      break;
    case NAV_APP.countdown: {
      const place = b.length >= 9 ? text(7) : null;
      if (place !== null) m = { type: "countdown", seconds: b[1], mode: navigationModes[b[2]!], remainingDam: v.getUint16(3, true), remainingS: v.getUint16(5, true), place };
      break;
    }
    case NAV_APP.preview: {
      const place = b.length >= 8 ? text(6) : null;
      if (place !== null) m = { type: "preview", mode: navigationModes[b[1]!], remainingDam: v.getUint16(2, true), remainingS: v.getUint16(4, true), place };
      break;
    }
    case NAV_APP.back:
      if (b.length === 2) m = { type: "back", reason: navBackReasons[b[1]!] };
      break;
    case NAV_APP.cancel:
      if (b.length === 2) m = { type: "cancel", page: navPages[b[1]!] };
      break;
    case NAV_APP.state: {
      const fw = b.length >= 8 ? text(6) : null;
      const flags = b[3]!;
      if (fw !== null && flags <= 7 && (flags & 4 || v.getUint16(4, true) === 0))
        m = { type: "state", page: navPages[b[1]!], battery: b[2] === 255 ? null : b[2], charging: !!(flags & 1), low: !!(flags & 2), seq: flags & 4 ? v.getUint16(4, true) : null, fw };
      break;
    }
  }
  const parsed = navAppMessage.safeParse(m);
  return parsed.success ? parsed.data : null;
}
