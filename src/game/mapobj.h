/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The map object system (WP3, docs/game-map.md: 0x4ed550-0x4ef4fb and
 * map_object_think 0x437184; docs/map.md "Object map and map objects").
 *
 * Map objects
 * -----------
 * g_state.objects[1..62] (0x64e5f0, saved; slot 0 is never used) are the
 * sprites that move over the map: Karma trains (set 16), boats (set 2),
 * construction and dust animations (sets 1, 14, 15), the Bad Things'
 * monsters, and a few parked ones (plane 99). A record holds the plane
 * (0 = free), the origin cell, the step (dx, dy) added every move, the
 * size (footprint; negative = hidden for one step), the tile set and the
 * sprite shown (frame) and to show next (first_frame), a step delay
 * (delay = the reload value, delay_reset = the countdown), the tile sets
 * drawn while it moves (set_mask), a per-kind state byte (fade), a
 * priority and `flags` (non-zero: keeps moving while the game is stopped).
 *
 * The object map (g_state.plane[p].objmap, one byte per cell) holds
 * `slot << 2 | count`: the object drawn at the cell and how many objects
 * cover the buildings there; CELL_OBJECT marks the covered cells. Objects
 * claim whole buildings: marking an object's area claims every structure
 * it touches (with its footprint), but only the area's two front edges
 * take the object's slot (unless a higher-priority object is there).
 *
 * Every 125 ms the shell (game_frame_update 0x407282) runs, per plane,
 * mapobj_animate_cells (the 25 animated cells of g_state.unk_64e500) and
 * mapobj_step: every due object is unmarked, moved by (dx, dy) (freed once
 * it leaves the map), re-marked and redrawn, and then "thinks"
 * (mapobj_think): its kind's behaviour picks the next sprite and step.
 * mapobj_frame does that whole block.
 *
 * Drawing is not done here: every redraw request goes to
 * g_mapobj_hooks.redraw (gfx/map_objects.h has the renderer half,
 * map_view_redraw_object, the port of 0x4ed550's drawing).
 */
#ifndef GAME_MAPOBJ_H
#define GAME_MAPOBJ_H

#include <stdbool.h>
#include <stdint.h>

/* A redraw request (the arguments of 0x4ed550 map_object_redraw plus the
 * focus state the original keeps in globals around the call). */
typedef struct MapObjRedraw {
    int plane;          /* the selected plane (g_world.plane) */
    int x, y, size;     /* the area: origin cell and footprint */
    int slot;           /* the object drawn there (its sprite height counts); < 0: the area
                           object -slot just left (cleared to the background first), 0: none */
    int set, frame;     /* > 0: a building of set/entry (1-based) about to appear there, whose
                           height counts too (and the background is cleared first) */
    bool now;           /* redraw at once (wm_redraw) instead of invalidating (wm_invalidate) */
    /* 0x64e4f0 / 0x64e4f4 / 0x64eba0 (map_focus) and MapView +0x3b..+0x43:
     * 0 = none; 1 = the area is the moving object's (deferred buildings in
     * it are left to the object); 2 = also only the object's set_mask sets
     * are drawn from the area's cell on (the object itself moving). */
    int focus;
    uint32_t set_mask;
} MapObjRedraw;

typedef struct MapObjHooks {
    void (*redraw)(const MapObjRedraw *r);  /* 0x4ed550 (display) */
    void (*sound)(uint32_t id, int prio);   /* snd_play */
    void (*window_update)(void);            /* wm_update(main window) after a step */
    void (*select_plane)(int plane);        /* the renderer half of 0x42b102 (mapobj_frame) */
} MapObjHooks;

extern MapObjHooks g_mapobj_hooks;

/* Resets the runtime state (the step guard). */
void mapobj_reset(void);

/* 0x4ee5d8 map_object_mark_cells(&map_view, x, y, size, slot, add): adds
 * (add != 0) or removes map object `slot` over the size x size area at
 * (x, y) in the object map of the selected plane, maintaining CELL_OBJECT.
 * Returns the slot left at the area's front edge: `slot` if it got (or,
 * removing, held) the cells, else the other object there. */
int mapobj_mark_cells(int x, int y, int size, int slot, int add);

/* 0x4ed550 map_object_redraw(&map_view, window, x, y, size, slot, set,
 * frame, now): asks the renderer to redraw the area (see MapObjRedraw). */
void mapobj_redraw(int x, int y, int size, int slot, int set, int frame, int now);

/* 0x4ee402 (13 arguments): takes the first free map object slot (1..62,
 * set byte 0) and fills it; returns the slot or -1. Arguments in the
 * original's order. */
int mapobj_spawn(int x, int y, int set, int frame, int dx, int dy, int delay, int size, int plane, int fade,
                 uint32_t set_mask, int priority, uint32_t flags);

/* 0x4ee3a9: the number of free map object slots (1..62). */
int mapobj_free_count(void);

/* 0x4edf31(&map_view, x, y, slot, a, b, plane, c, d): adds an animated cell
 * to the 25-entry table g_state.unk_64e500 (9 bytes: +0 plane, +1/+2 x/y,
 * +3 c (redraw size), +4 slot (first entry), +5 a (entries), +6/+7 b
 * (delay, countdown), +8 d (flags: 1 once, 4 back and forth, 8 going
 * back, 0x40 also while stopped, 0x80 clear CELL_TINT at the end)),
 * dropping any other record at (x, y); returns the entry or -1. */
int mapobj_place(int x, int y, int slot, int a, int b, int plane, int c, int d);

/* 0x4ee088(window, &map_view, plane, animate): steps the animated cells of
 * `plane` (the selected one). */
void mapobj_animate_cells(int plane, int animate);

/* 0x4eedc1 map_objects_step(window, &map_view, plane, animate): moves,
 * redraws and runs the behaviour of the objects of `plane` (the selected
 * one). With animate 0 only objects with flags != 0 move. */
void mapobj_step(int plane, int animate);

/* 0x437184 map_object_think(window, &map_view, slot): the behaviour of
 * object `slot` after it moved (current plane selected). The Bad Things'
 * kinds (sets 3, 6, 0x14, 0x15, 0x18-0x1b) are WP4's
 * (badthings_object_think, game/badthings.h). */
void mapobj_think(int slot);

/* The map object part of game_frame_update (0x407282, every 125 ms while
 * not paused): planes 1, 2 (only if animate) and 3, each selected (both
 * halves: world_select_plane and g_mapobj_hooks.select_plane), get
 * mapobj_animate_cells and mapobj_step. Restores the selected plane. */
void mapobj_frame(int animate);

/* The footprint of the structure whose origin is (x, y) on the selected
 * plane: what 0x4ee5d8 reads from the tile entry (+0x1a), derived here
 * from the map: 1 + the run of part cells (set 0) east of the origin
 * that point back at it. 1 for a set-0 cell. */
int mapobj_footprint(int x, int y);

#endif
