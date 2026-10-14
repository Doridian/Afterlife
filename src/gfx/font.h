/*
 * Bitmap fonts (0x4cf400-0x4d056e). Up to 10 fonts are cached by resource
 * ID; slot 0 is a built-in font (ID 0xf0000001) linked into ALIFE.EXE, the
 * rest are loaded from the bundle on first use (the game preloads 500 and
 * 501). Text is drawn straight into an 8-bit surface.
 *
 * Font data (offsets from the start of the resource):
 *   +0x06  colormap: palette index for each pixel value (0 = transparent)
 *   +0x15  bits per pixel (1, 2, 4 or 8)
 *   +0x16  line height
 *   +0x19  256 x u32 glyph offsets, relative to +0x15
 * Glyph: width, height, x offset (s8), y offset (s8), then the pixels,
 * MSB first, rows packed without padding.
 */
#ifndef AFTERLIFE_GFX_FONT_H
#define AFTERLIFE_GFX_FONT_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/surface.h"

#define GFX_FONT_BUILTIN 0xf0000001u
#define GFX_FONT_SLOTS 10

/* gfx_font_draw_text flags */
#define GFX_TEXT_CENTER 0x01   /* x = center of the clip rectangle */
#define GFX_TEXT_NOJUSTIFY 0x10 /* spaces keep their width (default: lines
                                   that wrap are justified) */
#define GFX_TEXT_SET_BOTTOM 0x20 /* area->bottom = y after the last line + 1 */

/* 0x4cf400 font_initialize: slot 0 = the built-in font (read from the
 * user's ALIFE.EXE; missing if it can't be), selected. */
void gfx_font_init(void);
void gfx_font_shutdown(void); /* 0x4cf444 (port: frees the cache) */
/* 0x4cf456 font_load: caches resource `id` in a free slot. */
bool gfx_font_load(uint32_t id);
/* Port-only: puts font data the caller owns into a slot (tests, or a font
 * from somewhere other than the bundle). */
bool gfx_font_register(uint32_t id, const uint8_t *data, uint32_t size);
/* 0x4cf504 font_select: makes `id` current (loading it if needed). Returns
 * the previously current ID, or 0 if `id` couldn't be loaded. */
uint32_t gfx_font_select(uint32_t id);
uint32_t gfx_font_current(void); /* 0x4cf559 */

/* 0x4d0102 font_set_colors: colormap[first + i] = colors[i] for the font
 * `id` (0 = current); ignored unless first + count < 15. */
void gfx_font_set_colors(uint32_t id, unsigned first, unsigned count, const unsigned *colors);
/* 0x4d01d3 / 0x4d02a8: copy up to n colormap entries out of / into font
 * `id` (0 = current). The limit is 1 << (bpp - 1), as in the original. */
unsigned gfx_font_get_colormap(uint32_t id, unsigned n, uint8_t *out);
void gfx_font_set_colormap(uint32_t id, unsigned n, const uint8_t *in);

int gfx_font_char_width(uint8_t ch);       /* 0x4d0361: glyph width (tab 8) */
int gfx_font_char_height(uint8_t ch);      /* 0x4d041f */
int gfx_font_text_width(const char *str);  /* 0x4d045e: sum of advances */
int gfx_font_height(uint32_t id);          /* 0x4d050d: line height, 0 = current */

/* 0x4cf56e font_draw_char: draws one glyph of the current font with its
 * origin at (x, y), clipped to `clip` (NULL: the surface). Returns the
 * advance (x offset + drawn width). */
int gfx_font_draw_char(GfxSurface *s, uint8_t ch, int x, int y, const GfxRect *clip);
/* 0x4cf9f0 font_draw_text: draws `str` from (x, y), breaking lines at
 * '\n' / '\r' and at the right edge of `clip` (NULL: the surface); each line
 * advances y by the line height + 1. Glyphs are clipped to `area` (NULL:
 * clip). '&' underlines the next character, tab is 8 pixels. Returns the
 * measured width + 1 (0 if nothing could be drawn). */
int gfx_font_draw_text(GfxSurface *s, int x, int y, const GfxRect *clip, GfxRect *area, const char *str,
                       unsigned flags);
/* 0x4cff0e font_draw_wrapped: word-wraps `str` into `rect` with font `id`
 * in colors c1 / c2 (colormap entries 1 and 2), one line per font height. */
void gfx_font_draw_wrapped(GfxSurface *s, const GfxRect *rect, const char *str, uint32_t id, uint8_t c1,
                           uint8_t c2);

#endif
