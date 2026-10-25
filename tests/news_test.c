/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Headless test of the news pop-up, the help balloon (src/ui/news.c) and
 * the about box with its credits (src/ui/about.c). Needs the user's game
 * files; none are in the repo.
 *
 *   news_test <data dir> <bundle> <scenario> <out dir> [--window]
 *
 * e.g. news_test orig MEDIUM.000 orig/ASCEN001.ASC /tmp/out
 *
 * Builds a main window like the game's (menu bar, scrollbars, a view
 * child), then: a help balloon closed by a left click in it through the
 * mouse path, one closed by a right click elsewhere, a left click
 * outside that leaves it up; the news pop-ups 0x36d ("Twilight of the
 * Demiurges", closed by a click in its balloon) and 900 (a special
 * structure, closed by its timeout through sys_pump); the about box
 * (shown, credits run for a few hundred steps, right-click help on its
 * close button, the close button's menu command). Writes
 * <out dir>/news_*.png along the way.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compbun/cbres.h"
#include "game/resource.h"
#include "game/shell.h"
#include "game/sound.h"
#include "game/startup.h"
#include "game/state.h"
#include "game/util.h"
#include "game/world.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/mouse.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "platform/platform.h"
#include "ui/about.h"
#include "ui/controls.h"
#include "ui/menu.h"
#include "ui/news.h"
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
    snprintf(path, sizeof path, "%s/news_%s.png", g_out, name);
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

static GfxSurface *load(uint32_t id)
{
    GfxSurface *s = gfx_load_bmp_resource(id, NULL);
    if (!s)
        fprintf(stderr, "missing bitmap %#x\n", id);
    return s;
}

static intptr_t g_last_command = -1;
static int g_repaints;

/* the main window: records menu commands and repaint requests */
static intptr_t main_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    if (msg == WM_MSG_MENU_COMMAND) {
        g_last_command = a;
        return 0;
    }
    if (msg == SHELL_MSG_REPAINT) {
        g_repaints++;
        return 0;
    }
    return wm_default_proc(w, msg, a, b);
}

static intptr_t view_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b) { return wm_default_proc(w, msg, a, b); }

static int wm_owner(void) { return 0; }
static int ctl_owner(void) { return 0; }

static void pump(int n)
{
    for (int i = 0; i < n; i++)
        sys_pump();
}

static void click_at(int x, int y, unsigned button)
{
    gfx_mouse_on_move(x, y);
    pump(2);
    gfx_mouse_on_button(button, true);
    pump(2);
    gfx_mouse_on_button(button, false);
    pump(3);
}

static void centre(const WmWindow *w, int *x, int *y)
{
    *x = (w->rect.left + w->rect.right) / 2;
    *y = (w->rect.top + w->rect.bottom) / 2;
    if (w->parent)
        wm_point_to_screen(w->parent, x, y);
}

static int g_paint_main_calls;
static void paint_main(int x, int y, int w, int h)
{
    (void)x, (void)y;
    CHECK(w >= 0 && h >= 0);
    g_paint_main_calls++;
}

static uint8_t g_game_pal[256 * 4];

static bool game_palette_on(void)
{
    uint8_t now[256 * 4];
    gfx_palette_get(now, 0, 256);
    for (int i = GFX_PAL_FIRST; i <= GFX_PAL_LAST; i++)
        if (memcmp(now + i * 4, g_game_pal + i * 4, 3) != 0)
            return false;
    return true;
}

static uint32_t hash_surface(const GfxSurface *s, int x0, int y0, int x1, int y1)
{
    uint32_t h = 2166136261u;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            h = (h ^ s->pixels[(size_t)y * (size_t)s->stride + (size_t)x]) * 16777619u;
    return h;
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: %s <data dir> <bundle> <scenario> <out dir> [--window]\n", argv[0]);
        return 2;
    }
    g_out = argv[4];
    bool window = argc > 5 && strcmp(argv[5], "--window") == 0;
    if (!window) {
#ifdef _WIN32
        _putenv_s("SDL_VIDEODRIVER", "dummy");
#else
        SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1);
#endif
    }
    crc_init();
    snd_sfx_enabled = 0; /* no iMUSE here */
    snd_music_enabled = 0;
    plat_set_data_dir(argv[1]);
    PlatFile *cd = plat_file_open("ALIFE.000");
    if (cd)
        plat_file_close(cd);
    if (!rc_initialize(argv[2], cd ? "ALIFE.000" : argv[2]))
        return 1;
    rc_hook_compbun(); /* the sound module streams through compbun */
    compbun_init(1, 0);
    if (!plat_init("news_test", W, H) || !gfx_init(W, H, false))
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
    if (!pal)
        return 1;
    gfx_palette_set(pal, GFX_PAL_FIRST, GFX_PAL_COUNT);
    gfx_palette_get(g_game_pal, 0, 256);
    /* shade table 1 as tile_load_all builds it (the pop-up darkens with it) */
    gfx_palette_build_shade_table(1, pal, 0xf0, 0xf0, 0xf0, 10, 0xff, 0x26, 0x2f);
    free(pal);
    gfx_palette_build_highlight();

    CHECK(wm_initialize() == 0);
    sys_register_task(wm_owner, NULL, NULL, NULL, wm_idle);
    ctl_initialize();
    sys_register_task(ctl_owner, NULL, NULL, NULL, ctl_idle);
    ctl_set_pause_flag(&g_game_paused);
    wm_set_input_live(true);

    char err[256];
    if (!world_load(argv[3], err, sizeof err)) {
        fprintf(stderr, "can't load %s: %s\n", argv[3], err);
        return 1;
    }
    rng_seed(1);

    /* the game's skins and a main window like the game's */
    GfxSurface *sb[8] = {load(0x4000), load(0x4001), load(0x4006), load(0x4004),
                         load(0x4002), load(0x4003), load(0x4006), load(0x4005)};
    ctl_scroll_set_vbitmaps(sb[0], sb[1], sb[2], sb[3]);
    ctl_scroll_set_hbitmaps(sb[4], sb[5], sb[6], sb[7]);
    GfxSurface *mk[9];
    for (int i = 0; i < 9; i++)
        mk[i] = load(0x4010 + (uint32_t)i);
    menu_set_skin(mk[0], mk[1], mk[2], mk[3], mk[4], mk[5], mk[6], mk[7], mk[8]);
    menu_set_font(501);
    GfxRect full = {0, W - 1, 0, H - 1};
    WmWindow *mainw = wm_create_toplevel(&full, main_proc, WM_F_MENUBAR | WM_F_VSCROLL | WM_F_HSCROLL);
    CHECK(mainw && mainw->menubar && mainw->vscroll);
    if (!mainw)
        return 1;
    GfxSurface *back = load(1210);
    if (back) {
        gfx_tile_fill(mainw->surface, 0, 0, W, H, back, 0, 0, back->width, back->height);
        gfx_surface_destroy(back);
    }
    WmWindow *file = menu_add_item(mainw->menubar, MENU_TEXT, 100, " &File ", 0, 501);
    WmWindow *fm = menu_create(file, NULL, 0);
    menu_add_item(fm, MENU_TEXT, MENU_ABOUT, "&About", 0, 501);
    wm_show(mainw, true);
    menu_show(mainw->menubar, true);
    wm_show_scrollbars(mainw, 3, true);
    g_main_window = mainw;

    /* a view (stands in for the Graphview & co.) */
    GfxRect vr = {40, 239, 60, 179};
    WmWindow *view = wm_create_window(mainw, &vr, view_proc, WM_F_MINIMIZABLE | WM_F_NOFOCUS, 0);
    CHECK(view != NULL);
    gfx_fill_rect(view->surface, 0, 0, 200, 120, 0x50);
    wm_show(view, true);
    pump(3);

    news_hooks.paint_main = paint_main;
    news_init();
    WmWindow *nw = news_window();
    CHECK(nw != NULL && !wm_is_visible(nw));
    if (!nw)
        return 1;
    printf("balloon bitmap %dx%d\n", nw->surface->width, nw->surface->height);

    /* --- help balloon, closed by a left click in it ---------------------- */
    help_balloon(300, 200, 0x78, "This button CLOSES the window.  A rather long text to show the wrapping.");
    CHECK(wm_is_visible(nw) && news_active());
    CHECK(wm_capture() == nw);
    CHECK(g_game_paused == 1);
    CHECK(nw->rect.left == 300 && nw->rect.top == 200);
    CHECK(nw->rect.right - nw->rect.left < nw->surface->width);
    pump(2);
    shot("balloon");
    /* a left click outside leaves it up */
    click_at(20, 400, GFX_MOUSE_LEFT);
    CHECK(wm_is_visible(nw));
    int x, y;
    centre(nw, &x, &y);
    click_at(x, y, GFX_MOUSE_LEFT);
    CHECK(!wm_is_visible(nw) && !news_active());
    CHECK(g_game_paused == 0);
    CHECK(wm_capture() == NULL);

    /* --- one near the bottom right (clamped), closed by a right click ---- */
    help_balloon(W - 10, H - 10, 300, "Clamped to the main window.");
    CHECK(wm_is_visible(nw));
    CHECK(nw->rect.right <= W && nw->rect.bottom <= H);
    click_at(30, 30, GFX_MOUSE_RIGHT);
    CHECK(!wm_is_visible(nw));
    pump(2);
    shot("after_balloon");

    /* --- news pop-up 0x36d, closed by a click in its balloon ------------- */
    WmProc own = wm_get_proc(mainw);
    g_repaints = 0;
    news_popup_show(0x36d, 0, 0);
    CHECK(news_popup_active());
    CHECK(wm_get_proc(mainw) == news_main_proc);
    CHECK(wm_is_visible(nw));
    CHECK(!wm_is_visible(view));
    CHECK(!wm_is_visible(mainw->vscroll) && !wm_is_visible(mainw->hscroll));
    CHECK(g_paint_main_calls == 3);
    CHECK(!game_palette_on());
    CHECK(g_game_paused == 1);
    pump(2);
    shot("popup_36d");
    centre(nw, &x, &y);
    click_at(x, y, GFX_MOUSE_LEFT);
    CHECK(!news_popup_active() && !wm_is_visible(nw));
    CHECK(wm_get_proc(mainw) == own);
    CHECK(wm_is_visible(view));
    CHECK(wm_is_visible(mainw->vscroll));
    CHECK(game_palette_on());
    CHECK(g_repaints >= 2);
    CHECK(g_game_paused == 0);

    /* --- pop-up 900 (a special structure), closed by its timeout ---------- */
    g_state.unk_638668 &= ~1u;
    g_state.unk_63042c &= ~1u;
    wm_set_minimized(view, true);
    news_popup_show(900, 0, 50);
    CHECK(news_popup_active());
    CHECK(g_state.unk_63042c & 1u);
    pump(2);
    shot("popup_900");
    uint32_t t0 = plat_ticks_ms();
    while (news_popup_active() && plat_ticks_ms() - t0 < 3000) {
        sys_pump();
        plat_delay_ms(2);
    }
    CHECK(!news_popup_active() && !wm_is_visible(nw));
    CHECK(wm_is_visible(view) && wm_is_minimized(view));
    CHECK(game_palette_on());
    wm_set_minimized(view, false);

    /* an unknown id: the default balloon; Esc to the balloon closes */
    news_popup_show(0x3a0, 0, 0);
    CHECK(news_popup_active());
    wm_send(nw, nw, WM_MSG_KEYUP, 0x1b, 0);
    CHECK(!news_popup_active());
    pump(2);

    /* --- the about box --------------------------------------------------- */
    CHECK(about_create());
    WmWindow *ab = about_window();
    CHECK(ab && !wm_is_visible(ab));
    if (!ab)
        return 1;
    printf("about box %dx%d at %d,%d\n", ab->surface->width, ab->surface->height, ab->rect.left, ab->rect.top);
    wm_set_title(ab, "Afterlife v1.0");
    wm_show(ab, true);
    wm_raise(ab);
    CHECK(about_credits_running());
    int pending = about_credits_pending();
    printf("credits: %d entries\n", pending);
    CHECK(pending > 300);
    GfxRect area = {0x15f, ab->surface->width - 1 - 0xd, 0x10, ab->surface->height - 1 - 0xd};
    uint32_t h0 = hash_surface(ab->surface, area.left, area.top, area.right, area.bottom);
    for (int i = 0; i < 120; i++)
        about_credits_step();
    CHECK(hash_surface(ab->surface, area.left, area.top, area.right, area.bottom) != h0);
    pump(2);
    shot("about_120");
    uint32_t hp = hash_surface(ab->surface, 14, 16, 14 + 319, 16 + 199);
    for (int i = 0; i < 280; i++)
        about_credits_step();
    pump(2);
    shot("about_400");
    CHECK(hash_surface(ab->surface, 14, 16, 14 + 319, 16 + 199) != hp);
    CHECK(about_credits_pending() < pending);
    /* the whole run wraps around and starts again */
    int steps = 0;
    int prev = about_credits_pending();
    bool wrapped = false;
    for (; steps < 20000 && !wrapped; steps++) {
        about_credits_step();
        int now = about_credits_pending();
        wrapped = now > prev;
        prev = now;
    }
    printf("credits wrapped after %d more steps\n", steps);
    CHECK(wrapped);
    shot("about_wrap");

    /* right-click help on the close button, then its click */
    WmWindow *cb = ctl_find_by_id(ab->children, ABOUT_ID_CLOSE);
    CHECK(cb != NULL);
    if (cb) {
        wm_send(ab, cb, WM_MSG_CTL_RDOWN, ABOUT_ID_CLOSE, 0);
        CHECK(wm_is_visible(nw));
        pump(2);
        shot("about_help");
        click_at(5, 470, GFX_MOUSE_RIGHT);
        CHECK(!wm_is_visible(nw));
        centre(cb, &x, &y);
        click_at(x, y, GFX_MOUSE_LEFT);
        CHECK(g_last_command == MENU_ABOUT);
    }
    /* minimizing stops the credits */
    wm_set_minimized(ab, true);
    CHECK(!about_credits_running());
    wm_set_minimized(ab, false);
    CHECK(about_credits_running());
    wm_show(ab, false);

    about_destroy();
    news_shutdown();
    CHECK(news_window() == NULL && about_window() == NULL);
    wm_destroy(mainw);
    g_main_window = NULL;
    ctl_shutdown();
    wm_shutdown();
    for (int i = 0; i < 8; i++)
        gfx_surface_destroy(sb[i]);
    for (int i = 0; i < 9; i++)
        gfx_surface_destroy(mk[i]);
    gfx_font_shutdown();
    world_free();
    gfx_shutdown();
    plat_shutdown();
    rc_shutdown();
    printf("%s\n", g_failures ? "FAILED" : "ok");
    return g_failures ? 1 : 0;
}
