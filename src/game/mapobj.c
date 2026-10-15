/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The map object system. See mapobj.h; addresses per function.
 */
#include "game/mapobj.h"

#include <stddef.h>

#include "game/badthings.h"
#include "game/karma.h"
#include "game/realm.h"
#include "game/world.h"

MapObjHooks g_mapobj_hooks;

/* 0x57d878: set while map_objects_step runs (spawning clears it). */
static bool g_stepping;
/* The focus state around a redraw (0x64e4f0.., MapView +0x3b..+0x43). */
static int g_focus;
static uint32_t g_focus_mask;

void mapobj_reset(void)
{
    g_stepping = false;
    g_focus = 0;
    g_focus_mask = 0;
}

/* ---- Raw access (flat indexes like the original, see karma.c) ------------- */

static int lin(int x, int y)
{
    int i = y * GS_MAP_W + x;
    return i >= 0 && i < GS_MAP_CELLS ? i : -1;
}
static uint32_t mc(int x, int y)
{
    int i = lin(x, y);
    return i < 0 ? 0 : g_world.cells[i];
}
static int i8(int v) { return (int8_t)v; }

/* ---- Redraw ------------------------------------------------------------------- */

/* 0x4ed550 */
void mapobj_redraw(int x, int y, int size, int slot, int set, int frame, int now)
{
    if (!g_mapobj_hooks.redraw)
        return;
    MapObjRedraw r = {g_world.plane, x, y, size, slot, set, frame, now != 0, g_focus, g_focus_mask};
    g_mapobj_hooks.redraw(&r);
}

/* ---- The object map ------------------------------------------------------------ */

int mapobj_footprint(int x, int y)
{
    if (mc(x, y) >> 27 == 0)
        return 1;
    int n = 1;
    while (n < 8 && x + n < GS_MAP_W) {
        uint32_t c = mc(x + n, y);
        if (c >> 27 != 0 || ((c >> 16) & 0x7f) != (uint32_t)n)
            break;
        n++;
    }
    return n;
}

/* 0x4ee5d8 */
int mapobj_mark_cells(int x, int y, int size, int slot, int add)
{
    int8_t seen[64][2];
    int nseen = 0;
    unsigned ret = (unsigned)slot & 0xff;
    x = i8(x), y = i8(y), size = i8(size);
    slot &= 0xff;
    /* the area's front edges in this view */
    int edge_x = g_state.view_dir_x < 1 ? x : i8(size + x - 1);
    int edge_y = g_state.view_dir_y < 1 ? i8(y - size + 1) : y;
    uint8_t *om = g_world.objmap;
    for (int cx = x; cx < size + x; cx = i8(cx + 1))
        for (int cy = y; y - size < cy; cy = i8(cy - 1)) {
            if (cx < 0 || cy < 0 || cx >= GS_MAP_W || cy >= GS_MAP_H)
                continue;
            uint32_t c = mc(cx, cy);
            int ox = cx, oy = cy;
            if ((c & 0xf8000000u) == 0) {
                ox = i8(cx - (int)((c >> 16) & 7));
                oy = i8((int)((c & 0x700000) >> 20) + cy);
            }
            bool dup = false;
            for (int k = 0; k < nseen && !dup; k++)
                dup = seen[k][0] == ox && seen[k][1] == oy;
            if (dup)
                continue;
            if (nseen < 64)
                seen[nseen][0] = (int8_t)ox, seen[nseen][1] = (int8_t)oy, nseen++;
            int fs = mapobj_footprint(ox, oy);
            for (int px = ox; px < ox + fs; px = i8(px + 1))
                for (int py = oy; oy - fs < py; py = i8(py - 1)) {
                    int i = lin(px, py);
                    if (i < 0)
                        continue;
                    uint8_t b = om[i];
                    unsigned cnt = b & 3, id = b >> 2;
                    if (!add) {
                        if (id == (unsigned)slot || id == 0) {
                            om[i] &= 3;
                            cnt = 0;
                        } else {
                            ret = id;
                            if (cnt != 0)
                                cnt++;
                        }
                        cnt = (int)cnt - 1 < 1 ? 0 : cnt - 1;
                        om[i] = (uint8_t)((om[i] & 0xfc) | cnt);
                        if (cnt == 0)
                            g_world.cells[i] &= ~0x40u;
                    } else {
                        unsigned r = ret;
                        if ((px == edge_x && py <= y && y - size < py) || (py == edge_y && x <= px && px < x + size)) {
                            if (cnt == 0 || id == (unsigned)slot || id == 0) {
                                if (cnt == 0)
                                    om[i] = (uint8_t)((om[i] & 3) | slot << 2);
                            } else if (g_state.objects[id].priority < g_state.objects[slot].priority) {
                                om[i] = (uint8_t)((om[i] & 3) | slot << 2);
                            } else {
                                r = id;
                            }
                        }
                        ret = r;
                        cnt++;
                        if (cnt > 2)
                            cnt = 3;
                        if (cnt < 2)
                            cnt = 1;
                        om[i] = (uint8_t)((om[i] & 0xfc) | cnt);
                        if (cnt == 1)
                            g_world.cells[i] |= 0x40u;
                    }
                }
        }
    return (int)(ret & 0xff);
}

/* ---- Slots ------------------------------------------------------------------------ */

/* 0x4ee402 */
int mapobj_spawn(int x, int y, int set, int frame, int dx, int dy, int delay, int size, int plane, int fade,
                 uint32_t set_mask, int priority, uint32_t flags)
{
    int i = 1;
    while (g_state.objects[i].set != 0 && i + 1 < GS_MAP_OBJECTS)
        i++;
    int r = -1;
    MapObjectRec *o = &g_state.objects[i];
    if (o->set == 0) {
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
        r = i;
    }
    g_stepping = false;
    return r;
}

/* 0x4ee3a9 */
int mapobj_free_count(void)
{
    int n = 0;
    for (int i = 1; i < GS_MAP_OBJECTS; i++)
        if (g_state.objects[i].set == 0)
            n++;
    return n;
}

/* 0x4edf31 */
int mapobj_place(int x, int y, int slot, int a, int b, int plane, int c, int d)
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

/* ---- Per-frame steps ------------------------------------------------------------- */

/* 0x4ee088 */
void mapobj_animate_cells(int plane, int animate)
{
    for (int i = 0; i < 25; i++) {
        uint8_t *r = g_state.unk_64e500[i];
        if (r[0] != (unsigned)plane || !(animate || (r[8] & 0x40)))
            continue;
        if (r[7] != 0) {
            r[7]--;
            continue;
        }
        unsigned x = r[1], y = r[2];
        if (x >= GS_MAP_W || y >= GS_MAP_H) {
            r[0] = 0;
            continue;
        }
        uint32_t *cell = cell_ptr((int)x, (int)y);
        uint32_t c = *cell;
        unsigned e = (c >> 16) & 0x7f;
        unsigned ne;
        if ((int)e < (int)(r[4] + r[5]) - 1) {
            if (!(r[8] & 8)) {
                ne = e + 1;
            } else if (r[4] < e) {
                ne = e - 1;
            } else if (!(r[8] & 1)) {
                r[8] &= 0xf7;
                ne = e + 1;
            } else {
                if (r[8] & 0x80)
                    *cell &= ~2u;
                ne = r[4];
                r[0] = 0;
            }
        } else if (!(r[8] & 4)) {
            ne = r[4];
            if (r[8] & 1) {
                if (r[8] & 0x80)
                    *cell &= ~2u;
                r[0] = 0;
            }
        } else {
            ne = e - 1;
            r[8] |= 8;
        }
        r[7] = r[6];
        cell_set_index((int)x, (int)y, ne & 0xff);
        mapobj_redraw((int)x, (int)y, (int8_t)r[3], 0, (int8_t)(c >> 27), (int8_t)(e + 1), 0);
    }
}

static bool due(const MapObjectRec *o, int plane, int animate)
{
    return o->plane == (unsigned)plane && o->delay_reset == 0 && (animate || o->flags != 0);
}

/* 0x4eedc1 */
void mapobj_step(int plane, int animate)
{
    if (g_stepping)
        return;
    g_stepping = true;
    /* take every due object off the map */
    for (int i = 0; i < GS_MAP_OBJECTS; i++) {
        MapObjectRec *o = &g_state.objects[i];
        if (!due(o, plane, animate))
            continue;
        o->frame = o->first_frame;
        if (o->size < 1) {
            o->size = (int8_t)-o->size; /* hidden for a step */
        } else {
            g_focus = 1;
            if ((int8_t)mapobj_mark_cells(o->x, o->y, o->size, i, 0) != 0)
                mapobj_redraw(o->x, o->y, o->size, -i, 0, 0, 0);
            g_focus = 0;
        }
    }
    /* move them; free those that left the map */
    for (int i = 0; i < GS_MAP_OBJECTS; i++) {
        MapObjectRec *o = &g_state.objects[i];
        if (!due(o, plane, animate))
            continue;
        o->x = (int8_t)(o->x + o->dx);
        o->y = (int8_t)(o->y + o->dy);
        if (o->size + o->x - 1 < 0 || GS_MAP_H <= o->y - o->size + 1 || GS_MAP_W <= o->x || o->y < 0) {
            o->set = 0;
            o->plane = 0;
        }
    }
    /* put them back and draw them */
    for (int i = 0; i < GS_MAP_OBJECTS; i++) {
        MapObjectRec *o = &g_state.objects[i];
        if (!due(o, plane, animate))
            continue;
        if ((mapobj_mark_cells(o->x, o->y, o->size, i, 1) & 0xff) == i) {
            g_focus = 2;
            g_focus_mask = o->set_mask;
            mapobj_redraw(o->x, o->y, o->size, i, 0, 0, 0);
        }
        g_focus = 0;
        g_focus_mask = 0;
    }
    /* behaviour */
    for (int i = 0; i < GS_MAP_OBJECTS; i++) {
        MapObjectRec *o = &g_state.objects[i];
        if (o->plane != (unsigned)plane || !(animate || o->flags != 0))
            continue;
        if (o->delay_reset == 0) {
            o->delay_reset = o->delay;
            mapobj_think(i);
        } else {
            o->delay_reset--;
        }
    }
    g_stepping = false;
    if (g_mapobj_hooks.window_update)
        g_mapobj_hooks.window_update();
}

/* An object leaves: its next move takes it off the map (x, y -> 127). */
static void retire(MapObjectRec *o, int x, int y)
{
    o->dx = (int8_t)(0x7f - x);
    o->dy = (int8_t)(0x7f - y);
    g_state.object_count--;
}

/* 0x437184 map_object_think, minus the Bad Things' kinds (WP4) */
void mapobj_think(int slot)
{
    slot &= 0xff;
    MapObjectRec *o = &g_state.objects[slot];
    int dx = o->dx, dy = o->dy;
    int x = o->x, y = o->y;
    int set = o->set;
    unsigned frame = o->frame;
    int size = o->size;
    int px = i8(x - dx), py = i8(y - dy);

    /* a building being replaced (construction under CELL_DEFER): the
     * object fades in over it two steps at a time, then becomes it */
    if (x >= 0 && y >= 0 && x < 100 && y < 100 && (set == 0x0e || set == 0x0f || set == 3) &&
        (mc(x, y) & 0x20)) {
        unsigned e = (mc(x, y) >> 16) & 0x7f;
        o->delay = 2, o->delay_reset = 2;
        if (o->fade == 9 && g_mapobj_hooks.sound) {
            bool hell = g_world.plane == GS_PLANE_HELL;
            if (e < frame) {
                unsigned d = frame - e > 2 ? 3 : frame - e;
                g_mapobj_hooks.sound((hell ? 0x10048u : 0x10045u) + d, 2);
            } else {
                unsigned d = e - frame > 2 ? 3 : e - frame;
                g_mapobj_hooks.sound((hell ? 0x1004eu : 0x1004bu) + d, 2);
            }
        }
        if (o->fade == 0) {
            retire(o, x, y);
            realm_building_replace(x, y, (unsigned)set, frame, (unsigned)(size & 0xff));
        }
        o->fade = o->fade < 2 ? 0 : (uint8_t)(o->fade - 2);
    }

    switch (set) {
    case 1: /* dust and smoke: plays entries 2..6 (or 0x1d..0x21), fade = repeats */
        if ((frame > 1 && frame < 6) || (frame > 0x1c && frame < 0x21)) {
            if (o->fade == 0) {
                retire(o, x, y);
            } else {
                o->delay = 1, o->delay_reset = 1;
                o->fade--;
                o->first_frame = (uint8_t)(frame + 1);
            }
        }
        break;
    case 2:
        karma_boat_step(slot);
        break;
    case 3:
    case 6:
    case 0x14:
    case 0x15:
    case 0x18:
    case 0x19:
    case 0x1a:
    case 0x1b:
        badthings_object_think(slot);
        break;
    case 0x0e:
    case 0x0f:
        /* the construction is over once its building is drawn again */
        if (!(mc(x, y) & 0x20))
            retire(o, x, y);
        break;
    case 0x10:
        karma_car_step(px, py, x, y, slot, (int)frame);
        break;
    default:
        break;
    }
}

static void select_both(int plane)
{
    world_select_plane(plane);
    if (g_mapobj_hooks.select_plane)
        g_mapobj_hooks.select_plane(plane);
}

/* game_frame_update 0x407282, the map object block */
void mapobj_frame(int animate)
{
    int saved = g_world.plane;
    select_both(GS_PLANE_HELL);
    mapobj_animate_cells(GS_PLANE_HELL, animate);
    mapobj_step(GS_PLANE_HELL, animate);
    select_both(GS_PLANE_MIDDLE);
    if (animate) {
        mapobj_animate_cells(GS_PLANE_MIDDLE, animate);
        mapobj_step(GS_PLANE_MIDDLE, animate);
    }
    select_both(GS_PLANE_HEAVEN);
    mapobj_animate_cells(GS_PLANE_HEAVEN, animate);
    mapobj_step(GS_PLANE_HEAVEN, animate);
    if (g_world.plane != saved && saved >= GS_PLANE_HELL && saved <= GS_PLANE_HEAVEN)
        select_both(saved);
}
