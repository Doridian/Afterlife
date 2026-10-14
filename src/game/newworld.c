/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * World setup: game/newworld.h.
 */
#include "game/newworld.h"

#include <string.h>

#include "game/build.h"
#include "game/mapobj.h"
#include "game/planet.h"
#include "game/realm.h"
#include "game/structure.h"
#include "game/util.h"
#include "game/world.h"
#include "ui/graphview.h"

/* ---- rivers and rocks ------------------------------------------------------------- */

static void water_cell(int x, int y)
{
    int wm = g_world.water_entry_max;
    map_stamp_building(x, y, 2, (unsigned)rand_range(wm - 3, wm), 1, true);
    sim_cell(x, y)->b[1] = 0;
}

/* 0x44a0d7: a river `width` wide entering at (x, y), flowing by (dx, dy)
 * (one of them 0) and meandering, with up to three branches. */
static void river(int x, int y, int width, int dx, int dy, int depth)
{
    int w = width;
    unsigned run = 1; /* local_8: tracked but unused */
    if (dy == 0) {
        int end = dx < 0 ? -1 : 100;
        int lo = y; /* local_2c: the upper bank */
        int c = y;
        do {
            for (; c < lo + w; c++)
                if ((uint8_t)c < 100 && (uint8_t)x < 100)
                    water_cell((uint8_t)x, (uint8_t)c);
            int pw = w;
            if (rand_range(0, 0x10) == 0 && depth < 3 && x > 0xf && x < 0x55) {
                depth++;
                if (rand_range(0, 1) == 0)
                    river(x, (uint8_t)c, w, 0, 1, depth);
                else
                    river(x, (uint8_t)(c - 5), w, 0, -1, depth);
            }
            int8_t r = (int8_t)rand_range(-w, 0xf - w);
            if (r < -5)
                w--, run = 1;
            else if (r < 6)
                run = run + 1 > 10 ? 10 : run + 1;
            else
                w++, run = 1;
            if (w > 10)
                w = 10;
            if (w < 5)
                w = 5;
            int d = (uint8_t)lo - (uint8_t)y;
            if (rand_range(0, (d < 0 ? -d : d) / 2) == 0) {
                if (rand_range(0, 1) == 0) {
                    if (pw <= w)
                        lo = (uint8_t)(lo - 1), run = 1;
                } else if (w <= pw) {
                    lo = (uint8_t)(lo + 1), run = 1;
                }
            }
            x = (uint8_t)(x + dx);
            c = lo;
        } while (x != (end & 0xff) && x < 100);
    } else {
        int end = dy < 0 ? -1 : 100;
        int lo = x;
        int c = x;
        do {
            for (; c < lo + w; c++)
                if ((uint8_t)c < 100 && (uint8_t)y < 100)
                    water_cell((uint8_t)c, (uint8_t)y);
            int pw = w;
            if (rand_range(0, 0x10) == 0 && depth < 3 && y > 0xf && y < 0x55) {
                depth++;
                if (rand_range(0, 1) == 0)
                    river((uint8_t)c, y, w, 1, 0, depth);
                else
                    river((uint8_t)(c + 5), y, w, -1, 0, depth);
            }
            int8_t r = (int8_t)rand_range(-w, 0xf - w);
            if (r < -5)
                w--, run = 1;
            else if (r < 6)
                run = run + 1 > 10 ? 10 : run + 1;
            else
                w++, run = 1;
            if (w > 10)
                w = 10;
            if (w < 5)
                w = 5;
            int d = (uint8_t)lo - (uint8_t)x;
            if (rand_range(0, (d < 0 ? -d : d) / 2) == 0) {
                if (rand_range(0, 1) == 0) {
                    if (pw <= w)
                        lo = (uint8_t)(lo - 1), run = 1;
                } else if (w <= pw) {
                    lo = (uint8_t)(lo + 1), run = 1;
                }
            }
            y = (uint8_t)(y + dy);
            c = lo;
        } while (y != (end & 0xff) && y < 100);
    }
    (void)run;
}

/* 0x452da1: a trail of rocks (set 10, index base+1..base+4) from (x, y)
 * in direction (dx, dy) (random if both 0), branching */
static void rock_trail(int x, int y, int dx, int dy, unsigned base, int len)
{
    bool stepped = false;
    if (dx == 0 && dy == 0) {
        int r;
        do
            r = rand_range(0, 8);
        while (r == 4);
        dx = r / 3 - 1;
        dy = r % 3 - 1;
        len = rand_range(0x14, 0x5a);
    }
    for (int i = 0; i < len && x >= 0 && y >= 0 && x < 100 && y < 100; i++) {
        if (cell_tileset(x, y) == 1 && rand_range(0, g_state.hdr.difficulty * 7) == 0)
            map_stamp_building(x, y, 10, (unsigned)rand_range((int)(base & 0xff) + 1, (int)(base & 0xff) + 4), 1,
                               false);
        if (rand_range(0, 0xc) == 0) {
            if (rand_range(0, 1) == 0)
                rock_trail(x, y, -dy, -dx, base, rand_range(1, len >> 1));
            else
                rock_trail(x, y, dy, dx, base, rand_range(1, len >> 1));
        }
        if (dx != 0 && dy != 0 && !stepped && cell_get_flat(x, (uint8_t)(y - dy)) >> 27 == 1 &&
            rand_range(0, g_state.hdr.difficulty * 7) == 0)
            map_stamp_building(x, y - dy, 10,
                               (unsigned)rand_range((int)(base & 0xff) + 1, (int)(base & 0xff) + 4), 1, false);
        if (rand_range(0, 3) == 0) {
            stepped = true;
            if (dy == 0)
                y = (int8_t)(rand_range(0, 1) == 0 ? y - 1 : y + 1);
            else if (dx == 0)
                x = (int8_t)(rand_range(0, 1) == 0 ? x - 1 : x + 1);
            else if (rand_range(0, 1) == 0)
                y = (int8_t)(y - dy);
            else
                x = (int8_t)(x - dx);
        } else {
            stepped = false;
            x = (int8_t)(x + dx);
            y = (int8_t)(y + dy);
        }
    }
}

/* 0x45311c: the rock value of the realm: for every rock, rock_value of
 * the 2x2 cells from it (the original tests the rock cell itself again
 * inside the loop) */
static uint32_t rock_total(void)
{
    uint32_t sum = 0;
    for (int x = 1; x < 99; x++)
        for (int y = 1; y < 99; y++)
            if (cell_tileset(x, y) == 10)
                for (int cx = x; cx <= x + 1; cx++)
                    for (int cy = y; cy <= y + 1; cy++)
                        sum += (uint32_t)rock_value(cx, cy);
    return sum;
}

/* ---- realm tile type tables ------------------------------------------------------ */

static void tile_info_fill(RealmTileInfo *t, bool hell)
{
    int mult = 1;
    for (unsigned i = 0; i < REALM_TILE_TYPES; i++) {
        RealmTileInfo *e = &t[i];
        if (i < 0x5b) {
            unsigned m = i % 13;
            int w = 0;
            if (m == 0)
                mult = 1;
            e->zone_kind = (uint8_t)(i / 13);
            e->unk_08 = (m * 5 + 5) * 0x19;
            switch (i / 13) {
            case 0:
                w = (int)m - 6;
                break;
            case 1:
            case 4:
                if (m < 4 || m > 8)
                    w = ((int)(m % 9) + (int)(m / 9) * -6) * 2;
                else
                    w = 6 - ((int)m * 2 - 6);
                if (i / 13 == 4)
                    w = -w;
                break;
            case 2:
            case 5:
                if (m < 6)
                    w = (int)m * 2 - 6;
                else
                    w = 6 - ((int)m * 2 - 0xc);
                if (i / 13 == 5)
                    w = -w;
                break;
            case 3:
                w = 6 - (int)m;
                break;
            case 6:
                if (m < 7)
                    w = (m & 1) == 0 ? -(int)m : (int)((m + 1) / 2) * 2;
                else
                    w = (m & 1) == 0 ? (int)m - 0xc : ((0xd - (int)m) / 2) * 2;
                break;
            }
            if (hell)
                w = -w;
            e->unk_13 = (int8_t)w;
            if (m == 0) {
                e->base_cost = 500;
            } else if (m < 5) {
                e->base_cost = (uint32_t)(mult * 500);
                mult *= 3;
            } else if (m < 0xc) {
                int c = ((int)m - 3) * ((int)m - 4) * ((int)m - 4) * 2000;
                e->base_cost = (uint32_t)(c < 0x1771 ? 6000 : c);
            } else {
                e->base_cost = 1000000;
            }
        } else {
            int k = (int)i - 0x5b;
            e->zone_kind = 7;
            if (k == 0)
                mult = 1;
            e->unk_08 = (uint32_t)((k * 5 + 5) * 0x32);
            static const int8_t weight[21] = {0,  -2, 0, 1,  2, 1,  0, -2, -3, -4, -3,
                                              -2, -1, 0, 1,  2, 3,  4, -6, 0,  6};
            e->unk_13 = weight[k < 21 ? k : 13];
            if (k < 1) {
                e->base_cost = 0xfa;
            } else if (k < 5) {
                e->base_cost = (uint32_t)(mult * 0xfa);
                mult *= 5;
            } else if (k < 0x12) {
                int c = ((int)i - 0x5e) * ((int)i - 0x5f) * 800;
                e->base_cost = (uint32_t)(c < 0x5dd ? 0x5dc : c);
            } else if (k < 0x14) {
                e->base_cost = (i - 0x6c) * 0x1e848;
            } else {
                e->base_cost = 2000000;
            }
        }
        e->cost = e->base_cost;
    }
}

void realm_tile_info_init(void)
{
    tile_info_fill(g_world.tile_info_hell, true);
    tile_info_fill(g_world.tile_info_heaven, false);
}

/* 0x452581 */
void realm_tile_info_fill(void)
{
    realm_cur()->unk_8046 = 1;
    tile_info_fill(g_world.tile_info, g_world.plane == GS_PLANE_HELL);
    realm_tile_costs_update(true);
}

/* 0x452267 */
void realm_tile_costs_update(bool force)
{
    PlaneStats *r = realm_cur();
    if (force) {
        for (unsigned k = 0; k < 10; k++)
            if (!(g_state.unk_638668 & 1u << k))
                g_build.special_pos[k][0] = g_build.special_pos[k][1] = 0xa6;
        for (int x = 0; x < 100; x++)
            for (int y = 0; y < 100; y++)
                if (cell_tileset(x, y) == 4) {
                    unsigned i = cell_index(x, y);
                    if (i < 10) /* the original writes past the table for larger indexes */
                        g_build.special_pos[i][0] = (uint8_t)x, g_build.special_pos[i][1] = (uint8_t)y;
                }
    }
    if (r->unk_8046 == 100 && !force)
        return;
    uint32_t v = g_state.embos_next.hi < 1 ? g_state.embos_next.lo : 1000000001u;
    uint32_t level = 0, lo = 0, top = 0, a = 0, b = 0;
    if (v >= 1000000000u)
        level = 100;
    else if (v >= 100000000u)
        lo = 100000000u, top = 999999999u, a = 0x32, b = 100;
    else if (v >= 10000000u)
        lo = 10000000u, top = 99999999u, a = 0x14, b = 0x32;
    else if (v >= 1000000u)
        lo = 1000000u, top = 9999999u, a = 10, b = 0x14;
    else if (v >= 10000u)
        lo = 10000u, top = 999999u, a = 1, b = 10;
    else
        level = 1;
    if (level == 0)
        level = a + (v - lo) / ((top - lo) / (b - a));
    if (!force) {
        if (level <= r->unk_8046)
            return;
    } else if (level < r->unk_8046) {
        level = r->unk_8046;
    }
    for (unsigned i = 0; i < REALM_TILE_TYPES; i++)
        g_world.tile_info[i].cost = g_world.tile_info[i].base_cost * level / 20;
    r->unk_8046 = (uint8_t)level;
}

/* ---- after loading ------------------------------------------------------------------ */

void build_after_load(void)
{
    /* the original's tables are whatever the last new world built; their
     * base costs only depend on the realm */
    if (g_world.tile_info_hell[0].base_cost == 0 || g_world.tile_info_heaven[0].base_cost == 0)
        realm_tile_info_init();
    world_select_plane(GS_PLANE_MIDDLE);
    if (!(g_build.view_flags[GS_PLANE_MIDDLE] & 0x40))
        build_zone_blocks(0x19, true);
    world_select_plane(GS_PLANE_HEAVEN);
    if (!(g_build.view_flags[GS_PLANE_HEAVEN] & 0x40))
        build_zone_blocks(0x18, true);
    realm_tile_costs_update(true);
    world_select_plane(GS_PLANE_HELL);
    if (!(g_build.view_flags[GS_PLANE_HELL] & 0x40))
        build_zone_blocks(0x17, true);
    realm_tile_costs_update(true);
}

/* ---- new world ----------------------------------------------------------------------- */

/* 0x45546c */
void game_init_defaults(void)
{
    build_init_defaults();
    /* 0x5fa620..0x5fa649: tile chooser layout (UI) */
    game_reset_world();
}

static void realm_defaults(PlaneStats *r)
{
    r->unk_7ff0[17] = 1;  /* +0x8034 */
    r->unk_7ff0[18] = 4;  /* +0x8038 */
    r->unk_7ff0[19] = 2;  /* +0x803c */
    r->unk_80f8 = 100;
    /* +0x812f = 1 is past the saved record (runtime, not kept) */
}

static void realm_plane_init(int plane)
{
    world_select_plane(plane);
    g_build.view_flags[plane] = 0xc0;
    realm_cur()->unk_78c4[0][0] = 0;
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++) {
            *cell_ptr(x, y) = 0;
            g_world.objmap[y * GS_MAP_W + x] = 0;
            cell_set_strip(x, y, 1);
            cell_set_tileset(x, y, 1);
            rand_range(0, 2); /* both outcomes index 0 */
            cell_set_index(x, y, 0);
            memset(sim_cell(x, y), 0, sizeof(SimCell));
        }
}

static void realm_water(void)
{
    realm_cur()->n_ports = 0;
    for (int i = 0; i < 100; i++) {
        water_cell(i, 0);
        water_cell(i, 99);
        water_cell(0, i);
        water_cell(99, i);
    }
    if (rand_range(0, 1) != 0) {
        if (rand_range(0, 1) != 0)
            river(0, rand_range(0x14, 0x50), 5, 1, 0, 1);
        else
            river(99, rand_range(0x14, 0x50), 5, -1, 0, 1);
    } else if (rand_range(0, 1) != 0) {
        river(rand_range(0x14, 0x50), 0, 5, 0, 1, 1);
    } else {
        river(rand_range(0x14, 0x50), 99, 5, 0, -1, 1);
    }
    water_autotile_all();
}

static void realm_rocks(int plane)
{
    world_select_plane(plane);
    rand_range(2, 8); /* a count the original decrements but never reads */
    do {
        int x = (uint8_t)rand_range(10, 0x5a);
        int y = (uint8_t)rand_range(10, 0x5a);
        rock_trail(x, y, 0, 0, 0x10, 0);
    } while (rock_total() < 48000);
}

/* 0x4535db */
void game_reset_world(void)
{
    GameState *g = &g_state;
    int diff = g->hdr.difficulty;
    g_build.map_dirty = 1;
    g->planet_aaaa = 0xfa;
    int c = 0;
    for (int i = 0; i < 7; i++) {
        g->unk_593260[i][0] = (uint8_t)i;
        if (diff == 1) {
            int a = rand_range(0x10, 0x70), b = rand_range(0x10, 0x70), d = rand_range(-32, 0x1f);
            c = a + b + d;
        } else if (diff == 2) {
            int a = rand_range(0x10, 0x60), b = rand_range(0x10, 0x60), d = rand_range(-32, 0x3f);
            c = a + b + d;
        } else if (diff == 3) {
            int a = rand_range(-32, 0x5f), b = rand_range(0x10, 0x50), d = rand_range(0x10, 0x50);
            c = a + b + d;
        }
        if (c < 0x10)
            c = 0xf;
        if (c > 0xfe)
            c = 0xff;
        g->unk_593260[i][1] = (uint8_t)c;
        g->unk_57e060[i] = (uint8_t)i;
        g->unk_593240[i] = (uint8_t)i;
    }
    memset(&g->plane[1].stats, 0, sizeof g->plane[1].stats);
    memset(&g->plane[3].stats, 0, sizeof g->plane[3].stats);
    for (int pl = 1; pl <= 3; pl++) {
        memset(g->plane[pl].sim, 0, sizeof g->plane[pl].sim);
        memset(g->plane[pl].cells, 0, sizeof g->plane[pl].cells);
        memset(g->plane[pl].objmap, 0, sizeof g->plane[pl].objmap);
    }
    memset(g->sectors, 0, sizeof g->sectors);
    memset(g->unk_64e500, 0, sizeof g->unk_64e500);
    memset(g->objects, 0, sizeof g->objects);
    memset(g->soul_groups, 0, sizeof g->soul_groups);
    memset(g->rec_5c4eb0, 0, sizeof g->rec_5c4eb0);
    memset(g->unk_5f4cd0, 0, sizeof g->unk_5f4cd0);
    strcpy(g->scenario_title, "x");
    strcpy(g->scenario_description, "x");
    strcpy(g->scenario_button, "x");
    strcpy(g->scenario_commands, "x");
    g->unk_63048c = 0xffffffffu;
    g->unk_63050d = 0;
    g->unk_630425 = 0;
    memset(g->unk_5bb1e0, 0, sizeof g->unk_5bb1e0);
    memset(g->unk_5931d0, 0, sizeof g->unk_5931d0);
    g->unk_5c6678 = 100;
    g->unk_593247 = 0;
    g->bad_things = 1;
    g->unk_5b2f61 = 0;
    g->unk_57e058 = 0;
    g->unk_630420 = 0;
    g->unk_5bb108 = 0;
    realm_defaults(&g->plane[1].stats);
    realm_defaults(&g->plane[3].stats);
    g->unk_5b2f5c = 0;
    g->unk_5fa5f4 = 0;
    g->unk_5f7ed4 = 0;
    g->unk_6301f2 = 0;
    g_build.karma_dirty = 0;
    g->unk_6301f4 = 0;
    g->unk_59ceb0 = 0;
    g->unk_630508 = 0;
    g->unk_630484 = 0;
    g->view_dir_x = g->view_dir_y = 1;
    g->unk_63042c = 0;
    g->unk_638668 = 0;
    g->unk_630430 = 0x1a;
    g->unk_5b2f5a = 0;
    g->unk_626598 = 0;
    g->lost_souls = 0;
    g->pennies.hi = 0;
    if (diff == 1)
        g->pennies.lo = 9999999, g->unk_5f746c = 0;
    else if (diff == 2)
        g->pennies.lo = 6666666, g->unk_5f746c = 3;
    else if (diff == 3)
        g->pennies.lo = 3333333, g->unk_5f746c = 0xf;
    g->year = 0;
    g->sim_phase = 0x1e;
    memset(g_build.special_pos, 0xa6, sizeof g_build.special_pos);

    /* parked map objects (plane 99) */
    g_build.obj_57e018 = (uint8_t)mapobj_spawn(-1, -1, 10, 0x23, 0, 0, 99, 1, 99, 0, 0xffffffffu, 2, 1);
    if (g_build_hooks.soulview_reset)
        g_build_hooks.soulview_reset();
    for (int i = 0; i < 8; i++) {
        g->unk_5f7400[i] = (uint8_t)mapobj_spawn(-1, -1, 10, i + 8, 0, 0, 99, 1, 99, 0, 0xffffffffu, 1, 1);
        g->unk_6301f2++;
    }
    g_build.obj_5f7408 = (uint8_t)mapobj_spawn(-1, -1, 10, 0x25, 0, 0, 99, 1, 99, 0, 0xffffffffu, 1, 1);

    /* the Karma plane: empty land */
    world_select_plane(GS_PLANE_MIDDLE);
    g_build.view_flags[GS_PLANE_MIDDLE] = 0xc0;
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++) {
            *cell_ptr(x, y) = 0;
            g_world.objmap[y * GS_MAP_W + x] = 0;
            cell_set_strip(x, y, 1);
            cell_set_tileset(x, y, 1);
            cell_set_index(x, y, 1);
        }

    /* Hell */
    realm_plane_init(GS_PLANE_HELL);
    static const uint8_t dirs[4][3] = {{0xff, 0, 0}, {1, 0, 1}, {0, 0xff, 2}, {0, 1, 3}};
    memcpy(g->unk_5f25b0, dirs, sizeof dirs);
    memcpy(g->unk_630410, dirs, sizeof dirs);
    g->unk_5b2f58 = 0;
    realm_water();

    /* Heaven */
    realm_plane_init(GS_PLANE_HEAVEN);
    g->unk_630426 = 0;
    realm_water();

    /* (the original clears the realms' tables again here; they are zero) */

    /* 3-6 Karma stations away from the centre */
    int n = rand_range(3, 6);
    for (int k = 0; k < n; k++) {
        world_select_plane(GS_PLANE_MIDDLE);
        int8_t cx, cy;
        bool ok = false;
        do {
            cx = (int8_t)rand_range(1, 0x60);
            cy = (int8_t)rand_range(4, 0x62);
            if (cx < 45 || cx > 55 || cy < 45 || cy > 55) {
                ok = true;
                for (int x = cx; x < cx + 3; x++)
                    for (int y = cy; y > cy - 3; y--)
                        if (cell_tileset(x, y) != 1)
                            ok = false;
            }
        } while (!ok);
        map_stamp_building(cx, cy, 18, 0, 3, false);
        world_select_plane(GS_PLANE_HEAVEN);
        map_stamp_building(cx, cy, 18, 3, 3, true);
        world_select_plane(GS_PLANE_HELL);
        map_stamp_building(cx, cy, 18, 4, 3, true);
    }

    realm_rocks(GS_PLANE_HELL);
    realm_rocks(GS_PLANE_HEAVEN);

    world_select_plane(GS_PLANE_MIDDLE);
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++)
            cell_clear_flags(x, y, 0x8000);

    g->embos_next.hi = 0, g->embos_next.lo = 0;
    planet_generate();
    g->embos.hi = 0, g->embos.lo = 5000;
    for (int i = 0; i < GS_SOUL_GROUPS; i++)
        g->soul_groups[i].count = 0, g->soul_groups[i].next = 0;

    /* the Karma Station in the middle of the Karma plane */
    world_select_plane(GS_PLANE_MIDDLE);
    map_stamp_building(0x31, 0x32, 10, 0, 2, false);
    mapobj_place(0x31, 0x32, 0, 8, 6, 2, 2, 0);

    for (int pl = GS_PLANE_HELL; pl <= GS_PLANE_HEAVEN; pl += 2) {
        world_select_plane(pl);
        realm_tile_info_fill();
        for (int x = 0x31; x <= 0x32; x++)
            for (int y = 0x31; y <= 0x32; y++)
                cell_set_flags(x, y, 8);
    }
    /* (control panel buttons: UI) */
    planet_reset_counters();
    world_select_plane(GS_PLANE_HEAVEN);
    realm_vibes_update();
    world_select_plane(GS_PLANE_HELL);
    realm_vibes_update();
    g_build.status_dirty = 1;
    graph_history_record();
    if (g_build_hooks.views_refresh)
        g_build_hooks.views_refresh();
}
