/*
 * Controls ("ctl", 0x4ba100-0x4c1568): widgets built on the window manager.
 *
 * Every control is a child WmWindow with extra data (wm_get_data /
 * wm_set_data); most have WM_F_NOSURFACE | WM_F_NOCLIP and draw into their
 * parent's surface. They report to the parent by posting messages
 * (wm_post) whose target is the control, so the parent's proc sees
 * (control, WM_MSG_CTL_*, id, ...).
 *
 * Ported here: the task callbacks, ctl_idle, the bitmap button
 * (0x4ba18e-0x4ba90c) and the id/state accessors. The scrollbar, text
 * field, list box, label and the message-box dialogs are mapped in
 * docs/ui.md but not ported yet.
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

#define CTL_STYLE_BITMAP 0x04  /* required by ctl_button_create */
#define CTL_STYLE_STICKY 0x20  /* stays pressed on release, reports CTL_CLICK */
#define CTL_STYLE_TOGGLE 0x80  /* each press toggles */

#define CTL_STATE_PRESSED 0x04
#define CTL_STATE_DISABLED 0x08
#define CTL_STATE_FOCUS 0x10   /* dotted focus frame */

/* ctl_button_draw `how` bits, on top of the CTL_STATE_* bits */
#define CTL_DRAW_BITMAP 0x01
#define CTL_DRAW_FLUSH 0x02

/* Optional hooks into modules that aren't ported yet. */
typedef struct CtlHooks {
    /* 0x4bc9c0: draw the text cursor of a text field (phase toggles) */
    void (*draw_text_cursor)(WmWindow *field, bool phase);
} CtlHooks;
extern CtlHooks ctl_hooks;

/* 0x4ba100 ctl_initialize. The original registers {ctl_initialize, no
 * shutdown, ctl_suspend, ctl_resume, ctl_idle} as a system task. */
int ctl_initialize(void);
int ctl_shutdown(void);  /* 0x4ba130 */
int ctl_suspend(void);   /* 0x4ba14f: pauses the game (0x5fa61c = 1) */
int ctl_resume(void);    /* 0x4ba172: restores the pause flag */
/* 0x4bf68a: message-box timeout, scroll-arrow auto-repeat (every 100 ms),
 * text cursor blink (every 250 ms) */
void ctl_idle(void);
/* Port-only: the game's pause flag (0x5fa61c) that suspend/resume save. */
void ctl_set_pause_flag(int *flag);
/* 0x4c1568: the text field whose cursor ctl_idle blinks (NULL: none) */
void ctl_set_blink_field(WmWindow *field);
/* Port-only setters for state the unported modules own: the scroll arrow
 * being held (0x524790) with its initial delay in idle ticks (0x524794),
 * and the timed message box (0x6726b0) with its deadline (0x6726b8). */
void ctl_set_repeat_arrow(WmWindow *arrow, int delay);
void ctl_set_timed_dialog(WmWindow *dialog, uint32_t deadline_ms);

uint32_t ctl_get_id(WmWindow *c);  /* 0x4bc5c1 (Ghidra's "__mkgmtime") */
bool ctl_is_pressed(WmWindow *c);  /* 0x4bc5df */

/* 0x4ba2eb: a bitmap button under `parent`. With r == NULL it sits at
 * 0, 0; otherwise at r's top-left, half as wide as r (r spans both
 * frames) and as tall as r. The button takes ownership of `bitmap`. It is
 * drawn (without flushing) and shown. NULL unless style has
 * CTL_STYLE_BITMAP. */
WmWindow *ctl_button_create(WmWindow *parent, const GfxRect *r, uint32_t id, uint32_t style, GfxSurface *bitmap);
void ctl_button_destroy(WmWindow *b);                  /* 0x4ba253 */
void ctl_button_enable(WmWindow *b, bool enable);      /* 0x4ba26a (no redraw) */
void ctl_button_show(WmWindow *b, bool show);          /* 0x4ba2c1 */
/* 0x4ba3e8: draws the frame (CTL_DRAW_BITMAP; pressed frame with
 * CTL_STATE_PRESSED), the focus frame, the centred label (grey when
 * CTL_STATE_DISABLED), and with CTL_DRAW_FLUSH repaints the screen. */
void ctl_button_draw(WmWindow *b, unsigned how);
/* 0x4ba74a: sets the label, font and colors (packed bytes), optionally
 * redrawing. Returns the label. */
const char *ctl_button_set_label(WmWindow *b, const char *text, uint32_t font, uint32_t colors, bool redraw);
const char *ctl_button_get_label(WmWindow *b);           /* 0x4ba7cd */
/* 0x4ba7e9: returns the previous pressed bit (CTL_STATE_PRESSED or 0) */
unsigned ctl_button_set_pressed(WmWindow *b, bool pressed, bool redraw);
void ctl_button_set_focus(WmWindow *b, bool focus);     /* 0x4ba870 (redraws) */
unsigned ctl_button_has_focus(WmWindow *b);             /* 0x4ba8da */
/* 0x4ba90c: the button's window proc */
intptr_t ctl_button_proc(WmWindow *b, unsigned msg, intptr_t a, intptr_t c);

#endif
