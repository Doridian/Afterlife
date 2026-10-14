/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The realm year: game/economy.h.
 */
#include "game/economy.h"

#include <stdio.h>

#include "game/badthings.h"
#include "game/build.h"
#include "game/mapobj.h"
#include "game/realm.h"
#include "game/sim.h"
#include "game/structure.h"
#include "game/tables.h"
#include "game/util.h"
#include "game/world.h"

/* The pass state (0x4f30cc..0x4f31b4 in the original), reset when a pass
 * starts at column 0. */
static uint8_t s_col;              /* 0x65ede8: next column */
static uint32_t s_penalty;         /* 0x4f30cc */
static uint32_t s_satisfaction;    /* 0x4f30f8: set-8 (150 - sim +0xd) x workers */
static uint32_t s_roads;           /* 0x4f3130 */
static uint32_t s_jobs;            /* 0x4f30d0 */
static uint32_t s_training;        /* 0x4f3134: training capacity */
static uint32_t s_centres;         /* 0x4f3184: active training centres */
static uint32_t s_set8_workers;    /* 0x4f3140 */
static uint16_t s_variety;         /* 0x4f3104 */
static uint32_t s_jobs_set[9];     /* 0x4f3124 zones, 0x4f3160, 0x4f3128, 0x4f3150, 0x4f3164, 0x4f310c,
                                      0x4f312c, 0x4f3118, 0x4f31b4: in RW_JOBS_* order */
static uint8_t s_mask_misc;        /* 0x4f3154 */
static uint8_t s_mask_set8;        /* 0x4f3148 */
static uint8_t s_mask_set7;        /* 0x4f315c */
static uint8_t s_mask_set19;       /* 0x4f3110 */
static uint8_t s_mask_set30;       /* 0x4f3158 */
static uint8_t s_mask_set13;       /* 0x4f314c */
static uint8_t s_mask_set9;        /* 0x4f3168 */
static uint32_t s_mask_zones[8];   /* 0x4f30d8: levels seen per zone kind, [7] special structures */
/* realm +0x812b / +0x812f, past the saved record: the "Over 90%
 * homegrown" warning is showing / enabled (game_reset_world: 1) */
static uint8_t s_warned[4];
static uint8_t s_warn_on[4] = {1, 1, 1, 1};

void economy_reset(void)
{
    s_col = 0;
    for (int i = 0; i < 4; i++)
        s_warned[i] = 0, s_warn_on[i] = 1;
}

static int message(const char *title, const char *text, const char *buttons)
{
    return g_sim_hooks.message ? g_sim_hooks.message(title, text, buttons) : 1;
}

static void views_changed(void)
{
    if (g_sim_hooks.views_changed)
        g_sim_hooks.views_changed();
}

/* x86 SHL masks the count to 5 bits */
static uint32_t bit(unsigned n) { return 1u << (n & 31); }

/* the "first of its kind" bonus of the variety score */
static void variety8(uint8_t *mask, unsigned b, unsigned n)
{
    if ((bit(b) & *mask) == 0) {
        *mask |= (uint8_t)bit(b);
        s_variety = (uint16_t)(n * n + s_variety);
    }
}

static uint32_t halve_if(uint32_t c, uint32_t v) { return (c & 4) ? (uint32_t)((int32_t)v >> 1) : v; }

static void start_pass(PlaneStats *r)
{
    s_penalty = s_satisfaction = s_roads = s_jobs = s_training = s_centres = s_set8_workers = 0;
    s_variety = 0;
    for (int i = 0; i < 9; i++)
        s_jobs_set[i] = 0;
    s_mask_misc = s_mask_set8 = s_mask_set7 = s_mask_set19 = s_mask_set30 = s_mask_set13 = s_mask_set9 = 0;
    for (int i = 0; i < 8; i++) {
        r->unk_7fac[i] = 0;
        r->unk_7fcc[i] = 0;
        s_mask_zones[i] = 0;
    }
    for (int i = 0; i < 5; i++) {
        r->unk_8077[i] = 0;
        r->unk_804f[i] = 0;
        r->unk_8063[i] = 0;
    }
    r->n_zones = 0;
    r->unk_80e7 = 0;
    r->unk_80eb = 0;
}

static void add_jobs(int which, uint32_t n)
{
    s_jobs += n;
    s_jobs_set[which - RW_JOBS_ZONES] += n;
}

/* set 19: a bank's loan (sim +1 amount, +5 instalments left / state, +6
 * interest per mille) */
static void bank_year(int x, int y)
{
    SimCell *s = sim_cell(x, y);
    uint32_t amt = sim_cell_u32(s, 1);
    if (cell_test_shade(x, y) || amt == 0)
        return;
    PlaneStats *r = realm_cur();
    int8_t left = (int8_t)s->b[5], rate = (int8_t)s->b[6];
    if (g_world.plane == GS_PLANE_HEAVEN) {
        if (left < 2) {
            sim_cell_set_u32(s, 1, 0);
            if (r->unk_80ef > 100)
                r->unk_80ef = 0;
            if (r->unk_80ef != 0) {
                r->unk_80ef--;
                if (r->unk_80ef == 0)
                    message(NULL, "You've paid off your final debt to Heaven's Banks!", "&Spiffy!");
                else
                    message(NULL, "You've paid off your debt to one of Heaven's Banks!", "&Groovy!");
                views_changed();
            }
        } else {
            uint32_t pay = amt / (uint32_t)(int32_t)left;
            bignum_sub(&g_state.pennies, pay);
            g_state.unk_5f7ed4 += pay;
            sim_cell_set_u32(s, 1, (uint32_t)(rate + 1000) * ((amt - pay) / 1000));
            s->b[5]--;
        }
        return;
    }
    if (left < 0) {
        if (amt < g_state.unk_630420) {
            message(NULL, "You've paid off your debt to one of Hell's Banks!", "&Finally!");
            r->unk_80ef--;
            g_state.unk_630420 -= amt;
            sim_cell_set_u32(s, 1, 0);
            views_changed();
        } else {
            sim_cell_set_u32(s, 1, sim_cell_u32(s, 1) - g_state.unk_630420);
            g_state.unk_630420 = 0;
        }
    }
    if (left == 1) {
        uint32_t v = g_state.unk_630430 ? ((uint32_t)(rate + 10) * (amt / 10) * 0x100) / g_state.unk_630430 : 0;
        g_state.unk_57e058 += v;
        sim_cell_set_u32(s, 1, v);
        char num[FMT_NUM_LEN], text[240];
        snprintf(text, sizeof text,
                 "You've defaulted on a loan from a Hell Bank.  As punishment, the Powers That Be will garnish your "
                 "wages for the next %s SOULs!",
                 fmt_thousands(num, v));
        message(NULL, text, "&Heck!");
        s->b[5] = 0xff;
        views_changed();
    }
    if (left > 0)
        s->b[5]--;
}

/* sets 14/15: a zone building or special structure */
static void zone_year(int x, int y, uint32_t c, unsigned n, uint8_t *last_level)
{
    PlaneStats *r = realm_cur();
    unsigned e = (c >> 16) & 0x7f;
    const RealmTileInfo *ti = &g_world.tile_info[e];
    uint8_t hs = sim_happiness_sum(x, y, n);
    if ((c & 0x8024) == 0) {
        uint32_t pop = realm_building_pop(x, y);
        if ((cell_get(x, y) & 2) == 0) {
            if (!zone_entry_is_empty(e)) {
                add_jobs(RW_JOBS_ZONES, ti->unk_08);
                uint32_t pct = ti->cost ? pop * 100 / ti->cost + 1 : 1;
                if (!sim_test_flag_e7(x, y)) {
                    if (!cell_test_shade(x, y) && (uint32_t)rand_range(0x32, 100) <= pct && rand_range(0, 4) == 0)
                        realm_zone_grow(x, y, e, n, hs);
                } else {
                    s_penalty += zone_entry_level(e);
                }
            } else if (!realm_secret_structure(x, y, e) && pop != 0) {
                s_jobs += ti->unk_08;
                realm_zone_grow(x, y, e, n, hs);
            }
        } else if (rand_range(0, 100) == 0 && !zone_entry_is_empty(e)) {
            if (!cell_test_shade(x, y))
                realm_building_ruin(x, y, n);
        } else {
            add_jobs(RW_JOBS_ZONES, ti->unk_08);
        }
    } else if ((c & 4) && rand_range(0, 10) == 0) {
        if (!sim_test_flag_e7(x, y)) {
            if (!cell_test_shade(x, y))
                realm_zone_decay(x, y, e, n, hs);
        } else {
            s_penalty += zone_entry_level(e);
        }
    }
    /* the building may have changed: the rest uses the values from before */
    SimCell *s = sim_cell(x, y);
    if (e < 0x5b) {
        if (e % 13 != 0) {
            unsigned v = (unsigned)hs * 2;
            if (v > 0xfd)
                v = 0xfe;
            s->b[0x11] = (uint8_t)(v + 1);
            r->unk_80eb += realm_building_years_per_soul(x, y);
        }
        unsigned kind = e / 13;
        /* the original computes e - 13 * (a stale byte), not e % 13 */
        *last_level = (uint8_t)(e - (unsigned)*last_level * 13);
        if ((s_mask_zones[kind] & bit(*last_level)) == 0) {
            s_mask_zones[kind] |= bit(*last_level);
            s_variety = (uint16_t)(n * n + s_variety);
        }
        r->unk_7fac[kind] += ti->cost;
        if (cell_test_shade(x, y))
            r->unk_7fcc[kind] += ti->cost;
        r->n_zones++;
        return;
    }
    if (e != 0x5b) {
        r->unk_80eb += realm_building_years_per_soul(x, y);
        s->b[0x11] = (uint8_t)((hs >> 1) + 1);
    }
    r->n_zones++;
    int32_t spare = (int32_t)(ti->cost - realm_building_pop(x, y));
    uint8_t mask = 0;
    *last_level = (uint8_t)(e - 0x5b);
    if ((s_mask_zones[7] & bit(*last_level)) == 0) {
        s_mask_zones[7] |= bit(*last_level);
        s_variety = (uint16_t)(n * n + s_variety);
    }
    for (unsigned lx = (unsigned)x; lx < (unsigned)x + n; lx++)
        for (int ly = y; y - (int)n < ly; ly--) {
            if (lx >= 100 || ly < 0)
                continue;
            unsigned k = sim_zone_kind((int)lx, ly);
            if (k == 7 || (mask & bit(k)) != 0)
                continue;
            mask |= (uint8_t)bit(k);
            if (k >= 8)
                continue; /* the original writes past unk_7fac / unk_7fcc */
            uint32_t p = sim_pop((int)lx, ly);
            r->unk_7fac[k] += (uint32_t)spare + p;
            if (cell_test_shade(x, y))
                r->unk_7fcc[k] += (uint32_t)spare + p;
        }
}

static void cell_year(int x, int y, uint8_t *last_level)
{
    PlaneStats *r = realm_cur();
    const SimTables *t = &g_tables;
    unsigned set = cell_tileset(x, y);
    if (set != 0 && cell_test_bracket(x, y) && rand_range(0, 10) == 0) {
        structure_destroy(x, y, 3, 5);
        set = cell_tileset(x, y);
    }
    uint32_t c = cell_get(x, y);
    unsigned n = (unsigned)structure_size(x, y) & 0xff;
    unsigned b = (c >> 16) & 0xff; /* entry byte with bit 7 */
    unsigned e = b & 0x7f;
    bool shade = cell_test_shade(x, y) != 0;
    bool idle = shade || (c & 2) != 0;
    SimCell *s = sim_cell(x, y);
    switch (set) {
    case 3:
        if ((s_mask_misc & 0x20) == 0) {
            s_mask_misc |= 0x20;
            s_variety = (uint16_t)(s_variety + 0x31);
        }
        break;
    case 7: {
        variety8(&s_mask_set7, b, n);
        uint32_t cap = halve_if(c, e < 8 ? t->set7[e].capacity : 0);
        s_training += cap;
        if (!shade)
            s_centres++;
        else
            r->unk_8063[2] += cap;
        break;
    }
    case 8: {
        variety8(&s_mask_set8, b, n);
        const BuildingSpec *sp = &t->set8[e % 6];
        add_jobs(RW_JOBS_SET8, sp->jobs);
        if (!idle) {
            uint32_t w = sim_cell_u32(s, 1);
            s_set8_workers += w;
            sim_cell_set_u32(s, 9, sim_cell_u32(s, 9) + w);
            sim_cell_set_u32(s, 5, w);
            r->unk_8077[4] += w;
            s_satisfaction += (0x96 - (s->b[0xd] & 0x7f)) * w;
        }
        uint32_t cap = halve_if(c, sp->capacity);
        r->unk_804f[4] += cap;
        if (idle)
            r->unk_8063[4] += cap;
        sim_cell_set_u32(s, 1, 0);
        break;
    }
    case 9: {
        variety8(&s_mask_set9, b, n);
        const BuildingSpec *sp = &t->set9[e % 6];
        add_jobs(RW_JOBS_SET9, sp->jobs);
        uint32_t cap = halve_if(c, sp->capacity);
        r->unk_804f[3] += cap;
        if (idle)
            r->unk_8063[3] += cap;
        if (!idle) {
            sim_cell_set_u32(s, 5, sim_cell_u32(s, 1));
            r->unk_8077[3] += sim_cell_u32(s, 5);
        }
        break;
    }
    case 12:
        if (!shade) {
            road_autotile(x, y, false, true, true);
            uint32_t v = sim_cell_u32(s, 9);
            int d = 0;
            if (v != 0) {
                int next = 0;
                do {
                    d = next;
                    v /= 10;
                    next = d + 2;
                } while (v != 0);
            }
            s_roads += bit((unsigned)d);
        }
        break;
    case 13: {
        variety8(&s_mask_set13, e >> 1, n);
        const BuildingSpec *sp = &t->set13[(e >> 1) & 7];
        add_jobs(RW_JOBS_SET13, sp->jobs);
        uint32_t cap = halve_if(c, sp->capacity);
        r->unk_804f[0] += cap;
        if (idle)
            r->unk_8063[0] += cap;
        if (!idle)
            r->unk_8077[0] += sim_cell_u32(s, 5);
        break;
    }
    case 14:
    case 15:
        zone_year(x, y, c, n, last_level);
        break;
    case 16:
        if ((s_mask_misc & 0x80) == 0) {
            s_mask_misc |= 0x80;
            s_variety++;
        }
        add_jobs(RW_JOBS_SET16, 0x78);
        break;
    case 17: {
        unsigned k = ((e - 4) >> 1) & 0xff;
        if (((uint32_t)s_mask_misc & bit(k)) == 0) {
            s_mask_misc |= (uint8_t)bit(k);
            s_variety = (uint16_t)(s_variety + 9);
        }
        add_jobs(RW_JOBS_SET17, k < 6 ? t->set17[k].jobs : 0);
        unsigned i = s->b[10];
        uint32_t cap = halve_if(c, i < 6 ? t->set17[i].capacity : 0);
        r->unk_804f[1] += cap;
        if (idle)
            r->unk_8063[1] += cap;
        if (!idle) {
            if (s->b[9] == 0)
                r->unk_80e7 = 1;
            else
                r->unk_8077[1] += sim_cell_u32(s, 5);
        }
        break;
    }
    case 19:
        variety8(&s_mask_set19, b, n);
        /* indexes the port table by the entry, unlike 0x445f2f */
        add_jobs(RW_JOBS_SET19, t->set13[e & 7].jobs);
        bank_year(x, y);
        break;
    case 0x1d:
        if ((s_mask_misc & 0x40) == 0) {
            s_mask_misc |= 0x40;
            s_variety++;
        }
        add_jobs(RW_JOBS_SET29, 0xf0);
        break;
    case 0x1e:
        variety8(&s_mask_set30, b, n);
        add_jobs(RW_JOBS_SET30, (uint32_t)((int32_t)t->set30[e % 3].jobs / 1000));
        break;
    default:
        break;
    }
    if ((c & 4) && set != 0 && set != 10 && rand_range(0, 0x46) == 0) {
        for (unsigned lx = (unsigned)x; lx < (unsigned)x + n; lx++)
            for (int ly = y; y - (int)n < ly; ly--)
                if (lx < 100 && ly >= 0)
                    cell_clear_flags((int)lx, ly, 4);
        if (g_sim_hooks.sound)
            g_sim_hooks.sound(0x10016, 1);
        mapobj_redraw(x, y, (int)n, 0, 0, 0, 1);
    }
}

/* 0x43ed55 */
bool economy_realm_year(void)
{
    PlaneStats *r = realm_cur();
    if (s_col == 0)
        start_pass(r);
    uint8_t col = s_col;
    uint8_t last_level = 0; /* a stack byte of the original, see zone_year */
    uint32_t t0 = sim_clock_ms();
    do {
        for (int y = 99; y >= 0; y--)
            cell_year(col, y, &last_level);
        col++;
    } while (sim_clock_ms() - t0 < 100 && col < 100);
    s_col = col;
    if (col < 100)
        return false;

    int ri = g_world.realm_plane & 3;
    r->unk_8127 = s_penalty;
    bignum_sub(&g_state.pennies, s_penalty);
    g_state.unk_5f7ed4 += s_penalty;
    r->unk_811f = s_variety;
    s_col = 0;
    realm_specials_dedupe();

    uint32_t *w = r->unk_7ff0;
    r->unk_804f[2] = s_training;
    s_training -= r->unk_8063[2];
    w[RW_TRAINED] = s_training <= s_jobs ? s_training : s_jobs;
    r->unk_8077[2] = w[RW_TRAINED];
    w[RW_IMPORTED] = s_jobs - w[RW_TRAINED];
    if (w[RW_WORKFORCE] + w[RW_SURPLUS] + s_set8_workers < s_jobs) {
        w[RW_WORKFORCE] += w[RW_SURPLUS] + s_set8_workers;
        w[RW_SHORTFALL] = s_jobs - w[RW_WORKFORCE];
        w[RW_SURPLUS] = 0;
        uint32_t pct = mul_div_clamp(w[RW_WORKFORCE], s_jobs, 100);
        if (pct < 0x5a || s_warned[ri] != 0) {
            if (pct < 0x51)
                s_warned[ri] = 0;
        } else if (s_warn_on[ri] != 0) {
            s_warned[ri] = 1;
            int btn;
            if (g_world.plane == GS_PLANE_HEAVEN)
                btn = message(NULL,
                              "Aria sez: 'Over 90% of your Angels are homegrown!  Maybe you should turn off your "
                              "Training Centers!'",
                              "~&Thanks!");
            else
                btn = message(NULL,
                              "Jasper sez: 'Over 90% of your Demons are homegrown!  Maybe you'd better turn off "
                              "your Training Centers!'",
                              "~&Thanks!");
            if (btn == 2)
                s_warn_on[ri] = 0;
        }
    } else {
        if (s_jobs < w[RW_WORKFORCE] + s_set8_workers)
            w[RW_SURPLUS] += (w[RW_WORKFORCE] + s_set8_workers) - s_jobs;
        else
            w[RW_SURPLUS] -= s_jobs - (w[RW_WORKFORCE] + s_set8_workers);
        w[RW_WORKFORCE] = s_jobs;
        w[RW_SHORTFALL] = 0;
    }

    w[RW_WAGES] = 0;
    uint32_t trainees = s_training;
    if (w[RW_WORKFORCE] + w[RW_SURPLUS] != 0) {
        uint32_t all = w[RW_WORKFORCE] + w[RW_SURPLUS];
        uint32_t tr = s_training <= all ? s_training : all;
        uint32_t other = all - tr;
        trainees = s_training - tr;
        if (tr != 0)
            w[RW_WAGES] += mul_div_clamp(w[RW_RATE_D] + w[RW_RATE_A], 0xff, tr);
        if (other != 0)
            w[RW_WAGES] += mul_div_clamp(w[RW_RATE_A] + w[RW_RATE_C], 0xff, other);
    }
    if (w[RW_SHORTFALL] != 0) {
        uint32_t tr = trainees <= w[RW_SHORTFALL] ? trainees : w[RW_SHORTFALL];
        uint32_t other = w[RW_SHORTFALL] - tr;
        if (tr != 0)
            w[RW_WAGES] += mul_div_clamp(w[RW_RATE_D] + w[RW_RATE_B], 0xff, tr);
        if (other != 0)
            w[RW_WAGES] += mul_div_clamp(w[RW_RATE_B] + w[RW_RATE_C], 0xff, other);
    }
    r->unk_80f0 = w[RW_SHORTFALL] * 100 + r->unk_80f4 + s_satisfaction;
    r->unk_80f4 += s_satisfaction;
    if (w[RW_SURPLUS] + s_jobs == 0)
        r->unk_80f8 = 100;
    else
        r->unk_80f8 = (uint8_t)(r->unk_80f0 / (w[RW_SURPLUS] + s_jobs));
    for (int i = 0; i < 9; i++)
        w[RW_JOBS_ZONES + i] = s_jobs_set[i];

    if (s_centres != 0)
        for (int x = 0; x < 100; x++)
            for (int y = 99; y >= 0; y--) {
                if (cell_tileset(x, y) != 7)
                    continue;
                uint32_t c = cell_get(x, y);
                unsigned e = (c >> 16) & 0x7f;
                uint32_t cap = halve_if(c, e < 8 ? g_tables.set7[e].capacity : 0);
                SimCell *s = sim_cell(x, y);
                if (s_centres < 2)
                    sim_cell_set_u32(s, 1, w[RW_TRAINED]);
                else
                    sim_cell_set_u32(s, 1, mul_div_clamp(w[RW_TRAINED], s_training, cap));
            }
    if (!g_build.editor)
        bignum_sub(&g_state.pennies, w[RW_WAGES]);
    g_state.unk_5f7ed4 += w[RW_WAGES];
    w[RW_ROAD_UPKEEP] = s_roads / 0x45;
    bignum_sub(&g_state.pennies, w[RW_ROAD_UPKEEP]);
    g_state.unk_5f7ed4 += w[RW_ROAD_UPKEEP];
    if (!g_sim.milestone_shown)
        realm_milestones();
    return true;
}

/* 0x43eb7c */
void economy_score(void)
{
    PlaneStats *he = world_realm(GS_PLANE_HEAVEN), *hl = world_realm(GS_PLANE_HELL);
    uint32_t v = he->unk_811f + hl->unk_811f;
    if (v > 0x6f3)
        v = 0x6f4;
    uint32_t acc = mul_div_clamp(v, 0x6f4, 0x1000000);
    Bignum souls = hl->souls;
    bignum_add(&souls, he->souls.lo);
    souls.hi = (int32_t)((uint32_t)souls.hi + (uint32_t)he->souls.hi);
    bignum_add(&souls, hl->population);
    bignum_add(&souls, he->population);
    uint32_t u = (uint32_t)(souls.hi >> 5) * 0x100000u + (souls.lo >> 12);
    if (u > 0xffffff)
        u = 0x1000000;
    acc += u;
    if (g_state.pennies.hi >= 0) {
        u = (uint32_t)(g_state.pennies.hi >> 5) * 0x100000u + (g_state.pennies.lo >> 12);
        if (u > 0xffffff)
            u = 0x1000000;
        acc += u;
    }
    if (g_state.embos_next.hi > 0)
        acc += 0x1000000;
    if (acc > 0x3ffffff)
        acc = 0x4000000;
    uint32_t lost = mul_div_clamp(g_state.lost_souls, he->population + hl->population, 0x1000000);
    if (acc <= lost)
        lost = acc;
    uint32_t h = g_state.unk_638690;
    if (h > 0x3f)
        h = 0x40;
    g_state.unk_57e09c = h * ((acc - lost) >> 1) >> 5;
}

/* 0x43e4e5 */
void economy_random_bad_things(void)
{
    if (g_state.unk_5f746c == 0)
        return;
    int d = 4 - (int)g_state.hdr.difficulty;
    if ((uint32_t)rand_range(d * 0x32, d * 300) < g_state.unk_5b2f5c) {
        g_state.unk_5b2f5c = 0;
        int k;
        do
            k = rand_range(0, 7);
        while ((g_state.unk_5f746c & bit((unsigned)k)) == 0);
        bad_thing_start(-k);
    } else {
        g_state.unk_5b2f5c++;
    }
}
