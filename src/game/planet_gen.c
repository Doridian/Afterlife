/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Planet world generation (ALIFE.EXE 0x496e71-0x49818e): ice caps, four
 * continents with mountain ranges and the first tribe, run by
 * game_reset_world for a new game. See game/planet.h.
 *
 * The generator draws from the shared RNG (rand_range and, in the continent
 * walk, the CRT rand directly) in the original's order.
 */
#include <stdlib.h>
#include <string.h>

#include "game/planet.h"
#include "game/util.h"

/* floor(v / 2^n), the original's arithmetic shift */
static int asr(int v, int n)
{
    return v >= 0 ? v >> n : -(int)((~(unsigned)v) >> n) - 1;
}

/* The original indexes sectors as x * 32 + y without checks; the mountain
 * code runs y one past the edges into the neighbouring column. NULL where
 * the original would leave the array. */
static PlanetSector *sec_lin(int x, int y)
{
    int i = x * PLANET_H + y;
    return i >= 0 && i < PLANET_SECTORS ? &g_state.sectors[i / PLANET_H][i % PLANET_H] : NULL;
}

/* 0x49797d: ice in the north and south, wobbling rows. */
static void ice_caps(void)
{
    int8_t north = 0;
    uint8_t south = 31;
    for (int half = 0; half < 2; half++) {
        int x0 = half * 32 + 10;
        for (int x = x0; x < x0 + 12; x++) {
            north = (int8_t)(north + rand_range(-1, 1));
            if (north < 0)
                north = (int8_t)rand_range(0, 1);
            else if (north == 3)
                north = (int8_t)rand_range(1, 2);
            for (int y = 0; y <= north; y++) {
                g_state.sectors[x][y].terrain = PLANET_ARCTIC;
                g_state.sectors[x][y].height = 1;
            }
            south = (uint8_t)(south + rand_range(-1, 1));
            if ((int8_t)south < 32) {
                if ((int8_t)south < 29)
                    south = (uint8_t)rand_range(29, 30);
            } else {
                south = (uint8_t)rand_range(30, 31);
            }
            for (int y = south; y < 32; y++) {
                g_state.sectors[x][y].terrain = PLANET_ARCTIC;
                g_state.sectors[x][y].height = 1;
            }
        }
    }
}

/* 0x497fc3: up to `count` random land sectors inside bounds {x0, x1, y0,
 * y1}, at least `mindist` apart. Returns how many (at most (x1-x0)/mindist). */
static unsigned pick_points(const int *b, uint8_t mindist, int (*out)[2], uint8_t count)
{
    unsigned x0 = b[0] < 1 ? 0 : (unsigned)b[0], x1 = b[1] > 62 ? 63 : (unsigned)b[1];
    unsigned y0 = b[2] < 1 ? 0 : (unsigned)b[2], y1 = b[3] > 30 ? 31 : (unsigned)b[3];
    if (mindist < 2)
        mindist = 1;
    int n = (int)((x1 & 0xff) - (x0 & 0xff)) / (int)mindist;
    if (count <= n)
        n = count;
    n &= 0xff;
    for (int i = 0; i < n;) {
        uint8_t px, py;
        do {
            px = (uint8_t)rand_range((int)(x0 & 0xff), (int)(x1 & 0xff));
            py = (uint8_t)rand_range((int)(y0 & 0xff), (int)(y1 & 0xff));
        } while (g_state.sectors[px][py].terrain != PLANET_LAND); /* px < 64, py < 32 */
        out[i][0] = px;
        out[i][1] = py;
        bool ok = true;
        for (int j = 0; j < i; j++)
            if (planet_distance(out[j][0], out[j][1], out[i][0], out[i][1]) < mindist) {
                ok = false;
                break;
            }
        if (ok)
            i++;
    }
    return (unsigned)n;
}

/* 0x49818e: a mountain range running east from (x, y), sloping off north
 * and south. */
static void mountains(int x, int y)
{
    if (x < 0 || x >= 64 || y < 0 || y >= 32)
        return;
    int cx = x, cy = y;
    int len = rand_range(6, 12);
    if (len > 64 - x)
        len = 64 - x;
    PlanetSector *s;
    for (int i = 0; i < len && (s = sec_lin(cx, cy)) && s->terrain != PLANET_WATER; i++) {
        s->height = (uint8_t)rand_range(3, 4);
        int yy = cy - 1 < 1 ? 0 : cy - 1 > 30 ? 31 : cy - 1;
        int lo = cy - 3 < 1 ? 0 : cy - 3 > 30 ? 31 : cy - 3;
        while (lo <= yy && g_state.sectors[cx][yy].terrain == PLANET_LAND) {
            PlanetSector *below = sec_lin(cx, yy + 1);
            g_state.sectors[cx][yy].height = (uint8_t)((below ? below->height : 0) - 1);
            yy--;
        }
        yy = cy + 1 < 1 ? 0 : cy + 1 > 30 ? 31 : cy + 1;
        int hi = cy + 3 < 1 ? 0 : cy + 3 > 30 ? 31 : cy + 3;
        while (yy <= hi && g_state.sectors[cx][yy].terrain == PLANET_LAND) {
            PlanetSector *above = sec_lin(cx, yy - 1);
            g_state.sectors[cx][yy].height = (uint8_t)((above ? above->height : 0) - 1);
            yy++;
        }
        cx++;
        cy += rand_range(-1, 1);
        if (cy < 1)
            cy = 0;
        if (cy > 31)
            cy = 32; /* original: one past the edge */
    }
    if (cx > 62)
        cx = 63;
    for (int yy = cy - 1; yy <= cy + 1; yy++) {
        PlanetSector *a = sec_lin(cx, yy), *w = sec_lin(cx - 1, yy);
        if (a && w && a->terrain == PLANET_LAND && w->terrain == PLANET_LAND && w->height != 0)
            a->height = (uint8_t)(w->height - 1);
    }
    int xw = x - 1 < 1 ? 0 : x - 1;
    for (int yy = y - 1; yy <= y + 1; yy++) {
        PlanetSector *a = sec_lin(xw, yy), *e = sec_lin(xw + 1, yy);
        if (a && e && a->terrain == PLANET_LAND && e->terrain == PLANET_LAND && e->height != 0)
            a->height = (uint8_t)(e->height - 1);
    }
}

static void make_land(int x, int y)
{
    PlanetSector *s = &g_state.sectors[x][y];
    s->pop = 0;
    s->terrain = PLANET_LAND;
}

/* 0x497bbe: a continent growing east from (px, py), up to 25 columns,
 * widening first, then narrowing. */
static void continent(uint8_t px, uint8_t py)
{
    int w = px + 25 < 64 ? 25 : 64 - px;
    int y0 = rand_range(py, py - 3);
    int y1 = rand_range(py, py + 3);
    for (int y = y0; y <= y1; y++) {
        assert(y >= 0 && y < PLANET_H);
        make_land(px, y);
        g_state.sectors[px][y].height = 0;
    }
    int col = px + 1;
    for (;;) {
        int a, b;
        if (col - px < w / 3)
            a = 0x58, b = 0x5c;
        else if (w - w / 3 < col - px)
            a = 5, b = 0x5d;
        else
            a = 0x21, b = 0x42;
        int r1 = crt_rand();
        int r2 = crt_rand() % 100;
        if (r1 % 100 < a)
            y0--;
        else if (y0 < b) /* original: compares the row, not the roll */
            y0++;
        if (r2 < a)
            y1++;
        else if (r2 < b)
            y1--;
        if (y0 < 4)
            y0 = 3;
        if (y1 > 27)
            y1 = 28;
        if (col > 62)
            col = 63;
        if (y0 < py - 8)
            y0 += rand_range(0, 1);
        else if (py < y0)
            y0 = py;
        if (py + 8 < y1)
            y1 += rand_range(-1, 0);
        else if (y1 < py)
            y1 = py;
        for (int y = y0; y <= y1; y++)
            make_land(col, y);
        if (!(col + 1 < px + w) || y1 == y0)
            break;
        col++;
    }
    int bounds[4] = {px, col, py - 8 < 4 ? 3 : py - 8, py + 8 > 27 ? 28 : py + 8};
    int span = (col - px) / 2;
    if (span > 5)
        span = 6;
    int pts[2][2];
    unsigned n = pick_points(bounds, (uint8_t)span, pts, 2);
    for (unsigned i = 0; i < n; i++)
        mountains(pts[i][0], pts[i][1]);
}

/* A random value around 0 pulled towards it: (rand(lo, hi) >> 1) minus
 * min(rand(0, spread) >> 1, |that|) towards zero. */
static int pulled(int lo, int hi, int s_lo, int s_hi)
{
    int a = asr(rand_range(lo, hi), 1);
    int b = asr(rand_range(s_lo, s_hi), 1);
    if (abs(a) <= b)
        b = abs(a);
    int sg = a < 0 ? -1 : a != 0;
    return a - b * sg;
}

/* 0x4975c1: the first tribe's beliefs. */
static void seed_tribe(int x, int y)
{
    uint8_t *t = g_state.sectors[x][y].tenet;
    t[TENET_AAAA] = (uint8_t)(pulled(-20, 20, 0, 10) - 11);
    int a = rand_range(-60, 60), c = rand_range(-50, 50);
    t[TENET_ALF] = (uint8_t)(asr(a, 1) + asr(c, 1) + 0x7f);
    t[TENET_RALF] = (uint8_t)(0xff - t[TENET_ALF]);
    a = rand_range(-60, 60), c = rand_range(-50, 50);
    t[TENET_SUMA] = (uint8_t)(asr(a, 1) + asr(c, 1) + 0x7f);
    t[TENET_SUSA] = (uint8_t)(0xff - t[TENET_SUMA]);
    t[TENET_HOHO] = (uint8_t)(pulled(-120, 120, 20, 80) - 0x67);
    uint8_t rest = (uint8_t)(0xff - t[TENET_HOHO]);
    unsigned base = rest * 0x4bu / 100;
    int spread = rest - (int)base;
    t[TENET_HAHA] = (uint8_t)(base + (unsigned)pulled(-spread, spread, 0, spread * 2 / 6));
    rest = (uint8_t)(rest - t[TENET_HAHA]);
    base = rest * 2u / 3;
    spread = rest - (int)base;
    t[TENET_OCRA] = (uint8_t)(base + (unsigned)pulled(-spread, spread, 0, spread * 2 / 6));
    t[TENET_OPRA] = (uint8_t)(rest - t[TENET_OCRA]);
}

/* 0x49733f */
static void make_world(void)
{
    static const int k_all[4] = {0, 0x3f, 3, 0x1c};
    int pts[4][2];
    ice_caps();
    int n = 0;
    for (;;) {
        pts[n][0] = rand_range(0, 0x33);
        pts[n][1] = rand_range(6, 0x19);
        bool ok = true;
        for (int i = 0; i < n; i++)
            if (planet_distance(pts[i][0], pts[i][1], pts[n][0], pts[n][1]) < 10) {
                ok = false;
                break;
            }
        if (ok)
            n++;
        if (n > 3)
            break;
    }
    for (int i = 0; i < 4; i++)
        continent((uint8_t)pts[i][0], (uint8_t)pts[i][1]);
    for (;;) {
        pick_points(k_all, (uint8_t)(k_all[1] - k_all[0] - 1), pts, 1);
        int x = pts[0][0], y = pts[0][1];
        bool ok = true;
        for (int yy = y - 1 < 1 ? 0 : y - 1; ok && yy <= (y + 1 > 30 ? 31 : y + 1); yy++)
            for (int xx = x - 1 < 1 ? 0 : x - 1; xx <= (x + 1 > 62 ? 63 : x + 1); xx++)
                if (g_state.sectors[xx][yy].height != 0 || g_state.sectors[xx][yy].terrain != PLANET_LAND) {
                    ok = false;
                    break;
                }
        if (ok) {
            g_state.planet.pop_k = 5000;
            g_state.sectors[x][y].pop = 5000;
            seed_tribe(x, y);
            /* 0x4a5270(0, x, y): the Planetview selects it */
            g_planet.sel_x = x;
            g_planet.sel_y = y;
            return;
        }
    }
}

/* 0x496e71 */
void planet_reset_counters(void)
{
    PlanetState *ps = &g_state.planet;
    ps->tech_year = 0;
    ps->tech_best = 0;
    ps->coastal_pop = 0;
    ps->sector_row = 0;
    ps->tech_row = 0;
    memset(ps->sin_avg, 0, sizeof ps->sin_avg);
    memset(ps->sin_sum, 0, sizeof ps->sin_sum);
    ps->pop_k = 0;
}

/* 0x496ed1 */
void planet_generate(void)
{
    GameState *g = &g_state;
    PlanetState *ps = &g->planet;
    for (int x = 0; x < PLANET_W; x++)
        for (int y = 0; y < PLANET_H; y++)
            planet_clear_sector(x, y, true);
    g->embos = g->embos_next;
    g->planet_deaths_last = (uint32_t)g->embos_next.hi * 0xffffffu + g->embos_next.lo / 0xff;
    ps->n_a = ps->n_b = ps->n_c = 0;
    ps->disaster = 0xff;
    ps->disaster_year = 300;
    memset(ps->grid, 0, sizeof ps->grid);
    memset(ps->tenet_acc, 0, sizeof ps->tenet_acc);
    memset(ps->sin_acc, 0, sizeof ps->sin_acc);
    planet_reset_counters();
    make_world();
    planet_summary();
    if (g_planet_hooks.view_reset)
        g_planet_hooks.view_reset();
}
