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

#include "compbun/cbres.h"
#include "formats/bmp8.h"
#include "game/resource.h"
#include "platform/platform.h"

#define SCREEN_W 640
#define SCREEN_H 480

/* Stand-ins for ALIFE.INI's "Resource file" (a hard-disk install's local
 * subset) and "CD Path" until the INI code is decompiled. */
static const char *const k_local_bundles[] = {"FULL.000", "MEDIUM.000"};
static const char *const k_cd_bundle = "ALIFE.000";

/* Reads a whole resource through the compbun stack, like the game's sound
 * code (0x4b9913). Returns a malloc'd buffer the caller frees, or NULL. */
static uint8_t *load_resource(uint32_t id, long *len)
{
    char name[RC_NAME_LEN];
    rc_resource_name(id, name);
    long size = rc_resource_size(id);
    CbHandle h = cbres_open(name, "rb");
    if (!h)
        return NULL;
    uint8_t *buf = malloc(size > 0 ? (size_t)size : 1);
    if (buf && size > 0)
        cbres_read(buf, size, 1, h);
    cbres_close(h);
    *len = size;
    return buf;
}

static int is_bmp(uint32_t id)
{
    char name[RC_NAME_LEN];
    uint8_t magic[2] = {0};
    rc_resource_name(id, name);
    CbHandle h = cbres_open(name, "rb");
    if (!h)
        return 0;
    cbres_read(magic, 1, 2, h);
    cbres_close(h);
    return magic[0] == 'B' && magic[1] == 'M';
}

static void show(uint32_t id)
{
    PlatColor pal[256] = {0};
    long len = 0;
    uint8_t *data = load_resource(id, &len);
    int w = 0, h = 0;
    memset(plat_video_framebuffer(), 0, SCREEN_W * SCREEN_H);
    if (data && bmp8_blit(data, (size_t)len, plat_video_framebuffer(), SCREEN_W, SCREEN_H, pal, &w, &h))
        plat_video_set_palette(pal, 0, 256);
    printf("resource %u: %dx%d\n", (unsigned)id, w, h);
    free(data);
}

int main(int argc, char **argv)
{
    plat_set_data_dir(argc > 1 ? argv[1] : "orig");

    const char *local = "None";
    for (size_t i = 0; i < sizeof(k_local_bundles) / sizeof(k_local_bundles[0]); i++) {
        FILE *f = plat_fopen_data(k_local_bundles[i]);
        if (f) {
            fclose(f);
            local = k_local_bundles[i];
            break;
        }
    }
    rc_initialize(local, k_cd_bundle);
    /* The original refuses to run without the CD; the viewer makes do with
     * a local bundle. */
    const RcIndex *index = rc_cd_index()->count ? rc_cd_index() : rc_local_index();
    if (!index->count) {
        fprintf(stderr, "No resource bundle found. Run tools/extract_cd.py first (see README.md).\n");
        return 1;
    }
    rc_hook_compbun();
    compbun_init(1, 0);

    uint32_t *bmps = malloc(index->count * sizeof(*bmps));
    int nbmp = 0, cur = 0;
    for (uint32_t i = 0; bmps && i < index->count; i++) {
        if (is_bmp(index->ids[i])) {
            if (index->ids[i] == 1210)
                cur = nbmp;
            bmps[nbmp++] = index->ids[i];
        }
    }
    printf("%u resources, %d bitmaps\n", (unsigned)index->count, nbmp);

    if (!plat_init("Afterlife", SCREEN_W, SCREEN_H))
        return 1;
    if (nbmp)
        show(bmps[cur]);

    for (int running = 1; running;) {
        PlatEvent ev;
        while (plat_poll_event(&ev)) {
            if (ev.type == PLAT_EV_QUIT || (ev.type == PLAT_EV_KEY_DOWN && ev.key == SDLK_ESCAPE))
                running = 0;
            if (ev.type == PLAT_EV_KEY_DOWN && nbmp && (ev.key == SDLK_LEFT || ev.key == SDLK_RIGHT)) {
                cur = (cur + (ev.key == SDLK_RIGHT ? 1 : nbmp - 1)) % nbmp;
                show(bmps[cur]);
            }
        }
        plat_video_present();
        plat_yield();
    }

    plat_shutdown();
    free(bmps);
    compbun_shutdown();
    rc_shutdown();
    return 0;
}
