/*
 * Behaviour test for the video core (src/gfx/). Needs the user's game files;
 * none are in the repo.
 *
 *   gfx_test <data dir> <bundle> <out.ppm> [bitmap id] [--window]
 *
 * Loads the game palette (resource 33) and a bitmap (default 1210) from the
 * bundle, draws them with the decompiled blits, primitives and fonts onto
 * the screen surface, presents it, and writes the framebuffer through the
 * palette as a binary PPM. Also runs a few self-checks on the palette and
 * primitives. Headless (SDL dummy video driver) unless --window is given,
 * in which case it also shows the result until a key or click.
 */
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
    for (int y = 0; y < scr->height; y++) {
        for (int x = 0; x < scr->width; x++) {
            const uint8_t *c = pal + scr->pixels[(size_t)y * scr->stride + x] * 4;
            fputc(c[0], f);
            fputc(c[1], f);
            fputc(c[2], f);
        }
    }
    return fclose(f) == 0;
}

static uint8_t px(const GfxSurface *s, int x, int y) { return s->pixels[(size_t)y * s->stride + x]; }

/* Primitive checks on a small scratch surface. */
static void self_checks(void)
{
    GfxSurface *s = gfx_surface_create(10, 6);
    CHECK(s && s->stride == 12);
    if (!s)
        return;
    gfx_fill_rect(s, -2, -2, 5, 4, 7); /* clips to (0,0)-(2,1) */
    CHECK(px(s, 2, 1) == 7 && px(s, 3, 1) == 0 && px(s, 2, 2) == 0);
    gfx_line(s, 0, 9, 5, 5, 3); /* inclusive horizontal line */
    CHECK(px(s, 0, 5) == 3 && px(s, 9, 5) == 3);
    gfx_line(s, 9, 4, 0, 5, 4); /* diagonal, reversed */
    CHECK(px(s, 9, 0) == 4 && px(s, 4, 5) == 4);
    GfxRect r = {8, 5, 4, 2}; /* unnormalized */
    gfx_frame_rect(s, &r, 9);
    CHECK(r.left == 5 && r.right == 8 && r.top == 2 && r.bottom == 4);
    CHECK(px(s, 5, 3) == 9 && px(s, 8, 3) == 9 && px(s, 7, 3) == 0 && px(s, 6, 4) == 9);

    GfxSurface *spr = gfx_surface_create(2, 2);
    spr->pixels[0] = 5;
    spr->pixels[spr->stride + 1] = 6;
    memset(s->pixels, 1, (size_t)s->stride * s->height);
    gfx_blit_transparent(s, 9, 5, 2, 2, spr, 0, 0, 0); /* clipped to 1x1 */
    CHECK(px(s, 9, 5) == 5);
    gfx_blit_transparent(s, 0, 0, 2, 2, spr, 0, 0, 0);
    CHECK(px(s, 0, 0) == 5 && px(s, 1, 0) == 1 && px(s, 1, 1) == 6);
    gfx_tile_fill(s, 0, 0, 5, 3, spr, 0, 0, 2, 2);
    CHECK(px(s, 4, 0) == 5 && px(s, 3, 1) == 6 && px(s, 2, 2) == 5);
    GfxRect sr = {0, 9, 0, 5};
    GfxRect upd = gfx_scroll_rect(s, &sr, 0, 2);
    CHECK(px(s, 4, 2) == 5 && upd.top == 0 && upd.bottom == 1 && upd.left == 0 && upd.right == 9);
    gfx_surface_destroy(spr);
    gfx_surface_destroy(s);

    /* Palette: exact matches, rotation, game-range clamping. */
    uint8_t pal[256 * 4] = {0}, back[256 * 4];
    for (int i = 0; i < 256; i++) {
        pal[i * 4] = (uint8_t)i;
        pal[i * 4 + 1] = (uint8_t)(255 - i);
        pal[i * 4 + 2] = (uint8_t)(i * 7);
    }
    gfx_palette_set(pal, 0, 256);
    gfx_palette_get(back, 0, 256);
    CHECK(back[5 * 4] != 5 && back[100 * 4] == 100 && back[250 * 4] != 250); /* statics untouched */
    CHECK(gfx_palette_closest(100, 155, (uint8_t)(100 * 7)) == 100);
    gfx_palette_rotate(20, 3, 1); /* 20 21 22 -> 22 20 21 */
    gfx_palette_get(back, 0, 256);
    CHECK(back[20 * 4] == 22 && back[21 * 4] == 20 && back[22 * 4] == 21);
    gfx_palette_rotate(20, 3, -1);
    gfx_palette_get(back, 0, 256);
    CHECK(back[20 * 4] == 20 && back[22 * 4] == 22);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <data dir> <bundle> <out.ppm> [bitmap id] [--window]\n", argv[0]);
        return 2;
    }
    bool window = argc > 5 && strcmp(argv[5], "--window") == 0;
    if (!window) {
#ifdef _WIN32
        _putenv_s("SDL_VIDEODRIVER", "dummy");
#else
        setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    }
    uint32_t bmp_id = argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 0) : 1210;
    plat_set_data_dir(argv[1]);
    if (!rc_initialize(argv[2], argv[2]))
        return 1;
    if (!plat_init("gfx_test", W, H) || !gfx_init(W, H, false))
        return 1;
    gfx_mouse_init();
    gfx_cycle_init();
    gfx_font_init();
    self_checks();

    /* The game palette, as the game's main (0x401000) sets it up. */
    long len = 0;
    uint8_t *raw = read_resource(33, &len);
    uint8_t *pal = gfx_palette_load_resource(raw, (size_t)len);
    free(raw);
    CHECK(pal != NULL);

    uint8_t *bmp_pal = NULL;
    GfxSurface *bmp = gfx_load_bmp_resource(bmp_id, &bmp_pal);
    CHECK(bmp != NULL);
    /* Prefer the bitmap's own palette (backgrounds ship with theirs). */
    gfx_palette_set(bmp_pal ? bmp_pal : pal, GFX_PAL_FIRST, GFX_PAL_COUNT);
    gfx_palette_build_highlight();

    gfx_fill_rect(NULL, 0, 0, W, H, 0);
    if (bmp) {
        printf("bitmap %u: %dx%d\n", (unsigned)bmp_id, bmp->width, bmp->height);
        /* Full-size copy, then a 4x4 tiled thumbnail and a keyed copy. */
        gfx_blit(NULL, 0, 0, bmp->width, bmp->height, bmp, 0, 0);
        GfxSurface *tile = gfx_surface_create(64, 48);
        gfx_blit(tile, 0, 0, 64, 48, bmp, bmp->width / 2, bmp->height / 2);
        gfx_tile_fill(NULL, W - 160, 10, 150, 100, tile, 0, 0, 64, 48);
        gfx_blit_transparent(NULL, W - 160, 120, 64, 48, tile, 0, 0, px(tile, 0, 0));
        gfx_surface_destroy(tile);
    }

    uint8_t white = gfx_palette_closest(255, 255, 255), black = gfx_palette_closest(0, 0, 0);
    uint8_t grey = gfx_palette_closest(128, 128, 128), red = gfx_palette_closest(255, 0, 0);
    GfxRect box = {20, 300, 300, 460};
    gfx_bevel_rect(NULL, &box, white, grey, black, false);
    GfxRect inner = {30, 290, 340, 450};
    gfx_bevel_rect(NULL, &inner, white, grey, black, true);
    gfx_dotted_frame(NULL, 310, 300, 140, 160, white, 0xcc);
    for (int i = 0; i < 8; i++)
        gfx_line(NULL, 320, 440, 310 + i * 18, 450 - i * 18, red);

    /* Text: the built-in font if ALIFE.EXE is there, then fonts 500/501. */
    unsigned col[2] = {white, black};
    int tw = 0;
    bool builtin = gfx_font_current() == GFX_FONT_BUILTIN && gfx_font_height(0) > 0;
    PlatCursor *cur = gfx_cursor_load(0x7d); /* the game's default cursor */
    gfx_cursor_set(cur);
    if (builtin) {
        gfx_font_set_colors(0, 1, 2, col);
        tw = gfx_font_draw_text(NULL, 30, 305, NULL, NULL, "Built-in font &Afterlife", 0);
    }
    gfx_font_select(500);
    if (gfx_font_current() == 500) {
        gfx_font_set_colors(0, 1, 2, col);
        tw = gfx_font_draw_text(NULL, 30, 320, NULL, NULL, "Font 500: Heaven and Hell", 0);
    }
    printf("text width %d, built-in font %s, cursor 0x7d %s\n", tw, builtin ? "loaded" : "missing",
           cur ? "loaded" : "missing");
    gfx_font_draw_wrapped(NULL, &inner,
                          "Font 501, wrapped: The Afterlife is a sim of the hereafter. Build Heaven and Hell, "
                          "keep the souls of the Planet coming in, and balance the books.",
                          501, black, white);

    gfx_present();
    CHECK(write_ppm(argv[3]));

    if (window) {
        for (bool run = true; run;) {
            PlatEvent ev;
            while (plat_poll_event(&ev)) {
                gfx_mouse_handle_event(&ev);
                if (ev.type == PLAT_EV_QUIT || ev.type == PLAT_EV_KEY_DOWN || ev.type == PLAT_EV_MOUSE_DOWN)
                    run = false;
            }
            gfx_cycle_idle();
            gfx_present();
            plat_delay_ms(16);
        }
    }

    gfx_cursor_destroy(cur);
    gfx_surface_destroy(bmp);
    free(bmp_pal);
    free(pal);
    gfx_font_shutdown();
    gfx_shutdown();
    plat_shutdown();
    rc_shutdown();
    printf("%s (%d failures), surfaces leaked: %ld bytes\n", g_failures ? "FAILED" : "ok", g_failures,
           gfx_surface_bytes());
    return g_failures ? 1 : 0;
}
