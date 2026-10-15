/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The news pop-up and the help balloon (ALIFE.EXE 0x4761a0-0x478d4f).
 * Overview: news.h.
 *
 * Balloon bitmaps: 0x44c the whole balloon (its size is the largest
 * balloon), 0x44d the right edge, 0x44e the bottom edge. A balloon is the
 * whole bitmap with the text drawn into its top-left part, then the
 * right and bottom edges blitted next to the text and the window cut down
 * to that size.
 */
#include "ui/news.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/resource.h"
#include "game/shell.h"
#include "game/sound.h"
#include "game/startup.h"
#include "game/world.h"
#include "game/timer.h"
#include "gfx/font.h"
#include "gfx/mouse.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "platform/platform.h"
#include "ui/menu.h"

NewsHooks news_hooks;

#define BALLOON_BMP 0x44c
#define BALLOON_RIGHT_BMP 0x44d
#define BALLOON_BOTTOM_BMP 0x44e
#define GAME_PALETTE 0x21
#define VOICE_SLOT 199

/* ---- state ------------------------------------------------------------------ */

static WmWindow *g_win;          /* 0x57e1b0 */
static GfxSurface *g_bmp;        /* 0x664468 (0x44c) */
static GfxSurface *g_bmp_bottom; /* 0x664460 (0x44e) */
static GfxSurface *g_bmp_right;  /* 0x664464 (0x44d) */
static int g_bmp_w, g_bmp_h;     /* 0x4f33ec / 0x4f33e8 */
static int g_edge_h;             /* 0x4f33e4: height of the bottom edge */
static int g_edge_w;             /* 0x4f33e0: width of the right edge */

static bool g_voice_stopped;     /* 0x664440: skip the remaining voice lines */
static bool g_saved_autoscroll;  /* 0x664444: the "Auto Scroll" check before the balloon */
static bool g_close_on_leave;    /* 0x664448: moving away closes (only with wm_get_mode() != 0) */
static bool g_first_balloon;     /* 0x66444c: cleared by the pop-up so its balloon ignores the button */
static WmProc g_main_proc;       /* 0x664458: the main window's own procedure */
static bool g_popup;             /* 0x66445c: a pop-up is up */
static uint8_t g_state_flag;     /* 0x66446c: 0 = the next click anywhere closes, 1 = only inside */
static bool g_cursor_hidden;     /* 0x664470 */
static bool g_had_scrollbars;    /* 0x664474 */
static WmWindow *g_prev_capture; /* 0x664478 */
static unsigned g_ignore_up;     /* 0x66447c: buttons held when the balloon was replaced */
static bool g_popup_balloon;     /* 0x664480: the balloon belongs to a pop-up */
static uint32_t g_deadline;      /* 0x664484: timeout (tm_get_time), 0 = none */
static uint32_t g_popup_music;   /* 0x664488: the structure's theme, stopped on close */
static bool g_main_down;         /* 0x4f33dc: left button went down on the main window */

/* The windows the pop-up hid (the original's table 0x646390 of the
 * game's windows, 0x12 entries {flags, window, ...}; the port walks the
 * main window's children and the other top-level windows instead). */
#define HIDDEN_MAX 32
static struct {
    WmWindow *w;
    bool minimized;
} g_hidden[HIDDEN_MAX];
static int g_n_hidden;

WmWindow *news_window(void) { return g_win; }
bool news_popup_active(void) { return g_popup; }
bool news_active(void) { return g_popup || (g_win && wm_is_visible(g_win)); }

/* ---- helpers ----------------------------------------------------------------- */

static uint8_t *read_resource(uint32_t id, long *len)
{
    long size = rc_resource_size(id);
    PlatFile *f = size > 0 ? rc_open_resource(id) : NULL;
    if (!f)
        return NULL;
    uint8_t *buf = malloc((size_t)size);
    if (buf && plat_file_read(f, buf, 1, (size_t)size) != (size_t)size) {
        free(buf);
        buf = NULL;
    }
    plat_file_close(f);
    *len = size;
    return buf;
}

/* rc_load_resource(id) + pal_load_resource + di_set_palette(.., 10, 0xec)
 * (the original keeps the decoded palette in a global; the port frees it,
 * the shadow palette has it). */
static void set_palette_resource(uint32_t id)
{
    long len = 0;
    uint8_t *raw = read_resource(id, &len);
    uint8_t *pal = raw ? gfx_palette_load_resource(raw, (size_t)len) : NULL;
    free(raw);
    if (pal)
        gfx_palette_set(pal, GFX_PAL_FIRST, GFX_PAL_COUNT);
    free(pal);
}

static void voice_hold(bool hold)
{
    if (hold)
        snd_slot_stop_stream(VOICE_SLOT); /* 0x426427 */
    if (news_hooks.voice_hold)
        news_hooks.voice_hold(hold);
}

/* 0x4754d7 */
static void music(uint32_t id)
{
    if (news_hooks.music)
        news_hooks.music(id);
    else
        snd_play(id, 4);
}

/* One announcer line: stream it unless the pop-up was closed, and pump
 * until it has finished. */
static void say(uint32_t res)
{
    if (!g_voice_stopped)
        snd_slot_stream(res, VOICE_SLOT, 0x7f, SND_BUF_VOICE);
    while (snd_slot_get_param(VOICE_SLOT, 0x100) > 0)
        sys_pump();
}

/* menu_close_popups(bar, 1, bar) (0x4c7fe1): every open pop-up under the
 * bar. */
static void close_popups(WmWindow *m)
{
    for (WmWindow *it = m ? m->children : NULL; it; it = it->next)
        for (WmWindow *p = it->children; p; p = p->next) {
            close_popups(p);
            menu_show(p, false);
        }
}

static bool is_main_part(const WmWindow *w)
{
    const WmWindow *m = g_main_window;
    return w == m->menubar || w == m->vscroll || w == m->hscroll || w == m->top_bar || w == g_win;
}

/* the table loop of news_popup_show: hide the game's visible windows */
static void hide_windows(void)
{
    g_n_hidden = 0;
    WmWindow *lists[2] = {g_main_window->children, g_main_window->parent ? g_main_window->parent->children : NULL};
    for (int l = 0; l < 2; l++)
        for (WmWindow *w = lists[l]; w; w = w->next) {
            if (w == g_main_window || is_main_part(w) || (w->flags & WM_F_NOCLIP) || !wm_is_visible(w) ||
                g_n_hidden >= HIDDEN_MAX)
                continue;
            g_hidden[g_n_hidden].w = w;
            g_hidden[g_n_hidden].minimized = wm_is_minimized(w);
            g_n_hidden++;
        }
    for (int i = 0; i < g_n_hidden; i++)
        wm_show(g_hidden[i].w, false);
}

/* ... and the reverse loop of 0x478431 */
static void show_windows(void)
{
    for (int i = g_n_hidden - 1; i >= 0; i--) {
        WmWindow *w = g_hidden[i].w;
        wm_show(w, true);
        wm_redraw(w, NULL);
        if (g_hidden[i].minimized) {
            wm_send(w, w, WM_MSG_MINIMIZED, 1, 0);
            wm_set_minimized(w, true);
        }
    }
    g_n_hidden = 0;
}

/* The common part of ending a pop-up (0x478431's close and 0x4761a0's
 * left button up): stop the voice and the theme, give the main window
 * its procedure back, reload the game palette and bring back the menu
 * bar and the scrollbars. */
static void popup_restore(void)
{
    g_popup = false;
    g_voice_stopped = true;
    g_popup_balloon = false;
    snd_slot_stop_stream(VOICE_SLOT);
    if (g_popup_music)
        snd_stop(g_popup_music);
    g_popup_music = 0;
    g_deadline = 0;
    if (g_main_proc)
        wm_set_proc(g_main_window, g_main_proc);
    wm_send(g_main_window, g_main_window, SHELL_MSG_REPAINT, 0, 0);
    set_palette_resource(GAME_PALETTE);
    if (g_game_speed == 0) {
        gfx_cycle_disable(8);
        gfx_cycle_disable(7);
    }
    g_game_paused = 0;
    if (g_had_scrollbars)
        wm_show_scrollbars(g_main_window, 3, true);
    menu_show(g_main_window->menubar, true);
    if (g_main_window->top_bar)
        wm_show(g_main_window->top_bar, true);
    show_windows();
    menu_show(g_main_window->menubar, true);
    voice_hold(false); /* 0x426449 */
    wm_send(g_main_window, g_main_window, SHELL_MSG_REPAINT, 0, 0);
}

/* 0x478431's close: the balloon goes away (with the pop-up, if any) */
static void balloon_close(void)
{
    wm_release_capture();
    if (g_prev_capture) {
        wm_set_capture(g_prev_capture);
        g_prev_capture = NULL;
    }
    wm_show(g_win, false);
    wm_redraw(g_main_window, NULL);
    wm_activate(g_main_window);
    g_game_paused = 0;
    shell_menu_set_checked(MENU_AUTOSCROLL, g_saved_autoscroll);
    if (g_cursor_hidden)
        gfx_cursor_show(true);
    g_cursor_hidden = false;
    if (g_popup)
        popup_restore();
}

/* ---- 0x4764da ------------------------------------------------------------------ */

/* 0x4764da */
void news_darken_window(WmWindow *w, unsigned table)
{
    if (!w || !w->surface)
        return;
    const uint8_t *t = gfx_palette_shade_table(table);
    GfxSurface *s = w->surface;
    if (t)
        for (int y = 0; y < s->height; y++) {
            uint8_t *row = s->pixels + (size_t)y * (size_t)s->stride;
            for (int x = 0; x < s->width; x++)
                row[x] = t[row[x]];
        }
    wm_redraw(w, NULL);
}

/* ---- the main window while a pop-up is up --------------------------------------- */

/* 0x4761a0 */
intptr_t news_main_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    if (msg == WM_MSG_LBUTTONDOWN) {
        g_main_down = true;
    } else if (msg == WM_MSG_LBUTTONUP) {
        if (g_popup && g_main_down) {
            g_main_down = false;
            /* The original restores a subset here (no balloon hiding, no
             * windows shown again, the top bar stays hidden) and releases
             * the capture; the port closes it like a click in the
             * balloon. */
            balloon_close();
            g_main_window->flags &= ~WM_F_TOPMOST; /* 0x4079f0 */
            wm_lower(g_main_window);
            wm_release_capture();
        }
    } else if (msg == WM_MSG_KEYUP && a == 0x1b) {
        g_state_flag = 0;
        if (g_win)
            wm_send(g_win, g_win, WM_MSG_LBUTTONUP, (g_win->client.right >> 1) + g_win->rect.left,
                    (g_win->client.bottom >> 1) + g_win->rect.top);
    }
    return wm_default_proc(w, msg, a, b);
}

/* ---- the balloon window ------------------------------------------------------------ */

static bool in_rect(const GfxRect *r, int x, int y)
{
    return x >= r->left && x <= r->right && y >= r->top && y <= r->bottom;
}

/* left button up (and the other paths that end up there) */
static void balloon_release(int x, int y)
{
    if (g_ignore_up) {
        g_ignore_up = 0;
        return;
    }
    /* the original tests the local point plus the window's position
     * against the window's parent-relative rectangle; state 2 is never
     * set */
    bool inside = in_rect(&g_win->rect, g_win->rect.left + x, g_win->rect.top + y);
    if ((inside && g_state_flag != 2) || g_state_flag == 0)
        balloon_close();
}

/* 0x478431 */
intptr_t news_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    int x = (int)a, y = (int)b;
    switch (msg) {
    case WM_MSG_MOUSEMOVE: {
        if (wm_get_mode() == 0 || !g_close_on_leave)
            break;
        GfxRect near = {g_win->rect.left - 0x10, g_win->rect.right + 0x10, g_win->rect.top - 0x10,
                        g_win->rect.bottom + 0x10};
        if (in_rect(&near, g_win->rect.left + x, g_win->rect.top + y))
            break;
        x = g_win->rect.left;
        y = g_win->rect.top;
    }
        /* fall through */
    case WM_MSG_RBUTTONUP:
        if (!g_popup_balloon) {
            if (!g_ignore_up)
                g_state_flag = 0;
            balloon_release(x, y);
        }
        break;
    case WM_MSG_LBUTTONUP:
        balloon_release(x, y);
        break;
    case WM_MSG_KEYUP:
        if (a == 0x1b) {
            g_state_flag = 0;
            wm_send(g_win, g_win, WM_MSG_LBUTTONUP, (g_win->client.right >> 1) + g_win->rect.left,
                    (g_win->client.bottom >> 1) + g_win->rect.top);
        }
        break;
    case WM_MSG_CAPTURE_LOST:
        if ((WmWindow *)a != w) {
            g_ignore_up = 0;
            wm_send(w, w, WM_MSG_RBUTTONUP, (w->client.right >> 1) + w->rect.left,
                    (w->client.bottom >> 1) + w->rect.top);
        }
        break;
    default:
        /* 0x09-0x46 other than the above: swallowed (the default handling
         * below still runs, as in the original) */
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

/* ---- the timeout task ----------------------------------------------------------------- */

/* 0x4782a2 */
static void news_idle(void)
{
    if (g_popup && g_deadline && g_deadline <= tm_get_time()) {
        g_ignore_up = 0;
        wm_send(g_win, g_win, WM_MSG_LBUTTONUP, 1, 1);
    }
}

static int news_task_owner(void) { return 0; }

/* ---- init ------------------------------------------------------------------------------- */

/* 0x478304 */
void news_init(void)
{
    if (g_win)
        return;
    g_bmp = gfx_load_bmp_resource(BALLOON_BMP, NULL);
    g_bmp_bottom = gfx_load_bmp_resource(BALLOON_BOTTOM_BMP, NULL);
    g_bmp_right = gfx_load_bmp_resource(BALLOON_RIGHT_BMP, NULL);
    if (!g_bmp || !g_bmp_bottom || !g_bmp_right) {
        news_shutdown();
        return;
    }
    g_bmp_w = g_bmp->width;
    g_bmp_h = g_bmp->height;
    g_edge_h = g_bmp_bottom->height;
    g_edge_w = g_bmp_right->width;
    GfxRect r = {0, g_bmp_w - 1, 0, g_bmp_h - 1};
    g_win = wm_create_toplevel(&r, news_proc, WM_F_HITCHILDREN | WM_F_TOPMOST);
    if (!g_win) {
        news_shutdown();
        return;
    }
    gfx_blit(g_win->surface, 0, 0, g_bmp_w, g_bmp_h, g_bmp, 0, 0);
    wm_show(g_win, false);
    sys_register_task(news_task_owner, NULL, NULL, NULL, news_idle);
}

void news_shutdown(void)
{
    if (g_win) {
        sys_unregister_task(news_task_owner);
        if (wm_capture() == g_win)
            wm_release_capture();
        wm_destroy(g_win);
    }
    g_win = NULL;
    gfx_surface_destroy(g_bmp);
    gfx_surface_destroy(g_bmp_bottom);
    gfx_surface_destroy(g_bmp_right);
    g_bmp = g_bmp_bottom = g_bmp_right = NULL;
    g_popup = g_popup_balloon = false;
    g_prev_capture = NULL;
    g_n_hidden = 0;
}

/* ---- the help balloon ------------------------------------------------------------------- */

/* 0x478a14 */
void help_balloon_show(int x, int y, int width, const char *text, bool set_font, bool capture)
{
    if (!g_win || !g_main_window || !text)
        return;
    if (g_first_balloon && (gfx_mouse_get_buttons() & 1))
        return;
    g_first_balloon = true;
    if (!wm_is_visible(g_win)) {
        g_prev_capture = wm_capture();
    } else {
        wm_show(g_win, false);
        gfx_cursor_show(true);
        g_ignore_up = gfx_mouse_get_buttons() & 3;
    }
    g_state_flag = 1;
    wm_resize(g_win, g_bmp_w, g_bmp_h);
    gfx_blit(g_win->surface, 0, 0, g_bmp_w, g_bmp_h, g_bmp, 0, 0);
    g_voice_stopped = false;
    g_game_paused = 1;

    int max_w = g_bmp_w - g_edge_w * 2 - 8;
    if (width < max_w)
        max_w = width;
    GfxRect tr = {g_edge_w + 4, g_edge_w + 4 + max_w, g_edge_h + 4, g_bmp_h - 4};
    if (set_font) {
        const unsigned colors[2] = {0x1a, 0x13};
        gfx_font_set_colors(500, 1, 2, colors);
        gfx_font_select(500);
    }
    gfx_font_draw_text(g_win->surface, g_edge_w + 4, g_edge_h + 4, &tr, &tr, text,
                       GFX_TEXT_NOJUSTIFY | GFX_TEXT_SET_BOTTOM);
    int text_bottom = tr.bottom;
    gfx_blit(g_win->surface, tr.right + 4, 0, g_edge_w, g_bmp_h, g_bmp_right, 0, 0);
    gfx_blit(g_win->surface, 0, text_bottom + 4, g_bmp_w, g_edge_h, g_bmp_bottom, 0, 0);
    wm_raise(g_win);

    int w = g_edge_w * 2 + max_w + 7;
    int h = g_edge_h + text_bottom + 3;
    wm_resize(g_win, w, h);
    int left = g_main_window->rect.right - w;
    if (x < left)
        left = x;
    int top = g_main_window->rect.bottom - h;
    if (y < top)
        top = y;
    if (left < 1)
        left = 0;
    if (top < 1)
        top = 0;
    GfxRect r = {left, left + w, top, top + h};

    g_cursor_hidden = capture;
    if (capture)
        gfx_cursor_show(false);
    g_saved_autoscroll = shell_menu_checked(MENU_AUTOSCROLL);
    shell_menu_set_checked(MENU_AUTOSCROLL, false);
    g_close_on_leave = capture;
    wm_flush_input();
    wm_set_rect(g_win, &r);
    wm_show(g_win, true);
    wm_raise(g_win);
    wm_activate(g_win);
    wm_set_capture(g_win);
}

void help_balloon(int x, int y, int width, const char *text) { help_balloon_show(x, y, width, text, true, true); }

/* ---- the news pop-up ---------------------------------------------------------------------- */

/* One announcement: the balloon's title ("\n\nClick here to continue..."
 * appended if `more`), the theme (0x4754d7; with `bit`, only the first
 * time, remembered in 0x664488 to be stopped on close) and the voice
 * lines. `bit`: the special structure's bit, tested in 0x638668 and set
 * in 0x63042c; the theme and the voice play only while it is clear. */
typedef struct NewsItem {
    uint16_t id;
    bool more;
    const char *title;
    uint32_t bit;
    uint32_t theme;
    uint32_t voice[8];
} NewsItem;

static const NewsItem k_news[] = {
    {0x369, false, "Oops...", 0, 0, {0x1016f, 0x10e0d, 0x10e0e, 0x10170}},
    {0x36b, false, "THE FOUR SURFERS OF THE APOCALYPSO -- BIG TROUBLE", 0, 0,
     {0x10e05, 0x10e06, 0x10e07, 0x10e08, 0x10e09, 0x10e0a, 0x1016a, 0x1016b}},
    {0x36d, false, "Twilight of the Demiurges", 0, 0x1006f, {0x1016c, 0x1016d, 0x10e0b, 0x10e0c, 0x1016e}},
    {900, true, "THE VISTA ENHANCEMENT DOOHICKY", 0x1, 0x10062, {0x10171, 0x10172, 0x10173}},
    {0x386, true, "THE AUDIO IMPROVING EMBOPHONE", 0x2, 0x10063, {0x10177, 0x10178, 0x10179}},
    {0x388, true, "THE HEAVEN SCENT ATOMIZER", 0x4, 0x10064, {0x10174, 0x10175, 0x10176}},
    {0x38a, true, "THE CREAMY CANDY CASTLE", 0x8, 0x10065, {0x1017a, 0x1017b, 0x1017c}},
    {0x38c, true, "THE FLUFFY COMFORT DISPENSER", 0x10, 0x10066, {0x1017d, 0x1017e, 0x1017f, 0x10180}},
    {0x38e, true, "THE UGLINESS ENGINE", 0x20, 0x1005d, {0x10e0f, 0x10e10, 0x10e11, 0x10e12}},
    {0x390, true, "THE CRINKLY CACOPHONY CONTRIVANCE", 0x40, 0x1005e, {0x10e13, 0x10e14}},
    {0x392, true, "THE FLABBERGASTING FLATULENCE OL' FACTORY", 0x80, 0x1005f, {0x10e15, 0x10e16, 0x10e17}},
    {0x394, true, "THE WELLSPRING OF UNSAVORINESS", 0x100, 0x10060, {0x10e18, 0x10e19, 0x10e1a, 0x10e1b}},
    {0x396, true, "THE TACTILE DEGRADATION GIZMO", 0x200, 0x10061, {0x10e1c, 0x10e1d, 0x10e1e, 0x10e1f}},
    {0x3b4, true, "OMNIBOLGES!!!", 0x20000, 0x1005c, {0x10e20, 0x10e21, 0x10e22}},
    {0x3b6, true, "LOVE DOMES!!!", 0x10000, 0x1005b, {0x10181, 0x10182, 0x10183}},
};

/* 0x476582 */
void news_popup_show(unsigned id, int arg, uint32_t timeout_ms)
{
    (void)arg;
    WmWindow *m = g_main_window;
    if (!g_win || !m)
        return;
    shell_cancel_drag(); /* 0x403c23 */
    if (wm_get_proc(m) != news_main_proc)
        g_main_proc = wm_get_proc(m);
    wm_flush_input();
    voice_hold(true); /* 0x426427 */
    wm_set_proc(m, news_main_proc);
    g_popup = false;
    g_game_paused = 1;
    g_popup_balloon = true;
    g_first_balloon = false;

    /* repaint the strips outside the client rect (menu bar, scrollbars)
     * with the map, the client rect being the whole window meanwhile */
    GfxRect cl = m->client;
    m->client.left = 0;
    m->client.top = 0;
    m->client.right = m->rect.right - m->rect.left;
    m->client.bottom = m->rect.bottom - m->rect.top;
    if (news_hooks.paint_main) {
        news_hooks.paint_main(m->client.left, m->client.top, m->client.right - m->client.left + 1,
                              cl.top - m->client.top + 1);
        news_hooks.paint_main(cl.right, cl.top, m->client.right - cl.right + 1, m->client.bottom - cl.top + 1);
        news_hooks.paint_main(m->client.left, cl.bottom + 1, cl.right - m->client.left + 1, m->client.bottom - cl.bottom);
    }
    m->client = cl;
    close_popups(m->menubar);
    g_had_scrollbars = m->vscroll && wm_is_visible(m->vscroll);
    wm_show_scrollbars(m, 3, false);
    menu_show(m->menubar, false);
    if (m->top_bar)
        wm_show(m->top_bar, false);
    news_darken_window(m, 1);

    /* the picture in the middle of the screen, in its own palette */
    int sw = gfx_screen_width(), sh = gfx_screen_height();
    GfxSurface *pic = gfx_load_bmp_resource(id, NULL);
    int pw = pic ? pic->width : 0, ph = pic ? pic->height : 0;
    int px = (sw - pw) / 2, py = (sh - ph) / 2;
    set_palette_resource(id + 1);
    if (pic && m->surface)
        gfx_blit(m->surface, px, py, pw, ph, pic, 0, 0);
    gfx_surface_destroy(pic);
    wm_redraw(m, NULL);
    wm_redraw(m->menubar, NULL);
    wm_redraw(m->hscroll, NULL);
    wm_redraw(m->vscroll, NULL);
    g_popup = true;
    hide_windows();
    g_deadline = timeout_ms ? tm_get_time() + timeout_ms : 0;

    int bx = px + 0x14, by = py + 10, bw = pw - 0x28;
    const unsigned colors[2] = {0x21, 0x2e};
    gfx_font_set_colors(501, 1, 2, colors);
    gfx_font_select(501);

    const NewsItem *item = NULL;
    for (size_t i = 0; i < sizeof k_news / sizeof k_news[0]; i++)
        if (k_news[i].id == id)
            item = &k_news[i];
    if (!item) {
        help_balloon_show(bx, py - 0x23, bw, "Click here to continue...", false, false);
    } else {
        char text[500];
        snprintf(text, sizeof text, "%s%s", item->title, item->more ? "\n\nClick here to continue..." : "");
        help_balloon_show(bx, by, bw, text, false, false);
        bool play = true;
        if (item->bit) {
            play = (g_state.unk_638668 & item->bit) == 0;
            if (play) {
                g_popup_music = item->theme;
                music(item->theme);
                g_state.unk_63042c |= item->bit;
            }
        } else if (item->theme) {
            music(item->theme);
        }
        for (int i = 0; play && i < 8 && item->voice[i]; i++)
            say(item->voice[i]);
    }
    wm_flush_input();
    g_state_flag = 1;
}

void news_popup(unsigned id) { news_popup_show(id, 0, 120000); }
