/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Mapview internals that need the main map view (gfx/map_view.h), kept in
 * mapview_outline.c because game/world.h and gfx/tile_draw.h can't be
 * included together.
 */
#ifndef AFTERLIFE_UI_MAPVIEW_INT_H
#define AFTERLIFE_UI_MAPVIEW_INT_H

#include "gfx/surface.h"

/* The main view's rotation and size (the Mapview turns with it); +1, +1,
 * 100 x 100 without a map view. */
void mapview_view_geometry(int *dir_x, int *dir_y, int *w, int *h);

/* 0x465cb2: outlines in `dst` the part of the realm the main window shows,
 * for the Mapview showing cells x0.. / y0.. at `zoom`. */
void mapview_outline(GfxSurface *dst, int zoom, int x0, int y0);

#endif
