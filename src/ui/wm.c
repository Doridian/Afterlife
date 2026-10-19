/*
 * Window manager (0x4c1580-0x4c62cc). See wm.h and docs/ui.md.
 */
#include "ui/wm.h"

#include <stdlib.h>
#include <string.h>

#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/mouse.h"
#include "ui/controls.h"
#include "ui/menu.h"
#include "ui/region.h"

WmHooks wm_hooks;

static WmWindow *g_root;        /* 0x672704 */
static WmWindow *g_focus;       /* 0x672708 */
static WmWindow *g_last_active; /* 0x67270c */
static WmWindow *g_hover;       /* 0x672710 window under the mouse */
static WmWindow *g_capture;     /* 0x672718 */
static WmWindow *g_open_menu;   /* 0x6727cc, owned by the menu module */

/* frame pieces, 0x67271c-0x67273c */
static GfxSurface *g_frame[9];
enum { FR_TL, FR_TOP, FR_TR, FR_RIGHT, FR_BR, FR_BOTTOM, FR_BL, FR_LEFT, FR_FILL };

static bool g_clip_saved;          /* 0x672748 */
static GfxRect g_saved_clip;       /* 0x535178 */
static uint32_t g_mode;            /* 0x67274c */
static bool g_input_live;          /* 0x672750 */
static unsigned g_buttons;         /* 0x672754 */
static GfxMouseHandler g_prev_mouse;   /* 0x672758 */
static GfxPaintHandler g_prev_paint;   /* 0x67275c */
static PlatCursor *g_default_cursor;   /* 0x672764 */
static int g_last_key;                 /* 0x672768 */
static bool g_hover_sent;              /* 0x67276c */
static int g_last_mx = -1, g_last_my = -1; /* 0x672770, 0x672774 */
static bool g_in_move;                 /* 0x672778 */
static bool g_dragging;                /* 0x67277c */
static int g_drag_x, g_drag_y;         /* 0x535154, 0x53519c */
static int g_rest_x, g_rest_y;         /* 0x5351a0, 0x535168 */
static uint32_t g_rest_time;           /* 0x53515c */
static uint32_t g_click_time;          /* 0x5351c0 */
static unsigned g_click_button;        /* 0x535158 */
static int g_click_x, g_click_y;       /* 0x53514c, 0x535148 */
static bool g_shift;                   /* 0x674330 */
static WmWindow *g_hit;                /* 0x64e3b0 */

/* Posted messages: 1000 x {window, target, msg, a, b} (0x530328), read
 * index 0x5351ac, write index 0x535170. */
#define WM_QUEUE_SIZE 1000
typedef struct WmQueued {
    WmWindow *w, *target;
    unsigned msg;
    intptr_t a, b;
} WmQueued;
static WmQueued g_queue[WM_QUEUE_SIZE];
static int g_q_read, g_q_write;

static void repaint_rect(const GfxRect *r);

/* ---------------------------------------------------------------- lists */

/* 0x4c48ec: appends w (at the back) */
static WmWindow *list_append_window(WmWindow *list, WmWindow *w)
{
    if (!w)
        return list;
    w->next = NULL;
    if (!list) {
        w->prev = NULL;
        return w;
    }
    WmWindow *last = list;
    while (last->next)
        last = last->next;
    last->next = w;
    w->prev = last;
    return list;
}

/* 0x4c4965: inserts w in front of `at`; returns the list head */
static WmWindow *list_insert_before(WmWindow *at, WmWindow *w)
{
    if (!w)
        return at;
    if (!at) {
        w->next = w->prev = NULL;
    } else {
        w->prev = at->prev;
        w->next = at;
        if (at->prev)
            at->prev->next = w;
        at->prev = w;
    }
    while (w->prev)
        w = w->prev;
    return w;
}

/* 0x4c49fc */
static WmWindow *list_unlink_window(WmWindow *list, WmWindow *w)
{
    if (!w)
        return list;
    if (w->prev)
        w->prev->next = w->next;
    if (w->next)
        w->next->prev = w->prev;
    if (w == list)
        list = w->next;
    return list;
}

/* 0x4c234f: first window that isn't minimized */
static WmWindow *first_unminimized(WmWindow *list)
{
    while (list && (list->flags & WM_F_MINIMIZED))
        list = list->next;
    return list;
}

/* 0x4c238d: first window that is neither minimized nor topmost */
static WmWindow *first_normal(WmWindow *list)
{
    list = first_unminimized(list);
    while (list && (list->flags & WM_F_TOPMOST))
        list = list->next;
    return list;
}

/* 0x4c5c46: numbers windows front to back, each window's descendants
 * before the window itself */
static uint32_t number_windows(WmWindow *list, uint32_t n)
{
    for (; list; list = list->next) {
        if (list->children)
            n = number_windows(list->children, n);
        list->z = n++;
    }
    return n;
}

/* 0x4c5c2b */
void wm_update_zorder(void) { number_windows(g_root, 1); }

/* ------------------------------------------------------------ geometry */

/* 0x4c5777: screen position of w's top-left corner */
static void abs_origin(const WmWindow *w, int *x, int *y)
{
    int ox = 0, oy = 0;
    for (; w; w = w->parent) {
        ox += w->rect.left;
        oy += w->rect.top;
    }
    *x = ox;
    *y = oy;
}

static void offset_rect(GfxRect *r, int dx, int dy)
{
    r->left += dx;
    r->right += dx;
    r->top += dy;
    r->bottom += dy;
}

/* 0x4c2ff7 */
void wm_local_to_screen(const WmWindow *w, GfxRect *r)
{
    if (!w || !r)
        return;
    int x, y;
    abs_origin(w, &x, &y);
    offset_rect(r, x, y);
}

/* 0x4c2f0e */
void wm_screen_to_local(const WmWindow *w, GfxRect *r)
{
    if (!w || !r)
        return;
    int x, y;
    abs_origin(w, &x, &y);
    offset_rect(r, -x, -y);
}

/* 0x4c5902 */
void wm_parent_to_screen(const WmWindow *w, GfxRect *r)
{
    if (!w || !r)
        return;
    int x, y;
    abs_origin(w->parent, &x, &y);
    offset_rect(r, x, y);
}

/* 0x4c3094 */
void wm_point_to_local(const WmWindow *w, int *x, int *y)
{
    if (!w || !x || !y)
        return;
    int ox, oy;
    abs_origin(w, &ox, &oy);
    *x -= ox;
    *y -= oy;
}

/* 0x4c310d */
void wm_point_to_screen(const WmWindow *w, int *x, int *y)
{
    if (!w || !x || !y)
        return;
    int ox, oy;
    abs_origin(w, &ox, &oy);
    *x += ox;
    *y += oy;
}

/* 0x4c2fb3 (no callers) */
void wm_map_rect(const WmWindow *from, const WmWindow *to, GfxRect *r)
{
    if (!from)
        return;
    wm_local_to_screen(from, r);
    if (to)
        wm_screen_to_local(to, r);
}

/* 0x4c317e */
void rect_move_to(GfxRect *r, int x, int y)
{
    if (!r)
        return;
    r->right = r->right - r->left + x;
    r->bottom = r->bottom - r->top + y;
    r->left = x;
    r->top = y;
}

WmWindow *wm_parent(const WmWindow *w) { return w ? w->parent : NULL; } /* 0x4c582f */

/* 0x4c57eb */
WmWindow *wm_siblings(const WmWindow *w)
{
    if (w && w->parent)
        return w->parent->children;
    return g_root;
}

/* 0x4c5856 */
bool wm_is_ancestor(const WmWindow *a, const WmWindow *w)
{
    for (; w; w = w->parent)
        if (w == a)
            return true;
    return false;
}

/* 0x4c5896 */
bool wm_is_sibling(const WmWindow *w, const WmWindow *other)
{
    if (!w || !other)
        return false;
    for (const WmWindow *p = wm_siblings(w); p; p = p->next)
        if (p == other)
            return true;
    return false;
}

/* 0x4c5ece */
void wm_get_rect(WmWindow *w, GfxRect *out)
{
    if (!out)
        return;
    if (!w)
        w = g_root;
    *out = w->rect;
}

/* 0x4c2c9c */
void wm_get_local_rect(WmWindow *w, GfxRect *out)
{
    if (!out)
        return;
    if (!w)
        w = g_root;
    *out = w->rect;
    rect_move_to(out, 0, 0);
}

/* 0x4c2aac */
void wm_set_rect(WmWindow *w, const GfxRect *r)
{
    if (!w || !r)
        return;
    GfxRect old = w->rect, nr = *r;
    wm_send(w, w, WM_MSG_SETRECT, (intptr_t)&old, (intptr_t)&nr);
}

/* 0x4c2b28 */
void wm_move(WmWindow *w, int x, int y)
{
    if (!w)
        return;
    GfxRect r = w->rect;
    rect_move_to(&r, x, y);
    wm_set_rect(w, &r);
}

/* 0x4c2a26 */
void wm_resize(WmWindow *w, int width, int height)
{
    if (!w)
        return;
    GfxRect old = w->rect, nr = w->rect;
    nr.right = nr.left + width - 1;
    nr.bottom = nr.top + height - 1;
    wm_send(w, w, WM_MSG_SETRECT, (intptr_t)&old, (intptr_t)&nr);
}

/* 0x4c294c. The original passes the old rect through wm_local_to_screen
 * (offsetting it by the window's position twice), so WM_MSG_SETRECT
 * repaints the wrong old area; the port passes the parent-relative rect
 * like the other callers. */
void wm_center(WmWindow *w)
{
    if (!w)
        return;
    GfxRect old = w->rect;
    GfxRect nr = w->rect;
    rect_move_to(&nr, 0, 0);
    wm_local_to_screen(w, &nr);
    int width = nr.right - nr.left + 1, height = nr.bottom - nr.top + 1;
    rect_move_to(&nr, (int)((unsigned)(gfx_screen_width() - width) >> 1),
                 (int)((unsigned)(gfx_screen_height() - height) >> 1));
    if (w->parent)
        wm_screen_to_local(w->parent, &nr);
    wm_send(w, w, WM_MSG_SETRECT, (intptr_t)&old, (intptr_t)&nr);
}

/* 0x4c6096 */
intptr_t wm_screen_resized(int width, int height)
{
    if (!g_root)
        return 0;
    GfxRect r = g_root->rect;
    r.right = r.left + width - 1;
    r.bottom = r.top + height - 1;
    g_root->rect = r;
    g_root->client = r;
    return wm_send(NULL, NULL, WM_MSG_SCREEN_RESIZED, width, height);
}

bool wm_resize_surface(WmWindow *w, int width, int height, int dx, int dy, uint8_t fill)
{
    if (!w || width <= 0 || height <= 0)
        return false;
    if (w->surface) {
        GfxSurface *s = gfx_surface_create(width, height);
        if (!s)
            return false;
        if (fill)
            gfx_fill_rect(s, 0, 0, width, height, fill);
        gfx_blit(s, dx, dy, w->surface->width, w->surface->height, w->surface, 0, 0);
        gfx_surface_destroy(w->surface);
        w->surface = s;
    }
    w->rect.right = w->rect.left + width - 1;
    w->rect.bottom = w->rect.top + height - 1;
    return true;
}

static bool fit_rect(GfxRect *r, const GfxRect *area)
{
    int x = r->left, y = r->top;
    if (r->right > area->right)
        x -= r->right - area->right;
    if (r->bottom > area->bottom)
        y -= r->bottom - area->bottom;
    if (x < area->left)
        x = area->left;
    if (y < area->top)
        y = area->top;
    if (x == r->left && y == r->top)
        return false;
    rect_move_to(r, x, y);
    return true;
}

bool wm_fit_into(WmWindow *w, const GfxRect *area)
{
    if (!w || !area)
        return false;
    if (w->minimized)
        fit_rect(&w->restore_rect, area);
    return fit_rect(&w->rect, area);
}

/* -------------------------------------------------------------- frame */

/* 0x4c5f7a. The original stores the inset rect in parent coordinates
 * here, unlike wm_create_window, which stores it local; the port keeps it
 * local in both. */
static void apply_frame(WmWindow *list)
{
    for (; list; list = list->next) {
        if (list->children)
            apply_frame(list->children);
        if (!(list->flags & WM_F_FRAMED))
            continue;
        GfxRect r = list->rect;
        if (g_frame[FR_LEFT])
            r.left += g_frame[FR_LEFT]->width;
        if (g_frame[FR_TOP])
            r.top += g_frame[FR_TOP]->height;
        if (g_frame[FR_RIGHT])
            r.right -= g_frame[FR_RIGHT]->width;
        if (g_frame[FR_BOTTOM])
            r.bottom -= g_frame[FR_BOTTOM]->height;
        offset_rect(&r, -list->rect.left, -list->rect.top);
        list->client = r;
    }
}

/* 0x4c5f19 (no callers) */
void wm_set_frame(GfxSurface *top_left, GfxSurface *top, GfxSurface *top_right, GfxSurface *right,
                  GfxSurface *bottom_right, GfxSurface *bottom, GfxSurface *bottom_left, GfxSurface *left,
                  GfxSurface *fill)
{
    g_frame[FR_TL] = top_left;
    g_frame[FR_TOP] = top;
    g_frame[FR_TR] = top_right;
    g_frame[FR_RIGHT] = right;
    g_frame[FR_BR] = bottom_right;
    g_frame[FR_BOTTOM] = bottom;
    g_frame[FR_BL] = bottom_left;
    g_frame[FR_LEFT] = left;
    g_frame[FR_FILL] = fill;
    apply_frame(g_root);
}

/* -------------------------------------------------------------- data */

/* 0x4c5a00. Like the original, offset == extra_size is accepted (it wrote
 * 4 bytes past the block); the port allocates a spare slot for it. */
void wm_set_data(WmWindow *w, uint32_t offset, intptr_t value)
{
    if (w && offset <= w->extra_size)
        w->extra[offset / 4] = value;
}

/* 0x4c5a47 */
intptr_t wm_get_data(const WmWindow *w, uint32_t offset)
{
    if (!w || w->extra_size == 0 || offset > w->extra_size)
        return 0;
    return w->extra[offset / 4];
}

const char *wm_get_title(const WmWindow *w) { return w ? w->title : NULL; }        /* 0x4c5aa8 */
int wm_title_capacity(const WmWindow *w) { return w ? (int)w->title_size - 1 : 0; } /* 0x4c5acf */

/* 0x4c5af7 */
int wm_title_length(const WmWindow *w)
{
    return w && w->title ? (int)strlen(w->title) : 0;
}

/* 0x4c5b27 */
int wm_set_title_size(WmWindow *w, uint32_t size)
{
    if (!w || size == 0)
        return -1;
    char *t = calloc(size, 1);
    if (!t)
        return -1;
    if (w->title)
        strncpy(t, w->title, size - 1);
    free(w->title);
    w->title = t;
    /* the original doesn't update +0x58, so later wm_set_title calls still
     * use the old size; the port keeps them consistent */
    w->title_size = size;
    return 0;
}

/* 0x4c5bc2 */
int wm_set_title(WmWindow *w, const char *text)
{
    if (!w || !w->title || !text)
        return -1;
    if (text != w->title) {
        strncpy(w->title, text, w->title_size - 1);
    }
    w->title[w->title_size - 1] = '\0';
    return 0;
}

uint32_t wm_get_mode(void) { return g_mode; }       /* 0x4c6081 */
void wm_set_mode(uint32_t mode) { g_mode = mode; }  /* 0x4c606e */

WmWindow *wm_root(void) { return g_root; }
WmWindow *wm_focus(void) { return g_focus; }               /* 0x4c5eb9 */
void wm_set_focus(WmWindow *w) { g_focus = w; }            /* 0x4c62cc */
WmWindow *wm_last_activated(void) { return g_last_active; }
void wm_set_input_live(bool live) { g_input_live = live; }
bool wm_input_live(void) { return g_input_live; }
bool wm_shift_down(void) { return g_shift; }
void wm_set_open_menu(WmWindow *menu) { g_open_menu = menu; }
WmWindow *wm_open_menu(void) { return g_open_menu; }

/* --------------------------------------------------------- messages */

/* 0x4c2830 */
intptr_t wm_send(WmWindow *w, WmWindow *target, unsigned msg, intptr_t a, intptr_t b)
{
    if (!w) {
        if (msg == WM_MSG_SCREEN_RESIZED) {
            wm_broadcast(g_root, WM_MSG_SCREEN_RESIZED, a, b);
            return 0;
        }
        return -1;
    }
    if (w == g_root && msg != WM_MSG_CLOSE)
        return -1;
    if (!w->proc)
        return -1;
    return w->proc(target, msg, a, b);
}

/* 0x4c27ca */
void wm_broadcast(WmWindow *list, unsigned msg, intptr_t a, intptr_t b)
{
    for (; list; list = list->next) {
        if (list->children)
            wm_broadcast(list->children, msg, a, b);
        wm_send(list, list, msg, a, b);
    }
}

/* 0x4c5ce4 */
void wm_queue_clear(void) { g_q_read = g_q_write = 0; }

/* 0x4c5d03 */
void wm_post(WmWindow *w, WmWindow *target, unsigned msg, intptr_t a, intptr_t b)
{
    if (!target || !msg)
        return;
    g_queue[g_q_write] = (WmQueued){w, target, msg, a, b};
    if (++g_q_write >= WM_QUEUE_SIZE)
        g_q_write = 0;
    if (g_q_write == g_q_read && ++g_q_read >= WM_QUEUE_SIZE)
        g_q_read = 0; /* full: drop the oldest */
}

/* 0x4c5dd7. After advancing the read index the original also advances the
 * write index if it is now behind the read index, which after a wrap
 * replays stale entries; the port is a plain ring. */
static bool queue_pop(WmQueued *out)
{
    if (g_q_read == g_q_write)
        return false;
    *out = g_queue[g_q_read];
    if (++g_q_read >= WM_QUEUE_SIZE)
        g_q_read = 0;
    return true;
}

/* 0x4c614f */
void wm_flush_input(void)
{
    int x, y;
    wm_queue_clear();
    /* kb_queue_reset: the keyboard queue belongs to the key_poll hook */
    gfx_mouse_queue_reset();
    g_buttons = gfx_mouse_get_state(&x, &y);
}

/* 0x4c4700 / 0x4c46bd */
WmProc wm_set_proc(WmWindow *w, WmProc proc)
{
    if (!w || !proc)
        return NULL;
    WmProc old = w->proc;
    w->proc = proc;
    return old;
}

WmProc wm_get_proc(WmWindow *w) { return w ? w->proc : NULL; } /* 0x4c4743 */

/* --------------------------------------------------------- creation */

/* 0x4c47ee */
static WmWindow *alloc_window(uint32_t extra_size)
{
    WmWindow *w = calloc(1, sizeof *w);
    if (!w)
        return NULL;
    w->extra = calloc(extra_size / 4 + 1, sizeof *w->extra);
    if (!w->extra) {
        free(w);
        return NULL;
    }
    return w;
}

/* 0x4c4841 */
static void free_window(WmWindow *w)
{
    if (w->surface) {
        gfx_surface_destroy(w->surface);
        w->surface = NULL;
    }
    free(w->title);
    w->title = NULL;
    if (w->button_under)
        gfx_surface_destroy(w->button_under);
    /* port-only: the original leaks these and leaves a dangling capture */
    rgn_free(w->invalid);
    if (w == g_capture)
        wm_release_capture();
    if (w == g_open_menu)
        g_open_menu = NULL;
    if (w == g_last_active)
        g_last_active = NULL;
    if (w == g_focus)
        g_focus = NULL;
    if (w == g_hover)
        g_hover = NULL;
    /* port-only: drop queued messages for it (the original would deliver
     * them to freed memory) */
    for (int i = 0; i < WM_QUEUE_SIZE; i++)
        if (g_queue[i].w == w || g_queue[i].target == w)
            g_queue[i].msg = 0;
    free(w->extra);
    free(w);
}

/* 0x4c6220: the menu bar's close and minimize buttons are only shown when
 * the game fills the desktop */
void wm_menubar_update_buttons(WmWindow *menubar)
{
    if (!menubar)
        return;
    int dw = 0, dh = 0;
    plat_video_desktop_size(&dw, &dh);
    bool full = dw <= gfx_screen_width() && dh <= gfx_screen_height();
    WmWindow *minimize = menubar->min_button;
    wm_show(menubar->close_button, full);
    wm_show(minimize, full);
    wm_send(menubar, menubar, WM_MSG_MENUBAR, 0, 0);
}

/* the WM_F_MENUBAR part of wm_create_window */
static void create_menubar(WmWindow *w)
{
    WmWindow *bar = menu_create(w, NULL, MENU_BAR);
    w->menubar = bar;
    if (!bar)
        return;
    /* The original caches the two bitmaps (0x672740 / 0x672744) and hands
     * them to the buttons, which free them when destroyed, leaving the
     * cache dangling for the next bar. The port loads a copy per bar. */
    GfxSurface *bmp_close = gfx_load_bmp_resource(0x230, NULL);
    GfxSurface *bmp_min = gfx_load_bmp_resource(0x231, NULL);
    if (!bmp_close || !bmp_min) {
        if (bmp_close)
            gfx_surface_destroy(bmp_close);
        if (bmp_min)
            gfx_surface_destroy(bmp_min);
        return;
    }
    GfxRect r;
    wm_get_local_rect(bar, &r);
    r.top += 3;
    r.bottom = r.top + bmp_close->height - 1;
    r.left = (r.right - 3) - (bmp_close->width >> 1);
    r.right = r.left + bmp_close->width - 1;
    int min_w = bmp_min->width;
    bar->close_button = ctl_button_create(bar, &r, 0x230, 0x404, bmp_close);
    if (!bar->close_button)
        gfx_surface_destroy(bmp_close);
    r.left = (r.left - 1) - (min_w >> 1);
    r.right = r.left + min_w - 1;
    bar->min_button = ctl_button_create(bar, &r, 0x231, 0x404, bmp_min);
    if (!bar->min_button)
        gfx_surface_destroy(bmp_min);
    wm_menubar_update_buttons(bar);
}

/* 0x4c1bce */
WmWindow *wm_create_window(WmWindow *parent, GfxRect *rect, WmProc proc, uint32_t flags, uint32_t extra_size)
{
    if (!parent)
        parent = g_root;
    WmWindow *w = alloc_window(extra_size);
    if (!w)
        return NULL;
    w->extra_size = extra_size;
    if (!(flags & WM_F_NOSURFACE)) {
        w->surface = gfx_surface_create(rect->right - rect->left + 1, rect->bottom - rect->top + 1);
        if (!w->surface) {
            free(w->extra);
            free(w);
            return NULL;
        }
    }
    w->proc = proc;
    w->flags = flags | WM_F_EXISTS;
    w->parent = parent;
    w->cursor = g_default_cursor;
    if (parent)
        parent->children = list_append_window(parent->children, w);
    wm_update_zorder();
    if (!(flags & WM_F_NOTITLE)) {
        w->title = calloc(0x100, 1);
        if (!w->title) {
            wm_destroy(w);
            return NULL;
        }
        w->title_size = 0x100;
    }
    if ((flags & WM_F_CENTER) && w->parent) {
        int width = rect->right - rect->left + 1, height = rect->bottom - rect->top + 1;
        const GfxRect *pr = &w->parent->rect;
        rect->left = ((pr->right - pr->left + 1) - width) >> 1;
        rect->top = ((pr->bottom - pr->top + 1) - height) >> 1;
        rect->right = rect->left + width - 1;
        rect->bottom = rect->top + height - 1;
    }
    wm_send(w, w, WM_MSG_PRECREATE, (intptr_t)rect, 0);
    rgn_rect_set(&w->rect, rect->left, rect->right, rect->top, rect->bottom);
    if (flags & WM_F_FRAMED) {
        /* the caller's rect becomes the client area, in parent coordinates */
        if (g_frame[FR_LEFT])
            rect->left += g_frame[FR_LEFT]->width;
        if (g_frame[FR_TOP])
            rect->top += g_frame[FR_TOP]->height;
        if (g_frame[FR_RIGHT])
            rect->right -= g_frame[FR_RIGHT]->width;
        if (g_frame[FR_BOTTOM])
            rect->bottom -= g_frame[FR_BOTTOM]->height;
    }
    rgn_rect_set(&w->client, rect->left - w->rect.left, rect->right - w->rect.left, rect->top - w->rect.top,
                 rect->bottom - w->rect.top);
    if (wm_send(w, w, WM_MSG_CREATE, 0, 0) != 1) {
        wm_destroy(w);
        return NULL;
    }
    if (flags & WM_F_MENUBAR)
        create_menubar(w);
    if (flags & WM_F_HSCROLL)
        w->hscroll = ctl_scroll_create(w, NULL, 0, 0);
    if (flags & WM_F_VSCROLL)
        w->vscroll = ctl_scroll_create(w, NULL, 0, CTL_SCROLL_VERTICAL);
    return w;
}

/* 0x4c1ba2 */
WmWindow *wm_create_toplevel(GfxRect *rect, WmProc proc, uint32_t flags)
{
    return wm_create_window(g_root, rect, proc, flags, 0);
}

/* 0x4c20f7 */
void wm_destroy(WmWindow *w) { wm_send(w, w, WM_MSG_DESTROY, 0, 0); }

/* -------------------------------------------------- visibility, stacking */

/* 0x4c59a2 */
bool wm_is_visible(const WmWindow *w)
{
    for (; w; w = w->parent)
        if (!(w->flags & WM_F_VISIBLE))
            return false;
    return true;
}

bool wm_is_minimized(const WmWindow *w) { return w && w->minimized; } /* 0x4c59e3 */

/* 0x4c23da */
bool wm_show(WmWindow *w, bool show)
{
    if (!w)
        return false;
    if (show)
        wm_set_minimized(w, false);
    bool was = wm_is_visible(w);
    if (!show) {
        w->flags &= ~WM_F_VISIBLE;
    } else {
        w->flags |= WM_F_VISIBLE;
        if (was)
            return was;
    }
    bool now = wm_is_visible(w);
    if ((show || was) && was != now) {
        wm_update_zorder();
        GfxRect r = w->rect;
        wm_parent_to_screen(w, &r);
        repaint_rect(&r);
        wm_send(w, w, WM_MSG_SHOW, show, 0);
    }
    return was;
}

/* 0x4c2551 */
void wm_show_list(WmWindow *list, bool show)
{
    wm_update_zorder();
    for (; list; list = list->next) {
        wm_show(list, show);
        if (list->children)
            wm_show_list(list->children, show);
    }
}

/* 0x4c2531 */
void wm_show_all(bool show)
{
    if (g_root)
        wm_show_list(g_root->children, show);
}

/* 0x4c211d */
void wm_lower(WmWindow *w)
{
    if (!w)
        return;
    WmWindow *parent = w->parent;
    WmWindow *list = list_unlink_window(wm_siblings(w), w), *at;
    if (w->flags & WM_F_MINIMIZED) {
        at = first_unminimized(list);
        list = at ? list_insert_before(at, w) : list_append_window(list, w);
    } else if (!(w->flags & WM_F_TOPMOST)) {
        list = list_append_window(list, w);
    } else {
        at = first_normal(list);
        list = at ? list_insert_before(at, w) : list_append_window(list, w);
    }
    if (parent)
        parent->children = list;
    wm_update_zorder();
    wm_redraw(w, NULL);
}

/* 0x4c2248 */
void wm_raise(WmWindow *w)
{
    if (!w)
        return;
    WmWindow *parent = w->parent;
    WmWindow *list = list_unlink_window(wm_siblings(w), w), *at;
    if (w->flags & WM_F_MINIMIZED) {
        at = list;
    } else if (!(w->flags & WM_F_TOPMOST)) {
        at = first_normal(list);
        if (!at)
            at = list;
    } else {
        at = first_unminimized(list);
        if (!at)
            at = list;
    }
    list = list_insert_before(at, w);
    if (parent)
        parent->children = list;
    wm_update_zorder();
    wm_redraw(w, NULL);
}

/* 0x4c290a */
void wm_activate(WmWindow *w)
{
    if (w && (w->flags & WM_F_VISIBLE))
        wm_send(w, w, WM_MSG_ACTIVATE, 1, 0);
}

/* 0x4c5c9f */
void wm_show_scrollbars(WmWindow *w, unsigned which, bool show)
{
    if (which & 1)
        wm_show(w->vscroll, show);
    if (which & 2)
        wm_show(w->hscroll, show);
}

/* --------------------------------------------------------- painting */

/* the build / paint / free sequence the original repeats inline */
static void repaint_rect(const GfxRect *r)
{
    RgnNode *list = rgn_build_visible(g_root, r, NULL, true);
    rgn_paint(list);
    rgn_free(list);
}

/* 0x4c32f2 / 0x4c568f */
void wm_repaint_screen(const GfxRect *r)
{
    if (r)
        repaint_rect(r);
}

static void paint_handler(const GfxRect *r) { wm_repaint_screen(r); } /* 0x4c568f */

/* 0x4c25c4 */
void wm_redraw(WmWindow *w, const GfxRect *r)
{
    if (!w || !wm_is_visible(w))
        return;
    GfxRect s;
    if (!r) {
        s = w->rect;
        wm_parent_to_screen(w, &s);
    } else {
        s = *r;
        wm_local_to_screen(w, &s);
    }
    repaint_rect(&s);
}

/* 0x4c2690. With r == NULL the original takes the parent-relative rect
 * through wm_local_to_screen (offsetting it twice); the port invalidates
 * the whole window as intended. */
void wm_invalidate(WmWindow *w, const GfxRect *r)
{
    if (!w)
        return;
    GfxRect s;
    if (r) {
        s = *r;
        wm_local_to_screen(w, &s);
    } else {
        s = w->rect;
        wm_parent_to_screen(w, &s);
    }
    RgnNode *n = rgn_alloc(w, &s);
    if (n)
        w->invalid = rgn_append(w->invalid, n);
}

/* 0x4c2733 */
void wm_update(WmWindow *w)
{
    if (!w || !w->invalid)
        return;
    for (RgnNode *n = w->invalid; n; n = n->next)
        repaint_rect(&n->r);
    rgn_free(w->invalid);
    w->invalid = NULL;
}

/* 0x4c402c */
void wm_draw_title(WmWindow *w, const char *text, uint32_t font, uint8_t color, uint8_t bg)
{
    if (!w)
        return;
    const char *t = text ? text : w->title;
    if (!t)
        return;
    wm_set_title(w, t);
    GfxRect tr = w->title_rect, area = w->title_rect;
    gfx_fill_rect(w->surface, tr.left, tr.top, tr.right - tr.left + 1, tr.bottom - tr.top + 1, bg);
    gfx_font_select(font);
    unsigned colors[2] = {color, color};
    gfx_font_set_colors(font, 1, 2, colors);
    gfx_font_draw_text(w->surface, tr.left, tr.top, &tr, &area, w->title ? w->title : t, GFX_TEXT_NOJUSTIFY);
    wm_redraw(w, &w->title_rect);
}

/* ------------------------------------------------------------ minimize */

/* 0x4c457a: the minimize button's rect, top right (local) */
static void min_button_rect(WmWindow *w, GfxRect *out)
{
    if (!(w->flags & WM_F_MINIMIZABLE))
        return;
    const GfxSurface *bmp = (const GfxSurface *)wm_get_data(w->min_button, CTL_BUTTON_BITMAP);
    if (!bmp) {
        gfx_rect_set(out, 0, 0, 0, 0);
        return;
    }
    out->left = (w->rect.right - w->rect.left) - (bmp->width >> 1) + 1;
    out->right = out->left + bmp->width - 1;
    out->top = 0;
    out->bottom = bmp->height - 1;
}

/* 0x4c4616 */
static void place_min_button(WmWindow *w)
{
    if (!(w->flags & WM_F_MINIMIZABLE))
        return;
    GfxRect r = {0, 0, 0, 0};
    min_button_rect(w, &r);
    wm_set_rect(w->min_button, &r);
    w->button_rect = r;
}

/* 0x4c3fcc */
static void set_title_rect(WmWindow *w)
{
    if (!(w->flags & WM_F_MINIMIZABLE))
        gfx_rect_set(&w->title_rect, 0x14, w->rect.right - 3, 4, 0xb);
    else
        gfx_rect_set(&w->title_rect, 0x14, w->min_button->rect.left - 1, 4, 0xb);
}

/* 0x4c4683 (no callers) */
void wm_set_icon_height(WmWindow *w, int h)
{
    if (w && h && (w->flags & WM_F_MINIMIZABLE))
        w->icon_height = h;
}

/* 0x4c414f: finds a place for a new icon of `width` at (*x, *y), moving
 * right past icons on the same row and wrapping at max_x */
static void find_icon_slot(WmWindow *parent, WmWindow *list, int max_x, int width, int *x, int *y)
{
    if (!list)
        return;
    find_icon_slot(parent, list->next, max_x, width, x, y);
    if (!wm_is_minimized(list) || !wm_is_visible(list)) {
        find_icon_slot(parent, list->children, max_x, width, x, y);
    } else if (list->rect.top == *y && *x < list->rect.right) {
        if (max_x < list->rect.right + width + 1) {
            *x = parent->client.left;
            *y += list->icon_height;
            find_icon_slot(parent, parent, parent->client.right, width, x, y);
        } else {
            *x = list->rect.right + 1;
            *y = list->rect.top;
        }
    }
}

/* 0x4c4346: icons after (x, y), sorted by row then column */
static void collect_icons(WmWindow *list, RgnNode **out, int x, int y)
{
    if (!list)
        return;
    collect_icons(list->next, out, x, y);
    if (!wm_is_minimized(list)) {
        collect_icons(list->children, out, x, y);
        return;
    }
    if (!wm_is_visible(list) || !(y < list->rect.top || (list->rect.top == y && x < list->rect.left)))
        return;
    GfxRect none = {0, 0, 0, 0};
    RgnNode *n = rgn_alloc(list, &none);
    if (!n)
        return;
    RgnNode *c = *out;
    while (c && !(c->owner->rect.top > list->rect.top ||
                  (c->owner->rect.top == list->rect.top && c->owner->rect.left >= list->rect.left)))
        c = c->next;
    if (!c) {
        *out = rgn_append(*out, n);
    } else {
        n->next = c;
        n->prev = c->prev;
        if (c->prev)
            c->prev->next = n;
        else
            *out = n;
        c->prev = n;
    }
}

/* 0x4c426a: closes the gap an icon at (x, y) left */
static void relayout_icons(WmWindow *parent, int x, int y)
{
    RgnNode *list = NULL;
    collect_icons(parent, &list, x, y);
    for (RgnNode *n = list; n; n = n->next) {
        WmWindow *w = n->owner;
        int width = w->rect.right - w->rect.left + 1;
        if (parent->client.right < width + x) {
            x = 0;
            y += w->icon_height;
        }
        wm_move(w, x, y);
        x += width;
    }
    rgn_free(list);
}

/* 0x4c3bac */
bool wm_toggle_minimized(WmWindow *w)
{
    if (!w || !(w->flags & WM_F_MINIMIZABLE) || !w->min_button || !w->button_under)
        return false;
    const GfxSurface *bmp = (const GfxSurface *)wm_get_data(w->min_button, CTL_BUTTON_BITMAP);
    GfxSurface *under = w->button_under;
    GfxRect br;
    if (!w->minimized) {
        w->saved_flags = w->flags;
        w->flags = (w->flags & ~WM_F_TOPMOST) | WM_F_MINIMIZED;
        w->restore_rect = w->rect;
        /* put back what was under the button, shrink, save under its new place */
        gfx_blit(w->surface, w->button_rect.left, w->button_rect.top, under->width, under->height, under, 0, 0);
        int width = gfx_font_text_width(w->title ? w->title : "") + (bmp ? bmp->width >> 1 : 0) + 0x19;
        wm_resize(w, width, w->icon_height);
        min_button_rect(w, &br);
        gfx_blit(under, 0, 0, br.right - br.left + 1, br.bottom - br.top + 1, w->surface, br.left, br.top);
        place_min_button(w);
        ctl_button_draw(w->min_button, (unsigned)wm_get_data(w->min_button, CTL_BUTTON_STATE) | 3);
        WmWindow *parent = w->parent;
        int x = parent ? parent->client.left : 0, y = parent ? parent->client.top : 0;
        if (parent)
            find_icon_slot(parent, parent, parent->client.right, width, &x, &y);
        wm_move(w, x, y);
        w->minimized = true;
        set_title_rect(w);
        wm_send(w, w, WM_MSG_MINIMIZED, 1, 0);
        wm_raise(w);
        return true;
    }
    int old_left = w->rect.left, old_top = w->rect.top;
    gfx_blit(w->surface, w->button_rect.left, w->button_rect.top, under->width, under->height, under, 0, 0);
    wm_set_rect(w, &w->restore_rect);
    min_button_rect(w, &br);
    gfx_blit(under, 0, 0, br.right - br.left + 1, br.bottom - br.top + 1, w->surface, br.left, br.top);
    place_min_button(w);
    ctl_button_draw(w->min_button, (unsigned)wm_get_data(w->min_button, CTL_BUTTON_STATE) | 3);
    w->minimized = false;
    if (wm_is_visible(w) && w->parent)
        relayout_icons(w->parent, old_left, old_top);
    w->flags = w->saved_flags;
    set_title_rect(w);
    wm_send(w, w, WM_MSG_MINIMIZED, 0, 0);
    wm_raise(w);
    return false;
}

/* 0x4c3f4d */
void wm_set_minimized(WmWindow *w, bool minimize)
{
    if (!w || !(w->flags & WM_F_MINIMIZABLE) || w->minimized == minimize)
        return;
    ctl_button_set_pressed(w->min_button, minimize, true);
    wm_toggle_minimized(w);
}

/* ------------------------------------------------------------- input */

/* 0x4c2d88 */
static WmWindow *hit_list(WmWindow *list, int x, int y)
{
    for (; list; list = list->next) {
        if (!(list->flags & WM_F_VISIBLE))
            continue;
        GfxRect r = list->rect;
        wm_parent_to_screen(list, &r);
        if (!list->minimized) {
            if (list->children) {
                WmWindow *h = hit_list(list->children, x, y);
                if (h)
                    g_hit = h;
            }
            if ((!g_hit || list->z < g_hit->z) && gfx_rect_contains(&r, x, y))
                g_hit = list;
        } else if (gfx_rect_contains(&r, x, y) && list->children) {
            /* a minimized window only takes clicks on its children (the
             * minimize button) */
            WmWindow *h = hit_list(list->children, x, y);
            if (h)
                g_hit = h;
        }
    }
    return g_hit;
}

/* 0x4c2cf7 */
WmWindow *wm_window_at(int x, int y)
{
    g_hit = NULL;
    if (!g_capture)
        return hit_list(g_root, x, y);
    WmWindow *w = g_capture;
    if (g_capture->flags & WM_F_HITCHILDREN) {
        WmWindow *h = hit_list(g_capture->children, x, y);
        if (h)
            w = h;
    }
    return w;
}

/* 0x4c31e5. The clip rectangle is the parent's client rect as is (local
 * coordinates), as in the original. */
WmWindow *wm_set_capture(WmWindow *w)
{
    if (!w)
        return NULL;
    WmWindow *old = g_capture;
    if (g_capture)
        wm_send(g_capture, g_capture, WM_MSG_CAPTURE_LOST, (intptr_t)w, 0);
    g_capture = w;
    if (w->parent && (w->parent->flags & WM_F_CLIPMOUSE)) {
        gfx_mouse_set_clip_rect(&g_saved_clip, &w->parent->client);
        g_clip_saved = true;
    }
    return old;
}

WmWindow *wm_capture(void) { return g_capture; } /* 0x4c329d */

/* 0x4c32b2 */
void wm_release_capture(void)
{
    g_capture = NULL;
    if (g_clip_saved) {
        GfxRect prev;
        gfx_mouse_set_clip_rect(&prev, &g_saved_clip);
        g_clip_saved = false;
    }
}

/* 0x4c523a */
void wm_mouse_event(int event, int x, int y)
{
    uint32_t now = plat_ticks_ms();
    WmWindow *w = wm_window_at(x, y);
    if (!w)
        return;
    if (g_open_menu && !(wm_get_data(w, 0) & 0x800000) && event == 5) {
        /* a click outside the open menu goes to the menu's owner */
        wm_send(g_open_menu->parent, w, WM_MSG_LBUTTONUP, x, y);
        return;
    }
    if (w->cursor)
        gfx_cursor_set(w->cursor);
    if (w != g_hover) {
        wm_send(w, w, WM_MSG_MOUSEENTER, 1, (intptr_t)g_hover);
        wm_send(g_hover, g_hover, WM_MSG_MOUSEENTER, 0, (intptr_t)w);
        g_hover = w;
    }
    wm_point_to_local(w, &x, &y);
    unsigned button = 0;
    unsigned up_msg = 0, dbl_msg = 0;
    switch (event) {
    case 1:
        if (!wm_is_minimized(w))
            wm_send(w, w, WM_MSG_MOUSEMOVE, x, y);
        return;
    case 2:
        if (w != g_focus)
            wm_send(w, w, WM_MSG_ACTIVATE, 1, 0);
        wm_send(w, w, WM_MSG_LBUTTONDOWN, x, y);
        return;
    case 3:
        wm_send(w, w, WM_MSG_RBUTTONDOWN, x, y);
        return;
    case 4:
        wm_send(w, w, WM_MSG_MBUTTONDOWN, x, y);
        return;
    case 5:
        button = 1, up_msg = WM_MSG_LBUTTONUP, dbl_msg = WM_MSG_LDBLCLK;
        break;
    case 6:
        button = 2, up_msg = WM_MSG_RBUTTONUP, dbl_msg = WM_MSG_RDBLCLK;
        break;
    case 7:
        button = 4, up_msg = WM_MSG_MBUTTONUP, dbl_msg = WM_MSG_MDBLCLK;
        break;
    case 8:
        if (!wm_is_minimized(w))
            wm_send(w, w, WM_MSG_HOVER, x, y);
        return;
    default:
        return;
    }
    /* a release within 500 ms and 3 pixels of the previous release of the
     * same button is a double click */
    if (now - g_click_time < 500 && g_click_button == button && abs(x - g_click_x) < 4 && abs(y - g_click_y) < 4)
        wm_send(w, w, dbl_msg, x, y);
    wm_send(w, w, up_msg, x, y);
    g_click_button = button;
    g_click_x = x;
    g_click_y = y;
    g_click_time = now;
}

static void mouse_handler(int event, int x, int y) { wm_mouse_event(event, x, y); }

/* 0x4c56b5 */
bool wm_key_event(int type, int key)
{
    if (!g_focus)
        return true;
    if (type == WM_KEY_CHAR)
        wm_send(g_focus, g_focus, WM_MSG_CHAR, key, 0);
    else if (type == WM_KEY_DOWN)
        wm_send(g_focus, g_focus, WM_MSG_KEYDOWN, key, 0);
    else if (type == WM_KEY_UP)
        wm_send(g_focus, g_focus, WM_MSG_KEYUP, key, 0);
    return false;
}

static void attract_stop(void)
{
    if (wm_hooks.attract_stop)
        wm_hooks.attract_stop();
    else
        g_input_live = true;
}

/* 0x4c178f */
void wm_idle(void)
{
    uint32_t now = plat_ticks_ms();
    int mx, my, type, key, ch;
    unsigned buttons = gfx_mouse_get_state(&mx, &my);
    if (!g_input_live) {
        /* attract mode: a click or Esc ends it */
        if (buttons)
            attract_stop();
        if (wm_hooks.key_poll && wm_hooks.key_poll(&type, &key, &ch) && type == WM_KEY_CHAR) {
            if ((char)ch == 0x1b)
                attract_stop();
            else if ((char)ch == '.')
                wm_send(g_focus, g_focus, WM_MSG_CHAR, (char)ch, key);
        }
    } else {
        /* hover: the mouse resting within a pixel for 1.5 s */
        if (abs(mx - g_rest_x) < 2 && abs(my - g_rest_y) < 2) {
            if (!g_hover_sent && now - g_rest_time > 1500) {
                wm_mouse_event(8, g_rest_x, g_rest_y);
                g_hover_sent = true;
            } else if (buttons) {
                g_hover_sent = true;
            }
        } else {
            g_rest_x = mx;
            g_rest_y = my;
            g_hover_sent = false;
            g_rest_time = now;
        }
        if (wm_hooks.key_poll && wm_hooks.key_poll(&type, &key, &ch)) {
            if (key == 0x10) /* VK_SHIFT */
                g_shift = type == WM_KEY_DOWN;
            if (type == WM_KEY_CHAR)
                wm_send(g_focus, g_focus, WM_MSG_CHAR, (char)ch, key);
            else if (type == WM_KEY_DOWN)
                wm_send(g_focus, g_focus, WM_MSG_KEYDOWN, key, (char)ch);
            else if (type == WM_KEY_UP)
                wm_send(g_focus, g_focus, WM_MSG_KEYUP, key, (char)ch);
        }
        if ((g_last_mx != mx || g_last_my != my) && !g_in_move) {
            g_last_mx = mx;
            g_last_my = my;
            g_in_move = true;
            wm_mouse_event(1, mx, my);
            g_in_move = false;
        }
        unsigned changed = g_buttons ^ buttons;
        if (changed) {
            g_buttons = buttons;
            if (changed & 1)
                wm_mouse_event((buttons & 1) ? 2 : 5, mx, my);
            if (changed & 2)
                wm_mouse_event((buttons & 2) ? 3 : 6, mx, my);
            if (changed & 4)
                wm_mouse_event((buttons & 4) ? 4 : 7, mx, my);
        }
    }
    WmQueued q;
    if (queue_pop(&q) && q.msg)
        wm_send(q.w, q.target, q.msg, q.a, q.b);
}

/* 0x4c619d */
PlatCursor *wm_set_cursor(WmWindow *w, PlatCursor *c)
{
    if (!w || !c)
        return NULL;
    w->cursor = c;
    int x, y;
    gfx_mouse_get_state(&x, &y);
    if (wm_window_at(x, y) == w)
        gfx_cursor_set(c);
    return c;
}

void wm_set_default_cursor(PlatCursor *c) { g_default_cursor = c; } /* 0x4c620d */

/* ------------------------------------------------------- default proc */

/* 0x4c3331 */
intptr_t wm_default_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    if (!w)
        return 0;
    switch (msg) {
    case WM_MSG_CREATE:
        w->saved_flags = w->flags;
        if (w->flags & WM_F_MINIMIZABLE) {
            /* the minimize button: a two-frame bitmap, and a save of the
             * window surface under it */
            GfxSurface *bmp = gfx_load_bmp_resource(0x228, NULL);
            if (!bmp)
                return 0;
            w->button_under = gfx_surface_create(bmp->width >> 1, bmp->height);
            if (!w->button_under) {
                gfx_surface_destroy(bmp);
                return 0;
            }
            w->restore_rect = w->rect;
            w->icon_height = 0xe;
            GfxRect br;
            /* no button yet, so this is an empty rect (as in the original) */
            min_button_rect(w, &br);
            gfx_blit(w->button_under, 0, 0, br.right - br.left + 1, br.bottom - br.top + 1, w->surface, br.left,
                     br.top);
            w->min_button = ctl_button_create(w, &br, 1, CTL_STYLE_TOGGLE | CTL_STYLE_BITMAP, bmp);
            if (!w->min_button) {
                gfx_surface_destroy(bmp);
                gfx_surface_destroy(w->button_under);
                w->button_under = NULL;
                return 0;
            }
            place_min_button(w);
        }
        set_title_rect(w);
        return 1;

    case WM_MSG_DESTROY: {
        for (WmWindow *c = w->children; c;) {
            WmWindow *next = c->next;
            wm_send(c, c, WM_MSG_DESTROY, 0, 0);
            c = next;
        }
        if (w->button_under) {
            gfx_surface_destroy(w->button_under);
            w->button_under = NULL;
        }
        WmWindow *list = list_unlink_window(wm_siblings(w), w);
        if (w->parent)
            w->parent->children = list;
        else
            g_root = list;
        wm_update_zorder();
        free_window(w);
        return 1;
    }

    case WM_MSG_PAINT: {
        GfxRect src, dst;
        if (a) {
            src = *(const GfxRect *)a;
        } else {
            src.left = src.top = 0;
            src.right = w->rect.right - w->rect.left;
            src.bottom = w->rect.bottom - w->rect.top;
        }
        dst = src;
        wm_local_to_screen(w, &dst);
        if (w->surface)
            gfx_blit(NULL, dst.left, dst.top, dst.right - dst.left + 1, dst.bottom - dst.top + 1, w->surface,
                     src.left, src.top);
        return 0;
    }

    case WM_MSG_ACTIVATE:
        if (!a || w == g_focus)
            return 1;
        wm_send(g_focus, g_focus, WM_MSG_ACTIVATE, 0, (intptr_t)w);
        if (!(w->flags & WM_F_NORAISE))
            wm_raise(w);
        if (!(w->flags & WM_F_NOFOCUS)) {
            g_focus = w;
            wm_flush_input();
        }
        g_last_active = w;
        return 1;

    case WM_MSG_MOUSEMOVE:
        if ((w->flags & WM_F_DRAGGABLE) && g_dragging) {
            GfxRect r = w->rect;
            rect_move_to(&r, (int)a - g_drag_x, (int)b - g_drag_y);
            wm_local_to_screen(w, &r);
            wm_set_rect(w, &r);
        }
        return 0;

    case WM_MSG_LBUTTONDOWN:
        if (w->flags & WM_F_DRAGGABLE) {
            g_dragging = true;
            wm_set_capture(w);
            g_drag_x = (int)a;
            g_drag_y = (int)b;
        }
        return 0;

    case WM_MSG_LBUTTONUP:
        if ((w->flags & WM_F_DRAGGABLE) && g_dragging) {
            wm_release_capture();
            g_dragging = false;
        }
        return 0;

    case WM_MSG_SETRECT:
        if (a && b) {
            GfxRect old = *(const GfxRect *)a;
            wm_parent_to_screen(w, &old);
            w->rect = *(const GfxRect *)b;
            if (wm_is_visible(w)) {
                /* both lists reflect the new layout; the original paints
                 * new, old, new */
                GfxRect nr = w->rect;
                wm_parent_to_screen(w, &nr);
                RgnNode *lo = rgn_build_visible(g_root, &old, NULL, true);
                RgnNode *ln = rgn_build_visible(g_root, &nr, NULL, true);
                rgn_paint(ln);
                rgn_paint(lo);
                rgn_paint(ln);
                rgn_free(ln);
                rgn_free(lo);
            }
        }
        return 0;

    case WM_MSG_CTL_PRESS: {
        /* from the minimize button (w is the button) */
        WmWindow *owner = w->parent;
        if (owner && (owner->flags & WM_F_MINIMIZABLE) && owner->min_button &&
            ctl_get_id(w) == ctl_get_id(owner->min_button)) {
            bool pressed = ctl_is_pressed(w);
            wm_set_minimized(owner, pressed);
            if (pressed)
                wm_lower(owner);
        }
        return 0;
    }

    case WM_MSG_KEYUP:
        /* Alt released without Tab in between opens the menu bar */
        if (a == 0x12 && g_last_key != 9 && w->menubar)
            menu_activate(w->menubar);
        g_last_key = (int)a;
        return 0;

    case WM_MSG_SHOW:
        if (a && (w->flags & WM_F_MINIMIZABLE))
            ctl_button_draw(w->min_button, (unsigned)wm_get_data(w->min_button, CTL_BUTTON_STATE) | 3);
        if ((w->flags & WM_F_MINIMIZABLE) && w->minimized && !a && w->parent)
            relayout_icons(w->parent, w->rect.left, w->rect.top);
        return 0;

    case WM_MSG_CLOSE:
        for (WmWindow *c = w->children; c;) {
            WmWindow *next = c->next;
            wm_send(c, c, WM_MSG_CLOSE, 0, 0);
            c = next;
        }
        return 1;

    default:
        return 0;
    }
}

/* -------------------------------------------------------------- init */

/* 0x4c1580 */
int wm_initialize(void)
{
    wm_queue_clear();
    g_prev_mouse = gfx_mouse_set_handler(mouse_handler);
    /* kb_set_handler(wm_key_event): the port polls through wm_hooks.key_poll
     * instead (game/startup.c) */
    g_prev_paint = gfx_set_paint_handler(paint_handler);
    rgn_pool_init();
    GfxRect r = {0, gfx_screen_width() - 1, 0, gfx_screen_height() - 1};
    g_root = wm_create_window(NULL, &r, wm_default_proc, WM_F_NOSURFACE | WM_F_NOFOCUS | WM_F_FRAMED, 0);
    if (!g_root) {
        gfx_mouse_set_handler(g_prev_mouse);
        gfx_set_paint_handler(g_prev_paint);
        return -1;
    }
    g_capture = NULL;
    wm_show(g_root, true);
    return 0;
}


/* 0x4c1722. The original only unhooks; the port also frees the windows. */
int wm_shutdown(void)
{
    gfx_mouse_set_handler(g_prev_mouse);
    gfx_set_paint_handler(g_prev_paint);
    if (g_root) {
        /* destroy the top-level windows normally (the root takes no
         * messages), then free the root */
        while (g_root->children) {
            WmWindow *c = g_root->children;
            wm_destroy(c);
            if (g_root->children == c) /* a proc that refuses: unlink it */
                g_root->children = c->next;
        }
        free_window(g_root);
    }
    g_root = g_focus = g_last_active = g_hover = g_capture = g_open_menu = NULL;
    menu_reset(); /* port: the menu module's pointers into the freed tree */
    wm_queue_clear();
    return 0;
}

int wm_suspend(void) { return 0; } /* 0x4c176b */
int wm_resume(void) { return 0; }  /* 0x4c177d */
