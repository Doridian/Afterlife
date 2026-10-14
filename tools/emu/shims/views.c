/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * tools/emu wrappers for the information views' yearly records (WP6a,
 * ui/graphview.h): set the inputs of graph_history_record /
 * graph_history_update the way tools/emu/test_views.py does in the
 * original.
 */
#include <stdint.h>

#include "game/world.h"
#include "ui/graphview.h"

#if defined(_WIN32)
#define SHIM_API __declspec(dllexport)
#else
#define SHIM_API __attribute__((visibility("default")))
#endif

/* year, realm populations and SOUL pools, pennies and the six plain
 * history inputs (penny inflow/outflow, SOUL losses, efficiency, fate
 * structures, spiffiness) */
SHIM_API void shim_views_poke(uint32_t year, uint32_t hv_pop, int32_t hv_pool_hi, uint32_t hv_pool_lo, uint32_t hl_pop,
                              int32_t hl_pool_hi, uint32_t hl_pool_lo, int32_t pennies_hi, uint32_t pennies_lo,
                              uint32_t soul_rate, uint32_t inflow, uint32_t outflow, uint32_t losses, uint32_t eff,
                              uint32_t fate, uint32_t spiff)
{
    g_state.year = year;
    g_state.plane[3].stats.population = hv_pop;
    g_state.plane[3].stats.souls = (Bignum){hv_pool_hi, hv_pool_lo};
    g_state.plane[1].stats.population = hl_pop;
    g_state.plane[1].stats.souls = (Bignum){hl_pool_hi, hl_pool_lo};
    g_state.pennies = (Bignum){pennies_hi, pennies_lo};
    g_state.unk_630430 = soul_rate;
    g_state.unk_5fa5f4 = inflow;
    g_state.unk_5f7ed4 = outflow;
    g_state.unk_626598 = losses;
    g_state.unk_638690 = eff;
    g_state.unk_593250 = fate;
    g_state.unk_57e09c = spiff;
}
