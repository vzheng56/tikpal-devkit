# Mini apps: from nothing to a package

A mini app is a web page (HTML, CSS, JS) that runs inside the Tikpal app. It can only act through the "bridge" the app gives it: records, settings, scanning, sharing, copying… There is no other way out: no network, and no keys of any kind. People open it from the app's mini app sheet on their phone, or by scanning the code your app shows on Tikpal Voice.

This document goes from nothing to a package that passes the checks: start a project, preview it in a browser, write the page, build, check. 中文版：[miniapp.md](miniapp.md).

> **The first version is for invited developers only.** Build it in the browser, package it, pass the checks; then put it up as your own development version with `./kit miniapp preview` and open it on your phone (section 6).

## What you need

- Node.js 20 or newer (`node -v`).
- This kit. If you have an app repository made by `kit new`, the kit is its `devkit/`; if `devkit/miniapp/` is missing, `./kit update <version>` to a kit that has it.

## 1. Start a mini app

In your app repository (the folder with `app.json`):

```bash
./kit miniapp new my-timer     # goes into miniapp/; the slug is your Tikpal Voice app's
cd miniapp
npm install                    # slow in mainland China? add --registry=https://registry.npmmirror.com
```

In an app repository the slug must be `app.json`'s; another is refused. Outside an app repository: run `./kit miniapp new my-timer` in the kit's folder; it goes into `my-timer/` here, or wherever `--dir <folder>` says. A slug starts with a lowercase letter and has only lowercase letters, digits and `-`, 2–41 characters.

What you get:

| File | What it is |
|---|---|
| `manifest.json` | The mini app's manifest: slug, version, entry, name (four languages), capabilities (section 4.3) |
| `index.html` | The entry page. No Content Security Policy to write: the build writes the contract's in (section 4.1) |
| `src/main.ts` | The page's code; start here. `main.tsx` or `main.js` work too, and so do libraries such as React |
| `src/i18n.ts` | The page's words in four languages |
| `src/app.css` | The look: colours, insets and text size follow the app (section 3.4) |
| `public/` | Make it when you need it: files in it go into the package as they are (images, mp3 sounds) |
| `preview/fake.ts` | Make it when you need it: your own pretend answers for the preview (section 2) |
| `package.json`, `.npmrc` | The kit, installed as `@tikpal/miniapp` (`.npmrc` installs it as a copy) |

First fill in `name` in `manifest.json` in all four languages (`en`, `zh-Hans`, `ja`, `es-419`, at most 40 characters each).

In an app repository, the app's `app.json` also needs a `miniapp` part saying what `manifest.json` says (`kit miniapp new` prints it when it is done; copy it in):

```json
"miniapp": { "entry": "index.html", "capabilities": ["clipboard", "share"], "capabilityLevel": 2 }
```

When you change `capabilities` or `capabilityLevel` in `manifest.json`, change this part with it; `npm run build` checks that the two agree. To talk to your app on Tikpal Voice, `manifest.json` says `"minFirmware": "my-timer@0.1.3"` and this part `"minFirmware": "0.1.3"`.

## 2. Preview in a browser

```bash
npm run dev                    # open http://127.0.0.1:5180/
npm run dev -- --port 5199     # if 5180 is taken
```

The page on its own is at `http://127.0.0.1:5180/app/` (no panel; the pretend bridge with its defaults).

On the left a phone-sized frame with the app's capsule at the top right; on the right the panel:

| Panel | What it does |
|---|---|
| Language, look, colour, platform, text size | Sends `env.changed` as the app does; the page changes on the spot (section 3.1) |
| Capability level | The level the app offers (section 3.3): pick 1 to see what an older app does (level 2 methods answer `UNSUPPORTED`) |
| Width | 360, 375, 393, 430 |
| 断网 (offline) | Methods that need the server fail with `OFFLINE`; a recording playing stops as `failed` |
| 未登录 (signed out) | Methods that need the server fail with `SIGNED_OUT` |
| 慢网 (slow) | Every call waits 1.5 seconds, to see the loading state |
| 拒绝能力 (refuse a capability) | The app does not give that capability (the user took it back in the capsule's ···): its methods fail with `NOT_DECLARED`; camera, location and the like are simply not there |
| Call log | Every call: method, params, answer or error, how long; answers that break the contract are marked |
| Capsule | ✕ closes; `ctx.toast`, copy and share notes pop up near the bottom of the phone |
| 应用码 (app code) | With capability level 3 chosen: tick 从码打开, give the action (8–15) and the value, and the page opens again with them in `env.launch`; 又扫到 sends `voiceCode.scanned` (section 3.8) |
| Tikpal Voice, 行程 (trip), 地图模块 (map module), 家和公司 (home and work) | Only when the manifest declares `device.link`, `trip`, `map`, `places.saved`: whether Tikpal Voice is near, whether a trip can start (×10 to fast-forward, 走岔了 off the route, 到了 arrived), whether the map module answers (limit reached, Amap down, too far, no route), whether home and work are saved (sections 3.9–3.12) |

- Change the code, `index.html`, `manifest.json` or `public/` and the page reopens. If it does not build, the panel shows where in a red box.
- The preview's bridge is pretend: it checks every call against the contract (does the method exist, are the params right, is its capability declared), then answers with the contract's sample data. It reaches no server.
- The preview sends the page the same CSP and permissions policy the app does (section 4.1): what cannot get out in the preview cannot in the app either.
- The preview stays on this computer: it answers only `127.0.0.1` and `localhost`, and serves only the kit's own files, your code and `public/`.
- The insets (the capsule's row, the system's bar) are approximate; on a phone `env.insets` is what counts.
- Scanning and picking photos are the user's to do: in the preview the browser's dialogs stand in, and you play the user (cancel is the user closing it). Records and settings live in the page's memory and go when it reopens; the preview does not check kinds or setting names (the server checks them against app.json).

**Your own pretend data**: make `preview/fake.ts` with the answers you want, and a few switches for the panel:

```ts
import type { FakeHandlers, Switches } from "@tikpal/miniapp/preview";

// Two more choices on the panel, the first the default; changing one reopens the page.
export const switches: Switches = { records: ["some", "none"], saving: ["works", "schema refuses"] };

export const handlers: FakeHandlers = {
  "records.list": (params, ctx) => ({
    items: ctx.switches.records === "none" ? [] : [{ id: "r1", kind: "knock.session", at: "2026-11-02T08:30:00.000Z", data: { count: 108 } }],
    next: null,
  }),
  "records.add": (params, ctx) => {
    if (ctx.switches.saving !== "works") throw { code: "SERVER", status: 400, serverCode: "SCHEMA_INVALID" }; // to fail, throw
    return { id: "r2", at: new Date().toISOString() };
  },
};
```

`ctx` also has `ctx.emit("env.changed", …)` (send an event), `ctx.offline()` (is the panel offline), `ctx.toast("…")` and `ctx.env()`. Methods you leave out answer with the kit's samples. The preview checks params, capabilities and offline before your function runs, and checks what you answer against the contract too, marking a wrong answer in the call log.

## 3. Write the page

### 3.1 Start when the app has said where the page runs

```ts
import { watchEnv, type MiniappEnv } from "@tikpal/miniapp";

let env: MiniappEnv;
void watchEnv((next) => {        // every change after: language, look, colour, text size
  env = next;
  render();
}).then((first) => {             // the first time
  env = first;
  render();
});
```

`watchEnv` calls `env.get`, writes the colours, insets and text size as CSS variables (`applyEnv`), writes them again on every `env.changed`, then calls your function. **No top-level `await`**: Android's oldest WebView (Chrome 87) has none, and the build stops on it (section 4.5).

What `env` has:

| Field | What it is |
|---|---|
| `locale` | `en`, `zh-Hans`, `ja`, `es-419`: the app's language |
| `theme.scheme`, `theme.colors` | Light or dark, and the app's colours (section 3.4) |
| `fontScale` | The text size the user picked, 1 the standard |
| `platform` | `ios` or `android` |
| `capabilityLevel` | The app's capability level |
| `insets.top`, `insets.bottom` | CSS pixels to keep clear: the lower edge of the capsule's row, the system's bar |
| `launch` | From capability level 3: how the page was opened. From your app code (your own actions 8–15) it is `{ from: "voiceCode", action, value }`, otherwise `null`; older apps have no such field (section 3.8) |

### 3.2 Call the bridge

```ts
import { BridgeError, call, on } from "@tikpal/miniapp";

call("records.add", { kind: "knock.session", data: { count: 108 } }).then(
  (added) => showSaved(added.at),
  (error) => {
    if (error instanceof BridgeError && error.code === "OFFLINE") showOffline();
    else showFailed();
  },
);

const stop = on("env.changed", (env) => console.log(env.locale)); // what the app says unasked; watchEnv already listens to this one
```

- `call(method, params)` returns a promise; a method that takes nothing needs no params. Method names, params and answers are typed: your editor and `npm run typecheck` say when one is wrong.
- A failed call always throws `BridgeError`; look at `code`:

| `code` | When |
|---|---|
| `NOT_DECLARED` | The manifest's `capabilities` does not declare that group, or the app does not give it (section 4.3) |
| `UNSUPPORTED` | The app's capability level is below the method's (ask the user to update the app) |
| `BAD_PARAMS` | The params are wrong |
| `SIGNED_OUT` | The app is not signed in |
| `OFFLINE` | No network |
| `TIMEOUT` | No answer in 30 seconds |
| `SERVER` | The server said no: `status` is the HTTP status, `serverCode` its reason (such as `CREDITS_EXHAUSTED`) |
| `FAILED` | Anything else |

- Every call that can fail needs a line for the user. Network trouble is common: try offline and slow in the preview.

### 3.3 Capabilities and methods (capability level 2)

The capability kit is what the app gives a mini app, and it has levels (section 4.4). Level 2 is the first version: a new mini app says `"capabilityLevel": 2`.

**Bridge methods**:

| Method | Capability | Level | Params | What it does |
|---|---|---|---|---|
| `env.get` | every page | 1 | — | The `env` above |
| `app.close` | every page | 1 | — | Closes the mini app |
| `clipboard.write` | `clipboard` | 1 | `{ text }` | Copies |
| `user.id` | `user` | 2 | — | `{ id }`: the user's id in this mini app, different in every mini app, never the account itself |
| `records.list` | `records` | 2 | `{ kind?, cursor? }` | Your app's own records (the ones in the feed), up to 50 a page, newest first |
| `records.add` | `records` | 2 | `{ kind, data }` | Keeps one; `kind` and `data` as app.json's `records` says, else `SERVER` 400 `SCHEMA_INVALID` |
| `settings.get` | `settings` | 2 | — | `{ values }`: the app's settings, the same the app on Tikpal Voice reads |
| `settings.set` | `settings` | 2 | `{ values }` | Changes 1–20 of them and answers with them all; the names as app.json's `settings` |
| `scan.code` | `scan` | 2 | — | The app's scanner: `{ text }`, or `{ cancelled: true }` when the user closed it. Tikpal's own codes (pairing, sound codes…) never reach a page |
| `share.text` | `share` | 2 | `{ text }` | The system's share sheet: `{ shared }` |
| `photos.pick` | `photos` | 2 | `{ max? }` | Photos the user picks (up to 4): `{ photos: [{ type, width, height, data }] }`, JPEG in base64, the longer side at most 1600; `{ cancelled: true }` when none |
| `photos.save` | `photos` | 2 | `{ type, data }` | Saves an image the page made (PNG or JPEG base64) to the photo library: `{ saved }` |

`app.close` and `clipboard.write` answer an empty object `{}`. Event: `env.changed` (every page).

**The WebView's own**: not through the bridge but the web's standard APIs, still declared in `capabilities`; the app asks the user the first time (who can take it back in the capsule's ···), and one not declared is simply not there:

| Capability | API |
|---|---|
| `camera` | `navigator.mediaDevices.getUserMedia({ video })` |
| `microphone` | `getUserMedia({ audio })`: recording, so a person approves it at review |
| `motion` | `devicemotion`, `deviceorientation` |
| `location` | `navigator.geolocation`, while the page is open |

Using any of these four, write `permissions.phone` in the app's `app.json`, one line each on why (the app says it when it asks the user), such as `"permissions": { "phone": { "camera": "Take a photo of your wooden fish for the cover" } }`.

**Recording Card's only**: `recordings` and `audio` (the account's recordings made on Tikpal Voice, played by the app) are the Recording Card's; no other mini app may declare them.

**Capability level 3** (kit 1.11, Tikpal app 1.1.0 (31) or later): the action and value from an app code, and storage on the phone; the map module, home and work, the device link and the trip session in the background are applied for and not open yet. Sections 3.8–3.13.

**Planned, not yet**: picking files, storing files, asking the companion, AI conversation, speech recognition and synthesis, messages to your app on Tikpal Voice through the server. When a level offers one, the kit gets a new version and this table grows.

What each field means and what comes back on an error is the contract in the kit: `miniapp/src/contract/miniapps.ts` (the bridge) and `miniapp-capabilities.ts` (the capability kit).

### 3.4 Look like the app

`applyEnv` (which `watchEnv` calls for you) writes on the page's root:

| CSS | What it is |
|---|---|
| `--paper`, `--card`, `--line` | Background, cards, dividers |
| `--ink`, `--ink2`, `--ink3` | Text: main, secondary, faintest |
| `--accent`, `--accentStrong`, `--onAccent`, `--accentText` | The colour the user picked for the app: buttons, what is selected |
| `--sage`, `--sageStrong` | What goes on by itself, what is done |
| `--coral`, `--coralText`, `--danger` | Only for what waits for the user, and for errors |
| `--inset-top`, `--inset-bottom` | The insets to keep clear (px) |
| `--font-scale` | The text size: the user's on iOS; 1 on Android, whose WebView scales text itself |
| `color-scheme`, `lang`, `data-scheme` | Light or dark, language |

- **Draw from `--inset-top` down**: your title goes under the capsule's row, never beside it. Keep `--inset-bottom` clear at the bottom.
- Write sizes as `calc(15px * var(--font-scale, 1))`.
- **No blue.** Coral only where something waits for the user or went wrong.
- Write all four languages, and switch on `env.changed`.

### 3.5 Components: charts, the calendar, the ring

Drawn as the app draws a service's page (the same renderer), in the app's colours:

```ts
import { calendar, chart, ring } from "@tikpal/miniapp/components";

root.append(
  chart({ kind: "bar", title: "This week", points: [{ label: "Mon", value: 3 }, { label: "Tue", value: 5 }] }),
  ring({ title: "When", items: [{ label: "Morning", value: 38 }, { label: "Evening", value: 37 }], center: { value: "75", label: "times" } }),
  calendar({ start: "2026-10-01", values: [/* one a day, 7–371 */] }, { less: "Less", more: "More" }),
);
```

- `chart`: `kind` is `bar` or `line`, 2–31 points drawn in order. **The last point is drawn in coral**, as on the app's service pages, for "the latest": put today last ("the last 7 days", not "Monday to Sunday").
  - Stacked bars: give `series` (2–6, each may have a `tone`) and each point's `parts` in the series' order; `value` is still the total (older apps draw only that).
- `calendar`: one value a day from `start`, deeper for more; its `tone` at the top (`coral`, `sage`, `sun`, `ink`); the second argument is the legend's two words, in the page's language.
- `ring`: the shares of 1–8 items, each may have a `tone`; `center` is what the middle says.
- A `tone` is one of the app's colours: `coral`, `sage`, `sun`, `ink`, and Breathe's four scenes `wake`, `focus`, `sleep`, `mine`. Left out, they go in order.
- For HTML text (React's `dangerouslySetInnerHTML`, say) use `chartHtml`, `calendarHtml`, `ringHtml`. Every string is escaped.

### 3.6 Images, fonts, sounds

- Files in `public/` go into the package as they are; refer to them by relative path (`<img src="images/logo.png">`). Images you `import` in code are built into `app.js`.
- Sounds: short mp3, m4a or wav. **Not Ogg Opus** (iOS's WebView cannot play it). As phones do by default, the user taps the page once before it can make a sound.
- File names: lowercase letters, digits and `. _ - /` only.

### 3.7 Keeping things

Mini apps keep no browser storage in the app: `localStorage`, `IndexedDB` and cookies go when the mini app closes (on Android they may not work at all). What should stay goes through the bridge: records with `records`, settings with `settings` (section 3.3); small things kept on this phone only and never sent to the platform ("recent places") with `storage` (capability level 3, section 3.13).

The app does not use the `<title>` in `index.html` (it shows the manifest's `name`); `applyEnv` sets `<html lang>` to the app's language.

### 3.8 Opened from an app code (capability level 3)

Tikpal app 1.1.0 (31) or later. When your app shows a code of its own action (8–15) on Tikpal Voice and the user scans it with the app, the app opens your mini app and hands it the action and the value (how the firmware shows it, and how `voiceCode.own` in `app.json` declares it: [app codes](voice-code.md) section 4). No capability to declare: the code carries your own app number anyway.

```ts
void watchEnv(render).then((env) => {
  const code = env.launch;                       // { from: "voiceCode", action: 8, value: 0x790960 }, or null, or absent
  if (code?.action === 8) startFrom(code.value); // read the value as voiceCode.own says
  else showHome();                               // opened from the app page, or an older app: the ordinary way
});
on("voiceCode.scanned", ({ action, value }) => {  // your code scanned again while the mini app is open
  if (action === 8) startFrom(value);
});
```

- Only 8–15 are handed over. 0–7 are the app's own (0 the app page, 1 the service page, 2 a day; 3–7 only open the app page for now), and `launch` is `null`.
- The value is input from outside: read it as declared, and go the ordinary way when you cannot. What starts on a scan must be seen and stoppable (review rule M-09).
- Older apps (capability level 2) open the mini app as before, with no `launch`: the mini app must manage from the start.

### 3.9 Capability level 3: what there is, and how to get it

| Capability | What it does | Tier | The user |
|---|---|---|---|
| `map` | The map module: routes, typing suggestions, place search, address ↔ point, a static map (section 3.10) | Applied for, not open yet | No start or `near` means "my location": declare `location` too, and the user agrees the first time |
| `places.saved` | Home and work the user saved in the Tikpal app: their names only; a route says `{ place: "home" }` | Applied for, not open yet | Agrees the first time |
| `device.link` | The device link: messages with your extension on the user's own Tikpal Voice (section 3.11) | Applied for, not open yet | Agrees the first time |
| `trip` | The trip session in the background: the app follows the route with the phone locked and tells Tikpal Voice every second (section 3.12) | Applied for, not open yet | Agrees the first time, and the system's location ("while using" is enough, not "always") |
| `storage` | A little storage on this phone only (section 3.13) | Open | Not asked |

- **Open**: written in `manifest.json` and in `app.json`'s `miniapp.capabilities`, it works.
- **Applied for**: on the developer platform's Capabilities page for the app, "ask for a new capability": what it is for (shown to users), when it is used and how it stops. It is pending until a person approves it; once approved, every version that uses it is reviewed by the AI first as always, then held until a person has looked ([review rules](review-rules.en.md), "Capabilities you apply for"). Anyone may apply, and every developer is treated alike. A capability not approved is refused when the package is uploaded.
- **Not open yet**: the map module, home and work, the device link and the trip session in the background are not open for now; an application waits and is not approved. This page will say so when they open. What follows is how they work then, and you can write against the preview's pretend bridge meanwhile.
- `manifest.json` says `"capabilityLevel": 3`. The app's container is level 3 from Tikpal app 1.1.0 (31); apps 1.1.0 (30) and older are level 2 and cannot open a level 3 package: the platform hands them your release before it that their level reaches, and an app none of your releases fits does not list the mini app (section 4.4).

**The bridge's methods and events** (what they take and give is the contract in the kit: `miniapp/src/contract/miniapps.ts`, `map.ts`, `trip.ts`):

| Method | Capability | Params | Gives |
|---|---|---|---|
| `map.routes` | `map` | `{ to, mode, from?, alternatives?, maxPoints? }` | `{ routes, left }`: 1–3 routes |
| `map.tips` | `map` | `{ q, near?, city? }` | `{ items, left }`: suggestions while typing, at most 10 |
| `map.search` | `map` | `{ q, near?, city?, radius?, page? }` | `{ items, next, left }`: at most 20 a page |
| `map.geocode` | `map` | `{ address, city? }` | `{ items, left }`: an address to points, at most 5; none found is an empty list |
| `map.regeo` | `map` | `{ point? }` | `{ address, city, district, area, left }` |
| `map.static` | `map` | `{ fit }` or `{ center, zoom }`, with `width`, `height`, and `scale`, `markers`, `lines` if you like | `{ image, center, zoom, width, height, scale, left }`: `image` a PNG `data:` URI |
| `places.saved` | `places.saved` | — | `{ home, work }`: each `{ name, city }` or `null` |
| `device.link.open` | `device.link` | `{ mark? }` | `{ connected: true, name, mark }`, or `{ connected: false, reason }` |
| `device.link.send` | `device.link` | `{ data }` | One message: `data` is the bytes in base64 (`linkData(bytes)`) |
| `device.link.close` | `device.link` | — | Disconnects |
| `trip.start` | `trip` | `{ route, voice? }` | `{ started: true }`, or `{ started: false, reason }` |
| `trip.status` | `trip` | — | `{ state: "none" }`, or `{ route, progress }` |
| `trip.stop` | `trip` | — | Ends it |
| `storage.get`, `storage.set`, `storage.remove`, `storage.keys` | `storage` | `{ key }`, `{ key, value }` | `{ value }`, `{}`, `{}`, `{ keys }` |

Events: `voiceCode.scanned` (nothing to declare), `device.link.state`, `device.link.message`, `trip.progress`, `trip.rerouted`, `trip.ended`.

### 3.10 The map module (`map`, applied for, not open yet)

The platform calls Amap for you and keeps the key: neither you nor your mini app ever touch it. Every point is GCJ-02, Amap's datum, written `{ lng, lat }`.

- **"My location"**: `map.routes` without `from`, `map.tips` and `map.search` without `near`, `map.regeo` without `point` mean "my location": the app finds the position once and puts it in; the page never sees it. Declare `location` too (with why in `app.json`'s `permissions.phone.location`), and the user agrees the first time; without it, or if the user says no, `NOT_DECLARED`. With a point given there is nothing to declare.
- **Home and work**: `to: { place: "home" }` or `{ place: "work" }`, with `places.saved`. Their points never leave the platform; not saved is `SERVER` 409 `PLACE_NOT_SET`.
- **Ways to go**: `bike`, `ebike`, `walk`. **No driving, and never in a vehicle** (Amap's terms 3.4).
- **A route**: `line` is its points from start to end (`[lng, lat]`); `steps` each step (the action, metres, the road, Amap's own sentence, and `at`, its point's index in `line`; the last is `arrive`); `via` the two roads it mostly takes; `turns` how many turns.
- **A static map**: give `fit` (points it must hold: the platform picks centre and zoom), or `center` and `zoom`. The image comes with its `center`, `zoom`, width and height; `mapProject(view, point)` says where a point falls on it, so you draw "me" and the route yourself, and ask for another image when "me" leaves this one. Amap can draw lines and markers too (`lines`, `markers`), in `coral`, `sage`, `ink` or `gray` only.
- **Limits**: a Beijing day, for each user and for each app, in four kinds: routes, search (suggestions and place search together), address ↔ point, static maps. Today each user has 60 routes, 200 searches, 100 address look-ups and 100 static maps a day by default; go by `left` in each answer (what this user has left of that kind today). Over it is `SERVER` 429 `MAP_LIMIT`. No credits are spent.
- **Refusals** (by `serverCode`): `MAP_LIMIT` the day's limit; `MAP_UNAVAILABLE` Amap cannot answer now; `TOO_FAR` start and end too far apart; `NO_ROUTE` no way there this way; `OUTSIDE_MAINLAND` mainland China only; `PLACE_NOT_SET` home or work not saved. Each wants a sentence for the user.
- **Amap's rules** ([AMap Open Platform terms](https://lbs.amap.com/pages/terms/), review rule X-02):
  - where a map, a route or places are shown, name the source "高德地图" (AMap); never crop, cover or replace AMap's mark or the map approval number on a static map;
  - call it when the user wants it, never in bulk to gather data; drop results after use, never keeping them as a place database or map of your own (one or two places the user picked may stay on the phone);
  - a route is a suggestion: never say it is always right.

A small example: opened from an app code, it works out the route home, draws "me" on the static map and follows the trip once started (`manifest.json` declares `map`, `location`, `places.saved`, `trip`, `device.link`, and `capabilityLevel` 3):

```ts
import { BridgeError, call, mapProject, on, watchEnv, type MapRoute, type MapView } from "@tikpal/miniapp";

const status = document.querySelector("#status")!;
const me = document.querySelector<HTMLElement>("#me")!;   // a small dot over the map
let view: MapView | null = null;
let route: MapRoute | null = null;

const place = (p: { lng: number; lat: number }) => {
  if (!view) return;
  const { x, y } = mapProject(view, p);
  me.style.transform = `translate(${x}px, ${y}px)`;
};

async function home() {
  const { routes } = await call("map.routes", { to: { place: "home" }, mode: "bike" });   // no from: my location
  route = routes[0];
  status.textContent = `${(route.meters / 1000).toFixed(1)} km · ${Math.ceil(route.seconds / 60)} min`;
  const ends = [route.line[0], route.line[route.line.length - 1]].map(([lng, lat]) => ({ lng, lat }));
  const map = await call("map.static", { fit: ends, width: 360, height: 240 });
  document.querySelector<HTMLImageElement>("#map")!.src = map.image;                   // "高德地图" under it
  view = map;
  place(ends[0]);
}

on("trip.progress", (p) => {
  if (p.position) place(p.position);
  status.textContent = `${p.remainingM} m to go, ${p.toActionM} m to the next turn`;
});
on("trip.ended", (e) => (status.textContent = e.reason === "arrived" ? "You're there" : "Ended"));

void watchEnv().then((env) => {
  if (env.launch?.action === 8) {                       // app code action 8: the value's top 20 bits are the device's mark
    void call("device.link.open", { mark: env.launch.value >>> 4 });
    home().catch((e) => (status.textContent = e instanceof BridgeError && e.serverCode === "MAP_LIMIT" ? "No more routes today" : "No route"));
  }
});
document.querySelector("#go")!.addEventListener("click", () => route && call("trip.start", { route }));
```

### 3.11 The device link (`device.link`, applied for, not open yet)

The mini app sends and receives messages with **your app's** extension on **the user's own** Tikpal Voice, through the app, over Bluetooth, with no network. The connection, the encryption and reconnecting are the app's; the page sees no other Bluetooth device. On Tikpal Voice it is the kit's unpaired Bluetooth port (`bluetooth_unpaired` in `app.json`'s `permissions.device`, applied for: [app guide](app-guide.md)).

- `device.link.open({ mark })`: `mark` is the device's mark, usually from the app code's value (where `voiceCode.own` says it is), to pick the right one among those near; without it, the account's device that has your app open. Not connected says why in `reason`: `no_device` none near, `several` more than one and no mark (ask the user to scan), `no_key` offline the first time, `key_mismatch` Tikpal Voice holds another key, and the app has already fetched the keys again once ("let Tikpal Voice go online, then try again"), `bluetooth_off`, `denied` the user said no, `busy` another phone holds it.
- `device.link.send({ data })`: at most 86 bytes a message and 10 messages a second; not connected is `FAILED`. `linkData(bytes)` and `linkBytes(data)` turn bytes into base64 and back. What a message means is your mini app's and your extension's.
- Event `device.link.state`: `connecting`, `connected`, `lost` (out of reach: the app reconnects by itself; also when Tikpal Voice's key changed while connected and fetching again did not help, and then it stays lost until the page calls `device.link.open` again), `closed`. `device.link.message`: one from Tikpal Voice.
- The mark is no secret (anyone who sees the screen can scan it): it only picks which device; never use it as a key.
- Connect while the user is using it, and `device.link.close()` when done (review rule M-10).

### 3.12 The trip session in the background (`trip`, applied for, not open yet)

A web page stops running once the phone is locked, so following the route is the app's: the mini app hands the route the user chose to the app, which keeps taking the position in the background, works out how far the next turn is, asks for a new route itself when the user leaves it, and tells Tikpal Voice every second when it is connected. While the mini app is in front it gets the progress once a second.

- `trip.start({ route, voice })`: `route` is one of `map.routes`'; `voice: true` has the phone say the turns too (off by default). Not started says why: `busy` another mini app's trip is on, `denied` the user said no, `background_denied` the system will not let the app keep the location once the phone is locked (Android does not allow the location foreground service; iOS never answers it), so it would stop at the lock screen: tell the user, and do not start.
- Event `trip.progress` (once a second while in front): `state` (`navigating`, `rerouting`, `arrived`), `step` (the next step's index in `route.steps`), `toActionM`, `remainingM`, `remainingS`, `progress` (0–1000), `position` (where "me" is on the route, for your map), `heading`, `device` (whether Tikpal Voice is connected). `trip.rerouted`: off the route, a new route. `trip.ended`: over (`arrived`, `stopped`, or `failed` when the position was gone for good), with its time and distance.
- Back in front, read `trip.status()` once: it may have arrived or rerouted while locked.
- One trip at a time; closing the mini app does not end it; arriving, `trip.stop()`, the notification's "end navigation" or the app swiped away do, and the position stops at once.
- Start only once the user clearly started it (tapped "start", or the countdown after a scan ran out without a cancel), and stop on arriving (review rule M-11).
- `app.json`'s `permissions.phone.location` says why it needs the position all the time, matching what you wrote when applying.

### 3.13 Storage on the phone (`storage`)

Tikpal app 1.1.0 (31) or later. A little storage on this phone, your mini app's alone: never sent to the platform, gone on signing out or when the mini app is removed. For "recent places", "last choice" and the like.

- `storage.set({ key, value })`, `storage.get({ key })` (none is `{ value: null }`), `storage.remove({ key })`, `storage.keys()`.
- `key` is letters, digits and `. _ -`, at most 64; `value` is a string of at most 4096 characters (`JSON.stringify` an object yourself). At most 100 keys and 65536 characters in all; past that, `BAD_PARAMS`.
- What should follow the account to another phone goes in `records` or `settings`.

## 4. The rules

The build checks all of them (section 5): break one and there is no zip, only `dist/` to look at. The check goes by how the code is written, so a way around the wording slips past it, but not past the app's CSP: on a phone it is simply broken.

### 4.1 No outside addresses

- A mini app never reaches the network itself: no `fetch`, `XMLHttpRequest`, `WebSocket`, `EventSource` or `sendBeacon`. Everything goes through the bridge.
- Nothing loaded from elsewhere: no `<script src="https://…">`, no outside images, fonts or stylesheets. Only the package's own files, by relative path.
- No `eval` or `new Function`, no `<script>` written into the page and no `onclick="…"`: the app does not let them run. Put code in `src/` and use `addEventListener`.
- The app stops outside links, navigation, new windows and downloads.
- The Content Security Policy is the contract's, this one; the build writes it into `index.html` (one you wrote is replaced by it), and the app sends the page the same:

  ```
  default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; font-src 'self'; media-src 'self'; connect-src 'none'; frame-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'
  ```

### 4.2 No API keys

The app calls our services for the mini app with the user's own sign-in; the mini app never gets the sign-in and needs no key of its own. The check stops a package that looks like it holds an API key, a token or a private key.

### 4.3 Declare capabilities

- Name each group of methods and each of the WebView's own capabilities you use in `manifest.json` `capabilities` (`env` and `app` every mini app has), and in app.json's `miniapp` part with it:

  ```json
  "capabilities": ["clipboard", "share"]
  ```

- Calling one you did not declare fails with `NOT_DECLARED`, with a line in the console (a built page refuses it at once without asking the app; in the preview the pretend bridge answers, and it shows in the call log); the build check also says which method needs which capability, and speaks up when the code uses `getUserMedia`, `devicemotion` or `geolocation` without the capability. The app and the server each stop it again.
- Declare only what you use. Review asks why each capability is needed.

### 4.4 Capability level

- The capability kit goes up a level when it gets new capabilities; they are only ever added, never changed. Level 1 is the Recording Card's; level 2 is the first version (section 3.3); level 3 came with kit 1.11 (sections 3.8–3.13).
- `capabilityLevel` in `manifest.json` is the lowest level you need, and never below what your capabilities need (the contract's `capabilityLevelOf`: lower and the manifest breaks the contract). A new mini app says 2.
- The check says when you use a method from a higher level. An app below the level you need cannot open the package and does not ask to be updated: the platform hands it your release before that its level reaches, and with none it does not list the mini app (the next point).
- The apps' mini app container: level 3 from 1.1.0 (31), level 2 in 1.1.0 (30) and older (30 and 31 are both called 1.1.0; the number in brackets is the build), and level 1 (the Recording Card's) in older apps not updated yet. An app says its level when it downloads, and the platform hands it the newest release it can open: a level 3 package only to apps from 1.1.0 (31), the older ones your release before it that their level reaches. When you need a level above the apps', the build notes it, not as an error.

### 4.5 Old phones

- Android's system WebView is taken to be as old as Chrome 87, iOS's as Safari 16, and the kit builds for them: **no top-level `await`** (the build stops on it).
- Nor the APIs 87 lacks: `structuredClone`, `Object.hasOwn`, `.at()`, `findLast`, `toSorted`, `crypto.randomUUID`, `Object.groupBy`, `Promise.withResolvers` (and `toReversed`, `toSpliced`). The check looks through the built `app.js`, so one a library brings in counts too.

### 4.6 The package

- At most 200 files; the zip at most 5 MB, unpacked at most 20 MB, one file at most 10 MB.
- Paths of lowercase letters, digits and `. _ - /`, at most 120 characters; no symbolic links.

## 5. Build

```bash
npm run build
```

It makes two things:

- `dist/`: the package. `manifest.json`, `index.html`, `app.js`, `app.css`, and the files from `public/`.
- `my-timer-0.1.0.zip`: the zip for the platform, holding `dist/`'s files.

Then it checks the package against the contract (section 4), lists any problems, and exits with a non-zero code if there are, **with no zip** (an older zip of this version goes too, so the wrong one is never sent). The build writes `dist/manifest.json`: your manifest plus `files`, every file's SHA-256.

| Command | What it does |
|---|---|
| `npm run build` | Build and check. `-- --no-zip` for no zip, `-- --zip <file>` for another name |
| `npm run check` | Check `dist/` only; `npm run check -- my-timer-0.1.0.zip` checks the zip |
| `npm run verify` | Build again elsewhere and compare with `dist/` byte for byte. The same sources build the same on any computer; that is how review knows |
| `npm run typecheck` | Type checking |

- **A new version**: change `version` in `manifest.json` (three parts, such as 0.1.1), and `version` in `package.json` with it.
- **The platform signs.** The kit never signs and you need no key: once a package is uploaded and reviewed, Tikpal signs it and sends it out.

## 6. Hand it to the platform

First, in the browser: try all four languages, light and dark, a narrow screen, offline and a refused capability in the preview; then `npm run build` passes the checks.

### 6.1 Open it on your own phone (development version)

In the app's folder:

```
./kit miniapp preview
```

(In the mini app's folder, `npx tikpal-miniapp preview` does the same.) It builds, checks, and puts the package up on the developer platform together with your app's `app.json` as your **development version**: only your own account gets it, and the Tikpal App on your phone opens this version of the mini app. No review, no points, and nobody else sees it.

`app.json` needs a `miniapp` part whose `entry`, `capabilities` and `capabilityLevel` are the same as the mini app's `manifest.json`, and the same `slug` (see `docs/app-json.md`). The platform checks the package against it; what the mini app may use is still what the developer site's Capabilities approved, and listing more in `app.json` changes nothing. The reasons you give in `permissions.phone` for the phone abilities are what the phone shows when it asks the user. If `app.json` is not in the app's folder, point to it with `--app <path>`.

The first time, it signs you in: the terminal shows a code and an address (developer.tikpal.ai/cli). Open it in the browser, sign in with your developer account, check the code and press Allow; the terminal carries on by itself. You can also sign in on its own with `./kit login` (the same sign-in as `./kit miniapp login`, which `./kit submit` and `./kit status` use too). Signing in lasts 30 days; then do it again. `./kit logout` signs out. That page of the developer site lists the computers signed in: sign out any you don't know.

- When the platform refuses the package, it says what to change (in Chinese for now) and lists what is wrong; fix it and put it up again.
- While a version is on development, you can put the same version up again and the new one replaces it; a version already out to your test accounts or on stable can't be used again: raise the version.

### 6.2 For others

To give it to test accounts or to everyone, submit it for review: `./kit submit --miniapp-only --notes "what this version does"` makes the package, uploads and submits it (signed out, it only makes the package: upload it on the app's page of the developer site); `./kit status` follows it.

## 7. When something goes wrong

| You see | Do |
|---|---|
| A blank page in the preview | Look for a red box on the panel (it does not build), then the browser's console |
| `NOT_DECLARED` | Add the capability to `capabilities` in `manifest.json`; or untick 拒绝能力 on the panel. The map module without `from` or `near` means "my location", which needs `location` |
| `SERVER` 429 `MAP_LIMIT` | This user's limit of that kind is used up for today: say so; and do not ask for suggestions on every key, wait until typing pauses for 0.3 s |
| Note: `"trip" 要在开发者平台「能力和权限」页申请` | Not an error: a capability you apply for works once approved, and a person sees every version (section 3.9) |
| `Top-level await is not available` | Write it as `watchEnv(...).then(...)` (section 3.1) |
| `用了 .at()：最老的安卓 WebView（Chrome 87）没有` | Write it another way (`a[a.length - 1]`); if a library brought it in, change its version or the library |
| `CSP：… 要写 …` | The package was not built by this kit: build it with `npm run build`, which writes in section 4.1's policy |
| 提醒：`capabilityLevel 是 4：现在的 App 只到第 3 级` (a note) | Not an error: the apps have the level 3 container; a package that needs a higher level opens once it goes up again. Work in the preview meanwhile |
| `capabilityLevel is below what a capability needs` | You declared a level 2 capability: say `capabilityLevel` 2 |
| `app.json 要有 miniapp 一段` and the like | Change the app's `app.json` as it says, to agree with `manifest.json` (section 1) |
| `引用的 … 不在包里` | Use relative paths; the built code is `app.js` and the styles `app.css` |
| `端口 5180 被占了` (port taken) | `npm run dev -- --port 5199` |
| `… ：文件名和路径只能用小写字母…` (file name) | Rename that file in `public/` to lowercase, no spaces |
| `这里的 app.json 是 …` (slug) | A mini app's slug is its app's: start again with the slug it names |
| `npm install` hangs | In mainland China add `--registry=https://registry.npmmirror.com` |
| npm says esbuild's install script did not run (`allowScripts`) | Leave it, the build works; to run it anyway, `npm install-scripts approve esbuild` as it says |
| You want React | `npm install react react-dom`, rename the entry to `src/main.tsx`, write as usual |

The kit's messages are in Chinese; the table above has the common ones. Tikpal's own Recording Card mini app is written, previewed and built with this kit, just as described here.

## Changes

- **0.1.0** (2026-11-02): first version. The bridge's API and types (capability level 2), the template, charts, calendar and ring, the browser preview, building and checking.
- **Kit 1.11** (2026-11): capability level 3, Tikpal app 1.1.0 (31) or later: opened from an app code (`env.launch`, `voiceCode.scanned`), storage on the phone; the map module, home and work, the device link and the trip session in the background are applied for and not open yet. The preview's panel and pretend bridge with them; `mapProject`, `linkData`, `linkBytes`.
