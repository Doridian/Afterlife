/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * tools/emu wrappers for the SOUL flow (WP1b, src/game/souls.c, zones.c),
 * used by tools/emu/test_souls.py: the derived tables a loaded game has in
 * the original's process, in the original's memory layout, and the
 * module's runtime counters.
 */
#include <stdint.h>
#include <string.h>

#include "game/build.h"
#include "game/newworld.h"
#include "game/souls.h"
#include "game/souls_int.h"
#include "game/tables.h"
#include "game/world.h"

#if defined(_WIN32)
#define SHIM_API __declspec(dllexport)
#else
#define SHIM_API __attribute__((visibility("default")))
#endif

/* After world_load: what the original's process has besides the save
 * (game_init_defaults' tables, the tile type tables of game_reset_world,
 * save_load_game's 0x452267 for Heaven then Hell), the SOUL flow reset to
 * process start. Leaves Hell selected. */
SHIM_API void shim_souls_setup(void)
{
    tables_init_defaults();
    realm_tile_info_init();
    world_select_plane(GS_PLANE_HEAVEN);
    realm_tile_costs_update(true);
    world_select_plane(GS_PLANE_HELL);
    realm_tile_costs_update(true);
    souls_reset();
}

static void put32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v, b[1] = (uint8_t)(v >> 8), b[2] = (uint8_t)(v >> 16), b[3] = (uint8_t)(v >> 24);
}

static size_t specs(uint8_t *out, const BuildingSpec *s, int n)
{
    for (int i = 0; i < n; i++) {
        put32(out + i * 13, s[i].jobs);
        put32(out + i * 13 + 4, s[i].capacity);
        put32(out + i * 13 + 8, s[i].cost);
        out[i * 13 + 12] = s[i].size;
    }
    return (size_t)n * 13;
}

/* The building tables in the original's layout (13-byte records), one
 * after the other in the order test_souls.py writes them: set 7 (8), 8
 * (6), 9 (6), 13 (8), 17 (6), 5 (6), 19 (4), 30 (3); then
 * g_build.special_pos (20 bytes, 0x5bb0f0) and g_build.obj_5f7408. Returns
 * the length. */
SHIM_API size_t shim_souls_tables(uint8_t *out)
{
    size_t n = 0;
    n += specs(out + n, g_tables.set7, 8);
    n += specs(out + n, g_tables.set8, 6);
    n += specs(out + n, g_tables.set9, 6);
    n += specs(out + n, g_tables.set13, 8);
    n += specs(out + n, g_tables.set17, 6);
    n += specs(out + n, g_tables.set5, 6);
    n += specs(out + n, g_tables.set19, 4);
    n += specs(out + n, g_tables.set30, 3);
    memcpy(out + n, g_build.special_pos, 20);
    n += 20;
    out[n++] = g_build.obj_5f7408;
    return n;
}

/* Runtime counters and cursors, in the order of test_souls.py's RUNTIME
 * list. */
SHIM_API void shim_souls_runtime(uint32_t out[9])
{
    out[0] = souls_to_heaven;      /* 0x65fe54 */
    out[1] = souls_to_hell;        /* 0x65fe58 */
    out[2] = souls_groups_created; /* 0x630510 */
    out[3] = souls_gate_arrived;   /* 0x57e01c */
    souls_cursors(out + 4);        /* 0x65fe50, 0x65fe48, 0x65fe4c, 0x65fe40, 0x65fe44 */
}

SHIM_API int shim_souls_zone_scan(void) { return souls_zone_scan() ? 1 : 0; }
SHIM_API int shim_souls_flow(unsigned max_groups) { return souls_flow(max_groups) ? 1 : 0; }
SHIM_API int shim_souls_area_search(int x, int y, int size, int radius)
{
    int ox, oy;
    unsigned r = area_search(x, y, (unsigned)size, (unsigned)radius, &ox, &oy);
    return (int)(r | (unsigned)oy << 8);
}

/* ---- synthetic additions (test_souls.py --synthetic) ------------------------
 * The shipped scenarios have no Omnibolges / Love Domes (set 5) or siphons
 * (set 30) and no tracked SOULs. This adds some to the loaded state (both
 * sides then start from it): per realm, `n5` set-5 and `n30` set-30
 * buildings on plain land next to roads, a siphon pool, and a Soulview
 * tracker on the first groups found. Returns the number of buildings. */

#include "game/structure.h"

static int area_plain(int x, int y, int n)
{
    if (x < 0 || y - n + 1 < 0 || x + n > 100 || y > 99)
        return 0;
    for (int cx = x; cx < x + n; cx++)
        for (int cy = y; cy > y - n; cy--)
            if (cell_tileset(cx, cy) != 1)
                return 0;
    return 1;
}

static int near_road(int x, int y, int n)
{
    for (int cx = x - 2; cx < x + n + 2; cx++)
        for (int cy = y + 2; cy > y - n - 2; cy--)
            if (cx >= 0 && cx < 100 && cy >= 0 && cy < 100 && cell_tileset(cx, cy) == 12)
                return 1;
    return 0;
}

static int place(unsigned set, unsigned entry, int n, int skip)
{
    for (int y = 99; y >= 0; y--)
        for (int x = 0; x < 100; x++)
            if (area_plain(x, y, n) && near_road(x, y, n) && skip-- <= 0) {
                map_stamp_building(x, y, set, entry, n, true);
                for (int cx = x; cx < x + n; cx++)
                    for (int cy = y; cy > y - n; cy--) {
                        SimCell *c = sim_cell(cx, cy);
                        memset(c->b, 0, 10);
                        c->b[12] = c->b[13] = c->b[14] = c->b[15] = 0;
                    }
                return x | y << 8;
            }
    return -1;
}

SHIM_API int shim_souls_synthetic(int n5, int n30, int trackers)
{
    int made = 0;
    for (int pl = GS_PLANE_HEAVEN; pl >= GS_PLANE_HELL; pl -= 2) {
        world_select_plane(pl);
        PlaneStats *r = realm_cur();
        for (int i = 0; i < n5; i++) {
            int p = place(5, (unsigned)(i & 1), g_tables.set5[i & 1].size, i * 7);
            if (p < 0)
                break;
            SimCell *c = sim_cell(p & 0xff, p >> 8);
            c->b[0xd] = (uint8_t)(i & 1 ? 100 : 40); /* percent taken */
            sim_cell_set_u32(c, 5, 30000u * (uint32_t)(i + 1));
            sim_cell_set_u32(c, 9, 20000u);
            made++;
        }
        uint32_t cap = 0;
        int sip = 0;
        for (int i = 0; i < n30; i++) {
            int p = place(30, (unsigned)(i % 3), g_tables.set30[i % 3].size, i * 5);
            if (p < 0)
                break;
            SimCell *c = sim_cell(p & 0xff, p >> 8);
            sim_cell_set_u32(c, 1, 700u + 300u * (uint32_t)i); /* held this year */
            sim_cell_set_u32(c, 9, 0);                         /* far from bursting (0x42c09b is WP2's) */
            c->b[0xd] = c->b[0xe] = 0xff;                      /* no road remembered */
            cap += g_tables.set30[i % 3].capacity;
            sip++;
            made++;
        }
        r->unk_7f30 = (uint32_t)sip;
        r->unk_7f34 = cap ? cap : 50000;
        r->unk_7f38 = 0;
        for (int c = 0; c < 16; c++) {
            r->unk_7ef0[c] = 100u * (uint32_t)(c + 1);
            r->unk_7f38 += r->unk_7ef0[c];
        }
    }
    /* trackers on the first groups found (Heaven, then Hell) */
    Block26 *b = &g_state.block_4f33f8;
    int t = 0;
    for (int pl = GS_PLANE_HEAVEN; pl >= GS_PLANE_HELL && t < trackers; pl -= 2) {
        world_select_plane(pl);
        for (int i = 0; i < GS_MAP_CELLS && t < trackers && t < 10; i++) {
            if (g_world.cells[i] >> 27 != 12)
                continue;
            unsigned g = sim_cell_u32(&g_world.sim[i], 1) >> 16;
            if (g == 0 || g >= GS_SOUL_GROUPS || g_state.soul_groups[g].count == 0)
                continue;
            uint16_t m = g_state.soul_groups[g].mask;
            int cat = 0;
            while (cat < 16 && !(m & 1u << cat))
                cat++;
            if (cat == 16)
                continue;
            Rec26 *r = &b->rec[t++];
            memset(r, 0, sizeof *r);
            r->unk_00[0] = 2;
            r->unk_00[1] = (uint8_t)(t & 1);
            r->unk_10 = (uint8_t)pl;
            r->unk_17 = g;
            r->unk_1b = (uint8_t)cat;
            r->unk_11 = (uint8_t)(cat % 7);
            r->unk_12 = (uint8_t)(t % 16);
        }
    }
    b->unk_66480c = (uint8_t)t;
    world_select_plane(GS_PLANE_HELL);
    return made;
}
