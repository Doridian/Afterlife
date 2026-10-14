/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Build tools: game/build.h.
 */
#include "game/build.h"

#include <stdlib.h>
#include <string.h>

#include "game/mapobj.h"
#include "game/realm.h"
#include "game/souls.h"
#include "game/structure.h"
#include "game/tables.h"
#include "game/util.h"
#include "game/world.h"

/* After game_reset_world every plane shows grid and zone detail (0xc0). */
BuildState g_build = {.view_flags = {0, 0xc0, 0xc0, 0xc0}};
BuildHooks g_build_hooks;

/* ---- helpers ------------------------------------------------------------------- */

static void snd(uint32_t id, int ch)
{
    if (g_build_hooks.sound)
        g_build_hooks.sound(id, ch);
}

static void redraw(int x, int y, int size)
{
    if (g_build_hooks.redraw)
        g_build_hooks.redraw(x, y, size);
}

static void refresh(void)
{
    if (g_build_hooks.refresh)
        g_build_hooks.refresh();
}

static void vibes_update(void) { realm_vibes_update(); }

/* the "no road nearby" tint after placing (0x45ebe2 == 0xff) */
static void tint_if_no_access(int x, int y, int size, int radius)
{
    int ax, ay;
    if (area_search(x, y, (unsigned)size, (unsigned)radius, &ax, &ay) == 0xff && !(cell_get(x, y) & 2))
        cell_set_flags(x, y, 2);
}

static BuildingSpec spec_at(const BuildingSpec *t, size_t n, unsigned i)
{
    BuildingSpec none = {0, 0, 0, 0};
    return i < n ? t[i] : none;
}
#define SPEC(arr, i) spec_at(g_tables.arr, sizeof g_tables.arr / sizeof g_tables.arr[0], (i))

static inline uint8_t view_flags_cur(void) { return g_build.view_flags[g_world.plane & 3]; }

static inline bool pennies_less(int64_t v)
{
    return bignum_less(g_state.pennies, (int32_t)v) != 0;
}

static void pay(int32_t v) { bignum_sub(&g_state.pennies, (uint32_t)v); }

static void sim_zero(int x, int y, int off, int n)
{
    memset(&sim_cell(x, y)->b[off], 0, (size_t)n);
}

/* 0x44add1 */
static void redraw_cell_structure(int x, int y)
{
    uint32_t c = cell_get(x, y);
    if (c >> 27 == 0) {
        int ox = (uint8_t)(x - (int)((c >> 16) & 7)), oy = (uint8_t)(y + (int)((c >> 20) & 7));
        redraw(ox, oy, structure_size(ox, oy));
    } else {
        redraw(x, y, 1);
    }
}

/* ---- defaults and tool selection ------------------------------------------------- */

/* 0x45546c game_init_defaults (build part; newworld.c runs the rest) */
void build_init_defaults(void)
{
    g_build.cost_shown = 0;
    g_build.editor = 0;
    tables_init_defaults();
}

/* 0x450aba */
void build_select_tool(int tool)
{
    memset(g_build.tool_sounds, 0, sizeof g_build.tool_sounds);
    g_build.cost_shown = 0;
    g_build.tool_cost = 0;
    if ((unsigned)tool < 9) {
        g_build.tool_cost = tool == 8 ? 1000 : 2500;
        if (g_build.editor) {
            g_build.tool_set = g_world.plane == GS_PLANE_HELL ? 14 : 15;
            g_build.tool_index = (uint8_t)((tool - 1) * 13);
            g_build.tool_cost = 0;
        }
        g_build.tool_size = 1;
    } else {
        uint32_t *s = g_build.tool_sounds;
        switch (tool) {
        case 9:
            g_build.tool_size = 3, g_build.tool_cost = (int32_t)g_tables.set13[0].cost, s[0] = 0x10035;
            break;
        case 10:
            g_build.tool_size = 1, g_build.tool_cost = 100;
            break;
        case 11:
            g_build.tool_size = 3, g_build.tool_cost = (int32_t)g_tables.set17[0].cost, s[0] = 0x10019;
            break;
        case 12:
            g_build.tool_size = 1, g_build.tool_cost = 500;
            break;
        case 13:
            g_build.tool_size = 4, g_build.tool_cost = (int32_t)g_tables.set7[0].cost;
            s[0] = 0x10029, s[1] = 0x1002a;
            break;
        case 14:
            g_build.tool_size = 3, g_build.tool_cost = (int32_t)g_tables.set8[0].cost;
            s[0] = 0x10024, s[1] = 0x10022;
            break;
        case 15:
            g_build.tool_size = g_tables.set9[0].size, g_build.tool_cost = (int32_t)g_tables.set9[0].cost;
            s[0] = 0x10023;
            break;
        case 16:
            g_build.tool_set = 6, g_build.tool_index = 13, g_build.tool_size = 1, g_build.tool_cost = 5000;
            break;
        default:
            g_build.tool_size = 1, g_build.tool_cost = 0;
            break;
        case 20:
            g_build.tool_size = 1, g_build.tool_cost = 0;
            s[0] = 0x10057, s[1] = 0x10058, s[2] = 0x10059, s[3] = 0x1005a;
            break;
        case 0xfa:
            g_build.cheat_step = (int8_t)0xfa;
            memset(s, 0, sizeof g_build.tool_sounds);
            return;
        case 0xfb:
            if (g_build.cheat_step == -6) {
                g_build.cheat_step = (int8_t)0xfb;
                memset(s, 0, sizeof g_build.tool_sounds);
                return;
            }
            break;
        case 0xfc:
            if (g_build.cheat_step == -5) {
                memset(s, 0, sizeof g_build.tool_sounds);
                if ((int8_t)g_state.unk_630425 < 0)
                    return;
                if (!g_build.editor)
                    g_state.unk_630425++;
                if ((int8_t)g_state.unk_630425 > 4) {
                    g_state.unk_630425 = 0xff;
                    if (g_build_hooks.great_disturbance)
                        g_build_hooks.great_disturbance();
                    /* "Look at the size of that thing!" */
                    int x = rand_range(10, 0x5a);
                    mapobj_spawn(x, 3, 3, 2, 0, 1, 4, 4, 1, 10, 0xffffffffu, 0x5a, 1);
                }
                bignum_add(&g_state.pennies, 10000000);
                snd(0x10056, 2);
                g_build.status_dirty = 1;
                return;
            }
            break;
        }
        g_build.cheat_step = 0;
    }
    g_build.status_dirty = 1;
    g_build.cost_shown = g_build.tool_cost;
}

void build_tool_set_item(unsigned set, unsigned index, unsigned size)
{
    g_build.tool_set = (uint8_t)set;
    g_build.tool_index = (uint8_t)index;
    g_build.tool_size = (uint8_t)(size ? size : (unsigned)tile_index_footprint(index));
}

/* ---- footprint checks ------------------------------------------------------------- */

/* 0x44ac03 */
bool build_area_free(int x, int y, int size)
{
    for (int cx = x; cx < x + size; cx++)
        for (int cy = y; cy > y - size; cy--) {
            if (cx > 99 || cy < 0 || cx < 0 || cy > 99)
                return false;
            uint32_t c = cell_get(cx, cy);
            if (c & 0x20)
                return false;
            unsigned idx = (c >> 16) & 0x7f;
            switch (c >> 27) {
            case 1:
                if (cell_origin_index(cx, cy) == 0xb)
                    return false;
                break;
            case 14:
            case 15:
                if (idx % 13 != 0 || idx > 0x5b)
                    return false;
                break;
            default: /* includes 13 */
                return false;
            }
        }
    return true;
}

/* 0x44a711: a set-9 structure over land and water */
static bool place_set9(int x, int y, unsigned index, int size)
{
    PlaneStats *r = realm_cur();
    uint8_t(*list)[2] = r->unk_78c4;
    if ((int8_t)list[0][0] >= 0x7f)
        return false;
    if (x == 0 || y + 1 == size || size + x == 100 || y == 99)
        return false;
    int land = 0, water = 0;
    unsigned bit = 0;
    uint32_t mask = 0;
    uint8_t wb = 0;
    for (int cx = x; cx < x + size; cx++)
        for (int cy = y; cy > y - size; cy--) {
            if (cx > 99 || cy < 0)
                return false;
            uint32_t c = cell_get(cx, cy);
            unsigned idx = (c >> 16) & 0x7f;
            switch (c >> 27) {
            case 1:
                land++;
                break;
            case 2:
                water++;
                wb = sim_cell(cx, cy)->b[1];
                mask |= 1u << (bit & 31);
                break;
            case 14:
            case 15:
                if (idx % 13 != 0 || idx > 0x5b)
                    return false;
                land++;
                break;
            default:
                return false;
            }
            bit++;
        }
    if (!land || !water)
        return false;
    map_stamp_building(x, y, 9, index, size, true);
    SimCell *s = sim_cell(x, y);
    s->b[0xf] = wb;
    sim_cell_set_u32(sim_cell(x, y - 1), 2, mask); /* the water bits live in the part cell above */
    sim_cell_set_u32(s, 1, 0);
    sim_cell_set_u32(s, 5, 0);
    s->b[0xd] = 3;
    s->b[0xe] = 1;
    unsigned n = list[0][0], i = 1;
    bool done = false;
    if (n != 0) {
        do {
            if ((int8_t)list[i][0] < 0) {
                list[0][0] = (uint8_t)++n;
                list[i][0] = (uint8_t)x, list[i][1] = (uint8_t)y;
                done = true;
            } else {
                i++;
            }
        } while (i <= n && i < 128 && !done);
    }
    if (!done && n + 1 < 128) {
        list[0][0] = (uint8_t)++n;
        list[n][0] = (uint8_t)x, list[n][1] = (uint8_t)y;
    }
    tint_if_no_access(x, y, size, 3);
    return true;
}

/* 0x44b4fb */
bool build_place(unsigned set, unsigned index, int size, int x, int y, unsigned flags)
{
    bool show = flags & BUILD_REDRAW, raw = flags & BUILD_RAW;
    int plane = g_world.plane;
    uint8_t detail = view_flags_cur() & 0x40;
    if (!raw) {
        /* the clicked cell becomes the front corner */
        if (g_state.view_dir_x < 1) {
            if (y + size - 1 >= GS_MAP_H) {
                snd(0x10021, 2);
                return false;
            }
            y = y + size - 1;
        } else if (y - size + 1 < 0) {
            snd(0x10021, 2);
            return false;
        }
        if (g_state.view_dir_y < 0) {
            if (x - size + 1 < 0) {
                snd(0x10021, 2);
                return false;
            }
            x = x - (size - 1);
        } else if (x + size - 1 >= GS_MAP_W) {
            snd(0x10021, 2);
            return false;
        }
    }
    if (x < 0 || x >= GS_MAP_W || y < 0 || y >= GS_MAP_H)
        return false;

    int32_t cost;
    switch (set) {
    case 4:
        cost = 0;
        break;
    case 5:
        cost = (int32_t)SPEC(set5, index).cost;
        break;
    case 6:
        cost = 5000;
        break;
    case 7:
        cost = (int32_t)SPEC(set7, index).cost;
        break;
    case 8:
        cost = (int32_t)SPEC(set8, index).cost;
        break;
    case 9:
        cost = (int32_t)SPEC(set9, index).cost;
        break;
    case 13:
        cost = (int32_t)SPEC(set13, index >> 1).cost;
        break;
    case 14:
    case 15:
        cost = index < 0x5b ? 2500 : 1000;
        break;
    case 17:
        cost = (int32_t)SPEC(set17, index).cost;
        break;
    case 19:
        cost = (int32_t)SPEC(set19, index).cost;
        break;
    case 29:
        cost = 50000;
        break;
    case 30:
        cost = (int32_t)SPEC(set30, index).cost;
        break;
    default:
        cost = 500;
        break;
    }
    if (!g_build.editor && !raw) {
        if (pennies_less(cost) && cost > 0) {
            snd(0x10021, 2);
            return false;
        }
    } else {
        cost = 0;
    }

    PlaneStats *r = realm_cur();
    switch (set) {
    case 4: {
        if ((g_state.unk_638668 & 1u << (index & 0x1f)) && !g_build.editor)
            return false;
        unsigned zone = index / 5;
        if ((zone == 0 && plane == GS_PLANE_HELL) || (zone == 1 && plane == GS_PLANE_HEAVEN)) {
            snd(0x10021, 2);
            return false;
        }
        break;
    }
    case 5: {
        unsigned odd = index & 1;
        if ((odd == 0 && plane == GS_PLANE_HELL) || (odd == 1 && plane == GS_PLANE_HEAVEN)) {
            snd(0x10021, 2);
            return false;
        }
        break;
    }
    case 6: { /* an ornament on a road */
        if (cell_tileset(x, y) != 12)
            return false;
        SimCell *s = sim_cell(x, y);
        if (s->b[0xd] & 1)
            return false;
        s->b[0xd] |= 1;
        int slot = g_build_hooks.road_ornament_slot ? (int)g_build_hooks.road_ornament_slot() : 0;
        mapobj_mark_cells(x, y, 1, slot, 1);
        redraw(x, y, 1);
        pay(cost);
        g_build.status_dirty = 1;
        return true;
    }
    case 9: {
        bool ok = place_set9(x, y, index, SPEC(set9, index).size);
        if (ok) {
            if (show)
                redraw(x, y, SPEC(set9, index).size);
            pay(cost);
            g_build.status_dirty = 1;
            snd(0x10023, 2);
            return true;
        }
        snd(0x10021, 2);
        return false;
    }
    case 29:
        if (rock_value(x, y) == 0) {
            snd(0x10021, 2);
            return false;
        }
        break;
    default:
        break;
    }

    if (!build_area_free(x, y, size)) {
        if (show)
            snd(0x10021, 2);
        return false;
    }
    if (set == 13) {
        if ((int8_t)r->n_ports > 49) {
            snd(0x10021, 2);
            return false;
        }
    } else if (set == 19 && r->unk_804b > 19) {
        snd(0x10021, 2);
        return false;
    }

    unsigned orig_index = index;
    if (set == 13) {
        int n = (int8_t)r->n_ports;
        if (n >= 0 && n < 50)
            r->ports[n][0] = (uint8_t)x, r->ports[n][1] = (uint8_t)y;
        r->n_ports++;
    } else if (set == 17 || set == 18) {
        /* Karma stations stand on all three planes */
        int other = plane == GS_PLANE_HEAVEN ? GS_PLANE_HELL : GS_PLANE_HEAVEN;
        world_select_plane(GS_PLANE_MIDDLE);
        for (int cx = x; cx < x + 3; cx++)
            for (int cy = y; cy > y - 3; cy--)
                if (cx > 99 || cy < 0 || cell_tileset(cx, cy) != 1) {
                    world_select_plane(plane);
                    snd(0x10021, 2);
                    return false;
                }
        world_select_plane(other);
        if (set == 17) {
            for (int cx = x; cx < x + 3; cx++)
                for (int cy = y; cy > y - 3; cy--) {
                    cell_set_flags(cx, cy, 8);
                    redraw_cell_structure(cx, cy);
                }
        } else if (!build_area_free(x, y, 3)) {
            world_select_plane(plane);
            snd(0x10021, 2);
            return false;
        }
        world_select_plane(GS_PLANE_MIDDLE);
        map_stamp_building(x, y, set, index, size, false);
        redraw(x, y, size);
        for (int cx = x - 1; cx <= x + 3; cx++)
            for (int cy = y + 1; cy >= y - 3; cy--)
                if (cy >= 0 && cx >= 0 && cx < 100 && cy < 100 && cell_tileset(cx, cy) == 16) {
                    road_autotile(cx, cy, true, false, false);
                    redraw(cx, cy, 1);
                }
        g_build.karma_dirty = 1;
        if (set == 18) {
            world_select_plane(other);
            map_stamp_building(x, y, set, plane == GS_PLANE_HEAVEN ? 4 : 3, size, false);
            redraw(x, y, size);
        }
        world_select_plane(plane);
        if (set == 17)
            index = index * 2 + (plane == GS_PLANE_HEAVEN ? 4 : 5);
        else
            index = plane == GS_PLANE_HEAVEN ? 3 : 4;
    }

    g_build.map_dirty = 1;
    for (int cx = x; cx < x + size; cx++)
        for (int cy = y; cy > y - size; cy--)
            sim_zero(cx, cy, 1, 0x11);
    map_stamp_building(x, y, set, index, size, true);

    SimCell *s = sim_cell(x, y);
    switch (set) {
    case 4:
        g_state.unk_638668 |= 1u << (index & 0x1f);
        if (index < 10)
            g_build.special_pos[index][0] = (uint8_t)x, g_build.special_pos[index][1] = (uint8_t)y;
        cell_clear_flags(x, y, 2);
        snd(plane == GS_PLANE_HEAVEN ? 0x10027 : 0x10028, 2);
        break;
    case 5:
        sim_cell_set_u32(s, 1, 0), sim_cell_set_u32(s, 5, 0), sim_cell_set_u32(s, 9, 0);
        s->b[0xd] = 100;
        sim_cell_set_u16(s, 0xe, 0);
        vibes_update();
        tint_if_no_access(x, y, size, 3);
        if (plane == GS_PLANE_HEAVEN) {
            g_state.unk_638668 |= 0x10000;
            snd(0x10026, 2);
        } else {
            g_state.unk_638668 |= 0x20000;
            snd(0x10025, 2);
        }
        break;
    case 6:
        sim_cell_set_u32(s, 1, 0x1234567);
        s->b[5] = 0xff;
        memset(&s->b[6], 0xff, 9);
        break;
    case 7:
        sim_cell_set_u32(s, 1, 0);
        snd(plane == GS_PLANE_HEAVEN ? 0x10029 : 0x1002a, 2);
        break;
    case 8:
        sim_cell_set_u32(s, 1, 0), sim_cell_set_u32(s, 5, 0), sim_cell_set_u32(s, 9, 0);
        s->b[0xd] = (uint8_t)(100 - (index & 0xfe) * 10);
        tint_if_no_access(x, y, size, 3);
        snd(plane == GS_PLANE_HELL ? 0x10024 : 0x10022, 2);
        break;
    case 9:
        tint_if_no_access(x, y, size, 3);
        vibes_update();
        snd(0x10023, 2);
        break;
    case 12:
        cell_clear_flags(x, y, 2);
        sim_cell_set_u32(s, 1, 0), sim_cell_set_u32(s, 5, 0), sim_cell_set_u32(s, 9, 0);
        s->b[0xd] = 0;
        sim_cell_set_u16(s, 0xe, 0xffff);
        road_autotile(x, y, true, true, false);
        break;
    case 13:
        tint_if_no_access(x, y, size, 1);
        sim_cell_set_u32(s, 1, 0), sim_cell_set_u32(s, 5, 0), sim_cell_set_u32(s, 9, 0);
        vibes_update();
        snd(plane == GS_PLANE_HELL ? 0x10034 : 0x10035, 2);
        break;
    case 14:
    case 15:
        if (index % 13 != 0)
            cost = 0;
        for (int cx = x; cx < x + size; cx++)
            for (int cy = y; cy > y - size; cy--)
                sim_set_zone_kind(cx, cy, (index < 0x5b || index > 0x6f) ? index / 13 : 7);
        s->b[0xf] = 0x80;
        s->b[0x10] = (uint8_t)~s->b[0xf];
        s->b[0xe] = g_build.editor ? 0xff : 0x7f;
        s->b[0x11] = 0xf;
        sim_cell_set_u32(s, 6, 0), sim_cell_set_u32(s, 2, 0), sim_cell_set_u32(s, 10, 0);
        tint_if_no_access(x, y, size, 3);
        if (!detail)
            cell_set_flags(x, y, 0x10);
        break;
    case 17:
        s->b[9] = 0;
        cell_set_flags(x, y, 2);
        sim_cell_set_u32(s, 1, 0), sim_cell_set_u32(s, 5, 0);
        s->b[10] = (uint8_t)orig_index;
        vibes_update();
        g_state.unk_59ceb0++;
        tint_if_no_access(x, y, size, 3);
        g_build.karma_dirty = 1;
        snd(orig_index == 0 ? 0x10019 : orig_index == 1 ? 0x1001a : 0x1001b, 2);
        break;
    case 19: {
        uint32_t n = r->unk_804b;
        if (n < 20)
            r->unk_79c4[n][0] = (uint8_t)x, r->unk_79c4[n][1] = (uint8_t)y;
        sim_cell_set_u32(s, 1, 0);
        s->b[5] = 0, s->b[6] = 0;
        r->unk_804b++;
        r->unk_8047 += SPEC(set19, index).capacity;
        cell_clear_flags(x, y, 2);
        snd(0x10056, 2);
        break;
    }
    case 29:
        sim_cell_set_u32(s, 1, 0), sim_cell_set_u32(s, 5, 0);
        rock_mark(x, y);
        sim_cell_set_u32(s, 1, (uint32_t)rock_value(x, y));
        cell_clear_flags(x, y, 2);
        snd(0x1002d, 2);
        break;
    case 30:
        sim_cell_set_u32(s, 1, 0), sim_cell_set_u32(s, 5, 0), sim_cell_set_u32(s, 9, 0);
        s->b[0xd] = 0xff, s->b[0xe] = 0xff;
        r->unk_7f30++;
        r->unk_7f34 += SPEC(set30, index).capacity;
        cell_clear_flags(x, y, 2);
        snd(0x1001e, 2);
        break;
    default: /* 10, 11, 16, 18, 20-28 */
        break;
    }
    if (show) {
        redraw(x, y, size);
        refresh();
    }
    pay(cost);
    g_build.status_dirty = 1;
    return true;
}

/* ---- zones --------------------------------------------------------------------------- */

/* 0x44d34f, mode 2 (modes 0/1 only draw the drag highlight) */
bool build_zone_rect(int x0, int y0, int x1, int y1, int ztool)
{
    unsigned set = g_world.plane == GS_PLANE_HELL ? 14 : 15;
    int b = g_build.tool_size - 1;
    if (g_state.view_dir_y < 0)
        x0 -= b;
    else
        x1 += b;
    if (g_state.view_dir_x < 0)
        y1 += b;
    else
        y0 -= b;
    if (x1 > 98)
        x1 = 99;
    if (y1 > 98)
        y1 = 99;
    if (x0 < 1)
        x0 = 0;
    if (y0 < 1)
        y0 = 0;
    unsigned index = (unsigned)((ztool - 1) * 13) & 0xff;
    bool changed = false;
    for (int cx = x0; cx <= x1; cx++)
        for (int cy = y0; cy <= y1; cy++)
            if (cell_tileset(cx, cy) != set || cell_index(cx, cy) != index)
                changed |= build_place(set, index, 1, cx, cy, 0);
    g_build.cost_shown = g_build.tool_cost;
    g_build.status_dirty = 1;
    if (changed) {
        /* map_redraw_cells over the rectangle */
        for (int cx = x0; cx <= x1; cx++)
            for (int cy = y0; cy <= y1; cy++)
                redraw(cx, cy, 1);
        refresh();
    } else {
        snd(0x10021, 2);
    }
    return changed;
}

/* ---- roads ------------------------------------------------------------------------------ */

static void road_init_cell(int x, int y)
{
    map_stamp_building(x, y, 12, g_world.road_entry, 1, true);
    SimCell *s = sim_cell(x, y);
    sim_cell_set_u32(s, 1, 0), sim_cell_set_u32(s, 5, 0), sim_cell_set_u32(s, 9, 0);
    s->b[0xd] = 0;
    sim_cell_set_u16(s, 0xe, 0xffff);
    road_autotile(x, y, true, true, false);
    structure_update_access(x, y, 1);
    redraw(x, y, 1);
}

/* 0x44f44c, modes 1 (price) and 2 (build); mode 0 only takes the highlight off */
static int32_t road_pass(int x, int y, int dx, int dy, int n, int mode, int32_t cost)
{
    int32_t c = 0;
    bool diag = dx && dy;
    for (int i = 0; i <= n; i++, x += dx, y += dy) {
        if (mode == 0) {
            c += cost;
            if (diag && i != n)
                c += cost;
        } else if (mode == 1) {
            bool ok = build_area_free(x, y, 1);
            uint32_t cl = cell_get_flat(x, y);
            if ((!ok && (cost != 500 || i == 0 || n <= i)) || !g_build.road_cont) {
                if (cl >> 27 != 12 && cl >> 27 != 16)
                    g_build.road_cont = 0;
            } else {
                c += cost;
                if (cost == 500 && (cl & 8))
                    c -= 500;
            }
            if (diag && i != n) {
                int y2 = y + dy;
                bool ok2 = build_area_free(x, y2, 1);
                uint32_t c2 = cell_get_flat(x, y2);
                if ((!ok2 && cost != 500) || !g_build.road_cont) {
                    if (c2 >> 27 != 12 && c2 >> 27 != 16)
                        g_build.road_cont = 0;
                } else {
                    c += cost;
                    if (cost == 500 && (c2 & 8))
                        c -= 500;
                }
            }
        } else {
            g_build.map_dirty = 1;
            if (x < 0 || x > 99 || y < 0 || y > 99) {
                g_build.road_all_ok = 0;
                return c;
            }
            unsigned set = cell_tileset(x, y);
            if (set != 12 && !build_area_free(x, y, 1)) {
                g_build.road_all_ok = 0;
                return c;
            }
            if (pennies_less((int64_t)c + cost)) {
                g_build.road_all_ok = 0;
                return c;
            }
            if (set != 12)
                c += cost;
            road_init_cell(x, y);
            if (diag && i != n) {
                int y2 = y + dy;
                if (y2 < 0 || y2 > 99)
                    return c;
                unsigned set2 = cell_tileset(x, y2);
                if (set2 != 12 && !build_area_free(x, y2, 1))
                    return c;
                if (pennies_less((int64_t)c + cost)) {
                    g_build.road_all_ok = 0;
                    return c;
                }
                if (set2 != 12) {
                    g_build.road_all_ok = 0; /* sic */
                    c += cost;
                }
                road_init_cell(x, y2);
            }
        }
    }
    g_build.road_all_ok = 1;
    return c;
}

static void line_legs(int x0, int y0, int x1, int y1, int *sx, int *sy, int *n, int *ax, int *ay)
{
    *ax = abs((int8_t)(x1 - x0));
    *ay = abs((int8_t)(y1 - y0));
    *sx = *ax ? (int8_t)(x1 - x0) / *ax : 0;
    *sy = *ay ? (int8_t)(y1 - y0) / *ay : 0;
    if (*ax == 0 || *ay == 0)
        *n = *ax > *ay ? *ax : *ay;
    else
        *n = *ax < *ay ? *ax : *ay;
}

/* 0x44fc9b */
static int32_t road_line(int x0, int y0, int x1, int y1, int mode, int32_t cost)
{
    int sx, sy, n, ax, ay;
    line_legs(x0, y0, x1, y1, &sx, &sy, &n, &ax, &ay);
    g_build.road_all_ok = 1;
    g_build.road_cont = 1;
    int32_t c = road_pass(x0, y0, sx, sy, n, mode, cost);
    g_build.cost_shown = c;
    if (ay == ax || ax == 0 || ay == 0 || c == 0 || !bignum_ge_int(g_state.pennies, c) || !g_build.road_all_ok) {
        if (c != 0 && bignum_ge_int(g_state.pennies, c) && mode == 2) {
            pay(g_build.cost_shown);
            g_build.cost_shown = cost;
        }
    } else {
        int mx = sx * n + x0, my = sy * n + y0;
        int rx = (int8_t)(x1 - mx), ry = (int8_t)(y1 - my);
        int dx2 = 0, dy2 = 0, n2;
        if (rx == 0) {
            dy2 = ry / abs(ry);
            n2 = abs(ry);
        } else {
            dx2 = rx / abs(rx);
            n2 = abs(rx);
        }
        g_build.cost_shown += road_pass(mx, my, dx2, dy2, n2 & 0xff, mode, cost);
        if (mode == 2) {
            pay(g_build.cost_shown);
            g_build.cost_shown = cost;
        } else {
            g_build.cost_shown -= cost;
        }
    }
    g_build.status_dirty = 1;
    if (mode > 0)
        snd(cost == 500 ? 0x10018 : 0x10017, 1);
    if (mode == 2) {
        g_build.map_dirty = 1;
        refresh();
    }
    return g_build.cost_shown;
}

int32_t build_road_line(int x0, int y0, int x1, int y1, bool commit)
{
    return road_line(x0, y0, x1, y1, commit ? 2 : 1, 100);
}

/* ---- Karma rails --------------------------------------------------------------------------- */

/* 0x450017: whether either realm has a rail or station at (x, y) */
static bool rail_station_at(int x, int y)
{
    int plane = g_world.plane;
    world_select_plane(GS_PLANE_MIDDLE);
    world_select_plane(GS_PLANE_HELL);
    unsigned s = cell_origin_tileset(x, y);
    if (s == 16 || s == 18 || s == 17) {
        world_select_plane(plane);
        return true;
    }
    world_select_plane(GS_PLANE_HEAVEN);
    s = cell_origin_tileset(x, y);
    world_select_plane(plane);
    return s == 16 || s == 18 || s == 17;
}

/* 0x44aede, commit form, on plane 2 */
static int32_t rail_pass(int x, int y, int dx, int dy, int n)
{
    int32_t c = 0;
    bool diag = dx && dy;
    int px = -1, py = -1;
    for (int i = 0; i <= n; i++, x += dx, y += dy) {
        for (int k = 0; k < (diag && i != n ? 2 : 1); k++) {
            int cy = k ? y + dy : y;
            if (x < 0 || x > 99 || cy < 0 || cy > 99)
                continue;
            unsigned set = cell_tileset(x, cy);
            if (set == 1) {
                cell_set_strip(x, cy, 1);
                cell_set_tileset(x, cy, 16);
                cell_set_index(x, cy, 0);
                if (!(view_flags_cur() & 0x40))
                    cell_set_flags(x, cy, 0x10);
                world_select_plane(GS_PLANE_HELL);
                cell_set_flags(x, cy, 8);
                redraw_cell_structure(x, cy);
                world_select_plane(GS_PLANE_HEAVEN);
                cell_set_flags(x, cy, 8);
                redraw_cell_structure(x, cy);
                world_select_plane(GS_PLANE_MIDDLE);
                c += 500;
            }
            if (px >= 0) {
                road_autotile(px, py, true, false, false);
                redraw(px, py, 1);
            }
            if (set == 16 || set == 1)
                px = x, py = cy;
            else
                px = py = -1;
        }
    }
    if (px >= 0) {
        road_autotile(px, py, true, false, false);
        redraw(px, py, 1);
    }
    g_build.status_dirty = 1;
    g_build.cost_shown = c;
    return c;
}

/* 0x44d6cc: whether a rail may run from (x0, y0) to (x1, y1) */
static bool rail_endpoint_ok(int x, int y)
{
    if (x >= 49 && x <= 50 && y >= 49 && y <= 50)
        return false;
    switch (cell_origin_tileset(x, y)) {
    case 1:
    case 14:
    case 15:
        return build_area_free(x, y, 1);
    case 16:
    case 17:
    case 18:
        return true;
    default:
        return false;
    }
}

static bool rail_line_ok(int x0, int y0, int x1, int y1)
{
    if ((x0 >= 49 && x0 <= 50 && y0 >= 49 && y0 <= 50) || (x1 >= 49 && x1 <= 50 && y1 >= 49 && y1 <= 50))
        return false;
    if (!rail_endpoint_ok(x0, y0) || !rail_endpoint_ok(x1, y1))
        return false;
    if (x1 != x0 && y1 != y0) {
        int ax = abs((int8_t)(x1 - x0)), ay = abs((int8_t)(y1 - y0));
        if (ax != ay) {
            int m = ax < ay ? ax : ay;
            int cx = m * (ax / (int8_t)(x1 - x0)) + x0, cy = m * (ay / (int8_t)(y1 - y0)) + y0;
            if (!rail_endpoint_ok(cx, cy))
                return false;
        }
    }
    return true;
}

/* 0x45011d */
bool build_rail_line(int x0, int y0, int x1, int y1)
{
    int realm = g_world.plane;
    if (realm == GS_PLANE_MIDDLE)
        realm = g_world.realm_plane;
    world_select_plane(realm);
    bool s0 = rail_station_at(x0, y0), s1 = rail_station_at(x1, y1);
    unsigned idx = realm == GS_PLANE_HELL ? 12 : 11;
    int sx, sy, n, ax, ay;
    line_legs(x0, y0, x1, y1, &sx, &sy, &n, &ax, &ay);
    world_select_plane(GS_PLANE_MIDDLE);
    int mx = 0, my = 0;
    bool s2 = true;
    if (ay == ax || ax == 0 || ay == 0) {
        pay(rail_pass(x0, y0, sx, sy, n));
    } else {
        mx = sx * n + x0, my = n * sy + y0;
        world_select_plane(realm);
        s2 = rail_station_at(mx, my);
        world_select_plane(GS_PLANE_MIDDLE);
        int rx = (int8_t)(x1 - mx), ry = (int8_t)(y1 - my);
        int dx2 = 0, dy2 = 0, n2;
        if (rx == 0)
            dy2 = ry / abs(ry), n2 = abs(ry);
        else
            dx2 = rx / abs(rx), n2 = abs(rx);
        int32_t c1 = rail_pass(x0, y0, sx, sy, n);
        int32_t c2 = rail_pass(mx, my, dx2, dy2, n2);
        pay(c1 + c2);
    }
    world_select_plane(realm);
    const int pts[3][2] = {{x0, y0}, {x1, y1}, {mx, my}};
    const bool st[3] = {s0, s1, s2};
    for (int k = 0; k < 3; k++)
        if (!st[k]) {
            map_stamp_building(pts[k][0], pts[k][1], 16, idx, 1, true);
            cell_clear_flags(pts[k][0], pts[k][1], 8);
            redraw(pts[k][0], pts[k][1], 1);
        }
    g_build.cost_shown = 500;
    g_build.status_dirty = 1;
    world_select_plane(GS_PLANE_MIDDLE);
    g_build.karma_dirty = 1;
    refresh();
    world_select_plane(realm);
    return true;
}

/* ---- the tool dispatcher ------------------------------------------------------------------- */

/* 0x44da29, commit paths */
bool build_apply_tool(int tool, int x0, int y0, int x1, int y1)
{
    int plane = g_world.plane;
    if (tool < 9 && !g_build.editor) {
        if (tool < 1)
            return false;
        int ax = x0 < x1 ? x0 : x1, bx = x0 < x1 ? x1 : x0;
        int ay = y0 < y1 ? y0 : y1, by = y0 < y1 ? y1 : y0;
        build_zone_rect(ax, ay, bx, by, tool);
        return true;
    }
    if (tool == 10) {
        if (cell_tileset(x0, y0) != 12 && !build_area_free(x0, y0, 1))
            return true; /* the original takes the highlight off and returns 1 */
        road_line(x0, y0, x1, y1, 2, 100);
        return true;
    }
    if (tool == 12) {
        if (!rail_line_ok(x0, y0, x1, y1))
            return false;
        if (bignum_ge_int(g_state.pennies, 1))
            build_rail_line(x0, y0, x1, y1);
        return true;
    }
    int x = x1, y = y1;
    if (g_build.editor && tool < 20 && g_build.editor_clear + x < 100 && y - g_build.editor_clear > -2)
        for (int cx = x; cx < x + g_build.editor_clear; cx++)
            for (int cy = y; cy > y - g_build.editor_clear; cy--)
                if (cy >= 0)
                    structure_destroy(cx, cy, 0, 1);
    unsigned set = g_build.tool_set, idx = g_build.tool_index;
    int size = g_build.tool_size;
    bool heaven = plane == GS_PLANE_HEAVEN;
    switch (tool) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8: /* editor only */
        g_build.tool_set = (uint8_t)(plane == GS_PLANE_HELL ? 14 : 15);
        return build_place(g_build.tool_set, idx, size, x, y, BUILD_REDRAW);
    case 9:
        return build_place(set, heaven ? idx : idx + 2, size, x, y, BUILD_REDRAW);
    case 11:
    case 17:
    case 18:
    case 19:
        return build_place(set, idx, size, x, y, BUILD_REDRAW);
    case 13:
    case 14:
        return build_place(set, heaven ? idx : idx + 1, size, x, y, BUILD_REDRAW);
    case 15:
        if (set == 9)
            return build_place(9, heaven ? idx : idx + 1, size, x, y, BUILD_REDRAW);
        else {
            int w = heaven ? rand_range(0x12, 0x15) : rand_range(0, 3);
            bool ok = build_place(set, (unsigned)w, size, x, y, BUILD_REDRAW);
            water_autotile_all();
            return ok;
        }
    case 16:
        if (set == 10)
            return build_place(set, (unsigned)rand_range((int)idx, (int)idx + 2), size, x, y, BUILD_REDRAW);
        return build_place(set, heaven ? idx : idx + 1, size, x, y, BUILD_REDRAW);
    case 20:
        return structure_destroy(x, y, 3, 0);
    default:
        return false;
    }
}

int32_t build_tool_cost(int tool, int x0, int y0, int x1, int y1)
{
    if (tool < 9 && tool >= 1 && !g_build.editor) {
        unsigned set = g_world.plane == GS_PLANE_HELL ? 14 : 15, index = (unsigned)((tool - 1) * 13);
        int ax = x0 < x1 ? x0 : x1, bx = x0 < x1 ? x1 : x0;
        int ay = y0 < y1 ? y0 : y1, by = y0 < y1 ? y1 : y0;
        int32_t c = 0;
        for (int cx = ax; cx <= bx; cx++)
            for (int cy = ay; cy <= by; cy++)
                if (build_area_free(cx, cy, 1) && (cell_tileset(cx, cy) != set || cell_index(cx, cy) != index))
                    c += g_build.tool_cost;
        g_build.cost_shown = c;
        return c;
    }
    if (tool == 10 || tool == 12)
        return road_line(x0, y0, x1, y1, 1, tool == 10 ? 100 : 500);
    return g_build.tool_cost;
}

/* ---- auto-tiling ----------------------------------------------------------------------------- */

/* 0x43d333 */
static unsigned road_neighbour(int x, int y, unsigned bit, unsigned set)
{
    unsigned s = cell_tileset(x, y);
    if (s != set && (set != 16 || (s != 0 && s != 18 && s != 17)))
        return 0;
    return bit;
}

/* 0x43d3d8 */
void road_autotile(int x, int y, bool recurse, bool road, bool keep)
{
    /* the original takes bytes; at the border its x - 1 = 255 reads a flat
     * neighbour row. Only the editor can put roads/rails there; skipped. */
    if ((unsigned)x < 100 && (unsigned)y < 100) {
        unsigned set = road ? 12 : 16;
        uint32_t c = cell_get(x, y);
        unsigned cur = (c >> 16) & 0x7f;
        if (c >> 27 == set) {
            int dx = g_state.view_dir_x, dy = g_state.view_dir_y;
            unsigned shape;
            if (!keep) {
                unsigned n = 0;
                if (x != 0)
                    n += road_neighbour(x - 1, y, 1, set);
                if (x < 99)
                    n += road_neighbour(x + 1, y, 2, set);
                if (y != 0)
                    n += road_neighbour(x, y - 1, 4, set);
                if (y < 99)
                    n += road_neighbour(x, y + 1, 8, set);
                switch (n) {
                case 4:
                case 8:
                case 12:
                    shape = 7;
                    break;
                case 5:
                    shape = dx < 1 ? (dy < 1 ? 5 : 2) : (dy < 1 ? 4 : 1);
                    break;
                case 6:
                    shape = dx < 1 ? (dy < 1 ? 4 : 1) : (dy < 1 ? 5 : 2);
                    break;
                case 7:
                    shape = dy < 1 ? 6 : 3;
                    break;
                case 9:
                    shape = dx < 1 ? (dy < 1 ? 2 : 5) : (dy < 1 ? 1 : 4);
                    break;
                case 10:
                    shape = dx < 1 ? (dy < 1 ? 1 : 4) : (dy < 1 ? 2 : 5);
                    break;
                case 11:
                    shape = dy < 1 ? 3 : 6;
                    break;
                case 13:
                    shape = dx < 1 ? 9 : 8;
                    break;
                case 14:
                    shape = dx < 1 ? 8 : 9;
                    break;
                case 15:
                    shape = 10;
                    break;
                default: /* 0-3 */
                    shape = 0;
                }
            } else {
                shape = cur % 11;
            }
            int skip = 0;
            if (road) {
                uint32_t traffic = sim_cell_u32(sim_cell(x, y), 9);
                if (traffic > 100000)
                    shape += 0x16;
                else if (traffic > 1000)
                    shape += 0xb;
                if (keep)
                    skip = rand_range(0, 10);
                if (shape > 0x41)
                    shape = 0x41;
                shape += g_world.road_entry;
            }
            if (shape != cur) {
                cell_set_index(x, y, shape);
                if (skip == 0 && !recurse && road)
                    redraw(x, y, 1);
            }
        }
    }
    if (recurse) {
        road_autotile(x - 1, y, false, road, keep);
        road_autotile(x, y - 1, false, road, keep);
        road_autotile(x + 1, y, false, road, keep);
        road_autotile(x, y + 1, false, road, keep);
    }
}

/* 0x449c6e */
bool cell_is_water(int x, int y)
{
    uint32_t c = cell_get(x, y);
    unsigned set = c >> 27;
    int ox = x, oy = y;
    if (set == 2)
        return true;
    if (set == 0) {
        ox = (uint8_t)(x - (int)((c >> 16) & 7));
        oy = (uint8_t)(((c & 0x700000) >> 20) + (unsigned)y);
        set = cell_get_flat(ox, oy) >> 27;
    } else if (set != 9) {
        return false;
    }
    if (set != 9 || oy < 1 || oy > 99 || ox > 99)
        return false;
    int n = structure_size(ox, oy);
    uint32_t mask = sim_cell_u32(sim_cell(ox, oy - 1), 2);
    int bit = (x - ox) * n - y + oy;
    return (mask >> (bit & 31)) & 1;
}

/* 0x449e0a */
void water_autotile(int x, int y)
{
    int dx = g_state.view_dir_x, dy = g_state.view_dir_y;
    unsigned n = 0;
    if (x != 0 && !cell_is_water(x - 1, y))
        n = dx < 1 ? 2 : 1;
    if (x < 99 && !cell_is_water(x + 1, y))
        n += dx < 1 ? 1 : 2;
    if (y != 0 && !cell_is_water(x, y - 1))
        n += dy < 1 ? 8 : 4;
    if (y < 99 && !cell_is_water(x, y + 1))
        n += dy < 1 ? 4 : 8;
    if (n > 14)
        n = 0;
    int wm = g_world.water_entry_max;
    if (n == 0) {
        if ((int)cell_index(x, y) > wm)
            cell_set_index(x, y, (unsigned)rand_range(wm - 3, wm));
    } else {
        cell_set_index(x, y, (unsigned)wm + n);
    }
}

/* 0x44a65a */
void water_autotile_all(void)
{
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++)
            if (cell_tileset(x, y) == 2) {
                water_autotile(x, y);
                sim_cell(x, y)->b[1] = 0;
            }
}

/* 0x4506b6 */
void build_zone_blocks(int cmd, bool blocks)
{
    if (cmd == 0x17)
        world_select_plane(GS_PLANE_HELL);
    else if (cmd == 0x18)
        world_select_plane(GS_PLANE_HEAVEN);
    else if (cmd == 0x19)
        world_select_plane(GS_PLANE_MIDDLE);
    if (cmd == 0x17 || cmd == 0x18) {
        uint8_t *vf = &g_build.view_flags[g_world.plane];
        if (!blocks)
            *vf |= 0x40;
        else
            *vf &= 0xbf;
        for (int x = 0; x < 100; x++)
            for (int y = 0; y < 100; y++) {
                unsigned set = cell_tileset(x, y);
                if (set == 0) {
                    const SimCell *s = sim_cell(x, y);
                    set = cell_get_flat(s->b[10], s->b[11]) >> 27;
                }
                if (set == 14 || set == 15) {
                    if (!blocks)
                        cell_clear_flags(x, y, 0x10);
                    else
                        cell_set_flags(x, y, 0x10);
                }
            }
    } else if (cmd == 0x19) {
        for (int x = 0; x < 100; x++)
            for (int y = 0; y < 100; y++)
                if (cell_tileset(x, y) != 1) {
                    if (!blocks)
                        cell_clear_flags(x, y, 0x10);
                    else
                        cell_set_flags(x, y, 0x10);
                }
        if (!blocks) {
            g_build.view_flags[GS_PLANE_MIDDLE] |= 0x40;
            world_select_plane(GS_PLANE_HEAVEN);
            g_build.view_flags[GS_PLANE_HEAVEN] &= 0xef;
            world_select_plane(GS_PLANE_HELL);
            g_build.view_flags[GS_PLANE_HELL] &= 0xef;
        } else {
            g_build.view_flags[GS_PLANE_MIDDLE] &= 0xbf;
            world_select_plane(GS_PLANE_HEAVEN);
            g_build.view_flags[GS_PLANE_HEAVEN] |= 0x10;
            world_select_plane(GS_PLANE_HELL);
            g_build.view_flags[GS_PLANE_HELL] |= 0x10;
        }
    }
}
