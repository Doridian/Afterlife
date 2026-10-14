/*
 * Palette management: the Di layer's shadow palette (0x545c38), color
 * matching, and palette cycling (pcyc_*, 0x4ceba0-0x4cf2a2).
 *
 * The original runs on a 256-color desktop with an identity logical palette.
 * Entries 0-9 and 246-255 are the Windows static colors and are never
 * touched; the game owns 10-245 (GFX_PAL_FIRST..GFX_PAL_LAST), marked
 * PC_RESERVED so it can AnimatePalette them. Every DIB's color table is
 * refreshed from the shadow palette before each blit, so surfaces are plain
 * index buffers and changing the palette recolors everything on screen.
 *
 * Palette buffers passed around by the game are 256 four-byte entries
 * R, G, B, x (x is ignored on input).
 */
#ifndef AFTERLIFE_GFX_PALETTE_H
#define AFTERLIFE_GFX_PALETTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GFX_PAL_FIRST 10
#define GFX_PAL_LAST 245
#define GFX_PAL_COUNT (GFX_PAL_LAST - GFX_PAL_FIRST + 1) /* 0xec */

/* Shadow palette flags (the PALETTEENTRY peFlags byte). */
#define GFX_PALF_RESERVED 1 /* PC_RESERVED */
#define GFX_PALF_CYCLING 2  /* in a cycling range: skipped by color matching */

/* 0x4cb67a di_create_identity_palette + the static colors: resets the
 * shadow palette (game range to a grey ramp, statics to the Windows defaults)
 * and builds the color-matching tables (end of di_initialize). */
void gfx_palette_init(void);

/* 0x4cacc7 di_set_palette: sets entries first..first+count-1 (clamped to
 * the game range) from `rgbx` (indexed from 0, so the caller passes the whole
 * 256-entry table) and pushes them to the screen. rgbx NULL re-pushes the
 * shadow palette (WM_ACTIVATE does this). */
void gfx_palette_set(const uint8_t *rgbx, unsigned first, unsigned count);
/* 0x4caece di_get_palette: copies entries first..first+count-1 into rgbx
 * (indexed from 0, x bytes untouched). */
void gfx_palette_get(uint8_t *rgbx, unsigned first, unsigned count);
/* Shadow palette flags of one entry (port-only accessor). */
uint8_t gfx_palette_flags(unsigned index);

/* 0x4cb433 di_closest_color: best match in the game range by weighted
 * squared distance (30 R, 60 G, 20 B), skipping cycling entries. */
uint8_t gfx_palette_closest(uint8_t r, uint8_t g, uint8_t b);

/* 0x4cc3d4 di_rotate_palette: rotates entries first..first+count-1 by one
 * (dir >= 0: towards higher indices) and pushes them. */
void gfx_palette_rotate(unsigned first, unsigned count, int dir);

/* 0x4cec6d pal_from_rgb: malloc'd R,G,B,x table from 256 packed RGB
 * triples (768 bytes). */
uint8_t *gfx_palette_from_rgb(const uint8_t *rgb);
/* 0x4ced05 pal_load_resource: decodes a palette resource (256 B,G,R,x
 * entries followed by 15 eight-byte cycling records; resource 33 is the
 * game palette) into a malloc'd R,G,B,x table, and sets up cycling ranges
 * 0-14 from it. NULL if len is too short. */
uint8_t *gfx_palette_load_resource(const uint8_t *data, size_t len);

/* --- shade tables (0x64cf10) --------------------------------------------- */

#define GFX_SHADE_TABLES 13

/* 0x4ceee9 pal_build_shade_table: table[i] = i, then for i in lo..hi the
 * closest color (within match_lo..match_hi) to rgbx[i] scaled by r/g/b
 * (256 = unchanged); near-black entries are replaced by (r, g, b) / 4. */
void gfx_palette_build_shade_table(unsigned table, const uint8_t *rgbx, int r, int g, int b, uint8_t lo, uint8_t hi,
                                   uint8_t match_lo, uint8_t match_hi);
const uint8_t *gfx_palette_shade_table(unsigned table);
/* 0x4cf2a2 pal_closest_in_range: like gfx_palette_closest, but over
 * rgbx[lo..hi] at 6-bit precision. */
uint8_t gfx_palette_closest_in_range(const uint8_t *rgbx, int r, int g, int b, uint8_t lo, uint8_t hi);

/* 0x4c7d79 pal_build_highlight_remap: remap[i] = closest color to entry i
 * brightened by 20%. Used to draw highlighted (hovered) UI. */
void gfx_palette_build_highlight(void);
const uint8_t *gfx_palette_highlight(void); /* 0x535318 */

/* --- palette cycling (pcyc_*) --------------------------------------------- */

#define GFX_CYCLE_RANGES 100

/* 0x4ceba0 pcyc_initialize: clears all ranges. */
void gfx_cycle_init(void);
/* 0x4cec02 pcyc_set_range: range `slot` rotates count entries from first
 * every |rate| ticks (100 ms each), towards higher indices if rate >= 0. */
bool gfx_cycle_set(unsigned slot, unsigned first, unsigned count, int rate);
void gfx_cycle_stop_all(void);          /* 0x4cf042 */
void gfx_cycle_start_all(void);         /* 0x4cf089 */
void gfx_cycle_disable(unsigned slot);  /* 0x4cf0d0 */
void gfx_cycle_enable(unsigned slot);   /* 0x4cf0ec */
/* 0x4cf108 pcyc_idle: call from the main loop; advances due ranges. */
void gfx_cycle_idle(void);

#endif
