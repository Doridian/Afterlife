/*
 * Menus (0x4c6bf9-0x4c97de). See menu.h and docs/ui.md.
 */
#include "ui/menu.h"

#include <ctype.h>
#include <string.h>

#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/palette.h"
#include "ui/controls.h"

#define MENU_LAIDOUT 0x40000000u /* window flag: the menu has its surface and layout */

/* container messages */
#define MENU_MSG_BEGIN 0x1000         /* a = GfxRect * size (unhandled) */
#define MENU_MSG_FRAME WM_MSG_MENUBAR /* 0x1001 */
#define MENU_MSG_MEASURE 0x1003       /* item: a = GfxRect * {pad x, width, pad y, height} */
#define MENU_MSG_LAID_OUT 0x1004      /* unhandled */
#define MENU_MSG_DRAW 0x1005          /* item: a = GfxRect * new rect, or 0 */

enum { SK_TL, SK_TOP, SK_TR, SK_RIGHT, SK_BR, SK_BOTTOM, SK_BL, SK_LEFT, SK_FILL };
static GfxSurface
    *g_skin[9];         /* 0x64dc24, 0x64dc40, 0x64dc28, 0x64dc3c, 0x64dc20, 0x64dc2c, 0x64dc1c, 0x64dc34, 0x64dc38 */
static uint32_t g_font; /* 0x64dc18 */
static int g_check_w;   /* 0x535420: width of glyph 1 */
static int g_arrow_w;   /* 0x535418: width of glyph 3 */
static uint32_t g_sounds[3];     /* 0x6727b8.. */
static uint8_t g_black, g_white; /* 0x64dc30 / 0x64dc31 */

static WmWindow *g_hot;         /* 0x6727c4 highlighted item */
static WmWindow *g_saved_focus; /* 0x6727d0 focus before the menu took it */
static const int g_sticky = 0;  /* 0x6727b4, read but never set */
#define REDRAW_MAX 34
static WmWindow *g_redraw[REDRAW_MAX]; /* 0x535290 */
static int g_redraw_n;                 /* 0x6727d4 */

static int skin_w(int i) { return g_skin[i] ? g_skin[i]->width : 0; }
static int skin_h(int i) { return g_skin[i] ? g_skin[i]->height : 0; }
static uint32_t style_of(const WmWindow *w) { return (uint32_t)wm_get_data(w, MENU_DATA_STYLE); }

static bool is_item(const WmWindow *w) { return (style_of(w) & MENU_IS_ITEM) != 0; }        /* 0x4c81fb */
static bool is_separator(const WmWindow *w) { return (style_of(w) & MENU_SEPARATOR) != 0; } /* 0x4c8227 */
static bool is_menu_window(const WmWindow *w)
{
    return (style_of(w) & (MENU_IS_MENU | MENU_IS_ITEM)) != 0;
} /* 0x4c827f */
static bool is_bar(const WmWindow *w) { return (style_of(w) & MENU_BAR) != 0; }
static bool visible_flag(const WmWindow *w) { return w && (w->flags & WM_F_VISIBLE); }

/* 0x4c6c64 */
void menu_set_skin(GfxSurface *top_left, GfxSurface *top, GfxSurface *top_right, GfxSurface *right,
                   GfxSurface *bottom_right, GfxSurface *bottom, GfxSurface *bottom_left, GfxSurface *left,
                   GfxSurface *fill)
{
    g_skin[SK_TL] = top_left;
    g_skin[SK_TOP] = top;
    g_skin[SK_TR] = top_right;
    g_skin[SK_RIGHT] = right;
    g_skin[SK_BR] = bottom_right;
    g_skin[SK_BOTTOM] = bottom;
    g_skin[SK_BL] = bottom_left;
    g_skin[SK_LEFT] = left;
    g_skin[SK_FILL] = fill;
}

/* 0x4c6e1f */
void menu_set_font(uint32_t font)
{
    g_font = font;
    uint32_t prev = gfx_font_select(font);
    g_check_w = (uint8_t)gfx_font_char_width(1);
    g_arrow_w = (uint8_t)gfx_font_char_width(3);
    (void)prev; /* the original leaves it selected */
}

/* 0x4c6bf9 */
void menu_set_sounds(uint32_t open, uint32_t close, uint32_t choose)
{
    g_sounds[0] = open;
    g_sounds[1] = close;
    g_sounds[2] = choose;
}

WmWindow *menu_hot_item(void) { return g_hot; }

void menu_reset(void)
{
    g_hot = g_saved_focus = NULL;
    g_redraw_n = 0;
}

/* 0x4c6e66 */
WmWindow *menu_create(WmWindow *parent, const GfxRect *rect, uint32_t style)
{
    if (!parent)
        return NULL;
    if (!g_font)
        g_font = GFX_FONT_BUILTIN;
    GfxRect r;
    if (style & MENU_BAR) {
        r = parent->rect;
        rect_move_to(&r, 0, 0);
        r.bottom = skin_h(SK_BOTTOM) + gfx_font_height(g_font) + skin_h(SK_TOP) + 1;
        if (parent->top_bar)
            r.bottom = parent->top_bar->rect.bottom + (r.bottom - r.top) + 1;
        parent->client.top = r.bottom + 1;
    } else if (rect) {
        r = *rect;
    } else {
        r = (GfxRect){0, 1, 0, 1};
    }
    WmWindow *m = wm_create_window(parent, &r, menu_proc, WM_F_NOSURFACE | WM_F_NOTITLE | WM_F_MINIMIZED, 4);
    if (m)
        wm_set_data(m, MENU_DATA_STYLE, style | MENU_IS_MENU);
    return m;
}

/* 0x4c6fce */
WmWindow *menu_add_item(WmWindow *menu, uint32_t style, uint32_t id, const char *text, intptr_t value, uint32_t font)
{
    if (!menu || (!text && !value))
        return NULL;
    GfxRect r = {0, 1, 0, 1};
    WmWindow *it = wm_create_window(menu, &r, menu_proc, WM_F_NOSURFACE | WM_F_NOCLIP | WM_F_NORAISE, 0x14);
    if (!it)
        return NULL;
    wm_set_data(it, MENU_DATA_FONT, font ? font : GFX_FONT_BUILTIN);
    if (style & MENU_TEXT)
        wm_set_title(it, text ? text : "");
    else
        wm_set_data(it, MENU_DATA_VALUE, value);
    wm_set_data(it, MENU_DATA_STYLE, style | MENU_IS_ITEM);
    wm_set_data(it, MENU_DATA_ID, id);
    return it;
}

void menu_destroy(WmWindow *menu) { wm_destroy(menu); }      /* 0x4c70c8 */
void menu_destroy_item(WmWindow *item) { wm_destroy(item); } /* 0x4c70df */

/* 0x4c7525: the size a menu needs, {0, width, 0, height} (the original's
 * right/bottom are the size, one more than an inclusive rectangle) */
static void menu_measure(WmWindow *m, GfxRect *out)
{
    if (!(style_of(m) & MENU_FIXED)) {
        int w = 0, h = 0;
        for (WmWindow *c = m->children; c; c = c->next) {
            if (!is_item(c))
                continue;
            GfxRect s = {0, 0, 0, 0};
            wm_send(c, c, MENU_MSG_MEASURE, (intptr_t)&s, 0);
            if (!(style_of(c) & MENU_HORIZONTAL)) {
                if (s.left * 2 + s.right > w)
                    w = s.left * 2 + s.right;
                h += s.top * 2 + s.bottom;
            } else {
                w += s.right + s.left * 2;
                if (s.top * 2 + s.bottom > h)
                    h = s.top * 2 + s.bottom;
            }
        }
        out->left = 0;
        out->right = skin_w(SK_LEFT) + skin_w(SK_RIGHT) + w;
        out->top = 0;
        out->bottom = skin_h(SK_BOTTOM) + skin_h(SK_TOP) + h;
    } else {
        *out = m->rect;
    }
}

/* 0x4c7694: an item's metrics {pad x, width, pad y, height} */
static void item_measure(WmWindow *it, GfxRect *out)
{
    if (!it || !out)
        return;
    uint32_t cstyle = style_of(it->parent), style = style_of(it);
    if ((style & 0xff000000u) == MENU_TEXT) {
        uint32_t font = (uint32_t)wm_get_data(it, MENU_DATA_FONT);
        uint32_t prev = gfx_font_select(font);
        out->left = 10;
        out->right = gfx_font_text_width(wm_get_title(it)) + 0x14;
        out->bottom = gfx_font_height(font) + 2;
        if (prev)
            gfx_font_select(prev);
        if (cstyle & MENU_HORIZONTAL) {
            int ch = it->parent->rect.bottom - it->parent->rect.top + 1;
            out->top = (ch - skin_h(SK_TOP) - skin_h(SK_BOTTOM) - out->bottom) >> 1;
        } else {
            out->top = 1;
        }
    } else if ((style & 0xff000000u) == MENU_SEPARATOR) {
        out->left = 10;
        out->top = 1;
        out->bottom = 2;
        out->right = 1;
    }
}

/* 0x4c7f1f: brightens `r` of the menu's surface (pal_highlight_remap) */
static void highlight_rect(WmWindow *m, const GfxRect *r)
{
    if (!m || !r || !m->surface)
        return;
    const uint8_t *remap = gfx_palette_highlight();
    GfxSurface *s = m->surface;
    for (int y = r->top; y <= r->bottom; y++) {
        if (y < 0 || y >= s->height)
            continue;
        for (int x = r->left; x <= r->right; x++)
            if (x >= 0 && x < s->width)
                s->pixels[(size_t)y * s->stride + x] = remap[s->pixels[(size_t)y * s->stride + x]];
    }
}

/* 0x4c7813: draws an item into its menu's surface; with r, moves it there
 * first (during layout). */
static void item_draw(WmWindow *it, const GfxRect *r)
{
    if (!it || !it->parent)
        return;
    WmWindow *m = it->parent;
    uint32_t cstyle = style_of(m);
    if (!g_black)
        g_black = gfx_palette_closest(0, 0, 0);
    if (!g_white)
        g_white = gfx_palette_closest(0xff, 0xff, 0xff);
    GfxRect rr;
    int x0, y0;
    int mw = m->rect.right - m->rect.left; /* width - 1 */
    if (!r) {
        int fx, fw;
        if (!(cstyle & MENU_HORIZONTAL)) {
            fx = skin_w(SK_LEFT);
            fw = mw + 1 - skin_w(SK_LEFT) - skin_w(SK_RIGHT);
        } else {
            fx = it->rect.left;
            fw = it->rect.right - it->rect.left + 1;
        }
        if (m->surface && g_skin[SK_FILL])
            gfx_tile_fill(m->surface, fx, it->rect.top, fw, it->rect.bottom - it->rect.top + 1, g_skin[SK_FILL], 0, 0,
                          g_skin[SK_FILL]->width, g_skin[SK_FILL]->height);
        rr = it->rect;
    } else {
        rr = *r;
        wm_set_rect(it, r);
    }
    x0 = rr.left + 10;
    y0 = rr.top + 1;
    uint32_t style = style_of(it);
    if ((style & 0xff000000u) == MENU_TEXT) {
        char *title = it->title;
        uint32_t font = (uint32_t)wm_get_data(it, MENU_DATA_FONT);
        uint32_t prev = gfx_font_select(font);
        uint8_t saved[15];
        unsigned n = gfx_font_get_colormap(font, 15, saved);
        unsigned colors[3] = {0, g_white, g_black};
        gfx_font_set_colors(font, 0, 3, colors);
        if (m->surface) {
            if ((style & MENU_CHECKED) && (style & MENU_CHECKABLE))
                gfx_font_draw_text(m->surface, x0 - g_check_w, y0 + 1, NULL, NULL, "\x01", 0);
            char *tab = title ? strchr(title, '\t') : NULL;
            if (tab)
                *tab = 0;
            gfx_font_draw_text(m->surface, x0 + 1, y0 + 1, NULL, NULL, title ? title : "", 0);
            if (!(cstyle & MENU_HORIZONTAL)) {
                rr.right = mw - skin_w(SK_RIGHT);
                rr.left = skin_w(SK_LEFT);
                if (tab) {
                    int tw = gfx_font_text_width(tab + 1) + 10;
                    gfx_font_draw_text(m->surface, rr.right - tw, y0 + 1, NULL, NULL, tab + 1, 0);
                }
                if (it->children)
                    gfx_font_draw_text(m->surface, rr.right - g_arrow_w, y0 + 1, NULL, NULL, "\x03", 0);
            }
            if (tab) /* port: also restored for horizontal menus */
                *tab = '\t';
            if (style & MENU_HILITE)
                highlight_rect(m, &rr);
        }
        wm_redraw(m, r);
        gfx_font_set_colormap(font, n, saved);
        if (prev)
            gfx_font_select(prev);
    } else if ((style & 0xff000000u) == MENU_SEPARATOR) {
        int w = (mw - skin_w(SK_LEFT) - skin_w(SK_RIGHT)) - 0x13;
        GfxRect line = {x0 - 10, w + x0 + 9, y0, y0};
        highlight_rect(m, &line);
    }
}

/* 0x4c7c72: redraws the items whose highlight changed */
static void flush_redraws(void)
{
    while (g_redraw_n)
        item_draw(g_redraw[--g_redraw_n], NULL);
}

/* 0x4c7cac */
static void queue_redraw(WmWindow *it)
{
    if (g_redraw_n == REDRAW_MAX) /* port: the original overruns */
        flush_redraws();
    g_redraw[g_redraw_n++] = it;
}

/* 0x4c7ccd */
WmWindow *menu_bar_of(WmWindow *w)
{
    if (!w)
        return NULL;
    if (is_bar(w))
        return w;
    if (is_item(w))
        w = w->parent;
    while (w) {
        if (is_bar(w))
            return w;
        if (!w->parent)
            return NULL;
        w = w->parent->parent;
    }
    return NULL;
}

/* 0x4c978b: the bar item an item hangs under */
static WmWindow *top_item(WmWindow *it)
{
    if (!it)
        return NULL;
    WmWindow *bar = menu_bar_of(it);
    while (it->parent && it->parent != bar)
        it = it->parent;
    return it;
}

/* 0x4c74e1 */
static void popup_close(WmWindow *m)
{
    if (m && m->surface)
        wm_show(m, false);
}

/* 0x4c714b: lays the menu out the first time (surface, frame, items),
 * then raises and shows it */
static void popup_open(WmWindow *m)
{
    if (!m)
        return;
    if (!(m->flags & MENU_LAIDOUT)) {
        GfxRect r;
        menu_measure(m, &r);
        int w = r.right - r.left + 1, h = r.bottom - r.top + 1;
        GfxSurface *s = gfx_surface_create(w, h);
        if (!s)
            return;
        wm_resize(m, w, h);
        if (m->surface)
            gfx_surface_destroy(m->surface);
        m->surface = s;
        uint32_t style = style_of(m);
        if (!(style & MENU_FIXED) && m->parent && m->parent->parent) {
            /* next to the owning item: right of a vertical menu, below a bar */
            WmWindow *owner_item = m->parent, *outer = owner_item->parent;
            int x, y;
            if (!(style_of(outer) & MENU_BAR)) {
                x = (outer->rect.right - outer->rect.left + 1) - owner_item->rect.left;
                y = -skin_h(SK_TOP);
            } else {
                x = -skin_w(SK_LEFT);
                y = (outer->rect.bottom - outer->rect.top + 1) - owner_item->rect.top;
            }
            wm_move(m, x, y);
        }
        wm_send(m, m, MENU_MSG_BEGIN, (intptr_t)&r, 0);
        wm_send(m, m, MENU_MSG_FRAME, 0, 0);
        wm_send(m, m, MENU_MSG_LAID_OUT, (intptr_t)&r, 0);
        int x = skin_w(SK_LEFT), y = skin_h(SK_TOP);
        GfxRect ir;
        wm_get_local_rect(m, &ir);
        ir.left += skin_w(SK_LEFT);
        ir.right -= skin_w(SK_RIGHT);
        for (WmWindow *c = m->children; c; c = c->next) {
            if (!is_item(c))
                continue;
            wm_set_data(c, MENU_DATA_STYLE, style_of(c) & ~MENU_HILITE);
            GfxRect s2 = {0, 0, 0, 0};
            wm_send(c, c, MENU_MSG_MEASURE, (intptr_t)&s2, wm_get_data(c, MENU_DATA_ID));
            if (style & MENU_BAR) {
                ir.left = x + s2.left;
                ir.right = ir.left + s2.right - 1;
            }
            ir.top = y;
            ir.bottom = y + s2.bottom + s2.top * 2 - 1;
            wm_set_rect(c, &ir);
            wm_send(c, c, MENU_MSG_DRAW, (intptr_t)&ir, wm_get_data(c, MENU_DATA_ID));
            wm_show(c, true);
            if (!(style & MENU_HORIZONTAL))
                y += s2.top * 2 + s2.bottom;
            else
                x += s2.left * 2 + s2.right;
        }
        /* other children (the bar's close and minimize buttons) */
        for (WmWindow *c = m->children; c; c = c->next)
            if (!is_item(c) && (c->flags & WM_F_VISIBLE))
                ctl_button_draw(c, CTL_DRAW_BITMAP);
        m->flags |= MENU_LAIDOUT;
    }
    wm_raise(m);
    wm_show(m, true);
}

/* 0x4c70f6 */
void menu_show(WmWindow *m, bool open)
{
    if (!m || is_item(m))
        return;
    if (open)
        popup_open(m);
    else
        popup_close(m);
}

/* 0x4c7fe1: closes the pop-ups under `m` (and `m` itself if close_self),
 * except `keep` */
static void close_popups(WmWindow *m, bool close_self, WmWindow *keep)
{
    if (!m)
        return;
    for (WmWindow *c = m->children; c; c = c->next)
        if (is_item(c) && c->children)
            close_popups(c->children, true, keep);
    if (close_self && keep != m)
        menu_show(m, false);
}

/* 0x4c8183 */
static void mark_item(WmWindow *it, bool on)
{
    if (!it || !is_item(it))
        return;
    uint32_t s = style_of(it);
    s = on ? (s | MENU_HILITE) : (s & ~MENU_HILITE);
    wm_set_data(it, MENU_DATA_STYLE, s);
    queue_redraw(it);
}

/* 0x4c807b: makes `it` the highlighted item (with its owning items up to
 * the bar), or takes the highlight off */
static void set_hot(WmWindow *it, bool on)
{
    if ((on && g_hot == it) || !is_item(it))
        return;
    WmWindow *old = g_hot;
    if (old && old != it) {
        g_hot = NULL;
        set_hot(old, false);
    }
    g_hot = on ? it : NULL;
    mark_item(it, on);
    WmWindow *bar = menu_bar_of(it);
    for (WmWindow *p = it->parent; p && p != bar; p = p->parent)
        if (is_item(p))
            mark_item(p, on);
}

/* 0x4c92f1: the first selectable item from `it` on, wrapping to the
 * start of its menu; `it` if none */
static WmWindow *first_selectable(WmWindow *it)
{
    if (!it || !is_item(it))
        return it;
    WmWindow *p = it;
    while ((p && !is_item(p)) || (p && is_separator(p)))
        p = p->next;
    if (!p && it->parent) {
        p = it->parent->children;
        while ((p && !is_item(p)) || (p && is_separator(p)))
            p = p->next;
    }
    return p ? p : it;
}

/* 0x4c93dc: the next selectable item, wrapping */
static WmWindow *next_selectable(WmWindow *it)
{
    if (!it || !is_item(it))
        return it;
    WmWindow *p = it->next;
    while ((p && !is_item(p)) || (p && is_separator(p)))
        p = p->next;
    if (!p && it->parent) {
        p = it->parent->children;
        while ((p && !is_item(p)) || (p && is_separator(p)))
            p = p->next;
    }
    return p ? p : it;
}

/* 0x4c94cf: the previous selectable item; wrapping only finds the last
 * item if it is selectable (as in the original) */
static WmWindow *prev_selectable(WmWindow *it)
{
    if (!it || !is_item(it))
        return it;
    WmWindow *p = it->prev;
    while ((p && !is_item(p)) || (p && is_separator(p)))
        p = p->prev;
    if (!p) {
        p = it;
        while (p->next)
            p = p->next;
        while ((p && !is_item(p)) || (p && is_separator(p)))
            p = p->next;
    }
    return p ? p : it;
}

/* 0x4c925c: the first item of `it`'s pop-up, or `it` */
static WmWindow *first_in_popup(WmWindow *it)
{
    if (is_item(it) && it->children && it->children->children) {
        WmWindow *f = first_selectable(it->children->children);
        if (f)
            return f;
    }
    return it;
}

/* 0x4c95ca: moves the highlight to `it`, opening its menu if needed and,
 * with open_sub, its pop-up */
static void select_item(WmWindow *it, bool open_sub)
{
    if (!it)
        return;
    uint32_t cstyle = 0;
    if (it->parent) {
        cstyle = style_of(it->parent);
        if (!visible_flag(it->parent)) {
            WmWindow *m = it->parent;
            close_popups(menu_bar_of(it), false, m);
            menu_show(m, true);
            wm_set_open_menu(m);
        }
    }
    if (open_sub && it->children && !visible_flag(it->children) && !(cstyle & MENU_BAR)) {
        close_popups(it->parent, false, it->children);
        menu_show(it->children, true);
        wm_set_open_menu(it->children);
    }
    if (g_hot)
        set_hot(g_hot, false);
    if (g_sticky || wm_open_menu())
        set_hot(it, true);
    flush_redraws();
    if (wm_open_menu()) {
        close_popups(it->parent, false, it->children);
        if (it->children && !visible_flag(it->children) && (cstyle & MENU_BAR)) {
            set_hot(it, true);
            flush_redraws();
            menu_show(it->children, true);
            wm_set_open_menu(it->children);
        }
    }
}

/* 0x4c916a */
void menu_activate(WmWindow *menu)
{
    if (!menu) {
        if (g_hot) {
            set_hot(g_hot, true);
            flush_redraws();
            close_popups(menu_bar_of(g_hot), false, NULL);
            wm_set_open_menu(NULL);
        }
        WmWindow *f = g_saved_focus;
        if (f) {
            g_saved_focus = NULL;
            wm_activate(f);
        }
        return;
    }
    WmWindow *it = menu->children;
    while (it && !is_item(it))
        it = it->next;
    if (it) {
        wm_activate(it);
        set_hot(it, true);
        flush_redraws();
    }
}

/* 0x4c8355 */
bool menu_is_checked(WmWindow *item) { return is_item(item) && (style_of(item) & MENU_CHECKED); }

/* 0x4c8393 */
void menu_set_checked(WmWindow *item, bool checked)
{
    if (!is_item(item))
        return;
    uint32_t s = style_of(item);
    wm_set_data(item, MENU_DATA_STYLE, checked ? (s | MENU_CHECKED) : (s & ~MENU_CHECKED));
    item_draw(item, NULL);
}

/* 0x4c8408 */
WmWindow *menu_find_item_in(WmWindow *list, uint32_t id)
{
    for (; list; list = list->next) {
        if (list->children) {
            WmWindow *f = menu_find_item_in(list->children, id);
            if (f)
                return f;
        }
        if (is_item(list) && (uint32_t)wm_get_data(list, MENU_DATA_ID) == id)
            return list;
    }
    return NULL;
}

/* 0x4c84a2 */
WmWindow *menu_find_item(uint32_t id) { return wm_root() ? menu_find_item_in(wm_root()->children, id) : NULL; }

/* 0x4c97de: the item in a sibling list whose '&' key is `ch` */
static WmWindow *find_key(WmWindow *list, int ch)
{
    for (; list; list = list->next) {
        const char *amp = list->title ? strrchr(list->title, '&') : NULL;
        if (amp && toupper((unsigned char)amp[1]) == toupper((unsigned char)ch))
            return list;
    }
    return NULL;
}

/* closes everything and reports `it` to the bar's owner */
static void choose(WmWindow *it)
{
    WmWindow *bar = menu_bar_of(it);
    WmWindow *owner = bar ? bar->parent : NULL;
    intptr_t id = wm_get_data(it, MENU_DATA_ID);
    menu_activate(NULL);
    wm_send(owner, it, WM_MSG_MENU_COMMAND, id, 0);
}

/* closes all of `bar`'s pop-ups and gives the focus back */
static void close_all(WmWindow *bar)
{
    close_popups(bar, false, NULL);
    if (g_saved_focus)
        wm_activate(g_saved_focus);
    g_saved_focus = NULL;
    g_hot = NULL;
    wm_set_open_menu(NULL);
}

/* the arrow keys (0x4c84c7, key up) */
static void arrow_key(WmWindow *w, int key)
{
    if (!is_item(w) || !g_hot)
        return;
    WmWindow *hot = g_hot;
    bool in_bar = hot->parent && is_bar(hot->parent);
    switch (key) {
    case 0x25: /* left */
        if (!in_bar) {
            WmWindow *owner = hot->parent ? hot->parent->parent : NULL; /* item owning the pop-up */
            if (!owner || !owner->parent || is_bar(owner->parent)) {
                select_item(first_in_popup(prev_selectable(top_item(hot))), false);
            } else {
                menu_show(hot->parent, false);
                select_item(owner, false);
            }
        } else {
            select_item(prev_selectable(hot), true);
        }
        break;
    case 0x26: /* up */
        if (!in_bar)
            select_item(prev_selectable(hot), false);
        break;
    case 0x27: /* right */
        if (!in_bar) {
            if (!hot->children || visible_flag(hot->children)) {
                select_item(first_in_popup(next_selectable(top_item(hot))), false);
            } else {
                menu_show(hot->children, true);
                /* the original passes no second argument here */
                select_item(first_selectable(hot->children->children), false);
            }
        } else {
            select_item(next_selectable(hot), true);
        }
        break;
    case 0x28: /* down */
        if (!in_bar) {
            select_item(next_selectable(hot), false);
        } else if (hot->children) {
            WmWindow *m = hot->children;
            if (!visible_flag(m)) {
                menu_show(m, true);
                wm_set_open_menu(m);
            }
            select_item(m->children, false);
        }
        break;
    }
}

/* 0x4c84c7 */
intptr_t menu_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    bool item = is_item(w);
    switch (msg) {
    case WM_MSG_MOUSEENTER: {
        if (a && !g_saved_focus)
            g_saved_focus = wm_focus();
        if (g_hot && !is_menu_window((WmWindow *)b)) {
            mark_item(g_hot, false);
            flush_redraws();
        }
        return 0;
    }
    case WM_MSG_KEYUP:
        switch (a) {
        case 0xd:
            if (g_hot)
                choose(g_hot);
            break;
        case 0x12:
            menu_activate(NULL);
            break;
        case 0x25:
        case 0x26:
        case 0x27:
        case 0x28:
            arrow_key(w, (int)a);
            break;
        }
        break;
    case WM_MSG_CHAR:
        /* an item's key, among the highlighted item's siblings */
        if (g_hot && g_hot->parent) {
            WmWindow *it = find_key(g_hot->parent->children, (int)a);
            close_popups(g_hot->parent, false, it);
            if (it) {
                if (!it->children) {
                    choose(it);
                } else {
                    menu_show(it->children, true);
                    wm_set_open_menu(it->children);
                    if (it->children->children)
                        select_item(it->children->children, false);
                }
            }
        }
        break;
    case MENU_MSG_FRAME:
        /* the frame: corners, tiled edges, the rest in the fill's first
         * color */
        if (w->surface) {
            GfxSurface *s = w->surface;
            int W = s->width, H = s->height;
            uint8_t fill = g_skin[SK_FILL] ? g_skin[SK_FILL]->pixels[0] : 0;
            gfx_fill_rect(s, 0, 0, W, H, fill);
#define TILE(i, x, y, w_, h_)                                                                                          \
    do {                                                                                                               \
        if (g_skin[i])                                                                                                 \
            gfx_tile_fill(s, (x), (y), (w_), (h_), g_skin[i], 0, 0, g_skin[i]->width, g_skin[i]->height);              \
    } while (0)
#define CORNER(i, x, y)                                                                                                \
    do {                                                                                                               \
        if (g_skin[i])                                                                                                 \
            gfx_blit(s, (x), (y), g_skin[i]->width, g_skin[i]->height, g_skin[i], 0, 0);                               \
    } while (0)
            TILE(SK_TOP, skin_w(SK_TL), 0, W - (skin_w(SK_TR) + skin_w(SK_TL)), skin_h(SK_TOP));
            TILE(SK_RIGHT, W - skin_w(SK_RIGHT), skin_h(SK_TR), skin_w(SK_RIGHT), H - (skin_h(SK_BR) + skin_h(SK_TR)));
            TILE(SK_BOTTOM, skin_w(SK_BL), H - skin_h(SK_BOTTOM), W - (skin_w(SK_BL) + skin_w(SK_BR)),
                 skin_h(SK_BOTTOM));
            TILE(SK_LEFT, 0, skin_h(SK_TL), skin_w(SK_LEFT), H - (skin_h(SK_BL) + skin_h(SK_TL)));
            CORNER(SK_TL, 0, 0);
            CORNER(SK_TR, W - skin_w(SK_TR), 0);
            CORNER(SK_BL, 0, H - skin_h(SK_BL));
            CORNER(SK_BR, W - skin_w(SK_BR), H - skin_h(SK_BR));
#undef TILE
#undef CORNER
        }
        return 0;
    case MENU_MSG_MEASURE:
        item_measure(w, (GfxRect *)a);
        return 0;
    case MENU_MSG_DRAW:
        item_draw(w, (const GfxRect *)a);
        return 0;
    case WM_MSG_ACTIVATE:
        if (!a) {
            if (!is_menu_window((WmWindow *)b)) {
                /* the focus went elsewhere: close everything */
                if (g_hot)
                    set_hot(g_hot, false);
                flush_redraws();
                close_popups(menu_bar_of(w), false, NULL);
                WmWindow *f = g_saved_focus;
                wm_set_open_menu(NULL);
                if (f) {
                    g_saved_focus = NULL;
                    wm_activate(f);
                }
            }
        } else if (!g_saved_focus) {
            g_saved_focus = wm_focus();
        }
        break;
    case WM_MSG_MOUSEMOVE:
        if (item)
            select_item(w, true);
        return 0;
    case WM_MSG_LBUTTONDOWN:
        if (item) {
            if (w->children && visible_flag(w->children)) {
                /* a second click on an open item closes its menu */
                close_popups(w->parent, false, NULL);
                wm_set_open_menu(NULL);
                if (g_saved_focus) {
                    wm_activate(g_saved_focus);
                    g_saved_focus = NULL;
                }
                return 0;
            }
            close_popups(w->parent, false, w->children);
            if (w->children && !visible_flag(w->children)) {
                set_hot(w, true);
                flush_redraws();
                menu_show(w->children, true);
                wm_set_open_menu(w->children);
            }
        }
        return 0;
    case WM_MSG_LBUTTONUP:
        if (!item) {
            /* released outside the menus (the window manager forwards it
             * here with w = the window under the mouse) */
            WmWindow *open = wm_open_menu();
            if (open && open->parent) {
                set_hot(open->parent, false);
                flush_redraws();
                WmWindow *bar = menu_bar_of(open->parent);
                if (bar) {
                    close_popups(bar, false, NULL);
                    if (g_saved_focus)
                        wm_activate(g_saved_focus);
                    g_saved_focus = NULL;
                    g_hot = NULL;
                    wm_set_open_menu(NULL);
                }
            }
        } else {
            /* released on an item: chosen (an item with a pop-up too; the
             * owner ignores those ids) */
            WmWindow *bar = menu_bar_of(w);
            if (!g_sticky || !w->children) {
                set_hot(w, false);
                flush_redraws();
                close_all(bar);
            }
            wm_send(bar ? bar->parent : NULL, w, WM_MSG_MENU_COMMAND, wm_get_data(w, MENU_DATA_ID), 0);
        }
        wm_release_capture();
        return 0;
    }
    return wm_default_proc(w, msg, a, b);
}
