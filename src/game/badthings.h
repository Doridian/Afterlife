/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Bad Things and disasters (WP4, docs/game-map.md: 0x43dc3c-0x43e4e4,
 * 0x44180a-0x446241 except the WP0/WP1 functions in there, and the Bad
 * Things' cases of map_object_think 0x437184 with 0x436521 / 0x4366ab).
 *
 * The eight Bad Things
 * --------------------
 * bad_thing_start(k) unleashes one (the Global > Bad Things menu, the
 * scenario command 'D', economy_random_bad_things at the end of the year).
 * g_state.unk_5b2f61 holds one bit per Bad Thing in progress; a kind starts
 * only if no conflicting one runs (Heaven's 0x71, Hell's 0xb2 masks) and
 * enough map objects are free, and only while g_state.bad_things (the
 * "No Bad Things" menu toggle, saved) is set.
 *
 *   k  bit   name                  realm   how it works
 *   0  0x01  Birds of Paradise     Heaven  3-8 birds (set 0x14) fly in from an edge and
 *   1  0x02  Bats out of Hell      Hell    wander (set 0x15); now and then they soil the
 *                                          building below (CELL_OVERLAY). They leave when
 *                                          the realm's activity counter (realm +0x80fc,
 *                                          bumped per bird step) stays 0 for 5 s
 *                                          (sim_update's timer, game/sim.c).
 *   2  0x04  My Blue Heaven        Heaven  a random road (set 12) cell gets CELL_SHADE and
 *   3  0x08  Hell Freezes Over     Hell    the shade spreads (badthings_spread) for realm
 *                                          +0x80fa steps, waits +0x80fb years and recedes
 *                                          (badthings_recover); +0x80f9 counts the steps.
 *                                          Frozen roads lose their access (0x42bb64),
 *                                          receding ice destroys a few structures.
 *   4  0x10  Heaven Nose           Hell    a nose (set 0x19) sniffs up to 20 small
 *                                          buildings of Hell (sets 14/8/13), flies off the
 *                                          east edge and drops them as Heaven's equivalent
 *                                          on free land in Heaven.
 *   5  0x20  Hell in a Handbasket  Heaven  the same (set 0x18) from Heaven to Hell.
 *   6  0x40  Paradise Pair-o-Dice  Heaven  a croupier (set 0x1a, slot g_state.unk_63050d)
 *                                          throws two dice that trample a line across
 *                                          Heaven; the throws are a game of craps (point in
 *                                          unk_63026c, total in unk_5931c1, dice state in
 *                                          unk_59324c) that ends on 2/12 or a 7-out.
 *   7  0x80  Disco Inferno         Hell    Vinnie (set 0x1b, plane 1) dances across Hell
 *                                          crushing a 4x4 area per step; a mirror ball on
 *                                          plane 2 spins while he's there (unk_630484
 *                                          counts the dancers).
 *
 * The Handbasket's loot is g_state.unk_5bb0b0[20] {set, entry, size} with
 * the target cell unk_57e098/unk_57e09a and the count unk_5bb110; the
 * Nose's is unk_593200[20], unk_638653/unk_638654, unk_63050c. Every bit of
 * Bad Thing activity clears unk_5b2f5c, the counter of quiet years that
 * economy_random_bad_things rolls against.
 *
 * Special structures protect: each Bad Thing has one (g_build.special_pos,
 * bits of g_state.unk_638668) that repels it within some distance and
 * lights up (realm_mark_special_site).
 *
 * The bankruptcy ending (51 years in the red): four walkers
 * (g_state.unk_5f4cd0[4] {plane, x, y, dx, dy, steps left}) are set up,
 * walker 0 is stepped every 240 ms (badthings_doom_step): it burns a band
 * across the realms, leaving burning road tiles (set 2, sim +1 = 0xf5..0xfe)
 * that badthings_fire_step lets spread and burn out.
 *
 * The cheats: SAMNMAX (badthings_cheat, set 6 walkers on both realms) and
 * the build tool cheat's giant (set 3, game/build.c) are map objects whose
 * behaviour is here too.
 *
 * Side effects outside the game state go through g_badthings_hooks (each
 * may be NULL).
 */
#ifndef GAME_BADTHINGS_H
#define GAME_BADTHINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "game/state.h"

enum {
    BAD_BIRDS = 0, BAD_BATS = 1, BAD_BLUES = 2, BAD_FREEZE = 3,
    BAD_NOSE = 4, BAD_HANDBASKET = 5, BAD_DICE = 6, BAD_DISCO = 7,
    BAD_THINGS = 8
};

typedef struct BadThingsHooks {
    /* ctl_message_box(-1, -1, title, text, buttons, 120000) */
    int (*message)(const char *title, const char *text, const char *buttons);
    void (*news)(unsigned id);             /* news_popup_show(id, 0, 120000) 0x476582 */
    void (*sound)(uint32_t id, int prio);  /* snd_play 0x4751af */
    void (*sound_stop)(uint32_t id);       /* snd_stop 0x4750f0 */
    void (*sound_unload)(uint32_t id);     /* snd_unload 0x4754a1 */
    void (*music_play)(uint32_t id);       /* snd_music_play 0x4754d7 */
    void (*music_fade)(int ms);            /* snd_fade_out_music 0x4753f2 */
    /* the renderer half of map_view_select_plane (the main view follows
     * the plane the Bad Thing works on, as in the original) */
    void (*select_plane)(int plane);
    void (*window_update)(void);           /* wm_update(main window) */
    void (*repaint)(void);                 /* 0x5f7474 = 1: repaint the main view */
    /* the bankruptcy: if the helpers' window is visible, send the main
     * window menu command 0x1b (Helpers) */
    void (*helpers_off)(void);
} BadThingsHooks;

extern BadThingsHooks g_badthings_hooks;

/* Resets the runtime state (the giant's step target 0x4f3108/0x4f3100). */
void badthings_reset(void);

/* 0x43dc3c bad_thing_start: unleashes Bad Thing `which` (BAD_*). A negative
 * value (-kind, economy_random_bad_things) starts it silently: no "not
 * now" message when it can't start. Does nothing while
 * g_state.bad_things is 0. */
void bad_thing_start(int which);

/* 0x442122: Hell Freezes Over / My Blue Heaven spreads one step on the
 * current realm; false when it can't spread further */
bool badthings_spread(void);
/* 0x442287: it recedes one step; false when it is gone */
bool badthings_recover(void);

/* 0x444d23: the bankruptcy ending starts (pennies negative for 51 years) */
void badthings_bankruptcy(void);
/* 0x4442dd: one 240 ms step of the bankruptcy's walker (0x5f4cd0) */
void badthings_doom_step(void);
/* 0x443c9e: one 240 ms step of the fires it left, on both realms */
void badthings_fire_step(void);

/* 0x444ea5: the SAMNMAX cheat (five set-6 walkers) */
void badthings_cheat(void);

/* map_object_think 0x437184, the Bad Things' cases (sets 3, 6, 0x14,
 * 0x15, 0x18-0x1b): one behaviour step of object `slot` after it moved
 * (game/mapobj.c calls it from mapobj_think with the record as it was on
 * entry, before the construction fade block). */
void badthings_object_think(int slot, const MapObjectRec *before);

#endif
