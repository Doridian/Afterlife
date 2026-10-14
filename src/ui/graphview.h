/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Graphview (0x40b1c0-0x41722f, docs/game-map.md WP6a): the window
 * with the seven graphs (tenets, population, structures, history, labor,
 * belief, budget), and the yearly records they draw from.
 *
 * Data:
 *   - g_state.summary[0] / [1] (0x6399d0 Heaven / 0x639480 Hell): last
 *     year's copy of parts of each realm's statistics record, taken by
 *     graph_history_record() at the end of the year. Every graph except the
 *     History Graph reads only these. (Their ALSV block, 0x40c106 /
 *     0x40b1c0, is in game/savegame.c.)
 *   - g_state.rec_5c4eb0[30] (Rec72): the History Graph's ten time series
 *     at three time scales, appended by graph_history_update().
 *
 * sim_update (0x44876b, WP1) calls, at the end of phase 80:
 *     graph_history_record(); graph_history_update(); graphview_refresh();
 * and graphview_refresh() alone at the year start (phase 15).
 * game_reset_world (0x4535db, WP2) calls graph_history_record().
 *
 * The shell (WP7) creates the window once (graphview_create, hidden, like
 * game_main does at startup) and shows / hides it with the Window menu
 * (command 0x19) through wm_show(graphview_window(), ...). Context and
 * hooks: ui/views.h.
 */
#ifndef AFTERLIFE_UI_GRAPHVIEW_H
#define AFTERLIFE_UI_GRAPHVIEW_H

#include <stdbool.h>
#include <stdint.h>

#include "ui/wm.h"

/* Graph modes (0x651c78) and the mode buttons' ids (GV_ID_MODE + mode). */
enum {
    GRAPH_TENET = 0,
    GRAPH_POPULATION = 1, /* the initial mode */
    GRAPH_STRUCTURES = 2,
    GRAPH_HISTORY = 3,
    GRAPH_LABOR = 4,
    GRAPH_BELIEF = 5,
    GRAPH_BUDGET = 6,
    GRAPH_MODES = 7
};

/* Control ids of the Graphview's buttons. */
enum {
    GV_ID_SIN = 0x65, /* 0x65..0x6b: sin/virtue pair k toggle (population) */
    GV_ID_ALL_SINS = 0x6c,
    GV_ID_TENET = 0x6d, /* 0x6d..0x74: tenet toggles */
    GV_ID_ALL_TENETS = 0x75,
    GV_ID_HELL = 0x76,
    GV_ID_HEAVEN = 0x77,
    GV_ID_MODE = 0x78,   /* 0x78..0x7e: GV_ID_MODE + GRAPH_* */
    GV_ID_SCALE = 0x7f,  /* 0x7f..0x81: History Graph time scale 25 / 500 / 10000 years */
    GV_ID_SERIES = 0x82, /* 0x82..0x8b: History Graph series toggles */
    GV_ID_CLOSE = 0x8c
};

/* The History Graph's series (record GRAPH_SERIES_* * 3 + scale). */
enum {
    GRAPH_SERIES_HEAVEN_POP = 0,
    GRAPH_SERIES_HELL_POP = 1,
    GRAPH_SERIES_PENNIES = 2,
    GRAPH_SERIES_SOUL_RATE = 3,
    GRAPH_SERIES_PENNY_INFLOW = 4,
    GRAPH_SERIES_PENNY_OUTFLOW = 5,
    GRAPH_SERIES_SOUL_LOSSES = 6,
    GRAPH_SERIES_EFFICIENCY = 7,
    GRAPH_SERIES_FATE_STRUCTURES = 8,
    GRAPH_SERIES_SPIFFINESS = 9,
    GRAPH_SERIES = 10
};

/* 0x40d34b: copies this year's statistics of both realms into
 * g_state.summary[0] (Heaven) and [1] (Hell). */
void graph_history_record(void);

/* 0x4154ab: appends this year's value of every History Graph series
 * (realm populations with their SOUL pools, pennies, soul rate, penny
 * in/outflow, SOUL losses, efficiency, fate structures, spiffiness) to
 * the 25-year records, and every 20 / 400 years the average of the last
 * 20 points to the next scale. Reads the summaries, so it runs after
 * graph_history_record. */
void graph_history_update(void);

/* 0x414903: adds `value` (already >> shift) to series record `rec` (k * 3)
 * and the coarser scales, rescaling the series if `shift` grew. */
void graph_history_add(int rec, int32_t value, int shift);

/* 0x40d75e graphview_create: loads the window (bitmap 0x25b, centred in
 * the main window) and its 40 buttons, draws the population graph and
 * leaves it hidden. True if it exists. The original also resets the sin
 * tables 0x593240/0x57e060/0x593260 and both summaries here, at startup;
 * the port leaves the game state alone (a loaded game supplies them,
 * game_reset_world for a new one). */
bool graphview_create(void);
/* Port-only: destroys the window and frees what create loaded. */
void graphview_destroy(void);
/* 0x63865c */
WmWindow *graphview_window(void);
int graphview_mode(void);

/* 0x4159ba graphview_draw: applies the toggle `id` = `value` (0 for none),
 * then redraws the current graph into the window (if `draw`; the graph
 * area is cleared to "Nothing to graph." either way) and repaints it. */
void graphview_draw(unsigned id, int value, bool draw);
/* graphview_draw(0, 0, true), as sim_update calls it; no-op without the
 * window. */
void graphview_refresh(void);

#endif
