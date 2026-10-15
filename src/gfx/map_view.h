/*
 * The isometric map view: traversal and draw order (0x4ea0b6), screen <->
 * cell transforms (0x4eb79e / 0x4eb8c9 / 0x4ebc33), the three map planes
 * (0x42b102), partial redraws (0x4ebd8a), the full repaint (0x4076f5) and
 * the passes drawn over the tiles (markers 0x4eaf9d / 0x4ecd16, the area
 * highlight 0x4ed0f4 / 0x4ebfbc). Each cell is drawn with tile_draw
 * (gfx/tile_draw.h). See docs/map.md.
 *
 * Coordinates: a map cell (x, y) is first rotated into logical (i, j) by
 * the view's dir_x/dir_y (map_view_to_logical); logical (0, 0) is the top
 * corner of the diamond. Cell (i, j)'s left corner is at
 *   sx = origin_x + (i - j + height - 1) * hw,  sy = origin_y + (i + j) * hh
 * with hw/hh half a cell at the view's zoom (16 x 8 at zoom 0).
 */
#ifndef AFTERLIFE_GFX_MAP_VIEW_H
#define AFTERLIFE_GFX_MAP_VIEW_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/tile_draw.h"

#define MAP_OBJECTS 63 /* 0x64e5f0[63], 0x17 bytes each; slot 0 unused */

/* A map object (0x17-byte record at 0x64e5f0): a sprite that moves over the
 * map (the "bad things"), covering size x size cells whose objmap byte holds
 * its slot << 2. Only the fields the renderer reads are named. */
typedef struct MapObject {
    uint8_t plane; /* +0x00: 0 = free */
    int8_t x, y;   /* +0x01, +0x02: map cell of its origin */
    int8_t dx, dy; /* +0x03, +0x04: movement per step */
    int8_t size;   /* +0x05: footprint (negative while hidden) */
    uint8_t set;   /* +0x06: tile set slot */
    uint8_t frame; /* +0x07: entry index - 1 */
    uint8_t fade;  /* +0x0f: 0..10, cross-fade with the building underneath */
} MapObject;

/* Logical <-> map cell coordinates for the view's rotation (square maps:
 * the original uses width and height interchangeably here). */
void map_view_to_logical(const MapView *v, int mx, int my, int *li, int *lj);
void map_view_from_logical(const MapView *v, int li, int lj, int *mx, int *my);

/* 0x4eb79e: map cell -> screen offset of its top corner, relative to the
 * view origin (origin_x/origin_y not added). */
void map_view_cell_to_screen(const MapView *v, int mx, int my, int *sx, int *sy);
/* 0x4eb8c9: screen point -> map cell (clamped into the map; the original
 * hard-codes 100 x 100). */
void map_view_screen_to_cell(const MapView *v, int sx, int sy, int *mx, int *my);
/* 0x4ebc33: screen rectangle {left, width, top, height} covering cells
 * mx..mx+w-1 x my..my+h-1 (logical extents w along i, h along j), with
 * `extra` pixels (zoom 0) of headroom above for tall sprites. Note the
 * order: {x, w, y, h}. */
void map_view_cell_rect(const MapView *v, int mx, int my, int w, int h, int32_t out[4]);
/* 0x4ebbca: the whole map's pixel bounds at zoom 0 (inclusive). */
GfxRect map_view_bounds(const MapView *v);

/* 0x4ea0b6: draws the cells overlapping the view's clip rectangle (clamped
 * to `bounds`, the window's client area, inclusive) into dst, back to
 * front: rows of constant logical j from the top, cells left to right
 * along i. Draws deferred buildings and map objects, then the CELL_MARKER
 * pass. */
void map_view_draw(GfxSurface *dst, MapView *v, const GfxRect *bounds);

/* 0x4eaf9d: the CELL_MARKER pass over the clip rectangle. */
void map_view_draw_markers(GfxSurface *dst, MapView *v, const GfxRect *bounds);
/* 0x4ecd16: dotted diamond over cell (mx, my), colour from (2y + x) % 5. */
void map_view_draw_marker(GfxSurface *dst, const MapView *v, int mx, int my);

/* 0x4ebfbc (w = -1 form): remaps the pixels of the diamond over map cells
 * (mx, my, w x h) through the hue table (`on`: +10, else -10), clipped to
 * `bounds`. The highlight is drawn with on = true after the map and taken
 * off (on = false) before redrawing under it. */
void map_view_hilite_area(GfxSurface *dst, const MapView *v, const GfxRect *bounds, int mx, int my, int w, int h,
                          bool on);
/* 0x4ed0f4: map_view_hilite_area for every CELL_HILITE cell in the clip. */
void map_view_hilite_pass(GfxSurface *dst, MapView *v, const GfxRect *bounds, bool on);

/* --- planes -------------------------------------------------------------- */

/* One of the three maps stacked on the screen (0x42b102). */
typedef struct {
    uint32_t *cells;       /* 0x593270 (1, Hell) / 0x5bb270 (2) / 0x6265b0 (3, Heaven) */
    uint8_t *objmap;       /* 0x5f4cf0 / 0x5f25c0 / 0x5f7ee0 */
    int32_t y_offset;      /* zoom-0 pixels: 0x708 / 0x3a4 / 0x40 */
    int32_t margin_bottom; /* 0x1cc / 0x80 / 0x1cc */
    int32_t margin_top;    /* 0x48 */
    uint8_t flags;         /* view flags while this plane is selected (0x65ed98 / a0 / 9c) */
} MapPlane;

typedef struct {
    MapPlane plane[4]; /* 1..3 */
    int32_t scroll_y;  /* 0x63051c */
} MapPlanes;

/* Fills y_offset and the margins with the original's values. */
void map_planes_init(MapPlanes *p);
/* 0x42b102: saves the view's flags into the current plane and switches to
 * `plane` (cells, objmap, origin_y = scaled offset + scroll_y, margins,
 * flags). The original also points the simulation globals at the plane. */
void map_view_select_plane(MapView *v, MapPlanes *p, int plane);

/* 0x4ebd8a: redraws map cells mx..mx+w-1 x my..my+h-1 (map coordinates),
 * `extra` zoom-0 pixels above them, on the current plane and the planes
 * drawn after it. Returns the screen rectangle touched (left, right, top,
 * bottom; empty if none). */
GfxRect map_view_redraw_cells(GfxSurface *dst, MapView *v, MapPlanes *p, const GfxRect *bounds, int mx, int my, int w,
                              int h, int extra);
/* 0x4076f5: repaints x, y, w, h: background (colour 0x13, or `backdrop`
 * tiled), then planes 1, 2, 3, each with the highlight taken off, the map
 * drawn and the highlight put back. Keeps the selected plane. */
void map_view_paint(GfxSurface *dst, MapView *v, MapPlanes *p, const GfxRect *bounds, const GfxSurface *backdrop,
                    int x, int y, int w, int h);

/* The moving map object's area (0x64e4f0 / 0x64e4f4 / 0x64eba0): while
 * set, deferred buildings inside it are left to the object. -1, -1, 0 =
 * none (the default). */
void map_view_set_focus(int x, int y, int size);

/* Called every 300 cells while drawing (the original runs snd_idle and
 * another idle task there so audio keeps streaming). */
void map_view_set_idle(void (*fn)(void));

#endif
