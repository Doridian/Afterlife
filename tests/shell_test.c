/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * End-to-end test of the game shell (WP7): runs the real game_main
 * headless with the user's data (none is in the repo) and plays it through
 * the synthetic input path (SDL events pushed into the platform layer,
 * through main_wnd_proc, the mouse and keyboard modules and the window
 * manager):
 *
 *   shell_test <data dir> <out dir>
 *
 *   1. boots: INI, sys_initialize, game_main_init with the title; skips the
 *      intro with Escape, clicks "Start a New Game" and "Easy";
 *   2. the new game: the sim runs at full speed for a few years (the year
 *      advances, the money changes);
 *   3. builds: picks a zone tool on the control panel and drags a zone
 *      rectangle on the map; picks the Karma station chooser button, an
 *      item in the tile chooser, and places it;
 *   3d. opens the WP6b windows: the Microview (tool M on the gate, then
 *      its pages), the Soulview (its control panel button, then the tool
 *      on the zone) and the Macromanager (Window menu), and closes them;
 *   3e. the Audio Settings window (F9): the Music and Sound FX toggles,
 *      a slider click with its toggle off and on, the close box;
 *   4. saves through the File menu (F5) and checks the file, runs on,
 *      loads it back (F2) and checks the state round-tripped;
 *   5. loads orig/ASCEN001.ASC (F3) and runs it;
 *   6. quits through the menu (F8, Yes).
 * Screenshots (PNG) go to <out dir>.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/build.h"
#include "game/ini.h"
#include "game/savegame.h"
#include "game/shell.h"
#include "game/shell_int.h"
#include "game/sim.h"
#include "game/sound.h"
#include "game/startup.h"
#include "game/world.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "imuse/imuse.h"
#include "ui/audiodlg.h"
#include "ui/controls.h"
#include "ui/ctrlpanel.h"
#include "ui/macro.h"
#include "ui/microview.h"
#include "ui/soulview.h"
#include "ui/title.h"
#include "ui/wm.h"

static int g_failures;
static const char *g_out;
static char g_save_path[1100], g_scenario_path[1100];

#define CHECK(cond)                                                                                                        do {                                                                                                                       if (!(cond)) {                                                                                                             fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                        g_failures++;                                                                                                      }                                                                                                                  } while (0)

/* ---- PNG (stored deflate) ---------------------------------------------- */

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
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
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


static void write_png(const char *name, const GfxSurface *s)
{
    if (!g_out || !s)
        return;
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", g_out, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "can't write %s\n", path);
        g_failures++;
        return;
    }
    uint8_t rgbx[1024], pal[768];
    gfx_palette_get(rgbx, 0, 256);
    for (int i = 0; i < 256; i++)
        pal[i * 3] = rgbx[i * 4], pal[i * 3 + 1] = rgbx[i * 4 + 1], pal[i * 3 + 2] = rgbx[i * 4 + 2];
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    fwrite(sig, 1, 8, f);
    int w = s->width, h = s->height;
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8, ihdr[9] = 3, ihdr[10] = 0, ihdr[11] = 0, ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "PLTE", pal, 768);
    size_t raw_len = (size_t)(w + 1) * (size_t)h;
    size_t blocks = (raw_len + 65534) / 65535;
    uint8_t *raw = malloc(raw_len), *z = malloc(2 + raw_len + blocks * 5 + 4);
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * (size_t)(w + 1)] = 0;
        memcpy(raw + (size_t)y * (size_t)(w + 1) + 1, s->pixels + (size_t)y * (size_t)s->stride, (size_t)w);
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
    fclose(f);
}


/* ---- synthetic input --------------------------------------------------- */

static void push_key(SDL_Keycode key, SDL_Scancode sc)
{
    SDL_Event e;
    SDL_zero(e); /* not "= {0}": that zeroes only the union's first member (GCC 15+) */
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = key;
    e.key.scancode = sc;
    e.key.down = true;
    SDL_PushEvent(&e);
    e.type = SDL_EVENT_KEY_UP;
    e.key.down = false;
    SDL_PushEvent(&e);
}

static void push_move(int x, int y)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = (float)x;
    e.motion.y = (float)y;
    SDL_PushEvent(&e);
}

static void push_button(int x, int y, bool down)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.down = down;
    e.button.x = (float)x;
    e.button.y = (float)y;
    SDL_PushEvent(&e);
}

static void screen_center(WmWindow *w, int *x, int *y)
{
    GfxRect r = w->rect;
    wm_parent_to_screen(w, &r);
    *x = (r.left + r.right) / 2;
    *y = (r.top + r.bottom) / 2;
}

/* What the pump hook does between the test's own steps. */
static int g_answer = 1;        /* message boxes: the button (1-based) to click */
static int g_msgboxes;          /* message boxes answered */
static int g_pumps;
static int g_title_clicks;      /* 0 start, 1 "new game" clicked, 2 "easy" clicked */
static bool g_shot_title, g_shot_menu;

static WmWindow *find_button(int id)
{
    return g_main_window ? ctl_find_by_id(g_main_window->children, id) : NULL;
}

static void click_window(WmWindow *w)
{
    int x, y;
    screen_center(w, &x, &y);
    push_move(x, y);
    push_button(x, y, true);
    push_button(x, y, false);
}

static void pump_hook(void)
{
    g_pumps++;
    WmWindow *box = ctl_msgbox_window();
    if (box && wm_is_visible(box)) {
        if (g_pumps % 20 == 0) {
            WmWindow *b = ctl_msgbox_button(g_answer - 1);
            if (b && wm_is_visible(b)) {
                click_window(b);
                g_msgboxes++;
            } else {
                push_key(SDLK_RETURN, SDL_SCANCODE_RETURN);
            }
        }
        return;
    }
    if (g_game_in_progress)
        return;
    /* the title */
    TitleStage st = title_stage();
    if ((st == TITLE_STAGE_INTRO || st == TITLE_STAGE_ANIM) && g_pumps % 50 == 0) {
        if (st == TITLE_STAGE_ANIM && !g_shot_title) {
            write_png("shell_title.png", gfx_screen());
            g_shot_title = true;
        }
        push_key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    }
    if (g_pumps % 30 != 0)
        return;
    if (g_title_clicks == 0 && st == TITLE_STAGE_MENU && find_button(TITLE_BTN_NEW)) {
        write_png("shell_title_menu.png", gfx_screen());
        g_shot_menu = true;
        click_window(find_button(TITLE_BTN_NEW));
        g_title_clicks = 1;
    } else if (g_title_clicks == 1 && st == TITLE_STAGE_DIFFICULTY && find_button(TITLE_BTN_EASY)) {
        click_window(find_button(TITLE_BTN_EASY));
        g_title_clicks = 2;
    }
}

static bool pick_hook(int mode, char *out, size_t n)
{
    snprintf(out, n, "%s", mode == 2 ? g_scenario_path : g_save_path);
    return true;
}

/* runs the main loop until cond() or  steps */
static int run_until(bool (*cond)(void), int max)
{
    int i;
    for (i = 0; i < max && g_game_running; i++) {
        if (cond && cond())
            break;
        game_main_step();
    }
    return i;
}

static uint32_t g_target_year;
static bool year_reached(void) { return g_state.year >= g_target_year; }
static bool idle(void) { return g_game_command == 0 && g_game_next_command == 0; }

/* the screen position of map cell (x, y) of  in the main view */
static void cell_screen(int plane, int x, int y, int *sx, int *sy)
{
    shell_select_plane(plane);
    shell_view_sync_origin();
    int cx, cy;
    map_view_cell_to_screen(&g_map_view, x, y, &cx, &cy);
    int hh = mapview_half_h(&g_map_view);
    *sx = g_map_view.origin_x + cx;
    *sy = g_map_view.origin_y + cy + hh;
}

static void steps(int n)
{
    for (int i = 0; i < n && g_game_running; i++)
        game_main_step();
}

static void drag(int x0, int y0, int x1, int y1)
{
    push_move(x0, y0);
    steps(3);
    push_button(x0, y0, true);
    steps(3);
    for (int k = 1; k <= 4; k++) {
        push_move(x0 + (x1 - x0) * k / 4, y0 + (y1 - y0) * k / 4);
        steps(3);
    }
    push_button(x1, y1, false);
    steps(5);
}

/* a cell of `set` on `plane`, false if there is none */
static bool find_set(int plane, unsigned set, int *x, int *y)
{
    for (int i = 0; i < GS_MAP_CELLS; i++)
        if (g_state.plane[plane].cells[i] >> 27 == set) {
            *x = i % GS_MAP_W, *y = i / GS_MAP_W;
            return true;
        }
    return false;
}

/* clicks map cell (x, y) of `plane` with the current tool */
static void click_cell(int plane, int x, int y)
{
    shell_center_on_cell(plane, x, y);
    steps(5);
    int sx, sy;
    cell_screen(plane, x, y, &sx, &sy);
    drag(sx, sy, sx, sy);
}

static void click_child(WmWindow *w, int id)
{
    WmWindow *c = w ? ctl_find_by_id(w->children, id) : NULL;
    CHECK(c != NULL);
    if (c) {
        click_window(c);
        steps(10);
    }
}

/* clicks the track of `w`'s scrollbar `id` a quarter of the way along */
static void click_slider(WmWindow *w, int id)
{
    WmWindow *c = w ? ctl_find_by_id(w->children, id) : NULL;
    while (c && wm_get_data(c, CTL_SCROLL_KIND) != CTL_SCROLL_TRACK)
        c = c->parent;
    CHECK(c != NULL);
    if (!c)
        return;
    GfxRect r = c->rect;
    wm_parent_to_screen(c, &r);
    int x = r.left + (r.right - r.left) / 4, y = (r.top + r.bottom) / 2;
    push_move(x, y);
    push_button(x, y, true);
    push_button(x, y, false);
    steps(10);
}

static int slider_pos(WmWindow *w, int id)
{
    WmWindow *c = w ? ctl_find_by_id(w->children, id) : NULL;
    while (c && wm_get_data(c, CTL_SCROLL_KIND) != CTL_SCROLL_TRACK)
        c = c->parent;
    return c ? ctl_scroll_get_pos(c) : -1;
}

static int count_set(int plane, unsigned set)
{
    int n = 0;
    for (int i = 0; i < GS_MAP_CELLS; i++)
        if (g_state.plane[plane].cells[i] >> 27 == set)
            n++;
    return n;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <data dir> <out dir>\n", argv[0]);
        return 2;
    }
    g_out = argv[2];
    crc_init();
    SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 1);
    snprintf(g_save_path, sizeof g_save_path, "%s/shell_test.alf", g_out);
    plat_set_data_dir(argv[1]);
    if (!plat_data_path("ASCEN001.ASC", g_scenario_path, sizeof g_scenario_path)) {
        fprintf(stderr, "no ASCEN001.ASC in %s\n", argv[1]);
        return 2;
    }
    ini_set_path("ALIFE.ini");
    if (!plat_init(NULL, 1, 1) || sys_initialize() != 0) {
        fprintf(stderr, "init failed\n");
        return 1;
    }
    sys_pump_hook = pump_hook;
    shell_pick_file_hook = pick_hook;

    /* 1. boot through the title into a new game */
    int r = game_main_init(argc, argv);
    CHECK(r == 0);
    CHECK(g_shot_menu);
    CHECK(g_title_clicks == 2);
    CHECK(g_state.hdr.difficulty == 1);
    CHECK(g_main_window && g_ctrlpanel_window && wm_is_visible(g_ctrlpanel_window));
    printf("new game: year %u, pennies %lld, difficulty %u\n", (unsigned)g_state.year,
           (long long)gs_pennies(&g_state), g_state.hdr.difficulty);
    steps(20);
    write_png("shell_newgame.png", gfx_screen());

    /* 2. the sim runs: fastest speed (menu) for three years */
    uint32_t year0 = g_state.year;
    int64_t pennies0 = gs_pennies(&g_state);
    shell_menu_command(MENU_SPEED_FAST);
    CHECK(g_game_speed == 1);
    g_target_year = year0 + 3;
    int n = run_until(year_reached, 20000);
    printf("sim: year %u -> %u in %d steps, pennies %lld -> %lld\n", (unsigned)year0, (unsigned)g_state.year, n,
           (long long)pennies0, (long long)gs_pennies(&g_state));
    CHECK(g_state.year >= year0 + 3);
    shell_menu_command(MENU_SPEED_PAUSE);
    CHECK(g_game_speed == 0);

    /* 3a. a zone through the control panel and a drag on the map (Heaven) */
    shell_center_on_cell(GS_PLANE_HEAVEN, 50, 50);
    steps(5);
    WmWindow *zone = shell_ctrl_button(1);
    CHECK(zone != NULL);
    if (zone)
        click_window(zone);
    steps(10);
    CHECK(g_shell_tool == 1);
    int zones0 = count_set(GS_PLANE_HEAVEN, 15);
    int64_t p1 = gs_pennies(&g_state);
    int x0, y0, x1, y1;
    cell_screen(GS_PLANE_HEAVEN, 46, 46, &x0, &y0);
    cell_screen(GS_PLANE_HEAVEN, 50, 50, &x1, &y1);
    drag(x0, y0, x1, y1);
    int zones1 = count_set(GS_PLANE_HEAVEN, 15);
    printf("zoning: %d -> %d zone cells in Heaven, pennies %lld -> %lld\n", zones0, zones1, (long long)p1,
           (long long)gs_pennies(&g_state));
    CHECK(zones1 > zones0);
    CHECK(gs_pennies(&g_state) < p1);
    write_png("shell_zoned.png", gfx_screen());

    /* 3b. roads (the control panel's road button) along the zone */
    if (shell_ctrl_button(10))
        click_window(shell_ctrl_button(10));
    steps(10);
    CHECK(g_shell_tool == 10);
    int roads0 = count_set(GS_PLANE_HEAVEN, 12);
    int rx = 45;
    world_select_plane(GS_PLANE_HEAVEN);
    for (int x = 45; x > 20; x--) { /* a column of free land next to the zone (rivers run across the map) */
        bool free_col = true;
        for (int y = 46; y <= 50; y++)
            free_col = free_col && build_area_free(x, y, 1);
        if (free_col) {
            rx = x;
            break;
        }
    }
    cell_screen(GS_PLANE_HEAVEN, rx, 46, &x0, &y0);
    cell_screen(GS_PLANE_HEAVEN, rx, 50, &x1, &y1);
    drag(x0, y0, x1, y1);
    printf("roads: %d -> %d road cells\n", roads0, count_set(GS_PLANE_HEAVEN, 12));
    CHECK(count_set(GS_PLANE_HEAVEN, 12) > roads0);

    /* 3c. a gate through the tile chooser (button 9) */
    WmWindow *gates = shell_ctrl_button(9);
    if (gates)
        click_window(gates);
    steps(10);
    CHECK(g_shell_tool == 9);
    CHECK(wm_is_visible(g_tilechooser_window));
    write_png("shell_chooser.png", gfx_screen());
    {
        GfxRect cr = g_tilechooser_window->rect;
        wm_parent_to_screen(g_tilechooser_window, &cr);
        int cx = cr.left + 30, cy = cr.top + 40;
        push_move(cx, cy);
        steps(3);
        push_button(cx, cy, true);
        steps(3);
        push_button(cx, cy, false);
        steps(5);
    }
    CHECK(!wm_is_visible(g_tilechooser_window));
    CHECK(tilechooser_selection() >= 0);
    CHECK(g_build.tool_set == 0xd);
    int gates0 = count_set(GS_PLANE_HEAVEN, 13);
    /* click on free land left of the road (a 3 x 3 gate), trying a few spots */
    for (int tries = 0, gx = rx - 3; tries < 6 && count_set(GS_PLANE_HEAVEN, 13) == gates0; tries++, gx--) {
        world_select_plane(GS_PLANE_HEAVEN);
        if (!build_area_free(gx - 1, 49, 3))
            continue;
        cell_screen(GS_PLANE_HEAVEN, gx, 48, &x0, &y0);
        drag(x0, y0, x0, y0);
    }
    printf("gates: %d -> %d gate cells\n", gates0, count_set(GS_PLANE_HEAVEN, 13));
    CHECK(count_set(GS_PLANE_HEAVEN, 13) > gates0);
    steps(20);
    write_png("shell_built.png", gfx_screen());

    /* 3d. the Microview on the gate, through its pages */
    int gx, gy;
    CHECK(find_set(GS_PLANE_HEAVEN, 13, &gx, &gy));
    if (shell_ctrl_button(0x15))
        click_window(shell_ctrl_button(0x15));
    steps(10);
    CHECK(g_shell_tool == 0x15);
    click_cell(GS_PLANE_HEAVEN, gx, gy);
    WmWindow *mv = microview_window();
    CHECK(mv && wm_is_visible(mv));
    MicroviewState ms = microview_state();
    printf("microview: gate at (%d, %d) -> shows (%d, %d) set %u\n", gx, gy, ms.x, ms.y, ms.set);
    CHECK(ms.x >= 0 && ms.set == 13);
    write_png("shell_microview.png", gfx_screen());
    static const int pages[] = {MICROVIEW_DESCRIPTION, MICROVIEW_KNOW, MICROVIEW_CHANGE, MICROVIEW_PICTURE};
    for (size_t i = 0; i < sizeof pages / sizeof pages[0]; i++) {
        click_child(mv, pages[i]);
        CHECK(microview_state().page == pages[i]);
        if (pages[i] == MICROVIEW_KNOW)
            write_png("shell_microview_know.png", gfx_screen());
    }
    click_child(mv, MICROVIEW_CLOSE);
    CHECK(!mv || !wm_is_visible(mv));

    /* the Soulview: its control panel button shows it; the tool on the zone */
    if (shell_ctrl_button(0x16))
        click_window(shell_ctrl_button(0x16));
    steps(10);
    CHECK(g_shell_tool == 0x16);
    WmWindow *sv = soulview_window();
    CHECK(sv && wm_is_visible(sv));
    int zx, zy;
    CHECK(find_set(GS_PLANE_HEAVEN, 15, &zx, &zy));
    click_cell(GS_PLANE_HEAVEN, zx, zy);
    CHECK(sv && wm_is_visible(sv));
    click_child(sv, SOULVIEW_DATA);
    CHECK(!soulview_state().picture);
    write_png("shell_soulview.png", gfx_screen());
    click_child(sv, SOULVIEW_PICTURE);
    CHECK(soulview_state().picture);
    shell_hooks.soulview_show(false);
    CHECK(!wm_is_visible(sv));

    /* the Macromanager through the Window menu, and its close button */
    shell_menu_command(MENU_MACRO);
    steps(10);
    WmWindow *mm = macro_window();
    CHECK(mm && wm_is_visible(mm));
    CHECK(shell_menu_checked(MENU_MACRO));
    write_png("shell_macro.png", gfx_screen());
    shell_menu_command(MENU_MACRO);
    steps(5);
    CHECK(!wm_is_visible(mm));
    CHECK(!shell_menu_checked(MENU_MACRO));

    /* 3e. the Audio Settings window: F9 shows it */
    push_key(SDLK_F9, SDL_SCANCODE_F9);
    steps(10);
    WmWindow *ad = audiodlg_window();
    CHECK(ad && wm_is_visible(ad));
    CHECK(shell_menu_checked(MENU_AUDIO));
    CHECK(slider_pos(ad, AUDIODLG_MASTER_VOL) == 0x7f);
    write_png("shell_audio.png", gfx_screen());
    click_child(ad, AUDIODLG_MUSIC);
    CHECK(!snd_music_enabled);
    click_child(ad, AUDIODLG_SFX);
    CHECK(!snd_sfx_enabled);
    if (snd_running()) {
        CHECK(imuse_set_group_vol(4, -1) == 0);
        CHECK(imuse_set_group_vol(1, -1) == 0);
    }
    /* a slider stays put while its toggle is off, and moves when it's on */
    click_slider(ad, AUDIODLG_MUSIC_VOL);
    CHECK(slider_pos(ad, AUDIODLG_MUSIC_VOL) == 0x7f);
    click_slider(ad, AUDIODLG_MASTER_VOL);
    int master = slider_pos(ad, AUDIODLG_MASTER_VOL);
    CHECK(master >= 0 && master < 0x7f);
    if (snd_running())
        CHECK(imuse_set_group_vol(0, -1) == master);
    write_png("shell_audio_off.png", gfx_screen());
    click_child(ad, AUDIODLG_MUSIC);
    click_child(ad, AUDIODLG_SFX);
    CHECK(snd_music_enabled && snd_sfx_enabled);
    if (snd_running())
        CHECK(imuse_set_group_vol(1, -1) == 0x7f);
    /* the close box hides it and clears the menu's check mark */
    click_child(ad, AUDIODLG_CLOSE);
    CHECK(ad && !wm_is_visible(ad));
    CHECK(!shell_menu_checked(MENU_AUDIO));
    /* the tool back to zoning so nothing below inspects by accident */
    if (zone)
        click_window(zone);
    steps(10);

    /* 4. save (F5), run on, load back (F2) */
    remove(g_save_path);
    uint32_t saved_year = g_state.year;
    int64_t saved_pennies = gs_pennies(&g_state);
    int saved_zones = count_set(GS_PLANE_HEAVEN, 15);
    push_key(SDLK_F5, SDL_SCANCODE_F5);
    steps(10);
    run_until(idle, 200);
    GameState *gs = calloc(1, sizeof *gs);
    char err[256] = "";
    CHECK(savegame_load(gs, g_save_path, err, sizeof err));
    printf("saved: %s year %u (%s)\n", g_save_path, (unsigned)gs->year, err);
    CHECK(gs->year == saved_year);
    CHECK(bignum_value(gs->pennies) == saved_pennies);
    CHECK(memcmp(gs->plane[3].cells, g_state.plane[3].cells, sizeof gs->plane[3].cells) == 0);
    savegame_free(gs);
    free(gs);
    shell_menu_command(MENU_SPEED_FAST);
    g_target_year = g_state.year + 2;
    run_until(year_reached, 20000);
    shell_menu_command(MENU_SPEED_PAUSE);
    CHECK(g_state.year != saved_year);
    g_answer = 2; /* "Save Game Before Loading?" No */
    push_key(SDLK_F2, SDL_SCANCODE_F2);
    steps(10);
    run_until(idle, 400);
    g_answer = 1;
    printf("loaded back: year %u pennies %lld zones %d\n", (unsigned)g_state.year, (long long)gs_pennies(&g_state),
           count_set(GS_PLANE_HEAVEN, 15));
    CHECK(g_state.year == saved_year);
    CHECK(gs_pennies(&g_state) == saved_pennies);
    CHECK(count_set(GS_PLANE_HEAVEN, 15) == saved_zones);

    /* 5. a scenario (F3) */
    g_answer = 2;
    push_key(SDLK_F3, SDL_SCANCODE_F3);
    steps(10);
    run_until(idle, 400);
    g_answer = 1;
    steps(40);
    printf("scenario: \"%s\" year %u pennies %lld\n", g_state.scenario_title, (unsigned)g_state.year,
           (long long)gs_pennies(&g_state));
    CHECK(g_state.scenario_title[0] != 0);
    write_png("shell_scenario.png", gfx_screen());
    year0 = g_state.year;
    pennies0 = gs_pennies(&g_state);
    shell_menu_command(MENU_SPEED_FAST);
    g_target_year = year0 + 2;
    run_until(year_reached, 20000);
    printf("scenario sim: year %u -> %u, pennies %lld -> %lld\n", (unsigned)year0, (unsigned)g_state.year,
           (long long)pennies0, (long long)gs_pennies(&g_state));
    CHECK(g_state.year >= year0 + 2);
    CHECK(gs_pennies(&g_state) != pennies0);
    write_png("shell_scenario_running.png", gfx_screen());

    /* 6. quit (F8, "Save Game Before Quitting?" No) */
    g_answer = 2;
    push_key(SDLK_F8, SDL_SCANCODE_F8);
    run_until(NULL, 400);
    CHECK(!g_game_running);
    printf("message boxes answered: %d, pumps %d\n", g_msgboxes, g_pumps);

    sys_pump_hook = NULL;
    game_main_shutdown();
    sys_shutdown();
    plat_shutdown();
    remove(g_save_path);
    printf("%s (%d failures)\n", g_failures ? "FAILED" : "ok", g_failures);
    return g_failures ? 1 : 0;
}
