/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The world core: game/world.h.
 */
#include "game/world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/newworld.h"
#include "game/planet.h"
#include "game/savegame.h"

GameState g_state;
World g_world;

void world_free(void) { savegame_free(&g_state); }

void world_reset(void)
{
    world_free();
    memset(&g_state, 0, sizeof g_state);
    memset(&g_world, 0, sizeof g_world);
    world_select_plane(GS_PLANE_HEAVEN);
    world_select_plane(GS_PLANE_HELL);
}

/* 0x42b102 map_view_select_plane (the simulation globals it sets) */
void world_select_plane(int plane)
{
    World *w = &g_world;
    w->select_count++;
    if (plane == GS_PLANE_HELL) {
        w->cells = g_state.plane[1].cells;
        w->objmap = g_state.plane[1].objmap;
        w->realm = &g_state.plane[1].stats;
        w->tile_info = w->tile_info_hell;
        w->water_entry_max = 3;
        w->objframe_base = 0x24;
        w->unk_65ed78 = 0;
        w->road_entry = 0;
        w->sim = g_state.plane[1].sim;
        w->realm_plane = plane;
    } else if (plane == GS_PLANE_MIDDLE) {
        /* the realm, sim cells and constants stay on the last realm */
        w->cells = g_state.plane[2].cells;
        w->objmap = g_state.plane[2].objmap;
    } else if (plane == GS_PLANE_HEAVEN) {
        w->cells = g_state.plane[3].cells;
        w->objmap = g_state.plane[3].objmap;
        w->realm = &g_state.plane[3].stats;
        w->tile_info = w->tile_info_heaven;
        w->water_entry_max = 0x15;
        w->objframe_base = 0x2c;
        w->unk_65ed78 = 3;
        w->road_entry = 0x21;
        w->sim = g_state.plane[3].sim;
        w->realm_plane = plane;
    }
    w->plane = plane;
}

void world_apply_loaded(GameState *gs)
{
    world_free();
    g_state = *gs;
    memset(gs, 0, sizeof *gs);
    /* save_load_plane (0x48a83a) clamps this byte while reading; savegame.c
     * keeps the file value so files round-trip. */
    for (int pl = GS_PLANE_HELL; pl <= GS_PLANE_HEAVEN; pl += 2)
        if (g_state.plane[pl].stats.unk_80ef > 100)
            g_state.plane[pl].stats.unk_80ef = 0;
    /* End of save_load_game 0x48f837: selects planes 2, 3, 1 and between
     * them runs 0x4506b6(0x19/0x18/0x17, 1) unless the view shows zone
     * detail, and 0x452267(1) after Heaven and Hell (game/newworld.c). */
    build_after_load();
    /* save_load_game ends with the planet summary (0x499a77, WP5) */
    planet_summary();
}

bool world_load(const char *path, char *err, size_t err_len)
{
    GameState *tmp = calloc(1, sizeof *tmp);
    if (!tmp) {
        if (err && err_len)
            snprintf(err, err_len, "out of memory");
        return false;
    }
    if (!savegame_load(tmp, path, err, err_len)) {
        free(tmp);
        return false;
    }
    world_apply_loaded(tmp);
    free(tmp);
    return true;
}

/* The shared origin lookup of 0x42b4f4 / 0x42b65f (byte arithmetic). */
void cell_origin(int x, int y, int *ox, int *oy)
{
    uint32_t c = cell_get(x, y);
    if (c >> 27 == 0) {
        x = (uint8_t)(x - (int)((c >> 16) & 7));
        y = (uint8_t)(y + (int)((c >> 20) & 7));
    }
    *ox = x;
    *oy = y;
}

/* 0x42b4f4 map_cell_origin_set */
unsigned cell_origin_tileset(int x, int y)
{
    int ox, oy;
    cell_origin(x, y, &ox, &oy);
    return cell_tileset(ox, oy);
}

/* 0x42b65f map_cell_origin_index */
unsigned cell_origin_index(int x, int y)
{
    int ox, oy;
    cell_origin(x, y, &ox, &oy);
    return cell_index(ox, oy);
}

/* 0x42b75f map_cell_object_slot */
unsigned cell_object_slot(int x, int y)
{
    if (!(cell_get(x, y) & 0x40u)) /* CELL_OBJECT */
        return 0;
    return g_world.objmap[y * GS_MAP_W + x] >> 2;
}

/* 0x42b7c5 tile_index_footprint */
int tile_index_footprint(unsigned index)
{
    if (index < 0x5b) {
        unsigned m = index % 13;
        return m < 5 ? 1 : m < 12 ? 2 : 3;
    }
    if (index < 0x60)
        return 1;
    if (index < 0x6d)
        return 2;
    if (index < 0x6f)
        return 3;
    if (index < 0x70)
        return 4;
    return 2;
}

/* 0x458490 soul_group_get */
unsigned soul_group_get(int x, int y)
{
    uint8_t bx = (uint8_t)x, by = (uint8_t)y; /* the original takes bytes */
    if (bx >= GS_MAP_W || by >= GS_MAP_H)
        return 0;
    unsigned g = sim_cell_u32(sim_cell(bx, by), 1) >> 16;
    /* An index past the table reads beyond soul_groups in the original;
     * treat it like a free group. */
    if (g >= GS_SOUL_GROUPS || g_state.soul_groups[g].count == 0) {
        soul_group_set(bx, by, 0);
        g = 0;
    }
    return g;
}

/* 0x45852f soul_group_set */
void soul_group_set(int x, int y, unsigned group)
{
    SimCell *c = sim_cell((uint8_t)x, (uint8_t)y);
    uint32_t v = sim_cell_u32(c, 1);
    sim_cell_set_u32(c, 1, (v & 0xffffu) | (uint32_t)group << 16);
}
