/*
 * The isometric map view. See map_view.h and docs/map.md.
 */
#include "gfx/map_view.h"

#include <string.h>

#include "gfx/draw.h"

static void (*g_idle)(void);
static int g_idle_count; /* 0x4ea0b6's local counter */

/* The object being moved/redrawn (0x64e4f0 x, 0x64e4f4 y, 0x64eba0 size; -1,
 * -1, 0 when none). Set by the object code (FUN_004eedc1), which isn't
 * ported. */
static int32_t g_focus_x = -1, g_focus_y = -1, g_focus_size = 0;

void map_view_set_idle(void (*fn)(void)) { g_idle = fn; }

/* --- coordinates ----------------------------------------------------------- */

/* The rotation as the original writes it (0x4eb79e, 0x4ea0b6's cell
 * lookup). */
void map_view_to_logical(const MapView *v, int mx, int my, int *li, int *lj)
{
    if (v->dir_x < 1) {
        if (v->dir_y < 1) {
            *li = v->height - mx - 1;
            *lj = v->width - my - 1;
        } else {
            *li = my;
            *lj = v->height - mx - 1;
        }
    } else if (v->dir_y < 1) {
        *li = v->width - my - 1;
        *lj = mx;
    } else {
        *li = mx;
        *lj = my;
    }
}

void map_view_from_logical(const MapView *v, int li, int lj, int *mx, int *my)
{
    if (v->dir_x < 1) {
        if (v->dir_y < 1) {
            *mx = v->width - li - 1;
            *my = v->height - lj - 1;
        } else {
            *mx = v->height - lj - 1;
            *my = li;
        }
    } else if (v->dir_y < 1) {
        *mx = lj;
        *my = v->width - li - 1;
    } else {
        *mx = li;
        *my = lj;
    }
}

/* 0x4eb79e */
void map_view_cell_to_screen(const MapView *v, int mx, int my, int *sx, int *sy)
{
    int i, j;
    map_view_to_logical(v, mx, my, &i, &j);
    int hw = mapview_half_w(v), hh = mapview_half_h(v);
    *sx = v->height * hw + (i * hw - j * hw);
    *sy = j * hh + i * hh;
}

/* 0x4eb8c9. i - j = u / hw and i + j = v / hh = 2v / hw. */
void map_view_screen_to_cell(const MapView *v, int sx, int sy, int *mx, int *my)
{
    int hw = mapview_half_w(v);
    int u = sx + (hw * -v->height - v->origin_x);
    int w = sy - v->origin_y;
    int i = (w + u / 2) / hw, j = (w - u / 2) / hw;
    int max_i = v->width - 1, max_j = v->height - 1;
    if (j < 1)
        j = 0;
    if (j > max_j)
        j = max_j;
    if (i < 1)
        i = 0;
    if (i > max_i)
        i = max_i;
    /* back to map coordinates (the original's inverse, for dir < 0 tests) */
    if (v->dir_x < 0) {
        if (v->dir_y < 0) {
            *mx = max_i - i;
            *my = max_j - j;
        } else {
            *mx = max_j - j;
            *my = i;
        }
    } else if (v->dir_y < 0) {
        *mx = j;
        *my = max_i - i;
    } else {
        *mx = i;
        *my = j;
    }
}

/* 0x4ebc33 */
void map_view_cell_rect(const MapView *v, int mx, int my, int w, int h, int32_t out[4])
{
    uint8_t hw = (uint8_t)mapview_half_w(v), hh = (uint8_t)mapview_half_h(v);
    int i, j;
    map_view_to_logical(v, mx, my, &i, &j);
    out[0] = (v->height + i - j) * hw + v->origin_x - hw;
    out[2] = (j + i - v->magnify) * hh + v->origin_y;
    out[1] = (h + w) * hw;
    out[3] = (v->magnify + h + w) * hh;
}

/* 0x4ebbca */
GfxRect map_view_bounds(const MapView *v)
{
    GfxRect r;
    r.left = r.top = 0;
    r.right = (v->width + v->height) * (v->cell_w >> 1) - 1;
    r.bottom = (v->width + v->height) * (v->cell_h >> 1) - 1;
    return r;
}

/* --- traversal -------------------------------------------------------------- */

/* The rows and columns of logical cells that can show inside the clip
 * rectangle, as computed at the top of 0x4ea0b6 / 0x4eaf9d / 0x4ed0f4. */
typedef struct {
    int hw, hh;
    int c0, d0;   /* clip left/right relative to the map's x origin */
    int f4, l14;  /* clip top/bottom relative to the origin, with margins */
    int row_min, row_max;
} Span;

/* Clamps the clip to `bounds`; false if nothing is left. */
static bool clamp_clip(MapView *v, const GfxRect *b)
{
    if (b) {
        if (v->clip_x < b->left)
            v->clip_x = b->left;
        if (v->clip_y < b->top)
            v->clip_y = b->top;
        int r = v->clip_x + v->clip_w - 1, bt = v->clip_y + v->clip_h - 1;
        if (r > b->right)
            r = b->right;
        if (bt > b->bottom)
            bt = b->bottom;
        v->clip_w = r - v->clip_x + 1;
        v->clip_h = bt - v->clip_y + 1;
    }
    return v->clip_w >= 0 && v->clip_h >= 0;
}

/* `margins` is false for the highlight pass (0x4ed0f4 scans without them
 * and without the magnify slack). origin_x must already be shifted left by
 * hw. */
static void span_init(const MapView *v, Span *s, bool margins)
{
    s->hw = mapview_half_w(v);
    s->hh = mapview_half_h(v);
    int l7c = -(v->height * s->hw) - v->origin_x;
    int mt = margins ? (v->margin_top >> v->zoom) << v->magnify : 0;
    int mb = margins ? (v->margin_bottom >> v->zoom) << v->magnify : 0;
    s->c0 = v->clip_x + l7c;
    s->d0 = v->clip_x + v->clip_w - 1 + l7c;
    s->f4 = v->clip_y - v->origin_y - mt;
    s->l14 = mb + v->clip_y + v->clip_h - 1 - v->origin_y;
    int den = s->hh * s->hw * 2;
    s->row_min = (s->f4 * s->hw - s->d0 * s->hh) / den;
    if (s->row_min < 1)
        s->row_min = 0;
    s->row_max = (margins ? v->magnify : 0) + (s->hw * s->l14 - s->c0 * s->hh) / den + 1;
    if (s->row_max > v->height - 1)
        s->row_max = v->height - 1;
}

static void span_row(const MapView *v, const Span *s, int j, bool margins, int *i0, int *i1)
{
    int a = s->f4 / s->hh - j, b = j + s->c0 / s->hw;
    int lo = (a > b ? a : b) - (margins ? v->magnify * 2 + 2 : 2);
    if (lo < 1)
        lo = 0;
    a = s->l14 / s->hh - j;
    b = j + s->d0 / s->hw;
    int hi = (a < b ? a : b) + (margins ? v->magnify : 0) + 1;
    if (hi > v->width - 1)
        hi = v->width - 1;
    *i0 = lo;
    *i1 = hi;
}

/* Index of logical (i, j) in the cell grid and the step to (i + 1, j). */
static ptrdiff_t cell_at(const MapView *v, int i, int j, ptrdiff_t *step)
{
    int W = v->width, H = v->height;
    if (v->dir_x < 1) {
        if (v->dir_y < 1) {
            *step = -1;
            return (ptrdiff_t)(H - j - 1) * W + (W - i - 1);
        }
        *step = W;
        return (ptrdiff_t)i * W + (H - j - 1);
    }
    if (v->dir_y < 1) {
        *step = -W;
        return (ptrdiff_t)(W - i - 1) * W + j;
    }
    *step = 1;
    return (ptrdiff_t)j * W + i;
}

static bool cell_ok(const MapView *v, ptrdiff_t at) { return at >= 0 && at < (ptrdiff_t)v->width * v->height; }

/* The strip of a map object to draw at map cell (mx, my) (0x4ea0b6). */
static int object_strip(const MapView *v, const MapObject *o, int mx, int my)
{
    int size = o->size;
    if (size < 2)
        return 1;
    int dxa = (int8_t)(mx < o->x ? o->x - mx : mx - o->x) + 1;
    int dya = (int8_t)(my < o->y ? o->y - my : my - o->y);
    bool on_y = (v->dir_y < 1) ? o->y - (size - 1) == my : o->y == my;
    if (v->dir_x < 1) {
        if (v->dir_y < 1)
            return on_y ? size - dxa + 1 : size - dya + size - 1;
        return on_y ? size - dxa + 1 : size + dya;
    }
    if (v->dir_y < 1)
        return on_y ? dxa : size - dya + size - 1;
    return on_y ? dxa : size + dya;
}

/* 0x4ea0b6 */
void map_view_draw(GfxSurface *dst, MapView *v, const GfxRect *bounds)
{
    dst = gfx_target(dst);
    if (!dst || !v->cells || v->clip_w == 0 || v->clip_h == 0)
        return;
    /* The focus rectangle in logical cells (l38..d8 x l4c..e8). */
    int W = v->width, fx = g_focus_x, fy = g_focus_y, fs = g_focus_size;
    int l38, l4c;
    if (v->dir_x < 1) {
        if (v->dir_y < 1) {
            l38 = W - (fs + fx - 1) - 1;
            l4c = W - fy - 1;
        } else {
            l38 = fy - fs + 1;
            l4c = W - (fs + fx - 1) - 1;
        }
    } else if (v->dir_y < 1) {
        l38 = W - fy - 1;
        l4c = fx;
    } else {
        l38 = fx;
        l4c = fy - fs + 1;
    }
    tile_draw_focus_min_x = l38;
    tile_draw_focus_min_y = l4c;
    int d8 = l38 + fs - 1, e8 = l4c + fs - 1;

    int hw = mapview_half_w(v), hh = mapview_half_h(v);
    v->origin_x -= hw;
    int32_t save[4] = {v->clip_x, v->clip_y, v->clip_w, v->clip_h};
    if (!clamp_clip(v, bounds)) {
        v->origin_x += hw; /* quirk: the clamped clip stays */
        return;
    }
    Span sp;
    span_init(v, &sp, true);
    bool markers = false;
    for (int j = sp.row_min; j <= sp.row_max; j++) {
        int i0, i1;
        span_row(v, &sp, j, true, &i0, &i1);
        ptrdiff_t step, at = cell_at(v, i0, j, &step);
        int sx = (i0 - j) * hw + v->height * hw + v->origin_x;
        int sy = (j + i0) * hh + v->origin_y;
        for (int i = i0; i <= i1; i++, at += step, sx += hw, sy += hh) {
            if (!cell_ok(v, at))
                continue;
            uint32_t *cell = &v->cells[at];
            bool deferred = false;
            TileDrawResult r = {0, 0, 0, i, j};
            if (v->set_mask != 0) {
                uint32_t c = *cell;
                r = tile_draw(dst, v, i, j, sx, sy, cell, 0, 0, 0, 0, 0);
                if (++g_idle_count > 299) {
                    g_idle_count = 0;
                    if (g_idle)
                        g_idle();
                }
                deferred = (c & CELL_DEFER) != 0;
            }
            uint32_t c = *cell;
            if (!(c & CELL_OBJECT) || v->set_mask == 0) {
                /* deferred buildings outside the focused object: draw plainly */
                if (deferred && r.strip != 0 &&
                    (v->focus_x >= 0 || d8 < r.map_x || r.map_x < l38 || r.map_y < l4c || e8 < r.map_y))
                    tile_draw(dst, v, i, j, sx, sy, NULL, r.set, r.index, r.strip, 0, 0);
            } else {
                int mx, my;
                map_view_from_logical(v, i, j, &mx, &my);
                unsigned id = v->objmap ? v->objmap[(ptrdiff_t)W * my + mx] >> 2 : 0;
                const MapObject *o = v->objects && id < MAP_OBJECTS ? &v->objects[id] : NULL;
                if (o && o->set != r.set)
                    deferred = false;
                if (!o || o->plane == 0 || id == 0) {
                    if (deferred && r.strip != 0)
                        tile_draw(dst, v, i, j, sx, sy, NULL, r.set, r.index, r.strip, 0, 0);
                } else {
                    int strip = object_strip(v, o, mx, my);
                    if (!deferred) {
                        tile_draw(dst, v, i, j, sx, sy, NULL, o->set, o->frame + 1u, (unsigned)strip, 0, 1);
                    } else {
                        /* cross-fade between the building and the object */
                        if (r.strip != 0)
                            tile_draw(dst, v, i, j, sx, sy, NULL, r.set, r.index, r.strip, o->fade, 0);
                        tile_draw(dst, v, i, j, sx, sy, NULL, o->set, o->frame + 1u, (unsigned)strip, 10 - o->fade,
                                  0);
                    }
                }
            }
            if (!(v->flags & MAPVIEW_F_NO_MARKERS) && (*cell & CELL_MARKER))
                markers = true;
        }
    }
    v->clip_x = save[0];
    v->clip_y = save[1];
    v->clip_w = save[2];
    v->clip_h = save[3];
    v->origin_x += hw;
    if (markers)
        map_view_draw_markers(dst, v, bounds);
}

/* 0x4eaf9d */
void map_view_draw_markers(GfxSurface *dst, MapView *v, const GfxRect *bounds)
{
    dst = gfx_target(dst);
    if (!dst || !v->cells || v->clip_w == 0 || v->clip_h == 0)
        return;
    int hw = mapview_half_w(v);
    v->origin_x -= hw;
    int32_t save[4] = {v->clip_x, v->clip_y, v->clip_w, v->clip_h};
    if (clamp_clip(v, bounds)) {
        Span sp;
        span_init(v, &sp, true);
        for (int j = sp.row_min; j <= sp.row_max; j++) {
            int i0, i1;
            span_row(v, &sp, j, true, &i0, &i1);
            ptrdiff_t step, at = cell_at(v, i0, j, &step);
            for (int i = i0; i <= i1; i++, at += step) {
                if (cell_ok(v, at) && !(v->flags & MAPVIEW_F_NO_MARKERS) && (v->cells[at] & CELL_MARKER)) {
                    int mx, my;
                    map_view_from_logical(v, i, j, &mx, &my);
                    v->origin_x += hw;
                    map_view_draw_marker(dst, v, mx, my);
                    v->origin_x -= hw;
                }
            }
        }
    }
    /* quirk: the original restores the clip only partly (x/y/w/h are kept
     * clamped); harmless, so all of it is restored here */
    v->clip_x = save[0];
    v->clip_y = save[1];
    v->clip_w = save[2];
    v->clip_h = save[3];
    v->origin_x += hw;
}

/* 0x4ecd16. Every other pixel of a cell-sized diamond whose top vertex is
 * at the cell's top corner - 1. The original runs with origin_x shifted left
 * by hw twice (by 0x4eaf9d and again by 0x4ebc33) and adds hw back; this
 * takes the plain origin. */
void map_view_draw_marker(GfxSurface *dst, const MapView *v, int mx, int my)
{
    dst = gfx_target(dst);
    if (!dst)
        return;
    int32_t r[4];
    map_view_cell_rect(v, mx, my, 1, 1, r);
    uint8_t color;
    if (mx < 0x31 || my < 0x31 || mx > 0x32 || my > 0x32)
        color = (uint8_t)((my * 2 + mx) % 5 - 0x30);
    else
        color = (uint8_t)((my * 2 + mx) % 6 - 0x2b);
    int hh = mapview_half_h(v) & 0xff;
    int left = r[0], w = r[1], top = r[2], h = r[3] - v->magnify * hh;
    if (!(w + left > 0 && top + h > 0 && left < dst->width && top < dst->height))
        return;
    int xs = left + (w >> 1) - 1, y = top;
    for (int k = 0; k < h >> 1; k++, y++, xs -= 2) {
        int n = w - 2 * ((w >> 1) - 1 - 2 * k);
        if (y >= dst->height)
            return;
        for (int q = 0; q < n; q += 2)
            if (y >= 0 && xs + q >= 0 && xs + q < dst->width)
                dst->pixels[(size_t)y * (size_t)dst->stride + (size_t)(xs + q)] = color;
    }
    xs += 2;
    for (int k = 0; k < h >> 1; k++, y++, xs += 2) {
        int n = w - 4 * k;
        if (y >= dst->height)
            return;
        for (int q = 0; q < n; q += 2)
            if (y >= 0 && xs + q >= 0 && xs + q < dst->width)
                dst->pixels[(size_t)y * (size_t)dst->stride + (size_t)(xs + q)] = color;
    }
    gfx_touch(dst, left, left + w - 1, top, top + h - 1);
}

/* --- highlight -------------------------------------------------------------- */

/* 0x4ebfbc with w = -1 (the only form the map pass uses; with w > 0 it also
 * sets or clears CELL_HILITE on the area first). The four corners are the
 * top corners of cells (i, j), (i + w, j), (i + w, j + h), (i, j + h); the
 * scan steps the edges 2 px per row like the original. */
void map_view_hilite_area(GfxSurface *dst, const MapView *v, const GfxRect *bounds, int mx, int my, int w, int h,
                          bool on)
{
    dst = gfx_target(dst);
    if (!dst)
        return;
    uint8_t *table = tile_draw_hue_table(on ? 1 : -1);
    uint8_t from = on ? v->hilite_from : v->hilite_to, to = on ? v->hilite_to : v->hilite_from;
    uint8_t saved = table[from];
    table[from] = to;
    int hw = mapview_half_w(v), hh = mapview_half_h(v), H = v->height;
    int base = (mapview_dir(v) << v->magnify) + v->origin_x;
    int i = mx, j = my; /* the pass hands over logical coordinates (0x6773cc = 1) */
    int tx = (i + H - j) * hw + base, ty = (j + i - v->magnify) * hh + v->origin_y;
    int rx = (w + i + H - j) * hw + base, ry = (w + j + i - v->magnify) * hh + v->origin_y;
    int lx = (i + H - (h + j)) * hw + base, ly = (h + j + i - v->magnify) * hh + v->origin_y;
    int by = (h + w + j + i - v->magnify) * hh + v->origin_y;
    int bl = bounds ? bounds->left : 0, br = bounds ? bounds->right : dst->width - 1;
    int bt = bounds ? bounds->top : 0, bb = bounds ? bounds->bottom : dst->height - 1;
    if (bl < 0)
        bl = 0;
    if (bt < 0)
        bt = 0;
    if (br > dst->width - 1)
        br = dst->width - 1;
    if (bb > dst->height - 1)
        bb = dst->height - 1;
    for (int y = ty > bt ? ty : bt; y < by && y <= bb; y++) {
        int xl = y < ly ? tx - 2 * (y - ty) : lx + 2 * (y - ly);
        int xr = y < ry ? tx + 2 * (y - ty) : rx - 2 * (y - ry);
        if (xl < bl)
            xl = bl;
        if (xr > br + 1)
            xr = br + 1;
        uint8_t *row = dst->pixels + (size_t)y * (size_t)dst->stride;
        for (int x = xl; x < xr; x++)
            row[x] = table[row[x]];
    }
    table[from] = saved;
    gfx_touch(dst, lx, rx, ty, by - 1);
}

/* 0x4ed0f4 */
void map_view_hilite_pass(GfxSurface *dst, MapView *v, const GfxRect *bounds, bool on)
{
    dst = gfx_target(dst);
    if (!dst || !v->cells || v->clip_w == 0 || v->clip_h == 0)
        return;
    int32_t save[4] = {v->clip_x, v->clip_y, v->clip_w, v->clip_h};
    /* unlike the map pass, origin_x isn't shifted here */
    if (clamp_clip(v, bounds)) {
        Span sp;
        span_init(v, &sp, false);
        for (int j = sp.row_min; j <= sp.row_max; j++) {
            int i0, i1;
            span_row(v, &sp, j, false, &i0, &i1);
            ptrdiff_t step, at = cell_at(v, i0, j, &step);
            for (int i = i0; i <= i1; i++, at += step)
                if (cell_ok(v, at) && (v->cells[at] & CELL_HILITE))
                    map_view_hilite_area(dst, v, bounds, i, j, 1, 1, on);
        }
    }
    v->clip_x = save[0];
    v->clip_y = save[1];
    v->clip_w = save[2];
    v->clip_h = save[3];
}

/* --- planes ----------------------------------------------------------------- */

void map_planes_init(MapPlanes *p)
{
    static const int32_t y_off[4] = {0, 0x708, 0x3a4, 0x40};
    static const int32_t bottom[4] = {0, 0x1cc, 0x80, 0x1cc};
    for (int i = 1; i <= 3; i++) {
        p->plane[i].y_offset = y_off[i];
        p->plane[i].margin_bottom = bottom[i];
        p->plane[i].margin_top = 0x48;
    }
}

/* 0x42b102 */
void map_view_select_plane(MapView *v, MapPlanes *p, int plane)
{
    if (v->plane >= 1 && v->plane <= 3)
        p->plane[v->plane].flags = v->flags;
    if (plane < 1 || plane > 3)
        return;
    const MapPlane *m = &p->plane[plane];
    v->cells = m->cells;
    v->objmap = m->objmap;
    v->origin_y = ((m->y_offset >> v->zoom) << v->magnify) + p->scroll_y;
    v->flags = m->flags;
    v->margin_bottom = m->margin_bottom;
    v->margin_top = m->margin_top;
    v->plane = (uint8_t)plane;
}

/* 0x4ebd8a */
GfxRect map_view_redraw_cells(GfxSurface *dst, MapView *v, MapPlanes *p, const GfxRect *bounds, int mx, int my, int w,
                              int h, int extra)
{
    GfxRect none = {0, -1, 0, -1};
    dst = gfx_target(dst);
    if (!dst)
        return none;
    /* move to the cell that is the area's logical top corner */
    if (v->dir_x < 1) {
        if (v->dir_y < 1) {
            my += h - 1;
            mx += w - 1;
        } else {
            mx += w - 1;
        }
    } else if (v->dir_y < 1) {
        my += h - 1;
    }
    int32_t r[4];
    map_view_cell_rect(v, mx, my, w, h, r);
    int ex = (extra >> v->zoom) << v->magnify;
    int x0 = r[0] - r[1], y0 = r[2] - ex, rw = r[1] * 2, rh = ex * 2 + r[3];
    if (!(rw + x0 > 0 && rh + y0 > 0 && x0 < dst->width && y0 < dst->height))
        return none;
    if (x0 < 1)
        x0 = 0;
    if (y0 < 1)
        y0 = 0;
    if (rw > dst->width - x0)
        rw = dst->width - x0;
    if (rh > dst->height - y0)
        rh = dst->height - y0;
    v->clip_x = x0;
    v->clip_y = y0;
    v->clip_w = rw;
    v->clip_h = rh;
    map_view_draw(dst, v, bounds);
    int cur = v->plane;
    if (cur < 3) {
        for (int pl = cur + 1; pl < 4; pl++) {
            map_view_select_plane(v, p, pl);
            map_view_draw(dst, v, bounds);
        }
        map_view_select_plane(v, p, cur);
    }
    GfxRect out = {x0, x0 + rw - 1, y0, y0 + rh - 1};
    gfx_touch(dst, out.left, out.right, out.top, out.bottom);
    return out;
}

/* 0x4076f5 */
void map_view_paint(GfxSurface *dst, MapView *v, MapPlanes *p, const GfxRect *bounds, const GfxSurface *backdrop,
                    int x, int y, int w, int h)
{
    dst = gfx_target(dst);
    if (!dst)
        return;
    int cur = v->plane;
    v->clip_x = x;
    v->clip_y = y;
    v->clip_w = w;
    v->clip_h = h;
    if (!backdrop)
        gfx_fill_rect(dst, x, y, w, h, 0x13);
    else
        gfx_tile_fill(dst, x, y, w, h, backdrop, 0, 0, backdrop->width, backdrop->height);
    for (int pl = 1; pl <= 3; pl++) {
        map_view_select_plane(v, p, pl);
        map_view_hilite_pass(dst, v, bounds, false);
        map_view_draw(dst, v, bounds);
        map_view_hilite_pass(dst, v, bounds, true);
    }
    /* the original calls FUN_004640c9 here (not identified) */
    if (v->plane != cur)
        map_view_select_plane(v, p, cur);
}
