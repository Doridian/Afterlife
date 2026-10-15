/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Bad Things' map objects: their cases of map_object_think 0x437184
 * and the helpers it calls (WP4). See badthings.h.
 *
 * `o` is the live record, `b` the record as it was when map_object_think
 * started (the original reads dx, dy, fade, plane and frame into locals
 * first and keeps testing those).
 */
#include <stddef.h>

#include "game/badthings.h"
#include "game/badthings_int.h"
#include "game/enhance.h"
#include "game/mapobj.h"
#include "game/realm.h"
#include "game/structure.h"
#include "game/util.h"

/* 0x4f3108 / 0x4f3100: the giant's next footstep (set 3) */
static int8_t s_step_x, s_step_y;

void badthings_reset(void)
{
    s_step_x = 0;
    s_step_y = 0;
}

static int8_t i8(int v) { return (int8_t)v; }

/* the object leaves: its next move takes it to (to, to), off the map */
static void send_off(MapObjectRec *o, int x, int y, int to)
{
    o->dx = i8(to - x);
    o->dy = i8(to - y);
}

/* ---- The giant (set 3, the build tool cheat) ------------------------------------- */

static int wander(int8_t v)
{
    if (v < 0x14)
        return rand_range(0, 1);
    if (v < 'Q')
        return rand_range(-1, 1);
    return rand_range(-1, 0);
}

static void think_giant(int slot, MapObjectRec *o, const MapObjectRec *b)
{
    int8_t x = b->x, y = b->y, dx = b->dx, dy = b->dy;
    unsigned frame = b->frame;
    if (frame < 2)
        return;
    if (frame == 3) {
        s_step_x = x;
        if (g_state.view_dir_y < 0)
            s_step_y = i8(y - 3);
        else
            s_step_y = y;
        if (g_state.view_dir_x < 0)
            s_step_x = i8(x + 3);
        if (b->fade != 0) {
            unsigned s = cell_origin_tileset(s_step_x, s_step_y);
            if (s == 0 || (s > 2 && s != 0x12)) {
                o->fade--;
                o->first_frame = 8;
                o->dx = 0, o->dy = 0;
                return;
            }
            o->first_frame = 4;
        }
    }
    if (frame == 8) {
        o->first_frame = 4;
        int ddx, ddy;
        do {
            ddx = wander(x);
            ddy = wander(y);
        } while (ddx == 0 && ddy == 0);
        o->dx = i8(ddx), o->dy = i8(ddy);
        structure_destroy(s_step_x, s_step_y, 3, 4);
        return;
    }
    if (dx + x < 0x61 && 0 < dx + x && dy + y < 100 && 3 < dy + y) {
        o->dx = dx, o->dy = dy;
    } else {
        if (dx + x > 0x60)
            o->dx = i8(-dx), o->dy = i8(rand_range(-1, 1));
        if (dx + x < 1)
            o->dx = i8(-dx), o->dy = i8(rand_range(-1, 1));
        if (dy + y > 99)
            o->dy = i8(-dy), o->dx = i8(rand_range(-1, 1));
        if (dy + y < 4)
            o->dy = i8(-dy), o->dx = i8(rand_range(-1, 1));
        if (b->fade == 0) {
            /* wades over into the other realm */
            o->fade = 10;
            mapobj_mark_cells(x, y, 4, slot, 0);
            mapobj_redraw(x, y, 4, slot, 3, (int)frame + 1, 1);
            o->plane = b->plane == 1 ? 3 : 1;
        }
    }
    o->first_frame = (uint8_t)(frame == 7 ? 2 : frame + 1);
}

/* ---- SAMNMAX (set 6, 0x4366ab) ---------------------------------------------------- */

static void stomp4(const MapObjectRec *o)
{
    structure_destroy(o->x, o->y, 3, 4);
    structure_destroy(o->x, i8(o->y - 1), 3, 4);
    structure_destroy(i8(o->x + 1), o->y, 3, 4);
    structure_destroy(i8(o->x + 1), i8(o->y - 1), 3, 4);
}

/* the step towards the view's front: (vx > 0 ? 1 : -1, vy > 0 ? 1 : -1) */
static void face(MapObjectRec *o)
{
    o->dx = g_state.view_dir_x < 1 ? -1 : 1;
    o->dy = g_state.view_dir_y < 1 ? -1 : 1;
}

/* 0x4366ab */
static void think_set6(MapObjectRec *o, unsigned frame, uint8_t fade)
{
    switch (frame) {
    case 0:
        if (fade == 0) {
            if (rand_range(0, 1) == 0)
                o->fade = 1, o->first_frame = 1;
        } else if (fade == 4 && rand_range(0, 1) == 0) {
            if (rand_range(0, 2) == 0) {
                o->first_frame = 8;
                if (rand_range(0, 3) == 0) {
                    o->fade = 0x15;
                } else {
                    o->first_frame = 8;
                    o->fade = rand_range(0, 1) == 0 ? 2 : 3;
                }
            } else {
                o->fade = 1, o->first_frame = 1;
            }
        }
        break;
    case 1:
        bt_sound(0x10005, 1);
        /* fall through */
    case 2:
    case 3:
    case 4:
    case 5:
        o->first_frame = (uint8_t)(frame + 1);
        face(o);
        break;
    case 6:
        o->first_frame = (uint8_t)(frame + 1);
        o->dx = 0, o->dy = 0;
        stomp4(o);
        break;
    case 7:
        o->first_frame = 0;
        o->dx = 0, o->dy = 0;
        o->fade = 4;
        break;
    case 8:
        o->dx = 0, o->dy = 0;
        if (fade < 0x15) {
            bt_sound(0x10005, 1);
            o->first_frame = fade == 3 ? 0xf : 9;
        } else if (fade == 0x15) {
            o->first_frame = 0x10;
        } else {
            o->first_frame = 0;
            o->fade = 4;
        }
        break;
    case 9:
    case 10:
    case 0xb:
    case 0xc:
    case 0xd:
    case 0xe:
    case 0xf:
        o->dx = 0, o->dy = 0;
        if (g_state.view_dir_x < 1) {
            if (g_state.view_dir_y < 1)
                o->dx = -1;
            else
                o->dy = -1;
        } else if (g_state.view_dir_y < 1) {
            o->dy = 1;
        } else {
            o->dx = 1;
        }
        if (fade == 3) {
            if (frame == 9) {
                o->dx = 0, o->dy = 0;
                o->first_frame = 8;
                o->fade = 0x16;
                stomp4(o);
            } else {
                o->first_frame = (uint8_t)(frame - 1);
            }
            o->dx = i8(-o->dx);
            o->dy = i8(-o->dy);
        } else {
            if (frame == 0xe)
                stomp4(o);
            if (frame > 0xd)
                o->dx = 0, o->dy = 0;
            if (frame == 0xf)
                o->first_frame = 8, o->fade = 0x16;
            else
                o->first_frame = (uint8_t)(frame + 1);
        }
        break;
    case 0x10:
    case 0x11:
    case 0x12:
        o->first_frame = (uint8_t)(frame + 1);
        break;
    case 0x13:
        if (rand_range(0, 1) == 0)
            o->first_frame = 8, o->fade = 0x16;
        else
            o->first_frame = 0x10;
        break;
    default:
        o->first_frame = 0;
        o->fade = 4;
        break;
    }
}

/* ---- Birds of Paradise / Bats out of Hell (sets 0x14 / 0x15) ----------------------- */

static void think_flock(MapObjectRec *o, const MapObjectRec *b)
{
    int8_t x = b->x, y = b->y;
    unsigned frame = b->frame;
    realm_cur()->unk_80fc++;
    if (frame > 7 && frame < 10) {
        /* resting on a special structure */
        g_state.unk_5b2f5c = 0;
        if (--o->fade == 0) {
            send_off(o, x, y, 0x7c);
            return;
        }
    }
    if (frame > 7)
        return;
    g_state.unk_5b2f5c = 0;
    o->delay = 1, o->delay_reset = 1;
    int site = b->plane == 1 ? 5 : 0;
    if (bt_site(site) && bt_site_dist(site, x, y) < 0xb) {
        o->dx = 0, o->dy = 0;
        o->first_frame = 8;
        o->fade = 3;
        bt_sound(0x1001c, 3);
        realm_mark_special_site((unsigned)site);
        return;
    }
    if ((frame & 1) == 0) {
        unsigned d = b->fade;
        if (b->dx == 1)
            d = 1;
        if (b->dx == -1)
            d = 3;
        if (b->dy == 1)
            d = 2;
        if (b->dy == -1)
            d = 4;
        o->first_frame = (uint8_t)(bt_dir_frame(d) + 1);
    } else if (rand_range(0, 10) == 0) {
        int k = rand_range(1, 4);
        o->dx = 0, o->dy = 0;
        switch (k) {
        case 1: o->dx = 1; break;
        case 2: o->dy = 1; break;
        case 3: o->dx = -1; break;
        case 4: o->dy = -1; break;
        }
        g_state.unk_5f25a8 = (uint8_t)bt_dir_frame((unsigned)k);
        o->first_frame = g_state.unk_5f25a8;
        g_state.unk_5f7ed0 = (uint8_t)(g_state.unk_5931f0 * 2);
        bt_sound(g_world.plane == GS_PLANE_HEAVEN ? 0x1000f : 0x10013, 1);
    } else {
        o->first_frame = (uint8_t)(frame - 1);
    }
    if (rand_range(0, 3) != 0)
        return;
    if (!bt_in_map(x, y))
        return;
    /* droppings on the building below */
    uint32_t c = bt_cell(x, y);
    unsigned set = c >> 27;
    uint8_t ox = (uint8_t)x, oy = (uint8_t)y;
    if (set == 0) {
        ox = (uint8_t)(x - ((c >> 16) & 7));
        oy = (uint8_t)(y + ((c & 0x700000) >> 20));
        set = bt_set(i8(ox), i8(oy));
    }
    int size = structure_size(i8(ox), i8(oy)) & 0xff;
    switch (set) {
    case 7:
    case 8:
    case 9:
    case 0xd:
        break;
    case 0xe:
    case 0xf:
        if (zone_entry_is_empty(bt_index(i8(ox), i8(oy))))
            return;
        if (bt_cell(i8(oy), i8(ox)) & 0x20) /* x and y swapped (sic) */
            return;
        break;
    default:
        return;
    }
    /* ENHANCEMENT: stop at row 0. The original's row counter is a byte
     * compared against y - size: for a building in the top rows (y - size
     * == -1) it wraps from 0 to 255 and never ends, so the game hangs. The
     * faithful branch hangs too, but its writes outside the grid go nowhere
     * (bt_cellp, a safety guard that is always on). */
    for (unsigned lx = ox; (int)lx < i8(ox) + size; lx = (lx + 1) & 0xff) {
        if (g_enhancements) {
            for (int ly = oy; i8(oy) - size < ly && ly >= 0; ly--)
                *bt_cellp((int)lx, ly) |= 4u;
        } else {
            for (unsigned ly = oy & 0xffu; i8(oy) - size < (int)ly; ly = (ly - 1) & 0xffu)
                *bt_cellp((int)lx, i8(ly)) |= 4u;
        }
    }
    bt_sound(g_world.plane == GS_PLANE_HEAVEN ? 0x10010 : 0x10014, 1);
    mapobj_redraw(i8(ox), i8(oy), size, 0, 0, 0, 1);
}

/* ---- Hell in a Handbasket (0x18) and the Heaven Nose (0x19) ------------------------ */

/* The two thieves share their logic: one steals in its home realm, flies
 * off the east edge into the other realm and builds the loot there. */
typedef struct Thief {
    int set;                /* 0x18 / 0x19 */
    int home, away;         /* the realms it steals from / drops into */
    uint8_t (*loot)[3];     /* [20] {set, entry, size} */
    uint8_t *tx, *ty, *n;   /* target cell, loot count */
    unsigned last_frame;    /* frames below are flying (4 / 8) */
    uint8_t bit;            /* in g_state.unk_5b2f61 */
    unsigned steal_sets[3]; /* what it takes */
    int steal_site, steal_site_d; /* the special structure guarding the home realm */
    uint32_t snd_steal, snd_drop;
    int drop_entry_shift;   /* +1 (Hell's equivalents of Heaven's buildings) / -1 */
} Thief;

static Thief thief(int set)
{
    if (set == 0x18)
        return (Thief){0x18, 3, 1, g_state.unk_5bb0b0, &g_state.unk_57e098, &g_state.unk_57e09a,
                       &g_state.unk_5bb110, 4, 0x20, {0xf, 8, 0xd}, 3, 0xf, 0x1001c, 0x1001d, 1};
    return (Thief){0x19, 1, 3, g_state.unk_593200, &g_state.unk_638653, &g_state.unk_638654,
                   &g_state.unk_63050c, 8, 0x10, {0xe, 8, 0xd}, 7, 0xd, 0x10067, 0x10068, -1};
}

/* 0x442a48 / 0x442e96: the next target in realm `plane`: a building to
 * take (home realm) or free land for loot[n] (away); (0x82, 0x32) if none */
static void thief_pick(const Thief *t, int plane)
{
    int8_t sx = rand_range(0, 1) == 0 ? 1 : -1;
    int8_t sy = rand_range(0, 1) == 0 ? 1 : -1;
    uint8_t bx = (uint8_t)rand_range(10, 0x5a);
    uint8_t by = (uint8_t)rand_range(10, 0x5a);
    uint8_t n = *t->n;
    if (n < 0x14 && (plane != t->away || t->loot[n][0] != 0)) {
        int8_t ly_dir = g_state.view_dir_y == -1 ? -1 : 0;
        int xs = g_state.view_dir_x == 1;
        int8_t lx = (int8_t)bx, ly = (int8_t)by;
        do {
            do {
                if (plane == t->home) {
                    bool skip = false;
                    if (t->set == 0x18 && bt_site(3) && bt_site_dist(3, lx, ly) < 0x10) {
                        realm_mark_special_site(3);
                        skip = true;
                    }
                    unsigned s = skip ? 0 : bt_set(lx, ly);
                    if (!skip && (s == t->steal_sets[0] || s == t->steal_sets[1] || s == t->steal_sets[2])) {
                        if (t->set == 0x19 && bt_site(7) && bt_site_dist(7, lx, ly) < 0xd) {
                            realm_mark_special_site(7);
                            skip = true;
                        }
                        unsigned e = bt_index(lx, ly);
                        int size;
                        if (!skip && (s != t->steal_sets[0] || !zone_entry_is_empty(e)) &&
                            (size = structure_size(lx, ly) & 0xff) < 4) {
                            t->loot[n][0] = (uint8_t)s;
                            t->loot[n][2] = (uint8_t)size;
                            t->loot[n][1] = (uint8_t)e;
                            *t->tx = (uint8_t)(lx - (3 - size) * xs);
                            *t->ty = (uint8_t)(ly - (3 - size) * ly_dir);
                            return;
                        }
                    }
                } else {
                    bool skip = false;
                    if (t->set == 0x18) {
                        if (bt_site(8) && abs(g_build.special_pos[8][1] - 1 - ly) +
                                                  abs(g_build.special_pos[8][0] + 1 - lx) < 0x10) {
                            realm_mark_special_site(8);
                            skip = true;
                        }
                    } else if (bt_site(3) && bt_site_dist(2, lx, ly) < 0xd) {
                        realm_mark_special_site(7); /* bit 3, site 2's place, marks 7 (sic) */
                        skip = true;
                    }
                    if (!skip) {
                        int size = (int8_t)t->loot[n][2];
                        if (build_area_free(lx, ly, size)) {
                            *t->tx = (uint8_t)(lx - (3 - size) * xs);
                            *t->ty = (uint8_t)(ly - (3 - size) * ly_dir);
                            return;
                        }
                    }
                }
                lx = i8(lx + sx);
                if ((uint8_t)lx == bx && ly == (int8_t)by) {
                    *t->tx = 0x82, *t->ty = 0x32;
                    return;
                }
            } while (lx < 0x60 && lx > 3);
            lx = sx < 1 ? 0x5f : 5;
            ly = i8(ly + sy);
            if (ly > 0x5f)
                ly = 4;
            if (ly < 4)
                ly = 0x5f;
        } while ((uint8_t)lx != bx || ly != (int8_t)by);
    }
    *t->ty = 0x32, *t->tx = 0x82;
}

/* 0x4432e4 / 0x44363a: heads for the target: steps of 3 (1 for the last
 * two cells) along the longer axis, fade = moves */
static void thief_aim(const Thief *t, MapObjectRec *o, int8_t x, int8_t y)
{
    int tx = *t->tx, ty = *t->ty;
    int8_t sx, sy;
    if (x < tx)
        sx = x + 2 < tx ? 3 : 1;
    else
        sx = tx < x - 2 ? -3 : -1;
    if (y < ty)
        sy = y + 2 < ty ? 3 : 1;
    else
        sy = ty < y - 2 ? -3 : -1;
    o->dx = 0, o->dy = 0;
    if (abs(tx - x) < abs(ty - y)) {
        o->dy = sy;
        o->fade = (uint8_t)abs((abs(ty - y) - 1) / sy);
    } else {
        o->dx = sx;
        o->fade = (uint8_t)abs((abs(tx - x) - 1) / sx);
    }
    if (t->set == 0x18) {
        /* 0x4432e4: the result depends on the view's y direction only */
        if (g_state.view_dir_y < 0)
            o->first_frame = o->dx == 0 ? 0 : 2;
        else
            o->first_frame = o->dx == 0 ? 2 : 0;
    } else if (o->dx == 0) {
        o->first_frame = o->dy * g_state.view_dir_y < 1 ? 4 : 0;
    } else {
        o->first_frame = o->dx * g_state.view_dir_y < 1 ? 6 : 2;
    }
}

static void think_thief(int slot, MapObjectRec *o, const MapObjectRec *b)
{
    Thief t = thief(b->set);
    int8_t x = b->x, y = b->y;
    unsigned frame = b->frame;
    int plane = b->plane;
    uint8_t n = *t.n;
    g_state.unk_5b2f5c = 0;
    if (frame < t.last_frame) {
        if (b->fade == 0) {
            if (*t.ty == 0) {
                thief_pick(&t, plane);
                thief_aim(&t, o, x, y);
            } else if ((int)*t.tx == x && (int)*t.ty == y) {
                if (bt_cell(x, y) & 0x20) {
                    thief_pick(&t, plane);
                    thief_aim(&t, o, x, y);
                    goto tail;
                }
                int8_t ly_dir = g_state.view_dir_y == -1 ? -1 : 0;
                uint8_t size = t.loot[n][2];
                uint8_t ox = (uint8_t)((3 - size) * (g_state.view_dir_x == 1) + x);
                uint8_t oy = (uint8_t)((3 - size) * ly_dir + y);
                if (plane == t.home) {
                    if (bt_site(t.steal_site) && bt_site_dist(t.steal_site, x, y) < t.steal_site_d) {
                        realm_mark_special_site((unsigned)t.steal_site);
                        thief_pick(&t, t.home);
                        thief_aim(&t, o, x, y);
                        goto tail;
                    }
                    if (bt_set(ox, oy) == t.loot[n][0] && (int8_t)size == (int8_t)structure_size(ox, oy)) {
                        bt_sound(t.snd_steal, 2);
                        t.loot[n][1] = (uint8_t)bt_index(ox, oy);
                        structure_destroy(ox, oy, 0, 0);
                        o->dx = 0, o->dy = 0;
                        o->first_frame = (uint8_t)(size + t.last_frame - 1);
                        o->fade = 0xc;
                    } else {
                        thief_pick(&t, t.home);
                        thief_aim(&t, o, x, y);
                    }
                } else {
                    bool site;
                    if (t.set == 0x18)
                        site = bt_site(8) && abs(g_build.special_pos[8][1] - 1 - y) +
                                                     abs(g_build.special_pos[8][0] + 1 - x) < 0xf;
                    else
                        site = bt_site(2) && bt_site_dist(2, x, y) < 0xd;
                    if (site) {
                        realm_mark_special_site(t.set == 0x18 ? 8 : 2);
                        thief_pick(&t, plane);
                        thief_aim(&t, o, x, y);
                        goto tail;
                    }
                    if (!build_area_free(i8(ox), i8(oy), (int8_t)size)) {
                        thief_pick(&t, plane);
                        thief_aim(&t, o, x, y);
                    } else {
                        bt_sound(t.snd_drop, 2);
                        unsigned s = t.loot[n][0];
                        int e = (int8_t)t.loot[n][1];
                        switch (s) {
                        case 7:
                        case 8:
                            e += t.drop_entry_shift;
                            break;
                        case 0xd:
                            e += 2 * t.drop_entry_shift;
                            break;
                        case 0xe:
                            if (t.set == 0x19)
                                s = 0xf;
                            break;
                        case 0xf:
                            if (t.set == 0x18)
                                s = 0xe;
                            break;
                        default:
                            break;
                        }
                        if (g_state.view_dir_x < 0)
                            oy = (uint8_t)(oy - (size - 1));
                        if (g_state.view_dir_y < 0)
                            ox = (uint8_t)(size - 1 + ox);
                        build_place(s, (unsigned)(uint8_t)e, (int8_t)size, i8(ox), i8(oy), BUILD_REDRAW | BUILD_RAW);
                        o->dx = 0, o->dy = 0;
                        o->first_frame = (uint8_t)(size + t.last_frame - 1);
                        o->fade = 0xc;
                    }
                }
            } else {
                thief_aim(&t, o, x, y);
            }
        } else {
            o->fade--;
            if (b->dx + x > 0x61) {
                mapobj_mark_cells(x, y, 3, slot, 0);
                mapobj_redraw(x, y, 3, slot, t.set, 1, 1);
                if (plane == t.home) {
                    /* over into the other realm */
                    o->dx = 0, o->dy = 0;
                    o->fade = 0;
                    o->plane = (uint8_t)t.away;
                    *t.ty = 0, *t.tx = 0, *t.n = 0;
                } else if (g_state.unk_5b2f61 & t.bit) {
                    bt_music_fade(0xb4);
                    if (t.set == 0x18)
                        bt_message(0x65f0f4, 0x65f0b0, 0x65f0a8);
                    else
                        bt_message(0x65f09c, 0x65f07c, 0x65f070);
                    g_state.unk_5b2f61 &= (uint8_t)~t.bit;
                }
            }
            o->first_frame = (uint8_t)((frame & 1) == 0 ? frame + 1 : frame - 1);
        }
    }
tail:
    if (frame > t.last_frame - 1 && frame < t.last_frame + 3) {
        /* taking or dropping a building */
        if (b->fade == 0) {
            (*t.n)++;
            thief_pick(&t, plane);
            thief_aim(&t, o, x, y);
        } else {
            if (t.set == 0x19)
                bt_sound(plane == 1 ? 0x10067 : 0x10068, 2);
            o->fade--;
        }
    }
}

/* ---- Paradise Pair-o-Dice (set 0x1a) ---------------------------------------------- */

/* 0x436521: a die rolls from (x, y) by (dx, dy), crushing what it hits
 * (1 in 4 cells; not near the protecting special structure 4) */
static void trample_line(int8_t x, int8_t y, int8_t dx, int8_t dy)
{
    int8_t sy = dy > 0 ? 1 : dy < 0 ? -1 : 0;
    int8_t sx = dx > 0 ? 1 : dx < 0 ? -1 : 0;
    int8_t ax = i8(abs(dx)), ay = i8(abs(dy));
    int n = ax < ay ? ay : ax;
    int ex = 0, ey = 0;
    for (int i = 0; i <= n; i++) {
        if (bt_site_dist(4, x, y) > 0xd && rand_range(0, 3) == 0)
            structure_destroy(x, y, 3, 4);
        ex += ax;
        ey += ay;
        if (n < ex)
            ex -= n, x = i8(x + sx);
        if (n < ey)
            ey -= n, y = i8(y + sy);
    }
}

/* 0x443a21: two dice thrown from (x, y) */
static void dice_throw(int8_t x, int8_t y)
{
    int c = rand_range(8, 10);
    int d = rand_range(-(c / 2), 0);
    if ((int8_t)mapobj_spawn(x, i8(y + 2), 0x1a, 0, d, c, 1, 2, 3, 0xf, 0xffffffffu, 0x1e, 1) > -1)
        g_state.object_count++;
    c = rand_range(8, 10);
    d = rand_range(1, c / 2);
    if ((int8_t)mapobj_spawn(i8(x + 3), i8(y + 2), 0x1a, 1, d, c, 1, 2, 3, 0xf, 0xffffffffu, 0x1e, 1) > -1)
        g_state.object_count++;
    g_state.unk_5931c1 = 0;
    if (g_state.unk_63026c == 0xff)
        g_state.unk_63026c = 0;
}

static void dice_end(MapObjectRec *croupier, uint32_t t, uint32_t s, uint32_t bn)
{
    croupier->delay = 1, croupier->delay_reset = 1;
    send_off(croupier, croupier->x, croupier->y, 0x78);
    bt_music_fade(0xb4);
    bt_message(t, s, bn);
    g_state.object_count--;
    g_state.unk_5b2f61 &= 0xbf;
}

static MapObjectRec *croupier(void)
{
    static MapObjectRec none;
    return g_state.unk_63050d < GS_MAP_OBJECTS ? &g_state.objects[g_state.unk_63050d] : &none;
}

static void think_dice(MapObjectRec *o, const MapObjectRec *b)
{
    int8_t x = b->x, y = b->y, dx = b->dx, dy = b->dy;
    unsigned frame = b->frame;
    if (frame < 6) {
        /* a die rolling */
        bt_sound(0x10020, 2);
        g_state.unk_5b2f5c = 0;
        if (bt_site(4) && bt_site_dist(4, x, y) < 0xf) {
            bt_sound(0x10005, 1);
            o->dx = i8(-dx), o->dy = i8(-dy);
            dx = o->dx, dy = o->dy;
            realm_mark_special_site(4);
        }
        if (dx + x > 0x62)
            o->dx = i8(-dx);
        if (dx + x < 1)
            o->dx = i8(-dx);
        if (dy + y > 0x62)
            o->dy = i8(-dy);
        if (dy + y < 1)
            o->dy = i8(-dy);
        o->fade--;
        trample_line(i8(b->x - b->dx), i8(b->y - b->dy), dx, dy);
        if (o->fade == 0)
            o->first_frame = (uint8_t)rand_range(6, 0xb);
        else if (frame < 5)
            o->first_frame = (uint8_t)(frame + 1);
        else
            o->first_frame = 0;
    }
    if (frame > 5 && frame < 0xc) {
        /* a die showing frame - 5 */
        g_state.unk_5b2f5c = 0;
        if ((int8_t)g_state.unk_59324c > 1) {
            g_state.unk_59324c = 0xff;
            o->delay = 1, o->delay_reset = 1;
            send_off(o, x, y, 0x7c);
            o->fade = 0;
            g_state.object_count--;
            return;
        }
        if (g_state.unk_59324c == 0xff && o->fade != 0) {
            /* both dice are down: the croupier calls the throw */
            o->fade = 0;
            o->delay = 1, o->delay_reset = 1;
            send_off(o, x, y, 0x7c);
            g_state.object_count--;
            uint8_t total = g_state.unk_5931c1;
            if (total == 2 || total == 0xc) {
                g_state.unk_59324c = 0;
                g_state.unk_63026c = 0xff;
                dice_end(croupier(), 0x65eff4, 0x65efc8, 0x65efc0);
                g_state.unk_63050d = 0;
                return;
            }
            bool natural = total == 3 || total == 7 || total == 0xb;
            if (g_state.unk_63026c == 0) {
                if (!natural)
                    g_state.unk_63026c = total;
                g_state.unk_59324c = 0;
                MapObjectRec *c = croupier();
                c->first_frame = 0xc;
                c->delay = 1, c->delay_reset = 1;
                bt_dice_start();
                return;
            }
            if (!natural) {
                if ((int)total == (int8_t)g_state.unk_63026c)
                    g_state.unk_63026c = 0xff;
                g_state.unk_59324c = 0;
                MapObjectRec *c = croupier();
                c->delay = 1, c->delay_reset = 1;
                c->first_frame = 0xc;
                bt_dice_start();
                return;
            }
            g_state.unk_63026c = 0xff;
            g_state.unk_59324c = 0;
            MapObjectRec *c = croupier();
            c->delay = 1, c->delay_reset = 1;
            send_off(c, c->x, c->y, 0x78);
            g_state.unk_63050d = 0;
            bt_music_fade(0xb4);
            bt_message(0x65f04c, 0x65f020, 0x65f018);
            g_state.object_count--;
            g_state.unk_5b2f61 &= 0xbf;
            return;
        }
        if ((int8_t)g_state.unk_59324c >= 0) {
            o->delay = 0x18, o->delay_reset = 0x18;
            o->dx = 0, o->dy = 0;
            if (o->fade == 0) {
                o->fade = 1;
                g_state.unk_5931c1 = (uint8_t)(frame + g_state.unk_5931c1 - 5);
                g_state.unk_59324c++;
            }
        }
    }
    if (frame > 0xb && frame < 0xe) {
        /* the croupier */
        g_state.unk_5b2f5c = 0;
        if (b->fade == 0) {
            if (o->dx == 0) {
                if ((int8_t)mapobj_free_count() < 3) {
                    bt_sound(0x1001f, 2);
                    o->first_frame = frame == 0xc ? 0xd : 0xc;
                } else {
                    o->first_frame = 0xe;
                    dice_throw(x, y);
                }
            } else {
                o->fade = 7;
                o->dx = 0;
            }
        } else {
            bt_sound(0x1001f, 2);
            o->first_frame = frame == 0xc ? 0xd : 0xc;
            o->fade--;
        }
    }
    if (frame > 0xd && frame < 0xf) {
        g_state.unk_5b2f5c = 0;
        o->delay = 0x18, o->delay_reset = 0x18;
    }
}

/* ---- Disco Inferno (set 0x1b) ----------------------------------------------------- */

static void think_disco(MapObjectRec *o, const MapObjectRec *b)
{
    int8_t x = b->x, y = b->y;
    unsigned frame = b->frame;
    g_state.unk_5b2f5c = 0;
    if (frame >= 4) {
        /* the mirror ball on plane 2 */
        if (!(g_state.unk_5b2f61 & 0x80)) {
            if (frame == 4)
                send_off(o, x, y, 0x7c);
            else
                o->first_frame = (uint8_t)(frame - 1);
        } else if (frame != 7) {
            o->first_frame = (uint8_t)(frame + 1);
        }
        return;
    }
    if (frame < 2) {
        if (rand_range(0, 3) == 0) {
            o->dx = 0, o->dy = 0;
            o->first_frame = (uint8_t)(frame + 2);
        } else {
            o->first_frame = frame == 0 ? 1 : 0;
        }
    } else {
        int8_t r = i8(rand_range(-1, 1));
        switch (b->fade) {
        case 1:
            if (rand_range(0, 1) == 0)
                o->dy = 1;
            else
                o->dx = r;
            break;
        case 2:
            if (rand_range(0, 1) == 0)
                o->dy = -1;
            else
                o->dx = r;
            break;
        case 3:
            if (rand_range(0, 1) == 0)
                o->dx = 1;
            else
                o->dy = r;
            break;
        default:
            if (rand_range(0, 1) == 0)
                o->dx = -1;
            else
                o->dy = r;
            break;
        }
        if (bt_site(6) && bt_site_dist(6, x, y) < 0xc) {
            realm_mark_special_site(6);
            bt_sound(0x10005, 1);
            o->dy = i8(-o->dy);
            o->dx = i8(-o->dx);
            switch (b->fade) {
            case 1: o->fade = 2; break;
            case 2: o->fade = 1; break;
            case 3: o->fade = 4; break;
            case 4: o->fade = 3; break;
            }
        }
        o->first_frame = (uint8_t)(frame - 2);
    }
    if (o->dx != 0 || o->dy != 0) {
        x = i8(o->dx + x);
        y = i8(o->dy + y);
        if (x > 0x5f || x < 0 || y > 99 || y < 4) {
            g_state.unk_630484--;
            send_off(o, x, y, 0x7c);
            g_state.object_count--;
            bt_disco_end();
        }
    }
    if (o->dx != 0 || o->dy != 0) {
        /* the dance floor ahead */
        for (int8_t i = 0; i < 4; i++)
            for (int8_t j = 3; j > -1; j--)
                if (i + x < 99 && y - j > -1 &&
                    abs(g_build.special_pos[6][1] - 1 - (y - j)) + abs(g_build.special_pos[6][0] + 1 - (i + x)) > 0xb)
                    structure_destroy(i + x, y - j, 3, 4);
    }
}

/* ---- Dispatch --------------------------------------------------------------------- */

void badthings_object_think(int slot, const MapObjectRec *before)
{
    slot &= 0xff;
    if (slot >= GS_MAP_OBJECTS)
        return;
    MapObjectRec *o = &g_state.objects[slot];
    switch (before->set) {
    case 3:
        think_giant(slot, o, before);
        break;
    case 6:
        think_set6(o, before->frame, before->fade);
        break;
    case 0x14:
    case 0x15:
        think_flock(o, before);
        break;
    case 0x18:
    case 0x19:
        think_thief(slot, o, before);
        break;
    case 0x1a:
        think_dice(o, before);
        break;
    case 0x1b:
        think_disco(o, before);
        break;
    default:
        break;
    }
}
