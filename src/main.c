/*
 * Entry point: the port's version of the CRT entry (0x4b1007) and WinMain
 * (win_main 0x4c62e0). Follows the original's startup (see
 * docs/startup.md): parse the command line, pick the INI file, run the
 * subsystem init sequence, run the game, shut down.
 *
 * Usage: afterlife [DATA_DIR] [FILE.INI] [SAVE.ALF|SCENARIO.ASC] [--quit]
 *                  [--enhancements | --no-enhancements] [--ui-scale=N]
 *                  [--port-ini=FILE] [--window-size=WxH] [--fullscreen | --windowed]
 *                  [--window-test=DIR] [--debug-window]
 *   DATA_DIR stands in for the original's install folder (default: the
 *   folder of FILE.INI if one is given, else "orig", in the current folder
 *   or next to the executable, else the folder or CD image the player
 *   picked, platform/datafind.c). FILE.INI is resolved
 *   in it (default ALIFE.ini, matched case-insensitively). --quit exits right
 *   after initialisation (for testing). The rest are port settings
 *   (game/enhance.h, docs/enhancements.md), overriding ALIFEPORT.INI.
 *   --window-test=DIR is a port self-check of the real window: once the
 *   game runs it resizes the window through SDL, changes the UI scale,
 *   toggles fullscreen with injected Alt+Enter and F11 key events, saves a
 *   screenshot of the window (BMP) into DIR after each step, and quits.
 *   Message boxes are answered with Enter. Run it on a headless compositor
 *   (weston --backend=headless) or SDL's offscreen driver, not a desktop.
 *   --debug-window opens the original's unused "Debug" tile browser
 *   (ui/debugwin.h) once a game is running.
 *
 * The game itself is game_main (0x401000, src/game/shell.c): the title,
 * then the main loop until the player quits.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/config.h"
#include "game/display.h"
#include "game/enhance.h"
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
#include "platform/datafind.h"
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
    printf("Port settings:   %s\n", port_settings_path());
    printf("Enhancements:    %s, UI scale %d\n", g_enhancements ? "on" : "off", plat_ui_scale());
    PlatWindowState ws;
    plat_window_get_state(&ws);
    printf("Window:          %dx%d%s%s\n", ws.w, ws.h, ws.maximized ? ", maximized" : "",
           ws.fullscreen ? ", fullscreen" : "");
    if (g_arg_load_file[0])
        printf("Load file:       %s\n", g_arg_load_file);
}

/* --- --window-test ---------------------------------------------------------- */

static const char *g_wt_dir;
static int g_wt_step;
static uint32_t g_wt_at;

static void wt_push_key(SDL_Scancode sc, SDL_Keycode key, SDL_Keymod mod, bool down)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = sc;
    e.key.key = key;
    e.key.mod = mod;
    e.key.down = down;
    SDL_PushEvent(&e);
}

/* a key press, with Alt held around it if `alt` */
static void wt_key(SDL_Scancode sc, SDL_Keycode key, bool alt)
{
    SDL_Keymod mod = alt ? SDL_KMOD_LALT : SDL_KMOD_NONE;
    if (alt)
        wt_push_key(SDL_SCANCODE_LALT, SDLK_LALT, mod, true);
    wt_push_key(sc, key, mod, true);
    wt_push_key(sc, key, mod, false);
    if (alt)
        wt_push_key(SDL_SCANCODE_LALT, SDLK_LALT, SDL_KMOD_NONE, false);
}

static void wt_shot(const char *name)
{
    char path[1200];
    int pw, ph;
    plat_window_pixel_size(&pw, &ph);
    snprintf(path, sizeof path, "%s/%s.bmp", g_wt_dir, name);
    bool ok = plat_video_screenshot(path);
    printf("window-test %-14s window %dx%d px, screen %dx%d, UI scale %d, fullscreen %d, relayouts %u -> %s%s\n",
           name, pw, ph, gfx_screen_width(), gfx_screen_height(), plat_ui_scale(), plat_window_fullscreen(),
           display_relayout_count(), path, ok ? "" : " (FAILED)");
    fflush(stdout);
}

static void window_test_pump(void)
{
    uint32_t now = plat_ticks_ms();
    /* answer message boxes (the scenario's FYI) with Enter */
    static uint32_t box_at;
    WmWindow *box = ctl_msgbox_window();
    if (box && wm_is_visible(box)) {
        if ((int32_t)(now - box_at) >= 0) {
            box_at = now + 500;
            wt_key(SDL_SCANCODE_RETURN, SDLK_RETURN, false);
        }
        return;
    }
    if (!g_game_running)
        return;
    if (g_wt_step == 0) {
        g_wt_at = now + 3000;
        g_wt_step = 1;
    }
    if ((int32_t)(now - g_wt_at) < 0)
        return;
    g_wt_at = now + 2500;
    switch (g_wt_step++) {
    case 1:
        wt_shot("01-start");
        plat_window_resize(1000, 700);
        break;
    case 2:
        wt_shot("02-1000x700");
        plat_window_resize(1600, 1000);
        break;
    case 3:
        wt_shot("03-1600x1000");
        display_apply_settings(true, 2);
        break;
    case 4:
        wt_shot("04-scale2");
        wt_key(SDL_SCANCODE_RETURN, SDLK_RETURN, true);
        break;
    case 5:
        wt_shot("05-fullscreen");
        wt_key(SDL_SCANCODE_F11, SDLK_F11, false);
        break;
    case 6:
        wt_shot("06-windowed");
        display_apply_settings(false, 0);
        break;
    case 7:
        wt_shot("07-scale1");
        g_game_running = 0;
        break;
    default:
        break;
    }
}

static void save_window_state(void)
{
    if (!g_wt_dir)
        enhance_save_window_state();
}

/* No DATA_DIR: "orig" in the current folder, else "orig" next to the
 * executable (SDL_GetBasePath: inside an app bundle on macOS, its
 * Contents/Resources), as a program started from a file manager or the
 * Dock doesn't run in its own folder. */
static const char *default_data_dir(void)
{
    static char path[1024];
    SDL_PathInfo info;
    if (SDL_GetPathInfo("orig", &info) && info.type == SDL_PATHTYPE_DIRECTORY)
        return "orig";
    const char *base = SDL_GetBasePath();
    if (base) {
        snprintf(path, sizeof path, "%sorig", base);
        if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY)
            return path;
    }
    return "orig";
}

int main(int argc, char **argv)
{
    bool quit_after_init = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quit") == 0)
            quit_after_init = true;
        else if (strncmp(argv[i], "--window-test=", 14) == 0)
            g_wt_dir = argv[i] + 14;
    }

    /* app_already_running (0x4c6b3e) and app_chdir_to_exe (0x4c6a64) have
     * no port equivalent; the data dir plays the role of the exe's folder. */
    const char *dir = NULL;
    if (!app_parse_args(argc, argv, &dir)) {
        printf("usage: %s [DATA_DIR] [FILE.INI] [SAVE.ALF|SCENARIO.ASC] [--quit]\n"
               "       [--enhancements | --no-enhancements] [--ui-scale=N (0 = auto)] [--port-ini=FILE]\n"
               "       [--window-size=WxH] [--fullscreen | --windowed] [--window-test=DIR] [--debug-window]\n",
               argv[0]);
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
    ini_set_path(ini_name);

    /* app_register_class (0x4cd892): the window class and a hidden 1x1
     * window, before any subsystem. The display init sizes and shows it. */
    if (!plat_init(NULL, 1, 1))
        return 1;

#ifdef __EMSCRIPTEN__
    plat_set_data_dir(dir ? dir : default_data_dir());
#else
    /* No DATA_DIR and no game data in the default place: the player picks
     * the CD or an image of it (platform/datafind.c), unless headless. */
    if (!dir) {
        const char *driver = SDL_GetCurrentVideoDriver();
        bool ask = !quit_after_init && driver && strcmp(driver, "dummy") != 0 && strcmp(driver, "offscreen") != 0;
        dir = datafind_data_dir(default_data_dir(), ask);
        if (!dir) {
            plat_shutdown();
            return 1;
        }
    }
    plat_set_data_dir(dir);
#endif

    /* Port settings (ALIFEPORT.INI and the switches above); the UI scale
     * must be known before the display init reads the desktop size. */
    enhance_init(argc, argv);
    plat_set_ui_scale(enhance_ui_scale_request());
    /* the window's size, place and fullscreen state are kept for the next
     * run, however the game ends */
    atexit(save_window_state);
    if (g_wt_dir)
        sys_pump_hook = window_test_pump;

    int ret = 1;
    if (sys_initialize() == 0) {
        print_config();
        ret = quit_after_init ? 0 : game_main(argc, argv);
    }
    sys_shutdown();
    plat_shutdown();
    return ret;
}
