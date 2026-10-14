/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Map object system (WP3, docs/game-map.md: 0x4ed550-0x4ef4fb; docs/map.md
 * "Object map and map objects").
 *
 * STUBS: WP3 owns these. Until mapobj.c lands they do nothing, so the
 * object map and CELL_OBJECT are not maintained for the callers below.
 * WP3 replaces them with declarations of the same name and signature.
 */
#ifndef GAME_MAPOBJ_H
#define GAME_MAPOBJ_H

/* 0x4ee5d8 map_object_mark_cells(&map_view, x, y, size, slot, set): adds
 * (set != 0) or removes map object `slot` over the size x size area at
 * (x, y) in the object map of the selected plane, maintaining CELL_OBJECT. */
static inline void mapobj_mark_cells(int x, int y, int size, int slot, int set)
{
    (void)x, (void)y, (void)size, (void)slot, (void)set;
}

/* 0x4ed550 map_object_redraw(&map_view, window, x, y, size, slot, a, b, c):
 * redraws the area (display only). */
static inline void mapobj_redraw(int x, int y, int size, int slot, int a, int b, int c)
{
    (void)x, (void)y, (void)size, (void)slot, (void)a, (void)b, (void)c;
}

/* 0x4ee402 (13 arguments): takes the first free map object slot (1..62,
 * plane byte 0) and fills it; returns the slot or -1. Pure data, so this
 * stub is a faithful port (added by WP1 for the construction animation of
 * 0x43c533). Arguments in the original's order. */
#include "game/world.h"
static inline int mapobj_spawn(int x, int y, int set, int frame, int dx, int dy, int delay, int size, int plane,
                               int fade, uint32_t set_mask, int priority, uint32_t flags)
{
    int i = 1;
    while (g_state.objects[i].set != 0 && i + 1 < GS_MAP_OBJECTS)
        i++;
    MapObjectRec *o = &g_state.objects[i];
    if (o->set != 0)
        return -1;
    o->x = (int8_t)x, o->y = (int8_t)y;
    o->set = (uint8_t)set;
    o->frame = (uint8_t)frame, o->first_frame = (uint8_t)frame;
    o->size = (int8_t)size;
    o->delay = (uint8_t)delay, o->delay_reset = (uint8_t)delay;
    o->dx = (int8_t)dx, o->dy = (int8_t)dy;
    o->plane = (uint8_t)plane;
    o->fade = (uint8_t)fade;
    o->set_mask = set_mask;
    o->priority = (uint8_t)priority;
    o->flags = flags;
    return i;
}

/* 0x4edf31(&map_view, x, y, slot, a, b, plane, c, d): adds a record to the
 * 25-entry table 0x64e500 (g_state.unk_64e500, 9 bytes: +0 plane, +1/+2
 * x/y, +3 c, +4 slot, +5 a, +6/+7 b, +8 d), dropping any other record at
 * (x, y); returns the entry or -1. Pure data, so a faithful port (added by
 * WP2 for game_reset_world). */
static inline int mapobj_place(int x, int y, int slot, int a, int b, int plane, int c, int d)
{
    int found = -1;
    for (int i = 0; i < 25; i++) {
        uint8_t *r = g_state.unk_64e500[i];
        if (r[0] == 0)
            found = i; /* the last free one */
        else if ((int8_t)r[1] == (int8_t)x && (int8_t)r[2] == (int8_t)y)
            r[0] = 0;
    }
    if (found < 0)
        return -1;
    uint8_t *r = g_state.unk_64e500[found];
    r[0] = (uint8_t)plane, r[1] = (uint8_t)x, r[2] = (uint8_t)y, r[4] = (uint8_t)slot, r[5] = (uint8_t)a;
    r[3] = (uint8_t)c, r[6] = (uint8_t)b;
    if (!(d & 2))
        r[7] = (uint8_t)b;
    r[8] = (uint8_t)d;
    return found;
}

#endif
