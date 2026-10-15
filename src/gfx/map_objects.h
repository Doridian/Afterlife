/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The renderer half of the map object system: redrawing the area a map
 * object (or an animated cell) covers, map_object_redraw 0x4ed550. The
 * simulation half (moving the objects, the object map) is game/mapobj.c,
 * which asks for these redraws through g_mapobj_hooks.redraw; whoever owns
 * the main map view installs a hook that calls map_view_redraw_object (and
 * map_view_set_focus / MapView focus_x, focus_y and set_mask for the
 * request's focus) on its surface. See docs/map.md.
 */
#ifndef AFTERLIFE_GFX_MAP_OBJECTS_H
#define AFTERLIFE_GFX_MAP_OBJECTS_H

#include "gfx/map_view.h"

/* 0x4ed550 map_object_redraw(view, window, x, y, size, slot, set, frame,
 * now), the drawing: computes the screen box of the size x size area at
 * map cell (x, y) of the view's plane, tall enough for the tallest of the
 * sprite there (the building at (x, y), or object |slot|'s current frame
 * from v->objects, and, with set > 0, entry `frame` (1-based) of `set`),
 * clips it to `bounds` (the window's client area, inclusive) and the
 * surface, clears it to the background if slot <= 0 or set != 0 (colour
 * 0x13, or `backdrop` tiled), and redraws planes 1, 2 and 3 there in
 * that order, each with the highlight taken off and put back. The planes
 * behind the view's plane and the view's own are drawn with the view's
 * focus and set_mask as the caller left them; set_mask is then reset to
 * 0xffffffff for the planes in front. Keeps the view's plane. Returns the
 * screen rectangle redrawn (inclusive; empty if none). */
GfxRect map_view_redraw_object(GfxSurface *dst, MapView *v, MapPlanes *p, const GfxRect *bounds,
                               const GfxSurface *backdrop, int x, int y, int size, int slot, int set, int frame);

#endif
