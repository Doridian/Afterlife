/*
 * Controls ("ctl", 0x4ba100-0x4c1568): widgets built on the window manager.
 *
 * Every control is a child WmWindow with extra data (wm_get_data /
 * wm_set_data); most have WM_F_NOSURFACE | WM_F_NOCLIP and draw into their
 * parent's surface. They report to the parent by posting messages
 * (wm_post) whose target is the control, so the parent's proc sees
 * (control, WM_MSG_CTL_*, id, ...).
 *
 * The whole module is ported, split by widget:
 *   controls.c   task callbacks, ctl_idle, the bitmap button
 *                (0x4ba18e-0x4ba90c), id/state accessors, child helpers
 *   scrollbar.c  scrollbars (0x4baca2-0x4bc482, 0x4c110a)
 *   textfield.c  single-line text field (0x4bc611-0x4bcb13) and the
 *                modal text prompt (0x4bf970)
 *   listbox.c    the selectable list with a scrollbar (0x4bcfbc-0x4be5d2)
 *                and the scrolling text viewer (0x4be691-0x4bf5e3)
 *   msgbox.c     the modal message box (0x4bfc69-0x4c0f99) and
 *                find-control-by-id (0x4c1064)
 * The menus (0x4c6bf9-0x4c97de) are in ui/menu.h.
 */
#ifndef AFTERLIFE_UI_CONTROLS_H
#define AFTERLIFE_UI_CONTROLS_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/surface.h"
#include "ui/wm.h"

/* Button extra data (0x18 bytes) */
#define CTL_BUTTON_ID 0x00     /* control id (common to all controls) */
#define CTL_BUTTON_STYLE 0x04  /* CTL_STYLE_* */
#define CTL_BUTTON_STATE 0x08  /* CTL_STATE_* (common to all controls) */
#define CTL_BUTTON_BITMAP 0x0c /* GfxSurface *: normal | pressed, side by side; owned */
#define CTL_BUTTON_FONT 0x10   /* label font id */
#define CTL_BUTTON_COLORS 0x14 /* label colormap entries 0-3, one per byte */

#define CTL_STYLE_BITMAP 0x04 /* required by ctl_button_create */
#define CTL_STYLE_STICKY 0x20 /* stays pressed on release, reports CTL_CLICK */
#define CTL_STYLE_TOGGLE 0x80 /* each press toggles */

#define CTL_STATE_PRESSED 0x04
#define CTL_STATE_DISABLED 0x08
#define CTL_STATE_FOCUS 0x10 /* dotted focus frame */

/* ctl_button_draw `how` bits, on top of the CTL_STATE_* bits */
#define CTL_DRAW_BITMAP 0x01
#define CTL_DRAW_FLUSH 0x02

/* Hooks into the game. All may be NULL. */
typedef struct CtlHooks {
    /* sys_pump (0x4c63f0), run by the modal loops (message box, text
     * prompt). NULL: wm_idle + ctl_idle + gfx_present. */
    void (*pump)(void);
    /* The message box's game-side preparation before it shows: 0x403c23
     * and sending message 0x0a to the window at 0x5bb200 if visible. */
    void (*modal_begin)(void);
    /* Whether a message box may time out: the game checks a menu item
     * (0x4c8355(0x6465e8)). NULL: always. */
    bool (*timeouts_enabled)(void);
} CtlHooks;
extern CtlHooks ctl_hooks;

/* 0x4ba100 ctl_initialize. The original registers {ctl_initialize, no
 * shutdown, ctl_suspend, ctl_resume, ctl_idle} as a system task. */
int ctl_initialize(void);
int ctl_shutdown(void); /* 0x4ba130 */
int ctl_suspend(void);  /* 0x4ba14f: pauses the game (0x5fa61c = 1) */
int ctl_resume(void);   /* 0x4ba172: restores the pause flag */
/* 0x4bf68a: message-box timeout, scroll-arrow auto-repeat (every 100 ms),
 * text cursor blink (every 250 ms) */
void ctl_idle(void);
/* Port-only: the game's pause flag (0x5fa61c) that suspend/resume save. */
void ctl_set_pause_flag(int *flag);
/* 0x4c1568: the text field whose cursor ctl_idle blinks (NULL: none) */
void ctl_set_blink_field(WmWindow *field);

uint32_t ctl_get_id(WmWindow *c); /* 0x4bc5c1 (Ghidra's "__mkgmtime") */
bool ctl_is_pressed(WmWindow *c); /* 0x4bc5df */

/* 0x4ba2eb: a bitmap button under `parent`. With r == NULL it sits at
 * 0, 0; otherwise at r's top-left, half as wide as r (r spans both
 * frames) and as tall as r. The button takes ownership of `bitmap`. It is
 * drawn (without flushing) and shown. NULL unless style has
 * CTL_STYLE_BITMAP. */
WmWindow *ctl_button_create(WmWindow *parent, const GfxRect *r, uint32_t id, uint32_t style, GfxSurface *bitmap);
void ctl_button_destroy(WmWindow *b);             /* 0x4ba253 */
void ctl_button_enable(WmWindow *b, bool enable); /* 0x4ba26a (no redraw) */
void ctl_button_show(WmWindow *b, bool show);     /* 0x4ba2c1 */
/* 0x4ba3e8: draws the frame (CTL_DRAW_BITMAP; pressed frame with
 * CTL_STATE_PRESSED), the focus frame, the centred label (grey when
 * CTL_STATE_DISABLED), and with CTL_DRAW_FLUSH repaints the screen. */
void ctl_button_draw(WmWindow *b, unsigned how);
/* 0x4ba74a: sets the label, font and colors (packed bytes), optionally
 * redrawing. Returns the label. */
const char *ctl_button_set_label(WmWindow *b, const char *text, uint32_t font, uint32_t colors, bool redraw);
const char *ctl_button_get_label(WmWindow *b); /* 0x4ba7cd */
/* 0x4ba7e9: returns the previous pressed bit (CTL_STATE_PRESSED or 0) */
unsigned ctl_button_set_pressed(WmWindow *b, bool pressed, bool redraw);
void ctl_button_set_focus(WmWindow *b, bool focus); /* 0x4ba870 (redraws) */
unsigned ctl_button_has_focus(WmWindow *b);         /* 0x4ba8da */
/* 0x4ba90c: the button's window proc */
intptr_t ctl_button_proc(WmWindow *b, unsigned msg, intptr_t a, intptr_t c);

/* 0x4bf8d8: a plain child window (rect NULL: the parent's rect); NULL
 * without a proc. 0x4bf959 destroys it. */
WmWindow *ctl_create_child(WmWindow *parent, const GfxRect *r, WmProc proc, uint32_t flags, uint32_t extra_size);
void ctl_destroy_child(WmWindow *w);
/* 0x4c1064: depth-first search of a sibling list (children first) for the
 * window whose extra data at 0 (the control id) is `id`. 0x4c10ea is the
 * same; the game uses it to find a dialog's button (the tutorial points
 * the mouse at it). */
WmWindow *ctl_find_by_id(WmWindow *list, intptr_t id);

/* --- scrollbar (scrollbar.c) --------------------------------------------- *
 * A track window (kind 0x7ffc) with three NOCLIP parts drawn into its
 * surface: the forward arrow (0x7ffe, down/right, created first so it is
 * the head of the child list), the back arrow (0x7fff, up/left) and the
 * thumb (0x7ffd). The owner hears about it through posted messages whose
 * target is the track: WM_MSG_V/HSCROLL from the arrows (a = position + /-
 * 1, b = delta, auto-repeated by ctl_idle every 100 ms after 0.5 s) and
 * WM_MSG_V/HTRACK from a track click or a thumb drag (a = position, b =
 * old - new). The scrollbar doesn't move itself: the owner calls
 * ctl_scroll_set_pos. */
#define CTL_SCROLL_ID 0x00    /* owner's id */
#define CTL_SCROLL_KIND 0x04  /* CTL_SCROLL_TRACK..CTL_SCROLL_BACK */
#define CTL_SCROLL_STYLE 0x08 /* CTL_SCROLL_VERTICAL | CTL_SCROLL_SHOW */
#define CTL_SCROLL_BITMAP 0x0c
#define CTL_SCROLL_POS 0x10
#define CTL_SCROLL_MIN 0x14
#define CTL_SCROLL_MAX 0x18
#define CTL_SCROLL_LAST 0x1c /* position last reported by the auto-repeat */

#define CTL_SCROLL_VERTICAL 0x01
#define CTL_SCROLL_SHOW 0x20 /* shown on creation */

#define CTL_SCROLL_TRACK 0x7ffc
#define CTL_SCROLL_THUMB 0x7ffd
#define CTL_SCROLL_FWD 0x7ffe  /* down / right: position + 1 */
#define CTL_SCROLL_BACK 0x7fff /* up / left: position - 1 */

/* 0x4baca2 / 0x4bad6d: the part bitmaps for all scrollbars, not owned
 * (the game loads 0x4000, 0x4001, 0x4006, 0x4004 and 0x4002, 0x4003,
 * 0x4006, 0x4005). Arrows and the thumb have two frames side by side
 * (normal, pressed); the track is tiled. Without them parts are 24 px and
 * nothing is drawn. Note that horizontal scrollbars take the thumb height
 * from the vertical thumb (the game uses the same bitmap for both). */
void ctl_scroll_set_vbitmaps(GfxSurface *up, GfxSurface *down, GfxSurface *thumb, GfxSurface *track);
void ctl_scroll_set_hbitmaps(GfxSurface *left, GfxSurface *right, GfxSurface *thumb, GfxSurface *track);
/* 0x4bae38 (never called): redraws one scrollbar's parts with other
 * bitmaps. */
void ctl_scroll_set_skin(WmWindow *bar, GfxSurface *back, GfxSurface *fwd, GfxSurface *thumb, GfxSurface *track);
/* 0x4bb012: a scrollbar under `owner`. r NULL: along the owner's right
 * (vertical) or bottom edge, below the menu bar / left of the vertical
 * scrollbar, shrinking the owner's client rect. Range 0..99, position 0.
 * Hidden unless style has CTL_SCROLL_SHOW. */
WmWindow *ctl_scroll_create(WmWindow *owner, const GfxRect *r, uint32_t id, unsigned style);
/* 0x4bb878: clamps to the range, places the thumb, redraws. */
void ctl_scroll_set_pos(WmWindow *bar, int pos);
/* 0x4bba71: an empty range (min == max) hides the thumb and becomes
 * min..min + 1. */
void ctl_scroll_set_range(WmWindow *bar, int min, int max);
int ctl_scroll_get_pos(WmWindow *bar); /* port-only: data 0x10 */
/* Port-only: a vertical scrollbar's width / a horizontal one's height
 * (the track bitmap's). */
int ctl_scroll_thickness(bool vertical);
/* 0x4bc3f8: the parts (any may be NULL) */
void ctl_scroll_parts(WmWindow *bar, WmWindow **back, WmWindow **fwd, WmWindow **thumb);
/* 0x4bc482: clamps a thumb rectangle between the arrows */
void ctl_scroll_clamp_thumb(WmWindow *bar, GfxRect *r);
/* 0x4c110a: draws the track and the parts into the track's surface; the
 * part `kind` is drawn pressed if `pressed`. */
void ctl_scroll_layout(WmWindow *bar, unsigned kind, bool pressed);
intptr_t ctl_scroll_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b); /* 0x4bbb4a */

/* --- text field (textfield.c) -------------------------------------------- *
 * A one-line edit box with its own surface. The text lives in the title
 * buffer (255 characters). Characters 0x20-0x7c are accepted except
 * @ { [ & ` = ] ^ _; backspace deletes the last one. */
#define CTL_TEXT_ID 0x00
#define CTL_TEXT_LEN 0x04
#define CTL_TEXT_BG 0x08
#define CTL_TEXT_FG1 0x0c
#define CTL_TEXT_FG2 0x10
#define CTL_TEXT_CURSOR 0x14 /* cursor glyph, '|' */
#define CTL_TEXT_CUR1 0x18
#define CTL_TEXT_CUR2 0x1c
#define CTL_TEXT_STYLE 0x20
#define CTL_TEXT_FONT 0x24
#define CTL_TEXT_CURSOR_X 0x28

#define CTL_TEXT_RCLICK 0x01   /* right click notifies the parent (CTL_TEXT_MSG_RCLICK) */
#define CTL_TEXT_UPPER 0x02    /* typed letters are upper-cased */
#define CTL_TEXT_READONLY 0x04 /* grey, no typing, no cursor */
#define CTL_TEXT_FOCUS 0x08    /* takes the blinking cursor on creation and keeps it */

/* to the parent (wm_send(parent, parent, ...)): a = field, b = id */
#define CTL_TEXT_MSG_ENTER 0x8000
#define CTL_TEXT_MSG_RCLICK 0x80c8
/* to the field */
#define CTL_TEXT_MSG_SET 0x8001 /* a = text or 0 (clear) */
#define CTL_TEXT_MSG_DRAW 0x8002
#define CTL_TEXT_MSG_GET_ID 0x8003
#define CTL_TEXT_MSG_READONLY 0x8004 /* a = read-only */

/* 0x4bc611: creates and shows a text field (raised). */
WmWindow *ctl_text_create(WmWindow *parent, GfxRect *r, uint32_t id, uint8_t bg, uint8_t fg1, uint8_t fg2, uint8_t cur1,
                          uint8_t cur2, uint32_t font, unsigned style);
const char *ctl_text_get(WmWindow *field);                                 /* port-only: the title */
void ctl_text_draw(WmWindow *field);                                       /* 0x4bc7cd, no repaint */
void ctl_text_draw_cursor(WmWindow *field, bool phase);                    /* 0x4bc9c0 (false erases) */
intptr_t ctl_text_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b); /* 0x4bcb13 */
/* 0x4bf970: modal prompt on bitmap 0x260 with a text field; runs the pump
 * until Enter. Returns a static buffer: the text, or `def` if it was left
 * empty and def isn't NULL. NULL on failure. (x or y < 0: centred.) */
const char *ctl_prompt(int x, int y, const char *title, const char *prompt, const char *def);
/* Port-only: ctl_prompt with `def` already in the field. */
const char *ctl_prompt_filled(int x, int y, const char *title, const char *prompt, const char *def);

/* --- list box (listbox.c) ------------------------------------------------ *
 * Up to 101 lines of 255 characters with a vertical scrollbar along the
 * right edge (26 px), one selected line drawn inverted. Each line carries
 * an intptr_t of item data. */
#define CTL_LIST_MAX 101
#define CTL_LIST_ALLOW_END 0x01 /* the line after the last can be selected */
#define CTL_LIST_FORWARD 0x02   /* characters and key-ups also go to the parent */
#define CTL_LIST_NOSELECT 0x04  /* no selection bar */
#define CTL_LIST_PARTIAL 0x08   /* keep the height (a partly visible last row) */

/* to the list (wm_send(list, list, msg, a, b)) */
#define CTL_LIST_ADD 0x8000      /* a = text, b = data */
#define CTL_LIST_DELETE 0x8001   /* a = index (-1: selected); 1 if deleted */
#define CTL_LIST_SET_DATA 0x8002 /* a = index, b = data */
#define CTL_LIST_SCROLL 0x8003   /* a = rows; redraws everything */
#define CTL_LIST_SELECT 0x8004   /* a = index */
#define CTL_LIST_ROW_RECT 0x8005 /* a = GfxRect *, b = visible row (-1: selected) */
#define CTL_LIST_GET_SEL 0x8006
#define CTL_LIST_GET_TOP 0x8007
#define CTL_LIST_GET_TEXT 0x8008 /* a = index (-1: selected), b = char[256]; length or -1 */
#define CTL_LIST_TEXT_LEN 0x8009
#define CTL_LIST_GET_DATA 0x800a
#define CTL_LIST_COUNT 0x800b
#define CTL_LIST_USER 0x800c     /* the creator's value */
#define CTL_LIST_SET_TEXT 0x800d /* a = text, word-wrapped into lines */
#define CTL_LIST_CLEAR 0x800e    /* a = redraw */
/* to the parent: a = list, b = selected index */
#define CTL_LIST_MSG_SELECTED 0x8064
#define CTL_LIST_MSG_DBLCLK 0x8065

/* 0x4bcfbc: NULL if the font can't be selected. */
WmWindow *ctl_list_create(WmWindow *parent, const GfxRect *r, unsigned style, intptr_t user, uint32_t scroll_id,
                          uint32_t font, uint8_t bg, uint8_t fg);
intptr_t ctl_list_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b); /* 0x4bd2c6 */

/* --- text viewer (listbox.c) --------------------------------------------- *
 * Read-only word-wrapped text with the window manager's vertical
 * scrollbar (WM_F_VSCROLL). */
#define CTL_VIEW_OWNS_TEXT 0x01 /* the text passed in is malloc'd and freed by the viewer */
#define CTL_VIEW_PARTIAL 0x02   /* a partly visible last line */

#define CTL_VIEW_SET_TEXT 0x8000 /* a = text (kept, not copied) */
#define CTL_VIEW_REDRAW 0x8001
#define CTL_VIEW_RESIZE 0x8002 /* a = width, b = height (0: keep) */

/* 0x4be691: buffer for the wrapped text, at least 32 KB. */
WmWindow *ctl_view_create(WmWindow *parent, const GfxRect *r, unsigned style, uint32_t id, uint32_t font, uint8_t bg,
                          uint8_t fg1, uint8_t fg2, uint32_t bufsize);
intptr_t ctl_view_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b); /* 0x4be8d4 */

/* --- message box (msgbox.c) ---------------------------------------------- */
/* 0x4c0d2e: loads bitmaps 0x260 (background) and 0x227 (buttons) and
 * creates the (hidden) dialog window with four buttons, ids 1000-1003.
 * Called by every message box; the game also calls it at startup. */
bool ctl_msgbox_init(void);
/* 0x4bfc69: modal message box at x, y (-1: centred) with a title, the
 * word-wrapped text and up to four buttons: "Yes\0No\0" (double NUL
 * terminated). A '&' marks the key of a button, a leading '~' the one Esc
 * and F1 choose (with a single button, that one). Runs the pump until a
 * button is chosen and returns its number (1-4), or 0 on failure. With
 * timeout_ms, closes by itself after that long, returning 1. */
int ctl_message_box(int x, int y, const char *title, const char *text, const char *buttons, uint32_t timeout_ms);
/* The two halves of ctl_message_box (port-only), for callers that run
 * their own loop: open shows it (false on failure), ctl_msgbox_result is
 * 0 until a button is chosen, close hides it and returns the result. */
bool ctl_msgbox_open(int x, int y, const char *title, const char *text, const char *buttons, uint32_t timeout_ms);
int ctl_msgbox_result(void);
int ctl_msgbox_close(void);
WmWindow *ctl_msgbox_window(void);                                           /* 0x6726b0 */
WmWindow *ctl_msgbox_button(int i);                                          /* 0x6726f0[i] */
intptr_t ctl_msgbox_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b); /* 0x4c06d7 */
/* Port-only: frees the message box window and bitmaps. */
void ctl_msgbox_shutdown(void);

#endif
