/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The "Afterlife Tutorial" text window (0x4ab8e0-0x4ac2dd). See
 * ui/tutorialwin.h: unused by the original, ported for completeness.
 */
#include "ui/tutorialwin.h"

#include <stdlib.h>
#include <string.h>

#include "game/resource.h"
#include "gfx/surface.h"
#include "ui/controls.h"
#include "ui/exe_text.h"
#include "ui/news.h"
#include "ui/views.h"

#define ID_CLOSE 1000
#define ID_PREV 0x3e9
#define ID_NEXT 0x3ea
#define ID_VIEW 2000
#define ID_MINIMIZE 1 /* the window manager's minimize button */

/* texts in ALIFE.EXE */
#define TEXT_TITLE 0x66fce4
#define TEXT_PREV 0x66fcf8
#define TEXT_NEXT 0x66fd00
#define TEXT_HELP_CLOSE 0x66fd08
#define TEXT_HELP_PREV 0x66fd2c
#define TEXT_HELP_NEXT 0x66fd5c
#define TEXT_HELP_MAXIMIZE 0x66fd88
#define TEXT_HELP_MINIMIZE 0x66fdac
#define TEXT_HELP_UNKNOWN 0x66fdd0

static struct {
    WmWindow *win;    /* 0x5fa5f0 */
    WmWindow *view;   /* 0x66fcc8 */
    uint32_t page;    /* 0x66fccc */
    bool dragging;    /* 0x66fcd8 */
    int grab_x, grab_y; /* 0x66fcd0 / 0x66fcd4 */
} tw;

WmWindow *tutorialwin_window(void) { return tw.win; }
uint32_t tutorialwin_page(void) { return tw.page; }

size_t tutorialwin_filter(const uint8_t *in, size_t n, char *out)
{
    char *o = out;
    bool after_newline = false;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = in[i];
        if (c == '\r') {
            /* (the original looks at the byte before the output even when
             * nothing was written yet) */
            if (after_newline || (i != 0 && o > out && o[-1] == ' '))
                continue;
            *o++ = ' ';
        } else if (c < 0x20) {
            continue;
        } else if (c == '\\') {
            if (++i >= n) /* (the original reads past the end) */
                break;
            if (in[i] == 'n') {
                after_newline = true;
                *o++ = '\n';
            } else if (in[i] == '0') {
                break;
            } else {
                after_newline = false;
                *o++ = (char)in[i];
            }
        } else {
            after_newline = false;
            *o++ = (char)c;
        }
    }
    *o = '\0';
    return (size_t)(o - out);
}

/* 0x4abeaf */
bool tutorialwin_show_page(uint32_t id)
{
    if (id < TUTORIALWIN_FIRST_PAGE)
        id = TUTORIALWIN_LAST_PAGE;
    if (id > TUTORIALWIN_LAST_PAGE)
        id = TUTORIALWIN_FIRST_PAGE;
    tw.page = id;
    if (!tw.win || !tw.view)
        return false;
    PlatFile *f = rc_open_resource(id);
    if (!f)
        return false;
    long size = rc_resource_size(id);
    uint8_t *raw = size > 0 ? malloc((size_t)size) : NULL;
    if (!raw || plat_file_read(f, raw, 1, (size_t)size) != (size_t)size) {
        free(raw);
        plat_file_close(f);
        return false;
    }
    plat_file_close(f);
    char *text = calloc((size_t)size + 1, 1);
    if (!text) {
        free(raw);
        return false;
    }
    tutorialwin_filter(raw, (size_t)size, text);
    free(raw);
    /* the viewer owns the text (CTL_VIEW_OWNS_TEXT) */
    wm_send(tw.view, tw.view, CTL_VIEW_SET_TEXT, (intptr_t)text, 0);
    wm_send(tw.view, tw.view, CTL_VIEW_REDRAW, 0, 0);
    wm_redraw(tw.view, NULL);
    wm_show(tw.win, true);
    wm_set_minimized(tw.win, false);
    return true;
}

static WmWindow *bitmap_button(const GfxRect *r, uint32_t id, GfxSurface *bmp)
{
    WmWindow *b = ctl_button_create(tw.win, r, id, CTL_STYLE_BITMAP, bmp);
    if (!b)
        gfx_surface_destroy(bmp);
    return b;
}

/* 0x4ab8e0 */
bool tutorialwin_create(WmWindow *main)
{
    if (tw.win)
        return true;
    if (!main)
        return false;
    GfxSurface *bg = gfx_load_bmp_resource(0x25f, NULL);
    if (!bg)
        return false;
    GfxRect r;
    r.left = main->client.right - bg->width - 0x14;
    r.top = 0x50;
    r.right = bg->width + r.left - 1;
    r.bottom = bg->height + 0x4f;
    tw.win = wm_create_window(main, &r, tutorialwin_proc, WM_F_MINIMIZABLE | WM_F_NOFOCUS, 0);
    if (!tw.win) {
        gfx_surface_destroy(bg);
        return false;
    }
    gfx_blit(tw.win->surface, 0, 0, bg->width, bg->height, bg, 0, 0);
    gfx_surface_destroy(bg);
    static const GfxRect view_rect = {14, 417, 16, 309}; /* 0x66fcb8 */
    tw.view = ctl_view_create(tw.win, &view_rect, CTL_VIEW_OWNS_TEXT | CTL_VIEW_PARTIAL, ID_VIEW, 0x1f5, 0x13, 0x1a,
                              0x13, 0);
    if (!tw.view) {
        tutorialwin_destroy();
        return false;
    }
    wm_set_title(tw.win, exe_str(TEXT_TITLE));
    views_draw_title(tw.win);
    GfxSurface *bmp = gfx_load_bmp_resource(0x259, NULL);
    if (!bmp) {
        tutorialwin_destroy();
        return false;
    }
    r = (GfxRect){0, bmp->width - 1, 0, bmp->height - 1};
    tw.win->close_button = bitmap_button(&r, ID_CLOSE, bmp);
    bmp = gfx_load_bmp_resource(0x578, NULL);
    if (!bmp) {
        tutorialwin_destroy();
        return false;
    }
    r.left = ((0x37 - (bmp->width >> 1)) >> 1) + 0x1a7;
    r.top = 0x5a;
    r.right = bmp->width + r.left - 1;
    r.bottom = bmp->height + 0x59;
    int h = bmp->height;
    WmWindow *b = bitmap_button(&r, ID_PREV, bmp);
    if (b)
        ctl_button_set_label(b, exe_str(TEXT_PREV), 500, 0x1a, true);
    bmp = gfx_load_bmp_resource(0x578, NULL);
    if (!bmp) {
        tutorialwin_destroy();
        return false;
    }
    r.top = (h >> 1) + r.bottom;
    r.bottom = bmp->height + r.top - 1;
    b = bitmap_button(&r, ID_NEXT, bmp);
    if (b)
        ctl_button_set_label(b, exe_str(TEXT_NEXT), 500, 0x1a, true);
    return true;
}

void tutorialwin_destroy(void)
{
    if (tw.win) {
        wm_show(tw.win, false);
        wm_destroy(tw.win);
    }
    memset(&tw, 0, sizeof tw);
}

/* 0x4ac155: right-click help on a button */
static void help(WmWindow *c)
{
    int id = (int)wm_get_data(c, 0);
    int x = ((c->rect.right - c->rect.left) >> 1) + tw.win->rect.left + c->rect.left;
    int y = ((c->rect.bottom - c->rect.top) >> 1) + tw.win->rect.top + c->rect.top;
    uint32_t text = TEXT_HELP_UNKNOWN;
    if (id == ID_CLOSE)
        text = TEXT_HELP_CLOSE;
    else if (id == ID_MINIMIZE)
        text = wm_is_minimized(c->parent) ? TEXT_HELP_MAXIMIZE : TEXT_HELP_MINIMIZE;
    else if (id == ID_PREV)
        text = TEXT_HELP_PREV;
    else if (id == ID_NEXT)
        text = TEXT_HELP_NEXT;
    help_balloon_show(x, y, 0x78, exe_str(text), true, true);
}

/* 0x4abc3d */
intptr_t tutorialwin_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_MOUSEMOVE:
        if (tw.dragging) {
            GfxRect r = w->rect;
            rect_move_to(&r, (int)a - tw.grab_x, (int)b - tw.grab_y);
            wm_local_to_screen(w, &r);
            if (w->parent) /* (the original's parent is at 0, 0) */
                wm_screen_to_local(w->parent, &r);
            wm_set_rect(w, &r);
        }
        break;
    case WM_MSG_LBUTTONDOWN:
        wm_raise(w);
        tw.grab_x = (int)a;
        tw.grab_y = (int)b;
        tw.dragging = true;
        wm_set_capture(w);
        break;
    case WM_MSG_LBUTTONUP:
        wm_release_capture();
        tw.dragging = false;
        break;
    case WM_MSG_CTL_PRESS: {
        uint32_t id = ctl_get_id(w);
        WmWindow *parent = w->parent;
        wm_raise(parent);
        if (id == ID_CLOSE)
            wm_show(parent, false);
        else if (id == ID_MINIMIZE)
            wm_show(tw.view, false);
        else if (id == ID_PREV)
            tutorialwin_show_page(tw.page - 1);
        else if (id == ID_NEXT)
            tutorialwin_show_page(tw.page + 1);
        break;
    }
    case WM_MSG_MINIMIZED:
        if (a == 0)
            wm_show(tw.view, true);
        break;
    case WM_MSG_CTL_HOVER:
        if (wm_get_mode() == 0)
            break;
        /* fall through */
    case WM_MSG_CTL_RDOWN:
        help(w);
        break;
    default:
        break;
    }
    return wm_default_proc(w, msg, a, b);
}
