/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The debug window (ui/debugwin.h, ALIFE.EXE 0x457b01-0x45848f), headless
 * with the user's data: the real game_main with --debug-window, through
 * the synthetic input path.
 *
 *   debugwin_test <data dir> <out dir>
 *
 *   1. --debug-window parsing; the game starts on ASCEN001.ASC with the
 *      window open: its place, size, title, buttons and close box, the
 *      start state, the frame untouched (no preview yet), on screen;
 *   2. the stepping logic of 0x457d98 on its own against the original's
 *      switch: wrapping, the set-11 skip, the index ranges and clamping;
 *   3. the four arrows by left click (1) and right click (10): the state,
 *      the preview box cleared and a building drawn, the screen;
 *   4. dragging the window by its frame;
 *   5. the close box: the window hides, the editor flag goes off.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/build.h"
#include "game/ini.h"
#include "game/enhance.h"
#include "game/shell.h"
#include "game/shell_int.h"
#include "game/startup.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "platform/present.h"
#include "ui/controls.h"
#include "ui/debugwin.h"
#include "ui/wm.h"

static int g_failures;
static const char *g_out;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

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

static void push_key(SDL_Scancode sc, SDL_Keycode key, bool down)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.key = key;
    e.key.scancode = sc;
    e.key.down = down;
    SDL_PushEvent(&e);
}

/* framebuffer coordinates, through the presentation like a real pointer;
 * what: 0 move, 1 down, -1 up */
static void push_mouse(int x, int y, int what, int button)
{
    float wx, wy;
    plat_video_fb_to_window(x, y, &wx, &wy);
    SDL_Event e;
    SDL_zero(e);
    if (what == 0) {
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.x = wx;
        e.motion.y = wy;
    } else {
        e.type = what > 0 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.button = (Uint8)button;
        e.button.down = what > 0;
        e.button.x = wx;
        e.button.y = wy;
    }
    SDL_PushEvent(&e);
}

static void steps(int n)
{
    for (int i = 0; i < n && g_game_running; i++)
        game_main_step();
}

static void click_window(WmWindow *w, int button)
{
    GfxRect r = w->rect;
    wm_parent_to_screen(w, &r);
    int x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
    push_mouse(x, y, 0, button);
    steps(2);
    push_mouse(x, y, 1, button);
    steps(2);
    push_mouse(x, y, -1, button);
    steps(3);
}

static int g_pumps;

/* answers message boxes (the scenario's opening box) with Enter */
static void pump_hook(void)
{
    g_pumps++;
    WmWindow *box = ctl_msgbox_window();
    if (box && wm_is_visible(box) && g_pumps % 20 == 0)
        push_key(SDL_SCANCODE_RETURN, SDLK_RETURN, true), push_key(SDL_SCANCODE_RETURN, SDLK_RETURN, false);
}

/* ---- the original's stepping logic, for comparison -------------------- */

typedef struct {
    int min, max, set, index;
} Model;

/* 0x457d98 as decompiled: the switch, the wrap, the clamp */
static void model_step(Model *m, int dset, int dindex)
{
    int set = m->set;
    if (dset) {
        set = (int8_t)(set + dset);
        if (set > 30)
            set = 1;
        if (set < 1)
            set = 30;
        int min = 0, max = 1;
        switch (set) {
        case 4: max = 9; break;
        case 5: max = 5; break;
        case 7: max = 7; break;
        case 8: max = 5; break;
        case 9: max = 5; break;
        case 10: min = 0x11, max = 0x14; break;
        case 11: set = 12; break;
        case 14: max = 0x6f; break;
        case 15: max = 0x6f; break;
        case 16: max = 10; break;
        case 17: max = 8; break;
        case 18: max = 4; break;
        case 19: max = 3; break;
        case 30: max = 2; break;
        default: break;
        }
        m->min = min, m->max = max;
    }
    int i = (int8_t)(m->index + dindex);
    if (m->max <= i)
        i = m->max;
    m->index = m->min <= i ? i : m->min;
    m->set = set;
}

static bool same(const Model *m, DebugwinState s)
{
    return m->min == s.index_min && m->max == s.index_max && m->set == s.set && m->index == s.index;
}

static void check_logic(void)
{
    DebugwinState saved = debugwin_state();
    WmWindow *win = debugwin_window();
    /* (each step also redraws the preview; check_buttons looks at that) */
    Model m = {0, 0x6f, 14, 1};
    debugwin_set_state((DebugwinState){0, 0x6f, 14, 1});
    static const int seq[][2] = {
        {1, 0},  {-1, 0}, {0, 10}, {0, 10},  {0, 10},  {0, 100}, {1, 0},   {-10, 0}, {0, 3},  {-1, 0},
        {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0},  {0, -10}, {-1, 0},  {-1, 0},  {-1, 0},  {-1, 0}, {-1, 0},
        {-1, 0}, {-1, 0}, {-1, 0}, {10, 0},  {1, 0},   {0, -1},  {0, 10},  {10, 0},  {10, 0}, {10, 0},
        {1, 0},  {-1, 0}, {0, -10}, {0, -10}, {-10, 0}, {-10, 0}, {-10, 0}, {0, 10},  {1, 0},  {1, 0},
    };
    int bad = 0;
    for (size_t k = 0; k < sizeof seq / sizeof seq[0]; k++) {
        model_step(&m, seq[k][0], seq[k][1]);
        debugwin_step(seq[k][0], seq[k][1]);
        if (!same(&m, debugwin_state())) {
            DebugwinState s = debugwin_state();
            fprintf(stderr, "  step %zu (%d, %d): model %d..%d set %d index %d, port %d..%d set %d index %d\n", k,
                    seq[k][0], seq[k][1], m.min, m.max, m.set, m.index, s.index_min, s.index_max, s.set, s.index);
            bad++;
        }
    }
    /* random walks */
    uint32_t r = 12345;
    for (int k = 0; k < 20000; k++) {
        r = r * 1103515245u + 12345u;
        static const int d[] = {1, -1, 10, -10};
        int ds = 0, di = 0;
        if (r >> 31)
            ds = d[(r >> 16) & 3];
        else
            di = d[(r >> 16) & 3];
        model_step(&m, ds, di);
        debugwin_step(ds, di);
        if (!same(&m, debugwin_state()))
            bad++;
    }
    CHECK(bad == 0);
    /* pinned cases */
    debugwin_set_state((DebugwinState){0, 0x14, 10, 0x12});
    debugwin_step(1, 0);
    CHECK(debugwin_state().set == 12); /* 11 is skipped */
    debugwin_step(-1, 0);
    CHECK(debugwin_state().set == 12); /* and can't be passed downwards */
    debugwin_step(-10, 0);
    CHECK(debugwin_state().set == 2);
    debugwin_step(-1, 0);
    debugwin_step(-1, 0);
    CHECK(debugwin_state().set == 30 && debugwin_state().index_max == 2);
    debugwin_step(1, 0);
    CHECK(debugwin_state().set == 1);
    debugwin_set_state((DebugwinState){0, 9, 9, 1});
    debugwin_step(1, 0); /* set 10: index pushed up to its minimum */
    CHECK(debugwin_state().set == 10 && debugwin_state().index == 0x11);
    debugwin_step(0, 10);
    CHECK(debugwin_state().index == 0x14);
    CHECK(win == debugwin_window());
    debugwin_set_state(saved);
    printf("logic: 0x457d98's stepping matches (%d walks)\n", 20000);
}

/* ---- the window ----------------------------------------------------------- */

static bool on_screen(WmWindow *w, int lx, int ly)
{
    int x = lx, y = ly;
    wm_point_to_screen(w, &x, &y);
    const GfxSurface *s = gfx_screen();
    return s->pixels[(size_t)y * (size_t)s->stride + (size_t)x] == w->surface->pixels[(size_t)ly * (size_t)w->surface->stride + (size_t)lx];
}

/* the preview box: all its pixels 0 except what the building covers */
static void preview_stats(int *zero, int *other)
{
    const GfxSurface *s = debugwin_window()->surface;
    *zero = *other = 0;
    for (int y = 0x14; y < 0x14 + 0x8b; y++)
        for (int x = 3; x < 3 + 0x74; x++)
            (s->pixels[(size_t)y * (size_t)s->stride + (size_t)x] ? (*other)++ : (*zero)++);
}

static void check_window(void)
{
    WmWindow *w = debugwin_window();
    CHECK(w != NULL);
    if (!w)
        return;
    CHECK(wm_is_visible(w));
    CHECK(w->parent == g_main_window);
    CHECK(w->rect.left == 0xaa && w->rect.top == 0x28);
    GfxSurface *bg = gfx_load_bmp_resource(0x262, NULL);
    CHECK(bg != NULL);
    if (bg) {
        CHECK(w->rect.right == 0xa9 + bg->width && w->rect.bottom == 0x27 + bg->height);
        /* nothing drawn into the preview box yet: still the frame */
        bool same_frame = true;
        for (int y = 0x14; y < 0x14 + 0x8b && y < bg->height; y++)
            for (int x = 3; x < 3 + 0x74 && x < bg->width; x++)
                if (w->surface->pixels[(size_t)y * (size_t)w->surface->stride + (size_t)x] !=
                    bg->pixels[(size_t)y * (size_t)bg->stride + (size_t)x])
                    same_frame = false;
        CHECK(same_frame);
        printf("window: %dx%d at 0xaa, 0x28\n", bg->width, bg->height);
        gfx_surface_destroy(bg);
    }
    CHECK(w->title && strcmp(w->title, "Debug") == 0);
    static const struct {
        int id, x, y;
    } b[] = {{DEBUGWIN_SET_UP, 0xd2, 0x22},
             {DEBUGWIN_SET_DOWN, 0x7f, 0x22},
             {DEBUGWIN_INDEX_UP, 0xd2, 0x4b},
             {DEBUGWIN_INDEX_DOWN, 0x7f, 0x4b},
             {DEBUGWIN_CLOSE, 0, 0}};
    for (int i = 0; i < 5; i++) {
        WmWindow *c = debugwin_button(b[i].id);
        CHECK(c && c->parent == w && ctl_get_id(c) == (uint32_t)b[i].id);
        if (c)
            CHECK(c->rect.left == b[i].x && c->rect.top == b[i].y);
    }
    CHECK(w->close_button == debugwin_button(DEBUGWIN_CLOSE));
    DebugwinState s = debugwin_state();
    CHECK(s.index_min == 0 && s.index_max == 0x6f && s.set == 14 && s.index == 1);
    CHECK(on_screen(w, 0x40, 0x40) && on_screen(w, 0xe0, 0x30));
    write_png("debugwin_open.png", gfx_screen());
}

static void check_buttons(void)
{
    WmWindow *w = debugwin_window();
    if (!w)
        return;
    struct {
        int id, button, set, index, min, max;
    } k[] = {
        {DEBUGWIN_SET_UP, SDL_BUTTON_LEFT, 15, 1, 0, 0x6f},      /* zones */
        {DEBUGWIN_INDEX_UP, SDL_BUTTON_RIGHT, 15, 11, 0, 0x6f},
        {DEBUGWIN_INDEX_UP, SDL_BUTTON_LEFT, 15, 12, 0, 0x6f},
        {DEBUGWIN_INDEX_DOWN, SDL_BUTTON_LEFT, 15, 11, 0, 0x6f},
        {DEBUGWIN_SET_UP, SDL_BUTTON_LEFT, 16, 10, 0, 10},
        {DEBUGWIN_INDEX_DOWN, SDL_BUTTON_RIGHT, 16, 0, 0, 10},
        {DEBUGWIN_SET_DOWN, SDL_BUTTON_RIGHT, 6, 0, 0, 1},
        {DEBUGWIN_SET_UP, SDL_BUTTON_RIGHT, 16, 0, 0, 10},
        {DEBUGWIN_SET_DOWN, SDL_BUTTON_LEFT, 15, 0, 0, 0x6f},
    };
    int drawn = 0;
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        click_window(debugwin_button(k[i].id), k[i].button);
        DebugwinState s = debugwin_state();
        CHECK(s.set == k[i].set && s.index == k[i].index && s.index_min == k[i].min && s.index_max == k[i].max);
        if (s.set != k[i].set || s.index != k[i].index)
            fprintf(stderr, "  click %zu: set %d index %d\n", i, s.set, s.index);
        int zero, other;
        preview_stats(&zero, &other);
        CHECK(zero > 0); /* the box was cleared */
        if (other > 0)
            drawn++;
        CHECK(on_screen(w, 3 + 0x3a, 0x14 + 0x45));
        char name[64];
        snprintf(name, sizeof name, "debugwin_%02zu_set%d_%d.png", i, s.set, s.index + 1);
        write_png(name, gfx_screen());
    }
    /* sets 15 and 16 have buildings for all these indices */
    CHECK(drawn == (int)(sizeof k / sizeof k[0]) - 1 || drawn == (int)(sizeof k / sizeof k[0]));
    printf("buttons: %zu clicks, %d previews drawn\n", sizeof k / sizeof k[0], drawn);
}

static void check_drag(void)
{
    WmWindow *w = debugwin_window();
    if (!w)
        return;
    int x = w->rect.left + 0x30, y = w->rect.top + 0x10;
    push_mouse(x, y, 0, SDL_BUTTON_LEFT);
    steps(2);
    push_mouse(x, y, 1, SDL_BUTTON_LEFT);
    steps(2);
    push_mouse(x + 40, y + 25, 0, SDL_BUTTON_LEFT);
    steps(2);
    push_mouse(x + 40, y + 25, -1, SDL_BUTTON_LEFT);
    steps(3);
    CHECK(w->rect.left == 0xaa + 40 && w->rect.top == 0x28 + 25);
    CHECK(on_screen(w, 0x40, 0x40));
    printf("drag: now at %d, %d\n", w->rect.left, w->rect.top);
    write_png("debugwin_dragged.png", gfx_screen());
}

static void check_close(void)
{
    WmWindow *w = debugwin_window();
    if (!w)
        return;
    g_build.editor = 1;
    click_window(debugwin_button(DEBUGWIN_CLOSE), SDL_BUTTON_LEFT);
    CHECK(!wm_is_visible(w));
    CHECK(g_build.editor == 0);
    CHECK(debugwin_window() == w); /* hidden, not destroyed */
    printf("close: hidden, editor off\n");
    write_png("debugwin_closed.png", gfx_screen());
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <data dir> <out dir>\n", argv[0]);
        return 2;
    }
    g_out = argv[2];
    setvbuf(stdout, NULL, _IONBF, 0);
    crc_init();
    SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 1);

    char *no[] = {"afterlife", "--debug", "--debug-windows", NULL};
    char *yes[] = {"afterlife", "x.ASC", "--debug-window", NULL};
    CHECK(!debugwin_requested(3, no));
    CHECK(debugwin_requested(3, yes));

    plat_set_data_dir(argv[1]);
    ini_set_path("ALIFE.ini");
    char ini[1100];
    snprintf(ini, sizeof ini, "%s/debugwin_test_port.ini", g_out);
    remove(ini);
    port_settings_set_path(ini);
    port_settings_defaults(&g_port_settings);
    if (!plat_init(NULL, 1, 1)) {
        fprintf(stderr, "init failed\n");
        return 1;
    }
    plat_set_ui_scale(1);
    if (sys_initialize() != 0) {
        fprintf(stderr, "init failed\n");
        return 1;
    }
    snprintf(g_arg_load_file, sizeof g_arg_load_file, "ASCEN001.ASC");
    sys_pump_hook = pump_hook;
    char *args[] = {"debugwin_test", "--debug-window", NULL};
    int r = game_main_init(2, args);
    CHECK(r == 0);
    if (r != 0)
        return 1;
    steps(10);
    sys_pump_hook = NULL;

    check_window();
    check_logic();
    check_buttons();
    check_drag();
    check_close();

    CHECK(g_game_running);
    game_main_shutdown();
    CHECK(debugwin_window() == NULL);
    sys_shutdown();
    plat_shutdown();
    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
