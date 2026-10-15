/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Planetview window and the planet disaster window (ALIFE.EXE
 * 0x49be17, 0x49de17-0x4a2087, 0x4a2efd-0x4a7c4a). Overview: planetview.h.
 *
 * Window layout (local coordinates of the 0x25e background bitmap):
 *   globe, near side   (0x11, 0x1f) 170 x 170      far side (0xd9, 0x1f)
 *   layer buttons      x ~0x19f, from y 0xaf; rotation pad below them
 *   legend             (0x19f..0x1e9, 0x19..0x9f)
 *   lower panel        (4..399, 0xe2..0x149): sector info, or the
 *                      influence controls (amount slider, Wield button,
 *                      sin sliders / tenet buttons / scientist info, cost)
 *
 * Texts, word lists and colour tables are looked up in ALIFE.EXE's data
 * (exe_res_va, compiled in from src/data) by their original addresses.
 * Window-local state (0x66c7a8-0x66c914) is in `pv` below.
 */
#include "ui/planetview.h"
#include "ui/soulview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "formats/flc.h"
#include "game/planet.h"
#include "game/resource.h"
#include "game/sound.h"
#include "game/util.h"
#include "game/world.h"
#include "gfx/draw.h"
#include "gfx/exe_res.h"
#include "gfx/font.h"
#include "gfx/surface.h"
#include "platform/platform.h"
#include "ui/controls.h"

PlanetviewHooks planetview_hooks;

/* ---- Geometry -------------------------------------------------------------- */

#define GLOBE 0xaa        /* hemisphere size */
#define GLOBE_X 0x11      /* near side */
#define GLOBE_BACK_X 0xd9 /* far side */
#define GLOBE_Y 0x1f
#define MAP_W PLANET_MAP_W
#define MAP_H PLANET_MAP_H
#define FONT 500
#define FONT_BIG 501

static const GfxRect k_front = {0x11, 0xba, 0x1f, 0xc8}; /* 0x66c8a0 */
static const GfxRect k_back = {0xd9, 0x182, 0x1f, 0xc8}; /* 0x66c8b0 */
static const GfxRect k_sci = {0x74, 0x130, 0xed, 0xf7};  /* 0x66c8f0: scientist panel text */

/* Slider window messages (0x49eea4). */
#define SL_DRAW 0x83e8
#define SL_SET 0x83e9   /* a = position from the bottom; returns it */
#define SL_GET 0x83ea   /* position from the bottom */
#define SL_STEPS 0x83eb /* a = number of steps */
#define SL_LEVEL 0x83ec /* step under the thumb, counted from the top */
#define SL_ID 0x83ed
#define SL_MOVE 0x83ef /* a = thumb top */
/* Slider extra data (byte offsets). */
#define SL_D_ID 0x00
#define SL_D_BITMAP 0x04 /* normal thumb */
#define SL_D_THUMB 0x08  /* GfxRect *, local */
#define SL_D_DRAG 0x0c
#define SL_D_STEPS 0x10
#define SL_D_GRAB 0x14
#define SL_D_UNDER 0x18 /* the parent's pixels under the slider */
#define SL_D_PRESSED 0x1c
#define SL_D_CURRENT 0x20

/* ---- State ----------------------------------------------------------------- */

static struct {
    WmWindow *main;      /* main_window 0x5f7460 */
    int *paused;         /* game_paused 0x5fa61c */
    bool menu_tech;      /* 0x6465e0 checked */
    bool menu_disasters; /* 0x6465e4 checked */

    WmWindow *win;                                                 /* 0x66c7d4 */
    WmWindow *btn_pop, *btn_tech, *btn_sin, *btn_tenets;           /* 0x66c7a8 .. 0x66c7b4 */
    WmWindow *btn_up, *btn_down, *btn_right, *btn_left, *btn_stop; /* 0x66c7b8 .. 0x66c7c8 */
    unsigned influence;                                            /* 0x66c7cc */
    uint16_t layer;                                                /* 0x66c7d8 */
    uint8_t sin;                                                   /* 0x66c7dc: sin/virtue pair 1..7 */
    uint8_t tenet;                                                 /* 0x66c7e0: tenet + 1 */
    GfxSurface *sectors;                                           /* 0x66c7ec: 64 x 32 land colour per sector */
    /* 0x66c7f0: the colour of a sector's non-land pixels. The original
     * allocates 640 x 320 bytes but only indexes [y * 640 + x] with sector
     * coordinates. */
    uint8_t water[PLANET_H][PLANET_W];
    GfxSurface *bg_panel, *bg_sins, *bg_tenets, *bg_tech; /* 0x66c7f4 .. 0x66c800 */
    uint16_t amount;                                      /* 0x66c804 */
    uint32_t cost;                                        /* 0x66c808 */
    WmWindow *sliders[8];                                 /* 0x66c80c: [0] amount, [1..7] sins (0x66c810..) */
    WmWindow *tenet_btn[10];                              /* 0x66c830 */
    WmWindow *wield;                                      /* 0x66c858 */
    bool visible;                                         /* 0x66c85c */
    uint32_t *lut;                                        /* 0x66c860: resource 0x14d, GLOBE x GLOBE */
    int off_x, off_y;                                     /* 0x66c864 / 0x66c868: globe rotation */
    bool rotating;                                        /* 0x66c86c */
    int8_t dx, dy;                                        /* 0x66c870 / 0x66c874 */
    uint8_t blink;                                        /* 0x66c878 */
    GfxRect sel_rect;                                     /* 0x66c888: the selected sector on the globe */
    bool dragging;                                        /* 0x66c90c */
    int grab_x, grab_y;                                   /* 0x506c38 / 0x4f4230 */
    uint32_t blink_time;                                  /* 0x66c914 */
    uint8_t sin_colours[8][9];                            /* 0x64f1f7: [0][1..7] per sin, [s][0..8] virtue .. sin */

    WmWindow *disaster;     /* 0x66c7d0 */
    bool d_dragging;        /* 0x66c908 */
    int d_grab_x, d_grab_y; /* 0x4f4234 / 0x4f3fac */
    FlcPlayer flc;          /* 0x50ac40 */
} pv = {
    .menu_tech = true,
    .menu_disasters = true,
    .layer = PV_LAYER_POPULATION,
    .sin = 1,
    .tenet = 1,
    .visible = true,
};

/* ---- Small helpers ------------------------------------------------------------ */

/* A NUL-terminated string at `va` in ALIFE.EXE, or "". */
static const char *xs(uint32_t va)
{
    uint32_t avail = 0;
    const uint8_t *p = va ? exe_res_va(va, &avail) : NULL;
    if (!p || !memchr(p, 0, avail))
        return "";
    return (const char *)p;
}

/* The string pointed to by the pointer at `va`. */
static const char *xsp(uint32_t va)
{
    uint32_t avail = 0;
    const uint8_t *p = exe_res_va(va, &avail);
    if (!p || avail < 4)
        return "";
    return xs((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

/* A format string from the EXE, used only if its conversions are exactly
 * `spec` (one letter per conversion, "%%" ignored); otherwise `fallback`. */
static const char *xfmt(uint32_t va, const char *spec, const char *fallback)
{
    const char *f = xs(va);
    const char *s = spec;
    for (const char *p = f; *p; p++) {
        if (*p != '%')
            continue;
        if (p[1] == '%') {
            p++;
            continue;
        }
        if (*s == 0 || p[1] != *s)
            return fallback;
        s++, p++;
    }
    return *f && *s == 0 ? f : fallback;
}

/* Copies a button list from the EXE and adds the second NUL. */
static const char *xbuttons(uint32_t va, char *buf, size_t size)
{
    snprintf(buf, size - 1, "%s", xs(va));
    buf[strlen(buf) + 1] = 0;
    return buf;
}

static void font_colors(uint32_t id, unsigned c1, unsigned c2) /* font_set_colors(id, 1, 2, ...) */
{
    const unsigned c[2] = {c1, c2};
    gfx_font_set_colors(id, 1, 2, c);
}

static int text(WmWindow *w, int x, int y, const char *s)
{
    return gfx_font_draw_text(w->surface, x, y, NULL, NULL, s, 0);
}

static void set_pressed(WmWindow *b, bool pressed)
{
    if (b)
        ctl_button_set_pressed(b, pressed, true);
}

static bool is_pressed(WmWindow *b) { return b && ctl_is_pressed(b); }

static void redraw(WmWindow *w, int left, int right, int top, int bottom)
{
    GfxRect r = {left, right, top, bottom};
    wm_redraw(w, &r);
}

static void help(int x, int y, const char *s)
{
    /* TODO(WP7): help_balloon_show 0x478a14 isn't ported; no-op without a hook. */
    if (planetview_hooks.help_balloon)
        planetview_hooks.help_balloon(x, y, 0x78, s);
}

/* Centre of a control in screen coordinates (the original adds its
 * parent's rectangle once, which is the same for a top-level parent). */
static void control_centre(WmWindow *c, int *x, int *y)
{
    *x = c->rect.left + ((c->rect.right - c->rect.left) >> 1);
    *y = c->rect.top + ((c->rect.bottom - c->rect.top) >> 1);
    if (c->parent)
        wm_point_to_screen(c->parent, x, y);
}

static int sel_x(void) { return g_planet.sel_x; }
static int sel_y(void) { return g_planet.sel_y; }

static int wrap(int v, int n)
{
    v %= n;
    return v < 0 ? v + n : v;
}

static uint8_t *planet_map(void)
{
    return g_state.planet.map && g_state.planet.map_size >= PLANET_MAP_SIZE ? g_state.planet.map : NULL;
}

static uint8_t *sector_pixel(int x, int y) { return &pv.sectors->pixels[(size_t)y * pv.sectors->stride + x]; }

/* ---- Sector colours ------------------------------------------------------------- */

/* 0x4a6dbf: stores the land colour of sector (x, y) and picks its water
 * colour. `plain` is set for sectors drawn in their terrain colours. */
static void sector_store(int x, int y, uint8_t c, bool plain)
{
    const PlanetSector *s = planet_sector(x, y);
    uint8_t *w = &pv.water[y][x];
    *sector_pixel(x, y) = c;
    if (s->pop == 0 || pv.layer == PV_LAYER_TERRAIN) {
        *w = 0x7e;
        if (s->terrain == PLANET_LAND && !planet_is_coastal(x, y)) {
            if (!plain) {
                if (s->height != 0)
                    *w = c;
            } else {
                *w = 10;
            }
        }
    } else {
        *w = c;
    }
}

/* The body shared by 0x4a6ef0 (one sector, the current layer) and
 * 0x4a745f (all sectors). */
static void sector_colour(int x, int y, unsigned layer, uint32_t max_pop)
{
    const PlanetSector *s = planet_sector(x, y);
    uint8_t c = 0x13;
    bool plain = true;
    if (s->pop != 0 && s->terrain != PLANET_WATER && s->height != 0 && layer != 0 && !planet_is_coastal(x, y))
        plain = false;
    if (layer == PV_LAYER_POP_SEL && x == sel_x() && y == sel_y()) {
        c = 0xc;
    } else if (layer == PV_LAYER_POP_SEL && s->pop != 0) {
        c = (uint8_t)(mul_div_clamp(s->pop, max_pop, 7) + 0x48);
    } else if ((layer & 0xffdf) == 0 || s->pop == 0) {
        if (s->terrain == PLANET_LAND)
            c = s->height == 0 ? 10 : (uint8_t)(0x67 - s->height);
        else if (s->terrain == PLANET_WATER)
            c = 0x7e;
        else if (s->terrain == PLANET_ARCTIC)
            c = 0x1a;
        else
            c = 0x13;
    } else {
        plain = false;
        if (layer == PV_LAYER_POPULATION) {
            c = (uint8_t)(mul_div_clamp(s->pop, max_pop, 7) + 0x48);
        } else if (layer == PV_LAYER_TECH) {
            int d = (int)g_planet.max_tech - s->tech;
            if (d > 6)
                d = 7;
            c = (uint8_t)(0x57 - d);
        } else if (layer == PV_LAYER_SIN) {
            unsigned row = pv.sin < 8 ? pv.sin : 0;
            int8_t v = row ? (int8_t)s->sin[row - 1] : 0;
            if (v == 0) {
                c = pv.sin_colours[row][4];
            } else {
                int level = (int)mul_div_clamp((uint32_t)abs(v), 0x7f, 3);
                int idx = (v < 0 ? 1 : -1) * (level + 1) + 4;
                c = pv.sin_colours[row][idx];
            }
        } else if (layer == PV_LAYER_TENETS) {
            if (pv.tenet == 1) {
                uint8_t v = s->tenet[TENET_AAAA];
                if (is_pressed(pv.tenet_btn[1])) /* NAAA */
                    v = (uint8_t)(0xff - v);
                c = (uint8_t)(mul_div_clamp(v, 0xff, 7) + 0x50);
            } else if (pv.tenet >= 2 && pv.tenet <= 9) {
                c = (uint8_t)(mul_div_clamp(s->tenet[pv.tenet - 1], 0xff, 7) + 0x50);
            }
        }
    }
    sector_store(x, y, c, plain);
}

/* 0x4a6ef0 */
static void sector_colour_one(int x, int y)
{
    if (pv.sectors)
        sector_colour(x, y, pv.layer, planet_max_pop());
}

/* 0x4a745f */
static void sector_colour_all(unsigned layer)
{
    uint32_t max_pop = planet_max_pop();
    for (int y = 0; y < PLANET_H; y++)
        for (int x = 0; x < PLANET_W; x++)
            sector_colour(x, y, layer, max_pop);
}

/* 0x4a6a9d */
static GfxSurface *sector_surface(void)
{
    if (!pv.sectors)
        pv.sectors = gfx_surface_create(PLANET_W, PLANET_H);
    return pv.sectors;
}

/* ---- The globe ---------------------------------------------------------------------- */

static uint8_t globe_pixel(const uint8_t *map, int mx, int my)
{
    return planet_map_test(map, (unsigned)mx, (unsigned)my, MAP_W) ? *sector_pixel(mx / 10, my / 10)
                                                                   : pv.water[my / 10][mx / 10];
}

/* 0x49e787: both hemispheres, w x h of the projection, the near side at
 * (x, y) and the far side 0xc8 pixels right of it. The far side shows the
 * opposite longitudes, mirrored north-south. */
static void draw_globe(GfxSurface *dst, int x, int y, int w, int h)
{
    const uint8_t *map = planet_map();
    if (!dst || !pv.sectors || !pv.lut || !map)
        return;
    const int back = GLOBE_BACK_X - GLOBE_X;
    for (int j = 0; j < h; j++) {
        int py = y + j;
        if (py < 0 || py >= dst->height)
            continue;
        uint8_t *row = dst->pixels + (size_t)py * dst->stride;
        for (int i = 0; i < w; i++) {
            uint32_t e = pv.lut[j * GLOBE + i];
            if (e == 0)
                continue;
            int mx = wrap((int)(e & 0xffff) + pv.off_x, MAP_W);
            int my = wrap((int)(e >> 16) + pv.off_y, MAP_H);
            int my2 = wrap((int)(e >> 16) - pv.off_y, MAP_H);
            int mx2 = wrap(mx + MAP_W / 2, MAP_W);
            if (x + i >= 0 && x + i < dst->width)
                row[x + i] = globe_pixel(map, mx, my);
            if (x + i + back >= 0 && x + i + back < dst->width)
                row[x + i + back] = globe_pixel(map, mx2, my2);
        }
    }
}

/* 0x49ea5d: the part of the globe in `r` (window coordinates, inside one
 * hemisphere; r->left == 0 means none). */
static void draw_globe_rect(GfxSurface *dst, const GfxRect *r)
{
    const uint8_t *map = planet_map();
    if (!r || r->left == 0 || !dst || !pv.sectors || !pv.lut || !map)
        return;
    bool far = r->left >= GLOBE_BACK_X;
    int ox = far ? GLOBE_BACK_X : GLOBE_X;
    for (int py = r->top; py <= r->bottom; py++) {
        int j = py - GLOBE_Y;
        if (j < 0 || j >= GLOBE || py >= dst->height)
            continue;
        for (int px = r->left; px <= r->right; px++) {
            int i = px - ox;
            if (i < 0 || i >= GLOBE || px >= dst->width)
                continue;
            uint32_t e = pv.lut[j * GLOBE + i];
            if (e == 0)
                continue;
            int mx = wrap((int)(e & 0xffff) + pv.off_x, MAP_W);
            int my = wrap((int)(e >> 16) + (far ? -pv.off_y : pv.off_y), MAP_H);
            if (far)
                mx = wrap(mx + MAP_W / 2, MAP_W);
            dst->pixels[(size_t)py * dst->stride + px] = globe_pixel(map, mx, my);
        }
    }
}

/* 0x4a7a61: the sector under window point (x, y), false off the globe.
 * (The original wraps the far side's longitude only once and can produce
 * x >= 64; wrapped properly here.) */
static bool point_to_sector(int x, int y, int *sx, int *sy)
{
    if (!pv.lut)
        return false;
    int mx, my;
    if (gfx_rect_contains(&k_front, x, y)) {
        uint32_t e = pv.lut[(x - k_front.left) + (y - k_front.top) * GLOBE];
        if (e == 0)
            return false;
        mx = (int)(e & 0xffff);
        my = (int)(e >> 16) + pv.off_y;
    } else if (gfx_rect_contains(&k_back, x, y)) {
        uint32_t e = pv.lut[(x - k_back.left) + (y - k_back.top) * GLOBE];
        if (e == 0)
            return false;
        mx = (int)(e & 0xffff) + MAP_W / 2;
        my = (int)(e >> 16) - pv.off_y;
    } else {
        return false;
    }
    mx = wrap(mx + pv.off_x, MAP_W);
    my = wrap(my, MAP_H);
    if (sx) {
        *sx = mx / 10;
        *sy = my / 10;
    }
    return true;
}

/* 0x4a7c4a: outside the globe area (a click there drags the window). */
static bool outside_globe(int x, int y)
{
    const GfxRect r = {0xc, 0x187, 0x1a, 0xcd};
    return !gfx_rect_contains(&r, x, y);
}

/* 0x4a4468: finds where the selected sector is on the globe by drawing
 * it in colour 0 and taking the bounding box of those pixels (near side
 * first), then draws it normally again. (The original scans every pixel
 * of the hemisphere and uses top == 0 as "nothing found yet"; this scans
 * the projected pixels only, with a proper flag.) */
static void select_rect(void)
{
    if (!pv.win || !pv.sectors)
        return;
    GfxSurface *s = pv.win->surface;
    int sx = sel_x(), sy = sel_y();
    *sector_pixel(sx, sy) = 0;
    pv.water[sy][sx] = 0;
    draw_globe(s, GLOBE_X, GLOBE_Y, GLOBE, GLOBE);
    pv.sel_rect = (GfxRect){0, 0, 0, 0};
    for (int hemi = 0; hemi < 2 && pv.lut; hemi++) {
        int ox = hemi ? GLOBE_BACK_X : GLOBE_X;
        bool found = false;
        GfxRect r = {0, 0, 0, 0};
        for (int j = 0; j < GLOBE && GLOBE_Y + j < s->height; j++)
            for (int i = 0; i < GLOBE && ox + i < s->width; i++) {
                if (pv.lut[j * GLOBE + i] == 0 || s->pixels[(size_t)(GLOBE_Y + j) * s->stride + ox + i] != 0)
                    continue;
                if (!found) {
                    r = (GfxRect){i, i, j, j};
                    found = true;
                } else {
                    if (i < r.left)
                        r.left = i;
                    if (i > r.right)
                        r.right = i;
                    if (j > r.bottom)
                        r.bottom = j;
                }
            }
        if (found) {
            pv.sel_rect = (GfxRect){r.left + ox, r.right + ox, r.top + GLOBE_Y, r.bottom + GLOBE_Y};
            break;
        }
    }
    sector_colour_one(sx, sy);
    draw_globe_rect(s, &pv.sel_rect);
}

/* ---- Lower panel: sector info ----------------------------------------------------------- */

static void redraw_info(WmWindow *w) { redraw(w, 4, 399, 0xe2, 0x149); }        /* 0x4a6a1d */
static void redraw_legend(WmWindow *w) { redraw(w, 0x19f, 0x1e9, 0x19, 0x9f); } /* 0x4a6a5d */

/* 0x4a51ab: a 30 x 10 bar, `value` of `max`. */
static void draw_bar(GfxSurface *s, int x, int y, uint32_t value, uint32_t max, uint8_t colour)
{
    GfxRect r = {x, x + 0x1d, y, y + 9};
    gfx_frame_rect(s, &r, colour);
    uint32_t w;
    if (value <= 10000000) {
        if (max < 2)
            max = 1;
        w = value * 0x1e / max;
    } else {
        max /= 0x1e;
        if (max < 2)
            max = 1;
        w = value / max;
    }
    gfx_fill_rect(s, x, y, (int)w, 10, colour);
}

static const char *tech_name(unsigned tech) { return tech < PLANET_TECHS ? g_planet_tables.tech_name[tech] : ""; }

static void scientist_info(bool flush);

/* 0x4a5270: with a window, selects the sector under (x, y) unless x is -1
 * and redraws the lower panel; without one, selects sector (x, y). */
static void draw_info(WmWindow *w, int x, int y)
{
    int fh = gfx_font_height(FONT);
    if (!w) {
        if (pv.sel_rect.left != 0 && pv.win) {
            sector_colour_one(sel_x(), sel_y());
            draw_globe_rect(pv.win->surface, &pv.sel_rect);
            wm_redraw(pv.win, &pv.sel_rect); /* original: wm_redraw(NULL, ...) */
        }
        pv.blink = 0;
        g_planet.sel_x = x;
        g_planet.sel_y = y;
        select_rect();
        return;
    }
    if (x != -1) {
        sector_colour_one(sel_x(), sel_y());
        if (!point_to_sector(x, y, &g_planet.sel_x, &g_planet.sel_y))
            return;
        pv.blink = 0;
        draw_globe_rect(w->surface, &pv.sel_rect);
        wm_redraw(w, &pv.sel_rect);
        select_rect();
    }
    if (pv.influence == PV_INFLUENCE_SCIENTIST)
        scientist_info(true);
    if (pv.influence != PV_INFLUENCE_NONE || !pv.sectors)
        return;

    GfxSurface *s = w->surface;
    const PlanetSector *sec = planet_sector(sel_x(), sel_y());
    char buf[100], num[FMT_NUM_LEN];
    int x0 = 6, ty = 0xe4;
    font_colors(FONT, 0x1a, 0x1a);
    gfx_font_select(FONT);
    gfx_fill_rect(s, 4, 0xe2, 0x18c, 0x68, 0x13);

    /* the sector, ten times enlarged */
    const uint8_t *map = planet_map();
    uint8_t land = *sector_pixel(sel_x(), sel_y()), sea = pv.water[sel_y()][sel_x()];
    for (int j = 0; j < 10 && map; j++)
        for (int i = 0; i < 10; i++) {
            bool l = planet_map_test(map, (unsigned)(sel_x() * 10 + i), (unsigned)(sel_y() * 10 + j), MAP_W);
            if (ty + j < s->height && x0 + i < s->width)
                s->pixels[(size_t)(ty + j) * s->stride + x0 + i] = l ? land : sea;
        }

    static const uint32_t k_terrain[3] = {0x66f974, 0x66f984, 0x66f994}; /* Land, Ocean, Arctic */
    if (sec->terrain == PLANET_LAND && sec->height != 0)
        snprintf(buf, sizeof buf, "%s", xs(0x66f99c)); /* "Mountainous Terrain" */
    else
        snprintf(buf, sizeof buf, xfmt(0x66f9b0, "s", "%s"), sec->terrain < 3 ? xs(k_terrain[sec->terrain]) : "");
    text(w, x0 + 0xf, ty, buf);
    ty += fh + 6;
    snprintf(buf, sizeof buf, xfmt(0x66f9bc, "s", "%s"), fmt_thousands(num, sec->pop)); /* "%s Inhabitants" */
    text(w, x0, ty, buf);
    ty += fh + 6;
    if (sec->pop == 0)
        snprintf(buf, sizeof buf, "%s", xs(0x66f9d0)); /* "None" */
    else
        snprintf(buf, sizeof buf, "%s", tech_name(sec->tech));
    text(w, x0, ty, xs(0x66f9d8)); /* "TECH LEVEL:" */
    text(w, 0x50, ty, buf);
    int ty2 = fh + ty;
    ty = ty2 + 6;

    if (sec->pop != 0) {
        /* sin levels: a bar up (virtue) or down (sin) from the base line */
        ty = ty2 + 0x14;
        int bx = x0 + 5 + text(w, x0, ty - (fh >> 1), xs(0x66f9e4)); /* "Sin Levels" */
        for (int i = 0; i < 7; i++) {
            int8_t v = (int8_t)sec->sin[i];
            int level = (int)mul_div_clamp((uint32_t)abs(v), 0x7f, 0xf);
            int top, ftop;
            if (v < 0) {
                top = ty;
                ftop = ty;
            } else {
                top = ty - level;
                ftop = ty - 0xe;
            }
            uint8_t col = pv.sin_colours[0][i + 1];
            if (v == 0) {
                gfx_line(s, bx, bx + 9, top, top, col);
            } else {
                GfxRect fr = {bx, bx + 9, ftop, ftop + 0xe};
                gfx_frame_rect(s, &fr, col);
                gfx_fill_rect(s, bx, top, 10, level + 1, col);
            }
            bx += 0xe;
        }
        bx += 5;
        text(w, bx, ty - 0xf, xs(0x66f9f0));      /* "Virtue" */
        text(w, bx, ty + 0xf - fh, xs(0x66f9f8)); /* "Sin" */

        /* tenets: share in percent; the fates add up to 100, each pair too */
        static const uint32_t k_names[9] = {0x66fa04, 0x66fa14, 0x66fa24, 0x66fa34, 0x66fa44,
                                            0x66fa54, 0x66fa64, 0x66fa74, 0x66fa84};
        const uint8_t *t = sec->tenet;
        uint8_t pct[9];
        pct[0] = (uint8_t)(t[0] * 100u / 0xff);
        for (int i = 1; i < 5; i++) {
            pct[i] = (uint8_t)(t[i] * 100u / 0xff);
            if (t[i] != 0 && pct[i] < 2)
                pct[i] = 1;
        }
        unsigned big = 1;
        uint8_t sum = pct[1];
        for (unsigned i = 2; i < 5; i++) {
            sum = (uint8_t)(sum + pct[i]);
            if (pct[big] < pct[i])
                big = i;
        }
        if (sum < 0x65) {
            if (sum < 100)
                pct[big] = (uint8_t)(pct[big] - sum + 100);
        } else {
            pct[big] = (uint8_t)(pct[big] - (uint8_t)(sum + 0x9c));
        }
        pct[5] = (uint8_t)(t[TENET_ALF] * 100u / 0xff);
        if (t[TENET_ALF] != 0 && pct[5] < 2)
            pct[5] = 1;
        pct[6] = (uint8_t)(100 - pct[5]);
        pct[7] = (uint8_t)(t[TENET_SUMA] * 100u / 0xff);
        if (t[TENET_SUMA] != 0 && pct[7] < 2)
            pct[7] = 1;
        pct[8] = (uint8_t)(100 - pct[7]);

        int nx = 0xe7, ny = 0xe4;
        uint8_t col = 0xc;
        font_colors(FONT, 0xc, 0xc);
        for (int i = 0; i < 9; i++) {
            if (i == 1 || i == 5 || i == 7) {
                col = i == 1 ? 0x24 : i == 5 ? 10 : 0xb;
                font_colors(FONT, col, col);
            }
            text(w, nx, ny, xs(k_names[i]));
            if (t[i] != 0)
                draw_bar(s, nx + 0x28, ny, t[i], 0xff, col);
            snprintf(buf, sizeof buf, xfmt(0x66fa8c, "d", "%d%%"), pct[i]);
            text(w, nx + 0x50, ny, buf);
            ny += 0xb;
            if (i == 0)
                nx += 0x28;
        }
    }
    redraw_info(w);
}

/* ---- Legend ----------------------------------------------------------------------------- */

/* 0x4a6864: an optional title, then `n` colour swatches with labels. */
static void draw_legend_list(WmWindow *w, uint8_t colour, const uint32_t *labels, GfxRect *title_r,
                             const uint8_t *colours, const GfxRect *list, unsigned n, const char *title)
{
    GfxSurface *s = w->surface;
    int fh = gfx_font_height(FONT) & 0xff;
    GfxRect lr = {list->left + 0xc, list->right, list->top, list->top + fh - 1};
    gfx_font_select(FONT);
    font_colors(FONT, colour, colour);
    if (title) {
        gfx_fill_rect(s, title_r->left, title_r->top, title_r->right - title_r->left + 1,
                      title_r->bottom - title_r->top + 1, 0x13);
        gfx_font_draw_text(s, title_r->left, title_r->top, title_r, title_r, title, GFX_TEXT_CENTER);
    }
    font_colors(FONT, colour, colour);
    gfx_fill_rect(s, list->left, list->top, list->right - list->left + 1, list->bottom - list->top + 1, 0x13);
    for (unsigned i = 0; i < n; i++) {
        gfx_fill_rect(s, list->left, lr.top, 10, 10, colours[i]);
        if (labels)
            gfx_font_draw_text(s, lr.left, lr.top, &lr, &lr, xs(labels[i]), GFX_TEXT_NOJUSTIFY);
        lr.top += 10;
        lr.bottom += 10;
    }
}

/* 0x4a6254: the colour key of `layer`. */
static void draw_legend(WmWindow *w, unsigned layer)
{
    static const uint32_t k_levels[8] = {0x66fa9c, 0x66faa8, 0x66fab0, 0x66fab8, /* Lowest .. Highest */
                                         0x66fac4, 0x66fad0, 0x66fad8, 0x66fae4};
    static const uint32_t k_morals[9] = {0x66faf4, 0x66fb00, 0x66fb10, 0x66fb20, 0x66fb2c, /* Angelic .. Demonic */
                                         0x66fb38, 0x66fb44, 0x66fb50, 0x66fb5c};
    static const uint32_t k_strength[8] = {0x66fb6c, 0x66fb78, 0x66fb88, 0x66fb98, /* Weak .. Strong */
                                           0x66fba4, 0x66fbb0, 0x66fbb8, 0x66fbc4};
    static const uint32_t k_tenets[9] = {0x66fbf4, 0x66fc04, 0x66fc14, 0x66fc24, 0x66fc34,
                                         0x66fc40, 0x66fc4c, 0x66fc5c, 0x66fc6c};
    static const uint8_t k_pop[8] = {0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f};
    static const uint8_t k_tech[8] = {0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57};
    GfxSurface *s = w->surface;
    gfx_fill_rect(s, 0x19f, 0x19, 0x4b, 0x87, 0x13);
    if (layer == PV_LAYER_TERRAIN || layer == PV_LAYER_POP_SEL) {
        redraw_info(w);
        redraw_legend(w);
        return;
    }
    font_colors(FONT, 0x1a, 0x1a);
    gfx_font_select(FONT);
    int fh = gfx_font_height(FONT);
    GfxRect title_r = {0x19f, 0x1e9, 0x1b, 0x1b + fh - 1};
    GfxRect list = {0x1a3, 0x1e9, 0x1b + fh + 0x13, 0x88};
    const uint8_t *colours = NULL;
    const uint32_t *labels = NULL;
    unsigned n = 0;
    uint8_t colour = 0x1a;
    char title[60] = "";
    switch (layer) {
    case PV_LAYER_POPULATION:
        colours = k_pop, n = 8, labels = k_levels;
        snprintf(title, sizeof title, "%s", xs(0x66fbcc)); /* "POPULATION" */
        break;
    case PV_LAYER_TECH:
        colours = k_tech, n = 8, labels = k_levels;
        snprintf(title, sizeof title, "%s", xs(0x66fbd8)); /* "TECHNOLOGY" */
        break;
    case PV_LAYER_SIN:
        if (pv.sin != 0 && pv.sin < 8) {
            colours = pv.sin_colours[pv.sin], n = 9, labels = k_morals;
            colour = pv.sin == 1 ? 0x58 : pv.sin_colours[0][pv.sin];
            gfx_font_select(FONT);
            font_colors(FONT, colour, colour);
            int lh = gfx_font_height(FONT);
            gfx_font_draw_text(s, title_r.left, title_r.top, &title_r, &title_r, xsp(0x66c5fc + pv.sin * 4u),
                               GFX_TEXT_CENTER); /* the virtue */
            title_r.top += lh;
            title_r.bottom += lh;
            gfx_font_draw_text(s, title_r.left, title_r.top, &title_r, &title_r, xsp(0x66c654 + pv.sin * 4u),
                               GFX_TEXT_CENTER); /* the sin */
            list.top += lh;
            list.bottom += lh;
            draw_legend_list(w, colour, labels, &title_r, colours, &list, n, NULL);
            redraw_legend(w);
            return;
        }
        break;
    case PV_LAYER_TENETS:
        if (pv.tenet != 0) {
            colours = k_tech, n = 8, labels = k_strength;
            if (is_pressed(pv.tenet_btn[1]))
                snprintf(title, sizeof title, "%s", xs(0x66fc74)); /* "NAAAism" */
            else
                snprintf(title, sizeof title, xfmt(0x66fc7c, "s", "%s"),
                         pv.tenet <= 9 ? xs(k_tenets[pv.tenet - 1]) : "");
        }
        break;
    }
    draw_legend_list(w, colour, labels, &title_r, colours, &list, n, title);
    redraw_legend(w);
}

/* ---- The influence slider (0x49ed19 / 0x49eea4) -------------------------------------------- */

static GfxRect *slider_thumb(WmWindow *w) { return (GfxRect *)wm_get_data(w, SL_D_THUMB); }
static GfxSurface *slider_bmp(WmWindow *w, uint32_t off) { return (GfxSurface *)wm_get_data(w, off); }

static void help_for(WmWindow *control);

/* 0x49eea4 */
static intptr_t slider_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    GfxRect *thumb = slider_thumb(w);
    GfxSurface *bmp = slider_bmp(w, SL_D_BITMAP);
    int h = w->rect.bottom - w->rect.top + 1;
    int th = bmp ? bmp->height : 0;
    WmWindow *parent = w->parent;
    switch (msg) {
    case WM_MSG_CREATE: {
        int ww = w->rect.right - w->rect.left + 1;
        GfxSurface *under = gfx_surface_create(ww, h);
        if (!under)
            return 0;
        if (parent)
            gfx_blit(under, 0, 0, ww, h, parent->surface, w->rect.left, w->rect.top);
        wm_set_data(w, SL_D_UNDER, (intptr_t)under);
        break;
    }
    case WM_MSG_DESTROY: {
        GfxSurface *under = slider_bmp(w, SL_D_UNDER);
        gfx_surface_destroy(slider_bmp(w, SL_D_PRESSED));
        wm_set_data(w, SL_D_PRESSED, 0);
        gfx_surface_destroy(bmp);
        wm_set_data(w, SL_D_BITMAP, 0);
        free(thumb);
        wm_set_data(w, SL_D_THUMB, 0);
        if (under) {
            if (parent) {
                gfx_blit(parent->surface, w->rect.left, w->rect.top, under->width, under->height, under, 0, 0);
                redraw(parent, w->rect.left, w->rect.left + under->width - 1, w->rect.top,
                       w->rect.top + under->height - 1);
            }
            gfx_surface_destroy(under);
            wm_set_data(w, SL_D_UNDER, 0);
        }
        break;
    }
    case WM_MSG_MOUSEMOVE:
        if (wm_get_data(w, SL_D_DRAG) && thumb) {
            intptr_t grab = wm_get_data(w, SL_D_GRAB);
            intptr_t old = wm_send(w, w, SL_GET, 0, 0);
            wm_send(w, w, SL_MOVE, b - grab, 0);
            intptr_t now = wm_send(w, w, SL_GET, 0, 0);
            wm_send(parent, parent, PV_MSG_SLIDER, (intptr_t)w, now - old);
            wm_send(w, w, SL_DRAW, 0, 0);
        }
        break;
    case WM_MSG_LBUTTONDOWN:
        if (!thumb)
            break;
        wm_set_capture(w);
        wm_set_data(w, SL_D_DRAG, 1);
        wm_set_data(w, SL_D_CURRENT, wm_get_data(w, SL_D_PRESSED));
        if (!gfx_rect_contains(thumb, (int)a, (int)b)) {
            intptr_t old = wm_send(w, w, SL_GET, 0, 0);
            wm_send(w, w, SL_MOVE, b - (th >> 1), 0);
            intptr_t now = wm_send(w, w, SL_GET, 0, 0);
            wm_set_data(w, SL_D_GRAB, b - thumb->top);
            wm_send(parent, parent, PV_MSG_SLIDER, (intptr_t)w, now - old);
        } else {
            wm_set_data(w, SL_D_GRAB, b - thumb->top);
            wm_send(parent, parent, PV_MSG_SLIDER, (intptr_t)w, 0);
        }
        wm_send(w, w, SL_DRAW, 0, 0);
        break;
    case WM_MSG_LBUTTONUP:
        wm_release_capture();
        wm_set_data(w, SL_D_DRAG, 0);
        break;
    case WM_MSG_RBUTTONDOWN:
        help_for(w);
        break;
    case WM_MSG_HOVER:
        if (wm_get_mode() != 0)
            help_for(w);
        break;
    case WM_MSG_SHOW:
        if (a)
            wm_send(w, w, SL_DRAW, 0, 0);
        break;
    case SL_DRAW: {
        GfxSurface *under = slider_bmp(w, SL_D_UNDER);
        GfxSurface *cur = slider_bmp(w, SL_D_CURRENT);
        if (cur && under && thumb && parent) {
            GfxRect r = {w->rect.left, w->rect.left + under->width - 1, w->rect.top, w->rect.top + under->height - 1};
            gfx_blit(parent->surface, r.left, r.top, under->width, under->height, under, 0, 0);
            gfx_blit_transparent(parent->surface, thumb->left + r.left, thumb->top + r.top, cur->width, cur->height,
                                 cur, 0, 0, 0);
            wm_redraw(parent, &r);
        }
        break;
    }
    case SL_SET: {
        intptr_t range = h - th;
        if (a < 1)
            a = 0;
        if (a > range)
            a = range;
        wm_send(w, w, SL_MOVE, range - a, 0);
        return thumb ? range - thumb->top : 0;
    }
    case SL_GET:
        return thumb ? (h - th) - thumb->top : 0;
    case SL_STEPS:
        wm_set_data(w, SL_D_STEPS, a);
        break;
    case SL_LEVEL: {
        intptr_t steps = wm_get_data(w, SL_D_STEPS);
        if (steps < 2)
            steps = 1;
        intptr_t q = h / steps;
        if (q < 2)
            q = 1;
        return thumb ? thumb->top / q : 0;
    }
    case SL_ID:
        return wm_get_data(w, SL_D_ID);
    case SL_MOVE: {
        intptr_t range = h - th;
        if (a < 1)
            a = 0;
        if (a > range)
            a = range;
        if (thumb)
            rect_move_to(thumb, 0, (int)a);
        break;
    }
    }
    return wm_default_proc(w, msg, a, b);
}

/* 0x49ed19: a vertical slider `id` in `r` with thumb bitmaps `normal` /
 * `pressed`, the thumb at the bottom. */
static WmWindow *slider_create(WmWindow *parent, GfxRect *r, uint32_t id, uint32_t normal, uint32_t pressed, int steps)
{
    if (!parent)
        return NULL;
    WmWindow *w = wm_create_window(parent, r, slider_proc,
                                   WM_F_NOSURFACE | WM_F_NOFOCUS | WM_F_NOCLIP | WM_F_NOTITLE | WM_F_CONTROL, 0x24);
    if (!w)
        return NULL;
    GfxRect *thumb = malloc(sizeof *thumb);
    GfxSurface *bmp = gfx_load_bmp_resource(normal, NULL);
    if (!thumb || !bmp) {
        free(thumb);
        gfx_surface_destroy(bmp);
        wm_destroy(w);
        return NULL;
    }
    thumb->left = 0;
    thumb->right = bmp->width - 1;
    thumb->bottom = w->rect.bottom - w->rect.top;
    thumb->top = thumb->bottom - bmp->height + 1;
    GfxSurface *down = gfx_load_bmp_resource(pressed, NULL);
    wm_set_data(w, SL_D_BITMAP, (intptr_t)bmp);
    wm_set_data(w, SL_D_DRAG, 0);
    wm_set_data(w, SL_D_ID, (intptr_t)id);
    wm_set_data(w, SL_D_STEPS, steps);
    wm_set_data(w, SL_D_GRAB, 0);
    wm_set_data(w, SL_D_THUMB, (intptr_t)thumb);
    wm_set_data(w, SL_D_PRESSED, (intptr_t)down);
    wm_set_data(w, SL_D_CURRENT, (intptr_t)bmp);
    return w;
}

/* ---- Influence panel ------------------------------------------------------------------------- */

/* 0x49f761 / 0x4a1577 / 0x4a17ee: the panel backgrounds. */
static bool blit_bg(WmWindow *w, GfxSurface *bg, int x, int y, bool flush)
{
    if (!bg)
        return false;
    gfx_blit(w->surface, x, y, bg->width, bg->height, bg, 0, 0);
    if (flush)
        redraw(w, x, x + bg->width - 1, y, y + bg->height - 1);
    return true;
}
static bool draw_panel_bg(WmWindow *w, bool flush) { return blit_bg(w, pv.bg_panel, 4, 0xe2, flush); }  /* 0x49f761 */
static bool draw_sins_bg(WmWindow *w, bool flush) { return blit_bg(w, pv.bg_sins, 0x70, 0xe2, flush); } /* 0x4a1577 */

/* 0x4a17ee (the redraw rectangle is fixed) */
static bool draw_tenets_bg(WmWindow *w, bool flush)
{
    if (!pv.bg_tenets)
        return false;
    blit_bg(w, pv.bg_tenets, 0x70, 0xe2, false);
    if (flush)
        redraw(w, 0x70, 0x133, 0xe2, 0x149);
    return true;
}

static void show_sin_sliders(bool show) /* 0x4a1719 */
{
    for (int i = 1; i < 8; i++)
        if (pv.sliders[i])
            wm_show(pv.sliders[i], show);
}

static void destroy_sin_sliders(void) /* 0x4a1772 */
{
    for (int i = 1; i < 8; i++)
        if (pv.sliders[i]) {
            wm_show(pv.sliders[i], false);
            wm_destroy(pv.sliders[i]);
            pv.sliders[i] = NULL;
        }
}

/* 0x4a161a */
static bool create_sin_sliders(WmWindow *w)
{
    GfxRect r = {0x89, 0x95, 0xe7, 0x144};
    for (unsigned i = 0; i < 7; i++) {
        GfxRect rr = r;
        pv.sliders[i + 1] = slider_create(w, &rr, PV_ID_SIN_SLIDER + i, 0x12f + i, 0x154 + i, 3);
        if (!pv.sliders[i + 1])
            return false;
        wm_send(pv.sliders[i + 1], pv.sliders[i + 1], SL_SET, 0x22, 0);
        r.left += 0x18;
        r.right += 0x18;
    }
    show_sin_sliders(false);
    return true;
}

static void show_tenet_buttons(bool show) /* 0x4a1c5e */
{
    for (int i = 0; i < 10; i++)
        if (pv.tenet_btn[i]) {
            ctl_button_show(pv.tenet_btn[i], show);
            if (show)
                ctl_button_draw(pv.tenet_btn[i], CTL_DRAW_BITMAP | CTL_DRAW_FLUSH);
        }
}

static void destroy_tenet_buttons(void) /* 0x4a1cf7 */
{
    for (int i = 0; i < 10; i++)
        if (pv.tenet_btn[i]) {
            ctl_button_show(pv.tenet_btn[i], false);
            ctl_button_destroy(pv.tenet_btn[i]);
            pv.tenet_btn[i] = NULL;
        }
}

static WmWindow *button(WmWindow *parent, int x, int y, uint32_t id, uint32_t style, uint32_t bitmap)
{
    GfxSurface *bmp = gfx_load_bmp_resource(bitmap, NULL);
    if (!bmp)
        return NULL;
    GfxRect r = {x, x + bmp->width - 1, y, y + bmp->height - 1};
    WmWindow *b = ctl_button_create(parent, &r, id, style, bmp);
    if (!b)
        gfx_surface_destroy(bmp);
    return b;
}

/* 0x4a1888: AAAA NAAA OCRA HAHA OPRA HOHO SUSA RALF SUMA ALF. (The
 * original sizes the second button of a column like the first.) */
static bool create_tenet_buttons(WmWindow *w)
{
    static const struct {
        int x, y;
        uint32_t bmp;
    } k[10] = {
        {0x75, 0xe9, 0x141},  {0x75, 0x125, 0x142}, {0x9b, 0xf7, 0x143},  {0x9b, 0x119, 0x145}, {0xc1, 0xf7, 0x144},
        {0xc1, 0x119, 0x146}, {0xe7, 0xe9, 0x147},  {0xe7, 0x125, 0x149}, {0x10d, 0xe9, 0x148}, {0x10d, 0x125, 0x14a},
    };
    for (unsigned i = 0; i < 10; i++)
        pv.tenet_btn[i] =
            button(w, k[i].x, k[i].y, PV_ID_TENET_BUTTON + i, CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE, k[i].bmp);
    for (int i = 0; i < 10; i++)
        if (!pv.tenet_btn[i])
            return false;
    show_tenet_buttons(false);
    return true;
}

static void scientist_info(bool flush) /* 0x4a1d73 */
{
    WmWindow *w = pv.win;
    if (!w)
        return;
    GfxRect r = {0x71, 0x133, 0xe3, 0x149};
    gfx_fill_rect(w->surface, r.left, r.top, r.right - r.left + 1, r.bottom - r.top + 1, 0x67);
    const PlanetSector *s = planet_sector(sel_x(), sel_y());
    if (s->tech < 0xf) {
        uint32_t old = gfx_font_select(FONT);
        font_colors(FONT, 0x1a, 0x1a);
        int fh = gfx_font_height(FONT);
        GfxRect t = k_sci;
        t.bottom = t.top + fh - 1;
        char buf[100], num[FMT_NUM_LEN];
        snprintf(buf, sizeof buf, xfmt(0x66eee8, "dd", "%d, %d"), sel_x(), sel_y()); /* "Sector:  %d, %d" */
        gfx_font_draw_text(w->surface, t.left, t.top, &t, &t, buf, 0);
        t.top += fh + 1;
        t.bottom += fh + 1;
        snprintf(buf, sizeof buf, xfmt(0x66eef8, "s", "%s"), fmt_thousands(num, s->pop)); /* "%s Inhabitants" */
        gfx_font_draw_text(w->surface, t.left, t.top, &t, &t, buf, 0);
        t.top += fh + 1;
        t.bottom += fh + 1;
        if (s->pop == 0)
            snprintf(buf, sizeof buf, "%s", xs(0x66ef18)); /* "Tech Level:  None" */
        else
            snprintf(buf, sizeof buf, xfmt(0x66ef08, "s", "%s"), tech_name(s->tech));
        gfx_font_draw_text(w->surface, t.left, t.top, &t, &t, buf, 0);
        if (old != FONT && old != 0)
            gfx_font_select(old);
    }
    if (flush)
        wm_redraw(w, &r);
}

/* 0x4a2025 */
static bool draw_tech_bg(bool flush)
{
    if (!pv.bg_tech)
        return false;
    scientist_info(flush);
    return true;
}

/* 0x4a205d, 0x4a207c, 0x4a2087: the scientist mode's controls (none). */
static bool create_tech_controls(void) { return true; }
static void show_tech_controls(bool show) { (void)show; }
static void destroy_tech_controls(void) {}

/* 0x4a0f84: the price of the influence set up. */
static uint32_t influence_cost(void)
{
    uint32_t cost = 0, amount = pv.amount;
    if (pv.influence == PV_INFLUENCE_ARTIST) {
        for (int i = 1; i < 8; i++) {
            int level = (int)wm_send(pv.sliders[i], pv.sliders[i], SL_LEVEL, 0, 0);
            cost += (uint32_t)abs(level - 1) * amount * 0xc35;
        }
    } else if (pv.influence == PV_INFLUENCE_FAMOUS) {
        WmWindow **t = pv.tenet_btn;
        if (is_pressed(t[0]) || is_pressed(t[1]))
            cost = amount * 0xc35;
        if (is_pressed(t[6]) || is_pressed(t[8]))
            cost += amount * 0xc35;
        if (is_pressed(t[7]) || is_pressed(t[9]))
            cost += amount * 0xc35;
        if (is_pressed(t[2]) || is_pressed(t[3]) || is_pressed(t[4]) || is_pressed(t[5]))
            cost += amount * 0xc35;
    } else if (pv.influence == PV_INFLUENCE_SCIENTIST) {
        if (amount != 0)
            cost = ((amount >> 4) + 1) * (g_planet.max_tech + 1u) * amount * 0x4074;
    }
    return cost;
}

/* 0x4a11f0 */
static void draw_cost(WmWindow *w)
{
    GfxRect r = {7, 0x6a, 0x140, 0x148};
    char buf[40], num[FMT_NUM_LEN];
    pv.cost = influence_cost();
    gfx_fill_rect(w->surface, r.left, r.top, r.right - r.left + 1, r.bottom - r.top + 1, 0x1a);
    snprintf(buf, sizeof buf, xfmt(0x66eedc, "s", "%s"), fmt_thousands(num, pv.cost)); /* "COST:  %s" */
    gfx_font_select(FONT);
    font_colors(FONT, 0x13, 0x13);
    gfx_font_draw_text(w->surface, r.left, r.top + 1, &r, &r, buf, 0);
    wm_redraw(w, &r);
}

/* 0x4a12c6: the influence mode follows the pressed layer button. */
static void influence_mode(void)
{
    unsigned old = pv.influence;
    if (is_pressed(pv.btn_tech)) {
        pv.influence = PV_INFLUENCE_SCIENTIST;
    } else if (is_pressed(pv.btn_sin)) {
        pv.influence = PV_INFLUENCE_ARTIST;
    } else if (is_pressed(pv.btn_tenets)) {
        pv.influence = PV_INFLUENCE_FAMOUS;
    } else {
        pv.influence = PV_INFLUENCE_NONE;
        if (pv.sliders[0])
            wm_show(pv.sliders[0], false);
        ctl_button_show(pv.wield, false);
        show_sin_sliders(false);
        show_tenet_buttons(false);
        show_tech_controls(false);
        return;
    }
    if ((old & 0xff) == pv.influence)
        return;
    if (old == PV_INFLUENCE_NONE)
        draw_panel_bg(pv.win, pv.influence != PV_INFLUENCE_SCIENTIST);
    if (pv.sliders[0])
        wm_show(pv.sliders[0], true);
    if (pv.wield) {
        ctl_button_show(pv.wield, true);
        ctl_button_draw(pv.wield, CTL_DRAW_BITMAP | CTL_DRAW_FLUSH);
    }
    if (pv.influence == PV_INFLUENCE_ARTIST) {
        show_tenet_buttons(false);
        show_tech_controls(false);
        draw_sins_bg(pv.win, true);
        show_sin_sliders(true);
    } else if (pv.influence == PV_INFLUENCE_FAMOUS) {
        show_tech_controls(false);
        show_sin_sliders(false);
        draw_tenets_bg(pv.win, true);
        show_tenet_buttons(true);
    } else {
        show_sin_sliders(false);
        show_tenet_buttons(false);
        draw_tech_bg(true);
        show_tech_controls(true);
    }
    if (old == PV_INFLUENCE_NONE && pv.influence == PV_INFLUENCE_SCIENTIST && pv.bg_panel)
        redraw(pv.win, 4, pv.bg_panel->width + 3, 0xe2, pv.bg_panel->height + 0xe1);
    draw_cost(pv.win);
}

/* 0x4a076c: the amount slider and Wield button at panel (x, y), then the
 * mode-specific controls. */
static bool create_influence_controls(WmWindow *w, int x, int y)
{
    GfxRect r = {x + 0x13c, x + 0x157, y + 6, y + 0x62};
    pv.sliders[0] = slider_create(w, &r, PV_ID_AMOUNT, 0x12e, 0x12e, r.bottom - r.top + 1);
    if (!pv.sliders[0])
        return false;
    GfxSurface *bmp = gfx_load_bmp_resource(0x515, NULL);
    if (!bmp)
        return false;
    GfxRect br = {r.left + 0x24, r.left + 0x24 + bmp->width - 1, r.bottom - bmp->height - 1, r.bottom};
    pv.wield = ctl_button_create(w, &br, PV_ID_WIELD, CTL_STYLE_BITMAP, bmp);
    if (!pv.wield) {
        gfx_surface_destroy(bmp);
        return false;
    }
    wm_show(pv.sliders[0], false);
    ctl_button_show(pv.wield, false);
    bool a = create_sin_sliders(w);
    bool b = create_tenet_buttons(w);
    bool c = create_tech_controls();
    return a && b && c;
}

static void destroy_influence_controls(void) /* 0x4a08a8 */
{
    if (pv.sliders[0]) {
        wm_show(pv.sliders[0], false);
        wm_destroy(pv.sliders[0]);
        pv.sliders[0] = NULL;
    }
    if (pv.wield) {
        ctl_button_show(pv.wield, false);
        ctl_button_destroy(pv.wield);
        pv.wield = NULL;
    }
    destroy_sin_sliders();
    destroy_tenet_buttons();
    destroy_tech_controls();
}

static void influence_close(void) /* 0x4a072d */
{
    destroy_influence_controls();
    pv.influence = PV_INFLUENCE_NONE;
    pv.cost = 0;
    pv.amount = 0;
    if (pv.win)
        draw_info(pv.win, -1, -1);
}

/* 0x49f682: the panel bitmaps and the influence controls. */
static bool influence_init(WmWindow *w)
{
    if (!(pv.bg_panel = gfx_load_bmp_resource(0x137, NULL)))
        return false;
    if (!(pv.bg_sins = gfx_load_bmp_resource(0x14b, NULL)))
        return false;
    if (!(pv.bg_tenets = gfx_load_bmp_resource(0x140, NULL)))
        return false;
    if (!(pv.bg_tech = gfx_load_bmp_resource(0x14e, NULL)))
        return false;
    draw_panel_bg(w, false);
    draw_sins_bg(w, false);
    return create_influence_controls(w, 4, 0xe2);
}

static void influence_free_bitmaps(void) /* port-only: the original never frees them */
{
    gfx_surface_destroy(pv.bg_panel);
    gfx_surface_destroy(pv.bg_sins);
    gfx_surface_destroy(pv.bg_tenets);
    gfx_surface_destroy(pv.bg_tech);
    pv.bg_panel = pv.bg_sins = pv.bg_tenets = pv.bg_tech = NULL;
}

/* 0x4a092c: a slider moved by `delta`. The amount slider adds to the
 * amount; a sin slider snaps to virtue / neither / sin and becomes the
 * layer's sin. */
static void slider_moved(WmWindow *w, WmWindow *slider, int delta)
{
    intptr_t id = wm_send(slider, slider, SL_ID, 0, 0);
    if (id == PV_ID_AMOUNT) {
        uint16_t v = (uint16_t)(pv.amount + delta);
        pv.amount = v < 0x7f ? v : 0x7f;
        draw_cost(w);
        return;
    }
    GfxSurface *bmp = slider_bmp(slider, SL_D_BITMAP);
    int th = bmp ? bmp->height : 0, h = slider->rect.bottom - slider->rect.top + 1;
    uint16_t pos = (uint16_t)wm_send(slider, slider, SL_GET, 0, 0);
    uint16_t range = (uint16_t)(h - th);
    if (pos < range / 3)
        range = 0;
    else if (pos <= (uint16_t)(range / 3 * 2))
        range = (uint16_t)(h / 2 - th / 2);
    wm_send(slider, slider, SL_SET, range, 0);
    draw_cost(w);
    uint8_t old = pv.sin;
    pv.sin = (uint8_t)(id - 0xf);
    if (old != pv.sin) {
        WmWindow *prev = old < 8 ? pv.sliders[old] : NULL;
        if (prev) {
            wm_set_data(prev, SL_D_CURRENT, wm_get_data(prev, SL_D_BITMAP));
            wm_send(prev, prev, SL_DRAW, 0, 0);
        }
        draw_legend(w, pv.layer);
        wm_send(pv.win, pv.win, PV_MSG_REFRESH, 0, 0);
    }
}

/* 0x4a0b76: a tenet button; picks the tenet the layer shows and releases
 * the buttons that contradict it. */
static void tenet_button(WmWindow *b)
{
    WmWindow *w = b->parent, **t = pv.tenet_btn;
    uint8_t old = pv.tenet;
    wm_raise(w);
    switch (ctl_get_id(b)) {
    case 0x424: /* AAAA */
        pv.tenet = 0;
        set_pressed(t[1], false);
        draw_cost(w);
        break;
    case 0x425: /* NAAA: no afterlife, so no fates and pairs either */
        pv.tenet = 0;
        for (int i = 0; i < 10; i++)
            if (i != 1)
                set_pressed(t[i], false);
        draw_cost(w);
        break;
    case 0x426: /* OCRA */
        pv.tenet = 4;
        set_pressed(t[1], false), set_pressed(t[3], false), set_pressed(t[4], false), set_pressed(t[5], false);
        draw_cost(w);
        break;
    case 0x427: /* HAHA */
        pv.tenet = 1;
        set_pressed(t[1], false), set_pressed(t[2], false), set_pressed(t[4], false), set_pressed(t[5], false);
        draw_cost(w);
        break;
    case 0x428: /* OPRA */
        pv.tenet = 3;
        set_pressed(t[1], false), set_pressed(t[2], false), set_pressed(t[3], false), set_pressed(t[5], false);
        draw_cost(w);
        break;
    case 0x429: /* HOHO */
        pv.tenet = 2;
        set_pressed(t[1], false), set_pressed(t[2], false), set_pressed(t[3], false), set_pressed(t[4], false);
        draw_cost(w);
        break;
    case 0x42a: /* SUSA */
        pv.tenet = 8;
        set_pressed(t[1], false), set_pressed(t[8], false);
        draw_cost(w);
        break;
    case 0x42b: /* RALF */
        pv.tenet = 6;
        set_pressed(t[1], false), set_pressed(t[9], false);
        draw_cost(w);
        break;
    case 0x42c: /* SUMA */
        pv.tenet = 7;
        set_pressed(t[1], false), set_pressed(t[6], false);
        draw_cost(w);
        break;
    case 0x42d: /* ALF */
        pv.tenet = 5;
        set_pressed(t[1], false), set_pressed(t[7], false);
        draw_cost(w);
        break;
    }
    pv.tenet++;
    if (pv.tenet != old)
        wm_send(w, w, PV_MSG_REFRESH, 0, 1);
}

/* 0x47f8fe, the Soulview's SOUL name generator (ui/soulview.h). */
static void soul_name(uint32_t seed, char *out)
{
    if (planetview_hooks.soul_name)
        planetview_hooks.soul_name(seed, out);
    else
        soulview_soul_name(seed, out);
}

static void msgbox(uint32_t title, const char *msg, uint32_t buttons, uint32_t timeout)
{
    char b[64];
    ctl_message_box(-1, -1, xs(title), msg, xbuttons(buttons, b, sizeof b), timeout);
}

/* 0x49f804: Wield Influence on sector (x, y): queue the influence (it
 * spreads over the next years, game/planet.c), pay and announce it. */
static void wield(WmWindow *w, int x, int y)
{
    static const uint32_t k_artist[27] = {
        0x66e244, 0x66e258, 0x66e26c, 0x66e27c, 0x66e290, 0x66e2a8, 0x66e2c0, 0x66e2d8, 0x66e2f0,
        0x66e308, 0x66e320, 0x66e334, 0x66e348, 0x66e360, 0x66e380, 0x66e3a4, 0x66e3c0, 0x66e3e0,
        0x66e3fc, 0x66e40c, 0x66e420, 0x66e438, 0x66e450, 0x66e46c, 0x66e48c, 0x66e4b0, 0x66e4cc,
    };
    static const uint32_t k_famous[2] = {0x66e5b0, 0x66e5c4};
    static const uint32_t k_science[39] = {
        0x66e710, 0x66e728, 0x66e740, 0x66e758, 0x66e76c, 0x66e780, 0x66e798, 0x66e7b4, 0x66e7d0, 0x66e7ec,
        0x66e80c, 0x66e830, 0x66e854, 0x66e870, 0x66e88c, 0x66e8b0, 0x66e8d4, 0x66e8f4, 0x66e914, 0x66e930,
        0x66e950, 0x66e970, 0x66e98c, 0x66e9ac, 0x66e9cc, 0x66e9ec, 0x66ea0c, 0x66ea28, 0x66ea44, 0x66ea64,
        0x66ea84, 0x66eaa4, 0x66eac4, 0x66eae0, 0x66eafc, 0x66eb18, 0x66eb30, 0x66eb50, 0x66eb7c,
    };
    static const uint32_t k_power[8] = {0x66eba0, 0x66ebb8, 0x66ebd0, 0x66ebe8, 0x66ebfc, 0x66ec14, 0x66ec30, 0x66ec44};
    static const uint32_t k_wit[6] = {0x66ec58, 0x66ec6c, 0x66ec80, 0x66eca8, 0x66eccc, 0x66ecdc};
    static char msg[600]; /* 0x6386a0 */
    char name[60];
    PlanetState *ps = &g_state.planet;
    const PlanetSector *s = planet_sector(x, y);

    wm_raise(w);
    if (s->pop == 0 || pv.cost == 0)
        return;
    if (pv.influence != PV_INFLUENCE_SCIENTIST && bignum_less(g_state.pennies, (int32_t)pv.cost)) {
        msgbox(0x66e220, xs(0x66e200), 0x66e1f8, 120000); /* not enough Pennies */
        return;
    }
    if (pv.influence == PV_INFLUENCE_ARTIST) {
        if (ps->n_a > 0x3f) {
            msgbox(0x66e514, xs(0x66e4dc), 0x66e4d4, 120000); /* too weary */
            return;
        }
        for (unsigned i = 0; i < ps->n_a; i++)
            if (ps->rec_a[i].x == (uint32_t)x && ps->rec_a[i].y == (uint32_t)y) {
                msgbox(0x66e570, xs(0x66e52c), 0x66e520, 0); /* still active */
                return;
            }
        PlanetRecA *r = &ps->rec_a[ps->n_a];
        for (int i = 0; i < 7; i++) {
            int16_t level = (int16_t)wm_send(pv.sliders[i + 1], pv.sliders[i + 1], SL_LEVEL, 0, 0);
            r->v[i] = (uint16_t)(-((level - 1) * (int)pv.amount));
        }
        r->x = (uint32_t)x;
        r->y = (uint32_t)y;
        ps->n_a++;
        int who = rand_range(0, 0x1a);
        soul_name((uint32_t)rand_range(1, 0x7fffffff), name);
        snprintf(msg, sizeof msg, xfmt(0x66e584, "ss", "%s %s"), xs(k_artist[who]), name);
    } else if (pv.influence == PV_INFLUENCE_FAMOUS) {
        WmWindow **t = pv.tenet_btn;
        char what[32] = "";
        if (ps->n_b > 0x3f) {
            msgbox(0x66e60c, xs(0x66e5d4), 0x66e5cc, 120000);
            return;
        }
        for (unsigned i = 0; i < ps->n_b; i++)
            if (ps->rec_b[i].x == (uint32_t)x && ps->rec_b[i].y == (uint32_t)y) {
                msgbox(0x66e668, xs(0x66e624), 0x66e618, 0);
                return;
            }
        /* original: tenets whose buttons are up keep the record's old values */
        PlanetRecB *r = &ps->rec_b[ps->n_b];
        int16_t a = (int16_t)pv.amount;
        if (is_pressed(t[0])) {
            r->v[TENET_AAAA] = (uint16_t)a;
            snprintf(what, sizeof what, "%s", xs(0x66e67c)); /* AAAA */
        } else if (is_pressed(t[1])) {
            r->v[TENET_AAAA] = (uint16_t)-a;
            snprintf(what, sizeof what, "%s", xs(0x66e684)); /* NAAA */
        }
        r->v[TENET_HAHA] = is_pressed(t[3]) ? (uint16_t)a : 0;
        r->v[TENET_HOHO] = is_pressed(t[5]) ? (uint16_t)a : 0;
        r->v[TENET_OPRA] = is_pressed(t[4]) ? (uint16_t)a : 0;
        r->v[TENET_OCRA] = is_pressed(t[2]) ? (uint16_t)a : 0;
        for (unsigned f = 1; f < 5; f++)
            if (r->v[f] != 0) {
                strncat(what, xsp(0x66c590 + f * 4), sizeof what - strlen(what) - 1);
                break;
            }
        if (is_pressed(t[7])) { /* RALF */
            r->v[TENET_RALF] = (uint16_t)a;
            r->v[TENET_ALF] = (uint16_t)-a;
            strncat(what, xs(0x66e68c), sizeof what - strlen(what) - 1);
        } else if (is_pressed(t[9])) { /* ALF */
            r->v[TENET_ALF] = (uint16_t)a;
            r->v[TENET_RALF] = (uint16_t)-a;
            strncat(what, xs(0x66e694), sizeof what - strlen(what) - 1);
        }
        if (is_pressed(t[6])) { /* SUSA */
            r->v[TENET_SUSA] = (uint16_t)a;
            r->v[TENET_SUMA] = (uint16_t)-a;
            strncat(what, xs(0x66e698), sizeof what - strlen(what) - 1);
        } else if (is_pressed(t[8])) { /* SUMA */
            r->v[TENET_SUMA] = (uint16_t)a;
            r->v[TENET_SUSA] = (uint16_t)-a;
            strncat(what, xs(0x66e6a0), sizeof what - strlen(what) - 1);
        }
        strncat(what, xs(0x66e6a8), sizeof what - strlen(what) - 1); /* "ism" */
        r->x = (uint32_t)x;
        r->y = (uint32_t)y;
        ps->n_b++;
        int who = rand_range(0, 1);
        soul_name((uint32_t)rand_range(1, 0x7fffffff), name);
        snprintf(msg, sizeof msg, xfmt(0x66e6ac, "sss", "%s %s: %s"), xs(k_famous[who]), name, what);
    } else if (pv.influence == PV_INFLUENCE_SCIENTIST) {
        if (s->tech > 0xd) {
            msgbox(0x66ed38, xs(0x66ecf0), 0x66ece4, 120000); /* already at the pinnacle */
            return;
        }
        if (ps->n_c > 0x3f) {
            msgbox(0x66ed8c, xs(0x66ed54), 0x66ed4c, 120000);
            return;
        }
        if (bignum_less(g_state.pennies, (int32_t)pv.cost)) {
            msgbox(0x66edbc, xs(0x66ed9c), 0x66ed94, 120000);
            return;
        }
        uint8_t v = (uint8_t)mul_div_clamp(pv.amount, 0x51, 0xa0);
        for (unsigned i = 0; i < ps->n_c; i++)
            if (ps->rec_c[i].x == (uint32_t)x && ps->rec_c[i].y == (uint32_t)y) {
                msgbox(0x66ee24, xs(0x66ede4), 0x66edd8, 0);
                return;
            }
        PlanetRecC *r = &ps->rec_c[ps->n_c];
        r->left = v;
        r->initial = v;
        r->x = (uint32_t)x;
        r->y = (uint32_t)y;
        ps->n_c++;
        int who = rand_range(0, 0x26);
        int power = rand_range(0, 7);
        int wit = rand_range(0, 5);
        unsigned next = s->tech + 1u;
        if (next > 0xd)
            next = 0xe;
        soul_name((uint32_t)rand_range(1, 0x7fffffff), name);
        snprintf(msg, sizeof msg, xfmt(0x66ee38, "sssss", "%s %s %s %s: %s"), xs(k_power[power]), xs(k_wit[wit]),
                 xs(k_science[who]), name, tech_name(next));
    }
    bignum_sub(&g_state.pennies, pv.cost);
    if (planetview_hooks.status_redraw)
        planetview_hooks.status_redraw();
    if (pv.cost != 0) {
        snd_play(0x10056, 2);
        msgbox(0x66eec0, msg, 0x66eeb8, 120000); /* "Demiurge Influences Planet" */
    }
}

/* ---- Help ---------------------------------------------------------------------------- */

/* 0x4a4a2d: the help balloon of a Planetview control. */
static void help_for(WmWindow *c)
{
    static const struct {
        uint16_t id;
        uint32_t va;
    } k[] = {
        {1000, 0x66ef84},  {0x3e9, 0x66f000}, {0x3ea, 0x66f094}, {0x3ed, 0x66ef64}, {0x3ee, 0x66f120},
        {0x3fc, 0x66f1b0}, {0x3fd, 0x66f1d4}, {0x3fe, 0x66f1fc}, {0x3ff, 0x66f224}, {0x400, 0x66f24c},
        {0x409, 0x66f27c}, {0x40a, 0x66f2b8}, {0x410, 0x66f2ec}, {0x411, 0x66f348}, {0x412, 0x66f3a4},
        {0x413, 0x66f404}, {0x414, 0x66f460}, {0x415, 0x66f4b8}, {0x416, 0x66f510}, {0x424, 0x66f56c},
        {0x425, 0x66f5bc}, {0x426, 0x66f60c}, {0x427, 0x66f668}, {0x428, 0x66f6d0}, {0x429, 0x66f72c},
        {0x42a, 0x66f78c}, {0x42b, 0x66f854}, {0x42c, 0x66f7f0}, {0x42d, 0x66f8a8},
    };
    intptr_t id = wm_get_data(c, 0);
    int x, y;
    control_centre(c, &x, &y);
    uint32_t va = 0x66f944; /* "I have no idea what this button does." */
    if (id == 1)
        va = wm_is_minimized(c->parent) ? 0x66f8fc : 0x66f920;
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (k[i].id == id)
            va = k[i].va;
    help(x, y, xs(va));
}

/* 0x49e663: the disaster window's. (The original offsets the position by
 * the Planetview's rectangle instead of the disaster window's.) */
static void disaster_help(WmWindow *c)
{
    intptr_t id = wm_get_data(c, 0);
    int x, y;
    control_centre(c, &x, &y);
    if (id == 1)
        help(x, y, xs(wm_is_minimized(c->parent) ? 0x66e188 : 0x66e1ac));
    else if (id == 1000)
        help(x, y, xs(0x66e168));
    else
        help(x, y, xs(0x66e1d0));
}

/* ---- The window -------------------------------------------------------------------------- */

/* 0x4a36bd */
static void press_rotation(bool pressed)
{
    set_pressed(pv.btn_up, pressed);
    set_pressed(pv.btn_down, pressed);
    set_pressed(pv.btn_right, pressed);
    set_pressed(pv.btn_left, pressed);
}

/* 0x4a3718: stops the rotation and turns sector (x, y) (x = 0xff: the
 * selected one) to the middle of the near side. */
static void centre_on(WmWindow *w, int x, int y)
{
    if (!w)
        return;
    if (pv.rotating)
        press_rotation(false);
    if (x == 0xff) {
        x = sel_x();
        y = sel_y();
    }
    pv.rotating = false;
    pv.dx = pv.dy = 0;
    pv.off_x = x * 10 - 0x9b;
    pv.off_y = y * 10 - 0x9b;
    draw_globe(w->surface, GLOBE_X, GLOBE_Y, GLOBE, GLOBE);
    wm_redraw(w, &k_front);
    wm_redraw(w, &k_back);
    draw_info(w, -1, -1);
}

/* 0x4a5167 */
static void planet_free_map(void)
{
    free(g_state.planet.map);
    g_state.planet.map = NULL;
    g_state.planet.map_size = 0;
}

/* 0x4a50f6 planet_alloc_map. (Port: a smaller map from a save is
 * replaced, the view draws all of it.) */
static bool planet_alloc_map(uint32_t size)
{
    if (g_state.planet.map && g_state.planet.map_size >= size)
        return true;
    if (size == 0)
        return false;
    uint8_t *m = calloc(1, size);
    if (!m)
        return false;
    if (g_state.planet.map)
        memcpy(m, g_state.planet.map, g_state.planet.map_size);
    free(g_state.planet.map);
    g_state.planet.map = m;
    g_state.planet.map_size = size;
    return true;
}

/* rc_load_resource(0x14d), decoded to native words. */
static uint32_t *load_lut(void)
{
    long size = rc_resource_size(0x14d);
    if (size < GLOBE * GLOBE * 4)
        return NULL;
    PlatFile *f = rc_open_resource(0x14d);
    if (!f)
        return NULL;
    uint8_t *raw = malloc((size_t)GLOBE * GLOBE * 4);
    uint32_t *lut = malloc((size_t)GLOBE * GLOBE * sizeof *lut);
    bool ok = raw && lut && plat_file_read(f, raw, 1, (size_t)GLOBE * GLOBE * 4) == (size_t)GLOBE * GLOBE * 4;
    plat_file_close(f);
    if (ok)
        for (size_t i = 0; i < (size_t)GLOBE * GLOBE; i++)
            lut[i] = (uint32_t)raw[i * 4] | (uint32_t)raw[i * 4 + 1] << 8 | (uint32_t)raw[i * 4 + 2] << 16 |
                     (uint32_t)raw[i * 4 + 3] << 24;
    free(raw);
    if (!ok) {
        free(lut);
        return NULL;
    }
    return lut;
}

/* 0x4a4fb3: WM_MSG_CREATE. */
static bool init_window(WmWindow *w)
{
    if (!pv.sectors)
        return false;
    if (!planet_alloc_map(PLANET_MAP_SIZE))
        return false;
    if (!pv.lut && !(pv.lut = load_lut()))
        return false;
    wm_send(w, w, PV_MSG_RESET, 0, 0);
    return true;
}

/* 0x4a5077: WM_MSG_CTL_RELEASE. */
static void free_window(void)
{
    gfx_surface_destroy(pv.sectors);
    pv.sectors = NULL;
    planet_free_map();
    free(pv.lut);
    pv.lut = NULL;
}

/* The rotation buttons (0x3fc .. 0x3ff). */
static void rotation_button(WmWindow *b, int8_t dx, int8_t dy)
{
    if (!pv.rotating) {
        sector_colour_one(sel_x(), sel_y());
        draw_globe_rect(pv.win->surface, &pv.sel_rect);
    } else {
        select_rect();
    }
    WmWindow *all[4] = {pv.btn_up, pv.btn_down, pv.btn_left, pv.btn_right};
    for (int i = 0; i < 4; i++)
        if (all[i] != b)
            set_pressed(all[i], false);
    pv.rotating = ctl_is_pressed(b);
    pv.dx = dx;
    pv.dy = dy;
}

static void control_clicked(WmWindow *c)
{
    WmWindow *w = c->parent;
    switch (ctl_get_id(c)) {
    case PV_ID_POPULATION:
        pv.layer = (uint16_t)((pv.layer & 1) ^ 1);
        set_pressed(pv.btn_tech, false);
        set_pressed(pv.btn_sin, false);
        set_pressed(pv.btn_tenets, false);
        influence_mode();
        break;
    case 1: /* the minimize button */
        break;
    case PV_ID_TECH:
        pv.layer = PV_LAYER_TECH;
        set_pressed(pv.btn_pop, false);
        set_pressed(pv.btn_sin, false);
        set_pressed(pv.btn_tenets, false);
        influence_mode();
        break;
    case PV_ID_SIN:
        pv.layer = PV_LAYER_SIN;
        set_pressed(pv.btn_pop, false);
        set_pressed(pv.btn_tech, false);
        set_pressed(pv.btn_tenets, false);
        influence_mode();
        break;
    case PV_ID_CLOSE:
        if (planetview_hooks.menu_command)
            planetview_hooks.menu_command(0x18);
        else
            planetview_create(NULL);
        break;
    case PV_ID_TENETS:
        pv.layer = PV_LAYER_TENETS;
        set_pressed(pv.btn_pop, false);
        set_pressed(pv.btn_tech, false);
        set_pressed(pv.btn_sin, false);
        influence_mode();
        break;
    case PV_ID_UP:
        rotation_button(c, 0, 1);
        break;
    case PV_ID_DOWN:
        rotation_button(c, 0, -1);
        break;
    case PV_ID_LEFT:
        rotation_button(c, 1, 0);
        break;
    case PV_ID_RIGHT:
        rotation_button(c, -1, 0);
        break;
    case PV_ID_STOP:
        if (pv.rotating)
            select_rect();
        press_rotation(false);
        pv.rotating = false;
        pv.dx = pv.dy = 0;
        break;
    case PV_ID_WIELD:
        wield(w, sel_x(), sel_y());
        break;
    default:
        tenet_button(c);
        break;
    }
    if (!pv.rotating)
        pv.dx = pv.dy = 0;
    if (!pv.win)
        return;
    draw_legend(w, pv.layer);
    wm_send(w, w, PV_MSG_REFRESH, 0, 0);
}

/* Moves a window dragged by its body (message 8 of both windows). */
static void drag_window(WmWindow *w, int x, int y, int gx, int gy)
{
    GfxRect r = w->rect;
    rect_move_to(&r, w->rect.left + x - gx, w->rect.top + y - gy);
    wm_set_rect(w, &r);
}

/* 0x4a3827 */
static intptr_t planetview_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_CREATE:
        if (!init_window(w))
            return 0;
        break;
    case WM_MSG_MOUSEMOVE:
        if (pv.dragging)
            drag_window(w, (int)a, (int)b, pv.grab_x, pv.grab_y);
        /* TODO(WP7): with influence on, the cursor (0x6464cc, 0x64653c over
         * the globe when there's a price) comes from the game shell. */
        break;
    case WM_MSG_LBUTTONDOWN:
        wm_raise(w);
        if (outside_globe((int)a, (int)b)) {
            pv.grab_x = (int)a;
            pv.grab_y = (int)b;
            pv.dragging = true;
            wm_set_capture(w);
        } else if (pv.layer == PV_LAYER_POP_SEL) {
            pv.layer = PV_LAYER_POPULATION;
            set_pressed(pv.btn_pop, true);
            set_pressed(pv.btn_tech, false);
            set_pressed(pv.btn_sin, false);
            set_pressed(pv.btn_tenets, false);
            draw_legend(w, pv.layer);
            wm_send(w, w, PV_MSG_REFRESH, 0, 1);
        } else {
            draw_info(w, (int)a, (int)b);
        }
        break;
    case WM_MSG_RBUTTONDOWN: {
        int sx, sy;
        if (point_to_sector((int)a, (int)b, &sx, &sy)) {
            pv.blink = 0;
            sector_colour_one(sel_x(), sel_y());
            g_planet.sel_x = sx;
            g_planet.sel_y = sy;
            centre_on(w, 0xff, 0xff);
            select_rect();
        }
        break;
    }
    case WM_MSG_LBUTTONUP:
        wm_release_capture();
        pv.dragging = false;
        break;
    case WM_MSG_CTL_PRESS:
        wm_raise(w->parent);
        break;
    case WM_MSG_CTL_RDOWN:
        help_for(w);
        break;
    case WM_MSG_CTL_HOVER:
        if (wm_get_mode() != 0)
            help_for(w);
        break;
    case WM_MSG_CTL_CLICK:
        control_clicked(w);
        break;
    case WM_MSG_CHAR:
        if (pv.main)
            wm_send(pv.main, pv.main, msg, a, b);
        break;
    case WM_MSG_KEYDOWN:
    case WM_MSG_KEYUP:
        return wm_send(w->parent, w->parent, msg, a, b);
    case WM_MSG_SHOW:
        /* TODO(WP7): hidden: back to the normal cursor (0x6464cc). */
        break;
    case WM_MSG_CTL_RELEASE:
        influence_close();
        wm_show(w, false);
        free_window();
        pv.win = NULL;
        pv.btn_pop = pv.btn_tech = pv.btn_sin = pv.btn_tenets = NULL;
        pv.btn_up = pv.btn_down = pv.btn_left = pv.btn_right = pv.btn_stop = NULL;
        pv.layer = PV_LAYER_POPULATION;
        pv.rotating = false;
        if (pv.main)
            wm_activate(pv.main);
        wm_post(w, w, WM_MSG_DESTROY, 0, 0);
        break;
    case PV_MSG_REFRESH:
        planetview_refresh(b != 0);
        break;
    case PV_MSG_RESET:
        if (g_state.planet.map) {
            /* original: (re)opening the view forgets queued artist and
             * famous-person influence, and clears the pending sums */
            g_state.planet.n_a = 0;
            g_state.planet.n_b = 0;
            memset(g_state.planet.map, 0, g_state.planet.map_size);
            planet_map_draw_all(g_state.planet.map);
            centre_on(w, 0xff, 0xff);
            wm_send(w, w, PV_MSG_CLEAR, 0, 0);
        }
        break;
    case PV_MSG_CLEAR:
        memset(g_state.planet.sin_acc, 0, sizeof g_state.planet.sin_acc);
        memset(g_state.planet.tenet_acc, 0, sizeof g_state.planet.tenet_acc);
        draw_legend(w, pv.layer);
        wm_send(w, w, PV_MSG_REFRESH, 0, 1);
        break;
    case PV_MSG_SLIDER:
        slider_moved(w, (WmWindow *)a, (int)b);
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

static void load_colour_tables(void)
{
    uint32_t avail = 0;
    const uint8_t *p = exe_res_va(0x64f1f7, &avail);
    if (p && avail >= sizeof pv.sin_colours)
        memcpy(pv.sin_colours, p, sizeof pv.sin_colours);
}

/* 0x4a2efd */
WmWindow *planetview_create(WmWindow *parent)
{
    if (parent)
        pv.main = parent;
    if (pv.win) {
        pv.visible = !pv.visible;
        wm_send(pv.win, pv.win, PV_MSG_REFRESH, 0, pv.visible);
        wm_show(pv.win, pv.visible);
        if (!pv.visible) {
            pv.rotating = false;
            set_pressed(pv.btn_up, false);
            set_pressed(pv.btn_down, false);
            set_pressed(pv.btn_left, false);
            set_pressed(pv.btn_right, false);
            if (planetview_hooks.hidden)
                planetview_hooks.hidden();
            if (pv.main)
                wm_activate(pv.main);
        } else {
            wm_activate(pv.win);
        }
        return pv.win;
    }

    pv.visible = false;
    if (!sector_surface())
        return NULL;
    load_colour_tables();
    GfxSurface *bmp = gfx_load_bmp_resource(0x25e, NULL);
    if (!bmp)
        return NULL;
    WmWindow *par = pv.main ? pv.main : wm_root();
    GfxRect r = {par->client.right - bmp->width + 1, par->client.right, par->client.bottom - bmp->height + 1,
                 par->client.bottom};
    WmWindow *w = wm_create_window(par, &r, planetview_proc, WM_F_MINIMIZABLE | WM_F_NOFOCUS, 0);
    if (!w) {
        gfx_surface_destroy(bmp);
        return NULL;
    }
    pv.win = w;
    gfx_blit(w->surface, 0, 0, bmp->width, bmp->height, bmp, 0, 0);
    gfx_surface_destroy(bmp);
    wm_set_title(w, xs(0x66ef2c));            /* "Planetview" */
    wm_draw_title(w, NULL, FONT, 0x1a, 0x66); /* wnd_draw_title_std 0x4081e6 */
    influence_init(w);
    w->close_button = button(w, 0, 0, PV_ID_CLOSE, CTL_STYLE_BITMAP, 0x259);

    /* the layer buttons, centred over the legend column */
    static const struct {
        uint32_t id, style, label;
    } k_layers[4] = {
        {PV_ID_POPULATION, CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE, 0x66ef38},
        {PV_ID_TECH, CTL_STYLE_BITMAP | CTL_STYLE_STICKY, 0x66ef44},
        {PV_ID_SIN, CTL_STYLE_BITMAP | CTL_STYLE_STICKY, 0x66ef50},
        {PV_ID_TENETS, CTL_STYLE_BITMAP | CTL_STYLE_STICKY, 0x66ef5c},
    };
    WmWindow **layer_btn[4] = {&pv.btn_pop, &pv.btn_tech, &pv.btn_sin, &pv.btn_tenets};
    int y = 0xaf, bottom = y;
    for (int i = 0; i < 4; i++) {
        GfxSurface *lb = gfx_load_bmp_resource(0x91, NULL);
        if (!lb)
            break;
        int x = ((0x4b - (lb->width >> 1)) >> 1) + 0x19f;
        GfxRect br = {x, x + lb->width - 1, y, y + lb->height - 1};
        bottom = br.bottom;
        y += lb->height + 2;
        *layer_btn[i] = ctl_button_create(w, &br, k_layers[i].id, k_layers[i].style, lb);
        if (*layer_btn[i])
            ctl_button_set_label(*layer_btn[i], xs(k_layers[i].label), FONT, 0x13, true);
        else
            gfx_surface_destroy(lb);
    }

    /* the rotation pad: up, stop, down in a column, left and right */
    GfxSurface *ub = gfx_load_bmp_resource(0x152, NULL);
    if (ub) {
        int x = 0x1c4 - (ub->width >> 2);
        GfxRect br = {x, x + ub->width - 1, bottom + 2, bottom + 1 + ub->height};
        if (!(pv.btn_up = ctl_button_create(w, &br, PV_ID_UP, CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE, ub)))
            gfx_surface_destroy(ub);
        GfxSurface *sb = gfx_load_bmp_resource(0x14f, NULL);
        GfxSurface *db = gfx_load_bmp_resource(0x153, NULL);
        GfxSurface *lb = gfx_load_bmp_resource(0x150, NULL);
        GfxSurface *rb = gfx_load_bmp_resource(0x151, NULL);
        int stop_top = br.bottom + 1, stop_bottom = stop_top + (sb ? sb->height : 0) - 1;
        int down_top = stop_bottom + 1;
        if (sb) {
            GfxRect r2 = {x, x + sb->width - 1, stop_top, stop_bottom};
            if (!(pv.btn_stop = ctl_button_create(w, &r2, PV_ID_STOP, CTL_STYLE_BITMAP, sb)))
                gfx_surface_destroy(sb);
        }
        if (db) {
            GfxRect r2 = {x, x + db->width - 1, down_top, down_top + db->height - 1};
            if (!(pv.btn_down = ctl_button_create(w, &r2, PV_ID_DOWN, CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE, db)))
                gfx_surface_destroy(db);
        }
        if (lb) {
            int lx = x - (lb->width >> 1), ly = down_top - lb->height;
            GfxRect r2 = {lx, lx + lb->width - 1, ly, ly + lb->height - 1};
            if (!(pv.btn_left = ctl_button_create(w, &r2, PV_ID_LEFT, CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE, lb)))
                gfx_surface_destroy(lb);
            if (rb) {
                int rx = lx + rb->width;
                GfxRect r3 = {rx, rx + rb->width - 1, ly, ly + rb->height - 1};
                if (!(pv.btn_right = ctl_button_create(w, &r3, PV_ID_RIGHT, CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE, rb)))
                    gfx_surface_destroy(rb);
                rb = NULL;
            }
        }
        gfx_surface_destroy(rb);
    }
    set_pressed(pv.btn_pop, true);
    planetview_disaster(0, 0, 0);
    return w;
}

WmWindow *planetview_window(void) { return pv.win; }
int planetview_layer(void) { return pv.layer; }
int planetview_influence(void) { return (int)pv.influence; }

void planetview_set_context(WmWindow *main_window, int *paused)
{
    pv.main = main_window;
    pv.paused = paused;
}

void planetview_set_menu_checks(bool tech_announcements, bool disaster_announcements)
{
    pv.menu_tech = tech_announcements;
    pv.menu_disasters = disaster_announcements;
}

/* 0x4a3593 */
void planetview_rotate_tick(void)
{
    if (!pv.win || !wm_is_visible(pv.win) || !pv.rotating)
        return;
    pv.off_x += pv.dx * 7;
    pv.off_y += pv.dy * 7;
    if (pv.off_x < -0x27f)
        pv.off_x += 0x280;
    else if (pv.off_x > 0x27f)
        pv.off_x -= 0x280;
    if (pv.off_y < -0x13f)
        pv.off_y += 0x140;
    else if (pv.off_y > 0x13f)
        pv.off_y -= 0x140;
    draw_globe(pv.win->surface, GLOBE_X, GLOBE_Y, GLOBE, GLOBE);
    wm_redraw(pv.win, &k_front);
    wm_redraw(pv.win, &k_back);
}

/* 0x4a479f. (The phase flips on every call, the redraw happens at most
 * every 500 ms, as in the original.) */
void planetview_blink_cursor(void)
{
    if (!pv.win || pv.rotating || !wm_is_visible(pv.win) || wm_is_minimized(pv.win) || !pv.sectors)
        return;
    pv.blink ^= 1;
    uint32_t now = plat_ticks_ms();
    if (now < pv.blink_time + 500)
        return;
    pv.blink_time = now;
    int sx = sel_x(), sy = sel_y();
    if (pv.blink == 0) {
        sector_colour_one(sx, sy);
    } else {
        uint8_t c = pv.layer == PV_LAYER_TECH || pv.layer == PV_LAYER_TENETS ? 0xb : 0xc;
        *sector_pixel(sx, sy) = c;
        pv.water[sy][sx] = c;
    }
    draw_globe_rect(pv.win->surface, &pv.sel_rect);
    wm_redraw(pv.win, &pv.sel_rect);
}

/* 0x4a4943 */
void planetview_refresh(bool full)
{
    if (!pv.win || !pv.sectors)
        return;
    if (!((wm_is_visible(pv.win) && !wm_is_minimized(pv.win)) || full))
        return;
    sector_colour_all(pv.layer);
    draw_globe(pv.win->surface, GLOBE_X, GLOBE_Y, GLOBE, GLOBE);
    draw_info(pv.win, -1, -1);
    redraw(pv.win, 0xc, 0x28b, 0x1a, 0x159);
}

/* 0x49be17 */
void planetview_select_sector(int x, int y)
{
    if (!pv.win || x < 0 || x >= PLANET_W || y < 0 || y >= PLANET_H)
        return;
    pv.off_x = x * 10 - 0x9b;
    pv.off_y = y * 10 - 0x9b;
    sector_colour_one(sel_x(), sel_y());
    draw_globe_rect(pv.win->surface, &pv.sel_rect);
    wm_redraw(pv.win, &pv.sel_rect);
    g_planet.sel_x = x;
    g_planet.sel_y = y;
    pv.blink = 0;
    select_rect();
}

/* ---- The disaster window ------------------------------------------------------------------- */

static void flc_present(void *ctx, const FlcTarget *t, int x0, int y0, int x1, int y1, int result)
{
    (void)t;
    (void)result;
    GfxRect r = {x0, x1, y0, y1};
    wm_redraw((WmWindow *)ctx, &r);
    gfx_present();
}

/* Plays FLC resource `id` in the window's animation area (0x83e8 of
 * 0x49e324). The original streams it from the bundle (so the palette is
 * left alone); the port reads it whole and ignores COLOR chunks. */
static void play_animation(WmWindow *w, uint32_t id)
{
    long size = rc_resource_size(id);
    PlatFile *f = size > 0 ? rc_open_resource(id) : NULL;
    if (!f)
        return;
    uint8_t *data = malloc((size_t)size);
    bool ok = data && plat_file_read(f, data, 1, (size_t)size) == (size_t)size;
    plat_file_close(f);
    if (ok && flc_open(&pv.flc, data, (size_t)size) == FLC_OK) {
        GfxSurface *s = w->surface;
        FlcTarget t = {s->pixels, s->stride, s->width, s->height, NULL, 1000}; /* flc_target_init 0x40b0c9 */
        pv.flc.x = 0xd;
        pv.flc.y = 0xe;
        if (planetview_hooks.pump)
            planetview_hooks.pump();
        flc_play(&pv.flc, &t, flc_present, w);
        flc_close(&pv.flc);
    }
    free(data);
}

/* 0x49e324 */
static intptr_t disaster_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_MOUSEMOVE:
        if (pv.d_dragging)
            drag_window(w, (int)a, (int)b, pv.d_grab_x, pv.d_grab_y);
        break;
    case WM_MSG_LBUTTONDOWN:
        wm_raise(w);
        pv.d_grab_x = (int)a;
        pv.d_grab_y = (int)b;
        pv.d_dragging = true;
        if (pv.paused)
            *pv.paused = 1;
        wm_set_capture(w);
        break;
    case WM_MSG_LBUTTONUP:
        wm_release_capture();
        pv.d_dragging = false;
        if (pv.paused)
            *pv.paused = 0;
        break;
    case WM_MSG_CTL_PRESS: /* w = the close or minimize button */
        if (ctl_get_id(w) == 1000)
            wm_show(w->parent, false);
        wm_raise(w->parent);
        break;
    case WM_MSG_CTL_RDOWN:
        disaster_help(w);
        break;
    case WM_MSG_CTL_HOVER:
        if (wm_get_mode() != 0)
            disaster_help(w);
        break;
    case WM_MSG_CTL_RELEASE:
        pv.disaster = NULL;
        flc_close(&pv.flc);
        wm_show(w, false);
        if (pv.win && wm_is_visible(pv.win))
            wm_activate(pv.win);
        else if (pv.main)
            wm_activate(pv.main);
        break;
    case PV_MSG_REFRESH: /* a = FLC resource, b = kind */
        if (a == 0 || pv.flc.playing)
            return 0;
        gfx_fill_rect(w->surface, 0xd, 0xe, 0x140, 200, 0x13);
        flc_close(&pv.flc);
        wm_raise(w);
        wm_show(w, true);
        play_animation(w, (uint32_t)a);
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

/* 0x49de17 */
void planetview_disaster(unsigned kind, uint32_t killed, uint32_t injured)
{
    static const uint32_t k_headline[PLANET_DISASTERS] = {
        0x66df3c, 0x66df60, 0x66df84, 0x66dfa8, 0x66dfd4, 0x66e00c, 0x66e03c, 0x66e060, 0x66e094, 0x66e0cc,
    };
    if (!pv.disaster) {
        GfxSurface *bmp = gfx_load_bmp_resource(0x14c, NULL);
        if (!bmp)
            return;
        GfxRect r = {100, bmp->width + 99, 100, bmp->height + 99};
        pv.disaster = wm_create_window(pv.main, &r, disaster_proc, WM_F_MINIMIZABLE | WM_F_NOFOCUS, 0);
        if (!pv.disaster) {
            gfx_surface_destroy(bmp);
            return;
        }
        WmWindow *w = pv.disaster;
        gfx_blit(w->surface, 0, 0, bmp->width, bmp->height, bmp, 0, 0);
        gfx_surface_destroy(bmp);
        wm_center(w);
        w->close_button = button(w, 0, 0, 1000, CTL_STYLE_BITMAP, 0x259);
        wm_set_title(w, xs(0x66e0e4)); /* "Disaster Strikes the Planet!" */
        wm_draw_title(w, NULL, FONT, 0x1a, 0x66);
        memset(&pv.flc, 0, sizeof pv.flc);
        return;
    }
    WmWindow *w = pv.disaster;
    GfxSurface *s = w->surface;
    if (s->height > 0xdf && s->width > 0x10)
        gfx_fill_rect(s, 0x10, 0xdf, 0x13a, 0x34, s->pixels[(size_t)0xdf * s->stride + 0x10]);
    if (!pv.menu_disasters)
        return;
    char buf[80], num[FMT_NUM_LEN];
    int bh = gfx_font_height(FONT) & 0xff;
    font_colors(FONT, 0x13, 0x13);
    font_colors(FONT_BIG, 0x1a, 0x13);
    GfxRect r = {0x10, 0x149, 0xe4, 0xe4 + gfx_font_height(FONT_BIG) - 1};
    gfx_font_select(FONT_BIG);
    if (kind == 100) {
        gfx_font_draw_text(s, r.left, r.top, &r, &r, xs(0x66e104), GFX_TEXT_CENTER); /* mother of all floods */
        snprintf(buf, sizeof buf, "%s", xs(0x66e138)); /* "All but a handful of EMBOs Killed" */
    } else {
        gfx_font_draw_text(s, r.left, r.top, &r, &r, kind < PLANET_DISASTERS ? xs(k_headline[kind]) : "",
                           GFX_TEXT_CENTER);
        snprintf(buf, sizeof buf, xfmt(0x66e124, "s", "%s"), fmt_thousands(num, killed)); /* "%s  EMBOs Killed" */
    }
    r.top = 0x112 - (gfx_font_height(FONT) * 2 + 2);
    r.left += 2;
    r.bottom = bh + r.top - 1;
    gfx_font_select(FONT);
    gfx_font_draw_text(s, r.left, r.top, &r, &r, buf, 0);
    r.top += bh + 2;
    r.bottom = bh + r.top - 1;
    if (kind != 100) {
        snprintf(buf, sizeof buf, xfmt(0x66e15c, "s", "%s"), fmt_thousands(num, injured)); /* "%s  Injured" */
        gfx_font_draw_text(s, r.left, r.top, &r, &r, buf, 0);
    }
    redraw(w, 0x10, 0x149, 0xdf, 0x112);
    wm_set_minimized(w, false);
    wm_send(w, w, PV_MSG_REFRESH, kind == 100 ? 0x1450 : 0x1450 + kind, kind);
}

WmWindow *planetview_disaster_window(void) { return pv.disaster; }
bool planetview_disaster_busy(void) { return pv.flc.playing; }

void planetview_shutdown(void)
{
    WmWindow *w = pv.win;
    if (w) {
        wm_send(w, w, WM_MSG_CTL_RELEASE, 0, 0);
        wm_destroy(w);
    }
    w = pv.disaster;
    if (w) {
        wm_send(w, w, WM_MSG_CTL_RELEASE, 0, 0);
        wm_destroy(w);
    }
    influence_free_bitmaps();
    free_window();
    pv.visible = true;
}

/* ---- Hooks for the planet simulation --------------------------------------------------------- */

static bool hook_tech_announcements(void) { return pv.menu_tech; }

/* The announcement of 0x49b05c (planet_tech_update). */
static void hook_tech_discovered(unsigned tech, unsigned variant)
{
    static char msg[1024]; /* 0x6386a0 */
    char btn[24];
    if (tech == 0 || tech >= PLANET_TECHS)
        return;
    const char *blurb = g_planet_tables.tech_blurb[tech];
    uint32_t button_va;
    if (tech == 14) {
        button_va = 0x66dcd4; /* "SWING!" */
        snprintf(msg, sizeof msg, "%s", blurb);
    } else {
        button_va = 0x66dcdc; /* "Beats me!" */
        snprintf(msg, sizeof msg, xfmt(0x66dce8, "ss", "%s!%s"), g_planet_tables.tech_name[tech], blurb);
    }
    uint32_t id = variant == 0 ? 0x100a4 + tech - 1 : tech == 14 ? 0x100b1 : 0x100b2 + tech - 1;
    snd_preload(id);
    snd_play(id, 1);
    ctl_message_box(-1, -1, xs(0x66ddf4), msg, xbuttons(button_va, btn, sizeof btn), 120000);
}

static bool hook_disaster_reports(void) { return pv.disaster && pv.menu_disasters; }

static void hook_flood_warning(void) /* 0x49c226 */
{
    msgbox(0x66df1c, xs(0x66de10), 0x66df10, 120000); /* "Here it comes!" */
}

static void hook_view_refresh(bool full)
{
    if (pv.win)
        wm_send(pv.win, pv.win, PV_MSG_REFRESH, 0, full);
}

/* The view part of 0x49c8b8 after a disaster. */
static void hook_view_focus(int x, int y)
{
    if (!pv.win)
        return;
    planetview_select_sector(x, y);
    set_pressed(pv.btn_pop, false);
    set_pressed(pv.btn_tech, false);
    set_pressed(pv.btn_sin, false);
    draw_legend(pv.win, pv.layer);
    planetview_select_sector(x, y);
    wm_send(pv.win, pv.win, PV_MSG_REFRESH, 0, 1);
}

static void hook_view_reset(void)
{
    if (pv.win)
        wm_send(pv.win, pv.win, PV_MSG_RESET, 0, 0);
}

static void hook_busy_cursor(bool on)
{
    /* TODO(WP7): ui_busy_cursor isn't ported; no-op without a hook. */
    if (planetview_hooks.busy_cursor)
        planetview_hooks.busy_cursor(on);
}

void planetview_install_hooks(void)
{
    g_planet_hooks.tech_announcements = hook_tech_announcements;
    g_planet_hooks.tech_discovered = hook_tech_discovered;
    g_planet_hooks.disaster_reports = hook_disaster_reports;
    g_planet_hooks.disaster = planetview_disaster;
    g_planet_hooks.disaster_busy = planetview_disaster_busy;
    g_planet_hooks.flood_warning = hook_flood_warning;
    g_planet_hooks.view_refresh = hook_view_refresh;
    g_planet_hooks.view_focus = hook_view_focus;
    g_planet_hooks.view_reset = hook_view_reset;
    g_planet_hooks.busy_cursor = hook_busy_cursor;
}
