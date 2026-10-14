/*
 * Entry point: the port's version of the CRT entry (0x4b1007) and WinMain
 * (win_main 0x4c62e0). Follows the original's startup (see
 * docs/startup.md): parse the command line, pick the INI file, run the
 * subsystem init sequence, run the game, shut down.
 *
 * Usage: afterlife [DATA_DIR] [FILE.INI] [SAVE.ALF|SCENARIO.ASC] [--quit]
 *   DATA_DIR stands in for the original's install folder (default: the
 *   folder of FILE.INI if one is given, else "orig"). FILE.INI is resolved
 *   in it (default ALIFE.ini, matched case-insensitively). --quit exits right
 *   after initialisation (for testing).
 *
 * game_main (0x401000) isn't decompiled yet. Until it is, this runs a small
 * resource viewer on top of the initialised subsystems: left/right pages
 * through the bitmaps in the bundles, Escape quits.
 */
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_keycode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compbun/cbres.h"
#include "formats/bmp8.h"
#include "game/config.h"
#include "game/ini.h"
#include "game/resource.h"
#include "game/startup.h"
#include "platform/platform.h"

/* --- placeholder for game_main ------------------------------------------- */

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
    int sw = plat_video_width(), sh = plat_video_height();
    memset(plat_video_framebuffer(), 0, (size_t)sw * (size_t)sh);
    if (data && bmp8_blit(data, (size_t)len, plat_video_framebuffer(), sw, sh, pal, &w, &h))
        plat_video_set_palette(pal, 0, 256);
    printf("resource %u: %dx%d\n", (unsigned)id, w, h);
    free(data);
}

static uint32_t *g_bmps;
static int g_nbmp, g_cur;
static bool g_escape;

static void viewer_event(const PlatEvent *ev)
{
    if (ev->type != PLAT_EV_KEY_DOWN)
        return;
    if (ev->key == SDLK_ESCAPE)
        g_escape = true;
    if (g_nbmp && (ev->key == SDLK_LEFT || ev->key == SDLK_RIGHT)) {
        g_cur = (g_cur + (ev->key == SDLK_RIGHT ? 1 : g_nbmp - 1)) % g_nbmp;
        show(g_bmps[g_cur]);
    }
}

/* Stand-in for game_main (0x401000). The original's shape:
 *   load cursors, palette, bitmaps; create the main and control-panel
 *   windows; intro/title unless a file was given on the command line;
 *   init the game modules; then
 *   do { sys_pump(); game_frame_update(); ui_update(...); sys_pump();
 *        ...; switch (game_command) { load, save, new game, ... }
 *   } while (game_running);
 */
static int game_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!g_config.sound_enabled) {
        fprintf(stderr, "Sound = NONE: compbun is not initialised, nothing to show.\n");
        return 0;
    }
    const RcIndex *index = rc_cd_index()->count ? rc_cd_index() : rc_local_index();
    g_bmps = malloc((index->count ? index->count : 1) * sizeof(*g_bmps));
    for (uint32_t i = 0; g_bmps && i < index->count; i++) {
        if (is_bmp(index->ids[i])) {
            if (index->ids[i] == 1210)
                g_cur = g_nbmp;
            g_bmps[g_nbmp++] = index->ids[i];
        }
    }
    printf("%u resources, %d bitmaps\n", (unsigned)index->count, g_nbmp);
    if (g_nbmp)
        show(g_bmps[g_cur]);

    sys_set_event_hook(viewer_event);
    while (!sys_quit_requested() && !g_escape) {
        sys_pump();
        plat_video_present();
    }
    sys_set_event_hook(NULL);
    free(g_bmps);
    return 0;
}

/* --- entry ---------------------------------------------------------------- */

static void print_config(void)
{
    const AlConfig *c = &g_config;
    printf("INI file:        %s\n", ini_path());
    printf("Heap size:       %u bytes\n", (unsigned)c->heap_size);
    printf("CD Path:         \"%s\" (%u resources)\n", c->cd_path, (unsigned)rc_cd_index()->count);
    printf("Resource file:   \"%s\" (%u resources)\n", c->resource_file,
           (unsigned)rc_local_index()->count);
    printf("Resolution:      %s -> %dx%d, mode %d\n", c->resolution_max ? "MAX" : "set", c->width,
           c->height, c->display_mode);
    printf("Color depth:     %s%d\n", c->has_color_depth ? "" : "don't care / ", c->color_depth);
    printf("Window name:     \"%s\"\n", c->window_name);
    printf("Sound:           %s\n", c->sound_enabled ? "on" : "NONE");
    printf("Music stream:    %d, %d, %d\n", c->music_stream[0], c->music_stream[1],
           c->music_stream[2]);
    printf("Voice stream:    %d, %d, %d\n", c->voice_stream[0], c->voice_stream[1],
           c->voice_stream[2]);
    printf("Mixer channels:  %d\n", c->mixer_channels);
    if (g_arg_load_file[0])
        printf("Load file:       %s\n", g_arg_load_file);
}

int main(int argc, char **argv)
{
    bool quit_after_init = false;
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--quit") == 0)
            quit_after_init = true;

    /* app_already_running (0x4c6b3e) and app_chdir_to_exe (0x4c6a64) have
     * no port equivalent; the data dir plays the role of the exe's folder. */
    const char *dir = NULL;
    if (!app_parse_args(argc, argv, &dir)) {
        printf("usage: %s [DATA_DIR] [FILE.INI] [SAVE.ALF|SCENARIO.ASC] [--quit]\n", argv[0]);
        return 0;
    }

    /* An INI path with a directory and no DATA_DIR: use its folder. */
    static char ini_dir[1024];
    const char *ini_name = g_arg_ini_path;
    const char *slash = strrchr(g_arg_ini_path, '/');
    const char *bslash = strrchr(g_arg_ini_path, '\\');
    if (bslash && (!slash || bslash > slash))
        slash = bslash;
    if (slash && !dir) {
        size_t n = (size_t)(slash - g_arg_ini_path);
        if (n >= sizeof(ini_dir))
            n = sizeof(ini_dir) - 1;
        memcpy(ini_dir, g_arg_ini_path, n);
        ini_dir[n] = '\0';
        dir = n ? ini_dir : "/";
        ini_name = slash + 1;
    }
    plat_set_data_dir(dir ? dir : "orig");
    ini_set_path(ini_name);

    int ret = 1;
    if (sys_initialize() == 0) {
        print_config();
        ret = quit_after_init ? 0 : game_main(argc, argv);
    }
    sys_shutdown();
    return ret;
}
