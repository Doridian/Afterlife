/*
 * Mouse and cursor ("Ms", 0x4b8cd0-0x4b9470).
 *
 * The cursor is the hardware (USER32) cursor: the game loads cursor
 * resources out of ALIFE.EXE by ID (LoadCursorA) and switches between them
 * with SetCursor; nothing is drawn into the 8-bit surfaces. The mouse module
 * tracks the position (clamped to a clip rectangle), the button state, and
 * queues button transitions (32 entries) so clicks between polls aren't
 * lost. main_wnd_proc (0x4cb7ad) feeds it from WM_MOUSEMOVE / WM_xBUTTONxxx.
 */
#ifndef AFTERLIFE_GFX_MOUSE_H
#define AFTERLIFE_GFX_MOUSE_H

#include <stdbool.h>

#include "gfx/surface.h"
#include "platform/platform.h"

#define GFX_MOUSE_LEFT 1
#define GFX_MOUSE_RIGHT 2
#define GFX_MOUSE_MIDDLE 4

/* 0x4b8cd0 ms_initialize: centers the pointer and clips it to the screen. */
void gfx_mouse_init(void);

/* 0x4b8d5e ms_get_state: the oldest queued button event if there is one,
 * else the current state. Position clamped to the clip rectangle. Returns
 * the GFX_MOUSE_* button mask. */
unsigned gfx_mouse_get_state(int *x, int *y);
unsigned gfx_mouse_get_buttons(void); /* 0x4b8e2a */

/* 0x4b8e3f ms_set_clip_rect: clip == NULL reads the previously saved clip
 * into *prev; otherwise saves the current one, sets `clip` (clamped to the
 * screen) and returns the old one in *prev. */
void gfx_mouse_set_clip_rect(GfxRect *prev, const GfxRect *clip);
void gfx_mouse_get_clip_rect(GfxRect *out); /* 0x4b8f1f */

/* 0x4b8f57 ms_set_handler: stores a mouse event callback (wm_initialize
 * installs 0x4c523a; event 1 = move, 2/3/4 = left/right/middle down,
 * 5/6/7 = up). Returns the previous one. */
typedef void (*GfxMouseHandler)(int event, int x, int y);
GfxMouseHandler gfx_mouse_set_handler(GfxMouseHandler fn);

void gfx_mouse_on_move(int x, int y);           /* 0x4b8f90 */
void gfx_mouse_on_button(unsigned button, bool down); /* 0x4b901d..0x4b90f9 */

void gfx_mouse_set_pos(int x, int y);           /* 0x4b92af */
void gfx_mouse_get_screen_pos(int *x, int *y);  /* 0x4b92ff (framebuffer coords here) */

void gfx_mouse_queue_reset(void);               /* 0x4b933b */
/* 0x4b935a ms_queue_pop: false if empty. */
bool gfx_mouse_queue_pop(int *x, int *y, unsigned *buttons);

/* --- cursors -------------------------------------------------------------- */

/* 0x4b9125 ms_load_cursor: cursor resource `id` from the user's ALIFE.EXE
 * (gfx/exe_res.h), or NULL (the system arrow) if it can't be loaded. */
PlatCursor *gfx_cursor_load(unsigned id);
void gfx_cursor_destroy(PlatCursor *c);   /* 0x4b9164 */
/* 0x4b9179 ms_show_cursor: forces the cursor visible (show != 0) or hidden,
 * whatever the ShowCursor count was. Returns whether it was visible. */
bool gfx_cursor_show(bool show);
PlatCursor *gfx_cursor_get(void);         /* 0x4b9274 */
void gfx_cursor_set(PlatCursor *c);       /* 0x4b928a */

/* Port-only: main_wnd_proc's mouse cases (WM_MOUSEMOVE, button messages
 * with SetCapture/ReleaseCapture, WM_SETCURSOR). Returns true if the event
 * was a mouse event. */
bool gfx_mouse_handle_event(const PlatEvent *ev);

#endif
