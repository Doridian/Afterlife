/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Structures on the map (WP2, 0x42b88b-0x42d637 and 0x4301b0 in ALIFE.EXE):
 * footprints, stamping, clearing and destroying buildings of the selected
 * plane, plus the small rock helpers the build tools use.
 *
 * A building of footprint n has its origin (the cell with tile set and
 * index) at (x, y) and covers x..x+n-1 by y-n+1..y; every other cell has
 * set 0 and dx/dy in its index bits, and on Hell/Heaven the sim cell's
 * +0xa/+0xb repeat the origin (docs/map.md).
 */
#ifndef GAME_STRUCTURE_H
#define GAME_STRUCTURE_H

#include <stdbool.h>
#include <stdint.h>

/* 0x42b88b: footprint of the structure covering (x, y) (its origin is
 * found through the sim cell's +0xa/+0xb, like the original), from the
 * tile set and index: 1 for most single-cell sets, the parameter tables'
 * size for sets 8, 13, 19, 30, ... */
int structure_size(int x, int y);

/* 0x445f2f: the upkeep of the structure whose origin is (x, y) (set given,
 * or 0 = the cell's own). (In WP4's range; ported here with the tables.) */
uint32_t structure_upkeep(int x, int y, unsigned set);

/* 0x4301b0 map_stamp_building: writes a size x size footprint with origin
 * (x, y): origin cell gets set/index, part cells set 0 and their offsets
 * (and, if `sim`, the origin in sim +0xa/+0xb), strips for the current
 * rotation; clears CELL_TINT on the origin; sets or clears CELL_ALT by the
 * view's zone-detail flag (see the original's rules in structure.c). */
void map_stamp_building(int x, int y, unsigned set, unsigned index, int size, bool sim);

/* 0x42bf5c: turns the size x size area at origin (x, y) back into plain
 * land (or, with flags bit 1 outside the Karma plane, random water). */
void structure_clear_area(int x, int y, int size, unsigned flags);

/* 0x42bb64: re-evaluates the road access (CELL_TINT "no road") of the
 * structures within 3 cells of (x, y) after a road was removed
 * (`added` = 0: tint those without access) or added (`added` = 1:
 * untint those that now have one). */
void structure_update_access(int x, int y, int added);

/* 0x42c09b: destroys the structure covering (x, y) on the current plane.
 * sound: bit 0 play a sound, bit 1 dust clouds (map objects), bit 2 no
 * fade. flags: bit 0 forced (no questions, water and roads too; set in the
 * editor), bit 2 no confirmation, bit 3 explosion sound. Returns false if
 * nothing was destroyed (out of the map, refused, or indestructible). */
bool structure_destroy(int x, int y, unsigned sound, unsigned flags);

/* 0x430ca0: the rock bonus of (x, y): 0x2a << (rocks in the 3x3 - 1), or 0.
 * (WP1's range; ported here for the build tools and world generation.) */
int rock_value(int x, int y);
/* 0x430a77: marks the rocks around a set-29 structure (CELL_OVERLAY). */
void rock_mark(int x, int y);
/* 0x430b43: unmarks rocks around (x, y) no other set-29 structure uses. */
void rock_unmark(int x, int y);

/* Flat grid access like the original's `cells[y * 100 + x]` without
 * bounds: (x, y) outside 0..99 address a neighbouring row; outside the
 * whole grid this returns 0 (the original would read other globals). */
uint32_t cell_get_flat(int x, int y);

#endif
