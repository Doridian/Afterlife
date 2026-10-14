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
byte-identical (1, 2, 12, 23, 24) and are told apart by slot. `tile_draw`
treats slots 14/15 specially and redirects them to slot 28 (resource 29) at
some zoom levels.

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

`tile_draw` takes 13 arguments (result, surface, view, map x/y, screen x/y,
map cell pointer, set, index, strip, fade, force) and is ~14 KB. It does much
more than decode: with a map cell it takes the set from bits 27-31, the
index from bits 16-22 and the strip from bits 23-26 (cells of big buildings
point back at their origin cell through bits 16-22), clips against the view,
and supports
- zoom: the view's shift (+0x31) skips `2^z - 1` rows and steps x, and a
  magnify flag (+0x35) writes each pixel 2x2;
- rotation/mirroring: the x step is `view[+0x39] * view[+0x3a]` (+-1);
- overlays decoded in parallel from other tile sets (selection/highlight
  strips, via 0x57d4a0), and palette remaps through the 256-byte tables at
  0x64cf10, 0x64d010, 0x64d310 and 0x64d410;
- a dithered "under construction" fade (random mask from 0x57d980).

The port's `tile_draw_strip` is its zoom-0 path with no effects:
`lut[byte]` written for every non-skip pixel (the effect path also treats
colour 0 as transparent).
