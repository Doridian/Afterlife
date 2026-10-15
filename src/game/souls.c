/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * SOUL flow: groups, the road walk and the buildings that take SOULs
 * (ALIFE.EXE 0x458490-0x45e9a8). Overview in game/souls.h; the zone scan,
 * arrivals and area search are in zones.c.
 *
 * Faithful to the original, including its integer quirks (all counters
 * are unsigned 32-bit and wrap), the order of every random draw, and a few
 * apparent bugs that are marked "(sic)".
 */
#include "game/souls.h"

#include <stdlib.h>
#include <string.h>

#include "game/build.h"
#include "game/mapobj.h"
#include "game/realm.h"
#include "game/souls_int.h"
#include "game/soultrack.h"
#include "game/structure.h"
#include "game/tables.h"
#include "game/util.h"
#include "game/world.h"

uint32_t souls_to_heaven; /* 0x65fe54 */
uint32_t souls_to_hell;   /* 0x65fe58 */
SoulsStats souls_stats;

/* ---- The group being walked (0x45e1fc and below; not saved) ------------ */

static uint32_t walk_total;     /* 0x639070: SOULs of the group still unplaced */
static uint32_t walk_cat[16];   /* 0x639074: ... per category */
static uint16_t walk_track;     /* 0x6390d6: categories of tracked SOULs (Soulview) */
static uint8_t walk_cat_age[9]; /* 0x6390c0: group age when a category was settled */
static uint8_t walk_no_merge;   /* 0x6390b4: always 0 in ALIFE.EXE */
static uint8_t walk_start_x;    /* 0x6390d4 */
static uint8_t walk_start_y;    /* 0x6390d5 */
static uint32_t walk_best_n;    /* 0x639058: largest share lost (marker sprite) */
static uint32_t walk_best_cat;  /* 0x6390d8 */
static int32_t walk_depth;      /* 0x63905c: road walk recursion depth */
static int32_t walk_max_depth;  /* 0x6390b8 */

/* Statistics the original keeps but never reads. */
static uint32_t stat_yields WRITE_ONLY;        /* 0x639060 */
static uint32_t stat_zone_visits;   /* 0x6390d0 */
static uint32_t stat_walks;         /* 0x6390dc */
static uint32_t stat_groups_walked; /* 0x6390cc */
static uint8_t stat_65fe5c;         /* 0x65fe5c */

/* souls_flow's resume point (0x65fe40 x, 0x65fe44 y) and done flag. */
static uint8_t flow_x, flow_y;
static uint32_t flow_done WRITE_ONLY = 1; /* 0x65fe3c */

static SoulGroup *grp(unsigned g) { return &g_state.soul_groups[g & (GS_SOUL_GROUPS - 1)]; }
static int32_t grp_next(unsigned g) { return (int32_t)grp(g)->next; }

static uint32_t umin(uint32_t a, uint32_t b) { return a < b ? a : b; }

/* ---- Small sim-cell accessors (0x458592-0x45866a, 0x45aea5-0x45af7e) -----
 * Road cells (set 12): +1 low half = mark of the last group that walked
 * the cell. Gates (set 13): +9 low half = the same mark. Set-9 buildings:
 * +9 high half = the group that reached it, +9 low half its mark. */

/* 0x458592 */
static unsigned road_mark(int x, int y) { return sc_u32(x, y, 1) & 0xffff; }
/* 0x4585ce */
static unsigned gate_mark(int x, int y) { return sc_u32(x, y, 9) & 0xffff; }
/* 0x45860a */
static void road_mark_set(int x, int y, unsigned v) { sc_set_u32(x, y, 1, (sc_u32(x, y, 1) & 0xffff0000u) | v); }
/* 0x45866a */
static void gate_mark_set(int x, int y, unsigned v) { sc_set_u32(x, y, 9, (sc_u32(x, y, 9) & 0xffff0000u) | v); }
/* 0x45aea5 */
static unsigned portal_group(int x, int y) { return sc_u32(x, y, 9) >> 16; }
/* 0x45aedf */
static unsigned portal_mark(int x, int y) { return sc_u32(x, y, 9) & 0xffff; }
/* 0x45af1b */
static void portal_group_set(int x, int y, unsigned g)
{
    sc_set_u32(x, y, 9, (sc_u32(x, y, 9) & 0xffff) | (uint32_t)g << 16);
}
/* 0x45af7e */
static void portal_mark_set(int x, int y, unsigned v) { sc_set_u32(x, y, 9, (sc_u32(x, y, 9) & 0xffff0000u) | v); }

/* The map object slot of the "SOULs lost here" marker of a category
 * (0x5f7400[], saved; index 8 is 0x5f7408, a slot game_reset_world
 * allocates at runtime). */
static uint8_t marker_frame(unsigned i) { return i < 8 ? g_state.unk_5f7400[i] : g_build.obj_5f7408; }

/* ---- The transit ledger and the arrival statistics ---------------------- */

/* 0x4599f4: n SOULs of category cat (0..13) settle: they leave the ledger
 * row cat (taken from its belief columns in proportion) and are counted
 * in hist_b[kind][j].unk_04 (arrived this pass; 0x4591bf moves them on). */
static void ledger_take(unsigned cat, uint32_t n)
{
    if (n == 0)
        return;
    PlaneStats *r = realm_cur();
    unsigned kind = cat < 7 ? cat : cat - 7;
    PlaneStatRow *row = &r->unk_7a08[cat];
    uint32_t total = row->unk_00;
    uint32_t done = 0;
    for (int j = 0; j < 16; j++) {
        uint32_t have = row->v[j];
        if (have == 0)
            continue;
        uint32_t t = mul_div_clamp(have, total, n);
        if (have < t)
            t = have;
        r->hist_b[kind * 16 + j].unk_04 += t;
        if (row->unk_00 < t) {
            done += row->unk_00;
            row->v[j] -= row->unk_00;
            row->unk_00 = 0;
        } else {
            done += t;
            row->v[j] -= t;
            row->unk_00 -= t;
        }
    }
    uint32_t rest = n - done;
    if (rest == 0)
        return;
    for (int j = 0; j < 16; j++) {
        uint32_t have = row->v[j];
        if (have == 0)
            continue;
        uint32_t *arr = &r->hist_b[kind * 16 + j].unk_04;
        if (have < rest) {
            if (row->unk_00 < have) {
                row->v[j] = 0;
                *arr += row->unk_00;
                rest -= row->unk_00;
                row->unk_00 = 0;
            } else {
                row->v[j] = 0;
                *arr += have;
                row->unk_00 -= have;
                rest -= have;
            }
        } else {
            if (row->unk_00 < rest) {
                row->v[j] = 0;
                *arr += row->unk_00;
                row->unk_00 = 0;
            } else {
                row->v[j] -= rest;
                *arr += rest;
                row->unk_00 -= rest;
            }
            break;
        }
    }
}

/* Moves t SOULs of hist_b[k][j] age bucket b into the residents (hist_a)
 * and the realm/global totals (one step of 0x4591bf). */
static void settle_move(PlaneStats *r, int k, int j, int b, uint32_t t)
{
    PlaneStatHist *hb = &r->hist_b[k * 16 + j], *ha = &r->hist_a[k * 16 + j];
    hb->unk_04 -= t;
    hb->unk_00 -= t;
    hb->v[b] -= t;
    ha->unk_00 += t;
    ha->v[b] += t;
    r->unk_7820[k] += t;
    g_state.unk_5fa600[k] -= t;
    r->unk_77e0[j] += t;
    g_state.unk_5b2f10[j] -= t;
}

/* 0x4591bf: end of the flow. The SOULs that settled this pass
 * (hist_b[k][j].unk_04 of the hist_b.unk_00 expected) move from hist_b
 * into hist_a, keeping their age buckets: in proportion, then the rest
 * from the first buckets. */
static void settle_arrivals(void)
{
    PlaneStats *r = realm_cur();
    for (int k = 0; k < 7; k++) {
        for (int j = 0; j < 16; j++) {
            PlaneStatHist *hb = &r->hist_b[k * 16 + j];
            uint32_t expect = hb->unk_00;
            uint32_t arrived = hb->unk_04;
            uint32_t done = 0;
            if (arrived != 0) {
                for (int b = 0; b < 32; b++) {
                    uint32_t v = hb->v[b];
                    if (v == 0)
                        continue;
                    if (expect != arrived) {
                        uint32_t t = mul_div_clamp(v, expect, arrived);
                        if (arrived < t + done)
                            t = arrived - done;
                        if (v < t)
                            t = v;
                        settle_move(r, k, j, b, t);
                        done += t;
                    } else {
                        settle_move(r, k, j, b, v);
                        hb->v[b] = 0;
                        done += v;
                    }
                }
                if (expect != arrived && done <= arrived && (arrived -= done) != 0) {
                    for (int b = 0; b < 32; b++) {
                        uint32_t v = hb->v[b];
                        if (v == 0)
                            continue;
                        if (v < arrived) {
                            settle_move(r, k, j, b, v);
                            hb->v[b] = 0;
                            arrived -= v;
                        } else {
                            settle_move(r, k, j, b, arrived);
                            break;
                        }
                    }
                }
            }
            hb->unk_04 = 0;
        }
    }
}

/* ---- Lost SOULs ------------------------------------------------------------ */

static void mask_clear(unsigned g, unsigned cat) { grp(g)->mask &= (uint16_t)~(1u << cat); }

/* 0x4586ca: up to n of the walking group's SOULs wait in the realm's
 * siphons (capacity unk_7f34, held unk_7f38, by category unk_7ef0[]);
 * if all fit, the whole group goes in. Returns n as clamped to the room
 * (the original's return; not what was moved). */
static uint32_t siphon_store_(unsigned g, uint32_t n);
static uint32_t siphon_store(unsigned g, uint32_t n)
{
    uint32_t before = walk_total;
    uint32_t r = siphon_store_(g, n);
    souls_stats.walk_siphons += before - walk_total;
    return r;
}
static uint32_t siphon_store_(unsigned g, uint32_t n)
{
    PlaneStats *r = realm_cur();
    uint32_t total0 = walk_total;
    bool all = false;
    if (r->unk_7f34 == 0)
        return 0;
    if (r->unk_7f34 < r->unk_7f38 + n) {
        n = r->unk_7f34 - r->unk_7f38;
        if (n == 0)
            return 0;
    } else {
        all = true;
    }
    if (walk_total == 0)
        return 0;
    uint32_t moved = 0;
    for (unsigned k = 0; k < 7; k++) {
        uint32_t got = 0;
        if (all) {
            if (walk_cat[k] != 0) {
                r->unk_7ef0[k] += walk_cat[k];
                ledger_take(k, walk_cat[k]);
            }
            if (walk_cat[k + 7] != 0) {
                r->unk_7ef0[k + 7] += walk_cat[k + 7];
                ledger_take(k + 7, walk_cat[k + 7]);
            }
            got = walk_cat[k + 7] + walk_cat[k];
            r->unk_7f38 += walk_cat[k + 7] + walk_cat[k];
            walk_cat[k] = 0;
            walk_cat[k + 7] = 0;
        } else {
            uint32_t t;
            if (walk_cat[k] != 0 && (t = mul_div_clamp(walk_cat[k], total0, n)) != 0) {
                moved += t;
                r->unk_7ef0[k] += t;
                ledger_take(k, t);
                r->unk_7f38 += t;
                walk_cat[k] -= t;
                got = t;
                if (walk_cat[k] == 0)
                    mask_clear(g, k);
            }
            if (walk_cat[k + 7] != 0 && (t = mul_div_clamp(walk_cat[k + 7], total0, n)) != 0) {
                got += t;
                moved += t;
                r->unk_7ef0[k + 7] += t;
                ledger_take(k + 7, t);
                r->unk_7f38 += t;
                walk_cat[k + 7] -= t;
                if (walk_cat[k + 7] == 0)
                    mask_clear(g, k + 7);
            }
        }
        if (walk_best_n < got) {
            walk_best_n = got;
            walk_best_cat = k;
        }
    }
    if (all) {
        r->unk_7f38 += walk_cat[14] + walk_cat[15];
        r->unk_7ef0[14] += walk_cat[14];
        r->unk_7ef0[15] += walk_cat[15];
        walk_cat[14] = walk_cat[15] = 0;
    } else {
        if (walk_cat[14] != 0) {
            uint32_t t = mul_div_clamp(walk_cat[14], total0, n);
            moved += t;
            r->unk_7ef0[14] += t;
            r->unk_7f38 += t;
            walk_cat[14] -= t;
            if (walk_cat[14] == 0)
                mask_clear(g, 14);
            if (walk_best_n < t) {
                walk_best_cat = 7;
                walk_best_n = t;
            }
        }
        if (walk_cat[15] != 0) {
            uint32_t t = mul_div_clamp(walk_cat[15], total0, n);
            moved += t;
            r->unk_7ef0[15] += t;
            r->unk_7f38 += t;
            walk_cat[15] -= t;
            if (walk_cat[15] == 0)
                mask_clear(g, 15);
        }
    }
    /* 0x47d67d (Soulview) is called here; it does nothing. */
    r->unk_7f38 = umin(r->unk_7f38, r->unk_7f34);
    if (all)
        walk_total = 0;
    else
        walk_total -= umin(moved, walk_total);
    return n;
}

/* 0x458cf1: n of the walking group's SOULs (at road cell (x, y)) found
 * nowhere to go. Siphons take what they can; the rest are lost
 * (unk_809f[cat], unk_8113, unk_626598 -> lost_souls). */
static void souls_lose(int x, int y, unsigned g, uint32_t n)
{
    PlaneStats *r = realm_cur();
    walk_best_n = 0;
    walk_best_cat = 0;
    uint32_t stored = siphon_store(g, n);
    uint32_t total = walk_total;
    uint32_t before = walk_total;
    uint32_t lost = n - umin(stored, n);
    bool part = walk_total != lost;
    uint32_t sum = 0;
    if (lost != 0) {
        for (unsigned k = 0; k < 7; k++) {
            soultrack_lost((int8_t)x, (int8_t)y, g, k);
            uint32_t c;
            if (part) {
                c = 0;
                if (walk_cat[k] != 0) {
                    c = mul_div_clamp(lost, total, walk_cat[k]);
                    walk_cat[k] -= c;
                    if (walk_cat[k] == 0)
                        mask_clear(g, k);
                }
                if (walk_cat[k + 7] != 0) {
                    uint32_t t = mul_div_clamp(lost, total, walk_cat[k + 7]);
                    c += t;
                    walk_cat[k + 7] -= t;
                    if (walk_cat[k + 7] == 0)
                        mask_clear(g, k + 7);
                }
            } else {
                c = walk_cat[k + 7] + walk_cat[k];
                walk_cat[k] = walk_cat[k + 7] = 0;
            }
            r->unk_809f[k] += c;
            sum += c;
            if (walk_best_n < c) {
                walk_best_n = c;
                walk_best_cat = k;
            }
        }
        uint32_t c14, c15;
        if (part) {
            c14 = c15 = 0;
            if (walk_cat[14] != 0) {
                c14 = mul_div_clamp(lost, total, walk_cat[14]);
                walk_cat[14] -= c14;
                if (walk_cat[14] == 0)
                    mask_clear(g, 14);
            }
            if (walk_cat[15] != 0) {
                c15 = mul_div_clamp(lost, total, walk_cat[15]);
                walk_cat[15] -= c15;
                if (walk_cat[15] == 0)
                    mask_clear(g, 15);
            }
        } else {
            c14 = walk_cat[14];
            c15 = walk_cat[15];
            walk_cat[14] = walk_cat[15] = 0;
        }
        r->unk_809f[14] += c14;
        if (walk_best_n < c14)
            walk_best_cat = 7;
        sum += c14 + c15;
        r->unk_809f[15] += c14; /* (sic) c14, not c15 */
        if (walk_best_n < c15)
            walk_best_cat = 8;
        if (part) {
            r->unk_8113 += sum;
            g_state.unk_626598 += sum;
            walk_total -= sum;
        } else {
            r->unk_8113 += walk_total;
            g_state.unk_626598 += walk_total;
            walk_total = 0;
        }
    }
    souls_stats.walk_lost += before - walk_total;
    if (sum != 0) {
        mapobj_mark_cells((int8_t)x, (int8_t)y, 1, marker_frame(walk_best_cat), 1);
        mapobj_redraw((int8_t)x, (int8_t)y, 1, marker_frame(walk_best_cat), 0, 0, 0);
    }
}

/* ---- Groups ------------------------------------------------------------------ */

/* 0x4599a8: is g the newest group of some category (unk_7e68[])? */
static bool group_is_newest(unsigned g)
{
    const PlaneStats *r = realm_cur();
    for (int i = 0; i < 16; i++)
        if (r->unk_7e68[i] == g)
            return true;
    return false;
}

/* 0x459f3c: every group of the chain ages a year (one per road cell). */
static void group_age(int32_t g)
{
    if (g < 0)
        return;
    do {
        SoulGroup *s = grp((unsigned)g);
        if (s->age < 0xfe)
            s->age++;
        g = (int32_t)s->next;
    } while (g > 0);
}

/* 0x45ad67: frees the group (and its chain) and clears the road cell. */
static void group_free(int x, int y, unsigned g)
{
    soul_group_set(x, y, 0);
    SoulGroup *s = grp(g);
    s->count = 0;
    s->mask = 0;
    s->age = 0;
    if (s->next == 0)
        return;
    int64_t n;
    do {
        n = (int32_t)grp(g)->next;
        if (n < 0)
            n = -n;
        grp(g)->next = 0;
        if (n < GS_SOUL_GROUPS && n >= 0) {
            SoulGroup *t = grp((unsigned)n);
            t->count = 0;
            t->mask = 0;
            t->age = 0;
            g = (unsigned)n;
        } else {
            n = 0;
        }
    } while (grp((unsigned)n)->next != 0 && n > 0);
}

/* 0x45ddf0: the group's total is split over its categories again, by
 * each category's share of all SOULs put on the roads this year
 * (unk_7eac[]); the newest group of a category (unk_7e68[]) takes the
 * category's remainder. Rounding carries over to the next group
 * (unk_6265a0, unk_57e020; both saved). */
static void group_unpack(unsigned g)
{
    PlaneStats *r = realm_cur();
    SoulGroup *s = grp(g);
    uint16_t m = s->mask;
    s->count += g_state.unk_6265a0;
    if (g_state.unk_57e020 < s->count) {
        s->count -= g_state.unk_57e020;
        g_state.unk_57e020 = 0;
    } else {
        g_state.unk_57e020 -= s->count >> 1;
        s->count -= s->count >> 1;
    }
    g_state.unk_6265a0 = 0;
    if (s->count == 0)
        return;
    uint32_t sum = 0;
    for (int c = 0; c < 16; c++)
        if (m & 1u << c)
            sum += r->unk_7eac[c];
    if (sum == 0)
        return;
    uint32_t got = 0;
    for (int c = 0; c < 16; c++) {
        if (!(m & 1u << c) || r->unk_7eac[c] == 0)
            continue;
        uint32_t t;
        if (r->unk_7e68[c] == g)
            t = r->unk_7eac[c];
        else
            t = mul_div_clamp(r->unk_7eac[c], sum, s->count);
        if (r->unk_7eac[c] < t) {
            g_state.unk_6265a0 += t - r->unk_7eac[c];
            t = r->unk_7eac[c];
        }
        if (r->unk_7e68[c] != g && r->unk_7eac[c] <= t)
            t = r->unk_7eac[c] - 1;
        if (s->count < t + got) {
            if (r->unk_7e68[c] == g) {
                uint32_t over = (t + got) - s->count;
                g_state.unk_57e020 += over;
                s->count += over;
            } else {
                g_state.unk_6265a0 += (t + got) - s->count;
                t = s->count - got;
            }
        }
        walk_cat[c] += t;
        walk_total += t;
        r->unk_7eac[c] -= t;
        r->unk_7ea8 -= t;
        sum -= t;
        got += t;
    }
    if (got < s->count)
        g_state.unk_6265a0 = s->count - got;
    souls_stats.unpacked += got;
}

/* 0x45cc82: the walking group meets group `other` on its road: if no
 * set-9 building was seen yet, the masks are equal and the sum fits,
 * other is chained to it and its SOULs join the walk. */
static void group_merge(unsigned g, unsigned other)
{
    if ((int8_t)g_state.unk_63041c < 0 && grp(other)->mask == grp(g)->mask &&
        grp(other)->count < (uint32_t)(-(int32_t)grp(g)->count) - 1u && !group_is_newest(other)) {
        unsigned last = g;
        int32_t n = grp_next(g);
        while (n > 0) {
            last = (unsigned)n;
            n = grp_next((unsigned)n);
        }
        grp(last)->next = (uint32_t)other;
        grp(other)->next = (uint32_t)n;
        group_unpack(other);
    }
}

/* 0x45cd7f: the chain's SOULs pass the road cell: +9 (this pass) and +5
 * (traffic) count them. */
static void road_add_traffic(int x, int y, int32_t g)
{
    if (g < 0)
        return;
    do {
        uint32_t n = grp((unsigned)g)->count;
        sc_add_u32(x, y, 9, n);
        sc_add_u32(x, y, 5, n);
        g = grp_next((unsigned)g);
    } while (g > 0);
}

/* ---- Buildings that take SOULs ------------------------------------------------- */

/* 0x459fa3: the zone building's SOUL-years (+10) grow by the age of the
 * newcomers (the chain's ages weighted by its share of `added`). */
static void zone_add_years(int x, int y, int32_t g, uint32_t added, uint32_t total)
{
    if (added == 0)
        return;
    uint32_t sum = 0;
    do {
        SoulGroup *s = grp((unsigned)g);
        sum += (uint32_t)s->age * mul_div_clamp(s->count, total, added);
        g = (int32_t)s->next;
    } while (g > 0);
    sc_add_u32(x, y, 10, sum);
}

/* 0x45a065: SOULs in a zone building (sum over its footprint for the
 * multi-cell special structures). */
static uint32_t building_pop(unsigned entry, uint8_t x, uint8_t y)
{
    if (entry < 0x60)
        return sc_u32(x, y, 2) + sc_u32(x, y, 6);
    unsigned n = entry < 0x6d ? 2 : entry < 0x6f ? 3 : 4;
    uint32_t sum = 0;
    for (unsigned cx = x; cx < x + n; cx++)
        for (int cy = y; (int)y - (int)n < cy; cy--)
            sum += sc_u32((int)(uint8_t)cx, (int)(uint8_t)cy, 2) + sc_u32((int)(uint8_t)cx, (int)(uint8_t)cy, 6);
    return sum;
}

/* The zone kinds of a zone building's cells, in the original's footprint
 * order (x, y), (x+1, y), (x+1, y-1), (x, y-1), (x+2, y), (x+2, y-1),
 * (x+2, y-2); special structures (entry >= 0x5b) keep a kind per cell
 * (7 = not decided yet). Returns the count; *special = entry >= 0x5b. */
static int zone_kinds(unsigned entry, int x, int y, uint8_t kinds[7], int *special)
{
    if (entry < 0x5b) {
        *special = 0;
        kinds[0] = g_world.tile_info[entry].zone_kind;
        return 1;
    }
    *special = 1;
    kinds[0] = (uint8_t)sc_zone_kind(x, y);
    if (entry >= 0x70) {
        kinds[1] = (uint8_t)sc_zone_kind(x + 1, y);
        return 2;
    }
    if (entry <= 0x5f)
        return 1;
    kinds[1] = (uint8_t)sc_zone_kind(x + 1, y);
    kinds[2] = (uint8_t)sc_zone_kind(x + 1, y - 1);
    kinds[3] = (uint8_t)sc_zone_kind(x, y - 1);
    if (entry <= 0x6c)
        return 4;
    kinds[4] = (uint8_t)sc_zone_kind(x + 2, y);
    kinds[5] = (uint8_t)sc_zone_kind(x + 2, y - 1);
    kinds[6] = (uint8_t)sc_zone_kind(x + 2, y - 2);
    return 7;
}

/* Steps (cx, cy) to the next footprint cell after cell i (byte math). */
static void footprint_next(int i, uint8_t *cx, uint8_t *cy)
{
    switch (i) {
    case 0:
        (*cx)++;
        break;
    case 1:
        (*cy)--;
        break;
    case 2:
        (*cx)--;
        break;
    case 3:
        *cx = (uint8_t)(*cx + 2), (*cy)++;
        break;
    case 4:
        (*cy)--;
        break;
    case 5:
        (*cy)--;
        break;
    default:
        break;
    }
}

/* 0x45a29d: the walking group passes a zone building (entry `entry` at
 * origin (x, y)) with room left: SOULs of its kind move in (permanent
 * SOULs from category k, temporary ones from k + 7, half the room each
 * first). Undecided cells of special structures take the first kind the
 * group carries (Hell: from the lowest rank, Heaven: from the highest).
 * Returns the categories the building could still take (0 when full), as
 * the road cell's +0xe mask. */
static uint16_t zone_absorb(uint8_t x, uint8_t y, unsigned g, uint8_t entry, uint16_t mask)
{
    uint16_t res = 0;
    uint32_t pop = building_pop(entry, x, y);
    uint32_t total0 = walk_total;
    uint32_t added = 0;
    uint32_t cap = g_world.tile_info[entry].cost; /* +0x00: capacity (scaled by 0x452267) */
    uint32_t room = pop < cap ? cap - pop : 0;
    if (room == 0)
        return 0;
    uint8_t kinds[7];
    int special;
    int nk = zone_kinds(entry, x, y, kinds, &special);
    uint8_t cx = x, cy = y;
    for (int i = 0; i < nk; i++) {
        uint8_t k = kinds[i];
        if (k == 7) {
            int hell = souls_in_hell();
            for (int n = 0; n < 7; n++) {
                int rank = hell ? n : 6 - n;
                uint8_t kk = souls_rank_kind(rank);
                if (((mask & 1u << (kk & 31)) && walk_cat[kk & 15]) ||
                    ((mask & 1u << ((kk + 7) & 31)) && walk_cat[(kk + 7) & 15])) {
                    int ok = 1;
                    for (int m = i + 1; m < nk; m++)
                        if (kk == kinds[m])
                            ok = 0;
                    if (ok) {
                        k = kk;
                        break;
                    }
                }
            }
        }
        if (k == 7) {
            res = 0x3fff;
        } else {
            uint8_t k2 = (uint8_t)(k + 7);
            res |= (uint16_t)(1u << k2 | 1u << k);
            uint32_t half = room >> 1;
            uint32_t ex_t = 0, take_t = 0, ex_p = 0, take_p = 0;
            if (mask & 1u << k) {
                if (half < walk_cat[k]) {
                    take_p = half;
                    ex_p = walk_cat[k] - half;
                } else {
                    take_p = walk_cat[k];
                }
            }
            if (mask & 1u << k2) {
                if (half < walk_cat[k2]) {
                    take_t = half;
                    ex_t = walk_cat[k2] - half;
                } else {
                    take_t = walk_cat[k2];
                }
            }
            uint32_t np = take_p, nt = take_t;
            room -= take_t + take_p;
            if (room != 0) {
                if (ex_t != 0) {
                    if (room < ex_t) {
                        nt = take_t + room;
                        ex_t -= room;
                        room = 0;
                    } else {
                        nt = take_t + ex_t;
                        room -= ex_t;
                        ex_t = 0;
                    }
                }
                if (ex_p != 0) {
                    if (room < ex_p) {
                        np = take_p + room;
                        ex_p -= room;
                        room = 0;
                    } else {
                        np = take_p + ex_p;
                        room -= ex_p;
                        ex_p = 0;
                    }
                }
            }
            if (kinds[i] == 7)
                sc_set_u8(cx, cy, 1, (uint8_t)((sc_u8(cx, cy, 1) & 0xf0) | k));
            added += np + nt;
            souls_stats.to_zones += np + nt;
            uint32_t before = walk_total;
            if (np != 0) {
                realm_perm_add(cx, cy, np, special, k);
                ledger_take(k, np);
                if (np < before) {
                    walk_total -= np;
                    walk_cat[k] -= np;
                } else {
                    walk_cat[k] = 0;
                    walk_total = 0;
                    mask_clear(g, k);
                }
            }
            if (nt != 0) {
                realm_temp_add(cx, cy, nt, special, k);
                ledger_take(k2, nt);
                if (nt < walk_total) {
                    walk_total -= nt;
                    walk_cat[k2] -= nt;
                } else {
                    walk_cat[k2] = 0;
                    walk_total = 0;
                    mask_clear(g, k2);
                }
            }
            souls_stats.walk_zones += before - walk_total;
            if (walk_cat[k] == 0)
                mask_clear(g, k);
            if (walk_cat[k2] == 0)
                mask_clear(g, k2);
            if (ex_p == 0 && ex_t == 0)
                walk_cat_age[k] = grp(g)->age;
        }
        if (room == 0)
            i = 100;
        stat_zone_visits++;
        footprint_next(i, &cx, &cy);
    }
    if (added != 0) {
        if (walk_track != 0)
            walk_track = soultrack_arrive(x, y, g);
        zone_add_years(x, y, (int32_t)g, added, total0);
    }
    if (room == 0)
        res = 0;
    return res;
}

/* 0x45b397: training centre (set 8): temporary SOULs (categories 7-13)
 * join, each category with the building's chance (+0xd percent). */
static void training_absorb(int x, int y, unsigned g)
{
    unsigned e = mc_entry(x, y);
    if (sc_u8(x, y, 0xd) & 0x80)
        return;
    uint32_t cap = e < 6 ? g_tables.set8[e].capacity : 0;
    bool whole = mc_bit2(x, y) == 0;
    uint32_t room = (whole ? cap : (uint32_t)((int32_t)cap >> 1)) - sc_u32(x, y, 1);
    if ((int32_t)room <= 0)
        return;
    unsigned c = 7;
    uint32_t before = walk_total;
    do {
        if (walk_cat[c] != 0 && rand_range(0, 99) < (int)sc_u8(x, y, 0xd)) {
            if (room < walk_cat[c]) {
                walk_cat[c] -= room;
                walk_total -= room;
                sc_set_u32(x, y, 1, whole ? cap : (uint32_t)((int32_t)cap >> 1));
                room = 0;
            } else {
                walk_total -= walk_cat[c];
                room -= walk_cat[c];
                sc_add_u32(x, y, 1, walk_cat[c]);
                walk_cat[c] = 0;
                mask_clear(g, c);
                walk_cat_age[c - 7] = grp(g)->age;
            }
            if (walk_track != 0)
                walk_track = soultrack_arrive(x, y, g);
        }
        c++;
    } while (room != 0 && c < 14);
    souls_stats.walk_training += before - walk_total;
}

/* 0x45b6c6: Gate (set 13): category-15 SOULs leave for the other realm. */
static void gate_absorb(int x, int y, unsigned g)
{
    unsigned i = mc_entry(x, y) >> 1;
    uint32_t cap = i < 8 ? g_tables.set13[i].capacity : 0;
    if (mc_bit2(x, y))
        cap = (uint32_t)((int32_t)cap >> 1);
    uint32_t room = cap - sc_u32(x, y, 1);
    if ((int32_t)room <= 0 || walk_cat[15] == 0)
        return;
    uint32_t before = walk_total;
    if (room < walk_cat[15]) {
        walk_cat[15] -= room;
        walk_total -= room;
        sc_set_u32(x, y, 1, cap);
    } else {
        walk_total -= walk_cat[15];
        room = walk_cat[15];
        sc_add_u32(x, y, 1, walk_cat[15]);
        walk_cat[15] = 0;
        grp(g)->mask &= 0x7fff;
        walk_cat_age[8] = grp(g)->age;
    }
    souls_stats.walk_gates += before - walk_total;
    if (souls_in_hell())
        souls_to_heaven += room;
    else
        souls_to_hell += room;
    if (walk_track != 0)
        walk_track = soultrack_arrive(x, y, g);
}

/* 0x45b8ee: Karma station (set 17, if +9 is set): category-14 SOULs
 * board (capacity by the +10 byte). Returns 0x4000 if all did. */
static uint16_t station_absorb(int x, int y, unsigned g)
{
    uint16_t res = 0;
    if (sc_u8(x, y, 9) == 0)
        return 0;
    unsigned idx = sc_u8(x, y, 10);
    uint32_t cap = idx < 6 ? g_tables.set17[idx].capacity : 0;
    uint32_t room = cap - sc_u32(x, y, 1);
    if ((int32_t)room <= 0)
        return 0;
    uint32_t before = walk_total;
    if (room < walk_cat[14]) {
        walk_cat[14] -= room;
        walk_total -= room;
        sc_set_u32(x, y, 1, cap);
    } else {
        walk_total -= walk_cat[14];
        sc_add_u32(x, y, 1, walk_cat[14]);
        walk_cat[14] = 0;
        grp(g)->mask &= 0xbfff;
        walk_cat_age[8] = grp(g)->age;
        res = 0x4000;
    }
    souls_stats.walk_stations += before - walk_total;
    if (walk_track != 0)
        walk_track = soultrack_arrive(x, y, g);
    return res;
}

/* 0x45bac5: Omnibolge / Love Domes (set 5), once per group: takes +0xd
 * percent of every category (scaled down to the room left), out of the
 * ledger into the realm's SOUL pool (souls bignum) and its cell counters
 * (+1 for beliefs j & 7 < 5, Hell's +9 for j & 3 == 0, else +5).
 * Returns 0xffff (or 0 when it can't take any). */
static uint16_t omnibolge_absorb(int x, int y, unsigned g)
{
    PlaneStats *r = realm_cur();
    if (sc_u16(x, y, 0xe) == (uint16_t)g)
        return 0xffff;
    sc_set_u16(x, y, 0xe, (uint16_t)g);
    unsigned e = mc_entry(x, y);
    uint32_t cap = e < 6 ? g_tables.set5[e].capacity : 0;
    uint32_t room = cap - (sc_u32(x, y, 5) + sc_u32(x, y, 9) + sc_u32(x, y, 1));
    uint8_t pct = sc_u8(x, y, 0xd);
    if (pct == 0 || room == 0)
        return 0;
    bool all = false;
    uint32_t got = 0;
    uint32_t before = walk_total;
    if (walk_track != 0)
        walk_track = soultrack_arrive(x, y, g);
    for (unsigned c = 0; c < 16; c++) {
        if (walk_cat[c] == 0)
            continue;
        uint32_t want = pct < 100 ? mul_div_clamp(pct, 100, walk_cat[c]) : walk_cat[c];
        if (want == 0)
            continue;
        if (room < walk_total)
            want = mul_div_clamp(room, walk_total, want);
        else if (pct == 100)
            all = true;
        if (want == 0)
            continue;
        PlaneStatRow *row = &r->unk_7a08[c];
        uint32_t tk = 0;
        for (unsigned j = 0; j < 16; j++) {
            uint32_t have = row->v[j];
            if (have == 0)
                continue;
            uint32_t t = mul_div_clamp(have, row->unk_00, want);
            if (have < t)
                t = have;
            if (room - got < t)
                t = room - got;
            row->v[j] -= t;
            row->unk_00 -= t;
            tk += t;
            got += t;
            bignum_add(&r->souls, t);
            if ((j & 7) < 5)
                sc_add_u32(x, y, 1, t);
            else if (g_world.plane == GS_PLANE_HELL && (j & 3) == 0)
                sc_add_u32(x, y, 9, t);
            else
                sc_add_u32(x, y, 5, t);
        }
        uint32_t u = umin(umin(walk_cat[c], tk), walk_total);
        walk_cat[c] -= u;
        if (all)
            walk_cat[c] = 0;
        if (walk_cat[c] == 0)
            mask_clear(g, c);
        walk_total -= u;
    }
    if (all)
        walk_total = 0;
    souls_stats.walk_omnibolges += before - walk_total;
    if (walk_total == 0)
        grp(g)->mask = 0;
    return 0xffff;
}

/* ---- The road walk ---------------------------------------------------------------- */

/* 0x45c038: the walking group stands on road cell (x, y) and looks at
 * every building cell within Manhattan distance 3 (only 1 deep where the
 * road continues to the side), rows or columns first and each in a random
 * direction (CRT rand parity). The road cell's +0xe gets the categories
 * the buildings around could take next time. Returns 1 if the group is
 * empty. */
static int road_look_around(uint8_t x, uint8_t y, unsigned g)
{
    PlaneStats *r = realm_cur();
    uint16_t old = sc_u16(x, y, 0xe);
    uint16_t res = 0;
    uint8_t lx = x, hx = x, ly = y, hy = y;
    int8_t sx = 1, sy = 1;
    uint8_t px = x, py = y;
    int horiz = rand_range(0, 1) != 0;
    uint16_t gm = grp(g)->mask;
    if (!horiz) {
        if (y != 0 && mc_set(x, (int8_t)(y - 1)) != 12)
            ly = (uint8_t)(y - 3);
        if (y < 99 && mc_set(x, (int8_t)(y + 1)) != 12)
            hy = (uint8_t)(y + 3);
        if ((int8_t)ly < 0)
            ly = 0;
        if ((int8_t)hy > 99)
            hy = 99;
        sy = 1;
        if (hy != ly && (crt_rand() & 1)) {
            uint8_t t = ly;
            sy = -1;
            ly = hy;
            hy = t;
        }
        py = ly;
    } else {
        if (x != 0 && mc_set((int8_t)(x - 1), y) != 12)
            lx = (uint8_t)(x - 3);
        if (x < 99 && mc_set((int8_t)(x + 1), y) != 12)
            hx = (uint8_t)(x + 3);
        if ((int8_t)lx < 0)
            lx = 0;
        if ((int8_t)hx > 99)
            hx = 99;
        sx = 1;
        if (hx != lx && (crt_rand() & 1)) {
            uint8_t t = lx;
            sx = -1;
            lx = hx;
            hx = t;
        }
        px = lx;
    }
    bool more;
    do {
        if (!horiz) {
            int8_t d = (int8_t)abs((int8_t)py - (int)y);
            lx = hx = x;
            if (x != 0 && mc_set((int8_t)(x - 1), y) != 12)
                lx = (uint8_t)(x - (3 - d));
            if (x < 99 && mc_set((int8_t)(x + 1), y) != 12)
                hx = (uint8_t)((3 - d) + x);
            if ((int8_t)lx < 0)
                lx = 0;
            if ((int8_t)hx > 99)
                hx = 99;
            sx = 1;
            if (hx != lx && (crt_rand() & 1)) {
                uint8_t t = lx;
                sx = -1;
                lx = hx;
                hx = t;
            }
            px = lx;
        } else {
            int8_t d = (int8_t)abs((int8_t)px - (int)x);
            ly = hy = y;
            if (y != 0 && mc_set(x, (int8_t)(y - 1)) != 12)
                ly = (uint8_t)(y - (3 - d));
            if (y < 99 && mc_set(x, (int8_t)(y + 1)) != 12)
                hy = (uint8_t)((3 - d) + y);
            if ((int8_t)ly < 0)
                ly = 0;
            if ((int8_t)hy > 99)
                hy = 99;
            sy = 1;
            if (hy != ly && (crt_rand() & 1)) {
                uint8_t t = ly;
                sy = -1;
                ly = hy;
                hy = t;
            }
            py = ly;
        }
        bool inner;
        do {
            if (y != (int8_t)py || x != (int8_t)px) {
                int ox = (int8_t)px, oy = (int8_t)py;
                unsigned set = mc(ox, oy) >> 27;
                if (set == 0) {
                    uint8_t bx = sc_u8(ox, oy, 10), by = sc_u8(ox, oy, 11);
                    ox = (int8_t)bx;
                    oy = (int8_t)by;
                    set = mc(ox, oy) >> 27;
                }
                if (!mc_shade(ox, oy)) {
                    switch (set) {
                    case 5:
                        res |= omnibolge_absorb(ox, oy, g);
                        break;
                    case 8:
                        if (grp(g)->mask & 0x3f80)
                            training_absorb(ox, oy, g);
                        res |= old & 0x3f80;
                        break;
                    case 9: {
                        portal_group_set(ox, oy, g);
                        unsigned e = mc_entry(ox, oy);
                        uint32_t cap = e < 6 ? g_tables.set9[e].capacity : 0;
                        uint32_t free_ = cap - sc_u32(ox, oy, 1);
                        if (g_state.unk_5bb10c < free_) {
                            g_state.unk_5bb10c = free_;
                            g_state.unk_626594 = g_state.unk_57e1ac;
                            g_state.unk_63041c = (uint8_t)ox;
                            g_state.unk_630424 = (uint8_t)oy;
                        }
                        if (cap != sc_u32(ox, oy, 1))
                            res = 0xffff;
                        break;
                    }
                    case 13:
                        if (grp(g)->mask & 0x8000)
                            gate_absorb(ox, oy, g);
                        res = 0xffff;
                        break;
                    case 14:
                    case 15: {
                        unsigned e = mc_entry(ox, oy);
                        uint16_t r2;
                        if (e < 0x5b) {
                            unsigned k = e / 13;
                            if ((gm & 1u << k) == 0 && (gm & 1u << (k + 7)) == 0)
                                r2 = (uint16_t)((1u << (k + 7) | 1u << k) & old);
                            else
                                r2 = zone_absorb((uint8_t)ox, (uint8_t)oy, g, (uint8_t)e, grp(g)->mask);
                        } else {
                            r2 = zone_absorb((uint8_t)ox, (uint8_t)oy, g, (uint8_t)e, grp(g)->mask);
                        }
                        res |= r2;
                        break;
                    }
                    case 17:
                        if ((grp(g)->mask & 0x4000) == 0)
                            res |= old & 0x4000;
                        else
                            res |= station_absorb(ox, oy, g);
                        break;
                    default: /* 6, 7, 10, 11, 12, 16 and the rest: nothing */
                        break;
                    }
                }
            }
            if (!horiz) {
                inner = px != hx;
                px = (uint8_t)(px + sx);
            } else {
                inner = py != hy;
                py = (uint8_t)(py + sy);
            }
        } while (inner);
        if (!horiz) {
            more = py != hy;
            py = (uint8_t)(py + sy);
        } else {
            more = px != hx;
            px = (uint8_t)(px + sx);
        }
    } while (more);
    sc_set_u16(x, y, 0xe, res);
    SoulGroup *s = grp(g);
    if (walk_track != 0 && ((s->mask & walk_track) != walk_track || s->mask == 0 || walk_total == 0)) {
        for (unsigned c = 0; c < 16; c++) {
            if (walk_track & 1u << c) {
                s->mask |= (uint16_t)(1u << c);
                walk_total += soultrack_cat[c];
                walk_cat[c] += soultrack_cat[c];
                souls_stats.tracked_added += soultrack_cat[c];
            }
        }
    }
    (void)r;
    return s->mask == 0 || walk_total == 0;
}

/* 0x45cf9b: counts the chain's SOULs as traffic along the straight road
 * run back from (x, y) in direction (dx, dy), `steps` cells. */
static void road_traffic_back(uint8_t x, uint8_t y, int8_t dx, int8_t dy, int steps, unsigned g)
{
    bool go = true;
    int8_t c = (int8_t)(g_state.view_dir_x * g_state.view_dir_y);
    do {
        if (mc_set(x, y) == 12) {
            road_add_traffic(x, y, (int32_t)g);
            if (walk_track != 0)
                soultrack_step((int8_t)x, (int8_t)y, g);
            int8_t ndx = dx, ndy = dy;
            switch (mc_entry(x, y) % 11) {
            case 0:
            case 7:
                break;
            case 1:
            case 5:
                ndx = (int8_t)-(c * dy);
                ndy = (int8_t)-(c * dx);
                break;
            case 2:
            case 4:
                ndx = (int8_t)(c * dy);
                ndy = (int8_t)(c * dx);
                break;
            default:
                go = false;
                break;
            }
            dx = ndx;
            dy = ndy;
            x = (uint8_t)(x + ndx);
            y = (uint8_t)(y + ndy);
        } else {
            go = false;
        }
        steps--;
    } while (steps != 0 && go);
}

static int road_walk(uint8_t x, uint8_t y, unsigned g, int8_t dir, unsigned mark);

/* 0x45d19d: the walk reaches a Gate (set 13, origin (x, y)): it continues
 * from every road cell around the Gate's footprint (rows above and below,
 * then the columns left and right) until the group is empty. */
static unsigned gate_walk(uint8_t x, uint8_t y, unsigned g, unsigned mark)
{
    unsigned r = 0;
    uint8_t n = (uint8_t)structure_size(x, y);
    gate_mark_set(x, y, mark);
    uint8_t cx = (uint8_t)(x - 1);
    uint8_t b;
    do {
        uint8_t cy = (uint8_t)(y - n);
        if (mc_set(cx, cy) == 12 && !mc_shade(cx, cy) && road_mark(cx, cy) != mark)
            r |= (unsigned)road_walk(cx, cy, g, -2, mark);
        if (r == 0) {
            cy = (uint8_t)(y + 1);
            if (mc_set(cx, cy) == 12 && !mc_shade(cx, cy) && road_mark(cx, cy) != mark)
                r = (unsigned)road_walk(cx, cy, g, -2, mark);
        }
        b = (uint8_t)(cx + 1);
        cx = b;
    } while (r == 0 && (unsigned)b <= (unsigned)x + n);
    if (r == 0) {
        uint8_t cy = y;
        do {
            cx = (uint8_t)(x - 1);
            if (mc_set(cx, cy) == 12 && !mc_shade(cx, cy) && road_mark(cx, cy) != mark)
                r |= (unsigned)road_walk(cx, cy, g, -2, mark);
            if (r == 0) {
                cx = (uint8_t)(x + n);
                if (mc_set(cx, cy) == 12 && !mc_shade(cx, cy) && road_mark(cx, cy) != mark)
                    r = (unsigned)road_walk(cx, cy, g, -2, mark);
            }
            b = (uint8_t)(cy - 1);
            cy = b;
        } while (r == 0 && (int)y - (int)n < (int)b);
    }
    return r;
}

/* 0x45d4ac: the recursive road walk. From road cell (x, y) the group
 * follows the road straight (turning with the road pieces, entry % 11)
 * as long as there is no junction, marking each cell with `mark` and
 * looking around for buildings that take it (0x45c038) where the cell's
 * +0xe says it may find some. At a junction (or a Gate) it branches into
 * the four neighbours in the shuffled direction order. A dead end, a
 * marked cell or the map edge ends a branch. dir is the direction index
 * it came in (-1 = start of a group's walk: resets the set-9 search, -2 =
 * from a Gate). Returns 1 when the group is empty. */
static int road_walk(uint8_t x, uint8_t y, unsigned g, int8_t dir, unsigned mark)
{
    if (dir == -1) {
        g_state.unk_63041c = 0xff;
        g_state.unk_630424 = 0xff;
        g_state.unk_626594 = 0xffffffffu;
        g_state.unk_57e1ac = 0;
        g_state.unk_5bb10c = 0;
    }
    stat_walks++;
    uint8_t prevx = x, prevy = y;
    if (!(x < 100 && y < 100))
        return 0;
    if (mc_set(x, y) != 12) {
        walk_depth--; /* (sic) without the increment */
        return 0;
    }
    walk_depth++;
    if (walk_max_depth < walk_depth)
        walk_max_depth = walk_depth;
    int8_t c = (int8_t)(g_state.view_dir_x * g_state.view_dir_y);
    int8_t dx = 0, dy = 0;
    if (dir >= 0) {
        dx = (int8_t)g_state.unk_5f25b0[dir & 3][0];
        dy = (int8_t)g_state.unk_5f25b0[dir & 3][1];
    }
    uint8_t cx = x, cy = y;
    int steps = 1;
    int8_t ldx = 0, ldy = 0;
    bool stop;
    do {
        int8_t odx = dx, ody = dy;
        unsigned other = soul_group_get(x, y);
        if (other != 0 && g != other && walk_no_merge == 0) {
            walk_start_x = x;
            walk_start_y = y;
            group_merge(g, other);
        }
        group_age((int32_t)g);
        road_add_traffic(x, y, (int32_t)g);
        road_mark_set(x, y, mark);
        int done = 0;
        if (grp(g)->mask & sc_u16(x, y, 0xe))
            done = road_look_around(x, y, g);
        if (done) {
            walk_depth--;
            return 1;
        }
        if (walk_track != 0)
            soultrack_step((int8_t)x, (int8_t)y, g);
        stop = false;
        g_state.unk_57e1ac++;
        if (dir < 0) {
            stop = true;
        } else {
            int8_t ndx = dx, ndy = dy;
            switch (mc_entry((int8_t)cx, (int8_t)cy) % 11) {
            case 0:
            case 7:
                break;
            case 1:
            case 5:
                ndx = (int8_t)-(c * dy);
                ndy = (int8_t)-(c * dx);
                break;
            case 2:
            case 4:
                ndx = (int8_t)(dy * c);
                ndy = (int8_t)(dx * c);
                break;
            default:
                stop = true;
                break;
            }
            if (!stop) {
                dx = ndx;
                dy = ndy;
                prevx = cx;
                prevy = cy;
                x = (uint8_t)(ndx + (int8_t)cx);
                cx = x;
                y = (uint8_t)(ndy + (int8_t)cy);
                cy = y;
                if ((int8_t)x < 0 || (int8_t)x > 99 || (int8_t)y < 0 || (int8_t)y > 99) {
                    walk_depth--;
                    return 0;
                }
                unsigned os = cell_origin_tileset(cx, cy);
                if (os == 12) {
                    if (mc_shade(cx, cy) || road_mark(cx, cy) == mark) {
                        road_traffic_back(prevx, prevy, (int8_t)-odx, (int8_t)-ody, steps, g);
                        walk_depth--;
                        return 0;
                    }
                } else {
                    if (os != 13 || mc_shade(cx, cy)) {
                        road_traffic_back(prevx, prevy, (int8_t)-odx, (int8_t)-ody, steps, g);
                        walk_depth--;
                        return 0;
                    }
                    stop = true;
                }
                steps++;
                ldx = odx;
                ldy = ody;
            }
        }
    } while (!stop);
    unsigned found = 0;
    int8_t i = 0;
    do {
        int8_t nx = (int8_t)(g_state.unk_5f25b0[i][0] + x);
        int8_t ny = (int8_t)(g_state.unk_5f25b0[i][1] + y);
        if (nx >= 0 && nx < 100 && ny >= 0 && ny < 100 && !mc_shade(nx, ny)) {
            unsigned s = mc_set(nx, ny);
            if (s == 0) {
                int8_t ox = (int8_t)sc_u8(nx, ny, 10), oy = (int8_t)sc_u8(nx, ny, 11);
                if (mc_set(ox, oy) == 13 && gate_mark(ox, oy) != mark)
                    found |= gate_walk((uint8_t)ox, (uint8_t)oy, g, mark) & 0xff;
            } else if (s == 12) {
                if (road_mark(nx, ny) != mark)
                    found |= (unsigned)road_walk((uint8_t)nx, (uint8_t)ny, g, i, mark);
            } else if (s == 13 && gate_mark(nx, ny) != mark) {
                found |= gate_walk((uint8_t)nx, (uint8_t)ny, g, mark) & 0xff;
            }
        }
        i++;
    } while (found == 0 && i != 4);
    if (found == 0) {
        road_traffic_back(x, y, ldx, ldy, steps, g);
        walk_depth--;
        return 0;
    }
    walk_depth--;
    return 1;
}

/* 0x45dcfb: shuffles the four walk directions (unk_5f25b0[4][3], saved). */
static void dirs_shuffle(void)
{
    for (int i = 0; i < 4; i++) {
        if (rand_range(0, 9) != 0) {
            int j = (int8_t)rand_range(0, 3);
            if (j != i) {
                uint8_t t[3];
                memcpy(t, g_state.unk_5f25b0[i], 3);
                memcpy(g_state.unk_5f25b0[i], g_state.unk_5f25b0[j & 3], 3);
                memcpy(g_state.unk_5f25b0[j & 3], t, 3);
            }
        }
    }
}

/* 0x45afde: the walk ended with SOULs left but saw a set-9 building
 * (unk_63041c/unk_630424, the one with the most room): the group rides to
 * another set-9 building (random start in the realm's list unk_78c4,
 * [0][0] = count) with a road within 3 cells, filling the visited ones'
 * load (+1) on the way. Returns 0 and the road cell to continue from, or
 * -1 when there is none (the SOULs are then lost at (cx, cy)). */
static int take_portal(int8_t px, int8_t py, unsigned g, int cx, int cy, int *ox, int *oy)
{
    PlaneStats *r = realm_cur();
    uint8_t n = r->unk_78c4[0][0];
    if (n < 2) {
        souls_lose(cx, cy, g, walk_total);
        return -1;
    }
    uint8_t i = (uint8_t)rand_range(1, n);
    uint8_t tries = 0;
    int fx = -1, fy = -1;
    do {
        uint8_t sx = r->unk_78c4[i & 127][0], sy = r->unk_78c4[i & 127][1];
        if (sx < 100 && ((int)sx != px || (int)sy != py)) {
            if (portal_group(sx, sy) == g) {
                i++;
            } else {
                int rx, ry;
                if (souls_area_search(sx, sy, 3, 3, &rx, &ry) == AREA_NONE) {
                    i++;
                } else {
                    fx = rx;
                    fy = ry;
                }
            }
        } else {
            i++;
        }
        tries++;
        if (n < i)
            i = 1;
    } while (fx < 0 && tries < n);
    if (fx < 0) {
        souls_lose(cx, cy, g, walk_total);
        return -1;
    }
    uint32_t left = walk_total;
    for (unsigned k = 1; k <= n; k++) {
        uint8_t sx = r->unk_78c4[k & 127][0], sy = r->unk_78c4[k & 127][1];
        if (portal_group(sx, sy) == g && portal_mark(sx, sy) != g) {
            portal_mark_set(sx, sy, g);
            uint32_t load = sc_u32(sx, sy, 1);
            unsigned e = mc_entry(sx, sy);
            uint32_t cap = e < 6 ? g_tables.set9[e].capacity : 0;
            if ((int32_t)cap < (int32_t)(load + left)) {
                left -= cap - load;
                sc_set_u32(sx, sy, 1, cap);
            } else {
                sc_add_u32(sx, sy, 1, left);
                left = 0;
            }
            if (left == 0)
                k = n + 1u;
        }
    }
    if (left != 0) {
        souls_lose(cx, cy, g, left);
        r->unk_811b += left;
    }
    *ox = fx;
    *oy = fy;
    return 0;
}

/* 0x45e1fc: one group's turn: unpack, walk, ride set-9 buildings, and
 * lose what is left; the group is freed afterwards. */
static void group_walk(uint8_t x, uint8_t y, unsigned g)
{
    walk_no_merge = 0;
    walk_total = 0;
    soultrack_count = 0;
    for (int c = 0; c < 16; c++) {
        walk_cat[c] = 0;
        soultrack_cat[c] = 0;
    }
    walk_track = soultrack_group_mask(g);
    memset(walk_cat_age, 0, sizeof walk_cat_age);
    walk_start_x = x;
    walk_start_y = y;
    group_unpack(g);
    dirs_shuffle();
    walk_depth = 0;
    int state = 0;
    do {
        if (road_walk(x, y, g, -1, g) == 0) {
            if ((int8_t)g_state.unk_63041c < 0) {
                state = 1;
            } else {
                SoulGroup *s = grp(g);
                if (0xfeu - g_state.unk_626594 < (uint32_t)s->age)
                    s->age = 0xfe;
                else
                    s->age = (uint8_t)(s->age + g_state.unk_626594);
                int nx, ny;
                if (take_portal((int8_t)g_state.unk_63041c, (int8_t)g_state.unk_630424, g, (int8_t)x, (int8_t)y, &nx,
                                &ny) < 0) {
                    state = 1;
                    x = g_state.unk_63041c;
                    y = g_state.unk_630424;
                } else {
                    x = (uint8_t)nx;
                    y = (uint8_t)ny;
                }
            }
        } else {
            state = 2;
        }
    } while (state == 0);
    if (state == 1)
        souls_lose((int8_t)x, (int8_t)y, g, walk_total);
    souls_stats.walk_left += walk_total;
    group_free(x, y, g);
}

/* Clears the per-pass sim-cell fields (start of 0x45e44b). */
static void flow_clear_cells(void)
{
    for (int x = 0; x < 100; x++) {
        for (int y = 0; y < 100; y++) {
            unsigned slot = g_world.objmap ? g_world.objmap[y * 100 + x] >> 2 : 0;
            if (slot != 0) {
                for (unsigned i = 0; i < 9; i++) {
                    if (marker_frame(i) == slot) {
                        mapobj_mark_cells(x, y, 1, marker_frame(i), 0);
                        mapobj_redraw(x, y, 1, marker_frame(i), 10, 6, 0);
                        break;
                    }
                }
            }
            switch (mc_set(x, y)) {
            case 5:
                sc_set_u16(x, y, 0xe, 0);
                break;
            case 9:
                sc_set_u32(x, y, 9, 0);
                sc_set_u32(x, y, 1, 0);
                sc_set_u8(x, y, 0xe, 1);
                portal_group_set(x, y, 0);
                portal_mark_set(x, y, 0);
                break;
            case 12:
                road_mark_set(x, y, 0);
                sc_set_u32(x, y, 9, 0);
                sc_set_u16(x, y, 0xe, 0xffff);
                break;
            case 13:
                gate_mark_set(x, y, 0);
                break;
            case 17:
                sc_set_u32(x, y, 1, 0);
                break;
            default:
                break;
            }
        }
    }
}

/* 0x45e44b */
bool souls_flow(unsigned max_groups)
{
    PlaneStats *r = realm_cur();
    uint8_t bx = flow_x, by = flow_y;
    if (flow_x == 0 && flow_y == 0) {
        stat_yields = 0;
        stat_zone_visits = 0;
        stat_65fe5c = 0;
        g_state.unk_6265a0 = 0;
        g_state.unk_57e020 = 0;
        walk_max_depth = 0;
        stat_walks = 0;
        stat_groups_walked = 0;
        r->unk_811b = 0;
        for (int c = 0; c < 16; c++)
            r->unk_809f[c] = 0;
        if (!souls_in_hell()) {
            if (souls_to_heaven != 0) {
                souls_gate_spawn(souls_to_heaven, g_state.unk_593240[6], -1);
                souls_to_heaven = 0;
            }
        } else if (souls_to_hell != 0) {
            if (g_state.unk_638670[7] != 0) {
                for (unsigned k = 0; k < 7; k++) {
                    if (g_state.unk_638670[k] != 0) {
                        uint32_t t = mul_div_clamp(g_state.unk_638670[k], g_state.unk_638670[7], souls_to_hell);
                        if (t != 0)
                            souls_gate_spawn(t, k + 7, -1);
                        g_state.unk_638670[k] = 0;
                    }
                }
                g_state.unk_638670[7] = 0;
            }
            souls_to_hell = 0;
        }
        flow_clear_cells();
        bx = flow_x;
        by = flow_y;
    }
    unsigned walked = 0;
    for (;;) {
        unsigned g;
        if (mc_set(bx, by) == 12 && (g = soul_group_get(bx, by)) != 0 && g < GS_SOUL_GROUPS) {
            stat_groups_walked++;
            group_walk(bx, by, g);
            walked++;
            if (max_groups != 0 && walked >= max_groups) {
                stat_yields++;
                if (by != 99) {
                    flow_x = bx;
                    flow_y = (uint8_t)(by + 1);
                    return false;
                }
                if (bx != 99) {
                    flow_x = (uint8_t)(bx + 1);
                    flow_y = 0;
                    return false;
                }
            }
        }
        by++;
        if (by > 99) {
            bx++;
            by = 0;
            if (bx > 99)
                break;
        }
    }
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++)
            if (mc_set(x, y) == 17) {
                sc_set_u32(x, y, 5, sc_u32(x, y, 1));
                sc_set_u32(x, y, 1, 0);
            }
    settle_arrivals();
    for (int c = 0; c < 16; c++) {
        r->unk_7eac[c] = 0;
        r->unk_7e48[c][0] = 0;
        r->unk_7e48[c][1] = 0;
        r->unk_7e68[c] = 0;
    }
    r->unk_7ea8 = 0;
    flow_x = flow_y = 0;
    flow_done = 1;
    (void)walk_start_x, (void)walk_start_y, (void)stat_65fe5c;
    return true;
}

void souls_flow_cursor(uint32_t out[2])
{
    out[0] = flow_x;
    out[1] = flow_y;
}

void souls_flow_reset(void)
{
    walk_total = 0;
    memset(walk_cat, 0, sizeof walk_cat);
    walk_track = 0;
    memset(walk_cat_age, 0, sizeof walk_cat_age);
    walk_no_merge = 0;
    walk_start_x = walk_start_y = 0;
    walk_best_n = walk_best_cat = 0;
    walk_depth = walk_max_depth = 0;
    stat_yields = stat_zone_visits = stat_walks = stat_groups_walked = 0;
    stat_65fe5c = 0;
    flow_x = flow_y = 0;
    flow_done = 1;
    souls_to_heaven = souls_to_hell = 0;
    memset(&souls_stats, 0, sizeof souls_stats);
}
