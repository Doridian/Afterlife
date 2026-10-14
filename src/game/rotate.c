/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Map rotation: game/rotate.h.
 */
#include "game/rotate.h"

#include "game/build.h"
#include "game/structure.h"
#include "game/util.h"
#include "game/world.h"

static unsigned strip_at(int x, int y) { return (cell_get_flat(x, y) >> 23) & 0xf; }

static void strip_set(int x, int y, unsigned s)
{
    if (x >= 0 && x < GS_MAP_W && y >= 0 && y < GS_MAP_H)
        cell_set_strip(x, y, s);
}

static void strip_swap(int x1, int y1, int x2, int y2)
{
    unsigned a = strip_at(x1, y1), b = strip_at(x2, y2);
    strip_set(x1, y1, b);
    strip_set(x2, y2, a);
}

/* road / rail shape (index % 11) for the other rotation */
static unsigned turn_shape(unsigned s, int f)
{
    switch (s) {
    case 1:
        return f ? 4 : 2;
    case 2:
        return f ? 5 : 1;
    case 3:
        return f ? 6 : 3;
    case 4:
        return f ? 1 : 5;
    case 5:
        return f ? 2 : 4;
    case 6:
        return f ? 3 : 6;
    case 8:
        return f ? 8 : 9;
    case 9:
        return f ? 9 : 8;
    default: /* 0, 7, 10 */
        return s;
    }
}

static void set_cell(int x, int y, unsigned set, unsigned index)
{
    if (x < 0 || x >= GS_MAP_W || y < 0 || y >= GS_MAP_H)
        return;
    cell_set_tileset(x, y, set);
    cell_set_index(x, y, index);
    cell_set_strip(x, y, 1);
}

static void rotate_water(int x, int y, int f)
{
    int wm = g_world.water_entry_max;
    unsigned idx = cell_index(x, y);
    unsigned k = idx < 0x34 ? idx - (unsigned)wm : idx;
    int to = -1;
    switch (k) {
    case 1:
        to = f ? -1 : 2;
        break;
    case 2:
        to = f ? -1 : 1;
        break;
    case 4:
        to = f ? 8 : -1;
        break;
    case 5:
        to = f ? 9 : 6;
        break;
    case 6:
        to = f ? 10 : 5;
        break;
    case 7:
        to = f ? 0xb : -1;
        break;
    case 8:
        to = f ? 4 : -1;
        break;
    case 9:
        to = f ? 5 : 10;
        break;
    case 10:
        to = f ? 6 : 9;
        break;
    case 0xb:
        to = f ? 7 : -1;
        break;
    case 0xd:
        to = f ? -1 : 0xe;
        break;
    case 0xe:
        to = f ? -1 : 0xd;
        break;
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x39:
    case 0x3a:
    case 0x3b:
    case 0x3c:
    case 0x3d:
    case 0x3e:
    case 0x3f:
    case 0x40: {
        /* a 2x2 water feature becomes plain water */
        int r0 = rand_range(0, 2), r1 = rand_range(0, 2), r2 = rand_range(0, 2), r3 = rand_range(0, 2);
        set_cell(x, y, 2, (unsigned)r0);
        set_cell(x + 1, y - 1, 2, (unsigned)r1);
        set_cell(x, y - 1, 2, (unsigned)r2);
        set_cell(x + 1, y, 2, (unsigned)r3);
        return;
    }
    default:
        break;
    }
    if (to >= 0)
        cell_set_index(x, y, (unsigned)(wm + to));
}

/* the front corner of a building: swap its two edges for the new rotation */
static void rotate_strips(int x, int y, int dx, int dy, int same_x)
{
    unsigned b = strip_at(x, y);
    if (b <= 1 || strip_at(x - dx, y) != b - 1 || strip_at(x, y - dy) != b + 1)
        return;
    int u = ((int)b - 2) / 2;
    if (same_x) {
        int far = y - ((int)b - 1) * dy;
        for (int i = 0, cx = x; i < (int)b; i++, cx -= dx)
            strip_swap(cx, y, cx, far);
        for (int i = 0, a = y - dy, c = y - ((int)b - 2) * dy; i < u; i++, a -= dy, c += dy)
            strip_swap(x, a, x, c);
    } else {
        int far = x - ((int)b - 1) * dx;
        for (int i = 0, cy = y; i < (int)b; i++, cy -= dy)
            strip_swap(x, cy, far, cy);
        for (int i = 0, a = x - dx, c = x - ((int)b - 2) * dx; i < u; i++, a -= dx, c += dx)
            strip_swap(a, y, c, y);
    }
}

/* 0x4514a4 map_rotate_cells */
void map_rotate_cells(int new_dx, int new_dy, int ccw)
{
    for (int i = 0; i < 4; i++)
        g_state.unk_5f4cd0[i][3] = g_state.unk_5f4cd0[i][4] = 0;
    int dx = g_state.view_dir_x, dy = g_state.view_dir_y;
    int same_x = dx == new_dx;
    int x0 = dx < 1 ? 99 : 0, x_end = dx < 1 ? -1 : 100;
    int y0 = dy < 1 ? 99 : 0, y_end = dy < 1 ? -1 : 100;
    int f = (new_dx * new_dy > 0) ^ (ccw != 0);
    for (int pl = 1; pl < 4; pl++) {
        world_select_plane(pl);
        for (int x = x0; x != x_end; x += dx)
            for (int y = y0; y != y_end; y += dy) {
                uint32_t c = cell_get(x, y);
                unsigned set = c >> 27, idx = (c >> 16) & 0x7f;
                if (set == 2) {
                    rotate_water(x, y, f);
                } else if (set == 12) {
                    cell_set_index(x, y, turn_shape(idx % 11, f) + idx / 11 * 11);
                } else if (set == 16) {
                    if (idx < 11)
                        cell_set_index(x, y, turn_shape(idx, f));
                } else {
                    rotate_strips(x, y, dx, dy, same_x);
                }
            }
        if (g_build_hooks.soulview_rotated)
            g_build_hooks.soulview_rotated();
    }
    g_state.view_dir_x = (int8_t)new_dx;
    g_state.view_dir_y = (int8_t)new_dy;
}

void world_rotate(int dir)
{
    static const int8_t ring[4][2] = {{1, 1}, {1, -1}, {-1, -1}, {-1, 1}};
    int plane = g_world.plane, i = 0;
    while (i < 3 && (ring[i][0] != g_state.view_dir_x || ring[i][1] != g_state.view_dir_y))
        i++;
    int j = (i + (dir > 0 ? 1 : 3)) & 3;
    map_rotate_cells(ring[j][0], ring[j][1], dir > 0);
    world_select_plane(plane);
}
