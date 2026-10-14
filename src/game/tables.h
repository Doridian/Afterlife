/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Simulation constant tables: building specs, zone level ranges and the
 * soul-rate curve.
 *
 * In ALIFE.EXE these are .bss globals that game_init_defaults (0x45546c)
 * and game_reset_world (0x4535db) fill with fixed values; they are not
 * saved. game_init_defaults belongs to WP2 (newgame.c), which should call
 * tables_init_defaults() where the original writes them; the simulation
 * (WP1 realm year, WP1b arrivals, the views) only reads them. world_load
 * does not fill them, so code that loads a save before any new game calls
 * tables_init_defaults() itself (it is idempotent).
 */
#ifndef GAME_TABLES_H
#define GAME_TABLES_H

#include <stdint.h>

/* One 13-byte record {u32, u32, u32, u8} of the original. Most tables have
 * every entry twice (tile entry index 2k and 2k+1 share one), see below. */
typedef struct BuildingSpec {
    uint32_t jobs;     /* +0x0: workers the building employs (siphons: x1000) */
    uint32_t capacity; /* +0x4: SOULs / trainees / pennies it handles */
    uint32_t cost;     /* +0x8: price */
    uint8_t size;      /* +0xc: footprint n (n x n cells) */
} BuildingSpec;

/* Zone level range {lo, hi} of the u16 tables 0x639250 / 0x6390f0
 * (0x43b9ac returns the u16, lo in the low byte). */
typedef struct LevelRange {
    uint8_t lo, hi;
} LevelRange;

/* 0x6391d0: soul rate curve, 6 points {SOUL total, rate a, rate b}
 * (8.8 fixed point rates), read by sim_soul_rate (0x448150). */
typedef struct SoulRatePoint {
    uint32_t souls, rate_hi, rate_lo;
} SoulRatePoint;

typedef struct SimTables {
    /* 0x6304a0 tile set 7 (training centres): 8 entries, index = cell entry */
    BuildingSpec set7[8];
    /* 0x57e110 tile set 8: 6 entries */
    BuildingSpec set8[6];
    /* 0x57e0b0 tile set 9: 6 entries */
    BuildingSpec set9[6];
    /* 0x630200 tile set 13 (ports; index entry >> 1) and, in the realm
     * year only, set 19 (index entry): 8 entries */
    BuildingSpec set13[8];
    /* 0x5f7410 tile set 17: 6 entries (index (entry - 4) >> 1, or the sim
     * cell's +10 byte for the capacity) */
    BuildingSpec set17[6];
    /* 0x59cec0 tile set 5: 6 entries */
    BuildingSpec set5[6];
    /* 0x630280 tile set 19 (banks): 4 entries */
    BuildingSpec set19[4];
    /* 0x57e030 tile set 30 (siphons): 3 entries, jobs in 1/1000 */
    BuildingSpec set30[3];
    /* 0x639250: zone building levels by entry % 13 */
    LevelRange zone_levels[13];
    /* 0x6390f0: special structure levels by entry - 0x5b */
    LevelRange special_levels[21];
    /* 0x6391d0..0x639217 (game_reset_world) */
    SoulRatePoint soul_rate[6];
} SimTables;

extern SimTables g_tables;

/* The table writes of game_init_defaults (0x45546c) and the soul-rate
 * curve of game_reset_world (0x4535db). */
void tables_init_defaults(void);

#endif
