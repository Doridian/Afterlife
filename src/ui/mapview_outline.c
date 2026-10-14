/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The Mapview's outline of the main view (0x465cb2): see ui/mapview_int.h. */
#include <stdlib.h>
#include <string.h>

#include "gfx/draw.h"
#include "gfx/map_view.h"
#include "ui/mapview.h"
#include "ui/mapview_int.h"
#include "ui/views.h"

void mapview_view_geometry(int *dir_x, int *dir_y, int *w, int *h)
{
    const MapView *v = views_map_view();
    *dir_x = v ? v->dir_x : 1;
    *dir_y = v ? v->dir_y : 1;
    *w = v ? v->width : 100;
    *h = v ? v->height : 100;
}

/* 0x4674a5: screen point -> map cell of the view, unclamped */
static void screen_to_cell_raw(const MapView *v, int sx, int sy, int *mx, int *my)
{
    int hw = ((v->cell_w >> 1) >> v->zoom) << v->magnify;
    if (hw == 0)
        hw = 1;
    int u = -(v->height * hw) - v->origin_x + sx, w = sy - v->origin_y;
    int i = (w + u / 2) / hw, j = (w - u / 2) / hw;
    if (v->dir_x < 0) {
        if (v->dir_y < 0)
            *mx = v->width - i - 1, *my = v->height - j - 1;
        else
            *mx = v->height - j - 1, *my = i;
    } else if (v->dir_y < 0)
        *mx = j, *my = v->width - i - 1;
    else
        *mx = i, *my = j;
}

/* 0x467065: clips the segment to r = {x min, x max, y min, y max}; false
 * (and nothing to draw) for horizontal or vertical segments and ones
 * outside */
static bool clip_line(int *x1, int *y1, int *x2, int *y2, const int r[4])
{
    int xmin = r[0], xmax = r[1], ymin = r[2], ymax = r[3];
    if (*x2 == *x1 || *y2 == *y1)
        return false;
    bool swap = *x2 < *x1;
    int t;
    if (swap) {
        t = *x2, *x2 = *x1, *x1 = t;
        t = *y2, *y2 = *y1, *y1 = t;
    }
    if (!(*x1 < xmax && xmin < *x2 && (*y1 < ymax || *y2 < ymax) && (ymin < *y1 || ymin < *y2))) {
        if (swap) {
            t = *x1, *x1 = *x2, *x2 = t;
            t = *y1, *y1 = *y2, *y2 = t;
        }
        return false;
    }
    if (*x1 < xmin) {
        *y1 += ((*y2 - *y1) * (xmin - *x1)) / (*x2 - *x1);
        *x1 = xmin;
    }
    if (*y1 < ymin) {
        *x1 += ((*x2 - *x1) * (ymin - *y1)) / (*y2 - *y1);
        *y1 = ymin;
    }
    if (ymax < *y1) {
        *x1 += ((*x2 - *x1) * (ymax - *y1)) / (*y2 - *y1);
        *y1 = ymax;
    }
    if (*x1 <= xmin)
        *x1 = xmin;
    if (xmax <= *x1)
        *x1 = xmax;
    if (*y1 <= ymin)
        *y1 = ymin;
    if (ymax <= *y1)
        *y1 = ymax;
    if (xmax < *x2) {
        *y2 = *y1 + ((*y2 - *y1) * (xmax - *x1)) / (*x2 - *x1);
        *x2 = xmax;
    }
    if (*y2 < ymin) {
        *x2 = *x1 + ((*x2 - *x1) * (ymin - *y1)) / (*y2 - *y1);
        *y2 = ymin;
    }
    if (ymax < *y2) {
        *x2 = *x1 + ((*x2 - *x1) * (ymax - *y1)) / (*y2 - *y1);
        *y2 = ymax;
    }
    if (*x2 <= xmin)
        *x2 = xmin;
    if (xmax <= *x2)
        *x2 = xmax;
    if (*y2 <= ymin)
        *y2 = ymin;
    if (ymax <= *y2)
        *y2 = ymax;
    if (swap) {
        t = *x1, *x1 = *x2, *x2 = t;
        t = *y1, *y1 = *y2, *y2 = t;
    }
    if (*x2 == *x1 && *y2 == *y1) {
        *x1 = *y1 = *x2 = *y2 = 0;
        return false;
    }
    return true;
}

/* 0x465cb2: outlines the part of the realm the main window shows */
void mapview_outline(GfxSurface *dst, int zoom, int x0, int y0)
{
    const MapView *v = views_map_view();
    WmWindow *mw = views_main_window();
    if (!v || !mw || !mw->surface)
        return;
    int seg[5][4];
    int nseg = 0, n = 100 >> zoom, s = 2 << zoom;
    int sw = mw->surface->width, sh = mw->surface->height;
    memset(seg, 0, sizeof seg);

    /* the window's corners in cells, clipped to the cells shown */
    int vis[4] = {x0, n + x0 - 1, y0, n + y0 - 1};
    int pt[5][2];
    screen_to_cell_raw(v, 0, 0, &pt[0][0], &pt[0][1]);
    screen_to_cell_raw(v, sw - 1, 0, &pt[1][0], &pt[1][1]);
    screen_to_cell_raw(v, sw - 1, sh - 1, &pt[2][0], &pt[2][1]);
    screen_to_cell_raw(v, 0, sh - 1, &pt[3][0], &pt[3][1]);
    pt[4][0] = pt[0][0], pt[4][1] = pt[0][1];
    for (int k = 0; k < 4; k++) {
        int *g = seg[nseg];
        g[0] = pt[k][0], g[1] = pt[k][1], g[2] = pt[k + 1][0], g[3] = pt[k + 1][1];
        if (clip_line(&g[0], &g[1], &g[2], &g[3], vis)) {
            mapview_cell_pos(g[0], g[1], &g[0], &g[1]);
            mapview_cell_pos(g[2], g[3], &g[2], &g[3]);
            gfx_line(dst, g[0], g[2], g[1], g[3], 0xb);
            nseg++;
        }
    }

    /* the edges of the cells shown, where they cross the main window */
    int scr[4] = {0, sw - 1, 0, sh - 1};
    int wn = v->width >> zoom, hn = v->height >> zoom;
    map_view_cell_to_screen(v, x0, y0, &pt[0][0], &pt[0][1]);
    map_view_cell_to_screen(v, x0 + wn - 1, y0, &pt[1][0], &pt[1][1]);
    map_view_cell_to_screen(v, x0 + wn - 1, y0 + hn - 1, &pt[2][0], &pt[2][1]);
    map_view_cell_to_screen(v, x0, y0 + hn - 1, &pt[3][0], &pt[3][1]);
    for (int k = 0; k < 4; k++)
        pt[k][0] += v->origin_x, pt[k][1] += v->origin_y;
    pt[4][0] = pt[0][0], pt[4][1] = pt[0][1];
    GfxRect area = {MAPVIEW_MAP_X, n * s + MAPVIEW_MAP_X - 1, MAPVIEW_MAP_Y, n * s + MAPVIEW_MAP_Y - 1};
    for (int k = 0; k < 4; k++) {
        int a[4] = {pt[k][0], pt[k][1], pt[k + 1][0], pt[k + 1][1]};
        if (!clip_line(&a[0], &a[1], &a[2], &a[3], scr))
            continue;
        int c[2][2];
        for (int e = 0; e < 2; e++) {
            int mx, my;
            map_view_screen_to_cell(v, a[e * 2], a[e * 2 + 1], &mx, &my);
            mapview_cell_pos((uint8_t)mx, (uint8_t)my, &c[e][0], &c[e][1]);
        }
        /* snap the ends onto the window outline's ends */
        for (int i = 0; i < nseg; i++)
            for (int e = 0; e < 2; e++) {
                int d1x = abs(c[e][0] - seg[i][0]), d1y = abs(c[e][1] - seg[i][1]);
                int d2x = abs(c[e][0] - seg[i][2]), d2y = abs(c[e][1] - seg[i][3]);
                if (s < d1x || s < d1y) {
                    if (d2x <= s && d2y <= s)
                        c[e][0] = seg[i][2], c[e][1] = seg[i][3];
                } else
                    c[e][0] = seg[i][0], c[e][1] = seg[i][1];
            }
        if (gfx_rect_contains(&area, c[0][0], c[0][1]) && gfx_rect_contains(&area, c[1][0], c[1][1]))
            gfx_line(dst, c[0][0], c[1][0], c[0][1], c[1][1], 0xb);
    }
}
