/*
 * Software drawing primitives. See draw.h.
 *
 * The originals do little or no clipping (di_blit_transparent and
 * di_tile_fill none at all; gr_fill_rect loops forever on a rectangle that
 * clips to nothing). The port clips everything, which changes nothing for
 * calls that stayed inside the surface.
 */
#include "gfx/draw.h"

#include <stddef.h>

static int imin(int a, int b) { return a < b ? a : b; }
static int iabs(int v) { return v < 0 ? -v : v; }

/* 0x4d1066 */
void gfx_rect_normalize(GfxRect *r)
{
    if (r->right < r->left) {
        int t = r->right;
        r->right = r->left;
        r->left = t;
    }
    if (r->bottom < r->top) {
        int t = r->bottom;
        r->bottom = r->top;
        r->top = t;
    }
}

/* 0x4d12d3 */
GfxRect *gfx_rect_intersect(GfxRect *r, const GfxRect *with)
{
    if (with->left > r->left)
        r->left = with->left;
    if (with->top > r->top)
        r->top = with->top;
    if (with->right < r->right)
        r->right = with->right;
    if (with->bottom < r->bottom)
        r->bottom = with->bottom;
    return r;
}

/* 0x4d1353 */
bool gfx_rect_contains(const GfxRect *r, int x, int y)
{
    return x >= r->left && x <= r->right && y >= r->top && y <= r->bottom;
}

/* 0x4d13aa */
void gfx_rect_copy(GfxRect *dst, const GfxRect *src)
{
    if (dst && src)
        *dst = *src;
}

/* 0x4d13fc */
void gfx_rect_set(GfxRect *r, int left, int right, int top, int bottom)
{
    if (r)
        *r = (GfxRect){left, right, top, bottom};
}

/* Clips (x, y, w, h) to the surface; false if nothing is left. */
static bool clip_xywh(const GfxSurface *s, int *x, int *y, int *w, int *h)
{
    if (*x < 0) {
        *w += *x;
        *x = 0;
    }
    if (*x + *w >= s->width)
        *w = s->width - *x;
    if (*y < 0) {
        *h += *y;
        *y = 0;
    }
    if (*y + *h >= s->height)
        *h = s->height - *y;
    return *w > 0 && *h > 0;
}

/* 0x4d08e0 */
void gfx_fill_rect(GfxSurface *s, int x, int y, int w, int h, uint8_t color)
{
    s = gfx_target(s);
    if (!clip_xywh(s, &x, &y, &w, &h))
        return;
    for (int row = 0; row < h; row++) {
        uint8_t *p = s->pixels + (size_t)(y + row) * s->stride + x;
        for (int i = 0; i < w; i++)
            p[i] = color;
    }
    gfx_touch(s, x, x + w - 1, y, y + h - 1);
}

/* 0x4d09bb. The original clamps the right/bottom edge to width/height (one
 * past the last pixel) and only when the left/top edge didn't need
 * clamping; the port clamps both edges to the surface. */
void gfx_frame_rect(GfxSurface *s, GfxRect *r, uint8_t color)
{
    s = gfx_target(s);
    gfx_rect_normalize(r);
    if (r->left < 0)
        r->left = 0;
    if (r->right >= s->width)
        r->right = s->width - 1;
    if (r->top < 0)
        r->top = 0;
    if (r->bottom >= s->height)
        r->bottom = s->height - 1;
    if (r->left > r->right || r->top > r->bottom)
        return;
    int w = r->right - r->left + 1;
    uint8_t *p = s->pixels + (size_t)r->top * s->stride + r->left;
    for (int i = 0; i < w; i++)
        p[i] = color;
    if (r->top != r->bottom) {
        for (int y = r->top + 1; y < r->bottom; y++) {
            p += s->stride;
            p[0] = color;
            p[w - 1] = color;
        }
        p += s->stride;
        for (int i = 0; i < w; i++)
            p[i] = color;
    }
    gfx_touch(s, r->left, r->right, r->top, r->bottom);
}

/* 0x4d0b34: top and bottom edges together, then left and right edges, each
 * pass restarting the pattern at bit 7. */
void gfx_dotted_frame(GfxSurface *s, int x, int y, int w, int h, uint8_t color, uint8_t pattern)
{
    s = gfx_target(s);
    if (w == 0 || h == 0 || !clip_xywh(s, &x, &y, &w, &h))
        return;
    uint8_t *top = s->pixels + (size_t)y * s->stride + x;
    uint8_t *bottom = top + (size_t)(h - 1) * s->stride;
    unsigned mask = 0x80;
    for (int i = 0; i < w; i++) {
        if (mask & pattern)
            top[i] = bottom[i] = color;
        mask = mask >> 1 ? mask >> 1 : 0x80;
    }
    mask = 0x80;
    for (int i = 0; i < h; i++) {
        if (mask & pattern)
            top[(size_t)i * s->stride] = top[(size_t)i * s->stride + w - 1] = color;
        mask = mask >> 1 ? mask >> 1 : 0x80;
    }
    gfx_touch(s, x, x + w - 1, y, y + h - 1);
}

/* 0x4d0cd1. A DDA over the inclusive extent: n = max(|dx|, |dy|) with
 * dx = x1 - x0 +/- 1; n + 1 plots, the first pixel twice. */
void gfx_line(GfxSurface *s, int x0, int x1, int y0, int y1, uint8_t color)
{
    s = gfx_target(s);
    int dx = x1 - x0, dy = y1 - y0;
    dx += dx > 0 ? 1 : dx < 0 ? -1 : 0;
    dy += dy > 0 ? 1 : dy < 0 ? -1 : 0;
    int sx = dx > 0 ? 1 : dx < 0 ? -1 : 0, sy = dy > 0 ? 1 : dy < 0 ? -1 : 0;
    int ax = iabs(dx), ay = iabs(dy), n = ax > ay ? ax : ay;
    int ex = 0, ey = 0, x = x0, y = y0;
    for (int i = 0; i <= n; i++) {
        if (y >= 0 && y < s->height && x >= 0 && x < s->width)
            s->pixels[(size_t)y * s->stride + x] = color;
        ex += ax;
        ey += ay;
        if (ex > n) {
            ex -= n;
            x += sx;
        }
        if (ey > n) {
            ey -= n;
            y += sy;
        }
    }
    gfx_touch(s, imin(x0, x1), x0 > x1 ? x0 : x1, imin(y0, y1), y0 > y1 ? y0 : y1);
}

/* 0x4d0eb8 */
void gfx_bevel_rect(GfxSurface *s, const GfxRect *r, uint8_t light, uint8_t fill, uint8_t dark, bool sunken)
{
    gfx_fill_rect(s, r->left, r->top, r->right - r->left + 1, r->bottom - r->top + 1, fill);
    uint8_t tl = sunken ? dark : light, br = sunken ? light : dark;
    gfx_line(s, r->left, r->right, r->top, r->top, tl);
    gfx_line(s, r->left, r->left, r->top, r->bottom, tl);
    gfx_line(s, r->right, r->right, r->top + 1, r->bottom, br);
    gfx_line(s, r->left + 1, r->right, r->bottom, r->bottom, br);
}

/* Clips a dst/src rectangle pair to both surfaces. */
static bool clip_blit(const GfxSurface *dst, int *dx, int *dy, int *w, int *h, const GfxSurface *src, int *sx,
                      int *sy)
{
    int ox = 0, oy = 0;
    if (*dx < 0)
        ox = -*dx;
    if (*sx + ox < 0)
        ox = -*sx;
    if (*dy < 0)
        oy = -*dy;
    if (*sy + oy < 0)
        oy = -*sy;
    *dx += ox;
    *sx += ox;
    *w -= ox;
    *dy += oy;
    *sy += oy;
    *h -= oy;
    *w = imin(*w, imin(dst->width - *dx, src->width - *sx));
    *h = imin(*h, imin(dst->height - *dy, src->height - *sy));
    return *w > 0 && *h > 0;
}

/* 0x4cc827 */
void gfx_blit_transparent(GfxSurface *dst, int dx, int dy, int w, int h, const GfxSurface *src, int sx, int sy,
                          uint8_t key)
{
    dst = gfx_target(dst);
    src = gfx_target((GfxSurface *)src);
    if (!clip_blit(dst, &dx, &dy, &w, &h, src, &sx, &sy))
        return;
    for (int y = 0; y < h; y++) {
        uint8_t *d = dst->pixels + (size_t)(dy + y) * dst->stride + dx;
        const uint8_t *p = src->pixels + (size_t)(sy + y) * src->stride + sx;
        for (int x = 0; x < w; x++) {
            if (p[x] != key)
                d[x] = p[x];
        }
    }
    gfx_touch(dst, dx, dx + w - 1, dy, dy + h - 1);
}

/* 0x4cc906. The tile itself is assumed to lie inside src (as in the
 * original); the destination is clipped, keeping the tile phase. */
void gfx_tile_fill(GfxSurface *dst, int dx, int dy, int w, int h, const GfxSurface *src, int sx, int sy, int tw,
                   int th)
{
    dst = gfx_target(dst);
    src = gfx_target((GfxSurface *)src);
    if (tw <= 0 || th <= 0 || sx < 0 || sy < 0 || sx + tw > src->width || sy + th > src->height)
        return;
    int x0 = dx < 0 ? -dx : 0, y0 = dy < 0 ? -dy : 0;
    int x1 = imin(w, dst->width - dx), y1 = imin(h, dst->height - dy);
    for (int y = y0; y < y1; y++) {
        uint8_t *d = dst->pixels + (size_t)(dy + y) * dst->stride + dx;
        const uint8_t *p = src->pixels + (size_t)(sy + y % th) * src->stride + sx;
        for (int x = x0; x < x1; x++)
            d[x] = p[x % tw];
    }
    if (x1 > x0 && y1 > y0)
        gfx_touch(dst, dx + x0, dx + x1 - 1, dy + y0, dy + y1 - 1);
}
