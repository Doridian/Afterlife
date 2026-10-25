/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Behaviour test for the information views (src/ui/graphview.c, mapview.c).
 * Needs the user's game files; none are in the repo.
 *
 *   views_test <data dir> <bundle> <out dir> <save.ASC|.ALF> ...
 *
 * For each save: loads it (world_load), paints the main map into a main
 * window, opens the Graphview and the Mapview headless and drives them
 * through the synthetic mouse path (gfx_mouse_on_move/on_button + wm_idle):
 * every graph mode, every Mapview layer in both realms, zoom and
 * click-to-scroll, a window drag. Checks the drawings against the save's
 * data (history curves, population bars, structure counts, minimap cell
 * colours in all four rotations) and writes PNGs into <out dir>:
 *   <save>_graph_<mode>.png     the Graphview window per mode
 *   <save>_map_<layer>_<realm>.png  the Mapview window per layer
 *   <save>_screen.png           the whole screen with both views open
 * Also checks graph_history_record / graph_history_update on synthetic
 * years and that the views leave the save's state byte-identical.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/resource.h"
#include "game/savegame.h"
#include "game/tables.h"
#include "game/util.h"
#include "game/world.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/mouse.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "platform/platform.h"
#include "ui/controls.h"
#include "ui/graphview.h"
#include "ui/mapview.h"
#include "ui/views.h"
#include "ui/wm.h"

#define W 1024
#define H 768

static int g_failures;
static const char *g_out;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

/* ---- PNG (stored deflate) ---------------------------------------------- */

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

static void write_png(const char *name, const GfxSurface *s)
{
    if (!g_out || !s)
        return;
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", g_out, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "can't write %s\n", path);
        g_failures++;
        return;
    }
    uint8_t rgbx[1024], pal[768];
    gfx_palette_get(rgbx, 0, 256);
    for (int i = 0; i < 256; i++)
        pal[i * 3] = rgbx[i * 4], pal[i * 3 + 1] = rgbx[i * 4 + 1], pal[i * 3 + 2] = rgbx[i * 4 + 2];
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    fwrite(sig, 1, 8, f);
    int w = s->width, h = s->height;
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8, ihdr[9] = 3, ihdr[10] = 0, ihdr[11] = 0, ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "PLTE", pal, 768);
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

/* ---- helpers ------------------------------------------------------------- */

static uint8_t *read_resource(uint32_t id, size_t *len)
{
    PlatFile *f = rc_open_resource(id);
    if (!f)
        return NULL;
    long l = rc_resource_size(id);
    uint8_t *buf = l > 0 ? malloc((size_t)l) : NULL;
    if (buf)
        plat_file_read(f, buf, 1, (size_t)l);
    plat_file_close(f);
    *len = buf ? (size_t)l : 0;
    return buf;
}

static void pump(int n)
{
    for (int i = 0; i < n; i++) {
        wm_idle();
        ctl_idle();
    }
}

static void mouse_to(int x, int y)
{
    gfx_mouse_on_move(x, y);
    pump(2);
}

static void click_at(int x, int y, unsigned button)
{
    mouse_to(x, y);
    gfx_mouse_on_button(button, true);
    pump(4);
    gfx_mouse_on_button(button, false);
    pump(6);
}

/* a window-local point to the screen */
static void to_screen(const WmWindow *w, int *x, int *y) { wm_point_to_screen(w, x, y); }

static void click_local(WmWindow *w, int x, int y, unsigned button)
{
    to_screen(w, &x, &y);
    click_at(x, y, button);
}

/* clicks the middle of a button found by its id */
static bool click_button(WmWindow *win, intptr_t id)
{
    WmWindow *b = ctl_find_by_id(win->children, id);
    if (!b || !wm_is_visible(b))
        return false;
    int x = (b->rect.left + b->rect.right) / 2, y = (b->rect.top + b->rect.bottom) / 2;
    click_local(win, x, y, GFX_MOUSE_LEFT);
    return true;
}

static uint8_t wpx(const WmWindow *w, int x, int y)
{
    return w->surface->pixels[(size_t)y * (size_t)w->surface->stride + (size_t)x];
}

/* ---- the main view, as the shell (WP7) will own it (views_test_gfx.c) --- */

void vt_gfx_init(const uint8_t *rgbx);
void vt_gfx_free(void);
struct MapView *vt_gfx_setup(uint32_t *cells[4], uint8_t *objmap[4], int w, int h);
void vt_gfx_select_plane(int plane);
int vt_gfx_plane(void);
void vt_gfx_paint(GfxSurface *dst);
void vt_gfx_center(int plane, int x, int y);
void vt_gfx_rotate(int dir_x, int dir_y);

static WmWindow *g_main;
static int g_paused;
static int g_center_calls, g_center_plane, g_center_x, g_center_y;
static int g_help_calls;
static unsigned g_menu_cmd;

static intptr_t main_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b) { return wm_default_proc(w, msg, a, b); }

/* both halves of 0x42b102, as the shell does */
static int select_plane(int plane)
{
    int prev = vt_gfx_plane();
    world_select_plane(plane);
    vt_gfx_select_plane(plane);
    return prev;
}

static void paint_main(void)
{
    vt_gfx_paint(g_main->surface);
    wm_redraw(g_main, NULL);
}

/* 0x406be8 stand-in: centre the main view on the cell and repaint, which
 * refreshes the Mapview (map_view_paint 0x4076f5 does) */
static void center_on_cell(int plane, int x, int y)
{
    g_center_calls++;
    g_center_plane = plane, g_center_x = x, g_center_y = y;
    vt_gfx_center(plane, x, y);
    paint_main();
    mapview_refresh();
}

static void help_balloon(int x, int y, int width, const char *text)
{
    (void)x, (void)y, (void)width;
    if (text && *text)
        g_help_calls++;
}

static void menu_command(unsigned id)
{
    g_menu_cmd = id;
    if (id == 0x19)
        wm_show(graphview_window(), !wm_is_visible(graphview_window()));
    if (id == 0x1a)
        wm_show(mapview_window(), !wm_is_visible(mapview_window()));
}

static void view_setup(void)
{
    uint32_t *cells[4] = {0};
    uint8_t *objmap[4] = {0};
    for (int pl = 1; pl <= 3; pl++)
        cells[pl] = g_state.plane[pl].cells, objmap[pl] = g_state.plane[pl].objmap;
    views_set_context(g_main, vt_gfx_setup(cells, objmap, W, H), &g_paused);
    select_plane(GS_PLANE_HEAVEN);
}

/* ---- independent expectations ----------------------------------------------- */

/* zoning colour of a cell from the save's grids (docs/map.md cell layout) */
static int expect_zoning(int plane, int x, int y)
{
    static const uint8_t col[9] = {0x0a, 0x0b, 0x0d, 0x1d, 0x0f, 0x0c, 0x0e, 0x23, 0x62};
    const uint32_t *cells = g_state.plane[plane].cells;
    const SimCell *sim = g_state.plane[plane].sim;
    uint32_t c = cells[y * 100 + x];
    if (c >> 27 == 0) {
        int ox = sim[y * 100 + x].b[10], oy = sim[y * 100 + x].b[11];
        c = cells[oy * 100 + ox];
    }
    unsigned set = c >> 27, idx = (c >> 16) & 0x7f;
    if (set == 1)
        return 0x13; /* background */
    if (set == 2)
        return idx < 0x12 ? 0x41 : 0x79;
    if (set == 5)
        return col[8];
    if (set == 14 || set == 15)
        return col[idx / 13 > 6 ? 7 : idx / 13];
    return 0x2b;
}

static int check_zoning(WmWindow *mv, int plane)
{
    MapviewState st = mapview_state();
    int bad = 0, n = 100 >> st.zoom, s = 2 << st.zoom;
    for (int x = st.x0; x < st.x0 + n; x++)
        for (int y = st.y0; y < st.y0 + n; y++) {
            int px, py;
            mapview_cell_pos(x, y, &px, &py);
            int cx, cy;
            if (!mapview_cell_at(px + s / 2, py + s / 2, &cx, &cy) || cx != x || cy != y)
                bad++;
            uint8_t got = wpx(mv, px + s / 2, py + s / 2);
            /* the view outline (colour 0xb) may cross the cell */
            if (got != expect_zoning(plane, x, y) && got != 0x0b)
                bad++;
        }
    return bad;
}

/* the History Graph's sample y, as graph_history_draw scales it */
static int series_y(int32_t v, int32_t lo, int32_t hi, int top, int bottom)
{
    int32_t range = (int32_t)((uint32_t)hi - (uint32_t)lo);
    int step = range / (bottom - top);
    if (step < 2)
        step = 1;
    int mul = step < 100 ? 100 : 1;
    step = (int32_t)((uint32_t)range * (uint32_t)mul) / (bottom - top);
    if (step < 2)
        step = 1;
    int y = bottom - (int32_t)((uint32_t)(v - lo) * (uint32_t)mul) / step;
    if (y <= top)
        y = top;
    if (y > 0xd6)
        y = 0xd7;
    if (y < 0x11)
        y = 0x10;
    return y;
}

/* ---- per save -------------------------------------------------------------------- */

static const char *const mode_names[GRAPH_MODES] = {"tenet", "population", "structures", "history",
                                                    "labor", "belief",     "budget"};
static const char *const layer_names[8] = {"tempperm", "efficiency", "vibes", "capacity",
                                           "zoning",   "traffic",    "karma", "rocks"};

static void test_graphs(const char *stem)
{
    WmWindow *gv = graphview_window();
    char name[256];
    wm_raise(gv);
    for (int m = 0; m < GRAPH_MODES; m++) {
        CHECK(click_button(gv, GV_ID_MODE + m));
        CHECK(graphview_mode() == m);
        if (m == GRAPH_HISTORY) {
            /* only the Heaven population curve: Hell, pennies off */
            CHECK(click_button(gv, GV_ID_SERIES + 0));
            CHECK(click_button(gv, GV_ID_SERIES + 2));
            const Rec72 *r = &g_state.rec_5c4eb0[0];
            const Rec72 *r3 = &g_state.rec_5c4eb0[3];
            int32_t lo = (int32_t)r->min <= (int32_t)r3->min ? (int32_t)r->min : (int32_t)r3->min;
            int32_t hi = (int32_t)r3->max <= (int32_t)r->max ? (int32_t)r->max : (int32_t)r3->max;
            int bad = 0;
            for (int i = 1; i < (int8_t)r->count && i < 25; i++) {
                int y = series_y((int32_t)r->v[i], lo, hi, 0x10, 0xd7);
                if (wpx(gv, 0x2f + 9 * (i - 1), y) != 0xe)
                    bad++;
            }
            printf("  history: Heaven pop %d samples (shift %u, %d..%d), %d off the curve\n", (int8_t)r->count,
                   (unsigned)r->shift, (int)r->min, (int)r->max, bad);
            CHECK(bad == 0);
            snprintf(name, sizeof name, "%s_graph_history_heaven.png", stem);
            write_png(name, gv->surface);
            CHECK(click_button(gv, GV_ID_SERIES + 0));
            CHECK(click_button(gv, GV_ID_SERIES + 2));
            /* the 500-year scale */
            CHECK(click_button(gv, GV_ID_SCALE + 1));
            snprintf(name, sizeof name, "%s_graph_history500.png", stem);
            write_png(name, gv->surface);
            CHECK(click_button(gv, GV_ID_SCALE + 0));
        }
        if (m == GRAPH_POPULATION) {
            /* every bar: the first (dark) part of each row, both realms on */
            static const uint8_t col48[7] = {0x0a, 0x4d, 0x56, 0x67, 0x36, 0x44, 0x7e};
            static const uint32_t steps[] = {1000, 5000, 25000, 125000, 625000, 3125000, 15625000, 78125000, 390625000};
            int32_t mx = 0;
            for (int k = 0; k < 7; k++)
                for (int r = 0; r < 2; r++) {
                    const GraphSummary *s = &g_state.summary[r];
                    uint8_t z = g_state.unk_593240[k];
                    int32_t sum = (int32_t)(s->unk_084[z] + s->unk_068[z] + s->unk_0bc[z] + s->unk_0a0[z]);
                    if (mx < (int32_t)s->unk_0d8[z])
                        mx = (int32_t)s->unk_0d8[z];
                    if (mx < sum)
                        mx = sum;
                }
            uint32_t top = 3000000000u;
            for (size_t i = 0; i < 9; i++)
                if ((uint32_t)mx < steps[i] + 1) {
                    top = steps[i];
                    break;
                }
            int div = (int)(top / 200), bars = 0, bad = 0;
            if (div < 3)
                div = 2;
            for (int k = 0; k < 7 && mx > 0; k++)
                for (int r = 0; r < 2; r++) {
                    const GraphSummary *s = &g_state.summary[r];
                    uint8_t z = g_state.unk_593240[k];
                    int px = (int32_t)(s->unk_084[z] + s->unk_068[z]) / div;
                    if (px + 0x54 > 0x131)
                        px = 0xdd;
                    if (px < 3)
                        continue;
                    int y = 0xd7 - 0x99 + (k * 2 + r) * 0xb + 5;
                    bars++;
                    if (wpx(gv, 0x54 + px / 2, y) != col48[z])
                        bad++;
                }
            printf("  population: %d bars checked (scale %u), %d wrong\n", bars, (unsigned)top, bad);
            CHECK(bad == 0);
        }
        snprintf(name, sizeof name, "%s_graph_%s.png", stem, mode_names[m]);
        write_png(name, gv->surface);
    }
    /* right-click help over the graph and on a button */
    int h0 = g_help_calls;
    click_local(gv, 0x40, 0x60, GFX_MOUSE_RIGHT);
    WmWindow *b = ctl_find_by_id(gv->children, GV_ID_MODE + GRAPH_HISTORY);
    if (b)
        click_local(gv, (b->rect.left + b->rect.right) / 2, (b->rect.top + b->rect.bottom) / 2, GFX_MOUSE_RIGHT);
    CHECK(g_help_calls >= h0 + 2);
    /* back to population */
    CHECK(click_button(gv, GV_ID_MODE + GRAPH_POPULATION));
}

static void test_mapview(const char *stem)
{
    WmWindow *mv = mapview_window();
    char name[256];
    wm_raise(mv);
    for (int heaven = 1; heaven >= 0; heaven--) {
        CHECK(click_button(mv, heaven ? MAPVIEW_HEAVEN : MAPVIEW_HELL) || mapview_state().layer == MAPVIEW_KARMA);
        for (int l = 0; l < 8; l++) {
            int id = MAPVIEW_TEMP_PERM + l;
            if (!click_button(mv, id))
                CHECK(mapview_state().layer == id); /* already selected (sticky) */
            CHECK(mapview_state().layer == id);
            if (id == MAPVIEW_KARMA && heaven == 0)
                continue; /* the same picture for both realms */
            if (id == MAPVIEW_ZONING) {
                int bad = check_zoning(mv, heaven ? GS_PLANE_HEAVEN : GS_PLANE_HELL);
                printf("  mapview zoning %s: %d cells off\n", heaven ? "Heaven" : "Hell", bad);
                CHECK(bad == 0);
            }
            snprintf(name, sizeof name, "%s_map_%s_%s.png", stem, layer_names[l],
                     id == MAPVIEW_KARMA ? "all"
                     : heaven            ? "heaven"
                                         : "hell");
            write_png(name, mv->surface);
            /* the realm buttons are hidden on the Karma layer */
            if (id == MAPVIEW_KARMA)
                CHECK(!wm_is_visible(ctl_find_by_id(mv->children, MAPVIEW_HEAVEN)));
        }
    }
    CHECK(click_button(mv, MAPVIEW_HEAVEN));
    CHECK(click_button(mv, MAPVIEW_ZONING));
    CHECK(mapview_state().heaven);

    /* click-to-scroll: left click zooms in on the cell and centres the main
     * view there; the outline follows */
    int px, py;
    mapview_cell_pos(30, 60, &px, &py);
    int c0 = g_center_calls;
    click_local(mv, px + 1, py + 1, GFX_MOUSE_LEFT);
    MapviewState st = mapview_state();
    CHECK(g_center_calls == c0 + 1 && g_center_plane == GS_PLANE_HEAVEN && g_center_x == 30 && g_center_y == 60);
    CHECK(st.zoom == 1 && st.x0 == 5 && st.y0 == 35);
    CHECK(check_zoning(mv, GS_PLANE_HEAVEN) == 0);
    /* the outline (colour 0xb) is drawn around the centre */
    int outline = 0;
    for (int y = MAPVIEW_MAP_Y; y < MAPVIEW_MAP_Y + 200; y++)
        for (int x = MAPVIEW_MAP_X; x < MAPVIEW_MAP_X + 200; x++)
            outline += wpx(mv, x, y) == 0xb;
    CHECK(outline > 100);
    snprintf(name, sizeof name, "%s_map_zoomed.png", stem);
    write_png(name, mv->surface);
    /* right click zooms out again (and scrolls) */
    mapview_cell_pos(50, 50, &px, &py);
    click_local(mv, px + 1, py + 1, GFX_MOUSE_RIGHT);
    st = mapview_state();
    CHECK(st.zoom == 0 && st.x0 == 0 && st.y0 == 0);
    CHECK(g_center_calls == c0 + 2 && g_center_x == 50 && g_center_y == 50);
    snprintf(name, sizeof name, "%s_map_scrolled.png", stem);
    write_png(name, mv->surface);

    /* the minimap turns with the main view: all four rotations */
    static const int8_t rot[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    for (int r = 0; r < 4; r++) {
        vt_gfx_rotate(rot[r][0], rot[r][1]);
        mapview_draw();
        int bad = check_zoning(mv, GS_PLANE_HEAVEN);
        CHECK(bad == 0);
        if (r == 1) {
            snprintf(name, sizeof name, "%s_map_rotated.png", stem);
            write_png(name, mv->surface);
        }
    }
    vt_gfx_rotate(1, 1);
    mapview_draw();
}

static void test_drag(void)
{
    WmWindow *mv = mapview_window();
    GfxRect before = mv->rect;
    int x = 120, y = 6; /* the title bar, outside the map */
    to_screen(mv, &x, &y);
    mouse_to(x, y);
    gfx_mouse_on_button(GFX_MOUSE_LEFT, true);
    pump(4);
    CHECK(g_paused == 1);
    mouse_to(x + 40, y + 30);
    gfx_mouse_on_button(GFX_MOUSE_LEFT, false);
    pump(6);
    CHECK(g_paused == 0);
    CHECK(mv->rect.left == before.left + 40 && mv->rect.top == before.top + 30);
    wm_set_rect(mv, &before);
}

/* graph_history_record copies the realm records; graph_history_update
 * appends a year (and every 20 years a 20-year average) */
static void test_history_logic(void)
{
    graph_history_record();
    const PlaneStats *hv = &g_state.plane[3].stats, *hl = &g_state.plane[1].stats;
    CHECK(g_state.summary[0].unk_118[0] == hv->population && g_state.summary[1].unk_118[0] == hl->population);
    CHECK(memcmp(g_state.summary[0].unk_000, hv->unk_77e0, sizeof hv->unk_77e0) == 0);
    CHECK(g_state.summary[1].unk_1a5 == hl->unk_810f && g_state.summary[0].unk_1ad == hv->unk_8127);
    CHECK(g_state.summary[0].unk_19d == (uint32_t)hv->souls.hi && g_state.summary[0].unk_1a1 == hv->souls.lo);

    /* synthetic series: soul rate 100 + year, years 1..60 */
    memset(g_state.rec_5c4eb0, 0, sizeof g_state.rec_5c4eb0);
    for (uint32_t y = 0; y <= 60; y++) {
        g_state.year = y;
        graph_history_add(9, (int32_t)(100 + y), 0);
    }
    const Rec72 *r9 = &g_state.rec_5c4eb0[9], *r10 = &g_state.rec_5c4eb0[10];
    CHECK((int8_t)r9->count == 25);
    CHECK((int32_t)r9->v[24] == 160 && (int32_t)r9->v[0] == 136);
    CHECK((int32_t)r9->min == 136 && (int32_t)r9->max == 160);
    /* the 20-year scale's running sample follows the current value; at
     * year 20 it becomes the average of years 1..20 (110), at 40 of 21..40
     * (130), at 60 of 41..60 (150); then 160 is running */
    printf("  20-year scale: count %d: %d %d %d %d\n", (int8_t)r10->count, (int)r10->v[0], (int)r10->v[1],
           (int)r10->v[2], (int)r10->v[3]);
    CHECK((int8_t)r10->count == 4);
    CHECK((int32_t)r10->v[0] == 110 && (int32_t)r10->v[1] == 130 && (int32_t)r10->v[2] == 150 &&
          (int32_t)r10->v[3] == 160);
    /* large values rescale the whole series */
    g_state.year = 61;
    graph_history_add(9, 1000, 2);
    CHECK(r9->shift == 2 && (int32_t)r9->v[23] == 40 && (int32_t)r9->v[24] == 1000);
}

static void run_save(const char *path)
{
    char err[200];
    if (!world_load(path, err, sizeof err)) {
        fprintf(stderr, "%s: %s\n", path, err);
        g_failures++;
        return;
    }
    tables_init_defaults();
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char stem[64];
    snprintf(stem, sizeof stem, "%.*s", (int)strcspn(base, "."), base);
    printf("%s: \"%s\", year %u, Heaven %u SOULs, Hell %u SOULs\n", base, g_state.hdr.title, (unsigned)g_state.year,
           (unsigned)g_state.plane[3].stats.population, (unsigned)g_state.plane[1].stats.population);
    size_t len0 = 0;
    uint8_t *before = savegame_save_mem(&g_state, &len0);

    view_setup();
    center_on_cell(GS_PLANE_HEAVEN, 50, 50);
    graphview_refresh();
    mapview_refresh();
    test_graphs(stem);
    test_mapview(stem);
    test_drag();
    graphview_refresh();
    char name[128];
    snprintf(name, sizeof name, "%s_screen.png", stem);
    write_png(name, gfx_screen());

    /* the views only read the state */
    size_t len1 = 0;
    uint8_t *after = savegame_save_mem(&g_state, &len1);
    CHECK(before && after && len0 == len1 && memcmp(before, after, len0) == 0);
    free(before);
    free(after);
    test_history_logic();
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: %s <data dir> <bundle> <out dir> <save> ...\n", argv[0]);
        return 2;
    }
    SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1);
    g_out = argv[3];
    crc_init();
    plat_set_data_dir(argv[1]);
    if (!rc_initialize(argv[2], argv[2]))
        return 1;
    if (!plat_init("views_test", W, H) || !gfx_init(W, H, false))
        return 1;
    gfx_mouse_init();
    gfx_font_init();
    gfx_font_load(500);
    gfx_font_load(501);
    size_t plen = 0;
    uint8_t *raw = read_resource(33, &plen);
    uint8_t *rgbx = raw ? gfx_palette_load_resource(raw, plen) : NULL;
    free(raw);
    CHECK(rgbx != NULL);
    if (!rgbx)
        return 1;
    gfx_palette_set(rgbx, GFX_PAL_FIRST, GFX_PAL_COUNT);
    gfx_palette_build_highlight();
    vt_gfx_init(rgbx);
    free(rgbx);

    CHECK(wm_initialize() == 0);
    ctl_initialize();
    ctl_set_pause_flag(&g_paused);
    wm_set_input_live(true);
    GfxRect full = {0, W - 1, 0, H - 1};
    g_main = wm_create_toplevel(&full, main_proc, 0);
    CHECK(g_main != NULL);
    if (!g_main)
        return 1;
    wm_show(g_main, true);
    views_hooks.select_plane = select_plane;
    views_hooks.center_on_cell = center_on_cell;
    views_hooks.help_balloon = help_balloon;
    views_hooks.menu_command = menu_command;

    world_reset();
    view_setup();
    CHECK(graphview_create());
    CHECK(mapview_create());
    WmWindow *gv = graphview_window(), *mv = mapview_window();
    CHECK(gv && mv && !wm_is_visible(gv) && !wm_is_visible(mv));
    if (!gv || !mv)
        return 1;
    printf("Graphview %dx%d, Mapview %dx%d\n", gv->surface->width, gv->surface->height, mv->surface->width,
           mv->surface->height);
    /* side by side: the Mapview top left, the Graphview bottom right */
    wm_move(gv, W - gv->surface->width - 8, H - gv->surface->height - 8);
    wm_show(gv, true);
    wm_show(mv, true);

    for (int i = 4; i < argc; i++)
        run_save(argv[i]);

    /* the close buttons go through the Window menu */
    CHECK(click_button(mv, MAPVIEW_CLOSE));
    CHECK(g_menu_cmd == 0x1a && !wm_is_visible(mv));
    CHECK(click_button(gv, GV_ID_CLOSE));
    CHECK(g_menu_cmd == 0x19 && !wm_is_visible(gv));

    mapview_destroy();
    graphview_destroy();
    ctl_shutdown();
    wm_shutdown();
    vt_gfx_free();
    world_free();
    gfx_font_shutdown();
    gfx_shutdown();
    plat_shutdown();
    rc_shutdown();
    printf("%d failures\n", g_failures);
    return g_failures != 0;
}
