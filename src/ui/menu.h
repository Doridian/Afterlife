/*
 * Menus (0x4c6bf9-0x4c97de): the menu bar, pop-up menus and their items.
 *
 * Menus are windows too. A menu (bar or pop-up) is a container window
 * whose children are items; an item's child, if it has one, is its pop-up
 * menu, positioned next to (or below) the item. Every window of a menu
 * tree has menu_proc (0x4c84c7) and a style word in its extra data at 0:
 *
 *   container: MENU_IS_MENU | MENU_HORIZONTAL / MENU_FIXED / ... (MENU_BAR
 *              = 0x3c: the window manager's WM_F_MENUBAR bar)
 *   item:      MENU_IS_ITEM | MENU_TEXT or MENU_SEPARATOR | MENU_CHECKABLE,
 *              MENU_CHECKED, MENU_HILITE; +4 font, +8 data (non-text
 *              items), +0x0c the command id
 *
 * Choosing an item (mouse release on it, Enter, or its '&' key) closes the
 * menus and sends WM_MSG_MENU_COMMAND (0x1006) to the menu bar's owner
 * window, with w = the item and a = its id. In the game the owner is the
 * main window, whose proc (0x404023, installed by game_main) switches on
 * the id and sets game_command (0x650048) for game_main's loop, or acts
 * directly. See docs/ui.md.
 *
 * Pop-ups are laid out and get their surface the first time they open
 * (menu_open_popup); items added later aren't laid out.
 */
#ifndef AFTERLIFE_UI_MENU_H
#define AFTERLIFE_UI_MENU_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/surface.h"
#include "ui/wm.h"

/* container style */
#define MENU_HORIZONTAL 0x04     /* items side by side */
#define MENU_FIXED 0x08          /* keeps its rectangle (isn't sized or placed) */
#define MENU_BAR 0x3c            /* a menu bar (any of 0x3c set) */
#define MENU_IS_MENU 0x00400000u /* set by menu_create */
/* item style */
#define MENU_CHECKED 0x00000200u
#define MENU_CHECKABLE 0x00000400u /* draws a check mark (glyph 1) when MENU_CHECKED */
#define MENU_HILITE 0x00000800u
#define MENU_IS_ITEM 0x00800000u /* set by menu_add_item */
#define MENU_TEXT 0x01000000u    /* the label is the title; a tab separates a right-aligned part */
#define MENU_SEPARATOR 0x08000000u

#define MENU_DATA_STYLE 0x00
#define MENU_DATA_FONT 0x04
#define MENU_DATA_VALUE 0x08
#define MENU_DATA_ID 0x0c

/* 0x4c6c64: the nine frame bitmaps (not owned): corners, edges and the
 * fill; the game loads 0x4010-0x4018 in this order. */
void menu_set_skin(GfxSurface *top_left, GfxSurface *top, GfxSurface *top_right, GfxSurface *right,
                   GfxSurface *bottom_right, GfxSurface *bottom, GfxSurface *bottom_left, GfxSurface *left,
                   GfxSurface *fill);
/* 0x4c6e1f: the default font (the game uses 501); also measures the check
 * mark (glyph 1) and the sub-menu arrow (glyph 3). */
void menu_set_font(uint32_t font);
/* 0x4c6bf9 (never called): sound slots 700-702 for the menus. The port
 * keeps the ids only. */
void menu_set_sounds(uint32_t open, uint32_t close, uint32_t choose);

/* 0x4c6e66: a menu under `parent` (an item for a pop-up, the owner window
 * for a bar). style & MENU_BAR: as wide as the owner and one line high,
 * moving the owner's client top below it; otherwise `r` (NULL: 0, 0, 1,
 * 1, sized when it opens). Hidden. */
WmWindow *menu_create(WmWindow *parent, const GfxRect *r, uint32_t style);
/* 0x4c6fce: an item at the end of `menu`. With MENU_TEXT, `text` is the
 * label ('&' marks the key, '\t' starts a right-aligned part); otherwise
 * `value` is stored. font 0: the built-in font. NULL if menu or both
 * text and value are missing. */
WmWindow *menu_add_item(WmWindow *menu, uint32_t style, uint32_t id, const char *text, intptr_t value, uint32_t font);
void menu_destroy(WmWindow *menu);      /* 0x4c70c8 */
void menu_destroy_item(WmWindow *item); /* 0x4c70df */
/* 0x4c70f6: opens (lays out the first time, raises, shows) or closes a
 * menu; ignored for items. The game opens its bar this way. */
void menu_show(WmWindow *menu, bool open);
/* Port-only (the port window's relayout): the bar becomes `width` wide,
 * its right-hand buttons (close, minimize) move with the right edge, open
 * pop-ups close, and a bar that was laid out and shown is laid out
 * again. */
void menu_bar_resize(WmWindow *bar, int width);
/* 0x4c916a: menu NULL closes everything (the Alt / Enter path); a menu
 * bar activates its first item (the Alt key) */
void menu_activate(WmWindow *menu);
/* 0x4c8393: sets or clears MENU_CHECKED and redraws */
void menu_set_checked(WmWindow *item, bool checked);
bool menu_is_checked(WmWindow *item); /* 0x4c8355 */
/* 0x4c84a2: the item with command id `id` anywhere under the root */
WmWindow *menu_find_item(uint32_t id);
WmWindow *menu_find_item_in(WmWindow *list, uint32_t id); /* 0x4c8408 */
/* 0x4c7ccd: the menu bar a menu window belongs to (NULL if none) */
WmWindow *menu_bar_of(WmWindow *w);
WmWindow *menu_hot_item(void);                                         /* 0x6727c4: the highlighted item */
intptr_t menu_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b); /* 0x4c84c7 */
/* Port-only: forgets the module's window pointers (call after the windows
 * are destroyed, e.g. at shutdown). */
void menu_reset(void);

#endif
