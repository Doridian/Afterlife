/*
 * Window manager ("wm", 0x4c1580-0x4c62cc).
 *
 * Every piece of the game's UI, the main view, panels, dialogs, buttons and
 * scrollbars, is a WmWindow in one tree under a screen-sized root window.
 * Each window normally owns an off-screen 8-bit surface the size of its
 * rectangle; game code draws into it and then asks the WM to repaint the
 * window (wm_redraw). Repainting builds a list of visible rectangle pieces
 * (src/ui/region.c), each owned by the front-most window covering it, and
 * sends every owner WM_MSG_PAINT, which by default blits that piece from
 * the window's surface to the screen.
 *
 * Messages go through one callback per window, WmProc. Unhandled messages
 * are passed on to wm_default_proc (0x4c3331), which implements create,
 * destroy, paint, activation, dragging, moving and minimizing. Messages
 * can also be posted to a 1000-entry queue that wm_idle dispatches one at a
 * time.
 *
 * Coordinates: a window's `rect` is in its parent's coordinates (the root
 * is at 0, 0, so for top-level windows these are screen coordinates).
 * "Local" coordinates are relative to the window's own top-left corner,
 * which is also its surface's origin. Rectangles are GfxRect, inclusive.
 *
 * See docs/ui.md.
 */
#ifndef AFTERLIFE_UI_WM_H
#define AFTERLIFE_UI_WM_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/surface.h"
#include "platform/platform.h"

typedef struct WmWindow WmWindow;
typedef struct RgnNode RgnNode;

/* Window callback. `w` is the window the message is about; for messages
 * dispatched to a window about one of its children (control notifications)
 * it's the child. Pointer arguments (rectangles) travel in a / b. */
typedef intptr_t (*WmProc)(WmWindow *w, unsigned msg, intptr_t a, intptr_t b);

/* --- window flags (+0x38) ----------------------------------------------- */
#define WM_F_EXISTS 0x00000001u      /* always set by wm_create_window */
#define WM_F_VISIBLE 0x00000002u     /* shown (wm_show) */
#define WM_F_TOPMOST 0x00000004u     /* kept in front of normal siblings */
#define WM_F_NOSURFACE 0x00000008u   /* no surface; paints nothing (root, controls) */
#define WM_F_CENTER 0x00000200u      /* creation rect is centred in the parent */
#define WM_F_NOFOCUS 0x00000400u     /* activation doesn't move the keyboard focus */
#define WM_F_NOCLIP 0x00000800u      /* doesn't occlude: drawn in the parent's surface */
#define WM_F_NOTITLE 0x00001000u     /* no 256-byte title buffer */
#define WM_F_CONTROL 0x00002000u     /* set on buttons (meaning unknown) */
#define WM_F_NORAISE 0x00008000u     /* activation doesn't raise */
#define WM_F_VSCROLL 0x00010000u     /* create a vertical scrollbar (controls module) */
#define WM_F_HSCROLL 0x00020000u     /* create a horizontal scrollbar */
#define WM_F_MENUBAR 0x00040000u     /* create a menu bar with close/minimize buttons */
#define WM_F_MINIMIZED 0x00080000u   /* set while minimized (to an icon) */
#define WM_F_HITCHILDREN 0x00100000u /* while captured, hit-test its children too */
#define WM_F_FRAMED 0x00200000u      /* client rect inset by the frame (wm_set_frame) */
#define WM_F_CLIPMOUSE 0x00400000u   /* a captured child clips the mouse to this client */
#define WM_F_MINIMIZABLE 0x00800000u /* has a minimize button at the top right */
#define WM_F_DRAGGABLE 0x02000000u   /* left-drag anywhere moves it */

/* --- messages ------------------------------------------------------------ */
#define WM_MSG_CREATE 0x01        /* return 1 to accept */
#define WM_MSG_DESTROY 0x02
#define WM_MSG_PAINT 0x03         /* a = const GfxRect * (local) or 0 = all */
#define WM_MSG_ACTIVATE 0x06      /* a = 1 activate / 0 deactivate, b = other window */
#define WM_MSG_MOUSEMOVE 0x08     /* a, b = local x, y */
#define WM_MSG_LBUTTONDOWN 0x09
#define WM_MSG_RBUTTONDOWN 0x0a
#define WM_MSG_MBUTTONDOWN 0x0b
#define WM_MSG_LBUTTONUP 0x0c
#define WM_MSG_RBUTTONUP 0x0d
#define WM_MSG_MBUTTONUP 0x0e
#define WM_MSG_SETRECT 0x0f       /* a = old rect, b = new rect (parent coords) */
#define WM_MSG_CTL_PRESS 0x10     /* control -> parent: a = id, b = pressed */
#define WM_MSG_CTL_RDOWN 0x11
#define WM_MSG_CTL_CLICK 0x14     /* a = id */
#define WM_MSG_CTL_RUP 0x15
#define WM_MSG_HSCROLL 0x1b       /* scrollbar -> parent: a = position, b = delta */
#define WM_MSG_VSCROLL 0x1c
#define WM_MSG_CHAR 0x1e          /* a = character, b = key */
#define WM_MSG_KEYDOWN 0x1f       /* a = key, b = character */
#define WM_MSG_KEYUP 0x20
#define WM_MSG_MINIMIZED 0x28     /* a = 1 minimized / 0 restored */
#define WM_MSG_HOVER 0x29         /* mouse rested 1.5 s */
#define WM_MSG_CTL_HOVER 0x2a
#define WM_MSG_LDBLCLK 0x2b
#define WM_MSG_RDBLCLK 0x2c
#define WM_MSG_MDBLCLK 0x2d
#define WM_MSG_MOUSEENTER 0x40    /* a = 1 entered / 0 left, b = other window */
#define WM_MSG_SHOW 0x45          /* a = shown */
#define WM_MSG_CAPTURE_LOST 0x47  /* a = new capture */
#define WM_MSG_SCREEN_RESIZED 0x200 /* broadcast, a = width, b = height */
#define WM_MSG_PRECREATE 0x208    /* a = GfxRect * creation rect (may be edited) */
#define WM_MSG_CTL_RELEASE 0x400  /* controls free their bitmaps */
#define WM_MSG_CLOSE_BUTTON 0x401 /* menu bar close button */
#define WM_MSG_CLOSE 0x406        /* quit request, sent to the root (WM_CLOSE) */
#define WM_MSG_MENUBAR 0x1001

/* Keyboard event types (kb_flush_one's records). */
#define WM_KEY_CHAR 1
#define WM_KEY_DOWN 2
#define WM_KEY_UP 3

struct WmWindow {
    WmWindow *next, *prev;     /* +0x00 siblings; the list head is in front */
    WmWindow *parent;          /* +0x08 */
    WmWindow *children;        /* +0x0c */
    GfxSurface *surface;       /* +0x10 NULL with WM_F_NOSURFACE */
    GfxRect rect;              /* +0x14 in parent coordinates */
    GfxRect client;            /* +0x24 local */
    uint32_t z;                /* +0x34 painting order, lower = in front */
    uint32_t flags;            /* +0x38 WM_F_* */
    uint32_t extra_size;       /* +0x3c bytes of extra data */
    WmProc proc;               /* +0x40 */
    RgnNode *invalid;          /* +0x44 pending wm_invalidate rectangles (screen) */
    WmWindow *top_bar;         /* +0x48 read by the menu bar layout */
    WmWindow *menubar;         /* +0x4c */
    WmWindow *vscroll;         /* +0x50 */
    WmWindow *hscroll;         /* +0x54 */
    uint32_t title_size;       /* +0x58 */
    char *title;               /* +0x5c also a control's label */
    int32_t icon_height;       /* +0x64 minimized height (14) */
    WmWindow *min_button;      /* +0x68 */
    GfxRect restore_rect;      /* +0x6c rect before minimizing */
    GfxRect button_rect;       /* +0x7c where the minimize button sits (local) */
    GfxRect title_rect;        /* +0x8c where wm_draw_title draws (local) */
    GfxSurface *button_under;  /* +0x9c the surface under the minimize button */
    bool minimized;            /* +0xa0 */
    uint32_t saved_flags;      /* +0xa1 flags before minimizing */
    WmWindow *close_button;    /* +0xa5 menu bar only */
    PlatCursor *cursor;        /* +0xa9 NULL: keep the current one */
    intptr_t *extra;           /* +0xad extra data, one slot per 4 bytes */
};

/* Optional hooks for modules that aren't ported yet. All may be NULL. */
typedef struct WmHooks {
    /* 0x4c6e66: menu bar (wm_create_window with WM_F_MENUBAR) */
    WmWindow *(*create_menubar)(WmWindow *owner, const GfxRect *r, unsigned style);
    /* 0x4c916a: Alt released over a window with a menu bar */
    void (*menu_activate)(WmWindow *menubar);
    /* 0x4bb012: scrollbar (WM_F_VSCROLL / WM_F_HSCROLL) */
    WmWindow *(*create_scrollbar)(WmWindow *owner, const GfxRect *r, unsigned id, unsigned style);
    /* 0x4ac69c(0): input arrived while the attract mode runs (input_live == 0). */
    void (*attract_stop)(void);
    /* kb_flush_one (0x4d0...): next keyboard event, false if none. */
    bool (*key_poll)(int *type, int *key, int *ch);
    /* snd_idle: called while building long region lists */
    void (*sound_idle)(void);
} WmHooks;
extern WmHooks wm_hooks;

/* --- init ------------------------------------------------------------------ */
/* 0x4c1580 wm_initialize: hooks the mouse and paint callbacks, sets up the
 * rectangle pool and the message queue, and creates and shows the root
 * window covering the screen. gfx_init must have run. 0 on success. The
 * caller registers wm_idle as a system task (the original passes
 * wm_initialize, wm_shutdown, two no-op suspend/resume, wm_idle). */
int wm_initialize(void);
int wm_shutdown(void);  /* 0x4c1722 */
int wm_suspend(void);   /* 0x4c176b, no-op */
int wm_resume(void);    /* 0x4c177d, no-op */
/* 0x4c178f: polls the mouse and keyboard, turns changes into window
 * messages, and dispatches one posted message. Call it from the pump. */
void wm_idle(void);

WmWindow *wm_root(void);   /* 0x672704 */
WmWindow *wm_focus(void);  /* 0x4c5eb9 */
void wm_set_focus(WmWindow *w); /* 0x4c62cc */
WmWindow *wm_last_activated(void); /* 0x67270c */
/* 0x672750: 0 while the attract mode runs; the original starts at 0 and
 * the first input calls hooks.attract_stop (0x4ac69c), which sets it. */
void wm_set_input_live(bool live);
bool wm_shift_down(void);  /* 0x674330 */

/* --- creation ---------------------------------------------------------------- */
/* 0x4c1bce: new hidden window under `parent` (NULL = root), behind its
 * siblings, at `rect` (parent coordinates; may be changed by WM_F_CENTER
 * or WM_MSG_PRECREATE, and receives the final rect). extra_size bytes of
 * extra data are zeroed. NULL if the proc refuses WM_MSG_CREATE. */
WmWindow *wm_create_window(WmWindow *parent, GfxRect *rect, WmProc proc, uint32_t flags, uint32_t extra_size);
/* 0x4c1ba2: a top-level window */
WmWindow *wm_create_toplevel(GfxRect *rect, WmProc proc, uint32_t flags);
/* 0x4c20f7: sends WM_MSG_DESTROY (children first, then frees it). Doesn't
 * repaint; hide the window first. */
void wm_destroy(WmWindow *w);

/* --- messages ------------------------------------------------------------------ */
/* 0x4c2830: calls w->proc(target, msg, a, b). w = NULL broadcasts
 * WM_MSG_SCREEN_RESIZED to every window; the root only takes WM_MSG_CLOSE.
 * Returns the proc's result, or -1. */
intptr_t wm_send(WmWindow *w, WmWindow *target, unsigned msg, intptr_t a, intptr_t b);
/* 0x4c27ca: sends to every window in a sibling list, children first */
void wm_broadcast(WmWindow *list, unsigned msg, intptr_t a, intptr_t b);
/* 0x4c5d03: queues wm_send(w, target, msg, a, b) for wm_idle. Ignored if
 * target or msg is 0. Overwrites the oldest entry when full. */
void wm_post(WmWindow *w, WmWindow *target, unsigned msg, intptr_t a, intptr_t b);
void wm_queue_clear(void); /* 0x4c5ce4 */
/* 0x4c614f: drops queued messages and keyboard/mouse events */
void wm_flush_input(void);
/* 0x4c3331: default handling, see docs/ui.md */
intptr_t wm_default_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b);
WmProc wm_set_proc(WmWindow *w, WmProc proc); /* 0x4c4700 (also 0x4c46bd) */
WmProc wm_get_proc(WmWindow *w);              /* 0x4c4743 */

/* --- visibility, stacking ---------------------------------------------------- */
/* 0x4c23da: shows or hides (showing restores a minimized window first).
 * Repaints and sends WM_MSG_SHOW if the effective visibility changed.
 * Returns whether it was effectively visible before. */
bool wm_show(WmWindow *w, bool show);
void wm_show_all(bool show);                   /* 0x4c2531 */
void wm_show_list(WmWindow *list, bool show);  /* 0x4c2551, recursive */
/* 0x4c59a2: the window and all its ancestors have WM_F_VISIBLE (NULL: true) */
bool wm_is_visible(const WmWindow *w);
bool wm_is_minimized(const WmWindow *w);       /* 0x4c59e3 */
void wm_raise(WmWindow *w);                    /* 0x4c2248 */
void wm_lower(WmWindow *w);                    /* 0x4c211d */
void wm_activate(WmWindow *w);                 /* 0x4c290a */
void wm_update_zorder(void);                   /* 0x4c5c2b */

/* --- painting -------------------------------------------------------------- */
/* 0x4c25c4: repaints `r` (local) of a visible window now; NULL = all */
void wm_redraw(WmWindow *w, const GfxRect *r);
/* 0x4c2690: remembers `r` (local, NULL = all) for wm_update */
void wm_invalidate(WmWindow *w, const GfxRect *r);
/* 0x4c2733: repaints and forgets the rectangles from wm_invalidate */
void wm_update(WmWindow *w);
/* 0x4c32f2 / 0x4c568f: repaints screen rectangle `r` (the WM_PAINT path) */
void wm_repaint_screen(const GfxRect *r);
/* 0x4c402c: copies `text` (NULL: keep) into the title and draws it into
 * the title area in font `font`, color `color` on `bg`, then repaints. */
void wm_draw_title(WmWindow *w, const char *text, uint32_t font, uint8_t color, uint8_t bg);
/* 0x4c5f19: frame pieces; the widths/heights of top, right, bottom, left
 * inset the client rect of WM_F_FRAMED windows. */
void wm_set_frame(GfxSurface *top_left, GfxSurface *top, GfxSurface *top_right, GfxSurface *right,
                  GfxSurface *bottom_right, GfxSurface *bottom, GfxSurface *bottom_left, GfxSurface *left,
                  GfxSurface *fill);

/* --- geometry ---------------------------------------------------------------- */
void wm_set_rect(WmWindow *w, const GfxRect *r);   /* 0x4c2aac, parent coords */
void wm_move(WmWindow *w, int x, int y);           /* 0x4c2b28 */
void wm_resize(WmWindow *w, int width, int height);/* 0x4c2a26 */
void wm_center(WmWindow *w);                       /* 0x4c294c, on the screen */
void wm_get_rect(WmWindow *w, GfxRect *out);       /* 0x4c5ece, parent coords */
void wm_get_local_rect(WmWindow *w, GfxRect *out); /* 0x4c2c9c, at 0, 0 */
/* 0x4c6096: the display changed size: resizes the root and broadcasts
 * WM_MSG_SCREEN_RESIZED. */
intptr_t wm_screen_resized(int width, int height);
void wm_local_to_screen(const WmWindow *w, GfxRect *r);   /* 0x4c2ff7 */
void wm_screen_to_local(const WmWindow *w, GfxRect *r);   /* 0x4c2f0e */
void wm_parent_to_screen(const WmWindow *w, GfxRect *r);  /* 0x4c5902 */
void wm_point_to_local(const WmWindow *w, int *x, int *y);  /* 0x4c3094 */
void wm_point_to_screen(const WmWindow *w, int *x, int *y); /* 0x4c310d */
/* 0x4c2fb3: local rect of `from` to the local coordinates of `to` */
void wm_map_rect(const WmWindow *from, const WmWindow *to, GfxRect *r);
void rect_move_to(GfxRect *r, int x, int y);               /* 0x4c317e */
WmWindow *wm_parent(const WmWindow *w);                    /* 0x4c582f */
WmWindow *wm_siblings(const WmWindow *w);                  /* 0x4c57eb */
bool wm_is_ancestor(const WmWindow *a, const WmWindow *w); /* 0x4c5856 (a == w counts) */
bool wm_is_sibling(const WmWindow *w, const WmWindow *other); /* 0x4c5896 */

/* --- input ------------------------------------------------------------------- */
/* 0x4c2cf7: window under screen point (x, y), or the capture */
WmWindow *wm_window_at(int x, int y);
/* 0x4c31e5: routes the mouse to `w` until wm_release_capture. Returns the
 * previous capture. */
WmWindow *wm_set_capture(WmWindow *w);
WmWindow *wm_capture(void);       /* 0x4c329d */
void wm_release_capture(void);    /* 0x4c32b2 */
/* 0x4c523a (public wrapper 0x4c617a): mouse event 1 move, 2/3/4 left/right/
 * middle down, 5/6/7 up, 8 hover, at screen (x, y). */
void wm_mouse_event(int event, int x, int y);
/* 0x4c56b5: keyboard callback: sends type 1/2/3 to the focus window as
 * WM_MSG_CHAR/KEYDOWN/KEYUP. Returns true if there is no focus window. */
bool wm_key_event(int type, int key);
/* 0x4c619d: a window's cursor (applied when the mouse is over it) */
PlatCursor *wm_set_cursor(WmWindow *w, PlatCursor *c);
void wm_set_default_cursor(PlatCursor *c); /* 0x4c620d */
/* 0x6727cc: the open menu (set by the menu module, 0x4c84c7); a left
 * release outside it goes to the menu's owner. */
void wm_set_open_menu(WmWindow *menu);

/* --- extra data and title ---------------------------------------------------------- */
void wm_set_data(WmWindow *w, uint32_t offset, intptr_t value); /* 0x4c5a00 */
intptr_t wm_get_data(const WmWindow *w, uint32_t offset);       /* 0x4c5a47 */
const char *wm_get_title(const WmWindow *w);                     /* 0x4c5aa8 */
int wm_title_capacity(const WmWindow *w);                        /* 0x4c5acf */
int wm_title_length(const WmWindow *w);                          /* 0x4c5af7 */
int wm_set_title_size(WmWindow *w, uint32_t size);               /* 0x4c5b27 */
int wm_set_title(WmWindow *w, const char *text);                 /* 0x4c5bc2 */
void wm_show_scrollbars(WmWindow *w, unsigned which, bool show); /* 0x4c5c9f: 1 v, 2 h */

/* --- minimizing ---------------------------------------------------------------- */
void wm_set_minimized(WmWindow *w, bool minimize); /* 0x4c3f4d */
bool wm_toggle_minimized(WmWindow *w);             /* 0x4c3bac */
void wm_set_icon_height(WmWindow *w, int h);       /* 0x4c4683 */

uint32_t wm_get_mode(void);        /* 0x4c6081 (0x67274c, never set) */
void wm_set_mode(uint32_t mode);   /* 0x4c606e */

#endif
