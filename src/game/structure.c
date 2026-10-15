/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Structures on the map: game/structure.h.
 */
#include "game/structure.h"

#include <stdlib.h>

#include "game/build.h"
#include "game/karma.h"
#include "game/mapobj.h"
#include "game/realm.h"
#include "game/souls.h"
#include "game/tables.h"
#include "game/util.h"
#include "game/world.h"

/* ---- helpers ------------------------------------------------------------------- */

uint32_t cell_get_flat(int x, int y)
{
    int i = y * GS_MAP_W + x;
    if (i < 0 || i >= GS_MAP_CELLS)
        return 0;
    return g_world.cells[i];
}

static uint32_t *cell_ptr_flat(int x, int y)
{
    int i = y * GS_MAP_W + x;
    if (i < 0 || i >= GS_MAP_CELLS)
        return NULL;
    return &g_world.cells[i];
}

static inline bool in_map(int x, int y) { return x >= 0 && x < GS_MAP_W && y >= 0 && y < GS_MAP_H; }

/* The (set, index) record of a building table, or an empty one past it
 * (the original reads whatever follows). */
static BuildingSpec spec_at(const BuildingSpec *t, size_t n, unsigned i)
{
    BuildingSpec none = {0, 0, 0, 0};
    return i < n ? t[i] : none;
}
#define SPEC(arr, i) spec_at(g_tables.arr, sizeof g_tables.arr / sizeof g_tables.arr[0], (i))

static void hook_redraw(int x, int y, int size)
{
    if (g_build_hooks.redraw)
        g_build_hooks.redraw(x, y, size);
}

static void hook_redraw_fade(int x, int y, int size, int obj, int set, int frame, int fade_in)
{
    if (g_build_hooks.redraw_fade)
        g_build_hooks.redraw_fade(x, y, size, obj, set, frame, fade_in);
    else
        hook_redraw(x, y, size);
}

static void hook_sound(uint32_t id, int ch)
{
    if (g_build_hooks.sound)
        g_build_hooks.sound(id, ch);
}

/* the origin of (x, y) through the sim cell, as 0x42b88b / 0x42c09b do */
static void sim_origin(int *x, int *y)
{
    if (cell_tileset(*x, *y) == 0) {
        const SimCell *c = sim_cell(*x, *y);
        *x = c->b[10];
        *y = c->b[11];
    }
}

static inline uint8_t view_flags_cur(void) { return g_build.view_flags[g_world.plane & 3]; }

/* ---- footprints ------------------------------------------------------------------ */

/* 0x42b88b */
int structure_size(int x, int y)
{
    sim_origin(&x, &y);
    uint32_t c = cell_get_flat(x, y);
    unsigned set = c >> 27, idx = (c >> 16) & 0x7f;
    switch (set) {
    case 1:
        return idx == 0xb ? 2 : 1;
    case 2:
        return idx < 0x34 ? 1 : 2;
    case 3:
        return 7;
    case 4:
    case 7:
        return 4;
    case 5:
        return (int)(idx >> 1) + 5;
    case 8:
        return SPEC(set8, idx).size;
    case 9:
        return (int)(idx >> 2) + 3;
    case 13:
        return SPEC(set13, idx >> 1).size;
    case 14:
    case 15:
        return tile_index_footprint(idx);
    case 17:
    case 18:
        return 3;
    case 19:
        return SPEC(set19, idx).size;
    case 30:
        return SPEC(set30, idx).size;
    default: /* 6, 12, 16 and the rest */
        return 1;
    }
}

/* 0x445f2f */
uint32_t structure_upkeep(int x, int y, unsigned set)
{
    uint32_t c = cell_get(x, y);
    unsigned idx = (c >> 16) & 0x7f;
    if (set == 0)
        set = c >> 27;
    switch (set) {
    case 7:
        return SPEC(set7, idx).capacity; /* 0x6304a4: +4 for set 7 */
    case 8:
        return SPEC(set8, idx).jobs;
    case 9:
        return SPEC(set9, idx).jobs;
    case 13:
        return SPEC(set13, idx >> 1).jobs;
    case 14:
    case 15:
        return idx < REALM_TILE_TYPES ? g_world.tile_info[idx].unk_08 : 0;
    case 16:
        return 0x78;
    case 17:
        return SPEC(set17, ((idx - 4) >> 1) & 0xff).jobs;
    case 19:
        return SPEC(set19, idx).jobs;
    case 29:
        return 0xf0;
    case 30:
        return (uint32_t)((int32_t)SPEC(set30, idx).jobs / 1000);
    default:
        return 0;
    }
}

/* 0x4301b0 map_stamp_building */
void map_stamp_building(int x, int y, unsigned set, unsigned index, int size, bool sim)
{
    if (size < 2) {
        cell_set_index(x, y, index);
        cell_set_tileset(x, y, set);
        cell_set_strip(x, y, 1);
    } else {
        for (int cx = x; cx < x + size; cx++)
            for (int cy = y; cy > y - size; cy--) {
                if (cx == x && cy == y) {
                    cell_set_index(cx, cy, index);
                } else {
                    cell_set_tileset(cx, cy, 0);
                    if (sim) {
                        SimCell *c = sim_cell(cx, cy);
                        c->b[10] = (uint8_t)x;
                        c->b[11] = (uint8_t)y;
                    }
                    cell_set_index(cx, cy, (unsigned)((y - cy) * 16 + (cx - x)));
                }
                cell_set_strip(cx, cy, 0);
            }
        cell_set_tileset(x, y, set);
        int dx = g_state.view_dir_x, dy = g_state.view_dir_y;
        int lx = dx < 1 ? x + size - 1 : x;
        int ly = dy < 1 ? y - size + 1 : y;
        /* front edges: `size` strips along x, then size-1 along y */
        unsigned strip = 1;
        int cx = lx;
        for (int i = 0; i < size; i++, cx += dx)
            cell_set_strip(cx, ly, strip++);
        cx -= dx;
        int cy = ly - dy;
        for (int i = 1; i < size; i++, cy -= dy)
            cell_set_strip(cx, cy, strip++);
    }
    cell_clear_flags(x, y, 2); /* CELL_TINT */
    uint8_t vf = view_flags_cur();
    bool alt = false, clear = false;
    if (!sim) {
        alt = !(vf & 0x40) && set != 1;
    } else if (!(vf & 0x40)) {
        if (set < 14 || set > 15)
            clear = true;
        else
            alt = true;
    }
    if (alt || clear)
        for (int cx = x; cx < x + size; cx++)
            for (int cy = y; cy > y - size; cy--) {
                if (alt)
                    cell_set_flags(cx, cy, 0x10);
                else
                    cell_clear_flags(cx, cy, 0x10);
            }
}

/* 0x42bf5c */
void structure_clear_area(int x, int y, int size, unsigned flags)
{
    g_build.map_dirty = 1;
    for (int cx = x; cx < x + size; cx++)
        for (int cy = y; cy > y - size; cy--) {
            if (!(flags & 2) || g_world.plane == GS_PLANE_MIDDLE) {
                cell_set_tileset(cx, cy, 1);
                cell_set_index(cx, cy, g_world.plane == GS_PLANE_MIDDLE ? 1 : 0);
            } else {
                cell_set_tileset(cx, cy, 2);
                cell_set_index(cx, cy, (unsigned)rand_range(0, 3));
            }
            cell_set_strip(cx, cy, 1);
            *cell_ptr(cx, cy) &= 0xffff7fc8u;
        }
}


/* 0x42bb64 */
void structure_update_access(int x, int y, int added)
{
    int x0 = x - 3 < 0 ? 0 : x - 3, x1 = x + 3 > 99 ? 99 : x + 3;
    int y0 = y - 3 < 0 ? 0 : y - 3, y1 = y + 3 > 99 ? 99 : y + 3;
    for (int cx = x0; cx <= x1; cx++)
        for (int cy = y0; cy <= y1; cy++) {
            int dist = abs(cx - x) + abs(cy - y);
            if (dist >= 4)
                continue;
            int ox = cx, oy = cy;
            unsigned set = cell_tileset(cx, cy);
            if (set == 0) {
                sim_origin(&ox, &oy);
                set = cell_get_flat(ox, oy) >> 27;
            }
            int radius;
            switch (set) {
            case 5:
            case 8:
            case 9:
            case 14:
            case 15:
            case 17:
                radius = 3;
                break;
            case 13:
                radius = 1;
                break;
            default:
                radius = 0;
            }
            if (!radius || !in_map(ox, oy))
                continue;
            if (!added) {
                int n = structure_size(ox, oy);
                int ax, ay;
                unsigned r = area_search(ox, oy, (unsigned)n, (unsigned)radius, &ax, &ay);
                if (r == 0xff && !(cell_get(ox, oy) & 2)) {
                    cell_set_flags(ox, oy, 2);
                    hook_redraw(ox, oy, n);
                }
            } else if (dist <= radius && (cell_get(ox, oy) & 2) &&
                       (set != 17 || (sim_cell(ox, oy)->b[9] & 0x31))) {
                cell_clear_flags(ox, oy, 2);
                hook_redraw(ox, oy, structure_size(ox, oy));
            }
        }
}

/* ---- rocks (0x430a77-0x430d52) ----------------------------------------------------- */

/* 0x430ca0 */
int rock_value(int x, int y)
{
    int n = 0;
    for (int cx = x - 1; cx <= x + 1; cx++)
        for (int cy = y - 1; cy <= y + 1; cy++)
            if (cell_get_flat(cx, cy) >> 27 == 10)
                n++;
    return n ? 0x2a << (n - 1) : 0;
}

/* 0x430a77 */
void rock_mark(int x, int y)
{
    for (int cx = x - 1; cx <= x + 1; cx++)
        for (int cy = y - 1; cy <= y + 1; cy++) {
            uint32_t *c = cell_ptr_flat(cx, cy);
            if (c && *c >> 27 == 10) {
                *c |= 4;
                hook_redraw(cx, cy, 1);
            }
        }
}

/* 0x430b43 */
void rock_unmark(int x, int y)
{
    for (int cx = x - 1; cx <= x + 1; cx++)
        for (int cy = y - 1; cy <= y + 1; cy++) {
            uint32_t *c = cell_ptr_flat(cx, cy);
            if (!c || *c >> 27 != 10)
                continue;
            bool free = true;
            for (int ax = cx - 1; ax <= cx + 1; ax++)
                for (int ay = cy - 1; ay <= cy + 1; ay++)
                    if (cell_get_flat(ax, ay) >> 27 == 29)
                        free = false;
            if (free) {
                *c &= ~4u;
                hook_redraw(cx, cy, 1);
            }
        }
}

/* ---- destroying ------------------------------------------------------------------------ */

/* 0x44add1: redraw the structure covering a cell (after a flag change) */
static void redraw_cell_structure(int x, int y)
{
    uint32_t c = cell_get(x, y);
    if (c >> 27 == 0) {
        int ox = (uint8_t)(x - (int)((c >> 16) & 7)), oy = (uint8_t)(y + (int)((c >> 20) & 7));
        hook_redraw(ox, oy, structure_size(ox, oy));
    } else {
        hook_redraw(x, y, 1);
    }
}

static int other_realm(int plane) { return plane == GS_PLANE_HELL ? GS_PLANE_HEAVEN : GS_PLANE_HELL; }

/* 0x42c09b */
bool structure_destroy(int x, int y, unsigned sound, unsigned flags)
{
    if ((unsigned)x >= 100 || (unsigned)y >= 100)
        return false;
    if (g_build.editor)
        flags |= 1;
    bool forced = flags & 1, noask = flags & 4;
    int ox = x, oy = y;
    unsigned set = cell_tileset(x, y);
    if (set == 0) {
        sim_origin(&ox, &oy);
        set = cell_get_flat(ox, oy) >> 27;
        if (!in_map(ox, oy))
            return false; /* a broken origin; the original would write into other globals */
    }
    int size = structure_size(ox, oy);
    unsigned idx = cell_index(ox, oy);
    int plane = g_world.plane;
    PlaneStats *r = realm_cur();
    bool bracket = false;

    if (cell_test_bracket(ox, oy)) {
        uint32_t j = structure_upkeep(ox, oy, set) * 5;
        /* the other realm's +0x8004 (as the original indexes them) */
        uint32_t *v = &world_realm(plane == GS_PLANE_HELL ? GS_PLANE_HEAVEN : GS_PLANE_HELL)->unk_7ff0[5];
        *v = *v < j ? 0 : *v - j;
        bracket = true;
    }

    switch (set) {
    case 1:
        if (!forced) {
            if (idx != 10 && idx != 12) {
                if (idx == 11)
                    hook_sound(0x10021, 2);
                return false;
            }
        } else if (idx < 10 || idx > 12) {
            return false;
        }
        structure_clear_area(ox, oy, size, flags);
        break;
    case 2:
        if (!forced)
            return false;
        structure_clear_area(ox, oy, size, flags);
        sound = 4;
        if (g_build.editor)
            water_autotile_all();
        break;
    case 3:
    case 7:
    case 8:
        structure_clear_area(ox, oy, size, flags);
        break;
    case 4:
        if (!forced && !noask && g_build_hooks.confirm && !g_build_hooks.confirm(BUILD_CONFIRM_SPECIAL))
            return false;
        structure_clear_area(ox, oy, size, flags);
        if (g_state.hdr.difficulty == 3) {
            if (idx < 10)
                g_build.special_pos[idx][0] = g_build.special_pos[idx][1] = 0x78;
        } else {
            g_state.unk_638668 &= ~(1u << (idx & 0x1f));
        }
        break;
    case 5: {
        if (!forced && !noask && g_build_hooks.confirm && !g_build_hooks.confirm(BUILD_CONFIRM_OMNIBOLGE))
            return false;
        const SimCell *c = sim_cell(ox, oy);
        bignum_sub(&r->souls, sim_cell_u32(c, 5));
        bignum_sub(&r->souls, sim_cell_u32(c, 9));
        bignum_sub(&r->souls, sim_cell_u32(c, 1));
        structure_clear_area(ox, oy, size, flags);
        break;
    }
    default: /* 0, 6, 11, 20-28, 31: just the origin cell */
        cell_set_tileset(ox, oy, 1);
        rand_range(0, 2); /* both outcomes index 0 */
        cell_set_index(ox, oy, 0);
        cell_set_strip(ox, oy, 1);
        break;
    case 9: {
        uint8_t(*list)[2] = r->unk_78c4; /* [0][0] = count, entries 1.. */
        unsigned n = list[0][0], i = 1;
        bool found = false;
        do {
            if ((int)(int8_t)list[i][0] == ox && (int)(int8_t)list[i][1] == oy) {
                list[i][0] = list[i][1] = 0xff;
                found = true;
            } else {
                i = (i + 1) & 0xff;
            }
        } while (i <= n && i < 128 && !found);
        if (found) {
            list[0][0] = (uint8_t)(n >= 1 ? n - 1 : 0);
            if (i < n) {
                for (unsigned j = i; j < n && j + 1 < 128; j++)
                    list[j][0] = list[j + 1][0], list[j][1] = list[j + 1][1];
                list[n][0] = list[n][1] = 0xff;
            }
        }
        uint32_t mask = sim_cell_u32(sim_cell(ox, oy - 1), 2);
        unsigned bit = 0;
        for (int cx = ox; cx < ox + size; cx++)
            for (int cy = oy; cy > oy - size; cy--) {
                if (!(mask & 1u << (bit & 31))) {
                    cell_set_tileset(cx, cy, 1);
                    cell_set_index(cx, cy, 0);
                } else {
                    cell_set_tileset(cx, cy, 2);
                    int wm = g_world.water_entry_max;
                    cell_set_index(cx, cy, (unsigned)rand_range(wm - 3, wm));
                }
                cell_set_strip(cx, cy, 1);
                bit++;
                *cell_ptr(cx, cy) &= 0xffff7fc8u;
            }
        for (int cx = ox; cx < ox + size; cx++)
            for (int cy = oy; cy > oy - size; cy--)
                if (cell_tileset(cx, cy) == 2)
                    water_autotile(cx, cy);
        break;
    }
    case 10:
        if (!forced)
            return false;
        structure_clear_area(ox, oy, size, flags);
        break;
    case 12: {
        structure_clear_area(ox, oy, size, flags);
        structure_update_access(ox, oy, 0);
        road_autotile(ox, oy, true, true, false);
        unsigned slot = cell_object_slot(ox, oy);
        if (slot) {
            /* the eight walker objects 0x5f7400[] and the one after them */
            for (int k = 0; k < 9; k++) {
                unsigned s = k < 8 ? g_state.unk_5f7400[k] : g_build.obj_5f7408;
                if (s == slot) {
                    mapobj_mark_cells(ox, oy, 1, (int)slot, 0);
                    hook_redraw_fade(ox, oy, 1, (int)slot, 10, 6, 0);
                    break;
                }
            }
        }
        break;
    }
    case 13: {
        structure_clear_area(ox, oy, size, flags);
        int n = (int8_t)r->n_ports, i = 0;
        bool found = false;
        do {
            if (i < 50 && (int)(int8_t)r->ports[i][0] == ox && (int)(int8_t)r->ports[i][1] == oy)
                found = true;
            else
                i++;
        } while (!found && i < n);
        if (found) {
            r->n_ports--;
            for (int j = i; j < (int8_t)r->n_ports && j + 1 < 50; j++)
                r->ports[j][0] = r->ports[j + 1][0], r->ports[j][1] = r->ports[j + 1][1];
        }
        break;
    }
    case 14:
    case 15:
        souls_zone_evict(ox, oy, realm_building_pop(ox, oy));
        structure_clear_area(ox, oy, size, flags);
        cell_clear_flags(ox, oy, 0x20);
        break;
    case 16:
        structure_clear_area(ox, oy, size, flags);
        karma_rail_remove(ox, oy, true); /* 0x42f129(wnd, view, x, y, 1) */
        g_build.karma_dirty = 1;
        break;
    case 17:
        g_state.karma_stations--;
        structure_clear_area(ox, oy, size, flags);
        world_select_plane(GS_PLANE_MIDDLE);
        structure_clear_area(ox, oy, size, flags);
        hook_redraw_fade(ox, oy, size, 0, (int)set, (int)(idx >> 1) - 1, 1);
        world_select_plane(other_realm(plane));
        for (int cx = ox; cx < ox + 3; cx++)
            for (int cy = oy; cy > oy - 3; cy--) {
                cell_clear_flags(cx, cy, 8);
                redraw_cell_structure(cx, cy);
            }
        world_select_plane(plane);
        g_build.karma_dirty = 1;
        break;
    case 18:
        if (!forced)
            return false;
        structure_clear_area(ox, oy, size, flags);
        world_select_plane(GS_PLANE_MIDDLE);
        structure_clear_area(ox, oy, size, flags);
        hook_redraw_fade(ox, oy, size, 0, (int)set, (int)(idx >> 1), 1);
        world_select_plane(other_realm(plane));
        structure_clear_area(ox, oy, size, flags);
        world_select_plane(plane);
        break;
    case 19: {
        if (sim_cell_u32(sim_cell(ox, oy), 1) != 0) { /* a bank with loans out */
            if (!forced && !noask)
                hook_sound(0x10021, 2);
            return false;
        }
        uint32_t n = r->unk_804b, i;
        bool found = false;
        for (i = 0; i < n && i < 20; i++)
            /* sic: both coordinates are compared with y */
            if ((int)(int8_t)r->unk_79c4[i][0] == oy && (int)(int8_t)r->unk_79c4[i][1] == oy) {
                found = true;
                break;
            }
        if (found)
            for (uint32_t j = i; j < n && j < 20; j++) {
                if (j + 1 < 20) {
                    r->unk_79c4[j][0] = r->unk_79c4[j + 1][0];
                    r->unk_79c4[j][1] = r->unk_79c4[j + 1][1];
                } else { /* the original reads on into +0x79ec */
                    r->unk_79c4[j][0] = (uint8_t)r->unk_79ec[0];
                    r->unk_79c4[j][1] = (uint8_t)(r->unk_79ec[0] >> 8);
                }
            }
        structure_clear_area(ox, oy, size, flags);
        r->unk_804b--;
        r->unk_8047 -= SPEC(set19, idx).capacity;
        break;
    }
    case 29:
        structure_clear_area(ox, oy, size, flags);
        rock_unmark(ox, oy);
        break;
    case 30:
        structure_clear_area(ox, oy, size, flags);
        r->unk_7f30--;
        r->unk_7f34 -= SPEC(set30, idx).capacity;
        break;
    }

    if (sound & 1) {
        if (!(flags & 8))
            hook_sound((uint32_t)rand_range(0x10057, 0x1005a), 3);
        else
            hook_sound(0x10044, 3);
    }
    if (sound & 2) {
        if (bracket)
            hook_redraw_fade(ox, oy, size, 0, 10, size * 2 + 0x25, 1);
        /* dust clouds: one map object per corner */
        uint8_t dust = plane == GS_PLANE_HEAVEN ? 0x1d : 2;
        int corners = size < 2 ? 1 : 4;
        for (int k = 0; k < corners; k++) {
            int cx = (k & 1) ? ox + size - 1 : ox, cy = (k & 2) ? oy - size + 1 : oy;
            if (mapobj_spawn(cx, cy, 1, dust, 0, 0, 0, 1, (uint8_t)plane, 3, 0xffffffffu, 2, 1) > 0)
                g_state.object_count++;
        }
    }
    hook_redraw_fade(ox, oy, size, 0, (int)set, (int)idx + 1, (sound & 4) ? 0 : 1);
    if (g_build_hooks.refresh)
        g_build_hooks.refresh();
    return true;
}
