/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Shared helpers of badthings.c and badthing_objects.c (WP4).
 */
#ifndef GAME_BADTHINGS_INT_H
#define GAME_BADTHINGS_INT_H

#include <stdint.h>
#include <stdlib.h>

#include "game/badthings.h"
#include "game/build.h"
#include "game/world.h"

/* Flat cell access like the original's cells[y * 100 + x] with signed byte
 * coordinates: out-of-row x wraps into the neighbouring row; outside the
 * grid reads 0 and writes go nowhere (SAFETY, always on: the original reads
 * and writes whatever memory is there, which can't be reproduced; no
 * differential test reaches it). */
static inline uint32_t *bt_cellp(int x, int y)
{
    static uint32_t dummy;
    int i = y * GS_MAP_W + x;
    if (i < 0 || i >= GS_MAP_CELLS || !g_world.cells) {
        dummy = 0;
        return &dummy;
    }
    return &g_world.cells[i];
}
static inline uint32_t bt_cell(int x, int y) { return *bt_cellp(x, y); }
static inline unsigned bt_set(int x, int y) { return bt_cell(x, y) >> 27; }
static inline unsigned bt_index(int x, int y) { return (bt_cell(x, y) >> 16) & 0x7f; }
static inline SimCell *bt_sim(int x, int y)
{
    static SimCell dummy;
    int i = y * GS_MAP_W + x;
    if (i < 0 || i >= GS_MAP_CELLS || !g_world.sim) {
        SimCell z = {0};
        dummy = z;
        return &dummy;
    }
    return &g_world.sim[i];
}
static inline int bt_in_map(int x, int y) { return x >= 0 && y >= 0 && x < 100 && y < 100; }

/* abs(special site i's x + 1 - x) + abs(its y - 1 - y), and whether the
 * site exists (bit i of g_state.unk_638668) */
static inline int bt_site_dist(int i, int x, int y)
{
    return abs(g_build.special_pos[i][0] + 1 - x) + abs(g_build.special_pos[i][1] - 1 - y);
}
static inline int bt_site(int i) { return (g_state.unk_638668 >> i) & 1; }

/* the hooks, NULL-safe */
void bt_sound(uint32_t id, int prio);
void bt_music_play(uint32_t id);
void bt_music_fade(int ms);
void bt_select_plane(int plane);
/* a one-button message box with the title, text and button read from
 * ALIFE.EXE at the given addresses */
void bt_message(uint32_t title_va, uint32_t text_va, uint32_t button_va);

/* 0x445064: the sprite of heading d (1 east, 2 south, 3 west, 4 north in
 * map terms) for the view rotation: entry (d' * 2 - 2) */
unsigned bt_dir_frame(unsigned d);
/* 0x4438ed: the croupier (re)starts a throw: walks to a random column */
void bt_dice_start(void);
/* 0x443c58: Vinnie has left */
void bt_disco_end(void);

#endif
