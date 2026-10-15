/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Bad Things: starting them, the freeze/blues spread, the bankruptcy
 * ending and its fires (WP4). See badthings.h; addresses per function.
 * The map objects' behaviour is in badthing_objects.c.
 */
#include "game/badthings.h"

#include <stddef.h>
#include <string.h>

#include "game/badthings_int.h"
#include "game/mapobj.h"
#include "game/realm.h"
#include "game/structure.h"
#include "game/util.h"

BadThingsHooks g_badthings_hooks;

static int8_t i8(int v) { return (int8_t)v; }

/* ---- Hooks ------------------------------------------------------------------------ */

void bt_sound(uint32_t id, int prio)
{
    if (g_badthings_hooks.sound)
        g_badthings_hooks.sound(id, prio);
}
void bt_music_play(uint32_t id)
{
    if (g_badthings_hooks.music_play)
        g_badthings_hooks.music_play(id);
}
void bt_music_fade(int ms)
{
    if (g_badthings_hooks.music_fade)
        g_badthings_hooks.music_fade(ms);
}
static void sound_unload(uint32_t id)
{
    if (g_badthings_hooks.sound_unload)
        g_badthings_hooks.sound_unload(id);
}
static void sound_stop(uint32_t id)
{
    if (g_badthings_hooks.sound_stop)
        g_badthings_hooks.sound_stop(id);
}
/* map_view_select_plane(&map_view, plane): both halves */
void bt_select_plane(int plane)
{
    world_select_plane(plane);
    if (g_badthings_hooks.select_plane)
        g_badthings_hooks.select_plane(plane);
}
void bt_message(const char *title, const char *text, const char *buttons)
{
    if (g_badthings_hooks.message)
        g_badthings_hooks.message(title, text, buttons);
}

/* ---- Freeze / Blues (0x441b7a-0x4425a1) ------------------------------------------- */

/* 0x441b7a / 0x441bf8: sets / clears CELL_SHADE over a size x size area */
static void shade_area(int x, int y, int size, bool on)
{
    for (int8_t lx = i8(x); lx < size + i8(x); lx++)
        for (int8_t ly = i8(y); i8(y) - size < ly; ly--) {
            uint32_t *c = bt_cellp(lx, ly);
            *c = on ? *c | 1u : *c & ~1u;
        }
}

/* 0x441c76: the ice reaches (x, y); 1 if something froze */
static int freeze_cell(int x, int y)
{
    x = i8(x), y = i8(y);
    if (!bt_in_map(x, y))
        return 0;
    if (g_world.plane == GS_PLANE_HELL) {
        if (bt_site(9) && bt_site_dist(9, x, y) < 0xf) {
            realm_mark_special_site(9);
            return 0;
        }
    } else if (bt_site(1) && bt_site_dist(1, x, y) < 0xc) {
        realm_mark_special_site(1);
        return 0;
    }
    if (cell_test_shade(x, y))
        return 0;
    unsigned set = bt_set(x, y);
    if (set == 0) {
        SimCell *s = bt_sim(x, y);
        x = (uint8_t)s->b[10], y = (uint8_t)s->b[11];
        set = bt_set(i8(x), i8(y));
    }
    if (set == 1) {
        if (bt_index(i8(x), i8(y)) >= 10)
            return 0;
        bool big = false;
        if (i8(x) < 99 && i8(y) > 0) {
            big = true;
            for (int8_t lx = i8(x); lx < i8(x) + 2; lx++)
                for (int8_t ly = i8(y); i8(y) - 2 < ly; ly--)
                    if (bt_set(lx, ly) != 1)
                        big = false;
        }
        if (rand_range(0, 4) != 0)
            big = false;
        if (g_world.plane == GS_PLANE_HELL) {
            if (big) {
                map_stamp_building(i8(x), i8(y), 1, 0xb, 2, true);
                mapobj_redraw(i8(x), i8(y), 2, 0, 1, 1, 1);
            } else {
                cell_set_index(i8(x), i8(y), 10);
                mapobj_redraw(i8(x), i8(y), 1, 0, 1, 1, 1);
            }
        } else {
            cell_set_index(i8(x), i8(y), 0xc);
            mapobj_redraw(i8(x), i8(y), 1, 0, 1, 1, 1);
        }
        return 1;
    }
    if (set == 2)
        return 0;
    int size = structure_size(i8(x), i8(y)) & 0xff;
    uint32_t c = bt_cell(i8(x), i8(y));
    shade_area(x, y, size, true);
    mapobj_redraw(i8(x), i8(y), size, 0, (int)set, (int)((c >> 16) & 0x7f) + 1, 1);
    if (set == 12)
        structure_update_access(i8(x), i8(y), 0);
    return 1;
}

/* the scan order of 0x442122 / 0x442287: a random corner, column by column */
typedef struct Scan {
    int8_t sx, x0, xend, sy, y0, yend;
} Scan;
static Scan scan_dirs(void)
{
    Scan s;
    if (rand_range(0, 1) == 0)
        s.sx = -1, s.x0 = 99, s.xend = -1;
    else
        s.sx = 1, s.x0 = 1, s.xend = 100;
    if (rand_range(0, 1) == 0)
        s.sy = -1, s.y0 = 99, s.yend = -1;
    else
        s.sy = 1, s.y0 = 1, s.yend = 100;
    return s;
}

/* 0x442122 */
bool badthings_spread(void)
{
    uint32_t snd = g_world.plane == GS_PLANE_HELL ? 0x10007 : 0x1000b;
    int total = 0;
    Scan s = scan_dirs();
    int8_t x = s.x0, y = s.y0;
    for (;;) {
        if (cell_test_shade(x, y)) {
            int a = freeze_cell(x - s.sx, y);
            int b = freeze_cell(x, y - s.sy);
            total += a + b;
        }
        y = i8(y + s.sy);
        if (y == s.yend) {
            x = i8(x + s.sx);
            y = s.y0;
            if (x == s.xend)
                break;
        }
    }
    if (total != 0) {
        bt_sound((uint32_t)rand_range((int)snd, (int)snd + 1), 2);
        realm_cur()->unk_80f9++;
    }
    return total != 0;
}

/* 0x442287 */
bool badthings_recover(void)
{
    uint32_t snd = g_world.plane == GS_PLANE_HELL ? 0x10009 : 0x1002f;
    int count = 0;
    Scan s = scan_dirs();
    int8_t x = s.x0, y = s.y0;
    for (;;) {
        unsigned set = bt_set(x, y);
        if (!(bt_cell(x, y) & 1u)) {
            unsigned e = bt_index(x, y);
            if (set == 1 && e > 9 && e < 0xd) {
                count++;
                if (rand_range(0, 5) == 0)
                    structure_destroy(x, y, 0, 1);
            }
        } else {
            count++;
            if (rand_range(0, 5) == 0) {
                /* the scan position itself moves to the origin (sic) */
                if (set == 0) {
                    SimCell *c = bt_sim(x, y);
                    x = (int8_t)c->b[10];
                    y = (int8_t)c->b[11];
                    set = bt_set(x, y);
                }
                uint32_t c = bt_cell(x, y);
                int size = structure_size(x, y) & 0xff;
                shade_area(x, y, size, false);
                mapobj_redraw(x, y, size, 0, (int)set, (int)((c >> 16) & 0x7f) + 1, 1);
                if (set == 12)
                    structure_update_access(x, y, 1);
            }
        }
        y = i8(y + s.sy);
        if (s.yend == y) {
            x = i8(x + s.sx);
            y = s.y0;
            if (s.xend == x)
                break;
        }
    }
    if (count != 0)
        bt_sound((uint32_t)rand_range((int)snd, (int)snd + 1), 2);
    return count != 0;
}

static int count_roads(void)
{
    int n = 0;
    for (int i = 0; i < GS_MAP_CELLS; i++)
        if (g_world.cells[i] >> 27 == 12)
            n++;
    return n;
}

/* 0x4425a1: the ice / blues start on a random road of `plane` */
static void freeze_start(int plane)
{
    uint8_t bit = plane == GS_PLANE_HEAVEN ? 4 : 8;
    bt_select_plane(plane);
    PlaneStats *r = realm_cur();
    if (r->unk_80fa != 0 || r->unk_80f9 != 0) {
        g_state.unk_5b2f61 &= (uint8_t)~bit;
        return;
    }
    int n = count_roads();
    if (n == 0) {
        g_state.unk_5b2f61 &= (uint8_t)~bit;
        return;
    }
    n = rand_range(1, n);
    uint8_t fx = 0, fy = 0;
    unsigned x = 0, y;
    do {
        y = 0;
        do {
            if (cell_tileset((int)x, (int)y) == 12 && --n == 0)
                fx = (uint8_t)x, fy = (uint8_t)y;
            y++;
        } while (y < 100 && n != 0);
        x++;
    } while (x < 100 && n != 0);
    uint32_t c = cell_get(fx, fy);
    int size = structure_size(fx, fy) & 0xff;
    bt_music_play(plane == GS_PLANE_HEAVEN ? 0x10054 : 0x1006e);
    r->unk_80fb = (uint8_t)rand_range(0x19, 0x4b);
    r->unk_80fa = (uint8_t)rand_range(0x14, 0x3c);
    r->unk_80f9 = 1;
    shade_area(fx, fy, size, true);
    mapobj_redraw(fx, fy, size, 0, (int)(c >> 27), (int)((c >> 16) & 0x7f) + 1, 1);
}

/* ---- The other starts ------------------------------------------------------------- */

/* 0x442878 (Handbasket, set 0x18 on Heaven) / 0x442960 (Nose, set 0x19 on Hell) */
static void thief_start(bool nose)
{
    int y = rand_range(5, 0x5a);
    int slot = (int8_t)mapobj_spawn(0, (int8_t)y, nose ? 0x19 : 0x18, 0, 0, 0, 1, 3, nose ? 1 : 3, 0, 0xfffffffdu,
                                    0x19, 1);
    if (slot < 1) {
        g_state.unk_5b2f61 &= nose ? 0xef : 0xdf;
        return;
    }
    if (nose) {
        memset(g_state.unk_593200, 0, sizeof g_state.unk_593200);
        g_state.unk_638654 = 0, g_state.unk_638653 = 0, g_state.unk_63050c = 0;
        sound_stop(0x10055);
        bt_music_play(0x10055);
    } else {
        memset(g_state.unk_5bb0b0, 0, sizeof g_state.unk_5bb0b0);
        g_state.unk_57e09a = 0, g_state.unk_57e098 = 0, g_state.unk_5bb110 = 0;
        sound_stop(0x1006c);
        bt_music_play(0x1006c);
    }
}

/* 0x4438ed */
void bt_dice_start(void)
{
    int8_t x;
    if (g_state.unk_63050d == 0) {
        x = 0;
        g_state.unk_63050d = (uint8_t)mapobj_spawn(0, 4, 0x1a, 0xc, 1, 0, 1, 5, 3, 0xf, 0xffffffffu, 0x1e, 1);
        bt_music_play(0x1006d);
        g_state.object_count++;
    } else {
        x = g_state.unk_63050d < GS_MAP_OBJECTS ? g_state.objects[g_state.unk_63050d].x : 0;
    }
    int r = rand_range(0x14, 0x50);
    if (g_state.unk_63050d >= GS_MAP_OBJECTS)
        return; /* SAFETY (always on): the spawn failed; the original writes past the object table */
    MapObjectRec *d = &g_state.objects[g_state.unk_63050d];
    d->dx = x < r ? 4 : -4;
    d->fade = (uint8_t)(abs(r - x) / 4);
}

/* 0x443b27: Vinnie enters Hell from an edge, the mirror ball appears */
static void disco_start(void)
{
    int x, y, dir;
    if (rand_range(0, 1) == 0) {
        y = rand_range(5, 0x5a);
        if (rand_range(0, 1) == 0)
            x = 0x5f, dir = 4;
        else
            x = 0, dir = 3;
    } else {
        x = rand_range(5, 0x5a);
        if (rand_range(0, 1) == 0)
            y = 99, dir = 2;
        else
            y = 4, dir = 1;
    }
    if ((int8_t)mapobj_spawn(x, y, 0x1b, 0, 0, 0, 1, 4, 1, dir, 0xffffffffu, 0x1e, 1) > 0) {
        bt_music_play(0x10053);
        g_state.object_count++;
        g_state.unk_630484++;
        if (g_badthings_hooks.repaint)
            g_badthings_hooks.repaint();
        if ((int8_t)mapobj_spawn(0x31, 0x32, 0x1b, 4, 0, 0, 4, 2, 2, 0, 0xffffffffu, 0x1e, 1) > 0)
            g_state.object_count++;
    }
}

/* 0x443c58 */
void bt_disco_end(void)
{
    g_state.unk_5b2f61 &= 0x7f;
    bt_music_fade(0xb4);
    bt_message("It's About Time" /* 0x65f950 */, "Vinnie the Disco Inferno guy has boogied out of Hell!" /* 0x65f918 */,
               "&Cool!\0" /* 0x65f910 */);
}

/* 0x445064 */
unsigned bt_dir_frame(unsigned d)
{
    d &= 0xff;
    int8_t vx = g_state.view_dir_x, vy = g_state.view_dir_y;
    static const uint8_t neg_neg[5] = {0, 3, 4, 1, 2}, neg_pos[5] = {0, 3, 2, 1, 4}, pos_neg[5] = {0, 1, 4, 3, 2};
    const uint8_t *t = NULL;
    if (vx < 1)
        t = vy < 0 ? neg_neg : neg_pos;
    else if (vy < 0)
        t = pos_neg;
    if (t && d >= 1 && d <= 4)
        d = t[d];
    return d * 2 - 2;
}

/* 0x4451d7: a flock of 3-8 birds (Heaven, set 0x14) or bats (Hell, 0x15) */
static void flock_start(int plane)
{
    int set = plane == GS_PLANE_HELL ? 0x15 : 0x14;
    uint32_t music = plane == GS_PLANE_HELL ? 0x1006a : 0x1006b;
    g_state.unk_5931f0 = (uint8_t)rand_range(3, 8);
    for (uint8_t i = 1; i <= g_state.unk_5931f0; i++) {
        int x, y, dx = 0, dy = 0, dir;
        if (rand_range(0, 1) == 0) {
            y = rand_range(0x1e, 0x46);
            if (rand_range(0, 1) == 0)
                x = 0x61, dx = -1, dir = 3;
            else
                x = 0, dx = 1, dir = 1;
        } else {
            x = rand_range(0x1e, 0x46);
            if (rand_range(0, 1) == 0)
                dy = -1, y = 99, dir = 4;
            else
                y = 2, dy = 1, dir = 2;
        }
        int f = (int)(bt_dir_frame((unsigned)dir) & 0xff);
        if (plane == GS_PLANE_HEAVEN) {
            world_realm(GS_PLANE_HEAVEN)->unk_80fc++;
            bt_sound(0x1000e, 2);
        } else {
            world_realm(GS_PLANE_HELL)->unk_80fc++;
            bt_sound(0x10012, 2);
        }
        bt_music_play(music);
        mapobj_spawn(x, y, set, f, dx, dy, 1, 3, plane, 0, 0xffffffffu, 0x14, 1);
    }
}

/* 0x444ea5: SAMNMAX */
void badthings_cheat(void)
{
    int8_t vx = g_state.view_dir_x, vy = g_state.view_dir_y;
    for (int k = 0; k < 5; k++) {
        int x, y;
        if (vx < 1) {
            if (vy < 1) {
                if (rand_range(0, 1) == 0)
                    x = 0x62, y = rand_range(0x32, 0x62);
                else
                    x = rand_range(0x32, 0x62), y = 0x62;
            } else if (rand_range(0, 1) == 0) {
                x = rand_range(0x32, 0x62), y = 2;
            } else {
                x = 0x62, y = rand_range(2, 0x31);
            }
        } else if (vy < 1) {
            if (rand_range(0, 1) == 0)
                x = rand_range(2, 0x31), y = 0x62;
            else
                x = 2, y = rand_range(0x32, 0x62);
        } else if (rand_range(0, 1) == 0) {
            y = rand_range(2, 0x31), x = 2;
        } else {
            x = rand_range(2, 0x31), y = 2;
        }
        int plane = rand_range(0, 1) == 0 ? 3 : 1;
        if ((int8_t)mapobj_spawn(x, y, 6, 0, 0, 0, 1, 2, plane, 0, 0xffffffffu, 0x1e, 1) > 0)
            g_state.object_count++;
    }
}

/* ---- bad_thing_start 0x43dc3c ------------------------------------------------------ */

/* message box texts: {title, text, button} */
typedef struct Msg {
    const char *t, *s, *b;
} Msg;
static const Msg k_started[BAD_THINGS] = {
    {"Trouble in Paradise!" /* 0x65f188 */, "The Birds of Paradise are flying over Heaven!" /* 0x65f158 */,
     "&Oh no!\0" /* 0x65f14c */},
    {"Hellzapoppin!" /* 0x65f218 */, "The Bats Out Of Hell have been unleashed!" /* 0x65f1ec */,
     "&Argh!\0" /* 0x65f1e4 */},
    {"Heaven's got a Problem!" /* 0x65f308 */, "Heaven is getting the Blues!" /* 0x65f2e8 */,
     "&Darn!\0" /* 0x65f2e0 */},
    {"Brrrrrr!" /* 0x65f3d4 */, "Hell is Freezing Over!" /* 0x65f3bc */, "&Rats!\0" /* 0x65f3b4 */},
    {"Proboscis Problems" /* 0x65f47c */, "The Heaven Nose is sniffing up everything in sight!" /* 0x65f448 */,
     "&Nertz!\0" /* 0x65f43c */},
    {"Tsk, tsk, tsk..." /* 0x65f524 */, "Heaven is Going to Hell in a Handbasket!" /* 0x65f4f8 */,
     "&Bogus!\0" /* 0x65f4ec */},
    {"Snake Eyes..." /* 0x65f5d0 */, "The Paradise Pair of Dice are thundering across Heaven!" /* 0x65f598 */,
     "&Craps!\0" /* 0x65f58c */},
    {"Burn Baby, Burn..." /* 0x65f66c */, "Hell is being assaulted by a Disco Inferno!" /* 0x65f640 */,
     "&Oy...\0" /* 0x65f638 */},
};
/* "not now" (for 2 and 3: it's already there) */
static const Msg k_refused[BAD_THINGS] = {
    {"Not now..." /* 0x65f140 */, "The Birds of Paradise are currently unavailable!" /* 0x65f10c */,
     "&Phooey!\0" /* 0x65f100 */},
    {"Hold it!" /* 0x65f1d8 */, "The Bats out of Hell are currently unavailable!" /* 0x65f1a8 */,
     "&Wah!\0" /* 0x65f1a0 */},
    {"Oh, for Pete's sake..." /* 0x65f264 */, "Heaven's already got the Blues right now." /* 0x65f238 */,
     "That &Bites!\0" /* 0x65f228 */},
    {"The devil you say..." /* 0x65f350 */, "Hell's already frozen enough right now." /* 0x65f328 */,
     "&Wah!\0" /* 0x65f320 */},
    {"It snot funny..." /* 0x65f428 */, "The Heaven Nose is running amok somewhere else right now." /* 0x65f3ec */,
     "&Boogers!\0" /* 0x65f3e0 */},
    {"Wicker woes..." /* 0x65f4dc */, "Heaven doesn't want to go to Hell in a Handbasket right now." /* 0x65f49c */,
     "&Ieeee!\0" /* 0x65f490 */},
    {"Lucky devil..." /* 0x65f57c */, "The Paradise Pair of Dice don't wanna roll right now." /* 0x65f544 */,
     "&Hooplah!\0" /* 0x65f538 */},
    {"Bummer..." /* 0x65f62c */, "Hell's scene isn't hip enough for the Disco Inferno right now." /* 0x65f5ec */,
     "&Shucks!\0" /* 0x65f5e0 */},
};
/* the realm has no roads to freeze */
static const Msg k_no_roads[2] = {
    {"A momentary lapse of mojo..." /* 0x65f2c0 */,
     "Your Heaven is incapable of getting the Blues right now." /* 0x65f284 */, "&Ok.\0" /* 0x65f27c */},
    {"It's hot, hot, hot..." /* 0x65f39c */, "Your Hell is too tiny to freeze right now." /* 0x65f370 */,
     "&Cool.\0" /* 0x65f368 */}};

static void msg(const Msg *m) { bt_message(m->t, m->s, m->b); }

static void unload4(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    sound_unload(a), sound_unload(b), sound_unload(c), sound_unload(d);
}

void bad_thing_start(int which)
{
    bool verbose = which >= 0;
    if (which < 0)
        which = -which;
    if (g_state.bad_things == 0)
        return;
    uint8_t *bits = &g_state.unk_5b2f61;
    int free = (int8_t)mapobj_free_count();
    switch (which) {
    case BAD_BIRDS:
    case BAD_BATS:
    case BAD_NOSE:
    case BAD_HANDBASKET:
    case BAD_DICE:
    case BAD_DISCO: {
        /* Heaven's kinds exclude each other (0x71), Hell's too (0xb2) */
        bool heaven = which == BAD_BIRDS || which == BAD_HANDBASKET || which == BAD_DICE;
        uint8_t mask = heaven ? 0x71 : 0xb2;
        if ((*bits & mask) != 0 || free <= (which == BAD_DICE ? 3 : 1)) {
            if (verbose)
                msg(&k_refused[which]);
            break;
        }
        bt_music_fade(0xb4);
        msg(&k_started[which]);
        if (which != BAD_DISCO)
            *bits |= (uint8_t)(1u << which);
        switch (which) {
        case BAD_BIRDS:
            flock_start(GS_PLANE_HEAVEN);
            break;
        case BAD_BATS:
            flock_start(GS_PLANE_HELL);
            break;
        case BAD_NOSE:
            thief_start(true);
            unload4(0x1001c, 0x1001d, 0x1001f, 0x10020);
            break;
        case BAD_HANDBASKET:
            thief_start(false);
            unload4(0x10067, 0x10068, 0x1001f, 0x10020);
            break;
        case BAD_DICE:
            bt_dice_start();
            unload4(0x1001c, 0x1001d, 0x10067, 0x10068);
            break;
        default:
            disco_start();
            *bits |= 0x80;
            unload4(0x1001c, 0x1001d, 0x10067, 0x10068);
            break;
        }
        break;
    }
    case BAD_BLUES:
    case BAD_FREEZE: {
        bool hell = which == BAD_FREEZE;
        uint8_t bit = hell ? 8 : 4;
        if (*bits & bit) {
            if (verbose)
                msg(&k_refused[which]);
            break;
        }
        bt_select_plane(hell ? GS_PLANE_HELL : GS_PLANE_HEAVEN);
        if (count_roads() == 0) {
            if (verbose)
                msg(&k_no_roads[hell]);
            break;
        }
        bt_music_fade(0xb4);
        msg(&k_started[which]);
        *bits |= bit;
        freeze_start(hell ? GS_PLANE_HELL : GS_PLANE_HEAVEN);
        sound_unload(hell ? 0x10009 : 0x1002f);
        sound_unload(hell ? 0x1000a : 0x10030);
        break;
    }
    default:
        bt_message("Somebody Call Mike..." /* 0x65f6ac */, "This is an unknown disaster!" /* 0x65f68c */,
                   "&Kewl...\0" /* 0x65f680 */);
        break;
    }
}

/* ---- The bankruptcy (0x444d23, 0x4442dd, 0x4440b3, 0x443c9e) ----------------------- */

/* 0x444d23 */
void badthings_bankruptcy(void)
{
    uint8_t (*w)[6] = g_state.unk_5f4cd0;
    int8_t vx = g_state.view_dir_x, vy = g_state.view_dir_y;
    w[0][0] = 3;
    bt_music_fade(0xb4);
    if (g_badthings_hooks.news)
        g_badthings_hooks.news(0x36b);
    if (g_badthings_hooks.helpers_off)
        g_badthings_hooks.helpers_off();
    bt_music_play(0x10052);
    for (int i = 0; i < 4; i++)
        w[i][1] = 0, w[i][2] = 1;
    w[0][1] = (uint8_t)rand_range(0xc, 0x58);
    if (vy < 0)
        w[0][2] = 99;
    w[0][5] = 6;
    w[1][1] = (uint8_t)rand_range(0xc, 0x58);
    if (vy < 0)
        w[1][2] = 99;
    w[1][0] = 1;
    w[1][5] = 6;
    w[2][2] = (uint8_t)rand_range(0xc, 0x58);
    if (vx < 0)
        w[2][1] = 0x62;
    w[2][0] = 3;
    w[2][5] = 6;
    w[3][2] = (uint8_t)rand_range(0xc, 0x58);
    if (vx < 0)
        w[3][1] = 0x62;
    w[3][0] = 1;
    w[3][5] = 6;
}

/* 0x4440b3: burns the size x size area at (x, y) (roads only if entry > 3)
 * and, for entry > 3, stamps a set-2 tile `entry` (scorched ground) there;
 * for entry <= 3 the burnt cells are left burning (sim +1 0xf5..0xfe). */
static void burn_area(int x, int y, int entry, int size, unsigned snd)
{
    x = i8(x), y = i8(y);
    entry &= 0xff, size &= 0xff;
    bool any = false;
    for (int8_t lx = (int8_t)x; lx < size + x; lx++)
        for (int8_t ly = (int8_t)y; y - size < ly; ly--) {
            if (!bt_in_map(lx, ly))
                continue;
            bool hit = cell_origin_tileset(lx, ly) != 2 || cell_origin_index(lx, ly) > 3 || entry > 3;
            any = any || hit;
            if (hit) {
                structure_destroy(lx, ly, snd, 3);
                if (entry < 4)
                    sim_cell(lx, ly)->b[1] = (uint8_t)rand_range(0xf5, 0xfe);
            }
        }
    if (any && entry > 3 && x >= 0 && size - 1 < y && x <= 100 - size && y < 100) {
        map_stamp_building(x, y, 2, (unsigned)entry, size, true);
        mapobj_redraw(x, y, size, 0, 2, 0x3f, 0);
    }
}

static void burn_small(int x, int y)
{
    int e = rand_range(0, 3);
    burn_area(x, y, e, 1, 7);
}

/* 0x4442dd: walker 0 of 0x5f4cd0 (the others are set up but never move) */
void badthings_doom_step(void)
{
    int saved = g_world.plane;
    uint8_t *w = g_state.unk_5f4cd0[0];
    int8_t vx = g_state.view_dir_x, vy = g_state.view_dir_y;
    if ((int8_t)w[3] == vx || (int8_t)w[4] == vy) {
        if (w[5] == 0) {
            w[5] = (uint8_t)rand_range(3, 8);
            if (w[3] == 0 || w[4] == 0) {
                w[3] = (uint8_t)vx, w[4] = (uint8_t)vy;
            } else if (rand_range(0, 1) == 0) {
                w[4] = (uint8_t)vy, w[3] = 0;
            } else {
                w[3] = (uint8_t)vx, w[4] = 0;
            }
        } else {
            w[5]--;
            if (rand_range(0, 0x1b) == 0)
                bt_sound(rand_range(0, 1) == 0 ? 0x10032 : 0x10033, 1);
        }
    } else {
        w[3] = (uint8_t)vx, w[4] = (uint8_t)vy;
    }
    bt_select_plane(w[0]);
    int8_t cx = (int8_t)w[1], cy = (int8_t)w[2], ddx = (int8_t)w[3], ddy = (int8_t)w[4];
    int8_t ox = 0, oy = 0;
    if (ddx == 0)
        ox = i8(vx * 2);
    if (ddy == 0)
        oy = i8(vy * 2);
    /* the band ahead */
    int8_t px = i8(cx + ox - vx);
    int8_t n = i8(abs(ox) + 8);
    do {
        int8_t py = i8(cy + oy + vy * -2);
        int8_t m = i8(abs(oy) + 8);
        do {
            if (bt_in_map(px, py)) {
                if (cell_origin_tileset(px, py) != 2 || cell_origin_index(px, py) > 3) {
                    structure_destroy(px, py, 4, 3);
                    sim_cell(px, py)->b[1] = (uint8_t)rand_range(0xf5, 0xfe);
                }
            }
            py = i8(py - vy);
        } while (--m != 0);
        px = i8(px - vx);
    } while (--n != 0);
    /* the scorched trail behind */
    int8_t X = cx, Y = cy;
    if (ddx == 0) {
        burn_area(X, Y, 0x40, 2, 7);
        X = i8(cx - ox);
        burn_area(X, Y, 0x3e, 2, 7);
        int8_t c8 = i8(cx - ox - ox);
        X = c8;
        burn_area(X, Y, 0x3d, 2, 7);
        c8 = i8(c8 - ox);
        X = c8;
        burn_area(X, Y, 0x3f, 2, 7);
        c8 = i8(c8 - vx);
        X = c8;
        burn_small(X, Y);
        Y = i8(cy - vy);
        burn_small(X, Y);
        X = i8(c8 - vx);
        Y = i8(cy - vy + vy);
        burn_small(X, Y);
    }
    if (ddy == 0) {
        burn_area(X, Y, 0x3b, 2, 7);
        Y = i8(Y - oy);
        burn_area(X, Y, 0x39, 2, 7);
        Y = i8(Y - oy);
        burn_area(X, Y, 0x3a, 2, 7);
        Y = i8(Y - oy);
        burn_area(X, Y, 0x3c, 2, 7);
        Y = i8(Y - oy);
        burn_small(X, Y);
        X = i8(X + vx);
        burn_small(X, Y);
        X = i8(X - vx);
        Y = i8(Y - vy);
        burn_small(X, Y);
    } else if (ddx != 0) {
        burn_area(X, Y, 0x36, 2, 7);
        Y = i8(Y + vy * -2);
        burn_area(X, Y, 0x3a, 2, 7);
        Y = i8(Y + vy * -2);
        burn_area(X, Y, 0x3c, 2, 7);
        Y = i8(Y + vy * -2);
        burn_area(X, Y, 0x35, 2, 7);
        Y = i8(Y + vy * -2);
        burn_small(X, Y);
        X = i8(X + vx);
        burn_small(X, Y);
        X = i8(X - vx);
        Y = i8(Y - vy);
        burn_small(X, Y);
        Y = cy;
        int8_t c13 = i8(cx + vx * -2);
        X = c13;
        burn_area(X, Y, 0x3d, 2, 7);
        c13 = i8(c13 + vx * -2);
        X = c13;
        burn_area(X, Y, 0x3f, 2, 7);
        c13 = i8(c13 + vx * -2);
        X = c13;
        burn_area(X, Y, 0x34, 2, 7);
        c13 = i8(c13 - vx);
        X = c13;
        burn_small(X, Y);
        Y = i8(cy - vy);
        burn_small(X, Y);
        X = i8(c13 - vx);
        Y = i8(cy - vy + vy);
        burn_small(X, Y);
    }
    if (g_badthings_hooks.window_update)
        g_badthings_hooks.window_update();
    w[1] = (uint8_t)(w[1] + w[3] * 2);
    w[2] = (uint8_t)(w[2] + w[4] * 2);
    if ((int8_t)w[1] < -10 || (int8_t)w[1] > 'm' || (int8_t)w[2] < -10 || (int8_t)w[2] > 'm') {
        w[1] = 0, w[2] = 0;
        if (rand_range(0, 1) == 0) {
            w[2] = (uint8_t)rand_range(0xc, 0x58);
            if (g_state.view_dir_x < 0)
                w[1] = 99;
        } else {
            w[1] = (uint8_t)rand_range(0xc, 0x58);
            if (g_state.view_dir_y < 0)
                w[2] = 99;
        }
        w[0] = w[0] == 1 ? 3 : 1;
    }
    bt_select_plane(saved);
}

/* 0x443c9e: burning cells (set 2, sim +1 > 0xf4) burn down one step and
 * may set a neighbouring structure on fire; at most 20 per realm */
void badthings_fire_step(void)
{
    int saved = g_world.plane;
    for (int p = GS_PLANE_HELL; p < 4; p += 2) {
        bt_select_plane(p);
        int budget = 0x14;
        for (int8_t x = 0; x < 100; x++)
            for (int8_t y = 0; y < 100; y++) {
                uint8_t b;
                if (cell_tileset(x, y) == 2 && (b = sim_cell(x, y)->b[1]) > 0xf4 && rand_range(0, 0x14) == 0) {
                    uint8_t nb = (uint8_t)(b - 1);
                    bool spread = false;
                    for (int8_t nx = i8(x - 1); nx <= x + 1; nx++) {
                        if (nx < 0 || nx >= 100)
                            continue;
                        for (int8_t ny = i8(y - 1); ny <= y + 1; ny++) {
                            if (ny < 0 || ny >= 100)
                                continue;
                            unsigned s = bt_set(nx, ny);
                            int8_t ox = nx, oy = ny;
                            if (s == 0) {
                                SimCell *c = bt_sim(nx, ny);
                                ox = (int8_t)c->b[10], oy = (int8_t)c->b[11];
                                s = bt_set(ox, oy);
                            }
                            unsigned e = bt_index(ox, oy);
                            if (!(bt_cell(ox, oy) & 0x20) && (s != 2 || (e > 3 && e < 0x34))) {
                                spread = true;
                                int size = structure_size(ox, oy) & 0xff;
                                structure_destroy(ox, oy, 3, 3);
                                budget--;
                                for (int8_t lx = ox; lx < size + ox; lx++)
                                    for (int8_t ly = oy; oy - size < ly; ly--)
                                        bt_sim(lx, ly)->b[1] = nb;
                                nx = i8(x + 3);
                                ny = i8(y + 3);
                            }
                        }
                    }
                    sim_cell(x, y)->b[1] = spread ? nb : 0xf4;
                }
                if (budget < 1)
                    x = 'n', y = 'n';
            }
    }
    bt_select_plane(saved);
}
