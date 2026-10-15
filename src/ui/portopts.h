/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Port-only: the Port Options window (Global menu > Port Options...), built
 * from the game's own toolkit like its message box: the message box
 * background (bitmap 0x260) stretched to size, bitmap buttons (0x227) and
 * a horizontal scrollbar used as a slider.
 *
 *   Enhancements   [ On ]     a toggle button
 *   UI scale       Auto (2x)  the slider: Auto, 1x, 2x, 3x, 4x; only
 *   [<|====|>]                active with the enhancements on
 *   [  OK  ]  [Cancel]
 *
 * Every change applies at once (game/display.c relays the screen out
 * live); OK saves ALIFEPORT.INI, Cancel (or Esc) puts the old settings
 * back. Keys: Enter OK, Esc Cancel, E the toggle, Left / Right the slider.
 * See docs/enhancements.md.
 */
#ifndef AFTERLIFE_UI_PORTOPTS_H
#define AFTERLIFE_UI_PORTOPTS_H

#include <stdbool.h>

#include "ui/wm.h"

/* Control ids */
enum {
    PORTOPTS_ENHANCE = 3000, /* the toggle button */
    PORTOPTS_SCALE = 3001,   /* the slider (scrollbar track) */
    PORTOPTS_OK = 3002,
    PORTOPTS_CANCEL = 3003,
};

/* Slider positions: 0 = automatic, 1..4 = that scale. */
#define PORTOPTS_SCALE_MAX 4

/* Runs the window modally. Returns true if OK was chosen. */
bool portopts_run(void);

/* While it runs (tests): the window and a control by id, else NULL. */
WmWindow *portopts_window(void);
WmWindow *portopts_control(int id);
/* The slider's x (screen) for a position, for clicking it. */
int portopts_slider_x(int pos);

#endif
