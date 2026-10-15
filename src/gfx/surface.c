/*
 * Display interface: surfaces, blits, the screen. See surface.h.
 */
#include "gfx/surface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/resource.h"
#include "gfx/mouse.h"
#include "gfx/palette.h"
#include "platform/platform.h"

static GfxSurface g_screen;           /* wraps the platform framebuffer */
static int g_desktop_w, g_desktop_h;  /* 0x64e390 / 0x64e394 */
static unsigned g_display_flags;      /* 0x545428 */
static long g_surface_bytes;          /* 0x672930 */
static GfxPaintHandler g_paint;       /* 0x672924 */
static GfxRect g_dirty;
static bool g_dirty_valid;

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static void desktop_size(int *w, int *h)
{
    plat_video_desktop_size(w, h);
    /* Unknown (e.g. headless): don't limit the resolution. */
    if (*w <= 0 || *h <= 0) {
        *w = 0x7fff;
        *h = 0x7fff;
    }
}

static void refresh_screen(void)
{
    g_screen.width = plat_video_width();
    g_screen.height = plat_video_height();
    g_screen.stride = plat_video_pitch(); /* the platform row pitch, not the width */
    g_screen.pixels = plat_video_framebuffer();
    g_screen.owns_pixels = false;
}

/* The INI half of 0x4ca1c0. */
bool gfx_parse_resolution(const char *value, int *w, int *h)
{
    int dw, dh;
    desktop_size(&dw, &dh);
    *w = dw;
    *h = dh;
    bool ok = true;
    if (value && strcmp(value, "MAX") != 0) {
        int pw, ph;
        if (sscanf(value, "%dx%d", &pw, &ph) == 2 && pw > 0 && ph > 0) {
            *w = pw;
            *h = ph;
        } else {
            ok = false;
        }
    }
    if (*w > dw)
        *w = dw;
    if (*h > dh)
        *h = dh;
    return ok;
}

/* 0x4ca1c0. The original also checks the desktop color depth against INI
 * "Color depth", warns about low memory, and with "Force display settings"
 * insists the resolution is one EnumDisplaySettings offers (fatal
 * "DiInitialize" error otherwise). It then creates the identity palette
 * (0x4cb67a), clears the system palette (0x4cb54c), creates the main window
 * at the resolution (0x4cd73a) and two memory DCs for blitting. */
bool gfx_init(int width, int height, bool force)
{
    desktop_size(&g_desktop_w, &g_desktop_h);
    if (width <= 0 || height <= 0)
        return false;
    g_display_flags = GFX_DISPLAY_FULL;
    if (force)
        g_display_flags |= GFX_DISPLAY_FORCE;
    else if (width < g_desktop_w || height < g_desktop_h)
        g_display_flags = GFX_DISPLAY_WINDOWED;

    /* The original only switches modes (0x4cd448) when the flags are exactly
     * 4, which di_initialize never produces (forcing gives 1 | 4), so it
     * always runs in a window the size of the resolution. The port treats
     * "force" as fullscreen. */
    if (!plat_video_set_mode(width, height, force))
        return false;
    refresh_screen();
    memset(g_screen.pixels, 0, (size_t)g_screen.stride * (size_t)g_screen.height);

    gfx_palette_init();
    GfxRect clip = {0, width - 1, 0, height - 1};
    gfx_mouse_set_clip_rect(NULL, &clip);
    gfx_invalidate(NULL);
    return true;
}

bool gfx_screen_resize(int width, int height)
{
    if (width <= 0 || height <= 0 || !plat_video_resize(width, height))
        return false;
    refresh_screen();
    memset(g_screen.pixels, 0, (size_t)g_screen.stride * (size_t)g_screen.height);
    GfxRect clip = {0, width - 1, 0, height - 1};
    gfx_mouse_set_clip_rect(NULL, &clip);
    int mx, my;
    gfx_mouse_get_screen_pos(&mx, &my);
    gfx_mouse_on_move(mx, my); /* clamps the position to the new clip */
    gfx_invalidate(NULL);
    return true;
}

/* 0x4ca6ac: restores the display mode (0x4cd448(0, 0)), destroys the window
 * and puts the saved system palette back (0x4cbf16). */
void gfx_shutdown(void)
{
    if (g_display_flags & GFX_DISPLAY_FORCE)
        plat_video_set_mode(g_screen.width, g_screen.height, false);
    g_paint = NULL;
}

int gfx_screen_width(void) { return g_screen.width; }
int gfx_screen_height(void) { return g_screen.height; }
unsigned gfx_display_flags(void) { return g_display_flags; }
GfxSurface *gfx_screen(void) { return &g_screen; }

/* 0x4cafbb. The original creates a DIB section with the current logical
 * palette as its color table and biHeight = -height (top-down). */
GfxSurface *gfx_surface_create(int width, int height)
{
    if (width <= 0 || height <= 0)
        return NULL;
    GfxSurface *s = malloc(sizeof(*s));
    if (!s)
        return NULL;
    s->width = width;
    s->height = height;
    s->stride = (width + 3) & ~3;
    s->pixels = calloc((size_t)s->stride * (size_t)height, 1);
    s->owns_pixels = true;
    if (!s->pixels) {
        free(s);
        return NULL;
    }
    g_surface_bytes += (long)s->stride * height;
    return s;
}

/* 0x4cb244 */
void gfx_surface_destroy(GfxSurface *s)
{
    if (!s)
        return;
    if (s->owns_pixels) {
        g_surface_bytes -= (long)s->stride * s->height;
        free(s->pixels);
        free(s);
    }
}

long gfx_surface_bytes(void) { return g_surface_bytes; }

void gfx_invalidate(const GfxRect *r)
{
    GfxRect full = {0, g_screen.width - 1, 0, g_screen.height - 1};
    if (!r)
        r = &full;
    if (!g_dirty_valid) {
        g_dirty = *r;
        g_dirty_valid = true;
    } else {
        g_dirty.left = imin(g_dirty.left, r->left);
        g_dirty.right = imax(g_dirty.right, r->right);
        g_dirty.top = imin(g_dirty.top, r->top);
        g_dirty.bottom = imax(g_dirty.bottom, r->bottom);
    }
}

GfxSurface *gfx_target(GfxSurface *s) { return s ? s : &g_screen; }

void gfx_touch(const GfxSurface *s, int left, int right, int top, int bottom)
{
    if (s == &g_screen) {
        GfxRect r = {left, right, top, bottom};
        gfx_invalidate(&r);
    }
}

void gfx_present(void)
{
    if (!g_dirty_valid)
        return;
    g_dirty_valid = false;
    plat_video_present();
}

/* BitBlt SRCCOPY between two surfaces, clipped to both. Rows are moved
 * with memmove in an order that is safe when src and dst overlap. */
static void copy_rect(GfxSurface *dst, int dx, int dy, int w, int h, const GfxSurface *src, int sx, int sy)
{
    if (sx < 0) {
        dx -= sx;
        w += sx;
        sx = 0;
    }
    if (sy < 0) {
        dy -= sy;
        h += sy;
        sy = 0;
    }
    if (dx < 0) {
        sx -= dx;
        w += dx;
        dx = 0;
    }
    if (dy < 0) {
        sy -= dy;
        h += dy;
        dy = 0;
    }
    w = imin(w, imin(src->width - sx, dst->width - dx));
    h = imin(h, imin(src->height - sy, dst->height - dy));
    if (w <= 0 || h <= 0)
        return;
    if (dy > sy && dst->pixels == src->pixels) {
        for (int y = h - 1; y >= 0; y--)
            memmove(dst->pixels + (size_t)(dy + y) * dst->stride + dx,
                    src->pixels + (size_t)(sy + y) * src->stride + sx, (size_t)w);
    } else {
        for (int y = 0; y < h; y++)
            memmove(dst->pixels + (size_t)(dy + y) * dst->stride + dx,
                    src->pixels + (size_t)(sy + y) * src->stride + sx, (size_t)w);
    }
    if (dst == &g_screen) {
        GfxRect r = {dx, dx + w - 1, dy, dy + h - 1};
        gfx_invalidate(&r);
    }
}

/* 0x4ca737. Before blitting, the original selects each DIB into its memory
 * DC and refreshes color table entries 10-245 from the shadow palette; with
 * index-only surfaces that is a no-op. */
void gfx_blit(GfxSurface *dst, int dx, int dy, int w, int h, const GfxSurface *src, int sx, int sy)
{
    copy_rect(dst ? dst : &g_screen, dx, dy, w, h, src ? src : &g_screen, sx, sy);
}

/* 0x4ca905 (no callers in the game) */
void gfx_stretch_blit(GfxSurface *dst, int dx, int dy, int dw, int dh, const GfxSurface *src, int sx, int sy,
                      int sw, int sh)
{
    if (!dst)
        dst = &g_screen;
    if (!src)
        src = &g_screen;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
        return;
    for (int y = imax(0, -dy); y < dh && dy + y < dst->height; y++) {
        int yy = sy + (int)((long)y * sh / dh);
        if (yy < 0 || yy >= src->height)
            continue;
        for (int x = imax(0, -dx); x < dw && dx + x < dst->width; x++) {
            int xx = sx + (int)((long)x * sw / dw);
            if (xx >= 0 && xx < src->width)
                dst->pixels[(size_t)(dy + y) * dst->stride + dx + x] = src->pixels[(size_t)yy * src->stride + xx];
        }
    }
    if (dst == &g_screen) {
        GfxRect r = {dx, dx + dw - 1, dy, dy + dh - 1};
        gfx_invalidate(&r);
    }
}

/* 0x4caa9c */
void gfx_tile_blit(GfxSurface *dst, int dx, int dy, int w, int h, const GfxSurface *src, int sx, int sy, int tw,
                   int th)
{
    if (tw <= 0 || th <= 0)
        return;
    for (int x = dx; x < dx + w; x += tw) {
        int cw = x > dx + w - tw ? dx + w - x : tw;
        for (int y = dy; y < dy + h; y += th) {
            int ch = y > dy + h - th ? dy + h - y : th;
            gfx_blit(dst, x, y, cw, ch, src, sx, sy);
        }
    }
}

/* 0x4cb290: ScrollDC(dc, dx, dy, rc, rc, NULL, &update). */
GfxRect gfx_scroll_rect(GfxSurface *s, const GfxRect *rect, int dx, int dy)
{
    if (!s)
        s = &g_screen;
    /* Clip the scroll rectangle to the surface, as the DC would. */
    int l = imax(rect->left, 0), t = imax(rect->top, 0);
    int r = imin(rect->right + 1, s->width), b = imin(rect->bottom + 1, s->height);
    GfxRect upd = {0, -1, 0, -1};
    if (l >= r || t >= b || (dx == 0 && dy == 0))
        return upd;
    /* Move the part of the rectangle that stays inside it. */
    int w = (r - l) - (dx < 0 ? -dx : dx), h = (b - t) - (dy < 0 ? -dy : dy);
    if (w > 0 && h > 0) {
        int sx = dx >= 0 ? l : l - dx, sy = dy >= 0 ? t : t - dy;
        copy_rect(s, sx + dx, sy + dy, w, h, s, sx, sy);
    }
    /* The update rectangle bounds the uncovered area (an L shape when both
     * offsets are set, i.e. the whole rectangle). */
    int ul = l, ur = r, ut = t, ub = b;
    if (dy == 0 && w > 0) {
        if (dx > 0)
            ur = l + dx;
        else
            ul = r + dx;
    } else if (dx == 0 && h > 0) {
        if (dy > 0)
            ub = t + dy;
        else
            ut = b + dy;
    }
    upd = (GfxRect){ul, ur - 1, ut, ub - 1};
    if (s == &g_screen) {
        GfxRect all = {l, r - 1, t, b - 1};
        gfx_invalidate(&all);
    }
    return upd;
}

/* 0x4cb3fa */
GfxPaintHandler gfx_set_paint_handler(GfxPaintHandler fn)
{
    GfxPaintHandler old = g_paint;
    g_paint = fn;
    return old;
}

/* main_wnd_proc (0x4cb7ad) WM_PAINT: BeginPaint, handler(rcPaint), EndPaint.
 * rcPaint is passed as {left, right, top, bottom} exclusive-right/bottom
 * values; the handler treats them as inclusive. */
void gfx_repaint(void)
{
    if (g_paint) {
        GfxRect r = {0, g_screen.width, 0, g_screen.height};
        g_paint(&r);
    }
    gfx_invalidate(NULL);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* 0x4ce09f. Reads the headers, the color table, then the rows bottom-up
 * straight after it (bfOffBits is not used). */
GfxSurface *gfx_load_bmp_resource(uint32_t id, uint8_t **palette)
{
    PlatFile *f = rc_open_resource(id);
    if (!f) {
        fprintf(stderr, "HILEVEL ERROR RcLoadBMPResource: Unable to open resource ID %u\n", (unsigned)id);
        return NULL;
    }
    uint8_t fh[14], ih[40];
    if (plat_file_read(f, fh, 14, 1) != 1 || fh[0] != 'B' || fh[1] != 'M') {
        plat_file_close(f);
        fprintf(stderr, "HILEVEL ERROR RcLoadBMPResource: Resource ID %u is not a BMP file\n", (unsigned)id);
        return NULL;
    }
    if (plat_file_read(f, ih, 40, 1) != 1 || (ih[14] | ih[15] << 8) != 8) {
        plat_file_close(f);
        fprintf(stderr, "HILEVEL ERROR RcLoadBMPResource: Bitmap is not a 256 color image\n");
        return NULL;
    }
    int32_t width = (int32_t)rd32(ih + 4), height = (int32_t)rd32(ih + 8);
    uint32_t colors = rd32(ih + 32);
    if (colors == 0 || colors > 256)
        colors = 256;
    if (palette) {
        uint8_t *pal = calloc(256, 4);
        for (uint32_t i = 0; pal && i < colors; i++) {
            uint8_t q[4] = {0};
            plat_file_read(f, q, 4, 1);
            pal[i * 4] = q[2];
            pal[i * 4 + 1] = q[1];
            pal[i * 4 + 2] = q[0];
            pal[i * 4 + 3] = q[3];
        }
        *palette = pal;
    } else {
        plat_file_seek(f, (long)colors * 4, 1);
    }
    GfxSurface *s = gfx_surface_create(width, height);
    if (s) {
        for (int y = s->height - 1; y >= 0; y--)
            plat_file_read(f, s->pixels + (size_t)y * s->stride, 1, (size_t)s->stride);
    }
    plat_file_close(f);
    return s;
}
