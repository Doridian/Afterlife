/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Behaviour test for the title sequence (src/ui/title.c). Needs the user's
 * game files; none are in the repo. The FLCs and sounds are only in the CD
 * bundle (ALIFE.000), the bitmaps in both.
 *
 *   title_test <data dir> <bundle> <cd bundle> <out prefix> [--window]
 *
 * Creates the main window the way game_main does and runs title_run three
 * times with synthetic input fed from the modal pump (ctl_hooks.pump):
 *   1. skip the intro and the animated title with a key, wait for the
 *      menu, "Start a New Game", "Medium": returns 0, difficulty 2;
 *   2. Esc, "Load a Scenario" with a test file picker: shell_pending_load;
 *   3. Esc, Right x4 + Enter on "Quit Afterlife", "Yes": returns 1.
 * Writes <prefix>_{intro,title,menu,difficulty,quitbox}.ppm. With
 * TITLE_TEST_FULL set, the animated title plays to the end (about a
 * minute) with a screenshot every 8 s (<prefix>_title_NN.ppm).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compbun/cbres.h"
#include "game/resource.h"
#include "game/shell.h"
#include "game/sound.h"
#include "game/world.h"
#include "gfx/font.h"
#include "gfx/mouse.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "platform/platform.h"
#include "ui/controls.h"
#include "ui/title.h"
#include "ui/wm.h"

#define W 640
#define H 480

static int g_failures;
static const char *g_prefix;
static char g_scenario[1024];

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            g_failures++;                                                                                              \
        }                                                                                                              \
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

static void write_ppm(const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s_%s.ppm", g_prefix, name);
    const GfxSurface *scr = gfx_screen();
    uint8_t pal[256 * 4];
    gfx_palette_get(pal, 0, 256);
    FILE *f = fopen(path, "wb");
    if (!f) {
        CHECK(!"can't write screenshot");
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", scr->width, scr->height);
    for (int y = 0; y < scr->height; y++)
        for (int x = 0; x < scr->width; x++) {
            const uint8_t *c = pal + scr->pixels[(size_t)y * scr->stride + x] * 4;
            fputc(c[0], f);
            fputc(c[1], f);
            fputc(c[2], f);
        }
    fclose(f);
    printf("wrote %s\n", path);
}

/* Distinct colours on screen (to tell a picture from an empty screen). */
static int screen_colours(void)
{
    const GfxSurface *s = gfx_screen();
    bool seen[256] = {false};
    int n = 0;
    for (int y = 0; y < s->height; y++)
        for (int x = 0; x < s->width; x++) {
            uint8_t c = s->pixels[(size_t)y * s->stride + x];
            if (!seen[c])
                seen[c] = true, n++;
        }
    return n;
}

/* ---- synthetic input ---- */

typedef struct {
    int type, key, ch;
} Key;
static Key g_keys[64];
static int g_key_r, g_key_w;

static bool key_poll(int *type, int *key, int *ch)
{
    if (g_key_r == g_key_w)
        return false;
    *type = g_keys[g_key_r].type;
    *key = g_keys[g_key_r].key;
    *ch = g_keys[g_key_r].ch;
    g_key_r = (g_key_r + 1) % 64;
    return true;
}

static void key_tap(int vk, int ch)
{
    g_keys[g_key_w] = (Key){WM_KEY_DOWN, vk, ch};
    g_key_w = (g_key_w + 1) % 64;
    g_keys[g_key_w] = (Key){WM_KEY_UP, vk, ch};
    g_key_w = (g_key_w + 1) % 64;
}

static void click_window(const WmWindow *w)
{
    int x = (w->rect.left + w->rect.right) / 2, y = (w->rect.top + w->rect.bottom) / 2;
    if (w->parent)
        wm_point_to_screen(w->parent, &x, &y);
    gfx_mouse_on_move(x, y);
    gfx_mouse_on_button(GFX_MOUSE_LEFT, true);
    gfx_mouse_on_button(GFX_MOUSE_LEFT, false);
}

static WmWindow *button(uint32_t id) { return ctl_find_by_id(g_main_window->children, id); }

/* ---- the script, run from the pump ---- */

enum { RUN_NEW_GAME = 1, RUN_SCENARIO, RUN_QUIT };
static int g_run;
static TitleStage g_stage;
static uint32_t g_stage_since, g_run_start;
static int g_step; /* per-run progress */
static bool g_seen[TITLE_STAGE_FADE + 1];
static int g_title_colours;
static bool g_full;
static unsigned g_full_shots;

static void test_pump(void)
{
    uint32_t now = plat_ticks_ms();
    if (now - g_run_start > 120000) {
        fprintf(stderr, "FAIL: run %d stuck in stage %d (step %d)\n", g_run, (int)title_stage(), g_step);
        exit(1);
    }
    TitleStage s = title_stage();
    if (s != g_stage) {
        g_stage = s;
        g_stage_since = now;
        g_seen[s] = true;
    }
    uint32_t in_stage = now - g_stage_since;
    WmWindow *box = ctl_msgbox_window();
    bool box_up = box && wm_is_visible(box);

    switch (g_run) {
    case RUN_NEW_GAME:
        if (s == TITLE_STAGE_INTRO && g_step == 0 && in_stage > 5000) {
            write_ppm("intro");
            key_tap(PLAT_VK_SPACE, ' ');
            g_step = 1;
        } else if (s == TITLE_STAGE_ANIM && g_full) {
            /* TITLE_TEST_FULL: let the whole animation play, a shot every 8 s */
            if (in_stage / 8000 > g_full_shots) {
                char name[32];
                snprintf(name, sizeof name, "title_%02d", ++g_full_shots);
                write_ppm(name);
                if (g_full_shots == 1)
                    g_title_colours = screen_colours();
            }
            g_step = 2;
        } else if (s == TITLE_STAGE_ANIM && g_step <= 1 && in_stage > 2500) {
            g_title_colours = screen_colours();
            write_ppm("title");
            key_tap(PLAT_VK_SPACE, ' ');
            g_step = 2;
        } else if (s == TITLE_STAGE_MENU_BG && g_step <= 2 && in_stage > 300) {
            key_tap(PLAT_VK_SPACE, ' '); /* don't wait the 5 s */
            g_step = 3;
        } else if (s == TITLE_STAGE_MENU && g_step <= 3 && in_stage > 100) {
            write_ppm("menu");
            CHECK(button(TITLE_BTN_NEW) && wm_is_visible(button(TITLE_BTN_NEW)));
            if (button(TITLE_BTN_NEW))
                click_window(button(TITLE_BTN_NEW));
            g_step = 4;
        } else if (s == TITLE_STAGE_DIFFICULTY && g_step == 4 && in_stage > 100) {
            write_ppm("difficulty");
            CHECK(button(TITLE_BTN_MEDIUM) && wm_is_visible(button(TITLE_BTN_MEDIUM)));
            CHECK(!wm_is_visible(button(TITLE_BTN_NEW)));
            if (button(TITLE_BTN_MEDIUM))
                click_window(button(TITLE_BTN_MEDIUM));
            g_step = 5;
        }
        break;
    case RUN_SCENARIO:
        if (s == TITLE_STAGE_INTRO && g_step == 0) {
            key_tap(PLAT_VK_ESCAPE, 0x1b);
            g_step = 1;
        } else if (s == TITLE_STAGE_MENU && g_step == 1 && in_stage > 100) {
            if (button(TITLE_BTN_SCENARIO))
                click_window(button(TITLE_BTN_SCENARIO));
            g_step = 2;
        }
        break;
    case RUN_QUIT:
        if (s == TITLE_STAGE_INTRO && g_step == 0) {
            key_tap(PLAT_VK_ESCAPE, 0x1b);
            g_step = 1;
        } else if (s == TITLE_STAGE_MENU && g_step == 1 && in_stage > 100) {
            for (int i = 0; i < 4; i++)
                key_tap(PLAT_VK_RIGHT, 0);
            key_tap(PLAT_VK_RETURN, '\r');
            g_step = 2;
        } else if (g_step == 2 && box_up) {
            write_ppm("quitbox");
            CHECK(ctl_button_has_focus(button(TITLE_BTN_QUIT)));
            click_window(ctl_msgbox_button(0)); /* "Yes" */
            g_step = 3;
        }
        break;
    }
    wm_idle();
    ctl_idle();
    plat_delay_ms(1);
}

static bool pick_file(int mode, char *out, size_t out_len)
{
    CHECK(mode == 2);
    snprintf(out, out_len, "%s", g_scenario);
    return true;
}

static int run(int which)
{
    g_run = which;
    g_step = 0;
    g_stage = TITLE_STAGE_NONE;
    g_run_start = plat_ticks_ms();
    memset(g_seen, 0, sizeof(g_seen));
    int r = title_run();
    printf("run %d: title_run = %d, step %d, %u ms\n", which, r, g_step, (unsigned)(plat_ticks_ms() - g_run_start));
    CHECK(title_stage() == TITLE_STAGE_NONE);
    CHECK(wm_get_proc(g_main_window) == title_wnd_proc);
    CHECK(g_game_in_progress == 1);
    for (int id = TITLE_BTN_NEW; id <= TITLE_BTN_QUIT; id++)
        CHECK(button((uint32_t)id) == NULL);
    return r;
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: %s <data dir> <bundle> <cd bundle> <out prefix> [--window]\n", argv[0]);
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
    g_prefix = argv[4];
    g_full = getenv("TITLE_TEST_FULL") != NULL;
    snprintf(g_scenario, sizeof g_scenario, "%s/ASCEN001.ASC", argv[1]);
    plat_set_data_dir(argv[1]);
    if (!rc_initialize(argv[2], argv[3]))
        return 1;
    rc_hook_compbun(); /* the sound module streams through compbun */
    compbun_init(1, 0);
    if (!plat_init("title_test", W, H) || !gfx_init(W, H, false))
        return 1;
    gfx_mouse_init();
    gfx_font_init();
    gfx_font_load(500);
    gfx_font_load(501);
    long len = 0;
    uint8_t *raw = read_resource(0x21, &len);
    uint8_t *pal = raw ? gfx_palette_load_resource(raw, (size_t)len) : NULL;
    if (pal)
        gfx_palette_set(pal, GFX_PAL_FIRST, GFX_PAL_COUNT);
    free(pal);
    free(raw);
    gfx_palette_build_highlight();

    wm_hooks.key_poll = key_poll;
    CHECK(wm_initialize() == 0);
    ctl_initialize();
    ctl_hooks.pump = test_pump;
    wm_set_input_live(true);
    CHECK(ctl_msgbox_init());

    /* game_main: the main window, menu bar and scrollbars hidden */
    GfxRect full = {0, W - 1, 0, H - 1};
    g_main_window = wm_create_toplevel(&full, title_wnd_proc, WM_F_MENUBAR | WM_F_VSCROLL | WM_F_HSCROLL | WM_F_CLIPMOUSE);
    CHECK(g_main_window != NULL);
    if (!g_main_window)
        return 1;
    if (g_main_window->menubar)
        wm_show(g_main_window->menubar, false);
    wm_show_scrollbars(g_main_window, 3, false);
    shell_pick_file_hook = pick_file;

    /* 1: new game, medium */
    g_state.hdr.difficulty = 0;
    CHECK(run(RUN_NEW_GAME) == 0);
    CHECK(g_state.hdr.difficulty == 2);
    CHECK(g_seen[TITLE_STAGE_INTRO] && g_seen[TITLE_STAGE_ANIM] && g_seen[TITLE_STAGE_MENU] &&
          g_seen[TITLE_STAGE_DIFFICULTY]);
    CHECK(g_step == 5);
    CHECK(g_title_colours > 16);
    CHECK(shell_pending_load()[0] == '\0');

    /* 2: load a scenario */
    g_state.hdr.difficulty = 0;
    CHECK(run(RUN_SCENARIO) == 0);
    CHECK(strcmp(shell_pending_load(), g_scenario) == 0);
    CHECK(!g_seen[TITLE_STAGE_ANIM]); /* Esc skipped it */
    CHECK(g_state.hdr.difficulty == 0);

    /* 3: quit through the keyboard and the message box */
    CHECK(run(RUN_QUIT) == 1);
    CHECK(g_step == 3);

    wm_destroy(g_main_window);
    g_main_window = NULL;
    ctl_msgbox_shutdown();
    wm_shutdown();
    gfx_font_shutdown();
    gfx_shutdown();
    plat_shutdown();
    rc_shutdown();
    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("title_test: ok\n");
    return 0;
}
