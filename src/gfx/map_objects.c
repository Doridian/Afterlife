/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Map object redraws (0x4ed550). See map_objects.h.
 */
#include "gfx/map_objects.h"

#include "gfx/draw.h"

/* tile_set_entries[set][index - 1]'s height (+0x14) and anchor (+0x16),
 * false if there is no such entry. */
static bool entry_box(int set, int index1, int *height, int *anchor, int *size)
{
    const TileSet *ts = set > 0 && set < TILE_MAX_SETS ? tile_draw_set(set) : NULL;
    TileEntry e;
    if (!ts || index1 < 1 || tile_get_entry(ts, index1 - 1, &e) != TILE_OK)
        return false;
    *height = e.height;
    *anchor = e.anchor;
    if (size)
        *size = e.size;
    return true;
}

static int scale(const MapView *v, int n) { return (n >> v->zoom) << v->magnify; }

static void draw_plane(GfxSurface *dst, MapView *v, const GfxRect *bounds)
{
    map_view_hilite_pass(dst, v, bounds, false);
    map_view_draw(dst, v, bounds);
    map_view_hilite_pass(dst, v, bounds, true);
}

/* 0x4ed550 */
GfxRect map_view_redraw_object(GfxSurface *dst, MapView *v, MapPlanes *p, const GfxRect *bounds,
                               const GfxSurface *backdrop, int x, int y, int size, int slot, int set, int frame)
{
    GfxRect none = {0, -1, 0, -1};
    dst = gfx_target(dst);
    if (!dst || !v->cells)
        return none;
    int8_t sl = (int8_t)slot, st = (int8_t)set;
    int obj = sl < 0 ? -sl : sl;
    int W = v->width, H = v->height;
    int mx = x <= W - 1 ? x : W - 1, my = y <= H - 1 ? y : H - 1;
    if (mx < 1)
        mx = 0;
    if (my < 1)
        my = 0;
    uint32_t c = v->cells[my * W + mx];
    int hw = mapview_half_w(v), hh = mapview_half_h(v);
    /* the tallest sprite involved: its height and the rows above the
     * ground line */
    int extra_h = 0, extra_a = 0, hgt = 0, anc = 0;
    if (st > 0)
        entry_box(st, (uint8_t)frame, &extra_h, &extra_a, NULL);
    if (obj == 0) {
        entry_box((int)(c >> 27), (int)((c >> 16) & 0x7f) + 1, &hgt, &anc, NULL);
    } else {
        int h0 = 0, a0 = 0;
        if (c >> 27 == 0 && sl < 0) {
            /* the building whose part the object left */
            int ox = (int8_t)(mx - (int)((c >> 16) & 7)), oy = (int8_t)((int)((c >> 20) & 7) + my);
            int i = oy * W + ox;
            uint32_t oc = i >= 0 && i < W * H ? v->cells[i] : 0;
            int s = (int)(oc >> 27), e = (int)((oc >> 16) & 0x7f) + 1;
            if (s == 0)
                s = 1, e = 1;
            entry_box(s, e, &h0, &a0, NULL);
        }
        const MapObject *o = v->objects && obj < MAP_OBJECTS ? &v->objects[obj] : NULL;
        int oh, oa;
        if (!o || !entry_box(o->set, o->frame + 1, &oh, &oa, NULL))
            return none;
        hgt = oh > h0 ? oh : h0;
        anc = oa > a0 ? oa : a0;
    }
    if (st > 0) {
        if (hgt < extra_h)
            hgt = extra_h;
        if (anc < extra_a)
            anc = extra_a;
    }
    hgt = scale(v, hgt);
    anc = scale(v, anc);
    /* the area's top corner in logical cells */
    int li = mx, lj = my;
    if (v->dir_x < 1) {
        if (v->dir_y < 1) {
            lj = H - (my - size + 1) - 1;
            li = W - (size + mx - 1) - 1;
        } else {
            lj = W - mx - 1;
            li = my - size + 1;
        }
    } else if (v->dir_y < 1) {
        lj = size + mx - 1;
        li = H - my - 1;
    }
    int w = (size * 2 + 1) * hw;
    int h = (size * 2 + 1) * hh + hgt;
    int left = ((H - lj) - 1 + li) * hw + v->origin_x;
    int top = (lj + li) * hh - anc + v->origin_y;
    if (!(left + w > 0 && top + h > 0 && left < dst->width && top < dst->height))
        return none;
    if (left < 1)
        left = 0;
    if (top < 1)
        top = 0;
    if (w > dst->width - left)
        w = dst->width - left;
    if (h > dst->height - top)
        h = dst->height - top;
    int x0 = left, y0 = top;
    if (bounds) {
        if (x0 < bounds->left)
            x0 = bounds->left;
        if (y0 < bounds->top)
            y0 = bounds->top;
        if (w > bounds->right + 1 - x0)
            w = bounds->right + 1 - x0;
        if (h > bounds->bottom + 1 - y0)
            h = bounds->bottom + 1 - y0;
    }
    if (h < 1)
        h = 0;
    v->clip_x = x0;
    v->clip_y = y0;
    v->clip_w = w;
    v->clip_h = h;
    if (sl < 1 || st != 0) {
        if (!backdrop)
            gfx_fill_rect(dst, x0, y0, w, h, 0x13);
        else if (backdrop->width > 0 && backdrop->height > 0)
            gfx_tile_fill(dst, x0, y0, w, h, backdrop, x0 % backdrop->width, y0 % backdrop->height,
                          backdrop->width, backdrop->height);
    }
    int cur = v->plane;
    if (cur > 1) {
        for (int pl = 1; pl < cur; pl++) {
            map_view_select_plane(v, p, pl);
            draw_plane(dst, v, bounds);
        }
        map_view_select_plane(v, p, cur);
    }
    v->clip_y = y0;
    v->clip_h = h;
    draw_plane(dst, v, bounds);
    v->set_mask = 0xffffffffu;
    if (cur < 3) {
        for (int pl = cur + 1; pl < 4; pl++) {
            map_view_select_plane(v, p, pl);
            draw_plane(dst, v, bounds);
        }
        map_view_select_plane(v, p, cur);
    }
    GfxRect out = {x0, x0 + w - 1, y0, y0 + h - 1};
    if (w > 0 && h > 0)
        gfx_touch(dst, out.left, out.right, out.top, out.bottom);
    return out;
}
