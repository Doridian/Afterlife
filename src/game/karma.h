/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Karma network (WP3, docs/game-map.md: 0x42d638-0x4306cf except
 * 0x4301b0, 0x43293d-0x436470): rails on the middle plane, Karma stations,
 * trains, plus the harbour boats of the realm year.
 *
 * How it fits together
 * --------------------
 * A Karma station (tile set 17, 3x3) stands on Hell or Heaven and has a
 * twin of set 17 on plane 2 at the same cells. Karma anchors (set 18,
 * "Karma portals", 3x3 with the origin at the front corner) only exist on
 * plane 2. Rails (set 16, entry = shape, bits 16-22 of the cell) run on
 * plane 2 between them; a rail cell whose realm cell is also set 16 is a
 * rail's touch-down point in a realm.
 *
 * karma_rebuild (0x433106, whenever g_build.karma_dirty is set) floods the
 * rails from each anchor and then from each station already connected:
 * every rail cell gets a network number 0..15 (cell bits 11-14) and the
 * stations reached get bits in their realm sim cell +9 (1 reached; 0x10
 * reached from an anchor, 0x20 from another station). Stations that
 * reach nothing are tinted (CELL_TINT, "no rail"); the realm cells under
 * rail touch-downs count the rail cells behind them (sim +1).
 *
 * karma_trains_tick (0x434dc4, every 500 ms with plane 2 selected) picks
 * a station (round robin with a random skip), takes a rail next to it,
 * routes to an anchor or another station (0x43455c: a depth-first search
 * leaving the direction to take in cell bits 8-9 and a mark in bit 10),
 * and when the network is idle (karma_busy) and enough map objects are
 * free, spawns a train: 1, 3 or 4 cars (map objects of set 16, by the
 * station's kind) that follow the direction bits. Each car moves one cell
 * per step (mapobj_step); the first car counts the cells travelled per
 * network (karma_load), and when the train arrives the trip length goes
 * into the history (karma_history[], whose average g_state.unk_5c6678
 * feeds the planet's belief drift) and the network is released.
 *
 * The direction table g_state.karma_dirs[4] ({dx, dy, n}, saved) is
 * reshuffled by karma_clear (0x434c38) before every rebuild, so floods and
 * routes try neighbours in a random order.
 *
 * Like the original, everything works on the planes through
 * world_select_plane and leaves the plane selected where the original
 * does. Sounds and view refreshes go through g_karma_hooks.
 */
#ifndef GAME_KARMA_H
#define GAME_KARMA_H

#include <stdbool.h>
#include <stdint.h>

typedef struct KarmaHooks {
    void (*sound)(uint32_t id, int prio); /* snd_play */
    void (*views_changed)(void);          /* microview_refresh (a station's tint changed) */
} KarmaHooks;

extern KarmaHooks g_karma_hooks;

/* Resets the runtime state (the station scan cursor, the train sound
 * timer). The original zeroes these only at process start (0x65eda8,
 * 0x65edac, 0x65ede4 are written by karma_trains_tick alone), so a second
 * game loaded in one session resumes the scan where the last one stopped.
 * The port resets them with sim_reset on every new game / load: the cursor
 * picks which station dispatches a train first, so without the reset two
 * runs of the same save in one process diverge (sim_test). */
void karma_reset(void);

/* 0x434c38: reshuffles g_state.karma_dirs (draws from the RNG). */
void karma_clear(void);
/* 0x433106: rebuilds network numbers, station bits, tints and touch-down
 * counts from the rails (plane 2 selected; leaves Heaven selected). */
void karma_rebuild(void);
/* 0x434dc4: maybe dispatches a train (every 500 ms, plane 2 selected). */
void karma_trains_tick(void);
/* 0x42fddf: drops one dangling rail cell (fewer than two non-land
 * neighbours) per call (sim phase 60; keeps the plane). */
void karma_rail_check(void);
/* 0x435f3d: the realm's yearly boat (not Karma, but in this range): 1 in
 * 7, a boat (map object of set 2) puts out from a random port (tile set 9,
 * the realm's list unk_78c4[], count in [0][0]) facing open water, on
 * the current realm (phase 80, per realm). It sails until it meets another
 * port or the coast (karma_boat_step). */
void karma_year(void);
/* 0x434d2d: while trains are pending (karma_pending), clears the count
 * once no map object is left on plane 2 (phase 80; selects plane 2). */
void karma_pending_scan(void);

/* 0x42f129(wnd, view, x, y, remove): the rail cell (x, y) of plane 2 was
 * (or is about to be) removed: traces the rails connected to it up to the
 * realm touch-downs and returns the number of cells reached (1 + the
 * traced cells; 0 if (x, y) is no rail). With `remove`, the traced cells
 * that lead nowhere become plain land. */
int karma_rail_remove(int x, int y, bool remove);

/* ---- Map object behaviour (called by mapobj_think) ---------------------- */

/* 0x43404a: one step of the train car in map object `slot` that just moved
 * from (px, py) to (x, y), showing `frame`. */
void karma_car_step(int px, int py, int x, int y, int slot, int frame);
/* 0x435509: one step of the boat in map object `slot`. */
void karma_boat_step(int slot);

#endif
