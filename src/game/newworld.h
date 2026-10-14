/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * World setup (WP2, 0x4514a4-0x455ed6 in ALIFE.EXE): new world generation
 * (game_reset_world), the startup defaults, the per-realm tile type tables
 * and the recompute the loader runs after reading a save.
 */
#ifndef GAME_NEWWORLD_H
#define GAME_NEWWORLD_H

#include <stdbool.h>

/* 0x45546c game_init_defaults: the build tool defaults and parameter
 * tables, then game_reset_world. Called once at startup by the original. */
void game_init_defaults(void);

/* 0x4535db game_reset_world: a new game in g_state for difficulty
 * g_state.hdr.difficulty (1-3; anything else is treated like the original
 * does, leaving the money unset): sin/virtue weights, empty realms, water
 * borders and a river on each realm, 3-6 Karma stations, rocks, the
 * Planet (hook), map objects, the Karma Station in the centre, tile type
 * tables. Draws its random numbers in the original's order. Ends with
 * Hell selected. */
void game_reset_world(void);

/* 0x452581: fills the current realm's RealmTileInfo table (costs, kinds,
 * weights; Hell's weights negated), sets realm +0x8046 to 1 and runs
 * realm_tile_costs_update(true). */
void realm_tile_info_fill(void);
/* The table part of 0x452581 alone (no state change), for both realms;
 * what a loaded game finds in the original's process. */
void realm_tile_info_init(void);

/* 0x452267: the cost inflation of the current realm: with `force` also
 * rebuilds g_build.special_pos from the set-4 cells; derives a level
 * 1..100 from the EMBOs (g_state.embos_next), keeps realm +0x8046 at the
 * maximum of it and the stored level, and sets each tile type's cost to
 * base * level / 20. */
void realm_tile_costs_update(bool force);

/* What save_load_game (0x48f837) runs after reading a file, between its
 * plane selects: zone stand-ins per g_build.view_flags (0x4506b6) and
 * realm_tile_costs_update(true) for Heaven and Hell. Leaves Hell selected.
 * world_apply_loaded calls it. */
void build_after_load(void);

#endif
