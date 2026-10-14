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
 * game_main (0x401000) isn't decompiled yet. Until it is, this runs a smoke
 * test on top of the initialised subsystems: a window manager window
 * showing the bundles' bitmaps with a button, and a music track through the
 * game's sound module. Left/right page through the bitmaps; the button,
 * Escape or closing the window quits.
 */
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/config.h"
#include "game/ini.h"
#include "game/resource.h"
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

/* --- placeholder for game_main ------------------------------------------- */

#define VIEW_TITLE_H 16       /* title strip of the viewer window */
#define VIEW_BUTTON_BMP 560   /* a bitmap button: normal | pressed side by side */
#define VIEW_BUTTON_ID 1
#define VIEW_MUSIC_ID 65595   /* a 3 minute music track (iMUS, streamed) */

typedef struct {
    WmWindow *win, *button;
    uint32_t *bmps;
    int nbmp, cur;
    bool quit;
    long keys, clicks;
} Viewer;

static Viewer g_view;

static int is_bmp(uint32_t id)
{
    uint8_t magic[2] = {0};
    PlatFile *f = rc_open_resource(id);
    if (!f)
        return 0;
    plat_file_read(f, magic, 1, 2);
    plat_file_close(f);
    return magic[0] == 'B' && magic[1] == 'M';
}

/* Redraws the viewer window's surface: title strip, the current bitmap
 * (with its own palette) and the button, then repaints it. */
static void view_draw(void)
{
    WmWindow *w = g_view.win;
    GfxSurface *s = w->surface;
    uint8_t *pal = NULL;
    GfxSurface *bmp = g_view.nbmp ? gfx_load_bmp_resource(g_view.bmps[g_view.cur], &pal) : NULL;
    if (pal)
        gfx_palette_set(pal, 0, 256); /* clamped to the game's 10-245 */
    free(pal);
    uint8_t frame = gfx_palette_closest(60, 60, 140), black = gfx_palette_closest(0, 0, 0);
    gfx_fill_rect(s, 0, 0, s->width, s->height, black);
    int aw = s->width - 4, ah = s->height - VIEW_TITLE_H - 2;
    if (bmp) {
        int bw = bmp->width < aw ? bmp->width : aw, bh = bmp->height < ah ? bmp->height : ah;
        gfx_blit(s, 2 + (aw - bw) / 2, VIEW_TITLE_H + (ah - bh) / 2, bw, bh, bmp, 0, 0);
    }
    GfxRect fr = {0, s->width - 1, 0, s->height - 1};
    gfx_frame_rect(s, &fr, frame);
    gfx_fill_rect(s, 1, 1, s->width - 2, VIEW_TITLE_H - 2, frame);
    char title[64];
    if (bmp)
        snprintf(title, sizeof(title), "Resource %u (%dx%d)", (unsigned)g_view.bmps[g_view.cur], bmp->width,
                 bmp->height);
    else
        snprintf(title, sizeof(title), "No bitmaps");
    wm_draw_title(w, title, GFX_FONT_BUILTIN, gfx_palette_closest(255, 255, 255), frame);
    if (g_view.button)
        ctl_button_draw(g_view.button, (unsigned)wm_get_data(g_view.button, CTL_BUTTON_STATE) | CTL_DRAW_BITMAP);
    if (bmp)
        printf("resource %u: %dx%d\n", (unsigned)g_view.bmps[g_view.cur], bmp->width, bmp->height);
    gfx_surface_destroy(bmp);
    wm_redraw(w, NULL);
}

static intptr_t view_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_CLOSE: /* the main window's close button (WM_CLOSE) */
        g_view.quit = true;
        return 0;
    case WM_MSG_CTL_CLICK:
        if (a == VIEW_BUTTON_ID)
            g_view.quit = true;
        return 0;
    case WM_MSG_CHAR:
        g_view.keys++;
        if (a == 0x1b)
            g_view.quit = true;
        return 0;
    case WM_MSG_KEYDOWN:
        g_view.keys++;
        if (g_view.nbmp && (a == PLAT_VK_LEFT || a == PLAT_VK_RIGHT)) {
            g_view.cur = (g_view.cur + (a == PLAT_VK_RIGHT ? 1 : g_view.nbmp - 1)) % g_view.nbmp;
            view_draw();
        }
        return 0;
    case WM_MSG_LBUTTONDOWN:
        g_view.clicks++;
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

/* Stand-in for game_main (0x401000). The original's shape:
 *   load cursors, palette, bitmaps; create the main and control-panel
 *   windows; intro/title unless a file was given on the command line;
 *   init the game modules; then
 *   do { sys_pump(); game_frame_update(); ui_update(...); sys_pump();
 *        ...; switch (game_command) { load, save, new game, ... }
 *   } while (game_running);
 *   snd_slot_stop_stream(199)
 * This one shows the bundle's bitmaps in a draggable window manager window
 * with a bitmap button (which quits), and streams a music track through
 * the game's sound API. Keys arrive through the window manager (wm_idle
 * pops the keyboard queue): left/right page, Escape quits; so does the
 * window's close button (message 0x406). AFTERLIFE_FRAMES=n quits after n
 * pumps (testing). */
static int game_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    PlatCursor *arrow = gfx_cursor_load(0x7d); /* the game's default cursor */
    if (arrow)
        gfx_cursor_set(arrow);

    const RcIndex *index = rc_cd_index()->count ? rc_cd_index() : rc_local_index();
    g_view.bmps = malloc((index->count ? index->count : 1) * sizeof(*g_view.bmps));
    for (uint32_t i = 0; g_view.bmps && i < index->count; i++) {
        if (is_bmp(index->ids[i])) {
            if (index->ids[i] == 1210)
                g_view.cur = g_view.nbmp;
            g_view.bmps[g_view.nbmp++] = index->ids[i];
        }
    }
    printf("%u resources, %d bitmaps\n", (unsigned)index->count, g_view.nbmp);

    /* The root paints nothing: a black backdrop behind everything, as the
     * game's main view covers the screen. */
    int sw = gfx_screen_width(), sh = gfx_screen_height();
    GfxRect full = {0, sw - 1, 0, sh - 1};
    WmWindow *desk = wm_create_window(NULL, &full, wm_default_proc, WM_F_NOFOCUS | WM_F_NORAISE, 0);
    GfxRect r = {16, sw - 17, 16, sh - 17};
    g_view.win = wm_create_window(NULL, &r, view_proc, WM_F_DRAGGABLE, 0);
    if (!desk || !g_view.win) {
        fprintf(stderr, "game_main: cannot create the windows\n");
        free(g_view.bmps);
        return 1;
    }
    gfx_fill_rect(desk->surface, 0, 0, sw, sh, 0);
    GfxSurface *bbmp = gfx_load_bmp_resource(VIEW_BUTTON_BMP, NULL);
    if (bbmp) {
        GfxSurface *s = g_view.win->surface;
        int bx = s->width - bbmp->width / 2 - 6, by = s->height - bbmp->height - 6;
        GfxRect br = {bx, bx + bbmp->width - 1, by, by + bbmp->height - 1};
        g_view.button = ctl_button_create(g_view.win, &br, VIEW_BUTTON_ID, CTL_STYLE_BITMAP, bbmp);
        if (!g_view.button)
            gfx_surface_destroy(bbmp);
    }
    view_draw();
    wm_lower(desk);
    wm_show(desk, true);
    wm_show(g_view.win, true);
    wm_set_focus(g_view.win);
    wm_set_input_live(true); /* no attract mode yet */

    int music = 0;
    if (g_config.sound_enabled) {
        music = snd_play(VIEW_MUSIC_ID, 4);
        printf("music %u: slot %d\n", (unsigned)VIEW_MUSIC_ID, music);
    } else {
        printf("Sound = NONE: no music\n");
    }

    const char *frames_env = getenv("AFTERLIFE_FRAMES");
    long frames = frames_env ? strtol(frames_env, NULL, 10) : -1;
    long pumps = 0;
    uint32_t start = tm_get_time(), tm_start_elapsed = tm_get_elapsed();
    while (!g_view.quit && !sys_quit_requested() && frames != 0) {
        sys_pump();
        pumps++;
        if (frames > 0)
            frames--;
        plat_delay_ms(1); /* port: the original spins; spare the CPU */
    }
    printf("main loop: %ld pumps, %ld key events, %ld clicks, %u ms wall, %u ms by timer ticks", pumps,
           g_view.keys, g_view.clicks, (unsigned)(tm_get_time() - start),
           (unsigned)(tm_get_elapsed() - tm_start_elapsed));
    if (music)
        printf(", music %s", snd_slot_get_param(music, IMUSE_P_PLAYING) ? "playing" : "stopped");
    printf("\n");
    if (music)
        snd_stop(VIEW_MUSIC_ID);
    wm_show(g_view.win, false);
    wm_destroy(g_view.win);
    wm_show(desk, false);
    wm_destroy(desk);
    free(g_view.bmps);
    if (arrow) {
        gfx_cursor_set(NULL);
        gfx_cursor_destroy(arrow);
    }
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
