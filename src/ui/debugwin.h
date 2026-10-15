/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The "Debug" window (ALIFE.EXE 0x457b01-0x45848f): a developers' tile
 * browser. A framed bitmap (610) with four arrow buttons and a close box:
 *
 *   the top pair (y 0x22)    tile set   - 1 / + 1   (1..30, wrapping)
 *   the bottom pair (y 0x4b) tile index - 1 / + 1   (clamped to the set's range)
 *
 * A left click steps by 1, a right click (released on the button) by 10.
 * Each step clears a 0x74 x 0x8b box at (3, 0x14) and draws the building
 * set:index+1 into it at zoom 0 (tile_draw_preview). The window opens
 * blank: nothing is drawn until the first step. Starting state: set 14
 * (zones), index 1, range 0..0x6f. Set 11 is skipped going up (a +1 from
 * 10 lands on 12; a -1 from 12 lands on 11 and is pushed back to 12, so
 * going down stops there, as in the original). The close box hides the
 * window and turns the scenario editor (g_build.editor) off; nothing
 * reopens it. The window can be dragged anywhere.
 *
 * Nothing in ALIFE.EXE creates or shows it: debugwin_create has no
 * callers and no pointer to it exists, and even if called it would leave
 * the window hidden (wm_show(..., true) is never called on it). Like
 * build_editor_mode, which only game_init_defaults and this close box
 * write, it belonged to a development build. The port opens it with the
 * port-only command-line option `--debug-window` (shell.c, once a game
 * is running; see docs/ui.md).
 */
#ifndef AFTERLIFE_UI_DEBUGWIN_H
#define AFTERLIFE_UI_DEBUGWIN_H

#include <stdbool.h>
#include <stdint.h>

#include "ui/wm.h"

/* Control ids (0x65..0x69) */
enum {
    DEBUGWIN_SET_UP = 0x65,     /* bitmap 0x89 at (0xd2, 0x22) */
    DEBUGWIN_SET_DOWN = 0x66,   /* bitmap 0x8a at (0x7f, 0x22) */
    DEBUGWIN_INDEX_UP = 0x67,   /* bitmap 0x89 at (0xd2, 0x4b) */
    DEBUGWIN_INDEX_DOWN = 0x68, /* bitmap 0x8a at (0x7f, 0x4b) */
    DEBUGWIN_CLOSE = 0x69,      /* bitmap 0x259 at (0, 0), the close box */
};

#define DEBUGWIN_FIRST_ID DEBUGWIN_SET_UP
#define DEBUGWIN_LAST_ID DEBUGWIN_CLOSE

/* The browser's state (0x65edd0..0x65eddc, signed bytes). */
typedef struct DebugwinState {
    int8_t index_min; /* 0x65edd0 */
    int8_t index_max; /* 0x65edd4 */
    int8_t set;       /* 0x65edd8: 1..30 */
    int8_t index;     /* 0x65eddc: 0-based, drawn as index + 1 */
} DebugwinState;

/* 0x457b01: creates the window, hidden, at (0xaa, 0x28) with its
 * buttons, under the main window (the original: under the root, see
 * debugwin.c). True if it exists (also when it already did). */
bool debugwin_create(void);
/* Port-only: destroys it and puts the state back to the start. */
void debugwin_destroy(void);
/* Port-only (--debug-window): creates the window if needed, shows and
 * raises it. True if it is up. */
bool debugwin_open(void);
/* Port-only: true if argv has --debug-window. */
bool debugwin_requested(int argc, char **argv);

WmWindow *debugwin_window(void);    /* 0x639244 */
WmWindow *debugwin_button(int id);  /* 0x63909c[id], id 0x65..0x69, else NULL */

/* 0x457d98: moves the set by `dset` (wrapping 1..30, taking that set's
 * index range) and the index by `dindex` (clamped), then redraws the
 * preview (when the window exists). */
void debugwin_step(int dset, int dindex);
DebugwinState debugwin_state(void);
void debugwin_set_state(DebugwinState s); /* tests */

/* 0x458200 */
intptr_t debugwin_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b);

#endif
