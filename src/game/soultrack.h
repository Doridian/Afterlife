/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Soulview's tracked SOULs: the simulation side (0x47d67d-0x47f8fe without
 * the window; WP1b did the SOUL flow's calls, WP6b the rest).
 *
 * The player can follow up to ten SOULs in the Soulview
 * (g_state.block_4f33f8.rec[], 0x4f33f8, 0x26-byte records Rec26):
 *
 *   +0x00 state (TRACK_*)            +0x01 tracking: show the walker on the map
 *   +0x02/+0x03 the cell clicked     +0x04/+0x05 its building's origin
 *   +0x06/+0x07 walker position      +0x08 seed (name, face, life story)
 *   +0x0c temporary resident (else permanent; the Soulview's labels say
 *         the opposite, as in the original)
 *   +0x10 plane                      +0x11 zone kind    +0x12 planet belief 0..15
 *   +0x13 time left in the building (years x the building's steps)
 *   +0x17 SOUL group while walking   +0x1b category (souls.h; -1 reincarnate)
 *   +0x1c year of the last change    +0x20 road cells walked
 *   +0x22/+0x23 tile set / index of the building
 *   +0x24 map object under the walker (not saved)   +0x25 age bucket
 *
 * block_4f33f8.unk_664808 (0x664808) counts the records in use,
 * unk_66480c (0x66480c) the walking ones, `selected` (0x664810) is the
 * record the Soulview shows. A walking record rides in SOUL group +0x17 on
 * plane +0x10 as category +0x1b; the SOUL flow asks which categories of a
 * group are tracked and tells the trackers when their SOUL settles, moves
 * or gets lost. The window (src/ui/soulview.c) is told through
 * soultrack_hooks.
 */
#ifndef GAME_SOULTRACK_H
#define GAME_SOULTRACK_H

#include <stdbool.h>
#include <stdint.h>

#include "game/world.h"

enum {
    TRACK_NONE = 0,
    TRACK_RESIDENT = 1, /* in a zone building (sets 14/15) */
    TRACK_WALKING = 2,
    TRACK_REINCARNATED = 3, /* at a Karma station (set 17) */
    TRACK_LOST = 4,
    TRACK_GATE = 5,     /* left through a Gate (set 13) */
    TRACK_TRAINING = 6, /* training centre (set 8) */
    TRACK_OMNIBOLGE = 7 /* Omnibolge / Love Dome (set 5) */
};

#define SOULTRACK_MAX 10

/* What the Soulview window wants to hear; every hook may be NULL. */
typedef struct SoultrackHooks {
    /* 0x47f259(soulview, i): tracker i changed, redraw its status lines
     * (only if it is the one shown) */
    void (*changed)(unsigned i);
    /* soulview_refresh 0x47e6cc: redraw the whole window */
    void (*refresh)(void);
    /* soultrack_step's 100 ms pause after moving a walker (the original
     * busy-waits with sys_pump and game_frame_update) */
    void (*step_wait)(void);
} SoultrackHooks;

extern SoultrackHooks soultrack_hooks;

/* 0x630440 / 0x630444: tracked SOULs in the group being walked (per
 * category); souls.c clears them for each group. */
extern uint32_t soultrack_count;
extern uint32_t soultrack_cat[16];

static inline Rec26 *soultrack_rec(unsigned i) { return &g_state.block_4f33f8.rec[i % SOULTRACK_MAX]; }

/* 0x47e165: categories of group g that hold tracked SOULs (and counts
 * them into soultrack_count / soultrack_cat). */
uint16_t soultrack_group_mask(unsigned g);
/* 0x47de6c: group g reached the building at (x, y): trackers whose
 * category it takes settle there (state by building, 0x47eaf7/0x47ed71).
 * Returns the categories still tracked in the group. */
uint16_t soultrack_arrive(int x, int y, unsigned g);
/* 0x47e3be: group g passes road cell (x, y) (every second cell moves the
 * walker shown on the map). */
void soultrack_step(int x, int y, unsigned g);
/* 0x47e4be: SOULs of zone kind `kind` in group g are lost: such trackers
 * end ("Lost forever"). */
void soultrack_lost(int x, int y, unsigned g, unsigned kind);

/* 0x47eaf7: the tracker now sits in the structure covering (x, y) of the
 * selected plane (state by its tile set, kind, origin, walker position).
 * With `redraw` the walker is moved on the map if it is shown. */
void soultrack_place(Rec26 *t, int x, int y, bool redraw);
/* 0x47ed71: a permanent resident gets an age bucket and the time it has
 * left in the building, drawn from its realm's statistics (2 RNG draws);
 * anything else gets no time. */
void soultrack_pick_age(Rec26 *t);
/* 0x47e875: shows / hides the tracker's walker (the map object
 * g_build.obj_57e018 at +6/+7) on its plane. */
void soultrack_walker(Rec26 *t, bool show);
/* 0x47e653: frees the record (hides its walker, fixes the counts). */
void soultrack_remove(Rec26 *t);
/* 0x47e5bc without the window: frees every record, selects the first. */
void soultrack_reset(void);
/* 0x47e229: after map_rotate_cells, walkers shown in big buildings of the
 * selected plane move to the building's new front corner. */
void soultrack_rotated(void);
/* 0x47d6fb, sim_update phase 15: the trackers' year. Finished records
 * (training, Omnibolge, lost, reincarnated, through a Gate) are freed five
 * years after they ended; residents whose time is up (or whose building
 * emptied) leave on the roads as the SOUL flow's leavers do. */
void soultrack_age(void);
/* 0x47e7cc: starts following a resident of the building covering (x, y)
 * on `plane` (the selected plane) in record t: seed (0x47f7be), belief
 * (0x47ef9f) and age, 4 RNG draws in all. */
void soultrack_start(Rec26 *t, int plane, int x, int y);
/* 0x47f7be: the seed of a new tracker, and whether it is temporary
 * (one RNG draw). Returns the seed. */
uint32_t soultrack_seed(Rec26 *t);
/* 0x47ef9f: a planet belief for the new tracker, weighted by its zone's
 * residents (one RNG draw). */
uint8_t soultrack_pick_belief(const Rec26 *t);
/* 0x47ee8f: years the tracker has left in its building; 0 if it is not a
 * resident, -1 for temporary ones (shown as "Permanent resident"). */
int soultrack_years_left(const Rec26 *t);

#endif
