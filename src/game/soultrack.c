/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Soulview trackers, simulation side: game/soultrack.h.
 */
#include "game/soultrack.h"

#include "game/realm.h"
#include "game/structure.h"
#include "game/util.h"
#include "game/world.h"

uint32_t soultrack_count;
uint32_t soultrack_cat[16];

static Block26 *blk(void) { return &g_state.block_4f33f8; }

/* The original indexes its per-category counters (0x630444) with the
 * signed category byte; -1 lands on the total (0x630440). */
static void cat_add(int8_t cat, uint32_t d)
{
    if (cat >= 0 && cat < 16)
        soultrack_cat[cat] += d;
    else if (cat == -1)
        soultrack_count += d;
}

static int walking(const Rec26 *t, unsigned g)
{
    return t->unk_00[0] == 2 && t->unk_17 == g && t->unk_10 == (uint8_t)g_world.plane;
}

/* 0x47e165 */
uint16_t soultrack_group_mask(unsigned g)
{
    uint16_t m = 0;
    if (blk()->unk_66480c == 0)
        return 0;
    for (int i = 0; i < 10; i++) {
        Rec26 *t = &blk()->rec[i];
        if (walking(t, g)) {
            m |= (uint16_t)(1u << (t->unk_1b & 31));
            soultrack_count++;
            cat_add((int8_t)t->unk_1b, 1);
        }
    }
    return m;
}

/* 0x466575: tile set of the structure covering (x, y) */
static unsigned origin_set(int x, int y)
{
    unsigned s = cell_tileset((uint8_t)x, (uint8_t)y);
    if (s == 0) {
        SimCell *c = sim_cell((uint8_t)x, (uint8_t)y);
        s = cell_tileset(c->b[10], c->b[11]);
    }
    return s;
}

/* 0x47eaf7 without the walker sprite (0x47e875): the tracker now sits in
 * the structure at (x, y). */
static void place(Rec26 *t, int x, int y)
{
    int ox = (uint8_t)x, oy = (uint8_t)y; /* 0x46666d: origin via sim +10/+11 */
    if (cell_tileset((uint8_t)x, (uint8_t)y) == 0) {
        SimCell *c = sim_cell((uint8_t)x, (uint8_t)y);
        ox = c->b[10];
        oy = c->b[11];
    }
    t->unk_00[4] = (uint8_t)ox;
    t->unk_00[5] = (uint8_t)oy;
    t->unk_22 = (uint8_t)cell_tileset(ox, oy);
    t->unk_23 = (uint8_t)cell_index(ox, oy);
    switch (t->unk_22) {
    case 5:
        t->unk_00[0] = 7;
        break; /* disappearing into an Omnibolge / Love Domes */
    case 8:
        t->unk_00[0] = 6;
        break; /* training */
    case 13:
        t->unk_00[0] = 5;
        break; /* exited through a Gate */
    case 14:
    case 15:
        t->unk_00[0] = 1;
        break; /* resident */
    case 17:
        t->unk_00[0] = 3;
        break; /* reincarnated */
    default:
        t->unk_00[0] = 2;
        break;
    }
    t->unk_11 = (uint8_t)sim_zone_kind(ox, oy);
    t->unk_00[2] = (uint8_t)x;
    t->unk_00[3] = (uint8_t)y;
    int8_t n = (int8_t)structure_size(ox, oy);
    if (g_state.view_dir_x < 1) {
        t->unk_00[6] = t->unk_00[4];
        t->unk_00[7] = g_state.view_dir_y < 1 ? (uint8_t)(t->unk_00[5] - (n - 1)) : t->unk_00[5];
    } else {
        t->unk_00[6] = (uint8_t)(t->unk_00[4] + n - 1);
        t->unk_00[7] = g_state.view_dir_y < 1 ? (uint8_t)(t->unk_00[5] - (n - 1)) : t->unk_00[5];
    }
}

/* 0x47ed71: a resident tracker gets an age bucket (unk_25) and the years
 * into it (unk_13), drawn from the residents of its zone kind/belief. */
static void pick_age(Rec26 *t)
{
    if (t->unk_00[0] == 1 && t->unk_0c == 0) {
        uint8_t list[32];
        unsigned n = 0;
        const PlaneStatHist *h = &realm_cur()->hist_a[(t->unk_11 * 16 + t->unk_12) % 112];
        for (unsigned b = 2; b < 32; b++)
            if (h->v[b] != 0)
                list[n++] = (uint8_t)b;
        if (n == 0) {
            t->unk_13 = 0;
        } else {
            t->unk_25 = list[rand_range(0, (int)n - 1)];
            t->unk_13 = (uint32_t)(t->unk_25 - 2) * 0x421;
            t->unk_13 += (uint32_t)(rand_range(1, 0x421) >> 1);
        }
    } else {
        t->unk_13 = 0;
    }
}

/* 0x47de6c */
uint16_t soultrack_arrive(int x, int y, unsigned g)
{
    uint16_t m = 0;
    if (blk()->unk_66480c == 0)
        return 0;
    for (int i = 0; i < 10; i++) {
        Rec26 *t = &blk()->rec[i];
        if (!walking(t, g))
            continue;
        unsigned s = origin_set(x, y);
        int8_t cat = (int8_t)t->unk_1b;
        int done = 0;
        if (s == 5) {
            done = 1;
        } else if (cat < 14) {
            if (s == 14 || s == 15)
                done = (unsigned)(cat & 7) == sim_zone_kind((uint8_t)x, (uint8_t)y);
            else if (s == 8)
                done = cat > 6;
        } else if (cat == 15) {
            done = s == 13;
        } else {
            done = s == 17;
        }
        if (!done) {
            m |= (uint16_t)(1u << (t->unk_1b & 31));
            continue;
        }
        t->unk_17 = 0;
        t->unk_20 = 0;
        t->unk_1c = g_state.year;
        blk()->unk_66480c--;
        soultrack_count--;
        cat_add(cat, (uint32_t)-1);
        place(t, x, y);
        pick_age(t);
        if (t->unk_0c != 0) {
            int special = t->unk_23 > 0x5a;
            realm_perm_sub(t->unk_00[4], t->unk_00[5], 1, special, t->unk_11);
            realm_temp_add(t->unk_00[4], t->unk_00[5], 1, special, t->unk_11);
        }
        /* 0x47f259 redraws the Soulview here (WP6b) */
        return m;
    }
    return m;
}

/* 0x47e3be */
void soultrack_step(int x, int y, unsigned g)
{
    if (blk()->unk_66480c == 0)
        return;
    for (int i = 0; i < 10; i++) {
        Rec26 *t = &blk()->rec[i];
        if (walking(t, g)) {
            t->unk_20++;
            /* the original also erases/redraws the walker and waits 100 ms */
            if ((t->unk_20 & 1) && t->unk_00[1] != 0) {
                t->unk_00[6] = (uint8_t)x;
                t->unk_00[7] = (uint8_t)y;
            }
        }
    }
}

/* 0x47e4be */
void soultrack_lost(int x, int y, unsigned g, unsigned kind)
{
    (void)x, (void)y;
    if (blk()->unk_66480c == 0)
        return;
    for (int i = 0; i < 10; i++) {
        Rec26 *t = &blk()->rec[i];
        if (walking(t, g) && t->unk_11 == (uint8_t)kind) {
            blk()->unk_66480c--;
            t->unk_00[0] = 4; /* lost forever */
            t->unk_1c = g_state.year;
            t->unk_13 = 0;
            t->unk_22 = t->unk_23 = 0;
            t->unk_00[2] = t->unk_00[3] = 0;
            t->unk_00[4] = t->unk_00[5] = 0;
            /* 0x47f259 redraws the Soulview here (WP6b) */
        }
    }
}
