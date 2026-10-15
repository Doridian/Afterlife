/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * SOUL flow (WP1b, docs/game-map.md; ALIFE.EXE 0x458490-0x4619ef).
 *
 * How SOULs move through a realm
 * ------------------------------
 * SOULs are tracked in sixteen categories (where they are headed): 0-6
 * permanent residents of a zone of kind k (the seven sins / virtues),
 * 7-13 temporary residents of kind k - 7 (SOULs that believe in
 * reincarnation; training centres also take these), 14 SOULs headed for a
 * Karma station (tile set 17, reincarnation to the planet) and 15 SOULs
 * headed for a Gate (set 13, on to the other realm). The realm's
 * `unk_7a08[cat]` rows are the ledger of SOULs in transit per category
 * ({total, by planet belief j 0..15}).
 *
 *  1. Arrivals (sim phase 30): SOULs of the dead (planet -> 0x445785, WP1)
 *     land in the ledger and the hist_b statistics. souls_gate_arrivals
 *     (0x46114f) puts them on the roads next to the realm's Gates,
 *     souls_siphon_release (0x460b16) the SOULs siphons held from last
 *     year.
 *  2. On the roads SOULs are kept in SOUL groups (g_state.soul_groups, a
 *     road cell's sim +1 high half is the group index): a total and the
 *     mask of categories it holds. soul_group_add (0x4609c7) adds SOULs
 *     at a road cell. Groups can be chained (SoulGroup.next).
 *  3. The zone scan (phases 100/90, souls_zone_scan 0x4607d6) walks the
 *     zone buildings (sets 14/15): SOULs whose time is up leave them for
 *     the next zone, a Karma station or a Gate, or go back to the planet
 *     (reincarnation); they are put on the nearest road as new groups.
 *     At the end the realm's SOULs age by one step (0x445416, WP1).
 *  4. The flow (phases 40/50, souls_flow 0x45e44b) takes every group on
 *     the map in turn: its total is split over the categories again by
 *     the realm-wide shares, then it walks the road network (a recursive
 *     depth-first walk from its cell along straight road runs, branching at
 *     junctions and Gates); every road cell it passes counts traffic and
 *     looks around (a diamond of radius 3) for buildings that take its
 *     categories: zones of a matching kind with room (they become
 *     residents), training centres (set 8), Karma stations (17), Gates
 *     (13), Omnibolges/Love Domes (5). Set-9 buildings (Karma portals?)
 *     let it jump to another one. What is left over is lost: up to the
 *     siphons' capacity it waits there (unk_7ef0[], released next year),
 *     the rest is counted lost (unk_809f[], unk_8113, unk_626598 ->
 *     lost_souls). Every group is freed after its turn, so the roads are
 *     empty after the flow until the next arrivals and departures.
 *
 * All of this runs on the selected plane (g_world, world_select_plane)
 * exactly like the original; the scheduler (WP1 sim_update) selects the
 * realm before each step.
 */
#ifndef GAME_SOULS_H
#define GAME_SOULS_H

#include <stdbool.h>
#include <stdint.h>

/* Categories (bits of SoulGroup.mask). */
enum {
    SOUL_CAT_KINDS = 7,    /* 0-6 permanent, 7-13 temporary, by zone kind */
    SOUL_CAT_STATION = 14, /* -> Karma station (set 17) */
    SOUL_CAT_GATE = 15,    /* -> Gate (set 13) */
    SOUL_CATS = 16
};

/* ---- Sim phase entry points (the selected plane must be the realm) ---- */

/* 0x4607d6 sim_zone_scan, phases 100 (Heaven) / 90 (Hell): updates up to 50
 * zone buildings (and every set-5 cell passed) per call, resuming where the
 * last call stopped. Returns true once the whole map is done (and runs the
 * end-of-scan ageing, 0x445416). */
bool souls_zone_scan(void);

/* 0x45e44b, phases 40 (Heaven) / 50 (Hell): walks every SOUL group on the
 * map. The original stops after the group that takes it past 100 ms of
 * real time and resumes on the next call; here `max_groups` is that
 * budget (0 = no limit). Returns true when the whole map is done. */
bool souls_flow(unsigned max_groups);

/* 0x46114f, phase 30: SOULs in the ledger go out through the realm's
 * Gates onto the roads around them. */
void souls_gate_arrivals(void);
/* 0x460b16, phase 30: siphons (set 30) release the SOULs they hold. */
void souls_siphon_release(void);

/* ---- Shared with other packages ---------------------------------------- */

/* 0x4609c7: add n SOULs of category cat to the group at road cell (x, y)
 * (a new group if the cell has none). Returns the group index. */
unsigned soul_group_add(uint32_t n, unsigned cat, int x, int y);

/* 0x45ebe2: the road cell to put SOULs on for a structure of size x size
 * cells at (x, y) (its origin cell, footprint x..x+size-1, y-size+1..y):
 * the closest un-shaded road within `radius` (Manhattan distance to the
 * footprint), random among equals, preferring cells that already hold a
 * group. Returns the x coordinate and sets *ox and *oy (both may be NULL),
 * or 0xff (and 0xff in both) if there is none. Used by WP2 (placement), WP3, Soulview. */
unsigned area_search(int x, int y, unsigned size, unsigned radius, int *ox, int *oy);

/* 0x45fd84: remove n SOULs from the zone building at (x, y) (it is being
 * destroyed or rebuilt): they leave like departing SOULs would. Used by
 * WP1 (building upgrade 0x43bbe7) and WP2 (destroy 0x42c09b). */
void souls_zone_evict(int x, int y, uint32_t n);

/* 0x461636: put n SOULs of category cat at the next Gate with room
 * (round robin over the ports list), ledger row cat / belief j (j < 0:
 * total only). Returns false if no Gate could take them. */
bool souls_gate_spawn(uint32_t n, unsigned cat, int j);

/* 0x45ef44: the category a resident of zone kind `kind`, planet belief j,
 * moves on to when it leaves its zone (the next sin / virtue by rank, a
 * Karma station 14 or a Gate 15), or -1: back to the planet. Used by the
 * Soulview trackers (soultrack.c). */
int souls_zone_exit_cat(uint8_t kind, uint8_t j);
/* 0x45f074: n residents of belief j of the zone building cell (cx, cy) go
 * back to the planet through the road at (rx, ry). Returns the last SOUL
 * group used (0 if none). */
unsigned souls_reincarnate(uint32_t n, int8_t j, int rx, int ry, uint8_t cx, uint8_t cy);

/* ---- Runtime counters (not saved) -------------------------------------- */

/* 0x630510: SOUL groups created; the scheduler copies and clears it at the
 * end of phase 50. Also biases area_search (busier year, fewer new
 * groups). */
extern uint32_t souls_groups_created;
/* 0x57e01c: SOULs that came in through Gates this year (pays per SOUL in
 * phase 30; the scheduler clears it). */
extern uint32_t souls_gate_arrived;
/* 0x65fe54: SOULs leaving Hell for Heaven (through Hell's Gates and its
 * set-5 drain), spawned at Heaven's Gates at the start of Heaven's flow. */
extern uint32_t souls_to_heaven;
/* 0x65fe58: SOULs leaving Heaven through its Gates, spawned in Hell (split
 * by g_state.unk_638670[]) at the start of Hell's flow. */
extern uint32_t souls_to_hell;

/* Port-only bookkeeping for tests (the original keeps none of this):
 * where the walking groups' SOULs went, and what left zone buildings. */
typedef struct SoulsStats {
    uint64_t unpacked;      /* SOULs groups carried into their walks (0x45ddf0) */
    uint64_t tracked_added; /* re-added for Soulview trackers (0x45c038) */
    uint64_t to_zones;      /* new residents (sum of 0x4324c0/0x43258b) */
    uint64_t walk_zones, walk_training, walk_gates, walk_stations, walk_omnibolges, walk_siphons, walk_lost;
    uint64_t walk_left;     /* still unplaced when a group was freed */
    uint64_t departed;      /* zone scan: residents due to leave (with a road) */
    uint64_t departed_lost; /* zone scan: due residents without a road */
    uint64_t left_zones;    /* zone scan/evict: residents actually removed */
    uint64_t left_to_roads; /* zone scan/evict: SOULs put into groups */
} SoulsStats;
extern SoulsStats souls_stats;

/* Resets the module's runtime state (scan cursors, counters) to the
 * original's process start values. */
void souls_reset(void);

#endif
