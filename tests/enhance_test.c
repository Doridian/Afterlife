/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Port-only settings and UI scale (docs/enhancements.md), no game data:
 *   - g_enhancements is off until enhance_init, the settings default to on;
 *   - ALIFEPORT.INI round trip, defaults, comments, other sections, junk;
 *   - enhance_init: --port-ini, --enhancements / --no-enhancements,
 *     --ui-scale, AFTERLIFE_ENHANCEMENTS, and the scale it asks for;
 *   - the port window's state in the file (size, position, maximized,
 *     fullscreen) and --window-size / --fullscreen / --windowed;
 *   - the presentation layout and the mouse mapping at UI scales 1-3, the
 *     automatic scale and its clamping, the port window's logical size.
 *
 *   enhance_test [scratch dir]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/enhance.h"
#include "platform/present.h"

static int g_failures;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);                                  \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void test_settings(const char *dir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/enhance_test.ini", dir);

    CHECK(!g_enhancements); /* faithful until enhance_init (the tests) */

    PortSettings s;
    port_settings_defaults(&s);
    CHECK(s.enhancements && s.ui_scale == 0); /* the game: enhanced by default */

    /* round trip */
    for (int e = 0; e < 2; e++)
        for (int k = 0; k <= PRESENT_SCALE_MAX; k++) {
            PortSettings a, b;
            port_settings_defaults(&a);
            a.enhancements = e != 0;
            a.ui_scale = k;
            port_settings_defaults(&b);
            CHECK(port_settings_save(path, &a));
            CHECK(port_settings_load(path, &b));
            CHECK(b.enhancements == a.enhancements && b.ui_scale == a.ui_scale);
        }

    /* missing file: false, values kept */
    PortSettings m;
    port_settings_defaults(&m);
    m.enhancements = true;
    m.ui_scale = 3;
    CHECK(!port_settings_load("/nonexistent/dir/ALIFEPORT.INI", &m));
    CHECK(m.enhancements && m.ui_scale == 3);

    /* hand-written: case, spaces, comments, keys outside [Port], range */
    write_file(path, "Enhancements = 1\n"
                     "[Other]\nUI scale = 2\n"
                     "  [ port ]  \r\n"
                     "; comment\n"
                     "  enhancements=  on   ; trailing comment\r\n"
                     "UI SCALE = 9\n"
                     "garbage line\n");
    port_settings_defaults(&s);
    s.enhancements = false;
    CHECK(port_settings_load(path, &s));
    CHECK(s.enhancements);
    CHECK(s.ui_scale == PRESENT_SCALE_MAX); /* clamped */

    write_file(path, "[Port]\nEnhancements = off\nUI scale = -3\n");
    s.enhancements = true;
    CHECK(port_settings_load(path, &s));
    CHECK(!s.enhancements && s.ui_scale == 0);

    /* enhance_init: file, then environment, then the command line */
    PortSettings f;
    port_settings_defaults(&f);
    f.enhancements = true;
    f.ui_scale = 2;
    CHECK(port_settings_save(path, &f));
    char arg_ini[1100];
    snprintf(arg_ini, sizeof arg_ini, "--port-ini=%s", path);
    unsetenv("AFTERLIFE_ENHANCEMENTS");
    {
        char *argv[] = {"afterlife", arg_ini, "orig", NULL};
        enhance_init(3, argv);
        CHECK(strcmp(port_settings_path(), path) == 0);
        CHECK(g_enhancements && g_port_settings.ui_scale == 2);
        CHECK(enhance_ui_scale_request() == 2);
    }
    {
        char *argv[] = {"afterlife", arg_ini, "--no-enhancements", "--ui-scale=3", NULL};
        enhance_init(4, argv);
        CHECK(!g_enhancements && g_port_settings.ui_scale == 3);
        CHECK(enhance_ui_scale_request() == 1); /* no scaling without enhancements */
    }
    setenv("AFTERLIFE_ENHANCEMENTS", "0", 1);
    {
        char *argv[] = {"afterlife", arg_ini, NULL};
        enhance_init(2, argv);
        CHECK(!g_enhancements); /* the environment beats the file */
    }
    {
        char *argv[] = {"afterlife", arg_ini, "--enhancements", NULL};
        enhance_init(3, argv);
        CHECK(g_enhancements); /* the command line beats the environment */
    }
    unsetenv("AFTERLIFE_ENHANCEMENTS");
    /* overrides aren't written back unless asked: the file is unchanged */
    PortSettings back;
    port_settings_defaults(&back);
    CHECK(port_settings_load(path, &back));
    CHECK(back.enhancements && back.ui_scale == 2);
    /* enhance_save writes the current settings */
    g_port_settings.enhancements = false;
    g_port_settings.ui_scale = 1;
    CHECK(enhance_save());
    CHECK(port_settings_load(path, &back));
    CHECK(!back.enhancements && back.ui_scale == 1);

    /* no file at all: defaults, enhancements on, automatic scale */
    remove(path);
    {
        char *argv[] = {"afterlife", arg_ini, NULL};
        enhance_init(2, argv);
        CHECK(g_enhancements && g_port_settings.ui_scale == 0);
        CHECK(enhance_ui_scale_request() == 0);
    }
    /* ... and --no-enhancements opts out */
    {
        char *argv[] = {"afterlife", arg_ini, "--no-enhancements", NULL};
        enhance_init(3, argv);
        CHECK(!g_enhancements && enhance_ui_scale_request() == 1);
    }
    g_enhancements = false;
}

static void test_window_state(const char *dir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/enhance_test_window.ini", dir);
    PortSettings s;
    port_settings_defaults(&s);
    CHECK(s.window.w == 0 && s.window.h == 0 && s.window.x == PLAT_WINDOW_POS_DEFAULT &&
          s.window.y == PLAT_WINDOW_POS_DEFAULT && !s.window.fullscreen && !s.window.maximized);

    /* round trip, with negative positions (a monitor left of the primary) */
    static const PlatWindowState cases[] = {
        {1600, 1000, 120, 80, false, false},
        {1365, 853, -1500, -20, true, false},
        {800, 600, PLAT_WINDOW_POS_DEFAULT, PLAT_WINDOW_POS_DEFAULT, false, true},
        {0, 0, PLAT_WINDOW_POS_DEFAULT, PLAT_WINDOW_POS_DEFAULT, true, true},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        PortSettings a, b;
        port_settings_defaults(&a);
        a.enhancements = true;
        a.ui_scale = 2;
        a.window = cases[i];
        CHECK(port_settings_save(path, &a));
        port_settings_defaults(&b);
        CHECK(port_settings_load(path, &b));
        CHECK(b.enhancements && b.ui_scale == 2);
        CHECK(b.window.w == a.window.w && b.window.h == a.window.h);
        CHECK(b.window.x == a.window.x && b.window.y == a.window.y);
        CHECK(b.window.fullscreen == a.window.fullscreen && b.window.maximized == a.window.maximized);
    }

    /* hand-written and junk values */
    write_file(path, "[Port]\nWindow size = 1024 x 700\nWindow position = 10,20\nFullscreen = yes\n"
                     "Maximized = off\n");
    port_settings_defaults(&s);
    CHECK(port_settings_load(path, &s));
    CHECK(s.window.w == 1024 && s.window.x == 10 && s.window.y == 20 && s.window.fullscreen);
    write_file(path, "[Port]\nWindow size = 1024x700\n");
    port_settings_defaults(&s);
    CHECK(port_settings_load(path, &s));
    CHECK(s.window.w == 1024 && s.window.h == 700);
    write_file(path, "[Port]\nWindow size = -5x700\nWindow position = junk\nFullscreen = maybe\n");
    port_settings_defaults(&s);
    CHECK(port_settings_load(path, &s));
    CHECK(s.window.w == 0 && s.window.h == 0 && s.window.x == PLAT_WINDOW_POS_DEFAULT && !s.window.fullscreen);

    /* the command line */
    PortSettings f;
    port_settings_defaults(&f);
    f.window = (PlatWindowState){1200, 900, 5, 6, true, true};
    CHECK(port_settings_save(path, &f));
    char arg_ini[1100];
    snprintf(arg_ini, sizeof arg_ini, "--port-ini=%s", path);
    {
        char *argv[] = {"afterlife", arg_ini, NULL};
        enhance_init(2, argv);
        CHECK(g_port_settings.window.w == 1200 && g_port_settings.window.fullscreen && g_port_settings.window.maximized);
    }
    {
        char *argv[] = {"afterlife", arg_ini, "--window-size=900x650", "--windowed", NULL};
        enhance_init(4, argv);
        CHECK(g_port_settings.window.w == 900 && g_port_settings.window.h == 650);
        CHECK(!g_port_settings.window.fullscreen && !g_port_settings.window.maximized);
        CHECK(g_port_settings.window.x == 5 && g_port_settings.window.y == 6);
    }
    {
        char *argv[] = {"afterlife", arg_ini, "--window-size=bogus", "--fullscreen", NULL};
        enhance_init(4, argv);
        CHECK(g_port_settings.window.w == 1200 && g_port_settings.window.fullscreen);
    }
    /* without the port window open there is no window state to save */
    CHECK(!enhance_save_window_state());
    remove(path);
    g_enhancements = false;
}

static void test_logical_size(void)
{
    int w, h, k;
    /* scale 1: the window's pixels */
    present_logical_size(2048, 1280, 1, &w, &h, &k);
    CHECK(w == 2048 && h == 1280 && k == 1);
    /* scale 2 */
    present_logical_size(2048, 1280, 2, &w, &h, &k);
    CHECK(w == 1024 && h == 640 && k == 2);
    /* odd sizes round down (the rest is a margin of under k pixels) */
    present_logical_size(2049, 1281, 2, &w, &h, &k);
    CHECK(w == 1024 && h == 640 && k == 2);
    /* too small for scale 3: lowered to 2 */
    present_logical_size(1600, 1000, 3, &w, &h, &k);
    CHECK(k == 2 && w == 800 && h == 500);
    /* too small for 2: 1 */
    present_logical_size(1000, 700, 2, &w, &h, &k);
    CHECK(k == 1 && w == 1000 && h == 700);
    /* smaller than 640x480 at scale 1: the framebuffer stays 640x480
     * (letterboxed) */
    present_logical_size(500, 300, 1, &w, &h, &k);
    CHECK(k == 1 && w == 640 && h == 480);
    present_logical_size(800, 300, 4, &w, &h, &k);
    CHECK(k == 1 && w == 800 && h == 480);
    /* scale 4 on a 4K window */
    present_logical_size(3840, 2160, 4, &w, &h, &k);
    CHECK(k == 4 && w == 960 && h == 540);
    present_logical_size(3840, 2160, 9, NULL, NULL, &k);
    CHECK(k == PRESENT_SCALE_MAX);
    /* the layout of the logical framebuffer fills the window exactly */
    PresentRect r = present_layout(2048, 1280, 1024, 640);
    CHECK(r.x == 0 && r.y == 0 && r.w == 2048 && r.h == 1280);
    r = present_layout(500, 300, 640, 480);
    CHECK(r.w <= 500 && r.h <= 300 && r.w * 480 == r.h * 640);
}

static void test_present(void)
{
    /* 1:1 */
    PresentRect r = present_layout(2560, 1600, 2560, 1600);
    CHECK(r.x == 0 && r.y == 0 && r.w == 2560 && r.h == 1600);
    int fx, fy;
    present_to_fb(r, 2560, 1600, 1234.0f, 567.9f, &fx, &fy);
    CHECK(fx == 1234 && fy == 567);

    /* UI scale 2 at Resolution=MAX on a 2560x1600 panel: 1280x800 */
    r = present_layout(2560, 1600, 1280, 800);
    CHECK(r.x == 0 && r.y == 0 && r.w == 2560 && r.h == 1600);
    present_to_fb(r, 1280, 800, 0.0f, 0.0f, &fx, &fy);
    CHECK(fx == 0 && fy == 0);
    present_to_fb(r, 1280, 800, 1.9f, 1.0f, &fx, &fy);
    CHECK(fx == 0 && fy == 0);
    present_to_fb(r, 1280, 800, 2.0f, 3.0f, &fx, &fy);
    CHECK(fx == 1 && fy == 1);
    present_to_fb(r, 1280, 800, 2559.5f, 1599.5f, &fx, &fy);
    CHECK(fx == 1279 && fy == 799);
    /* round trip through the warp direction */
    for (int x = 0; x < 1280; x += 97)
        for (int y = 0; y < 800; y += 89) {
            float px, py;
            present_from_fb(r, 1280, 800, x, y, &px, &py);
            present_to_fb(r, 1280, 800, px, py, &fx, &fy);
            CHECK(fx == x && fy == y);
        }

    /* UI scale 3: 853x533 in 2560x1600, a 1 px / 1 px margin */
    r = present_layout(2560, 1600, 2560 / 3, 1600 / 3);
    CHECK(r.w == 2559 && r.h == 1599 && r.x == 0 && r.y == 0);
    present_to_fb(r, 853, 533, 3.0f * 100 + 2.5f, 3.0f * 200 + 0.1f, &fx, &fy);
    CHECK(fx == 100 && fy == 200);

    /* a framed 800x600 window at scale 2, output a bit larger: centred */
    r = present_layout(1610, 1210, 800, 600);
    CHECK(r.w == 1600 && r.h == 1200 && r.x == 5 && r.y == 5);
    present_to_fb(r, 800, 600, 4.0f, 4.0f, &fx, &fy);
    CHECK(fx == -1 && fy == -1); /* in the margin: outside the framebuffer */
    present_to_fb(r, 800, 600, 5.0f + 1599.0f, 5.0f + 1199.0f, &fx, &fy);
    CHECK(fx == 799 && fy == 599);

    /* smaller window than the framebuffer: scaled down, letterboxed */
    r = present_layout(640, 300, 1280, 800);
    CHECK(r.w == 480 && r.h == 300 && r.x == 80 && r.y == 0);
    present_to_fb(r, 1280, 800, 80.0f + 240.0f, 150.0f, &fx, &fy);
    CHECK(fx == 640 && fy == 400);

    /* the automatic scale */
    CHECK(present_auto_scale(1.0f) == 1);
    CHECK(present_auto_scale(1.25f) == 1);
    CHECK(present_auto_scale(1.39f) == 1);
    CHECK(present_auto_scale(1.4f) == 2);
    CHECK(present_auto_scale(1.5f) == 2);
    CHECK(present_auto_scale(1.75f) == 2);
    CHECK(present_auto_scale(2.0f) == 2);
    CHECK(present_auto_scale(2.5f) == 3);
    CHECK(present_auto_scale(3.0f) == 3);
    CHECK(present_auto_scale(9.0f) == PRESENT_SCALE_MAX);
    CHECK(present_auto_scale(0.0f) == 1);

    /* clamping: the scaled desktop stays >= 640x480 */
    CHECK(present_clamp_scale(2, 2560, 1600) == 2);
    CHECK(present_clamp_scale(3, 2560, 1600) == 3);
    CHECK(present_clamp_scale(4, 2560, 1600) == 3); /* 640x400 is too short */
    CHECK(present_clamp_scale(2, 1920, 1080) == 2);
    CHECK(present_clamp_scale(3, 1920, 1080) == 2);
    CHECK(present_clamp_scale(2, 1024, 768) == 1);
    CHECK(present_clamp_scale(4, 3840, 2160) == 4);
    CHECK(present_clamp_scale(0, 3840, 2160) == 1);
    CHECK(present_clamp_scale(99, 0, 0) == PRESENT_SCALE_MAX); /* unknown desktop */
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    test_settings(dir);
    test_window_state(dir);
    test_present();
    test_logical_size();
    if (g_failures) {
        fprintf(stderr, "enhance_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("enhance_test: ok\n");
    return 0;
}
