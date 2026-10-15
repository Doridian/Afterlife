/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Shared between microview.c and microview_info.c. */
#ifndef AFTERLIFE_UI_MICROVIEW_INT_H
#define AFTERLIFE_UI_MICROVIEW_INT_H

#include "ui/wm.h"

/* The window and the plane of the cell shown (0x638ef1). */
WmWindow *microview_win(void);
int microview_cell_plane(void);

/* 0x46ffc1: the THINGS TO KNOW page for the structure with origin (x, y)
 * of tile set `set` / entry `index` on the selected plane (the caller has
 * selected the cell's plane and drawn the page's background). */
void microview_info_draw(int x, int y, unsigned set, unsigned index);

#endif
