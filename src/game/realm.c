/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Realm population and per-cell rules: game/realm.h.
 */
#include "game/realm.h"

#include <stdlib.h>

#include "game/build.h"
#include "game/mapobj.h"
#include "game/sim.h"
#include "game/souls.h"
#include "game/structure.h"
#include "game/tables.h"

/* ---- Raw access ---------------------------------------------------------------
 *
 * The original indexes the grids as base + y * 400 + x * 4 with byte or
 * signed-char coordinates and no checks. Where a coordinate can leave the
 * map these helpers use the flat index y * 100 + x as the original would
 * and ignore anything outside the grid. */

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
static uint8_t sc_u8(int x, int y, int off)
{
    SimCell *c = sc(x, y);
    return c ? c->b[off] : 0;
}
static uint32_t sc_u32(int x, int y, int off)
{
    SimCell *c = sc(x, y);
    return c ? sim_cell_u32(c, off) : 0;
}
static void sc_set_u32(int x, int y, int off, uint32_t v)
{
    SimCell *c = sc(x, y);
    if (c)
        sim_cell_set_u32(c, off, v);
}
static bool in_heaven(void) { return g_world.realm_plane == GS_PLANE_HEAVEN; }

static void redraw(int x, int y, int size, int slot, int a, int b, int c) { mapobj_redraw(x, y, size, slot, a, b, c); }

/* ---- Per-cell values ------------------------------------------------------------ */

/* 0x4306d0 */
void cell_set_vibes(int x, int y, int v)
{
    mc_and(x, y, 0xffffe0ffu);
    mc_or(x, y, (uint32_t)(int32_t)(int8_t)(v + 15) << 8);
}

/* 0x430737 */
int cell_vibes(int x, int y) { return (int8_t)((((mc(x, y) & 0x1f00) >> 8) - 15) & 0xff); }

/* 0x43077b */
unsigned sim_happiness(int x, int y) { return sc_u8(x, y, 1) >> 4; }

/* 0x4307bf */
uint8_t sim_happiness_sum(int x, int y, unsigned n)
{
    uint8_t s = 0;
    for (int cx = (int8_t)x; cx < (int)n + (int8_t)x; cx++)
        for (int cy = (int8_t)y; (int)((int8_t)y - (int)n) < cy; cy--)
            s = (uint8_t)(s + (sc_u8(cx, cy, 1) >> 4));
    return s;
}

/* 0x43094c */
unsigned sim_scan_mark(int x, int y) { return sc_u8(x, y, 0) & 0x10; }

/* 0x430989 */
void sim_clear_scan_marks(void)
{
    for (int i = 0; i < GS_MAP_CELLS; i++)
        g_world.sim[i].b[0] &= 0xcf;
}

/* ---- Rocks and the set-29 network ---------------------------------------------- */

/* The original's flood fills keep 10000-entry stacks of signed-char
 * coordinates; entries are only pushed while the depth is below 10000. */
#define FILL_MAX 10000
static int8_t g_fill_x[FILL_MAX + 2], g_fill_y[FILL_MAX + 2];

static bool fill_open(int x, int y)
{
    return (int8_t)x >= 0 && (int8_t)x < 100 && (int8_t)y >= 0 && (int8_t)y < 100 &&
           (sc_u8((int8_t)x, (int8_t)y, 0) & 0x20) == 0;
}

/* pushes the four neighbours of (x, y) like the original (its first push
 * reuses the slot just popped) */
static int fill_push4(int depth, int8_t x, int8_t y)
{
    int d = depth - 1;
    if (fill_open((int8_t)(x + 1), y) && d < FILL_MAX) {
        g_fill_x[depth] = (int8_t)(x + 1), g_fill_y[depth] = y;
        d = depth;
    }
    if (fill_open((int8_t)(x - 1), y) && d < FILL_MAX) {
        g_fill_x[d + 1] = (int8_t)(x - 1), g_fill_y[d + 1] = y;
        d++;
    }
    if (fill_open(x, (int8_t)(y + 1)) && d < FILL_MAX) {
        g_fill_x[d + 1] = x, g_fill_y[d + 1] = (int8_t)(y + 1);
        d++;
    }
    if (fill_open(x, (int8_t)(y - 1)) && d < FILL_MAX) {
        g_fill_x[d + 1] = x, g_fill_y[d + 1] = (int8_t)(y - 1);
        d++;
    }
    return d;
}

/* 0x430d57: flood fill from (x, y) through every built cell; returns the
 * rock yield of the set-29 cells reached (each gets +1 = its yield,
 * +5 = 0) */
static uint32_t set29_collect(int x, int y)
{
    uint32_t total = 0;
    int depth = 0;
    if (fill_open(x, y)) {
        g_fill_x[1] = (int8_t)x, g_fill_y[1] = (int8_t)y;
        depth = 1;
    }
    while (depth != 0) {
        int8_t cx = g_fill_x[depth], cy = g_fill_y[depth];
        int next = depth - 1;
        sc(cx, cy)->b[0] |= 0x20;
        unsigned set = mc_set(cx, cy);
        if (set == 1) {
            depth = next;
            continue;
        }
        if (set == 14 || set == 15) {
            if (zone_entry_is_empty(mc_entry(cx, cy))) {
                depth = next;
                continue;
            }
        } else if (set == 0x1d) {
            uint32_t v = (uint32_t)rock_value(cx, cy);
            total += v;
            sc_set_u32(cx, cy, 5, 0);
            sc_set_u32(cx, cy, 1, v);
        }
        depth = fill_push4(depth, cx, cy);
    }
    return total;
}

/* 0x431288: flood fill handing `amount` to the zone buildings reached
 * (each takes its level); `mode` 0 only marks. Returns what is left. */
static int32_t set29_supply(int x, int y, int32_t amount, int mode)
{
    int depth = 0;
    if (fill_open(x, y)) {
        g_fill_x[1] = (int8_t)x, g_fill_y[1] = (int8_t)y;
        depth = 1;
    }
    while (depth != 0) {
        int8_t cx = g_fill_x[depth], cy = g_fill_y[depth];
        int next = depth - 1;
        if (amount < 1)
            return 0;
        SimCell *c = sc(cx, cy);
        c->b[0] |= 0x20;
        unsigned set = mc_set(cx, cy);
        if (set == 1) {
            depth = next;
            continue;
        }
        if (set == 14 || set == 15) {
            if (mode == 0) {
                depth = next;
                continue;
            }
            if ((c->b[0] & 0x10) == 0) {
                unsigned e = mc_entry(cx, cy);
                if (!zone_entry_is_empty(e)) {
                    amount -= (int32_t)zone_entry_level(e);
                    if (amount < 1)
                        return 0;
                }
                realm_cur()->unk_80e1++;
            }
        }
        if (mode == 0 && sim_scan_mark(cx, cy)) {
            depth = next;
            continue;
        }
        c->b[0] |= 0x10;
        depth = fill_push4(depth, cx, cy);
    }
    return amount;
}

/* 0x43188a */
void realm_set29_year(void)
{
    sim_clear_scan_marks();
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++)
            if (mc_set(x, y) == 0x1d && !sim_scan_mark(x, y))
                sc_set_u32(x, y, 5, set29_collect(x, y));
    sim_clear_scan_marks();
    PlaneStats *r = realm_cur();
    r->unk_80e3 = 0;
    r->unk_80e1 = 0;
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++) {
            int32_t v;
            if (mc_set(x, y) == 0x1d && (v = (int32_t)sc_u32(x, y, 5)) != 0)
                r->unk_80e3 += (uint32_t)set29_supply(x, y, v, 1);
        }
    if (r->unk_80e3 == 0)
        for (int x = 0; x < 100; x++)
            for (int y = 0; y < 100; y++)
                if (mc_set(x, y) == 0x1d && sc_u32(x, y, 5) == 0)
                    set29_supply(x, y, (int32_t)sc_u32(x, y, 1), 0);
}

/* ---- Population of buildings ----------------------------------------------------- */

static uint32_t building_sum(int x, int y, int off)
{
    uint8_t bx = (uint8_t)x, by = (uint8_t)y;
    if (mc_set(bx, by) == 0) {
        uint8_t ox = sc_u8(bx, by, 10), oy = sc_u8(bx, by, 11);
        bx = ox, by = oy;
    }
    unsigned e = mc_entry(bx, by);
    if (e < 0x5b)
        return sc_u32(bx, by, off);
    unsigned n = (unsigned)tile_index_footprint(e) & 0xff;
    if (n == 1)
        return sc_u32(bx, by, off);
    uint32_t sum = 0;
    for (unsigned cx = bx; cx < (unsigned)bx + n; cx++)
        for (int cy = by; (int)by - (int)n < cy; cy--)
            sum += sc_u32((uint8_t)cx, (uint8_t)cy, off);
    return sum;
}

/* 0x431ca0 */
uint32_t realm_building_perm(int x, int y) { return building_sum(x, y, 2); }
/* 0x431ebf */
uint32_t realm_building_temp(int x, int y) { return building_sum(x, y, 6); }
/* 0x4320de */
uint32_t realm_building_pop(int x, int y) { return realm_building_perm(x, y) + realm_building_temp(x, y); }

/* 0x432112 */
uint32_t realm_building_years_per_soul(int x, int y)
{
    uint32_t pop = realm_building_pop(x, y);
    return pop == 0 ? 0 : sc_u32((uint8_t)x, (uint8_t)y, 10) / pop;
}

/* The per-kind totals: kind 7 lands in the next array like the original. */
static uint32_t *kind_total(PlaneStats *r, int off, int kind)
{
    if ((int8_t)kind == -1)
        return NULL;
    int k = (int8_t)kind;
    if (k < 0 || k > 7)
        return NULL;
    if (off == 2)
        return k < 7 ? &r->unk_7f74[k] : &r->unk_7f90[0];
    return k < 7 ? &r->unk_7f3c[k] : &r->unk_7f58[0];
}

static void pop_add(int x, int y, uint32_t n, int kind, int off)
{
    SimCell *c = sc((uint8_t)x, (uint8_t)y);
    if (c)
        sim_cell_set_u32(c, off, sim_cell_u32(c, off) + n);
    PlaneStats *r = realm_cur();
    if (off == 2)
        g_state.unk_5bb114 += n;
    else
        g_state.unk_57e1b4 += n;
    r->population += n;
    uint32_t *k = kind_total(r, off, kind);
    if (k)
        *k += n;
}

static void pop_sub(int x, int y, uint32_t n, int kind, int off)
{
    PlaneStats *r = realm_cur();
    if (r->population < n)
        n = r->population;
    SimCell *c = sc((uint8_t)x, (uint8_t)y);
    uint32_t have = c ? sim_cell_u32(c, off) : 0;
    if (have < n)
        n = have;
    if (c)
        sim_cell_set_u32(c, off, have - n);
    if (off == 2)
        g_state.unk_5bb114 -= n;
    else
        g_state.unk_57e1b4 -= n;
    r->population -= n;
    uint32_t *k = kind_total(r, off, kind);
    if (k)
        *k = n < *k ? *k - n : 0;
}

/* 0x4324c0 */
void realm_perm_add(int x, int y, uint32_t n, int special, int kind)
{
    (void)special;
    pop_add(x, y, n, kind, 2);
}
/* 0x43258b */
void realm_temp_add(int x, int y, uint32_t n, int special, int kind)
{
    (void)special;
    pop_add(x, y, n, kind, 6);
}
/* 0x43217a */
void realm_perm_sub(int x, int y, uint32_t n, int special, int kind)
{
    (void)special;
    pop_sub(x, y, n, kind, 2);
}
/* 0x43231d */
void realm_temp_sub(int x, int y, uint32_t n, int special, int kind)
{
    (void)special;
    pop_sub(x, y, n, kind, 6);
}

/* ---- Karma helpers ------------------------------------------------------------------- */

/* 0x432656 */
bool karma_piece_is_branch(unsigned entry)
{
    switch (entry & 0xff) {
    case 0: case 1: case 2: case 4: case 5: case 7:
        return false;
    default:
        return true;
    }
}

/* 0x4326c2 */
bool karma_piece_is_through(unsigned entry) { return (entry & 0xff) != 0 && (entry & 0xff) != 7; }

/* 0x432713 */
unsigned karma_station_mark(int x, int y, unsigned bits)
{
    uint8_t ox = (uint8_t)x, oy = (uint8_t)y;
    uint32_t c = mc(ox, oy);
    if ((c >> 27) == 0) {
        ox = (uint8_t)(x - ((c >> 16) & 7));
        oy = (uint8_t)(((c & 0x700000) >> 20) + (unsigned)y);
    }
    world_select_plane(GS_PLANE_HELL);
    if (mc_set(ox, oy) != 0x11)
        world_select_plane(GS_PLANE_HEAVEN);
    SimCell *s = sc(ox, oy);
    int8_t old = s ? (int8_t)s->b[9] : 0;
    if (s)
        s->b[9] |= (uint8_t)bits;
    world_select_plane(GS_PLANE_MIDDLE);
    unsigned r = (unsigned)(int)old;
    return (r & 1) == 0 ? r | bits : 0;
}

/* ---- Vibes and happiness ------------------------------------------------------------- */

/* 0x43a493 */
void realm_influence(int x, int y, int base, int step, bool inner)
{
    int8_t st = (int8_t)step;
    int8_t r = (int8_t)(abs(base) / abs((int)st));
    unsigned n = (unsigned)structure_size(x, y) & 0xff;
    int px = x & 0xff, py = y & 0xff;
    int x0 = px - r;
    if (x0 < 1)
        x0 = 0;
    int x1 = px + (int)n + r;
    if (x1 > 98)
        x1 = 99;
    int y1 = py + r;
    if (y1 > 98)
        y1 = 99;
    int y0 = py - (int)n - r;
    if (y0 < 1)
        y0 = 0;
    for (int8_t lx = (int8_t)x0; lx < (int8_t)x1; lx++)
        for (int8_t ly = (int8_t)y1; (int8_t)y0 < ly; ly--) {
            int v = 0;
            int8_t cur = (int8_t)cell_vibes(lx, ly);
            bool in_x = lx >= px && lx < px + (int)n;
            bool in_y = ly <= py && ly > py - (int)n;
            if (!in_x || !in_y || !inner) {
                uint8_t dx = 0, dy = 0;
                if (!in_x) {
                    int a = abs(lx - (px + (int)n - 1)), b = abs(lx - px);
                    dx = (uint8_t)(b < a ? b : a);
                }
                if (!in_y) {
                    int a = abs(ly - (py - (int)n + 1)), b = abs(ly - py);
                    dy = (uint8_t)(b < a ? b : a);
                }
                int8_t d = (int8_t)(dx + dy);
                if (d <= r) {
                    if (!inner) {
                        if (d > 0)
                            v = (d - 1) * st + base;
                    } else {
                        v = d * st + base;
                    }
                }
            } else {
                v = base;
            }
            if (v != 0) {
                int nv = cur + v;
                if (nv < -15)
                    nv = -15;
                if (nv > 16)
                    nv = 16;
                if (cur != nv)
                    cell_set_vibes(lx, ly, nv);
            }
        }
}

/* 0x43a81d */
void realm_vibes_update(void)
{
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++)
            cell_set_vibes(x, y, 0);
    bool heaven = in_heaven();
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++) {
            if (cell_test_shade(x, y))
                continue;
            uint32_t c = cell_get(x, y);
            unsigned e = (c >> 16) & 0x7f;
            if (cell_test_overlay(x, y))
                realm_influence(x, y, heaven ? -4 : 4, heaven ? 1 : -1, true);
            switch (c >> 27) {
            case 4:
                realm_influence(x, y, (int)(e % 5) * 2 + 10, -1, true);
                break;
            case 5:
                realm_influence(x, y, (int)(e >> 1) * 8 + 16, -1, true);
                break;
            case 7:
                realm_influence(x, y, (int)(e + 10) >> 1, -1, true);
                break;
            case 8:
                realm_influence(x, y, (int)((c >> 16) & 0x7e) + 4, -1, true);
                break;
            case 9:
                realm_influence(x, y, (int)(e >> 1) * 3 + 3, -1, true);
                break;
            case 13:
                realm_influence(x, y, -(int)((e & ~3u) + 4), 2, true);
                break;
            case 14:
            case 15: {
                (void)structure_size(x, y);
                int w = g_world.tile_info[e].unk_13;
                if (!zone_entry_is_empty(e) && w != 0)
                    realm_influence(x, y, w, -w, false);
                break;
            }
            case 16:
                realm_influence(x, y, -((sim_cell(x, y)->b[1] >> 5) + 1), 1, true);
                break;
            case 17:
                realm_influence(x, y, -(int)((e >> 1) * 3 + 3), 1, true);
                break;
            case 18:
                realm_influence(x, y, -4, 1, true);
                break;
            default:
                break;
            }
        }
}

/* 0x43b13a */
int realm_score_years(int x, int y)
{
    uint32_t pop = realm_building_pop(x, y);
    if (pop == 0)
        return 1;
    int32_t q = (int32_t)(sc_u32((uint8_t)x, (uint8_t)y, 10) / pop);
    int s = q < 0x20 ? 0 : q < 0x40 ? 1 : q < 0x80 ? 2 : 3;
    return in_heaven() ? 3 - s : s;
}

/* 0x43b210 */
int realm_score_vibes(int x, int y)
{
    int v = (int)((mc((uint8_t)x, (uint8_t)y) & 0x1f00) >> 8) - 15;
    v /= 4;
    if (v > 3)
        v = 4;
    if (v < -3)
        v = -4;
    if (v < -1)
        realm_cur()->unk_8107++;
    return v;
}

/* 0x43b2a7 */
int realm_score_mix(int x, int y)
{
    uint32_t pop = realm_building_pop(x, y);
    uint32_t temp = realm_building_temp(x, y);
    (void)realm_building_perm(x, y);
    if (pop == 0)
        return 0;
    int32_t diff = (int32_t)((uint32_t)sc_u8((uint8_t)x, (uint8_t)y, 0xf) - (uint32_t)(temp * 0xffu) / pop);
    int d = diff < 0 ? (diff == INT32_MIN ? INT32_MIN : -diff) : diff;
    int s = d < 8 ? 7 : d < 0x12 ? 6 : d < 0x21 ? 5 : d < 0x35 ? 4 : d < 0x4e ? 3 : d < 0x80 ? 2 : 1;
    if (s > 5)
        s = 6;
    if (s < 2)
        realm_cur()->unk_8105++;
    return s;
}

/* 0x43ae85: neighbour points along a line of n cells from (x0, y0) */
static int8_t neighbour_line(int8_t x0, int8_t y0, unsigned set, unsigned entry, unsigned kind, int8_t n, int8_t dx,
                             int8_t dy)
{
    int8_t score = 0;
    bool hell = !in_heaven();
    int8_t x = x0, y = y0;
    do {
        if (x >= 0 && y >= 0 && x < 100 && y < 100) {
            unsigned s = mc_set(x, y);
            int8_t ox = x, oy = y;
            if (s == 0) {
                ox = (int8_t)sc_u8(x, y, 10);
                oy = (int8_t)sc_u8(x, y, 11);
                s = mc_set(ox, oy);
            }
            if (s == set) {
                unsigned e2 = mc_entry(ox, oy);
                if (e2 == entry) {
                    if (hell)
                        score += 3;
                } else {
                    unsigned k2 = e2 / 13;
                    if (k2 > 6)
                        k2 = 7;
                    if (k2 == kind)
                        score += 1;
                    else if (!hell)
                        score += 3;
                }
            } else if (s == 1) {
                score += hell ? 1 : 2;
            } else if (s == 12) {
                score += hell ? 2 : 1;
            }
        }
        x = (int8_t)(x + dx);
        y = (int8_t)(y + dy);
    } while ((int8_t)(n + x0) != x && (int8_t)(n + y0) != y);
    return score;
}

/* 0x43b469 */
int realm_score_neighbours(int x, int y)
{
    int8_t sx = (int8_t)x, sy = (int8_t)y;
    uint8_t n = (uint8_t)structure_size(x, y);
    uint32_t c = mc(sx, sy);
    unsigned e = (c >> 16) & 0x7f;
    unsigned kind = e / 13;
    if (kind > 6)
        kind = 7;
    unsigned set = c >> 27;
    int8_t a = neighbour_line((int8_t)(sx - 1), (int8_t)(sy - n), set, e, kind, (int8_t)(n + 1), 1, 0);
    int8_t b = neighbour_line((int8_t)(sx + n), (int8_t)(sy - n), set, e, kind, (int8_t)(n + 1), 0, 1);
    int8_t cc = neighbour_line(sx, (int8_t)(sy + 1), set, e, kind, (int8_t)(n + 1), 1, 0);
    int8_t d = neighbour_line((int8_t)(sx - 1), (int8_t)(sy - n - 1), set, e, kind, (int8_t)(n + 1), 0, 1);
    int8_t s = (int8_t)((int8_t)(d + cc + b + a) / (int)(n * 2 + 2));
    if (s > 2)
        s = 3;
    if (s < 1)
        s = 0;
    if (s < 1)
        realm_cur()->unk_8109++;
    return s;
}

/* 0x43b63e */
static void zone_happiness(int x, int y)
{
    int a = realm_score_years(x, y);
    int b = realm_score_vibes(x, y);
    int c = realm_score_mix(x, y);
    int s = a + b + c + realm_score_neighbours(x, y);
    if (s > 14)
        s = 15;
    if (s < 1)
        s = 0;
    unsigned n = (unsigned)tile_index_footprint(cell_index(x, y)) & 0xff;
    g_sim.happiness_sum += n * n * (uint32_t)s;
    g_state.unk_593250++;
    if (cell_get(x, y) & 4) {
        if (in_heaven()) {
            s >>= 1;
        } else {
            s *= 2;
            if (s > 14)
                s = 15;
        }
    }
    for (unsigned cx = (unsigned)x; cx < n + (unsigned)x; cx++)
        for (int cy = y; y - (int)n < cy; cy--) {
            SimCell *p = sc((uint8_t)cx, (uint8_t)cy);
            if (p)
                p->b[1] = (uint8_t)((p->b[1] & 0xf) | (uint8_t)(s << 4));
        }
}

/* 0x43b86f */
void realm_happiness_update(void)
{
    PlaneStats *r = realm_cur();
    r->unk_8105 = r->unk_8107 = r->unk_8109 = 0;
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++) {
            unsigned set = cell_tileset(x, y);
            if (set == 14 || set == 15)
                zone_happiness(x, y);
        }
}

/* ---- Growth ----------------------------------------------------------------------------- */

/* 0x43d2fc */
bool zone_entry_is_empty(unsigned entry) { return entry < 0x5c && entry % 13 == 0; }

/* 0x43ea54 */
unsigned zone_entry_level(unsigned entry)
{
    entry &= 0xff;
    return entry < 0x5b ? entry % 13 : entry - 0x5b;
}

/* 0x43b9ac: level range of an entry */
static LevelRange entry_levels(unsigned entry)
{
    if (entry < 0x5b)
        return g_tables.zone_levels[entry % 13];
    unsigned i = (uint8_t)(entry - 0x5b);
    if (i < 21)
        return g_tables.special_levels[i];
    LevelRange none = {0, 0}; /* past the table: the original reads other globals */
    return none;
}

/* 0x43ba1f */
static bool merge_ok(unsigned entry, unsigned e2, unsigned f, unsigned cur, unsigned maxn)
{
    if (entry < 0x5b && e2 < 0x5b && e2 / 13 == entry / 13 && cur + f <= maxn)
        return true;
    return entry < 0x70 && e2 < 0x70 && entry > 0x5a && e2 > 0x5a && cur + f <= maxn;
}

/* 0x43bac3: (x, y) becomes a part of the building with origin (ox, oy) */
static void make_part(int x, int y, int ox, int oy)
{
    uint32_t perm = sim_perm(x, y), temp = sim_temp(x, y);
    cell_set_tileset(x, y, 0);
    SimCell *s = sim_cell(x, y);
    s->b[10] = (uint8_t)ox;
    s->b[11] = (uint8_t)oy;
    cell_set_index(x, y, (uint8_t)(((oy - y) * 16 - ox) + x));
    sim_cell_set_u32(s, 2, perm);
    sim_cell_set_u32(s, 6, temp);
}

/* 0x43bbe7 */
void realm_building_replace(int x, int y, unsigned set, unsigned entry, unsigned n)
{
    uint32_t old = cell_get(x, y);
    cell_set_index(x, y, entry);
    int8_t vdx = g_state.view_dir_x, vdy = g_state.view_dir_y;
    if (n > 1) {
        for (unsigned lx = (unsigned)x; lx < (unsigned)x + n; lx++)
            for (int ly = y; ly > y - (int)n; ly--) {
                cell_set_strip((int)lx, ly, 0);
                if ((int)lx != x || ly != y)
                    make_part((int)lx, ly, x, y);
            }
        uint8_t by = (uint8_t)y, sx;
        if (vdx < 1) {
            sx = (uint8_t)(n + (unsigned)x - 1);
            if (vdy < 1)
                by = (uint8_t)(y - (int)n + 1);
        } else {
            if (vdy < 1)
                by = (uint8_t)(y - (int)n + 1);
            sx = (uint8_t)x;
        }
        uint8_t cx = sx, s = 1;
        int guard = 0;
        do {
            cell_set_strip(cx, by, s);
            cx = (uint8_t)(cx + vdx);
            s++;
        } while ((int)n * vdx + sx != cx && ++guard < 256);
        uint8_t lx = (uint8_t)(cx - vdx);
        uint8_t ly = (uint8_t)(by - vdy);
        guard = 0;
        do {
            if (lx < 100 && ly < 100)
                cell_set_strip(lx, ly, s);
            s++;
            ly = (uint8_t)(ly - vdy);
            if ((int)by - (int)n * vdy == ly)
                break;
        } while (n * 2 != s && ++guard < 256);
    }
    cell_set_tileset(x, y, set);
    redraw(x, y, (int)n, 0, (int)(old >> 27), (int)((old >> 16) & 0x7f) + 1, 1);
    if (set == 15 || set == 14) {
        unsigned kind = entry < 0x5b ? entry / 13 : 0;
        for (int ly = y; y - (int)n < ly; ly--)
            for (unsigned lx = (unsigned)x; lx < n + (unsigned)x; lx++) {
                g_world.cells[ly * GS_MAP_W + (int)lx] &= 0xffffffdbu;
                if (entry < 0x5b) {
                    sim_set_zone_kind((int)lx, ly, kind);
                    if (y != ly || x != (int)lx) {
                        uint32_t p = sim_perm((int)lx, ly), t = sim_temp((int)lx, ly);
                        SimCell *o = sim_cell(x, y);
                        sim_cell_set_u32(o, 2, sim_cell_u32(o, 2) + p);
                        sim_cell_set_u32(o, 6, sim_cell_u32(o, 6) + t);
                        sim_cell_set_u32(sim_cell((int)lx, ly), 2, 0);
                        sim_cell_set_u32(sim_cell((int)lx, ly), 6, 0);
                    }
                }
            }
        uint32_t pop = realm_building_pop(x, y);
        uint32_t cap = g_world.tile_info[entry].cost; /* +0: the building's capacity */
        if (cap < pop)
            souls_zone_evict(x, y, pop - cap);
    } else {
        for (int ly = y; y - (int)n < ly; ly--)
            for (unsigned lx = (unsigned)x; lx < (unsigned)x + n; lx++)
                g_world.cells[ly * GS_MAP_W + (int)lx] &= 0xffffffdbu;
    }
    if (g_sim_hooks.views_changed)
        g_sim_hooks.views_changed();
}

/* 0x43c533 */
void realm_building_upgrade(int x, int y, unsigned n, unsigned level, unsigned entry, unsigned set)
{
    bool found = false;
    unsigned lo = 0, hi = 0, base = 0, ne = entry;
    level &= 0xff;
    n &= 0xff;
    if (entry < 0x5b) {
        base = (entry / 13) * 13;
        if (n == 1)
            lo = 1, hi = 4;
        else if (n == 2)
            lo = 5, hi = 11;
        else if (n == 3)
            ne = base + 12;
        if (lo != 0) {
            unsigned i = lo;
            do {
                if (level < g_tables.zone_levels[i].lo || g_tables.zone_levels[i].hi < level)
                    i++;
                else
                    found = true;
            } while (i <= hi && !found);
            if (!found)
                i = lo;
            ne = base + i;
        }
    } else if (entry < 0x70) {
        base = 0x5b;
        switch (n) {
        case 1: lo = 1, hi = 4; break;
        case 2: lo = 5, hi = 0x11; break;
        case 3: lo = 0x12, hi = 0x13; break;
        case 4: ne = 0x6f; break;
        }
        if (lo != 0) {
            unsigned i = lo;
            do {
                if (level < g_tables.special_levels[i].lo || g_tables.special_levels[i].hi < level)
                    i++;
                else
                    found = true;
            } while (i <= hi && !found);
            if (!found)
                i = lo;
            ne = i + 0x5b;
        }
    }
    int plane = set == 14 ? 1 : 3;
    uint8_t mask = 0xff;
    for (int8_t lx = (int8_t)x; lx < (int)n + (int8_t)x; lx++)
        for (int8_t ly = (int8_t)y; (int)(y - (int)n) < ly; ly--)
            mask &= (uint8_t)sim_scan_mark(lx, ly);
    if (mask == 0) {
        if (n > 2)
            return;
        unsigned v = (unsigned)((int)(hi - lo) / 2) + lo + base;
        if ((int)ne > (int)v)
            ne = v;
        ne &= 0xff;
    }
    if (ne != entry && (((unsigned)tile_index_footprint(entry) & 0xff) == n ||
                        g_world.tile_info[ne].cost >> 2 <= realm_cur()->population)) {
        int delay = rand_range(0, 20);
        int slot = mapobj_spawn(x, y, (int)set, (int)ne, 0, 0, delay + 2, (int)n, plane, 9, 0xffffffffu, 1, 0);
        if ((int8_t)slot < 0) {
            realm_building_replace(x, y, set, ne, n);
        } else {
            g_state.object_count++;
            for (int i = 0; i < (int)n; i++)
                for (int j = 0; j < (int)n; j++)
                    mc_or(x + i, y - j, 0x20);
        }
    }
}

/* the chance test of 0x43c9a0: (pop * 16 / capacity + 1) against a draw */
static unsigned crowding(uint32_t pop, unsigned entry)
{
    uint32_t cap = g_world.tile_info[entry].cost;
    return (uint8_t)((uint8_t)((pop << 4) / cap) + 1);
}

/* 0x43c9a0 */
void realm_zone_grow(int x, int y, unsigned entry, unsigned n, unsigned level)
{
    unsigned set = cell_tileset(x, y);
    n &= 0xff;
    level &= 0xff;
    uint8_t maxn;
    if (entry < 0x5b || entry > 0x6e)
        maxn = entry < 0x5b ? 3 : 2;
    else
        maxn = 4;
    if (100 - maxn < x)
        maxn = (uint8_t)(100 - x);
    if (y < maxn - 1)
        maxn = (uint8_t)(y + 1);
    LevelRange lr = entry_levels(entry);
    if (level < lr.lo) {
        uint32_t pop;
        if (n < 3 && (pop = realm_building_pop(x, y)) != 0) {
            unsigned cr = crowding(pop, entry);
            if ((int)cr <= rand_range(6, 0x10)) {
                cell_clear_flags(x, y, 2);
                realm_building_upgrade(x, y, n, level, entry, set);
            }
        }
        return;
    }
    if (n >= maxn) {
        if (lr.hi <= level)
            realm_building_upgrade(x, y, n, level, entry, set);
        return;
    }
    uint8_t total = (uint8_t)level, cur = (uint8_t)n, best = 0, reach = 0;
    bool ok;
    do {
        uint8_t cx = (uint8_t)(x + cur), cy = (uint8_t)y, acc = 0;
        do {
            ok = false;
            if (cx < 100 && cy < 100 && (mc(cx, cy) & 0x20) == 0) {
                unsigned s = mc_set(cx, cy);
                if (s == 14 || s == 15) {
                    if (!sim_test_flag_e7(cx, cy)) {
                        unsigned e2 = mc_entry(cx, cy);
                        uint8_t f = (uint8_t)tile_index_footprint(e2);
                        if (merge_ok(entry, e2, f, cur, maxn)) {
                            if (best < (uint8_t)(f + cur))
                                best = (uint8_t)(f + cur);
                            uint32_t p = realm_building_pop(cx, cy);
                            if (p != 0 && rand_range(6, 0x10) <= (int)crowding(p, e2) && rand_range(0, cur) != 0) {
                                acc = (uint8_t)(acc + sim_happiness_sum(cx, cy, f));
                                ok = true;
                            }
                        }
                    }
                } else if (s == 0 && (unsigned)x <= sc_u8(cx, cy, 10) && sc_u8(cx, cy, 11) <= (unsigned)y) {
                    ok = true;
                }
            }
            cy--;
        } while (y - (int)cur <= (int)cy && ok);
        cy = (uint8_t)(y - cur);
        cx = (uint8_t)x;
        do {
            if (!ok)
                break;
            ok = false;
            if (cx < 100 && cy < 100 && (mc(cx, cy) & 0x20) == 0) {
                unsigned s = mc_set(cx, cy);
                if (s == 14 || s == 15) {
                    if (!sim_test_flag_e7(cx, cy)) {
                        unsigned e2 = mc_entry(cx, cy);
                        uint8_t f = (uint8_t)tile_index_footprint(e2);
                        if (merge_ok(entry, e2, f, cur, maxn)) {
                            if (best < (uint8_t)(f + cur))
                                best = (uint8_t)(f + cur);
                            uint32_t p = realm_building_pop(cx, cy);
                            if (p != 0 && rand_range(4, 0x10) <= (int)crowding(p, e2)) {
                                acc = (uint8_t)(acc + sim_happiness_sum(cx, cy, f));
                                ok = true;
                            }
                        }
                    }
                } else if (s == 0 && (unsigned)x <= sc_u8(cx, cy, 10) && sc_u8(cx, cy, 11) <= (unsigned)y) {
                    ok = true;
                }
            }
            cx++;
        } while (cx < (unsigned)x + cur);
        if (ok) {
            unsigned need = 0;
            if (cur == 2)
                need = entry < 0x5b ? g_tables.zone_levels[12].lo : g_tables.special_levels[18].lo;
            else if (cur == 3)
                need = 0xb8;
            if ((unsigned)acc + total < need) {
                ok = false;
            } else {
                if (reach < best)
                    reach = best;
                total = (uint8_t)(acc + total);
                cur++;
            }
        }
    } while (cur < maxn && ok);
    if (n < cur && reach <= cur)
        realm_building_upgrade(x, y, cur, total, entry, set);
    else if (lr.hi <= level)
        realm_building_upgrade(x, y, n, level, entry, set);
}

/* 0x43d259 */
void realm_zone_decay(int x, int y, unsigned entry, unsigned n, unsigned level)
{
    uint32_t c = cell_get(x, y);
    if ((level & 0xff) < entry_levels(entry).lo) {
        cell_clear_flags(x, y, 2);
        realm_building_upgrade(x, y, n, level, entry, c >> 27);
    }
}

/* 0x43c1b1 */
void realm_specials_dedupe(void)
{
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++) {
            unsigned set = cell_tileset(x, y);
            unsigned e = cell_index(x, y);
            if ((set != 15 && set != 14) || e <= 0x5f)
                continue;
            unsigned n = (unsigned)tile_index_footprint(e) & 0xff;
            uint32_t mask = 0;
            for (int sy = y; y - (int)n < sy; sy--)
                for (int sx = x; sx < (int)n + x; sx++) {
                    unsigned k = sim_zone_kind(sx, sy);
                    if (k == 7)
                        continue;
                    if ((mask & 1u << k) == 0) {
                        mask |= 1u << k;
                        continue;
                    }
                    sim_set_zone_kind(sx, sy, 7);
                    bool moved = false;
                    int dx = x, dy = y;
                    for (;;) {
                        unsigned k2 = sim_zone_kind(dx, dy);
                        if (k2 == k || k2 == 7) {
                            uint32_t p = sim_perm(sx, sy), t = sim_temp(sx, sy);
                            SimCell *d = sim_cell(dx, dy);
                            sim_cell_set_u32(d, 2, sim_cell_u32(d, 2) + p);
                            sim_cell_set_u32(d, 6, sim_cell_u32(d, 6) + t);
                            sim_cell_set_u32(sim_cell(sx, sy), 2, 0);
                            sim_cell_set_u32(sim_cell(sx, sy), 6, 0);
                            moved = true;
                        }
                        if (dx + 1 < (int)n + x) {
                            dx++;
                            if (!moved)
                                continue;
                            break;
                        }
                        dy--;
                        if (y - (int)n < dy && !moved) {
                            dx = x;
                            continue;
                        }
                        break;
                    }
                }
        }
}

/* ---- Milestones and special arrangements ---------------------------------------------- */

static void news(unsigned id)
{
    if (g_sim_hooks.news)
        g_sim_hooks.news(id);
}

/* 0x43d9c0 */
void realm_milestones(void)
{
    bool hell = g_world.plane == GS_PLANE_HELL;
    uint8_t c = hell ? 9 : 4;
    int8_t d = hell ? 7 : 6;
    unsigned big = hell ? 0x11 : 0x10;
    uint32_t pop = realm_cur()->population;
    if (pop > 999999999u && (g_state.unk_63042c & 1u << big) == 0) {
        if (g_sim_hooks.music_fade)
            g_sim_hooks.music_fade(0x5a);
        news(hell ? 0x3b4 : 0x3b6);
        g_state.unk_63042c |= 1u << big;
        g_sim.milestone_shown = 1;
    }
    uint32_t t = 500000000;
    do {
        if (pop >= t && g_state.unk_5f4cd0[0][0] == 0 && (g_state.unk_63042c & 1u << (c & 31)) == 0) {
            news((unsigned)c * 2 + 900);
            g_state.unk_63042c |= 1u << (c & 31);
            if (d >= 0)
                g_state.unk_5f746c |= 5u << (d & 31);
            g_sim.milestone_shown = 1;
            return;
        }
        if (t < 500000000)
            d = (int8_t)(d - 2);
        t /= 10;
        c--;
    } while (t > 9999);
}

/* 0x43db51 */
void realm_mark_special_site(unsigned i)
{
    i &= 0xff;
    if (i >= 10)
        return;
    uint8_t x = g_build.special_pos[i][0], y = g_build.special_pos[i][1];
    if (x < 100 && y < 100 && (cell_get(x, y) & 2) == 0) {
        cell_set_flags(x, y, 2);
        mapobj_place(x, y, (int)i, 1, 6, g_world.plane, 4, 0xc1);
        redraw(x, y, 4, 0, 4, (int)i + 1, 1);
    }
}

/* 0x43e59d */
bool realm_secret_structure(int x, int y, unsigned entry)
{
    bool heaven = g_world.plane != GS_PLANE_HELL;
    unsigned zset = heaven ? 15 : 14;
    int8_t px = (int8_t)x, py = (int8_t)y;
    unsigned size = 0;
    uint8_t k;
    bool ok = false;
    /* the original loops k from (plane != Hell) to the same value: one
     * arrangement per realm, 0 in Hell and 1 in Heaven */
    for (k = heaven ? 1 : 0; k <= (heaven ? 1 : 0); k++) {
        ok = false;
        if (g_state.unk_5b2f5a & 1u << k)
            continue;
        if (k == 0) {
            size = 7;
            if (entry < 0x5a && px + 6 < 100 && py + 6 < 100) {
                ok = true;
                unsigned want = (entry / 13) * 13;
                uint8_t ly = (uint8_t)py;
                do {
                    uint8_t lx = (uint8_t)px;
                    do {
                        if (px != lx || py != ly) {
                            if (mc_set(lx, ly) == zset) {
                                unsigned e = mc_entry(lx, ly);
                                if (e < want || want + 4 < e)
                                    ok = false;
                            } else {
                                ok = false;
                            }
                        }
                        want += 13;
                        if (want > 0x5a)
                            want = 0;
                        lx++;
                    } while (lx < px + 7 && ok);
                    want += 13;
                    if (want > 0x5a)
                        want = 0;
                    ly++;
                } while (ly < py + 7 && ok);
            }
            if (ok)
                py = (int8_t)(py + 6);
        } else {
            size = 2;
            if (entry > 0x5a && entry % 13 == 0 && px > 2 && py > 2 && px < 0x60 && py < 0x61) {
                ok = true;
                uint8_t lx = (uint8_t)(px - 3);
                do {
                    uint8_t ly = (uint8_t)(py - 4);
                    do {
                        bool out_x = lx < px || px + 1 < lx;
                        bool out_y = ly < py - 1 || py < ly;
                        if (out_x && out_y) {
                            if (mc_set(lx, ly) != 0x10)
                                ok = false;
                        } else if (out_x || out_y) {
                            if (mc_set(lx, ly) != 0xc)
                                ok = false;
                        } else if (mc_set(lx, ly) == 0xf) {
                            if (mc_entry(lx, ly) < 0x5b)
                                ok = false;
                        } else {
                            ok = false;
                        }
                        ly++;
                    } while (ly < py + 4 && ok);
                    lx++;
                } while (lx < px + 5 && ok);
            }
        }
        if (ok)
            break;
    }
    if (!ok)
        return false;
    g_state.unk_5b2f5a |= (uint16_t)(1u << k);
    realm_building_replace((uint8_t)px, (uint8_t)py, 3, k, size);
    return true;
}

/* 0x43ea93 */
void realm_building_ruin(int x, int y, unsigned n)
{
    structure_destroy(x, y, 3, 0xc);
    if (rand_range(0, 4) == 0)
        for (unsigned lx = (unsigned)x; lx < n + (unsigned)x; lx++)
            for (int ly = y; y - (int)n < ly; ly--) {
                unsigned e = (unsigned)(uint8_t)rand_range(0x11, 0x14);
                map_stamp_building((int)lx, ly, 10, e, 1, true);
                redraw((int)lx, ly, 1, 0, 0, 0, 1);
            }
}

/* ---- SOULs -------------------------------------------------------------------------------- */

/* 0x445416 */
void realm_age_departures(void)
{
    PlaneStats *r = realm_cur();
    for (int k = 0; k < 7; k++)
        for (int j = 0; j < 16; j++) {
            PlaneStatHist *h = &r->hist_a[k * 16 + j];
            uint32_t dep = h->unk_04;
            uint32_t left = h->unk_00 - h->v[0] - h->v[1];
            uint32_t done = 0;
            if (dep != 0 && left != 0) {
                for (int b = 2; b < 32; b++) {
                    uint32_t have = h->v[b];
                    if (have == 0)
                        continue;
                    uint32_t take = mul_div_clamp(have, left, dep);
                    if (take == 0 && (uint32_t)rand_range(0, (int)left) < have)
                        take = 1;
                    if (have < take)
                        take = have;
                    if (dep < done + take)
                        take = dep - done;
                    if (take != 0) {
                        h->v[b] -= take;
                        h->v[b - 1] += take;
                        done += take;
                    }
                }
            }
            h->unk_04 = 0;
        }
}

/* 0x65edf0 / 0x65ee00 / 0x65ee10: per belief type: fate (1 HAHA both,
 * 2 HOHO either, 3 OPRA Hell, 4 OCRA Heaven), 5/6 (5 = counted in the
 * belief statistics), 7/8 (8 = judged by the strongest sin/virtue only) */
static const uint8_t g_belief_fate[16] = {1, 2, 3, 4, 1, 2, 3, 4, 1, 2, 3, 4, 1, 2, 3, 4};
static const uint8_t g_belief_stat[16] = {5, 5, 5, 5, 6, 6, 6, 6, 5, 5, 5, 5, 6, 6, 6, 6};
static const uint8_t g_belief_single[16] = {7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8};

static void hist_add(int plane, int kind, int type, int bucket, uint32_t n)
{
    PlaneStatHist *h = &g_state.plane[plane].stats.hist_b[kind * 16 + type];
    h->unk_00 += n;
    if (bucket >= 0 && bucket < 32)
        h->v[bucket] += n;
}

/* 0x5bb210 / 0x5bb22c / 0x5bb248: three consecutive u32[7] */
static void belief_stat_add(int i, uint32_t n)
{
    if (i >= 0 && i < 7)
        g_state.unk_5bb210[i] += n;
    else if (i >= 7 && i < 14)
        g_state.unk_5bb22c[i - 7] += n;
    else if (i >= 14 && i < 21)
        g_state.unk_5bb248[i - 14] += n;
}

/* 0x445785 */
void realm_souls_from_planet(int sx, int sy, int type, uint32_t count, uint32_t size)
{
    int p = type & 0xff;
    if (p >= 16)
        return;
    uint8_t fate = g_belief_fate[p], stat = g_belief_stat[p], single = g_belief_single[p];
    const PlanetSector *sec = &g_state.sectors[sx][sy];
    for (uint32_t it = 0; it < size; it++) {
        if (g_state.unk_5fa61d >= g_state.unk_5b2f58)
            g_state.unk_5fa61d = 0;
        if (g_state.unk_57e067 >= g_state.unk_630426)
            g_state.unk_57e067 = 0;
        int a[7];
        int sum = 0, vmin = 65000, vmax = -65000, imin = 0, imax = 0;
        for (int s = 0; s < 7; s++) {
            int lvl = -(int)(int8_t)sec->sin[s];
            int r1 = crt_rand(), r2 = crt_rand(), r3 = crt_rand();
            int v = (r1 & 0x7f) + (r2 & 0x7f) + r3 % 17 + lvl - 0x87;
            if (v > 0x7f)
                v = 0xfe - v;
            if (v < -0x7f)
                v = -0xfe - v;
            a[s] = (int)g_state.unk_593260[g_state.unk_593240[s] % 7][1] * v;
            sum += a[s];
            if (vmax < a[s])
                vmax = a[s], imax = s;
            if (a[s] < vmin)
                vmin = a[s], imin = s;
        }
        int k, plane;
        if (single == 8) {
            if (fate == 3 || (fate != 4 && sum > 0)) {
                k = stat == 5 ? imax + 7 : imax;
                plane = GS_PLANE_HELL;
                hist_add(GS_PLANE_HELL, imax, p, vmax < 1 ? 0 : vmax / 0x421, count);
            } else {
                k = imin + 7;
                plane = GS_PLANE_HEAVEN;
                hist_add(GS_PLANE_HEAVEN, imin, p, vmin < 1 ? abs(vmin / 0x421) : 0, count);
            }
        } else if (fate == 3 || (fate == 2 && sum > 0)) {
            k = g_state.unk_593240[0];
            plane = GS_PLANE_HELL;
            for (int s = 0; s < 7; s++)
                hist_add(GS_PLANE_HELL, s, p, a[s] < 1 ? 0 : a[s] / 0x421, count);
            if (stat == 5) {
                belief_stat_add((p - 1) * 7 + imax, count);
                g_state.unk_5fa660[p & 3] += count;
            }
        } else if (fate == 4 || fate == 2) {
            k = g_state.unk_593240[6];
            plane = GS_PLANE_HEAVEN;
            for (int s = 0; s < 7; s++)
                hist_add(GS_PLANE_HEAVEN, s, p, a[s] < 1 ? abs(a[s] / 0x421) : 0, count);
            if (stat == 5) {
                belief_stat_add((p - 1) * 7 + imin, count);
                g_state.unk_5fa660[p & 3] += count;
            }
        } else {
            k = g_state.unk_593240[0];
            plane = GS_PLANE_HELL;
            for (int s = 0; s < 7; s++) {
                int b3 = a[s] < 1 ? abs(a[s] / 0x421) : 0;
                int b1 = a[s] < 1 ? 0 : a[s] / 0x421;
                hist_add(GS_PLANE_HELL, s, p, b1, count);
                hist_add(GS_PLANE_HEAVEN, s, p, b3, count);
            }
            if (stat == 5) {
                int i = vmin < 1 ? 7 + imin : imin; /* 0x6303ec / 0x6303d0, flat [7][2] */
                g_state.unk_6303d0[i >> 1][i & 1] += count;
                g_state.unk_5fa660[p & 3] += count;
            }
        }
        if (k >= 0 && k < 16) {
            PlaneStatRow *row = &g_state.plane[plane].stats.unk_7a08[k];
            row->v[p] += count;
            row->unk_00 += count;
        }
    }
}

/* ---- Workers and ports ---------------------------------------------------------------- */

static bool overflow_set(unsigned s)
{
    switch (s) {
    case 7: case 8: case 9: case 0xd: case 0xe: case 0xf: case 0x11: case 0x1d: case 0x1e:
        return true;
    default:
        return false;
    }
}

/* 0x446242 */
void realm_worker_overflow(int from, int to)
{
    int saved = g_world.plane;
    bool changed = false;
    world_select_plane(from);
    PlaneStats *r = realm_cur();
    int32_t excess = 0, deficit = 0;
    bool surplus;
    if (r->unk_7ff0[4] == 0) {
        deficit = (int32_t)r->unk_7ff0[5];
        surplus = false;
    } else {
        surplus = r->unk_7ff0[5] <= r->unk_7ff0[4];
        if (surplus)
            excess = (int32_t)(r->unk_7ff0[4] - r->unk_7ff0[5]);
        else
            deficit = (int32_t)(r->unk_7ff0[5] - r->unk_7ff0[4]);
    }
    if (excess > 100 || deficit != 0) {
        world_select_plane(to);
        int8_t x = (int8_t)rand_range(1, 0x62);
        int8_t y = (int8_t)rand_range(1, 0x62);
        int8_t outer = 0;
        uint32_t moved = 0;
        do {
            int8_t inner = 0;
            do {
                unsigned s = mc_set(x, y);
                if (overflow_set(s)) {
                    if (excess == 0) {
                        if (cell_test_bracket(x, y)) {
                            int32_t j = (int32_t)structure_upkeep(x, y, s);
                            deficit += j * -5;
                            if (deficit < 1)
                                deficit = 0;
                            moved += (uint32_t)(j * 5);
                            unsigned n = (unsigned)structure_size(x, y) & 0xff;
                            for (int8_t lx = x; lx < (int)x + (int)n; lx++)
                                for (int8_t ly = y; (int)y - (int)n < ly; ly--)
                                    mc_and(lx, ly, 0xffff7fffu);
                            redraw(x, y, (int)n, 0, 10, (int)n * 2 + 0x24, 1);
                        }
                        if (deficit == 0)
                            outer = inner = 's';
                    } else {
                        if (!cell_test_bracket(x, y) && (mc(x, y) & 0x20) == 0) {
                            int32_t j = (int32_t)structure_upkeep(x, y, s);
                            if (j * 5 <= excess) {
                                moved += (uint32_t)(j * 5);
                                excess += j * -5;
                                unsigned n = (unsigned)structure_size(x, y) & 0xff;
                                for (int8_t lx = x; lx < (int)x + (int)n; lx++)
                                    for (int8_t ly = y; (int)y - (int)n < ly; ly--) {
                                        mc_or(lx, ly, 0x8000);
                                        changed = true;
                                    }
                                redraw(x, y, (int)n, 0, 10, (int)n * 2 + 0x25, 1);
                            }
                        }
                        if (excess == 0)
                            outer = inner = 's';
                    }
                }
                y++;
                if (y > 99)
                    y = 0;
                inner++;
            } while (inner < 100);
            x++;
            if (x > 99)
                x = 0;
            outer++;
        } while (outer < 100);
        if (changed) {
            uint32_t id = from == GS_PLANE_HEAVEN ? (uint32_t)rand_range(0x10042, 0x10043)
                                                  : (uint32_t)rand_range(0x10040, 0x10041);
            if (g_sim_hooks.sound)
                g_sim_hooks.sound(id, 3);
        }
        world_select_plane(from);
        r = realm_cur();
        if (surplus)
            r->unk_7ff0[5] += moved;
        else if (r->unk_7ff0[5] < moved)
            r->unk_7ff0[5] = 0;
        else {
            r->unk_7ff0[5] -= moved;
            if (r->unk_7ff0[4] < r->unk_7ff0[5])
                r->unk_7ff0[5] = r->unk_7ff0[4];
        }
    }
    world_select_plane(saved);
}

/* 0x446829 */
void realm_ports_year(void)
{
    PlaneStats *r = realm_cur();
    for (int i = 0; i < (int8_t)r->n_ports && i < 50; i++) {
        uint8_t x = r->ports[i][0], y = r->ports[i][1];
        sc_set_u32(x, y, 5, sc_u32(x, y, 1));
        sc_set_u32(x, y, 1, 0);
    }
}

/* 0x447e8f */
void realm_ports_animate(void)
{
    PlaneStats *r = realm_cur();
    for (int i = 0; i < (int8_t)r->n_ports && i < 50; i++) {
        int8_t x = (int8_t)r->ports[i][0], y = (int8_t)r->ports[i][1];
        uint32_t traffic = sc_u32(x, y, 1);
        uint32_t c = mc(x, y);
        unsigned e = (c >> 16) & 0x7f;
        int32_t cap = (int32_t)g_tables.set13[(e >> 1) & 7].capacity;
        if (c & 4)
            cap >>= 1;
        if ((c & 0x10000) == 0) {
            if ((uint32_t)rand_range(1, cap) < traffic) {
                cell_set_index(x, y, e + 1);
                redraw(x, y, 3, 0, 0, 0, 1);
            }
        } else if (traffic < (uint32_t)rand_range(0, cap - 1)) {
            cell_set_index(x, y, e - 1);
            redraw(x, y, 3, 0, 0, 0, 1);
        }
    }
}

/* 0x42dcdf / 0x42dc42: the two fill gauges of a siphon (redraw test) */
static int8_t siphon_gauge1(int x, int y, unsigned e)
{
    return (int8_t)(-1 - (int8_t)mul_div_clamp(sc_u32(x, y, 5), g_tables.set30[e % 3].capacity, 9));
}
static uint8_t siphon_gauge2(int x, int y, unsigned e)
{
    uint32_t full = g_tables.set30[e % 3].capacity * 0x2a;
    uint32_t v = sc_u32(x, y, 9);
    if (full <= v)
        v = full;
    uint8_t b = (uint8_t)(10 - (uint8_t)mul_div_clamp(full - v, full, 9));
    return b > 8 ? 9 : b;
}

/* 0x42dd50 */
void realm_siphons_fill(void)
{
    uint32_t pool = realm_cur()->unk_7f38;
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++) {
            if (mc_set(x, y) != 0x1e)
                continue;
            unsigned e = mc_entry(x, y);
            if (e >= 3)
                continue; /* past the table (the original reads other globals) */
            int8_t g1 = siphon_gauge1(x, y, e);
            uint8_t g2 = siphon_gauge2(x, y, e);
            uint32_t take = g_tables.set30[e].capacity;
            if (pool <= take)
                take = pool;
            sc_set_u32(x, y, 1, take);
            sc_set_u32(x, y, 5, take);
            sc_set_u32(x, y, 9, sc_u32(x, y, 9) + take);
            pool -= take;
            if (siphon_gauge1(x, y, e) != g1 || siphon_gauge2(x, y, e) != g2)
                redraw(x, y, g_tables.set30[e].size, 0, 0, 0, 1);
        }
}

/* 0x448077 */
void realm_hist_totals(void)
{
    PlaneStats *r = realm_cur();
    for (int j = 0; j < 16; j++)
        for (int k = 0; k < 7; k++) {
            PlaneStatHist *h = &r->hist_a[k * 16 + j];
            uint32_t s = 0;
            for (int b = 0; b < 32; b++)
                s += h->v[b];
            h->unk_00 = s;
        }
}
