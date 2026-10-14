/*
 * Keyboard queue and the main window procedure. See input.h and
 * docs/startup.md.
 */
#include "game/input.h"

#include "game/startup.h"
#include "gfx/mouse.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "ui/wm.h"

/* ======================================================================== */
/* Keyboard                                                                  */
/* ======================================================================== */

static KbEvent g_queue[KB_QUEUE_LEN]; /* kb_queue 0x548628 */
static int g_write;                   /* kb_queue_write 0x548600 */
static int g_read;                    /* kb_queue_read 0x54860c */
static int g_count;                   /* kb_queue_count 0x548604 */
static KbEvent g_out;                 /* kb_event_out 0x548618 */
static KbHandler g_handler;           /* kb_handler 0x548610 */

/* 0x4d05e0 */
int kb_initialize(void)
{
    kb_queue_reset();
    return 0;
}

int kb_shutdown(void) { return 0; } /* 0x4d05f7 */
int kb_suspend(void) { return 0; }  /* 0x4d0609 */
int kb_resume(void) { return 0; }   /* 0x4d061b */

/* 0x4d062d */
KbHandler kb_set_handler(KbHandler fn)
{
    KbHandler old = g_handler;
    g_handler = fn;
    return old;
}

/* 0x4d0653 */
void kb_restore_handler(KbHandler fn) { g_handler = fn; }

/* 0x4d0666 */
const KbEvent *kb_get_event(void) { return kb_queue_pop(); }

/* 0x4d067b */
int kb_pending(void) { return g_count; }

/* 0x4d0690 */
uint8_t kb_wait_key(void)
{
    while (g_count == 0) {
        if (!sys_dispatch_message())
            plat_yield(); /* port: the original spins on PeekMessage */
    }
    return kb_queue_pop()->ch;
}

/* 0x4d06f1: the original passes wParam as both vk and ch. */
void kb_on_key_down(int vk) { kb_queue_push(KB_EV_DOWN, vk, (uint8_t)vk); }
/* 0x4d070e */
void kb_on_key_up(int vk) { kb_queue_push(KB_EV_UP, vk, (uint8_t)vk); }
/* 0x4d072b */
void kb_on_char(uint8_t ch) { kb_queue_push(KB_EV_CHAR, 0, ch); }

/* 0x4d0746 kb_debug_mda (not ported, no callers): draws the queue's read
 * and write indices as 'I' / 'O' on a monochrome (MDA) debug monitor. */

/* 0x4d07b0 */
void kb_queue_push(int type, int vk, uint8_t ch)
{
    if (g_count >= KB_QUEUE_LEN)
        return;
    g_count++;
    g_queue[g_write] = (KbEvent){type, vk, ch};
    if (++g_write >= KB_QUEUE_LEN)
        g_write = 0;
}

/* 0x4d0820 */
const KbEvent *kb_queue_pop(void)
{
    if (g_count == 0)
        return NULL;
    g_count--;
    g_out = g_queue[g_read];
    if (++g_read == KB_QUEUE_LEN)
        g_read = 0;
    return &g_out;
}

/* 0x4d088e */
void kb_queue_reset(void) { g_write = g_read = g_count = 0; }

/* 0x4d08b7 */
void kb_post(int type, int vk, uint8_t ch) { kb_queue_push(type, vk, ch); }

/* ======================================================================== */
/* main_wnd_proc (0x4cb7ad)                                                  */
/* ======================================================================== */

static int g_app_active = 1;  /* app_active 0x672934 */
static int g_app_minimized;   /* app_minimized 0x67292c */
static int g_app_closing;     /* app_closing 0x672938 */
static bool g_close_requested;

bool app_close_requested(void) { return g_close_requested; }

/* WM_ACTIVATE. The original also moves a desktop-sized window to the
 * bottom of the Z order when deactivated (topmost when activated) and,
 * with display flags exactly 4 (never), switches the display mode. */
static void on_activate(bool active)
{
    if (!active) {
        if (g_app_active) {
            g_app_active = 0;
            sys_suspend();
        }
        return;
    }
    if (g_app_minimized)
        return;
    gfx_palette_set(NULL, GFX_PAL_FIRST, GFX_PAL_COUNT); /* re-realize the palette */
    if (!g_app_active) {
        g_app_active = 1;
        sys_resume();
    }
}

/* Message routing of the original (hidden 1x1 window: DefWindowProc):
 *   WM_DESTROY (2)          PostQuitMessage(0)
 *   WM_PAINT (0xf)          BeginPaint; di_paint_handler(paint rect); EndPaint
 *   WM_ACTIVATE (6)         see on_activate
 *   WM_SETCURSOR (0x20)     ms_set_cursor(current); return TRUE
 *   WM_CLOSE (0x10)         once at a time: ShowWindow(SW_SHOWNORMAL), then
 *                           message 0x406 to the root window (quit dialog);
 *                           never DestroyWindow here
 *   WM_WINDOWPOSCHANGING    flags |= SWP_NOSIZE (the window can't be resized)
 *   WM_DISPLAYCHANGE (0x7e) new desktop size; re-create the main window
 *                           (0x4cd73a) and tell the window manager (0x4c6096)
 *   WM_SYSCOMMAND (0x112)   SC_MINIMIZE: minimized, suspend; SC_RESTORE:
 *                           not minimized (resume comes with WM_ACTIVATE);
 *                           SC_MOVE and SC_SCREENSAVE swallowed
 *   WM_KEYDOWN/SYSKEYDOWN   kb_on_key_down(wParam)
 *   WM_KEYUP/SYSKEYUP       kb_on_key_up(wParam)
 *   WM_CHAR (0x102)         kb_on_char(wParam); WM_SYSCHAR is not handled
 *   WM_MOUSEMOVE (0x200)    ms_on_move(x, y)
 *   WM_L/R/MBUTTONDOWN      SetCapture; ms_on_*_down
 *   WM_L/R/MBUTTONUP        ReleaseCapture; ms_on_*_up
 *   WM_QUERYNEWPALETTE,
 *   WM_PALETTECHANGED       (not from itself) re-realize the identity palette
 * The window class has no CS_DBLCLKS, so there are no double-click
 * messages: the window manager's mouse hook (0x4c523a) makes its own from
 * two button-ups of the same button within 500 ms and 4 pixels. */
bool main_wnd_proc(const PlatEvent *ev)
{
    switch (ev->type) {
    case PLAT_EV_EXPOSE:
        gfx_repaint();
        return true;
    case PLAT_EV_ACTIVATE:
        on_activate(ev->active);
        return true;
    case PLAT_EV_MINIMIZE:
        g_app_minimized = 1;
        if (g_app_active) {
            g_app_active = 0;
            sys_suspend();
        }
        return true;
    case PLAT_EV_RESTORE:
        g_app_minimized = 0;
        return true;
    case PLAT_EV_CLOSE:
    case PLAT_EV_QUIT:
        if (!g_app_closing) {
            g_app_closing = 1;
            /* The original also ShowWindow(SW_SHOWNORMAL)s first. Before
             * the window manager is up (port: or without it, as in the
             * tests) the port only raises its close flag. */
            if (wm_root())
                wm_send(wm_root(), wm_root(), WM_MSG_CLOSE, 0, 0);
            else
                g_close_requested = true;
            g_app_closing = 0;
        }
        return true;
    case PLAT_EV_KEY_DOWN:
        kb_on_key_down(ev->vk);
        return true;
    case PLAT_EV_KEY_UP:
        kb_on_key_up(ev->vk);
        return true;
    case PLAT_EV_CHAR:
        kb_on_char(ev->ch);
        return true;
    case PLAT_EV_MOUSE_MOVE:
    case PLAT_EV_MOUSE_DOWN:
    case PLAT_EV_MOUSE_UP:
        /* Capture is implicit in SDL while a button is held. */
        return gfx_mouse_handle_event(ev);
    default:
        return false;
    }
}
