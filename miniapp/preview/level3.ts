/**
 * The preview's pretend answers for 能力套装 3: the device link, the trip, the map module, home and work, and storage.
 * Nothing here reaches a device, the position or Amap: the panel says what the world is like (Tikpal Voice near or
 * not, the map module refusing, home saved or not), and the answers follow it.
 *
 * - **device.link**: open connects after a moment (device.link.state connecting, then connected) when the panel says
 *   Tikpal Voice is near; send needs it connected and keeps to LINK.frame.appPerSecond; the panel's 「断一下」 loses it
 *   and connects again, as the app does by itself. What the device answers is the project's own (preview/fake.ts:
 *   ctx.emit("device.link.message", …)).
 * - **trip**: started, it goes along the route at the mode's pace (×10 when the panel says), trip.progress every second,
 *   「走岔了」 a few seconds off the route then trip.rerouted, 「到了」 to the end; then trip.ended.
 * - **map.***: answers about the contract's example route home (fixtures/miniapps.json level3, Wangjing, Beijing); 「我这里」 (no `from`, `near`,
 *   `point`) needs `location` declared, `{ place }` needs `places.saved`; the static map is a drawn stand-in with
 *   the same view, so mapProject puts a point where it would be on the real one.
 * - **storage.***: kept by the preview's window, so it outlives reopening the page; the contract's limits apply.
 */
import { LINK } from "../src/contract/device-link.js";
import { MINIAPP } from "../src/contract/miniapps.js";
import type { MapRoute, MapView, Params, Result, TripProgress } from "../src/index.js";
import miniapps from "./fixtures/miniapps.json";
import { fail } from "./failure.js";
import type { FakeContext, FakeHandlers, PreviewSettings } from "./types.js";

type Point = { lng: number; lat: number };
type Manifest = { capabilities: string[] };
/** What the panel's buttons do to the page's world. */
export type Level3Controls = { scan(): void; drop(): void; offRoute(): void; arrive(): void };

const R = 6371008.8;
const rad = Math.PI / 180;
/** Metres between two points, equirectangular at their mean latitude (as trip.ts follows a route). */
function metres(a: [number, number], b: [number, number]) {
  const x = (b[0] - a[0]) * rad * Math.cos(((a[1] + b[1]) / 2) * rad) * R;
  const y = (b[1] - a[1]) * rad * R;
  return Math.hypot(x, y);
}
const bearing = (a: [number, number], b: [number, number]) => {
  const x = (b[0] - a[0]) * Math.cos(((a[1] + b[1]) / 2) * rad);
  const y = b[1] - a[1];
  return x === 0 && y === 0 ? null : ((Math.atan2(x, y) / rad) % 360 + 360) % 360;
};
/** A pace for each way, metres a second (the preview's own, near enough). */
const PACE = { bike: 4.2, ebike: 6, walk: 1.3 } as const;

type Call = { request: { method: string }; response: { ok: boolean; result?: unknown } };
/** The contract's level 3 example answer of map.routes: routes home in Wangjing, Beijing. */
const example = (miniapps.level3.calls.valid as Call[]).find((c) => c.request.method === "map.routes" && c.response.ok)!.response.result as Result<"map.routes">;
const HERE: Point = { lng: example.routes[0].line[0][0], lat: example.routes[0].line[0][1] };
const PLACES = ["望京 SOHO", "望京公园", "望京地铁站", "望京西园四区", "方恒购物中心", "望京医院", "阜通地铁站", "花家地公园"];

/** The view a static map shows: centre and zoom as given, or the largest zoom that holds every `fit` point. */
export function viewOf(p: Params<"map.static">): MapView {
  const { width, height } = p;
  if (p.center && p.zoom) return { center: p.center, zoom: p.zoom, width, height };
  const fit = p.fit ?? [HERE];
  const x = (lng: number) => (lng + 180) / 360;
  const y = (lat: number) => {
    const s = Math.sin(lat * rad);
    return 0.5 - Math.log((1 + s) / (1 - s)) / (4 * Math.PI);
  };
  const xs = fit.map((f) => x(f.lng));
  const ys = fit.map((f) => y(f.lat));
  const [x0, x1, y0, y1] = [Math.min(...xs), Math.max(...xs), Math.min(...ys), Math.max(...ys)];
  let zoom = 17;
  while (zoom > 3 && ((x1 - x0) * 512 * 2 ** zoom > width - 40 || (y1 - y0) * 512 * 2 ** zoom > height - 40)) zoom--;
  const cy = (y0 + y1) / 2;
  const lat = (Math.atan(Math.sinh(Math.PI * (1 - 2 * cy))) / rad);
  const center = { lng: Math.round((((x0 + x1) / 2) * 360 - 180) * 1e6) / 1e6, lat: Math.round(lat * 1e6) / 1e6 };
  return { center, zoom, width, height };
}

/** Where a point falls on a view (Web Mercator, the world 512·2^zoom wide, as Amap's static map): src/map.ts mapProject's. */
function project(view: MapView, p: Point) {
  const world = 512 * 2 ** view.zoom;
  const mx = (lng: number) => ((lng + 180) / 360) * world;
  const my = (lat: number) => {
    const s = Math.sin(lat * rad);
    return (0.5 - Math.log((1 + s) / (1 - s)) / (4 * Math.PI)) * world;
  };
  return { x: view.width / 2 + mx(p.lng) - mx(view.center.lng), y: view.height / 2 + my(p.lat) - my(view.center.lat) };
}

const COLORS = { coral: "#df795d", sage: "#7f9a6e", ink: "#2d322e", gray: "#9a9d96" } as const;

/** A drawn stand-in for Amap's image: paper, a street grid, the request's lines and markers, Amap's name where its mark is. */
function drawn(view: MapView, p: Params<"map.static">, scale: 1 | 2): string {
  const canvas = document.createElement("canvas");
  canvas.width = view.width * scale;
  canvas.height = view.height * scale;
  const g = canvas.getContext("2d")!;
  g.scale(scale, scale);
  g.fillStyle = "#efece4";
  g.fillRect(0, 0, view.width, view.height);
  g.strokeStyle = "#ffffff";
  g.lineWidth = 6;
  for (let i = 24; i < Math.max(view.width, view.height); i += 72) {
    g.beginPath();
    g.moveTo(i, 0);
    g.lineTo(i - 20, view.height);
    g.moveTo(0, i);
    g.lineTo(view.width, i + 12);
    g.stroke();
  }
  for (const line of p.lines ?? []) {
    g.strokeStyle = COLORS[line.color ?? "coral"];
    g.lineWidth = 5;
    g.lineJoin = "round";
    g.beginPath();
    line.points.forEach((pt, i) => {
      const at = project(view, pt);
      if (i) g.lineTo(at.x, at.y);
      else g.moveTo(at.x, at.y);
    });
    g.stroke();
  }
  for (const m of p.markers ?? []) {
    const at = project(view, m.point);
    g.fillStyle = COLORS[m.color ?? "coral"];
    g.beginPath();
    g.arc(at.x, at.y, 8, 0, Math.PI * 2);
    g.fill();
  }
  g.fillStyle = "#8b8f87";
  g.font = "12px system-ui, sans-serif";
  g.textAlign = "center";
  g.fillText("预览：画出来的，不是真的地图", view.width / 2, view.height / 2);
  g.textAlign = "left";
  g.font = "bold 11px system-ui, sans-serif";
  g.fillText("高德地图", 6, view.height - 6);
  return canvas.toDataURL("image/png");
}

/** Where the preview keeps a page's storage: the preview's own window, so it outlives reopening the page. */
function storageOf(): Map<string, string> {
  if (typeof window === "undefined") return new Map(); // Node (the kit's tests): the page's alone
  const box = (() => {
    try {
      return window.parent !== window ? (window.parent as unknown as { __tikpalStorage?: Map<string, string> }) : null;
    } catch {
      return null;
    }
  })() ?? (window as unknown as { __tikpalStorage?: Map<string, string> });
  box.__tikpalStorage ??= new Map();
  return box.__tikpalStorage;
}

export function level3(ctx: FakeContext, manifest: Manifest, settings: () => PreviewSettings): { handlers: FakeHandlers; controls: Level3Controls } {
  const declared = (c: string) => manifest.capabilities.includes(c) && !settings().denied.includes(c);
  const left = { routes: 57, search: 180, geo: 95, static: 90 };
  const spend = (kind: keyof typeof left) => (left[kind] = Math.max(0, left[kind] - 1));

  // ---------------------------------------------------------------- the map module

  /** 「我这里」: the app finds the position, which takes `location` declared (and its card agreed). */
  function here(given: Point | undefined, what: string): Point {
    if (given) return given;
    if (!declared("location")) throw fail("NOT_DECLARED", { message: `${what} 不写就是「我这里」：manifest.json 要声明 location（用户要点同意）` });
    return HERE;
  }
  function refused(kind: keyof typeof left, route = false) {
    const s = settings().map;
    if (s === "limit") throw fail("SERVER", { status: 429, serverCode: "MAP_LIMIT", message: `今天的${kind}次数用完了（预览）` });
    if (s === "down") throw fail("SERVER", { status: 503, serverCode: "MAP_UNAVAILABLE" });
    if (route && s === "far") throw fail("SERVER", { status: 422, serverCode: "TOO_FAR" });
    if (route && s === "noroute") throw fail("SERVER", { status: 422, serverCode: "NO_ROUTE" });
    if (route && s === "outside") throw fail("SERVER", { status: 422, serverCode: "OUTSIDE_MAINLAND" });
  }
  const placeName = { home: "家", work: "公司" } as const;
  function saved(place: "home" | "work") {
    if (!declared("places.saved")) throw fail("SERVER", { status: 403, serverCode: "MINIAPP_SCOPE", message: "去家、公司要声明 places.saved（需申请）" });
    const s = settings().places;
    if (s === "none" || (s === "home" && place === "work")) throw fail("SERVER", { status: 409, serverCode: "PLACE_NOT_SET" });
  }
  const place = (name: string, near: Point | undefined, i: number): Result<"map.tips">["items"][number] => {
    const point = { lng: Math.round((HERE.lng + 0.004 * Math.cos(i * 1.7)) * 1e6) / 1e6, lat: Math.round((HERE.lat + 0.003 * Math.sin(i * 1.3)) * 1e6) / 1e6 };
    return {
      name,
      address: `朝阳区望京街道${i + 1}号`,
      city: "北京市",
      district: "朝阳区",
      point,
      meters: near ? Math.round(metres([near.lng, near.lat], [point.lng, point.lat])) : null,
    };
  };
  const found = (q: string, near: Point | undefined, n: number) => {
    const names = [q, ...PLACES.filter((p) => p.includes(q) || q.includes(p.slice(0, 2))), ...PLACES.map((p) => `${q}（${p}）`)];
    const items = [...new Set(names)].slice(0, n).map((name, i) => place(name, near, i));
    return near ? items.sort((a, b) => (a.meters ?? 0) - (b.meters ?? 0)) : items;
  };

  function routes(p: Params<"map.routes">): Result<"map.routes"> {
    here(p.from, "map.routes 的 from");
    const to = "place" in p.to ? p.to.place : null;
    if (to) saved(to);
    refused("routes", true);
    const mode = p.mode;
    const out = example.routes.slice(0, p.alternatives ?? 1).map((r): MapRoute => ({
      ...structuredClone(r),
      mode,
      seconds: Math.round(r.meters / PACE[mode]),
      to: { name: to ? placeName[to] : "目的地" },
    }));
    return { routes: out, left: spend("routes") };
  }

  // ---------------------------------------------------------------- the trip

  type Trip = { route: MapRoute; along: number; state: "navigating" | "rerouting" | "arrived"; off: number; started: number; timer: ReturnType<typeof setInterval> };
  let trip: Trip | null = null;
  let link: { connected: boolean; mark: number } = { connected: false, mark: 0 };
  const sent: number[] = [];

  function measure(route: MapRoute) {
    const at = [0];
    for (let i = 1; i < route.line.length; i++) at.push(at[i - 1] + metres(route.line[i - 1], route.line[i]));
    return at;
  }
  function progress(t: Trip): { route: MapRoute; progress: TripProgress } {
    const at = measure(t.route);
    const total = at[at.length - 1] || 1;
    const along = Math.min(t.along, total);
    let seg = 0;
    while (seg < at.length - 2 && at[seg + 1] < along) seg++;
    const a = t.route.line[seg];
    const b = t.route.line[seg + 1] ?? a;
    const f = at[seg + 1] - at[seg] > 0 ? (along - at[seg]) / (at[seg + 1] - at[seg]) : 0;
    const position = { lng: Math.round((a[0] + (b[0] - a[0]) * f) * 1e6) / 1e6, lat: Math.round((a[1] + (b[1] - a[1]) * f) * 1e6) / 1e6 };
    let step = t.route.steps.findIndex((s) => at[s.at] > along);
    if (step < 0) step = t.route.steps.length - 1;
    const remainingM = Math.max(0, Math.round(total - along));
    return {
      route: t.route,
      progress: {
        state: t.state,
        step,
        toActionM: Math.min(65535, Math.max(0, Math.round(at[t.route.steps[step].at] - along))),
        remainingM,
        remainingS: Math.ceil((remainingM / total) * t.route.seconds),
        progress: Math.floor((along / total) * 1000),
        offRouteM: t.off,
        position,
        heading: bearing(a, b),
        device: link.connected,
      },
    };
  }
  function end(reason: "arrived" | "stopped" | "failed") {
    if (!trip) return;
    clearInterval(trip.timer);
    const done = trip;
    trip = null;
    ctx.emit("trip.ended", { reason, tripS: Math.round((Date.now() - done.started) / 1000), tripM: Math.round(Math.min(done.along, measure(done.route).at(-1) ?? 0)) });
  }
  function tick() {
    if (!trip) return;
    const total = measure(trip.route).at(-1) ?? 0;
    if (trip.off > 0) {
      trip.off += 15;
      if (trip.off > 60) {
        trip.off = 0;
        trip.state = "navigating";
        ctx.emit("trip.rerouted", { route: trip.route });
      } else trip.state = "rerouting";
    } else trip.along += PACE[trip.route.mode] * (settings().tripSpeed ?? 1);
    if (trip.along >= total) {
      trip.along = total;
      trip.state = "arrived";
    }
    ctx.emit("trip.progress", progress(trip).progress);
    if (trip.state === "arrived") end("arrived");
  }

  // ---------------------------------------------------------------- storage

  const store = storageOf();
  const total = () => [...store.values()].reduce((n, v) => n + v.length, 0);

  const handlers: FakeHandlers = {
    "device.link.open": async (p) => {
      const near = settings().device;
      if (near === "off") return { connected: false, reason: "bluetooth_off" };
      if (near === "none") return { connected: false, reason: "no_device" };
      if (near === "key") return { connected: false, reason: "key_mismatch" };
      if (near === "several" && p.mark === undefined) return { connected: false, reason: "several" };
      ctx.emit("device.link.state", { state: "connecting" });
      await new Promise((r) => setTimeout(r, 600));
      link = { connected: true, mark: p.mark ?? 0x79096 };
      ctx.emit("device.link.state", { state: "connected" });
      return { connected: true, name: "Tikpal Voice", mark: link.mark };
    },
    "device.link.send": (p) => {
      if (!link.connected) throw fail("FAILED", { message: "设备通道没连上：先 device.link.open" });
      const now = Date.now();
      while (sent.length && now - sent[0] > 1000) sent.shift();
      if (sent.length >= LINK.frame.appPerSecond) throw fail("BAD_PARAMS", { message: `一秒最多发 ${LINK.frame.appPerSecond} 条` });
      sent.push(now);
      const bytes = Math.floor((p.data.length * 3) / 4) - (p.data.endsWith("==") ? 2 : p.data.endsWith("=") ? 1 : 0);
      if (bytes > LINK.frame.appMaxBytes) throw fail("BAD_PARAMS", { message: `一条最多 ${LINK.frame.appMaxBytes} 字节` });
      ctx.toast(`发给 Tikpal Voice：${bytes} 字节`);
      return {};
    },
    "device.link.close": () => {
      if (link.connected) ctx.emit("device.link.state", { state: "closed" });
      link = { connected: false, mark: 0 };
      return {};
    },
    "trip.start": (p) => {
      const s = settings().trip;
      if (trip) return { started: false, reason: "busy" };
      if (s === "busy") return { started: false, reason: "busy" };
      if (s === "background") return { started: false, reason: "background_denied" };
      trip = { route: structuredClone(p.route) as MapRoute, along: 0, state: "navigating", off: 0, started: Date.now(), timer: setInterval(tick, 1000) };
      return { started: true };
    },
    "trip.status": () => (trip ? progress(trip) : { state: "none" }),
    "trip.stop": () => {
      end("stopped");
      return {};
    },
    "map.routes": (p) => routes(p),
    "map.tips": (p) => {
      const near = here(p.near, "map.tips 的 near");
      refused("search");
      return { items: found(p.q, near, 6), left: spend("search") };
    },
    "map.search": (p) => {
      const near = here(p.near, "map.search 的 near");
      refused("search");
      return { items: (p.page ?? 1) > 1 ? [] : found(p.q, near, 8), next: null, left: spend("search") };
    },
    "map.geocode": (p) => {
      refused("geo");
      return { items: [{ point: place(p.address, undefined, 1).point!, address: `北京市朝阳区${p.address}`, level: "兴趣点" }], left: spend("geo") };
    },
    "map.regeo": (p) => {
      here(p.point, "map.regeo 的 point");
      refused("geo");
      return { address: "北京市朝阳区望京街道望京西园四区", city: "北京市", district: "朝阳区", area: "望京", left: spend("geo") };
    },
    "map.static": (p) => {
      refused("static");
      const view = viewOf(p);
      const scale = p.scale ?? 2;
      return { ...view, image: drawn(view, p, scale), scale, left: spend("static") };
    },
    "places.saved": () => {
      const s = settings().places;
      return {
        home: s === "none" ? null : { name: "望京西园四区", city: "北京市" },
        work: s === "both" ? { name: "望京 SOHO", city: "北京市" } : null,
      };
    },
    "storage.get": (p) => ({ value: store.get(p.key) ?? null }),
    "storage.set": (p) => {
      if (!store.has(p.key) && store.size >= MINIAPP.storage.keys) throw fail("BAD_PARAMS", { message: `最多 ${MINIAPP.storage.keys} 个键` });
      if (total() - (store.get(p.key)?.length ?? 0) + p.value.length > MINIAPP.storage.totalChars) throw fail("BAD_PARAMS", { message: `一共最多 ${MINIAPP.storage.totalChars} 个字` });
      store.set(p.key, p.value);
      return {};
    },
    "storage.remove": (p) => {
      store.delete(p.key);
      return {};
    },
    "storage.keys": () => ({ keys: [...store.keys()] }),
  };

  const controls: Level3Controls = {
    scan() {
      const { action, value } = settings().launch;
      ctx.emit("voiceCode.scanned", { action, value });
    },
    drop() {
      if (!link.connected) return;
      ctx.emit("device.link.state", { state: "lost" });
      setTimeout(() => {
        if (!link.connected) return;
        ctx.emit("device.link.state", { state: "connecting" });
        setTimeout(() => link.connected && ctx.emit("device.link.state", { state: "connected" }), 800);
      }, 2000);
    },
    offRoute() {
      if (trip && trip.state === "navigating") trip.off = 15;
    },
    arrive() {
      if (trip) trip.along = Number.MAX_SAFE_INTEGER;
    },
  };
  return { handlers, controls };
}
