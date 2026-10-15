/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Port-only: the port window's live relayout (docs/rendering.md "Port
 * window").
 *
 * The game runs in a resizable window whose pixels, divided by the UI
 * scale, are the game's logical desktop: the framebuffer. When the window
 * changes size (the user drags it, fullscreen is toggled, the UI scale
 * changes), the framebuffer is reallocated, the window manager's root is
 * resized and the game lays its windows out again: the main window and its
 * menu bar and scrollbars follow the new size, the map view keeps its
 * centre, and the other windows are moved back inside the screen.
 *
 * The original never resizes: its main window is created once at the
 * resolution (di_set_main_window_size 0x4cd73a), and on WM_DISPLAYCHANGE it
 * re-creates that window and broadcasts WM_MSG_SCREEN_RESIZED, which no
 * window procedure of ALIFE.EXE handles.
 */
#ifndef AFTERLIFE_GAME_DISPLAY_H
#define AFTERLIFE_GAME_DISPLAY_H

#include <stdbool.h>

/* The game's part (installed by the shell). Both may be NULL. */
typedef struct {
    /* false while the screen can't change size (the title's animations
     * keep pointers into the main window's surface, the loading screen);
     * the relayout then waits. */
    bool (*can_relayout)(void);
    /* after the screen and the root window: lay the game's windows out
     * for new_w x new_h (the screen was old_w x old_h) */
    void (*relayout)(int old_w, int old_h, int new_w, int new_h);
} DisplayHooks;
extern DisplayHooks display_hooks;

/* The minimum time between two relayouts while the window is dragged. */
#define DISPLAY_RELAYOUT_MS 100

/* PLAT_EV_RESIZE: the window changed size; relayout soon. */
void display_request_relayout(void);
/* From sys_pump: runs a pending relayout, at most every
 * DISPLAY_RELAYOUT_MS. */
void display_idle(void);
/* Relayout now if the window wants another framebuffer size. False if it
 * had to wait (it stays pending); true when done or nothing to do. */
bool display_relayout(void);
/* Number of relayouts done (tests). */
unsigned display_relayout_count(void);

/* Applies the UI scale setting (0 = automatic, 1..4) and the
 * enhancements switch live: the scale in effect is 1 with the
 * enhancements off. Doesn't save. */
void display_apply_settings(bool enhancements, int ui_scale);

#endif
