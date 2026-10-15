/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The advisors, Jasper and Aria (WP7, ALIFE.EXE 0x417230-0x41f610): the
 * helper window with the two talking heads (gfx/akos_talk.h, AKOS
 * costumes 2003 / 2004, voices through sound slot 199), the issue
 * evaluation that picks what they talk about, and their advice texts.
 *
 * The shell (src/game/shell.c) creates the window at startup
 * (advisor_create, hidden), shows / hides it for the Window menu's
 * "Helpers" (MENU_HELPERS) and the control panel's button 0x1f, and
 * sim_update calls advisor_update() at the end of each year (and
 * views_refresh_all() does too).
 */
#ifndef AFTERLIFE_UI_ADVISORS_H
#define AFTERLIFE_UI_ADVISORS_H

#include <stdbool.h>
#include <stdint.h>

#include "ui/wm.h"

/* Message to the advisor window: stop talking, both heads to animation 6
 * (the main window sends it for '.'). */
#define ADVISOR_MSG_HUSH 0x1772

/* 0x417230 advisor_create: the window (child of the main window, hidden)
 * and its controls, the talkers. True if it exists. */
bool advisor_create(void);
/* Port-only: destroys the window and frees what create loaded. */
void advisor_destroy(void);
/* 0x638660 */
WmWindow *advisor_window(void);

/* 0x417b94 advisor_show(on): opens (and starts talking) or closes. */
void advisor_show(bool on);

/* 0x418a39 advisor_update: re-evaluates the issues (yearly; also from
 * views_refresh_all). Selects Hell and Heaven in turn (views_select_plane)
 * and, unlike the original, which leaves Heaven selected, restores the
 * plane selected before. Does nothing unless the window runs advisor_proc. */
void advisor_update(void);

/* Control ids: 1000 the close button, then the five issue buttons (most
 * urgent first). */
#define ADVISOR_ID_CLOSE 1000
#define ADVISOR_ID_ISSUE 1001 /* .. 1005 */

/* 0x41751b: the window procedure. advisor_show installs it. */
intptr_t advisor_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b);
/* 0x41f2c9: the procedure the tutorial (WP8, 0x425e3b) installs instead:
 * only lip sync, idle animations and ADVISOR_MSG_HUSH. */
intptr_t advisor_tutorial_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b);

/* Part of 0x425e3b (the tutorial, WP8): hides the issue buttons, clears
 * the subtitle area, opens both heads and gives each its own subtitle
 * rectangle. The caller installs advisor_tutorial_proc. */
void advisor_tutorial_setup(void);

/* 0x4196ae: what issue button i (0..4) does: Jasper and Aria talk about
 * that issue (blocking in akos_talk_wait while the voice plays), or make a
 * random remark if the button has none. */
void advisor_pick(int i);

/* 0x426427: stops the current speech (the news pop-up calls it before it
 * plays its own sound) and 0x426449 allows speaking again. */
void advisor_silence(void);
void advisor_unsilence(void);

/* Port-only, for tests: button i's issue codes (0xdc = none). */
unsigned advisor_issue(int i, bool hell);

#endif
