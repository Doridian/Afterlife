/*
 * Display interface ("Di", around 0x4ca1c0-0x4cd892): the 8-bit surfaces the
 * game draws into and the path that puts them on screen.
 *
 * The original keeps every off-screen bitmap in an 8-bit top-down DIB section
 * (di_create_surface) and copies rectangles between them, or onto the main
 * window, with GDI BitBlt (di_blit). A NULL surface means "the window". There
 * is no back buffer of the whole screen: the window manager (wm_*, 0x4c1580)
 * keeps one surface per window and repaints dirty rectangles of the window
 * DC from those.
 *
 * The port keeps the same model, except that "the window" is the platform's
 * 8-bit framebuffer (gfx_screen()). Blits to it accumulate a dirty rectangle;
 * gfx_present() hands the framebuffer to the platform when something changed.
 *
 * See docs/rendering.md.
 */
#ifndef AFTERLIFE_GFX_SURFACE_H
#define AFTERLIFE_GFX_SURFACE_H

#include <stdbool.h>
#include <stdint.h>

/* The original's 20-byte surface: width, height, stride (width rounded up
 * to 4), bits, HBITMAP. Rows are top-down. */
typedef struct GfxSurface {
    int32_t width;
    int32_t height;
    int32_t stride;
    uint8_t *pixels;
    bool owns_pixels; /* port-only: false for the screen wrapper */
} GfxSurface;

/* Rectangles in the original are four inclusive ints in this order. */
typedef struct GfxRect {
    int32_t left, right, top, bottom;
} GfxRect;

/* gfx_init flags (di_display_flags, 0x545428) */
#define GFX_DISPLAY_FULL 1      /* resolution equals the desktop */
#define GFX_DISPLAY_WINDOWED 2  /* smaller than the desktop: framed window */
#define GFX_DISPLAY_FORCE 4     /* INI "Force display settings" = ON */

/* Parses the INI "Resolution" value the way di_initialize does: "MAX" or a
 * missing value means the desktop size, otherwise "%dx%d". The result is
 * clamped to the desktop (when the platform knows it). Returns false if the
 * value doesn't parse, leaving the desktop size in *w, *h. */
bool gfx_parse_resolution(const char *value, int *w, int *h);

/* 0x4ca1c0 di_initialize, minus the INI reads and the memory warnings: opens
 * the video mode at width x height (plat_init must have run), installs the
 * identity palette and the color-matching tables, and clips the mouse to the
 * screen. `force` is INI "Force display settings" = "ON" (a real mode switch,
 * here: fullscreen). Returns false on failure. */
bool gfx_init(int width, int height, bool force);
/* 0x4ca6ac di_shutdown */
void gfx_shutdown(void);
/* Port-only (the port window, game/display.c): the screen becomes width x
 * height (cleared to black), the mouse clip the whole screen. The window
 * manager and the game's windows are not touched. */
bool gfx_screen_resize(int width, int height);

int gfx_screen_width(void);  /* 0x64dc50 */
int gfx_screen_height(void); /* 0x64dc54 */
unsigned gfx_display_flags(void);
/* The framebuffer as a surface (port-only). Blits with dst/src NULL use it. */
GfxSurface *gfx_screen(void);

/* 0x4cafbb di_create_surface: zero-filled width x height surface. */
GfxSurface *gfx_surface_create(int width, int height);
/* 0x4cb244 di_destroy_surface */
void gfx_surface_destroy(GfxSurface *s);
/* Port-only: total bytes in live surfaces (0x672930). */
long gfx_surface_bytes(void);

/* 0x4ca737 di_blit: copies w x h from src(sx, sy) to dst(dx, dy). Either may
 * be NULL for the screen. Clipped to both surfaces like BitBlt. */
void gfx_blit(GfxSurface *dst, int dx, int dy, int w, int h, const GfxSurface *src, int sx, int sy);
/* 0x4ca905 di_stretch_blit (unused by the game): StretchBlt, nearest. */
void gfx_stretch_blit(GfxSurface *dst, int dx, int dy, int dw, int dh, const GfxSurface *src, int sx, int sy,
                      int sw, int sh);
/* 0x4caa9c di_tile_blit: fills dst(dx, dy, w, h) with copies of the tw x th
 * tile at src(sx, sy). */
void gfx_tile_blit(GfxSurface *dst, int dx, int dy, int w, int h, const GfxSurface *src, int sx, int sy, int tw,
                   int th);
/* 0x4cb290 di_scroll_rect: ScrollDC of `rect` (inclusive) by dx, dy within
 * itself. Returns the uncovered area that needs repainting (0x546490). */
GfxRect gfx_scroll_rect(GfxSurface *s, const GfxRect *rect, int dx, int dy);

/* Port-only helpers for drawing code: the surface itself, or the screen for
 * NULL; and gfx_touch marks [left..right] x [top..bottom] dirty if `s` is
 * the screen. */
GfxSurface *gfx_target(GfxSurface *s);
void gfx_touch(const GfxSurface *s, int left, int right, int top, int bottom);

/* Marks part of the screen as changed (port-only; the original drew straight
 * to the window DC). gfx_blit to the screen does this itself. */
void gfx_invalidate(const GfxRect *r);
/* Shows the framebuffer if anything was drawn to it since the last call
 * (port-only: the role GDI's immediate window drawing played). */
void gfx_present(void);

/* 0x4cb3fa di_set_paint_handler: the WM_PAINT callback (the window manager
 * installs wm_paint, 0x4c568f). Called with the inclusive rectangle to
 * repaint. Returns the previous handler. */
typedef void (*GfxPaintHandler)(const GfxRect *r);
GfxPaintHandler gfx_set_paint_handler(GfxPaintHandler fn);
/* WM_PAINT for the whole screen (port-only: after a mode change or expose). */
void gfx_repaint(void);

/* 0x4ce09f rc_load_bmp_resource: loads an 8-bit BMP resource into a new
 * surface. If palette is non-NULL, *palette gets a malloc'd 256-entry R,G,B,x
 * table (the caller frees it). NULL if the resource is missing or not 8-bit
 * (fatal errors in the original). */
GfxSurface *gfx_load_bmp_resource(uint32_t id, uint8_t **palette);

#endif
