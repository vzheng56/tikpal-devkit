/**
 * Page helpers for 能力套装 3, plain functions (no zod in a page): where a point falls on a static map (map.static's
 * view), and the device link's messages as bytes. mapProject is Amap's static map as measured on Amap's own images: the
 * world at zoom z is 512·2^z CSS pixels wide, not the 256·2^z of web tiles, the same as the contract's map.ts
 * mapProject; test/level3.test.ts holds this one to it and to its fixtures.
 */
import type { MapView } from "./contract/map.js";

/** Amap's static map: the world's width at zoom 0, in CSS pixels (before `scale`). */
const WORLD = 512;
const mercX = (lng: number, zoom: number) => ((lng + 180) / 360) * WORLD * 2 ** zoom;
const mercY = (lat: number, zoom: number) => {
  const s = Math.sin((lat * Math.PI) / 180);
  return (0.5 - Math.log((1 + s) / (1 - s)) / (4 * Math.PI)) * WORLD * 2 ** zoom;
};

/**
 * Where a GCJ-02 point falls on a static map, in its CSS pixels from the top left (before `scale`). Off the image it
 * comes out below 0 or past the width or height: time to ask map.static for another one around the rider.
 */
export function mapProject(view: Pick<MapView, "center" | "zoom" | "width" | "height">, point: { lng: number; lat: number }): { x: number; y: number } {
  const x = view.width / 2 + mercX(point.lng, view.zoom) - mercX(view.center.lng, view.zoom);
  const y = view.height / 2 + mercY(point.lat, view.zoom) - mercY(view.center.lat, view.zoom);
  return { x: Math.round(x * 100) / 100, y: Math.round(y * 100) / 100 };
}

/** Bytes for device.link.send: `{ data: linkData(bytes) }`. At most 86 bytes a message (LINK.frame.appMaxBytes). */
export function linkData(bytes: Uint8Array): string {
  let text = "";
  for (const b of bytes) text += String.fromCharCode(b);
  return btoa(text);
}

/** The bytes of a device.link.message event's `data`. */
export function linkBytes(data: string): Uint8Array {
  const text = atob(data);
  const out = new Uint8Array(text.length);
  for (let i = 0; i < text.length; i++) out[i] = text.charCodeAt(i);
  return out;
}
