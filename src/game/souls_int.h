/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Private to the SOUL flow module (souls.c, zones.c): raw cell access and
 * the state the two files share.
 *
 * The original addresses the 100 x 100 grids as base + y * 400 + x * 4
 * (map cells) and base + y * 0x708 + x * 0x12 (sim cells) with byte or
 * signed-char coordinates and no range checks, so a coordinate of 255
 * (x - 1 at the left edge) reads a cell of the next rows. These helpers
 * reproduce that: the flat index y * 100 + x is used as is, and only an
 * index outside the grid (where the original reads neighbouring globals)
 * reads as 0 and ignores writes.
 */
#ifndef GAME_SOULS_INT_H
#define GAME_SOULS_INT_H

#include <stddef.h>
#include <stdint.h>

#include "game/world.h"

static inline int raw_index(int x, int y)
{
    int i = y * GS_MAP_W + x;
    return i >= 0 && i < GS_MAP_CELLS ? i : -1;
}

/* map cells of the selected plane */
static inline uint32_t mc(int x, int y)
{
    int i = raw_index(x, y);
    return i < 0 ? 0 : g_world.cells[i];
}
static inline unsigned mc_set(int x, int y) { return mc(x, y) >> 27; }
static inline unsigned mc_entry(int x, int y) { return (mc(x, y) >> 16) & 0x7f; }
/* bit 2 (CELL_OVERLAY): halves the capacity of ports, training centres */
static inline unsigned mc_bit2(int x, int y) { return mc(x, y) & 4u; }
/* 0x441a44 map_cell_test_shade with the original's unchecked indexing */
static inline unsigned mc_shade(int x, int y) { return mc(x, y) & 1u; }

/* sim cells of the current realm */
static inline SimCell *sc(int x, int y)
{
    int i = raw_index(x, y);
    return i < 0 ? NULL : &g_world.sim[i];
}
static inline uint32_t sc_u32(int x, int y, int off)
{
    SimCell *c = sc(x, y);
    return c ? sim_cell_u32(c, off) : 0;
}
static inline void sc_set_u32(int x, int y, int off, uint32_t v)
{
    SimCell *c = sc(x, y);
    if (c)
        sim_cell_set_u32(c, off, v);
}
static inline void sc_add_u32(int x, int y, int off, uint32_t v)
{
    SimCell *c = sc(x, y);
    if (c)
        sim_cell_set_u32(c, off, sim_cell_u32(c, off) + v);
}
static inline uint16_t sc_u16(int x, int y, int off)
{
    SimCell *c = sc(x, y);
    return c ? sim_cell_u16(c, off) : 0;
}
static inline void sc_set_u16(int x, int y, int off, uint16_t v)
{
    SimCell *c = sc(x, y);
    if (c)
        sim_cell_set_u16(c, off, v);
}
static inline uint8_t sc_u8(int x, int y, int off)
{
    SimCell *c = sc(x, y);
    return c ? c->b[off] : 0;
}
static inline void sc_set_u8(int x, int y, int off, uint8_t v)
{
    SimCell *c = sc(x, y);
    if (c)
        c->b[off] = v;
}
/* 0x43085d sim_cell_zone_kind, unchecked */
static inline unsigned sc_zone_kind(int x, int y) { return sc_u8(x, y, 1) & 0x0f; }

/* The realm the selected plane belongs to (sim_cells_current == Hell?) */
static inline int souls_in_hell(void) { return g_world.realm_plane == GS_PLANE_HELL; }

/* 0x45ebe2 result: x in the low byte, 0xff = nothing found */
#define AREA_NONE 0xff

/* Runtime cursors for the emulator tests (tools/emu/shims/souls.c):
 * next free group (0x65fe50), zone scan x/y (0x65fe48/4c), flow x/y
 * (0x65fe40/44). */
void souls_cursors(uint32_t out[5]);
void souls_flow_cursor(uint32_t out[2]); /* souls.c */
void souls_flow_reset(void);             /* souls.c */

/* zones.c */
unsigned souls_area_search(int x, int y, unsigned size, unsigned radius, int *ox, int *oy);

/* The order of the seven sins/virtues: unk_593240[rank] (0x593240), with
 * 0x593247 (a separate saved byte) as rank 7. */
static inline uint8_t souls_rank_kind(int rank)
{
    if (rank >= 0 && rank < 7)
        return g_state.unk_593240[rank];
    if (rank == 7)
        return g_state.unk_593247;
    return 0; /* 0x59323f: unsaved, never reached with a consistent order */
}

#endif
