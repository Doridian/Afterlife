/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * tools/emu wrappers for the WP1 simulation (game/sim.h, economy.h,
 * realm.h): a controllable sim clock and access to the runtime globals
 * that aren't part of g_state.
 */
#include <stdint.h>
#include <string.h>

#include "game/economy.h"
#include "game/planet.h"
#include "game/realm.h"
#include "game/sim.h"
#include "game/souls.h"
#include "game/tables.h"
#include "game/world.h"

#if defined(_WIN32)
#define SHIM_API __declspec(dllexport)
#else
#define SHIM_API __attribute__((visibility("default")))
#endif

static uint32_t s_clock, s_step;
static uint32_t shim_clock(void)
{
    s_clock += s_step;
    return s_clock;
}

/* Fresh WP1 runtime state (tables, g_sim, the economy pass, the SOUL
 * flow's counters), no UI hooks, and a clock at `t` that advances by
 * `step` ms per read (the emulator's timeGetTime advances 16 per call;
 * 0 = stopped). */
SHIM_API void shim_sim_setup(uint32_t t, uint32_t step)
{
    tables_init_defaults();
    souls_reset();
    sim_reset();
    g_planet_hooks.souls = realm_souls_from_planet; /* the planet shim installs a logger */
    memset(&g_sim_hooks, 0, sizeof g_sim_hooks);
    g_sim_hooks.clock = shim_clock;
    s_clock = t;
    s_step = step;
}

SHIM_API void shim_sim_clock(uint32_t t, uint32_t step)
{
    s_clock = t;
    s_step = step;
}

/* g_sim fields by original address (0 if unknown) */
SHIM_API uint32_t shim_sim_get(uint32_t addr)
{
    switch (addr) {
    case 0x63911c: return g_sim.happiness_sum;
    case 0x6391c8: return g_sim.milestone_shown;
    case 0x6390e8: return (uint32_t)g_sim.souls_total.hi;
    case 0x6390ec: return g_sim.souls_total.lo;
    case 0x639220: return g_sim.groups_last;
    case 0x630510: return souls_groups_created;
    case 0x57e01c: return souls_gate_arrived;
    default: return 0;
    }
}

SHIM_API void shim_sim_set(uint32_t addr, uint32_t v)
{
    switch (addr) {
    case 0x63911c: g_sim.happiness_sum = v; break;
    case 0x6391c8: g_sim.milestone_shown = (uint8_t)v; break;
    case 0x6390e8: g_sim.souls_total.hi = (int32_t)v; break;
    case 0x6390ec: g_sim.souls_total.lo = v; break;
    case 0x630510: souls_groups_created = v; break;
    case 0x57e01c: souls_gate_arrived = v; break;
    default: break;
    }
}

/* g_tables as the original's bytes, for writing into the emulator:
 * which 0 = the BuildingSpec tables in the order of the address list in
 * tools/emu/test_sim.py, 1 = the level ranges, 2 = the soul rate curve.
 * Returns the bytes written (out may be NULL to ask). */
SHIM_API int shim_sim_tables(int which, uint8_t *out)
{
    const SimTables *t = &g_tables;
    int n = 0;
#define PUT8(v) (out ? (void)(out[n] = (uint8_t)(v)) : (void)0, n++)
#define PUT32(v) (PUT8(v), PUT8((v) >> 8), PUT8((v) >> 16), PUT8((v) >> 24))
    if (which == 0) {
        const BuildingSpec *tabs[] = {t->set7, t->set8, t->set9, t->set13, t->set17, t->set5, t->set19, t->set30};
        const int counts[] = {8, 6, 6, 8, 6, 6, 4, 3};
        for (int k = 0; k < 8; k++)
            for (int i = 0; i < counts[k]; i++) {
                PUT32(tabs[k][i].jobs);
                PUT32(tabs[k][i].capacity);
                PUT32(tabs[k][i].cost);
                PUT8(tabs[k][i].size);
            }
    } else if (which == 1) {
        for (int i = 0; i < 13; i++)
            PUT8(t->zone_levels[i].lo), PUT8(t->zone_levels[i].hi);
        for (int i = 0; i < 21; i++)
            PUT8(t->special_levels[i].lo), PUT8(t->special_levels[i].hi);
    } else if (which == 2) {
        for (int i = 0; i < 6; i++) {
            PUT32(t->soul_rate[i].souls);
            PUT32(t->soul_rate[i].rate_hi);
            PUT32(t->soul_rate[i].rate_lo);
        }
    }
#undef PUT8
#undef PUT32
    return n;
}
