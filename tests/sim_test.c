/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * sim_test <data dir> [-y years] save.ASC|.ALF ...
 *
 * Runs the WP1 simulation (src/game/sim.c, economy.c, realm.c) on each save
 * with a stopped clock (so every incremental step does its whole pass) and
 * checks invariants:
 * - the realm year alone, per realm: the money spent equals what the year
 *   books as spending (unk_5f7ed4); the workforce figures add up (trained +
 *   imported = jobs >= the per-set job totals; a shortfall only without a
 *   surplus); the wages and the road upkeep match the formulas of
 *   game/economy.h recomputed from the map and the realm's rates;
 * - whole years through sim_update: the year advances once per pass of the
 *   phase machine; over the year the pennies change by exactly income -
 *   spending (unk_5fa5f4 - unk_5f7ed4, checked right before year end resets
 *   them); every realm's population is the sum of its zone cells' SOULs and
 *   n_zones the number of zone cells; the soul rate stays in 0xd..0xa00;
 * - determinism: two runs from the same load and RNG state end in
 *   byte-identical saves, and the result still round-trips through the
 *   ALSV writer.
 * The data dir must hold ALIFE.EXE (the planet's tables come from it).
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/economy.h"
#include "game/planet.h"
#include "game/realm.h"
#include "game/savegame.h"
#include "game/sim.h"
#include "game/souls.h"
#include "game/tables.h"
#include "game/util.h"
#include "game/world.h"
#include "platform/platform.h"

static int g_failures;
static int g_messages;

#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("  FAIL: " __VA_ARGS__);                                                                            \
            printf("\n");                                                                                              \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

static uint32_t g_clock;
static uint32_t test_clock(void) { return g_clock; }
static int test_message(const char *title, const char *text, const char *buttons)
{
    (void)title, (void)text, (void)buttons;
    g_messages++;
    return 1;
}

/* the year-end check, called by sim_update right before it clears the
 * year's income/spending */
static int64_t g_year_pennies;
static int g_year_checked;
static void on_graph_record(void)
{
    int64_t want = g_year_pennies + (int64_t)g_state.unk_5fa5f4 - (int64_t)g_state.unk_5f7ed4;
    CHECK(bignum_value(g_state.pennies) == want, "year %" PRIu32 ": pennies %" PRId64 ", want %" PRId64
          " (start %" PRId64 " + income %" PRIu32 " - spending %" PRIu32 ")",
          g_state.year, bignum_value(g_state.pennies), want, g_year_pennies, g_state.unk_5fa5f4,
          g_state.unk_5f7ed4);
    g_year_checked++;
}

static void setup(void)
{
    tables_init_defaults();
    souls_reset();
    sim_reset();
    memset(&g_sim_hooks, 0, sizeof g_sim_hooks);
    g_sim_hooks.clock = test_clock;
    g_sim_hooks.message = test_message;
    g_sim_hooks.graph_record = on_graph_record;
    g_planet_hooks.souls = realm_souls_from_planet;
    g_clock = 1000000;
}

static bool load(const char *path)
{
    char err[256];
    if (!world_load(path, err, sizeof err)) {
        printf("  cannot load: %s\n", err);
        return false;
    }
    setup();
    return true;
}

/* population and zone count of the current realm against its cells */
static void check_realm_cells(const char *when)
{
    PlaneStats *r = realm_cur();
    uint64_t pop = 0;
    unsigned zones = 0;
    for (int y = 0; y < 100; y++)
        for (int x = 0; x < 100; x++) {
            unsigned set = cell_tileset(x, y);
            if (set == 14 || set == 15)
                zones++;
            unsigned os = set == 0 ? cell_origin_tileset(x, y) : set;
            if (os == 14 || os == 15)
                pop += sim_pop(x, y);
        }
    CHECK(pop == r->population, "%s, plane %d: population %" PRIu32 ", cells hold %" PRIu64, when, g_world.plane,
          r->population, pop);
    CHECK(zones == r->n_zones || r->n_zones == 0, "%s, plane %d: n_zones %u, cells %u", when, g_world.plane,
          (unsigned)r->n_zones, zones);
}

/* the wages of economy.h, from the finished year's figures */
static uint32_t expected_wages(const PlaneStats *r)
{
    const uint32_t *w = r->unk_7ff0;
    uint32_t training = r->unk_804f[2] - r->unk_8063[2];
    uint32_t wages = 0, trainees = training;
    uint32_t all = w[RW_WORKFORCE] + w[RW_SURPLUS];
    if (all != 0) {
        uint32_t tr = training < all ? training : all;
        trainees = training - tr;
        if (tr)
            wages += mul_div_clamp(w[RW_RATE_D] + w[RW_RATE_A], 255, tr);
        if (all - tr)
            wages += mul_div_clamp(w[RW_RATE_A] + w[RW_RATE_C], 255, all - tr);
    }
    if (w[RW_SHORTFALL] != 0) {
        uint32_t tr = trainees < w[RW_SHORTFALL] ? trainees : w[RW_SHORTFALL];
        if (tr)
            wages += mul_div_clamp(w[RW_RATE_D] + w[RW_RATE_B], 255, tr);
        if (w[RW_SHORTFALL] - tr)
            wages += mul_div_clamp(w[RW_RATE_B] + w[RW_RATE_C], 255, w[RW_SHORTFALL] - tr);
    }
    return wages;
}

static uint32_t expected_road_upkeep(void)
{
    uint32_t sum = 0;
    for (int x = 0; x < 100; x++)
        for (int y = 0; y < 100; y++) {
            if (cell_tileset(x, y) != 12 || cell_test_shade(x, y))
                continue;
            int d = 0;
            for (uint32_t v = sim_cell_u32(sim_cell(x, y), 9) / 10; v != 0; v /= 10)
                d += 2;
            sum += 1u << d;
        }
    return sum / 0x45;
}

static void test_realm_year(int plane)
{
    world_select_plane(plane);
    PlaneStats *r = realm_cur();
    uint32_t roads = expected_road_upkeep();
    int64_t p0 = bignum_value(g_state.pennies);
    uint32_t spent0 = g_state.unk_5f7ed4;
    int calls = 0;
    while (!economy_realm_year() && ++calls < 1000) {
    }
    CHECK(calls == 0, "plane %d: realm year took %d extra calls with a stopped clock", plane, calls);
    const uint32_t *w = r->unk_7ff0;
    uint32_t spent = g_state.unk_5f7ed4 - spent0;
    CHECK(bignum_value(g_state.pennies) == p0 - (int64_t)spent,
          "plane %d: pennies %" PRId64 " -> %" PRId64 ", spending booked %" PRIu32, plane, p0,
          bignum_value(g_state.pennies), spent);
    uint32_t jobs = w[RW_TRAINED] + w[RW_IMPORTED], per_set = 0;
    for (int i = RW_JOBS_ZONES; i <= RW_JOBS_SET30; i++)
        per_set += w[i];
    CHECK(jobs >= per_set, "plane %d: jobs %" PRIu32 " < per-set jobs %" PRIu32, plane, jobs, per_set);
    if (w[RW_SHORTFALL] != 0)
        CHECK(w[RW_SURPLUS] == 0 && w[RW_WORKFORCE] + w[RW_SHORTFALL] == jobs,
              "plane %d: shortfall %" PRIu32 " with surplus %" PRIu32 ", workforce %" PRIu32 ", jobs %" PRIu32, plane,
              w[RW_SHORTFALL], w[RW_SURPLUS], w[RW_WORKFORCE], jobs);
    else
        CHECK(w[RW_WORKFORCE] == jobs, "plane %d: workforce %" PRIu32 " != jobs %" PRIu32, plane, w[RW_WORKFORCE],
              jobs);
    CHECK(w[RW_WAGES] == expected_wages(r), "plane %d: wages %" PRIu32 ", formula %" PRIu32, plane, w[RW_WAGES],
          expected_wages(r));
    CHECK(w[RW_ROAD_UPKEEP] == roads, "plane %d: road upkeep %" PRIu32 ", formula %" PRIu32, plane,
          w[RW_ROAD_UPKEEP], roads);
    /* the rest of the spending is bank instalments */
    CHECK(spent >= r->unk_8127 + w[RW_WAGES] + w[RW_ROAD_UPKEEP], "plane %d: spending %" PRIu32 " too small", plane,
          spent);
    check_realm_cells("after the realm year");
    printf("  plane %d: jobs %" PRIu32 " (trained %" PRIu32 "), workforce %" PRIu32 ", short %" PRIu32
           ", surplus %" PRIu32 ", wages %" PRIu32 ", roads %" PRIu32 ", penalty %" PRIu32 ", spent %" PRIu32 "\n",
           plane, jobs, w[RW_TRAINED], w[RW_WORKFORCE], w[RW_SHORTFALL], w[RW_SURPLUS], w[RW_WAGES],
           w[RW_ROAD_UPKEEP], r->unk_8127, spent);
}

/* runs whole years through sim_update; returns false if stuck */
static bool run_years(int years, bool verbose)
{
    for (int i = 0; i < years; i++) {
        uint32_t year = g_state.year;
        int calls = 0;
        /* a loaded game may be mid-year: the first check starts at phase 0 */
        g_year_pennies = bignum_value(g_state.pennies) - (int64_t)g_state.unk_5fa5f4 + (int64_t)g_state.unk_5f7ed4;
        while (g_state.year == year && calls < 200000) {
            sim_update(1);
            g_clock += 1;
            calls++;
            uint32_t rate = g_state.unk_630430;
            if (g_state.sim_phase == 70 && (rate < 0xd || rate > 0xa00)) {
                CHECK(false, "soul rate %" PRIu32 " out of range", rate);
                return false;
            }
        }
        if (g_state.year != year + 1) {
            CHECK(false, "year stuck at %" PRIu32 " in phase %u after %d calls", year, g_state.sim_phase, calls);
            return false;
        }
        CHECK(g_state.sim_phase == 0, "phase %u after year end", g_state.sim_phase);
        for (int p = GS_PLANE_HELL; p <= GS_PLANE_HEAVEN; p += 2) {
            world_select_plane(p);
            check_realm_cells("after a year");
        }
        if (verbose)
            printf("  year %" PRIu32 ": %d calls, pennies %" PRId64 ", soul rate %" PRIu32 ".%02" PRIu32
                   ", SOULs %" PRIu32 " + %" PRIu32 ", score %" PRIu32 "\n",
                   g_state.year, calls, bignum_value(g_state.pennies), g_state.unk_630430 >> 8,
                   (g_state.unk_630430 & 0xff) * 100 / 256, world_realm(GS_PLANE_HEAVEN)->population,
                   world_realm(GS_PLANE_HELL)->population, g_state.unk_57e09c);
    }
    return true;
}

static uint8_t *snapshot(size_t *len)
{
    return savegame_save_mem(&g_state, len);
}

static void test_save(const char *path, int years)
{
    printf("%s\n", path);
    if (!load(path)) {
        g_failures++;
        return;
    }
    for (int p = GS_PLANE_HELL; p <= GS_PLANE_HEAVEN; p += 2) {
        world_select_plane(p);
        check_realm_cells("loaded");
    }

    /* the realm year alone */
    rng_seed(12345);
    test_realm_year(GS_PLANE_HEAVEN);
    test_realm_year(GS_PLANE_HELL);

    /* whole years, twice from the same start */
    size_t len1 = 0, len2 = 0;
    uint8_t *a = NULL, *b = NULL;
    for (int run = 0; run < 2; run++) {
        if (!load(path)) {
            g_failures++;
            return;
        }
        rng_seed(777);
        g_messages = 0;
        g_year_checked = 0;
        bool ok = run_years(years, run == 0);
        CHECK(!ok || g_year_checked >= years, "only %d year-end checks for %d years", g_year_checked, years);
        if (run == 0)
            a = snapshot(&len1);
        else
            b = snapshot(&len2);
        if (!ok)
            break;
    }
    if (a && b)
        CHECK(len1 == len2 && memcmp(a, b, len1) == 0, "two runs from the same state differ");
    if (a) {
        GameState *gs = calloc(1, sizeof *gs);
        char err[128];
        if (gs && savegame_load_mem(gs, a, len1, err, sizeof err)) {
            size_t len3 = 0;
            uint8_t *c = savegame_save_mem(gs, &len3);
            CHECK(c && len3 == len1 && memcmp(a, c, len1) == 0, "the simulated state doesn't round-trip");
            free(c);
            savegame_free(gs);
        } else {
            CHECK(false, "the simulated state doesn't load: %s", err);
        }
        free(gs);
    }
    printf("  %d message boxes\n", g_messages);
    free(a);
    free(b);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <data dir> [-y years] save.ASC|.ALF ...\n", argv[0]);
        return 2;
    }
    plat_set_data_dir(argv[1]);
    if (!planet_tables_load()) {
        fprintf(stderr, "cannot read the planet tables from %s/ALIFE.EXE\n", argv[1]);
        return 2;
    }
    int years = 3;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-y") == 0 && i + 1 < argc) {
            years = atoi(argv[++i]);
            continue;
        }
        test_save(argv[i], years);
    }
    world_free();
    printf(g_failures ? "%d failures\n" : "ok\n", g_failures);
    return g_failures != 0;
}
