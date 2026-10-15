/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The Mapview (0x463b70-0x46788f): see ui/mapview.h. */
#include "ui/mapview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/realm.h"
#include "game/tables.h"
#include "game/util.h"
#include "game/world.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "ui/controls.h"
#include "ui/mapview_int.h"
#include "ui/views.h"

#define FONT 500
#define BG 0x13
#define MAP_X MAPVIEW_MAP_X
#define MAP_Y MAPVIEW_MAP_Y

static WmWindow *g_win;              /* 0x630270 */
static WmWindow *g_btn[11];          /* 0x4f31c8: [0] close, [1..8] layers, [9] Heaven, [10] Hell */
static int g_layer = MAPVIEW_ZONING; /* 0x6607b0 */
static int g_heaven = 1;             /* 0x660770 */
static uint8_t g_zoom;               /* 0x6607b4 */
static uint8_t g_x0, g_y0;           /* 0x6607b8, 0x6607bc */
static int g_dragging;               /* 0x6607c0 */
static int g_grab_x, g_grab_y;       /* 0x4f31f4, 0x4f31c0 */

/* window areas (inclusive): legend 0x660778, view title 0x660788, layer title 0x660798 */
static const GfxRect legend_area = {0xe0, 0x151, 0x81, 0xde};
static const GfxRect title_area = {0x0f, 0xcd, 0x10, 0x17};
static const GfxRect layer_area = {0xe0, 0x14e, 0x6c, 0x73};

/* layer colours, 0x660700.. */
static const uint8_t col_zoning[9] = {0x0a, 0x0b, 0x0d, 0x1d, 0x0f, 0x0c, 0x0e, 0x23, 0x62};
static const uint8_t col_traffic[8] = {0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57};
static const uint8_t col_capacity[8] = {0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f};
static const uint8_t col_efficiency[8] = {0x7f, 0x7e, 0x7d, 0x7c, 0x7b, 0x7a, 0x79, 0x78};
static const uint8_t col_tempperm[8] = {0x59, 0x5b, 0x5d, 0x5e, 0x45, 0x44, 0x42, 0x40};
static const uint8_t col_vibes_heaven[9] = {0x40, 0x42, 0x44, 0x45, 0x13, 0x5e, 0x5d, 0x5b, 0x59};
static const uint8_t col_vibes_hell[9] = {0x59, 0x5b, 0x5d, 0x5e, 0x13, 0x45, 0x44, 0x42, 0x40};
static const uint8_t col_karma[6] = {0x48, 0x23, 0x7c, 0x41, 0x58, 0x0d};
static const uint8_t col_rocks[5] = {0x0a, 0x0e, 0x24, 0x0b, 0x0d};

static int cells_shown(void) { return 100 >> g_zoom; }
static int cell_size(void) { return 2 << g_zoom; }

static void font_colors(unsigned c1, unsigned c2)
{
    unsigned c[2] = {c1 & 0xff, c2 & 0xff};
    gfx_font_set_colors(FONT, 1, 2, c);
}

static void fill_rect(const GfxRect *r, uint8_t c)
{
    gfx_fill_rect(g_win->surface, r->left, r->top, r->right - r->left + 1, r->bottom - r->top + 1, c);
}

/* ---- cells of the current plane (bounds-checked; the original passes
 * bytes and reads whatever is there) ---- */

static uint32_t cell_lin(int x, int y)
{
    int i = y * GS_MAP_W + x;
    if (!g_world.cells || i < 0 || i >= GS_MAP_CELLS)
        return 0;
    return g_world.cells[i];
}

static uint32_t cell_raw(int x, int y)
{
    if (x < 0 || x >= GS_MAP_W || y < 0 || y >= GS_MAP_H)
        return 0;
    return cell_lin(x, y);
}

static const SimCell *sim_raw(int x, int y)
{
    static const SimCell zero;
    if (!g_world.sim || x < 0 || x >= GS_MAP_W || y < 0 || y >= GS_MAP_H)
        return &zero;
    return &g_world.sim[y * GS_MAP_W + x];
}

/* 0x46666d: the origin of the building covering (x, y) */
static void origin_xy(int *x, int *y)
{
    if (cell_raw(*x, *y) >> 27 == 0) {
        const SimCell *s = sim_raw(*x, *y);
        *x = s->b[10];
        *y = s->b[11];
    }
}

/* 0x466575: tile set of the origin cell */
static unsigned origin_set(int x, int y)
{
    origin_xy(&x, &y);
    return cell_raw(x, y) >> 27;
}

/* 0x4665f6: entry index (bits 16-22) of the origin cell */
static unsigned origin_index(int x, int y)
{
    origin_xy(&x, &y);
    return (cell_raw(x, y) >> 16) & 0x7f;
}

/* origin is a zone building cell the population functions can walk */
static bool origin_ok(int x, int y)
{
    origin_xy(&x, &y);
    return x < GS_MAP_W && y < GS_MAP_H;
}

/* 0x466d0b: window position of a cell's square, in bytes like the original */
void mapview_cell_pos(int cx, int cy, int *px, int *py)
{
    int dir_x, dir_y, w, h;
    mapview_view_geometry(&dir_x, &dir_y, &w, &h);
    uint32_t b1 = (uint8_t)(cx - g_x0), b2 = (uint8_t)(cy - g_y0);
    uint32_t n3 = ((uint32_t)(uint8_t)w >> g_zoom) & 0xff, n4 = ((uint32_t)(uint8_t)h >> g_zoom) & 0xff;
    unsigned z = g_zoom;
    uint32_t x, y;
    if (dir_x < 0) {
        if (dir_y < 0) {
            x = ((n3 - b1) * 2 - 2) << z;
            y = ((n4 - b2) * 2 - 2) << z;
        } else {
            x = (b2 * 2) << z;
            y = ((n4 - b1) * 2 - 2) << z;
        }
    } else if (dir_y < 0) {
        x = ((n3 - b2) * 2 - 2) << z;
        y = (b1 * 2) << z;
    } else {
        x = (b1 * 2) << z;
        y = (b2 * 2) << z;
    }
    *px = (int32_t)(x + MAP_X);
    *py = (int32_t)(y + MAP_Y);
}

/* 0x466eb0 */
bool mapview_cell_at(int px, int py, int *cx, int *cy)
{
    int n = cells_shown(), s = cell_size();
    GfxRect r = {MAP_X, s * n + MAP_X - 1, MAP_Y, s * n + MAP_Y - 1};
    if (!gfx_rect_contains(&r, px, py)) {
        *cx = *cy = 0;
        return false;
    }
    int dir_x, dir_y, w, h;
    mapview_view_geometry(&dir_x, &dir_y, &w, &h);
    int n5 = ((uint8_t)w >> g_zoom) & 0xff, n6 = ((uint8_t)h >> g_zoom) & 0xff;
    int i = (px - MAP_X) / s, j = (py - MAP_Y) / s;
    if (dir_x < 0) {
        if (dir_y < 0)
            *cx = n5 - i - 1, *cy = n6 - j - 1;
        else
            *cx = n6 - j - 1, *cy = i;
    } else if (dir_y < 0)
        *cx = j, *cy = n5 - i - 1;
    else
        *cx = i, *cy = j;
    *cx += g_x0;
    *cy += g_y0;
    return true;
}

static void plot(int x, int y, uint8_t c)
{
    int px, py, s = cell_size();
    mapview_cell_pos(x, y, &px, &py);
    gfx_fill_rect(g_win->surface, px, py, s, s, c);
}

/* water: shallow / deep by entry */
static uint8_t water_colour(int x, int y) { return origin_index(x, y) < 0x12 ? 0x41 : 0x79; }

/* ---- legend ---------------------------------------------------------------- */

/* 0x466745: the legend (colour squares and labels) and, with a layer title,
 * the titles */
static void legend(const char *const *labels, const uint8_t *colours, int n, const char *title)
{
    int fh = gfx_font_height(FONT);
    GfxRect r = {legend_area.left + 0xc, legend_area.right + 0xc - 0xd, legend_area.top, legend_area.top + fh - 1};
    gfx_font_select(FONT);
    font_colors(0x1a, 0x1a);
    if (title) {
        GfxRect a = layer_area;
        fill_rect(&layer_area, BG);
        gfx_font_draw_text(g_win->surface, a.left, a.top, &a, &a, title, GFX_TEXT_CENTER);
        char buf[40];
        snprintf(buf, sizeof buf, g_zoom == 0 ? "%s - Normal View" : "%s - Zoomed In", g_heaven ? "Heaven" : "Hell");
        font_colors(g_heaven ? 0x7b : 0x40, g_heaven ? 0x7b : 0x40);
        a = title_area;
        fill_rect(&title_area, BG);
        gfx_font_draw_text(g_win->surface, a.left, a.top, &a, &a, buf, GFX_TEXT_CENTER);
    }
    font_colors(0x1a, 0x1a);
    for (int i = 0; i < n; i++) {
        gfx_fill_rect(g_win->surface, legend_area.left, r.top, 10, 10, colours[i]);
        if (labels) {
            GfxRect a = r;
            gfx_font_draw_text(g_win->surface, a.left, a.top, &a, &a, labels[i], 0);
        }
        r.top += 10;
        r.bottom += 10;
    }
}

/* ---- the layers ------------------------------------------------------------ */

#define FOR_CELLS(x, y)                                                                                                \
    for (int x = g_x0; x < cells_shown() + g_x0; x++)                                                                  \
        for (int y = g_y0; y < cells_shown() + g_y0; y++)

/* 0x465021: share of temporary SOULs (the legend's ends are labelled the
 * other way round in the original) */
static void layer_temp_perm(void)
{
    static const char *const labels[8] = {"All Permament", "", "", "", "", "", "", "All Temporary"};
    FOR_CELLS(x, y)
    {
        uint8_t c;
        switch (origin_set(x, y)) {
        case 1:
            continue;
        case 2:
            c = water_colour(x, y);
            break;
        case 5: {
            int ox = x, oy = y;
            origin_xy(&ox, &oy);
            const SimCell *s = sim_raw(ox, oy);
            uint32_t a = sim_cell_u32(s, 1), t = sim_cell_u32(s, 5) + sim_cell_u32(s, 9);
            if (t == 0) {
                if (a == 0)
                    continue;
                c = col_tempperm[0];
            } else
                c = col_tempperm[mul_div_clamp(t, t + a, 7)];
            break;
        }
        case 14:
        case 15: {
            if (!origin_ok(x, y))
                continue;
            uint32_t temp = realm_building_temp(x, y), perm = realm_building_perm(x, y);
            uint32_t pop = realm_building_pop(x, y);
            if (perm == 0) {
                if (temp == 0)
                    continue;
                c = col_tempperm[0];
            } else
                c = col_tempperm[mul_div_clamp(perm, pop, 7)];
            break;
        }
        default:
            c = 0x23;
            break;
        }
        plot(x, y, c);
    }
    legend(labels, col_tempperm, 8, "Temp/Perm Ratio");
}

/* 0x46536b: zone building efficiency (sim cell +1 high nibble, 0..14) */
static void layer_efficiency(void)
{
    static const char *const labels[8] = {"F-/F", "F+/D-", "D/D+", "C-/C", "C+/B-", "B/B+", "A-/A", "A+"};
    FOR_CELLS(x, y)
    {
        uint8_t c;
        switch (origin_set(x, y)) {
        case 1:
            continue;
        case 2:
            c = water_colour(x, y);
            break;
        case 14:
        case 15: /* 0x43077b */
            c = col_efficiency[mul_div_clamp(sim_raw(x, y)->b[1] >> 4, 0xe, 7)];
            break;
        default:
            c = 0x23;
            break;
        }
        plot(x, y, c);
    }
    legend(labels, col_efficiency, 8, "Efficiency");
}

/* 0x4643e0: the vibes (map cell bits 8-12, -15..16, negated in Hell) */
static void layer_vibes(void)
{
    static const char *const labels[9] = {"Very Bad Vibes",      "", "Slightly Bad Vibes", "", "No Vibes", "",
                                          "Slightly Good Vibes", "", "Very Good Vibes"};
    const uint8_t *tab = g_heaven ? col_vibes_heaven : col_vibes_hell;
    FOR_CELLS(x, y)
    {
        int v = (int8_t)((((cell_raw(x, y) & 0x1f00) >> 8) - 0xf) & 0xff); /* 0x430737 */
        if (!g_heaven)
            v = -v;
        v += 0x10;
        uint8_t c = tab[mul_div_clamp((uint32_t)v, 0x20, 8)];
        unsigned set = origin_set(x, y);
        if (set == 12)
            c = 0x23;
        else if (v == 0x10 && set == 2)
            c = water_colour(x, y);
        plot(x, y, c);
    }
    legend(labels, tab, 9, "Vibes");
}

static uint32_t spec_capacity(const BuildingSpec *t, size_t n, unsigned i) { return i < n ? t[i].capacity : 0; }
#define CAP(tab, i) spec_capacity(g_tables.tab, sizeof g_tables.tab / sizeof g_tables.tab[0], (i))

/* 0x4645f6: how full buildings are */
static void layer_capacity(void)
{
    static const char *const labels[8] = {"Vacant", "", "Roomy", "", "", "Cramped", "", "No Vacancy"};
    FOR_CELLS(x, y)
    {
        int ox = x, oy = y;
        origin_xy(&ox, &oy);
        unsigned set = origin_set(x, y), idx = origin_index(x, y);
        const SimCell *s = sim_raw(ox, oy);
        uint32_t pop, cap;
        uint8_t c;
        switch (set) {
        case 1:
            continue;
        case 2:
            c = idx < 0x12 ? 0x41 : 0x79;
            break;
        case 5:
            pop = sim_cell_u32(s, 5) + sim_cell_u32(s, 9) + sim_cell_u32(s, 1);
            cap = CAP(set5, idx);
            c = col_capacity[mul_div_clamp(pop, cap, 7)];
            break;
        case 7:
            c = col_capacity[mul_div_clamp(sim_cell_u32(s, 1), CAP(set7, idx), 7)];
            break;
        case 8:
            c = col_capacity[mul_div_clamp(sim_cell_u32(s, 5), CAP(set8, idx), 7)];
            break;
        case 9:
            c = col_capacity[mul_div_clamp(sim_cell_u32(s, 5), CAP(set9, idx), 7)];
            break;
        case 10:
        case 11:
        case 12:
        case 16:
        case 18:
            c = 0x23;
            break;
        case 13:
            c = col_capacity[mul_div_clamp(sim_cell_u32(s, 5), CAP(set13, idx >> 1), 7)];
            break;
        case 17:
            c = col_capacity[mul_div_clamp(sim_cell_u32(s, 5), CAP(set17, s->b[10]), 7)];
            break;
        default:
            /* zones and everything else: building population against the
             * realm tile table's +0 (filled by 0x452581, WP2) */
            pop = origin_ok(x, y) ? realm_building_pop(x, y) : 0;
            cap = g_world.tile_info && idx < REALM_TILE_TYPES ? g_world.tile_info[idx].cost : 0;
            c = col_capacity[mul_div_clamp(pop, cap, 7)];
            break;
        }
        plot(x, y, c);
    }
    legend(labels, col_capacity, 8, "Capacity");
}

/* 0x465a31: zone kinds */
static void layer_zoning(void)
{
    static const char *const hell[9] = {"Envy",  "Greed", "Gluttony", "Sloth",    "Lust",
                                        "Anger", "Pride", "Generic",  "Omnibolge"};
    static const char *const heaven[9] = {"Contentment",  "Charity",  "Temperance", "Diligence", "Chastity",
                                          "Peacefulness", "Humility", "Generic",    "Love Dome"};
    FOR_CELLS(x, y)
    {
        unsigned set = origin_set(x, y), idx = origin_index(x, y);
        uint8_t c;
        switch (set) {
        case 1:
            continue;
        case 2:
            c = idx < 0x12 ? 0x41 : 0x79;
            break;
        case 5:
            c = col_zoning[8];
            break;
        case 14:
        case 15: {
            unsigned k = idx / 13;
            c = col_zoning[k > 6 ? 7 : k];
            break;
        }
        default:
            c = 0x2b;
            break;
        }
        plot(x, y, c);
    }
    legend(g_heaven ? heaven : hell, col_zoning, 9, "Zoning");
}

/* 0x4666de: bucket 0..n-1 of v by powers of 2^(32/n) */
static int log_bucket(uint32_t v, uint8_t n)
{
    uint32_t c = 0x80000000u;
    unsigned step = 32u / n;
    for (;;) {
        n--;
        if (n == 0 || v >= c)
            return n;
        c >>= step & 31;
    }
}

/* 0x46582f: traffic on roads (sim cell +9) */
static void layer_traffic(void)
{
    static const char *const labels[8] = {"Light", "", "", "Moderate", "", "Congested", "", "L.A. Rush Hour"};
    FOR_CELLS(x, y)
    {
        unsigned set = origin_set(x, y);
        uint8_t c;
        if (set == 1)
            continue;
        if (set == 2)
            c = water_colour(x, y);
        else if (set == 12)
            c = col_traffic[log_bucket(sim_cell_u32(sim_raw(x, y), 9), 8)];
        else
            c = 0x23;
        plot(x, y, c);
    }
    legend(labels, col_traffic, 8, "Traffic");
}

/* 0x464b0c: Karma track and stations of the middle plane and both realms */
static void layer_karma(void)
{
    static const char *const labels[6] = {"PORTAL",         "TRACK",           "STATION - Heaven",
                                          "STATION - Hell", "ANCHOR - Heaven", "ANCHOR - Hell"};
    int prev = views_select_plane(GS_PLANE_MIDDLE);
    FOR_CELLS(x, y)
    {
        unsigned set = origin_set(x, y);
        if (set == 16)
            plot(x, y, col_karma[1]);
        else if (set == 18)
            plot(x, y, col_karma[0]);
    }
    views_select_plane(GS_PLANE_HEAVEN);
    FOR_CELLS(x, y)
    {
        unsigned set = origin_set(x, y);
        if (set == 16)
            plot(x, y, col_karma[4]);
        else if (set == 17)
            plot(x, y, col_karma[2]);
    }
    views_select_plane(GS_PLANE_HELL);
    FOR_CELLS(x, y)
    {
        unsigned set = origin_set(x, y);
        if (set == 16)
            plot(x, y, col_karma[5]);
        else if (set == 17)
            plot(x, y, col_karma[3]);
    }
    gfx_font_select(FONT);
    font_colors(0x1a, 0x1a);
    fill_rect(&layer_area, BG);
    fill_rect(&title_area, BG);
    GfxRect a = layer_area;
    gfx_font_draw_text(g_win->surface, a.left, a.top, &a, &a, "Track", GFX_TEXT_CENTER);
    a = title_area;
    gfx_font_draw_text(g_win->surface, a.left, a.top, &a, &a, g_zoom == 0 ? "Karma - Normal View" : "Karma - Zoomed In",
                       GFX_TEXT_CENTER);
    views_select_plane(prev);
    legend(labels, col_karma, 6, NULL);
}

/* 0x465574: Ad Infinitum rocks (set 10), tapped next to a siphon (29) */
static void layer_rocks(void)
{
    static const char *const labels[5] = {"Empowered", "Unempowered", "Untapped Rocks", "Tapped Rocks", "Siphon"};
    FOR_CELLS(x, y)
    {
        unsigned set = origin_set(x, y);
        uint8_t c;
        bool power = (sim_raw(x, y)->b[0] & 0x10) != 0; /* 0x43094c */
        switch (set) {
        case 1:
            continue;
        case 2:
            c = power ? col_rocks[0] : water_colour(x, y);
            break;
        case 10:
            c = col_rocks[2];
            /* the original loops over bytes: nothing for x or y = 0 */
            for (unsigned nx = (uint8_t)(x - 1); nx <= (unsigned)x + 1; nx++)
                for (unsigned ny = (uint8_t)(y - 1); ny <= (unsigned)y + 1; ny++)
                    if (cell_lin((int)nx, (int)ny) >> 27 == 0x1d)
                        c = col_rocks[3];
            break;
        case 0x1d:
            c = col_rocks[4];
            break;
        default:
            c = power ? col_rocks[0] : col_rocks[1];
            break;
        }
        plot(x, y, c);
    }
    legend(labels, col_rocks, 5, "Ad Infinitum Charge");
}

/* ---- drawing and the window -------------------------------------------------- */

/* 0x464105 */
void mapview_draw(void)
{
    if (!g_win)
        return;
    int prev = views_select_plane(g_heaven ? GS_PLANE_HEAVEN : GS_PLANE_HELL);
    gfx_fill_rect(g_win->surface, MAP_X, MAP_Y, MAPVIEW_MAP_SIZE, MAPVIEW_MAP_SIZE, BG);
    fill_rect(&legend_area, BG);
    bool realms = true;
    switch (g_layer) {
    case MAPVIEW_TEMP_PERM:
        layer_temp_perm();
        break;
    case MAPVIEW_EFFICIENCY:
        layer_efficiency();
        break;
    case MAPVIEW_VIBES:
        layer_vibes();
        break;
    case MAPVIEW_CAPACITY:
        layer_capacity();
        break;
    case MAPVIEW_ZONING:
        layer_zoning();
        break;
    case MAPVIEW_TRAFFIC:
        layer_traffic();
        break;
    case MAPVIEW_KARMA:
        realms = false;
        layer_karma();
        break;
    case MAPVIEW_ROCKS:
        layer_rocks();
        break;
    default:
        views_select_plane(prev);
        return;
    }
    /* the realm buttons don't apply to the Karma layer */
    ctl_button_show(g_btn[9], realms);
    ctl_button_show(g_btn[10], realms);
    for (int i = 9; i < 11; i++) {
        WmWindow *b = g_btn[i];
        if (!b)
            continue;
        if (!realms)
            gfx_fill_rect(g_win->surface, b->rect.left, b->rect.top, b->rect.right - b->rect.left + 1,
                          b->rect.bottom - b->rect.top + 1, 100);
        else
            ctl_button_draw(b, (unsigned)wm_get_data(b, CTL_BUTTON_STATE) | CTL_DRAW_BITMAP);
    }
    mapview_outline(g_win->surface, g_zoom, g_x0, g_y0);
    views_select_plane(prev);
    wm_redraw(g_win, NULL);
}

/* 0x4640c9 */
void mapview_refresh(void)
{
    if (wm_is_visible(g_win) && !wm_is_minimized(g_win))
        mapview_draw();
}

/* 0x466c35: zoom in (to 1) or out by one step, centred on (cx, cy) */
static void zoom(int in, int cx, int cy)
{
    if (!in) {
        if (g_zoom != 0)
            g_zoom--;
    } else if (g_zoom == 0)
        g_zoom = 1;
    int16_t n = (int16_t)(100 >> g_zoom);
    cx -= n >> 1;
    if (cx < 1)
        cx = 0;
    if (cx > 100 - n)
        cx = 100 - n;
    cy -= n >> 1;
    if (cy < 1)
        cy = 0;
    if (cy > 100 - n)
        cy = 100 - n;
    g_x0 = (uint8_t)cx;
    g_y0 = (uint8_t)cy;
}

void mapview_zoom(int in, int cx, int cy) { zoom(in, cx, cy); }

static void center_main(int cx, int cy)
{
    int plane = g_layer == MAPVIEW_KARMA ? GS_PLANE_MIDDLE : g_heaven ? GS_PLANE_HEAVEN : GS_PLANE_HELL;
    if (views_hooks.center_on_cell)
        views_hooks.center_on_cell(plane, cx, cy);
}

/* 0x4675e0: right click on a button */
static void mapview_help(WmWindow *c)
{
    static const char *const layers[8] = {
        "This button sets the Mapview to display the PERMANENT/TEMPORARY POPULATION RATIOS of the currently "
        "selected realm.",
        "This button sets the Mapview to display the EFFECTIVENESS LEVELS of the currently selected realm.",
        "This button sets the Mapview to display the various INFLUENCES at work in the currently selected realm.",
        "This button sets the Mapview to display the FULLNESS of the currently selected realm.",
        "This button sets the Mapview to display the ZONING of the currently selected realm.",
        "This button sets the Mapview to display the TRAFFIC LEVELS of the currently selected realm.",
        "This button sets the Mapview to display KARMA TRACK AND STATION INFORMATION.",
        "This button sets the Mapview to display the AD INFINITUM LEVELS in the currently selected realm."};
    int id = (int)wm_get_data(c, 0);
    const char *t = "I have no idea what this button does.";
    if (id == MAPVIEW_CLOSE)
        t = "This button CLOSES the Mapview.";
    else if (id == 1)
        t = wm_is_minimized(c->parent) ? "This button MAXIMIZES the window" : "This button MINIMIZES the window";
    else if (id >= MAPVIEW_TEMP_PERM && id <= MAPVIEW_ROCKS)
        t = layers[id - MAPVIEW_TEMP_PERM];
    else if (id == MAPVIEW_HEAVEN)
        t = "This button toggles the Mapview to observe HEAVEN.";
    else if (id == MAPVIEW_HELL)
        t = "This button toggles the Mapview to observe HELL.";
    views_control_help(c, t);
}

/* 0x463cd0 mapview_proc */
static intptr_t mapview_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    int cx, cy;
    switch (msg) {
    case WM_MSG_MOUSEMOVE:
        if (g_dragging) {
            GfxRect r = w->rect;
            rect_move_to(&r, (int)a - g_grab_x, (int)b - g_grab_y);
            wm_local_to_screen(w, &r);
            if (w->parent) /* (the original passes screen coordinates; its parent is at 0, 0) */
                wm_screen_to_local(w->parent, &r);
            wm_set_rect(w, &r);
        }
        break;
    case WM_MSG_LBUTTONDOWN:
        wm_set_capture(w);
        wm_raise(w);
        g_grab_x = (int)a;
        g_grab_y = (int)b;
        if (!mapview_cell_at((int)a, (int)b, &cx, &cy)) {
            g_dragging = 1;
            views_set_paused(1);
        } else {
            g_dragging = 0;
            zoom(1, cx, cy);
            mapview_draw();
            center_main(cx, cy);
        }
        break;
    case WM_MSG_RBUTTONDOWN:
        if (mapview_cell_at((int)a, (int)b, &cx, &cy)) {
            zoom(0, cx, cy);
            mapview_draw();
            center_main(cx, cy);
        }
        break;
    case WM_MSG_LBUTTONUP:
        wm_release_capture();
        g_dragging = 0;
        views_set_paused(0);
        break;
    case WM_MSG_CTL_CLICK: {
        int id = (int)ctl_get_id(w);
        if (id != 1 && w->parent)
            wm_raise(w->parent);
        if (id == MAPVIEW_CLOSE) {
            if (views_hooks.menu_command)
                views_hooks.menu_command(0x1a);
            else if (g_win)
                wm_show(g_win, false);
        } else if (id >= MAPVIEW_TEMP_PERM && id <= MAPVIEW_ROCKS) {
            if (g_layer != id) {
                ctl_button_set_pressed(g_btn[g_layer - MAPVIEW_CLOSE], false, true);
                g_layer = id;
                mapview_draw();
            }
        } else if (id == MAPVIEW_HEAVEN) {
            g_heaven = 1;
            ctl_button_set_pressed(g_btn[10], false, true);
            mapview_draw();
        } else if (id == MAPVIEW_HELL) {
            g_heaven = 0;
            ctl_button_set_pressed(g_btn[9], false, true);
            mapview_draw();
        }
        break;
    }
    case WM_MSG_CTL_HOVER:
        if (wm_get_mode() == 0)
            break;
        /* fall through */
    case WM_MSG_CTL_RDOWN:
        if (w->parent == g_win)
            mapview_help(w);
        break;
    case WM_MSG_SHOW:
        if (a != 0 && w == g_win)
            mapview_draw();
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

/* 0x4669ba: the buttons */
static bool create_buttons(WmWindow *w)
{
    static const uint32_t bmp[11] = {0x259, 400, 0x191, 0x192, 0x193, 0x6e, 0x70, 0x72, 0x194, 0x7e, 0x7d};
    static const int16_t pos[11][2] = {{0, 0},        {0x106, 0x17}, {0x106, 0x2f}, {0x106, 0x47},
                                       {0x11e, 0x23}, {0x11e, 0x3c}, {0x136, 0x17}, {0x136, 0x2f},
                                       {0x136, 0x47}, {0xe2, 0x23},  {0xe2, 0x3c}};
    for (int i = 0; i < 11; i++) {
        GfxSurface *s = gfx_load_bmp_resource(bmp[i], NULL);
        if (!s)
            continue;
        GfxRect r = {pos[i][0], s->width + pos[i][0] - 1, pos[i][1], s->height + pos[i][1] - 1};
        uint32_t style = i == 0 ? CTL_STYLE_BITMAP : CTL_STYLE_BITMAP | CTL_STYLE_STICKY;
        g_btn[i] = ctl_button_create(w, &r, (uint32_t)(MAPVIEW_CLOSE + i), style, s);
        if (!g_btn[i])
            gfx_surface_destroy(s);
        if (bmp[i] == 0x259)
            w->close_button = g_btn[i];
    }
    ctl_button_set_pressed(g_btn[g_layer - MAPVIEW_CLOSE], true, true);
    ctl_button_set_pressed(g_btn[g_heaven ? 9 : 10], true, true);
    return true;
}

/* 0x463b70 */
bool mapview_create(void)
{
    if (g_win)
        return true; /* (the original sends it message 0x400) */
    GfxSurface *bg = gfx_load_bmp_resource(0x25c, NULL);
    if (!bg)
        return false;
    g_zoom = g_x0 = g_y0 = 0;
    WmWindow *mw = views_main_window();
    int left = mw ? mw->client.left : 0, top = mw ? mw->client.top : 0;
    GfxRect r = {left, bg->width + left - 1, top, bg->height + top - 1};
    g_win = wm_create_window(mw, &r, mapview_proc, WM_F_MINIMIZABLE | WM_F_NOFOCUS, 0);
    if (!g_win) {
        gfx_surface_destroy(bg);
        return false;
    }
    wm_set_title(g_win, "Mapview");
    gfx_blit(g_win->surface, 0, 0, bg->width, bg->height, bg, 0, 0);
    gfx_surface_destroy(bg);
    if (!create_buttons(g_win))
        return false;
    views_draw_title(g_win);
    return true;
}

void mapview_destroy(void)
{
    if (g_win) {
        wm_show(g_win, false);
        wm_destroy(g_win);
    }
    g_win = NULL;
    memset(g_btn, 0, sizeof g_btn);
    g_dragging = 0;
}

WmWindow *mapview_window(void) { return g_win; }

MapviewState mapview_state(void) { return (MapviewState){g_layer, g_heaven != 0, g_zoom, g_x0, g_y0}; }
