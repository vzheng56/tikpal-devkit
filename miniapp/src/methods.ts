/**
 * Every bridge method's capability and level, as plain data: what the tools check a package against and the preview
 * answers by. Never imported by the page API, so a built page carries only the methods its manifest declares
 * (tools/build.ts writes them in as __TIKPAL_ALLOWED__), and a method named anywhere else in it is one it calls.
 * test/api.test.ts holds this table to the contract's MINIAPP_BRIDGE.
 */
import type { MiniappCapability, MiniappMethod } from "./contract/miniapps.js";

export const METHODS: Readonly<Record<MiniappMethod, { capability: MiniappCapability | null; level: number }>> = {
  "env.get": { capability: null, level: 1 },
  "app.close": { capability: null, level: 1 },
  "recordings.list": { capability: "recordings", level: 1 },
  "recordings.get": { capability: "recordings", level: 1 },
  "recordings.transcribe": { capability: "recordings", level: 1 },
  "recordings.delete": { capability: "recordings", level: 1 },
  "clipboard.write": { capability: "clipboard", level: 1 },
  "audio.play": { capability: "audio", level: 1 },
  "audio.control": { capability: "audio", level: 1 },
  "user.id": { capability: "user", level: 2 },
  "records.list": { capability: "records", level: 2 },
  "records.add": { capability: "records", level: 2 },
  "settings.get": { capability: "settings", level: 2 },
  "settings.set": { capability: "settings", level: 2 },
  "scan.code": { capability: "scan", level: 2 },
  "share.text": { capability: "share", level: 2 },
  "photos.pick": { capability: "photos", level: 2 },
  "photos.save": { capability: "photos", level: 2 },
  "device.link.open": { capability: "device.link", level: 3 },
  "device.link.send": { capability: "device.link", level: 3 },
  "device.link.close": { capability: "device.link", level: 3 },
  "trip.start": { capability: "trip", level: 3 },
  "trip.status": { capability: "trip", level: 3 },
  "trip.stop": { capability: "trip", level: 3 },
  "map.routes": { capability: "map", level: 3 },
  "map.tips": { capability: "map", level: 3 },
  "map.search": { capability: "map", level: 3 },
  "map.geocode": { capability: "map", level: 3 },
  "map.regeo": { capability: "map", level: 3 },
  "map.static": { capability: "map", level: 3 },
  "places.saved": { capability: "places.saved", level: 3 },
  "storage.get": { capability: "storage", level: 3 },
  "storage.set": { capability: "storage", level: 3 },
  "storage.remove": { capability: "storage", level: 3 },
  "storage.keys": { capability: "storage", level: 3 },
};

/** The highest level the kit knows: the preview's container offers it unless the panel says less. */
export const KIT_LEVEL = Math.max(...Object.values(METHODS).map((m) => m.level));

/** The events the app sends unasked, and the capability each needs (null: every page gets it). */
export const EVENTS: Readonly<Record<string, MiniappCapability | null>> = {
  "audio.state": "audio",
  "env.changed": null,
  "device.link.state": "device.link",
  "device.link.message": "device.link",
  "trip.progress": "trip",
  "trip.rerouted": "trip",
  "trip.ended": "trip",
  // A 声波码 of the page's own action (app.json voiceCode.own): no capability of its own.
  "voiceCode.scanned": null,
};

/** The methods a manifest's capabilities allow: the ones every page has, and its declared groups'. */
export function allowedMethods(capabilities: readonly string[]): MiniappMethod[] {
  return (Object.keys(METHODS) as MiniappMethod[]).filter((m) => {
    const capability = METHODS[m].capability;
    return capability === null || capabilities.includes(capability);
  });
}
