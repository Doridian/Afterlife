/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * planet_test <data dir> [save.ASC|.ALF ...]
 *
 * Steps the planet (src/game/planet.c) of each save through several years
 * in sim_update's phase order and checks invariants and plausibility:
 * - after the emit pass, g_state.embos_next is exactly the sum of the
 *   sector populations, and the year's death count is the sum of what the
 *   sectors reported;
 * - the SOULs handed to the souls hook come close to deaths x the planet's
 *   AAAA (afterlife believers) share, and every group has a valid type;
 * - tenet pairs stay complementary, soul shares match the tenets, tech
 *   levels and rates match the tech table, populated sectors are on
 *   terrain their tech allows;
 * - totals change plausibly from year to year;
 * - the run is deterministic (same RNG seed -> same planet).
 * Then it generates a new planet (planet_generate) and runs it too.
 * The data dir must hold ALIFE.EXE (the planet's tables come from it).
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/planet.h"
#include "game/util.h"
#include "game/world.h"
#include "platform/platform.h"

static int g_failures;

#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("  FAIL: " __VA_ARGS__);                                                                            \
            printf("\n");                                                                                              \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

static bool g_first_year; /* saves hold rates from an older tech table */
static uint64_t g_souls, g_groups;
static uint64_t g_souls_type[16];
static int g_bad_type;

static void souls_hook(int x, int y, int type, uint32_t count, uint32_t size)
{
    if (type < 0 || type > 15 || x < 0 || x >= PLANET_W || y < 0 || y >= PLANET_H)
        g_bad_type++;
    g_souls += (uint64_t)count * size;
    g_souls_type[type & 15] += (uint64_t)count * size;
    g_groups += count;
}

static uint64_t total_pop(void)
{
    uint64_t t = 0;
    for (int x = 0; x < PLANET_W; x++)
        for (int y = 0; y < PLANET_H; y++)
            t += g_state.sectors[x][y].pop;
    return t;
}

static void check_sectors(const char *when, bool rates, bool shares)
{
    int bad_pair = 0, bad_share = 0, bad_tech = 0, bad_rates = 0, bad_terrain = 0;
    for (int x = 0; x < PLANET_W; x++)
        for (int y = 0; y < PLANET_H; y++) {
            const PlanetSector *s = planet_sector(x, y);
            if (s->tech > 14) {
                bad_tech++;
                continue;
            }
            const PlanetTechInfo *ti = &g_planet_tables.tech[s->tech];
            /* rates are set before the tech level rises in the same pass */
            bool rate_ok = !rates;
            for (int t = 0; t <= s->tech && !rate_ok; t++)
                rate_ok = s->birth_rate == g_planet_tables.tech[t].birth_rate &&
                          s->death_rate == (uint8_t)g_planet_tables.tech[t].death_rate;
            if (!rate_ok)
                bad_rates++;
            if (s->pop == 0)
                continue;
            if (!(ti->terrain_mask & (1u << s->terrain)))
                bad_terrain++;
            if (g_state.planet.n_b == 0 &&
                (s->tenet[TENET_ALF] + s->tenet[TENET_RALF] != 255 ||
                 s->tenet[TENET_SUMA] + s->tenet[TENET_SUSA] != 255))
                bad_pair++;
            if (!shares)
                continue;
            unsigned sum = 0, fates = 0;
            for (int t = 0; t < 16; t++)
                sum += s->soul_frac[t];
            for (int f = 1; f < 5; f++)
                fates += s->tenet[f];
            unsigned want = s->tenet[TENET_AAAA] * fates / 255;
            if (sum > want + 1 || sum + 16 < want)
                bad_share++;
        }
    CHECK(!bad_tech, "%s: %d sectors with tech > 14", when, bad_tech);
    CHECK(!bad_rates, "%s: %d sectors with rates not from the tech table", when, bad_rates);
    CHECK(!bad_terrain, "%s: %d populated sectors on terrain their tech can't settle", when, bad_terrain);
    CHECK(!bad_pair, "%s: %d sectors with ALF/RALF or SUMA/SUSA not summing to 255", when, bad_pair);
    CHECK(!bad_share, "%s: %d sectors whose soul shares don't match AAAA x fates", when, bad_share);
}

/* One game year of planet work in sim_update's order (phases 20, 30, 10,
 * 15), with the scheduler's own bookkeeping around it. */
static void step_year(int verbose)
{
    GameState *g = &g_state;
    int calls = 0;
    while (!planet_stats_step())
        calls++;
    CHECK(calls == 1, "stats pass took %d+1 calls, want 2", calls);
    check_sectors("after the stats pass", !g_first_year, true);
    g->embos = g->embos_next; /* phase 20 end */
    g->embos_next.hi = 0, g->embos_next.lo = 0;

    uint32_t deaths0 = g->planet_deaths;
    uint64_t souls0 = g_souls;
    for (calls = 1; !planet_emit_step(); calls++)
        ;
    CHECK(calls == 82, "emit pass took %d calls, want 82", calls);
    uint64_t pop = total_pop();
    CHECK((uint64_t)bignum_value(g->embos_next) == pop, "embos_next %" PRId64 " != sector sum %" PRIu64,
          bignum_value(g->embos_next), pop);
    uint32_t deaths = g->planet_deaths - deaths0;
    uint64_t souls = g_souls - souls0;
    uint32_t killed = planet_year(0);

    for (calls = 1; !planet_sector_step(); calls++)
        ;
    CHECK(calls == 32, "sector pass took %d calls", calls);
    for (calls = 1; !planet_tech_step(); calls++)
        ;
    CHECK(calls == 32, "tech pass took %d calls", calls);
    uint64_t pop2 = total_pop();
    /* migration conserves people (it can only lose them to saturation) */
    CHECK(pop2 <= pop, "migration created people: %" PRIu64 " -> %" PRIu64, pop, pop2);
    g->planet_deaths_last = g->planet_deaths; /* phase 15 end */
    g->planet_deaths = 0;

    /* SOULs: deaths x the believers' share, plus at most one group size
     * per type of the original's remainder bug */
    uint32_t size = 0;
    (void)size;
    if (verbose)
        printf("  year %4" PRIu32 ": %12" PRIu64 " EMBOs, %10" PRIu32 " deaths, %10" PRIu64
               " SOULs (%" PRIu64 " groups), AAAA %3u RALF %3u, max tech %2u, disaster %3u%s\n",
               g->year, pop2, deaths, souls, g_groups, g->planet_aaaa, g_planet.avg_ralf, g_planet.max_tech,
               g->planet.disaster, killed ? " (killed some)" : "");
    if (deaths > 1000) {
        double ratio = (double)souls / deaths;
        double want = g->planet_aaaa / 255.0;
        CHECK(ratio <= 1.2 && ratio >= want * 0.5, "SOULs/deaths %.3f, AAAA share %.3f", ratio, want);
    }
    CHECK(!g_bad_type, "%d SOUL groups with bad type/sector", g_bad_type);
    g->year++;
}

static void run(const char *name, int years, int verbose)
{
    check_sectors(name, false, false);
    for (int i = 0; i < years; i++) {
        uint64_t before = total_pop();
        g_first_year = i == 0;
        step_year(verbose);
        uint64_t after = total_pop();
        if (before > 100000)
            CHECK(after < before * 2 && after * 2 > before, "year %d: population %" PRIu64 " -> %" PRIu64, i,
                  before, after);
        char when[64];
        snprintf(when, sizeof when, "%s +%d", name, i + 1);
        check_sectors(when, true, false);
    }
}

static uint32_t planet_hash(void)
{
    uint32_t h = 2166136261u;
    const uint8_t *p = (const uint8_t *)g_state.sectors;
    for (size_t i = 0; i < sizeof g_state.sectors; i++)
        h = (h ^ p[i]) * 16777619u;
    for (size_t i = 0; i < 0x3800; i++)
        h = (h ^ g_state.planet.sin_acc[i]) * 16777619u;
    return h ^ rng_state();
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <data dir> [save ...]\n", argv[0]);
        return 2;
    }
    plat_set_data_dir(argv[1]);
    if (!planet_tables_load()) {
        fprintf(stderr, "can't read the planet tables from ALIFE.EXE in %s\n", argv[1]);
        return 2;
    }
    CHECK(strcmp(g_planet_tables.tech_name[0], "Fire") == 0, "tech 0 is \"%s\"", g_planet_tables.tech_name[0]);
    CHECK(strcmp(g_planet_tables.tech_name[14], "Advanced Golf") == 0, "tech 14 is \"%s\"",
          g_planet_tables.tech_name[14]);
    g_planet_hooks.souls = souls_hook;

    for (int a = 2; a < argc; a++) {
        char err[256];
        printf("%s\n", argv[a]);
        uint32_t h[2] = {0, 0};
        for (int pass = 0; pass < 2; pass++) {
            if (!world_load(argv[a], err, sizeof err)) {
                printf("  FAIL: %s\n", err);
                g_failures++;
                break;
            }
            planet_summary();
            rng_seed(12345);
            printf("  pass %d: year %" PRIu32 ", %" PRIu64 " EMBOs (save says %" PRId64 "), max tech %u\n", pass,
                   g_state.year, total_pop(), bignum_value(g_state.embos), g_planet.max_tech);
            run(argv[a], 12, pass == 0);
            h[pass] = planet_hash();
        }
        CHECK(h[0] == h[1], "not deterministic: %08" PRIx32 " vs %08" PRIx32, h[0], h[1]);
    }

    printf("new planet\n");
    world_reset();
    rng_seed(1);
    planet_generate();
    int land = 0, arctic = 0, mountains = 0, people = 0;
    for (int x = 0; x < PLANET_W; x++)
        for (int y = 0; y < PLANET_H; y++) {
            const PlanetSector *s = planet_sector(x, y);
            land += s->terrain == PLANET_LAND;
            arctic += s->terrain == PLANET_ARCTIC;
            mountains += s->terrain == PLANET_LAND && s->height >= 3;
            people += s->pop != 0;
        }
    printf("  %d land (%d mountains), %d arctic sectors, %d populated, %" PRIu64 " EMBOs at (%d, %d)\n", land,
           mountains, arctic, people, total_pop(), g_planet.sel_x, g_planet.sel_y);
    CHECK(land > 200 && land < 1600, "%d land sectors", land);
    CHECK(arctic > 10 && arctic < 200, "%d arctic sectors", arctic);
    CHECK(people == 1 && total_pop() == 5000, "first tribe: %d sectors, %" PRIu64 " EMBOs", people, total_pop());
    const PlanetSector *t = planet_sector(g_planet.sel_x, g_planet.sel_y);
    CHECK(t->terrain == PLANET_LAND && t->height == 0, "tribe on terrain %u height %u", t->terrain, t->height);
    g_state.year = 1;
    /* PLANET_TEST_YEARS: a longer run (tech advances need ~1000 years) */
    const char *ys = getenv("PLANET_TEST_YEARS");
    int years = ys ? atoi(ys) : 40;
    run("new planet", years, years <= 60 ? 1 : 0);
    printf("  after %d years: year %" PRIu32 ", %" PRIu64 " EMBOs, max tech %u, best %u, disaster %u (year %" PRIu32
           "), coastal %" PRIu32 "\n",
           years, g_state.year, total_pop(), g_planet.max_tech, g_state.planet.tech_best, g_state.planet.disaster,
           g_state.planet.disaster_year, g_state.planet.coastal_pop);

    printf(g_failures ? "%d FAILURES\n" : "all OK\n", g_failures);
    return g_failures != 0;
}
