/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Mapview (0x463b70-0x46788f, docs/game-map.md WP6a): the overview
 * window with one coloured square per map cell of Heaven or Hell, in one of
 * eight data layers, with the main view's visible area outlined.
 *
 * Left click on the map zooms in (50 x 50 cells) around the cell and
 * scrolls the main view there (views_hooks.center_on_cell, 0x406be8);
 * right click zooms back out and scrolls too; a left drag elsewhere moves
 * the window. Buttons pick the layer and the realm.
 *
 * The shell (WP7) creates it once (mapview_create, hidden, as game_main
 * does), shows / hides it for the Window menu's command 0x1a, and calls
 * mapview_refresh() when the main view scrolled (map_view_paint 0x4076f5)
 * or from views_refresh_all (0x408c4d); sim_update calls it at the end of
 * the year. Context and hooks: ui/views.h.
 */
#ifndef AFTERLIFE_UI_MAPVIEW_H
#define AFTERLIFE_UI_MAPVIEW_H

#include <stdbool.h>

#include "ui/wm.h"

/* Layers: the ids of their buttons (0x6607b0 holds the current one). */
enum {
    MAPVIEW_CLOSE = 1099,
    MAPVIEW_TEMP_PERM = 0x44c, /* temporary / permanent SOULs */
    MAPVIEW_EFFICIENCY = 0x44d,
    MAPVIEW_VIBES = 0x44e,
    MAPVIEW_CAPACITY = 0x44f,
    MAPVIEW_ZONING = 0x450, /* the initial layer */
    MAPVIEW_TRAFFIC = 0x451,
    MAPVIEW_KARMA = 0x452,  /* Karma track and stations, all three planes */
    MAPVIEW_ROCKS = 0x453,  /* Ad Infinitum: rocks and siphons */
    MAPVIEW_HEAVEN = 0x454, /* realm buttons */
    MAPVIEW_HELL = 0x455
};

/* The map area in the window: 200 x 200 pixels at (14, 25). */
#define MAPVIEW_MAP_X 0x0e
#define MAPVIEW_MAP_Y 0x19
#define MAPVIEW_MAP_SIZE 200

/* 0x463b70 mapview_create: the window (bitmap 0x25c, at the main window's
 * client top-left) with its 11 buttons, hidden. True if it exists. */
bool mapview_create(void);
/* Port-only: destroys the window. */
void mapview_destroy(void);
/* 0x630270 */
WmWindow *mapview_window(void);

/* 0x4640c9: mapview_draw if the window is visible and not minimized. */
void mapview_refresh(void);
/* 0x464105: redraws the layer, legend and view outline and repaints. */
void mapview_draw(void);

/* 0x466c35: zoom in (to 1) or out by one step, centred on cell (cx, cy);
 * no redraw (the tutorial calls it, WP8). */
void mapview_zoom(int in, int cx, int cy);

/* Port-only state access (the original's globals 0x6607b0..0x6607bc). */
typedef struct MapviewState {
    int layer;   /* MAPVIEW_TEMP_PERM .. MAPVIEW_ROCKS */
    bool heaven; /* realm shown (0x660770) */
    int zoom;    /* 0 all 100 x 100 cells, 1 zoomed in to 50 x 50 */
    int x0, y0;  /* first cell shown */
} MapviewState;
MapviewState mapview_state(void);
/* Cell under window-local (x, y) of the map area (0x466eb0); false if
 * outside it. */
bool mapview_cell_at(int x, int y, int *cx, int *cy);
/* Window-local top-left of a cell's square (0x466d0b). */
void mapview_cell_pos(int cx, int cy, int *x, int *y);

#endif
