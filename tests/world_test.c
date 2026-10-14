/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * world_test [save.ASC|.ALF ...]
 *
 * Checks the world core (src/game/world.c, util.c):
 * - the RNG against the original's rand (0x4b1202, MSVC LCG; the first
 *   values from state 1 are the well-known MSVC sequence) and rand_range
 *   (0x42b088) against values traced by hand from its disassembly;
 * - bignum, mul_div and number formatting edge cases, derived from the
 *   decompiled originals;
 * - for each save: loads it through world_load and cross-checks the cell
 *   and sim-cell accessors against the realm statistics the save holds
 *   (population = sum of zone SOULs, n_zones, the port list), plus
 *   building footprints and both origin encodings.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/util.h"
#include "game/world.h"

static int g_failures;

#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("  FAIL: " __VA_ARGS__);                                                                            \
            printf("\n");                                                                                              \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

static Bignum bn(int32_t hi, uint32_t lo)
{
    Bignum b = {hi, lo};
    return b;
}

static void test_rng(void)
{
    static const int msvc[10] = {41, 18467, 6334, 26500, 19169, 15724, 11478, 29358, 26962, 24464};
    CHECK(rng_state() == RNG_INITIAL_STATE, "initial state %" PRIu32, rng_state());
    for (int i = 0; i < 10; i++) {
        int r = crt_rand();
        CHECK(r == msvc[i], "rand #%d = %d, want %d", i, r, msvc[i]);
    }
    CHECK(rng_state() == 0xdf90722bu, "state after 10 draws %08" PRIx32, rng_state());

    static const int r99[8] = {41, 67, 34, 0, 69, 24, 78, 58};
    rng_seed(1);
    for (int i = 0; i < 8; i++) {
        int r = rand_range(0, 99);
        CHECK(r == r99[i], "rand_range(0,99) #%d = %d, want %d", i, r, r99[i]);
    }
    static const int rsw[8] = {10, -2, 3, 9, 7, 6, 2, -10};
    rng_seed(1);
    for (int i = 0; i < 8; i++) {
        int r = rand_range(10, -10); /* swapped bounds, negative values */
        CHECK(r == rsw[i], "rand_range(10,-10) #%d = %d, want %d", i, r, rsw[i]);
    }
    /* ranges >= 0x8000 use two draws, the first as the high half */
    static const int rbig[4] = {5416, 27373, 62746, 44044};
    rng_seed(1);
    for (int i = 0; i < 4; i++) {
        int r = rand_range(0, 100000);
        CHECK(r == rbig[i], "rand_range(0,100000) #%d = %d, want %d", i, r, rbig[i]);
    }
    {
        uint32_t st = rng_state();
        rng_seed(1);
        for (int i = 0; i < 8; i++)
            crt_rand();
        CHECK(st == rng_state(), "two draws per value");
    }
    static const int rneg[4] = {-294558, 131317, -725320, -748810};
    rng_seed(1);
    for (int i = 0; i < 4; i++) {
        int r = rand_range(-1000000, 1000000);
        CHECK(r == rneg[i], "rand_range(-1e6,1e6) #%d = %d, want %d", i, r, rneg[i]);
    }
    rng_seed(5);
    CHECK(rand_range(7, 7) == 7, "rand_range(7,7)");
    rng_seed(1);
    rng_warmup(12345);
    CHECK(rng_state() == 0x0ebf9b38u, "state after warmup %08" PRIx32, rng_state());
    CHECK(crt_rand() == 2762, "draw after warmup");
    rng_seed(RNG_INITIAL_STATE);
}

static void test_bignum(void)
{
    Bignum b = bn(0, 0xfffffff0u);
    bignum_add(&b, 0x20); /* carry */
    CHECK(b.hi == 1 && b.lo == 0x10, "add carry: %d:%" PRIu32, b.hi, b.lo);
    bignum_sub(&b, 0x20); /* borrow */
    CHECK(b.hi == 0 && b.lo == 0xfffffff0u, "sub borrow: %d:%" PRIu32, b.hi, b.lo);
    b = bn(0, 0xffffffffu);
    bignum_add(&b, 1);
    CHECK(b.hi == 1 && b.lo == 0, "add exact carry");
    b = bn(0, 5);
    bignum_sub(&b, 8); /* crosses zero: sign and magnitude */
    CHECK(b.hi == -1 && b.lo == 3 && bignum_to_i64(b) == -3, "sub cross: %d:%" PRIu32, b.hi, b.lo);
    bignum_sub(&b, 4);
    CHECK(b.hi == -1 && b.lo == 7, "sub negative");
    bignum_add(&b, 2);
    CHECK(b.hi == -1 && b.lo == 5, "add negative");
    bignum_add(&b, 5); /* to exactly zero */
    CHECK(b.hi == 0 && b.lo == 0, "add to zero: %d:%" PRIu32, b.hi, b.lo);
    b = bn(-1, 5);
    bignum_add(&b, 9);
    CHECK(b.hi == 0 && b.lo == 4, "add cross");
    b = bn(0, 0);
    bignum_sub(&b, 0);
    bignum_add(&b, 0);
    CHECK(b.hi == 0 && b.lo == 0, "zero amounts");
    b = bn(0, 0);
    bignum_sub(&b, 1);
    CHECK(b.hi == -1 && b.lo == 1, "0 - 1");
    b = bn(-1, 0xfffffff0u);
    bignum_sub(&b, 0x20); /* negative magnitude carries */
    CHECK(b.hi == -2 && b.lo == 0x10 && bignum_to_i64(b) == -(int64_t)0x100000010, "sub neg carry");
    b = bn(2, 3);
    bignum_sub(&b, 4);
    CHECK(b.hi == 1 && b.lo == 0xffffffffu, "sub borrow hi 2");

    CHECK(bignum_less(bn(0, 5), 6) && !bignum_less(bn(0, 6), 6), "less small");
    CHECK(!bignum_less(bn(1, 0), 0x7fffffff), "less hi>0");
    CHECK(bignum_less(bn(-1, 1), 0), "pennies < 0 (bankruptcy test)");
    CHECK(!bignum_less(bn(0, 0), 0), "0 < 0");
    CHECK(bignum_less(bn(-1, 10), -5) && !bignum_less(bn(-1, 5), -5) && !bignum_less(bn(-1, 3), -5), "less neg");
    CHECK(!bignum_less(bn(0, 1), -5), "pos < neg");
    CHECK(bignum_less(bn(-2, 0), -5), "less hi<-1");

    CHECK(bignum_ge(bn(0, 5), bn(0, 5)) && !bignum_gt(bn(0, 5), bn(0, 5)), "ge/gt equal");
    CHECK(bignum_gt(bn(1, 0), bn(0, 0xffffffffu)), "gt hi");
    CHECK(bignum_gt(bn(-1, 3), bn(-1, 5)) && !bignum_gt(bn(-1, 5), bn(-1, 3)), "gt negative");
    CHECK(bignum_ge(bn(-1, 3), bn(-1, 3)) && !bignum_ge(bn(-1, 5), bn(-1, 3)), "ge negative");
    CHECK(bignum_gt(bn(0, 0), bn(-1, 1)) && !bignum_ge(bn(-1, 1), bn(0, 0)), "mixed sign");

    CHECK(bignum_ge_int(bn(0, 5), 5) && !bignum_ge_int(bn(0, 4), 5), "ge_int");
    CHECK(bignum_ge_int(bn(1, 0), 0x7fffffff), "ge_int hi");
    CHECK(!bignum_ge_int(bn(-1, 1), 1), "ge_int negative");
    CHECK(bignum_ge_int(bn(0, 0), 0) && bignum_ge_int(bn(0, 0), -3), "ge_int v<=0");
    CHECK(!bignum_ge_int(bn(-1, 2), -10), "ge_int quirk: -2 >= -10 is false in the original");

    static const int64_t vals[] = {0, 1, -1, 0xffffffffLL, -0xffffffffLL, 0x123456789LL, 1000000000000LL};
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++)
        CHECK(bignum_to_i64(bignum_from_i64(vals[i])) == vals[i], "from/to i64 %" PRId64, vals[i]);
}

static void test_mul_div(void)
{
    CHECK(mul_div_clamp(10, 3, 20) == 20, "clamp 10*20/3 -> clamped to 20");
    CHECK(mul_div_round(2, 3, 10) == 7, "round 2*10/3 = 6.67 -> 7");
    CHECK(mul_div_round(1, 3, 10) == 3, "round 10/3 = 3.33 -> 3");
    CHECK(mul_div_round(1, 4, 10) == 2, "round 2.5 -> 2 (remainder must exceed b/2)");
    CHECK(mul_div_round(3, 4, 10) == 7, "round 7.5 -> 7");
    CHECK(mul_div_clamp(5, 2, 3) == 3, "clamp to c");
    CHECK(mul_div_round(5, 2, 3) == 7, "round: no clamp when a > b");
    CHECK(mul_div_round(2, 2, 3) == 3, "round: a <= b");
    CHECK(mul_div_clamp(7, 0, 3) == 0 && mul_div_clamp(7, 3, 0) == 0, "zero b/c");
    CHECK(mul_div_clamp(0x80000000u, 0x80000000u, 0x10000) == 0x10000, "overflow path halves 8 times");
    CHECK(mul_div_round(0xffffffffu, 3, 2) == 0xfffffffeu, "overflow quirk: shifted quotient");
    CHECK(mul_div_clamp(0xffffffffu, 3, 2) == 2, "overflow then clamp");
    CHECK(mul_div_clamp(0xffffffffu, 1, 1) == 0, "halving b to 0 -> 0");
}

static void test_fmt(void)
{
    char b[FMT_NUM_LEN];
    struct {
        uint32_t v;
        const char *s, *k;
    } t[] = {
        {0, "0", "0"},
        {7, "7", "7"},
        {999, "999", "999"},
        {1000, "1,000", "1,000"},
        {1001, "1,001", "1,001"},
        {1234567, "1,234,567", "1,234K"},
        {1000000, "1,000,000", "1,000,000"},
        {1000001, "1,000,001", "1,000K"},
        {1000000000, "1,000,000,000", "1,000,000K"},
        {4294967295u, "4,294,967,295", "4,294,967K"},
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        CHECK(strcmp(fmt_thousands(b, t[i].v), t[i].s) == 0, "fmt %" PRIu32 " -> \"%s\"", t[i].v, b);
        CHECK(strcmp(fmt_thousands_k(b, t[i].v), t[i].k) == 0, "fmt_k %" PRIu32 " -> \"%s\"", t[i].v, b);
    }
}

static void test_footprint_table(void)
{
    static const struct {
        unsigned idx;
        int n;
    } t[] = {{0, 1},    {4, 1},    {5, 2},    {11, 2},   {12, 3},   {13, 1},   {90, 3},   {0x5b, 1},
             {0x5f, 1}, {0x60, 2}, {0x6c, 2}, {0x6d, 3}, {0x6e, 3}, {0x6f, 4}, {0x70, 2}, {0x7f, 2}};
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        CHECK(tile_index_footprint(t[i].idx) == t[i].n, "footprint(%u)", t[i].idx);
}

/* Bit-level accessors on a scratch state. */
static void test_accessors(void)
{
    world_reset();
    world_select_plane(GS_PLANE_HEAVEN);
    cell_set_tileset(10, 20, 15);
    cell_set_index(10, 20, 0x6f);
    cell_set_strip(10, 20, 9);
    cell_set_flags(10, 20, 0x8005);
    CHECK(cell_get(10, 20) == (15u << 27 | 0x6fu << 16 | 9u << 23 | 0x8005u), "cell bits %08" PRIx32, cell_get(10, 20));
    CHECK(g_state.plane[3].cells[20 * 100 + 10] == cell_get(10, 20), "row-major grid");
    CHECK(cell_tileset(10, 20) == 15 && cell_index(10, 20) == 0x6f && cell_strip(10, 20) == 9, "getters");
    CHECK(cell_test_bracket(10, 20) && cell_test_shade(10, 20) && cell_test_overlay(10, 20), "flag tests");
    cell_clear_flags(10, 20, 0x8001);
    CHECK(!cell_test_bracket(10, 20) && !cell_test_shade(10, 20) && cell_test_overlay(10, 20), "clear flags");
    /* a part cell of that 4x4 building: dx 2, dy 3 */
    cell_set_tileset(12, 17, 0);
    cell_set_index(12, 17, 3 << 4 | 2);
    CHECK(cell_origin_tileset(12, 17) == 15 && cell_origin_index(12, 17) == 0x6f, "origin lookup");
    CHECK(cell_origin_tileset(10, 20) == 15, "origin of an origin");
    /* object slot */
    g_state.plane[3].objmap[17 * 100 + 12] = 5 << 2 | 3;
    CHECK(cell_object_slot(12, 17) == 0, "no CELL_OBJECT");
    cell_set_flags(12, 17, 0x40);
    CHECK(cell_object_slot(12, 17) == 5, "object slot");
    /* sim cells */
    sim_set_zone_kind(3, 4, 6);
    sim_cell(3, 4)->b[1] |= 0x50;
    sim_set_zone_kind(3, 4, 2);
    CHECK(sim_zone_kind(3, 4) == 2 && sim_cell(3, 4)->b[1] == 0x52, "zone kind nibble");
    sim_cell_set_u32(sim_cell(3, 4), 2, 1000);
    sim_cell_set_u32(sim_cell(3, 4), 6, 234);
    CHECK(sim_perm(3, 4) == 1000 && sim_temp(3, 4) == 234 && sim_pop(3, 4) == 1234, "perm/temp");
    CHECK(g_state.plane[3].sim[4 * 100 + 3].b[2] == (1000 & 0xff), "sim row-major");
    /* soul groups */
    soul_group_set(50, 60, 77);
    CHECK(soul_group_get(50, 60) == 0 && sim_cell_u32(sim_cell(50, 60), 1) >> 16 == 0, "free group cleared");
    g_state.soul_groups[77].count = 12;
    soul_group_set(50, 60, 77);
    sim_cell(50, 60)->b[1] = 0xab; /* the low half is untouched */
    CHECK(soul_group_get(50, 60) == 77 && sim_cell(50, 60)->b[1] == 0xab, "soul group");
    CHECK(soul_group_get(200, 60) == 0 && soul_group_get(-1, 0) == 0, "out of map");
    /* plane switch */
    world_select_plane(GS_PLANE_MIDDLE);
    CHECK(g_world.cells == g_state.plane[2].cells && g_world.sim == g_state.plane[3].sim &&
              realm_cur() == world_realm(3) && g_world.road_entry == 0x21 && g_world.plane == 2,
          "plane 2 keeps the realm");
    world_select_plane(GS_PLANE_HELL);
    CHECK(realm_cur() == world_realm(1) && g_world.tile_info == g_world.tile_info_hell &&
              g_world.water_entry_max == 3 && g_world.objframe_base == 0x24,
          "Hell constants");
    world_reset();
}

static void check_save(const char *path)
{
    char err[160];
    if (!world_load(path, err, sizeof err)) {
        printf("%s: load failed: %s\n", path, err);
        g_failures++;
        return;
    }
    printf("%s: \"%s\", year %" PRIu32 ", pennies %" PRId64 "\n", path, g_state.hdr.title, g_state.year,
           gs_pennies(&g_state));
    CHECK(g_world.plane == GS_PLANE_HELL && realm_cur() == world_realm(1), "Hell selected after load");
    for (int pl = GS_PLANE_HEAVEN; pl >= GS_PLANE_HELL; pl -= 2) {
        world_select_plane(pl);
        PlaneStats *r = realm_cur();
        int sets[32] = {0};
        int zones = 0, buildings = 0, specials = 0, parts = 0, zone_cells = 0, roads = 0, grouped = 0;
        uint64_t pop = 0;
        for (int y = 0; y < GS_MAP_H; y++)
            for (int x = 0; x < GS_MAP_W; x++) {
                unsigned set = cell_tileset(x, y);
                sets[set]++;
                unsigned oset = cell_origin_tileset(x, y);
                if (oset == 14 || oset == 15)
                    pop += sim_pop(x, y);
                if (set == 0) {
                    int ox, oy;
                    cell_origin(x, y, &ox, &oy);
                    const SimCell *sc = sim_cell(x, y);
                    /* the sim cell's +0xa/+0xb repeat the origin; one set-18
                     * structure each in ASCEN001 Heaven and ASCEN002 Hell has
                     * them 0, so set 18 is left out */
                    if (oset != 0 && oset != 18 && (sc->b[10] != ox || sc->b[11] != oy)) {
                        CHECK(0, "plane %d (%d,%d): sim origin %d,%d != cell origin %d,%d (set %u)", pl, x, y,
                              sc->b[10], sc->b[11], ox, oy, oset);
                    }
                    if (oset != 0)
                        parts++;
                }
                if (set == 14 || set == 15) {
                    zone_cells++;
                    unsigned idx = cell_index(x, y);
                    if (idx >= 0x5b)
                        specials++;
                    else if (idx % 13 == 0)
                        zones++;
                    else
                        buildings++;
                    /* every cell of the footprint points back here */
                    int n = tile_index_footprint(idx);
                    for (int dx = 0; dx < n; dx++)
                        for (int dy = 0; dy < n; dy++) {
                            int px = x + dx, py = y - dy;
                            if (px >= GS_MAP_W || py < 0) {
                                CHECK(0, "plane %d (%d,%d): footprint %d leaves the map", pl, x, y, n);
                                continue;
                            }
                            int ox, oy;
                            cell_origin(px, py, &ox, &oy);
                            if (ox != x || oy != y)
                                CHECK(0, "plane %d (%d,%d) part (%d,%d) -> origin (%d,%d)", pl, x, y, px, py, ox, oy);
                        }
                }
                if (set == 12) {
                    roads++;
                    grouped += soul_group_get(x, y) != 0;
                }
            }
        const char *name = pl == 1 ? "Hell" : "Heaven";
        printf("  %-6s population %" PRIu32 " (cells %" PRIu64 "), zone cells %d (%d empty, %d buildings, %d special, "
               "%d part cells), ports %u, roads %d (%d with a soul group)\n",
               name, r->population, pop, zone_cells, zones, buildings, specials, parts, r->n_ports, roads, grouped);
        printf("         cells by set:");
        for (int i = 0; i < 32; i++)
            if (sets[i])
                printf(" %d:%d", i, sets[i]);
        printf("\n");
        CHECK(pop == r->population, "%s: population %" PRIu32 " != sum of zone SOULs %" PRIu64, name, r->population,
              pop);
        CHECK(zone_cells == r->n_zones, "%s: n_zones %u != zone cells %d", name, r->n_zones, zone_cells);
        CHECK(sets[13] == r->n_ports, "%s: n_ports %u != set-13 cells %d", name, r->n_ports, sets[13]);
        for (int i = 0; i < r->n_ports && i < 50; i++) {
            int x = r->ports[i][0], y = r->ports[i][1];
            CHECK(x < GS_MAP_W && y < GS_MAP_H && cell_tileset(x, y) == 13, "%s: port %d (%d,%d) not set 13", name, i,
                  x, y);
            for (int j = 0; j < i; j++)
                CHECK(r->ports[j][0] != x || r->ports[j][1] != y, "%s: port %d repeats", name, i);
        }
        CHECK(r->unk_80ef <= 100, "%s: +0x80ef not clamped", name);
    }
    world_select_plane(GS_PLANE_MIDDLE);
    CHECK(realm_cur() == world_realm(1), "plane 2 keeps Hell's realm");
}

int main(int argc, char **argv)
{
    test_rng();
    test_bignum();
    test_mul_div();
    test_fmt();
    test_footprint_table();
    test_accessors();
    printf("unit checks: %s\n", g_failures ? "FAILED" : "ok");
    for (int a = 1; a < argc; a++)
        check_save(argv[a]);
    world_free();
    if (g_failures)
        printf("%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
