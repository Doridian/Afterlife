/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Soulview (0x478d50-0x47fca6, docs/game-map.md WP6b): the window that
 * follows one of up to ten tracked SOULs. The trackers themselves (what
 * the SOUL flow and the yearly pass do to them) are the simulation's:
 * game/soultrack.h.
 *
 * The window shows the selected tracker's name in the title, its status
 * (where it is, years left, planet belief) and either its picture (a face
 * composed from five bitmap layers picked by its seed, tinted by its skin
 * colour) or its life story (a text viewer). Buttons: picture / data,
 * tracking (the walker on the main map), previous / next tracker, centre
 * the main view on it, close.
 *
 * The shell (WP7) creates the window once (soulview_create, hidden, as
 * game_main does at startup), shows it with soulview_open() (the second
 * and later calls of 0x479e29), and when the Soulview tool (21 in the
 * original's tool numbering, 0x16) is clicked on the map calls
 * soulview_track_cell() (game_view_proc's message 0x83e8). After loading
 * a game (save_load_block26 0x47951c) and from views_refresh_all it calls
 * soulview_refresh(). infoviews_install_hooks() (ui/views.h) connects the
 * simulation's hooks: g_sim_hooks.soulview_age (sim_update phase 15),
 * soultrack_hooks, g_build_hooks.soulview_reset / soulview_rotated.
 * Context and hooks (main window, centring the main view, help): ui/views.h.
 */
#ifndef AFTERLIFE_UI_SOULVIEW_H
#define AFTERLIFE_UI_SOULVIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "game/state.h"
#include "ui/wm.h"

/* Control ids. */
enum {
    SOULVIEW_CLOSE = 0x3e9,
    SOULVIEW_PICTURE = 5000, /* sticky, pressed while the picture is shown */
    SOULVIEW_DATA = 0x1389,  /* sticky: the life story */
    SOULVIEW_TRACK = 0x138a, /* toggle: show the walker on the map */
    SOULVIEW_NEXT = 0x138b,
    SOULVIEW_PREV = 0x138c,
    SOULVIEW_CENTER = 0x138d
};

/* Window-local areas (0x6647d8, 0x6647e8; inclusive). */
#define SOULVIEW_STATUS_LEFT 0x11
#define SOULVIEW_STATUS_RIGHT 0xc8
#define SOULVIEW_STATUS_TOP 0xd4
#define SOULVIEW_STATUS_BOTTOM 0xf9
#define SOULVIEW_PIC_LEFT 0x0e
#define SOULVIEW_PIC_RIGHT 0xa9
#define SOULVIEW_PIC_TOP 0x10
#define SOULVIEW_PIC_BOTTOM 0xcb

#define SOULVIEW_NAME_LEN 60

/* 0x479e29, first call: the window (bitmap 0x25a, at the top right of the
 * main window's client area) and its seven buttons, hidden, showing the
 * picture. The original also resets the trackers here (0x47e5bc); the
 * port leaves the game state alone. True if it exists. */
bool soulview_create(void);
/* 0x479e29, later calls: shows and raises it. */
void soulview_open(void);
/* Port-only: destroys the window. */
void soulview_destroy(void);
WmWindow *soulview_window(void); /* 0x5b2f50 */

/* 0x47e6cc: redraws the status, the picture or the story of the selected
 * tracker. No-op without the window. */
void soulview_refresh(void);
/* 0x47f259: redraws tracker i's status lines and the title, if it is the
 * one shown. */
void soulview_status(unsigned i);
/* Message 0x83e8 (game_view_proc, the Soulview tool on map cell (x, y) of
 * the selected plane): starts following a resident of the zone building
 * there in the selected record, or, if there is none, frees that record.
 * Returns true if handled (false while the window is busy). */
bool soulview_track_cell(int x, int y);
/* 0x47e5bc: frees every tracker and shows the first, with the picture. */
void soulview_reset(void);
/* g_sim_hooks.soulview_age: soultrack_age (0x47d6fb). */
void soulview_age(void);

/* Port-only state access. */
typedef struct SoulviewState {
    bool picture; /* 0x664824: picture (else the story) */
    unsigned selected;
} SoulviewState;
SoulviewState soulview_state(void);

/* 0x47f8fe: the name of the SOUL with this seed, 3 or 4 syllables (the
 * syllable tables are in ALIFE.EXE), "No Soul" for 0. out >=
 * SOULVIEW_NAME_LEN bytes. No RNG. */
void soulview_soul_name(uint32_t seed, char *out);
/* 0x47adea / 0x47af6f: the tracker's life story into out (NUL-terminated,
 * truncated to size). Reads the selected plane's sim cells for temporary
 * trackers, as the original. Returns its length. No RNG. */
size_t soulview_describe(const Rec26 *t, char *out, size_t size);

#endif
