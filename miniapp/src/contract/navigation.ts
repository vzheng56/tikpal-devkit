// 生成的，别手改：packages/contracts/src/navigation.ts 里开发包用得到的那些（scripts/sync.ts KEPT），别的不进公开包。改那份，再跑 pnpm miniapp:sync。
/**
 * What a route is made of (map.ts mapRoute): the modes (on foot, by bicycle, by e-bike), each step's action, and
 * GCJ-02 points (`gcjPoint`, Amap's datum: the app turns the system's WGS-84 into it).
 */
import { z } from "zod";

export const NAVIGATION = {
  /** Routes an account may ask for in a Beijing day. */
  routesPerDay: 60,
  /** Straight-line metres from start to end, at most, by mode: further is refused before Amap is asked. */
  maxMeters: { bike: 100_000, ebike: 150_000, walk: 30_000 },
  /** The route line, thinned to at most this many points (the phone follows it, the device draws only what is ahead). */
  maxPoints: 4000,
} as const;

/** 骑车, 电动车, 步行. */
export const navigationModes = ["bike", "ebike", "walk"] as const;
export type NavigationMode = (typeof navigationModes)[number];

/**
 * What to do at a step's start, as the device shows it (its arrow) and the phone says it. Amap's own words
 * (navi.action, navi.assistant_action) are turned into one of these on the server; one it does not know is
 * `straight`, with the step's instruction kept as Amap wrote it.
 */
export const navigationActions = [
  "straight",
  "left",
  "right",
  "slight_left",
  "slight_right",
  "sharp_left",
  "sharp_right",
  "uturn",
  "roundabout",
  "keep_left",
  "keep_right",
  "arrive",
] as const;
export type NavigationAction = (typeof navigationActions)[number];

/** A GCJ-02 point, Amap's datum: longitude and latitude, six decimals at most (about 10 cm). */
export const gcjPoint = z
  .object({
    lng: z.number().min(73).max(136),
    lat: z.number().min(3).max(54),
  })
  .strict();
export type GcjPoint = z.infer<typeof gcjPoint>;

/** One step: what to do where it starts, then how far and how long until the next. `at` is its first point in `line`. */
export const routeStep = z
  .object({
    action: z.enum(navigationActions),
    /** For `roundabout`: which exit, when Amap says. */
    exit: z.number().int().min(1).max(12).optional(),
    meters: z.number().int().min(0),
    seconds: z.number().int().min(0),
    /** The road it goes onto, if it has a name. */
    road: z.string().max(60).nullable(),
    /** Amap's own line for it (「向右前方行驶进入阜通东大街」): for the phone's list and its voice. */
    instruction: z.string().max(200),
    at: z.number().int().min(0),
  })
  .strict();
export type RouteStep = z.infer<typeof routeStep>;
