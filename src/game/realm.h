/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Realm population and per-cell rules (WP1, docs/game-map.md):
 * 0x4306d0-0x43293c (vibes, happiness nibble, population of buildings,
 * the set-29 rock network), 0x43a493-0x43dc3b (vibes, zone happiness,
 * building growth and replacement, milestones), 0x43e59d-0x43eb7b,
 * 0x445416 (SOUL ageing), 0x445785 (planet SOULs entering), 0x446242
 * (cross-realm worker overflow), 0x446829 / 0x447e8f (ports), 0x42dd50
 * (set-30 siphons). The realm year, money and score are game/economy.h;
 * the scheduler is game/sim.h.
 *
 * Everything works on the current realm (g_world, world_select_plane) like
 * the original. Population lives in the sim cells of zone buildings: +2
 * u32 permanent SOULs, +6 u32 temporary ones, per cell of the footprint
 * (docs/game-map.md "State model"). The realm keeps the totals:
 * population (+0x7fec), per zone kind unk_7f74[] (permanent) / unk_7f3c[]
 * (temporary), and the global unk_5bb114 / unk_57e1b4 (all permanent /
 * temporary SOULs).
 *
 * Map cell bits used here (docs/map.md): 8-12 vibes + 15 ("vibes", -15..16),
 * bit 1 a building's "not growing" mark, bit 2 CELL_OVERLAY (a building
 * that lost road access: halved capacity, bad vibes), bit 5 a building under
 * construction (a map object animates it), bit 15 CELL_BRACKET (workers
 * lent to the other realm, 0x446242). Sim cell byte 0: bit 4 / bit 5 scan
 * marks of the flood fills; +1 high nibble: zone happiness 0..15.
 */
#ifndef GAME_REALM_H
#define GAME_REALM_H

#include <stdbool.h>
#include <stdint.h>

#include "game/util.h"
#include "game/world.h"

/* ---- Per-cell values (0x4306d0-0x43094c) ---------------------------------- */

/* 0x4306d0: sets the cell's vibes (-15..16; stored + 15 in bits 8-12) */
void cell_set_vibes(int x, int y, int v);
/* 0x430737: the cell's vibes, -15..16 */
int cell_vibes(int x, int y);
/* 0x43077b: zone happiness of a sim cell (+1 high nibble, 0..15) */
unsigned sim_happiness(int x, int y);
/* 0x4307bf: happiness summed over the n x n footprint with origin (x, y)
 * (x .. x+n-1, y .. y-n+1), wrapping at 8 bits like the original */
uint8_t sim_happiness_sum(int x, int y, unsigned n);
/* 0x43094c: scan mark bit 4 of sim byte 0 */
unsigned sim_scan_mark(int x, int y);
/* 0x430989: clears scan marks 0x10 and 0x20 of every sim cell */
void sim_clear_scan_marks(void);

/* ---- The set-29 network (0x430d57-0x43188a) ----------------------------------
 *
 * (The rock helpers 0x430a77, 0x430b43 and 0x430ca0 of this range are
 * ported in game/structure.h: rock_mark, rock_unmark, rock_value.) */

/* 0x43188a: yearly pass over the set-29 structures: each connected group
 * (flood fill through built cells) sums its rock yield into the first
 * cell's +5, which is then handed out to the zones it reaches (realm
 * unk_80e1 = zones supplied, unk_80e3 = yield left over). */
void realm_set29_year(void);

/* ---- Population of buildings (0x431ca0-0x43258b) ---------------------------- */

/* 0x431ca0: permanent SOULs of the building covering (x, y) */
uint32_t realm_building_perm(int x, int y);
/* 0x431ebf: temporary SOULs of the building covering (x, y) */
uint32_t realm_building_temp(int x, int y);
/* 0x4320de */
uint32_t realm_building_pop(int x, int y);
/* 0x432112: sim cell +10 (accumulated SOUL-years) per SOUL of the building */
uint32_t realm_building_years_per_soul(int x, int y);
/* 0x4324c0 (perm) and 0x43258b (temp): adds n SOULs to the cell and the
 * realm totals; kind -1 skips the per-kind total. The original's unused 4th
 * argument is kept as `special`. */
void realm_perm_add(int x, int y, uint32_t n, int special, int kind);
void realm_temp_add(int x, int y, uint32_t n, int special, int kind);
/* 0x43217a (perm) and 0x43231d (temp): removes up to n SOULs (clamped to
 * the realm population and the cell's count). */
void realm_perm_sub(int x, int y, uint32_t n, int special, int kind);
void realm_temp_sub(int x, int y, uint32_t n, int special, int kind);

/* ---- Karma helpers in this range (0x432656-0x432713, used by WP3) -------- */

/* 0x432656: whether a Karma rail piece (entry) is a corner/junction */
bool karma_piece_is_branch(unsigned entry);
/* 0x4326c2: 0 and 7 are the station's dead-end pieces */
bool karma_piece_is_through(unsigned entry);
/* 0x432713: marks direction bits on the Karma station (set 17, sim +9)
 * under plane-2 cell (x, y) in Hell, or else Heaven; returns the old bits
 * | bits, or 0 if bit 0 was set. Leaves plane 2 selected. */
unsigned karma_station_mark(int x, int y, unsigned bits);

/* ---- Vibes and happiness (0x43a493-0x43b86f) -------------------------------- */

/* 0x43a493: spreads `base` (+ step per cell of distance, out to
 * |base / step|) around the structure at (x, y) into the vibes; `inner`
 * also sets the footprint itself. */
void realm_influence(int x, int y, int base, int step, bool inner);
/* 0x43a81d: recomputes the vibes of the current realm from scratch */
void realm_vibes_update(void);
/* 0x43b13a, 0x43b210, 0x43b2a7, 0x43b469: the four parts of a zone
 * building's happiness (SOUL-years per SOUL, vibes, the perm/temp mix
 * against the building's wish, neighbours); they count unhappy buildings
 * in realm unk_8105 / unk_8107 / unk_8109. */
int realm_score_years(int x, int y);
int realm_score_vibes(int x, int y);
int realm_score_mix(int x, int y);
int realm_score_neighbours(int x, int y);
/* 0x43b86f: recomputes the happiness nibble of every zone building */
void realm_happiness_update(void);

/* ---- Growth (0x43b9ac-0x43d2fc) ------------------------------------------------ */

/* 0x43d2fc: entry is an empty zone tile (entry % 13 == 0 below 0x5c) */
bool zone_entry_is_empty(unsigned entry);
/* 0x43ea54: level of an entry within its kind (entry % 13, or - 0x5b) */
unsigned zone_entry_level(unsigned entry);
/* 0x43bbe7: replaces the building at (x, y) by set/entry with footprint n
 * (parts, strips by view rotation, merges the parts' SOULs into the
 * origin; SOULs over the new capacity move out through 0x45fd84). */
void realm_building_replace(int x, int y, unsigned set, unsigned entry, unsigned n);
/* 0x43c533: up/downgrades the zone building at (x, y) to the entry that
 * fits footprint n and happiness `level` (through the construction
 * animation if a map object is free). */
void realm_building_upgrade(int x, int y, unsigned n, unsigned level, unsigned entry, unsigned set);
/* 0x43c9a0: the yearly growth test of a zone building: shrink if unhappy,
 * else try to merge with happy neighbours into a bigger one. */
void realm_zone_grow(int x, int y, unsigned entry, unsigned n, unsigned level);
/* 0x43d259: the decay test of a building without road access */
void realm_zone_decay(int x, int y, unsigned entry, unsigned n, unsigned level);
/* 0x43c1b1: special structures (entry > 0x5f) keep one cell per zone
 * kind; duplicates are merged */
void realm_specials_dedupe(void);

/* ---- Milestones and special arrangements (0x43d9c0-0x43ea93) -------------- */

/* 0x43d9c0: population milestone pop-ups of the current realm (enable more
 * random Bad Things in unk_5f746c) */
void realm_milestones(void);
/* 0x43db51: marks special site i (g_build.special_pos) on the map */
void realm_mark_special_site(unsigned i);
/* 0x43e59d: an empty zone at (x, y) that completes one of the two secret
 * arrangements becomes the matching set-3 structure; true if so */
bool realm_secret_structure(int x, int y, unsigned entry);
/* 0x43ea93: a building without workers falls into ruin (destroyed, 1 in 5
 * becomes rocks) */
void realm_building_ruin(int x, int y, unsigned n);

/* ---- SOULs (0x445416, 0x445785) ------------------------------------------------- */

/* 0x445416: end of the zone scan, ages the realm's SOULs. For each
 * [zone kind][j] entry with an age step count (unk_04, from the zone scan)
 * and SOULs past the first two buckets (unk_00 - v[0] - v[1]), that many
 * SOULs move one age bucket down (v[b] -> v[b - 1], b = 2..31), taken from
 * the buckets in proportion (rand_range rounds tiny shares up); unk_04 is
 * cleared. */
void realm_age_departures(void);
/* 0x445785: SOULs of belief type `type` (0..15) dying in planet sector
 * (sx, sy): `size` times, `count` SOULs are judged by the sector's sin
 * levels (three crt_rand draws per sin) and added to the arrivals of Hell
 * and/or Heaven (hist_b, unk_7a08) and the belief statistics. The
 * signature of PlanetHooks.souls (game/planet.h). */
void realm_souls_from_planet(int sx, int sy, int type, uint32_t count, uint32_t size);

/* ---- Workers and ports (0x446242-0x447e8f, 0x42dd50) --------------------------- */

/* 0x446242: the worker surplus/shortage of realm `from` borrows from or
 * lends to the buildings of realm `to` (CELL_BRACKET); keeps the plane. */
void realm_worker_overflow(int from, int to);
/* 0x446829: year end, ports move this year's arrivals (+1) to +5 */
void realm_ports_year(void);
/* 0x447e8f: port gates open/close with their traffic (every 500 ms) */
void realm_ports_animate(void);
/* 0x42dd50: hands the realm's siphon pool (unk_7f38) to the set-30 cells */
void realm_siphons_fill(void);
/* 0x42dcdf siphon_gauge_fill / 0x42dc42 siphon_gauge_total: the two gauges
 * of the siphon with origin (x, y) and set-30 entry e on the sim grid `sim`
 * (the original reads the current realm's): `fill` 246..255 from the
 * year's intake (+5) against the entry's capacity, `total` 1..9 from the
 * store (+9) against 42 x capacity. The map renderer draws them as the
 * set-30 meter (gfx/tile_draw.h TileMeterFn). False if e is past the
 * table (the original reads whatever follows it). */
bool realm_siphon_gauges(const SimCell *sim, int x, int y, unsigned e, uint8_t *fill, uint8_t *total);
/* The set-30 meter of the map renderer (a gfx/tile_draw.h TileMeterFn;
 * tile_draw 0x4e6a6a calls the two gauges): lo = total, hi = fill, read
 * from the sim cells of the plane the view draws, the way
 * map_view_select_plane (0x42b102) moves sim_cells_current along with the
 * view. ctx is the GameState (NULL: g_state; the middle plane then uses
 * g_world's current realm like the original, other states draw no meter
 * there). */
struct MapView;
bool realm_siphon_meter(void *ctx, const struct MapView *v, int x, int y, int index0, uint8_t *lo, uint8_t *hi);
/* 0x448077: recomputes hist_a[].unk_00 as the sum of its buckets */
void realm_hist_totals(void);

#endif
