/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The port window (docs/rendering.md "Port window"), headless with the
 * user's data: the real game_main through the synthetic input path.
 *
 *   display_test <data dir> <out dir>
 *
 *   1. opens the port window at a saved size (1000x700) and checks the
 *      screen follows it;
 *   2. resizes it at the title menu: the picture and its buttons stay
 *      centred;
 *   3. in the game, with the views open and pushed to the edges: resizes
 *      the window to several sizes at UI scales 1-4 (and below the
 *      minimum): the screen is the window's pixels / the scale in effect,
 *      the main window, its menu bar and scrollbars follow, every window
 *      stays inside, the map keeps its centre (PNG per step);
 *   4. F11 and Alt+Enter toggle fullscreen and don't reach the game;
 *   5. the window state round trip through ALIFEPORT.INI;
 *   6. the Port Options window through clicks: the toggle, the slider
 *      (applied live), OK saves; Cancel puts the settings back.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/display.h"
#include "game/enhance.h"
#include "game/input.h"
#include "game/ini.h"
#include "game/shell.h"
#include "game/shell_int.h"
#include "game/startup.h"
#include "game/state.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "platform/present.h"
#include "ui/controls.h"
#include "ui/ctrlpanel.h"
#include "ui/graphview.h"
#include "ui/macro.h"
#include "ui/mapview.h"
#include "ui/menu.h"
#include "ui/portopts.h"
#include "ui/title.h"
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

static bool inside(const GfxRect *r, const GfxRect *area)
{
    return r->left >= area->left && r->top >= area->top && r->right <= area->right && r->bottom <= area->bottom;
}

/* ---- synthetic input --------------------------------------------------- */

static void push_key(SDL_Scancode sc, SDL_Keycode key, SDL_Keymod mod, bool down)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.key = key;
    e.key.scancode = sc;
    e.key.mod = mod;
    e.key.down = down;
    SDL_PushEvent(&e);
}

/* framebuffer coordinates, through the presentation like a real pointer */
static void push_mouse(int x, int y, int what)
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
        e.button.button = SDL_BUTTON_LEFT;
        e.button.down = what > 0;
        e.button.x = wx;
        e.button.y = wy;
    }
    SDL_PushEvent(&e);
}

static void click_at(int x, int y)
{
    push_mouse(x, y, 0);
    push_mouse(x, y, 1);
    push_mouse(x, y, -1);
}

static void screen_center(WmWindow *w, int *x, int *y)
{
    GfxRect r = w->rect;
    wm_parent_to_screen(w, &r);
    *x = (r.left + r.right) / 2;
    *y = (r.top + r.bottom) / 2;
}

static void click_window(WmWindow *w)
{
    int x, y;
    screen_center(w, &x, &y);
    click_at(x, y);
}

static void steps(int n)
{
    for (int i = 0; i < n && g_game_running; i++)
        game_main_step();
}

static void pumps(int n)
{
    for (int i = 0; i < n; i++)
        sys_pump();
}

/* waits (pumping) until the throttled relayout has run */
static void settle(void)
{
    int w, h;
    for (int i = 0; i < 400 && plat_video_target_size(&w, &h); i++) {
        sys_pump();
        SDL_Delay(1);
    }
    pumps(3);
}

/* ---- the title ------------------------------------------------------------ */

static int g_pumps, g_title_state;
static bool g_title_checked;

static WmWindow *find_button(int id) { return g_main_window ? ctl_find_by_id(g_main_window->children, id) : NULL; }

/* the title menu: resize twice; the buttons go along the new edges, as
 * the title lays them out for a screen of that size */
static void check_title(void)
{
    WmWindow *b = find_button(TITLE_BTN_NEW), *sc = find_button(TITLE_BTN_SCENARIO), *q = find_button(TITLE_BTN_QUIT);
    CHECK(b && sc && q);
    if (!b || !sc || !q)
        return;
    static const int sizes[2][2] = {{1300, 900}, {900, 640}};
    for (int i = 0; i < 2; i++) {
        unsigned n0 = display_relayout_count();
        plat_window_resize(sizes[i][0], sizes[i][1]);
        settle();
        CHECK(display_relayout_count() > n0);
        CHECK(gfx_screen_width() == sizes[i][0] && gfx_screen_height() == sizes[i][1]);
        CHECK(g_main_window->rect.right == sizes[i][0] - 1 && g_main_window->rect.bottom == sizes[i][1] - 1);
        CHECK(g_main_window->surface->width == sizes[i][0] && g_main_window->surface->height == sizes[i][1]);
        int right = g_main_window->client.right, bottom = g_main_window->client.bottom;
        CHECK(b->rect.left == 0x16 && b->rect.top == 0x13);
        CHECK(sc->rect.left == right - 0xab && sc->rect.top == 0x13);
        CHECK(q->rect.top == bottom - 0x3f);
        GfxRect screen = {0, sizes[i][0] - 1, 0, sizes[i][1] - 1};
        for (WmWindow *c = g_main_window->children; c; c = c->next)
            if ((c->flags & WM_F_VISIBLE) && c != g_main_window->vscroll && c != g_main_window->hscroll)
                CHECK(c->rect.right <= screen.right && c->rect.bottom <= screen.bottom);
        char name[64];
        snprintf(name, sizeof name, "display_title_%dx%d.png", sizes[i][0], sizes[i][1]);
        write_png(name, gfx_screen());
    }
    printf("title: resized to 1300x900 and 900x640, buttons along the edges\n");
}

static void pump_hook(void)
{
    g_pumps++;
    WmWindow *box = ctl_msgbox_window();
    if (box && wm_is_visible(box)) {
        if (g_pumps % 20 == 0)
            push_key(SDL_SCANCODE_RETURN, SDLK_RETURN, 0, true), push_key(SDL_SCANCODE_RETURN, SDLK_RETURN, 0, false);
        return;
    }
    if (g_game_in_progress)
        return;
    TitleStage st = title_stage();
    if ((st == TITLE_STAGE_INTRO || st == TITLE_STAGE_ANIM) && g_title_state == 0) {
        /* a resize during the animations waits for the menu */
        unsigned n0 = display_relayout_count();
        g_title_state = 1;
        plat_window_resize(1100, 760);
        pumps(20);
        CHECK(display_relayout_count() == n0 && gfx_screen_width() == 1000);
    }
    if ((st == TITLE_STAGE_INTRO || st == TITLE_STAGE_ANIM) && g_pumps % 50 == 0) {
        push_key(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, 0, true);
        push_key(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, 0, false);
    }
    if (g_pumps % 30 != 0)
        return;
    if (st == TITLE_STAGE_MENU && find_button(TITLE_BTN_NEW) && g_title_state < 2) {
        g_title_state = 2;
        sys_pump_hook = NULL; /* check_title pumps itself */
        settle();
        /* the resize asked for during the animation has been applied */
        printf("title menu: screen %dx%d\n", gfx_screen_width(), gfx_screen_height());
        CHECK(g_title_state == 2 && gfx_screen_width() == 1100 && gfx_screen_height() == 760);
        check_title();
        g_title_checked = true;
        sys_pump_hook = pump_hook;
        click_window(find_button(TITLE_BTN_NEW));
    } else if (g_title_state == 2 && st == TITLE_STAGE_DIFFICULTY && find_button(TITLE_BTN_EASY)) {
        click_window(find_button(TITLE_BTN_EASY));
        g_title_state = 3;
    }
}

/* ---- the game ------------------------------------------------------------- */

static int center_cell(int *plane, int *cx, int *cy)
{
    WmWindow *m = g_main_window;
    shell_view_sync_origin();
    shell_view_cell_at((m->client.left + m->client.right) / 2, (m->client.top + m->client.bottom) / 2, plane, cx, cy);
    return *plane;
}

/* everything is where it should be for the current screen */
static void check_layout(const char *what)
{
    int sw = gfx_screen_width(), sh = gfx_screen_height();
    GfxRect screen = {0, sw - 1, 0, sh - 1};
    WmWindow *m = g_main_window;
    CHECK(m->rect.left == 0 && m->rect.top == 0 && m->rect.right == sw - 1 && m->rect.bottom == sh - 1);
    CHECK(m->surface->width == sw && m->surface->height == sh);
    CHECK(wm_root()->rect.right == sw - 1 && wm_root()->rect.bottom == sh - 1);
    if (m->menubar) {
        CHECK(m->menubar->rect.right == sw - 1);
        CHECK(m->menubar->surface && m->menubar->surface->width == sw);
        CHECK(m->client.top == m->menubar->rect.bottom + 1);
    }
    if (m->vscroll) {
        CHECK(m->vscroll->rect.right == sw - 1);
        CHECK(m->client.right == m->vscroll->rect.left - 1);
        CHECK(m->vscroll->surface->height == m->vscroll->rect.bottom - m->vscroll->rect.top + 1);
    }
    if (m->hscroll) {
        CHECK(m->hscroll->rect.bottom == sh - 1);
        CHECK(m->client.bottom == m->hscroll->rect.top - 1);
        CHECK(m->hscroll->surface->width == m->hscroll->rect.right - m->hscroll->rect.left + 1);
    }
    int bad = 0;
    for (WmWindow *c = m->children; c; c = c->next) {
        if (c == m->menubar || c == m->vscroll || c == m->hscroll || !(c->flags & WM_F_VISIBLE))
            continue;
        int cw = c->rect.right - c->rect.left + 1, ch = c->rect.bottom - c->rect.top + 1;
        bool fits = cw <= m->client.right - m->client.left + 1 && ch <= m->client.bottom - m->client.top + 1;
        if (fits ? !inside(&c->rect, &m->client)
                 : (c->rect.left != m->client.left && c->rect.right > m->client.right)) {
            fprintf(stderr, "  %s: window '%s' at %d,%d-%d,%d outside the client %d,%d-%d,%d\n", what,
                    c->title ? c->title : "", c->rect.left, c->rect.top, c->rect.right, c->rect.bottom,
                    m->client.left, m->client.top, m->client.right, m->client.bottom);
            bad++;
        }
    }
    for (WmWindow *c = wm_root()->children; c; c = c->next)
        if ((c->flags & WM_F_VISIBLE) && !inside(&c->rect, &screen) &&
            c->rect.right - c->rect.left < sw && c->rect.bottom - c->rect.top < sh)
            bad++;
    CHECK(bad == 0);
    CHECK(g_ctrlpanel_window && wm_is_visible(g_ctrlpanel_window));
}

static void resize_case(int w, int h, int scale, int *n)
{
    display_apply_settings(true, scale);
    plat_window_resize(w, h);
    settle();
    steps(3);
    int pw, ph, ew, eh, ek;
    plat_window_pixel_size(&pw, &ph);
    present_logical_size(pw, ph, scale ? scale : 1, &ew, &eh, &ek);
    printf("window %4dx%-4d scale %d -> screen %4dx%-4d (UI scale in effect %d)\n", pw, ph, scale, gfx_screen_width(),
           gfx_screen_height(), plat_ui_scale());
    /* the window can't get smaller than 640x480 pixels */
    CHECK(pw == (w > 640 ? w : 640) && ph == (h > 480 ? h : 480));
    CHECK(gfx_screen_width() == ew && gfx_screen_height() == eh);
    CHECK(plat_ui_scale() == ek);
    char what[64];
    snprintf(what, sizeof what, "%dx%d@%d", w, h, scale);
    check_layout(what);
    char name[64];
    snprintf(name, sizeof name, "display_%02d_%dx%d_s%d.png", (*n)++, w, h, scale);
    write_png(name, gfx_screen());
}

static void check_game(void)
{
    /* open the views and push some to the edges */
    shell_menu_command(MENU_MACRO);
    steps(5);
    shell_menu_command(MENU_GRAPHVIEW);
    steps(5);
    shell_menu_command(MENU_MAPVIEW);
    steps(5);
    plat_window_resize(1600, 1000);
    settle();
    steps(3);
    WmWindow *views[3] = {macro_window(), graphview_window(), mapview_window()};
    for (int i = 0; i < 3; i++)
        if (views[i] && wm_is_visible(views[i])) {
            int vw = views[i]->rect.right - views[i]->rect.left, vh = views[i]->rect.bottom - views[i]->rect.top;
            wm_move(views[i], g_main_window->client.right - vw - i * 7, g_main_window->client.bottom - vh - i * 9);
        }
    wm_move(g_ctrlpanel_window, g_main_window->client.right - (g_ctrlpanel_window->rect.right - g_ctrlpanel_window->rect.left),
            g_main_window->client.top + 40);
    /* zoomed in, so the map is larger than the window and can scroll */
    shell_view_zoom_in();
    shell_view_zoom_in();
    shell_center_on_cell(GS_PLANE_HEAVEN, 50, 50);
    steps(3);
    int plane0, cx0, cy0;
    center_cell(&plane0, &cx0, &cy0);
    printf("map centre: plane %d cell %d,%d\n", plane0, cx0, cy0);
    int n = 0;
    /* the map keeps its centre */
    resize_case(1300, 850, 1, &n);
    int plane, cx, cy;
    center_cell(&plane, &cx, &cy);
    printf("map centre after 1600x1000 -> 1300x850: plane %d cell %d,%d\n", plane, cx, cy);
    CHECK(plane == plane0 && abs(cx - cx0) <= 2 && abs(cy - cy0) <= 2);

    static const int cases[][3] = {
        {1600, 1000, 1}, {1000, 700, 1}, {1600, 1000, 2}, {1280, 960, 2}, {1920, 1200, 3}, {2560, 1600, 4},
        {2560, 1600, 0}, {1300, 900, 3}, /* too small for 3: 2 */ {700, 500, 2}, /* 1 */
        {640, 480, 1},   {500, 400, 1},  /* below the minimum: letterboxed */ {1234, 777, 1},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
        resize_case(cases[i][0], cases[i][1], cases[i][2], &n);
    shell_view_zoom_out();
    shell_view_zoom_out();
    steps(3);
    steps(50); /* the game runs on at the new size */
    CHECK(g_game_running);
}

/* ---- fullscreen shortcut -------------------------------------------------- */

static int drain_keys(int *alt_up, int *enter, int *f11)
{
    int n = 0;
    const KbEvent *e;
    while ((e = kb_get_event()) != NULL) {
        n++;
        if (e->vk == PLAT_VK_MENU && e->type == 3)
            (*alt_up)++;
        if (e->vk == PLAT_VK_RETURN || e->ch == '\r')
            (*enter)++;
        if (e->vk == PLAT_VK_F1 + 10)
            (*f11)++;
    }
    return n;
}

static void dispatch_all(void)
{
    for (int i = 0; i < 50; i++)
        sys_dispatch_message();
}

static void check_fullscreen(void)
{
    int alt_up = 0, enter = 0, f11 = 0;
    pumps(5);
    drain_keys(&alt_up, &enter, &f11);
    alt_up = enter = f11 = 0;
    bool fs0 = plat_window_fullscreen();
    push_key(SDL_SCANCODE_F11, SDLK_F11, 0, true);
    push_key(SDL_SCANCODE_F11, SDLK_F11, 0, false);
    dispatch_all();
    CHECK(plat_window_fullscreen() == !fs0);
    settle();
    check_layout("F11");
    write_png("display_fullscreen.png", gfx_screen());
    printf("F11: fullscreen %d, screen %dx%d\n", plat_window_fullscreen(), gfx_screen_width(), gfx_screen_height());
    /* Alt+Enter back: Alt down reaches the game, nothing else does */
    push_key(SDL_SCANCODE_LALT, SDLK_LALT, SDL_KMOD_LALT, true);
    push_key(SDL_SCANCODE_RETURN, SDLK_RETURN, SDL_KMOD_LALT, true);
    push_key(SDL_SCANCODE_RETURN, SDLK_RETURN, SDL_KMOD_LALT, false);
    push_key(SDL_SCANCODE_LALT, SDLK_LALT, 0, false);
    dispatch_all();
    CHECK(plat_window_fullscreen() == fs0);
    drain_keys(&alt_up, &enter, &f11);
    CHECK(alt_up == 0 && enter == 0 && f11 == 0);
    CHECK(wm_open_menu() == NULL && menu_hot_item() == NULL);
    settle();
    check_layout("Alt+Enter");
    printf("Alt+Enter: fullscreen %d, screen %dx%d; no key reached the game\n", plat_window_fullscreen(),
           gfx_screen_width(), gfx_screen_height());
    /* a plain Enter still does */
    push_key(SDL_SCANCODE_RETURN, SDLK_RETURN, 0, true);
    push_key(SDL_SCANCODE_RETURN, SDLK_RETURN, 0, false);
    dispatch_all();
    enter = 0;
    drain_keys(&alt_up, &enter, &f11);
    CHECK(enter > 0);
}

/* ---- the window state in ALIFEPORT.INI ------------------------------------ */

static void check_window_state(void)
{
    plat_window_resize(1234, 777);
    settle();
    /* live (unsaved) settings that differ from the file's (the defaults) */
    PortSettings live = g_port_settings;
    g_port_settings.enhancements = false;
    g_port_settings.ui_scale = 3;
    CHECK(enhance_save_window_state());
    PortSettings s;
    port_settings_defaults(&s);
    s.enhancements = false;
    s.ui_scale = 3;
    CHECK(port_settings_load(port_settings_path(), &s));
    printf("saved window: %dx%d fullscreen %d maximized %d\n", s.window.w, s.window.h, s.window.fullscreen,
           s.window.maximized);
    CHECK(s.window.w == 1234 && s.window.h == 777 && !s.window.fullscreen && !s.window.maximized);
    /* the other keys are the file's, not the live (unsaved) settings */
    CHECK(s.enhancements && s.ui_scale == 0);
    g_port_settings.enhancements = live.enhancements;
    g_port_settings.ui_scale = live.ui_scale;
    plat_window_set_fullscreen(true);
    CHECK(enhance_save_window_state());
    port_settings_defaults(&s);
    CHECK(port_settings_load(port_settings_path(), &s));
    CHECK(s.window.fullscreen && s.window.w == 1234 && s.window.h == 777); /* the windowed size is kept */
    plat_window_set_fullscreen(false);
    settle();
}

/* ---- Port Options --------------------------------------------------------- */

static int g_po_step, g_po_wait;
static bool g_po_cancel;

static void portopts_hook(void)
{
    WmWindow *w = portopts_window();
    if (!w || !wm_is_visible(w) || ++g_po_wait < 15)
        return;
    g_po_wait = 0;
    int x, y;
    switch (g_po_step) {
    case 0: /* the toggle: on */
        if (!g_enhancements)
            click_window(portopts_control(PORTOPTS_ENHANCE));
        g_po_step++;
        break;
    case 1:
        CHECK(g_enhancements);
        /* the slider: 2x (a click on the track) */
        screen_center(portopts_control(PORTOPTS_SCALE), &x, &y);
        x = portopts_slider_x(g_po_cancel ? 1 : 2);
        click_at(x, y);
        g_po_step++;
        break;
    case 2:
        /* applied live once the button is up */
        printf("port options: slider -> scale %d, in effect %d, screen %dx%d\n", g_port_settings.ui_scale,
               plat_ui_scale(), gfx_screen_width(), gfx_screen_height());
        CHECK(g_port_settings.ui_scale == (g_po_cancel ? 1 : 2));
        CHECK(plat_ui_scale() == (g_po_cancel ? 1 : 2));
        write_png(g_po_cancel ? "display_portopts_cancel.png" : "display_portopts.png", gfx_screen());
        {
            GfxRect r = w->rect, screen = {0, gfx_screen_width() - 1, 0, gfx_screen_height() - 1};
            CHECK(inside(&r, &screen));
        }
        click_window(portopts_control(g_po_cancel ? PORTOPTS_CANCEL : PORTOPTS_OK));
        g_po_step++;
        break;
    default:
        break;
    }
}

static void check_portopts(void)
{
    plat_window_resize(1600, 1000);
    display_apply_settings(false, 0);
    settle();
    CHECK(plat_ui_scale() == 1 && gfx_screen_width() == 1600);
    /* OK */
    g_po_step = g_po_wait = 0;
    g_po_cancel = false;
    sys_pump_hook = portopts_hook;
    shell_menu_command(MENU_PORT_OPTIONS);
    sys_pump_hook = NULL;
    CHECK(portopts_window() == NULL);
    CHECK(g_po_step == 3);
    CHECK(g_enhancements && g_port_settings.ui_scale == 2 && plat_ui_scale() == 2);
    CHECK(gfx_screen_width() == 800 && gfx_screen_height() == 500);
    check_layout("port options OK");
    PortSettings s;
    port_settings_defaults(&s);
    CHECK(port_settings_load(port_settings_path(), &s));
    CHECK(s.enhancements && s.ui_scale == 2);
    /* Cancel puts 2x back */
    g_po_step = g_po_wait = 0;
    g_po_cancel = true;
    sys_pump_hook = portopts_hook;
    shell_menu_command(MENU_PORT_OPTIONS);
    sys_pump_hook = NULL;
    CHECK(g_po_step == 3);
    CHECK(g_enhancements && g_port_settings.ui_scale == 2 && plat_ui_scale() == 2);
    CHECK(gfx_screen_width() == 800);
    port_settings_defaults(&s);
    CHECK(port_settings_load(port_settings_path(), &s));
    CHECK(s.enhancements && s.ui_scale == 2);
    printf("port options: OK saved 2x, Cancel kept it\n");
    display_apply_settings(false, 0);
    settle();
    steps(10);
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
    plat_set_data_dir(argv[1]);
    ini_set_path("ALIFE.ini");
    char ini[1100];
    snprintf(ini, sizeof ini, "%s/display_test_port.ini", g_out);
    remove(ini);
    port_settings_set_path(ini);
    port_settings_defaults(&g_port_settings);
    g_port_settings.window.w = 1000;
    g_port_settings.window.h = 700;
    if (!plat_init(NULL, 1, 1)) {
        fprintf(stderr, "init failed\n");
        return 1;
    }
    plat_set_ui_scale(1);
    if (sys_initialize() != 0) {
        fprintf(stderr, "init failed\n");
        return 1;
    }
    /* 1. the saved size */
    printf("port window: screen %dx%d\n", gfx_screen_width(), gfx_screen_height());
    CHECK(plat_window_managed());
    CHECK(gfx_screen_width() == 1000 && gfx_screen_height() == 700);

    /* 2. the title */
    sys_pump_hook = pump_hook;
    int r = game_main_init(argc, argv);
    sys_pump_hook = NULL;
    CHECK(r == 0);
    CHECK(g_title_checked);
    if (r != 0)
        return 1;
    steps(20);
    /* 3.-6. */
    check_game();
    check_fullscreen();
    check_window_state();
    check_portopts();
    CHECK(g_game_running);
    game_main_shutdown();
    sys_shutdown();
    plat_shutdown();
    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
