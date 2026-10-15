/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * SOUL flow, the zone side (ALIFE.EXE 0x45ebe2-0x4619ef): the area
 * search, SOULs leaving zone buildings (the zone scan), adding SOULs to
 * groups, and the arrivals at Gates and siphons. Overview in game/souls.h.
 */
#include <stdlib.h>

#include "game/build.h" /* g_build.special_pos */
#include "game/realm.h"
#include "game/souls.h"
#include "game/souls_int.h"
#include "game/structure.h"
#include "game/tables.h"
#include "game/util.h"
#include "game/world.h"

uint32_t souls_groups_created; /* 0x630510 */
uint32_t souls_gate_arrived;   /* 0x57e01c */

static uint8_t scan_x, scan_y;      /* 0x65fe48, 0x65fe4c: souls_zone_scan resume point */
static int32_t next_free_group = 1; /* 0x65fe50 (initially 1) */

void souls_reset(void)
{
    scan_x = scan_y = 0;
    next_free_group = 1;
    souls_groups_created = 0;
    souls_gate_arrived = 0;
    souls_flow_reset();
}

void souls_cursors(uint32_t out[5])
{
    out[0] = (uint32_t)next_free_group;
    out[1] = scan_x;
    out[2] = scan_y;
    souls_flow_cursor(out + 3);
}

static uint32_t umin(uint32_t a, uint32_t b) { return a < b ? a : b; }

/* realm_perm_sub / realm_temp_sub / soul_group_add for SOULs leaving zone
 * buildings, with the port-only bookkeeping (souls_stats). The original's
 * shares are computed from the origin cell and the realm-wide statistics,
 * so the removal (clamped to the cell) can be smaller than what goes on
 * the road. */
static void leave_perm(int x, int y, uint32_t n, int special, int kind)
{
    uint32_t before = realm_cur()->population;
    realm_perm_sub(x, y, n, special, kind);
    souls_stats.left_zones += before - realm_cur()->population;
}
static void leave_temp(int x, int y, uint32_t n, int special, int kind)
{
    uint32_t before = realm_cur()->population;
    realm_temp_sub(x, y, n, special, kind);
    souls_stats.left_zones += before - realm_cur()->population;
}
static unsigned leave_to_road(uint32_t n, unsigned cat, int x, int y)
{
    souls_stats.left_to_roads += n;
    return soul_group_add(n, cat, x, y);
}

/* ---- Area search (0x45ebe2) --------------------------------------------- */

unsigned souls_area_search(int x, int y, unsigned size, unsigned radius, int *ox, int *oy)
{
    int8_t px = (int8_t)x, py = (int8_t)y;
    uint8_t sz = (uint8_t)size, rad = (uint8_t)radius;
    uint8_t best = 0xff;
    uint8_t bx = 0xff, by = 0xff;
    int fresh = 0;
    int x0 = px - rad;
    int x1 = (rad - 1) + sz + px;
    int y0 = (py - sz) - (rad - 1);
    int y1 = rad + py;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > 99)
        x1 = 99;
    if (y1 > 99)
        y1 = 99;
    for (int cx = x0; cx <= x1; cx++) {
        for (int cy = y0; cy <= y1; cy++) {
            int dx = 0, dy = 0;
            if (cx < px || sz + px <= cx) {
                int a = abs(cx - (sz + px - 1)), b = abs(cx - px);
                dx = b < a ? b : a;
            }
            if (py < cy || cy <= py - sz) {
                int a = abs((py - sz + 1) - cy), b = abs(py - cy);
                dy = b < a ? b : a;
            }
            uint8_t d = (uint8_t)((int8_t)dy + (int8_t)dx);
            if (d > rad || mc_set(cx, cy) != 12 || mc_shade(cx, cy))
                continue;
            if (soul_group_get(cx, cy) == 0) {
                if (best == 0xff) {
                    bx = (uint8_t)cx;
                    by = (uint8_t)cy;
                    fresh = 1;
                    best = d;
                } else if (rand_range(0, d + 1) == 0 && (uint32_t)rand_range(1, 0x1fff) > souls_groups_created) {
                    bx = (uint8_t)cx;
                    by = (uint8_t)cy;
                    best = d;
                }
            } else if (rand_range(0, d + 1) == 0 || best == 0xff || fresh) {
                fresh = 0;
                bx = (uint8_t)cx;
                by = (uint8_t)cy;
                best = d;
            }
        }
    }
    if (ox)
        *ox = bx;
    if (oy)
        *oy = by;
    return bx;
}

unsigned area_search(int x, int y, unsigned size, unsigned radius, int *ox, int *oy)
{
    return souls_area_search(x, y, size, radius, ox, oy);
}

/* ---- Groups (0x46094e, 0x4609c7) ------------------------------------------- */

/* 0x46094e: moves the free-group cursor to the next free group */
static void group_next_free(void)
{
    int32_t start = next_free_group;
    do {
        next_free_group++;
        if (next_free_group > 0x1fff)
            next_free_group = 1;
    } while (g_state.soul_groups[next_free_group].count != 0 && next_free_group != start);
    souls_groups_created++;
}

/* 0x4609c7 */
unsigned soul_group_add(uint32_t n, unsigned cat, int x, int y)
{
    PlaneStats *r = realm_cur();
    unsigned g = soul_group_get(x, y);
    cat &= 15;
    if (g == 0) {
        g = (unsigned)next_free_group;
        group_next_free();
    }
    r->unk_7ea8 += n;
    r->unk_7eac[cat] += n;
    soul_group_set(x, y, g);
    if ((int8_t)r->unk_7e48[cat][0] * 100 + (int8_t)r->unk_7e48[cat][1] < (int8_t)x * 100 + (int8_t)y) {
        r->unk_7e68[cat] = g;
        r->unk_7e48[cat][0] = (uint8_t)x;
        r->unk_7e48[cat][1] = (uint8_t)y;
    }
    SoulGroup *s = &g_state.soul_groups[g & (GS_SOUL_GROUPS - 1)];
    s->count += n;
    s->mask |= (uint16_t)(1u << cat);
    return g;
}

/* ---- Leaving zones ---------------------------------------------------------------- */

/* 0x45ef44: where a resident of zone kind `kind`, belief j, goes when it
 * leaves (souls.h): Hell moves it to the next worse sin (by rank), Heaven to the
 * next better virtue; at the end of the line (or j >= 12) a Karma station
 * (14) or Gate (15); -1 = back to the planet (reincarnation, 0x45f074);
 * j 8..11 give 0 (never asked). */
int souls_zone_exit_cat(uint8_t kind, uint8_t j)
{
    if (j >= 12)
        return 0xe;
    if (j >= 8)
        return 0;
    if (souls_in_hell()) {
        if ((int8_t)g_state.unk_593240[6] == (int)kind) {
            if (j == 0 || j == 4)
                return 0xf;
            if (j == 5 || j == 6)
                return 0xe;
            return -1;
        }
        return (int8_t)souls_rank_kind((int8_t)g_state.unk_57e060[kind % 7] + 1);
    }
    if ((int8_t)g_state.unk_593240[0] == (int)kind)
        return (j < 4 || j > 7) ? -1 : 0xe;
    return (int8_t)souls_rank_kind((int8_t)g_state.unk_57e060[kind % 7] - 1);
}

/* 0x5fa660[j]: the original reads 4 entries and, for j >= 4, the u32s
 * that follow (the start of Hell's sim cells, 0x5fa670). */
static uint32_t planet_weight_total(int j)
{
    if (j >= 0 && j < 4)
        return g_state.unk_5fa660[j];
    if (j >= 4 && j < 8)
        return sim_cell_u32(&g_state.plane[GS_PLANE_HELL].sim[0], (j - 4) * 4 > 14 ? 14 : (j - 4) * 4);
    return 0;
}
/* 0x5bb210 + (j - 1) * 0x1c + k * 4: three 7-entry tables (j 1..3);
 * further j read unknown globals (0 here). */
static uint32_t planet_weight(int j, int k)
{
    switch (j) {
    case 1:
        return g_state.unk_5bb210[k];
    case 2:
        return g_state.unk_5bb22c[k];
    case 3:
        return g_state.unk_5bb248[k];
    default:
        return 0;
    }
}

/* 0x45f074: n residents of belief j of the zone building cell (cx, cy)
 * go back to the planet (reincarnate): split by the planet's weights
 * (j 0: 0x6303d0[14], categories 0-6 counted in 0x638670[] and sent to
 * Gates (15), 7-13 as they are; else 0x5bb210 tables, categories 7-13),
 * put on the road at (rx, ry). Returns the last group used. */
unsigned souls_reincarnate(uint32_t n, int8_t j, int rx, int ry, uint8_t cx, uint8_t cy)
{
    PlaneStats *r = realm_cur();
    unsigned last = 0;
    if (n == 0)
        return 0;
    uint32_t w = planet_weight_total(j);
    uint8_t e = mc_set(cx, cy) == 0 ? (uint8_t)cell_origin_index(cx, cy) : (uint8_t)mc_entry(cx, cy);
    int special = e > 0x5a;
    uint8_t kind = special ? (uint8_t)sc_zone_kind(cx, cy) : (uint8_t)(e / 13);
    if (w == 0)
        return 0;
    if (j == 0) {
        const uint32_t *wt = &g_state.unk_6303d0[0][0];
        for (int i = 0; i < 14; i++) {
            uint32_t t = mul_div_clamp(wt[i], w, n);
            if (t == 0)
                continue;
            int c = i;
            if (i < 7) {
                g_state.unk_638670[i] += t;
                g_state.unk_638670[7] += t;
                c = 15;
            }
            r->unk_7a08[c].v[0] += t;
            r->unk_7a08[c].unk_00 += t;
            leave_perm(cx, cy, t, special, kind);
            last = leave_to_road(t, (unsigned)c, rx, ry);
        }
    } else {
        for (int i = 0; i < 7; i++) {
            uint32_t t = mul_div_clamp(planet_weight(j, i), w, n);
            if (t == 0)
                continue;
            r->unk_7a08[i + 7].v[j & 15] += t;
            r->unk_7a08[i + 7].unk_00 += t;
            leave_perm(cx, cy, t, special, kind);
            last = leave_to_road(t, (unsigned)(i + 7), rx, ry);
        }
    }
    return last;
}

/* Zone kinds of a building's cells in footprint order (as souls.c). */
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
    if (entry < 0x60)
        return 1;
    kinds[1] = (uint8_t)sc_zone_kind(x + 1, y);
    kinds[2] = (uint8_t)sc_zone_kind(x + 1, y - 1);
    kinds[3] = (uint8_t)sc_zone_kind(x, y - 1);
    if (entry < 0x6d)
        return 4;
    kinds[4] = (uint8_t)sc_zone_kind(x + 2, y - 2);
    kinds[5] = (uint8_t)sc_zone_kind(x + 2, y - 1);
    kinds[6] = (uint8_t)sc_zone_kind(x + 2, y);
    return 7;
}

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

/* One step of the departure test: of `share` residents, how many leave
 * by chance (0x45f382: the cell's +0x11 rate per 0x421, times the realm's
 * unk_80f8 percent, rounded up by a draw when 0). */
static uint32_t departures(uint8_t rate, uint8_t pct, uint32_t share, uint32_t limit)
{
    uint32_t d = (uint32_t)pct * mul_div_clamp(rate, 0x421, share) / 100;
    if (d == 0) {
        unsigned u = rate < 2 ? 1 : rate;
        if ((uint32_t)rand_range(0, (int)(0x421 / u)) < limit)
            d = 1;
    }
    return d;
}

/* 0x45f382: one zone building (origin (x, y)) in the zone scan. Its
 * permanent residents are split over the beliefs j by the realm's mix
 * (unk_77e0[j] of the population); of each share, the oldest age buckets
 * v[0] and v[1] are due: v[0] leave in proportion, of v[1] a rate-based
 * part. Leavers go on to the next place (zone_exit_cat) on the nearest
 * road; without a road they are lost. The rest of the share counts age
 * steps in unk_04 for 0x445416. The building's SOUL-years (+10) shrink. */
static void zone_cell_update(uint8_t x, uint8_t y)
{
    PlaneStats *r = realm_cur();
    uint32_t pop = realm_building_pop(x, y);
    if (pop == 0)
        return;
    uint32_t yps = realm_building_years_per_soul(x, y);
    uint32_t rem = sc_u32(x, y, 10) % pop;
    uint32_t left_total = 0;
    unsigned e = mc_entry(x, y);
    uint8_t size = (uint8_t)tile_index_footprint(e);
    uint8_t kinds[7];
    int special;
    int nk = zone_kinds(e, x, y, kinds, &special);
    uint32_t rpop = r->population;
    uint8_t pct = r->unk_80f8;
    int rx, ry;
    bool noroad = souls_area_search(x, y, size, 3, &rx, &ry) == AREA_NONE;
    uint8_t cx = x, cy = y;
    uint8_t rate = sc_u8(x, y, 0x11);
    for (int i = 0; i < nk; i++) {
        uint8_t k = kinds[i];
        uint32_t perm;
        if (k != 7 && (perm = sc_u32(x, y, 2)) != 0) {
            for (int j = 0; j < 16; j++) {
                uint32_t w;
                if (!((j < 8 || j > 11) && (w = r->unk_77e0[j]) != 0))
                    continue;
                uint32_t share;
                if (w == rpop) {
                    share = perm;
                } else {
                    share = mul_div_clamp(w, rpop, perm);
                    if (perm < share)
                        share = perm;
                }
                if (share == 0)
                    continue;
                PlaneStatHist *h = &r->hist_a[(k % 7) * 16 + j];
                uint32_t tot = h->unk_00;
                if (tot == 0)
                    continue;
                uint32_t a = h->v[0];
                uint32_t d = mul_div_clamp(a, tot, share);
                if (a < d)
                    d = a;
                if (share < d)
                    d = share;
                uint32_t got = d;
                h->v[0] -= d;
                uint32_t moved = d;
                a = h->v[1];
                d = mul_div_clamp(a, tot, share);
                if (a < d)
                    d = a;
                got += d;
                if (d != 0)
                    d = departures(sc_u8(x, y, 0x11), pct, d, share);
                if (a < d)
                    d = a;
                if (share < got + d)
                    d = share - got;
                h->v[1] -= d;
                moved += d;
                r->unk_7820[k % 7] -= moved;
                g_state.unk_5fa600[k % 7] += moved;
                r->unk_77e0[j] -= moved;
                g_state.unk_5b2f10[j] += moved;
                if (moved != 0) {
                    left_total += moved;
                    if (noroad)
                        souls_stats.departed_lost += moved;
                    else
                        souls_stats.departed += moved;
                    if (!noroad) {
                        int c = souls_zone_exit_cat(k, (uint8_t)j);
                        if ((int8_t)c == -1) {
                            souls_reincarnate(moved, (int8_t)j, rx, ry, cx, cy);
                        } else {
                            r->unk_7a08[c & 15].v[j] += moved;
                            r->unk_7a08[c & 15].unk_00 += moved;
                            leave_perm(cx, cy, moved, special, k);
                            leave_to_road(moved, (unsigned)(c & 15), rx, ry);
                        }
                    }
                    h->unk_00 -= moved;
                }
                if (got < share) {
                    share -= got;
                    uint32_t st = departures(sc_u8(x, y, 0x11), pct, share, share);
                    h->unk_04 += st;
                }
            }
        }
        footprint_next(i, &cx, &cy);
    }
    (void)rate;
    if (left_total != 0) {
        uint32_t d = sc_u32(x, y, 10);
        if (yps * left_total <= d)
            d = yps * left_total;
        sc_set_u32(x, y, 10, sc_u32(x, y, 10) - d);
        d = sc_u32(x, y, 10);
        if (rem >> 1 <= d)
            d = rem >> 1;
        sc_set_u32(x, y, 10, sc_u32(x, y, 10) - d);
    }
    if (noroad && left_total != 0) {
        r->unk_8113 += left_total;
        g_state.unk_626598 += left_total;
        r->unk_8123 += left_total;
    }
}

/* 0x45fd84 */
void souls_zone_evict(int x, int y, uint32_t n)
{
    PlaneStats *r = realm_cur();
    uint32_t pop = realm_building_pop(x, y);
    if (pop == 0)
        return;
    unsigned e = mc_entry(x, y);
    uint8_t size = (uint8_t)tile_index_footprint(e);
    uint8_t kinds[7];
    int special;
    int nk = zone_kinds(e, x, y, kinds, &special);
    uint32_t rpop = r->population;
    int rx, ry;
    bool noroad = souls_area_search(x, y, size, 3, &rx, &ry) == AREA_NONE;
    uint8_t cx = (uint8_t)x, cy = (uint8_t)y;
    for (int i = 0; i < nk; i++) {
        uint8_t k = kinds[i];
        if (k != 7) {
            for (int pass = 0; pass < 2; pass++) {
                uint32_t have = pass == 0 ? sc_u32(cx, cy, 6) : sc_u32(cx, cy, 2);
                uint32_t take;
                if (pop == n)
                    take = have;
                else if (have == 0)
                    take = 0;
                else
                    take = mul_div_clamp(have, pop, n);
                if (take == 0)
                    continue;
                if (pass == 0)
                    leave_temp(cx, cy, take, special, k);
                else
                    leave_perm(cx, cy, take, special, k);
                for (unsigned j = 0; j < 16; j++) {
                    uint32_t w;
                    if (!(((pass != 0 && (j < 8 || j > 11)) || (pass == 0 && (j & 4) == 0)) &&
                          (w = r->unk_77e0[j]) != 0))
                        continue;
                    uint32_t s;
                    if (rpop == w) {
                        s = take;
                    } else {
                        s = mul_div_clamp(w, rpop, take);
                        if (take < s)
                            s = take;
                    }
                    if (s == 0)
                        continue;
                    PlaneStatHist *h = &r->hist_a[(k % 7) * 16 + j];
                    PlaneStatHist *hb = &r->hist_b[(k % 7) * 16 + j];
                    uint32_t tot = h->unk_00;
                    uint32_t out = 0;
                    if (pass == 0) {
                        out = s;
                    } else if (tot != 0) {
                        uint32_t ratio = tot / s;
                        uint32_t carry = 0;
                        for (int b = 0; b < 32; b++) {
                            uint32_t vb = h->v[b];
                            uint32_t t = mul_div_clamp(carry + vb, tot, s);
                            if (vb < t)
                                t = vb;
                            if (s < t)
                                t = s;
                            if (t == 0)
                                carry += vb;
                            else if (ratio * t < carry)
                                carry -= ratio * t;
                            else
                                carry = 0;
                            h->v[b] -= t;
                            if (!noroad)
                                hb->v[b] += t;
                            out += t;
                        }
                    }
                    if (out == 0)
                        continue;
                    r->unk_7820[k % 7] -= out;
                    g_state.unk_5fa600[k % 7] += out;
                    r->unk_77e0[j] -= out;
                    g_state.unk_5b2f10[j] += out;
                    if (!noroad) {
                        unsigned c;
                        if (pass == 0) {
                            c = k + 7u;
                        } else {
                            r->unk_7a08[k % 7].v[j] += out;
                            r->unk_7a08[k % 7].unk_00 += out;
                            c = k;
                        }
                        leave_to_road(out, c, rx, ry);
                        hb->unk_00 += out;
                    }
                    if (h->unk_00 < out || pass == 0) {
                        h->unk_04 = h->unk_00;
                        h->unk_00 = 0;
                    } else {
                        h->unk_04 += out;
                        h->unk_00 -= out;
                    }
                }
            }
        }
        footprint_next(i, &cx, &cy);
    }
    if (noroad && n != 0) {
        r->unk_8113 += n;
        r->unk_8123 += n;
        g_state.unk_626598 += n;
    }
}

/* 0x4606cf: an Omnibolge / Love Domes cell (set 5) lets 1/20 of its SOULs
 * (+5; in Hell also +9, which go on to Heaven) out of the realm's pool. */
static void omnibolge_drain(int x, int y)
{
    PlaneStats *r = realm_cur();
    uint32_t d = sc_u32(x, y, 5) / 20;
    sc_set_u32(x, y, 5, sc_u32(x, y, 5) - d);
    bignum_sub(&r->souls, d);
    if (g_world.plane == GS_PLANE_HELL) {
        d = sc_u32(x, y, 9) / 20;
        souls_to_heaven += d;
        sc_set_u32(x, y, 9, sc_u32(x, y, 9) - d);
        bignum_sub(&r->souls, d);
    }
}

/* 0x4607d6 sim_zone_scan */
bool souls_zone_scan(void)
{
    unsigned n = 0;
    bool stop = false;
    uint8_t x = scan_x, y = scan_y;
    do {
        do {
            if (!mc_shade(x, y)) {
                unsigned s = mc_set(x, y);
                if (s == 5) {
                    omnibolge_drain(x, y);
                } else if (s > 13 && s < 16) {
                    zone_cell_update(x, y);
                    if (++n > 49)
                        stop = true;
                }
            }
            if (!stop)
                y++;
        } while (y < 100 && !stop);
        if (!stop) {
            y = 0;
            x++;
        }
    } while (x < 100 && !stop);
    scan_x = x;
    scan_y = y;
    if (!stop) {
        scan_x = scan_y = 0;
        realm_age_departures();
    }
    return !stop;
}

/* ---- Arrivals --------------------------------------------------------------------- */

/* A Gate's capacity, halved when CELL_OVERLAY (bit 2) is set. */
static uint32_t gate_capacity(int x, int y)
{
    unsigned i = mc_entry(x, y) >> 1;
    uint32_t c = i < 8 ? g_tables.set13[i].capacity : 0;
    return mc_bit2(x, y) ? (uint32_t)((int32_t)c >> 1) : c;
}
static unsigned gate_size(int x, int y)
{
    unsigned i = mc_entry(x, y) >> 1;
    return i < 8 ? g_tables.set13[i].size : 0;
}

/* 0x46114f */
void souls_gate_arrivals(void)
{
    PlaneStats *r = realm_cur();
    uint8_t n = r->n_ports;
    if (n == 0)
        return;
    uint32_t cap = 0;
    for (unsigned i = 0; i < n && i < 50; i++) {
        uint8_t px = r->ports[i][0], py = r->ports[i][1];
        if (!mc_shade(px, py) && souls_area_search(px, py, gate_size(px, py), 1, NULL, NULL) != AREA_NONE)
            cap += gate_capacity(px, py);
    }
    if (cap == 0)
        return;
    uint32_t total = 0;
    for (int c = 0; c < 16; c++)
        total += r->unk_7a08[c].unk_00;
    if (total == 0)
        return;
    uint32_t excess = cap < total ? total - cap : 0;
    for (unsigned i = 0; i < n && i < 50; i++) {
        uint8_t px = r->ports[i][0], py = r->ports[i][1];
        if (mc_shade(px, py))
            continue;
        int rx, ry;
        if (souls_area_search(px, py, gate_size(px, py), 1, &rx, &ry) == AREA_NONE)
            continue;
        uint32_t c54 = gate_capacity(px, py);
        uint32_t share = excess == 0 ? mul_div_clamp(c54, cap, total) : c54;
        for (unsigned c = 0; c < 16; c++) {
            if (r->unk_7a08[c].unk_00 == 0)
                continue;
            uint32_t t = mul_div_clamp(r->unk_7a08[c].unk_00, total, share);
            if (t != 0) {
                sc_add_u32(px, py, 1, t);
                souls_gate_arrived += t;
                r->unk_810b += t;
                soul_group_add(t, c, rx, ry);
            }
        }
        if (cap < total)
            sc_set_u32(px, py, 1, c54);
    }
}

/* 0x461636 */
bool souls_gate_spawn(uint32_t n, unsigned cat, int j)
{
    PlaneStats *r = realm_cur();
    if (r->n_ports == 0)
        return false;
    int last = (int8_t)r->n_ports - 1;
    if ((int8_t)r->unk_8045 <= last)
        last = (int8_t)r->unk_8045;
    r->unk_8045 = (uint8_t)last;
    uint8_t i = r->unk_8045;
    unsigned found = AREA_NONE, alt = AREA_NONE;
    int fx = 0, fy = 0, ax = 0, ay = 0;
    uint8_t px = 0, py = 0, bx = 0, by = 0;
    uint32_t best = 0;
    do {
        i++;
        if ((int)(int8_t)r->n_ports <= (int)i)
            i = 0;
        px = r->ports[i % 50][0];
        py = r->ports[i % 50][1];
        if (mc_shade(px, py))
            continue;
        uint32_t cap = gate_capacity(px, py);
        if (cap < sc_u32(px, py, 1) + n) {
            uint32_t room = cap - sc_u32(px, py, 1);
            int rx, ry;
            if (best < room && souls_area_search(px, py, gate_size(px, py), 1, &rx, &ry) != AREA_NONE) {
                by = py;
                bx = px;
                best = room;
                alt = (unsigned)rx;
                ax = rx;
                ay = ry;
            }
        } else {
            found = souls_area_search(px, py, gate_size(px, py), 1, &fx, &fy);
        }
    } while (found == AREA_NONE && (int)(int8_t)r->unk_8045 != (int)i);
    if (found == AREA_NONE) {
        if (alt == AREA_NONE)
            return false;
        n = best;
        px = bx;
        py = by;
        fx = ax;
        fy = ay;
    }
    sc_add_u32(px, py, 1, n);
    if ((int8_t)j >= 0)
        r->unk_7a08[cat & 15].v[j & 15] += n;
    r->unk_7a08[cat & 15].unk_00 += n;
    soul_group_add(n, cat, fx, fy);
    r->unk_8045 = i;
    souls_gate_arrived += n;
    r->unk_810b += n;
    return true;
}

/* g_build.special_pos[i] (0x5bb0f0): where special structure i stands */
static void special_pos(int i, int *x, int *y)
{
    *x = g_build.special_pos[i][0];
    *y = g_build.special_pos[i][1];
}

/* 0x460b16: each siphon (set 30) puts the SOULs it holds (+1) back on a
 * road (+0xd/+0xe remember it), the realm's waiting categories
 * (unk_7ef0[]) in proportion; a siphon over its limit (capacity * 42,
 * halved in Hell near special structure 8, doubled in Heaven near 3 when
 * those are active, g_state.unk_638668) bursts. */
void souls_siphon_release(void)
{
    PlaneStats *r = realm_cur();
    uint32_t left = r->unk_7f30;
    uint32_t pool = r->unk_7f38;
    if (left == 0 || pool == 0)
        return;
    bool roads = false, found = false;
    uint8_t fx = 0, fy = 0;
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++)
            if (mc_set(x, y) == 12) {
                roads = true;
                if (fx == 0) {
                    if (rand_range(0, 10) == 0) {
                        fx = (uint8_t)x;
                        fy = (uint8_t)y;
                    }
                } else {
                    fx = (uint8_t)x;
                    fy = (uint8_t)y;
                }
            }
    if (!roads)
        return;
    for (int x = 0; x < 100; x++) {
        for (int y = 0; y < 100; y++) {
            if (mc_set(x, y) != 30)
                continue;
            unsigned e = mc_entry(x, y);
            uint32_t cap = e < 3 ? g_tables.set30[e].capacity : 0;
            unsigned size = e < 3 ? g_tables.set30[e].size : 0;
            uint32_t lim = cap * 0x2a;
            int sx, sy;
            if (g_world.plane == GS_PLANE_HELL) {
                if (g_state.unk_638668 & 0x100) {
                    special_pos(8, &sx, &sy);
                    if (abs((sy - 1) - y) + abs((sx + 1) - x) < 12)
                        lim >>= 1;
                }
            } else if (g_state.unk_638668 & 8) {
                special_pos(3, &sx, &sy);
                if (abs((sx + 1) - x) + abs((sy - 1) - y) < 12)
                    lim = cap * 0x54;
            }
            if (lim < sc_u32(x, y, 9))
                structure_destroy(x, y, 3, 0);
            uint32_t cnt = sc_u32(x, y, 1);
            if (cnt != 0) {
                int8_t tx = (int8_t)sc_u8(x, y, 0xd), ty = (int8_t)sc_u8(x, y, 0xe);
                int rx = AREA_NONE, ry = AREA_NONE;
                bool search = false;
                if (tx < 0) {
                    if (found) {
                        rx = fx;
                        ry = fy;
                    } else {
                        search = true;
                    }
                } else if (mc_set(tx, ty) == 12) {
                    rx = tx;
                    ry = ty;
                } else if (found) {
                    rx = fx;
                    ry = fy;
                } else {
                    search = true;
                }
                if (search) {
                    found = true;
                    if (souls_area_search(x, y, size, 0x62, &rx, &ry) != AREA_NONE) {
                        sc_set_u8(x, y, 0xd, (uint8_t)rx);
                        sc_set_u8(x, y, 0xe, (uint8_t)ry);
                    }
                }
                if (rx != AREA_NONE) {
                    for (unsigned c = 0; c < 16; c++) {
                        uint32_t t;
                        if (r->unk_7ef0[c] != 0 && (t = mul_div_clamp(r->unk_7ef0[c], pool, cnt)) != 0) {
                            soul_group_add(t, c, rx, ry);
                            r->unk_7ef0[c] -= t;
                            r->unk_7f38 -= t;
                        }
                    }
                }
                sc_set_u32(x, y, 1, 0);
            }
            if (--left == 0) {
                r->unk_7f38 = 0;
                return;
            }
        }
    }
    (void)umin;
}
