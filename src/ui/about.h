/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The about box and the credits (WP7, ALIFE.EXE 0x4acc30-0x4b0fff): the
 * "Afterlife v1.0" window of the File menu (MENU_ABOUT, bitmap 0x261,
 * close button 0x259 with id 100) with the scrolling credits drawn into
 * its right part (0x4addfb) and the team's pictures revealed tile by tile
 * in its left part, animated from game_frame_update (0x4ada7b) every
 * 100 ms while it is open.
 *
 * The credits script is the original's code: 0x4addfb is a long run of
 * calls credits_add(kind, text, x, y, arg, flags) with two random
 * choices. The port interprets those instructions straight from the
 * user's ALIFE.EXE (gfx/exe_res.h), so neither the script nor its text is
 * in the repo; without the EXE the credits area stays empty.
 */
#ifndef AFTERLIFE_UI_ABOUT_H
#define AFTERLIFE_UI_ABOUT_H

#include <stdbool.h>
#include <stdint.h>

#include "ui/wm.h"

#define ABOUT_ID_CLOSE 100

/* 0x4acc30: creates the window (child of the main window, hidden,
 * centred). True if it exists. */
bool about_create(void);
/* Port-only: destroys it and frees the credits picture. */
void about_destroy(void);
/* 0x5f25a0 */
WmWindow *about_window(void);
/* 0x4acdda */
intptr_t about_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b);

/* 0x57e0a0: the credits are running (the window is shown, not
 * minimized). */
bool about_credits_running(void);
/* 0x4ada7b: one step of the credits animation if 100 ms have passed
 * since the last (game_frame_update calls it while
 * about_credits_running()). */
void about_credits_tick(void);
/* Port-only: the step itself, without the clock (tests). */
void about_credits_step(void);
/* 0x4addfb: (re)starts the credits from the top. */
void about_credits_restart(void);
/* Port-only: credit entries on their way (0 when the script couldn't be
 * read). */
int about_credits_pending(void);

#endif
