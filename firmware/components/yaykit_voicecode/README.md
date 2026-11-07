# 声波码 (yaykit_voicecode)

Tikpal's own round code: shown on the round screen, read by the Tikpal app. Requirement:
`docs/delivery/2026-10-30-voice-code.md`. **The contract is `packages/contracts/src/voice-code.ts`**: this component
follows it bit for bit, and `test/run.sh` checks every example in `packages/contracts/fixtures/voice-code.json`. When
the two disagree, the contract is right.

The code itself is plain C99 + libm, no ESP-IDF, no hardware: it owns no resources. The round screen's part (11-01,
handoff `docs/handoff/2026-11-01-voice-code.md` 2.1①) draws the code with LVGL and lets an app show its own 应用码
(`yk_vc_show()`); since 0.1.14 every firmware with `yaykit_app` builds it (`yaykit_app` requires it,
`firmware/reproducible.cmake` adds the directory; an app that calls `yk_vc_show()` still lists it in `main`'s
`REQUIRES`). The app ring's 「在手机上打开」 (2.1②) is `yk_vc_show_ring()`, which yaykit_app links weakly (below); the
pairing page's code (2.1③) is `yk_vc_pair_layer()`, which yaykit_ui's `yk_ui_pair_content()` links weakly (below).

| File | What | Who links it |
|---|---|---|
| `include/yk_voicecode.h` | the API | everyone |
| `yk_voicecode.c` | layout, payloads, Reed-Solomon encoder | firmware, apps, tests |
| `yk_voicecode_decode.c` | Reed-Solomon decoder with erasures, `yk_vc_decode_levels` | firmware (built, kept only if called), apps, tests |
| `yk_voicecode_scan.c` | reading a code from a camera frame, `yk_vc_scan` | apps, tests (megabytes per frame: not the firmware) |
| `include/yk_vc_view.h`, `yk_vc_view.c` | the code on the round screen: drawn, talking, `yk_vc_show()` (LVGL, `yaykit_ui`, `yaykit_board`) | firmware |
| `yk_vc_view_logic.c` | its pure parts: pixels, the 24 boxes redrawn, the talking, the app's number | firmware, tests |
| `voicecode_number.cmake` | the app's number from `app.json` into the image | the build, tests |
| `test/` | host tests: the contract's examples, round trips, damaged codes, rendered frames | `bash test/run.sh` |

## Using it

On the round screen: build a payload, encode it, draw the 48 bars.

```c
#include "yk_voicecode.h"

yk_vc_payload_t p;
uint8_t levels[YK_VC_BARS];
// pairing: the Bluetooth name's last 4 hex digits and the 8-digit pairing code, as text
if (yk_vc_pair("dd76", "06418273", &p) == 0 && yk_vc_encode(&p, levels) == 0) {
    for (int k = 0; k < YK_VC_BARS; k++) {
        float angle = yk_vc_bar_angle(k);               // clockwise from 12 o'clock, screen coordinates
        float length = yk_vc_bar_length(levels[k]);     // in units of the ring's radius, centred on YK_VC_BASE_R
        // draw it; level 3 may be coral, colour carries nothing
    }
}
```

- **自拍屏's hotspot**: `yk_vc_selfie(suffix, password, &p)`; the hotspot must be named
  `YK_VC_SELFIE_SSID_PREFIX` + the suffix (`yk_vc_selfie_hotspot()` writes both from a payload).
- **应用码**: `{.kind = YK_VC_APP, .app = <the app's number>, .action = YK_VC_ACTION_DAY, .value = <days since 1970>}`.
  The app's number comes from the caller: this component does not read `app.json` (where an app's number will be
  registered is the `voiceCode` contract, not decided yet).
- `yk_vc_encode()` refuses a payload out of range (an app number of 0, a value over 24 bits for an app code, more than
  8 digits ...). `yk_vc_pair()` / `yk_vc_selfie()` take exactly 4 lower-case hex digits and exactly 8 digits.
- Logs: `yk_vc_payload_text()` leaves the 8 digits out (a pairing code or a password).

### An app shows its own code

```c
#include "yk_vc_view.h"

// the app page (action 0); hides itself after 60 s, on a tap, or with yk_vc_hide()
yk_vc_show(YK_VC_ACTION_PAGE, 0, NULL);
// its records of a day, for 5 minutes, told when it is gone
yk_vc_show(YK_VC_ACTION_DAY, days_since_1970, &(yk_vc_show_opts_t){.hide_after_s = 300, .on_hidden = gone});
// in the yk_app_on_key() handler: a tap of the side key hides a shown code
if (yk_vc_on_key(ev)) return true;
```

- **The number is never passed**: the build reads `voiceCode.number` from the project's `app.json`
  (`voicecode_number.cmake`) and builds it in; `yk_vc_app_number()` says which. No `voiceCode`: `yk_vc_show()` returns
  `ESP_ERR_NOT_SUPPORTED`. A test number (65280..65535) goes in `app.json` or on the build's command line
  (`-D YK_VC_APP_NUMBER=65280`, which wins); a broken `app.json` or a number out of range stops the build. Beside the
  build, `yk_voice_code.txt` says what was built in; `pack-firmware.ts` holds it against `app.json`.
- **What shows**: pure black over the app's screen (the app ring, opened later, comes above it), the code centred at
  R = 184 px (`YK_VC_VIEW_R`), the Tikpal mark (`yk_ui_mark()`) 0.40R across in the middle. The bars talk
  `YK_VC_ANIM_MS` (1.2 s) and settle; the app reads the code only then, and settled the bars never change. While they
  talk a timer redraws only the 24 boxes of the pairs that moved, never the whole screen; settled, nothing
  redraws. A frame at most every 33 ms (`YK_VC_FRAME_MS`), and the next only once the last is on the screen and
  LVGL has slept `YK_VC_YIELD_MS` (15 ms, `yk_vc_next_frame_ms()`): a frame takes about as long to draw (the first,
  over the whole screen, ~110 ms), and frame after frame the LVGL task kept its lock through the talking, so the
  firmware's own screen updates timed out (11-03). About 17 frames a talking now; the lengths follow the time, so the
  talking and the code it settles on are as before. The settled logs say how many: `YK_VC settled levels=… frames=…`,
  `YK_VC pair settled frames=…`. Ink and coral from the theme (`yk_round.h`); colour carries nothing.
- **Hiding**: after 60 s, on a tap on it, or on a swipe on it any way (`YK_VC_HIDDEN_SWIPE`; the swipe never reaches
  the app's screen and its release goes nowhere); `keep_on_tap` keeps it through both.
- **The screen stays on only if the app keeps it on**: this component never touches `yk_app_keep_screen_on()`. An app
  whose screen may go off first keeps it on while `yk_vc_showing()`.
- **The app ring's 「在手机上打开」** (handoff 2.1②): `yk_vc_show_ring(hidden)` shows this app's page code (action 0,
  value 0) for yaykit_app when its own place on the ring is tapped, and calls `hidden(why)` however it goes (the app's
  own code taking its place too). yaykit_app links it, `yk_vc_hide()` and `yk_vc_app_number()` weakly, and this
  component's CMakeLists keeps them in every image that lists it (`-u`): an app with `voiceCode` gets the phone mark on
  its place just by being rebuilt, and a firmware without this component builds as before (no mark, a tap on its place
  does nothing). While that code is up yaykit_app takes the side key (a press hides it) and keeps the screen on with a
  switch of its own (`YAY_APP screen keep_on_code=`), never the app's.
- Takes `board_lock()` itself (it is recursive): any task may call it. Logs: `YK_VC show <payload>`,
  `YK_VC settled levels=<48>` (the levels only for app and test codes),
  `YK_VC hidden why=<app|timeout|tap|key|replaced|swipe>`.
- On a page of the firmware's own: `yk_vc_view_create(parent, cx, cy, r)` and `yk_vc_view_set(view, &payload, true)`;
  `yk_vc_view_set_middle(view, "0641 8273")` puts one line of 28 px text in the mark's place, inside the logo's circle
  (0.40R: the code carries nothing there and the reader looks past it), NULL the mark again.
- **The pairing page** (handoff 2.1③): `yk_vc_pair_layer(screen, suffix, digits)` covers the screen in pure black with
  the pairing code (`yk_vc_pair`) at R = 184, talking then settling, and the 8 digits in its middle as "0641 8273" for
  an app that types them; the caller binds a tap. yaykit_ui's `yk_ui_pair_content()` (the launcher's 「连接手机」) links
  it weakly, kept by `-u` like the ring's: a firmware that lists this component shows the voice code (a tap turns
  Bluetooth off, a phone on the link hides it), one that does not keeps the QR code. Yay's three code pages
  (`yay_pages.c`: the pairing page, Settings › Wi-Fi, 换个网络) put it over the page too: a tap turns the phone link off
  and the page stays (`YAY_BTN_PAIR_CODE`), the rim stays outside the code. As large as the app code because
  the phones read 1280x720 frames with no zoom: at R = 184 the ring's radius is about 120 camera pixels at 13 cm and 80
  at 20 cm (estimated for a 60-degree view; the reader's moderate and small conditions); smaller, the phone has to come
  closer. Logs
  `YK_VC show pair YK-dd76`, `YK_VC pair settled`: never the digits.
- Memory: the widget's state (about 1 KB) is in PSRAM, made when shown and freed when hidden; no task, no internal
  buffers.

In the app: `yk_vc_scan(luma, width, height, stride, &result)` on each camera frame (thread-safe, a few ms at
1280x720). `YK_VC_OK`: `result.payload`. `YK_VC_LATER`: a code of a later version, ask for the app to be updated.
Anything else: not read in this frame; to pick the hint for a run of frames, keep the status that got furthest by
`yk_vc_status_stage()` (the status numbers are fixed, not in the reader's order: `no_dot` comes after `bad_levels`).
For an app code, `yk_vc_action_does(action)` says what to open ("page", "service", "day", "ask", "own").

## Layout

Light marks on a black round screen. All sizes are in units of the ring's radius (1.0).

| Part | Where | Size |
|---|---|---|
| Ring | circle of radius 1.0 | stroke 0.02 |
| Coral dot | straight up from the centre, at 0.88 | radius 0.045 |
| Bars | 48 of them, bar k at angle `-pi/2 + (k + 0.5) * 2pi/48` (screen coordinates, clockwise from 12 o'clock), centred on the circle of radius 0.64, pointing at the centre | stroke 0.03 with round caps; length `0.08 + 0.07 * level` cap centre to cap centre, level 0..3 |
| Logo | the middle | inside radius 0.40; carries nothing (the round screen draws the Tikpal mark 0.40 across: the code large, the middle small) |

Every bar's midpoint lies on the circle of radius 0.64 whatever its length: a reader finds the code's centre and
tilt from the bars' midpoints alone, then reads each bar's length along its own direction. Colour carries nothing:
the reader looks at brightness only.

## Bits (as in the contract)

1. **Data, 6 bytes**: byte 0 is the version in bits 7..6 (0), the kind in bits 5..4, then payload bits 43..40;
   bytes 1..5 are payload bits 39..0, most significant first.
2. **Kinds** (`yk_vc_kind_t`):
   - 0 `test`: a 44-bit number;
   - 1 `pair` or `selfie`: bits 43..28 are the last 4 hex digits of the device id, bits 27..1 eight digits
     (0..99999999), and bit 0 tells them apart:
     - 0 `pair` (`YK_VC_PAIR`): Bluetooth pairing, the device `YK-xxxx` and its pairing code;
     - 1 `selfie` (`YK_VC_SELFIE`): 自拍屏's hotspot `Tikpal-Selfie-xxxx`, its WPA2 password the 8 digits (made anew
       each time 自拍屏 opens). The app joins it straight away and opens the viewfinder.
   - 2 `lookup`: a 44-bit id the server resolves (not used yet);
   - 3 `app`, 应用码: the app's number in bits 43..28 (1..65535), an action in bits 27..24, a value in bits 23..0.
     Actions: 0 the app page, 1 its whole service page, 2 its records of a day (value: days since 1970-01-01, the
     round screen's local date), 3 「问 Yay」 with a sentence from its registration (value: its index from 0);
     4..7 not used yet (the app opens the page); 8..15 the app's own.
3. **Reed-Solomon over GF(256)**: polynomial `0x11d`, alpha = 2; generator `g(x) = (x - a^0)(x - a^1)...(x - a^5)`;
   codeword `c[0..11]` = the 6 data bytes followed by 6 parity bytes, read as `c(x) = c[0] x^11 + ... + c[11]`, a
   multiple of `g(x)`. Corrects 3 wrong bytes, or 6 erased ones (`2 * errors + erasures <= 6`). A bar that cannot be
   read makes its byte an erasure.
4. **Mask**: XOR the 12 codeword bytes with `5B C2 37 9E 64 A9 1D F0 83 4E B6 29`, so that no payload draws an even
   ring.
5. **Bars**: byte j is bars 4j..4j+3, most significant pair first (bar 4j = bits 7..6).
6. **Reading**: a code that repairs but has another version is "later" (`YK_VC_DECODE_LATER`, `YK_VC_LATER`); one
   whose bits no payload has (app number 0, eight digits over 99999999) is not a code.

**Example**: `test 0x05A3C91E2B4` draws the levels `112321200023003320120131132123011102220031033313`.

## Tests

`bash test/run.sh` (CI runs it through `yaykit/scripts/test-firmware.sh`):

- `test_fixtures.c`: every example in the contract's fixtures: constants, encode (levels, and back), refuse, damaged,
  not codes, later, actions. Needs ESP-IDF's cJSON (`IDF_PATH`); in CI a missing one fails.
- `test_view.c`: the round screen's part, built with no app number, 300, 65535 and the test number 65280: the
  contract's app examples drawn in whole pixels give back their levels character for character, and its pair examples
  (from the text, as `yk_vc_pair_layer` makes them) at R = 184; the talking's last frame is the code exactly and
  nothing changes after it; every frame inside the safe circle (radius 200) and inside its redrawn box; the pairing
  page's digits inside the logo's circle; the screen as drawn (mark included, or the pairing page's digits in its
  place) read back by `yk_vc_scan` (suffix and digits too), and no talking frame read as another code.
- `test_number.sh`: `voicecode_number.cmake` (CMake from PATH or ESP-IDF's tools): numbers read and refused, and a new
  number in `app.json` makes a new object while the same one builds the very same.
- `test_voicecode.c`: round trips of every kind, Reed-Solomon within and beyond its reach, and `yk_vc_scan` on
  rendered 1280x720 frames (tilt, perspective, blur, noise, glare, a dark sector, clutter, hand shake, a later
  version), with negative frames (the bars' animation, clutter, half a code) that must never read.
  How far a frame got: one that finds the centre but not the levels says `bad_levels`, one that reads the levels but
  has no dot says `no_dot`, and a frame with both says the furthest, `no_dot` (runs with `--quick` too). `--quick` skips the
  frames (about 30 s on all cores).

Changing the format means changing the contract first; then its fixtures, then this component and both apps.
