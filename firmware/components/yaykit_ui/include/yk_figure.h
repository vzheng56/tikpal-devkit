// A buddy's picture pack (docs/delivery/2026-11-06-形象设计-加工流程.md step 15): the pictures Pip has built in, the eyes
// shut and the mouth at three levels as small blocks to paste at (x, y). yaykit/scripts/figure-pack.ts has the byte table,
// and readFigurePack the reading this follows. Pure C, host tested (test/test_figure.c): nothing in a pack is believed
// until yk_figure_read has checked it, and after that nothing here reads outside the pack.
//
// The pack's bytes are the caller's and stay where they are, in PSRAM: yk_figure_t only points into them. yk_yay_figure()
// hangs one on a Yay, so only the instance that asks for it draws from it; every other one draws the built-in Pip.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "yk_yay.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YK_FIGURE_FORMAT 1          // PACK_FORMAT: a higher one is refused
#define YK_FIGURE_HEADER 2048       // PACK_HEADER_BYTES: the fixed part and both tables
#define YK_FIGURE_MAX_BYTES 643072  // BUDDY_PACK_MAX_BYTES (packages/contracts/src/device.ts)
#define YK_FIGURE_NAME 16           // a figure's, a theme's and a pose's name, with its terminator

typedef enum { YK_FIGURE_EYE = 0, YK_FIGURE_MOUTH = 1 } yk_figure_kind_t;

/** A pack that has been read. */
typedef struct yk_figure {
    const uint8_t *data;  // the pack, `bytes` long
    uint32_t bytes;
    uint32_t art_version;
    uint16_t format, pictures, patches;
    uint32_t picture_table, patch_table;
    char figure[YK_FIGURE_NAME], theme[YK_FIGURE_NAME];
    uint16_t blink_ms, gap_min_ms, gap_max_ms, breathe_px, breathe_ms;
} yk_figure_t;

typedef struct {
    uint16_t size;            // 176 or 264: square
    bool eyes_open;           // false: drawn with them shut, no eye patches
    const uint8_t *palette;   // 256 entries of B, G, R, A
    const uint8_t *pixels;    // size × size palette indices
    uint16_t first_patch, patches;
} yk_figure_picture_t;

typedef struct {
    uint16_t x, y, w, h;      // in the picture's own pixels
    const uint8_t *indices;   // w × h indices into that picture's palette
} yk_figure_patch_t;

/** Reads and checks `len` bytes: the header, both tables, every offset, and that the home screen's 16 pictures are all
 *  there. False and *why (a word for the log; `why` may be NULL) when anything does not hold: a format newer than this
 *  firmware, an offset not aligned or out of bounds, a size that does not match. */
bool yk_figure_read(const uint8_t *pack, size_t len, yk_figure_t *out, const char **why);

/** The picture to draw `pose` from at `size`, as yk_yay_pick picks Pip's: that size, the next larger, the largest
 *  smaller; THINKING_GLASS is THINKING when the pack does not have it. False: not in the pack at any size. */
bool yk_figure_pick(const yk_figure_t *f, yk_yay_pose_t pose, int size, yk_figure_picture_t *out);

/** A picture's patch: an eye (slot 0 left, 1 right) or the mouth (slot yk_yay_mouth_t: 0 shut, 1 slightly open, 2 open).
 *  False: this picture has none (it is drawn with its eyes shut, or only `speaking` has mouths). */
bool yk_figure_patch(const yk_figure_t *f, const yk_figure_picture_t *pic, yk_figure_kind_t kind, int slot,
                     yk_figure_patch_t *out);

/** The picture named `pose` (yk_yay_pose_name's word) at exactly `size`, as the LVGL I8 file it is: its 12-byte header,
 *  the palette, the indices, `*bytes` in all. NULL: the pack has none at that size. Pip's art from Pip's pack is this
 *  (yk_yay_pip, YAY_ART_PACKED): the same file as yay_<pose>_<size>.bin. */
const uint8_t *yk_figure_file(const yk_figure_t *f, const char *pose, int size, uint32_t *bytes);

#ifdef __cplusplus
}
#endif
