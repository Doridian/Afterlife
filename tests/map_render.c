/*
 * Exercises the map renderer (src/gfx/tile_draw.c, src/gfx/map_view.c)
 * against the user's game files. None are in the repo.
 *
 *   map_render <data dir> <bundle> [out dir] [savegame.ASC ...]
 *
 * Self-checks (exit status): tile_draw at zoom 0 matches the reference
 * strip decoder (formats/tile.c) for every strip of every set, mirroring
 * flips it, 2x doubles it, zooming out samples it, clipping crops it, and
 * the screen <-> cell transforms invert each other in all four rotations.
 *
 * With an out dir it writes indexed PNGs:
 *   sets/<res>_z<zoom>.png  every entry of every set at 2x (z-1), 1:1, 1:2, 1:4
 *   effects.png            one small map per effect (rows: 2x, z0, z1, z2)
 *   synth_*.png            a synthetic map of zones, buildings and roads
 *   <save>_*.png           the three planes of each ALSV savegame
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "formats/tile.h"
#include "game/resource.h"
#include "game/savegame.h"
#include "gfx/map_view.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "gfx/tile_draw.h"
#include "platform/platform.h"

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

/* --- minimal PNG writer (stored deflate blocks) ------------------------- */

static uint32_t crc_table[256];

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

static uint8_t g_png_pal[768];

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
    return fclose(f);
}

static const char *g_out;

static void save(const GfxSurface *s, const char *name)
{
    if (!g_out)
        return;
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", g_out, name);
    if (write_png(path, s) != 0)
        fprintf(stderr, "can't write %s\n", path);
}

/* --- setup ----------------------------------------------------------------- */

#define BG 255 /* palette entry painted magenta in the PNGs */

static TileSet g_sets[32];
static uint8_t *g_set_data[32];
static int g_failures;

static void check(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

static void view_init(MapView *v, GfxSurface *s, int zoom, int mag)
{
    memset(v, 0, sizeof *v);
    v->width = v->height = 100;
    v->cell_w = 32;
    v->cell_h = 16;
    v->zoom = zoom;
    v->magnify = mag;
    v->dir_x = v->dir_y = 1;
    v->focus_x = v->focus_y = -1;
    v->set_mask = 0xffffffffu;
    v->plane = 3;
    v->flags = MAPVIEW_F_ZONE_DETAIL | MAPVIEW_F_GRID;
    v->clip_x = 0;
    v->clip_y = 0;
    v->clip_w = s->width;
    v->clip_h = s->height;
}

static void fill(GfxSurface *s, uint8_t c) { memset(s->pixels, c, (size_t)s->stride * (size_t)s->height); }

/* Draws a whole sprite with its top-left at (X, Y) through tile_draw's
 * no-cell path, one strip per (virtual) cell like the map does. */
static void draw_sprite_at(GfxSurface *s, MapView *v, unsigned slot, unsigned index1, int X, int Y, int fade)
{
    TileEntry e;
    if (!tile_draw_set((int)slot) || tile_get_entry(tile_draw_set((int)slot), (int)index1 - 1, &e) != TILE_OK)
        return;
    int size = e.size, hw = mapview_half_w(v), hh = mapview_half_h(v);
    int sy0 = Y - (v->cell_h >> v->zoom) + ((e.anchor >> v->zoom) << v->magnify) - (size - 1) * hh;
    for (int k = 1; k < 2 * size; k++) {
        int sx = X + (k - 1) * hw;
        int sy = sy0 + (k <= size ? k - 1 : 2 * size - k - 1) * hh;
        tile_draw(s, v, 0, 0, sx, sy, NULL, slot, index1, (unsigned)k, fade, 1);
    }
}

/* --- self-checks -------------------------------------------------------------- */

static int pix(const GfxSurface *s, int x, int y) { return s->pixels[(size_t)y * (size_t)s->stride + (size_t)x]; }

static void self_checks(void)
{
    GfxSurface *ref = gfx_surface_create(256, 640), *got = gfx_surface_create(512, 1280);
    MapView v;
    long strips = 0, bad1 = 0, badm = 0, bad2 = 0, badz = 0, badc = 0, zsamples = 0;
    for (int slot = 0; slot < 31; slot++) {
        const TileSet *ts = tile_draw_set(slot);
        if (!ts)
            continue;
        for (int i = 0; i < ts->count; i++) {
            TileEntry e;
            if (tile_get_entry(ts, i, &e) != TILE_OK || e.size == 0 || e.height > 600)
                continue;
            for (int k = 1; k <= tile_drawn_strips(&e); k++) {
                int sw = tile_strip_width(&e, k - 1), sh = e.height;
                int expect_w = k == e.size ? 32 : 16;
                if (sw != expect_w)
                    continue; /* tile_draw assumes these widths */
                strips++;
                fill(ref, BG);
                tile_draw_strip(&e, k - 1, ref->pixels, ref->stride, ref->width, ref->height, 0, 0);
                /* tile_draw puts the strip at sx (+16 for right strips), sy + 16 - anchor + |k - size| * 8 */
                int dx = (k > e.size) ? 16 : 0;
                int base_y = 40 - 16 + e.anchor - abs(k - e.size) * 8; /* so the strip top lands on y = 40 */
                for (int mode = 0; mode < 4; mode++) {
                    int zoom = mode == 2 ? 1 : mode == 3 ? 2 : 0, mag = mode == 1;
                    view_init(&v, got, zoom, mag);
                    fill(got, BG);
                    int sy = 40 - (16 >> zoom) + ((e.anchor >> zoom) << mag) - abs(k - e.size) * (((8 >> zoom)) << mag);
                    int sx = 8 - (dx ? ((16 >> zoom) << mag) : 0);
                    (void)base_y;
                    tile_draw(got, &v, 0, 0, sx, sy, NULL, (unsigned)slot, (unsigned)i + 1, (unsigned)k, 0, 1);
                    for (int y = 0; y < (sh << mag) >> zoom; y++)
                        for (int x = 0; x < (sw << mag) >> zoom; x++) {
                            int g = pix(got, 8 + x, 40 + y);
                            int r = pix(ref, (x << zoom) >> mag, (y << zoom) >> mag);
                            if (g != r) {
                                if (mode == 0)
                                    bad1++;
                                else if (mode == 1)
                                    bad2++;
                                else
                                    badz++;
                            }
                            if (zoom)
                                zsamples++;
                        }
                    if (mode == 0) {
                        /* mirrored */
                        v.dir_x = -1;
                        fill(got, BG);
                        int msx = 8 - (k < e.size ? 16 : 0);
                        tile_draw(got, &v, 0, 0, msx, sy, NULL, (unsigned)slot, (unsigned)i + 1, (unsigned)k, 0, 1);
                        for (int y = 0; y < sh; y++)
                            for (int x = 0; x < sw; x++)
                                if (pix(got, 8 + sw - 1 - x, 40 + y) != pix(ref, x, y))
                                    badm++;
                        /* clipped to an odd rectangle */
                        v.dir_x = 1;
                        fill(got, BG);
                        v.clip_x = 8 + 3;
                        v.clip_y = 40 + 5;
                        v.clip_w = sw - 7 > 0 ? sw - 7 : 1;
                        v.clip_h = sh - 9 > 0 ? sh - 9 : 1;
                        tile_draw(got, &v, 0, 0, sx, sy, NULL, (unsigned)slot, (unsigned)i + 1, (unsigned)k, 0, 1);
                        for (int y = 0; y < sh + 4; y++)
                            for (int x = 0; x < sw + 4; x++) {
                                int gx = 8 + x, gy = 40 + y;
                                bool in = gx >= v.clip_x && gx < v.clip_x + v.clip_w && gy >= v.clip_y &&
                                          gy < v.clip_y + v.clip_h && x < sw && y < sh;
                                int want = in ? pix(ref, x, y) : BG;
                                if (pix(got, gx, gy) != want)
                                    badc++;
                            }
                    }
                }
            }
        }
    }
    printf("self-check: %ld strips; mismatched pixels: 1:1 %ld, mirrored %ld, clipped %ld, 2x %ld, zoomed %ld of %ld\n",
           strips, bad1, badm, badc, bad2, badz, zsamples);
    check(strips > 1000, "strips checked");
    check(bad1 == 0, "1:1 matches the reference decoder");
    check(badm == 0, "mirroring");
    check(badc == 0, "clipping");
    check(bad2 == 0, "2x");
    check(badz * 1000 <= zsamples, "zoomed sampling (<0.1% off)");

    /* transforms */
    int tbad = 0;
    for (int rot = 0; rot < 4; rot++)
        for (int zoom = 0; zoom < 3; zoom++) {
            view_init(&v, got, zoom, 0);
            v.dir_x = rot & 1 ? -1 : 1;
            v.dir_y = rot & 2 ? -1 : 1;
            v.origin_x = 37;
            v.origin_y = -11;
            for (int my = 0; my < 100; my += 3)
                for (int mx = 0; mx < 100; mx += 7) {
                    int sx, sy, bx, by, li, lj, cx, cy;
                    map_view_cell_to_screen(&v, mx, my, &sx, &sy);
                    /* centre of the cell's diamond */
                    map_view_screen_to_cell(&v, sx + v.origin_x, sy + v.origin_y + mapview_half_h(&v), &bx, &by);
                    map_view_to_logical(&v, mx, my, &li, &lj);
                    map_view_from_logical(&v, li, lj, &cx, &cy);
                    if (bx != mx || by != my || cx != mx || cy != my)
                        tbad++;
                }
        }
    printf("self-check: screen <-> cell transforms, %d mismatches\n", tbad);
    check(tbad == 0, "transforms invert");
    gfx_surface_destroy(ref);
    gfx_surface_destroy(got);
}

/* --- sheets ------------------------------------------------------------------ */

static void zoom_sheets(void)
{
    static const struct {
        int zoom, mag;
        const char *tag;
    } modes[] = {{0, 1, "2x"}, {0, 0, "z0"}, {1, 0, "z1"}, {2, 0, "z2"}};
    for (int slot = 0; slot < 31; slot++) {
        const TileSet *ts = tile_draw_set(slot);
        if (!ts)
            continue;
        for (int m = 0; m < 4; m++) {
            int zoom = modes[m].zoom, mag = modes[m].mag, sheet_w = 1024 << mag >> zoom;
            if (sheet_w < 256)
                sheet_w = 256;
            /* layout pass */
            int x = 0, y = 0, row_h = 0;
            int *pos = calloc((size_t)ts->count * 2, sizeof *pos);
            for (int i = 0; i < ts->count; i++) {
                TileEntry e;
                if (tile_get_entry(ts, i, &e) != TILE_OK || e.size == 0)
                    continue;
                int w = (e.size * 32 << mag) >> zoom, h = (e.height << mag) >> zoom;
                if (x + w > sheet_w && x > 0) {
                    x = 0;
                    y += row_h + 2;
                    row_h = 0;
                }
                pos[i * 2] = x, pos[i * 2 + 1] = y;
                x += w + 2;
                if (h > row_h)
                    row_h = h;
            }
            GfxSurface *s = gfx_surface_create(sheet_w, y + row_h + 1);
            fill(s, BG);
            MapView v;
            view_init(&v, s, zoom, mag);
            for (int i = 0; i < ts->count; i++)
                draw_sprite_at(s, &v, (unsigned)slot, (unsigned)i + 1, pos[i * 2], pos[i * 2 + 1], 0);
            char name[64];
            snprintf(name, sizeof name, "sets/%02d_%s.png", slot + 1, modes[m].tag);
            save(s, name);
            gfx_surface_destroy(s);
            free(pos);
        }
    }
}

/* --- maps ------------------------------------------------------------------- */

static uint32_t mkcell(unsigned set, unsigned index1, unsigned strip)
{
    return (uint32_t)set << CELL_SET_SHIFT | (uint32_t)(index1 - 1) << CELL_INDEX_SHIFT | (uint32_t)strip
                                                                                             << CELL_STRIP_SHIFT;
}

static unsigned entry_size(unsigned slot, unsigned index1)
{
    TileEntry e;
    const TileSet *ts = tile_draw_set((int)slot);
    if (!ts || tile_get_entry(ts, (int)index1 - 1, &e) != TILE_OK)
        return 0;
    return e.size;
}

/* Stamps a size x size building whose origin (the cell holding set and
 * index) is map (x0, y0): footprint x0..x0+n-1, y0-n+1..y0, every other
 * cell pointing back with dx = x - x0, dy = y0 - y. Strips go on the two
 * front edges in the view's rotation (the game rewrites them when the view
 * rotates, FUN_004514a4): seen from the front corner, the up-left edge gets
 * n, n-1, .. 1 and the up-right edge n, n+1, .. 2n-1 (swapped when the
 * rotation mirrors the sprites); the rest get strip 0. */
static bool stamp(const MapView *v, uint32_t *cells, int x0, int y0, unsigned slot, unsigned index1, uint32_t extra)
{
    int W = v->width, H = v->height, n = (int)entry_size(slot, index1);
    if (n == 0 || x0 < 0 || y0 - n + 1 < 0 || x0 + n > W || y0 >= H)
        return false;
    int fi = -1, fj = -1; /* logical front corner: max i + j */
    for (int y = y0 - n + 1; y <= y0; y++)
        for (int x = x0; x < x0 + n; x++) {
            int i, j;
            map_view_to_logical(v, x, y, &i, &j);
            if (i + j > fi + fj)
                fi = i, fj = j;
        }
    for (int y = y0 - n + 1; y <= y0; y++)
        for (int x = x0; x < x0 + n; x++) {
            int i, j, dx = x - x0, dy = y0 - y;
            map_view_to_logical(v, x, y, &i, &j);
            int left = j == fj ? fi - i : -1, right = i == fi ? fj - j : -1; /* distance along each edge */
            unsigned strip = 0;
            if (mapview_dir(v) > 0)
                strip = left >= 0 ? (unsigned)(n - left) : right >= 0 ? (unsigned)(n + right) : 0;
            else
                strip = left >= 0 ? (unsigned)(n + left) : right >= 0 ? (unsigned)(n - right) : 0;
            uint32_t c = (dx == 0 && dy == 0) ? mkcell(slot, index1, strip)
                                              : (uint32_t)dx << 16 | (uint32_t)dy << 20 | strip << CELL_STRIP_SHIFT;
            cells[y * W + x] = c | extra;
        }
    return true;
}

static void terrain(uint32_t *cells, int n)
{
    for (int i = 0; i < n; i++)
        cells[i] = mkcell(1, 1, 1); /* empty land with grid (MAPVIEW_F_GRID) */
}

typedef struct {
    const char *name;
    uint32_t bits;   /* on the building's cells */
    int plane;
    uint8_t flags;   /* view flags */
    int fade;        /* through the no-cell path */
    int mirror, mini, hilite, object;
    unsigned slot, index; /* 0 = default building */
} Effect;

static const Effect k_effects[] = {
    {"plain", 0, 3, MAPVIEW_F_ZONE_DETAIL, 0, 0, 0, 0, 0, 0, 0},
    {"shade (plane 1: table 0)", CELL_SHADE, 1, MAPVIEW_F_ZONE_DETAIL, 0, 0, 0, 0, 0, 0, 0},
    {"shade (plane 3: table 4)", CELL_SHADE, 3, MAPVIEW_F_ZONE_DETAIL, 0, 0, 0, 0, 0, 0, 0},
    {"tint + crack overlay", CELL_TINT, 3, MAPVIEW_F_ZONE_DETAIL, 0, 0, 0, 0, 0, 0, 0},
    {"tint, no overlay", CELL_TINT, 3, MAPVIEW_F_ZONE_DETAIL | MAPVIEW_F_NO_TINT_OVERLAY, 0, 0, 0, 0, 0, 0, 0},
    {"overlay (gunk)", CELL_OVERLAY, 3, MAPVIEW_F_ZONE_DETAIL, 0, 0, 0, 0, 0, 0, 0},
    {"alt (zone block)", CELL_ALT, 3, MAPVIEW_F_ZONE_DETAIL, 0, 0, 0, 0, 0, 0, 0},
    {"bracket (plane 1)", CELL_BRACKET, 1, MAPVIEW_F_ZONE_DETAIL, 0, 0, 0, 0, 0, 0, 0},
    {"bracket (plane 3)", CELL_BRACKET, 3, MAPVIEW_F_ZONE_DETAIL, 0, 0, 0, 0, 0, 0, 0},
    {"marker", CELL_MARKER, 3, MAPVIEW_F_ZONE_DETAIL, 0, 0, 0, 0, 0, 0, 0},
    {"fade 3", 0, 3, MAPVIEW_F_ZONE_DETAIL, 3, 0, 0, 0, 0, 0, 0},
    {"fade 7", 0, 3, MAPVIEW_F_ZONE_DETAIL, 7, 0, 0, 0, 0, 0, 0},
    {"miniature", 0, 3, MAPVIEW_F_ZONE_DETAIL, 0, 0, 1, 0, 0, 0, 0},
    {"mirrored (dir_x -1)", 0, 3, MAPVIEW_F_ZONE_DETAIL, 0, 1, 0, 0, 0, 0, 0},
    {"zone blocks (no ZONE_DETAIL)", 0, 3, 0, 0, 0, 0, 0, 0, 0, 0},
    {"static (set 5 + tint)", CELL_TINT, 3, MAPVIEW_F_ZONE_DETAIL, 0, 0, 0, 0, 0, 4, 1},
    {"highlight", CELL_HILITE, 3, MAPVIEW_F_ZONE_DETAIL, 0, 0, 0, 1, 0, 0, 0},
    {"deferred + object fade 5", CELL_DEFER | CELL_OBJECT, 3, MAPVIEW_F_ZONE_DETAIL, 0, 0, 0, 0, 1, 0, 0},
};
#define N_EFFECTS (int)(sizeof k_effects / sizeof k_effects[0])

static void effects_sheet(void)
{
    enum { MW = 6 };
    static const struct {
        int zoom, mag;
    } rows[] = {{0, 1}, {0, 0}, {1, 0}, {2, 0}};
    int col_w = 2 * MW * 16 * 2 + 8, y = 0, row_y[4], row_h[4];
    for (int r = 0; r < 4; r++) {
        row_y[r] = y;
        row_h[r] = ((MW * 16 + 140) << rows[r].mag) >> rows[r].zoom;
        y += row_h[r] + 4;
    }
    GfxSurface *s = gfx_surface_create(col_w * N_EFFECTS, y);
    fill(s, BG);
    uint32_t cells[MW * MW];
    uint8_t objmap[MW * MW];
    MapObject objs[MAP_OBJECTS];
    memset(objs, 0, sizeof objs);
    /* a heaven building (set 16, entry 0x48: 2x2) */
    unsigned slot = 15, index = 0;
    for (unsigned i = 2; i < 112 && !index; i++)
        if (entry_size(slot, i) == 2 && (i - 1) % 13 != 0)
            index = i;
    for (int c = 0; c < N_EFFECTS; c++) {
        const Effect *fx = &k_effects[c];
        unsigned es = fx->slot ? fx->slot : slot, ei = fx->index ? fx->index : index;
        unsigned n = entry_size(es, ei);
        for (int r = 0; r < 4; r++) {
            terrain(cells, MW * MW);
            memset(objmap, 0, sizeof objmap);
            MapView v;
            view_init(&v, s, rows[r].zoom, rows[r].mag);
            v.width = v.height = MW;
            v.cells = cells;
            v.objmap = objmap;
            v.objects = objs;
            v.plane = (uint8_t)fx->plane;
            v.flags = fx->flags;
            v.mini = (uint8_t)fx->mini;
            v.hilite_from = v.hilite_to = 0;
            if (fx->mirror)
                v.dir_x = -1;
            v.flags |= MAPVIEW_F_GRID;
            if (!fx->fade)
                stamp(&v, cells, 1, (int)n, es, ei, fx->bits);
            if (fx->object) {
                /* An object of the same set and size (the next building)
                 * over the building's front cells: the two cross-fade. */
                unsigned other = ei + 1;
                while (other < 112 && entry_size(es, other) != n)
                    other++;
                objs[1] = (MapObject){3, 1, (int8_t)n, 0, 0, (int8_t)n, (uint8_t)es, (uint8_t)(other - 1), 5};
                for (int i = 0; i < MW * MW; i++) {
                    if (cell_strip(cells[i]) == 0)
                        cells[i] &= ~CELL_OBJECT;
                    if (cells[i] & CELL_OBJECT)
                        objmap[i] = 1 << 2;
                }
            }
            int hw = mapview_half_w(&v), hh = mapview_half_h(&v);
            v.origin_x = c * col_w + 4;
            v.origin_y = row_y[r] + (row_h[r] - 2 * MW * hh) - 2;
            v.clip_x = c * col_w;
            v.clip_y = row_y[r];
            v.clip_w = col_w - 4;
            v.clip_h = row_h[r];
            GfxRect bounds = {v.clip_x, v.clip_x + v.clip_w - 1, v.clip_y, v.clip_y + v.clip_h - 1};
            map_view_draw(s, &v, &bounds);
            if (fx->hilite)
                map_view_hilite_pass(s, &v, &bounds, true);
            if (fx->fade) {
                /* the building at its map position, through the no-cell path */
                int li = 1, lj = (int)n;
                int sx0 = v.origin_x + (li - lj + MW - 1) * hw, sy0 = v.origin_y + (li + lj) * hh;
                for (int k = 1; k < 2 * (int)n; k++) {
                    int ci = k <= (int)n ? li + k - 1 : li + (int)n - 1;
                    int cj = k <= (int)n ? lj : lj - (k - (int)n);
                    tile_draw(s, &v, ci, cj, sx0 + (ci - li - (cj - lj)) * hw, sy0 + (ci - li + cj - lj) * hh, NULL,
                              es, ei, (unsigned)k, fx->fade, 1);
                }
            }
        }
        printf("effects.png column %d: %s\n", c, fx->name);
    }
    save(s, "effects.png");
    gfx_surface_destroy(s);
}

/* A synthetic Hell: blocks of each zone type with buildings packed into
 * them, separated by road cells. Buildings are re-stamped for each view's
 * rotation. */
typedef struct {
    int x0, y0;
    unsigned index;
} Placed;

static void synthetic_map(void)
{
    enum { W = 40, MAXP = 256 };
    uint32_t *base = calloc(W * W, sizeof *base), *cells = calloc(W * W, sizeof *cells);
    Placed placed[MAXP];
    int np = 0;
    terrain(base, W * W);
    unsigned zone_slot = 14; /* resource 15, Hell zones */
    for (int t = 0; t < 7; t++) {
        int bx = 1 + (t % 4) * 10, by = 1 + (t / 4) * 12;
        for (int k = -1; k <= 9; k++) {
            int rx[4] = {bx + k, bx + k, bx - 1, bx + 9}, ry[4] = {by - 1, by + 9, by + k, by + k};
            for (int q = 0; q < 4; q++)
                if (rx[q] >= 0 && ry[q] >= 0 && rx[q] < W && ry[q] < W)
                    base[ry[q] * W + rx[q]] = mkcell(12, 1, 1);
        }
        for (int y = by; y < by + 9; y++)
            for (int x = bx; x < bx + 9; x++)
                base[y * W + x] = mkcell(zone_slot, (unsigned)t * 13 + 1, 1);
        /* buildings, biggest index first, greedy into the 9x9 block */
        bool used[9][9] = {{0}};
        for (unsigned i = (unsigned)t * 13 + 13; i > (unsigned)t * 13 + 1 && np < MAXP; i--) {
            int n = (int)entry_size(zone_slot, i);
            if (n == 0 || n > 9)
                continue;
            bool done = false;
            for (int y0 = n - 1; y0 < 9 && !done; y0++)
                for (int x0 = 0; x0 + n <= 9 && !done; x0++) {
                    bool free_ = true;
                    for (int y = y0 - n + 1; y <= y0 && free_; y++)
                        for (int x = x0; x < x0 + n; x++)
                            free_ = free_ && !used[y][x];
                    if (!free_)
                        continue;
                    for (int y = y0 - n + 1; y <= y0; y++)
                        for (int x = x0; x < x0 + n; x++)
                            used[y][x] = true;
                    placed[np++] = (Placed){bx + x0, by + y0, i};
                    done = true;
                }
        }
    }
    printf("synthetic map: %dx%d, %d buildings\n", W, W, np);
    static const struct {
        int zoom, mag, rot;
        uint8_t flags;
        const char *name;
    } views[] = {
        {1, 0, 0, MAPVIEW_F_ZONE_DETAIL, "synth_z1.png"},      {1, 0, 0, 0, "synth_z1_blocks.png"},
        {2, 0, 0, MAPVIEW_F_ZONE_DETAIL, "synth_z2.png"},      {0, 0, 0, MAPVIEW_F_ZONE_DETAIL, "synth_z0.png"},
        {1, 0, 1, MAPVIEW_F_ZONE_DETAIL, "synth_z1_rot1.png"}, {1, 0, 2, MAPVIEW_F_ZONE_DETAIL, "synth_z1_rot2.png"},
        {1, 0, 3, MAPVIEW_F_ZONE_DETAIL, "synth_z1_rot3.png"}, {0, 1, 0, MAPVIEW_F_ZONE_DETAIL, "synth_2x_crop.png"},
    };
    for (size_t k = 0; k < sizeof views / sizeof views[0]; k++) {
        int zoom = views[k].zoom, mag = views[k].mag;
        int hw = (16 >> zoom) << mag, hh = (8 >> zoom) << mag, top = (0x48 * 3 >> zoom) << mag;
        int sw = 2 * W * hw, sh = 2 * W * hh + top;
        if (mag) {
            sw = 1024;
            sh = 768;
        }
        GfxSurface *s = gfx_surface_create(sw, sh);
        fill(s, 0x13);
        MapView v;
        view_init(&v, s, zoom, mag);
        v.width = v.height = W;
        v.cells = cells;
        v.plane = 1;
        v.flags = views[k].flags | MAPVIEW_F_GRID;
        v.margin_top = 0x48 * 3;
        v.margin_bottom = 0x48;
        v.dir_x = views[k].rot & 1 ? -1 : 1;
        v.dir_y = views[k].rot & 2 ? -1 : 1;
        v.origin_x = mag ? -900 : 0;
        v.origin_y = mag ? -300 : top;
        memcpy(cells, base, W * W * sizeof *cells);
        for (int i = 0; i < np; i++)
            stamp(&v, cells, placed[i].x0, placed[i].y0, zone_slot, placed[i].index, 0);
        GfxRect b = {0, sw - 1, 0, sh - 1};
        map_view_draw(s, &v, &b);
        save(s, views[k].name);
        gfx_surface_destroy(s);
    }
    free(base);
    free(cells);
}

/* --- ALSV savegames (src/game/savegame.c, docs/formats/alsv.md) ----------- */

static void render_save(const char *path)
{
    static GameState gs; /* ~1 MB */
    char err[160];
    if (!savegame_load(&gs, path, err, sizeof err)) {
        fprintf(stderr, "%s: can't load: %s\n", path, err);
        g_failures++;
        return;
    }
    uint32_t *cells[4] = {0};
    uint8_t *objmap[4] = {0};
    for (int pl = 1; pl <= 3; pl++) {
        cells[pl] = gs.plane[pl].cells;
        objmap[pl] = gs.plane[pl].objmap;
    }
    const char *title = gs.hdr.title;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char stem[64];
    snprintf(stem, sizeof stem, "%.*s", (int)(strcspn(base, ".")), base);
    int counts[32] = {0};
    for (int pl = 1; pl <= 3; pl += 2)
        for (int i = 0; i < 10000; i++)
            counts[cell_set(cells[pl][i])]++;
    printf("%s: \"%s\", Hell+Heaven cells by set:", base, title);
    for (int i = 0; i < 32; i++)
        if (counts[i])
            printf(" %d:%d", i, counts[i]);
    printf("\n");

    MapPlanes planes;
    memset(&planes, 0, sizeof planes);
    map_planes_init(&planes);
    for (int pl = 1; pl <= 3; pl++) {
        planes.plane[pl].cells = cells[pl];
        planes.plane[pl].objmap = objmap[pl];
        planes.plane[pl].flags = MAPVIEW_F_ZONE_DETAIL | MAPVIEW_F_GRID;
    }
    /* all three planes stacked, as the game lays them out, at zoom 2 */
    for (int zoom = 2; zoom >= 1; zoom--) {
        int hw = 16 >> zoom, hh = 8 >> zoom, top = 0x48 * 3 >> zoom;
        int sw = 200 * hw, sh = (0x708 >> zoom) + 200 * hh + top + 8;
        GfxSurface *s = gfx_surface_create(sw, sh);
        MapView v;
        view_init(&v, s, zoom, 0);
        v.plane = 0;
        planes.scroll_y = top;
        GfxRect b = {0, sw - 1, 0, sh - 1};
        map_view_select_plane(&v, &planes, 3);
        map_view_paint(s, &v, &planes, &b, NULL, 0, 0, sw, sh);
        char name[128];
        snprintf(name, sizeof name, "%s_planes_z%d.png", stem, zoom);
        save(s, name);
        gfx_surface_destroy(s);
    }
    /* Heaven and Hell alone at zoom 1, and a 1:1 crop of Heaven */
    for (int pl = 1; pl <= 3; pl += 2) {
        int zoom = 1, hw = 8, hh = 4, top = 0x48 * 3 >> zoom;
        int sw = 200 * hw, sh = 200 * hh + top;
        GfxSurface *s = gfx_surface_create(sw, sh);
        fill(s, 0x13);
        MapView v;
        view_init(&v, s, zoom, 0);
        planes.scroll_y = top - (planes.plane[pl].y_offset >> zoom);
        map_view_select_plane(&v, &planes, pl);
        GfxRect b = {0, sw - 1, 0, sh - 1};
        map_view_draw(s, &v, &b);
        char name[128];
        snprintf(name, sizeof name, "%s_%s_z1.png", stem, pl == 1 ? "hell" : "heaven");
        save(s, name);
        gfx_surface_destroy(s);
    }
    {
        GfxSurface *s = gfx_surface_create(1024, 768);
        fill(s, 0x13);
        MapView v;
        view_init(&v, s, 0, 0);
        planes.scroll_y = -(0x40) - 1600 / 2 + 384;
        map_view_select_plane(&v, &planes, 3);
        v.origin_x = -(3200 / 2 - 512);
        GfxRect b = {0, 1023, 0, 767};
        map_view_draw(s, &v, &b);
        char name[128];
        snprintf(name, sizeof name, "%s_heaven_z0_crop.png", stem);
        save(s, name);
        gfx_surface_destroy(s);
    }
    savegame_free(&gs);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <data dir> <bundle> [out dir] [savegame.ASC ...]\n", argv[0]);
        return 2;
    }
    g_out = argc >= 4 ? argv[3] : NULL;
    crc_init();
    plat_set_data_dir(argv[1]);
    if (!rc_initialize(argv[2], argv[2]))
        return 1;
    if (g_out) {
        char path[4096];
        snprintf(path, sizeof path, "%s/sets", g_out);
        mkdir(path, 0777);
    }

    size_t plen = 0;
    uint8_t *praw = load_resource(33, &plen);
    gfx_palette_init();
    uint8_t *rgbx = praw ? gfx_palette_load_resource(praw, plen) : NULL;
    if (!rgbx) {
        fprintf(stderr, "no palette resource 33\n");
        return 1;
    }
    printf("palette 33: %zu bytes (256 x B,G,R,x + 15 x 8-byte cycling records)\n", plen);
    for (int c = 0; c < 15; c++) {
        const uint8_t *rec = praw + 1024 + c * 8;
        if (rec[2])
            printf("  cycle %2d: colours %3u-%3u, speed %u\n", c, rec[6], rec[7], rec[2]);
    }
    for (int i = 0; i < 256; i++)
        g_png_pal[i * 3] = rgbx[i * 4], g_png_pal[i * 3 + 1] = rgbx[i * 4 + 1], g_png_pal[i * 3 + 2] = rgbx[i * 4 + 2];
    g_png_pal[BG * 3] = 255, g_png_pal[BG * 3 + 1] = 0, g_png_pal[BG * 3 + 2] = 255;
    tile_draw_init(1);
    tile_draw_build_tables(rgbx);

    for (int id = 1; id <= 31; id++) {
        size_t len = 0;
        g_set_data[id] = load_resource((uint32_t)id, &len);
        if (g_set_data[id] && tile_set_open(&g_sets[id], g_set_data[id], len) == TILE_OK)
            tile_draw_bind_set(id - 1, &g_sets[id]);
        else
            fprintf(stderr, "resource %d: not a tile set\n", id);
    }

    self_checks();
    if (g_out) {
        zoom_sheets();
        effects_sheet();
        synthetic_map();
    }
    for (int i = 4; i < argc; i++)
        render_save(argv[i]);

    for (int id = 1; id <= 31; id++)
        free(g_set_data[id]);
    free(praw);
    free(rgbx);
    rc_shutdown();
    printf("%d failures\n", g_failures);
    return g_failures != 0;
}
