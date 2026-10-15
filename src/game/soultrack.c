/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Soulview trackers, simulation side: game/soultrack.h.
 */
#include "game/soultrack.h"

#include <stddef.h>
#include <string.h>

#include "game/build.h"
#include "game/mapobj.h"
#include "game/realm.h"
#include "game/souls.h"
#include "game/structure.h"
#include "game/util.h"
#include "game/world.h"

SoultrackHooks soultrack_hooks;

uint32_t soultrack_count;
uint32_t soultrack_cat[16];

static Block26 *blk(void) { return &g_state.block_4f33f8; }

/* +0x24 of each record: the map object under the walker (not saved) */
static uint8_t g_under[SOULTRACK_MAX];

static uint8_t *under(const Rec26 *t)
{
    ptrdiff_t i = t - blk()->rec;
    static uint8_t spare;
    return i >= 0 && i < SOULTRACK_MAX ? &g_under[i] : &spare;
}

static void changed(unsigned i)
{
    if (soultrack_hooks.changed)
        soultrack_hooks.changed(i);
}

static void refresh(void)
{
    if (soultrack_hooks.refresh)
        soultrack_hooks.refresh();
}

/* The original indexes its per-category counters (0x630444) with the
 * signed category byte; -1 lands on the total (0x630440). */
static void cat_add(int8_t cat, uint32_t d)
{
    if (cat >= 0 && cat < 16)
        soultrack_cat[cat] += d;
    else if (cat == -1)
        soultrack_count += d;
}

static int walking(const Rec26 *t, unsigned g)
{
    return t->unk_00[0] == TRACK_WALKING && t->unk_17 == g && t->unk_10 == (uint8_t)g_world.plane;
}

/* The realm statistics entry [kind][belief] (the original indexes without
 * a check; out-of-range kinds wrap here). */
static PlaneStatHist *zone_stat(unsigned kind, unsigned j) { return &realm_cur()->hist_a[(kind * 16 + j) % 112]; }

/* 0x47e165 */
uint16_t soultrack_group_mask(unsigned g)
{
    uint16_t m = 0;
    if (blk()->unk_66480c == 0)
        return 0;
    for (int i = 0; i < SOULTRACK_MAX; i++) {
        Rec26 *t = &blk()->rec[i];
        if (walking(t, g)) {
            m |= (uint16_t)(1u << (t->unk_1b & 31));
            soultrack_count++;
            cat_add((int8_t)t->unk_1b, 1);
        }
    }
    return m;
}

/* 0x466575: tile set of the structure covering (x, y) */
static unsigned origin_set(int x, int y)
{
    unsigned s = cell_tileset((uint8_t)x, (uint8_t)y);
    if (s == 0) {
        SimCell *c = sim_cell((uint8_t)x, (uint8_t)y);
        s = cell_tileset(c->b[10], c->b[11]);
    }
    return s;
}

/* 0x46666d: origin of the structure covering (x, y), via sim +10/+11 */
static void origin_xy(int x, int y, int *ox, int *oy)
{
    *ox = (uint8_t)x;
    *oy = (uint8_t)y;
    if (cell_tileset((uint8_t)x, (uint8_t)y) == 0) {
        SimCell *c = sim_cell((uint8_t)x, (uint8_t)y);
        *ox = c->b[10];
        *oy = c->b[11];
    }
}

/* the walker's cell: the building's front corner for the view rotation */
static void walker_corner(Rec26 *t, int n)
{
    if (g_state.view_dir_x < 1) {
        t->unk_00[6] = t->unk_00[4];
        t->unk_00[7] = g_state.view_dir_y < 1 ? (uint8_t)(t->unk_00[5] - (n - 1)) : t->unk_00[5];
    } else {
        t->unk_00[6] = (uint8_t)(t->unk_00[4] + n - 1);
        t->unk_00[7] = g_state.view_dir_y < 1 ? (uint8_t)(t->unk_00[5] - (n - 1)) : t->unk_00[5];
    }
}

/* 0x47e875 */
void soultrack_walker(Rec26 *t, bool show)
{
    int saved = g_world.plane;
    bool already = false;
    if (!t)
        return;
    if (t->unk_10 != 0 && t->unk_10 != (uint8_t)g_world.plane)
        world_select_plane(t->unk_10);
    int x = t->unk_00[6], y = t->unk_00[7];
    if (show) {
        /* 0x4453e1: the object under a road cell; the walker's own object
         * there means it is already drawn. The saved slot is always
         * cleared again (so the restore below never runs). */
        if (x < GS_MAP_W && y < GS_MAP_H && cell_tileset(x, y) == 12 && g_world.objmap)
            *under(t) = g_world.objmap[y * GS_MAP_W + x] >> 2;
        const MapObjectRec *o = &g_state.objects[*under(t) % GS_MAP_OBJECTS];
        already = o->set == 10 && o->frame == 0x23;
        *under(t) = 0;
    }
    if (!show || !already) {
        mapobj_mark_cells(x, y, 1, g_build.obj_57e018, show);
        mapobj_redraw(x, y, 1, g_build.obj_57e018, 10, 0x23, 1);
    }
    if (!show && *under(t) != 0) {
        const MapObjectRec *o = &g_state.objects[*under(t) % GS_MAP_OBJECTS];
        mapobj_mark_cells(x, y, o->size, *under(t), 1);
        mapobj_redraw(x, y, o->size, *under(t), o->set, o->frame, 1);
    }
    if (g_world.plane != saved)
        world_select_plane(saved);
}

/* 0x47eaf7 */
void soultrack_place(Rec26 *t, int x, int y, bool redraw)
{
    if (t->unk_00[1] != 0 && redraw && t->unk_00[0] != TRACK_NONE)
        soultrack_walker(t, false);
    int ox, oy;
    origin_xy(x, y, &ox, &oy);
    t->unk_00[4] = (uint8_t)ox;
    t->unk_00[5] = (uint8_t)oy;
    t->unk_22 = (uint8_t)cell_tileset(ox, oy);
    t->unk_23 = (uint8_t)cell_index(ox, oy);
    switch (t->unk_22) {
    case 5:
        t->unk_00[0] = TRACK_OMNIBOLGE;
        break;
    case 8:
        t->unk_00[0] = TRACK_TRAINING;
        break;
    case 13:
        t->unk_00[0] = TRACK_GATE;
        break;
    case 14:
    case 15:
        t->unk_00[0] = TRACK_RESIDENT;
        break;
    case 17:
        t->unk_00[0] = TRACK_REINCARNATED;
        break;
    default:
        t->unk_00[0] = TRACK_WALKING;
        break;
    }
    t->unk_11 = (uint8_t)sim_zone_kind(ox, oy);
    t->unk_00[2] = (uint8_t)x;
    t->unk_00[3] = (uint8_t)y;
    walker_corner(t, (int8_t)structure_size(ox, oy));
    if (t->unk_00[1] != 0 && redraw)
        soultrack_walker(t, true);
}

/* 0x47ed71: a resident tracker gets an age bucket (unk_25) and the years
 * into it (unk_13), drawn from the residents of its zone kind/belief. */
void soultrack_pick_age(Rec26 *t)
{
    if (t->unk_00[0] == TRACK_RESIDENT && t->unk_0c == 0) {
        uint8_t list[32];
        unsigned n = 0;
        const PlaneStatHist *h = zone_stat(t->unk_11, t->unk_12);
        for (unsigned b = 2; b < 32; b++)
            if (h->v[b] != 0)
                list[n++] = (uint8_t)b;
        if (n == 0) {
            t->unk_13 = 0;
        } else {
            t->unk_25 = list[rand_range(0, (int)n - 1)];
            t->unk_13 = (uint32_t)(t->unk_25 - 2) * 0x421;
            t->unk_13 += (uint32_t)(rand_range(1, 0x421) >> 1);
        }
    } else {
        t->unk_13 = 0;
    }
}

/* 0x47de6c */
uint16_t soultrack_arrive(int x, int y, unsigned g)
{
    uint16_t m = 0;
    if (blk()->unk_66480c == 0)
        return 0;
    for (unsigned i = 0; i < SOULTRACK_MAX; i++) {
        Rec26 *t = &blk()->rec[i];
        if (!walking(t, g))
            continue;
        unsigned s = origin_set(x, y);
        int8_t cat = (int8_t)t->unk_1b;
        int done = 0;
        if (s == 5) {
            done = 1;
        } else if (cat < 14) {
            if (s == 14 || s == 15)
                done = (unsigned)(cat & 7) == sim_zone_kind((uint8_t)x, (uint8_t)y);
            else if (s == 8)
                done = cat > 6;
        } else if (cat == 15) {
            done = s == 13;
        } else {
            done = s == 17;
        }
        if (!done) {
            m |= (uint16_t)(1u << (t->unk_1b & 31));
            continue;
        }
        t->unk_17 = 0;
        t->unk_20 = 0;
        t->unk_1c = g_state.year;
        blk()->unk_66480c--;
        soultrack_count--;
        cat_add(cat, (uint32_t)-1);
        soultrack_place(t, x, y, true);
        soultrack_pick_age(t);
        if (t->unk_0c != 0) {
            int special = t->unk_23 > 0x5a;
            realm_perm_sub(t->unk_00[4], t->unk_00[5], 1, special, t->unk_11);
            realm_temp_add(t->unk_00[4], t->unk_00[5], 1, special, t->unk_11);
        }
        changed(i);
        return m;
    }
    return m;
}

/* 0x47e3be */
void soultrack_step(int x, int y, unsigned g)
{
    if (blk()->unk_66480c == 0)
        return;
    for (int i = 0; i < SOULTRACK_MAX; i++) {
        Rec26 *t = &blk()->rec[i];
        if (walking(t, g)) {
            t->unk_20++;
            if ((t->unk_20 & 1) && t->unk_00[1] != 0) {
                soultrack_walker(t, false);
                t->unk_00[6] = (uint8_t)x;
                t->unk_00[7] = (uint8_t)y;
                soultrack_walker(t, true);
                if (soultrack_hooks.step_wait)
                    soultrack_hooks.step_wait();
            }
        }
    }
}

/* 0x47e4be */
void soultrack_lost(int x, int y, unsigned g, unsigned kind)
{
    (void)x, (void)y;
    if (blk()->unk_66480c == 0)
        return;
    for (unsigned i = 0; i < SOULTRACK_MAX; i++) {
        Rec26 *t = &blk()->rec[i];
        if (walking(t, g) && t->unk_11 == (uint8_t)kind) {
            blk()->unk_66480c--;
            t->unk_00[0] = TRACK_LOST;
            t->unk_1c = g_state.year;
            t->unk_13 = 0;
            t->unk_22 = t->unk_23 = 0;
            t->unk_00[2] = t->unk_00[3] = 0;
            t->unk_00[4] = t->unk_00[5] = 0;
            changed(i);
        }
    }
}

/* 0x47e653 */
void soultrack_remove(Rec26 *t)
{
    if (!t)
        return;
    if (t->unk_00[0] != TRACK_NONE) {
        if (t->unk_00[1] != 0)
            soultrack_walker(t, false);
        blk()->unk_664808--;
        if (t->unk_00[0] == TRACK_WALKING)
            blk()->unk_66480c--;
    }
    memset(t, 0, sizeof *t);
    *under(t) = 0;
}

/* 0x47e5bc, the records part */
void soultrack_reset(void)
{
    for (int i = 0; i < SOULTRACK_MAX; i++)
        soultrack_remove(&blk()->rec[i]);
    blk()->selected = 0;
    blk()->unk_664808 = 0;
    blk()->unk_66480c = 0;
}

/* 0x47e229 */
void soultrack_rotated(void)
{
    for (int i = 0; i < SOULTRACK_MAX; i++) {
        Rec26 *t = &blk()->rec[i];
        if (t->unk_00[1] == 0 || t->unk_10 != (uint8_t)g_world.plane)
            continue;
        int ox, oy;
        origin_xy(t->unk_00[4], t->unk_00[5], &ox, &oy);
        t->unk_00[4] = (uint8_t)ox;
        t->unk_00[5] = (uint8_t)oy;
        int8_t n = (int8_t)structure_size(ox, oy);
        if (n > 1) {
            soultrack_walker(t, false);
            walker_corner(t, n);
            soultrack_walker(t, true);
        }
    }
}

/* the five-year wait after a tracker ended */
static bool expired(const Rec26 *t) { return t->unk_1c + 5u <= g_state.year; }

static void finish(Rec26 *t)
{
    if (t->unk_00[1] != 0)
        soultrack_walker(t, false);
    soultrack_remove(t);
    refresh();
}

/* 0x47d6fb */
void soultrack_age(void)
{
    int saved = g_world.plane;
    /* Set when a resident's building emptied: the category it leaves as.
     * Never reset inside the loop, as in the original, so the trackers
     * after it leave as that category too. */
    int8_t leave_cat = -1;
    if (blk()->unk_664808 == 0)
        return;
    for (int i = 0; i < SOULTRACK_MAX; i++) {
        Rec26 *t = &blk()->rec[i];
        if (t->unk_00[0] == TRACK_NONE)
            continue;
        if ((uint8_t)g_world.plane != t->unk_10)
            world_select_plane(t->unk_10);
        uint8_t st = t->unk_00[0];
        if (st == TRACK_TRAINING || st == TRACK_OMNIBOLGE || st == TRACK_LOST) {
            if (expired(t))
                finish(t);
            continue;
        }
        if (t->unk_1b == 14) {
            if (!expired(t))
                t->unk_00[0] = TRACK_REINCARNATED;
            else
                finish(t);
            continue;
        }
        if (t->unk_1b == 15) {
            if (!expired(t))
                t->unk_00[0] = TRACK_GATE;
            else
                finish(t);
            continue;
        }
        if (t->unk_22 == 17 || realm_building_pop(t->unk_00[4], t->unk_00[5]) != 0) {
            if (t->unk_0c != 0)
                continue; /* temporary residents of a lived-in building stay */
        } else {
            leave_cat = (int8_t)t->unk_11;
            if (t->unk_0c != 0)
                leave_cat = (int8_t)(leave_cat + 7);
        }
        soultrack_place(t, t->unk_00[6], t->unk_00[7], false);
        int x = t->unk_00[4], y = t->unk_00[5];
        if (cell_test_shade(x, y)) {
            if (t->unk_10 != (uint8_t)saved)
                world_select_plane(saved);
            continue;
        }
        const SimCell *s =
            &g_state.plane[t->unk_10 == GS_PLANE_HELL ? GS_PLANE_HELL : GS_PLANE_HEAVEN].sim[y * GS_MAP_W + x];
        uint32_t step = s->b[0x11] < t->unk_13 ? s->b[0x11] : t->unk_13;
        t->unk_13 -= step;
        if (t->unk_13 != 0 && leave_cat == -1)
            continue;
        int rx, ry;
        area_search(x, y, (unsigned)tile_index_footprint(t->unk_23), 3, &rx, &ry);
        if (rx == 0xff || ry == 0xff)
            continue;
        uint8_t entry = t->unk_23;
        blk()->unk_66480c++;
        t->unk_00[0] = TRACK_WALKING;
        t->unk_1b = leave_cat == -1 ? (uint8_t)souls_zone_exit_cat(t->unk_11, t->unk_12) : (uint8_t)leave_cat;
        PlaneStatHist *h = zone_stat(t->unk_11, t->unk_12);
        unsigned b = 1;
        while (b < 32 && h->v[b] == 0)
            b++;
        if (b == 32)
            b = 0;
        if ((int8_t)t->unk_1b == -1) {
            t->unk_0c = 1;
            blk()->unk_66480c--;
            soultrack_pick_age(t);
            t->unk_17 = souls_reincarnate(1, (int8_t)t->unk_12, rx, ry, t->unk_00[2], t->unk_00[3]);
            if (t->unk_17 == 0)
                t->unk_00[0] = TRACK_RESIDENT;
        } else {
            PlaneStats *r = realm_cur();
            int8_t c = (int8_t)t->unk_1b;
            h->v[b]--;
            if (c >= 0 && c < 16) {
                r->unk_7a08[c].v[t->unk_12 & 15]++;
                r->unk_7a08[c].unk_00++;
            }
            if (t->unk_0c == 0)
                realm_perm_sub(x, y, 1, entry > 0x5a, t->unk_11);
            else
                realm_temp_sub(x, y, 1, entry > 0x5a, t->unk_11);
            t->unk_17 = soul_group_add(1, t->unk_1b, rx, ry);
        }
        h->unk_00--;
    }
    changed(blk()->selected);
    /* (the original compares the plane with a byte past the records, which
     * practically always differs) */
    if (g_world.plane != saved)
        world_select_plane(saved);
}

/* 0x47f7be */
uint32_t soultrack_seed(Rec26 *t)
{
    uint32_t pop = realm_building_pop(t->unk_00[4], t->unk_00[5]);
    if (pop < 2)
        pop = 1;
    uint32_t r = (uint32_t)rand_range(1, (int)pop);
    uint32_t s = (uint32_t)t->unk_00[4] * t->unk_00[5] * 0x1251bu;
    if (r < 0xffff)
        s += r * 0xfde9u;
    unsigned k = r & 0x1f;
    if (k != 0) {
        r = r << k | r >> ((0x1f - k) & 31);
        s += r;
    }
    t->unk_0c = realm_building_temp(t->unk_00[4], t->unk_00[5]) < r ? 0 : 1;
    t->unk_08 = s ^ 0xaaaaaaabu;
    return t->unk_08;
}

/* 0x47ef9f */
uint8_t soultrack_pick_belief(const Rec26 *t)
{
    uint8_t list[16];
    int n = 0;
    uint32_t total = 0;
    unsigned kind = t->unk_11;
#define ADD(e) (total += zone_stat(kind, (e))->unk_00, list[n++] = (e))
    ADD(t->unk_10 == GS_PLANE_HELL ? 2 : 3);
    ADD(0);
    ADD(1);
    if (t->unk_0c == 0) {
        if (t->unk_10 == GS_PLANE_HELL) {
            ADD(6);
            ADD(14);
        } else {
            ADD(7);
            ADD(15);
        }
        ADD(4);
        ADD(5);
        ADD(12);
        ADD(13);
    } else {
        ADD(t->unk_10 == GS_PLANE_HELL ? 10 : 11);
        ADD(8);
        ADD(9);
    }
#undef ADD
    uint32_t r = (uint32_t)rand_range(1, (int)total);
    int i = 0;
    while (i < n - 1 && zone_stat(kind, list[i])->unk_00 < r) {
        r -= zone_stat(kind, list[i])->unk_00;
        i++;
    }
    return list[i];
}

/* 0x47e7cc */
void soultrack_start(Rec26 *t, int plane, int x, int y)
{
    if (t->unk_00[0] == TRACK_NONE)
        blk()->unk_664808++;
    soultrack_walker(t, false);
    t->unk_10 = (uint8_t)plane;
    soultrack_place(t, x, y, true);
    soultrack_seed(t);
    t->unk_10 = (uint8_t)plane;
    t->unk_12 = soultrack_pick_belief(t);
    t->unk_17 = 0;
    t->unk_20 = 0;
    t->unk_1c = g_state.year;
    soultrack_pick_age(t);
}

/* 0x47ee8f */
int soultrack_years_left(const Rec26 *t)
{
    const SimCell *s = &g_state.plane[t->unk_10 == GS_PLANE_HELL ? GS_PLANE_HELL : GS_PLANE_HEAVEN]
                            .sim[(t->unk_00[5] * GS_MAP_W + t->unk_00[4]) % GS_MAP_CELLS];
    uint8_t steps = s->b[0x11];
    uint16_t n;
    if (t->unk_0c != 0)
        return -1;
    if (t->unk_00[0] != TRACK_RESIDENT)
        return 0;
    if (steps == 0) {
        n = (uint16_t)t->unk_13;
    } else {
        n = (uint16_t)(t->unk_13 / steps);
        if (t->unk_13 % steps != 0)
            n++;
    }
    return n == 0xffff ? -1 : n; /* (a 16-bit result; 0xffff reads as "permanent") */
}
