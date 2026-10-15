/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Planet simulation (ALIFE.EXE 0x495300-0x4a2efc, 0x4a8337-0x4ab8df).
 * Overview and the phase interface: game/planet.h. World generation is in
 * planet_gen.c, the coastline bitmap in planet_map.c, the window in
 * ui/planetview.c.
 *
 * Arithmetic follows the original closely, including unsigned wrap-around,
 * byte truncation and its known bugs (marked "original:"), because the
 * results feed the RNG order and the SOUL counts.
 */
#include "game/planet.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/enhance.h"
#include "game/util.h"

PlanetRuntime g_planet;
PlanetHooks g_planet_hooks;

/* ---- Tables ------------------------------------------------------------------ */

const PlanetTables g_planet_tables = {
    /* 0x64f048: {capacity, unk_04, sea_range, land_range, birth_rate, unk_0d, death_rate, terrain_mask} */
    .tech =
        {
            {5000, 0, 0, 0, 204, 0, 164, 0x1},        /* Fire */
            {5500, 0, 0, 0, 154, 0, 134, 0x1},        /* Pottery */
            {6000, 0, 0, 0, 144, 0, 130, 0x1},        /* Irrigation */
            {6000, 0, 1, 0, 134, 0, 124, 0x1},        /* Boating */
            {6000, 0, 1, 0, 130, 0, 122, 0x1},        /* Philosophy */
            {6000, 0, 2, 0, 126, 0, 119, 0x1},        /* Navigation */
            {7000, 0, 3, 0, 110, 0, 104, 0x1},        /* Industrialization */
            {7500, 0, 4, 2, 99, 0, 94, 0x1},          /* Mass Transit */
            {7500, 0, 4, 2, 83, 0, 78, 0x1},          /* Medicine */
            {7500, 0, 4, 6, 66, 0, 62, 0x1},          /* Aviation */
            {7500, 0, 5, 7, 48, 0, 45, 0x1},          /* Nuclear Technology */
            {8000, 0, 5, 8, 40, 0, 36, 0x3},          /* Gravity Control */
            {8500, 1, 5, 10, 40, 0, 37, 0x3},         /* Climate Control */
            {8500, 2, 5, 10, 40, 0, 38, 0xffffffff},  /* Weather Control */
            {10500, 2, 5, 10, 40, 0, 38, 0xffffffff}, /* Advanced Golf */
        },
    /* 0x64f178 (rows 16-31 are the data that follows, which the original reads past the 16 tech steps) */
    .tech_sin =
        {
            {0, 0, 0, 0, 0, 0, 0},        {0, 0, 0, 0, 0, 0, 0},         {0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0},        {-1, -1, 0, 0, 0, 0, 0},       {0, 0, 0, -1, 0, 0, 0},
            {2, 2, 2, 2, 1, 0, 0},        {0, 1, 0, 1, 0, 2, 0},         {0, 0, -1, 1, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0},        {0, 0, 0, 0, 0, 1, 2},         {0, 0, 0, 1, 0, 0, 0},
            {0, 0, 0, -1, 0, 0, -1},      {-1, -1, -1, 0, -1, -1, 0},    {2, 2, 2, 2, 2, 2, 2},
            {0, 0, 0, 0, 0, 0, 0},        {64, 66, 68, 70, -95, 95, 93}, {91, 89, 0, 0, 0, 0, 0},
            {0, 0, 10, 11, 13, 29, 15},   {12, 14, 16, 34, 35, 36, 37},  {39, 41, 42, 44, 45, 34, 35},
            {36, 37, 39, 41, 42, 44, 45}, {34, 35, 36, 37, 39, 41, 42},  {44, 45, 34, 35, 36, 37, 39},
            {41, 42, 44, 45, 34, 35, 36}, {37, 39, 41, 42, 44, 45, 34},  {35, 36, 37, 39, 41, 42, 44},
            {45, 34, 35, 36, 37, 39, 41}, {42, 44, 45, 0, -1, -1, -1},   {-1, 49, 17, 75, 0, 72, 17},
            {75, 0, 0, 0, 0, 0, 6},       {0, 0, 6, 0, 1, 0, 0},
        },
    .belief_index = {0, 0, 1, 2, 3, 0, 4, 0, 8}, /* 0x66c918 */
    /* 0x66c780: {min_tech, max_tech, radius, kill_percent} */
    .disaster = {{0, 12, 0, 100},
                 {0, 0, 4, 20},
                 {0, 0, 6, 25},
                 {0, 2, 2, 15},
                 {3, 4, 4, 15},
                 {5, 6, 3, 20},
                 {7, 10, 4, 20},
                 {10, 11, 5, 20},
                 {11, 0, 7, 20},
                 {11, 0, 0, 60}},
    .tech_name = {"Fire", "Pottery", "Irrigation", "Boating", "Philosophy", "Navigation", "Industrialization",
                  "Mass Transit", "Medicine", "Aviation", "Nuclear Technology", "Gravity Control", "Climate Control",
                  "Weather Control", "Advanced Golf"}, /* 0x66c740 */
    /* the announcements (immediates in planet_tech_update 0x49b05c) */
    .tech_blurb =
        {
            /* 0x66c938 */
            "",
            /* 0x66c9c0 */
            "  With Pottery, EMBOs can stockpile foodstuffs to guard against times of crisis, allowing more EMBOs to "
            "live in the same area.  ",
            /* 0x66cae8 */
            "  Irrigation makes farms more productive, reducing food shortages and allowing cities to grow larger.  "
            "Water plants regularly, and they grow; who would've thunk?",
            /* 0x66cc38 */
            "  Boating allows EMBOs to cross short distances over water.  Maybe someday we'll find the answer to "
            "that age old question, \"Daddy, is the world really as flat your head?\"",
            /* 0x66cd68 */
            "  EMBOs now spend more of their time contemplating difficult, and often controversial, issues and less "
            "time fighting and bickering.",
            /* 0x66ce44 */
            "  EMBOs can now build larger, sturdier vessels capable of venturing further from shore.",
            /* 0x66cf08 */
            "  Giant factories spur urban job growth, attracting more and more EMBOs from rural areas into the "
            "cities.",
            /* 0x66d01c */
            "  Beep! Beep! \"Move it, buddy!\"  Transportation speed and efficiency increase dramatically, allowing "
            "EMBOs more leisure time for vacations and just plain exploring.",
            /* 0x66d15c */
            "  Medicine evolves from folklore and wives' tales into a true science increasing both the life "
            "expectancy and the quality of life for all EMBOs, achew!",
            /* 0x66d284 */
            "  No longer relegated to using the cumbersome flying snails of the past, modern EMBOs utilize large "
            "flying machines to travel across the world.",
            /* 0x66d3fc */
            "  Besides making blindingly beautiful watch dials, radioactive materials make nifty energy sources that "
            "power entire cities and allow ships to stay out at sea almost indefinitely.  Don't forget to put those "
            "control rods back in!",
            /* 0x66d5dc */
            "  \"Will this contraption really float?\"  EMBOs can use Gravity Control Technology to not only build "
            "some really cool flying toys but also just about any flying or floating object imaginable regardless of "
            "its size or weight, even floating cities!",
            /* 0x66d798 */
            "  Brrrr!  It can get pretty cold living on top of Mount Evertall.  Never fear, with Climate Control "
            "<TM> you too can feel just as warm and cozy on top of a mountain as you do on top of a beach.",
            /* 0x66d9dc */
            "  Been planning that Arctic ski trip but shivered at the thought of year-round, sub-zero temperatures?  "
            "Well, now you can thanks to the micro-miracle of Weather Control!  Unlike Climate Control, which allows "
            "only limited control of one's environment, Weather Control gives any EMBO the ability to completely "
            "control his or her environment.  Hurry, before the polar caps melt away!",
            /* 0x66dc18 */
            "The EMBOs have just discovered the pinnacle of scientic and social achievement; a discovery so vast in "
            "scope that it defies description.\n"
            "Sire, the EMBOs have discovered Advanced Golf!!",
        },
};

bool planet_tables_load(void) { return true; }

static const PlanetTechInfo *tech_info(unsigned tech)
{
    /* The original indexes without a check; levels stay <= 14. */
    return &g_planet_tables.tech[tech < PLANET_TECHS ? tech : PLANET_TECHS - 1];
}

/* ---- Small helpers ------------------------------------------------------------- */

#define SEC(x, y) (&g_state.sectors[x][y])

static uint32_t sat_add_room(uint32_t v, uint32_t total) /* min(v, ~total) */
{
    return v <= ~total ? v : ~total;
}

/* Counts EMBO deaths into the year's total without wrapping (the
 * original's `min(n, -deaths - 1)`). */
static uint32_t count_deaths(uint32_t n)
{
    n = sat_add_room(n, g_state.planet_deaths);
    g_state.planet_deaths += n;
    return n;
}

static int clampi(int v, int lo, int hi) /* 0x4a23ef (adds before) */
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void pump(bool frame)
{
    if (g_planet_hooks.pump)
        g_planet_hooks.pump(frame);
}

static void sound(uint32_t id, int mode)
{
    if (g_planet_hooks.sound)
        g_planet_hooks.sound(id, mode);
}

static void view_refresh(bool full)
{
    if (g_planet_hooks.view_refresh)
        g_planet_hooks.view_refresh(full);
}

/* 0x49de17 is the UI; the "injured" figure it shows is an RNG draw made
 * only when the report is drawn, so it's decided here. */
static void disaster_report(unsigned kind, uint32_t killed)
{
    uint32_t injured = 0;
    bool shown = g_planet_hooks.disaster_reports && g_planet_hooks.disaster_reports();
    if (shown && kind != 100)
        injured = (uint32_t)rand_range((int)(killed >> 1), (int)(killed * 2));
    if (g_planet_hooks.disaster)
        g_planet_hooks.disaster(kind, killed, injured);
}

/* ---- Queries ----------------------------------------------------------------------- */

/* 0x497b72 */
int planet_distance(int x0, int y0, int x1, int y1)
{
    return (int)sqrt((double)((y0 - y1) * (y0 - y1) + (x0 - x1) * (x0 - x1)));
}

/* 0x49920d */
bool planet_is_coastal(int x, int y)
{
    uint8_t t = SEC(x, y)->terrain;
    if (t == PLANET_WATER)
        return false;
    if (t == PLANET_LAND && (x == 0 || y == 0 || x == PLANET_W - 1 || y == PLANET_H - 1))
        return true;
    int y1 = y + 1 > PLANET_H - 1 ? PLANET_H - 1 : y + 1;
    int x1 = x + 1 > PLANET_W - 1 ? PLANET_W - 1 : x + 1;
    for (int yy = y - 1 < 1 ? 0 : y - 1; yy <= y1; yy++)
        for (int xx = x - 1 < 1 ? 0 : x - 1; xx <= x1; xx++)
            if (SEC(xx, yy)->terrain != t)
                return true;
    return false;
}

/* 0x498eb0 */
uint32_t planet_max_pop(void)
{
    uint32_t m = 0;
    for (int y = 0; y < PLANET_H; y++)
        for (int x = 0; x < PLANET_W; x++)
            if (SEC(x, y)->pop > m)
                m = SEC(x, y)->pop;
    return m;
}

/* ---- Sector maintenance --------------------------------------------------------------- */

/* 0x498dd5 */
void planet_set_rates(int x, int y)
{
    PlanetSector *s = SEC(x, y);
    const PlanetTechInfo *ti = tech_info(s->tech);
    s->birth_rate = ti->birth_rate;
    s->death_rate = (uint8_t)ti->death_rate;
}

/* 0x4aaca0: the share of deaths (of 255) per SOUL belief type: AAAA x fate
 * x ALF/RALF x SUMA/SUSA. */
void planet_soul_shares(int x, int y)
{
    PlanetSector *s = SEC(x, y);
    const int8_t *bi = g_planet_tables.belief_index;
    for (int c = 1; c < 5; c++)
        for (int d = 5; d < 7; d++)
            for (int e = 7; e < 9; e++) {
                uint32_t v = (uint32_t)s->tenet[e] * s->tenet[d] * s->tenet[c] * s->tenet[0] / 0xfd02ffu;
                int idx = bi[e] + bi[d] + bi[c];
                assert(idx >= 0 && idx < 16);
                s->soul_frac[idx] = (uint8_t)v;
            }
}

/* 0x49704f */
void planet_clear_sector(int x, int y, bool full)
{
    static const uint8_t k_tenets[9] = {0xff, 0, 0xff, 0, 0, 0x80, 0x7f, 0x80, 0x7f};
    PlanetSector *s = SEC(x, y);
    if (full) {
        s->terrain = PLANET_WATER;
        s->height = 0;
        s->tech = 0;
        s->flags = 0;
    }
    s->pop = 0;
    bignum_add(&g_state.embos_next, s->pop);
    memcpy(s->tenet, k_tenets, 9);
    planet_set_rates(x, y);
    planet_soul_shares(x, y);
    memset(planet_tenet_acc(x, y), 0, 9 * sizeof(int32_t));
    memset(planet_sin_acc(x, y), 0, 7 * sizeof(int32_t));
    memset(planet_migration(x, y), 0, 2 * sizeof(int32_t));
}

/* 0x4a2437 */
void planet_add_fate(int x, int y, unsigned amount, unsigned fate)
{
    PlanetSector *s = SEC(x, y);
    amount &= 0xff;
    fate &= 0xff;
    assert(fate >= 1 && fate <= 4);
    if (amount == 0)
        return;
    unsigned room = 0xffu - s->tenet[fate];
    uint8_t add = (uint8_t)(amount <= room ? amount : room);
    if (add == 0)
        return;
    unsigned sum = (unsigned)s->tenet[1] + s->tenet[2] + s->tenet[3];
    if (sum > 0xfe)
        sum = 0xff;
    s->tenet[TENET_OCRA] = (uint8_t)(0xff - sum);
    s->tenet[fate] = (uint8_t)(s->tenet[fate] + add);
    uint16_t left = add;
    uint16_t chunk = add / 3 < 2 ? 1 : (uint16_t)(add / 3);
    unsigned i = 1, idle = 0;
    while (left != 0) {
        if (fate != i) {
            uint16_t take = (int16_t)chunk <= (int16_t)s->tenet[i] ? chunk : s->tenet[i];
            s->tenet[i] = (uint8_t)(s->tenet[i] - take);
            left = (uint16_t)(left - take);
            int half = (int16_t)left / 2;
            chunk = (uint16_t)(half < 2 ? 1 : half);
            /* ENHANCEMENT: stop when nothing is left to take; the original
             * loops forever (the game hangs) */
            idle = take ? 0 : idle + 1;
            if (g_enhancements && idle > 4)
                break;
        }
        if (++i > 4)
            i = 1;
    }
}

/* 0x499a77 */
void planet_summary(void)
{
    PlanetRuntime *p = &g_planet;
    p->max_tech = 0;
    memset(p->sin_min, 0, sizeof p->sin_min);
    memset(p->sin_min_x, 0, sizeof p->sin_min_x);
    memset(p->sin_min_y, 0, sizeof p->sin_min_y);
    memset(p->sin_min_pop, 0, sizeof p->sin_min_pop);
    for (int y = 0; y < PLANET_H; y++)
        for (int x = 0; x < PLANET_W; x++) {
            const PlanetSector *s = SEC(x, y);
            if (s->tech > p->max_tech)
                p->max_tech = s->tech;
            for (int i = 0; i < 7; i++)
                if ((int8_t)s->sin[i] <= p->sin_min[i] && p->sin_min_pop[i] < s->pop) {
                    p->sin_min[i] = (int8_t)s->sin[i];
                    p->sin_min_x[i] = (uint32_t)x;
                    p->sin_min_y[i] = (uint32_t)y;
                    p->sin_min_pop[i] = s->pop;
                }
        }
}

/* ---- Phase 20: belief statistics ------------------------------------------------------- */

/* 0x4aa783: random walk of the tenets (fate pairs trade, pairs stay
 * complementary). */
void planet_tenet_drift(int x, int y)
{
    uint8_t *t = SEC(x, y)->tenet;
    int c = (int8_t)rand_range(-10, 10) / 10;
    if (c != 0 && t[0] + c < 0x100 && t[0] + c >= 0)
        t[0] = (uint8_t)(t[0] + c);
    for (int a = 1; a < 4; a++)
        for (int b = a + 1; b < 5; b++) {
            c = (int8_t)rand_range(-64, 64) / 64;
            if (c != 0 && t[a] + c < 0x100 && t[b] - c >= 0 && t[b] - c < 0x100 && t[a] + c >= 0) {
                t[a] = (uint8_t)(t[a] + c);
                t[b] = (uint8_t)(t[b] - c);
            }
        }
    for (int k = 5; k < 9; k += 2) {
        c = (int8_t)rand_range(-10, 10) / 10;
        if (c != 0 && t[k] + c < 0x100 && t[k] + c >= 0) {
            t[k] = (uint8_t)(t[k] + c);
            t[k + 1] = (uint8_t)(t[k + 1] - c);
        }
    }
}

/* 0x4aae01 planet_stats_accumulate */
bool planet_stats_step(void)
{
    PlanetRuntime *p = &g_planet;
    uint32_t end = g_state.planet_cursor + 0x400;
    if (end > 0x7ff)
        end = 0x800;
    const Bignum em = g_state.embos;
    for (uint32_t i = g_state.planet_cursor; i < end; i++) {
        int x = (int)(i >> 5), y = (int)(i & 31);
        PlanetSector *s = SEC(x, y);
        if (s->pop == 0)
            continue;
        planet_tenet_drift(x, y);
        planet_soul_shares(x, y);
        uint32_t w;
        if (em.hi == 0)
            w = em.lo < 1000001u ? s->pop : s->pop / 0xff;
        else
            w = s->pop / 1000000u;
        /* the same weights either way, in the original's u32 arithmetic */
        p->acc[0] += s->tenet[TENET_AAAA] * w;
        p->acc[1] += (0xffu - s->tenet[TENET_OCRA]) * w;
        p->acc[2] += (0xffu - s->tenet[TENET_OPRA]) * w;
        p->acc[3] += s->tenet[TENET_RALF] * w;
    }
    g_state.planet_cursor += 0x400;
    if (end < 0x800)
        return false;

    uint8_t out[4];
    if (em.lo == 0 && em.hi == 0) {
        out[0] = 0xff, out[1] = 0xff, out[2] = 0xff, out[3] = 0x80;
    } else if (em.hi == 0) {
        for (int k = 0; k < 4; k++) {
            uint32_t v = em.lo < 1000001u ? p->acc[k] / em.lo
                                          : (uint32_t)((uint64_t)p->acc[k] / ((uint64_t)em.lo / 0xff));
            out[k] = (uint8_t)(v > 0xfe ? 0xff : v);
        }
    } else {
        uint32_t d = (uint32_t)em.hi * 0x10c7u + em.lo / 1000000u;
        if (d == 0)
            d = 1; /* port: the original would divide by zero */
        for (int k = 0; k < 4; k++) {
            uint32_t v = p->acc[k] / d;
            out[k] = (uint8_t)(v > 999999 ? 1000000 : v); /* original: clamps to 1e6, stores the low byte */
        }
    }
    g_state.planet_aaaa = out[0];
    p->avg_not_ocra = out[1];
    p->avg_not_opra = out[2];
    p->avg_ralf = out[3];
    memset(p->acc, 0, sizeof p->acc);
    g_state.planet_cursor = 0;
    return true;
}

/* ---- Phase 30: births, deaths, SOULs ----------------------------------------------------- */

/* 0x4aa138 */
uint32_t planet_sector_deaths(int x, int y)
{
    PlanetSector *s = SEC(x, y);
    if (s->pop == 0)
        return 0;
    uint8_t lust = 0;
    int8_t chastity = (int8_t)s->sin[4];
    if (chastity < 0) {
        int bd = (int)s->birth_rate - (int)s->death_rate;
        uint32_t c = (uint32_t)(bd < 0 ? -((-bd + 3) >> 2) : bd >> 2); /* arithmetic >> 2 */
        uint32_t u = mul_div_round((uint32_t)-chastity, 0x80, c);
        lust = u < 2 ? 1 : (uint8_t)mul_div_round((uint32_t)-chastity, 0x80, c);
        if (lust > 4)
            lust = 5;
    }
    uint32_t pop = s->pop;
    uint32_t d = s->death_rate < 2 ? 1 : s->death_rate;
    uint32_t deaths = 0xffffffffu / d < pop ? s->death_rate * (pop / 0xff) : s->death_rate * pop / 0xff;
    deaths >>= 5;
    uint32_t b = s->birth_rate < 2 ? 1 : s->birth_rate;
    uint32_t births;
    if (0xffffffffu / b < pop)
        births = (s->birth_rate * (pop / 0xff) >> 5) + ((pop / 0xff) * lust >> 5);
    else
        births = (s->birth_rate * pop / 0xff >> 5) + (pop * lust / 0xff >> 5);
    if (births == 0 && deaths == 0) {
        int r = rand_range(0, s->death_rate + s->birth_rate - 1);
        if (r < s->birth_rate)
            births = 1, deaths = 0;
        else
            births = 0, deaths = 1;
    }
    if (births < deaths) {
        s->pop = pop < deaths - births ? 0 : pop - (deaths - births);
    } else {
        if (births == deaths)
            births += (uint32_t)rand_range(0, (int)(pop >> 8));
        s->pop = pop + sat_add_room(births - deaths, pop);
    }
    bignum_add(&g_state.embos_next, s->pop);
    return deaths;
}

/* 0x4ab5d4 */
void planet_send_souls(int x, int y, uint32_t deaths, uint32_t size)
{
    if (deaths == 0)
        return;
    const PlanetSector *s = SEC(x, y);
    for (int t = 0; t < 16; t++) {
        uint8_t f = s->soul_frac[t];
        if (f == 0)
            continue;
        uint32_t n = mul_div_clamp(f, 0xff, deaths);
        if (n == 0 && rand_range(0, 0xff) < f)
            n = 1;
        if (n == 0)
            continue;
        if (size == 0)
            size = 1;
        uint32_t per = n / size + 1, rest = n % size;
        if (rest != 0) {
            if (g_planet_hooks.souls)
                g_planet_hooks.souls(x, y, t, rest, per);
            per = n / size;
        }
        /* original: without a remainder `per` stays n / size + 1, so `size`
         * groups of that are sent (size more SOULs than died) */
        if (per != 0 && g_planet_hooks.souls)
            g_planet_hooks.souls(x, y, t, size - rest, per);
    }
}

/* 0x4ab77a planet_emit_souls */
bool planet_emit_step(void)
{
    GameState *g = &g_state;
    uint32_t end = g->planet_cursor + 25;
    if (end > 0x7ff)
        end = 0x800;
    uint32_t size = g->planet_deaths_last / 0x5000;
    for (uint32_t i = g->planet_cursor; i < end; i++) {
        int x = (int)(i >> 5), y = (int)(i & 31);
        uint32_t d = planet_sector_deaths(x, y);
        if (g->unk_57e058 != 0) {
            uint32_t m = d < g->unk_57e058 ? d : g->unk_57e058;
            g->unk_57e058 -= m;
            g->unk_630420 += m;
            g->unk_5bb108 += m;
        }
        if (d != 0)
            planet_send_souls(x, y, count_deaths(d), size + 1);
    }
    g->planet_cursor += 25;
    if (end < 0x800)
        return false;
    g->planet_cursor = 0;
    return true;
}

/* ---- Phase 10: migration -------------------------------------------------------------------- */

/* 0x499398: how unattractive (tx, ty) is for migrants from (sx, sy); -1
 * (as unsigned: the worst) if their tech can't settle that terrain. */
static uint32_t desirability(int tx, int ty, int sx, int sy)
{
    const PlanetSector *t = SEC(tx, ty);
    if ((tech_info(SEC(sx, sy)->tech)->terrain_mask & (1u << (t->terrain & 31))) == 0)
        return 0xffffffffu;
    uint8_t lat = (uint8_t)abs(16 - ty);
    return t->pop / tech_info(t->tech)->capacity + (((uint32_t)t->terrain << 4) << ((lat >> 1) & 31)) +
           ((uint32_t)t->height + lat) * 2;
}

static uint8_t terrain_at(int x, int y)
{
    assert(x >= 0 && x < PLANET_W && y >= 0 && y < PLANET_H);
    return SEC(x, y)->terrain;
}

/* 0x499484: (x1, y1) is next to (x0, y0) over the same terrain. */
static bool same_land(int x0, int y0, int x1, int y1)
{
    uint8_t t = terrain_at(x0, y0);
    if (terrain_at(x1, y1) != t)
        return false;
    if (x0 == 0 || y0 == 0 || x0 > 62 || y0 > 30)
        return true;
    int dx = (int8_t)(x0 - x1), dy = (int8_t)(y0 - y1);
    if (x1 == x0)
        return (terrain_at(x0 - 1, y0) == t && terrain_at(x0 - 1, y0 - dy) == t) ||
               (terrain_at(x0 + 1, y0) == t && terrain_at(x0 + 1, y0 - dy) == t);
    if (y1 == y0)
        return (terrain_at(x0, y0 - 1) == t && terrain_at(x0 - dx, y0 - 1) == t) ||
               (terrain_at(x0, y0 + 1) == t && terrain_at(x0 - dx, y0 + 1) == t);
    return terrain_at(x0 - dx, y0) == t || terrain_at(x0, y0 - dy) == t;
}

/* 0x4997d5 */
static bool reachable(int x0, int y0, int x1, int y1)
{
    const PlanetTechInfo *ti = tech_info(SEC(x0, y0)->tech);
    if ((ti->terrain_mask & (1u << (SEC(x1, y1)->terrain & 31))) == 0)
        return false;
    uint16_t dist = (uint16_t)planet_distance(x0, y0, x1, y1);
    uint16_t land = ti->land_range, sea = ti->sea_range;
    bool by_sea = sea != 0 && planet_is_coastal(x0, y0) && planet_is_coastal(x1, y1);
    if (by_sea)
        return dist <= (sea > land ? sea : land);
    if (same_land(x0, y0, x1, y1) && land < 2)
        land = 1;
    return dist <= land;
}

/* 0x498f39: a random neighbour to move to instead (preferring emptier,
 * lower ones). */
static void pick_neighbour(int x, int y, int *ox, int *oy)
{
    uint32_t mask = tech_info(SEC(x, y)->tech)->terrain_mask;
    int list[9][2] = {{x, y}};
    int n = 0;
    uint32_t best_pop = SEC(x, y)->pop;
    uint8_t best_h = SEC(x, y)->height;
    for (int dy = -1; dy < 2; dy++) {
        int ny = (int8_t)y + dy;
        if (ny < 0)
            ny += 32;
        else if (ny > 31)
            ny -= 32;
        for (int dx = -1; dx < 2; dx++) {
            int nx = (int8_t)x + dx;
            if (nx < 0)
                nx += 64;
            else if (nx > 63)
                nx -= 64;
            const PlanetSector *ns = SEC(nx, ny);
            if (!(mask & (1u << (ns->terrain & 31))) || !reachable(x, y, nx, ny))
                continue;
            if (ns->pop < best_pop && ns->height < best_h) {
                list[0][0] = nx, list[0][1] = ny;
                best_pop = ns->pop;
                best_h = ns->height;
                n = 1;
            } else if (ns->pop <= best_pop) {
                list[n][0] = nx, list[n][1] = ny;
                n++;
            }
        }
    }
    int k = n < 1 ? 0 : rand_range(0, n - 1);
    *ox = list[k][0];
    *oy = list[k][1];
}

/* 0x499c7a: migrants carry their tenets into (tx, ty). */
static void migrate_tenets(int sx, int sy, int tx, int ty, uint32_t n)
{
    PlanetSector *S = SEC(sx, sy), *D = SEC(tx, ty);
    int32_t *sa = planet_tenet_acc(sx, sy), *da = planet_tenet_acc(tx, ty);
    uint32_t dp = D->pop < 2 ? 1 : D->pop;
    uint32_t rem = n % dp;
    int16_t q = (int16_t)(n / dp);

    int16_t diff = (int16_t)((uint16_t)S->tenet[0] - (uint16_t)D->tenet[0]);
    if (diff != 0) {
        if (q != 0)
            D->tenet[0] = (uint8_t)clampi(D->tenet[0] + diff / q, 0, 0xff);
        int sign = diff < 0 ? -1 : 1;
        uint32_t a = (uint32_t)abs(sa[0]);
        uint32_t take = a < n ? a : n;
        int sg = sa[0] < 0 ? -1 : sa[0] != 0;
        sa[0] = (int32_t)(((uint32_t)abs(sa[0]) - take) * (uint32_t)sg);
        da[0] = (int32_t)((uint32_t)da[0] + rem * (uint32_t)sign);
    }
    for (int k = 5; k < 8; k += 2) {
        int dom, oth;
        if (S->tenet[k + 1] < S->tenet[k])
            dom = k, oth = k + 1;
        else
            dom = k + 1, oth = k;
        int16_t d = (int16_t)((uint16_t)S->tenet[dom] - (uint16_t)D->tenet[dom]);
        if (d == 0)
            continue;
        int up = dom, down = oth;
        if (d < 0)
            up = oth, down = dom;
        if (q != 0) {
            int room = 0xff - D->tenet[up], v = d / q;
            if (v > room)
                v = room;
            D->tenet[up] = (uint8_t)(D->tenet[up] + v);
            D->tenet[down] = (uint8_t)(0xff - D->tenet[up]);
        }
        uint32_t t = (int32_t)rem <= da[down] ? rem : (uint32_t)da[down];
        da[down] = (int32_t)((uint32_t)da[down] - t);
        int32_t e = (int32_t)(rem - t);
        if (e < 1)
            e = 0;
        da[up] += e;
        int sel = sa[oth] < sa[dom] ? dom : oth;
        sa[sel] = (int32_t)((uint32_t)sa[sel] - mul_div_clamp(n, S->pop, rem));
    }
    for (int k = 1; k < 5; k++) {
        int16_t d = (int16_t)((uint16_t)S->tenet[k] - (uint16_t)D->tenet[k]);
        if (d <= 0)
            continue;
        if (q != 0) {
            int room = 0xff - D->tenet[k], v = d / q;
            if (v > room)
                v = room;
            planet_add_fate(tx, ty, (unsigned)v & 0xff, (unsigned)k);
        }
        da[k] = (int32_t)((uint32_t)da[k] + mul_div_clamp(S->tenet[k], 0xff, rem));
    }
}

static int wrap_coord(int v, int n) /* the original's x % n (+ n - 1 if negative) */
{
    if (v < 0)
        return v % n + n - 1; /* original: off by one */
    if (v >= n)
        return v % n;
    return v;
}

/* 0x49a2da: one row per call. Crowded sectors (a third of their capacity
 * or more) send a quarter of their people along a spiral of growing rings
 * to better places in reach; the moves go into the migration grid that
 * planet_tech_step applies. */
bool planet_sector_step(void)
{
    PlanetState *ps = &g_state.planet;
    if (ps->sector_row == 0)
        memset(ps->grid, 0, sizeof ps->grid);
    int y = ps->sector_row;
    if (y < PLANET_H) {
        for (int x = 0; x < PLANET_W; x++) {
            PlanetSector *s = SEC(x, y);
            const PlanetTechInfo *ti = tech_info(s->tech);
            uint32_t cap3 = ti->capacity / 3;
            if (s->pop == 0 || s->pop < cap3)
                continue;
            uint32_t here = desirability(x, y, x, y);
            uint32_t left = s->pop >> 2;
            int cx = x, cy = y - 1;
            int side = 0;
            uint8_t step = 0, ring = 1, sea = 1;
            if (planet_is_coastal(x, y)) {
                sea = (uint8_t)ti->sea_range;
                if (sea < 2)
                    sea = 1;
            }
            uint32_t range = ti->land_range;
            if (range <= sea)
                range = sea;
            for (;;) {
                int tx, ty;
                uint32_t amount;
                for (;;) {
                    /* walk `step` cells along the ring */
                    do {
                        if (left == 0 || (range & 0xff) < ring)
                            goto next_x;
                        switch (side) {
                        case 0:
                            if (step + cx < ring + x) {
                                cx += step;
                                step = 0;
                            } else {
                                step = (uint8_t)(step - ((ring + x) - cx));
                                cx = ring + x;
                                cy = y - ring;
                                side++;
                            }
                            break;
                        case 1:
                            if (step + cy < ring + y) {
                                cy += step;
                                step = 0;
                            } else {
                                step = (uint8_t)(step - ((ring + y) - cy));
                                cy = ring + y;
                                cx = ring + x;
                                side++;
                            }
                            break;
                        case 2:
                            if (x - ring < cx - step) {
                                cx -= step;
                                step = 0;
                            } else {
                                step = (uint8_t)(step - (cx - (x - ring)));
                                cx = x - ring;
                                cy = ring + y;
                                side++;
                            }
                            break;
                        default:
                            if (cy - step < y - ring) {
                                step = (uint8_t)(step - (cy - (y - ring)));
                                cy = y - ring;
                                cx = x - ring;
                                ring++;
                                side = 0;
                            } else {
                                cy -= step;
                                step = 0;
                            }
                            break;
                        }
                    } while (step != 0);
                    tx = clampi(wrap_coord(cx, PLANET_W), 0, PLANET_W - 1);
                    ty = clampi(wrap_coord(cy, PLANET_H), 0, PLANET_H - 1);
                    int r = rand_range(-(int)(left >> 4), (int)(left >> 4));
                    amount = (left >> 1) + (uint32_t)r;
                    int8_t c3 = (int8_t)rand_range(0, (int)(range & 0xff) >> 1);
                    int8_t c4 = (int8_t)rand_range(0, (int)(range & 0xff) >> 1);
                    step = (uint8_t)(c3 + c4);
                    if (step < 2)
                        step = rand_range(0, 10) == 0 ? 1 : 2;
                    if (here <= desirability(tx, ty, x, y))
                        continue;
                    int r2 = rand_range(-(int)(cap3 / 10), (int)(cap3 / 10));
                    if (s->pop <= cap3 + (uint32_t)r2)
                        continue;
                    break;
                }
                if (rand_range(0, 20) == 0) {
                    pick_neighbour(x, y, &tx, &ty);
                    tx = clampi(tx, 0, PLANET_W - 1);
                    ty = clampi(ty, 0, PLANET_H - 1);
                    (void)desirability(tx, ty, x, y); /* original: result unused */
                }
                if (amount == 0)
                    continue;
                PlanetSector *t = SEC(tx, ty);
                int32_t *tg = planet_migration(tx, ty);
                int32_t dest = (int32_t)((uint32_t)tg[0] + t->pop);
                if (dest == 0) {
                    tg[1] = s->tech;
                    memcpy(t->sin, s->sin, 7);
                    memcpy(t->tenet, s->tenet, 9);
                    memcpy(t->soul_frac, s->soul_frac, 16);
                    memset(planet_sin_acc(tx, ty), 0, 7 * sizeof(int32_t));
                } else {
                    if (tg[0] > 0 && amount > ~(uint32_t)tg[0])
                        amount = ~(uint32_t)tg[0];
                    uint32_t v = amount <= ~(uint32_t)dest ? amount : ~(uint32_t)dest;
                    if (v == 0)
                        continue;
                    if (t->tech < s->tech) {
                        int d = s->tech - t->tech;
                        if (d <= tg[1])
                            d = tg[1];
                        tg[1] = d;
                    }
                    int32_t *ta = planet_sin_acc(tx, ty), *sa = planet_sin_acc(x, y);
                    for (int k = 0; k < 7; k++) {
                        int16_t sd = (int16_t)((int16_t)(int8_t)s->sin[k] - (int16_t)(int8_t)t->sin[k]);
                        ta[k] = (int32_t)((uint32_t)ta[k] + ((v >> 1) + 1) * (uint32_t)(int32_t)sd);
                        uint32_t share = mul_div_clamp(v, s->pop, (uint32_t)abs(sa[k]));
                        if (sa[k] < 1)
                            sa[k] = (int32_t)((uint32_t)sa[k] + share);
                        else
                            sa[k] = (int32_t)((uint32_t)sa[k] - share);
                    }
                    migrate_tenets(x, y, tx, ty, v);
                    amount = v;
                }
                int32_t *sg = planet_migration(x, y);
                sg[0] = (int32_t)((uint32_t)sg[0] - amount);
                tg[0] = (int32_t)((uint32_t)tg[0] + amount);
                left -= amount;
            }
        next_x:;
        }
    }
    ps->sector_row++;
    bool done = ps->sector_row > 31;
    if (done)
        ps->sector_row = 0;
    return done;
}

/* ---- Phase 15: tech, influence ------------------------------------------------------------- */

/* 0x4989b9: pending tenet influence (accumulated per person) moves the
 * tenets once it amounts to a whole step. */
static void apply_tenet_acc(int x, int y)
{
    PlanetSector *s = SEC(x, y);
    int32_t pop = (int32_t)s->pop, *a = planet_tenet_acc(x, y);
    if (pop == 0)
        return;
    if (a[0] != 0 && a[0] / pop != 0) {
        s->tenet[0] = (uint8_t)clampi(a[0] / pop + s->tenet[0], 0, 0xff);
        a[0] = 0;
    }
    for (int k = 5; k < 8; k += 2) {
        if (a[k] == 0 && a[k + 1] == 0)
            continue;
        int up, dn;
        if (a[k + 1] < a[k])
            up = k, dn = k + 1;
        else
            dn = k, up = k + 1;
        int v = a[up] / pop;
        if (v != 0) {
            int room = 0xff - s->tenet[up];
            if (v > room)
                v = room;
            s->tenet[up] = (uint8_t)(s->tenet[up] + v);
            s->tenet[dn] = (uint8_t)(s->tenet[dn] - v);
            a[up] = 0;
        }
    }
    for (int f = 1; f < 5; f++) {
        int v = a[f] / pop;
        if (v != 0) {
            planet_add_fate(x, y, (unsigned)v & 0xff, (unsigned)f);
            a[f] = 0;
        }
    }
}

/* 0x498681: sins shift with the tech levels passed (from -> to), and the
 * pending sin influence is applied. */
static void apply_sin_shift(int x, int y, uint8_t from, uint8_t to)
{
    PlanetSector *s = SEC(x, y);
    if (s->pop == 0)
        return;
    int8_t d[7] = {0};
    int lo = from < to ? from : to, hi = from > to ? from : to;
    for (int t = lo + 1; t <= hi; t++) {
        assert(t < 32);
        for (int i = 0; i < 7; i++)
            d[i] = (int8_t)(d[i] + g_planet_tables.tech_sin[t][i]);
    }
    int32_t *a = planet_sin_acc(x, y);
    int32_t pop = (int32_t)s->pop;
    for (int i = 0; i < 7; i++) {
        int sh = from < to ? d[i] : -d[i];
        s->sin[i] = (uint8_t)clampi((int8_t)s->sin[i] + sh, -128, 127);
        if (a[i] != 0 && a[i] / pop != 0) {
            int8_t old = (int8_t)s->sin[i];
            int8_t c = (int8_t)clampi(old + a[i] / pop, -128, 127);
            s->sin[i] = (uint8_t)c;
            a[i] = (int32_t)((uint32_t)a[i] - (uint32_t)pop * (uint32_t)(c - old));
        }
    }
}

/* 0x4a2092: artists' sin influence, halving every year. */
static void spread_artists(void)
{
    PlanetState *ps = &g_state.planet;
    for (int r = 0; r < ps->n_a; r++) {
        PlanetRecA *a = &ps->rec_a[r];
        for (int i = 0; i < 7; i++) {
            int16_t v = (int16_t)a->v[i];
            if (v == 0)
                continue;
            int rx = (int)a->x, ry = (int)a->y;
            for (int yy = ry - 4 < 1 ? 0 : ry - 4; yy <= (ry + 4 > 30 ? 31 : ry + 4); yy++)
                for (int xx = rx - 4 < 1 ? 0 : rx - 4; xx <= (rx + 4 > 62 ? 63 : rx + 4); xx++) {
                    PlanetSector *s = SEC(xx, yy);
                    if (s->pop == 0)
                        continue;
                    uint8_t d = (uint8_t)planet_distance(rx, ry, xx, yy);
                    if (d > 30)
                        d = 31;
                    s->sin[i] = (uint8_t)clampi((int8_t)s->sin[i] + v / (1 << d), -128, 127);
                }
            a->v[i] = (uint16_t)(int16_t)(v / 2);
        }
    }
    for (int r = 0; r < ps->n_a; r++) {
        int i = 0;
        while (i < 7 && ps->rec_a[r].v[i] == 0)
            i++;
        /* original: tests record 7's last sin instead of this record's */
        if (i == 7 && ps->rec_a[7].v[6] == 0) {
            for (int c = r; c < ps->n_a - 1; c++)
                ps->rec_a[c] = ps->rec_a[c + 1];
            ps->n_a--;
        }
    }
}

/* 0x4a26fb: famous persons' tenet influence. */
static void spread_famous(void)
{
    PlanetState *ps = &g_state.planet;
    for (int r = 0; r < ps->n_b; r++) {
        PlanetRecB *b = &ps->rec_b[r];
        int rx = (int)b->x, ry = (int)b->y;
        for (int yy = ry - 4 < 1 ? 0 : ry - 4; yy <= (ry + 4 > 30 ? 31 : ry + 4); yy++)
            for (int xx = rx - 4 < 1 ? 0 : rx - 4; xx <= (rx + 4 > 62 ? 63 : rx + 4); xx++) {
                PlanetSector *s = SEC(xx, yy);
                if (s->pop == 0)
                    continue;
                uint8_t d = (uint8_t)planet_distance(rx, ry, xx, yy);
                if (d > 30)
                    d = 31;
                int div = 1 << d;
                s->tenet[0] = (uint8_t)clampi(s->tenet[0] + (int16_t)((int16_t)b->v[0] / div), 0, 0xff);
                for (int k = 5; k < 9; k++)
                    s->tenet[k] = (uint8_t)clampi(s->tenet[k] + (int16_t)((int16_t)b->v[k] / div), 0, 0xff);
                int f = 1;
                while (f < 5 && b->v[f] == 0)
                    f++;
                if (f < 5)
                    planet_add_fate(xx, yy, (unsigned)(int16_t)((int16_t)b->v[f] / div) & 0xff, (unsigned)f);
            }
        for (int k = 0; k < 9; k++)
            b->v[k] = (uint16_t)(int16_t)((int16_t)b->v[k] / 2);
    }
    for (int r = 0; r < ps->n_b; r++) {
        int k = 0;
        while (k < 9 && ps->rec_b[r].v[k] == 0)
            k++;
        /* original: tests record 9's last tenet instead of this record's */
        if (k == 9 && ps->rec_b[9].v[8] == 0) {
            for (int c = r; c < ps->n_b - 1; c++)
                ps->rec_b[c] = ps->rec_b[c + 1];
            ps->n_b--;
        }
    }
}

/* 0x4a6aea: number of significant bits (negative for negative values). */
static int bit_length(int v)
{
    unsigned a = (unsigned)abs(v);
    int n = 32;
    for (unsigned m = 0x80000000u; n != 0 && !(a & m); m >>= 1)
        n--;
    return v < 0 ? -n : n;
}

/* 0x4a2b8a: scientists raise tech levels around them. */
static void spread_scientists(void)
{
    PlanetState *ps = &g_state.planet;
    for (int r = 0; r < ps->n_c; r++) {
        PlanetRecC *c = &ps->rec_c[r];
        int rad = c->initial >> 5;
        int bits = bit_length(c->initial);
        uint8_t cx = (uint8_t)c->x, cy = (uint8_t)c->y;
        int x0 = (int8_t)cx - (int8_t)rad, y0 = (int8_t)cy - (int8_t)rad;
        if (x0 < 1)
            x0 = 0;
        if (y0 < 1)
            y0 = 0;
        unsigned x1 = cx + (unsigned)rad, y1 = cy + (unsigned)rad;
        if (x1 > 62)
            x1 = 63;
        if (y1 > 30)
            y1 = 31;
        for (unsigned yy = (uint8_t)y0; yy <= (y1 & 0xff); yy++) {
            uint8_t dy = (uint8_t)abs((int8_t)yy - (int8_t)cy);
            for (unsigned xx = (uint8_t)x0; xx <= (x1 & 0xff); xx++) {
                uint8_t dx = (uint8_t)abs((int8_t)xx - (int8_t)cx);
                uint8_t m = dy > dx ? dy : dx;
                PlanetSector *s = SEC((int)xx, (int)yy);
                if ((c->left >> ((bits - 1 - m) & 31)) == 1 && s->pop != 0 && s->tech < 14) {
                    s->tech++;
                    if (s->tech > ps->tech_best)
                        ps->tech_best = s->tech;
                }
            }
        }
        c->left >>= 1;
    }
    for (int r = 0; r < ps->n_c; r++) {
        if (ps->rec_c[r].left != 0)
            continue;
        for (int i = r; i < ps->n_c - 1; i++)
            ps->rec_c[i] = ps->rec_c[i + 1];
        memset(&ps->rec_c[ps->n_c - 1], 0, sizeof ps->rec_c[0]);
        ps->n_c--;
        r--;
    }
}

/* 0x49b05c planet_tech_update */
bool planet_tech_step(void)
{
    GameState *g = &g_state;
    PlanetState *ps = &g->planet;
    PlanetRuntime *p = &g_planet;
    if (ps->tech_row == 0) {
        int16_t v = (int16_t)(g->unk_5c6678 - 0xfa);
        int sign = v < 0 ? -1 : v != 0;
        ps->coastal_pop = 0;
        memset(ps->sin_sum, 0, sizeof ps->sin_sum);
        ps->pop_k = 0;
        p->announced = 0;
        int16_t n = 0;
        for (; v != 0; v = (int16_t)(v / 3))
            n++;
        p->drift = (int16_t)(sign * n);
    }
    int16_t delta = (int16_t)(p->drift - (int16_t)ps->belief_drift);
    int y = ps->tech_row;
    if (y < PLANET_H) {
        for (int x = 0; x < PLANET_W; x++) {
            PlanetSector *s = SEC(x, y);
            int32_t *mg = planet_migration(x, y);
            apply_tenet_acc(x, y);
            apply_sin_shift(x, y, s->tech, (uint8_t)(s->tech + mg[1]));
            planet_set_rates(x, y);
            s->pop += (uint32_t)mg[0];
            uint8_t best = ps->tech_best;
            if (s->pop == 0) {
                planet_clear_sector(x, y, false);
                best = ps->tech_best;
            } else {
                if (delta != 0) {
                    int v = clampi((int16_t)(s->tenet[TENET_ALF] + delta), 0, 0xff);
                    s->tenet[TENET_ALF] = (uint8_t)v;
                    s->tenet[TENET_RALF] = (uint8_t)(0xff - v);
                }
                uint32_t k = s->pop / 1000;
                if (k != 0) {
                    for (int i = 0; i < 7; i++)
                        ps->sin_sum[i] += (uint32_t)((int8_t)s->sin[i] * (int32_t)k);
                    ps->pop_k += k;
                }
                if (planet_is_coastal(x, y))
                    ps->coastal_pop += s->pop;
                best = ps->tech_best;
                if (s->tech < 14) {
                    uint32_t q = tech_info(s->tech)->capacity >> 2;
                    int r = rand_range(s->tech - 13, 14 - (s->tech + 1));
                    uint32_t need = (uint32_t)(r + 1000) * (s->tech + 1u);
                    bool adv = false;
                    if (need < g->year && s->tech < 14) {
                        int r2 = rand_range(-(int)(q / 50), (int)(q / 50));
                        adv = q + (uint32_t)r2 + (g->year - need) * (uint32_t)-2 < s->pop;
                    }
                    if (adv) {
                        s->tech++;
                        ps->tech_year = g->year;
                        if (!p->announced && ps->tech_best < s->tech) {
                            ps->tech_best = s->tech;
                            if (g_planet_hooks.tech_announcements && g_planet_hooks.tech_announcements()) {
                                unsigned variant = (unsigned)rand_range(0, 1);
                                p->announced = 1;
                                if (g_planet_hooks.tech_discovered)
                                    g_planet_hooks.tech_discovered(s->tech, variant);
                            }
                        }
                    } else {
                        int room = 14 - s->tech, b = mg[1];
                        s->tech = (uint8_t)(s->tech + (b <= room ? b : room));
                    }
                    best = s->tech > ps->tech_best ? s->tech : ps->tech_best;
                }
            }
            ps->tech_best = best;
        }
    }
    ps->tech_row++;
    if (ps->tech_row < 32)
        return false;
    ps->tech_row = 0;
    ps->belief_drift = (uint16_t)p->drift;
    spread_artists();
    spread_famous();
    spread_scientists();
    planet_summary();
    if (ps->pop_k < 2)
        ps->pop_k = 1;
    for (int i = 0; i < 7; i++)
        ps->sin_avg[i] = (uint8_t)(int8_t)((int32_t)ps->sin_sum[i] / (int32_t)ps->pop_k);
    view_refresh(false);
    return true;
}

/* ---- Disasters ---------------------------------------------------------------------------- */

/* 0x49c038: flood one land sector (one step lower). Returns the drowned. */
static uint32_t flood_sector(int x, int y)
{
    PlanetSector *s = SEC(x, y);
    if (s->flags != 0 || s->terrain != PLANET_LAND)
        return 0;
    uint32_t c = s->pop;
    planet_clear_sector(x, y, false);
    s->tech = 0;
    s->flags = 1;
    if (s->height != 0) {
        s->flags |= 0x80;
        s->height--;
    }
    if (s->height == 0)
        s->terrain = PLANET_WATER;
    return c;
}

/* 0x49bbd4: a random disaster fitting the sector's tech level; wars are
 * less likely where people are peaceful. */
static unsigned pick_disaster(int x, int y)
{
    const PlanetSector *s = SEC(x, y);
    uint8_t list[PLANET_DISASTERS];
    uint8_t n = 0;
    for (int k = 0; k < PLANET_DISASTERS; k++) {
        const PlanetDisasterInfo *d = &g_planet_tables.disaster[k];
        if (d->min_tech <= s->tech && (d->max_tech == 0 || s->tech <= d->max_tech))
            list[n++] = (uint8_t)k;
    }
    if (n == 0)
        return 0; /* port: can't happen with the shipped table */
    uint16_t total = (uint16_t)(n * 20);
    uint8_t k;
    while ((k = (uint8_t)(n - 1)) != 0) {
        uint16_t r = (uint16_t)rand_range(0, (int16_t)total);
        uint16_t w;
        int peace = (int8_t)s->sin[5];
        if (list[k] < 3 || list[k] > 8)
            w = 20;
        else if (peace < 0)
            w = (uint16_t)(18 - (int16_t)((peace + 1) / 8));
        else {
            int v = 20 - (peace + 1) / 4;
            w = v < 0 ? (uint16_t)rand_range(0, 1) : (uint16_t)v;
        }
        if (r < w)
            break;
        total = (uint16_t)(total - 20);
        n = k;
    }
    return list[k];
}

static void flood_and_send(int x, int y, uint32_t *killed, uint32_t size)
{
    uint32_t n = count_deaths(flood_sector(x, y));
    *killed += n;
    planet_send_souls(x, y, n, size);
}

/* Case 0 of 0x49c8b8: coastal flood, one ring of land along the sea. */
static uint32_t coastal_flood(void)
{
    PlanetState *ps = &g_state.planet;
    uint32_t killed = 0, size = ps->coastal_pop / 0x5000 + 1;
    int ctr = 0;
    for (int y = 0; y < PLANET_H; y++) {
        if (SEC(0, y)->terrain == PLANET_LAND)
            flood_and_send(0, y, &killed, size);
        if (SEC(63, y)->terrain == PLANET_LAND)
            flood_and_send(63, y, &killed, size);
        if (++ctr > 15)
            pump(true), ctr = 0;
    }
    for (int x = 1; x < 63; x++)
        for (int y = 1; y < 31; y++) {
            const PlanetSector *a = SEC(x, y - 1), *b = SEC(x, y + 1);
            if (SEC(x, y)->terrain == PLANET_LAND && ((a->terrain == PLANET_WATER && a->flags == 0) ||
                                                      (b->terrain == PLANET_WATER && b->flags == 0)))
                flood_and_send(x, y, &killed, size);
            if (++ctr > 15)
                pump(true), ctr = 0;
        }
    for (int y = 0; y < PLANET_H; y++)
        for (int x = 1; x < 63; x++) {
            const PlanetSector *a = SEC(x - 1, y), *b = SEC(x + 1, y);
            if (SEC(x, y)->terrain == PLANET_LAND && ((a->terrain == PLANET_WATER && a->flags == 0) ||
                                                      (b->terrain == PLANET_WATER && b->flags == 0)))
                flood_and_send(x, y, &killed, size);
            if (++ctr > 15)
                pump(true), ctr = 0;
        }
    for (int y = 1; y < 31; y++) {
        for (int x = 1; x < 63; x++) {
            if (SEC(x, y)->flags == 0)
                continue;
            for (int yy = y - 1; yy <= y + 1; yy++)
                for (int xx = x - 1; xx <= x + 1; xx++)
                    if ((xx != x || yy != y) && SEC(xx, yy)->terrain != PLANET_WATER && ps->map) {
                        planet_map_clear_sector(ps->map, xx, yy);
                        planet_map_draw_sector(ps->map, xx, yy, PLANET_MAP_W);
                    }
            if (++ctr > 7)
                pump(true), ctr = 0;
        }
        pump(false);
    }
    view_refresh(true);
    sound(0x10028, 1);
    disaster_report(0, killed);
    return killed;
}

/* 0x49c8b8 */
uint32_t planet_year(unsigned forced)
{
    GameState *g = &g_state;
    PlanetState *ps = &g->planet;
    PlanetRuntime *p = &g_planet;
    uint32_t killed = 0, c = 0;
    bool force = false;
    unsigned kind = 0;
    int x = 0, y = 0;

    if (ps->disaster == 0 ||
        (ps->disaster == 100 && ps->disaster_year + (uint32_t)rand_range(-4, 4) + 40 <= g->year)) {
        /* the waters recede */
        for (int yy = 0; yy < PLANET_H; yy++)
            for (int xx = 0; xx < PLANET_W; xx++) {
                PlanetSector *s = SEC(xx, yy);
                if (s->flags == 0)
                    continue;
                if (s->flags & 0x80)
                    s->height++;
                s->flags = 0;
                s->terrain = PLANET_LAND;
            }
        if (ps->map) {
            memset(ps->map, 0, ps->map_size);
            planet_map_draw_all(ps->map);
        }
        view_refresh(true);
        ps->disaster = 0xff;
        return 0;
    }

    if ((p->cheats == 0 || forced == 0 || forced > 10) &&
        ps->disaster_year + (uint32_t)rand_range(-10, 10) + 100 <= g->year) {
        /* the planet's sins (envy 0? greed 1, wrath 5) invite wars */
        int w = 0;
        int a1 = (int8_t)ps->sin_avg[1], a0 = (int8_t)ps->sin_avg[0], a5 = (int8_t)ps->sin_avg[5];
        if (a1 < -31)
            w = -(a1 / 12);
        else if (a1 > 31)
            w = -((a1 >> 1) / 12);
        if (a0 < -31)
            w -= a0 / 12;
        else if (a0 > 31)
            w -= (a0 >> 1) / 12;
        if (a5 < -31)
            w -= a5 / 6;
        else if (a5 > 31)
            w -= (a5 >> 1) / 6;
        if (w > 0 && rand_range(0, 100) <= w) {
            int t;
            if (p->sin_min[5] > p->sin_min[1] && p->sin_min[0] > p->sin_min[1])
                t = 1;
            else if (p->sin_min[0] < p->sin_min[5] && p->sin_min[0] < p->sin_min[1])
                t = 0;
            else
                t = 5;
            x = (uint8_t)p->sin_min_x[t];
            y = (uint8_t)p->sin_min_y[t];
            if (x >= PLANET_W || y >= PLANET_H || SEC(x, y)->pop == 0)
                return 0;
            for (int k = 8; k > 2; k--) {
                const PlanetDisasterInfo *d = &g_planet_tables.disaster[k];
                uint8_t tech = SEC(x, y)->tech;
                if (d->min_tech <= tech && (d->max_tech == 0 || tech <= d->max_tech)) {
                    force = true;
                    kind = (unsigned)k;
                    break;
                }
            }
        }
    }
    if (g_planet_hooks.disaster_busy && g_planet_hooks.disaster_busy())
        return 0;
    if (p->cheats != 0 && forced != 0 && forced < 11) {
        force = true;
        kind = forced - 1;
        x = (uint8_t)p->sel_x;
        y = (uint8_t)p->sel_y;
        if (x >= PLANET_W || y >= PLANET_H)
            return 0; /* port: no sector selected */
    }
    if (!force && g->planet.disaster_year + (uint32_t)rand_range(-20, 20) + 250 > g->year)
        return 0;
    pump(false);
    if (!force) {
        uint8_t tries = 0;
        do {
            x = rand_range(0, 63);
            y = rand_range(0, 31);
            if (SEC(x, y)->pop != 0)
                break;
        } while (++tries < 100);
        if (tries > 99)
            return 0;
        kind = pick_disaster(x, y);
    }
    ps->disaster_year = g->year;
    uint8_t radius = g_planet_tables.disaster[kind].radius;
    uint32_t size = g->planet_deaths_last / 0x5000;
    ps->disaster = (uint8_t)kind;
    pump(false);
    pump(true);
    PlanetSector *s = SEC(x, y);
    switch (kind) {
    case 0:
        return coastal_flood();
    case 1: /* asteroid */
        c = s->pop;
        if (g->embos.lo == 0) { /* original: tests the low word of last year's EMBOs */
            c >>= 1;
            s->pop -= c;
        } else {
            s->pop = 0;
            planet_clear_sector(x, y, false);
        }
        s->tech = 0;
        c = count_deaths(c);
        planet_send_souls(x, y, c, size + 1);
        if (s->height != 0) {
            s->height = (uint8_t)(s->height + (int8_t)rand_range(-1, 0));
            if (ps->map) {
                planet_map_clear_sector(ps->map, x, y);
                planet_map_draw_sector(ps->map, x, y, PLANET_MAP_W);
            }
        }
        break;
    case 7: {
        uint8_t r = (uint8_t)rand_range(0, 1);
        s->tech = (uint8_t)(s->tech - (r <= s->tech ? r : s->tech));
        break;
    }
    case 8: {
        uint8_t r = (uint8_t)rand_range(1, 3);
        s->tech = (uint8_t)(s->tech - (r <= s->tech ? r : s->tech));
        break;
    }
    default:
        break;
    }
    pump(false);

    int x0 = x - radius, y0 = y - radius;
    if (x0 < 0)
        x0 += 64;
    if (y0 < 0)
        y0 += 32;
    int span = radius * 2 + 1;
    uint32_t total = 0;
    for (int j = 0, cy = y0; j < span; j++, cy = cy + 1 > 31 ? 0 : cy + 1)
        for (int i = 0, cx = x0; i < span; i++, cx = cx + 1 > 63 ? 0 : cx + 1)
            total += SEC(cx, cy)->pop;
    killed = c;
    int ctr = 0;
    uint8_t pct = g_planet_tables.disaster[kind].kill_percent;
    for (int j = 0, cy = y0; j < span; j++, cy = cy + 1 > 31 ? 0 : cy + 1)
        for (int i = 0, cx = x0; i < span; i++, cx = cx + 1 > 63 ? 0 : cx + 1) {
            if (++ctr > 7)
                pump(true), ctr = 0;
            PlanetSector *t = SEC(cx, cy);
            if (t->pop == 0)
                continue;
            uint32_t k = t->pop / 100 * pct;
            if (k == 0)
                k = 1;
            int32_t *sa = planet_sin_acc(cx, cy), *ta = planet_tenet_acc(cx, cy);
            for (int m = 0; m < 7; m++)
                sa[m] = (int32_t)((uint32_t)sa[m] - (uint32_t)(sa[m] / 100) * pct);
            for (int m = 0; m < 9; m++)
                ta[m] = (int32_t)((uint32_t)ta[m] - (uint32_t)(ta[m] / 100) * pct);
            t->pop -= k;
            uint32_t n = count_deaths(k);
            killed += n;
            planet_send_souls(cx, cy, n, total / 0x5000 + 1);
            if (kind == 7) {
                uint8_t r = (uint8_t)rand_range(0, 1);
                t->tech = (uint8_t)(t->tech - (r <= t->tech ? r : t->tech));
            } else if (kind == 8) {
                uint8_t r = (uint8_t)(rand_range(0, 20) / 10);
                t->tech = (uint8_t)(t->tech - (r <= t->tech ? r : t->tech));
            }
        }
    pump(false);
    if (killed != 0) {
        if (g_planet_hooks.view_focus)
            g_planet_hooks.view_focus(x, y);
        pump(false);
        sound(0x10028, 1);
        disaster_report(kind, killed);
    }
    return killed;
}

/* 0x49c226: the Flood */
void planet_flood(void)
{
    GameState *g = &g_state;
    PlanetState *ps = &g->planet;
    if (g_planet_hooks.flood_warning)
        g_planet_hooks.flood_warning();
    if (g_planet_hooks.busy_cursor)
        g_planet_hooks.busy_cursor(true);
    ps->disaster = 100;
    uint32_t size;
    if (g->embos_next.hi == 0)
        size = g->embos_next.lo / 0x5000 + 1;
    else /* original: the compiler's (hi << 32) / 0x5000 in 32 bits */
        size = (uint32_t)(g->embos_next.hi < 1000 ? g->embos_next.hi : 1000) * 0xccccc800u + 1;
    uint32_t best = 0, extra = 0, killed = 0;
    int bx = -1, by = -1;
    for (int x = 0; x < PLANET_W; x++)
        for (int y = 0; y < PLANET_H; y++) {
            PlanetSector *s = SEC(x, y);
            if (best < s->pop && s->height != 0) {
                best = s->pop;
                bx = x, by = y;
            }
            uint32_t c = flood_sector(x, y);
            if (c >= 20) { /* original: 200 survive each; wraps for 20..199 */
                c -= 200;
                extra += 200;
            }
            killed += c;
            g->planet_deaths += c;
            if (c != 0)
                planet_send_souls(x, y, c, size);
            s->pop = 0;
        }
    if (bx == -1)
        for (int x = 0; x < PLANET_W && bx == -1; x++)
            for (int y = 5; y < 27; y++)
                if (SEC(x, y)->height != 0) {
                    bx = x, by = y;
                    break;
                }
    if (bx == -1)
        bx = 0, by = 0; /* port: the original would index sector -1 */
    PlanetSector *s = SEC(bx, by);
    s->pop = extra;
    for (int k = 0; k < 7; k++) {
        int a = rand_range(0, 32);
        int b = rand_range(0, 32);
        s->sin[k] = (uint8_t)(a + b);
    }
    static const uint8_t k_tenets[9] = {0xff, 0, 0xff, 0, 0, 0x80, 0x7f, 0x80, 0x7f};
    s->flags &= 0x7f;
    s->terrain = PLANET_LAND;
    memcpy(s->tenet, k_tenets, 9);
    planet_set_rates(bx, by);
    view_refresh(true);
    if (g_planet_hooks.busy_cursor)
        g_planet_hooks.busy_cursor(false);
    sound(0x1000b, 1);
    disaster_report(100, killed);
}
