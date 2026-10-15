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
 * The game itself is game_main (0x401000, src/game/shell.c): the title,
 * then the main loop until the player quits.
 */
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/config.h"
#include "game/ini.h"
#include "game/resource.h"
#include "game/shell.h"
#include "game/sound.h"
#include "game/startup.h"
#include "game/timer.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/mouse.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "imuse/imuse.h"
#include "platform/platform.h"
#include "ui/controls.h"
#include "ui/wm.h"

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
    printf("Display:         %dx%d framebuffer, flags %u\n", gfx_screen_width(), gfx_screen_height(),
           gfx_display_flags());
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

    /* app_register_class (0x4cd892): the window class and a hidden 1x1
     * window, before any subsystem. The display init sizes and shows it. */
    if (!plat_init(NULL, 1, 1))
        return 1;

    int ret = 1;
    if (sys_initialize() == 0) {
        print_config();
        ret = quit_after_init ? 0 : game_main(argc, argv);
    }
    sys_shutdown();
    plat_shutdown();
    return ret;
}
