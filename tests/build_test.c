/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * build_test [-d <data dir> <bundle> <out dir>] [save.ASC|.ALF ...]
 *
 * The data dir (default orig/) must hold ALIFE.EXE (planet tables).
 *
 * Checks the build tools, structures, rotation and world generation (WP2:
 * src/game/build.c, structure.c, rotate.c, newworld.c):
 * - for each save: places zones, a port, buildings and roads on Hell and
 *   Heaven, destroys some, and checks the map invariants world_test uses
 *   (ports list = set-13 cells, footprints and both origin encodings,
 *   population = zone SOULs; n_zones by the delta, the original only
 *   recounts it in the realm year);
 * - rotates the view 4x each way and expects the identical grids, and
 *   checks after every quarter turn that each building's strips are the
 *   ones map_stamp_building writes for the new rotation;
 * - generates new worlds (each difficulty), checks their invariants and
 *   that the same RNG state gives the same world;
 * - with -d: renders a new world's three planes to <out dir>/newworld_*.png
 *   through the map renderer (like tests/map_render).
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the renderer's cell helpers share names with world.h's accessors */
#define cell_index tile_cell_index
#define cell_strip tile_cell_strip
#include "formats/tile.h"
#include "gfx/map_view.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "gfx/tile_draw.h"
#undef cell_index
#undef cell_strip

#include "game/build.h"
#include "game/newworld.h"
#include "game/planet.h"
#include "game/resource.h"
#include "game/rotate.h"
#include "game/structure.h"
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

/* ---- invariants ------------------------------------------------------------------- */

typedef struct Counts {
    int sets[32];
    int zone_cells;
    uint64_t pop;
} Counts;

/* every building's footprint points back at its origin through the cell
 * offsets and (Hell/Heaven) the sim cell; returns the number of problems */
static int check_footprints(int pl, const char *what)
{
    int bad = 0;
    world_select_plane(pl);
    for (int y = 0; y < GS_MAP_H; y++)
        for (int x = 0; x < GS_MAP_W; x++) {
            unsigned set = cell_tileset(x, y);
            if (set == 0) {
                int ox, oy;
                cell_origin(x, y, &ox, &oy);
                unsigned oset = cell_origin_tileset(x, y);
                if (oset == 0) {
                    if (bad++ < 5)
                        printf("  %s plane %d (%d,%d): part cell without an origin\n", what, pl, x, y);
                    continue;
                }
                int n = structure_size(ox, oy);
                if (pl != GS_PLANE_MIDDLE && oset != 18) {
                    const SimCell *sc = sim_cell(x, y);
                    if (sc->b[10] != ox || sc->b[11] != oy) {
                        if (bad++ < 5)
                            printf("  %s plane %d (%d,%d): sim origin %d,%d != %d,%d (set %u)\n", what, pl, x, y,
                                   sc->b[10], sc->b[11], ox, oy, oset);
                    }
                }
                if (pl != GS_PLANE_MIDDLE && (x - ox >= n || oy - y >= n)) {
                    if (bad++ < 5)
                        printf("  %s plane %d (%d,%d): outside the %dx%d footprint of (%d,%d)\n", what, pl, x, y, n,
                               n, ox, oy);
                }
            } else if (set == 14 || set == 15) {
                int n = tile_index_footprint(cell_index(x, y));
                for (int dx = 0; dx < n; dx++)
                    for (int dy = 0; dy < n; dy++) {
                        int px = x + dx, py = y - dy, ox, oy;
                        if (px >= GS_MAP_W || py < 0) {
                            bad++;
                            continue;
                        }
                        cell_origin(px, py, &ox, &oy);
                        if (ox != x || oy != y) {
                            if (bad++ < 5)
                                printf("  %s plane %d (%d,%d) part (%d,%d) -> (%d,%d)\n", what, pl, x, y, px, py, ox,
                                       oy);
                        }
                    }
            }
        }
    return bad;
}

static Counts count(int pl)
{
    Counts c;
    memset(&c, 0, sizeof c);
    world_select_plane(pl);
    for (int y = 0; y < GS_MAP_H; y++)
        for (int x = 0; x < GS_MAP_W; x++) {
            unsigned set = cell_tileset(x, y);
            c.sets[set]++;
            if (set == 14 || set == 15)
                c.zone_cells++;
            unsigned oset = cell_origin_tileset(x, y);
            if (oset == 14 || oset == 15)
                c.pop += sim_pop(x, y);
        }
    return c;
}

static void check_realm(int pl, const char *what)
{
    world_select_plane(pl);
    PlaneStats *r = realm_cur();
    Counts c = count(pl);
    CHECK(c.sets[13] == r->n_ports, "%s plane %d: n_ports %u != set-13 cells %d", what, pl, r->n_ports, c.sets[13]);
    for (int i = 0; i < r->n_ports && i < 50; i++) {
        int x = r->ports[i][0], y = r->ports[i][1];
        CHECK(x < GS_MAP_W && y < GS_MAP_H && cell_tileset(x, y) == 13, "%s plane %d: port %d (%d,%d) not set 13",
              what, pl, i, x, y);
    }
    CHECK(c.pop == r->population, "%s plane %d: population %" PRIu32 " != zone SOULs %" PRIu64, what, pl,
          r->population, c.pop);
    int bad = check_footprints(pl, what);
    CHECK(bad == 0, "%s plane %d: %d footprint problems", what, pl, bad);
}

/* a free n x n area (origin at the front-left corner, y = bottom row) */
static bool find_free(int n, int *ox, int *oy, int skip)
{
    for (int y = 10; y < 90; y++)
        for (int x = 10; x < 90; x++)
            if (build_area_free(x, y, n) && cell_tileset(x, y) == 1 && skip-- <= 0) {
                bool plain = true;
                for (int cx = x; cx < x + n; cx++)
                    for (int cy = y; cy > y - n; cy--)
                        if (cell_tileset(cx, cy) != 1)
                            plain = false;
                if (plain) {
                    *ox = x, *oy = y;
                    return true;
                }
            }
    return false;
}

static int g_sounds;
static void count_sound(uint32_t id, int ch)
{
    (void)id, (void)ch;
    g_sounds++;
}

/* ---- placing and destroying on a save ---------------------------------------- */

static void build_on_save(int pl, const char *name)
{
    world_select_plane(pl);
    PlaneStats *r = realm_cur();
    Counts before = count(pl);
    unsigned zset = pl == GS_PLANE_HELL ? 14 : 15;
    g_state.pennies = bignum_from_i64(2000000000);

    /* zones: tool 3 over a free 4x4 */
    int x, y;
    if (!find_free(4, &x, &y, 0)) {
        CHECK(0, "%s: no free 4x4 on plane %d", name, pl);
        return;
    }
    g_build.tool_size = 1;
    g_build.tool_cost = 2500;
    int64_t p0 = bignum_to_i64(g_state.pennies);
    bool ok = build_apply_tool(3, x, y - 3, x + 3, y);
    CHECK(ok, "%s plane %d: zoning failed", name, pl);
    int zoned = 0;
    for (int cx = x; cx < x + 4; cx++)
        for (int cy = y - 3; cy <= y; cy++)
            if (cell_tileset(cx, cy) == zset && cell_index(cx, cy) == 2 * 13 && sim_zone_kind(cx, cy) == 2)
                zoned++;
    CHECK(zoned == 16, "%s plane %d: %d of 16 cells zoned", name, pl, zoned);
    CHECK(p0 - bignum_to_i64(g_state.pennies) == 16 * 2500, "%s plane %d: zoning cost %" PRId64, name, pl,
          p0 - bignum_to_i64(g_state.pennies));

    /* a port (Heaven entry 0, Hell entry 2) */
    int px, py;
    unsigned n_ports = r->n_ports;
    bool have_port = find_free(3, &px, &py, 0) && n_ports < 50;
    if (have_port) {
        ok = build_place(13, pl == GS_PLANE_HEAVEN ? 0 : 2, 3, px, py, BUILD_RAW);
        CHECK(ok, "%s plane %d: port not placed", name, pl);
        CHECK(r->n_ports == n_ports + 1 && r->ports[n_ports][0] == px && r->ports[n_ports][1] == py,
              "%s plane %d: ports list not updated", name, pl);
    }
    /* a set-8 building (3x3), clicked with the rotation's front corner */
    int bx, by;
    bool have_b = find_free(3, &bx, &by, 0);
    if (have_b) {
        ok = build_place(8, 0, 3, bx, by, BUILD_RAW);
        CHECK(ok && cell_tileset(bx, by) == 8 && structure_size(bx + 1, by - 2) == 3, "%s plane %d: set 8 not placed",
              name, pl);
    }
    /* a road */
    int rx, ry;
    if (find_free(1, &rx, &ry, 0)) {
        int len = 0;
        while (len < 8 && rx + len < 99 && build_area_free(rx + len, ry, 1) && cell_tileset(rx + len, ry) == 1)
            len++;
        if (len > 1) {
            int32_t c = build_road_line(rx, ry, rx + len - 1, ry, false);
            int64_t q0 = bignum_to_i64(g_state.pennies);
            build_road_line(rx, ry, rx + len - 1, ry, true);
            int roads = 0;
            for (int i = 0; i < len; i++)
                roads += cell_tileset(rx + i, ry) == 12;
            CHECK(roads == len, "%s plane %d: %d of %d road cells", name, pl, roads, len);
            CHECK(c == len * 100 && q0 - bignum_to_i64(g_state.pennies) == len * 100,
                  "%s plane %d: road price %d paid %" PRId64, name, pl, c, q0 - bignum_to_i64(g_state.pennies));
        }
    }
    check_realm(pl, name);
    Counts mid = count(pl);
    CHECK(mid.zone_cells == before.zone_cells + 16, "%s plane %d: zone cells %d -> %d", name, pl, before.zone_cells,
          mid.zone_cells);

    /* destroy: the port, the building, four zone cells */
    if (have_port) {
        ok = structure_destroy(px + 1, py - 1, 1, 4);
        CHECK(ok && r->n_ports == n_ports, "%s plane %d: port not destroyed (%u ports)", name, pl, r->n_ports);
    }
    if (have_b) {
        ok = structure_destroy(bx + 2, by, 1, 4);
        CHECK(ok && cell_tileset(bx, by) == 1 && cell_tileset(bx + 2, by - 2) == 1, "%s plane %d: set 8 not destroyed",
              name, pl);
    }
    for (int i = 0; i < 4; i++)
        structure_destroy(x + i, y, 0, 4);
    check_realm(pl, name);
    Counts after = count(pl);
    CHECK(after.zone_cells == before.zone_cells + 12, "%s plane %d: zone cells %d -> %d after destroying", name, pl,
          before.zone_cells, after.zone_cells);
}

/* ---- rotation ----------------------------------------------------------------------- */

static uint32_t g_grid[4][GS_MAP_CELLS];

static void snapshot(void)
{
    for (int pl = 1; pl <= 3; pl++)
        memcpy(g_grid[pl], g_state.plane[pl].cells, sizeof g_grid[pl]);
}

static int diff_grids(void)
{
    int d = 0;
    for (int pl = 1; pl <= 3; pl++)
        for (int i = 0; i < GS_MAP_CELLS; i++)
            d += g_grid[pl][i] != g_state.plane[pl].cells[i];
    return d;
}

/* the strips each building must have for the current rotation (what
 * map_stamp_building writes), by footprint from the origin cells */
static int check_strips(void)
{
    int bad = 0, dx = g_state.view_dir_x, dy = g_state.view_dir_y, plane = g_world.plane;
    for (int pl = 1; pl <= 3; pl++) {
        world_select_plane(pl);
        for (int y = 0; y < GS_MAP_H; y++)
            for (int x = 0; x < GS_MAP_W; x++) {
                unsigned set = cell_tileset(x, y);
                if (set == 0 || set == 2 || set == 12 || set == 16)
                    continue;
                /* footprint from the part cells pointing here */
                int n = 1;
                while (x + n < GS_MAP_W && cell_tileset(x + n, y) == 0) {
                    int ox, oy;
                    cell_origin(x + n, y, &ox, &oy);
                    if (ox != x || oy != y)
                        break;
                    n++;
                }
                if (n < 2)
                    continue;
                int lx = dx < 1 ? x + n - 1 : x, ly = dy < 1 ? y - n + 1 : y;
                for (int cx = x; cx < x + n; cx++)
                    for (int cy = y; cy > y - n; cy--) {
                        unsigned want = 0;
                        if (cy == ly)
                            want = (unsigned)((cx - lx) * dx + 1);
                        int ex = lx + (n - 1) * dx;
                        if (cx == ex && cy != ly)
                            want = (unsigned)(n + (ly - cy) * dy);
                        if (cell_strip(cx, cy) != want && bad++ < 5)
                            printf("  plane %d building (%d,%d) n %d: strip of (%d,%d) %u, want %u (dir %d,%d)\n", pl,
                                   x, y, n, cx, cy, cell_strip(cx, cy), want, dx, dy);
                    }
            }
    }
    world_select_plane(plane);
    return bad;
}

static void test_rotation(const char *name)
{
    int plane = g_world.plane;
    int8_t dx0 = g_state.view_dir_x, dy0 = g_state.view_dir_y;
    snapshot();
    int bad = check_strips();
    CHECK(bad == 0, "%s: %d strips wrong before rotating", name, bad);
    for (int dir = 1; dir >= -1; dir -= 2) {
        for (int k = 0; k < 4; k++) {
            world_rotate(dir);
            bad = check_strips();
            CHECK(bad == 0, "%s: %d strips wrong after %d quarter turns (%+d)", name, bad, k + 1, dir);
        }
        int d = diff_grids();
        CHECK(d == 0 && g_state.view_dir_x == dx0 && g_state.view_dir_y == dy0,
              "%s: %d cells differ after 4 quarter turns (%+d)", name, d, dir);
        CHECK(g_world.plane == plane, "%s: plane not restored after rotating", name);
    }
    printf("  rotation: 2 x 4 quarter turns, grids %s\n", diff_grids() ? "DIFFER" : "identical");
}

/* ---- new worlds ----------------------------------------------------------------------- */

static void check_new_world(const char *what)
{
    for (int pl = 1; pl <= 3; pl += 2) {
        world_select_plane(pl);
        int water = 0;
        for (int i = 0; i < 100; i++)
            water += (cell_tileset(i, 0) == 2) + (cell_tileset(i, 99) == 2) + (cell_tileset(0, i) == 2) +
                     (cell_tileset(99, i) == 2);
        CHECK(water >= 4 * 100 - 12, "%s plane %d: border water %d", what, pl, water);
        Counts c = count(pl);
        CHECK(c.sets[10] > 0 && c.sets[2] > 400, "%s plane %d: %d rocks, %d water", what, pl, c.sets[10], c.sets[2]);
        CHECK(c.sets[18] >= 3 && c.sets[18] <= 6, "%s plane %d: %d Karma stations", what, pl, c.sets[18]);
        CHECK(realm_cur()->n_ports == 0 && realm_cur()->unk_8046 >= 1, "%s plane %d: realm not reset", what, pl);
        CHECK(g_world.tile_info[0].base_cost == 500 && g_world.tile_info[0x5b].base_cost == 250,
              "%s plane %d: tile info", what, pl);
        CHECK((cell_get(49, 49) & 8) && (cell_get(50, 50) & 8), "%s plane %d: centre not marked", what, pl);
        check_realm(pl, what);
    }
    world_select_plane(GS_PLANE_MIDDLE);
    Counts m = count(GS_PLANE_MIDDLE);
    CHECK(cell_tileset(0x31, 0x32) == 10 && m.sets[18] >= 3 && m.sets[18] <= 6, "%s: Karma plane", what);
    CHECK(check_footprints(GS_PLANE_MIDDLE, what) == 0, "%s: Karma plane footprints", what);
    world_select_plane(GS_PLANE_HELL);
}

static void test_new_worlds(void)
{
    static uint32_t first[4][GS_MAP_CELLS];
    for (int diff = 1; diff <= 3; diff++) {
        for (int rep = 0; rep < 2; rep++) {
            world_reset();
            g_state.hdr.difficulty = (uint16_t)diff;
            rng_seed(12345u + (uint32_t)diff);
            game_init_defaults();
            if (rep == 0) {
                for (int pl = 1; pl <= 3; pl++)
                    memcpy(first[pl], g_state.plane[pl].cells, sizeof first[pl]);
            } else {
                int d = 0;
                for (int pl = 1; pl <= 3; pl++)
                    d += memcmp(first[pl], g_state.plane[pl].cells, sizeof first[pl]) != 0;
                CHECK(d == 0, "new world difficulty %d not reproducible", diff);
            }
        }
        char what[32];
        snprintf(what, sizeof what, "new world (difficulty %d)", diff);
        check_new_world(what);
        Counts h = count(1), v = count(3);
        printf("%s: pennies %" PRId64 ", Hell %d water %d rocks, Heaven %d water %d rocks, rng %08" PRIx32 "\n", what,
               bignum_to_i64(g_state.pennies), h.sets[2], h.sets[10], v.sets[2], v.sets[10], rng_state());
        test_rotation(what);
        /* building on a fresh world */
        build_on_save(GS_PLANE_HELL, what);
        build_on_save(GS_PLANE_HEAVEN, what);
    }
}

/* ---- rendering --------------------------------------------------------------------------- */

static uint8_t g_pal[768];

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    static uint32_t t[256];
    if (!t[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t k = i;
            for (int j = 0; j < 8; j++)
                k = k & 1 ? 0xedb88320u ^ (k >> 1) : k >> 1;
            t[i] = k;
        }
    for (size_t i = 0; i < n; i++)
        c = t[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, size_t len)
{
    uint8_t b[4];
    put32(b, (uint32_t)len);
    fwrite(b, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len)
        fwrite(data, 1, len, f);
    uint32_t c = crc(0xffffffffu, (const uint8_t *)type, 4);
    c = crc(c, data, len) ^ 0xffffffffu;
    put32(b, c);
    fwrite(b, 1, 4, f);
}

static int write_png(const char *path, const GfxSurface *s)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    int w = s->width, h = s->height;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8, ihdr[9] = 3, ihdr[10] = 0, ihdr[11] = 0, ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "PLTE", g_pal, 768);
    size_t raw_len = (size_t)(w + 1) * (size_t)h, blocks = (raw_len + 65534) / 65535;
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
        *q++ = (uint8_t)n, *q++ = (uint8_t)(n >> 8), *q++ = (uint8_t)~n, *q++ = (uint8_t)(~n >> 8);
        memcpy(q, raw + off, n);
        for (size_t i = 0; i < n; i++)
            a = (a + raw[off + i]) % 65521, b = (b + a) % 65521;
        q += n, off += n;
    }
    put32(q, b << 16 | a);
    q += 4;
    chunk(f, "IDAT", z, (size_t)(q - z));
    chunk(f, "IEND", NULL, 0);
    free(raw);
    free(z);
    return fclose(f);
}

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

static void render_world(const char *out)
{
    static TileSet sets[32];
    static uint8_t *set_data[32];
    size_t plen = 0;
    uint8_t *praw = load_resource(33, &plen);
    gfx_palette_init();
    uint8_t *rgbx = praw ? gfx_palette_load_resource(praw, plen) : NULL;
    if (!rgbx) {
        CHECK(0, "no palette resource");
        free(praw);
        return;
    }
    for (int i = 0; i < 256; i++)
        g_pal[i * 3] = rgbx[i * 4], g_pal[i * 3 + 1] = rgbx[i * 4 + 1], g_pal[i * 3 + 2] = rgbx[i * 4 + 2];
    tile_draw_init(1);
    tile_draw_build_tables(rgbx);
    for (int id = 1; id <= 31; id++) {
        size_t len = 0;
        set_data[id] = load_resource((uint32_t)id, &len);
        if (set_data[id] && tile_set_open(&sets[id], set_data[id], len) == TILE_OK)
            tile_draw_bind_set(id - 1, &sets[id]);
    }
    MapPlanes planes;
    memset(&planes, 0, sizeof planes);
    map_planes_init(&planes);
    for (int pl = 1; pl <= 3; pl++) {
        planes.plane[pl].cells = g_state.plane[pl].cells;
        planes.plane[pl].objmap = g_state.plane[pl].objmap;
        planes.plane[pl].flags = g_build.view_flags[pl];
    }
    for (int zoom = 2; zoom >= 1; zoom--) {
        int hw = 16 >> zoom, hh = 8 >> zoom, top = 0x48 * 3 >> zoom;
        int sw = 200 * hw, sh = (0x708 >> zoom) + 200 * hh + top + 8;
        GfxSurface *s = gfx_surface_create(sw, sh);
        MapView v;
        memset(&v, 0, sizeof v);
        v.width = v.height = 100;
        v.cell_w = 32, v.cell_h = 16;
        v.zoom = (uint8_t)zoom;
        v.dir_x = g_state.view_dir_x, v.dir_y = g_state.view_dir_y;
        v.focus_x = v.focus_y = -1;
        v.set_mask = 0xffffffffu;
        v.flags = g_build.view_flags[3];
        v.clip_w = sw, v.clip_h = sh;
        planes.scroll_y = top;
        GfxRect b = {0, sw - 1, 0, sh - 1};
        map_view_select_plane(&v, &planes, 3);
        map_view_paint(s, &v, &planes, &b, NULL, 0, 0, sw, sh);
        char path[4096];
        snprintf(path, sizeof path, "%s/newworld_planes_z%d.png", out, zoom);
        if (write_png(path, s) != 0)
            CHECK(0, "can't write %s", path);
        else
            printf("wrote %s\n", path);
        gfx_surface_destroy(s);
    }
    for (int id = 1; id <= 31; id++)
        free(set_data[id]);
    free(praw);
    free(rgbx);
}

int main(int argc, char **argv)
{
    const char *data = NULL, *bundle = NULL, *out = NULL;
    int a = 1;
    if (argc >= 5 && !strcmp(argv[1], "-d")) {
        data = argv[2], bundle = argv[3], out = argv[4];
        a = 5;
    }
    /* the new world's planet generator needs ALIFE.EXE's tables */
    plat_set_data_dir(data ? data : "orig");
    if (!planet_tables_load()) {
        printf("can't read the planet tables from ALIFE.EXE (give -d <data dir>)\n");
        return 2;
    }
    tables_init_defaults();
    BuildHooks hooks = {0};
    hooks.sound = count_sound;
    g_build_hooks = hooks;

    for (; a < argc; a++) {
        char err[160];
        if (!world_load(argv[a], err, sizeof err)) {
            printf("%s: load failed: %s\n", argv[a], err);
            g_failures++;
            continue;
        }
        printf("%s: \"%s\", view %d,%d\n", argv[a], g_state.hdr.title, g_state.view_dir_x, g_state.view_dir_y);
        check_realm(GS_PLANE_HELL, argv[a]);
        check_realm(GS_PLANE_HEAVEN, argv[a]);
        world_select_plane(GS_PLANE_HELL);
        test_rotation(argv[a]);
        build_on_save(GS_PLANE_HELL, argv[a]);
        build_on_save(GS_PLANE_HEAVEN, argv[a]);
        test_rotation(argv[a]);
    }

    test_new_worlds();

    if (out) {
        world_reset();
        g_state.hdr.difficulty = 2;
        rng_seed(RNG_INITIAL_STATE);
        game_init_defaults();
        if (rc_initialize(bundle, bundle)) {
            render_world(out);
            rc_shutdown();
        } else {
            CHECK(0, "can't open %s", bundle);
        }
    }
    world_free();
    printf("%s (%d sounds)\n", g_failures ? "FAILED" : "ok", g_sounds);
    if (g_failures)
        printf("%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
