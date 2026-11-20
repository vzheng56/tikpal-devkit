// 生成的，别手改：packages/contracts/src/map.ts 里开发包用得到的那些（scripts/sync.ts KEPT），别的不进公开包。改那份，再跑 pnpm miniapp:sync。
/**
 * 地图模块 (level 3, `map`): routes on foot, by bicycle or e-bike, typing suggestions, place search, address ↔ point and
 * a static map, from Amap through Tikpal's platform. The bridge's map.* methods take and give these. Every point is
 * GCJ-02 (`gcjPoint`). Positions are used for one request only. Home and work are never points: a route names them as
 * `{ place }` and needs `places.saved`. Each answer says `left`, what the user has left of that kind today; over a
 * limit is MAP_LIMIT. A static map comes with its view, and mapProject places a point on the image.
 */
import { z } from "zod";
import { NAVIGATION, gcjPoint, navigationModes, routeStep } from "./navigation.js";

export const MAP = {
  /** Straight-line metres from start to end at most, by mode: further is TOO_FAR before Amap is asked. */
  maxMeters: NAVIGATION.maxMeters,
  maxPoints: NAVIGATION.maxPoints,
  /** Routes asked for at once (Amap's alternative routes). */
  maxAlternatives: 3,
  queryMax: 80,
  tipsMax: 10,
  searchPageSize: 20,
  searchRadiusMax: 50_000,
  geocodeMax: 5,
  /** The static map (Amap /v3/staticmap): CSS pixels before `scale`, zoom levels, what may be drawn on it. */
  static: { minSide: 64, maxSide: 1024, minZoom: 3, maxZoom: 17, maxMarkers: 10, maxLines: 4, maxLinePoints: 100, fitMaxPoints: 50 },
  /**
   * The device's way in (mapDevicePath), the same bodies with two differences: `maxPoints` defaults to this when not
   * given, and `static` takes at most deviceStaticSide a side and answers the PNG's bytes themselves (Content-Type
   * image/png), its view (mapView) as JSON in the X-Map-View header, `left` in X-Map-Left: no base64, no big JSON.
   */
  devicePoints: 300,
  deviceStaticSide: 368,
} as const;

const label = z.string().trim().min(1);
/** Where a route goes: a point (a place found with tips or search), or the user's home or work, which stay on the platform. */
export const mapDestination = z.union([gcjPoint, z.object({ place: z.enum(["home", "work"]) }).strict()]);
export type MapDestination = z.infer<typeof mapDestination>;

/** POST /v1/map/routes. `from` is where the user is (or any point); `alternatives` how many routes at most. */
export const mapRoutesRequest = z
  .object({
    from: gcjPoint,
    to: mapDestination,
    /**
     * The place's name when `to` is a point (from tips or search): what route.to.name says, and the device on arriving.
     * Without it, 「终点」; with `{place}`, 家 or 公司 whatever it says.
     */
    name: label.max(80).optional(),
    mode: z.enum(navigationModes),
    alternatives: z.number().int().min(1).max(MAP.maxAlternatives).default(1),
    /** Fewer points for a small screen (a Tikpal Voice app); the line is thinned to this. */
    maxPoints: z.number().int().min(2).max(MAP.maxPoints).default(MAP.maxPoints),
  })
  .strict();

/** One route: the line (GCJ-02, start to end), its steps (the last `arrive`), and two things a list shows. */
export const mapRoute = z
  .object({
    mode: z.enum(navigationModes),
    meters: z.number().int().min(0),
    seconds: z.number().int().min(0),
    line: z.array(z.tuple([z.number(), z.number()])).min(2).max(MAP.maxPoints),
    steps: z.array(routeStep).min(1),
    /** The place's name, as the device says it on arriving (家, 公司, or the place's). */
    to: z.object({ name: label.max(80) }).strict(),
    /** How many turns: steps whose action is not straight or arrive. */
    turns: z.number().int().min(0),
    /** The roads it goes along most, longest first (at most two): 「走学院路、阜通东大街」. */
    via: z.array(label.max(60)).max(2),
  })
  .strict();
export type MapRoute = z.infer<typeof mapRoute>;
export const mapRoutesResult = z.object({ routes: z.array(mapRoute).min(1).max(MAP.maxAlternatives), left: z.number().int().min(0) }).strict();

/** A place from tips or search. `meters` from `near` when given (the platform works it out; Amap's tips do not say). */
export const mapPlace = z
  .object({
    name: label.max(80),
    address: z.string().max(200),
    city: z.string().max(40),
    district: z.string().max(40),
    /** Its entrance when Amap has one, else its point; null for a tip that is only words (a bus line, a district). */
    point: gcjPoint.nullable(),
    meters: z.number().int().min(0).nullable(),
  })
  .strict();
export type MapPlace = z.infer<typeof mapPlace>;

/** POST /v1/map/tips: what the user is typing. `near` puts the user's city and the nearest first. */
export const mapTipsRequest = z.object({ q: label.max(MAP.queryMax), near: gcjPoint.optional(), city: label.max(40).optional() }).strict();
export const mapTipsResult = z.object({ items: z.array(mapPlace).max(MAP.tipsMax), left: z.number().int().min(0) }).strict();

/** POST /v1/map/search: with `near` around it (within `radius`, nearest first), else by `city`. */
export const mapSearchRequest = z
  .object({
    q: label.max(MAP.queryMax),
    near: gcjPoint.optional(),
    city: label.max(40).optional(),
    radius: z.number().int().min(100).max(MAP.searchRadiusMax).optional(),
    page: z.number().int().min(1).max(10).default(1),
  })
  .strict();
export const mapSearchResult = z
  .object({ items: z.array(mapPlace).max(MAP.searchPageSize), next: z.number().int().min(2).nullable(), left: z.number().int().min(0) })
  .strict();

/** POST /v1/map/geocode: an address to points (the likeliest first); none found is an empty list, not an error. */
export const mapGeocodeRequest = z.object({ address: label.max(200), city: label.max(40).optional() }).strict();
export const mapGeocodeResult = z
  .object({
    items: z
      .array(z.object({ point: gcjPoint, address: z.string().max(200), level: z.string().max(20) }).strict())
      .max(MAP.geocodeMax),
    left: z.number().int().min(0),
  })
  .strict();
export const mapRegeoResult = z
  .object({ address: z.string().max(200), city: z.string().max(40), district: z.string().max(40), area: z.string().max(40), left: z.number().int().min(0) })
  .strict();

/** The colours a line or marker may take: the apps' own, by name (the platform turns them into Amap's). No blue. */
export const mapColors = ["coral", "sage", "ink", "gray"] as const;

/**
 * POST /v1/map/static: an image of the map. Either `center` and `zoom`, or `fit` (points the map must hold: the
 * platform picks centre and zoom). Markers and lines are drawn by Amap; a page drawing its own (the 「我」 that moves)
 * places them with mapProject.
 */
export const mapStaticRequest = z
  .object({
    center: gcjPoint.optional(),
    zoom: z.number().int().min(MAP.static.minZoom).max(MAP.static.maxZoom).optional(),
    fit: z.array(gcjPoint).min(2).max(MAP.static.fitMaxPoints).optional(),
    width: z.number().int().min(MAP.static.minSide).max(MAP.static.maxSide),
    height: z.number().int().min(MAP.static.minSide).max(MAP.static.maxSide),
    /** 2: twice the pixels, for a sharp image on a phone (Amap's scale=2). */
    scale: z.union([z.literal(1), z.literal(2)]).default(2),
    markers: z
      .array(z.object({ point: gcjPoint, color: z.enum(mapColors).default("coral"), label: z.string().regex(/^[0-9A-Z]$|^\p{Script=Han}$/u).optional() }).strict())
      .max(MAP.static.maxMarkers)
      .default([]),
    lines: z
      .array(z.object({ points: z.array(gcjPoint).min(2).max(MAP.static.maxLinePoints), color: z.enum(mapColors).default("coral") }).strict())
      .max(MAP.static.maxLines)
      .default([]),
  })
  .strict()
  .refine((r) => (r.fit !== undefined) !== (r.center !== undefined && r.zoom !== undefined), "either fit, or center and zoom");
export type MapStaticRequest = z.input<typeof mapStaticRequest>;

/** The image (a PNG data: URI, as a page's CSP allows) and the view it shows, for mapProject. */
export const mapView = z
  .object({
    center: gcjPoint,
    zoom: z.number().int().min(MAP.static.minZoom).max(MAP.static.maxZoom),
    width: z.number().int().min(MAP.static.minSide).max(MAP.static.maxSide),
    height: z.number().int().min(MAP.static.minSide).max(MAP.static.maxSide),
  })
  .strict();
export type MapView = z.infer<typeof mapView>;
export const mapStaticResult = mapView
  .extend({
    image: z.string().startsWith("data:image/png;base64,").max(3_000_000),
    scale: z.union([z.literal(1), z.literal(2)]),
    left: z.number().int().min(0),
  })
  .strict();

/** GET /v1/map/saved: the names of home and work (never their points or doors), for a 小程序 with `places.saved`. */
export const mapSavedResult = z
  .object({
    home: z.object({ name: label.max(80), city: z.string().max(40) }).strict().nullable(),
    work: z.object({ name: label.max(80), city: z.string().max(40) }).strict().nullable(),
  })
  .strict();

/**
 * The refusals, as `error.code`, with their HTTP status: MAP_UNAVAILABLE (503: no key, Amap failed or timed out, the
 * key's quota gone), MAP_LIMIT (429, mapLimitDetails), PLACE_NOT_SET (409: home or work not saved), TOO_FAR (422: over
 * MAP.maxMeters for the mode), NO_ROUTE (422: Amap has none this way), OUTSIDE_MAINLAND (422: Amap routes mainland
 * China only). A 小程序 naming `{place}` without `places.saved`, or calling `saved` without it, is 403 MINIAPP_SCOPE;
 * a Tikpal Voice app without them (/device/v1/map/*, and /internal/v1/map/* for it) is 403 OPERATION_NOT_ALLOWED,
 * the device's own code (device.ts deviceErrorCodes).
 * Amap's own codes are logged, never answered.
 */
export const MAP_ERRORS = ["MAP_UNAVAILABLE", "MAP_LIMIT", "PLACE_NOT_SET", "TOO_FAR", "NO_ROUTE", "OUTSIDE_MAINLAND"] as const;

/**
 * The world's width at Amap's static-map zoom z, in CSS pixels: 512 × 2^z (a standard 256-pixel tile map's z + 1).
 * Measured on Amap's own images (the backend, 11-14: zoom 12 and 14, two lines each, the span exactly twice 256's).
 */
const WORLD_AT_ZOOM_0 = 512;
const worldPx = (zoom: number) => WORLD_AT_ZOOM_0 * 2 ** zoom;
const mercX = (lng: number, zoom: number) => ((lng + 180) / 360) * worldPx(zoom);
const mercY = (lat: number, zoom: number) => {
  const s = Math.sin((lat * Math.PI) / 180);
  return (0.5 - Math.log((1 + s) / (1 - s)) / (4 * Math.PI)) * worldPx(zoom);
};

/**
 * Where a GCJ-02 point falls on a static map, in its CSS pixels (before `scale`), from its top left: Web Mercator, the
 * world 512 × 2^zoom pixels wide, as Amap draws it. Points off the image come out below 0 or past the width or height: the page fetches
 * another image when the 「我」 it draws is about to leave this one.
 */
export function mapProject(view: MapView, point: { lng: number; lat: number }): { x: number; y: number } {
  const x = view.width / 2 + mercX(point.lng, view.zoom) - mercX(view.center.lng, view.zoom);
  const y = view.height / 2 + mercY(point.lat, view.zoom) - mercY(view.center.lat, view.zoom);
  return { x: Math.round(x * 100) / 100, y: Math.round(y * 100) / 100 };
}
