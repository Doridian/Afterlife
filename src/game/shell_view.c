/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The main map view of the game shell (ALIFE.EXE 0x403bfd-0x4078c4 and
 * tile_load_all 0x408eb0): the three planes drawn into the main window,
 * scrolling, zoom, rotation, the tool drag with its highlight, and
 * game_view_proc (0x404023) minus its menu-command switch (shell.c).
 *
 * Coordinates: the main window is screen-sized and its surface covers it;
 * the map is drawn into the client rectangle (below the menu bar, left of
 * the scrollbars). g_scroll_x / g_scroll_y are the original's view offsets
 * (0x646660 / 0x63051c): the map's left / top edge relative to the client
 * area at zoom 0 is shifted by them.
 */
#include <stdlib.h>
#include <string.h>

#include "formats/tile.h"
#include "game/build.h"
#include "game/planet.h"
#include "game/resource.h"
#include "game/rotate.h"
#include "game/shell_int.h"
#include "game/sim.h"
#include "game/sound.h"
#include "game/structure.h"
#include "game/util.h"
#include "game/world.h"
#include "gfx/draw.h"
#include "gfx/mouse.h"
#include "gfx/tile_draw.h"
#include "ui/advisors.h"
#include "ui/controls.h"
#include "ui/ctrlpanel.h"
#include "ui/mapview.h"
#include "ui/planetview.h"
#include "ui/statusbar.h"

MapView g_map_view;
MapPlanes g_map_planes;
int g_scroll_x, g_scroll_y;
int g_drag_x0 = -1, g_drag_y0 = -1, g_drag_x = -1, g_drag_y = -1, g_drag_plane;

static TileSet g_sets[32];
static uint8_t *g_set_data[32];
static MapObject g_objects[MAP_OBJECTS];
static int g_saved_drag_x0 = -1, g_saved_drag_x = -1; /* 0x6464c8 / 0x6462cc */
static bool g_right_click;                            /* 0x650088 */
static int g_hl_x0 = -1, g_hl_y0, g_hl_x1, g_hl_y1, g_hl_tool, g_hl_plane; /* the highlighted area */

/* zoom-0 pixels at the view's zoom (the original's `(n >> zoom) << magnify`) */
static int zs(int n) { return (n >> g_map_view.zoom) << g_map_view.magnify; }

static int client_w(void) { return g_main_window->client.right - g_main_window->client.left + 1; }
static int client_h(void) { return g_main_window->client.bottom - g_main_window->client.top + 1; }

/* ======================================================================== */
/* Tile sets and the view                                                    */
/* ======================================================================== */

static uint8_t *read_resource(uint32_t id, size_t *len)
{
    PlatFile *f = rc_open_resource(id);
    if (!f)
        return NULL;
    long l = rc_resource_size(id);
    uint8_t *buf = l > 0 ? malloc((size_t)l) : NULL;
    if (buf && plat_file_read(f, buf, 1, (size_t)l) != (size_t)l) {
        free(buf);
        buf = NULL;
    }
    plat_file_close(f);
    *len = buf ? (size_t)l : 0;
    return buf;
}

/* 0x408eb0 tile_load_all: the original loads tile sets 1-31 into slots
 * 0-30 with the progress bar, builds the shade tables from the game
 * palette and seeds the fades (tile_init 0x4e66b0, from the game RNG). */
bool shell_view_init(const uint8_t *rgbx)
{
    tile_draw_init(1);
    if (rgbx)
        tile_draw_build_tables(rgbx);
    int loaded = 0;
    for (int id = 1; id <= 31; id++) {
        size_t len = 0;
        g_set_data[id] = read_resource((uint32_t)id, &len);
        if (g_set_data[id] && tile_set_open(&g_sets[id], g_set_data[id], len) == TILE_OK) {
            tile_draw_bind_set(id - 1, &g_sets[id]);
            loaded++;
        }
    }
    /* game_main: map_view +0x10.. (100 x 100 cells of 32 x 16, zoom 2,
     * no magnification, unrotated, no focus object) */
    memset(&g_map_view, 0, sizeof g_map_view);
    g_map_view.width = g_map_view.height = 100;
    g_map_view.cell_w = 0x20;
    g_map_view.cell_h = 0x10;
    g_map_view.zoom = 2;
    g_map_view.magnify = 0;
    g_map_view.dir_x = g_map_view.dir_y = 1;
    g_map_view.focus_x = g_map_view.focus_y = -1;
    g_map_view.set_mask = 0xffffffffu;
    g_map_view.objects = g_objects;
    map_planes_init(&g_map_planes);
    shell_view_attach();
    return loaded > 0;
}

void shell_view_shutdown(void)
{
    for (int id = 1; id <= 31; id++) {
        tile_draw_bind_set(id - 1, NULL);
        free(g_set_data[id]);
        g_set_data[id] = NULL;
    }
}

/* The renderer's copies of what the game state keeps: rotation, view
 * flags (g_build.view_flags is the master copy, see build.h) and the map
 * objects (state.h's records, unpacked). */
static void view_sync(void)
{
    g_map_view.dir_x = g_state.view_dir_x ? g_state.view_dir_x : 1;
    g_map_view.dir_y = g_state.view_dir_y ? g_state.view_dir_y : 1;
    for (int p = 1; p <= 3; p++)
        g_map_planes.plane[p].flags = g_build.view_flags[p];
    if (g_map_view.plane >= 1 && g_map_view.plane <= 3)
        g_map_view.flags = g_build.view_flags[g_map_view.plane];
    for (int i = 0; i < MAP_OBJECTS; i++) {
        const MapObjectRec *r = &g_state.objects[i];
        MapObject *o = &g_objects[i];
        o->plane = r->plane;
        o->x = r->x, o->y = r->y;
        o->dx = r->dx, o->dy = r->dy;
        o->size = r->size;
        o->set = r->set;
        o->frame = r->frame;
        o->fade = r->fade;
    }
}

void shell_view_attach(void)
{
    for (int p = 1; p <= 3; p++) {
        g_map_planes.plane[p].cells = g_state.plane[p].cells;
        g_map_planes.plane[p].objmap = g_state.plane[p].objmap;
    }
    int p = g_map_view.plane ? g_map_view.plane : g_world.plane ? g_world.plane : GS_PLANE_HEAVEN;
    g_map_view.plane = 0;
    view_sync();
    map_view_select_plane(&g_map_view, &g_map_planes, p);
    g_hl_x0 = -1;
}

/* 0x42b102 map_view_select_plane, both halves */
int shell_select_plane(int plane)
{
    int prev = g_map_view.plane;
    if (plane < 1 || plane > 3)
        return prev;
    g_map_planes.plane[plane].flags = g_build.view_flags[plane];
    map_view_select_plane(&g_map_view, &g_map_planes, plane);
    world_select_plane(plane);
    return prev;
}

/* ======================================================================== */
/* Geometry: bounds, clamping, scrollbars                                     */
/* ======================================================================== */

/* 0x407095: the map's size in screen pixels at the view's zoom (the three
 * planes stacked: 0x709 extra rows below the top plane). */
static void view_size(int *w, int *h)
{
    GfxRect b = map_view_bounds(&g_map_view);
    *h = zs(b.bottom + 0x709);
    *w = zs(b.right + 1);
}

/* 0x407103: clamps (x, y) to the scrollable range (or centres the map if
 * it is smaller than the client area). */
static void view_clamp(int *px, int *py)
{
    int mw, mh;
    view_size(&mw, &mh);
    int cw = client_w(), ch = client_h(), nx, ny;
    mw += 400;
    if (mw < cw) {
        nx = (cw - mw) >> 1;
    } else {
        int x = *px > 199 ? 200 : *px, lim = mw - cw + 200;
        nx = x >= -lim ? x : -lim;
    }
    mh += zs(400);
    if (mh < ch) {
        ny = (ch - mh) >> 1;
    } else {
        int top = zs(200) + g_main_window->client.top;
        int y = *py <= top ? *py : top, lim = mh - ch - zs(200);
        ny = y >= -lim ? y : -lim;
    }
    if (g_map_view.magnify) {
        nx &= ~1;
        ny &= ~1;
    }
    *px = nx;
    *py = ny;
}

/* Tail of 0x406d76 / 0x406eec: scrollbar ranges and positions. */
void shell_view_update_scrollbars(void)
{
    int mw, mh;
    view_size(&mw, &mh);
    mh += zs(400);
    WmWindow *m = g_main_window;
    if (m->hscroll) {
        ctl_scroll_set_range(m->hscroll, -200, mw - m->rect.right - m->rect.left + 0xc9);
        ctl_scroll_set_pos(m->hscroll, -g_scroll_x);
    }
    if (m->vscroll) {
        ctl_scroll_set_range(m->vscroll, -zs(200), mh - (m->client.bottom - m->client.top) + 1 - zs(200));
        ctl_scroll_set_pos(m->vscroll, -g_scroll_y);
    }
}

/* ======================================================================== */
/* Painting                                                                   */
/* ======================================================================== */

void shell_view_sync_origin(void)
{
    WmWindow *m = g_main_window;
    view_sync();
    g_map_view.origin_x = g_scroll_x + m->client.left - m->rect.left;
    g_map_planes.scroll_y = g_scroll_y + m->client.top - m->rect.top;
    map_view_select_plane(&g_map_view, &g_map_planes, g_map_view.plane);
}

/* 0x4076f5 map_view_paint */
void shell_view_paint(int x, int y, int w, int h)
{
    WmWindow *m = g_main_window;
    if (!m || !m->surface)
        return;
    view_sync();
    int cur = g_map_view.plane;
    /* map_view +0x08 / the plane's y: the scroll plus the client offset */
    g_map_view.origin_x = g_scroll_x + m->client.left - m->rect.left;
    g_map_planes.scroll_y = g_scroll_y + m->client.top - m->rect.top;
    GfxRect b = m->client;
    GfxRect r = {x, x + w - 1, y, y + h - 1};
    if (!gfx_rect_intersect(&r, &b) || r.right < r.left || r.bottom < r.top) {
        return;
    }
    map_view_paint(m->surface, &g_map_view, &g_map_planes, &b, NULL, r.left, r.top, r.right - r.left + 1,
                   r.bottom - r.top + 1);
    if (g_map_view.plane != cur)
        map_view_select_plane(&g_map_view, &g_map_planes, cur);
    mapview_refresh();
}

void shell_view_repaint(void)
{
    WmWindow *m = g_main_window;
    if (!m)
        return;
    shell_view_paint(m->client.left, m->client.top, client_w(), client_h());
    wm_redraw(m, NULL);
}

/* map_object_redraw 0x4ed550 / map_view_redraw_cells 0x4ebd8a: the area's
 * screen rectangle on the current plane, repainted with the background and
 * every plane so that nothing is left behind. */
void shell_view_redraw_cells(int mx, int my, int w, int h, int extra)
{
    WmWindow *m = g_main_window;
    if (!m || !m->surface || w <= 0 || h <= 0)
        return;
    view_sync();
    g_map_view.origin_x = g_scroll_x + m->client.left - m->rect.left;
    g_map_planes.scroll_y = g_scroll_y + m->client.top - m->rect.top;
    map_view_select_plane(&g_map_view, &g_map_planes, g_map_view.plane);
    int ax = mx, ay = my;
    if (g_map_view.dir_x < 1) {
        if (g_map_view.dir_y < 1)
            ay += h - 1, ax += w - 1;
        else
            ax += w - 1;
    } else if (g_map_view.dir_y < 1) {
        ay += h - 1;
    }
    int32_t r[4];
    map_view_cell_rect(&g_map_view, ax, ay, w, h, r);
    int ex = zs(extra);
    int x0 = r[0] - r[1], y0 = r[2] - ex, rw = r[1] * 2, rh = ex * 2 + r[3];
    GfxRect area = {x0, x0 + rw - 1, y0, y0 + rh - 1};
    if (!gfx_rect_intersect(&area, &m->client) || area.right < area.left || area.bottom < area.top)
        return;
    shell_view_paint(area.left, area.top, area.right - area.left + 1, area.bottom - area.top + 1);
    wm_redraw(m, &area);
}

/* 0x40682a mainview_scroll. The original scrolls the surface with
 * di_scroll_rect and paints the uncovered strips; the port repaints the
 * client area (fast enough, and simpler with the planes' tall sprites). */
void shell_view_scroll(int dx, int dy)
{
    if (g_drag_x0 == -1)
        shell_cancel_drag();
    g_scroll_x += dx;
    g_scroll_y += dy;
    view_clamp(&g_scroll_x, &g_scroll_y);
    shell_view_repaint();
    int x, y;
    gfx_mouse_get_state(&x, &y);
    wm_send(g_main_window, g_main_window, WM_MSG_MOUSEMOVE, x - g_main_window->rect.left,
            y - g_main_window->rect.top);
}

/* Port-only (the port window's relayout): the client area was old_cw x
 * old_ch; keep the map point at its centre there, clamp, and repaint the
 * map into the main window's surface (the caller repaints the screen). */
void shell_view_resized(int old_cw, int old_ch)
{
    if (!g_main_window || !g_main_window->surface)
        return;
    g_scroll_x += (client_w() - old_cw) / 2;
    g_scroll_y += (client_h() - old_ch) / 2;
    view_clamp(&g_scroll_x, &g_scroll_y);
    shell_view_update_scrollbars();
    shell_view_paint(g_main_window->client.left, g_main_window->client.top, client_w(), client_h());
}

/* 0x406be8 */
void shell_center_on_cell(int plane, int x, int y)
{
    if (plane < 1 || plane > 3 || x < 0 || x > 99 || y < 0 || y > 99)
        return;
    shell_select_plane(plane);
    int sx, sy;
    map_view_cell_to_screen(&g_map_view, x, y, &sx, &sy);
    static const int y_off[4] = {0, 0x708, 0x3a4, 0x40};
    sy += zs(y_off[plane]);
    g_scroll_x = (int)((unsigned)(1 - (sx - (client_w() >> 1))) & ~1u);
    g_scroll_y = (int)((unsigned)(1 - (sy - (client_h() >> 1))) & ~1u);
    view_clamp(&g_scroll_x, &g_scroll_y);
    shell_view_update_scrollbars();
    g_shell_repaint = 1;
}

static void zoom_changed(void)
{
    view_clamp(&g_scroll_x, &g_scroll_y);
    shell_view_update_scrollbars();
    g_shell_repaint = 1;
}

/* 0x406d76: one step out (1:1 magnified -> 1:1 -> 1:2 -> 1:4 -> 1:8) */
void shell_view_zoom_out(void)
{
    if (g_map_view.zoom == 3)
        return;
    if (g_map_view.magnify == 0)
        g_map_view.zoom++;
    else
        g_map_view.magnify = 0;
    int cx = gfx_screen_width() >> 1, cy = gfx_screen_height() >> 1;
    g_scroll_x = ((g_scroll_x - cx) >> 1) + cx;
    g_scroll_y = ((g_scroll_y - cy) >> 1) + cy;
    zoom_changed();
}

/* 0x406eec: one step in */
void shell_view_zoom_in(void)
{
    if (g_map_view.zoom == 0 && g_map_view.magnify != 0)
        return;
    if (g_map_view.zoom == 0)
        g_map_view.magnify = 1;
    else
        g_map_view.zoom--;
    int cx = gfx_screen_width() >> 1, cy = gfx_screen_height() >> 1;
    g_scroll_x = g_scroll_x * 2 - cx;
    g_scroll_y = g_scroll_y * 2 - cy;
    zoom_changed();
}

/* 0x406b1b: the plane under window y (Hell is drawn below Heaven) */
static int plane_at(int y) { return zs(0x6c8) < y - g_scroll_y ? GS_PLANE_HELL : GS_PLANE_HEAVEN; }

static void screen_to_cell(int x, int y, int *cx, int *cy)
{
    map_view_screen_to_cell(&g_map_view, x, y, cx, cy);
}

/* 0x406b65 */
void shell_view_cell_at(int x, int y, int *plane, int *cx, int *cy)
{
    int p = plane_at(y);
    shell_select_plane(p);
    int mx, my;
    screen_to_cell(x, y, &mx, &my);
    if (plane)
        *plane = p;
    if (cx)
        *cx = mx;
    if (cy)
        *cy = my;
}

/* ======================================================================== */
/* Rotation                                                                   */
/* ======================================================================== */

/* 0x403f4c */
void shell_view_rotation_buttons(void)
{
    static const int buttons[4] = {0x21, 0x22, 0x23, 0x24};
    for (int i = 0; i < 4; i++)
        if (shell_ctrl_button(buttons[i]))
            ctl_button_set_pressed(shell_ctrl_button(buttons[i]), false, true);
    int dx = g_state.view_dir_x, dy = g_state.view_dir_y, b;
    if (dx < 1)
        b = dy < 0 ? 0x22 : 0x24;
    else
        b = dy < 1 ? 0x23 : 0x21;
    if (shell_ctrl_button(b))
        ctl_button_set_pressed(shell_ctrl_button(b), true, true);
}

/* The view centre's cell, for re-centring after a turn. */
static void center_cell(int *plane, int *cx, int *cy)
{
    shell_view_cell_at((g_main_window->rect.right - g_main_window->rect.left + 1) >> 1,
                       (g_main_window->rect.bottom - g_main_window->rect.top + 1) >> 1, plane, cx, cy);
}

static void after_turn(int plane, int cx, int cy)
{
    shell_view_attach();
    shell_center_on_cell(plane, cx, cy);
    if (g_drag_plane)
        shell_select_plane(g_drag_plane);
    shell_view_rotation_buttons();
}

/* ctrlpanel_proc 0x21-0x24: turns the view to rotation (1,1), (-1,-1),
 * (1,-1) or (-1,1) one or two quarter turns at a time, like the original
 * (which calls map_rotate_cells once or twice). */
void shell_view_rotate_to(int button)
{
    static const int8_t want[4][2] = {{1, 1}, {-1, -1}, {1, -1}, {-1, 1}};
    if (button < 0x21 || button > 0x24)
        return;
    int wx = want[button - 0x21][0], wy = want[button - 0x21][1];
    g_game_paused = 1;
    shell_cancel_drag();
    int plane, cx, cy;
    center_cell(&plane, &cx, &cy);
    for (int i = 0; i < 4 && (g_state.view_dir_x != wx || g_state.view_dir_y != wy); i++)
        world_rotate(1);
    after_turn(plane, cx, cy);
    g_game_paused = 0;
}

/* game_view_proc '[' / ']' */
void shell_view_rotate(int dir)
{
    shell_cancel_drag();
    int plane, cx, cy;
    center_cell(&plane, &cx, &cy);
    world_rotate(dir);
    after_turn(plane, cx, cy);
}

/* ======================================================================== */
/* The tool highlight                                                          */
/* ======================================================================== */

/* The cells a tool covers between the drag start and the cell under the
 * mouse: zones and the bulldozer the rectangle, roads and rails the two
 * legs of the line (diagonal first), buildings their footprint at the
 * mouse. (In the original 0x44da29 / 0x44d34f draw it; the port's build
 * API leaves the highlight to the UI.) */
static void hl_cells(int x0, int y0, int x1, int y1, int tool, bool on)
{
    uint32_t *cells = g_state.plane[g_map_view.plane].cells;
    int ax, ay, bx, by;
#define HL(cx, cy)                                                                                                     \
    do {                                                                                                               \
        int hx = (cx), hy = (cy);                                                                                      \
        if (hx >= 0 && hx < 100 && hy >= 0 && hy < 100) {                                                              \
            if (on)                                                                                                    \
                cells[hy * 100 + hx] |= CELL_HILITE;                                                                   \
            else                                                                                                       \
                cells[hy * 100 + hx] &= ~CELL_HILITE;                                                                  \
        }                                                                                                              \
    } while (0)
    if (tool == 10 || tool == 12) {
        int x = x0, y = y0;
        HL(x, y);
        while (x != x1 || y != y1) {
            if (x != x1 && y != y1) {
                x += x1 > x ? 1 : -1;
                y += y1 > y ? 1 : -1;
            } else if (x != x1) {
                x += x1 > x ? 1 : -1;
            } else {
                y += y1 > y ? 1 : -1;
            }
            HL(x, y);
        }
        ax = x0 < x1 ? x0 : x1, bx = x0 < x1 ? x1 : x0;
        ay = y0 < y1 ? y0 : y1, by = y0 < y1 ? y1 : y0;
    } else if ((tool >= 1 && tool <= 8 && !g_build.editor) || tool == 20) {
        ax = x0 < x1 ? x0 : x1, bx = x0 < x1 ? x1 : x0;
        ay = y0 < y1 ? y0 : y1, by = y0 < y1 ? y1 : y0;
        for (int y = ay; y <= by; y++)
            for (int x = ax; x <= bx; x++)
                HL(x, y);
    } else {
        int n = g_build.tool_size ? g_build.tool_size : 1;
        ax = x1, bx = x1 + n - 1, ay = y1 - n + 1, by = y1;
        for (int y = ay; y <= by; y++)
            for (int x = ax; x <= bx; x++)
                HL(x, y);
    }
#undef HL
    if (ax < 0)
        ax = 0;
    if (ay < 0)
        ay = 0;
    if (bx > 99)
        bx = 99;
    if (by > 99)
        by = 99;
    if (bx >= ax && by >= ay)
        shell_view_redraw_cells(ax, ay, bx - ax + 1, by - ay + 1, 0x40);
}

/* Takes the current highlight off (on its plane). */
static void hl_off(void)
{
    if (g_hl_x0 < 0)
        return;
    int cur = g_map_view.plane;
    if (cur != g_hl_plane)
        shell_select_plane(g_hl_plane);
    hl_cells(g_hl_x0, g_hl_y0, g_hl_x1, g_hl_y1, g_hl_tool, false);
    if (cur != g_hl_plane)
        shell_select_plane(cur);
    g_hl_x0 = -1;
}

/* Highlights (x0, y0)-(x1, y1) for the current tool on the current plane
 * and shows the price in the status bar (0x57e100). */
static void hl_on(int x0, int y0, int x1, int y1)
{
    hl_off();
    int tool = g_shell_tool;
    if (tool <= 0 || tool > 20)
        return;
    g_hl_x0 = x0, g_hl_y0 = y0, g_hl_x1 = x1, g_hl_y1 = y1, g_hl_tool = tool, g_hl_plane = g_map_view.plane;
    hl_cells(x0, y0, x1, y1, tool, true);
    if (tool != 20 && g_world.realm_plane) {
        int32_t cost = build_tool_cost(tool, x0, y0, x1, y1);
        if (cost != g_build.cost_shown) {
            g_build.cost_shown = cost;
            statusbar_draw();
        }
    }
}

/* 0x403bfd */
void shell_view_reset_drag(void)
{
    g_drag_x0 = -1;
    g_drag_x = -1;
    g_drag_plane = 0;
}

/* 0x403c23: ends a drag (or the hover highlight) without building. */
void shell_cancel_drag(void)
{
    if (!g_game_in_progress)
        return;
    g_saved_drag_x0 = g_drag_x0;
    g_saved_drag_x = g_drag_x;
    hl_off();
    wm_update(g_main_window);
    g_drag_x0 = g_drag_y0 = -1;
    g_drag_x = g_drag_y = -1;
    g_drag_plane = 0;
}

/* 0x403deb */
void shell_view_restore_drag(void)
{
    g_drag_x0 = g_saved_drag_x0;
    g_drag_x = g_saved_drag_x;
    if (g_drag_x0 < 0 && g_drag_x >= 0) {
        int cur = g_map_view.plane;
        if (g_drag_plane && cur != g_drag_plane)
            shell_select_plane(g_drag_plane);
        hl_on(g_drag_x, g_drag_y, g_drag_x, g_drag_y);
        if (g_drag_plane && cur != g_drag_plane)
            shell_select_plane(cur);
    }
}

/* The tool was applied: views and status follow. */
static void tool_applied(void)
{
    statusbar_draw();
    if (shell_hooks.views_changed)
        shell_hooks.views_changed();
}

/* ======================================================================== */
/* game_view_proc 0x404023                                                    */
/* ======================================================================== */

/* the original scrolls 50 pixels per key-down (auto-repeat included) */
static void scroll_by_keys(int dx, int dy) { shell_view_scroll(dx, dy); }

static void mouse_move(int x, int y)
{
    int tool = g_shell_tool;
    if (g_drag_x0 == -1) {
        int p = plane_at(y);
        if (g_map_view.plane != p)
            shell_select_plane(p);
        if (g_drag_plane == 0)
            g_drag_plane = g_map_view.plane;
        if (tool) {
            int cx, cy;
            screen_to_cell(x, y, &cx, &cy);
            if (cx != g_drag_x || cy != g_drag_y || g_drag_plane != g_map_view.plane) {
                g_drag_plane = g_map_view.plane;
                g_drag_x = cx, g_drag_y = cy;
                if (tool <= 20)
                    hl_on(cx, cy, cx, cy);
                wm_update(g_main_window);
            }
        }
    } else if (tool && g_map_view.plane == g_drag_plane) {
        int cx, cy;
        screen_to_cell(x, y, &cx, &cy);
        if (cx != g_drag_x || cy != g_drag_y) {
            g_drag_x = cx, g_drag_y = cy;
            if (tool == 20) {
                /* the bulldozer works as a brush while dragged */
                if (build_apply_tool(20, cx, cy, cx, cy))
                    tool_applied();
                hl_on(cx, cy, cx, cy);
            } else {
                hl_on(g_drag_x0, g_drag_y0, cx, cy);
            }
        }
    }
}

static void left_down(WmWindow *w, int x, int y)
{
    wm_release_capture();
    wm_set_capture(w);
    shell_select_plane(plane_at(y));
    if (!wm_shift_down()) {
        int tool = g_shell_tool;
        if (tool) {
            if (tool != 20)
                g_game_paused = 1;
            int cx, cy;
            screen_to_cell(x, y, &cx, &cy);
            g_drag_x = cx, g_drag_y = cy;
            if (g_drag_x0 == -1) {
                g_drag_plane = g_map_view.plane;
                g_drag_x0 = cx, g_drag_y0 = cy;
                if (tool == 20) {
                    if (build_apply_tool(20, cx, cy, cx, cy))
                        tool_applied();
                }
                if (tool <= 20)
                    hl_on(cx, cy, cx, cy);
            }
        }
    } else {
        /* Shift-click: zoom in on the cell */
        g_right_click = true;
        int cx, cy;
        screen_to_cell(x, y, &cx, &cy);
        int plane = g_map_view.plane;
        shell_cancel_drag();
        shell_view_zoom_in();
        shell_center_on_cell(plane, cx, cy);
    }
}

static void right_down(WmWindow *w, int x, int y)
{
    wm_release_capture();
    if (!wm_shift_down()) {
        if (g_drag_x0 == -1) {
            /* centre the view on the click, in steps of 8 pixels */
            int cx = ((w->client.right - w->client.left + 1) >> 1) + w->client.left;
            int cy = ((w->client.bottom - w->client.top + 1) >> 1) + w->client.top;
            int dx = (x - cx) & ~7, dy = (y - cy) & ~7;
            shell_view_scroll(-dx, -dy);
            shell_view_update_scrollbars();
        } else {
            /* a right click during a drag cancels it */
            shell_select_plane(g_drag_plane);
            hl_off();
            g_drag_x0 = g_drag_y0 = -1;
            g_drag_x = g_drag_y = -1;
            wm_update(g_main_window);
        }
    } else {
        g_right_click = true;
        int cx, cy;
        screen_to_cell(x, y, &cx, &cy);
        int plane = g_map_view.plane;
        shell_cancel_drag();
        shell_view_zoom_out();
        shell_center_on_cell(plane, cx, cy);
    }
}

static void left_up(int x, int y)
{
    wm_release_capture();
    g_game_paused = 0;
    int tool = g_shell_tool;
    if (tool && !g_right_click) {
        int cx = -1, cy = -1;
        if (g_drag_x0 >= 0) {
            if (g_map_view.plane != g_drag_plane)
                shell_select_plane(g_drag_plane);
            hl_off();
            int ex = g_drag_x, ey = g_drag_y;
            if (tool < 9) {
                screen_to_cell(x, y, &ex, &ey);
                if (plane_at(y) != g_drag_plane)
                    ex = g_drag_x, ey = g_drag_y;
            }
            if (tool != 20 && tool < 21 && build_apply_tool(tool, g_drag_x0, g_drag_y0, ex, ey))
                tool_applied();
            g_build.cost_shown = 0;
            statusbar_draw();
            cx = ex, cy = ey;
        }
        shell_select_plane(plane_at(y));
        if (cx < 0)
            screen_to_cell(x, y, &cx, &cy);
        if (tool == 0x15 && shell_hooks.microview_cell) {
            shell_hooks.microview_cell(cx, cy);
            if (shell_hooks.microview_show)
                shell_hooks.microview_show(true);
        }
        if (tool == 0x16 && shell_hooks.soulview_cell && shell_hooks.soulview_cell(cx, cy) &&
            shell_hooks.soulview_show)
            shell_hooks.soulview_show(true);
        g_drag_x = g_drag_y = -1;
    }
    g_right_click = false;
    g_drag_x0 = g_drag_y0 = -1;
}

/* The keys of game_view_proc's WM_MSG_CHAR (0x1e). */
static void key_char(int ch)
{
    shell_cheat_char(ch);
    if (wm_capture())
        return;
    if (g_drag_x0 != -1 && ch != '.') {
        wm_release_capture();
        shell_select_plane(g_drag_plane);
        hl_off();
        g_drag_x0 = g_drag_y0 = -1;
        g_drag_x = g_drag_y = -1;
        wm_update(g_main_window);
    }
    if (g_tilechooser_window && wm_is_visible(g_tilechooser_window) && ch != ' ')
        wm_send(g_tilechooser_window, g_tilechooser_window, WM_MSG_RBUTTONDOWN, 0, 0);
    switch (ch) {
    case ' ': /* toggle the pause, back to the last speed */
        if (g_game_speed == 0)
            shell_menu_command(shell_last_speed());
        else
            shell_menu_command(MENU_SPEED_PAUSE);
        break;
    case '!': /* the Planetview's forced disasters (editor) and the money cheat sequence */
    case '@':
    case '#':
    case '$':
    case '%':
    case '^':
    case '&':
    case '*':
    case '(':
    case ')': {
        static const char keys[] = "!@#$%^&*()";
        int n = (int)(strchr(keys, ch) - keys) + 1;
        if (g_build.editor)
            planet_year((unsigned)n);
        if (ch == '!')
            build_select_tool(0xfc);
        else if (ch == '@')
            build_select_tool(0xfb);
        else if (ch == '$')
            build_select_tool(0xfa);
        break;
    }
    case '+':
    case '=':
        shell_cancel_drag();
        shell_view_zoom_in();
        break;
    case '-':
    case '_':
        shell_cancel_drag();
        shell_view_zoom_out();
        break;
    case '.':
        snd_slot_stop_stream(199);
        if (advisor_window() && wm_is_visible(advisor_window()))
            wm_send(advisor_window(), advisor_window(), ADVISOR_MSG_HUSH, 0, 0);
        break;
    case '0':
        ctrlpanel_click(8);
        break;
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
        ctrlpanel_click(ch - '0');
        break;
    case 'A':
    case 'a': /* the map grid outline of empty land (map_view +0x54 mini) */
        g_map_view.mini ^= 1;
        g_shell_repaint = 1;
        break;
    case 'B':
    case 'b':
        ctrlpanel_click(0xf);
        break;
    case 'C':
    case 'c':
        ctrlpanel_click(0xd);
        break;
    case 'D':
    case 'd':
        ctrlpanel_click(0x1b);
        break;
    case 'E':
    case 'e':
        ctrlpanel_click(0x10);
        break;
    case 'F':
    case 'f':
        ctrlpanel_click(0x20);
        break;
    case 'G':
    case 'g':
        ctrlpanel_click(9);
        break;
    case 'H':
    case 'h':
        ctrlpanel_click(0x1f);
        break;
    case 'I':
    case 'i':
        ctrlpanel_click(0x16);
        break;
    case 'K':
    case 'k':
        ctrlpanel_click(0xb);
        break;
    case 'L':
    case 'l':
        ctrlpanel_click(0x13);
        break;
    case 'M':
    case 'm':
        ctrlpanel_click(0x15);
        if (shell_hooks.microview_show)
            shell_hooks.microview_show(true);
        break;
    case 'N':
    case 'n':
        ctrlpanel_click(0xe);
        break;
    case 'O':
    case 'o':
        ctrlpanel_click(0x12);
        break;
    case 'P':
    case 'p':
        ctrlpanel_click(0x1e);
        break;
    case 'R':
    case 'r':
        ctrlpanel_click(10);
        break;
    case 'S':
    case 's':
        ctrlpanel_click(0x11);
        break;
    case 'T':
    case 't':
        ctrlpanel_click(0xc);
        break;
    case 'U':
    case 'u':
        ctrlpanel_click(0x25);
        break;
    case 'X':
    case 'x':
        ctrlpanel_click(0x14);
        break;
    case 'Z':
    case 'z':
        shell_menu_command(MENU_CTRLPANEL);
        break;
    case '[':
    case '{':
        shell_view_rotate(-1);
        break;
    case ']':
    case '}':
        shell_view_rotate(1);
        break;
    default:
        shell_editor_key(ch);
        break;
    }
}

/* WM_MSG_KEYDOWN (0x1f) */
static void key_down(int vk)
{
    int plane, cx, cy;
    switch (vk) {
    case PLAT_VK_PRIOR: /* Page Up: from Hell to Heaven */
        center_cell(&plane, &cx, &cy);
        if (plane == GS_PLANE_HELL) {
            shell_cancel_drag();
            shell_center_on_cell(GS_PLANE_HEAVEN, cx, cy);
        }
        break;
    case PLAT_VK_NEXT:
        center_cell(&plane, &cx, &cy);
        if (plane == GS_PLANE_HEAVEN) {
            shell_cancel_drag();
            shell_center_on_cell(GS_PLANE_HELL, cx, cy);
        }
        break;
    case PLAT_VK_LEFT:
        scroll_by_keys(0x32, 0);
        shell_view_update_scrollbars();
        break;
    case PLAT_VK_UP:
        scroll_by_keys(0, 0x32);
        shell_view_update_scrollbars();
        break;
    case PLAT_VK_RIGHT:
        scroll_by_keys(-0x32, 0);
        shell_view_update_scrollbars();
        break;
    case PLAT_VK_DOWN:
        scroll_by_keys(0, -0x32);
        shell_view_update_scrollbars();
        break;
    case PLAT_VK_F1: { /* a random "Game Paused" box */
        static const char *const box[5][3] = {
            {"Game Paused", "This game is paused, babe.", "~Rock On\0"},
            {"Game Paused", "This game won't resume until you push the button.", "~Push Me\0"},
            {"Game Paused", "This game is, like, so paused.", "~Unpause\0"},
            {"Game Paused", "Game pause, dog paws, we all pause for game pause.", "~Resume\0"},
            {"Game Paused", "Is this any way to pause a game?", "~Move On\0"},
        };
        int i = rand_range(0, 4);
        ctl_message_box(-1, -1, box[i][0], box[i][1], box[i][2], 0);
        break;
    }
    case PLAT_VK_F1 + 1: /* F2 */
        shell_menu_command(MENU_LOAD);
        break;
    case PLAT_VK_F1 + 2:
        shell_menu_command(MENU_LOAD_SCENARIO);
        break;
    case PLAT_VK_F1 + 4:
        shell_menu_command(MENU_SAVE);
        break;
    case PLAT_VK_F1 + 5:
        if (g_build.editor) {
            g_game_command = GAME_CMD_SAVE_SCENARIO;
        }
        break;
    case PLAT_VK_F1 + 7:
        shell_menu_command(MENU_EXIT);
        break;
    case PLAT_VK_F1 + 8:
        shell_menu_command(MENU_AUDIO);
        break;
    case PLAT_VK_F1 + 9:
        shell_menu_command(MENU_NEW);
        break;
    default:
        break;
    }
}

/* 0x404023 */
intptr_t game_view_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_HTRACK: /* w = the scrollbar */
        ctl_scroll_set_pos(w, (int)a);
        shell_view_scroll((int)b, 0);
        break;
    case WM_MSG_VTRACK:
        ctl_scroll_set_pos(w, (int)a);
        shell_view_scroll(0, (int)b);
        break;
    case WM_MSG_HSCROLL:
        shell_view_scroll((int)b * -0x34, 0);
        shell_view_update_scrollbars();
        break;
    case WM_MSG_VSCROLL:
        shell_view_scroll(0, (int)b * -0x34);
        shell_view_update_scrollbars();
        break;
    case WM_MSG_MOUSEMOVE:
        if (w == g_main_window)
            mouse_move((int)a, (int)b);
        break;
    case WM_MSG_LBUTTONDOWN:
        if (w == g_main_window)
            left_down(w, (int)a, (int)b);
        break;
    case WM_MSG_RBUTTONDOWN:
        if (w == g_main_window)
            right_down(w, (int)a, (int)b);
        break;
    case WM_MSG_LBUTTONUP:
        if (w == g_main_window)
            left_up((int)a, (int)b);
        break;
    case WM_MSG_RBUTTONUP:
        g_right_click = false;
        break;
    case WM_MSG_CHAR:
        key_char((int)a);
        break;
    case WM_MSG_KEYDOWN:
        key_down((int)a);
        break;
    case WM_MSG_MINIMIZED: /* di_minimize: nothing to do in the port */
        break;
    case WM_MSG_MOUSEENTER:
        if (w == g_main_window) {
            if (!a) {
                wm_set_cursor(w, g_shell_arrow);
                g_right_click = false;
                hl_off();
                g_drag_x = g_drag_y = -1;
            } else {
                int x, y;
                gfx_mouse_get_state(&x, &y);
                wm_send(g_main_window, g_main_window, WM_MSG_MOUSEMOVE, x, y);
                wm_set_cursor(w, g_shell_cursor);
            }
        }
        break;
    case WM_MSG_CLOSE:
        if (g_game_running)
            shell_menu_command(MENU_EXIT);
        return 0;
    case WM_MSG_CLOSE_BUTTON:
        shell_menu_command(MENU_EXIT);
        break;
    case SHELL_MSG_REPAINT:
        shell_view_repaint();
        shell_view_rotation_buttons();
        return 0;
    case WM_MSG_MENU_COMMAND:
        shell_menu_dispatch(w, (unsigned)a);
        return 0;
    default:
        break;
    }
    return wm_default_proc(w, msg, a, b);
}
