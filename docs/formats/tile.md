# TILE sprite sets

Resources 1-31 (31 sets, in all three bundles) hold the isometric map
graphics: terrain, roads, zones and buildings, each with its in-game name and
description text. All values are little-endian. Decoder: `src/formats/tile.c`;
the original code is `tile_load_set` (0x4e688a), the `tile_get_*` accessors
(0x4ef4fb-0x4ef729) and the big map-cell renderer `tile_draw` (0x4e6a6a).
`tests/formats_dump` decodes every set into a contact sheet.

## Loading

`tile_load_all` (0x408eb0) loads resource N into slot N-1 of
`tile_set_data[0x100]` / `tile_set_entries[0x100]` (0x57da48 / 0x57d478) via
`tile_load_set(id, slot)`; any failure unloads everything. Several sets are
byte-identical (1, 2, 12, 23, 24) and are told apart by slot. Slots 14/15
(Hell/Heaven zones) are drawn as slot-28 blocks unless the view's zone-detail
flag is set (see Drawing).

`tile_load_set` reads the `u32` tag (`TILE`; `TEST` is recognised and
rejected), the `u32` body length, allocates and reads the body, and rewrites
the offset table in place into pointers (`body + 8 + 4*count + offset`).

## Layout

```
"TILE"  u32 len              len = body size (resource size - 8)
body:
+0  u16 count                entries
+2  u8  1                    (always 1; version?)
+3  u8  32                   (always 32; the width of a 1x1 cell in pixels)
+4  u32 16                   (always 16; the cell height)
+8  u32 offset[count]        relative to the end of this table
    entries...
```

A zero offset isn't special: it points at entry 0.

### Entry

```
+0x00 u32  0
+0x04 u32  name         string offsets, relative to entry + 0x1c; 0 = none
+0x08 u32  description  (tile_get_name / _desc / _str3 / _str4, 0x4ef603..)
+0x0c u32  str3         e.g. "After Hour Weinie Roasts" (event names?)
+0x10 u32  str4         e.g. "Tent Peg Collapses"
+0x14 u16  height       rows in every strip (tile_get_height, 0x4ef4fb)
+0x16 u16  anchor       rows above the cell's ground line: the sprite is drawn
                        at cell_y + cell_h - anchor
+0x18 u8   type         1 = RLE strips; tile_draw draws nothing for others
                        (every entry in the data is 1)
+0x19 u8   strips       number of strip offsets
+0x1a u8   size         footprint is size x size cells (tile_get_size, 0x4ef587)
+0x1b u8   colour-map length (low byte of strip_offset[0] in every entry; unused)
+0x1c u32  strip_offset[strips], relative to the end of this table
           u8  lut[strip_offset[0]]     colour map
           strip data...
           NUL-terminated strings (name, description, str3, str4)
```

Strip bytes are indexes into `lut`, which maps them to palette indexes. The
colour map sits between the offset table and the first strip, so
`strip_offset[0]` is its length.

### Strips

A sprite of footprint `size` is cut into `2*size - 1` vertical strips so the
renderer can draw each one with the map cell it overlaps, which gets the
depth order right. Strip `size-1` (0-based) is the centre one and is a full
cell (32 px) wide; the others are half a cell (16 px). The whole sprite is
`32 * size` px wide. Some entries store one extra strip (`strips =
2*size`), which `tile_draw` never draws. Every strip has `height` rows, and
put side by side, top-aligned, they form the complete image (that's what
`tile_draw_sprite` does).

Each row is a length byte `L` followed by `L` bytes of packets; the next row
follows directly (so a row is skipped with `p += *p + 1`). Packets:

| Byte | Meaning |
|---|---|
| `00nnnnnn` | skip n pixels (transparent) |
| `01nnnnnn` + n bytes | n literal pixels, each `lut[byte]` |
| `10nnnnnn` + 1 byte | n pixels of `lut[byte]` |
| `11nnnnnn` | not used; `tile_draw` neither draws nor advances |

The packets of a row add up to the strip width. Some entries are fully
transparent placeholders (a 1-byte LUT and rows of `01 20`, skip 32).

## Drawing (tile_draw, 0x4e6a6a)

Port: `src/gfx/tile_draw.c` (all of it). The map traversal around it is in
[map.md](../map.md). The reference decoder `tile_draw_strip` in
`src/formats/tile.c` stays as the zoom-0, no-effects path, and
`tests/map_render` checks `tile_draw` against it.

`tile_draw(result, window, view, map_x, map_y, sx, sy, cell, set, index,
strip, fade, force)` draws **one strip** (`index` and `strip` are 1-based)
and returns an 11-byte `{u8 set, u8 index, u8 strip, i32 map_x, i32 map_y}`
with what the cell resolved to.

**Resolving the sprite.** With a cell pointer, the set, index and strip come
from the cell (see map.md for the bits). A cell with set 0 belongs to a
bigger building and points at its origin cell (bits 16-18 dx, 20-22 dy;
origin at x - dx, y + dy). A non-zero `strip` argument keeps that strip and
forces drawing. `CELL_ALT` (0x10) draws a stand-in: on plane 2 set 1 entry 2;
for zone buildings (sets 14/15) the matching slot-28 block; otherwise set 15
entry 0x5c, one per cell. Set 1 entry 1 is drawn as entry 2 unless view flag
0x80 (grid) is set: entry 2 is empty land in colour 0, entry 1 the same with
its grid outline. Unless view flag 0x40 is set, zone sets 14/15 are replaced
by slot 28 (resource 29), entry `size*8 + min((index-1)/13, 7) - 7` (capped at
25): a plain diamond per footprint and zone type (13 entries per type).
Strip 0, or a strip >= 2*size, draws nothing.

**Placement.** `sx, sy` is the left corner of the strip's cell. Strips left
of the centre take the cell's left half, those right of it the right half
(+hw), the centre strip the whole cell. The top of the strip is at
`sy + (cell_h >> zoom) - ((anchor >> zoom) << mag) + |strip - size| * hh`,
which puts every strip of a building on the same line: the bottom of its
front cell. With hw/hh = half a cell at the view's zoom, the strip box is
`(src_w << mag) >> zoom` by `(height << mag) >> zoom`, where src_w is 32 for
the centre strip and 16 otherwise. It is clipped against the view's clip
rectangle.

**Zoom.** `view->zoom` is a shift (0 = 1:1, 1 = 1:2, 2 = 1:4, the game's
default). Zoomed out, the decoder takes every 2^zoom-th row and every
2^zoom-th pixel. Sampling restarts per packet, which only differs from
uniform sampling after an empty packet. `view->magnify` = 1 (only at zoom 0)
writes each pixel as a 2x2 block. Rows above the clip are skipped with a
16.16 accumulator. With an odd clip offset the 2x path starts half a source
pixel off, and can write one pixel past the clip on the right and bottom.

**Mirroring.** When `dir_x * dir_y < 0` the strip is drawn right to left and
the half strips swap sides.

**Effects.** Without any effect, `lut[byte]` is written for every non-skip
pixel, colour 0 included. With effects, each opaque pixel goes through these
steps in order, and colour 0 then means transparent:

1. set 30 meter (only when the origin x > 0): colours < 11 become 0 or 0x30
   and colours > 245 become 0 or 0x48, against two levels from the
   simulation (FUN_0042dc42 / FUN_0042dcdf, a callback in the port);
2. `CELL_TINT` (bit 1): with the crack overlay (set 10 entry 30+size, size <
   5, unless view flag 0x08), overlay pixels equal to `view->key` (0) become
   `table1[c]` and other overlay pixels replace c. Without the overlay, c =
   `table1[c]`;
3. `CELL_OVERLAY` (bit 2, size < 5): the gunk overlay (set 10 entry
   26+size; entry 37 for set 10 itself) replaces c where it isn't the key;
4. `CELL_SHADE` (bit 0): `table0[c]` (`table4[c]` on plane 3);
5. `CELL_TINT` on set 4 (resource 5): `table5[c]`, TV static;
6. fade (`fade` 1-9): per row a seed from a 50-entry table (0x57d980,
   cycled), per pixel `seed = rotl(seed, 3)`, drawn only if
   `seed < fade * 0x19999999`.

Overlays are decoded a row at a time into 0x57de48 / 0x57dea8. An overlay
row whose first visible pixel is colour 1 switches that overlay off for the
rest of the strip; the overlays' ground rows are colour 1, so the building's
base stays clean. Quirk: zoomed out, the overlays advance one row per
destination row and one pixel per drawn pixel, so they don't line up with
the sprite. Plane 2 cells get no colour effects.

**Tables** (built at the end of `tile_load_all` 0x408eb0 from the game
palette, resource 33):

| Table | Address | `pal_build_shade_table` args (r, g, b, range, match range) | Used for |
|---|---|---|---|
| 0 | 0x64cf10 | 256, 256, 128, 0x20-0xff, 0x78-0x7d | `CELL_SHADE` (planes 1, 2) |
| 1 | 0x64d010 | 240, 240, 240, 10-0xff, 0x26-0x2f (grey ramp) | `CELL_TINT` |
| 2 | 0x64d110 | 256, 256, 256, 0x20-0xff, 0x20-0x27 | not by tile_draw (FUN_004764da) |
| 3 | 0x64d210 | 128, 128, 500, 10-0xff, 0x20-0xaf | not by tile_draw |
| 4 | 0x64d310 | 256, 256, 256, 0x20-0xff, 0x7c-0x7f | `CELL_SHADE` on plane 3 |
| 5 | 0x64d410 | identity below 0x20, else random 0xe3-0xe6 | static on set 4 |

Colours 0xe3-0xe6 are in a palette cycling range, so the static flickers.
The renderer needs nothing else from the palette resource: its 1144 bytes are
256 B,G,R,x entries plus 15 cycling records (`gfx_palette_load_resource`).
Resource 33 uses 11 of them: 176-181, 182-187, 192-197, 202-207, 208-212,
213-218, 222-226, 227-230, 231-234, 235-239 and 241-245.

**Selection bracket.** For `CELL_BRACKET` (0x8000, not plane 2), set 10 entry
`base + 2*size` is drawn before the strip and `base + 2*size + 1` after it,
with base 0x25 on plane 1 and 0x2d otherwise. These are the two halves of
the ring. Quirk: when the strip is clipped away, the front half is skipped.

**Miniature mode** (`view->mini`, not for sets 1, 2, 9, 10, 12, 16-18,
20-29, empty zone tiles, or plane 2): the building is drawn one zoom level
smaller (or unmagnified), raised and recentred, then set 1 entry
`0x19 + size` (plane 1) or `0x15 + size` is drawn under it as a plinth
(size < 5).

**Focus rule.** A strip is drawn if `force` is set, or `view->focus_x < 0`,
or (the set's bit is in `view->set_mask` and the logical cell is at or past
0x64e4f8 / 0x64e4fc). The object code uses this to redraw only what an
object covers.

`tile_draw_preview` (0x4eb585) draws a whole building centred in the clip
rectangle at a given zoom (dialogs). `tile_stamp_footprint` (0x4eba4a) has
no callers and writes strips in bits 12+, which looks like an older cell
layout.
