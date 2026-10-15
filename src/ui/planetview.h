/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Planetview window and the planet disaster window (WP5 in
 * docs/game-map.md; ALIFE.EXE 0x49be17, 0x49de17-0x4a2087,
 * 0x4a2efd-0x4a7c4a). The simulation behind it is game/planet.c.
 *
 * The window shows the Earth as two hemispheres (170 x 170 pixels each,
 * the far side mirrored) projected through a lookup table (resource 0x14d:
 * one u32 per pixel, low 16 bits the x and high 16 bits the y on the
 * 640 x 320 coastline bitmap g_state.planet.map, 0 = off the globe). Land
 * pixels take the colour of their sector from a 64 x 32 surface, the rest
 * a per-sector "water" colour. Which data the colours show is the layer
 * (PV_LAYER_*), picked with the buttons on the right, which also pick the
 * kind of divine influence the lower panel wields on the selected sector:
 * an artist (sin/virtue), a famous person (tenets) or a scientist
 * (technology). Without influence the panel describes the selected
 * sector (terrain, inhabitants, tech level, sin levels, tenets). The
 * selected sector is g_planet.sel_x / sel_y; it blinks every 500 ms
 * (planetview_blink_cursor, from the main loop) and the globe can spin
 * (planetview_rotate_tick, from game_frame_update).
 *
 * The disaster window ("Disaster Strikes the Planet!", bitmap 0x14c)
 * reports a disaster and plays its FLC (resource 0x1450 + kind).
 *
 * The game shell (WP7: main window, cursors, help balloons, the menu
 * bar's checks, status bar) isn't ported yet; what this module needs from
 * it comes through planetview_hooks and the setters below. Any hook may
 * be NULL.
 */
#ifndef AFTERLIFE_UI_PLANETVIEW_H
#define AFTERLIFE_UI_PLANETVIEW_H

#include <stdbool.h>
#include <stdint.h>

#include "ui/wm.h"

/* Layers (0x66c7d8). */
enum {
    PV_LAYER_TERRAIN = 0,
    PV_LAYER_POPULATION = 1,
    PV_LAYER_TECH = 2,
    PV_LAYER_SIN = 4,       /* the sin picked with the influence sliders */
    PV_LAYER_POP_SEL = 8,   /* population with the selection marked (never set by the game) */
    PV_LAYER_TENETS = 0x10, /* the tenet picked with the influence buttons */
};

/* Influence modes (0x66c7cc), from the pressed layer button. */
enum { PV_INFLUENCE_NONE = 0, PV_INFLUENCE_ARTIST = 1, PV_INFLUENCE_FAMOUS = 2, PV_INFLUENCE_SCIENTIST = 3 };

/* Control ids (WM_MSG_CTL_CLICK). */
enum {
    PV_ID_POPULATION = 1000,
    PV_ID_TECH = 0x3e9,
    PV_ID_SIN = 0x3ea,
    PV_ID_CLOSE = 0x3ed,
    PV_ID_TENETS = 0x3ee,
    PV_ID_UP = 0x3fc,
    PV_ID_DOWN = 0x3fd,
    PV_ID_LEFT = 0x3fe,
    PV_ID_RIGHT = 0x3ff,
    PV_ID_STOP = 0x400,
    PV_ID_AMOUNT = 0x409, /* the amount slider */
    PV_ID_WIELD = 0x40a,
    PV_ID_SIN_SLIDER = 0x410,   /* .. 0x416: one per sin/virtue pair */
    PV_ID_TENET_BUTTON = 0x424, /* .. 0x42d: AAAA NAAA OCRA HAHA OPRA HOHO SUSA RALF SUMA ALF */
};

/* Messages to the Planetview window. */
#define PV_MSG_REFRESH 0x83e8 /* b = full (planetview_refresh) */
#define PV_MSG_RESET 0x83e9   /* planet regenerated: redraw the coastline map */
#define PV_MSG_CLEAR 0x83ea   /* clear pending influence, redraw */
#define PV_MSG_SLIDER 0x83ee  /* from a slider: a = slider, b = moved by */

typedef struct PlanetviewHooks {
    /* 0x478a14 help_balloon_show(x, y, 120, text, 1, 1) (WP7). */
    void (*help_balloon)(int x, int y, int width, const char *text);
    /* The close button: wm_send(main_window, menu item 0x6465c0,
     * WM_MSG_MENU_COMMAND, 0x18, 0), the Window menu's Planetview toggle.
     * NULL: planetview_create() toggles directly. */
    void (*menu_command)(unsigned id);
    /* The window was hidden: the shell unpresses the Control Panel's
     * Planetview button (0x646368). */
    void (*hidden)(void);
    /* statusbar_draw(ctrlpanel_window) after Pennies were spent. */
    void (*status_redraw)(void);
    /* sys_pump, before the disaster animation starts. */
    void (*pump)(void);
    /* ui_busy_cursor (g_planet_hooks.busy_cursor ends up here). */
    void (*busy_cursor)(bool on);
    /* 0x47f8fe, the Soulview's SOUL name generator (out >= 60 bytes).
     * NULL: soulview_soul_name (ui/soulview.h). */
    void (*soul_name)(uint32_t seed, char *out);
} PlanetviewHooks;

extern PlanetviewHooks planetview_hooks;

/* The main window (parent of both windows; NULL: the root) and the game's
 * pause flag (game_paused 0x5fa61c, set while the disaster window is
 * dragged; NULL: none). planetview_create sets the parent too. */
void planetview_set_context(WmWindow *main_window, int *paused);
/* The menu bar's "Planet Tech Announcements" (0x6465e0) and "Planet
 * Disaster Announcements" (0x6465e4) checks. Both start on. */
void planetview_set_menu_checks(bool tech_announcements, bool disaster_announcements);

/* 0x4a2efd: the first call creates the window (hidden) under `parent`,
 * its controls and the disaster window; later calls toggle it shown /
 * hidden. Returns the window, or NULL. */
WmWindow *planetview_create(WmWindow *parent);
WmWindow *planetview_window(void); /* 0x66c7d4 */
/* Port-only: releases both windows and everything they loaded (the
 * game never does; tests). Frees g_state.planet.map like closing does. */
void planetview_shutdown(void);

/* 0x4a479f: blinks the selected sector every 500 ms (main loop). */
void planetview_blink_cursor(void);
/* 0x4a3593: spins the globe while a rotation button is down
 * (game_frame_update, every 125 ms tick). */
void planetview_rotate_tick(void);
/* 0x4a4943: recolours every sector and redraws the globe and the panel
 * (if shown and not minimized, or `full`). */
void planetview_refresh(bool full);
/* 0x49be17: turns the globe to sector (x, y) and selects it. */
void planetview_select_sector(int x, int y);

int planetview_layer(void);     /* 0x66c7d8 */
int planetview_influence(void); /* 0x66c7cc */

/* 0x49de17: shows disaster `kind` (0..9, 100 = the Flood) with `killed`
 * and `injured` (the planet code draws the injured figure), or creates the
 * window on the first call. Plays the animation (blocking). */
void planetview_disaster(unsigned kind, uint32_t killed, uint32_t injured);
WmWindow *planetview_disaster_window(void); /* 0x66c7d0 */
bool planetview_disaster_busy(void);        /* 0x50ace9: animation playing */

/* Fills the UI members of g_planet_hooks (game/planet.h): tech and
 * disaster announcements, the disaster window, the Flood warning, view
 * refresh / focus / reset and the busy cursor. */
void planetview_install_hooks(void);

#endif
