/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * badthings_test <data dir> <out dir | -> save.ASC ...
 *
 * The Bad Things (src/game/badthings.c, badthing_objects.c) on each save:
 * - every kind forced with bad_thing_start (with the "No Bad Things"
 *   setting on, nothing starts), stepped through the map object block of
 *   game_frame_update and sim_update's real-time timers (speed 0, a fake
 *   clock 500 ms per four frames) until it is over: its bit in
 *   g_state.unk_5b2f61 clears and its objects leave within the frame
 *   budget; the object map stays consistent (CELL_OBJECT exactly where a
 *   count is), and the realm bookkeeping is reported (structure_destroy
 *   with the Bad Things' flags leaves a realm's population a little higher
 *   than its cells' SOULs, in the original too: test_badthings.py finds
 *   the state identical);
 * - the same run twice from the same seed ends in the same state, and a
 *   state saved mid-disaster round-trips through the ALSV writer;
 * - the bankruptcy ending (walker and fires) for 80 steps, the SAMNMAX
 *   cheat and the build cheat's giant, with the same checks;
 * - with an out dir: PNGs of disasters in progress (1:1 around the first
 *   monster, the frozen / blue realm and the bankrupt realm at zoom 1).
 * The data dir must hold MEDIUM.000 (the game's tables and strings are
 * built in).
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "formats/tile.h"
#include "game/badthings.h"
#include "game/build.h"
#include "game/karma.h"
#include "game/mapobj.h"
#include "game/planet.h"
#include "game/resource.h"
#include "game/savegame.h"
#include "game/sim.h"
#include "game/souls.h"
#include "game/tables.h"
#include "game/util.h"
#include "game/world.h"
#include "gfx/map_objects.h"
#include "gfx/map_view.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "gfx/tile_draw.h"
#include "platform/platform.h"

static int g_failures;
static const char *g_out;

#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("  FAIL: " __VA_ARGS__);                                                                            \
            printf("\n");                                                                                              \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

static const char *const k_names[BAD_THINGS] = {"birds", "bats", "blues", "freeze", "nose", "handbasket", "dice",
                                                "disco"};
static const int k_sets[BAD_THINGS] = {0x14, 0x15, 0, 0, 0x19, 0x18, 0x1a, 0x1b};

/* ---- PNG output (indexed, stored deflate) ------------------------------------------ */

static uint32_t crc_table[256];
static uint8_t g_png_pal[768];

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
static uint32_t crc_update(uint32_t c, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c;
}
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}
static void chunk(FILE *f, const char *type, const uint8_t *data, size_t len)
{
    uint8_t hdr[8];
    put32(hdr, (uint32_t)len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len)
        fwrite(data, 1, len, f);
    uint32_t c = crc_update(0xffffffffu, hdr + 4, 4);
    c = crc_update(c, data, len) ^ 0xffffffffu;
    put32(hdr, c);
    fwrite(hdr, 1, 4, f);
}
static void save_png(const GfxSurface *s, const char *name)
{
    if (!g_out)
        return;
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", g_out, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "can't write %s\n", path);
        return;
    }
    int w = s->width, h = s->height;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8, ihdr[9] = 3, ihdr[10] = 0, ihdr[11] = 0, ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "PLTE", g_png_pal, 768);
    size_t raw_len = (size_t)(w + 1) * (size_t)h;
    size_t blocks = (raw_len + 65534) / 65535;
    uint8_t *raw = malloc(raw_len), *z = malloc(2 + raw_len + blocks * 5 + 4);
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * (size_t)(w + 1)] = 0;
        memcpy(raw + (size_t)y * (size_t)(w + 1) + 1, s->pixels + (size_t)y * (size_t)s->stride, (size_t)w);
    }
    uint8_t *q = z;
    *q++ = 0x78, *q++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw_len;) {
        size_t n = raw_len - off < 65535 ? raw_len - off : 65535;
        *q++ = off + n == raw_len;
        *q++ = (uint8_t)n, *q++ = (uint8_t)(n >> 8);
        *q++ = (uint8_t)~n, *q++ = (uint8_t)(~n >> 8);
        memcpy(q, raw + off, n);
        for (size_t i = 0; i < n; i++) {
            a = (a + raw[off + i]) % 65521;
            b = (b + a) % 65521;
        }
        q += n;
        off += n;
    }
    put32(q, b << 16 | a);
    q += 4;
    chunk(f, "IDAT", z, (size_t)(q - z));
    chunk(f, "IEND", NULL, 0);
    free(raw);
    free(z);
    fclose(f);
}

/* ---- Rendering --------------------------------------------------------------------- */

static TileSet g_sets[32];
static uint8_t *g_set_data[32];
static bool g_gfx;
static MapObject g_mobj[MAP_OBJECTS];
static MapPlanes g_planes;

static uint8_t *load_resource(uint32_t id, size_t *len)
{
    long size = rc_resource_size(id);
    PlatFile *f = rc_open_resource(id);
    if (!f || size <= 0) {
        if (f)
            plat_file_close(f);
        return NULL;
    }
    uint8_t *buf = malloc((size_t)size);
    if (buf && plat_file_read(f, buf, 1, (size_t)size) != (size_t)size) {
        free(buf);
        buf = NULL;
    }
    plat_file_close(f);
    *len = (size_t)size;
    return buf;
}

static void gfx_setup(void)
{
    size_t plen = 0;
    uint8_t *praw = load_resource(33, &plen);
    gfx_palette_init();
    uint8_t *rgbx = praw ? gfx_palette_load_resource(praw, plen) : NULL;
    if (!rgbx) {
        free(praw);
        return;
    }
    for (int i = 0; i < 256; i++)
        g_png_pal[i * 3] = rgbx[i * 4], g_png_pal[i * 3 + 1] = rgbx[i * 4 + 1], g_png_pal[i * 3 + 2] = rgbx[i * 4 + 2];
    tile_draw_init(1);
    tile_draw_build_tables(rgbx);
    for (int id = 1; id <= 31; id++) {
        size_t len = 0;
        g_set_data[id] = load_resource((uint32_t)id, &len);
        if (g_set_data[id] && tile_set_open(&g_sets[id], g_set_data[id], len) == TILE_OK)
            tile_draw_bind_set(id - 1, &g_sets[id]);
    }
    free(praw);
    free(rgbx);
    g_gfx = true;
}

static void view_init(MapView *v, GfxSurface *s, int zoom)
{
    memset(v, 0, sizeof *v);
    v->width = v->height = 100;
    v->cell_w = 32;
    v->cell_h = 16;
    v->zoom = zoom;
    v->dir_x = g_state.view_dir_x ? g_state.view_dir_x : 1;
    v->dir_y = g_state.view_dir_y ? g_state.view_dir_y : 1;
    v->focus_x = v->focus_y = -1;
    v->set_mask = 0xffffffffu;
    v->plane = 0;
    v->flags = MAPVIEW_F_ZONE_DETAIL | MAPVIEW_F_GRID;
    v->clip_w = s->width;
    v->clip_h = s->height;
    v->objects = g_mobj;
}

static void sync_view(void)
{
    for (int i = 0; i < MAP_OBJECTS; i++) {
        const MapObjectRec *o = &g_state.objects[i];
        g_mobj[i] = (MapObject){o->plane, o->x, o->y, o->dx, o->dy, o->size, o->set, o->frame, o->fade};
    }
    memset(&g_planes, 0, sizeof g_planes);
    map_planes_init(&g_planes);
    for (int pl = 1; pl <= 3; pl++) {
        g_planes.plane[pl].cells = g_state.plane[pl].cells;
        g_planes.plane[pl].objmap = g_state.plane[pl].objmap;
        g_planes.plane[pl].flags = g_build.view_flags[pl] | MAPVIEW_F_GRID;
    }
}

/* the realm `plane` at zoom 1 */
static void render_plane(int plane, const char *name)
{
    if (!g_gfx || !g_out)
        return;
    sync_view();
    int zoom = 1, top = 0x48 * 3 >> zoom;
    int sw = 1600, sh = 800 + top;
    GfxSurface *s = gfx_surface_create(sw, sh);
    memset(s->pixels, 0x13, (size_t)s->stride * (size_t)s->height);
    MapView v;
    view_init(&v, s, zoom);
    g_planes.scroll_y = top - (g_planes.plane[plane].y_offset >> zoom);
    map_view_select_plane(&v, &g_planes, plane);
    GfxRect b = {0, sw - 1, 0, sh - 1};
    map_view_draw(s, &v, &b);
    save_png(s, name);
    gfx_surface_destroy(s);
}

/* 1:1 around the first live object of tile set `set` */
static void render_object(int set, const char *name)
{
    if (!g_gfx || !g_out)
        return;
    for (int i = 1; i < GS_MAP_OBJECTS; i++) {
        const MapObjectRec *o = &g_state.objects[i];
        if (o->set != set || o->plane < 1 || o->plane > 3 || o->x < 0 || o->x > 99 || o->y < 0 || o->y > 99)
            continue;
        sync_view();
        GfxSurface *c = gfx_surface_create(800, 500);
        memset(c->pixels, 0x13, (size_t)c->stride * (size_t)c->height);
        MapView v;
        view_init(&v, c, 0);
        int sx = (o->x - o->y + 99) * 16, sy = (o->x + o->y) * 8;
        g_planes.scroll_y = -g_planes.plane[o->plane].y_offset - sy + 250;
        map_view_select_plane(&v, &g_planes, o->plane);
        v.origin_x = -(sx - 400);
        GfxRect b = {0, 799, 0, 499};
        map_view_draw(c, &v, &b);
        save_png(c, name);
        gfx_surface_destroy(c);
        printf("    %s: object %d (set 0x%x) on plane %d at (%d, %d), frame %d\n", name, i, set, o->plane, o->x, o->y,
               o->frame);
        return;
    }
}

/* ---- Setup and invariants ---------------------------------------------------------- */

static uint32_t g_clock;
static uint32_t fake_clock(void) { return g_clock; }

static bool load(const char *path, uint32_t seed)
{
    char err[160];
    if (!world_load(path, err, sizeof err)) {
        printf("  can't load %s: %s\n", path, err);
        g_failures++;
        return false;
    }
    tables_init_defaults();
    souls_reset();
    sim_reset();
    karma_reset();
    mapobj_reset();
    badthings_reset();
    memset(&g_sim_hooks, 0, sizeof g_sim_hooks);
    memset(&g_mapobj_hooks, 0, sizeof g_mapobj_hooks);
    memset(&g_badthings_hooks, 0, sizeof g_badthings_hooks);
    g_sim_hooks.clock = fake_clock;
    g_clock = 1000000;
    rng_seed(seed);
    g_state.bad_things = 1;
    return true;
}

static void check_objmap(const char *what)
{
    int bad = 0;
    for (int pl = 1; pl <= 3; pl++)
        for (int i = 0; i < GS_MAP_CELLS; i++) {
            bool flag = (g_state.plane[pl].cells[i] & 0x40) != 0;
            bool count = (g_state.plane[pl].objmap[i] & 3) != 0;
            bad += flag != count;
        }
    CHECK(bad == 0, "%s: %d cells with CELL_OBJECT and the object map count disagreeing", what, bad);
    /* (object_count is no invariant: the dust clouds of destroyed
     * buildings count up without limit in the original too) */
}

static void check_realms(const char *what, bool strict)
{
    int keep = g_world.plane;
    for (int pl = 1; pl <= 3; pl += 2) {
        world_select_plane(pl);
        uint64_t pop = 0;
        unsigned zones = 0;
        for (int y = 0; y < 100; y++)
            for (int x = 0; x < 100; x++) {
                unsigned set = cell_tileset(x, y);
                if (set == 14 || set == 15)
                    zones++;
                if (set == 14 || set == 15 || (set == 0 && (cell_origin_tileset(x, y) == 14 ||
                                                             cell_origin_tileset(x, y) == 15)))
                    pop += sim_pop(x, y);
            }
        PlaneStats *r = realm_cur();
        if (!strict && pop > r->population)
            CHECK(0, "%s: plane %d population %" PRIu32 " below its cells' SOULs %" PRIu64, what, pl, r->population,
                  pop);
        else if (!strict && pop != r->population)
            printf("    (%s: plane %d population %" PRIu32 ", zone SOULs %" PRIu64 ")\n", what, pl, r->population, pop);
        else
            CHECK(pop == r->population, "%s: plane %d population %" PRIu32 " != zone SOULs %" PRIu64, what, pl,
              r->population, pop);
        (void)zones; /* n_zones is recounted by the zone scan, not kept up */
    }
    world_select_plane(keep);
}

static int live(int set)
{
    int n = 0;
    for (int i = 1; i < GS_MAP_OBJECTS; i++)
        n += g_state.objects[i].plane >= 1 && g_state.objects[i].plane <= 3 && g_state.objects[i].set == set;
    return n;
}

/* the frame block plus, every 4 frames, sim_update's timers */
static void step(int t)
{
    mapobj_frame(1);
    if (t % 4 == 3) {
        g_clock += 500;
        sim_update(0);
        for (int pl = 1; pl <= 3; pl += 2) {
            PlaneStats *r = world_realm(pl);
            if (r->unk_80fa == 0 && r->unk_80f9 != 0)
                r->unk_80fb = 0; /* recede now, not years later */
        }
    }
}

/* Runs Bad Thing k; returns the frames until it was over (-1: never). */
static int run_kind(const char *path, int k, uint32_t seed, bool pictures, uint8_t **final_state, size_t *len)
{
    if (!load(path, seed))
        return -1;
    bad_thing_start(k);
    uint8_t bit = (uint8_t)(1u << k);
    if (!(g_state.unk_5b2f61 & bit) && !(k == BAD_DISCO && live(0x1b)))
        return -2;
    int pic = k == BAD_BLUES || k == BAD_FREEZE ? 120 : k == BAD_HANDBASKET || k == BAD_NOSE ? 60 : 24;
    int over = -1;
    for (int t = 0; t < 4000; t++) {
        step(t);
        if (t % 50 == 49) {
            char what[64];
            snprintf(what, sizeof what, "%s frame %d", k_names[k], t);
            check_objmap(what);
        }
        if (pictures && t == pic) {
            char name[96];
            const char *base = strrchr(path, '/');
            base = base ? base + 1 : path;
            if (k_sets[k]) {
                snprintf(name, sizeof name, "%.8s_%s.png", base, k_names[k]);
                render_object(k_sets[k], name);
            } else {
                snprintf(name, sizeof name, "%.8s_%s_z1.png", base, k_names[k]);
                render_plane(k == BAD_FREEZE ? 1 : 3, name);
            }
        }
        if (!(g_state.unk_5b2f61 & bit) && (!k_sets[k] || live(k_sets[k]) == 0)) {
            over = t;
            break;
        }
    }
    char what[64];
    snprintf(what, sizeof what, "%s over", k_names[k]);
    check_objmap(what);
    check_realms(what, false);
    if (final_state)
        *final_state = savegame_save_mem(&g_state, len);
    return over;
}

static void test_save(const char *path)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    printf("%s\n", base);

    /* disabled: nothing starts */
    if (load(path, 1)) {
        g_state.bad_things = 0;
        for (int k = 0; k < BAD_THINGS; k++)
            bad_thing_start(k);
        CHECK(g_state.unk_5b2f61 == 0, "a Bad Thing started while disabled");
    }

    for (int k = 0; k < BAD_THINGS; k++) {
        uint32_t seed = 4242u + (uint32_t)k;
        uint8_t *a = NULL, *b = NULL;
        size_t alen = 0, blen = 0;
        int n = run_kind(path, k, seed, true, &a, &alen);
        if (n == -2) {
            free(a);
            printf("  %-10s refused\n", k_names[k]);
            continue;
        }
        printf("  %-10s over after %d frames\n", k_names[k], n);
        CHECK(n >= 0, "%s: still going after 4000 frames", k_names[k]);
        int m = run_kind(path, k, seed, false, &b, &blen);
        CHECK(m == n && a && b && alen == blen && memcmp(a, b, alen) == 0, "%s: not deterministic (%d vs %d frames)",
              k_names[k], n, m);
        free(a);
        free(b);
    }

    /* a disaster in progress saved and loaded back */
    if (load(path, 99)) {
        bad_thing_start(BAD_HANDBASKET);
        bad_thing_start(BAD_BATS);
        for (int t = 0; t < 80; t++)
            step(t);
        size_t len = 0;
        uint8_t *data = savegame_save_mem(&g_state, &len);
        GameState *gs = calloc(1, sizeof *gs);
        char err[160] = "";
        bool ok = data && savegame_load_mem(gs, data, len, err, sizeof err);
        CHECK(ok, "save/load mid-disaster: %s", err);
        if (ok) {
            CHECK(memcmp(gs->objects, g_state.objects, sizeof gs->objects) == 0, "map objects differ after load");
            CHECK(gs->unk_5b2f61 == g_state.unk_5b2f61 &&
                      memcmp(gs->unk_5bb0b0, g_state.unk_5bb0b0, sizeof gs->unk_5bb0b0) == 0 &&
                      gs->unk_57e098 == g_state.unk_57e098 && gs->unk_57e09a == g_state.unk_57e09a &&
                      gs->unk_5bb110 == g_state.unk_5bb110,
                  "Bad Things state differs after load");
        }
        savegame_free(gs);
        free(gs);
        free(data);
    }

    /* the bankruptcy */
    if (load(path, 7)) {
        badthings_bankruptcy();
        CHECK(g_state.unk_5f4cd0[0][0] == 3, "bankruptcy walker not on Heaven");
        int burning = 0;
        for (int i = 0; i < 80; i++) {
            badthings_doom_step();
            badthings_fire_step();
            if (i == 30) {
                char name[96];
                snprintf(name, sizeof name, "%.8s_bankrupt_z1.png", base);
                render_plane(g_state.unk_5f4cd0[0][0], name);
            }
        }
        for (int pl = 1; pl <= 3; pl += 2) {
            world_select_plane(pl);
            for (int y = 0; y < 100; y++)
                for (int x = 0; x < 100; x++)
                    burning += cell_tileset(x, y) == 2 && sim_cell(x, y)->b[1] > 0xf4;
        }
        printf("  bankrupt   80 steps, %d cells burning\n", burning);
        check_objmap("bankruptcy");
        check_realms("bankruptcy", false);
    }

    /* the cheats */
    if (load(path, 8)) {
        badthings_cheat();
        int n = live(6);
        CHECK(n > 0, "SAMNMAX: no walkers");
        for (int t = 0; t < 400; t++) {
            step(t);
            if (t == 60) {
                char name[96];
                snprintf(name, sizeof name, "%.8s_samnmax.png", base);
                render_object(6, name);
            }
        }
        check_objmap("SAMNMAX");
        check_realms("SAMNMAX", false);
        mapobj_spawn(40, 3, 3, 2, 0, 1, 4, 4, 1, 10, 0xffffffffu, 0x5a, 1);
        for (int t = 0; t < 600; t++) {
            step(t);
            if (t == 120) {
                char name[96];
                snprintf(name, sizeof name, "%.8s_giant.png", base);
                render_object(3, name);
            }
        }
        printf("  cheats     %d SAMNMAX walkers, giant %s\n", n, live(3) ? "still walking" : "gone");
        check_objmap("giant");
        check_realms("giant", false);
    }
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <data dir> <out dir | -> save.ASC ...\n", argv[0]);
        return 2;
    }
    plat_set_data_dir(argv[1]);
    g_out = strcmp(argv[2], "-") == 0 ? NULL : argv[2];
    crc_init();
    if (!rc_initialize("MEDIUM.000", "MEDIUM.000")) {
        fprintf(stderr, "no MEDIUM.000 in %s\n", argv[1]);
        return 1;
    }
    if (!planet_tables_load()) {
        fprintf(stderr, "planet_tables_load failed\n");
        return 1;
    }
    gfx_setup();
    for (int i = 3; i < argc; i++)
        test_save(argv[i]);
    world_free();
    for (int i = 0; i < 32; i++)
        free(g_set_data[i]);
    printf(g_failures ? "%d failures\n" : "all ok\n", g_failures);
    return g_failures ? 1 : 0;
}
