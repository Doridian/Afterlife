/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Control Panel and the Choose Tile Window (WP7, ALIFE.EXE: creation in
 * game_main 0x401000, ctrlpanel_proc 0x402996, tilechooser_proc 0x402789,
 * window placement 0x402695, the help texts 0x407a0e / 0x407bf8, and the
 * tile chooser's contents 0x455ed7-0x457b00).
 *
 * The panel (bitmap 100, at the main window's top left) holds the status
 * bar (ui/statusbar.h) and 37 buttons: the eight zone tools, gates, roads,
 * Karma stations and track, the tools that open the tile chooser (ports,
 * topias, training centres, siphons and banks, rocks, specials, Omnibolges
 * and Love Domes, Limbo structures), the bulldozer, the Microview and
 * Soulview pointers, zone blocks per plane, the grid, zoom, rotation and
 * the view toggles. Picking a tool calls build_select_tool (game/build.h);
 * the chooser then sets the item with build_tool_set_item.
 */
#ifndef AFTERLIFE_UI_CTRLPANEL_H
#define AFTERLIFE_UI_CTRLPANEL_H

#include <stdbool.h>
#include <stdint.h>

#include "ui/wm.h"

/* Creates the control panel (g_ctrlpanel_window, hidden) with its buttons
 * and tool cursors, and the tile chooser (g_tilechooser_window). */
bool ctrlpanel_create(void);
void ctrlpanel_destroy(void);

/* wm_send(ctrlpanel, button, WM_MSG_CTL_CLICK, id, 1): what the keyboard
 * shortcuts do. */
void ctrlpanel_click(int id);

/* 0x402695: stacks the views (Soulview, Microview, help, Mapview,
 * Graphview, Macromanager, Planetview) next to the control panel. */
void ctrlpanel_place_windows(WmWindow *const *windows, int n);

/* The chooser's current item (0x65ed94; -1 = none). */
int tilechooser_selection(void);

intptr_t ctrlpanel_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b);   /* 0x402996 */
intptr_t tilechooser_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b); /* 0x402789 */

#endif
