/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * views_test's renderer half: the main map view as the shell (WP7) will own
 * it (tile sets, MapView, planes, painting). Separate from views_test.c
 * because game/world.h and gfx/tile_draw.h can't be included together.
 */
#include <stdlib.h>
#include <string.h>

#include "formats/tile.h"
#include "game/resource.h"
#include "gfx/map_view.h"
#include "gfx/surface.h"
#include "gfx/tile_draw.h"
#include "platform/platform.h"

void vt_gfx_init(const uint8_t *rgbx);
void vt_gfx_free(void);
struct MapView *vt_gfx_setup(uint32_t *cells[4], uint8_t *objmap[4], int w, int h);
void vt_gfx_select_plane(int plane);
int vt_gfx_plane(void);
void vt_gfx_paint(GfxSurface *dst);
void vt_gfx_center(int plane, int x, int y);
void vt_gfx_rotate(int dir_x, int dir_y);

static TileSet g_sets[32];
static uint8_t *g_set_data[32];
static GfxSurface *g_backdrop;
static MapView g_view;
static MapPlanes g_planes;
static int g_w, g_h;

static uint8_t *read_resource(uint32_t id, size_t *len)
{
    PlatFile *f = rc_open_resource(id);
    if (!f)
        return NULL;
    long l = rc_resource_size(id);
    uint8_t *buf = l > 0 ? malloc((size_t)l) : NULL;
    if (buf)
        plat_file_read(f, buf, 1, (size_t)l);
    plat_file_close(f);
    *len = buf ? (size_t)l : 0;
    return buf;
}

void vt_gfx_init(const uint8_t *rgbx)
{
    tile_draw_init(1);
    tile_draw_build_tables(rgbx);
    for (int id = 1; id <= 31; id++) {
        size_t len = 0;
        g_set_data[id] = read_resource((uint32_t)id, &len);
        if (g_set_data[id] && tile_set_open(&g_sets[id], g_set_data[id], len) == TILE_OK)
            tile_draw_bind_set(id - 1, &g_sets[id]);
    }
    g_backdrop = NULL; /* plain colour 0x13 behind the planes */
}

void vt_gfx_free(void)
{
    for (int id = 1; id <= 31; id++) {
        tile_draw_bind_set(id - 1, NULL);
        free(g_set_data[id]);
        g_set_data[id] = NULL;
    }
    gfx_surface_destroy(g_backdrop);
    g_backdrop = NULL;
}

struct MapView *vt_gfx_setup(uint32_t *cells[4], uint8_t *objmap[4], int w, int h)
{
    g_w = w, g_h = h;
    memset(&g_view, 0, sizeof g_view);
    g_view.width = g_view.height = 100;
    g_view.cell_w = 32;
    g_view.cell_h = 16;
    g_view.zoom = 2;
    g_view.dir_x = g_view.dir_y = 1;
    g_view.focus_x = g_view.focus_y = -1;
    g_view.set_mask = 0xffffffffu;
    g_view.clip_w = w;
    g_view.clip_h = h;
    g_view.flags = MAPVIEW_F_ZONE_DETAIL | MAPVIEW_F_GRID;
    memset(&g_planes, 0, sizeof g_planes);
    map_planes_init(&g_planes);
    for (int pl = 1; pl <= 3; pl++) {
        g_planes.plane[pl].cells = cells[pl];
        g_planes.plane[pl].objmap = objmap[pl];
        g_planes.plane[pl].flags = MAPVIEW_F_ZONE_DETAIL | MAPVIEW_F_GRID;
    }
    map_view_select_plane(&g_view, &g_planes, 3);
    return &g_view;
}

void vt_gfx_select_plane(int plane) { map_view_select_plane(&g_view, &g_planes, plane); }
int vt_gfx_plane(void) { return g_view.plane; }

void vt_gfx_paint(GfxSurface *dst)
{
    GfxRect b = {0, g_w - 1, 0, g_h - 1};
    int plane = g_view.plane;
    map_view_paint(dst, &g_view, &g_planes, &b, g_backdrop, 0, 0, g_w, g_h);
    map_view_select_plane(&g_view, &g_planes, plane);
}

/* like 0x406be8: cell (x, y) of `plane` in the middle of the screen */
void vt_gfx_center(int plane, int x, int y)
{
    int prev = g_view.plane;
    map_view_select_plane(&g_view, &g_planes, plane);
    int sx, sy;
    map_view_cell_to_screen(&g_view, x, y, &sx, &sy);
    g_view.origin_x = g_w / 2 - sx;
    int top = (g_planes.plane[plane].y_offset >> g_view.zoom) << g_view.magnify;
    g_planes.scroll_y = g_h / 2 - sy - top;
    map_view_select_plane(&g_view, &g_planes, prev);
}

void vt_gfx_rotate(int dir_x, int dir_y)
{
    g_view.dir_x = (int8_t)dir_x;
    g_view.dir_y = (int8_t)dir_y;
}
