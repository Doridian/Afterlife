/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Regression test for the screen's row pitch (no game data needed): at a
 * width that isn't a multiple of 4 (1707 x 1067, a "Resolution = MAX" on
 * a 1707-pixel desktop) the gfx screen surface must use the platform
 * framebuffer's pitch (SDL aligns INDEX8 rows to 4 bytes), or every row
 * shifts by a pixel more than the last and the picture shears.
 *
 * Draws a distinct pattern into an off-screen surface (DIB stride), blits
 * it to the screen, presents, and compares every pixel of the platform
 * framebuffer (read at its pitch, as SDL's blit to the texture reads it)
 * and an SDL readback of the presented picture with the source.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "gfx/palette.h"
#include "gfx/surface.h"
#include "platform/platform.h"

#define W 1707
#define H 1067

int main(void)
{
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_RENDER_DRIVER", "software", 1);
    int failures = 0;
    if (!plat_init("stride_test", W, H) || !gfx_init(W, H, false)) {
        fprintf(stderr, "init failed\n");
        return 1;
    }
    GfxSurface *scr = gfx_screen();
    printf("screen %dx%d stride %d, platform pitch %d\n", scr->width, scr->height, scr->stride, plat_video_pitch());
    if (scr->width != W || scr->height != H || scr->stride != plat_video_pitch()) {
        fprintf(stderr, "FAIL: screen stride %d != platform pitch %d\n", scr->stride, plat_video_pitch());
        failures++;
    }
    /* a gray ramp palette so the readback can tell indexes apart */
    uint8_t pal[1024];
    for (int i = 0; i < 256; i++)
        pal[i * 4] = pal[i * 4 + 1] = pal[i * 4 + 2] = (uint8_t)i, pal[i * 4 + 3] = 0;
    gfx_palette_set(pal, 0, 256);
    GfxSurface *src = gfx_surface_create(W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            src->pixels[(size_t)y * (size_t)src->stride + (size_t)x] = (uint8_t)(10 + (x * 7 + y * 13) % 236);
    gfx_blit(scr, 0, 0, W, H, src, 0, 0);
    gfx_invalidate(NULL);
    gfx_present();

    const uint8_t *fb = plat_video_framebuffer();
    int bad = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (fb[(size_t)y * (size_t)plat_video_pitch() + (size_t)x] !=
                src->pixels[(size_t)y * (size_t)src->stride + (size_t)x])
                bad++;
    printf("framebuffer: %d pixels differ\n", bad);
    if (bad)
        failures++;

    /* what SDL makes of the framebuffer: the same conversion the present
     * does (indexed surface -> RGB), compared against the palette */
    SDL_Palette *sp = SDL_CreatePalette(256);
    SDL_Color cols[256];
    for (int i = 0; i < 256; i++)
        cols[i] = (SDL_Color){(Uint8)i, (Uint8)i, (Uint8)i, 255};
    SDL_SetPaletteColors(sp, cols, 0, 256);
    SDL_Surface *ind = SDL_CreateSurfaceFrom(W, H, SDL_PIXELFORMAT_INDEX8, (void *)fb, plat_video_pitch());
    SDL_SetSurfacePalette(ind, sp);
    SDL_Surface *rgb = SDL_ConvertSurface(ind, SDL_PIXELFORMAT_RGB24);
    bad = 0;
    for (int y = 0; rgb && y < H; y++)
        for (int x = 0; x < W; x++)
            if (((const uint8_t *)rgb->pixels)[(size_t)y * (size_t)rgb->pitch + (size_t)x * 3] !=
                src->pixels[(size_t)y * (size_t)src->stride + (size_t)x])
                bad++;
    printf("SDL conversion: %d pixels differ\n", rgb ? bad : -1);
    if (!rgb || bad)
        failures++;
    SDL_DestroySurface(rgb);
    SDL_DestroySurface(ind);
    SDL_DestroyPalette(sp);
    gfx_surface_destroy(src);
    gfx_shutdown();
    plat_shutdown();
    printf("%s (%d failures)\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}
