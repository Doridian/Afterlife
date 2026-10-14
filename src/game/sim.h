/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The simulation scheduler (WP1, docs/game-map.md: sim_update 0x44876b and
 * its helpers 0x447e8f-0x4486bc).
 *
 * sim_update runs once per main-loop iteration. Besides a few real-time
 * timers (port gates and Karma trains every 500 ms, the Bad Things in
 * progress every 240 ms, the end of Hell Freezes Over / My Blue Heaven
 * and of the Birds / Bats, the CD spin-up every 30 s) it advances the
 * game year as a state machine in g_state.sim_phase (saved):
 *
 *   0    year start                                             -> 20
 *   20   planet statistics, 1024 sectors per call (WP5); at the end the
 *        realm tables reset and EMBOs counted move to g_state.embos -> 30
 *   30   planet deaths become SOULs (WP5 -> realm_souls_from_planet), the
 *        planet year, Gate and siphon arrivals (WP1b); the rates paid or
 *        charged per arriving SOUL                               -> 10
 *   10   planet migration, one row per call (WP5)                 -> 15
 *   15   planet tech (WP5); the year's counters reset              -> 100
 *   100  Heaven zone scan, 50 buildings per call (WP1b)            -> 90
 *   90   Hell zone scan                                            -> 40
 *   40   Heaven SOUL flow (WP1b)                                   -> 50
 *   50   Hell SOUL flow; lost SOULs are charged, the SOUL total    -> 60
 *   60   vibes and happiness of both realms, the soul rate, Karma rail
 *        check                                                     -> 70
 *   70   Heaven realm year (game/economy.h)                        -> 80
 *   80   Hell realm year; worker overflow both ways, the score, the
 *        year-end passes of both realms, graphs, random Bad Things,
 *        bankruptcy, scenario events, year + 1                     -> 0
 *
 * Every phase but 0 and 90 waits until (speed * 5 - 5) * delay ms have
 * passed since the year started (delays 0xf0, 0x140, 0x50, 0xa0, 400,
 * 0x1e0, 0x230, 0x280, 0x2d0, 800 for phases 10, 15, 20, 30, 100, 40, 50,
 * 60, 70, 80), so speed 1 runs the year as fast as the machine allows.
 * Speed 0 (or the game paused) stops the year but not all timers.
 *
 * The clock is a hook (sim_set_clock, default tm_get_time) so tests can
 * drive the scheduler deterministically: with a clock that doesn't move,
 * the incremental steps that stop after 100 ms of real time (the realm
 * year) do their whole pass in one call.
 *
 * Window, sound and view side effects go through g_sim_hooks; every hook
 * may be NULL (skipped). Calls into other packages go through their
 * headers (planet.h, souls.h, karma.h, badthings.h, structure.h,
 * mapobj.h).
 */
#ifndef GAME_SIM_H
#define GAME_SIM_H

#include <stdbool.h>
#include <stdint.h>

#include "game/state.h"

/* Runtime state shared by the WP1 modules (not saved). */
typedef struct SimRuntime {
    /* 0x6390e8 (hi) / 0x6390ec (lo): both realms' SOULs (population plus
     * the SOUL pools), set at the end of phase 50, drives the soul rate */
    Bignum souls_total;
    uint32_t happiness_sum;   /* 0x63911c: zone happiness x size^2, summed in phase 60 */
    uint8_t milestone_shown;  /* 0x6391c8: a population milestone popped up this year */
    uint32_t groups_last;     /* 0x639220: SOUL groups created last year (souls_groups_created) */
    uint32_t step_count;      /* 0x4f3144: incremental steps since the last phase change */
    uint32_t update_count;    /* 0x4f3120: sim_update calls (wraps at 0xfffffff0) */
    uint8_t freeze_msg;       /* 0x65ee84: "Hell has thawed out!" is due */
    uint8_t blues_msg;        /* 0x65ee88: "...chased the Blues away!" is due */
    /* timeGetTime of the last run: */
    uint32_t t_year;          /* 0x65ee64: start of the year (phase delays) */
    uint32_t t_freeze;        /* 0x65ee68 */
    uint32_t t_blues;         /* 0x65ee6c */
    uint32_t t_badthings;     /* 0x65ee70 */
    uint32_t t_bats;          /* 0x65ee74 */
    uint32_t t_birds;         /* 0x65ee78 */
    uint32_t t_anim;          /* 0x65ee7c */
    uint32_t t_cd;            /* 0x65ee80 */
    uint8_t paused;           /* 0x5fa61c game_paused (the shell sets it) */
} SimRuntime;

extern SimRuntime g_sim;

typedef struct SimHooks {
    uint32_t (*clock)(void);                /* timeGetTime; NULL = tm_get_time */
    /* status bar: statusbar_draw 0x447243 (SOULs, pennies...), the date
     * 0x446904, 0x446dc5 */
    void (*statusbar)(void);
    void (*statusbar_date)(void);
    void (*statusbar_refresh)(void);
    /* ctl_message_box(-1, -1, title, text, buttons, 120000): returns the
     * button (1-based); NULL = 1 */
    int (*message)(const char *title, const char *text, const char *buttons);
    void (*news)(unsigned id);              /* news_popup_show(id, 0, 120000) 0x476582 */
    void (*sound)(uint32_t id, int prio);   /* snd_play */
    void (*sound_unload)(uint32_t id);      /* snd_unload */
    void (*music_fade)(int ms);             /* snd_fade_out_music 0x4753f2 */
    /* microview_refresh 0x46f414 and, if the Macromanager is open,
     * macro_refresh 0x46313b */
    void (*views_changed)(void);
    /* year end: graph_history_record 0x40d34b, 0x4154ab (graph summary),
     * graphview_draw 0x4159ba (also at phase 15), mapview_refresh 0x4640c9 */
    void (*graph_record)(void);
    void (*graph_summary)(void);
    void (*graphview_draw)(void);
    void (*mapview_refresh)(void);
    void (*advisor_update)(void);           /* 0x418a39 */
    void (*soulview_age)(void);             /* 0x47d6fb: Soulview's tracked SOULs get a year older */
    void (*cd_spin_up)(void);               /* 0x4486bd */
    /* both views' main window: the plane shown (the original's map view
     * plane, restored after the Karma rebuild); NULL = keep g_world */
    void (*select_plane)(int plane);
} SimHooks;

extern SimHooks g_sim_hooks;

/* The sim clock in ms: g_sim_hooks.clock, or tm_get_time (timeGetTime). */
uint32_t sim_clock_ms(void);

/* Resets g_sim and the economy's pass state (process start / new game)
 * and hooks the WP1 entry points into the other packages
 * (g_planet_hooks.souls) if still unset. */
void sim_reset(void);

/* 0x44876b: one scheduler step at `speed` (0 = stopped, 1 fastest .. 5). */
void sim_update(int speed);

/* The phase machine alone, without the real-time timers, honouring the
 * phase delays (for tests: runs until the phase changes or `max_calls`
 * sim_update calls happened). Returns the new phase. */
int sim_run_phase(int speed, int max_calls);

/* 0x448150: the soul rate (8.8 fixed point, 0x1a..0xa00 before the
 * clamp) from the SOUL total and the average happiness; sets
 * g_state.unk_638690 (average happiness) on the way. */
uint32_t sim_soul_rate(void);

/* 0x448594 scenario_event_step: one step of g_state.scenario_commands
 * ('D' + digit Bad Thing, 'F' flood, 'P' pause, 'W' + digit wait years). */
void scenario_event_step(void);

#endif
