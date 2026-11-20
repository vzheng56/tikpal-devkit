// 生成的，别手改：packages/contracts/src/trip.ts 里开发包用得到的那些（scripts/sync.ts KEPT），别的不进公开包。改那份，再跑 pnpm miniapp:sync。
/**
 * 后台行程会话 (level 3, `trip`, asked for and reviewed): the page hands the app a route from map.routes and the app
 * follows it, whether the page runs or not: the position in the background, how far to the next step, rerouting when
 * the user leaves the route, and the user's Tikpal Voice told where to turn when the page's app on it is connected.
 * While the page is in front it gets trip.progress once a second; back in front it reads trip.status. One trip at a
 * time; closing the page does not end it, arriving or trip.stop do.
 *
 * A countdown before setting off is the trip's (countdownSeconds): call trip.start the moment the user taps to go,
 * with the page in front, never after a countdown of the page's own (a page at the lock screen counts nothing, and
 * the position cannot start from there). The trip is in state countdown until `endsAt`, shows the countdown on the
 * device (the countdown frame) and starts by itself; trip.stop in it cancels. trip.progress.position comes only with
 * `location` in the manifest and allowed by the user.
 */
import { z } from "zod";
import { gcjPoint } from "./navigation.js";
import { mapRoute } from "./map.js";

export const TRIP = {
  earthRadiusM: 6371008.8,
  window: { backM: 50, aheadM: 300 },
  poorFixM: 50,
  /** #953 §2.3, the starting values; the road test settles them (a contract change, no app release). */
  offRoute: {
    bike: { meters: 40, seconds: 8 },
    ebike: { meters: 40, seconds: 8 },
    walk: { meters: 30, seconds: 10 },
  },
  beep: {
    bike: { onceM: 200, twiceM: 50 },
    ebike: { onceM: 200, twiceM: 50 },
    walk: { onceM: 100, twiceM: 20 },
  },
  /** #953 §2.3's 50 m (the device's 「到了」); the apps proposed 20 m on 11-13: the road test settles it. */
  arriveM: 50,
  aheadM: 400,
  aheadPieceM: 120,
  aheadMaxPoints: 40,
  rerouteRetrySeconds: 10,
  rerouteTimeoutSeconds: 15,
  /** How often the page gets trip.progress while it is in front, and the device a tick. */
  progressMs: 1000,
} as const;

/** countdown (11-14): from trip.start until its countdownSeconds are up (the file's header). Only ever added to. */
export const tripStates = ["navigating", "rerouting", "arrived", "countdown"] as const;

/**
 * trip.start: the route the user chose (one of map.routes'), whether the phone says the turns aloud (off by default),
 * and the seconds counted down before it sets off (0 by default; 导航 uses NAV_BLE.countdownSeconds), at most what
 * the device's countdown frame carries.
 */
export const tripStart = z
  .object({ route: mapRoute, voice: z.boolean().default(false), countdownSeconds: z.number().int().min(0).max(9).default(0) })
  .strict();
export type TripStart = z.input<typeof tripStart>;
/**
 * busy: another 小程序's trip is on (the user ends it first); denied: the user said no to the card or to the system's
 * location, or gave only an approximate one (iOS 精确位置 off, Android coarse only); background_denied: the system would
 * stop the position at the lock screen (Android, the foreground service of type location not allowed to start), so
 * the page says so and does not start. iOS never answers it: 「使用期间」 with the location background mode keeps a trip
 * started in front going (and trip.start comes in front, the countdown included: the file's header).
 */
export const tripStarted = z.union([
  z.object({ started: z.literal(true) }).strict(),
  z.object({ started: z.literal(false), reason: z.enum(["busy", "denied", "background_denied"]) }).strict(),
]);

/**
 * Once a second while the page is in front (trip.progress), and as trip.status's. In the countdown the numbers are the
 * route's from its start (along 0: remainingM its line's length, remainingS its seconds, progress 0, offRouteM 0).
 */
export const tripProgress = z
  .object({
    state: z.enum(tripStates),
    /**
     * Only in the countdown: when it is up, in milliseconds since 1970 by the phone's clock (the page's Date.now()), so
     * the page draws the seconds left itself, back from the lock screen too.
     */
    endsAt: z.number().int().positive().optional(),
    /** The next step's index in the route's steps. */
    step: z.number().int().min(0),
    toActionM: z.number().int().min(0).max(65535),
    remainingM: z.number().int().min(0),
    remainingS: z.number().int().min(0),
    progress: z.number().int().min(0).max(1000),
    offRouteM: z.number().int().min(0),
    /**
     * Where the rider is on the route (GCJ-02), for the page to draw them on its map; null before the first good fix.
     * Only when the 小程序's manifest names `location` and the user has allowed it for this 小程序 (location's card,
     * which the page asks with navigator.geolocation, never trip.start): the same position the page would get open, and
     * a trip does not go round that card. Otherwise always null.
     */
    position: gcjPoint.nullable(),
    /** Heading-up's direction, degrees from north, clockwise; null standing still. */
    heading: z.number().min(0).lt(360).nullable(),
    /** Whether the app on Tikpal Voice is connected now. */
    device: z.boolean(),
  })
  .strict()
  .refine((p) => (p.state === "countdown") === (p.endsAt !== undefined), "endsAt in the countdown, and only then");
export type TripProgress = z.infer<typeof tripProgress>;

/**
 * The trip is over: arrived, stopped (the page, the notification, the app, or the device's stop frame; in the
 * countdown tripS and tripM 0), or failed (the position gone for good).
 */
export const tripEnded = z
  .object({ reason: z.enum(["arrived", "stopped", "failed"]), tripS: z.number().int().min(0), tripM: z.number().int().min(0) })
  .strict();

/**
 * trip.status: the trip on, with its route now (a reroute replaces it); or the last one over, with how it ended (so a
 * page opened after arriving at the lock screen says 「到了」 with the time and length), kept by the app (in memory is
 * enough) until the next trip.start or a trip.stop; or none.
 */
export const tripStatus = z.union([
  z.object({ state: z.literal("none") }).strict(),
  z.object({ route: mapRoute, progress: tripProgress }).strict(),
  z.object({ route: mapRoute, ended: tripEnded }).strict(),
]);
