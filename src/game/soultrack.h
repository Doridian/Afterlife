/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Soulview's tracked SOULs: the simulation side (WP1b takes the non-UI
 * tracker logic of 0x47de6c-0x47f8fe, docs/game-map.md).
 *
 * The player can follow up to ten SOULs in the Soulview
 * (g_state.block_4f33f8.rec[], 0x4f33f8, 0x26-byte records). A record with
 * state (byte +0) 2 is walking: it rides in SOUL group +0x17 on plane +0x10
 * as category +0x1b; block_4f33f8.unk_66480c (0x66480c) counts those. The
 * SOUL flow asks which categories of a group are tracked and tells the
 * trackers when their SOUL settles, moves or gets lost. The window drawing
 * (0x47f259, 0x47e875 walker sprites, the 100 ms animation in 0x47e3be)
 * stays with the Soulview UI (WP6b) and is not called from here.
 */
#ifndef GAME_SOULTRACK_H
#define GAME_SOULTRACK_H

#include <stdint.h>

/* 0x630440 / 0x630444: tracked SOULs in the group being walked (per
 * category); souls.c clears them for each group. */
extern uint32_t soultrack_count;
extern uint32_t soultrack_cat[16];

/* 0x47e165: categories of group g that hold tracked SOULs (and counts
 * them into soultrack_count / soultrack_cat). */
uint16_t soultrack_group_mask(unsigned g);
/* 0x47de6c: group g reached the building at (x, y): trackers whose
 * category it takes settle there (state by building, 0x47eaf7/0x47ed71).
 * Returns the categories still tracked in the group. */
uint16_t soultrack_arrive(int x, int y, unsigned g);
/* 0x47e3be: group g passes road cell (x, y) (every second cell moves the
 * walker shown in the Soulview). */
void soultrack_step(int x, int y, unsigned g);
/* 0x47e4be: SOULs of zone kind `kind` in group g are lost: such trackers
 * end ("Lost forever"). */
void soultrack_lost(int x, int y, unsigned g, unsigned kind);

#endif
