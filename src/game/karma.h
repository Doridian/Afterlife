/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Karma network: rails on plane 2, stations, trains (WP3, docs/game-map.md:
 * 0x42d638-0x4306cf, 0x43293d-0x436470).
 *
 * STUBS: WP3 owns these. Until karma.c lands they do nothing; the
 * scheduler (WP1 sim_update) calls them where the original does. WP3
 * replaces each with a declaration of the same name and signature.
 */
#ifndef GAME_KARMA_H
#define GAME_KARMA_H

/* 0x434c38: forgets the station/train tables before a rebuild */
static inline void karma_clear(void) {}
/* 0x433106: rebuilds the stations and trains from the rails (plane 2
 * selected) */
static inline void karma_rebuild(void) {}
/* 0x434dc4: moves the trains (every 500 ms, plane 2 selected) */
static inline void karma_trains_tick(void) {}
/* 0x42fddf: checks which stations the rails connect (sim phase 60) */
static inline void karma_rail_check(void) {}
/* 0x435f3d(&map_view): the realm's yearly Karma accounting (phase 80, per
 * realm) */
static inline void karma_year(void) {}
/* 0x434d2d: while 0x6301f4 is set, looks for a pending plane-2 object and
 * clears the flag once there is none (phase 80) */
static inline void karma_pending_scan(void) {}

#endif
