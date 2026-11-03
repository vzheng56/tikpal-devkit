# 声波码 (yaykit_voicecode)

Tikpal's own round code: shown on the round screen, read by the Tikpal app. Requirement:
`docs/delivery/2026-10-30-voice-code.md`. **The contract is `packages/contracts/src/voice-code.ts`**: this component
follows it bit for bit, and `test/run.sh` checks every example in `packages/contracts/fixtures/voice-code.json`. When
the two disagree, the contract is right.

Plain C99 + libm, no ESP-IDF, no hardware: it owns no resources. Nothing in the firmware uses it yet; drawing the
code on the round screen (a `yaykit_ui` widget) and the API level for it come later (the delivery doc, step 3).

| File | What | Who links it |
|---|---|---|
| `include/yk_voicecode.h` | the API | everyone |
| `yk_voicecode.c` | layout, payloads, Reed-Solomon encoder | firmware, apps, tests |
| `yk_voicecode_decode.c` | Reed-Solomon decoder with erasures, `yk_vc_decode_levels` | firmware (built, kept only if called), apps, tests |
| `yk_voicecode_scan.c` | reading a code from a camera frame, `yk_vc_scan` | apps, tests (megabytes per frame: not the firmware) |
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

In the app: `yk_vc_scan(luma, width, height, stride, &result)` on each camera frame (thread-safe, a few ms at
1280x720). `YK_VC_OK`: `result.payload`. `YK_VC_LATER`: a code of a later version, ask for the app to be updated.
Anything else: not read in this frame. For an app code, `yk_vc_action_does(action)` says what to open ("page",
"service", "day", "ask", "own").

## Layout

Light marks on a black round screen. All sizes are in units of the ring's radius (1.0).

| Part | Where | Size |
|---|---|---|
| Ring | circle of radius 1.0 | stroke 0.02 |
| Coral dot | straight up from the centre, at 0.88 | radius 0.045 |
| Bars | 48 of them, bar k at angle `-pi/2 + (k + 0.5) * 2pi/48` (screen coordinates, clockwise from 12 o'clock), centred on the circle of radius 0.64, pointing at the centre | stroke 0.03 with round caps; length `0.08 + 0.07 * level` cap centre to cap centre, level 0..3 |
| Logo | the middle | inside radius 0.40; carries nothing |

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
- `test_voicecode.c`: round trips of every kind, Reed-Solomon within and beyond its reach, and `yk_vc_scan` on
  rendered 1280x720 frames (tilt, perspective, blur, noise, glare, a dark sector, clutter, hand shake, a later
  version), with negative frames (the bars' animation, clutter, half a code) that must never read. `--quick` skips the
  frames (about 30 s on all cores).

Changing the format means changing the contract first; then its fixtures, then this component and both apps.
