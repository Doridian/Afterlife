/*
 * Behaviour test for the window manager (src/ui/). Needs the user's game
 * files; none are in the repo.
 *
 *   ui_test <data dir> <bundle> <out.ppm> [--window]
 *
 * Creates overlapping windows filled with bundle bitmaps, a draggable
 * window with a minimize button and a bitmap button, then moves, raises,
 * hides, invalidates and minimizes them, checking the screen pixels
 * against the windows' surfaces after each step, and writes the final
 * screen as a binary PPM. Headless unless --window is given.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/resource.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/mouse.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "platform/platform.h"
#include "ui/controls.h"
#include "ui/region.h"
#include "ui/wm.h"

#define W 640
#define H 480

static int g_failures;

#define CHECK(cond)                                                                                                \
    do {                                                                                                           \
        if (!(cond)) {                                                                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                       \
            g_failures++;                                                                                          \
        }                                                                                                          \
    } while (0)

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

static bool write_ppm(const char *path)
{
    const GfxSurface *scr = gfx_screen();
    uint8_t pal[256 * 4];
    gfx_palette_get(pal, 0, 256);
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    fprintf(f, "P6\n%d %d\n255\n", scr->width, scr->height);
    for (int y = 0; y < scr->height; y++)
        for (int x = 0; x < scr->width; x++) {
            const uint8_t *c = pal + scr->pixels[(size_t)y * scr->stride + x] * 4;
            fputc(c[0], f);
            fputc(c[1], f);
            fputc(c[2], f);
        }
    return fclose(f) == 0;
}

static uint8_t spx(int x, int y)
{
    const GfxSurface *s = gfx_screen();
    return s->pixels[(size_t)y * s->stride + x];
}

static uint8_t wpx(const WmWindow *w, int lx, int ly)
{
    return w->surface->pixels[(size_t)ly * w->surface->stride + lx];
}

/* screen pixel (x, y) shows window w's surface */
static bool shows(const WmWindow *w, int x, int y)
{
    int lx = x, ly = y;
    wm_point_to_local(w, &lx, &ly);
    return spx(x, y) == wpx(w, lx, ly);
}

static intptr_t panel_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    if (msg == WM_MSG_CTL_CLICK)
        printf("click from control %ld in \"%s\"\n", (long)a, w->parent ? wm_get_title(w->parent) : "?");
    return wm_default_proc(w, msg, a, b);
}

/* A window showing part of a bitmap (or a pattern if it's missing), with a
 * title bar strip. */
static WmWindow *make_window(int x, int y, int w, int h, uint32_t bmp_id, uint32_t flags, const char *title,
                             uint8_t frame)
{
    GfxRect r = {x, x + w - 1, y, y + h - 1};
    WmWindow *win = wm_create_window(NULL, &r, panel_proc, flags, 0);
    CHECK(win != NULL);
    if (!win)
        return NULL;
    GfxSurface *bmp = gfx_load_bmp_resource(bmp_id, NULL);
    if (bmp) {
        gfx_tile_fill(win->surface, 0, 0, w, h, bmp, 0, 0, bmp->width, bmp->height);
        gfx_surface_destroy(bmp);
    } else {
        for (int yy = 0; yy < h; yy++)
            for (int xx = 0; xx < w; xx++)
                win->surface->pixels[(size_t)yy * win->surface->stride + xx] = (uint8_t)(20 + ((xx ^ yy) & 31));
    }
    GfxRect fr = {0, w - 1, 0, h - 1};
    gfx_frame_rect(win->surface, &fr, frame);
    gfx_fill_rect(win->surface, 1, 1, w - 2, 14, frame);
    wm_draw_title(win, title, GFX_FONT_BUILTIN, gfx_palette_closest(255, 255, 255), frame);
    if (win->flags & WM_F_MINIMIZABLE)
        ctl_button_draw(win->min_button, CTL_DRAW_BITMAP);
    return win;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <data dir> <bundle> <out.ppm> [--window]\n", argv[0]);
        return 2;
    }
    bool window = argc > 4 && strcmp(argv[4], "--window") == 0;
    if (!window) {
#ifdef _WIN32
        _putenv_s("SDL_VIDEODRIVER", "dummy");
#else
        SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1);
#endif
    }
    plat_set_data_dir(argv[1]);
    if (!rc_initialize(argv[2], argv[2]))
        return 1;
    if (!plat_init("ui_test", W, H) || !gfx_init(W, H, false))
        return 1;
    gfx_mouse_init();
    gfx_font_init();

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
    wm_set_input_live(true);

    /* The root has no surface and paints nothing, so like the game (whose
     * main view covers the screen) put a full-screen backdrop at the back. */
    GfxRect full = {0, W - 1, 0, H - 1};
    WmWindow *desk = wm_create_window(NULL, &full, wm_default_proc, WM_F_NOFOCUS | WM_F_NORAISE, 0);
    CHECK(desk != NULL);
    if (!desk)
        return 1;
    GfxSurface *back = gfx_load_bmp_resource(1210, NULL);
    if (back) {
        gfx_blit(desk->surface, 0, 0, back->width, back->height, back, 0, 0);
        gfx_surface_destroy(back);
    }

    uint8_t red = gfx_palette_closest(160, 20, 20), blue = gfx_palette_closest(20, 40, 160),
            green = gfx_palette_closest(20, 130, 40), grey = gfx_palette_closest(90, 90, 90);
    WmWindow *a = make_window(40, 40, 300, 200, 581, 0, "Window A (bitmap 581)", red);
    WmWindow *b = make_window(200, 120, 280, 220, 608, 0, "Window B (bitmap 608)", blue);
    WmWindow *c = make_window(120, 260, 260, 160, 1210, WM_F_MINIMIZABLE | WM_F_DRAGGABLE, "Window C", green);
    WmWindow *d = make_window(420, 30, 180, 140, 581, 0, "Window D", grey);
    if (!a || !b || !c || !d)
        return 1;

    /* a bitmap button inside B (bitmap 560 has the two frames side by side) */
    GfxSurface *bbmp = gfx_load_bmp_resource(560, NULL);
    WmWindow *btn = NULL;
    if (bbmp) {
        GfxRect br = {200, 200 + bbmp->width - 1, 150, 150 + bbmp->height - 1};
        btn = ctl_button_create(b, &br, 42, CTL_STYLE_BITMAP, bbmp);
        CHECK(btn != NULL);
    }

    wm_lower(desk);
    wm_show(desk, true);
    wm_show(a, true);
    wm_show(b, true);
    wm_show(c, true);
    wm_show(d, true);
    gfx_present();
    /* creation order = back to front reversed: a is in front of b */
    CHECK(shows(a, 250, 150));
    CHECK(shows(b, 400, 300));

    /* activating b (a click) raises it */
    wm_mouse_event(2, 400, 300);
    wm_mouse_event(5, 400, 300);
    CHECK(wm_focus() == b);
    CHECK(shows(b, 250, 150));

    /* press and release the button through the mouse path; the click is
     * posted and delivered by wm_idle */
    if (btn) {
        int bx = 200 + 3, by = 150 + 3;
        wm_point_to_screen(b, &bx, &by);
        CHECK(wm_window_at(bx, by) == btn);
        wm_mouse_event(2, bx, by);
        CHECK(ctl_is_pressed(btn));
        wm_mouse_event(5, bx, by);
        CHECK(!ctl_is_pressed(btn));
        for (int i = 0; i < 4; i++)
            wm_idle();
    }

    /* drag c by its title bar: down at (10, 5) local, move, up */
    int cx = 10, cy = 5;
    wm_point_to_screen(c, &cx, &cy);
    wm_mouse_event(2, cx, cy);
    wm_mouse_event(1, cx + 200, cy - 60);
    wm_mouse_event(5, cx + 200, cy - 60);
    CHECK(c->rect.left == 320 && c->rect.top == 200);
    CHECK(wm_capture() == NULL);
    CHECK(shows(c, 330, 230));
    /* the area c left shows what was behind it */
    CHECK(shows(a, 130, 230));
    CHECK(shows(desk, 130, 400));

    /* draw into d's surface; nothing changes until it's invalidated */
    gfx_fill_rect(d->surface, 20, 40, 60, 40, red);
    CHECK(spx(450, 90) != red);
    GfxRect part = {20, 79, 40, 79};
    wm_invalidate(d, &part);
    wm_update(d);
    CHECK(spx(450, 90) == red);

    /* hide a: b and the backdrop show where it was */
    wm_show(a, false);
    CHECK(shows(b, 250, 150));

    /* lower b behind c (c is in front anyway); raise a again */
    wm_show(a, true);
    wm_raise(a);
    CHECK(shows(a, 250, 150));

    /* minimize d via its state; d isn't minimizable, so nothing happens */
    wm_set_minimized(d, true);
    CHECK(!wm_is_minimized(d));
    /* minimize c: it shrinks to an icon at the top left of the root */
    wm_set_minimized(c, true);
    CHECK(wm_is_minimized(c));
    CHECK(c->rect.left == 0 && c->rect.top == 0 && c->rect.bottom - c->rect.top + 1 == 14);
    CHECK(shows(desk, 400, 380)); /* where c was */
    CHECK(shows(c, 5, 5));
    gfx_present();

    if (!write_ppm(argv[3])) {
        fprintf(stderr, "can't write %s\n", argv[3]);
        g_failures++;
    }

    /* restore c: back where it was */
    wm_set_minimized(c, false);
    CHECK(!wm_is_minimized(c) && c->rect.left == 320 && c->rect.top == 200);

    if (window) {
        printf("close the window or press a key\n");
        for (;;) {
            PlatEvent ev;
            bool quit = false;
            while (plat_poll_event(&ev)) {
                if (ev.type == PLAT_EV_QUIT || ev.type == PLAT_EV_CLOSE || ev.type == PLAT_EV_KEY_DOWN)
                    quit = true;
                gfx_mouse_handle_event(&ev);
            }
            if (quit)
                break;
            wm_idle();
            ctl_idle();
            gfx_present();
            plat_delay_ms(10);
        }
    }

    ctl_shutdown();
    wm_shutdown();
    CHECK(rgn_pool_free_count() == RGN_POOL_SIZE);
    gfx_font_shutdown();
    gfx_shutdown();
    plat_shutdown();
    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("ui_test: all passed, wrote %s\n", argv[3]);
    return 0;
}
