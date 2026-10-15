/*
 * The map tile renderer: tile_draw (0x4e6a6a) and the state it reads.
 *
 * tile_draw draws one vertical strip of a TILE sprite (src/formats/tile.h)
 * for one map cell, at any zoom level, mirrored or not, with the cell's
 * colour effects (shade/tint tables, overlay sprites, a dithered fade, the
 * selection bracket and the "miniature" mode). The map traversal that calls
 * it lives in gfx/map_view.h. See docs/formats/tile.md and docs/map.md.
 *
 * Tile sets are looked up by slot like the original's tile_set_entries
 * (0x57d478): resource N is bound to slot N-1 with tile_draw_bind_set().
 */
#ifndef AFTERLIFE_GFX_TILE_DRAW_H
#define AFTERLIFE_GFX_TILE_DRAW_H

#include <stdbool.h>
#include <stdint.h>

#include "formats/tile.h"
#include "gfx/surface.h"

/* --- map cells (u32, the grids at 0x593270 / 0x5bb270 / 0x6265b0) ------- */

#define CELL_SHADE 0x00000001u    /* remap through shade table 0 (plane 3: table 4) */
#define CELL_TINT 0x00000002u     /* remap through table 1, plus overlay 29+size from set 10 */
#define CELL_OVERLAY 0x00000004u  /* overlay 25+size (36 for set 10) from set 10 */
#define CELL_MARKER 0x00000008u   /* dotted diamond drawn after the map (map_view_draw_marker) */
#define CELL_ALT 0x00000010u      /* draw a stand-in instead of the building (see tile_draw) */
#define CELL_DEFER 0x00000020u    /* main sprite skipped; map_view_draw draws it later */
#define CELL_OBJECT 0x00000040u   /* a map object (objmap byte) covers the cell */
#define CELL_HILITE 0x00000080u   /* inside the highlighted area (cleared when saved) */
#define CELL_BRACKET 0x00008000u  /* selection bracket drawn behind and in front */
#define CELL_INDEX_SHIFT 16       /* bits 16-22: entry index - 1 */
#define CELL_INDEX_MASK 0x007f0000u
#define CELL_STRIP_SHIFT 23       /* bits 23-26: strip, 1-based; 0 = draw nothing */
#define CELL_STRIP_MASK 0x07800000u
#define CELL_SET_SHIFT 27         /* bits 27-31: tile set slot; 0 = part of a bigger */
#define CELL_SET_MASK 0xf8000000u /* building: bits 16-18 dx, 20-22 dy to its origin */

static inline unsigned cell_word_set(uint32_t c) { return c >> CELL_SET_SHIFT; }
static inline unsigned cell_word_index(uint32_t c) { return ((c & CELL_INDEX_MASK) >> CELL_INDEX_SHIFT) + 1; }
static inline unsigned cell_word_strip(uint32_t c) { return (c & CELL_STRIP_MASK) >> CELL_STRIP_SHIFT; }

/* --- the map view (0x5bb120 for the main map; 0x55+ bytes, packed) ------- */

/* MapView.flags (+0x48), saved per plane */
#define MAPVIEW_F_NO_TINT_OVERLAY 0x08 /* CELL_TINT only remaps, no overlay sprite */
#define MAPVIEW_F_NO_MARKERS 0x10      /* skip the CELL_MARKER pass */
#define MAPVIEW_F_ZONE_DETAIL 0x40     /* draw zone sets 14/15 as is, not as slot-28 blocks */
#define MAPVIEW_F_GRID 0x80         /* empty land (set 1 entry 1) with its grid outline;
                                        otherwise drawn as entry 2, plain colour 0 */

struct MapObject;

/* Callback for set 30's level meter (FUN_0042dc42 / FUN_0042dcdf read the
 * simulation cell): colours < 11 become 0 below `lo` and 0x30 from it,
 * colours > 245 become 0 below `hi` and 0x48 from it. Return false for no
 * meter. */
typedef bool (*TileMeterFn)(void *ctx, int map_x, int map_y, int index0, uint8_t *lo, uint8_t *hi);

typedef struct MapView {
    uint32_t *cells;          /* +0x00: width x height cells, row-major */
    uint8_t *objmap;          /* +0x04: per cell: map object id << 2 | 2-bit counter */
    int32_t origin_x;         /* +0x08: screen x of the map origin (scroll) */
    int32_t origin_y;         /* +0x0c */
    int32_t width, height;    /* +0x10, +0x14: 100 x 100 */
    int32_t clip_x, clip_y;   /* +0x18..+0x24: area to draw (surface coordinates) */
    int32_t clip_w, clip_h;
    int32_t cell_w, cell_h;   /* +0x28, +0x2c: 32 x 16 at zoom 0 */
    uint8_t key;              /* +0x30: overlay colour meaning "tile pixel" (0) */
    int32_t zoom;             /* +0x31: shift, 0 = 1:1 .. 2 = 1:4 (the default) */
    int32_t magnify;          /* +0x35: 1 = 2x (only at zoom 0) */
    int8_t dir_x, dir_y;      /* +0x39, +0x3a: +-1, the four map rotations */
    int32_t focus_x, focus_y; /* +0x3b, +0x3f: -1, or the object being redrawn */
    uint32_t set_mask;        /* +0x43: tile sets drawn while focus_x >= 0 */
    uint8_t plane;            /* +0x47: 1 = Hell, 2 = middle, 3 = Heaven */
    uint8_t flags;            /* +0x48: MAPVIEW_F_* */
    uint8_t hilite_from;      /* +0x49, +0x4a: colour swapped by the highlight pass */
    uint8_t hilite_to;
    int32_t margin_bottom;    /* +0x4c: rows below the view still scanned (tall sprites) */
    int32_t margin_top;       /* +0x50 */
    uint8_t mini;             /* +0x54: buildings drawn half size on a plinth */
    /* port-only */
    const struct MapObject *objects; /* 0x64e5f0, MAP_OBJECTS entries */
    TileMeterFn meter;
    void *meter_ctx;
} MapView;

/* Half a cell on screen at the view's zoom (the expression the original
 * repeats everywhere: (cell_w / 2 >> zoom) << magnify). */
static inline int mapview_half_w(const MapView *v) { return ((v->cell_w >> 1) >> v->zoom) << v->magnify; }
static inline int mapview_half_h(const MapView *v) { return ((v->cell_h >> 1) >> v->zoom) << v->magnify; }
/* x step of the view: -1 when the rotation mirrors the sprites. */
static inline int mapview_dir(const MapView *v) { return v->dir_x * v->dir_y; }

/* --- tile sets and tables ------------------------------------------------- */

/* Binds `ts` (NULL to unbind) to `slot` (tile_set_entries[slot]). The set
 * must stay valid while bound. */
void tile_draw_bind_set(int slot, const TileSet *ts);
const TileSet *tile_draw_set(int slot);

/* 0x4e66b0 tile_init, minus clearing the slots: builds the highlight hue
 * remaps (0x57d880 / 0x57df08) and the 50 fade seeds (0x57d980). The
 * original seeds from the game RNG (FUN_0042b088); `seed` feeds a local one. */
void tile_draw_init(uint32_t seed);
/* The tail of tile_load_all (0x408eb0): shade tables 0-4 from the game
 * palette (256 R,G,B,x) via pal_build_shade_table, and table 5 (0x64d410),
 * random colours 0xe3-0xe6 for indexes >= 0x20 (TV static). */
void tile_draw_build_tables(const uint8_t *rgbx);
/* The six 256-byte remap tables at 0x64cf10 (0-4 are gfx_palette's). */
const uint8_t *tile_draw_table(unsigned n);
/* 0x57d880 (dir < 0: hue -10) / 0x57df08 (dir >= 0: +10), within 0x20-0xaf.
 * Writable: the highlight pass swaps one entry while it runs. */
uint8_t *tile_draw_hue_table(int dir);

/* --- drawing -------------------------------------------------------------- */

/* 0x64e4f8 / 0x64e4fc: while view->focus_x >= 0, sets in view->set_mask are
 * only drawn from this logical cell on (map_view_draw sets them). */
extern int32_t tile_draw_focus_min_x, tile_draw_focus_min_y;

/* tile_draw's 11-byte return value. */
typedef struct {
    uint8_t set, index, strip; /* what the cell resolved to (index and strip 1-based) */
    int32_t map_x, map_y;      /* the coordinates passed in */
} TileDrawResult;

/* 0x4e6a6a tile_draw. Draws strip `strip` (1-based, 1..2*size-1) of entry
 * `index` (1-based) of slot `set` at the screen position of the cell's left
 * corner (sx, sy) into `dst`, clipped to the view's clip rectangle.
 *
 * With `cell` (a pointer into view->cells at logical (map_x, map_y)), set,
 * index and strip come from the cell, unless `strip` is non-zero, which keeps
 * the passed strip and forces drawing (`force`). `fade` 1..9 draws that many
 * tenths of the pixels (dithered), 0 all of them. `force` draws even when the
 * view's focus rules would skip the set. */
TileDrawResult tile_draw(GfxSurface *dst, MapView *view, int map_x, int map_y, int sx, int sy, const uint32_t *cell,
                         unsigned set, unsigned index, unsigned strip, int fade, int force);

/* 0x4eb585: a whole building centred in the view's clip rectangle at `zoom`
 * (dialogs and the building info window). With `cell` non-NULL the set and
 * index come from that cell. Forces dir_x = dir_y = 1 while drawing. */
void tile_draw_preview(GfxSurface *dst, MapView *view, unsigned set, unsigned index, int zoom, const uint32_t *cell);

#endif
