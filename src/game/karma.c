/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Karma network: rails, stations, trains and the realm boats. See
 * karma.h for how it works; addresses per function.
 */
#include "game/karma.h"

#include <stdlib.h>

#include "game/mapobj.h"
#include "game/realm.h"
#include "game/sim.h"
#include "game/souls.h"
#include "game/structure.h"
#include "game/build.h"
#include "game/util.h"
#include "game/world.h"

KarmaHooks g_karma_hooks;

/* Runtime state (not saved). */
static int8_t g_scan_x, g_scan_y; /* 0x65eda8 / 0x65edac: the last station a train left from */
static uint32_t g_t_sound;        /* 0x65ede4: timeGetTime of the last train whistle */

void karma_reset(void)
{
    g_scan_x = g_scan_y = 0;
    g_t_sound = 0;
}

/* The train kinds by Karma station entry (0x5fa620, [3][15] bytes written
 * by game_init_defaults 0x45546c): number of cars, then per car its first
 * sprite (set 16 entry - 1 of direction 1) and the last direction before
 * the sprites wrap around (directions 1..12, 6 sprites per half). */
typedef struct TrainKind {
    uint8_t cars;
    uint8_t frame[7];
    uint8_t wrap[7];
} TrainKind;

static const TrainKind k_trains[3] = {
    {3, {0x0d, 0x0d, 0x0d}, {6, 6, 6}},
    {1, {0x13}, {0x0c}},
    {4, {0x1f, 0x2b, 0x2b, 0x1f}, {0x0c, 6, 6, 0x0c}},
};

/* The original indexes 0x5fa620 + kind * 15 with any station entry; only
 * entries 0..2 exist on plane 2 (build_place). */
static const TrainKind *train_kind(unsigned kind) { return &k_trains[kind < 3 ? kind : 2]; }

/* ---- Raw access ----------------------------------------------------------------
 *
 * The original indexes the grids as base + y * 400 + x * 4 with signed-char
 * coordinates and no checks; these use the flat index like the original and
 * ignore anything outside the grid (see realm.c). */

static int lin(int x, int y)
{
    int i = y * GS_MAP_W + x;
    return i >= 0 && i < GS_MAP_CELLS ? i : -1;
}
static uint32_t mc(int x, int y)
{
    int i = lin(x, y);
    return i < 0 ? 0 : g_world.cells[i];
}
static unsigned mc_set(int x, int y) { return mc(x, y) >> 27; }
static unsigned mc_entry(int x, int y) { return (mc(x, y) >> 16) & 0x7f; }
static void mc_or(int x, int y, uint32_t v)
{
    int i = lin(x, y);
    if (i >= 0)
        g_world.cells[i] |= v;
}
static void mc_and(int x, int y, uint32_t v)
{
    int i = lin(x, y);
    if (i >= 0)
        g_world.cells[i] &= v;
}
static SimCell *sc(int x, int y)
{
    int i = lin(x, y);
    return i < 0 ? NULL : &g_world.sim[i];
}
static bool in_map(int x, int y) { return x >= 0 && y >= 0 && x < 100 && y < 100; }
static int i8(int v) { return (int8_t)v; }

static int dir_dx(int d) { return (int8_t)g_state.karma_dirs[d & 3][0]; }
static int dir_dy(int d) { return (int8_t)g_state.karma_dirs[d & 3][1]; }

static void sel(int plane) { world_select_plane(plane); }

/* Clears `mask` on every cell of the current plane. */
static void clear_all(uint32_t mask)
{
    for (int i = 0; i < GS_MAP_CELLS; i++)
        g_world.cells[i] &= ~mask;
}

static void redraw(int x, int y, int size) { mapobj_redraw(x, y, size, 0, 0, 0, 1); }

/* 0x44ae?? redraw_cell_structure (WP2, display only): the structure under
 * (x, y). */
static void redraw_structure(int x, int y)
{
    uint32_t c = mc(x, y);
    if (c >> 27 == 0) {
        int ox = (uint8_t)(x - (int)((c >> 16) & 7)), oy = (uint8_t)(y + (int)((c >> 20) & 7));
        redraw(ox, oy, structure_size(ox, oy));
    } else {
        redraw(x, y, 1);
    }
}

/* The origin's tile set like map_cell_origin_set (0x42b4f4), for cells known
 * to be in the map. */
static unsigned origin_set(int x, int y) { return cell_origin_tileset(x, y); }

/* The same with the original's unchecked byte arithmetic, for cells that
 * may be off the map (a map object's origin). */
static unsigned origin_set_flat(int x, int y)
{
    uint32_t c = mc(x, y);
    if (c >> 27 == 0) {
        x = (uint8_t)(x - (int)((c >> 16) & 7));
        y = (uint8_t)(y + (int)((c >> 20) & 7));
    }
    return mc_set(x, y);
}

/* Whether the rail cell (x, y) of plane 2 touches down in a realm: tests
 * plane `a`, then `b` (each a select), and selects plane 2 again. */
static bool touches_down(int x, int y, int a, int b)
{
    sel(a);
    if (mc_set(x, y) == 16) {
        sel(GS_PLANE_MIDDLE);
        return true;
    }
    sel(b);
    if (mc_set(x, y) == 16) {
        sel(GS_PLANE_MIDDLE);
        return true;
    }
    sel(GS_PLANE_MIDDLE);
    return false;
}

/* ---- Rail tracing (0x42dfa3-0x42f129) ----------------------------------------- */

/* 0x42dfa3: a rail piece in the stored (rotated) orientation -> the
 * piece for view direction (+, +). 0 and 7 are the straight pieces, 1, 2, 4
 * and 5 corners, 3, 6, 8, 9 T-junctions, 10 the crossing. */
static unsigned rail_unrotate(unsigned p)
{
    int dx = g_state.view_dir_x, dy = g_state.view_dir_y;
    switch (p) {
    case 1:
        return dx < 1 ? (dy < 1 ? 5 : 2) : (dy < 1 ? 4 : 1);
    case 2:
        return dx < 1 ? (dy < 1 ? 4 : 1) : (dy < 1 ? 5 : 2);
    case 3:
        return dy < 1 ? 6 : 3;
    case 4:
        return dx < 1 ? (dy < 1 ? 2 : 5) : (dy < 1 ? 1 : 4);
    case 5:
        return dx < 1 ? (dy < 1 ? 1 : 4) : (dy < 1 ? 2 : 5);
    case 6:
        return dy < 1 ? 3 : 6;
    case 8:
        return dx < 1 ? 9 : 8;
    case 9:
        return dx < 1 ? 8 : 9;
    default: /* 0, 7, 10 and anything else */
        return p;
    }
}

/* 0x42e29f: from junction (x, y), steps (dx1, dy1) then (dx2, dy2)
 * repeatedly (up to 300 times) over unmarked rails; true if that reaches
 * a touch-down. False at once if the leg is the way we came (ddx, ddy). */
static bool rail_probe(int x, int y, int dx1, int dy1, int dx2, int dy2, int ddx, int ddy)
{
    if (dx1 == 0 && dy2 == 0) {
        if ((-ddx == dx2 && -ddy == dy1) || (ddx == dx2 && ddy == dy1))
            return false;
    } else if ((-ddx == dx1 && -ddy == dy2) || (ddx == dx1 && ddy == dy2)) {
        return false;
    }
    for (int n = 0;;) {
        x = i8(x + dx1), y = i8(y + dy1);
        if (mc_set(x, y) != 16 || (mc(x, y) & 0x20))
            return false;
        if (touches_down(x, y, GS_PLANE_HELL, GS_PLANE_HEAVEN))
            return true;
        if (dx2 != 0 || dy2 != 0) {
            x = i8(x + dx2), y = i8(y + dy2);
            if (mc_set(x, y) != 16 || (mc(x, y) & 0x20))
                return false;
            if (touches_down(x, y, GS_PLANE_HELL, GS_PLANE_HEAVEN))
                return true;
        }
        if (++n > 299)
            return false;
    }
}

typedef struct Probe {
    int8_t dx1, dy1, dx2, dy2;
} Probe;

static bool any_probe(int x, int y, const Probe *p, int n, int ddx, int ddy)
{
    for (int i = 0; i < n; i++)
        if (rail_probe(x, y, p[i].dx1, p[i].dy1, p[i].dx2, p[i].dy2, ddx, ddy))
            return true;
    return false;
}

static const Probe k_probe3[] = {{1, 0, 0, 0}, {1, 0, 0, 1}, {-1, 0, 0, 0}, {-1, 0, 0, 1},
                                 {0, -1, 0, 0}, {0, -1, 1, 0}, {0, -1, -1, 0}};
static const Probe k_probe6[] = {{1, 0, 0, 0}, {1, 0, 0, 1}, {-1, 0, 0, 0}, {-1, 0, 0, 1},
                                 {0, 1, 0, 0}, {0, 1, 1, 0}, {0, 1, -1, 0}};
static const Probe k_probe8[] = {{0, 1, 0, 0}, {0, 1, 1, 0}, {0, -1, 0, 0}, {0, -1, 1, 0},
                                 {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 0, -1}};
static const Probe k_probe9[] = {{0, 1, 0, 0}, {0, 1, -1, 0}, {0, -1, 0, 0}, {0, -1, -1, 0},
                                 {1, 0, 0, 0}, {1, 0, 0, 1}, {1, 0, 0, -1}};
static const Probe k_probe10[] = {{0, 1, -1, 0}, {0, 1, 0, 0}, {0, 1, 1, 0}, {0, -1, -1, 0},
                                  {0, -1, 0, 0}, {0, -1, 1, 0}, {1, 0, 0, 0}, {1, 0, 0, 1},
                                  {1, 0, 0, -1}, {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 0, -1}};
#define NPROBE(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* 0x42e5fa: follows the rail leaving (x, y) in direction (dx, dy) (turning
 * at corners, at junctions toward (ddx, ddy)) until a touch-down, the end
 * of the rails or 300 steps, marking (CELL_DEFER) the cells passed that no
 * other branch of a junction connects to a touch-down. Returns the number
 * of cells marked. */
static int rail_trace(int x, int y, int dx, int dy, int ddx, int ddy)
{
    int marked = 0;
    int cx = i8(x + dx), cy = i8(y + dy);
    for (int steps = 0;;) {
        if (mc_set(cx, cy) != 16)
            return marked;
        if (touches_down(cx, cy, GS_PLANE_HELL, GS_PLANE_HEAVEN))
            return marked;
        unsigned p = rail_unrotate(mc_entry(cx, cy));
        bool lone = true;
        int nx = cx, ny = cy;
        switch (p) {
        case 0:
            nx = i8(cx + dx);
            break;
        case 1:
            if (dx == 0)
                nx = i8(cx - 1), dx = -1, dy = 0;
            else
                dx = 0, dy = -1, ny = i8(cy - 1);
            break;
        case 2:
            if (dy == 0)
                ny = i8(cy - 1), dy = -1, dx = 0;
            else
                dy = 0, nx = i8(cx + 1), dx = 1;
            break;
        case 3:
        case 6:
            if (p == 3 ? any_probe(cx, cy, k_probe3, NPROBE(k_probe3), ddx, ddy)
                       : any_probe(cx, cy, k_probe6, NPROBE(k_probe6), ddx, ddy))
                lone = false;
            if (ddx == 0)
                return marked;
            if (ddy == 0)
                nx = i8(dx + cx);
            else if (dx == 0)
                dx = ddx, dy = 0, nx = i8(ddx + cx);
            else
                ny = i8(ddy + cy), dy = ddy, dx = 0;
            break;
        case 4:
            if (dx == 0)
                nx = i8(cx - 1), dx = -1, dy = 0;
            else
                dx = 0, ny = i8(cy + 1), dy = 1;
            break;
        case 5:
            if (dx >= 0)
                dx = 1, dy = 0, nx = i8(cx + 1);
            else
                dx = 0, dy = 1, ny = i8(cy + 1);
            break;
        case 7:
            ny = i8(dy + cy);
            break;
        case 8:
        case 9:
            if (p == 8 ? any_probe(cx, cy, k_probe8, NPROBE(k_probe8), ddx, ddy)
                       : any_probe(cx, cy, k_probe9, NPROBE(k_probe9), ddx, ddy))
                lone = false;
            if (ddy == 0)
                return marked;
            if (ddx == 0)
                ny = i8(dy + cy);
            else if (dx == 0)
                dx = ddx, dy = 0, nx = i8(ddx + cx);
            else
                dy = ddy, dx = 0, ny = i8(ddy + cy);
            break;
        case 10:
            if (any_probe(cx, cy, k_probe10, NPROBE(k_probe10), ddx, ddy))
                lone = false;
            if (ddy == 0) {
                nx = i8(ddx + cx);
            } else if (ddx == 0) {
                ny = i8(ddy + cy);
            } else if (dx == 0) {
                if (dy != 0)
                    dy = 0, dx = ddx, nx = i8(ddx + cx);
            } else {
                dx = 0, dy = ddy, ny = i8(ddy + cy);
            }
            break;
        default:
            break;
        }
        if (lone && !(mc(cx, cy) & 0x20)) {
            mc_or(cx, cy, 0x20);
            marked++;
        }
        cx = nx, cy = ny;
        if (++steps > 299)
            return marked;
    }
}

/* 0x42e1ce: rail cell (x, y) of plane 2 becomes plain land; the station
 * markers of the realms there go away and the rails around re-tile. */
static void rail_drop(int x, int y)
{
    map_stamp_building(x, y, 1, 1, 1, false);
    sel(GS_PLANE_HELL);
    cell_clear_flags(x, y, 8);
    redraw_structure(x, y);
    sel(GS_PLANE_HEAVEN);
    cell_clear_flags(x, y, 8);
    redraw_structure(x, y);
    sel(GS_PLANE_MIDDLE);
    road_autotile(x, y, true, false, false);
    redraw(x, y, 1);
}

/* The traces 0x42f129 starts from a rail piece: {dx, dy, ddx, ddy} each,
 * in the original's order. */
typedef struct Trace {
    int8_t dx, dy, ddx, ddy;
} Trace;

static const Trace k_traces[11][12] = {
    [0] = {{1, 0, 1, 0}, {1, 0, 1, 1}, {1, 0, 1, -1}, {-1, 0, -1, 0}, {-1, 0, -1, 1}, {-1, 0, -1, -1}},
    [1] = {{-1, 0, -1, 1}, {-1, 0, -1, 0}, {-1, 0, -1, -1}, {0, -1, 1, -1}, {0, -1, 0, -1}, {0, -1, -1, -1}},
    [2] = {{1, 0, 1, 1}, {1, 0, 1, 0}, {1, 0, 1, -1}, {0, -1, -1, -1}, {0, -1, 0, -1}, {0, -1, 1, -1}},
    [3] = {{1, 0, 1, 0}, {1, 0, 1, 1}, {1, 0, 1, -1}, {-1, 0, -1, 0}, {-1, 0, -1, 1}, {-1, 0, -1, -1},
           {0, -1, -1, -1}, {0, -1, 1, -1}, {0, -1, 0, -1}},
    [4] = {{-1, 0, -1, -1}, {-1, 0, -1, 0}, {-1, 0, -1, 1}, {0, 1, 1, 1}, {0, 1, 0, 1}, {0, 1, -1, 1}},
    [5] = {{1, 0, 1, -1}, {1, 0, 1, 0}, {1, 0, 1, 1}, {0, 1, -1, 1}, {0, 1, 0, 1}, {0, 1, 1, 1}},
    [6] = {{1, 0, 1, 0}, {1, 0, 1, 1}, {1, 0, 1, -1}, {-1, 0, -1, 0}, {-1, 0, -1, 1}, {-1, 0, -1, -1},
           {0, 1, -1, 1}, {0, 1, 1, 1}, {0, 1, 0, 1}},
    [7] = {{0, 1, 0, 1}, {0, 1, 1, 1}, {0, 1, -1, 1}, {0, -1, 0, -1}, {0, -1, 1, -1}, {0, -1, -1, -1}},
    [8] = {{0, 1, 0, 1}, {0, 1, 1, 1}, {0, 1, -1, 1}, {0, -1, 0, -1}, {0, -1, 1, -1}, {0, -1, -1, -1},
           {-1, 0, -1, -1}, {-1, 0, -1, 1}, {-1, 0, -1, 0}},
    [9] = {{0, 1, 0, 1}, {0, 1, 1, 1}, {0, 1, -1, 1}, {0, -1, 0, -1}, {0, -1, 1, -1}, {0, -1, -1, -1},
           {1, 0, 1, -1}, {1, 0, 1, 1}, {1, 0, 1, 0}},
    [10] = {{1, 0, 1, -1}, {1, 0, 1, 1}, {1, 0, 1, 0}, {-1, 0, -1, -1}, {-1, 0, -1, 1}, {-1, 0, -1, 0},
            {0, 1, -1, 1}, {0, 1, 1, 1}, {0, 1, 0, 1}, {0, -1, -1, -1}, {0, -1, 1, -1}, {0, -1, 0, -1}},
};
static const uint8_t k_ntraces[11] = {6, 6, 6, 9, 6, 6, 9, 6, 9, 9, 12};

/* 0x42f129 */
int karma_rail_remove(int x, int y, bool remove)
{
    int saved = g_world.plane;
    sel(GS_PLANE_MIDDLE);
    x = i8(x), y = i8(y);
    if (mc_set(x, y) != 16)
        return 0; /* the original leaves plane 2 selected here */
    int count = 1;
    unsigned p = rail_unrotate(mc_entry(x, y));
    clear_all(0x20);
    mc_or(x, y, 0x20);
    if (p < 11)
        for (int i = 0; i < k_ntraces[p]; i++) {
            const Trace *t = &k_traces[p][i];
            count += rail_trace(x, y, t->dx, t->dy, t->ddx, t->ddy);
        }
    sel(GS_PLANE_MIDDLE);
    for (int cx = 1; cx < 99; cx++)
        for (int cy = 1; cy < 99; cy++)
            if (mc_set(cx, cy) == 16 && (mc(cx, cy) & 0x20)) {
                mc_and(cx, cy, ~0x20u);
                if (remove)
                    rail_drop(cx, cy);
            }
    sel(saved);
    return count;
}

/* 0x42fddf */
void karma_rail_check(void)
{
    int saved = g_world.plane;
    sel(GS_PLANE_MIDDLE);
    clear_all(0x20);
    for (int x = 1; x < 99; x++)
        for (int y = 1; y < 99; y++) {
            if (mc_set(x, y) != 16 || touches_down(x, y, GS_PLANE_HEAVEN, GS_PLANE_HELL))
                continue;
            int n = (mc_set(x + 1, y) != 1) + (mc_set(x - 1, y) != 1) + (mc_set(x, y + 1) != 1) +
                    (mc_set(x, y - 1) != 1);
            if (n < 2) {
                rail_drop(x, y);
                goto done;
            }
        }
done:
    sel(saved);
}

/* 0x43008b: the touch-down cells (set 16) of realm `plane` store how many
 * rail cells are connected behind them (sim +1, at most 255). */
static void touchdown_counts(int plane)
{
    int saved = g_world.plane;
    sel(plane);
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++)
            if (mc_set(x, y) == 16) {
                sel(GS_PLANE_MIDDLE);
                unsigned n = (unsigned)karma_rail_remove(x, y, false);
                if (n > 0xfe)
                    n = 0xff;
                sel(plane);
                sim_cell(x, y)->b[1] = (uint8_t)n;
            }
    sel(saved);
}

/* ---- Stations and networks (0x43293d-0x433106) -------------------------------- */

static void rail_set_net(int x, int y, unsigned net)
{
    mc_or(x, y, 0x8000);
    mc_and(x, y, 0xffff87ffu);
    mc_or(x, y, (net & 0xff) << 11);
}

/* What a flood finds at (x, y) next to a rail (in the map): stations get
 * their bits (karma_station_mark), anchors 0x10. */
static unsigned flood_touch(int x, int y, unsigned found)
{
    unsigned s = origin_set(x, y);
    if (s == 0 || s == 0x11)
        found |= 1 | karma_station_mark(x, y, found | 1);
    else if (s == 0x12)
        found |= 0x10;
    return found;
}

/* 0x43293d: floods network `net` over the rails from (x, y) (straight
 * pieces 0/7 in direction `dir`, from junctions in all four), marking them
 * (bit 15) and collecting what they reach: 1 | the stations' bits, 0x10
 * an anchor. `bits` is what the stations get. */
static unsigned station_flood(int x, int y, unsigned net, unsigned bits, int dir)
{
    unsigned found = bits;
    x = (uint8_t)x, y = (uint8_t)y;
    if (dir >= 0 && !karma_piece_is_through(mc_entry(x, y))) {
        for (bool junction = false;;) {
            rail_set_net(x, y, net);
            x = (uint8_t)(dir_dx(dir) + x), y = (uint8_t)(dir_dy(dir) + y);
            if (x > 99 || y > 99)
                return found;
            unsigned s = origin_set(x, y);
            if (s == 0 || s == 0x11)
                return found | 1 | karma_station_mark(x, y, found | 1);
            if (s == 0x12)
                return found | 0x10;
            if (s != 0x10)
                return found;
            if (karma_piece_is_through(mc_entry(x, y)))
                junction = true;
            int cx = i8(dir_dy(dir) + x), cy = i8(dir_dx(dir) + y);
            if (in_map(cx, cy))
                found = flood_touch(cx, cy, found);
            cx = i8(x - dir_dy(dir)), cy = i8(y - dir_dx(dir));
            if (in_map(cx, cy))
                found = flood_touch(cx, cy, found);
            if (junction)
                break;
        }
    }
    if (!(mc(x, y) & 0x8000)) {
        rail_set_net(x, y, net);
        for (int d = 0; d < 4; d++) {
            int cx = i8(dir_dx(d) + x), cy = i8(dir_dy(d) + y);
            if (!in_map(cx, cy))
                continue;
            unsigned s = origin_set(cx, cy);
            if (s == 0 || s == 0x11)
                found |= 1 | karma_station_mark(cx, cy, found | 1);
            else if (s == 0x10 && !(mc(cx, cy) & 0x8000))
                found |= station_flood(cx, cy, net, bits, d);
            else if (s == 0x12)
                found |= 0x10;
        }
    }
    return found;
}

/* The rail cells around the 3x3 structure with origin (x, y) that a flood
 * starts from: x-1..x+3 by y-3..y+1 at Chebyshev distance > 1 from its
 * centre (x+1, y-1). Returns true if a flood reached a station. */
static bool flood_around(int x, int y, unsigned *net, unsigned bits)
{
    bool any = false;
    for (int cx = i8(x - 1); cx <= x + 3; cx = i8(cx + 1))
        for (int cy = i8(y - 3); cy <= y + 1; cy = i8(cy + 1)) {
            if (!in_map(cx, cy))
                continue;
            int ax = abs(cx - i8(x + 1)), ay = abs(cy - i8(y - 1));
            int d = ay < ax ? ax : ay;
            if (d > 1 && mc_set(cx, cy) == 16 && !(mc(cx, cy) & 0x8000) &&
                (station_flood(cx, cy, *net, bits, -1) & 1)) {
                any = true;
                if (++*net > 15)
                    *net = 0;
            }
        }
    return any;
}

/* 0x432f28: tints the stations of realm `plane` that no rail reaches
 * (sim +9 bits 0x10/0x20 clear), untints those reached that have a road.
 * Leaves `plane` selected. */
static void station_tint(int plane)
{
    sel(plane);
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++) {
            if (mc_set(x, y) != 0x11)
                continue;
            if ((sim_cell(x, y)->b[9] & 0x30) == 0) {
                if (!(mc(x, y) & 2)) {
                    mc_or(x, y, 2);
                    redraw(x, y, 3);
                    if (g_karma_hooks.views_changed)
                        g_karma_hooks.views_changed();
                }
            } else if ((mc(x, y) & 2) && area_search(x, y, 3, 3, NULL, NULL) != 0xff) {
                mc_and(x, y, ~2u);
                redraw(x, y, 3);
                if (g_karma_hooks.views_changed)
                    g_karma_hooks.views_changed();
            }
        }
}

/* 0x433106 */
void karma_rebuild(void)
{
    unsigned net = 0;
    sel(GS_PLANE_MIDDLE);
    clear_all(0x8020);
    for (int plane = GS_PLANE_HELL; plane <= GS_PLANE_HEAVEN; plane += 2) {
        sel(plane);
        for (int x = 0; x < 100; x++)
            for (int y = 0; y < 100; y++)
                if (mc_set(x, y) == 0x11)
                    sim_cell(x, y)->b[9] = 0;
    }
    sel(GS_PLANE_MIDDLE);
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++)
            if (mc_set(x, y) == 0x12)
                flood_around(x, y, &net, 0x10);
    sel(GS_PLANE_MIDDLE);
    /* then from the stations already reached, starting over after each
     * success (the original's restart runs one column with x = -1, which
     * reads the flat cells (99, y - 1)) */
    bool restart = false;
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++) {
            if (mc_set(x, y) == 0x11) {
                sel(GS_PLANE_HELL);
                if (mc_set(x, y) != 0x11)
                    sel(GS_PLANE_HEAVEN);
                SimCell *s = sc(x, y);
                bool reached = s && (s->b[9] & 0x31);
                sel(GS_PLANE_MIDDLE);
                if (reached && flood_around(x, y, &net, 0x20))
                    restart = true;
            }
            if (restart) {
                x = -1, y = -1;
                restart = false;
            }
        }
    clear_all(0x8020);
    touchdown_counts(GS_PLANE_HEAVEN);
    touchdown_counts(GS_PLANE_HELL);
    sel(GS_PLANE_MIDDLE);
    clear_all(0x8020);
    station_tint(GS_PLANE_HELL);
    station_tint(GS_PLANE_HEAVEN);
    g_state.karma_busy = 0;
}

/* ---- Trains (0x43380b-0x434dc4) -------------------------------------------- */

/* 0x43380b: frees the rails of network `net` (visited/route bits and the
 * route's directions). */
static void net_release(unsigned net)
{
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++)
            if (mc_set(x, y) == 16 && ((mc(x, y) & 0x7800) >> 11) == net) {
                mc_and(x, y, ~0x8000u);
                mc_and(x, y, ~0x400u);
                mc_and(x, y, ~0x300u);
            }
}

/* 0x43392b: a car direction 1..12 for view direction (+, +) -> the
 * current view. */
static unsigned car_dir_rotate(unsigned d)
{
    int vx = g_state.view_dir_x, vy = g_state.view_dir_y;
    switch (d) {
    case 1:
        return vx < 0 ? 7 : d;
    case 2:
        return vx < 1 ? (vy < 0 ? 0xb : 9) : (vy < 0 ? 4 : d);
    case 3:
        return vx < 1 ? (vy < 0 ? 10 : 8) : (vy < 0 ? 5 : d);
    case 4:
        return vx < 1 ? (vy < 0 ? 9 : 0xb) : (vy < 0 ? 2 : d);
    case 5:
        return vx < 1 ? (vy < 0 ? 8 : 10) : (vy < 0 ? 3 : d);
    case 6:
        return vy < 0 ? 0xc : d;
    case 7:
        return vx < 0 ? 1 : d;
    case 8:
        return vx < 1 ? (vy < 0 ? 5 : 3) : (vy < 0 ? 10 : d);
    case 9:
        return vx < 1 ? (vy < 0 ? 4 : 2) : (vy < 0 ? 0xb : d);
    case 10:
        return vx < 1 ? (vy < 0 ? 3 : 5) : (vy < 0 ? 8 : d);
    case 11:
        return vx < 1 ? (vy < 0 ? 2 : 4) : (vy < 0 ? 9 : d);
    case 12:
        return vy < 0 ? 6 : d;
    default:
        return d;
    }
}

/* 0x433c59: the sprite of car `car` of train kind `kind` moving (dx, dy)
 * whose next move is (ndx, ndy) (or, if that is (0, 0), what rail piece
 * `piece` implies). */
static uint8_t car_frame(int dx, int dy, unsigned kind, unsigned car, unsigned piece, int ndx, int ndy)
{
    if (ndx == 0 && ndy == 0) {
        switch (piece & 0xff) {
        case 0:
            ndx = dx;
            break;
        case 1:
        case 5:
            ndx = -dy, ndy = -dx;
            break;
        case 2:
        case 4:
            ndx = dy, ndy = dx;
            break;
        case 7:
            ndy = dy;
            break;
        }
    }
    unsigned f;
    if (dx == 1)
        f = ndx == 1 ? 7 : ndy == 1 ? 10 : 8;
    else if (dy == 1)
        f = ndy == 1 ? 12 : ndx == 1 ? 9 : 2;
    else if (dy == -1)
        f = ndy == -1 ? 6 : ndx == 1 ? 11 : 4;
    else if (ndx == -1)
        f = 1;
    else if (ndy == 1)
        f = 5;
    else
        f = 3;
    f = car_dir_rotate(f) & 0xff;
    const TrainKind *k = train_kind(kind);
    car &= 0xff;
    /* the original reads 0x5fa620 + kind * 15 + 1 / + 8 + car */
    uint8_t wrap = car < 7 ? k->wrap[car] : 0, base = car < 7 ? k->frame[car] : 0;
    if (wrap < f)
        f = (f - 6) & 0xff;
    return (uint8_t)(base + f - 1);
}

/* 0x433e87: car_frame for a car moving from (px, py) onto rail (x, y),
 * next along the route direction stored there. */
static uint8_t car_frame_at(int px, int py, int x, int y, unsigned kind, unsigned car)
{
    uint32_t c = mc(x, y);
    unsigned d = (c >> 8) & 3;
    return car_frame(x - px, y - py, kind, car, (c >> 16) & 0x7f, dir_dx(d), dir_dy(d));
}

/* 0x433f2d: a trip of n cells (1..500) goes into the history; `push`
 * starts a new entry (the train reached an anchor), else it adds to the
 * last one. unk_5c6678 is the average of the non-zero entries. */
static void karma_history_add(unsigned n, bool push)
{
    uint16_t *h = g_state.karma_history;
    n &= 0xffff;
    if (n > 499)
        n = 500;
    if (n < 2)
        n = 1;
    uint8_t cnt = 0;
    uint32_t sum = 0;
    for (int i = 0; i < 15; i++) {
        if (push)
            h[i] = h[i + 1];
        if (h[i] != 0) {
            sum += h[i];
            cnt++;
        }
    }
    if (!push)
        n = (uint16_t)(n + h[15]);
    h[15] = (uint16_t)n;
    if ((uint8_t)(cnt + 1) != 0)
        g_state.unk_5c6678 = (uint16_t)((sum + h[15]) / (uint8_t)(cnt + 1));
}

/* 0x43404a */
void karma_car_step(int px, int py, int x, int y, int slot, int frame)
{
    MapObjectRec *o = &g_state.objects[slot & 0xff];
    px = i8(px), py = i8(py), x = i8(x), y = i8(y);
    frame &= 0xff;
    unsigned kind = frame < 0x13 ? 0 : frame < 0x1f ? 1 : 2;
    unsigned car = (o->fade & 0x38) >> 3;
    if ((int8_t)o->fade < 0)
        o->delay = 1, o->delay_reset = 1;
    bool arrived = false;
    unsigned d = (mc(x, y) >> 8) & 3;
    o->dx = (int8_t)dir_dx(d);
    o->dy = (int8_t)dir_dy(d);
    int nx = i8(dir_dx(d) + x), ny = i8(dir_dy(d) + y);
    unsigned net = (mc(x, y) & 0x7800) >> 11;
    if (in_map(nx, ny) && (px != nx || py != ny)) {
        unsigned d2 = (mc(nx, ny) >> 8) & 3;
        unsigned s = origin_set(nx, ny);
        if (!(g_build.view_flags[g_world.plane & 3] & 0x40))
            s = 0;
        if (s == 0x10) {
            if (car == 0)
                g_state.karma_load[net]++;
            o->first_frame = car_frame(nx - x, ny - y, kind, car, (unsigned)frame, dir_dx(d2), dir_dy(d2));
            if (kind != 2 || k_trains[2].cars - 1u != car)
                return;
            /* the last car of the big train is the other way round */
            if (o->first_frame < 0x25)
                o->first_frame = (uint8_t)(o->first_frame + 6);
            else
                o->first_frame = (uint8_t)(o->first_frame - 6);
            return;
        }
        if (s == 0x12) {
            arrived = true;
            uint32_t c = mc(nx, ny);
            int ax = i8(nx - (int)((c >> 16) & 7)), ay = i8((int)((c & 0x700000) >> 20) + ny);
            if ((mc(ax, ay) & 0x7f0000) == 0)
                mapobj_place(ax, ay, 0, 3, 2, 2, 3, 5); /* the anchor lights up */
        }
    }
    /* gone: off the map on the next move */
    o->dx = (int8_t)(0x7f - x);
    o->dy = (int8_t)(0x7f - y);
    g_state.object_count--;
    if (car == 0) {
        karma_history_add(g_state.karma_load[net], arrived);
        g_state.karma_load[net] = 0;
    }
    if (train_kind(kind)->cars - 1u == car) {
        net_release(net);
        g_state.karma_busy &= ~(1u << net);
        g_state.karma_pending--;
    }
}

/* 0x43455c: routes from rail (x, y) (entered in direction `dir`, -1 at the
 * start) to an anchor or, if `bits` has 0x20, to a station other than the
 * one at (sx, sy): a depth-first search over the rails that marks them
 * (bits 15 and 10) and leaves the way to go in bits 8-9. */
static bool route(int x, int y, unsigned bits, int dir, int sx, int sy)
{
    x = i8(x), y = i8(y), sx = i8(sx), sy = i8(sy);
#define OTHER_STATION(px, py) \
    ((bits & 0x20) && (sx + 3 <= (px) || (px) < sx || (py) <= sy - 3 || sy < (py)))
    bool junction = false;
    if (dir >= 0 && !karma_piece_is_through(mc_entry(x, y))) {
        do {
            mc_or(x, y, 0x8000);
            mc_or(x, y, 0x400);
            mc_or(x, y, (uint32_t)dir << 8);
            x = i8(dir_dx(dir) + x), y = i8(dir_dy(dir) + y);
            if (!in_map(x, y))
                return false;
            unsigned s = origin_set(x, y);
            if (s == 0x10) {
                if (karma_piece_is_through(mc_entry(x, y)))
                    junction = true;
                int cx = i8(dir_dy(dir) + x), cy = i8(dir_dx(dir) + y);
                if (in_map(cx, cy) && mc_set(cx, cy) == 0x12)
                    return true;
                cx = i8(x - dir_dy(dir)), cy = i8(y - dir_dx(dir));
                if (in_map(cx, cy) && mc_set(cx, cy) == 0x12)
                    return true;
            } else {
                if (s != 0x11)
                    return s == 0x12;
                if (OTHER_STATION(x, y))
                    return true;
            }
        } while (!junction);
    }
    if (!(mc(x, y) & 0x8000)) {
        mc_or(x, y, 0x8000);
        mc_or(x, y, 0x400);
        for (int d = 0; d < 4; d++) {
            int cx = i8(dir_dx(d) + x), cy = i8(dir_dy(d) + y);
            if (!in_map(cx, cy))
                continue;
            unsigned s = origin_set(cx, cy);
            bool go = false;
            if (s == 0x10) {
                if (!(mc(cx, cy) & 0x8000)) {
                    if (route(cx, cy, bits, d, sx, sy))
                        go = true;
                    else
                        mc_and(cx, cy, ~0x400u), mc_and(cx, cy, ~0x300u);
                }
            } else if (s == 0x11) {
                go = OTHER_STATION(cx, cy);
            } else if (s == 0x12) {
                go = true;
            }
            if (go) {
                mc_and(x, y, ~0x300u);
                mc_or(x, y, (uint32_t)d << 8);
                return true;
            }
        }
    }
    mc_and(x, y, ~0x400u);
    return false;
#undef OTHER_STATION
}

/* 0x434c38 */
void karma_clear(void)
{
    for (int i = 0; i < 4; i++) {
        if (rand_range(0, 9) == 0)
            continue;
        int j = (int8_t)rand_range(0, 3);
        if (j != i) {
            uint8_t t[3];
            for (int k = 0; k < 3; k++)
                t[k] = g_state.karma_dirs[i][k];
            for (int k = 0; k < 3; k++)
                g_state.karma_dirs[i][k] = g_state.karma_dirs[j & 3][k];
            for (int k = 0; k < 3; k++)
                g_state.karma_dirs[j & 3][k] = t[k];
        }
    }
}

/* 0x434d2d */
void karma_pending_scan(void)
{
    if (g_state.karma_pending == 0)
        return;
    sel(GS_PLANE_MIDDLE);
    for (int i = 0; i < GS_MAP_CELLS; i++)
        if (g_world.objmap[i] >> 2)
            return;
    g_state.karma_pending = 0;
}

/* 0x434dc4 */
void karma_trains_tick(void)
{
    if (g_build.karma_dirty) {
        karma_clear();
        int saved = g_world.plane;
        g_build.karma_dirty = 0;
        sel(GS_PLANE_MIDDLE);
        karma_rebuild();
        sel(saved);
    }
    if (g_state.karma_stations != 0) {
        /* the k-th station after the last one, k random */
        int k = g_state.karma_stations < 2 ? 1 : (int8_t)rand_range(1, g_state.karma_stations - 1);
        int x = i8(g_scan_x + 1), y = g_scan_y;
        if (x > 99) {
            x = 0;
            y = i8(g_scan_y + 1);
            if (y > 99)
                y = 0;
        }
        int sx = 0, sy = 0; /* uninitialised in the original if no station is found */
        unsigned n = 0;
        do {
            n++;
            if (mc_set(x, y) == 0x11) {
                k = i8(k - 1);
                sx = x, sy = y;
            }
            if (++x > 99) {
                x = 0;
                if (++y > 99)
                    y = 0;
            }
        } while (k != 0 && n < 0x2774);

        int bits;
        sel(GS_PLANE_HELL);
        if (mc_set(sx, sy) != 0x11)
            sel(GS_PLANE_HEAVEN);
        if (mc(sx, sy) & 1) /* map_cell_test_shade */
            return;
        SimCell *s = sc(sx, sy);
        bits = s ? (int8_t)s->b[9] : 0;
        if (bits == 0)
            return;
        sel(GS_PLANE_MIDDLE);
        if (!(g_build.view_flags[GS_PLANE_MIDDLE] & 0x40))
            return;
        uint32_t c = mc(sx, sy);
        unsigned kind = (c >> 16) & 0x7f;
        g_scan_x = (int8_t)sx, g_scan_y = (int8_t)sy;

        /* the m-th rail cell on the ring around the station */
        int m = (int8_t)rand_range(1, 12);
        int cx = sx, cy = i8(sy - 3), stx = 1, sty = 0, dirx = 0, diry = -1;
        int rx = -1, ry = 0;
        bool seen = false;
        do {
            if (mc_set(cx, cy) == 0x10) {
                m = i8(m - 1);
                seen = true;
                rx = cx, ry = cy;
            }
            cx = i8(cx + stx), cy = i8(cy + sty);
            if (stx == 1 && sx + 3 == cx) {
                stx = 0, sty = 1, dirx = 1, diry = 0, cy = i8(sy - 2);
            } else if (sty == 1 && sy + 1 == cy) {
                stx = -1, sty = 0, dirx = 0, diry = 1, cx = i8(sx + 2);
            } else if (stx == -1 && sx - 1 == cx) {
                sty = -1, stx = 0, dirx = -1, diry = 0, cy = sy;
            } else if (sty == -1 && sy - 3 == cy) {
                stx = 1, sty = 0, dirx = 0, diry = -1, cx = sx;
                if (!seen)
                    m = 0;
            }
        } while (m > 0);

        if (rx >= 0) {
            unsigned net = (mc(rx, ry) & 0x7800) >> 11;
            const TrainKind *t = train_kind(kind);
            if (!(g_state.karma_busy & (1u << net)) && route(rx, ry, (unsigned)bits, -1, sx, sy) &&
                (int)t->cars < (int)(int8_t)mapobj_free_count()) {
                g_state.karma_busy |= 1u << net;
                g_state.karma_pending++;
                for (unsigned car = 0; car < t->cars; car++) {
                    uint8_t f = car_frame_at(rx - dirx, ry - diry, rx, ry, kind, car);
                    if (kind == 2 && k_trains[2].cars - 1u == car)
                        f = (uint8_t)(f < 0x25 ? f + 6 : f - 6);
                    mapobj_spawn(rx - dirx, ry - diry, 0x10, f, dirx, diry, (int)car * 2 + 1, 1, 2,
                                 (int)car * 8 + 0x80, 0x60000, 2, 0);
                    g_state.object_count++;
                }
                uint32_t now = sim_clock_ms();
                int r = rand_range(10000, 60000);
                if (r + 33000 < (int)(now - g_t_sound)) {
                    uint32_t id;
                    if ((c & 0x7f0000) == 0)
                        id = 0x10019;
                    else if (kind == 2)
                        id = 0x1001b;
                    else
                        id = 0x1001a;
                    g_t_sound = now;
                    if (g_karma_hooks.sound)
                        g_karma_hooks.sound(id, 1);
                }
            }
        }
    }
    clear_all(0x8000);
}

/* ---- Boats (0x435509, 0x435f3d) ------------------------------------------------- */

/* 0x435509. The boat's fade byte holds flags: 0-2 a counter, 3 blocked
 * last time, 4 turning, 5 turn left, 6 sinking/leaving, 7 launching. */
void karma_boat_step(int slot)
{
    MapObjectRec *o = &g_state.objects[slot & 0xff];
    int x = o->x, y = o->y;
    int d = i8(o->frame - g_world.objframe_base);
    uint8_t f = o->fade, ctr = f & 7, left = f & 0x20;
#define KILL()                                   \
    do {                                         \
        o->dx = (int8_t)(0x7f - x);              \
        o->dy = (int8_t)(0x7f - y);              \
        g_state.object_count--;                  \
        g_state.boat_count--;                    \
    } while (0)
#define TURN_START()                             \
    do {                                         \
        o->fade |= 0x10;                         \
        if (rand_range(0, 1) == 0)               \
            o->fade |= 0x20;                     \
        o->dx = 0, o->dy = 0;                    \
        o->fade |= 2;                            \
    } while (0)
    if (origin_set_flat(x, y) == 9) {
        KILL();
    } else if (!(f & 0x10)) {
        if (!(f & 0x40)) {
            if (!(f & 0x80)) {
                if (realm_cur()->unk_811b == 0) {
                    int ddx = 0, ddy = 0;
                    switch (d) {
                    case 1: ddy = -1; break;
                    case 3: ddx = 1; break;
                    case 5: ddy = 1; break;
                    case 7: ddx = -1; break;
                    }
                    int nx = i8(x + ddx), ny = i8(y + ddy);
                    if (nx < 0 || ny < 1 || nx > 98 || ny > 99) {
                        KILL();
                    } else if (cell_tileset(nx, ny) == 2 && cell_tileset(nx + 1, ny) == 2 &&
                               cell_tileset(nx, ny - 1) == 2 && cell_tileset(nx + 1, ny - 1) == 2) {
                        o->dx = (int8_t)ddx, o->dy = (int8_t)ddy;
                        o->fade &= 0xf7;
                    } else {
                        o->fade |= 0x10;
                        if (!(f & 8)) {
                            o->fade &= 0xdf;
                            if (rand_range(0, 1) != 0)
                                o->fade |= 0x20;
                        }
                        o->fade |= 8;
                        o->dx = 0, o->dy = 0;
                        o->fade |= 2;
                    }
                } else {
                    TURN_START();
                }
            } else if (ctr == 0) {
                o->fade &= 0x7f;
                TURN_START();
            } else {
                o->fade = (uint8_t)((o->fade & 0xf8) | (ctr - 1));
            }
        } else if (ctr == 0) {
            KILL();
        } else {
            o->fade = (uint8_t)((o->fade & 0xf8) | (ctr - 1));
        }
    } else if (ctr == 0) {
        if (realm_cur()->unk_811b == 0)
            o->fade &= 0xef;
        else
            o->fade = (uint8_t)((o->fade & 0xf8) | 4);
    } else {
        /* turning: one of 8 directions per step */
        int nd = d;
        if (d >= 0 && d < 8)
            nd = left ? (d + 7) & 7 : (d + 1) & 7;
        o->first_frame = (uint8_t)(nd + g_world.objframe_base);
        o->fade = (uint8_t)((o->fade & 0xf8) | (ctr - 1));
    }
#undef KILL
#undef TURN_START
}

static bool is_open_water(int x, int y)
{
    uint32_t c = mc(x, y);
    return (c >> 27) == 2 && ((c >> 16) & 0x7f) <= g_world.water_entry_max;
}

/* 0x435f3d */
void karma_year(void)
{
    PlaneStats *r = realm_cur();
    int roll = rand_range(0, 6);
    uint8_t n = r->unk_78c4[0][0];
    if (roll != 0 || g_state.boat_count >= 9 || g_state.boat_count >= n || n <= 1)
        return;
    unsigned i = (uint8_t)rand_range(1, n);
    if (i > 127)
        return; /* past the list (the original reads the next fields) */
    int px = (int8_t)r->unk_78c4[i][0], py = (int8_t)r->unk_78c4[i][1];
    if (mc(px, py) & 2)
        return;
    if (!in_map(px, py))
        return; /* a removed entry (0xff, 0xff): the original goes on with garbage */
    unsigned sz = (uint8_t)structure_size(px, py);
    int wx = -1, wy = -1, ddx = 0, ddy = 0, dir = 0;
    /* water in front of the dock: behind (y - size) or in front (y + 2) */
    for (int cx = px; cx < px + (int)sz; cx = i8(cx + 1)) {
        if ((int)sz <= py && is_open_water(cx, i8(py - (int)sz))) {
            wx = cx, wy = i8(py - (int)sz), ddx = 0, ddy = -1, dir = 1;
            break;
        }
        if (py < 99 && wx < 0 && is_open_water(cx, i8(py + 2))) {
            wx = cx, wy = i8(py + 2), ddx = 0, ddy = 1, dir = 5;
            break;
        }
    }
    if (wx < 0)
        for (int cy = py; py - (int)sz < cy; cy = i8(cy - 1)) {
            if (px > 0 && is_open_water(i8(px - 2), cy)) {
                wy = cy, wx = i8(px - 2), ddx = -1, ddy = 0, dir = 7;
                break;
            }
            if (px < 100 - (int)sz && wx < 0 && is_open_water(i8(px + (int)sz), cy)) {
                wy = cy, wx = i8(px + (int)sz), ddx = 1, ddy = 0, dir = 3;
                break;
            }
        }
    if (wx < 0)
        return;
    /* how far the water goes: the launch run */
    unsigned len = 1;
    for (int cx = wx, cy = wy;;) {
        cx = i8(cx + ddx), cy = i8(cy + ddy);
        if (!in_map(cx, cy) || mc_set(cx, cy) != 2)
            break;
        len = (len + 1) & 0xff;
    }
    len >>= 1;
    if (len > 6)
        len = 7;
    int slot = mapobj_spawn(wx - ddx, wy - ddy, 2, dir + g_world.objframe_base, ddx, ddy, 2, 2, g_world.plane,
                            (int)(len | 0x80), 0xffffffffu, 2, 0);
    if ((int8_t)slot > 0) {
        g_state.object_count++;
        g_state.boat_count++;
    }
}
