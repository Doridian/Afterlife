/*
 * TILE resources: the map's isometric building/terrain sprite sets, decoded
 * the way ALIFE.EXE does it (tile_load_set 0x4e688a, the tile_get_*
 * accessors 0x4ef4fb-0x4ef729 and the strip decoder at the core of tile_draw
 * 0x4e6a6a). See docs/formats/tile.md.
 */
#ifndef AFTERLIFE_TILE_H
#define AFTERLIFE_TILE_H

#include <stddef.h>
#include <stdint.h>

/* Tile-set slots in the original (tile_set_data[0x100]); resource N is
 * loaded into slot N-1. */
#define TILE_MAX_SETS 0x100

enum {
    TILE_OK = 0,
    TILE_ERR_FORMAT = -1, /* not a TILE resource / bad header (the original's -1) */
    TILE_ERR_TRUNCATED = -2,
    TILE_ERR_RANGE = -3, /* entry or strip index out of range, or empty entry */
};

typedef struct {
    const uint8_t *body; /* everything after "TILE" + u32 length */
    size_t len;          /* body length */
    uint16_t count;      /* entries */
    uint8_t version;     /* +2, always 1 */
    uint8_t cell_width;  /* +3, always 32: width of a 1x1 cell in pixels */
    uint32_t cell_height;/* +4, always 16 */
} TileSet;

typedef struct {
    const uint8_t *base; /* entry start (within TileSet.body) */
    size_t avail;        /* bytes from base to the end of the body */
    uint32_t text_off[4];/* +0x04..+0x10: strings, relative to base + 0x1c (0 = none) */
    uint16_t height;     /* +0x14: rows in every strip */
    uint16_t anchor;     /* +0x16: rows above the cell's ground line */
    uint8_t type;        /* +0x18: 1 = RLE strips (the only kind; others aren't drawn) */
    uint8_t strips;      /* +0x19: number of strip offsets */
    uint8_t size;        /* +0x1a: footprint, size x size cells */
    const uint8_t *lut;  /* colour map: strip bytes are indexes into it */
    size_t lut_len;
} TileEntry;

/* 0x4e688a: validate a TILE resource (the whole payload, starting with the
 * "TILE" tag). `data` must stay valid while the set is used. */
int tile_set_open(TileSet *ts, const uint8_t *data, size_t len);

/* The original's pointer table lookup (tile_set_entries[set][index - 1];
 * the accessors take 0-based indexes, tile_draw 1-based ones). `index` is
 * 0-based here. A zero offset is not special: it aliases entry 0. */
int tile_get_entry(const TileSet *ts, int index, TileEntry *e);

/* 0x4ef603 (which=0, name), 0x4ef665 (1, description), 0x4ef6c7 (2) and
 * 0x4ef729 (3). NULL if the entry has no such string or it isn't
 * terminated inside the resource. */
const char *tile_entry_text(const TileEntry *e, int which);

/* Strips actually drawn by tile_draw: 2*size-1 (some entries store one
 * more). Strip size-1 is the full-cell centre strip. */
int tile_drawn_strips(const TileEntry *e);
/* Width of strip `strip` (0-based) in pixels, from its first row. */
int tile_strip_width(const TileEntry *e, int strip);

/* Draw one strip at 1:1 with its top-left at (x, y) into an 8-bit buffer,
 * clipped to w x h. Transparent (skip) pixels are left untouched. This is
 * tile_draw's inner loop without zoom or the highlight/overlay remaps.
 * Returns TILE_OK or an error. */
int tile_draw_strip(const TileEntry *e, int strip, uint8_t *dst, int pitch, int w, int h, int x, int y);

/* The whole sprite: the drawn strips side by side, top-aligned (the
 * original draws each strip with its own map cell, which lines them up the
 * same way). Width is tile_sprite_width(), height e->height. */
int tile_sprite_width(const TileEntry *e);
int tile_draw_sprite(const TileEntry *e, uint8_t *dst, int pitch, int w, int h, int x, int y);

#endif
