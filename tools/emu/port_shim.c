/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * C entry points of libafterlife_port (the port built as a shared library
 * for tools/emu, the differential-testing harness). Every non-static
 * function of afterlife_core is exported as is and can be called from
 * Python through ctypes; this file adds what can't be reached that way:
 * the static inline accessors of game/world.h, pointers into g_state and
 * g_world, and save/load helpers. Package-specific wrappers go into
 * tools/emu/shims/<package>.c (built in automatically).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "game/savegame.h"
#include "game/util.h"
#include "game/world.h"

#if defined(_WIN32)
#define SHIM_API __declspec(dllexport)
#else
#define SHIM_API __attribute__((visibility("default")))
#endif

/* ---- state ------------------------------------------------------------- */

SHIM_API int shim_world_load(const char *path, char *err, int err_len)
{
    return world_load(path, err, (size_t)err_len) ? 1 : 0;
}

SHIM_API int shim_world_load_mem(const uint8_t *data, size_t len, char *err, int err_len)
{
    GameState *tmp = calloc(1, sizeof *tmp);
    if (!tmp)
        return 0;
    if (!savegame_load_mem(tmp, data, len, err, (size_t)err_len)) {
        free(tmp);
        return 0;
    }
    world_apply_loaded(tmp);
    free(tmp);
    return 1;
}

/* Serializes g_state as format `version` (0: its own). free() the result
 * with shim_free. */
SHIM_API uint8_t *shim_save_mem(int version, size_t *len)
{
    int keep = g_state.hdr.version;
    if (version)
        g_state.hdr.version = version;
    uint8_t *buf = savegame_save_mem(&g_state, len);
    g_state.hdr.version = keep;
    return buf;
}

SHIM_API void shim_free(void *p) { free(p); }

/* Pointers to the parts of the port state whose layout is the original's
 * (so the harness can compare them byte for byte with the emulated
 * globals), by name; NULL for unknown names. *size gets the byte size. */
SHIM_API void *shim_ptr(const char *what, int plane, size_t *size)
{
    size_t dummy;
    if (!size)
        size = &dummy;
    *size = 0;
    if (plane < 0 || plane > 3)
        return NULL;
    MapPlaneState *p = &g_state.plane[plane];
    if (!strcmp(what, "state"))
        return *size = sizeof g_state, (void *)&g_state;
    if (!strcmp(what, "world"))
        return *size = sizeof g_world, (void *)&g_world;
    if (!strcmp(what, "cells"))
        return *size = sizeof p->cells, (void *)p->cells;
    if (!strcmp(what, "objmap"))
        return *size = sizeof p->objmap, (void *)p->objmap;
    if (!strcmp(what, "sim"))
        return *size = sizeof p->sim, (void *)p->sim;
    if (!strcmp(what, "tile_info"))
        return *size = sizeof g_world.tile_info_hell,
               (void *)(plane == GS_PLANE_HEAVEN ? g_world.tile_info_heaven : g_world.tile_info_hell);
    if (!strcmp(what, "planet_map"))
        return *size = g_state.planet.map ? g_state.planet.map_size : 0, (void *)g_state.planet.map;
    return NULL;
}

/* g_world's selection as the original's globals see it. out[]:
 *  0 plane (map_view +0x47)       1 plane of cells (map_cells_current)
 *  2 plane of sim (sim_cells_current)  3 plane of realm (plane_stats_current)
 *  4 plane of tile_info (realm_tile_info)  5 water_entry_max 0x65ed74
 *  6 unk_65ed78  7 road_entry 0x65ed7c  8 objframe_base 0x65ed80
 *  9 select_count 0x63921c  10 plane of objmap (map_view +0x04)
 * Planes are 0 when the pointer is NULL or points elsewhere. */
SHIM_API void shim_world_info(int32_t out[11])
{
    int pc = 0, ps = 0, pr = 0, pt = 0, po = 0;
    for (int p = 1; p <= 3; p++) {
        if (g_world.cells == g_state.plane[p].cells)
            pc = p;
        if (g_world.objmap == g_state.plane[p].objmap)
            po = p;
        if (g_world.sim == g_state.plane[p].sim)
            ps = p;
        if (g_world.realm == &g_state.plane[p].stats)
            pr = p;
    }
    if (g_world.tile_info == g_world.tile_info_hell)
        pt = 1;
    else if (g_world.tile_info == g_world.tile_info_heaven)
        pt = 3;
    out[0] = g_world.plane, out[1] = pc, out[2] = ps, out[3] = pr, out[4] = pt;
    out[5] = g_world.water_entry_max, out[6] = g_world.unk_65ed78, out[7] = g_world.road_entry;
    out[8] = g_world.objframe_base, out[9] = (int32_t)g_world.select_count, out[10] = po;
}

/* ---- world.h static inline accessors -------------------------------------- */

SHIM_API uint32_t shim_cell_get(int x, int y) { return cell_get(x, y); }
SHIM_API unsigned shim_cell_tileset(int x, int y) { return cell_tileset(x, y); }
SHIM_API void shim_cell_set_tileset(int x, int y, unsigned v) { cell_set_tileset(x, y, v); }
SHIM_API unsigned shim_cell_index(int x, int y) { return cell_index(x, y); }
SHIM_API void shim_cell_set_index(int x, int y, unsigned v) { cell_set_index(x, y, v); }
SHIM_API unsigned shim_cell_strip(int x, int y) { return cell_strip(x, y); }
SHIM_API void shim_cell_set_strip(int x, int y, unsigned v) { cell_set_strip(x, y, v); }
SHIM_API void shim_cell_set_flags(int x, int y, uint32_t f) { cell_set_flags(x, y, f); }
SHIM_API void shim_cell_clear_flags(int x, int y, uint32_t f) { cell_clear_flags(x, y, f); }
SHIM_API uint32_t shim_cell_test_bracket(int x, int y) { return cell_test_bracket(x, y); }
SHIM_API uint32_t shim_cell_test_shade(int x, int y) { return cell_test_shade(x, y); }
SHIM_API uint32_t shim_cell_test_overlay(int x, int y) { return cell_test_overlay(x, y); }
SHIM_API unsigned shim_sim_zone_kind(int x, int y) { return sim_zone_kind(x, y); }
SHIM_API void shim_sim_set_zone_kind(int x, int y, unsigned k) { sim_set_zone_kind(x, y, k); }
SHIM_API uint32_t shim_sim_perm(int x, int y) { return sim_perm(x, y); }
SHIM_API uint32_t shim_sim_temp(int x, int y) { return sim_temp(x, y); }
SHIM_API uint32_t shim_sim_pop(int x, int y) { return sim_pop(x, y); }
SHIM_API unsigned shim_sim_test_flag_e7(int x, int y) { return sim_test_flag_e7(x, y); }

/* ---- Bignum through pointers (ctypes-friendly) ------------------------------ */

SHIM_API void shim_bignum_add(int32_t *hi, uint32_t *lo, uint32_t v)
{
    Bignum b = {*hi, *lo};
    bignum_add(&b, v);
    *hi = b.hi, *lo = b.lo;
}
SHIM_API void shim_bignum_sub(int32_t *hi, uint32_t *lo, uint32_t v)
{
    Bignum b = {*hi, *lo};
    bignum_sub(&b, v);
    *hi = b.hi, *lo = b.lo;
}
SHIM_API int shim_bignum_less(int32_t hi, uint32_t lo, int32_t v) { return bignum_less((Bignum){hi, lo}, v); }
SHIM_API int shim_bignum_ge_int(int32_t hi, uint32_t lo, int32_t v) { return bignum_ge_int((Bignum){hi, lo}, v); }
SHIM_API int shim_bignum_ge(int32_t ah, uint32_t al, int32_t bh, uint32_t bl)
{
    return bignum_ge((Bignum){ah, al}, (Bignum){bh, bl});
}
SHIM_API int shim_bignum_gt(int32_t ah, uint32_t al, int32_t bh, uint32_t bl)
{
    return bignum_gt((Bignum){ah, al}, (Bignum){bh, bl});
}
