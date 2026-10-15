/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Headless test of the Planetview window (src/ui/planetview.c). Needs the
 * user's game files; none are in the repo.
 *
 *   planetview_test <data dir> <bundle> <save> <out dir> [--window]
 *
 * e.g. planetview_test orig MEDIUM.000 orig/ASCEN001.ASC /tmp/out
 * (ALIFE.000 in the data dir is used as the CD bundle if it's there.)
 *
 * Loads the save, opens the Planetview like the game does (create, then
 * toggle visible), selects the most populated sector and drives the
 * layer buttons, sliders and tenet buttons through window messages,
 * writing the screen as <out dir>/pv_<layer>.png after each step:
 * population, technology, sin (Humility/Pride selected), tenets (RALF),
 * then a Wield Influence (scientist) with its message box (pv_wield_msgbox)
 * and a disaster report (pv_disaster). Checks the selection, layer state, influence bookkeeping and
 * that the globe and panels were drawn.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/planet.h"
#include "game/resource.h"
#include "game/sound.h"
#include "game/util.h"
#include "game/world.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/mouse.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "platform/platform.h"
#include "ui/controls.h"
#include "ui/planetview.h"
#include "ui/wm.h"

#define W 640
#define H 480

static int g_failures;
static const char *g_out;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

/* --- minimal PNG writer (stored deflate blocks), from formats_dump.c ----- */

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
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
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

/* Indexed 8-bit PNG; pal is 256 R,G,B,x entries. */
static int write_png(const char *path, const uint8_t *pix, int w, int h, int pitch, const uint8_t *pal)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8, ihdr[9] = 3, ihdr[10] = 0, ihdr[11] = 0, ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    uint8_t plte[768];
    for (int i = 0; i < 256; i++)
        plte[i * 3] = pal[i * 4], plte[i * 3 + 1] = pal[i * 4 + 1], plte[i * 3 + 2] = pal[i * 4 + 2];
    chunk(f, "PLTE", plte, 768);

    size_t raw_len = (size_t)(w + 1) * (size_t)h;
    size_t blocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *raw = malloc(raw_len), *z = malloc(z_len);
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * (size_t)(w + 1)] = 0;
        memcpy(raw + (size_t)y * (size_t)(w + 1) + 1, pix + (size_t)y * (size_t)pitch, (size_t)w);
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

static void shot(const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/pv_%s.png", g_out, name);
    const GfxSurface *scr = gfx_screen();
    uint8_t pal[256 * 4];
    gfx_palette_get(pal, 0, 256);
    CHECK(write_png(path, scr->pixels, scr->width, scr->height, scr->stride, pal) == 0);
    printf("wrote %s\n", path);
}

/* --- helpers ------------------------------------------------------------ */

static uint8_t *read_resource(uint32_t id, long *len)
{
    PlatFile *f = rc_open_resource(id);
    if (!f)
        return NULL;
    *len = rc_resource_size(id);
    uint8_t *buf = *len > 0 ? malloc((size_t)*len) : NULL;
    if (buf)
        plat_file_read(f, buf, 1, (size_t)*len);
    plat_file_close(f);
    return buf;
}

static intptr_t main_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b) { return wm_default_proc(w, msg, a, b); }

/* screen pixel (x, y) of window-local (lx, ly) */
static uint8_t wpx(const WmWindow *w, int lx, int ly)
{
    int x = lx, y = ly;
    wm_point_to_screen(w, &x, &y);
    const GfxSurface *s = gfx_screen();
    return s->pixels[(size_t)y * s->stride + x];
}

/* distinct colours in a window-local rectangle of the screen */
static int colours_in(const WmWindow *w, int x0, int y0, int x1, int y1)
{
    bool seen[256] = {false};
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            uint8_t c = wpx(w, x, y);
            n += !seen[c];
            seen[c] = true;
        }
    return n;
}

static uint32_t hash_rect(const WmWindow *w, int x0, int y0, int x1, int y1)
{
    uint32_t h = 2166136261u;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            h = (h ^ wpx(w, x, y)) * 16777619u;
    return h;
}

/* a click on a control as its parent sees it */
static void click(WmWindow *pv, unsigned id, bool press)
{
    WmWindow *b = ctl_find_by_id(pv->children, id);
    CHECK(b != NULL);
    if (!b)
        return;
    if (press)
        ctl_button_set_pressed(b, true, true);
    wm_send(pv, b, WM_MSG_CTL_CLICK, id, 0);
}

static int g_pump;
static bool g_msgbox_shot;

/* the message boxes' modal loop: photograph the first one, then press its
 * button */
static void test_pump(void)
{
    if (++g_pump > 1000) {
        fprintf(stderr, "FAIL: modal loop didn't end\n");
        exit(1);
    }
    WmWindow *dlg = ctl_msgbox_window();
    if (dlg && wm_is_visible(dlg)) {
        if (!g_msgbox_shot) {
            shot("wield_msgbox");
            g_msgbox_shot = true;
        }
        wm_send(dlg, ctl_msgbox_button(0), WM_MSG_CTL_CLICK, 1000, 0);
    }
    wm_idle();
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: %s <data dir> <bundle> <save> <out dir> [--window]\n", argv[0]);
        return 2;
    }
    g_out = argv[4];
    bool window = argc > 5 && strcmp(argv[5], "--window") == 0;
    if (!window) {
#ifdef _WIN32
        _putenv_s("SDL_VIDEODRIVER", "dummy");
#else
        setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    }
    crc_init();
    snd_sfx_enabled = 0; /* no iMUSE here */
    snd_music_enabled = 0;
    plat_set_data_dir(argv[1]);
    /* the CD bundle has what a hard-disk install leaves out (the disaster
     * animations); fall back to the one bundle */
    PlatFile *cd = plat_file_open("ALIFE.000");
    if (cd)
        plat_file_close(cd);
    if (!rc_initialize(argv[2], cd ? "ALIFE.000" : argv[2]))
        return 1;
    if (!plat_init("planetview_test", W, H) || !gfx_init(W, H, false))
        return 1;
    gfx_mouse_init();
    gfx_font_init();
    gfx_font_load(500);
    gfx_font_load(501);
    long len = 0;
    uint8_t *raw = read_resource(33, &len);
    uint8_t *pal = raw ? gfx_palette_load_resource(raw, (size_t)len) : NULL;
    free(raw);
    CHECK(pal != NULL);
    if (pal)
        gfx_palette_set(pal, GFX_PAL_FIRST, GFX_PAL_COUNT);
    free(pal);
    CHECK(wm_initialize() == 0);
    ctl_initialize();
    ctl_hooks.pump = test_pump;
    int paused = 0;
    ctl_set_pause_flag(&paused);
    wm_set_input_live(true);

    if (!planet_tables_load()) {
        fprintf(stderr, "can't unpack the planet tables\n");
        return 1;
    }
    char err[256];
    if (!world_load(argv[3], err, sizeof err)) {
        fprintf(stderr, "can't load %s: %s\n", argv[3], err);
        return 1;
    }
    planet_summary();
    rng_seed(1);

    GfxRect full = {0, W - 1, 0, H - 1};
    WmWindow *mainw = wm_create_toplevel(&full, main_proc, 0);
    CHECK(mainw != NULL);
    if (!mainw)
        return 1;
    GfxSurface *back = gfx_load_bmp_resource(1210, NULL);
    if (back) {
        gfx_tile_fill(mainw->surface, 0, 0, W, H, back, 0, 0, back->width, back->height);
        gfx_surface_destroy(back);
    }
    wm_show(mainw, true);

    planetview_set_context(mainw, &paused);
    planetview_install_hooks();
    CHECK(g_planet_hooks.view_refresh != NULL && g_planet_hooks.disaster != NULL);

    /* the game creates it hidden at startup; the menu toggles it */
    WmWindow *pv = planetview_create(mainw);
    CHECK(pv != NULL);
    if (!pv)
        return 1;
    CHECK(!wm_is_visible(pv));
    CHECK(planetview_disaster_window() != NULL);
    CHECK(g_state.planet.map != NULL);
    CHECK(planetview_create(NULL) == pv);
    /* loading a game resets the view (save_load_game: 0x83e9), which also
     * draws the legend */
    g_planet_hooks.view_reset();
    CHECK(wm_is_visible(pv));
    CHECK(pv->rect.right == W - 1 && pv->rect.bottom == H - 1);
    printf("planetview at %d,%d-%d,%d\n", pv->rect.left, pv->rect.top, pv->rect.right, pv->rect.bottom);

    /* the most populated sector */
    int bx = 0, by = 0;
    for (int x = 0; x < PLANET_W; x++)
        for (int y = 0; y < PLANET_H; y++)
            if (planet_sector(x, y)->pop > planet_sector(bx, by)->pop)
                bx = x, by = y;
    printf("most populated sector %d,%d: %u EMBOs, tech %u\n", bx, by, planet_sector(bx, by)->pop,
           planet_sector(bx, by)->tech);
    planetview_select_sector(bx, by);
    CHECK(g_planet.sel_x == bx && g_planet.sel_y == by);
    /* the globe is turned to it: a left click in the middle of the near
     * side selects it (or a neighbour) */
    wm_send(pv, pv, WM_MSG_LBUTTONDOWN, 0x11 + 0x55, 0x1f + 0x55);
    wm_send(pv, pv, WM_MSG_LBUTTONUP, 0x11 + 0x55, 0x1f + 0x55);
    printf("click in the middle selects %d,%d\n", g_planet.sel_x, g_planet.sel_y);
    CHECK(abs(g_planet.sel_x - bx) <= 1 && abs(g_planet.sel_y - by) <= 1);
    planetview_select_sector(bx, by);
    /* a right click off the globe does nothing */
    wm_send(pv, pv, WM_MSG_RBUTTONDOWN, 2, 0x1f + 0x55);
    CHECK(g_planet.sel_x == bx && g_planet.sel_y == by);

    /* population (the default) with the sector panel */
    planetview_refresh(true);
    CHECK(planetview_layer() == PV_LAYER_POPULATION && planetview_influence() == PV_INFLUENCE_NONE);
    CHECK(colours_in(pv, 0x11, 0x1f, 0xba, 0xc8) > 4);
    CHECK(colours_in(pv, 0xd9, 0x1f, 0x182, 0xc8) > 2);
    CHECK(colours_in(pv, 4, 0xe2, 399, 0x149) > 4);
    uint32_t h_pop = hash_rect(pv, 0x11, 0x1f, 0x182, 0xc8);
    shot("population");

    /* technology: scientist influence */
    click(pv, PV_ID_TECH, true);
    CHECK(planetview_layer() == PV_LAYER_TECH && planetview_influence() == PV_INFLUENCE_SCIENTIST);
    uint32_t h_tech = hash_rect(pv, 0x11, 0x1f, 0x182, 0xc8);
    CHECK(h_tech != h_pop);
    shot("technology");

    /* sin/virtue: artist influence; pull the Humility/Pride slider to the
     * top (virtue) and raise the amount */
    click(pv, PV_ID_SIN, true);
    CHECK(planetview_layer() == PV_LAYER_SIN && planetview_influence() == PV_INFLUENCE_ARTIST);
    WmWindow *sl = ctl_find_by_id(pv->children, PV_ID_SIN_SLIDER + 6);
    CHECK(sl != NULL && wm_is_visible(sl));
    if (sl) {
        wm_send(sl, sl, WM_MSG_LBUTTONDOWN, 4, 1);
        wm_send(sl, sl, WM_MSG_LBUTTONUP, 4, 1);
        CHECK(wm_send(sl, sl, 0x83ec, 0, 0) == 0); /* top step: virtue */
    }
    WmWindow *amt = ctl_find_by_id(pv->children, PV_ID_AMOUNT);
    CHECK(amt != NULL && wm_is_visible(amt));
    if (amt) {
        wm_send(amt, amt, WM_MSG_LBUTTONDOWN, 4, 30);
        wm_send(amt, amt, WM_MSG_LBUTTONUP, 4, 30);
    }
    CHECK(hash_rect(pv, 0x11, 0x1f, 0x182, 0xc8) != h_tech);
    shot("sin");

    /* tenets: famous person; RALF */
    click(pv, PV_ID_TENETS, true);
    CHECK(planetview_layer() == PV_LAYER_TENETS && planetview_influence() == PV_INFLUENCE_FAMOUS);
    click(pv, PV_ID_TENET_BUTTON + 7, true);
    shot("tenets");

    /* Wield Influence as a scientist: queued, paid, announced */
    click(pv, PV_ID_TECH, true);
    CHECK(planetview_influence() == PV_INFLUENCE_SCIENTIST);
    g_state.pennies = bignum_from_i64(100000000);
    unsigned n_c = g_state.planet.n_c;
    if (planet_sector(bx, by)->tech <= 0xd) {
        click(pv, PV_ID_WIELD, false);
        CHECK(g_state.planet.n_c == n_c + 1);
        CHECK(g_state.planet.rec_c[n_c].x == (uint32_t)bx && g_state.planet.rec_c[n_c].y == (uint32_t)by);
        CHECK(bignum_value(g_state.pennies) < 100000000);
        CHECK(g_msgbox_shot);
        printf("wielded: %d pennies left, scientist %u left\n", (int)bignum_value(g_state.pennies),
               g_state.planet.rec_c[n_c].left);
    }

    /* rotation: the globe turns on each tick */
    click(pv, PV_ID_LEFT, true);
    uint32_t h0 = hash_rect(pv, 0x11, 0x1f, 0xba, 0xc8);
    planetview_rotate_tick();
    planetview_rotate_tick();
    CHECK(hash_rect(pv, 0x11, 0x1f, 0xba, 0xc8) != h0);
    click(pv, PV_ID_STOP, false);
    planetview_blink_cursor();

    /* back to population: the sector panel again */
    click(pv, PV_ID_POPULATION, true);
    CHECK(planetview_layer() == PV_LAYER_POPULATION && planetview_influence() == PV_INFLUENCE_NONE);

    /* a disaster report (plays the animation) */
    g_planet_hooks.disaster(2, 123456, 98765);
    WmWindow *dw = planetview_disaster_window();
    CHECK(dw && wm_is_visible(dw));
    shot("disaster");

    planetview_shutdown();
    CHECK(planetview_window() == NULL && planetview_disaster_window() == NULL);
    wm_destroy(mainw);
    ctl_shutdown();
    wm_shutdown();
    gfx_font_shutdown();
    world_free();
    gfx_shutdown();
    plat_shutdown();
    rc_shutdown();
    printf("%s\n", g_failures ? "FAILED" : "ok");
    return g_failures ? 1 : 0;
}
