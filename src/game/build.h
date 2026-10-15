/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Build tools (WP2, 0x449c6e-0x4514a3 in ALIFE.EXE): placing zones,
 * buildings, roads, Karma rails, ports/gates and water, the cost checks,
 * footprint validation and the auto-tiling of roads, rails and water.
 *
 * Everything works on the selected plane of g_world / g_state like the
 * original (world.h). The UI (the map window procs, the tile chooser)
 * keeps the mouse handling and calls:
 *
 *   build_select_tool(tool)            when a control-panel tool is picked
 *   build_tool_set_item(set, index)    when the tile chooser picks an item
 *   build_apply_tool(tool, x0, y0, x1, y1)
 *                                      on mouse-up: zones drag a rectangle,
 *                                      roads/rails a line, the rest place
 *                                      at (x1, y1)
 *   build_tool_cost(...)               the price shown while dragging
 *   build_place(set, index, size, x, y, flags)
 *                                      one building (also used by the sim)
 *   structure_destroy (structure.h)    bulldozer / disasters
 *
 * Side effects outside the simulation state (sounds, redraws, dialogs) and
 * calls into packages that aren't ported yet go through g_build_hooks;
 * every hook may be NULL (then it is skipped, see the notes per hook).
 *
 * Tool ids (the control panel's, 0x450aba / 0x44da29):
 *   1-8 zones (zone kind tool-1; 8 is the shared "Generic" zone),
 *   9 ports/gates (set 13), 10 roads, 11 Karma stations (set 17),
 *   12 Karma rails (set 16 on plane 2), 13 set 7, 14 set 8, 15 set 9 or
 *   water, 16 set 6 (on roads) or rocks (set 10), 17-19 chooser items
 *   (special structures etc.), 20 bulldozer, 0xfa-0xfc a money cheat
 *   sequence.
 */
#ifndef GAME_BUILD_H
#define GAME_BUILD_H

#include <stdbool.h>
#include <stdint.h>

#include "game/state.h"

/* The building parameter tables (cost, size, capacity per tile set) are
 * g_tables in game/tables.h; build_init_defaults fills them. */

/* ---- Tool state (runtime, not saved) -------------------------------------- */

typedef struct BuildState {
    uint8_t editor;           /* 0x630514: scenario editor / free building (costs 0, zones place buildings) */
    uint8_t tool_set;         /* 0x65ed8c: tile set the tool places */
    uint8_t tool_index;       /* 0x65ed88: tile index */
    uint8_t tool_size;        /* 0x65ed84: footprint (zone brush) */
    int32_t tool_cost;        /* 0x65ed90: price per cell/building */
    int32_t cost_shown;       /* 0x57e100: price shown in the status bar */
    int8_t cheat_step;        /* 0x65eda4: 0xfa/0xfb money cheat sequence */
    uint32_t tool_sounds[4];  /* 0x65ee8c..98: preloaded sound ids of the tool */
    /* MapView +0x48 flags per plane (0x65ed98 Hell / 0x65eda0 middle /
     * 0x65ed9c Heaven; the view's copy 0x5bb168 is the selected plane's):
     * 0x40 = zone detail (no CELL_ALT stand-ins), 0x10, 0x80 grid. The sim
     * reads 0x40; the owner of the main view keeps MapPlanes in sync. */
    uint8_t view_flags[4];
    uint8_t status_dirty;     /* 0x5bb1d0: status bar needs a redraw */
    uint8_t karma_dirty;      /* 0x6301f0: Karma network changed */
    uint8_t map_dirty;        /* 0x57e104 */
    /* 0x5bb0f0: map position of each built special structure (set 4,
     * index 0..9), 0xa6 = none. Rebuilt by realm_tile_costs_update(1). */
    uint8_t special_pos[10][2];
    uint8_t road_all_ok;      /* 0x4f31b0 */
    uint8_t road_cont;        /* 0x4f31a8 */
    uint8_t editor_clear;     /* 0x65ede0: editor only, area destroyed before placing */
    uint8_t obj_5f7408;       /* 0x5f7408: map object slot allocated by game_reset_world after 0x5f7400[] */
    uint8_t obj_57e018;       /* 0x57e018: another one (set 0x23) */
} BuildState;

extern BuildState g_build;

/* ---- Hooks --------------------------------------------------------------------- */

enum {
    BUILD_CONFIRM_SPECIAL = 1, /* "Are you absotively sure..." destroying a set-4 special */
    BUILD_CONFIRM_OMNIBOLGE,   /* the same for set 5 (Hell's / Heaven's text by plane) */
};

typedef struct BuildHooks {
    /* UI */
    void (*sound)(uint32_t id, int channel); /* snd_play */
    /* map_object_redraw(&map_view, main_window, x, y, size, 0, 0, 0, 1):
     * the n x n area with origin (x, y) on the current plane changed */
    void (*redraw)(int x, int y, int size);
    /* the full form for destroy's crossfades: (set, frame) of the old
     * building, fade_in as in the original's last argument */
    void (*redraw_fade)(int x, int y, int size, int obj, int set, int frame, int fade_in);
    bool (*confirm)(int what);               /* ctl_message_box; NULL = yes */
    void (*refresh)(void);                   /* microview_refresh, macro_refresh, control panel buttons */
    void (*great_disturbance)(void);         /* 0x450aba: the cheat's message box */
    /* packages that aren't ported yet (WP2 calls souls.h, realm.h, planet.h
     * and graphview.h directly) */
    /* 0x6390e0: map object slot set-6 road ornaments use. Nothing in
     * ALIFE.EXE writes 0x6390e0, so it is always 0 (NULL = 0). */
    unsigned (*road_ornament_slot)(void);
    void (*soulview_reset)(void);            /* 0x47e5bc (WP6b), new world: SOUL trackers */
    void (*soulview_rotated)(void);          /* 0x47e229 (WP6b), per plane after map_rotate_cells */
    void (*views_refresh)(void);             /* views_refresh_all 0x408c4d (WP7), new world */
} BuildHooks;

extern BuildHooks g_build_hooks;

/* ---- Tools --------------------------------------------------------------------- */

/* 0x45546c game_init_defaults, the build part: editor off, cost shown 0
 * (the parameter tables are constants here). Does not reset the world. */
void build_init_defaults(void);

/* 0x450aba: picks a control-panel tool (sets tool_set/index/size/cost,
 * the tool's sounds). 0xfa..0xfc: the money cheat sequence. */
void build_select_tool(int tool);
/* What the tile chooser does for tools 9-19 (and 0x45129a in the editor):
 * the item to place. size 0 = from tile_index_footprint for sets 14/15. */
void build_tool_set_item(unsigned set, unsigned index, unsigned size);

/* 0x44da29, the commit (mouse-up) path of the map click dispatcher, minus
 * the drag highlighting: applies `tool` to the area (x0, y0)-(x1, y1) of the
 * current plane (zones: the rectangle; roads, rails: the line, as two legs;
 * everything else at (x1, y1), adjusted to the building's front corner like
 * a click). Returns true if anything was built. */
bool build_apply_tool(int tool, int x0, int y0, int x1, int y1);

/* The price of applying `tool` to the area right now (0x44da29 /
 * 0x44fc9b preview passes; 0x57e100). */
int32_t build_tool_cost(int tool, int x0, int y0, int x1, int y1);

/* ---- Placement ------------------------------------------------------------------ */

enum {
    BUILD_REDRAW = 1, /* param_8: redraw, refresh views, fail sound */
    BUILD_RAW = 2,    /* param_9: (x, y) is the origin and nothing is paid / bounds-checked */
};

/* 0x44b4fb: places one building of tile `set`/`index` and footprint `size`
 * at (x, y) on the current plane: rotation-dependent origin, cost and
 * bounds check, per-set rules (one per customer, Heaven/Hell variants,
 * ports max 50, banks max 20, Karma stations on three planes), footprint
 * check, stamping, sim-cell init, road-access tint, pay. Returns true on
 * success. */
bool build_place(unsigned set, unsigned index, int size, int x, int y, unsigned flags);

/* 0x44ac03: whether the size x size area with origin (x, y) is free to
 * build on (plain land not marked rock, or empty zone tiles). */
bool build_area_free(int x, int y, int size);

/* 0x44d34f mode 2: zones (x0, y0)-(x1, y1) with zone tool `ztool` (1-8),
 * clamped and widened by the brush. Returns true if any cell changed. */
bool build_zone_rect(int x0, int y0, int x1, int y1, int ztool);

/* 0x44fc9b: a road line from (x0, y0) to (x1, y1) (diagonal leg first,
 * then straight; cost 100 per new cell). commit false = only the price. */
int32_t build_road_line(int x0, int y0, int x1, int y1, bool commit);

/* 0x45011d: Karma rail line on plane 2 between two points of the current
 * realm (cost 500 per new rail cell, stations at the ends). */
bool build_rail_line(int x0, int y0, int x1, int y1);

/* ---- Auto-tiling ---------------------------------------------------------------- */

/* 0x43d3d8: picks the road (road = 1, set 12, by traffic in sim +9) or rail
 * (road = 0, set 16) piece of (x, y) from its neighbours; `recurse` also
 * updates the four neighbours; `keep` keeps the shape and only varies the
 * traffic level. (In WP1's range; ported here for the build tools.) */
void road_autotile(int x, int y, bool recurse, bool road, bool keep);
/* 0x449e0a: the water piece of (x, y) from its neighbours. */
void water_autotile(int x, int y);
/* 0x44a65a: water_autotile for every water cell of the current plane. */
void water_autotile_all(void);
/* 0x449c6e: whether (x, y) counts as water for auto-tiling (set 2 or the
 * water part of a set-9 structure). */
bool cell_is_water(int x, int y);

/* 0x4506b6: zone stand-ins on/off ("blocks" = CELL_ALT on zone cells and
 * view flag 0x40 off). cmd 0x17 Hell, 0x18 Heaven, 0x19 the Karma plane
 * (all non-terrain cells, and view flag 0x10 of both realms). Selects the
 * plane like the original (and leaves it selected). */
void build_zone_blocks(int cmd, bool blocks);

#endif
