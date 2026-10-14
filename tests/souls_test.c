/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * souls_test [-y years] save.ASC|.ALF ...
 *
 * Runs the SOUL flow (src/game/souls.c, zones.c) on each save, phase by
 * phase like sim_update does it (phase 30 arrivals for Heaven then Hell,
 * zone scans 100/90, flows 40/50), for a few years, and checks:
 * - per realm, population == sum of SOULs in zone cells (as world_test)
 *   after every phase, and the per-kind/global totals move with it;
 * - zone scan: the population drops by exactly the residents removed, and
 *   the groups grow by exactly what was sent on (the two differ where the
 *   original's shares exceed a cell's SOULs; reported, not an error);
 * - arrivals: SOULs added to groups == SOULs the Gates counted in, and
 *   == what the siphons let go;
 * - flow: every SOUL a group carried is accounted for (zones, training,
 *   Gates, Karma stations, Omnibolges, siphons, lost), new residents ==
 *   population growth, and no road cell refers to a live group afterwards;
 * - resuming: the flow run with a budget of one group per call ends in the
 *   same state (byte for byte, through the ALSV writer) as one call;
 * - the state still saves and reloads.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/newworld.h"
#include "game/savegame.h"
#include "game/souls.h"
#include "game/tables.h"
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

static const char *realm_name(int pl) { return pl == GS_PLANE_HELL ? "Hell" : "Heaven"; }

/* Sum of permanent / temporary SOULs over zone building cells (sets 14/15
 * and their set-0 parts), as tests/world_test does. */
static void zone_sums(int pl, uint64_t *perm, uint64_t *temp)
{
    const MapPlaneState *p = &g_state.plane[pl];
    *perm = *temp = 0;
    for (int y = 0; y < 100; y++)
        for (int x = 0; x < 100; x++) {
            uint32_t c = p->cells[y * 100 + x];
            unsigned set = c >> 27;
            const SimCell *s = &p->sim[y * 100 + x];
            if (set == 0) {
                unsigned ox = s->b[0xa], oy = s->b[0xb];
                if (ox >= 100 || oy >= 100)
                    continue;
                set = p->cells[oy * 100 + ox] >> 27;
            }
            if (set == 14 || set == 15) {
                *perm += sim_cell_u32(s, 2);
                *temp += sim_cell_u32(s, 6);
            }
        }
}

static uint64_t group_total(void)
{
    uint64_t n = 0;
    for (int i = 0; i < GS_SOUL_GROUPS; i++)
        n += g_state.soul_groups[i].count;
    return n;
}

/* Road cells of the realm that still refer to a group with SOULs. */
static int live_road_groups(int pl)
{
    const MapPlaneState *p = &g_state.plane[pl];
    int n = 0;
    for (int i = 0; i < GS_MAP_CELLS; i++)
        if (p->cells[i] >> 27 == 12) {
            unsigned g = sim_cell_u32(&p->sim[i], 1) >> 16;
            if (g != 0 && g < GS_SOUL_GROUPS && g_state.soul_groups[g].count != 0)
                n++;
        }
    return n;
}

typedef struct Snap {
    uint32_t pop[4];
    uint64_t perm[4], temp[4];
    uint32_t gperm, gtemp; /* unk_5bb114 / unk_57e1b4 */
    uint64_t groups;
} Snap;

static void snap(Snap *s)
{
    for (int pl = GS_PLANE_HELL; pl <= GS_PLANE_HEAVEN; pl += 2) {
        s->pop[pl] = g_state.plane[pl].stats.population;
        zone_sums(pl, &s->perm[pl], &s->temp[pl]);
    }
    s->gperm = g_state.unk_5bb114;
    s->gtemp = g_state.unk_57e1b4;
    s->groups = group_total();
}

static int64_t d64(uint64_t a, uint64_t b) { return (int64_t)(a - b); }

/* population == zone cells, and the deltas of the global totals agree */
static void check_population(const char *what, const Snap *a, const Snap *b)
{
    int64_t dperm = 0, dtemp = 0;
    for (int pl = GS_PLANE_HELL; pl <= GS_PLANE_HEAVEN; pl += 2) {
        CHECK(b->pop[pl] == b->perm[pl] + b->temp[pl], "%s: %s population %" PRIu32 " != zone SOULs %" PRIu64, what,
              realm_name(pl), b->pop[pl], b->perm[pl] + b->temp[pl]);
        dperm += d64(b->perm[pl], a->perm[pl]);
        dtemp += d64(b->temp[pl], a->temp[pl]);
    }
    CHECK((int64_t)(int32_t)(b->gperm - a->gperm) == dperm, "%s: permanent total moved %" PRId32 ", cells %" PRId64,
          what, (int32_t)(b->gperm - a->gperm), dperm);
    CHECK((int64_t)(int32_t)(b->gtemp - a->gtemp) == dtemp, "%s: temporary total moved %" PRId32 ", cells %" PRId64,
          what, (int32_t)(b->gtemp - a->gtemp), dtemp);
}

static void phase_arrivals(int pl)
{
    world_select_plane(pl);
    PlaneStats *r = realm_cur();
    Snap a, b;
    snap(&a);
    uint32_t in0 = souls_gate_arrived;
    souls_gate_arrivals();
    snap(&b);
    check_population("gate arrivals", &a, &b);
    CHECK(d64(b.groups, a.groups) == (int64_t)(souls_gate_arrived - in0),
          "%s gate arrivals: groups +%" PRId64 ", Gates counted %" PRIu32, realm_name(pl), d64(b.groups, a.groups),
          souls_gate_arrived - in0);
    printf("    %-6s gates: %" PRIu32 " SOULs onto the roads\n", realm_name(pl), souls_gate_arrived - in0);

    uint32_t pool0 = r->unk_7f38;
    uint32_t left0 = r->unk_7f30;
    a = b;
    souls_siphon_release();
    snap(&b);
    check_population("siphon release", &a, &b);
    /* The pool is cleared when the last siphon is done; otherwise what the
     * siphons let go is exactly what reached the roads. */
    if (r->unk_7f38 != 0 || left0 == 0 || pool0 == 0)
        CHECK(d64(b.groups, a.groups) == (int64_t)(pool0 - r->unk_7f38),
              "%s siphons: groups +%" PRId64 ", pool -%" PRIu32, realm_name(pl), d64(b.groups, a.groups),
              pool0 - r->unk_7f38);
    if (b.groups != a.groups)
        printf("    %-6s siphons: %" PRId64 " SOULs onto the roads\n", realm_name(pl), d64(b.groups, a.groups));
}

static void phase_zone_scan(int pl)
{
    world_select_plane(pl);
    Snap a, b;
    snap(&a);
    SoulsStats s0 = souls_stats;
    int calls = 0;
    while (!souls_zone_scan())
        calls++;
    snap(&b);
    check_population("zone scan", &a, &b);
    int64_t dpop = (int64_t)b.pop[pl] - (int64_t)a.pop[pl];
    uint64_t removed = souls_stats.left_zones - s0.left_zones, onroad = souls_stats.left_to_roads - s0.left_to_roads;
    CHECK(-dpop == (int64_t)removed, "%s zone scan: population %+" PRId64 " but %" PRIu64 " residents removed",
          realm_name(pl), dpop, removed);
    CHECK(d64(b.groups, a.groups) == (int64_t)onroad, "%s zone scan: groups %+" PRId64 " but %" PRIu64 " sent",
          realm_name(pl), d64(b.groups, a.groups), onroad);
    printf("    %-6s zone scan (%d calls): %" PRIu64 " residents due, %" PRIu64 " without a road; removed %" PRIu64
           ", onto the roads %" PRIu64 " (the original's clamping: %+" PRId64 ")\n",
           realm_name(pl), calls + 1, souls_stats.departed - s0.departed, souls_stats.departed_lost - s0.departed_lost,
           removed, onroad, (int64_t)(onroad - removed));
}

static void phase_flow(int pl, unsigned budget)
{
    world_select_plane(pl);
    Snap a, b;
    snap(&a);
    SoulsStats s0 = souls_stats;
    uint32_t lost0 = g_state.unk_626598;
    int calls = 1;
    while (!souls_flow(budget))
        calls++;
    snap(&b);
    check_population("flow", &a, &b);
    SoulsStats s = souls_stats;
#define D(f) (s.f - s0.f)
    uint64_t in = D(unpacked) + D(tracked_added);
    uint64_t out = D(walk_zones) + D(walk_training) + D(walk_gates) + D(walk_stations) + D(walk_omnibolges) +
                   D(walk_siphons) + D(walk_lost) + D(walk_left);
    CHECK(in == out, "%s flow: groups carried %" PRIu64 " SOULs, accounted %" PRIu64, realm_name(pl), in, out);
    int64_t dpop = (int64_t)b.pop[pl] - (int64_t)a.pop[pl];
    CHECK(dpop == (int64_t)D(to_zones), "%s flow: population %+" PRId64 ", new residents %" PRIu64, realm_name(pl),
          dpop, D(to_zones));
    CHECK(live_road_groups(pl) == 0, "%s flow: %d road cells still hold a live group", realm_name(pl),
          live_road_groups(pl));
    printf("    %-6s flow (%d calls): carried %" PRIu64 " -> zones %" PRIu64 " (residents %" PRIu64
           "), training %" PRIu64 ", Gates %" PRIu64 ", stations %" PRIu64 ", Omnibolges %" PRIu64 ", siphons %" PRIu64
           ", lost %" PRIu64 " (lost_souls %+" PRId32 "), left %" PRIu64 "\n",
           realm_name(pl), calls, in, D(walk_zones), D(to_zones), D(walk_training), D(walk_gates), D(walk_stations),
           D(walk_omnibolges), D(walk_siphons), D(walk_lost), (int32_t)(g_state.unk_626598 - lost0), D(walk_left));
#undef D
}

static uint8_t *save_image(size_t *len)
{
    uint8_t *img = savegame_save_mem(&g_state, len);
    if (!img) {
        printf("  FAIL: savegame_save_mem\n");
        g_failures++;
    }
    return img;
}

static bool load(const char *path)
{
    char err[256];
    tables_init_defaults();
    realm_tile_info_init();
    if (!world_load(path, err, sizeof err)) {
        printf("  FAIL: %s: %s\n", path, err);
        g_failures++;
        return false;
    }
    souls_reset(); /* world_load ran build_after_load (0x4506b6, 0x452267) */
    rng_seed(RNG_INITIAL_STATE);
    return true;
}

static void run_year(int year, unsigned budget)
{
    printf("  year %d:\n", year);
    phase_arrivals(GS_PLANE_HEAVEN);
    phase_arrivals(GS_PLANE_HELL);
    phase_zone_scan(GS_PLANE_HEAVEN);
    phase_zone_scan(GS_PLANE_HELL);
    phase_flow(GS_PLANE_HEAVEN, budget);
    phase_flow(GS_PLANE_HELL, budget);
}

static void test_save(const char *path, int years)
{
    printf("%s\n", path);
    if (!load(path))
        return;
    Snap s;
    snap(&s);
    for (int pl = GS_PLANE_HELL; pl <= GS_PLANE_HEAVEN; pl += 2)
        printf("  %-6s population %" PRIu32 ", groups hold %" PRIu64 " SOULs, %d road cells with a group\n",
               realm_name(pl), s.pop[pl], s.groups, live_road_groups(pl));
    for (int y = 0; y < years; y++)
        run_year(y + 1, 0);
    size_t len_a;
    uint8_t *img_a = save_image(&len_a);

    /* The same years with the flow yielding after every group. */
    if (!load(path))
        return;
    printf("  again, flow budget 1 group per call:\n");
    for (int y = 0; y < years; y++)
        run_year(y + 1, 1);
    size_t len_b;
    uint8_t *img_b = save_image(&len_b);
    CHECK(img_a && img_b && len_a == len_b && memcmp(img_a, img_b, len_a) == 0,
          "flow with a budget ends in a different state");
    CHECK(rng_state() != RNG_INITIAL_STATE, "the RNG was never used");

    /* round trip */
    if (img_a) {
        GameState *gs = calloc(1, sizeof *gs);
        char err[256];
        CHECK(gs && savegame_load_mem(gs, img_a, len_a, err, sizeof err), "reload after the flow failed");
        if (gs) {
            size_t len_c;
            uint8_t *img_c = savegame_save_mem(gs, &len_c);
            CHECK(img_c && len_c == len_a && memcmp(img_c, img_a, len_a) == 0, "save/reload not byte-exact");
            free(img_c);
            savegame_free(gs);
            free(gs);
        }
    }
    free(img_a);
    free(img_b);
}

int main(int argc, char **argv)
{
    int years = 2;
    int i = 1;
    if (i + 1 < argc && strcmp(argv[i], "-y") == 0) {
        years = atoi(argv[i + 1]);
        i += 2;
    }
    if (i >= argc) {
        fprintf(stderr, "usage: souls_test [-y years] save.ASC|.ALF ...\n");
        return 2;
    }
    for (; i < argc; i++)
        test_save(argv[i], years);
    world_free();
    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("all OK\n");
    return 0;
}
