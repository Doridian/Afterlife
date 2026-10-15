/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Microview (0x4678f4-0x4750ef, docs/game-map.md WP6b): the inspector
 * for one map cell's structure, with four pages chosen by buttons:
 *
 *   PICTURE   the structure drawn by the map renderer, three zoom levels
 *             (left click in it zooms in, right click out);
 *   DESCRIPTION  the tile's text from its TILE resource, in a text viewer;
 *   THINGS TO KNOW  a summary: how full, staff, vibes, road access, Bad
 *             Things, efficiency, balance ... (microview_info.c);
 *   THINGS YOU CAN CHANGE  per structure: Fate Structures (sets 14/15)
 *             have the population bars, the funding focus slider and the
 *             lock; other structures an upgrade (paid: destroy and rebuild
 *             one size up), banks (19) their loan, training centres (8)
 *             and Omnibolges / Love Domes (5) an acceptance slider,
 *             training centres an on/off switch.
 *
 * The window grows downwards by 8-pixel rows to fit the structure's
 * sprite; the line at the bottom names the structure and shows its main
 * figure. An "artist's rendering" button appears for structures that have
 * a news pop-up.
 *
 * The shell (WP7) creates the window once (microview_create, hidden; two
 * RNG draws, as game_main), opens it on a cell with microview_open() (the
 * Microview tool, game_view_proc's 0x46f613; two RNG draws), and calls
 * microview_refresh() from views_refresh_all, after building and at the
 * end of the year (g_sim_hooks.views_changed, see infoviews_install_hooks
 * in ui/views.h). Shift + Left / Right move the funding slider:
 * microview_key_slide(-2 / +2) (game_view_proc's message 0x177a).
 * Context and hooks: ui/views.h, microview_hooks below.
 */
#ifndef AFTERLIFE_UI_MICROVIEW_H
#define AFTERLIFE_UI_MICROVIEW_H

#include <stdbool.h>
#include <stdint.h>

#include "ui/wm.h"

/* Control ids (the original keeps its buttons in an array by id). */
enum {
    MICROVIEW_PICTURE = 400, /* the pages: sticky buttons */
    MICROVIEW_DESCRIPTION = 0x191,
    MICROVIEW_KNOW = 0x192,
    MICROVIEW_CHANGE = 0x193,
    MICROVIEW_ARTIST = 0x194,
    MICROVIEW_LOCK = 0x195,
    MICROVIEW_UPGRADE = 0x196,
    MICROVIEW_LOAN = 0x197,
    MICROVIEW_ACTIVE = 0x198, /* training centre on / off */
    MICROVIEW_CLOSE = 1000
};

typedef struct MicroviewHooks {
    /* news_popup_show(id, 0, 0) 0x476582 (WP7): the artist's rendering */
    void (*news)(unsigned id);
    /* 0x46fc14: the secret building's talking head (AKOS talker 4 at
     * (0x23, 0xe3), costume 0x7d2, direction 2, animation 3) */
    void (*secret_talk)(WmWindow *win);
    /* the loan and upgrade questions: ctl_message_box(-1, -1, NULL, text,
     * buttons, 0); NULL = that */
    int (*message)(const char *text, const char *buttons);
    void (*sound)(uint32_t id, int prio); /* snd_play (clicks); NULL = silent */
} MicroviewHooks;
extern MicroviewHooks microview_hooks;

/* 0x4678f4 microview_create: the window (bitmap 600, top right of the
 * main window's client area) with its buttons and the text viewer,
 * hidden. Draws two random numbers (rand_range(0, 100) twice) like the
 * original. True if it exists. */
bool microview_create(void);
/* Port-only: destroys the window and frees what create loaded. */
void microview_destroy(void);
WmWindow *microview_window(void); /* 0x5c6670 */

/* 0x46f613: inspects map cell (x, y) of the selected plane (the main
 * view's): two RNG draws, restores, raises, draws and shows the window.
 * Leaves that plane selected. */
void microview_open(int x, int y);
/* 0x46f414 microview_refresh: redraws the figures of the cell shown if
 * it still holds the same structure, else the whole window (unless
 * minimized). No-op before the first microview_open. */
void microview_refresh(void);
/* game_view_proc's message 0x177a: moves the funding slider by dx pixels
 * (THINGS YOU CAN CHANGE of a Fate Structure only). */
void microview_key_slide(int dx);

/* Port-only state access. */
typedef struct MicroviewState {
    int page;            /* 0x638f14: MICROVIEW_PICTURE .. MICROVIEW_CHANGE */
    int x, y;            /* 0x638ed8 / 0x638ed9: the structure's origin, -1 = none */
    int plane;           /* 0x638ef1 */
    unsigned set, index; /* 0x6611f4, 0x638fd0 */
    int zoom;            /* 0x6611f0: 0 zoomed in, 1 normal, 2 zoomed out */
    int rows;            /* 0x6611ec: 8-pixel rows the window is extended by */
} MicroviewState;
MicroviewState microview_state(void);
/* The knob of the funding / acceptance slider (window-local x of its
 * centre, as the original keeps it in 0x4f3220), -1 if none is shown. */
int microview_slider_x(void);

/* Port-only, for tests (tools/emu/test_wp6b.py): points the Microview at
 * the structure covering (x, y) of `plane` without drawing (and computes
 * the upgrade's price like the page would), and runs the window's
 * actions; their drawing is skipped while there is no window. */
enum {
    MICROVIEW_ACT_FUND = 1,  /* 0x46e3ef: funding focus slider at window x */
    MICROVIEW_ACT_TRAINING,  /* 0x46f056: training centre acceptance at x */
    MICROVIEW_ACT_OMNIBOLGE, /* 0x46f2a1: Omnibolge acceptance at x */
    MICROVIEW_ACT_LOCK,      /* 0x4699d5(x) */
    MICROVIEW_ACT_ACTIVE,    /* 0x46ef1f(x) */
    MICROVIEW_ACT_LOAN,      /* 0x4694df */
    MICROVIEW_ACT_UPGRADE    /* 0x46f7d8 */
};
void microview_target(int plane, int x, int y);
void microview_action(int act, int x);
uint32_t microview_upgrade_cost(void); /* 0x6611f8 */

#endif
