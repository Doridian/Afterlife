/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The world core: the live game state, the plane switch and the map / sim
 * cell accessors every simulation module builds on.
 *
 * State model
 * -----------
 * ALIFE.EXE keeps its game state in a few hundred fixed globals plus two
 * per-realm records. The port keeps all of the persistent part in ONE
 * global, `GameState g_state` (game/state.h, laid out by save block, with
 * each field's original address in a comment). The ALSV loader fills it
 * (world_load -> savegame_load), the simulation reads and writes it in
 * place, and the saver writes it out. There are no shadow copies: code that
 * the original wrote as `pennies_hi` is `g_state.pennies.hi` here, a realm
 * record is `g_state.plane[1].stats` (Hell) / `[3]` (Heaven), and so on.
 *
 * The "current plane" globals that map_view_select_plane (0x42b102) points
 * at the selected plane (map_cells_current, sim_cells_current,
 * plane_stats_current, realm_tile_info, a few per-realm constants) live in
 * `World g_world` below and point into g_state. They are runtime-only
 * (not saved). Exactly like the original, selecting plane 2 (the Karma
 * plane, which has no sim cells or statistics) switches the cell grid but
 * leaves the realm, sim-cell and tile-info pointers on the last realm.
 *
 * Other non-persistent runtime state (windows, timers, scan cursors such as
 * sim_zone_scan's 0x65fe48) stays in the module that owns it; new
 * persistent fields go into state.h (and savegame.c).
 *
 * The accessors take map coordinates 0..99 (the original passes bytes and
 * doesn't check; here out-of-range coordinates trip an assert). The grids
 * are row-major, [y * 100 + x].
 */
#ifndef GAME_WORLD_H
#define GAME_WORLD_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "game/state.h"

/* ---- The live state ------------------------------------------------------- */

extern GameState g_state;

/* Per-realm table of tile types (0x5bb0a4 realm_tile_info -> 0x5f7480
 * Hell / 0x5c5c20 Heaven, 0x84 records of 0x14 bytes). Not saved: built by
 * 0x452581 (WP2, realm table reset). Index: zone tile index 0..0x5a (7 zone
 * kinds x 13: empty zone, then buildings), 0x5b.. special structures. */
#define REALM_TILE_TYPES 0x84
typedef struct RealmTileInfo {
    uint32_t cost;      /* +0x00: reset to base_cost */
    uint32_t base_cost; /* +0x04 */
    uint32_t unk_08;    /* +0x08: zones (i % 13 * 5 + 5) * 25, specials (i - 0x5b) * 250 + 250 */
    uint8_t unk_0c[6];  /* +0x0c */
    uint8_t zone_kind;  /* +0x12: i / 13 for zones (sin/virtue 0..6), 7 for specials */
    int8_t unk_13;      /* +0x13: per-type signed weight (negated in Hell for zones) */
} RealmTileInfo;

typedef struct World {
    int plane;                /* 0x5bb167 (map_view +0x47): 1 Hell, 2 middle, 3 Heaven */
    int realm_plane;          /* 1 or 3: the plane realm/sim/tile_info belong to (0 = none yet) */
    uint32_t *cells;          /* 0x630518 map_cells_current */
    uint8_t *objmap;          /* map_view +0x04 (0x5bb124): the selected plane's object map */
    SimCell *sim;             /* 0x57e014 sim_cells_current */
    PlaneStats *realm;        /* 0x630428 plane_stats_current */
    RealmTileInfo *tile_info; /* 0x5bb0a4 realm_tile_info */
    /* Per-realm constants (Hell / Heaven), set by the plane switch. */
    uint8_t water_entry_max; /* 0x65ed74 realm_water_entry_max: 3 / 0x15; set 2 entries max-3..max */
    uint8_t unk_65ed78;      /* 0x65ed78: 0 / 3, never read */
    uint8_t road_entry;      /* 0x65ed7c realm_road_entry: 0 / 0x21, base entry of set 12 roads */
    uint8_t objframe_base;   /* 0x65ed80 realm_objframe_base: 0x24 / 0x2c, map object frame base */
    uint32_t select_count;   /* 0x63921c plane_select_count (sim_update resets it) */
    RealmTileInfo tile_info_hell[REALM_TILE_TYPES];   /* 0x5f7480 tile_info_hell */
    RealmTileInfo tile_info_heaven[REALM_TILE_TYPES]; /* 0x5c5c20 tile_info_heaven */
} World;

extern World g_world;

/* Frees g_state's heap parts, zeroes g_state and g_world (tile tables too)
 * and selects Heaven, then Hell, so every pointer is valid. Does not touch
 * the RNG. */
void world_reset(void);

/* Loads an ALSV file into g_state (via a temporary, so g_state is untouched
 * on failure) and applies it like the end of save_load_game (0x48f837):
 * realm +0x80ef is clamped (> 100 -> 0), the planes are reselected in the
 * order 2, 3, 1 (Hell stays selected). The derived-table rebuilds the
 * original runs between those selects (0x4506b6 recompute and 0x452267,
 * WP2) are not ported yet. */
bool world_load(const char *path, char *err, size_t err_len);
/* The same for a state that was loaded elsewhere: takes ownership of *gs
 * (its heap parts move into g_state; *gs is zeroed). */
void world_apply_loaded(GameState *gs);
/* Frees g_state's heap parts (thumbnail, planet map). */
void world_free(void);

/* 0x42b102 map_view_select_plane, simulation half: points g_world at
 * plane 1..3 (see the state model above for plane 2). The renderer half is
 * map_view_select_plane in gfx/map_view.h; whoever owns the main view calls
 * both, as the original's single function did. */
void world_select_plane(int plane);

/* Realm records. world_realm(plane) is &g_state.plane[plane].stats for
 * plane 1 or 3. */
static inline PlaneStats *world_realm(int plane)
{
    assert(plane == GS_PLANE_HELL || plane == GS_PLANE_HEAVEN);
    return &g_state.plane[plane].stats;
}
/* *0x630428 plane_stats_current */
static inline PlaneStats *realm_cur(void) { return g_world.realm; }
/* realm hist_a/hist_b entry [zone kind 0..6][j 0..15] */
static inline PlaneStatHist *realm_zone_stat(PlaneStatHist *hist, int kind, int j)
{
    assert(kind >= 0 && kind < 7 && j >= 0 && j < 16);
    return &hist[kind * 16 + j];
}

/* ---- Map cells of the selected plane (0x42b37e-0x42b88a) -------------------
 *
 * Cell layout in docs/map.md (and CELL_* in gfx/tile_draw.h): bits 0-15
 * flags, 16-22 tile entry index (part cells: dx in 16-18, dy in 20-22),
 * 23-26 strip, 27-31 tile set (0 = part of a bigger building). */

static inline uint32_t *cell_ptr(int x, int y)
{
    assert(x >= 0 && x < GS_MAP_W && y >= 0 && y < GS_MAP_H && g_world.cells);
    return &g_world.cells[y * GS_MAP_W + x];
}
static inline uint32_t cell_get(int x, int y) { return *cell_ptr(x, y); }

/* 0x42b46e map_cell_set_at: the tile set (bits 27-31) */
static inline unsigned cell_tileset(int x, int y) { return cell_get(x, y) >> 27; }
/* 0x42b4a0 map_cell_set_tileset */
static inline void cell_set_tileset(int x, int y, unsigned set)
{
    uint32_t *c = cell_ptr(x, y);
    *c = (*c & 0x07ffffffu) + (uint32_t)set * 0x08000000u;
}
/* 0x42b5cb map_cell_get_index: tile entry index - 1 (bits 16-22) */
static inline unsigned cell_index(int x, int y) { return (cell_get(x, y) >> 16) & 0x7f; }
/* 0x42b60b map_cell_set_index */
static inline void cell_set_index(int x, int y, unsigned index)
{
    uint32_t *c = cell_ptr(x, y);
    *c = (*c & 0xff80ffffu) + (uint32_t)index * 0x10000u;
}
/* 0x42b37e map_cell_get_strip (bits 23-26) */
static inline unsigned cell_strip(int x, int y) { return (cell_get(x, y) >> 23) & 0xf; }
/* 0x42b3be map_cell_set_strip (ORs the value in after clearing the field) */
static inline void cell_set_strip(int x, int y, unsigned strip)
{
    uint32_t *c = cell_ptr(x, y);
    *c = (*c & 0xf87fffffu) | (uint32_t)strip << 23;
}
/* 0x42b412 map_cell_set_flags */
static inline void cell_set_flags(int x, int y, uint32_t flags) { *cell_ptr(x, y) |= flags; }
/* 0x42b43f map_cell_clear_flags */
static inline void cell_clear_flags(int x, int y, uint32_t flags) { *cell_ptr(x, y) &= ~flags; }
/* 0x441a12 map_cell_test_bracket: bit 15 (CELL_BRACKET) */
static inline uint32_t cell_test_bracket(int x, int y) { return cell_get(x, y) & 0x8000u; }
/* 0x441a44 map_cell_test_shade: bit 0 (CELL_SHADE). sim_zone_scan skips
 * such cells. */
static inline uint32_t cell_test_shade(int x, int y) { return cell_get(x, y) & 1u; }
/* 0x441a74 map_cell_test_overlay: bit 2 (CELL_OVERLAY) */
static inline uint32_t cell_test_overlay(int x, int y) { return cell_get(x, y) & 4u; }

/* The origin cell of the building covering (x, y): itself unless the cell
 * has tile set 0, then (x - dx, y + dy) with dx = bits 16-18 and dy =
 * bits 20-22, computed in bytes like the original (inside 0x42b4f4 and
 * 0x42b65f). */
void cell_origin(int x, int y, int *ox, int *oy);
/* 0x42b4f4 map_cell_origin_set: tile set of the origin cell */
unsigned cell_origin_tileset(int x, int y);
/* 0x42b65f map_cell_origin_index: entry index of the origin cell */
unsigned cell_origin_index(int x, int y);
/* 0x42b75f map_cell_object_slot: the map object covering the cell
 * (object map >> 2) if CELL_OBJECT is set, else 0. */
unsigned cell_object_slot(int x, int y);

/* 0x42b7c5: building footprint n (n x n cells) of a zone-set (14/15) entry
 * index: zones by index % 13 (0-4: 1, 5-11: 2, 12: 3), specials
 * 0x5b-0x5f: 1, 0x60-0x6c: 2, 0x6d-0x6e: 3, 0x6f: 4, else 2. */
int tile_index_footprint(unsigned index);

/* ---- Simulation cells of the current realm --------------------------------
 *
 * 18-byte records, a union keyed by the map cell's tile set (docs/formats/
 * alsv.md lists which bytes each set uses). Known fields:
 *   all sets      +0      kind byte
 *   0 (parts), 14/15 (zones and buildings):
 *                 +1 low nibble  zone kind 0..6 (7 = none/mixed)
 *                 +2 u32  permanent SOULs, +6 u32 temporary SOULs; their sum
 *                         over all set 0/14/15 cells is the realm population
 *                         (PlaneStats.population, tests/world_test)
 *   0             +0xa/+0xb origin x/y of the building
 *   12 (roads)    +1 u32  soul group index in the high 16 bits
 *   5             +5 u32 (+9 u32 in Hell): drained 1/20 per scan into the
 *                         realm's souls pool (0x4606cf)
 *   any           +0xe bit 7: a flag (0x441aa4)
 */

static inline SimCell *sim_cell(int x, int y)
{
    assert(x >= 0 && x < GS_MAP_W && y >= 0 && y < GS_MAP_H && g_world.sim);
    return &g_world.sim[y * GS_MAP_W + x];
}
/* 0x43085d sim_cell_zone_kind */
static inline unsigned sim_zone_kind(int x, int y) { return sim_cell(x, y)->b[1] & 0x0f; }
/* 0x43089b sim_cell_set_zone_kind (ORs kind into the cleared nibble) */
static inline void sim_set_zone_kind(int x, int y, unsigned kind)
{
    SimCell *c = sim_cell(x, y);
    c->b[1] = (uint8_t)((c->b[1] & 0xf0) | kind);
}
/* 0x431b40 sim_cell_perm (both of the original's branches read the cell
 * itself) */
static inline uint32_t sim_perm(int x, int y) { return sim_cell_u32(sim_cell(x, y), 2); }
/* 0x431bd6 sim_cell_temp */
static inline uint32_t sim_temp(int x, int y) { return sim_cell_u32(sim_cell(x, y), 6); }
/* 0x431c6c sim_cell_pop */
static inline uint32_t sim_pop(int x, int y) { return sim_temp(x, y) + sim_perm(x, y); }
/* 0x441aa4 sim_cell_test_flag_e7 */
static inline unsigned sim_test_flag_e7(int x, int y) { return sim_cell(x, y)->b[0xe] >> 7; }

/* 0x458490: the soul group of a road cell, 0 if none or out of the map. A
 * stale index (group count 0) is cleared on the way. */
unsigned soul_group_get(int x, int y);
/* 0x45852f */
void soul_group_set(int x, int y, unsigned group);

#endif
