/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Internals shared by the shell's files (src/game/shell.c, shell_view.c,
 * src/ui/ctrlpanel.c, src/ui/statusbar.c). Not for other modules: they use
 * game/shell.h.
 */
#ifndef AFTERLIFE_GAME_SHELL_INT_H
#define AFTERLIFE_GAME_SHELL_INT_H

#include <stdbool.h>
#include <stdint.h>

#include "game/shell.h"
#include "gfx/map_view.h"

/* ---- the main map view (shell_view.c) ------------------------------------- */

extern MapView g_map_view;     /* 0x5bb120 map_view */
extern MapPlanes g_map_planes; /* the three planes (0x42b102's tables) */
extern int g_scroll_x;         /* 0x646660: view x offset (screen pixels) */
extern int g_scroll_y;         /* 0x63051c map_scroll_y */

/* Tool drag state (0x646658 / 0x64665c start, 0x6462d8 / 0x6462dc current
 * cell, 0x646548 plane, -1 = none). */
extern int g_drag_x0, g_drag_y0, g_drag_x, g_drag_y, g_drag_plane;

/* 0x408eb0 tile_load_all (port: binds tile sets 1-31 and builds the remap
 * tables from the palette) and the main view's MapView / planes over
 * g_state. False if the tile sets can't be loaded. */
bool shell_view_init(const uint8_t *rgbx);
void shell_view_shutdown(void);
/* Points the planes at g_state's grids again (after a load or new game)
 * and copies the rotation and the view flags. */
void shell_view_attach(void);
/* The scroll ranges of the main window's scrollbars for the zoom
 * (0x406d76 / 0x406eec tail) and their positions. */
void shell_view_update_scrollbars(void);

/* The view's copies of the game state (rotation, view flags, objects)
 * and its origin from the scroll, before drawing into the main window. */
void shell_view_sync_origin(void);

/* 0x4076f5 map_view_paint: repaints x, y, w, h of the main window's
 * surface (local coordinates) and refreshes the Mapview. */
void shell_view_paint(int x, int y, int w, int h);
/* The whole client area, then wm_redraw (the 0x8235 message). */
void shell_view_repaint(void);
/* Port-only: the main window's client area changed size (game/display.c);
 * keeps the view's centre, clamps, repaints the map into the surface. */
void shell_view_resized(int old_cw, int old_ch);
/* Repaints the cells mx..mx+w-1 x my..my+h-1 of the current plane (and
 * those drawn after it) with `extra` zoom-0 pixels of headroom, and puts
 * them on the screen (map_object_redraw 0x4ed550 for the build hooks). */
void shell_view_redraw_cells(int mx, int my, int w, int h, int extra);
/* 0x40682a mainview_scroll by (dx, dy) screen pixels. */
void shell_view_scroll(int dx, int dy);
void shell_view_zoom_in(void);  /* 0x406eec */
void shell_view_zoom_out(void); /* 0x406d76 */
/* The control panel's rotate buttons 0x21-0x24 (to that rotation) and
 * the '[' / ']' keys (dir -1 / +1). */
void shell_view_rotate_to(int button);
void shell_view_rotate(int dir);
/* 0x403f4c: presses the rotate button matching the rotation. */
void shell_view_rotation_buttons(void);
/* 0x406b65: the plane and cell under window-local (x, y); selects it. */
void shell_view_cell_at(int x, int y, int *plane, int *cx, int *cy);
/* 0x403deb: puts back a drag that shell_cancel_drag interrupted. */
void shell_view_restore_drag(void);
/* Hides the hover highlight / drag (0x403bfd). */
void shell_view_reset_drag(void);

/* 0x4510cd: the "SAMNMAX" cheat watches the typed characters. */
void shell_cheat_char(int ch);
/* 0x45129a: the scenario editor's keys. */
void shell_editor_key(int ch);
/* The menu id of the speed before the pause (0x650070). */
unsigned shell_last_speed(void);

/* game_view_proc 0x404023 (the main window's procedure in the game). */
intptr_t game_view_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b);
/* The menu-command half of it (WM_MSG_MENU_COMMAND), shell.c. */
void shell_menu_dispatch(WmWindow *item, unsigned id);


#endif
