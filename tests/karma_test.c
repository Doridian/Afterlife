/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * karma_test <data dir> [-y years] [-t ticks] save.ASC|.ALF ...
 *
 * The Karma network and the map objects (src/game/karma.c, mapobj.c) on
 * each save:
 * - footprints: with the data dir's MEDIUM.000, mapobj_footprint (derived
 *   from the map) equals the tile entry's size (+0x1a, what the original's
 *   0x4ee5d8 reads) for every structure on the three planes;
 * - the network after karma_clear + karma_rebuild: no work bits left on
 *   plane 2, every station no rail reaches is tinted, karma_busy is clear,
 *   rebuilding again gives the same map;
 * - trains: karma_trains_tick every 500 ms (four object steps,
 *   mapobj_frame): train cars move one cell per step; trains get dispatched,
 *   arrive (the network is released and the trip lands in the history)
 *   and none is left once the dispatching stops;
 * - the object map: CELL_OBJECT is set exactly where a count is;
 * - years of sim_update (stopped clock) with the objects moving between
 *   calls: the realm population matches the zone cells, stations only
 *   take SOULs while a rail reaches them and never more than their
 *   capacity, the result is deterministic and round-trips through the
 *   ALSV writer.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "formats/tile.h"
#include "game/build.h"
#include "game/karma.h"
#include "game/mapobj.h"
#include "game/planet.h"
#include "game/realm.h"
#include "game/resource.h"
#include "game/savegame.h"
#include "game/sim.h"
#include "game/souls.h"
#include "game/tables.h"
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

/* ---- tile sets (for the footprint check) ---------------------------------- */

static TileSet g_sets[32];
static uint8_t *g_set_data[32];
static bool g_have_tiles;

static void load_tiles(void)
{
    if (!rc_initialize("MEDIUM.000", "MEDIUM.000"))
        return;
    for (int id = 1; id <= 31; id++) {
        long size = rc_resource_size((uint32_t)id);
        PlatFile *f = rc_open_resource((uint32_t)id);
        if (!f || size <= 0) {
            if (f)
                plat_file_close(f);
            continue;
        }
        uint8_t *buf = malloc((size_t)size);
        if (buf && plat_file_read(f, buf, 1, (size_t)size) == (size_t)size &&
            tile_set_open(&g_sets[id - 1], buf, (size_t)size) == TILE_OK) {
            g_set_data[id - 1] = buf;
            g_have_tiles = true;
        } else {
            free(buf);
        }
        plat_file_close(f);
    }
}

static int tile_size(unsigned set, unsigned index0)
{
    TileEntry e;
    if (set >= 32 || !g_set_data[set] || tile_get_entry(&g_sets[set], (int)index0, &e) != TILE_OK)
        return -1;
    return e.size;
}

static void check_footprints(void)
{
    int bad = 0, n = 0;
    for (int pl = 1; pl <= 3; pl++) {
        world_select_plane(pl);
        for (int y = 0; y < 100; y++)
            for (int x = 0; x < 100; x++) {
                unsigned set = cell_tileset(x, y);
                if (set == 0)
                    continue;
                int want = tile_size(set, cell_index(x, y));
                if (want < 0)
                    continue;
                n++;
                int got = mapobj_footprint(x, y);
                if (got != want && bad++ < 5)
                    printf("  FAIL: plane %d (%d, %d) set %u entry %u: footprint %d, tile size %d\n", pl, x, y, set,
                           cell_index(x, y), got, want);
            }
    }
    g_failures += bad;
    printf("  footprints: %d structures, %d mismatches\n", n, bad);
}

/* ---- setup -------------------------------------------------------------------- */

static uint32_t g_clock;
static uint32_t test_clock(void) { return g_clock; }
static int test_message(const char *t, const char *x, const char *b)
{
    (void)t, (void)x, (void)b;
    return 1;
}
static int g_redraws;
static void count_redraw(const MapObjRedraw *r)
{
    (void)r;
    g_redraws++;
}

static bool load(const char *path)
{
    char err[256];
    if (!world_load(path, err, sizeof err)) {
        printf("  cannot load: %s\n", err);
        return false;
    }
    tables_init_defaults();
    souls_reset();
    sim_reset();
    karma_reset();
    mapobj_reset();
    memset(&g_sim_hooks, 0, sizeof g_sim_hooks);
    g_sim_hooks.clock = test_clock;
    g_sim_hooks.message = test_message;
    g_planet_hooks.souls = realm_souls_from_planet;
    memset(&g_mapobj_hooks, 0, sizeof g_mapobj_hooks);
    g_mapobj_hooks.redraw = count_redraw;
    g_clock = 1000000;
    rng_seed(12345);
    return true;
}

/* ---- checks ----------------------------------------------------------------------- */

static void check_objmap(const char *when)
{
    for (int pl = 1; pl <= 3; pl++) {
        int bad = 0;
        for (int i = 0; i < GS_MAP_CELLS; i++) {
            bool flag = (g_state.plane[pl].cells[i] & 0x40) != 0, cnt = (g_state.plane[pl].objmap[i] & 3) != 0;
            if (flag != cnt && bad++ < 3)
                printf("  FAIL: %s: plane %d (%d, %d): CELL_OBJECT %d, objmap %#x\n", when, pl, i % 100, i / 100, flag,
                       g_state.plane[pl].objmap[i]);
        }
        g_failures += bad;
    }
}

typedef struct NetInfo {
    int rails, stations, reached, tinted_unreached, networks;
} NetInfo;

static NetInfo check_network(const char *when)
{
    NetInfo ni = {0};
    world_select_plane(GS_PLANE_MIDDLE);
    unsigned nets = 0;
    int work = 0;
    for (int y = 0; y < 100; y++)
        for (int x = 0; x < 100; x++) {
            uint32_t c = cell_get(x, y);
            if ((c >> 27) == 16) {
                ni.rails++;
                nets |= 1u << ((c >> 11) & 15);
            }
            if (c & 0x8000)
                work++;
        }
    for (int i = 0; i < 16; i++)
        ni.networks += (nets >> i) & 1;
    CHECK(work == 0, "%s: %d plane-2 cells keep the flood mark", when, work);
    for (int pl = 1; pl <= 3; pl += 2) {
        world_select_plane(pl);
        for (int y = 0; y < 100; y++)
            for (int x = 0; x < 100; x++) {
                if (cell_tileset(x, y) != 17)
                    continue;
                ni.stations++;
                uint8_t bits = sim_cell(x, y)->b[9];
                if (bits & 0x30) {
                    ni.reached++;
                } else {
                    CHECK(cell_get(x, y) & 2, "%s: unreached station at plane %d (%d, %d) not tinted", when, pl, x,
                          y);
                    ni.tinted_unreached++;
                }
            }
    }
    CHECK(g_state.karma_busy == 0, "%s: karma_busy %#" PRIx32 " after a rebuild", when, g_state.karma_busy);
    return ni;
}

static int live_cars(void)
{
    int n = 0;
    for (int i = 1; i < GS_MAP_OBJECTS; i++)
        if (g_state.objects[i].plane == 2 && g_state.objects[i].set == 16)
            n++;
    return n;
}

static int g_off_rails;

static void check_cars(const MapObjectRec *before, int *moves, int *bad_moves)
{
    world_select_plane(GS_PLANE_MIDDLE);
    for (int i = 1; i < GS_MAP_OBJECTS; i++) {
        const MapObjectRec *o = &g_state.objects[i];
        if (o->plane != 2 || o->set != 16)
            continue;
        if (o->x >= 0 && o->y >= 0 && o->x < 100 && o->y < 100) {
            unsigned s = cell_origin_tileset(o->x, o->y);
            /* the dispatcher puts the cars one step behind the rail it
             * found, against the side of the station ring it had reached
             * by then: after a corner that can be plain land (so does
             * ALIFE.EXE, tools/emu/test_karma.py) */
            if (s != 16 && s != 17 && s != 18)
                g_off_rails++;
        }
        const MapObjectRec *b = &before[i];
        if (b->plane == 2 && b->set == 16 && (b->x != o->x || b->y != o->y)) {
            int d = abs(o->x - b->x) + abs(o->y - b->y);
            (*moves)++;
            if (d != 1)
                (*bad_moves)++;
        }
    }
}

/* Trains: karma_trains_tick every 500 ms, the objects every 125 ms. */
static void run_trains(int ticks)
{
    int spawned = 0, moves = 0, bad_moves = 0, max_cars = 0;
    uint16_t hist0[16];
    memcpy(hist0, g_state.karma_history, sizeof hist0);
    unsigned pending0 = g_state.karma_pending;
    for (int t = 0; t < ticks + 400; t++) {
        if (t < ticks) {
            int before = live_cars();
            world_select_plane(GS_PLANE_MIDDLE);
            karma_trains_tick();
            int after = live_cars();
            if (after > before)
                spawned += after - before;
        }
        for (int k = 0; k < 4; k++) {
            MapObjectRec prev[GS_MAP_OBJECTS];
            memcpy(prev, g_state.objects, sizeof prev);
            mapobj_frame(1);
            check_cars(prev, &moves, &bad_moves);
        }
        int n = live_cars();
        if (n > max_cars)
            max_cars = n;
        if (t >= ticks && n == 0 && g_state.karma_busy == 0)
            break;
    }
    check_objmap("after the trains");
    bool hist_changed = memcmp(hist0, g_state.karma_history, sizeof hist0) != 0;
    printf("  trains: %d cars spawned (max %d at once), %d moves, history %s, pending %u -> %u, busy %#" PRIx32
           ", %d redraws\n",
           spawned, max_cars, moves, hist_changed ? "changed" : "unchanged", pending0, g_state.karma_pending,
           g_state.karma_busy, g_redraws);
    if (g_off_rails)
        printf("  (%d car positions off the rails)\n", g_off_rails);
    g_off_rails = 0;
    CHECK(bad_moves == 0, "%d car moves longer than one cell", bad_moves);
    CHECK(live_cars() == 0, "%d train cars still running after the dispatching stopped", live_cars());
    CHECK(g_state.karma_busy == 0, "networks still busy: %#" PRIx32, g_state.karma_busy);
    if (spawned > 0)
        CHECK(hist_changed, "trains ran but no trip reached the history");
}

static void check_realm_cells(int pl, const char *when)
{
    world_select_plane(pl);
    PlaneStats *r = realm_cur();
    uint64_t pop = 0;
    for (int y = 0; y < 100; y++)
        for (int x = 0; x < 100; x++) {
            unsigned set = cell_tileset(x, y);
            unsigned os = set == 0 ? cell_origin_tileset(x, y) : set;
            if (os == 14 || os == 15)
                pop += sim_pop(x, y);
        }
    CHECK(pop == r->population, "%s, plane %d: population %" PRIu32 ", cells hold %" PRIu64, when, pl,
          r->population, pop);
}

/* SOULs counted at the stations last year (sim +5) */
static uint64_t check_station_souls(const char *when)
{
    uint64_t total = 0;
    for (int pl = 1; pl <= 3; pl += 2) {
        world_select_plane(pl);
        for (int y = 0; y < 100; y++)
            for (int x = 0; x < 100; x++) {
                if (cell_tileset(x, y) != 17)
                    continue;
                SimCell *s = sim_cell(x, y);
                uint32_t n = sim_cell_u32(s, 5);
                unsigned idx = s->b[10];
                uint32_t cap = idx < 6 ? g_tables.set17[idx].capacity : 0;
                CHECK(n <= cap, "%s: station plane %d (%d, %d) took %" PRIu32 " SOULs, capacity %" PRIu32, when, pl,
                      x, y, n, cap);
                if (s->b[9] == 0)
                    CHECK(n == 0, "%s: station plane %d (%d, %d) has no rail but took %" PRIu32 " SOULs", when, pl, x,
                          y, n);
                total += n;
            }
    }
    return total;
}

static uint8_t *run_years(int years, size_t *len)
{
    uint32_t year0 = g_state.year;
    int calls = 0;
    uint64_t at_stations = 0;
    souls_stats = (SoulsStats){0};
    while (g_state.year < year0 + (uint32_t)years && calls < 200000) {
        int phase = g_state.sim_phase;
        sim_update(1);
        mapobj_frame(1);
        calls++;
        if (phase == 50 && g_state.sim_phase != 50)
            at_stations += check_station_souls("after the SOUL flow");
    }
    CHECK(g_state.year >= year0 + (uint32_t)years, "sim_update stuck in phase %d", g_state.sim_phase);
    check_realm_cells(GS_PLANE_HELL, "after the years");
    check_realm_cells(GS_PLANE_HEAVEN, "after the years");
    check_objmap("after the years");
    printf("  %d years: %d sim_update calls, %" PRIu64 " SOULs boarded at stations (%" PRIu64
           " counted), %d objects alive, boats %u, Karma average %u\n",
           years, calls, souls_stats.walk_stations, at_stations, GS_MAP_OBJECTS - 1 - mapobj_free_count(),
           g_state.boat_count, g_state.unk_5c6678);
    return savegame_save_mem(&g_state, len);
}

static void test_save(const char *path, int years, int ticks)
{
    printf("%s\n", path);
    if (!load(path))
        return;
    if (g_have_tiles)
        check_footprints();
    check_objmap("after loading");

    /* the network */
    world_select_plane(GS_PLANE_MIDDLE);
    karma_clear();
    karma_rebuild();
    NetInfo a = check_network("rebuild");
    printf("  network: %d rail cells in %d networks, %d stations (%d reached, %d tinted), %u stations counted\n",
           a.rails, a.networks, a.stations, a.reached, a.tinted_unreached, g_state.karma_stations);
    {
        static uint32_t cells[4][GS_MAP_CELLS];
        static SimCell sims[4][GS_MAP_CELLS];
        for (int pl = 1; pl <= 3; pl++) {
            memcpy(cells[pl], g_state.plane[pl].cells, sizeof cells[pl]);
            memcpy(sims[pl], g_state.plane[pl].sim, sizeof sims[pl]);
        }
        world_select_plane(GS_PLANE_MIDDLE);
        karma_rebuild();
        bool same = true;
        for (int pl = 1; pl <= 3; pl++)
            same = same && !memcmp(cells[pl], g_state.plane[pl].cells, sizeof cells[pl]) &&
                   !memcmp(sims[pl], g_state.plane[pl].sim, sizeof sims[pl]);
        CHECK(same, "a second rebuild changed the map");
    }
    /* rail removal leaves the network consistent */
    {
        int rails0 = a.rails, removed = 0;
        world_select_plane(GS_PLANE_MIDDLE);
        for (int y = 1; y < 99 && !removed; y++)
            for (int x = 1; x < 99 && !removed; x++)
                if (cell_tileset(x, y) == 16) {
                    int n = karma_rail_remove(x, y, false);
                    CHECK(n >= 1, "karma_rail_remove on a rail returned %d", n);
                    removed = 1;
                }
        world_select_plane(GS_PLANE_MIDDLE);
        CHECK(karma_rail_remove(0, 0, false) == 0 || cell_tileset(0, 0) == 16, "karma_rail_remove off the rails");
        (void)rails0;
    }

    /* trains */
    run_trains(ticks);

    /* years, twice from the same state: deterministic */
    if (years > 0) {
        if (!load(path))
            return;
        size_t l1 = 0, l2 = 0;
        uint8_t *s1 = run_years(years, &l1);
        if (!load(path)) {
            free(s1);
            return;
        }
        uint8_t *s2 = run_years(years, &l2);
        CHECK(s1 && s2 && l1 == l2 && memcmp(s1, s2, l1) == 0, "two runs from the same state differ");
        if (s1) {
            static GameState gs;
            char err[160];
            if (savegame_load_mem(&gs, s1, l1, err, sizeof err)) {
                size_t l3 = 0;
                uint8_t *s3 = savegame_save_mem(&gs, &l3);
                CHECK(s3 && l3 == l1 && memcmp(s1, s3, l1) == 0, "the simulated state doesn't round-trip");
                free(s3);
                savegame_free(&gs);
            } else {
                CHECK(false, "the simulated state doesn't load: %s", err);
            }
        }
        free(s1);
        free(s2);
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <data dir> [-y years] [-t ticks] save.ASC|.ALF ...\n", argv[0]);
        return 2;
    }
    plat_set_data_dir(argv[1]);
    if (!planet_tables_load()) {
        fprintf(stderr, "cannot unpack the planet tables\n");
        return 2;
    }
    load_tiles();
    if (!g_have_tiles)
        printf("(no MEDIUM.000 in %s: footprint check skipped)\n", argv[1]);
    int years = 3, ticks = 600;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-y") == 0 && i + 1 < argc) {
            years = atoi(argv[++i]);
            continue;
        }
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            ticks = atoi(argv[++i]);
            continue;
        }
        test_save(argv[i], years, ticks);
    }
    world_free();
    for (int i = 0; i < 32; i++)
        free(g_set_data[i]);
    if (g_have_tiles)
        rc_shutdown();
    printf(g_failures ? "%d failures\n" : "ok\n", g_failures);
    return g_failures != 0;
}
