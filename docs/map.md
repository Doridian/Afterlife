# The map: grids, cells and the isometric view

This page covers how ALIFE.EXE stores the map and draws it. Port:
`src/gfx/map_view.c` (traversal, transforms, planes, passes) and
`src/gfx/tile_draw.c` (one strip of one cell, see
[formats/tile.md](formats/tile.md#drawing-tile_draw-0x4e6a6a)). Simulation
logic is not covered here.

## Planes and grids

The game shows three 100 x 100 maps stacked on one screen.
`map_view_select_plane` (0x42b102) points the view at one of them:

| Plane | What | Cell grid (u32) | Object map (u8) | Sim cells (18 B) | y offset (zoom 0) | Margins below/above |
|---|---|---|---|---|---|---|
| 1 | Hell (bottom) | 0x593270 | 0x5f4cf0 | 0x5fa670 | 0x708 | 0x1cc / 0x48 |
| 2 | middle: Karma stations and rails | 0x5bb270 | 0x5f25c0 | none | 0x3a4 | 0x80 / 0x48 |
| 3 | Heaven (top) | 0x6265b0 | 0x5f7ee0 | 0x5c6680 | 0x40 | 0x1cc / 0x48 |

Every grid is row-major, `cells[y * 100 + x]`. The current plane is also in
0x630518 (cells) and 0x57e014 (sim cells, `y * 0x708 + x * 0x12`). Selecting a
plane also stores the view flags per plane (0x65ed98 / 0x65eda0 / 0x65ed9c)
and switches several simulation tables. The view's `origin_y` becomes
`(y_offset >> zoom << magnify) + scroll_y` (0x63051c). The "margins" make
the traversal scan extra rows below and above the visible area, so tall
sprites standing outside the view still get drawn.

### Cell (u32)

| Bits | Name (port) | Meaning |
|---|---|---|
| 0 | `CELL_SHADE` | remap through shade table 0 (table 4 on plane 3) |
| 1 | `CELL_TINT` | grey remap plus the crack overlay; TV static on set 4 |
| 2 | `CELL_OVERLAY` | gunk overlay sprite |
| 3 | `CELL_MARKER` | dotted coloured diamond drawn after the map |
| 4 | `CELL_ALT` | draw a stand-in (zone block / marker) instead of the building |
| 5 | `CELL_DEFER` | skipped by the first pass; redrawn plainly or cross-faded with a map object |
| 6 | `CELL_OBJECT` | a map object covers this cell (see the object map) |
| 7 | `CELL_HILITE` | inside the highlighted area; masked off when saving and loading |
| 8-12 | | used by the simulation (set on about half the cells), not read by the renderer |
| 15 | `CELL_BRACKET` | selection bracket around the building |
| 16-22 | index | tile entry index - 1 |
| 23-26 | strip | strip of the sprite this cell draws, 1-based; 0 = none |
| 27-31 | set | tile set slot (resource - 1); **0 = part of a bigger building** |

For a building of footprint n x n with its origin (the cell holding set
and index) at (x0, y0), the footprint is x0..x0+n-1 by y0-n+1..y0. Every
other cell has set 0, with dx = x - x0 in bits 16-18 and dy = y0 - y in bits
20-22. Strips sit on the two front edges, `strip = dx + 1 + dy` along row y0
and column x0+n-1, with the centre strip n on the front corner. All other
cells have strip 0. This holds for every such cell in the four sample saves.
The strips are stored for the current rotation: `map_rotate_cells`
(0x4514a4) rewrites the grids when the view rotates (directional road and
river pieces as well).

Common slots: 1 = terrain (entry 1 empty land with a grid outline, 2 plain),
2 = rivers and water, 12 = roads, 14 / 15 = Hell / Heaven zones (13 entries
per zone type: an empty zone tile, then buildings), 16-18 = plane-2 rails
and Karma stations, 28 = plain zone blocks, 10 = overlays, brackets and
planet sprites.

### Object map (u8) and map objects

Per cell: `object slot << 2 | counter`. There are 63 map objects (0x64e5f0,
0x17-byte records: +0 plane (0 = free), +1/+2 origin x/y, +3/+4 step,
+5 size, +6 set, +7 frame (entry - 1), +8 first frame, +9/+0xa frame delay,
+0xb set mask, +0xf fade, +0x12 priority, +0x13 flag). These are the
sprites that move over the map. `map_objects_step` (0x4eedc1) moves and
animates them. Around each step it sets the focus (0x64e4f0/4 = origin,
0x64eba0 = size, -1/-1/0 otherwise) and calls `map_object_redraw` (0x4ed550)
to redraw just the area they cover. `map_object_mark_cells` (0x4ee5d8)
maintains the object map and `CELL_OBJECT`. None of that is ported. The
renderer only reads the records.

### Savegames (ALSV)

The map part of an `.ASC` savegame (`save_load_game` 0x48f837,
`save_load_plane` 0x48a83a), all little-endian:

```
"ALSV" "7195"            version as 4 ASCII digits
u8, u32, u32, title\0
[ver > 7219] u16
[ver > 7025] u32 w, u32 h, w*h bytes   thumbnail (8-bit, w=104 h=78)
plane 1, plane 2, plane 3:
  for x in 0..99: for y in 0..99:      (column-major!)
    u32 cell (bit 7 masked off), u8 object map
    planes 1, 3 only: u8 kind, then a payload whose size depends on the
                      cell's set (bits 27-31; 0 for building parts): see
                      sim_payload() in tests/map_render.c
  planes 1, 3 only: a fixed block of statistics (~32 KB for version 7195)
... (objects and the rest of the game state follow)
```

`tests/map_render` parses this and renders all four sample saves.

## The view (0x5bb120)

`MapView` in `src/gfx/tile_draw.h` mirrors the packed original. The fields:
cells, object map, origin x/y, width/height (100), clip x/y/w/h, cell size
(32 x 16), overlay key (0), zoom shift (+0x31, default 2), magnify (+0x35),
dir_x/dir_y (+0x39/+0x3a), focus x/y (+0x3b/+0x3f), set mask (+0x43),
plane (+0x47), flags (+0x48: 0x08 no crack overlay, 0x10 no markers,
0x40 zone detail, 0x80 grid), highlight colours (+0x49/+0x4a), margins
(+0x4c/+0x50) and miniature mode (+0x54).

### Coordinates

With hw = `(cell_w/2 >> zoom) << magnify` and hh likewise (16 x 8 at
zoom 0):

- **map to logical** (rotation, `map_view_to_logical`):
  `(dir_x, dir_y) = (+,+)`: i = x, j = y; `(-,+)`: i = y, j = H-1-x;
  `(+,-)`: i = W-1-y, j = x; `(-,-)`: i = H-1-x, j = W-1-y. The sprites are
  mirrored when `dir_x * dir_y < 0`.
- **logical to screen**: the cell's left corner is at
  `sx = origin_x + (i - j + H - 1) * hw`, `sy = origin_y + (i + j) * hh`.
  Its diamond spans `sx .. sx + 2hw` and `sy .. sy + 2hh` (at magnify 1,
  `sy - hh .. sy + hh`, since the ground line is at `sy + (cell_h >> zoom)`).
  `map_cell_to_screen` (0x4eb79e) returns the top corner relative to the
  origin, `((H + i - j) * hw, (i + j) * hh)`.
- **screen to map** (`map_screen_to_cell` 0x4eb8c9): with
  `u = sx - origin_x - 100 hw` and `v = sy - origin_y`,
  `i = (v + u/2) / hw`, `j = (v - u/2) / hw` (truncating), clamped to 0..99,
  then rotated back. The original hard-codes 100.
- `map_cell_rect` (0x4ebc33): the screen box of a w x h area (x, width,
  y - magnify*hh, height), used to compute what to redraw.

The game's main view uses `origin_x = scroll_x + window offset` and the
plane's `origin_y`. All three planes share one scroll position.

### Draw order and clipping (map_view_draw, 0x4ea0b6)

1. Clamp the clip rectangle to the window's client area (inclusive rect).
2. Shift `origin_x` left by hw for the duration. Turn the clip rectangle,
   widened by the plane's margins, into a logical row range:
   `j_min = (top * hw - right * hh) / (2 hw hh)`,
   `j_max = (bottom * hw - left * hh) / (2 hw hh) + 1 + magnify`
   (coordinates relative to the map origin; clamped to the map).
3. For each row j, ascending, the column range is
   `i_min = max(top/hh - j, j + left/hw) - (2 magnify + 2)` and
   `i_max = min(bottom/hh - j, j + right/hw) + magnify + 1`. Each cell is
   visited left to right, stepping the cell pointer by +1, +W, -W or -1
   depending on the rotation.
4. Each cell gets `tile_draw(cell)`, which draws the strip that cell owns.
   Because every strip belongs to the front-edge cell it overlaps, this
   painter's order (j, then i) layers buildings correctly.
5. Then, for `CELL_DEFER` cells: if there's no object (or no `CELL_OBJECT`),
   the building is redrawn without cell effects, unless it lies inside the
   focused object's rectangle. Otherwise the object's strip is drawn
   (`object_strip`, from its size and the cell's offset). If the building has
   the object's set, the two cross-fade: the building at fade f, the object
   at 10 - f.
6. Every 300 cells it calls the idle tasks (`snd_idle`) so audio keeps
   streaming.
7. If any visible cell has `CELL_MARKER` (and flag 0x10 is clear), the
   marker pass (0x4eaf9d / 0x4ecd16) draws a diamond of every other pixel in
   colour `0xd0 + (2y + x) % 5` (`0xd5 + (2y + x) % 6` for cells 49-50,
   the map centre).

**Full repaint** (`map_view_paint` 0x4076f5): fill with colour 0x13 (or a
tiled backdrop bitmap), then for planes 1, 2, 3: take the highlight off
(`map_hilite_pass` 0x4ed0f4 with the -10 hue table), draw the plane, put the
highlight back (+10). **Partial redraw** (`map_redraw_cells` 0x4ebd8a): clip
to the area's screen box (doubled in width, plus headroom), draw the current
plane and every plane after it.

**Highlight** (`map_hilite_area` 0x4ebfbc): remaps the diamond over a cell
area through `tile_hue_up` / `tile_hue_down` (0x57df08 / 0x57d880, built by
`tile_init`). These rotate colours 0x20-0xaf by +-10, with one colour pair
from the view swapped. The original's scanline code steps the diamond
edges 2 px per row; the port scans the same diamond.

## Verification

```
map_render <data dir> MEDIUM.000 <out dir> orig/ASCEN00*.ASC
```

The self-checks compare every strip of every set (2365 strips) at 1:1, 2x,
1:2 and 1:4, mirrored and clipped, with the reference decoder. All match
pixel for pixel. The test also checks that the screen and cell transforms
invert each other in all four rotations and three zooms. It writes contact
sheets per set and zoom, a sheet of every effect, a synthetic Hell (zones,
buildings, roads, four rotations, blocks vs detail), and the three planes
of each savegame (stacked at 1:4 and 1:2, single planes, a 1:1 crop).

Not ported yet: the object system (0x4eedc1 and friends), the plane-select
side effects on simulation tables, the set-30 meter source (simulation), and
`map_rotate_cells`.
