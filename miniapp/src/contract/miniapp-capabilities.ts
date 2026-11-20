// 生成的，别手改：packages/contracts/src/miniapp-capabilities.ts 里开发包用得到的那些（scripts/sync.ts KEPT），别的不进公开包。改那份，再跑 pnpm miniapp:sync。
/**
 * 小程序's capabilities (能力套装, docs/delivery/2026-11-01-developer-platform.md §4.3, the plan's 2.1 and 2.5): every
 * capability a page may name in its manifest, which of the three kinds it is, the capability level the container offers
 * it from, and how it is granted. miniapps.ts builds the manifest and the bridge on it, app-spec.ts the 小程序 part of
 * app.json, the developer platform's C8 page shows it, and the review goes by it. Yaykit reads app specs, so this file
 * is copied there too (yaykit-copies.ts).
 *
 * The kinds (§4.3): 1, what a WebView has anyway (camera, microphone, motion, location, the clipboard), each allowed
 * per 小程序; 2, what the app shows for the user to pick (a photo, a code, where to share), the page getting only what was
 * picked; 3, Tikpal's own services, called through the bridge as the signed-in user, for that 小程序's own data only.
 *
 * Levels: 1 is what the apps' first container offers (录音卡). 2 is the first version's (the sprint, the container doc
 * §3): the first three kinds as far as invited developers need them, with the authorization card (A7). A capability
 * whose level is null is planned and named here so C8 and the review can speak of it, but no manifest may ask for it
 * until a level offers it. Only ever add: a capability's kind, level and grant never change once offered. 3 is
 * 能力套装 3 (11-13, 导航 the first to use it): the device link, trips, the map module, home and work, storage.
 */

type MiniappCapabilityInfo = {
  kind: 1 | 2 | 3;
  /** The capability level the container offers it from; null: planned, offered by no container yet. */
  level: number | null;
  /** Asked of the user the first time the page uses it (the container's card), and revocable in the capsule's ···. */
  ask: boolean;
  /** A person approves it for the app at review (C8 「人工批」): recording, AI, anything that costs points. */
  review: boolean;
  /** Costs the user's points: written in the manifest, counted against the app's daily limit. */
  points: boolean;
  /** Only these 小程序 may name it (录音卡's recordings are the account's own). Absent: any. */
  only?: readonly string[];
};

/**
 * Whether the map module is open to every developer (review false) or asked for and reviewed: asked for, for now (why:
 * map.ts). Turning it true is the one change a capability's grant may take once offered: it only drops a person's
 * check, nothing that was allowed stops.
 */
export const MAP_OPEN_TO_ALL = false;

const offered = (kind: 1 | 2 | 3, level: number, rest: Partial<MiniappCapabilityInfo> = {}): MiniappCapabilityInfo => ({
  kind,
  level,
  ask: false,
  review: false,
  points: false,
  ...rest,
});
const planned = (kind: 1 | 2 | 3, rest: Partial<MiniappCapabilityInfo> = {}): MiniappCapabilityInfo => ({
  kind,
  level: null,
  ask: false,
  review: false,
  points: false,
  ...rest,
});

export const MINIAPP_CAPABILITY = {
  // Level 1: 录音卡's.
  /** 录音卡's recordings (recordings.ts), the account's own, so 录音卡 alone. */
  recordings: offered(3, 1, { only: ["recording-card"] }),
  /** Copying text (clipboard.write); reading the clipboard is not offered. */
  clipboard: offered(1, 1),
  /** The app plays a recording (audio.play): Ogg Opus, which WebViews may not. A page's own short sounds need nothing. */
  audio: offered(3, 1, { only: ["recording-card"] }),

  // Level 2: the first version.
  /** getUserMedia video; the page shows it, nothing is uploaded unless the page sends it through a capability. */
  camera: offered(1, 2, { ask: true }),
  /** getUserMedia audio: recording, so a person approves it too (§6.3). */
  microphone: offered(1, 2, { ask: true, review: true }),
  /** devicemotion and deviceorientation. */
  motion: offered(1, 2, { ask: true }),
  /** navigator.geolocation, while the page is open. */
  location: offered(1, 2, { ask: true }),
  /** The app's own scanner (scan.code): the text of one code the user points it at. */
  scan: offered(2, 2),
  /** The system's share sheet (share.text). */
  share: offered(2, 2),
  /** Photos the user picks (photos.pick), and saving an image the page made to the photo library (photos.save). */
  photos: offered(2, 2),
  /** The app's own records (动态): what the page keeps, of the kinds app.json's records declares, checked by their schema. */
  records: offered(3, 2),
  /** The app's settings (app.json's settings), the same the round screen's app reads. */
  settings: offered(3, 2),
  /** The user's id in this 小程序 (user.id): another in every 小程序, and never the account's (the container doc §1.1). */
  user: offered(3, 2),

  // Level 3 (能力套装 3, docs/delivery/2026-11-13-导航-开发文档.md §3; 导航 the first to use it, every developer may).
  // Open ones (review false) are declared and used; asked-for ones (review true) a person grants (C8), every version
  // using them is held for a person, and the user agrees the first time (ask). The 声波码's own action and value need no
  // capability: miniapps.ts env.launch and the voiceCode.scanned event.
  /**
   * Messages with the page's own app on the user's own Tikpal Voice, over Bluetooth (device-link.ts): the phone app
   * keeps the connection, the key and the encryption; the page sees no other device. The user agrees once (the card).
   */
  "device.link": offered(3, 3, { ask: true }),
  /**
   * The app follows a route for the page in the background, the position on while the screen is locked (trip.ts), and
   * asks for a new route itself when the rider leaves it: hence the routes path. The rider's position itself reaches
   * the page only with `location` too (trip.ts tripProgress.position).
   */
  trip: offered(3, 3, { ask: true, review: true }),
  /**
   * The map module (map.ts): routes, typing suggestions, place search, address ↔ point, a static map. Omitting the
   * start or `near` means 「我这里」: the app finds the position once, which needs `location` declared too (its card).
   * Asked for and reviewed for now (MAP_OPEN_TO_ALL; why: map.ts).
   */
  map: offered(3, 3, { review: !MAP_OPEN_TO_ALL }),
  /** The user's home and work: their names (places.saved), and a route to them as `{place}` (map.ts mapNeedsPlaces). */
  "places.saved": offered(3, 3, { ask: true, review: true }),
  /**
   * A little storage of the page's own on this phone (storage.*): kept per 小程序 and account, never sent anywhere,
   * dropped with the 小程序 or on signing out; at most MINIAPP.storage (miniapps.ts). For 「最近去过」 and the like.
   * Never in the phone's backups or moved to a new phone (it may hold places; 11-14): iOS keeps it in a file under
   * Application Support marked isExcludedFromBackup (not UserDefaults), Android in a file its backup rules leave out
   * (fullBackupContent and dataExtractionRules, cloud backup and device transfer alike). A new phone starts it empty.
   */
  storage: offered(3, 3),

  // Planned: named for C8 and the review, offered by a later level.
  /** Files the user picks from the phone. */
  "files.pick": planned(2),
  /** Storing files of the app's own. */
  files: planned(3),
  /** 问伙伴: a question to the user's buddy. */
  buddy: planned(3, { review: true, points: true }),
  /** An AI conversation. */
  ai: planned(3, { review: true, points: true }),
  /** Speech recognition and synthesis. */
  speech: planned(3, { review: true, points: true }),
  /** A message to the app on the user's round screen (the plan's 2.7). */
  device: planned(3, { review: true }),
} as const satisfies Record<string, MiniappCapabilityInfo>;

export type MiniappCapabilityName = keyof typeof MINIAPP_CAPABILITY;
const names = Object.keys(MINIAPP_CAPABILITY) as MiniappCapabilityName[];
/** Every capability named, planned ones included. */
export const miniappCapabilityNames = names;
/** What a manifest may ask for: the capabilities some level offers. */
export const offeredCapabilities = names.filter((n) => MINIAPP_CAPABILITY[n].level !== null);
/** Kind 1 capabilities the user is asked for: app.json says why for each it uses (permissions.phone). */
export const phoneAbilities = ["camera", "microphone", "motion", "location"] as const satisfies readonly MiniappCapabilityName[];
export type PhoneAbility = (typeof phoneAbilities)[number];

/** A path in a package (manifest entry and files): lowercase letters, digits, `.`, `_`, `-`, `/`; no `.`, `..` or empty segment. */
export const MINIAPP_PATH = /^(?!.*(?:^|\/)\.\.?(?:\/|$))(?!\/)(?!.*\/\/)(?!.*\/$)[a-z0-9._/-]+$/;
export const MINIAPP_PATH_LENGTH = 120;

/** The lowest capability level a set of capabilities needs (1 for none). */
export function capabilityLevelOf(capabilities: readonly string[]) {
  let level = 1;
  for (const name of capabilities) {
    const info = (MINIAPP_CAPABILITY as Record<string, MiniappCapabilityInfo>)[name];
    if (info?.level && info.level > level) level = info.level;
  }
  return level;
}
