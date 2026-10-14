/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The realm year: jobs, workers, wages, banks and loans, upkeep (WP1,
 * docs/game-map.md: 0x43ed55, 0x43eb7c, 0x43e4e5).
 *
 * Money is g_state.pennies (a sign-and-magnitude Bignum, game/util.h).
 * Each year, per realm (Heaven in phase 70, Hell in phase 80), the realm
 * year walks the map column by column and:
 *
 *  - counts the jobs of every structure (zones: RealmTileInfo.unk_08 of the
 *    building; set 16 rails 120, set 29 240, the others the `jobs` of
 *    their BuildingSpec, siphons / 1000) into the realm's per-set job
 *    totals, and the capacity of training centres (set 7), ports (13),
 *    sets 8, 9 and 17;
 *  - lets zone buildings grow or shrink (realm_zone_grow / _decay), ruins
 *    some without workers, retiles roads by traffic;
 *  - banks (set 19): in Heaven a loan is repaid by its instalments
 *    (pennies -= amount / instalments left, the rest grows by interest
 *    sim +6 per mille), in Hell a loan comes due at once (a default
 *    garnishes SOULs);
 *  - charges a penalty for zone buildings without happiness data
 *    (realm unk_8127).
 *
 * At the end of the pass (indices into PlaneStats.unk_7ff0[], offset
 * 0x7ff0 + 4 i, see the RW_* names below):
 *   trained = min(training capacity of active centres, jobs)
 *   workforce: if last year's workforce + surplus + set-8 workers < jobs,
 *     it all goes to work and the rest is a shortfall; else the workforce
 *     is the jobs and the surplus grows (or shrinks) by the difference.
 *   wages = trained workers at (rate D + rate A) / 255 each, other
 *     workers at (rate A + rate C) / 255, the shortfall at (D + B) / 255
 *     up to the trainees left and (B + C) / 255 beyond (all mul_div_clamp,
 *     rates in realm unk_8034..unk_8040, Macromanager funding)
 *   road upkeep = sum over road cells of 4^(digits of traffic - 1) / 0x45
 *   pennies -= penalty + wages (not in editor mode) + road upkeep
 *
 * The incremental pass stops after 100 ms of the sim clock and resumes on
 * the next call (the original's behaviour; with a stopped clock it runs
 * the whole map).
 */
#ifndef GAME_ECONOMY_H
#define GAME_ECONOMY_H

#include <stdbool.h>
#include <stdint.h>

/* PlaneStats.unk_7ff0[] (record offset 0x7ff0 + 4 * index) */
enum {
    RW_IMPORTED = 0,     /* +0x7ff0: jobs not filled by trained workers */
    RW_TRAINED = 1,      /* +0x7ff4: homegrown (trained) workers */
    RW_WORKFORCE = 2,    /* +0x7ff8: workers employed */
    RW_SHORTFALL = 3,    /* +0x7ffc: jobs without a worker */
    RW_SURPLUS = 4,      /* +0x8000: workers without a job */
    RW_LENT = 5,         /* +0x8004: workers lent to / borrowed from the other realm (0x446242) */
    RW_JOBS_ZONES = 6,   /* +0x8008 */
    RW_JOBS_SET9 = 7,    /* +0x800c */
    RW_JOBS_SET17 = 8,   /* +0x8010 Karma stations */
    RW_JOBS_SET16 = 9,   /* +0x8014 Karma rails */
    RW_JOBS_SET29 = 10,  /* +0x8018 */
    RW_JOBS_SET19 = 11,  /* +0x801c banks */
    RW_JOBS_SET8 = 12,   /* +0x8020 */
    RW_JOBS_SET13 = 13,  /* +0x8024 ports/gates */
    RW_JOBS_SET30 = 14,  /* +0x8028 siphons */
    RW_WAGES = 15,       /* +0x802c: wages paid this year */
    RW_ROAD_UPKEEP = 16, /* +0x8030 */
    RW_RATE_A = 17,      /* +0x8034 (new game: 1) */
    RW_RATE_B = 18,      /* +0x8038 (4) */
    RW_RATE_C = 19,      /* +0x803c (2) */
    RW_RATE_D = 20,      /* +0x8040 (0) */
};

/* 0x43ed55: the realm year of the current realm, incremental. Returns true
 * when the pass is complete. */
bool economy_realm_year(void);

/* 0x43eb7c: the yearly score (g_state.unk_57e09c) from the variety of
 * both realms, SOULs, pennies, EMBOs, lost SOULs and the average
 * happiness. */
void economy_score(void);

/* 0x43e4e5: maybe starts a random Bad Thing (enabled kinds in
 * g_state.unk_5f746c, chance by difficulty). */
void economy_random_bad_things(void);

/* Process-start state of the module (the pass cursor, the realms'
 * "Over 90% homegrown" warning switches). */
void economy_reset(void);

#endif
