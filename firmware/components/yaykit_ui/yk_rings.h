// The two rings' shared sizes (Yay 0.1.18 dev doc 4.7: the user's 方案一, 11-11): the app ring (yk_apps) and the settings
// ring (yk_ring) are the same size, one set of numbers for both. Plain constants: yk_apps_logic.h includes this, and the
// pure parts both rings share are there (「both rings」).
//
// The places stand on one circle, each a dark disc that holds its icon and nothing else: no names on the ring (the upper
// ones' would leave the screen). A finger down on a place grows it, a sage ring inside its edge, the other places dim,
// and the middle says the place's name in place of what it shows at rest (the buddy; the settings' words). Let go on it:
// it opens (a tap, yk_tap). Moved off, held YK_TAP_HOLD_MS, swiped: the ring is as it was, nothing opens. A finger down
// on the middle says the middle's own words there (Tikpal; 回到 Tikpal in an extension).
//
// The round-screen spec (yaykit/docs/product/round-screen-design.md 2), pinned by test_apps.c and test_ring.c: every touch
// target at least YK_TOUCH_MIN_W across and YK_TOUCH_GAP apart (up to YK_RINGS_SPACED places), every word inside
// YK_R_CONTENT, nothing in the rim band (YK_R_BAND). The discs reach 4 px past the content circle, 12 pressed: they hold
// no words, and the screen's outer ring was empty (the design's: to be looked at on the device, dev doc 10).
#pragma once

#define YK_RINGS_R            150  // the places' centres from the screen's centre (0.1.17: 142 apps, 132 settings)
#define YK_RINGS_SAT_R        54   // a place's disc: 108 across (0.1.17: 76 apps, 68 settings)
#define YK_RINGS_HIT_R        58   // its touch target: 116 across
#define YK_RINGS_ICON         64   // what a disc holds: the app catalog's icon as it is (YK_CATALOG_ICON), space round it;
                                   // the settings' line icons at YK_RING_ICON
#define YK_RINGS_FIRST_PX     36   // the first character standing in for a missing icon
#define YK_RINGS_CENTER_R     75   // the middle's disc: 150 across (0.1.17: 112)
#define YK_RINGS_CENTER_HIT_R 75
#define YK_RINGS_FIGURE       104  // the buddy in the app ring's middle, on the middle's colour: its square inside the disc
                                   // (a buddy's pack draws on opaque paper only, yk_yay_figure; the design's 117 was a
                                   // transparent Pip's, whose corners would stand out past the disc). 0.1.17: 100 in 112
#define YK_RINGS_IN_USE_GAP   2    // the sage ring of what is in use: this far outside its disc (yk_draw_in_use) ...
#define YK_RINGS_IN_USE_W     3    // ... this wide
#define YK_RINGS_LEVEL_GAP    3    // a setting's level (Yay 0.1.19 4.7): an arc this far outside its disc ...
#define YK_RINGS_LEVEL_W      4    // ... this wide (r 57-61), 7:30 clockwise to 4:30 as the dial's scale, the accent's
                                   // thin line on the part reached and the disc's colour on the rest; hidden pressed
#define YK_RINGS_LEVEL_DEG    270

// A finger down on a place (the press, yk_rings_pressed()).
#define YK_RINGS_PRESS_R      62   // the place under it grows to 124 across (1.15 times) ...
#define YK_RINGS_PRESS_W      4    // ... a sage ring this wide inside its edge (outside, the place at the top would
                                   // reach the rim band), in place of the in-use ring ...
#define YK_RINGS_PRESS_MS     100  // ... in this long, eased out (every touch shows within YK_RESPONSE_MS)
#define YK_RINGS_DIM_OPA      115  // the other places meanwhile: 45 %, at once
// Its marks (the update dot, the cached ring, the download mark) stay where they are: the disc grows under them.

// The middle's words: what a ring shows there at rest (the settings' title, its line under it, the line below the
// places), or a pressed place's name. One block of lines about the centre, each inside the middle's disc.
#define YK_RINGS_NAME_PX      24   // a pressed place's name: YK_SIZE_HEADING; two lines when one does not hold it
#define YK_RINGS_WORDS_W      120  // a line's room: the disc's width 45 px above and below the centre
#define YK_RINGS_WORDS_H      90   // ... and the block's height (45 each way)

#define YK_RINGS_SPACED       7    // up to this many places keep YK_TOUCH_GAP apart (7: 14 px); 8 (the settings ring's
                                   // YK_RING_MAX) are drawn, their targets overlapping (the nearer place takes a touch)
