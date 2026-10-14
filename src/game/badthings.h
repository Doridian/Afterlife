/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Bad Things and disasters (WP4, docs/game-map.md: 0x43dc3c-0x43e4e4,
 * 0x44180a-0x446241 except the WP0/WP1 functions in there, 0x444d23).
 *
 * STUBS: WP4 owns these. Until badthings.c lands they do nothing (the
 * spread/recover steps report "finished" so the timers in sim_update end
 * the effect). The timers themselves are part of sim_update (game/sim.c).
 * WP4 replaces each with a declaration of the same name and signature.
 */
#ifndef GAME_BADTHINGS_H
#define GAME_BADTHINGS_H

#include <stdbool.h>

/* 0x43dc3c bad_thing_start: starts Bad Thing `which` (scenario commands
 * pass 0..; random ones from 0x43e4e5 pass -kind) */
static inline void bad_thing_start(int which) { (void)which; }
/* 0x4442dd and 0x443c9e: the 240 ms step of the Bad Things in progress
 * (0x5f4cd0) */
static inline void badthings_step(void) {}
static inline void badthings_step2(void) {}
/* 0x442122: Hell Freezes Over / My Blue Heaven spreads one step on the
 * current realm; false when it can't spread further */
static inline bool badthings_spread(void) { return false; }
/* 0x442287: it recedes one step; false when it is gone */
static inline bool badthings_recover(void) { return false; }
/* 0x444d23: the bankruptcy ending (pennies negative for 51 years) */
static inline void badthings_bankruptcy(void) {}

#endif
