/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The Microview (0x4678f4-0x46fd48): see ui/microview.h. THINGS TO KNOW:
 * microview_info.c. */
#include "ui/microview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "formats/tile.h"
#include "game/build.h"
#include "game/realm.h"
#include "game/structure.h"
#include "game/tables.h"
#include "game/util.h"
#include "game/world.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/tile_draw.h"
#include "ui/controls.h"
#include "ui/exe_text.h"
#include "ui/microview_int.h"
#include "ui/views.h"

#define FONT 500
#define BIG_FONT 0x1f5
#define BG 0x13

MicroviewHooks microview_hooks;

static WmWindow *g_win;                                           /* 0x5c6670 */
static WmWindow *g_viewer;                                        /* 0x6611fc: the description */
static WmWindow *g_btn[MICROVIEW_ACTIVE - MICROVIEW_PICTURE + 1]; /* 0x638f70..: by id - 400 */
static GfxSurface *g_strip;                     /* 0x638fb4: 8 rows of bitmap 600 (window extension) */
static GfxSurface *g_bottom;                    /* 0x638eb4: its last 0x5a rows */
static GfxSurface *g_banner;                    /* 0x638f10: under the structure's name */
static GfxSurface *g_knob_heaven, *g_knob_hell; /* 0x638eec (0x11e), 0x638ef4 (0x11c) */
static GfxSurface *g_under1, *g_under2;         /* 0x638f94, 0x638edc */
static GfxSurface *g_panel;                     /* 0x638ee0: bitmap 0x120 */
static GfxSurface *g_zone_panel;                /* 0x638f50: 0x123 */
static GfxSurface *g_tile;                      /* 0x638f20: 0x124 */
static GfxSurface *g_up_panel;                  /* 0x638eb8: 0x122 */
static GfxSurface *g_sl_panel;                  /* 0x638f18: 0x121 */

static int g_page = MICROVIEW_PICTURE;                         /* 0x638f14 */
static int8_t g_x = -1, g_y = -1;                              /* 0x638ed8, 0x638ed9 */
static uint8_t g_plane;                                        /* 0x638ef1 */
static uint32_t g_cached;                                      /* 0x638f1c: the map cell drawn */
static uint8_t g_set = 0xff;                                   /* 0x6611f4 */
static uint8_t g_index;                                        /* 0x638fd0 */
static int8_t g_zoom = 1;                                      /* 0x6611f0 */
static int g_rows = 1;                                         /* 0x6611ec */
static unsigned g_artist = 1;                                  /* 0x661200: news id of the artist's rendering */
static uint32_t g_up_cost;                                     /* 0x6611f8 */
static uint8_t g_up_size;                                      /* 0x638ee4 */
static uint8_t g_up_index;                                     /* 0x638ef0 */
static int g_k1x = -1, g_k1y = -1;                             /* 0x638ed0 / 0x638ed4: funding knob */
static int g_k2x = -1, g_k2y = -1;                             /* 0x638eac / 0x638eb0: acceptance knob */
static int g_knob_x10 = -1, g_knob_y;                          /* 0x4f3220, 0x4f3224 */
static int g_drag_win, g_drag_fund, g_drag_train, g_drag_omni; /* 0x66120c .. 0x661218 */
static int g_grab_x, g_grab_y;                                 /* 0x4f31f8, 0x4f320c */
static int g_secret_busy;                                      /* 0x661204 */

/* rectangles (inclusive) */
static const GfxRect r_focus = {0x40, 0xb7, 0x5d, 0x7d};   /* 0x638fc0 */
static const GfxRect r_accept = {0x24, 0xdc, 0x9b, 0xbd};  /* 0x638f60 */
static const GfxRect r_popbar = {0x15, 0xe2, 0x9c, 0xac};  /* 0x638f30 */
static const GfxRect r_fund = {0, 0, 0, 0};                /* 0x638ec0, never set */
static const GfxRect r_viewer = {0x12, 0xee, 0x3c, 0x163}; /* 0x638f00 */

/* dark / light colours of the population bars by kind (0x6611d0 / 0x6611e0) */
static const uint8_t k_dark[8] = {0x0a, 0x4d, 0x56, 0x67, 0x36, 0x44, 0x7e, 0x2b};
static const uint8_t k_light[8] = {0x58, 0x0b, 0x0d, 0x62, 0x30, 0x0c, 0x0e, 0x24};

WmWindow *microview_win(void) { return g_win; }
WmWindow *microview_window(void) { return g_win; }
int microview_cell_plane(void) { return g_plane; }
int microview_slider_x(void) { return g_knob_x10; }

MicroviewState microview_state(void)
{
    return (MicroviewState){g_page, g_x, g_y, g_plane, g_set, g_index, g_zoom, g_rows};
}

static WmWindow *btn(int id)
{
    return id >= MICROVIEW_PICTURE && id <= MICROVIEW_ACTIVE ? g_btn[id - MICROVIEW_PICTURE] : NULL;
}

static void font_colors(unsigned c1, unsigned c2)
{
    unsigned c[2] = {c1 & 0xff, c2 & 0xff};
    gfx_font_set_colors(FONT, 1, 2, c);
}

static void fill(int x, int y, int w, int h, uint8_t c)
{
    if (g_win && w > 0 && h > 0)
        gfx_fill_rect(g_win->surface, x, y, w, h, c);
}

static void redraw(int l, int r, int t, int b)
{
    GfxRect a = {l, r, t, b};
    if (g_win)
        wm_redraw(g_win, &a);
}

static void redraw_rect(const GfxRect *r)
{
    if (g_win)
        wm_redraw(g_win, r);
}

static void sound(uint32_t id)
{
    if (microview_hooks.sound)
        microview_hooks.sound(id, 2);
}

/* a message box (microview_hooks.message, else ctl_message_box) */
static int ask(const char *text, uint32_t buttons_va, int n)
{
    char b[64];
    exe_buttons(buttons_va, n, b, sizeof b);
    if (microview_hooks.message)
        return microview_hooks.message(text, b);
    return ctl_message_box(-1, -1, NULL, text, b, 0);
}

static void text_at(int x, int y, const GfxRect *clip, const char *s, unsigned flags)
{
    if (!g_win)
        return;
    GfxRect a = *clip;
    gfx_font_draw_text(g_win->surface, x, y, &a, &a, s, flags);
}

static void button_hide(int id)
{
    WmWindow *b = btn(id);
    if (!b)
        return;
    ctl_button_enable(b, false);
    ctl_button_show(b, false);
}

static void button_show(int id)
{
    WmWindow *b = btn(id);
    if (!b)
        return;
    ctl_button_enable(b, true);
    ctl_button_show(b, true);
}

static void press(int id, bool on)
{
    if (btn(id))
        ctl_button_set_pressed(btn(id), on, true);
}

static void label(int id, uint32_t va)
{
    if (btn(id))
        ctl_button_set_label(btn(id), exe_str(va), FONT, 0x131a13, true);
}

/* the cell's sim record (the cell's plane must be selected) */
static SimCell *cell_sim(void) { return sim_cell((uint8_t)g_x % GS_MAP_W, (uint8_t)g_y % GS_MAP_H); }

static int select_cell_plane(void)
{
    int prev = g_world.plane;
    if (prev != g_plane)
        views_select_plane(g_plane);
    return prev;
}

static void restore_plane(int prev)
{
    if (g_world.plane != prev)
        views_select_plane(prev);
}

static uint32_t cap_of(const BuildingSpec *t, size_t n, unsigned i) { return i < n ? t[i].capacity : 0; }
#define CAP(tab, i) cap_of(g_tables.tab, sizeof g_tables.tab / sizeof g_tables.tab[0], (i))
static const BuildingSpec *spec(const BuildingSpec *t, size_t n, unsigned i)
{
    static const BuildingSpec none;
    return i < n ? &t[i] : &none;
}
#define SPEC(tab, i) spec(g_tables.tab, sizeof g_tables.tab / sizeof g_tables.tab[0], (i))

static uint32_t pct_of(uint32_t n, uint32_t cap) { return cap ? n * 100u / cap : 0; }

/* ---- tile resources ---------------------------------------------------------------- */

static bool entry(unsigned set, unsigned index0, TileEntry *e)
{
    const TileSet *ts = set < 32 ? tile_draw_set((int)set) : NULL;
    return ts && tile_get_entry(ts, (int)index0, e) == TILE_OK;
}

/* 0x4ef4fb tile_get_height */
static int tile_height(unsigned set, unsigned index0)
{
    TileEntry e;
    return set != 0 && entry(set, index0, &e) ? e.height : 0;
}

/* 0x4ef603 tile_get_name (which 0) / 0x4ef665 tile_get_desc (1) */
static const char *tile_text(unsigned set, unsigned index0, int which)
{
    TileEntry e;
    const char *s = entry(set, index0, &e) ? tile_entry_text(&e, which) : NULL;
    return s ? s : "";
}

/* ---- the title and the window's height --------------------------------------------- */

/* 0x467890 */
static void draw_title(void)
{
    static const uint32_t zoom[3] = {0x661228, 0x661240, 0x661258};
    char buf[80];
    snprintf(buf, sizeof buf, exe_fmt(0x661264, "s", "Microview - %s"), exe_str(zoom[g_zoom % 3]));
    wm_set_title(g_win, buf);
    views_draw_title(g_win);
}

/* 0x469d65: extends the window by whole 8-pixel rows for the sprite */
static void fit_height(unsigned set, unsigned index)
{
    int h = tile_height(set, index);
    if (g_zoom == 0)
        h <<= 1;
    if (h > 0x117)
        h = 0x118;
    if (h < 0xb9)
        h = 0xb8;
    int rows = ((h / 8) * 8 - 0xb0) / 8;
    if (rows == g_rows)
        return;
    int w = g_win->surface->width;
    for (int i = 0; i < rows; i++)
        if (g_strip)
            gfx_blit(g_win->surface, 0, i * 8 + 0xc4, w, 8, g_strip, 0, 0);
    if (g_bottom)
        gfx_blit(g_win->surface, 0, rows * 8 + 0xc4, w, 0x5a, g_bottom, 0, 0);
    if (wm_is_minimized(g_win))
        wm_set_minimized(g_win, false);
    wm_resize(g_win, w, rows * 8 + 0x11e);
    if (g_viewer)
        wm_send(g_viewer, g_viewer, CTL_VIEW_RESIZE, 0, (0xde - 0x3c) + rows * 8 + 1);
    g_rows = rows;
}

static GfxRect pic_rect(void) { return (GfxRect){0xe, 0xf1, 0x38, g_rows * 8 + 0xe3}; } /* 0x638fa0 */

/* ---- the line at the bottom (0x46a1e0) -------------------------------------------------- */

static void status_line(unsigned set, unsigned index, int x, int y)
{
    if (!g_win)
        return;
    static const uint32_t punish[8] = {0x661ae0, 0x661b20, 0x661b64, 0x661ba8, 0x661be4, 0x661c20, 0x661c5c, 0x661c98};
    static const uint32_t reward[8] = {0x661b00, 0x661b44, 0x661b88, 0x661bc8, 0x661c04, 0x661c40, 0x661c7c, 0x661cb4};
    int top = g_rows * 8 + 0xee;
    GfxRect r = {0x11, 0xed, top, g_rows * 8 + 0x10c};
    char buf[96], num[FMT_NUM_LEN];
    bool hell = g_plane == GS_PLANE_HELL;
    const SimCell *s = sim_cell(x, y);
    uint32_t s1 = sim_cell_u32(s, 1), s5 = sim_cell_u32(s, 5), s9 = sim_cell_u32(s, 9);
    int ty = g_rows * 8 + 0xf0, y2 = g_rows * 8 + 0xfa, y3 = g_rows * 8 + 0x104;
    fill(0x11, top, 0xdc, 10, 100);
    fill(0x32, g_rows * 8 + 0xf7, 0xa5, 0x15, 100);
    font_colors(0x13, 0x13);
    gfx_font_select(FONT);
    switch (set) {
    case 1:
        text_at(0x19, ty, &r, exe_str(0x662290), 0);
        break;
    case 2:
        text_at(0x19, ty, &r, exe_str(index < 0x34 ? 0x6620e8 : 0x662100), 0);
        break;
    default:
        text_at(0x19, ty, &r, exe_str(0x6622d8), 0); /* "Other" */
        break;
    case 4:
        text_at(0x19, ty, &r, exe_str(0x662240), 0);
        break;
    case 5: {
        text_at(0x19, ty, &r, exe_str(hell ? 0x661df4 : 0x661e04), 0);
        uint32_t sum = s1 + s5 + s9, c = CAP(set5, index) / 100;
        snprintf(buf, sizeof buf, exe_fmt(0x661e10, "sd", "%s SOULs (%d%% Full)"), fmt_thousands(num, sum),
                 (int)(c ? sum / c : 0));
        text_at(0x32, y2, &r, buf, 0);
        break;
    }
    case 6:
        text_at(0x11, ty, &r, exe_str(0x661ee4), 0);
        break;
    case 7:
        text_at(0x19, ty, &r, exe_str(hell ? 0x66210c : 0x66213c), 0);
        snprintf(buf, sizeof buf, exe_fmt(hell ? 0x662124 : 0x66214c, "sd", "%s (%d%% Full)"), fmt_thousands(num, s1),
                 (int)pct_of(s1, CAP(set7, index)));
        text_at(0x32, y2, &r, buf, 0);
        break;
    case 8:
        text_at(0x19, ty, &r, exe_str(hell ? 0x66217c : 0x6621a8), 0);
        snprintf(buf, sizeof buf, exe_fmt(hell ? 0x662190 : 0x6621c4, "d", "%d trained last year"), (int)s5);
        text_at(0x32, y2, &r, buf, 0);
        snprintf(buf, sizeof buf, exe_fmt(0x6621dc, "d", "%d%% of Capacity"), (int)pct_of(s5, CAP(set8, index)));
        text_at(0x32, y3, &r, buf, 0);
        break;
    case 9:
        text_at(0x19, ty, &r, exe_str(hell ? 0x661f8c : 0x661f9c), 0);
        snprintf(buf, sizeof buf, exe_fmt(0x661fac, "sd", "%s SOULs (%d%% Full)"), fmt_thousands(num, s5),
                 (int)pct_of(s5, CAP(set9, index)));
        text_at(0x32, y2, &r, buf, 0);
        break;
    case 10:
        text_at(0x19, ty, &r, exe_str(0x6622bc), 0);
        break;
    case 12:
        text_at(0x19, ty, &r, exe_str(hell ? 0x661eec : 0x661efc), 0);
        snprintf(buf, sizeof buf, exe_fmt(0x661f10, "s", "%s SOULs"), fmt_thousands(num, s9));
        text_at(0x32, y2, &r, buf, 0);
        break;
    case 13:
        text_at(0x19, ty, &r, exe_str(hell ? 0x661e68 : 0x661e78), 0);
        snprintf(buf, sizeof buf, exe_fmt(0x661e8c, "sd", "%s SOULs (%d%% Full)"), fmt_thousands(num, s5),
                 (int)pct_of(s5, CAP(set13, index >> 1)));
        text_at(0x32, y2, &r, buf, 0);
        break;
    case 14:
    case 15: {
        unsigned k = index / 13;
        text_at(0x19, ty, &r, exe_str(k < 7 ? (hell ? punish[k] : reward[k]) : (hell ? punish[7] : reward[7])), 0);
        uint32_t pop = realm_building_pop(x, y);
        uint32_t cap = index < REALM_TILE_TYPES && g_world.tile_info ? g_world.tile_info[index].cost : 0;
        snprintf(buf, sizeof buf, exe_fmt(0x661ccc, "sd", "Pop: %s (%d%% full)"), fmt_thousands(num, pop),
                 (int)pct_of(pop, cap));
        text_at(0x32, y2, &r, buf, 0);
        break;
    }
    case 16:
        text_at(0x19, ty, &r, exe_str(0x662264), 0);
        break;
    case 17:
        text_at(0x19, ty, &r, exe_str(0x66207c), 0);
        snprintf(buf, sizeof buf, exe_fmt(0x662088, "s", "Serving %s SOULs"), fmt_thousands(num, s5));
        text_at(0x32, y2, &r, buf, 0);
        snprintf(buf, sizeof buf, exe_fmt(0x66209c, "d", "%d%% Full"), (int)pct_of(s5, CAP(set17, s->b[10])));
        text_at(0x32, y3, &r, buf, 0);
        break;
    case 18:
        text_at(0x19, ty, &r, exe_str(hell ? 0x662020 : 0x662044), 0);
        break;
    case 19:
        text_at(0x19, ty, &r, exe_str(0x6622d0), 0);
        break;
    case 29:
        text_at(0x19, ty, &r, exe_str(0x661dc4), 0);
        snprintf(buf, sizeof buf, exe_fmt(0x661ddc, "d", "Local charge of %d Qs"), (int)s1);
        text_at(0x32, y2, &r, buf, 0);
        break;
    case 30:
        text_at(0x19, ty, &r, exe_str(0x661f5c), 0);
        snprintf(buf, sizeof buf, exe_fmt(0x661f68, "s", "%s SOULs last year"), fmt_thousands(num, s5));
        text_at(0x32, y2, &r, buf, 0);
        snprintf(buf, sizeof buf, exe_fmt(0x661f7c, "s", "%s SOULs Served"), fmt_thousands(num, s9));
        text_at(0x32, y3, &r, buf, 0);
        break;
    }
    wm_redraw(g_win, &r);
}

/* ---- THINGS YOU CAN CHANGE: Fate Structures --------------------------------------------- */

/* 0x46a136: a bar of val / cap of w pixels from x0; returns its end */
static int bar(int y, int w, int h, uint32_t cap, uint32_t val, uint8_t bg, uint8_t fg, int x0)
{
    int len = (int)mul_div_clamp(val, cap, (uint32_t)w);
    if (len > w)
        len = w;
    if (bg != 0 && len < w)
        fill(len + x0, y, w - len, h, bg);
    fill(x0, y, len, h, fg);
    return len + x0;
}

/* 0x46c765: the population bars of a Fate Structure (the original labels
 * the temporary count "Perm Pop" and the permanent one "Temp Pop") */
static void population(unsigned index, int x, int y)
{
    if (!g_win)
        return;
    char buf[64], num[FMT_NUM_LEN];
    uint32_t temp = realm_building_temp(x, y), perm = realm_building_perm(x, y);
    int n = (uint8_t)structure_size(x, y);
    font_colors(0x13, 0x13);
    gfx_font_select(FONT);
    fill(0x2c, 0xb3, 0xb4, 9, 0x62);
    fill(0x2c, 0xc3, 0xb4, 9, 0x62);
    fill(0x2c, 0xd3, 0xb4, 9, 0x62);
    fill(0x15, 0x9c, 0xcd, 0x10, BG);
    fill(0x15, 0xb2, 0xe, 10, BG);
    fill(0x15, 0xc2, 0xe, 10, BG);
    fill(0x15, 0xd2, 0xe, 10, BG);
    uint32_t cap = index < REALM_TILE_TYPES && g_world.tile_info ? g_world.tile_info[index].cost : 0;
    snprintf(buf, sizeof buf, exe_fmt(0x6622e0, "s", "Capacity: %s SOULs"), fmt_thousands(num, cap));
    gfx_font_draw_text(g_win->surface, 0x2e, 0xb3, NULL, NULL, buf, 0);
    int end = 0x15, last = 0x15;
    unsigned k = index / 13;
    if (k > 6)
        k = 7;
    int w = n == 1 ? 0xe : n == 2 ? 3 : 2;
    if (index < 0x5b) {
        end = bar(0x9c, 0xcd, 0x10, cap, temp, BG, k_dark[k], 0x15);
        fill(0x15, 0xc2, 0xe, 10, k_dark[k]);
        fill(0x15, 0xb2, 0xe, 5, k_dark[k]);
    } else {
        int i = 0;
        bool first = true;
        for (unsigned bx = (uint8_t)x; bx < (unsigned)(n + (uint8_t)x); bx = (uint8_t)(bx + 1))
            for (int by = (uint8_t)y; (int)((uint8_t)y - n) < by; by = (uint8_t)(by - 1)) {
                if (bx >= GS_MAP_W || by >= GS_MAP_H)
                    break;
                unsigned kk = sim_zone_kind((int)bx, by);
                if (kk == 7) {
                    end = last;
                    continue;
                }
                end = bar(0x9c, 0xcd, 0x10, cap, sim_temp((int)bx, by), first ? BG : 0, k_dark[kk & 7], end);
                first = false;
                last = end;
                fill(i * w + 0x15, 0xc2, w, 10, k_dark[kk & 7]);
                fill(i * w + 0x15, 0xb2, w, 5, k_dark[kk & 7]);
                i++;
            }
    }
    snprintf(buf, sizeof buf, exe_fmt(0x6622f4, "s", "Perm Pop: %s SOULs"), fmt_thousands(num, temp));
    gfx_font_draw_text(g_win->surface, 0x2e, 0xc3, NULL, NULL, buf, 0);
    if (index < 0x5b) {
        bar(0x9c, 0xcd, 0x10, cap, perm, 0, k_light[k], end);
        fill(0x15, 0xd2, 0xe, 10, k_light[k]);
        fill(0x15, 0xb7, 0xe, 5, k_light[k]);
    } else {
        int i = 0;
        for (unsigned bx = (uint8_t)x; bx < (unsigned)(n + (uint8_t)x); bx = (uint8_t)(bx + 1))
            for (int by = (uint8_t)y; (int)((uint8_t)y - n) < by; by = (uint8_t)(by - 1)) {
                if (bx >= GS_MAP_W || by >= GS_MAP_H)
                    break;
                unsigned kk = sim_zone_kind((int)bx, by);
                if (kk == 7)
                    continue;
                end = bar(0x9c, 0xcd, 0x10, cap, sim_perm((int)bx, by), 0, k_light[kk & 7], end);
                fill(i * w + 0x15, 0xd2, w, 10, k_light[kk & 7]);
                fill(i * w + 0x15, 0xb7, w, 5, k_light[kk & 7]);
                i++;
            }
    }
    snprintf(buf, sizeof buf, exe_fmt(0x662308, "s", "Temp Pop: %s SOULs"), fmt_thousands(num, perm));
    gfx_font_draw_text(g_win->surface, 0x2e, 0xd3, NULL, NULL, buf, 0);
}

/* 0x46e0f0: the focus labels and the balance bar under the knob */
static void focus_labels(void)
{
    if (!g_win)
        return;
    char buf[16];
    const SimCell *s = cell_sim();
    fill(0x38, 0x7b, 0x82, 0x15, 0x62);
    fill(0x18, 0x86, 0x1b, 9, 0x47);
    fill(0xc5, 0x86, 0x1b, 9, 0x47);
    font_colors(0x1a, 0x1a);
    gfx_font_select(FONT);
    unsigned p = (unsigned)s->b[0xf] * 100 / 0xff;
    snprintf(buf, sizeof buf, exe_fmt(0x6623a4, "d", "%d%%"), (int)p);
    gfx_font_draw_text(g_win->surface, 0x1a, 0x87, NULL, NULL, buf, 0);
    snprintf(buf, sizeof buf, exe_fmt(0x6623ac, "d", "%d%%"), (int)(100 - p));
    gfx_font_draw_text(g_win->surface, 199, 0x87, NULL, NULL, buf, 0);
    uint8_t m = (uint8_t)realm_score_mix((uint8_t)g_x, (uint8_t)g_y);
    fill(0x38, 0x7b, 0x88, 0x15, (uint8_t)(0x2e - 2 * m));
    redraw(0x38, 0xbf, 0x7b, 0x8f);
    redraw(0x18, 0x32, 0x86, 0x8e);
    redraw(0xc5, 0xdf, 0x86, 0x8e);
}

/* a slider knob (save-under, colour-keyed) */
static void knob(GfxSurface *under, int *kx, int *ky, int x, int y)
{
    GfxSurface *k = g_plane == GS_PLANE_HEAVEN ? g_knob_heaven : g_knob_hell;
    if (!g_win || !k || !under)
        return;
    int w = k->width, h = k->height;
    if (*kx != -1)
        gfx_blit(g_win->surface, *kx, *ky, w, h, under, 0, 0);
    if (x != -1) {
        gfx_blit(under, 0, 0, w, h, g_win->surface, x, y);
        *kx = x;
        *ky = y;
        gfx_blit_transparent(g_win->surface, x, y, w, h, k, 0, 0, 0);
    }
}

/* 0x46e5c7: the funding focus knob */
static void knob1(int x, int y)
{
    knob(g_under1, &g_k1x, &g_k1y, x, y);
    if (x != -1)
        focus_labels();
    redraw(0x3e, 0xb8, 0x62, 0x78);
}

/* 0x46e334 */
static void knob1_place(void)
{
    int v = (int)mul_div_clamp(cell_sim()->b[0x10], 0xff, 99) + 0x40;
    if (v > 0xa2)
        v = 0xa3;
    if (v < 0x41)
        v = 0x40;
    g_knob_x10 = v + 10;
    g_knob_y = 0x62;
    knob1(v, 0x62);
}

/* 0x46e3ef: the funding focus slider follows the mouse */
static void fund_slide(int x)
{
    int v = x;
    if (v > 0xac)
        v = 0xad;
    if (v < 0x4b)
        v = 0x4a;
    int prev = select_cell_plane();
    SimCell *s = cell_sim();
    s->b[0x10] = v - 0x4a < 1 ? 1 : (uint8_t)mul_div_clamp((uint32_t)(v - 0x4a), 99, 0xff);
    s->b[0xf] = (uint8_t)-s->b[0x10];
    x -= 10;
    if (x > 0xa2)
        x = 0xa3;
    if (x < 0x41)
        x = 0x40;
    g_knob_x10 = x + 10;
    g_knob_y = 0x62;
    knob1(x, 0x62);
    restore_plane(prev);
}

/* 0x4699d5: the lock (sim +0xe bit 7) and its label */
static void lock_set(bool on)
{
    GfxRect r = {0x32, 0xdc, 0x4c, 0x7e};
    SimCell *s = cell_sim();
    char buf[48];
    fill(0x32, 0x4c, 0xaa, 0xc, 100);
    font_colors(0x13, 0x13);
    gfx_font_select(FONT);
    if (!on) {
        s->b[0xe] &= 0x7f;
        text_at(0x32, 0x4c, &r, exe_str(0x6618c0), GFX_TEXT_NOJUSTIFY);
        press(MICROVIEW_LOCK, false);
    } else {
        s->b[0xe] |= 0x80;
        unsigned lvl = g_index < 0x5b ? g_index % 13u : (uint8_t)(g_index + 0xa5);
        snprintf(buf, sizeof buf, exe_fmt(0x6618ac, "d", "LOCKED (%d)"), (int)lvl);
        text_at(0x32, 0x4c, &r, buf, GFX_TEXT_NOJUSTIFY);
        press(MICROVIEW_LOCK, true);
    }
    redraw_rect(&r);
}

/* ---- THINGS YOU CAN CHANGE: other structures -------------------------------------------- */

/* 0x4696e6: the upgrade's price */
static void upgrade_panel(void)
{
    const BuildingSpec *tab = NULL;
    size_t n = 0;
    unsigned target = 0, max = 0;
#define TAB(t) (tab = g_tables.t, n = sizeof g_tables.t / sizeof g_tables.t[0])
    switch (g_set) {
    case 7:
        target = g_index + 2u, max = 8, TAB(set7);
        break;
    case 8:
        target = g_index + 2u, max = 6, TAB(set8);
        break;
    case 9:
        target = g_index + 2u, max = 6, TAB(set9);
        break;
    case 13:
        target = g_index + 4u, max = 0x10, TAB(set13);
        break;
    case 17:
        target = g_index + 2u, max = 10, TAB(set17);
        break;
    case 19:
        target = g_index + 2u, max = 4, TAB(set19);
        break;
    case 30:
        target = g_index + 1u, max = 3, TAB(set30);
        break;
    }
#undef TAB
    target &= 0xff;
    GfxRect r = {99, 0xe1, 0x56, 0x83};
    char buf[96], num[FMT_NUM_LEN];
    font_colors(0x13, 0x13);
    gfx_font_select(FONT);
    if (tab && target < max) {
        button_show(MICROVIEW_UPGRADE);
        press(MICROVIEW_UPGRADE, false);
        g_up_index = (uint8_t)target;
        unsigned cur = g_index;
        if (g_set == 13) {
            target >>= 1;
            cur = g_index >> 1;
        }
        if (g_set == 17) {
            target = (uint8_t)(target - 4) >> 1;
            cur = (uint8_t)(cur - 4) >> 1;
        }
        g_up_cost = (uint32_t)((int32_t)(spec(tab, n, target)->cost * 2 - spec(tab, n, cur)->cost) >> 1);
        g_up_size = spec(tab, n, target)->size;
        snprintf(buf, sizeof buf, exe_fmt(0x661884, "s", "Upgrading this structure will cost %s"),
                 fmt_thousands(num, g_up_cost));
        text_at(0x77, 0x56, &r, buf, GFX_TEXT_NOJUSTIFY);
    } else {
        text_at(0x6d, 0x56, &r, exe_str(0x661860), GFX_TEXT_NOJUSTIFY | GFX_TEXT_CENTER);
    }
    r.left -= 0x4b;
    redraw_rect(&r);
}

/* 0x468df0: a bank's loan */
static void loan_panel(void)
{
    if (!g_win)
        return;
    const SimCell *s = cell_sim();
    uint32_t owed = sim_cell_u32(s, 1);
    int8_t years = (int8_t)s->b[5], rate = (int8_t)s->b[6];
    GfxRect r = {0x61, 0xe4, 0xa0, 0xd7};
    char buf[96], num[FMT_NUM_LEN];
    uint32_t soul_rate = g_state.unk_630430;
    PlaneStats *realm = realm_cur();
    bool hell = g_plane == GS_PLANE_HELL;
    fill(0x62, 0xa0, 0x83, 0x32, 100);
    if (owed == 0) {
        font_colors(0x1a, 0x1a);
        gfx_font_select(FONT);
        uint32_t avail = CAP(set19, g_index);
        if (realm->unk_80ef < 0x14) {
            int8_t c = (int8_t)(realm->unk_80ef + 1);
            if (hell) {
                snprintf(buf, sizeof buf, exe_fmt(0x6616f0, "s", "Loan Available: %s"), fmt_thousands(num, avail));
                text_at(0x62, 0xa0, &r, buf, GFX_TEXT_NOJUSTIFY);
                uint32_t risk = soul_rate ? (uint32_t)(c + 10) * (avail / 10) * 0x100u / soul_rate : 0;
                snprintf(buf, sizeof buf, exe_fmt(0x661704, "s", "Risk: %s SOULs"), fmt_thousands(num, risk));
                text_at(0x62, 0xb4, &r, buf, GFX_TEXT_NOJUSTIFY);
                text_at(0x62, 0xbe, &r, exe_str(0x661714), GFX_TEXT_NOJUSTIFY);
            } else {
                snprintf(buf, sizeof buf, exe_fmt(0x661728, "s", "Loan Available: %s"), fmt_thousands(num, avail));
                text_at(0x62, 0xa0, &r, buf, GFX_TEXT_NOJUSTIFY);
                snprintf(buf, sizeof buf, exe_fmt(0x66173c, "dd", "Yearly Interest: %1d.%1d%%"), c / 10, c % 10);
                text_at(0x62, 0xb4, &r, buf, GFX_TEXT_NOJUSTIFY);
                text_at(0x62, 0xbe, &r, exe_str(0x661758), GFX_TEXT_NOJUSTIFY);
            }
            label(MICROVIEW_LOAN, 0x661770); /* BORROW */
            press(MICROVIEW_LOAN, false);
        } else {
            text_at(0x62, 0xa2, &r, exe_str(0x661778), GFX_TEXT_NOJUSTIFY);
            text_at(0x62, 0xb6, &r, exe_str(0x6617a8), GFX_TEXT_NOJUSTIFY);
            button_hide(MICROVIEW_LOAN);
            fill(0x19, 0x9d, 0x48, 0x30, 100);
        }
    } else {
        font_colors(0x13, 0x13);
        gfx_font_select(FONT);
        if (hell) {
            if (years < 1) {
                button_hide(MICROVIEW_LOAN);
                fill(0x19, 0x9d, 0x48, 0x30, 100);
                text_at(0x62, 0xa0, &r, exe_str(0x66168c), GFX_TEXT_NOJUSTIFY);
                snprintf(buf, sizeof buf, exe_fmt(0x66169c, "s", "Remaining SOULs: %s"), fmt_thousands(num, owed));
                text_at(0x62, 0xb4, &r, buf, GFX_TEXT_NOJUSTIFY);
            } else {
                snprintf(buf, sizeof buf, exe_fmt(0x661650, "s", "Pennies Owed: %s"), fmt_thousands(num, owed));
                text_at(0x62, 0xa0, &r, buf, GFX_TEXT_NOJUSTIFY);
                snprintf(buf, sizeof buf, exe_fmt(0x661664, "d", "Years Left: %d"), years);
                text_at(0x62, 0xb4, &r, buf, GFX_TEXT_NOJUSTIFY);
                uint32_t risk = soul_rate ? (uint32_t)(rate + 10) * (owed / 10) * 0x100u / soul_rate : 0;
                snprintf(buf, sizeof buf, exe_fmt(0x661674, "s", "Risk: %s SOULs"), fmt_thousands(num, risk));
                text_at(0x62, 0xbe, &r, buf, GFX_TEXT_NOJUSTIFY);
                label(MICROVIEW_LOAN, 0x661684); /* REPAY */
                press(MICROVIEW_LOAN, false);
            }
        } else {
            snprintf(buf, sizeof buf, exe_fmt(0x6616b0, "s", "Pennies Owed %s."), fmt_thousands(num, owed));
            text_at(0x62, 0xa0, &r, buf, GFX_TEXT_NOJUSTIFY);
            snprintf(buf, sizeof buf, exe_fmt(0x6616c4, "d", "Years Left: %d"), years);
            text_at(0x62, 0xb4, &r, buf, GFX_TEXT_NOJUSTIFY);
            if (years != 0) {
                snprintf(buf, sizeof buf, exe_fmt(0x6616d4, "s", "Next Payment: %s."),
                         fmt_thousands(num, owed / (uint32_t)(int32_t)years));
                text_at(0x62, 0xbe, &r, buf, GFX_TEXT_NOJUSTIFY);
            }
            label(MICROVIEW_LOAN, 0x6616e8); /* REPAY */
            press(MICROVIEW_LOAN, false);
        }
    }
    r.left = 0x18;
    wm_redraw(g_win, &r);
}

/* 0x4694df: borrow or repay */
static void loan_action(void)
{
    int prev = g_world.plane;
    views_select_plane(g_plane);
    SimCell *s = cell_sim();
    uint32_t owed = sim_cell_u32(s, 1);
    PlaneStats *realm = realm_cur();
    if (owed == 0) {
        if (ask(exe_str(0x661830), 0x66181c, 2) == 1) {
            s->b[5] = 100;
            realm->unk_80ef++;
            s->b[6] = realm->unk_80ef;
            uint32_t amount = CAP(set19, g_index);
            sim_cell_set_u32(s, 1, amount);
            bignum_add(&g_state.pennies, amount);
            g_build.status_dirty = 1;
            loan_panel();
        }
    } else if (!bignum_ge_int(g_state.pennies, (int32_t)owed)) {
        ask(exe_str(0x6617e8), 0x6617e0, 1);
    } else {
        bignum_sub(&g_state.pennies, owed);
        sim_cell_set_u32(s, 1, 0);
        realm->unk_80ef--;
        g_build.status_dirty = 1;
        loan_panel();
    }
    views_select_plane(prev);
}

/* 0x46e6fb: a training centre's acceptance text */
static void training_text(void)
{
    if (!g_win)
        return;
    GfxRect r = {0x1a, 0xe7, 0xb8, 0xd9};
    char buf[96];
    const SimCell *s = cell_sim();
    fill(0x1a, 0xb8, 0xce, 0x22, 100);
    font_colors(0x13, 0x13);
    gfx_font_select(FONT);
    uint8_t lo = (uint8_t)((g_index & 0xfe) * -10 + 0x32);
    text_at(0x1c, 0xb9, &r, exe_str(0x6623b4), GFX_TEXT_NOJUSTIFY | GFX_TEXT_CENTER);
    snprintf(buf, sizeof buf, exe_fmt(g_plane == GS_PLANE_HEAVEN ? 0x6623d8 : 0x6623fc, "dd", "%d to %d%%"), lo,
             lo + 0x32);
    text_at(0x1c, 0xc3, &r, buf, GFX_TEXT_NOJUSTIFY | GFX_TEXT_CENTER);
    if (!(s->b[0xd] & 0x80))
        snprintf(buf, sizeof buf, exe_fmt(0x662434, "d", "Now accepting %d%% of applicants."), s->b[0xd] & 0x7f);
    else
        snprintf(buf, sizeof buf, "%s", exe_str(0x662420));
    text_at(0x1c, 0xcd, &r, buf, GFX_TEXT_NOJUSTIFY | GFX_TEXT_CENTER);
    wm_redraw(g_win, &r);
}

/* 0x46e941: an Omnibolge's / Love Dome's */
static void omnibolge_text(void)
{
    if (!g_win)
        return;
    GfxRect r = {0x1a, 0xe7, 0xb8, 0xd9};
    char buf[96];
    fill(0x1a, 0xb8, 0xce, 0x22, 100);
    font_colors(0x13, 0x13);
    gfx_font_select(FONT);
    text_at(0x1c, 0xb9, &r, exe_str(g_plane == GS_PLANE_HELL ? 0x662458 : 0x662468),
            GFX_TEXT_NOJUSTIFY | GFX_TEXT_CENTER);
    text_at(0x1c, 0xc3, &r, exe_str(0x662478), GFX_TEXT_NOJUSTIFY | GFX_TEXT_CENTER);
    snprintf(buf, sizeof buf, exe_fmt(0x662490, "d", "%d%% of passing SOULs."), cell_sim()->b[0xd]);
    text_at(0x1c, 0xcd, &r, buf, GFX_TEXT_NOJUSTIFY | GFX_TEXT_CENTER);
    wm_redraw(g_win, &r);
}

/* 0x46edce: the acceptance knob */
static void knob2(int x, int y)
{
    knob(g_under2, &g_k2x, &g_k2y, x, y);
    if (x != -1) {
        if (g_set == 8)
            training_text();
        else
            omnibolge_text();
    }
    redraw(0x22, 0xdd, 0xa1, 0xb7);
}

static int accept_knob_x(unsigned v)
{
    int k = (int)mul_div_clamp(v, 100, 0xa4) + 0x24;
    if (k > 199)
        k = 200;
    if (k < 0x25)
        k = 0x24;
    return k;
}

/* 0x46eacb: the acceptance slider of a training centre / Omnibolge */
static void accept_panel(void)
{
    const SimCell *s = cell_sim();
    int v = accept_knob_x(g_set == 8 ? s->b[0xd] & 0x7fu : s->b[0xd]);
    if (g_set == 8) {
        int lo = (g_index & ~1) * -10 + 0x32;
        int w = lo * 0xa4 / 100;
        uint8_t c = g_plane == GS_PLANE_HELL ? 0xc : 0x7a;
        fill(0x2e, 0xaf, 0xa4, 4, BG);
        fill(0x2e, 0xaf, w, 4, c);
        if (lo < 0x32) {
            int w2 = (0x32 - lo) * 0xa4 / 100;
            fill(0xd2 - w2, 0xaf, w2, 4, c);
        }
        if (!(s->b[0xd] & 0x80)) {
            label(MICROVIEW_ACTIVE, 0x6624ac); /* ON */
            press(MICROVIEW_ACTIVE, true);
        } else {
            label(MICROVIEW_ACTIVE, 0x6624a8); /* OFF */
            press(MICROVIEW_ACTIVE, false);
        }
    }
    g_k2x = g_k2y = -1;
    g_knob_x10 = v + 10;
    g_knob_y = 0xa1;
    knob2(v, 0xa1);
}

/* 0x46ef1f: training centre on / off (sim +0xd bit 7 = off) */
static void training_active(bool on)
{
    SimCell *s = cell_sim();
    if (!on) {
        s->b[0xd] |= 0x80;
        label(MICROVIEW_ACTIVE, 0x6624b4); /* OFF */
        press(MICROVIEW_ACTIVE, false);
    } else {
        s->b[0xd] &= 0x7f;
        label(MICROVIEW_ACTIVE, 0x6624b0); /* ON */
        press(MICROVIEW_ACTIVE, true);
    }
    training_text();
}

/* 0x46f056: a training centre accepts lo..lo+50 % */
static void training_slide(int x)
{
    if (x > 0xd1)
        x = 0xd2;
    if (x < 0x2f)
        x = 0x2e;
    uint8_t lo = (uint8_t)((g_index & 0xfe) * -10 + 0x32);
    int prev = select_cell_plane();
    uint8_t v = lo;
    if (x - 0x2e > 0)
        v = (uint8_t)mul_div_clamp((uint32_t)(x - 0x2e), 0xa4, 100);
    if (v <= lo)
        v = lo;
    unsigned u = lo + 0x32u;
    if (v <= lo + 0x32u)
        u = v;
    SimCell *s = cell_sim();
    s->b[0xd] = (uint8_t)((s->b[0xd] & 0x80) | (u & 0xff));
    int k = accept_knob_x(u & 0xff);
    g_knob_x10 = k + 10;
    g_knob_y = 0xa1;
    knob2(k, 0xa1);
    restore_plane(prev);
}

/* 0x46f2a1: an Omnibolge / Love Dome takes 0..100 % of passing SOULs */
static void omnibolge_slide(int x)
{
    if (x > 0xd1)
        x = 0xd2;
    if (x < 0x2f)
        x = 0x2e;
    int prev = select_cell_plane();
    uint8_t v = x - 0x2e < 1 ? 0 : (uint8_t)mul_div_clamp((uint32_t)(x - 0x2e), 0xa4, 100);
    cell_sim()->b[0xd] = v;
    int k = accept_knob_x(v);
    g_knob_x10 = k + 10;
    g_knob_y = 0xa1;
    knob2(k, 0xa1);
    restore_plane(prev);
}

/* 0x46f6b1: whether the cells x0..x0+dx, y0..y0+dy are free land (or,
 * for ports, water away from the map edge) */
static bool area_free(uint8_t x0, uint8_t y0, int8_t dx, int8_t dy)
{
    bool ok = true;
    unsigned bx = x0;
    for (int guard = 0; guard < 256 && (int)bx <= dx + (int)x0; guard++) {
        unsigned by = y0;
        for (int g2 = 0; g2 < 256 && (int)by <= dy + (int)y0; g2++) {
            int i = (int)by * GS_MAP_W + (int)bx;
            unsigned set = i >= 0 && i < GS_MAP_CELLS ? g_world.cells[i] >> 27 : 0;
            if (set != 1)
                ok = false;
            if (g_set == 9) {
                if (bx == 0 || by == 0 || bx > 0x62 || by > 0x62)
                    return false;
                if (set == 2)
                    ok = true;
            }
            if (!ok)
                return false;
            by = (uint8_t)(by + 1);
        }
        bx = (uint8_t)(bx + 1);
    }
    return ok;
}

/* 0x46f7d8: upgrade: pay, destroy and rebuild one size up */
static void upgrade_action(void)
{
    char buf[128], num[FMT_NUM_LEN];
    uint8_t target = g_up_index, set = g_set;
    uint8_t x = (uint8_t)g_x, y = (uint8_t)g_y;
    if (bignum_less(g_state.pennies, (int32_t)g_up_cost)) {
        ask(exe_str(0x6624c4), 0x6624b8, 1);
        return;
    }
    int prev = g_world.plane;
    views_select_plane(g_plane);
    if (set == 19 && sim_cell_u32(sim_cell(x % GS_MAP_W, y % GS_MAP_H), 1) != 0) {
        ask(exe_str(0x6624f0), 0x6624e4, 1);
        views_select_plane(prev);
        return;
    }
    uint8_t n = (uint8_t)structure_size(x, y);
    uint8_t nx = x, ny = y;
    if (n != g_up_size) {
        nx = ny = 200;
        int8_t m = (int8_t)n;
        if (area_free(x, (uint8_t)(y - n), m, 0) &&
            area_free((uint8_t)(n + x), (uint8_t)(y - n + 1), 0, (int8_t)(m - 1)))
            nx = x, ny = y;
        else if (area_free((uint8_t)(x - 1), (uint8_t)(y - n), m, 0) &&
                 area_free((uint8_t)(x - 1), (uint8_t)(y - n + 1), 0, (int8_t)(m - 1)))
            nx = (uint8_t)(x - 1), ny = y;
        else if (area_free((uint8_t)(x - 1), (uint8_t)(y + 1), m, 0) &&
                 area_free((uint8_t)(x - 1), (uint8_t)(y - n + 1), 0, (int8_t)(m - 1)))
            nx = (uint8_t)(x - 1), ny = (uint8_t)(y + 1);
        else if (area_free(x, (uint8_t)(y + 1), m, 0) &&
                 area_free((uint8_t)(n + x), (uint8_t)(y - n + 1), 0, (int8_t)(m - 1)))
            nx = x, ny = (uint8_t)(y + 1);
        if (nx == 200) {
            ask(exe_str(0x662534), 0x66252c, 1);
            views_select_plane(prev);
            return;
        }
    }
    snprintf(buf, sizeof buf, exe_fmt(0x662570, "s", "This'll cost %s Pennies.  Are you sure?"),
             fmt_thousands(num, g_up_cost));
    if (ask(buf, 0x662598, 2) == 2) {
        views_select_plane(prev);
        return;
    }
    bignum_sub(&g_state.pennies, g_up_cost);
    structure_destroy(x, y, 0, 0);
    if (set == 17)
        target = (uint8_t)((int)(target - 4) >> 1);
    build_place(set, target, g_up_size, nx, ny, BUILD_REDRAW | BUILD_RAW);
    microview_refresh();
    views_select_plane(prev);
}

/* 0x469c0c: the artist's rendering button, for structures with a news
 * pop-up (table 0x64f000: 26 {set, index} pairs, pop-up 900 + 2i) */
static void artist_button(unsigned set, unsigned index)
{
    unsigned id = 0;
    if (set == 2)
        index = (uint8_t)index / 0x12u;
    if (set == 5)
        index &= 1;
    for (unsigned i = 0; i < 0x1a; i++)
        if ((int)(int8_t)exe_u8(0x64f000 + i * 2) == (int)set && (int)(int8_t)exe_u8(0x64f001 + i * 2) == (int)index)
            id = i * 2 + 900;
    if (g_artist != 0 && id == 0) {
        button_hide(MICROVIEW_ARTIST);
        fill(0xf9, 0x9a, 0x18, 0x18, 0x65);
    }
    if (g_artist < 2 && id != 0) {
        button_show(MICROVIEW_ARTIST);
        press(MICROVIEW_ARTIST, false);
    }
    g_artist = id;
}

/* ---- the whole window (0x46ce40) -------------------------------------------------------- */

static void extension_rows(void)
{
    if (g_rows == 0 || !g_panel)
        return;
    fill(0xe, g_panel->height + 0x38, g_panel->width, g_rows * 8 - 4, 0x6d);
    if (g_tile)
        gfx_tile_fill(g_win->surface, 0x12, g_panel->height + 0x3c, g_panel->width - 4, g_rows * 8 - 8, g_tile, 0, 0,
                      g_tile->width, g_tile->height);
}

static void blit(const GfxSurface *s, int x, int y)
{
    if (s)
        gfx_blit(g_win->surface, x, y, s->width, s->height, s, 0, 0);
}

/* the structure drawn by the map renderer (page PICTURE) */
static void picture(uint32_t *cellp, unsigned set, unsigned index, int pic_h)
{
    const MapView *mv = views_map_view();
    MapView v;
    memset(&v, 0, sizeof v);
    v.cells = g_world.cells;
    v.width = mv ? mv->width : GS_MAP_W;
    v.height = mv ? mv->height : GS_MAP_H;
    v.clip_x = 0xe;
    v.clip_y = 0x38;
    v.clip_w = 0xe3;
    v.clip_h = pic_h;
    v.cell_w = mv ? mv->cell_w : 32;
    v.cell_h = mv ? mv->cell_h : 16;
    v.key = mv ? mv->key : 0;
    int z = g_zoom - 1;
    v.zoom = z < 1 ? 0 : z;
    v.magnify = g_zoom == 0;
    v.dir_x = mv ? mv->dir_x : 1;
    v.dir_y = 1;
    v.focus_x = v.focus_y = -1;
    v.set_mask = 0xffffffffu;
    v.plane = (uint8_t)g_world.plane;
    v.flags = MAPVIEW_F_ZONE_DETAIL;
    bool alt = (*cellp & CELL_ALT) != 0;
    if (alt)
        *cellp &= ~CELL_ALT;
    tile_draw_preview(g_win->surface, &v, set, index + 1, v.zoom, cellp);
    if (alt)
        *cellp |= CELL_ALT;
}

static void show_cell(void)
{
    if (!g_win || g_x < 0)
        return;
    int x = (uint8_t)g_x, y = (uint8_t)g_y;
    views_select_plane(g_plane);
    if (x >= GS_MAP_W || y >= GS_MAP_H)
        return;
    unsigned set = cell_tileset(x, y), index = cell_index(x, y);
    button_hide(MICROVIEW_LOAN);
    button_hide(MICROVIEW_UPGRADE);
    button_hide(MICROVIEW_ACTIVE);
    button_hide(MICROVIEW_LOCK);
    if (set == 0) {
        const SimCell *s = sim_cell(x, y);
        int ox = s->b[10], oy = s->b[11];
        if (ox >= GS_MAP_W || oy >= GS_MAP_H)
            return;
        set = cell_tileset(ox, oy);
        index = cell_index(ox, oy);
        g_x = (int8_t)ox;
        g_y = (int8_t)oy;
        x = ox, y = oy;
    }
    uint32_t *cellp = &g_world.cells[y * GS_MAP_W + x];
    g_cached = *cellp;
    fit_height(set, index);
    int pic_h = g_rows * 8 + 0xac;
    if (set != g_set) {
        if (set == 14 || set == 15 || set == 17 || set == 19 || set == 9 || set == 7 || set == 8 || set == 13 ||
            set == 30 || set == 5) {
            button_show(MICROVIEW_CHANGE);
            press(MICROVIEW_CHANGE, g_page == MICROVIEW_CHANGE);
        } else {
            if (g_page == MICROVIEW_CHANGE) {
                g_page = MICROVIEW_PICTURE;
                press(MICROVIEW_PICTURE, true);
                press(MICROVIEW_CHANGE, false);
            }
            button_hide(MICROVIEW_CHANGE);
            fill(0xf9, 0x82, 0x18, 0x18, 0x65);
        }
    }
    artist_button(set, index);
    fill(0xe, 0x38, 0xe5, pic_h + 1, BG);
    switch (g_page) {
    case MICROVIEW_PICTURE:
        picture(cellp, set, index, pic_h);
        wm_show(g_viewer, false);
        /* one in a thousand: a sound for Hell's entry 0x5f */
        if (g_plane == GS_PLANE_HELL && set == 14 && index == 0x5f && rand_range(0, 1000) == 0x29a)
            sound(0x10090);
        break;
    case MICROVIEW_DESCRIPTION:
        if (g_viewer) {
            wm_send(g_viewer, g_viewer, CTL_VIEW_SET_TEXT, (intptr_t)tile_text(set, index, 1), 0);
            wm_send(g_viewer, g_viewer, CTL_VIEW_REDRAW, 0, 0);
            wm_show(g_viewer, true);
        }
        break;
    case MICROVIEW_KNOW:
        blit(g_panel, 0xe, 0x38);
        extension_rows();
        microview_info_draw(x, y, set, index);
        wm_show(g_viewer, false);
        break;
    case MICROVIEW_CHANGE:
        blit(g_panel, 0xe, 0x38);
        if (set == 14 || set == 15) {
            blit(g_zone_panel, 0x14, 0x42);
        } else {
            if (set != 5)
                blit(g_up_panel, 0x18, 0x42);
            if (set == 19)
                blit(g_up_panel, 0x18, 0x9c);
            if (set == 8 || set == 5)
                blit(g_sl_panel, 0x18, 0x9c);
        }
        extension_rows();
        wm_show(g_viewer, false);
        break;
    default:
        wm_show(g_viewer, false);
        break;
    }
    /* the name banner */
    {
        unsigned c[2] = {0x1a, 0x13};
        gfx_font_set_colors(BIG_FONT, 1, 2, c);
        gfx_font_select(BIG_FONT);
        if (g_banner)
            gfx_blit(g_win->surface, 0x16, 0x10, 0xfd, 0x24, g_banner, 0, 0);
        char name[80];
        snprintf(name, sizeof name, "%s", tile_text(set, index, 0));
        GfxRect r = {0x16, 0x112, 0x12, 0x33};
        int w = r.right - r.left;
        if (w - 6 < gfx_font_text_width(name)) {
            r.right -= 3;
            r.left += 3;
            r.top += 2;
        } else {
            r.left += 3;
            r.right -= 3;
            r.top += (int)((unsigned)(r.bottom - r.top + 1 - gfx_font_height(BIG_FONT)) >> 1) + 1;
        }
        gfx_font_draw_wrapped(g_win->surface, &r, name, BIG_FONT, 0x1a, 0x13);
    }
    font_colors(0x13, 0x13);
    gfx_font_select(FONT);
    g_set = (uint8_t)set;
    g_index = (uint8_t)index;
    status_line(set, index, x, y);
    if (set < 14 || set > 15) {
        if (g_page == MICROVIEW_CHANGE) {
            if (set != 5)
                upgrade_panel();
            if (set == 19) {
                button_show(MICROVIEW_LOAN);
                loan_panel();
            }
            if (set == 8)
                button_show(MICROVIEW_ACTIVE);
            if (set == 8 || set == 5)
                accept_panel();
        }
    } else if (g_page == MICROVIEW_CHANGE) {
        button_show(MICROVIEW_LOCK);
        if (!sim_test_flag_e7(x, y)) {
            press(MICROVIEW_LOCK, false);
            lock_set(false);
        } else {
            lock_set(true);
        }
        population(index, x, y);
        knob1_place();
    }
    if (g_page != MICROVIEW_CHANGE) {
        button_hide(MICROVIEW_LOCK);
        button_hide(MICROVIEW_UPGRADE);
        button_hide(MICROVIEW_LOAN);
        button_hide(MICROVIEW_ACTIVE);
    }
    wm_redraw(g_win, NULL);
}

/* 0x46f414 */
void microview_refresh(void)
{
    if (!g_win || g_plane == 0)
        return;
    int prev = views_select_plane(g_plane);
    if (g_x >= 0 && g_y >= 0 && (uint8_t)g_x < GS_MAP_W && (uint8_t)g_y < GS_MAP_H) {
        int x = (uint8_t)g_x, y = (uint8_t)g_y;
        uint32_t word = cell_get(x, y);
        unsigned set = cell_origin_tileset(x, y), index = cell_origin_index(x, y);
        if (set == g_set && index == g_index && g_cached == word) {
            if (g_page == MICROVIEW_CHANGE) {
                if (set == 14 || set == 15) {
                    population(index, x, y);
                    wm_redraw(g_win, NULL);
                }
                if (set == 19) {
                    loan_panel();
                    wm_redraw(g_win, NULL);
                }
            }
            status_line(set, index, x, y);
            if (g_page == MICROVIEW_KNOW) {
                blit(g_panel, 0xe, 0x38);
                microview_info_draw(x, y, set, index);
            }
        } else if (!wm_is_minimized(g_win)) {
            g_cached = word;
            show_cell();
        }
    }
    views_select_plane(prev);
}

/* 0x46f613 */
void microview_open(int x, int y)
{
    g_x = (int8_t)x;
    g_y = (int8_t)y;
    g_plane = (uint8_t)g_world.plane;
    (void)rand_range(0, 100); /* 0x638eaa, 0x638fb0: never read */
    (void)rand_range(0, 100);
    if (!g_win)
        return;
    wm_set_minimized(g_win, false);
    wm_raise(g_win);
    show_cell();
    wm_show(g_win, true);
}

void microview_target(int plane, int x, int y)
{
    g_plane = (uint8_t)plane;
    int prev = views_select_plane(plane);
    if (x >= 0 && y >= 0 && x < GS_MAP_W && y < GS_MAP_H && cell_tileset(x, y) == 0) {
        const SimCell *s = sim_cell(x, y);
        x = s->b[10];
        y = s->b[11];
    }
    g_x = (int8_t)x;
    g_y = (int8_t)y;
    if (x >= 0 && y >= 0 && x < GS_MAP_W && y < GS_MAP_H) {
        g_set = (uint8_t)cell_tileset(x, y);
        g_index = (uint8_t)cell_index(x, y);
        g_up_cost = 0;
        g_up_size = g_up_index = 0;
        if (g_set != 5 && g_set != 14 && g_set != 15)
            upgrade_panel();
    }
    views_select_plane(prev);
}

void microview_action(int act, int x)
{
    int prev = g_world.plane;
    switch (act) {
    case MICROVIEW_ACT_FUND:
        fund_slide(x);
        break;
    case MICROVIEW_ACT_TRAINING:
        training_slide(x);
        break;
    case MICROVIEW_ACT_OMNIBOLGE:
        omnibolge_slide(x);
        break;
    case MICROVIEW_ACT_LOCK:
        views_select_plane(g_plane);
        lock_set(x != 0);
        restore_plane(prev);
        break;
    case MICROVIEW_ACT_ACTIVE:
        views_select_plane(g_plane);
        training_active(x != 0);
        restore_plane(prev);
        break;
    case MICROVIEW_ACT_LOAN:
        loan_action();
        break;
    case MICROVIEW_ACT_UPGRADE:
        upgrade_action();
        break;
    }
}

uint32_t microview_upgrade_cost(void) { return g_up_cost; }

/* ---- input --------------------------------------------------------------------------------- */

/* 0x469f1b: right click on a button */
static void microview_help(WmWindow *c)
{
    uint32_t va = 0;
    switch ((int)ctl_get_id(c)) {
    case 1:
        va = wm_is_minimized(c->parent) ? 0x661a98 : 0x661abc;
        break;
    case MICROVIEW_PICTURE:
        va = 0x6618f0;
        break;
    case MICROVIEW_CLOSE:
        va = 0x6618cc;
        break;
    case MICROVIEW_DESCRIPTION:
        va = 0x661928;
        break;
    case MICROVIEW_KNOW:
        va = 0x661964;
        break;
    case MICROVIEW_CHANGE:
        va = 0x6619b4;
        break;
    case MICROVIEW_ARTIST:
        va = 0x661a00;
        break;
    case MICROVIEW_LOCK:
        va = 0x661a58;
        break;
    }
    if (va)
        views_control_help(c, exe_str(va));
}

/* right click / hover on the window: the sliders' help */
static void area_help(int x, int y)
{
    uint32_t va = 0;
    if (g_page != MICROVIEW_CHANGE || !views_hooks.help_balloon)
        return;
    int sx = g_win->rect.left + x, sy = g_win->rect.top + y;
    if (g_set == 14 || g_set == 15) {
        if (gfx_rect_contains(&r_popbar, x, y))
            views_hooks.help_balloon(sx, sy, 0x78, exe_str(0x661294));
        if (gfx_rect_contains(&r_fund, x, y))
            views_hooks.help_balloon(sx, sy, 0x78, exe_str(0x661320));
        if (gfx_rect_contains(&r_focus, x, y))
            views_hooks.help_balloon(sx, sy, 0x78, exe_str(0x661368));
        return;
    }
    if (g_set == 8 && gfx_rect_contains(&r_accept, x, y))
        va = 0x66145c;
    else if (g_set == 5 && gfx_rect_contains(&r_accept, x, y))
        va = g_world.plane == GS_PLANE_HELL ? 0x661508 : 0x6615ac;
    if (va)
        views_hooks.help_balloon(sx, sy, 0x78, exe_str(va));
}

/* left button down (msg 9) or the arrow keys (0x177a, no capture) */
static void press_at(int x, int y, bool button)
{
    g_grab_x = x;
    g_grab_y = y;
    wm_raise(g_win);
    if (g_page == MICROVIEW_CHANGE) {
        if ((g_set == 14 || g_set == 15) && x > 0x3f && x < 0xb8 && y > 0x5b && y < 0x7f) {
            if (button) {
                g_drag_fund = 1;
                wm_set_capture(g_win);
                views_set_paused(1);
            }
            fund_slide(x);
            return;
        }
        if (g_set == 8 && x > 0x23 && x < 0xdd && y > 0x9a && y < 0xbe) {
            if (button) {
                g_drag_train = 1;
                wm_set_capture(g_win);
                views_set_paused(1);
            }
            training_slide(x);
            return;
        }
        if (g_set == 5 && x > 0x23 && x < 0xdd && y > 0x9a && y < 0xbe) {
            if (button) {
                g_drag_omni = 1;
                wm_set_capture(g_win);
                views_set_paused(1);
            }
            omnibolge_slide(x);
            return;
        }
    } else if (g_page == MICROVIEW_PICTURE && g_set == 3 && g_index == 0 && x > 0x3f && y > 0xa7 && x < 0x4f &&
               y < 0xbf && !g_secret_busy) {
        g_secret_busy = 1;
        if (microview_hooks.secret_talk)
            microview_hooks.secret_talk(g_win);
        g_secret_busy = 0;
    }
    if (button) {
        g_drag_win = 1;
        wm_set_capture(g_win);
        views_set_paused(1);
    }
}

void microview_key_slide(int dx)
{
    if (g_win && g_page == MICROVIEW_CHANGE && g_knob_x10 >= 0)
        press_at(dx + g_knob_x10, g_knob_y, false);
}

static void clicked(WmWindow *c, int id)
{
    sound(0x10002);
    int prev = g_world.plane;
    if (id == MICROVIEW_ARTIST) {
        if (g_artist != 0) {
            if (microview_hooks.news)
                microview_hooks.news(g_artist);
            press(MICROVIEW_ARTIST, false);
        }
    } else if (id >= MICROVIEW_PICTURE && id < MICROVIEW_ARTIST) {
        if (g_page != id) {
            press(id, true);
            press(g_page, false);
            g_page = id;
            show_cell();
        }
    } else if (id == MICROVIEW_CLOSE) {
        g_x = g_y = -1;
        g_cached = 0;
        knob1(-1, -1);
        knob2(-1, -1);
        wm_show(g_win, false);
    } else if (id == MICROVIEW_LOCK) {
        views_select_plane(g_plane);
        lock_set(!sim_test_flag_e7((uint8_t)g_x % GS_MAP_W, (uint8_t)g_y % GS_MAP_H));
        restore_plane(prev);
    } else if (id == MICROVIEW_UPGRADE) {
        upgrade_action();
    } else if (id == MICROVIEW_LOAN) {
        loan_action();
    } else if (id == MICROVIEW_ACTIVE) {
        views_select_plane(g_plane);
        training_active((cell_sim()->b[0xd] & 0x80) != 0);
        restore_plane(prev);
    }
    (void)c;
}

/* 0x4681ad microview_proc */
static intptr_t microview_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    int x = (int)a, y = (int)b;
    switch (msg) {
    case WM_MSG_MINIMIZED:
        if (w != g_win)
            break;
        if (a == 0) {
            if (ctl_is_pressed(btn(MICROVIEW_DESCRIPTION)))
                wm_show(g_viewer, true);
            microview_refresh();
        } else if (ctl_is_pressed(btn(MICROVIEW_DESCRIPTION))) {
            wm_show(g_viewer, false);
        }
        break;
    case WM_MSG_MOUSEMOVE:
        if (w != g_win)
            break;
        if (g_drag_win) {
            GfxRect r = w->rect;
            rect_move_to(&r, x - g_grab_x, y - g_grab_y);
            wm_local_to_screen(w, &r);
            if (w->parent)
                wm_screen_to_local(w->parent, &r);
            wm_set_rect(w, &r);
        } else if (g_drag_fund) {
            fund_slide(x);
        } else {
            if (g_drag_train)
                training_slide(x);
            if (g_drag_omni)
                omnibolge_slide(x);
        }
        break;
    case WM_MSG_LBUTTONDOWN:
        if (w == g_win)
            press_at(x, y, true);
        break;
    case WM_MSG_HOVER:
        if (wm_get_mode() == 0)
            break;
        /* fall through */
    case WM_MSG_RBUTTONDOWN:
        if (w != g_win)
            break;
        g_grab_x = x;
        g_grab_y = y;
        wm_raise(w);
        area_help(x, y);
        break;
    case WM_MSG_LBUTTONUP:
        if (w != g_win)
            break;
        if (g_page == MICROVIEW_PICTURE) {
            GfxRect pr = pic_rect();
            if (gfx_rect_contains(&pr, x, y) && g_zoom != 0) {
                g_zoom--;
                draw_title();
                show_cell();
            }
        }
        wm_release_capture();
        g_drag_win = g_drag_fund = g_drag_train = g_drag_omni = 0;
        views_set_paused(0);
        break;
    case WM_MSG_RBUTTONUP:
        if (w != g_win)
            break;
        if (g_page == MICROVIEW_PICTURE) {
            GfxRect pr = pic_rect();
            if (gfx_rect_contains(&pr, x, y) && g_zoom < 2) {
                g_zoom++;
                draw_title();
                show_cell();
            }
        }
        break;
    case WM_MSG_CTL_PRESS:
        if (ctl_get_id(w) == 1 && wm_is_visible(g_viewer))
            wm_show(g_viewer, false);
        if (w->parent)
            wm_raise(w->parent);
        break;
    case WM_MSG_CTL_HOVER:
        if (wm_get_mode() == 0)
            break;
        /* fall through */
    case WM_MSG_CTL_RDOWN:
        if (w->parent == g_win)
            microview_help(w);
        break;
    case WM_MSG_CTL_CLICK:
        if (w->parent == g_win)
            clicked(w, (int)ctl_get_id(w));
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

/* ---- creation ---------------------------------------------------------------------------- */

static GfxSurface *load(uint32_t id) { return gfx_load_bmp_resource(id, NULL); }

static GfxSurface *cut(const GfxSurface *src, int x, int y, int w, int h)
{
    GfxSurface *s = gfx_surface_create(w, h);
    if (s && src)
        gfx_blit(s, 0, 0, w, h, src, x, y);
    return s;
}

/* 0x4678f4 */
bool microview_create(void)
{
    if (g_win)
        return true;
    GfxSurface *bg = load(600);
    if (!bg)
        return false;
    int w = bg->width, h = bg->height;
    g_strip = cut(bg, 0, 0xc4, w, 8);
    g_bottom = cut(bg, 0, h - 0x5a, w, 0x5a);
    WmWindow *mw = views_main_window();
    int right = mw ? mw->client.right : gfx_screen_width() - 1, top = mw ? mw->client.top : 0;
    GfxRect r = {right - w - 0x10, 0, top + 0xf, 0};
    r.right = r.left + w - 1;
    r.bottom = r.top + h - 1;
    g_win = wm_create_window(mw, &r, microview_proc, WM_F_MINIMIZABLE | WM_F_NOFOCUS, 0);
    if (!g_win) {
        gfx_surface_destroy(bg);
        return false;
    }
    wm_set_title(g_win, exe_str(0x661274));
    gfx_blit(g_win->surface, 0, 0, w, h, bg, 0, 0);
    g_banner = cut(bg, 0x16, 0x10, 0xfd, 0x24);
    static const struct {
        int id;
        uint32_t bmp, style;
        int x, y;
        uint32_t label;
    } k_btns[] = {
        {MICROVIEW_PICTURE, 0x114, CTL_STYLE_BITMAP | CTL_STYLE_STICKY, 0xf9, 0x3a, 0},
        {MICROVIEW_DESCRIPTION, 0x115, CTL_STYLE_BITMAP | CTL_STYLE_STICKY, 0xf9, 0x52, 0},
        {MICROVIEW_KNOW, 0x116, CTL_STYLE_BITMAP | CTL_STYLE_STICKY, 0xf9, 0x6a, 0},
        {MICROVIEW_CHANGE, 400, CTL_STYLE_BITMAP | CTL_STYLE_STICKY, 0xf9, 0x82, 0},
        {MICROVIEW_ARTIST, 0x118, CTL_STYLE_BITMAP, 0xf9, 0x9a, 0},
        {MICROVIEW_LOCK, 0x119, CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE, 0x17, 0x45, 0},
        {MICROVIEW_UPGRADE, 0x226, CTL_STYLE_BITMAP, 0x1c, 0x54, 0x661280},
        {MICROVIEW_LOAN, 0x226, CTL_STYLE_BITMAP, 0x1c, 0xae, 0x661288},
        {MICROVIEW_ACTIVE, 0x226, CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE, 0x1c, 0x80, 0x661290},
    };
    GfxSurface *s = load(0x259);
    if (s) {
        GfxRect c = {0, s->width - 1, 0, s->height - 1};
        g_win->close_button = ctl_button_create(g_win, &c, MICROVIEW_CLOSE, CTL_STYLE_BITMAP, s);
        if (!g_win->close_button)
            gfx_surface_destroy(s);
    }
    for (size_t i = 0; i < sizeof k_btns / sizeof k_btns[0]; i++) {
        s = load(k_btns[i].bmp);
        if (!s)
            continue;
        GfxRect br = {k_btns[i].x, s->width + k_btns[i].x - 1, k_btns[i].y, s->height + k_btns[i].y - 1};
        WmWindow *b = ctl_button_create(g_win, &br, (uint32_t)k_btns[i].id, k_btns[i].style, s);
        if (!b)
            gfx_surface_destroy(s);
        g_btn[k_btns[i].id - MICROVIEW_PICTURE] = b;
        if (b && k_btns[i].label)
            ctl_button_set_label(b, exe_str(k_btns[i].label), FONT, 0x131a13, true);
    }
    g_knob_heaven = load(0x11e);
    g_knob_hell = load(0x11c);
    g_under1 = g_knob_heaven ? gfx_surface_create(g_knob_heaven->width, g_knob_heaven->height) : NULL;
    g_under2 = g_knob_heaven ? gfx_surface_create(g_knob_heaven->width, g_knob_heaven->height) : NULL;
    g_panel = load(0x120);
    g_zone_panel = load(0x123);
    g_tile = load(0x124);
    g_up_panel = load(0x122);
    g_sl_panel = load(0x121);
    g_x = g_y = -1;
    g_cached = 0;
    g_k1x = g_k1y = g_k2x = g_k2y = -1;
    g_page = MICROVIEW_PICTURE;
    press(MICROVIEW_PICTURE, true);
    (void)rand_range(0, 100); /* never read, as in microview_open */
    (void)rand_range(0, 100);
    wm_show(g_win, false);
    g_zoom = 1;
    g_viewer = ctl_view_create(g_win, &r_viewer, CTL_VIEW_PARTIAL, 10000, FONT, BG, 0x1a, 0x1a, 0);
    draw_title();
    if (g_viewer) {
        g_rows = 0xd;
        fit_height(0, 0);
        wm_show(g_viewer, false);
    }
    gfx_surface_destroy(bg);
    return true;
}

void microview_destroy(void)
{
    if (g_win) {
        wm_show(g_win, false);
        wm_destroy(g_win);
    }
    g_win = g_viewer = NULL;
    memset(g_btn, 0, sizeof g_btn);
    GfxSurface **all[] = {&g_strip,  &g_bottom, &g_banner,     &g_knob_heaven, &g_knob_hell, &g_under1,
                          &g_under2, &g_panel,  &g_zone_panel, &g_tile,        &g_up_panel,  &g_sl_panel};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
        gfx_surface_destroy(*all[i]);
        *all[i] = NULL;
    }
    g_x = g_y = -1;
    g_plane = 0;
    g_set = 0xff;
    g_artist = 1;
    g_rows = 1;
    g_drag_win = g_drag_fund = g_drag_train = g_drag_omni = 0;
}
