# Mini apps: from nothing to a package

A mini app is a web page (HTML, CSS, JS) that runs inside the Tikpal app. It can only act through the "bridge" the app gives it: records, settings, scanning, sharing, copying… There is no other way out: no network, and no keys of any kind. People open it from the app's mini app sheet on their phone, or by scanning the code your app shows on the round screen.

This document goes from nothing to a package that passes the checks: start a project, preview it in a browser, write the page, build, check. 中文版：[miniapp.md](miniapp.md).

> **The first version is for invited developers only.** Build it in the browser, package it, pass the checks; then put it up as your own development version with `./kit miniapp preview` and open it on your phone (section 6).

## What you need

- Node.js 20 or newer (`node -v`).
- This kit. If you have an app repository made by `kit new`, the kit is its `devkit/`; if `devkit/miniapp/` is missing, `./kit update <version>` to a kit that has it.

## 1. Start a mini app

In your app repository (the folder with `app.json`):

```bash
./kit miniapp new my-timer     # goes into miniapp/; the slug is your round screen app's
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

When you change `capabilities` or `capabilityLevel` in `manifest.json`, change this part with it; `npm run build` checks that the two agree. To talk to your app on the round screen, `manifest.json` says `"minFirmware": "my-timer@0.1.3"` and this part `"minFirmware": "0.1.3"`.

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
| `settings.get` | `settings` | 2 | — | `{ values }`: the app's settings, the same the round screen's app reads |
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

**Recording Card's only**: `recordings` and `audio` (the account's round screen recordings, played by the app) are the Recording Card's; no other mini app may declare them.

**Planned, not in the first version**: picking files, storing files, asking the companion, AI conversation, speech recognition and synthesis, messages to your app on the round screen. When a level offers one, the kit gets a new version and this table grows.

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

Mini apps keep no browser storage in the app: `localStorage`, `IndexedDB` and cookies go when the mini app closes (on Android they may not work at all). What should stay goes through the bridge: records with `records`, settings with `settings` (section 3.3).

The app does not use the `<title>` in `index.html` (it shows the manifest's `name`); `applyEnv` sets `<html lang>` to the app's language.

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

- The capability kit goes up a level when it gets new capabilities; they are only ever added, never changed. Level 1 is the Recording Card's; level 2 is the first version (section 3.3).
- `capabilityLevel` in `manifest.json` is the lowest level you need, and never below what your capabilities need (the contract's `capabilityLevelOf`: lower and the manifest breaks the contract). A new mini app says 2.
- The check says when you use a method from a higher level; an app below the level you need cannot open the package, and asks its user to update.
- The apps' mini app container is level 2 from the first version on; older apps not updated yet stay at level 1 (the Recording Card's), and the platform hands them only what they can open. When you need a level above the apps', the build notes it, not as an error.

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

The first time, it signs you in: the terminal shows a code and an address (developer.tikpal.ai/cli). Open it in the browser, sign in with your developer account, check the code and press Allow; the terminal carries on by itself. You can also sign in on its own with `./kit miniapp login`. Signing in lasts 30 days; then do it again. `./kit miniapp logout` signs out. That page of the developer site lists the computers signed in: sign out any you don't know.

- When the platform refuses the package, it says what to change (in Chinese for now) and lists what is wrong; fix it and put it up again.
- While a version is on development, you can put the same version up again and the new one replaces it; a version already out on preview or stable can't be used again: raise the version.

### 6.2 For others

To give it to test accounts or to everyone, submit it for review: `./kit submit` makes the package; upload it under the app's Versions on the developer site.

## 7. When something goes wrong

| You see | Do |
|---|---|
| A blank page in the preview | Look for a red box on the panel (it does not build), then the browser's console |
| `NOT_DECLARED` | Add the capability to `capabilities` in `manifest.json`; or untick 拒绝能力 on the panel |
| `Top-level await is not available` | Write it as `watchEnv(...).then(...)` (section 3.1) |
| `用了 .at()：最老的安卓 WebView（Chrome 87）没有` | Write it another way (`a[a.length - 1]`); if a library brought it in, change its version or the library |
| `CSP：… 要写 …` | The package was not built by this kit: build it with `npm run build`, which writes in section 4.1's policy |
| 提醒：`capabilityLevel 是 3：现在的 App 只到第 2 级` (a note) | Not an error: the apps have the level 2 container; a package that needs a higher level opens once it goes up again. Work in the preview meanwhile |
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
