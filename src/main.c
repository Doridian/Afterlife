/*
 * Entry point. Until the game's own WinMain is decompiled, this runs a small
 * resource viewer that exercises the platform layer and the bundle reader:
 * left/right pages through the bitmaps in the bundle, Escape quits.
 */
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_keycode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "formats/bmp8.h"
#include "formats/jiff.h"
#include "platform/platform.h"

#define SCREEN_W 640
#define SCREEN_H 480

static const char *const k_bundles[] = {"ALIFE.000", "FULL.000", "MEDIUM.000"};

static int is_bmp(const JiffBundle *b, const JiffEntry *e)
{
    uint8_t magic[2];
    return fseek(b->f, (long)e->offset, SEEK_SET) == 0 && fread(magic, 1, 2, b->f) == 2 && magic[0] == 'B' &&
           magic[1] == 'M';
}

static void show(const JiffBundle *b, const JiffEntry *e)
{
    PlatColor pal[256] = {0};
    uint8_t *data = jiff_load(b, e);
    int w = 0, h = 0;
    memset(plat_video_framebuffer(), 0, SCREEN_W * SCREEN_H);
    if (data && bmp8_blit(data, e->length, plat_video_framebuffer(), SCREEN_W, SCREEN_H, pal, &w, &h))
        plat_video_set_palette(pal, 0, 256);
    printf("resource %u: %dx%d\n", e->id, w, h);
    free(data);
}

int main(int argc, char **argv)
{
    plat_set_data_dir(argc > 1 ? argv[1] : "orig");

    JiffBundle bundle;
    FILE *f = NULL;
    for (size_t i = 0; i < sizeof(k_bundles) / sizeof(k_bundles[0]) && !f; i++)
        f = plat_fopen_data(k_bundles[i]);
    if (!f || !jiff_open(&bundle, f)) {
        fprintf(stderr, "No resource bundle found. Run tools/extract_cd.py first (see README.md).\n");
        return 1;
    }

    const JiffEntry **bmps = malloc(bundle.count * sizeof(*bmps));
    int nbmp = 0, cur = 0;
    for (uint32_t i = 0; i < bundle.count; i++) {
        if (is_bmp(&bundle, &bundle.entries[i])) {
            if (bundle.entries[i].id == 1210)
                cur = nbmp;
            bmps[nbmp++] = &bundle.entries[i];
        }
    }
    printf("%u resources, %d bitmaps\n", bundle.count, nbmp);

    if (!plat_init("Afterlife", SCREEN_W, SCREEN_H))
        return 1;
    if (nbmp)
        show(&bundle, bmps[cur]);

    for (int running = 1; running;) {
        PlatEvent ev;
        while (plat_poll_event(&ev)) {
            if (ev.type == PLAT_EV_QUIT || (ev.type == PLAT_EV_KEY_DOWN && ev.key == SDLK_ESCAPE))
                running = 0;
            if (ev.type == PLAT_EV_KEY_DOWN && nbmp && (ev.key == SDLK_LEFT || ev.key == SDLK_RIGHT)) {
                cur = (cur + (ev.key == SDLK_RIGHT ? 1 : nbmp - 1)) % nbmp;
                show(&bundle, bmps[cur]);
            }
        }
        plat_video_present();
        plat_yield();
    }

    plat_shutdown();
    free(bmps);
    jiff_close(&bundle);
    return 0;
}
