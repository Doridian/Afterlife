/*
 * Software drawing primitives on 8-bit surfaces: rectangle helpers
 * (0x4d1066-0x4d13fc), fills, frames and lines ("gr", 0x4d08e0-0x4d0eb8),
 * and the Di layer's memory-only blits (0x4cc827, 0x4cc906).
 *
 * Rectangles are GfxRect {left, right, top, bottom}, inclusive. Colors are
 * palette indices. Drawing functions accept NULL for the screen (port-only;
 * the originals require a surface).
 */
#ifndef AFTERLIFE_GFX_DRAW_H
#define AFTERLIFE_GFX_DRAW_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/surface.h"

void gfx_rect_normalize(GfxRect *r);                        /* 0x4d1066 */
GfxRect *gfx_rect_intersect(GfxRect *r, const GfxRect *with); /* 0x4d12d3, in place */
bool gfx_rect_contains(const GfxRect *r, int x, int y);     /* 0x4d1353 */
void gfx_rect_copy(GfxRect *dst, const GfxRect *src);       /* 0x4d13aa */
void gfx_rect_set(GfxRect *r, int left, int right, int top, int bottom); /* 0x4d13fc */

/* 0x4d08e0 gr_fill_rect: solid w x h at (x, y), clipped to the surface. */
void gfx_fill_rect(GfxSurface *s, int x, int y, int w, int h, uint8_t color);
/* 0x4d09bb gr_frame_rect: one-pixel outline of `r`, which is normalized and
 * clamped to the surface in place (the caller sees the change). */
void gfx_frame_rect(GfxSurface *s, GfxRect *r, uint8_t color);
/* 0x4d0b34 gr_dotted_frame: outline of w x h at (x, y) where bit 7..0 of
 * `pattern` (repeating) says which pixels are drawn; used for the dashed
 * selection box. */
void gfx_dotted_frame(GfxSurface *s, int x, int y, int w, int h, uint8_t color, uint8_t pattern);
/* 0x4d0cd1 gr_line: line from (x0, y0) to (x1, y1) inclusive, per-pixel
 * clipped. Note the original's argument order: x0, x1, y0, y1. */
void gfx_line(GfxSurface *s, int x0, int x1, int y0, int y1, uint8_t color);
/* 0x4d0eb8 gr_bevel_rect: filled 3D box, `light` on the top/left edges and
 * `dark` on the bottom/right (swapped when sunken). */
void gfx_bevel_rect(GfxSurface *s, const GfxRect *r, uint8_t light, uint8_t fill, uint8_t dark, bool sunken);

/* 0x4cc827 di_blit_transparent: like gfx_blit, but source pixels equal to
 * `key` are skipped (sprite blit with a color key). */
void gfx_blit_transparent(GfxSurface *dst, int dx, int dy, int w, int h, const GfxSurface *src, int sx, int sy,
                          uint8_t key);
/* 0x4cc906 di_tile_fill: fills w x h at dst(dx, dy) with the tw x th tile
 * at src(sx, sy), repeated from the top-left corner. */
void gfx_tile_fill(GfxSurface *dst, int dx, int dy, int w, int h, const GfxSurface *src, int sx, int sy, int tw,
                   int th);

#endif
