/*
 * Controls (0x4ba100-0x4c1568): task callbacks, idle, and the bitmap
 * button. See controls.h and docs/ui.md.
 */
#include "ui/controls.h"

#include <stddef.h>

#include "ui/controls_int.h"

#include "gfx/draw.h"
#include "gfx/font.h"
#include "platform/platform.h"

CtlHooks ctl_hooks;

int *ctl_pause_flag;           /* 0x5fa61c, a game global */
static int g_saved_pause;      /* 0x64e4ec */
WmWindow *ctl_msgbox_dlg;      /* 0x6726b0 */
uint32_t ctl_msgbox_deadline;  /* 0x6726b8 */
int ctl_msgbox_res;            /* 0x6726bc */
WmWindow *ctl_repeat_arrow;    /* 0x524790 */
int ctl_repeat_delay;          /* 0x524794 */
static uint32_t g_repeat_time; /* 0x6726d0 */
WmWindow *ctl_blink_field;     /* 0x6726c4 */
bool ctl_blink_phase;          /* 0x6726c8 */
static uint32_t g_blink_time;  /* 0x6726cc */

void ctl_modal_pump(void)
{
    if (ctl_hooks.pump) {
        ctl_hooks.pump();
        return;
    }
    wm_idle();
    ctl_idle();
    gfx_present();
}

/* ---------------------------------------------------------------- task */

/* 0x4ba100 */
int ctl_initialize(void) { return 0; }

/* 0x4ba130 (port: also frees the message box) */
int ctl_shutdown(void)
{
    ctl_msgbox_shutdown();
    ctl_blink_field = NULL;
    ctl_repeat_arrow = NULL;
    return 0;
}

/* 0x4ba14f */
int ctl_suspend(void)
{
    if (ctl_pause_flag) {
        g_saved_pause = *ctl_pause_flag;
        *ctl_pause_flag = 1;
    }
    return 0;
}

/* 0x4ba172 */
int ctl_resume(void)
{
    if (ctl_pause_flag)
        *ctl_pause_flag = g_saved_pause;
    return 0;
}

void ctl_set_pause_flag(int *flag) { ctl_pause_flag = flag; }
void ctl_set_blink_field(WmWindow *field) { ctl_blink_field = field; } /* 0x4c1568 */

/* 0x4bf68a */
void ctl_idle(void)
{
    uint32_t now;
    if (ctl_msgbox_deadline && ctl_msgbox_dlg && ctl_msgbox_deadline <= plat_ticks_ms()) {
        /* a timed message box: button 1 */
        ctl_msgbox_res = 1;
        wm_send(ctl_msgbox_dlg, ctl_msgbox_dlg, WM_MSG_CTL_RELEASE, 0, 0);
    }
    if (ctl_repeat_arrow && g_repeat_time + 100 < (now = plat_ticks_ms())) {
        g_repeat_time = now;
        if (ctl_repeat_delay == 0) {
            /* report the position once per change */
            unsigned style = (unsigned)wm_get_data(ctl_repeat_arrow, CTL_SCROLL_STYLE);
            intptr_t kind = wm_get_data(ctl_repeat_arrow, CTL_SCROLL_KIND);
            WmWindow *bar = ctl_repeat_arrow->parent;
            intptr_t pos = wm_get_data(bar, CTL_SCROLL_POS), last = wm_get_data(bar, CTL_SCROLL_LAST);
            if (last == pos)
                return;
            wm_set_data(bar, CTL_SCROLL_LAST, pos);
            unsigned msg = (style & CTL_SCROLL_VERTICAL) ? WM_MSG_VSCROLL : WM_MSG_HSCROLL;
            if (kind == CTL_SCROLL_FWD)
                wm_post(bar->parent, bar, msg, pos + 1, 1);
            else if (kind == CTL_SCROLL_BACK)
                wm_post(bar->parent, bar, msg, pos - 1, -1);
        } else {
            ctl_repeat_delay--;
        }
    }
    if (ctl_blink_field && g_blink_time + 250 < (now = plat_ticks_ms())) {
        ctl_blink_phase = !ctl_blink_phase;
        g_blink_time = now;
        ctl_text_draw_cursor(ctl_blink_field, ctl_blink_phase);
    }
}

/* 0x4bf8d8 */
WmWindow *ctl_create_child(WmWindow *parent, const GfxRect *r, WmProc proc, uint32_t flags, uint32_t extra_size)
{
    if (!proc)
        return NULL;
    GfxRect cr;
    if (r)
        cr = *r;
    else
        wm_get_rect(parent, &cr);
    return wm_create_window(parent, &cr, proc, flags, extra_size);
}

void ctl_destroy_child(WmWindow *w) { wm_destroy(w); } /* 0x4bf959 */

/* 0x4c1064 */
WmWindow *ctl_find_by_id(WmWindow *list, intptr_t id)
{
    for (; list; list = list->next) {
        if (list->children) {
            WmWindow *found = ctl_find_by_id(list->children, id);
            if (found)
                return found;
        }
        if (wm_get_data(list, 0) == id)
            return list;
    }
    return NULL;
}

/* -------------------------------------------------------- accessors */

/* 0x4bc5c1 */
uint32_t ctl_get_id(WmWindow *c) { return (uint32_t)wm_get_data(c, CTL_BUTTON_ID); }

/* 0x4bc5df */
bool ctl_is_pressed(WmWindow *c) { return (wm_get_data(c, CTL_BUTTON_STATE) & CTL_STATE_PRESSED) != 0; }

/* ------------------------------------------------------------ button */

/* 0x4ba18e */
static WmWindow *button_new(WmWindow *parent, GfxRect *r, uint32_t id, uint32_t style)
{
    if (!parent || !r)
        return NULL;
    WmWindow *b =
        wm_create_window(parent, r, ctl_button_proc, WM_F_NOSURFACE | WM_F_NOFOCUS | WM_F_NOCLIP | WM_F_CONTROL, 0x18);
    if (!b)
        return NULL;
    wm_set_data(b, CTL_BUTTON_ID, id);
    wm_set_data(b, CTL_BUTTON_STYLE, style);
    wm_set_data(b, CTL_BUTTON_STATE, 0);
    wm_set_data(b, CTL_BUTTON_BITMAP, 0);
    wm_set_data(b, CTL_BUTTON_FONT, 0);
    wm_set_data(b, CTL_BUTTON_COLORS, 0);
    return b;
}

/* 0x4ba2eb. The original takes a sixth argument that it ignores. */
WmWindow *ctl_button_create(WmWindow *parent, const GfxRect *r, uint32_t id, uint32_t style, GfxSurface *bitmap)
{
    if (!parent || !(style & CTL_STYLE_BITMAP) || !bitmap)
        return NULL;
    GfxRect br;
    if (!r) {
        /* one pixel taller than the bitmap, as in the original */
        br = (GfxRect){0, (bitmap->width >> 1) - 1, 0, bitmap->height};
    } else {
        int half = (r->right - r->left + 1) >> 1;
        br = (GfxRect){r->left, r->left + half - 1, r->top, r->bottom};
    }
    WmWindow *b = button_new(parent, &br, id, style);
    if (!b)
        return NULL;
    wm_set_data(b, CTL_BUTTON_BITMAP, (intptr_t)bitmap);
    ctl_button_draw(b, CTL_DRAW_BITMAP);
    wm_show(b, true);
    return b;
}

void ctl_button_destroy(WmWindow *b) { wm_destroy(b); } /* 0x4ba253 */

/* 0x4ba26a */
void ctl_button_enable(WmWindow *b, bool enable)
{
    if (!b)
        return;
    intptr_t s = wm_get_data(b, CTL_BUTTON_STATE);
    s = enable ? (s & ~(intptr_t)CTL_STATE_DISABLED) : (s | CTL_STATE_DISABLED);
    wm_set_data(b, CTL_BUTTON_STATE, s);
}

/* 0x4ba2c1 */
void ctl_button_show(WmWindow *b, bool show)
{
    if (b)
        wm_show(b, show);
}

/* 0x4ba3e8 */
void ctl_button_draw(WmWindow *b, unsigned how)
{
    if (!b)
        return;
    bool noclip = (b->flags & WM_F_NOCLIP) != 0;
    const char *label = wm_get_title(b);
    uint32_t font = (uint32_t)wm_get_data(b, CTL_BUTTON_FONT);
    uint32_t colors = (uint32_t)wm_get_data(b, CTL_BUTTON_COLORS);
    int pressed = (how & CTL_STATE_PRESSED) ? 1 : 0;
    bool normal = !(how & CTL_STATE_DISABLED);
    unsigned state = (unsigned)wm_get_data(b, CTL_BUTTON_STATE);
    const GfxSurface *bmp = (const GfxSurface *)wm_get_data(b, CTL_BUTTON_BITMAP);
    if (!bmp)
        return;
    int ox = 0, oy = 0;
    WmWindow *target = b;
    if (noclip) {
        /* drawn into the parent, at the button's place there */
        ox = b->rect.left;
        oy = b->rect.top;
        target = b->parent;
    }
    if (!target || !target->surface)
        return;
    GfxSurface *s = target->surface;
    int fw = bmp->width >> 1, fh = bmp->height;
    GfxRect r = {ox, ox + fw - 1, oy, oy + fh - 1};
    if (how & CTL_DRAW_BITMAP)
        gfx_blit(s, ox, oy, fw, fh, bmp, pressed ? bmp->width >> 1 : 0, 0);
    if ((how & CTL_STATE_FOCUS) || (state & CTL_STATE_FOCUS))
        gfx_dotted_frame(s, ox + 5, oy + 5, fw - 10, fh - 10, 0x13, 0xaa);
    if (label && font) {
        int maxw = (r.right - r.left) - 0xd;
        uint32_t prev = gfx_font_select(font);
        int tw = gfx_font_text_width(label);
        int th = gfx_font_height(font);
        int w = (maxw < 0 || tw <= maxw) ? tw : maxw; /* unsigned compare in the original */
        int x = ox + (((r.right - r.left) + 1 - w) >> 1);
        int y = oy + (int)((unsigned)((r.bottom - r.top + 1) - th) >> 1);
        GfxRect clip = {x + pressed, r.right - 7, y + pressed, y + pressed + th - 1};
        GfxRect area = clip;
        uint8_t saved[15];
        unsigned n = gfx_font_get_colormap(font, 15, saved);
        unsigned c[4];
        for (int i = 0; i < 4; i++)
            c[i] = normal ? (colors >> (8 * i)) & 0xff : 0x10;
        gfx_font_set_colors(font, 0, 4, c);
        gfx_font_draw_text(s, clip.left, clip.top, &clip, &area, label, GFX_TEXT_NOJUSTIFY);
        gfx_font_set_colormap(font, n, saved);
        gfx_font_select(prev);
    }
    if ((how & CTL_DRAW_FLUSH) && wm_is_visible(target))
        wm_redraw(target, &r);
}

/* 0x4ba74a */
const char *ctl_button_set_label(WmWindow *b, const char *text, uint32_t font, uint32_t colors, bool redraw)
{
    if (!b || !text)
        return NULL;
    wm_set_title(b, text);
    wm_set_data(b, CTL_BUTTON_FONT, font);
    wm_set_data(b, CTL_BUTTON_COLORS, colors);
    if (redraw)
        ctl_button_draw(b, CTL_DRAW_BITMAP | CTL_DRAW_FLUSH);
    return wm_get_title(b);
}

const char *ctl_button_get_label(WmWindow *b) { return wm_get_title(b); } /* 0x4ba7cd */

/* 0x4ba7e9 */
unsigned ctl_button_set_pressed(WmWindow *b, bool pressed, bool redraw)
{
    if (!b)
        return 0;
    unsigned old = (unsigned)wm_get_data(b, CTL_BUTTON_STATE);
    unsigned s = pressed ? (old | CTL_STATE_PRESSED) : (old & ~CTL_STATE_PRESSED);
    wm_set_data(b, CTL_BUTTON_STATE, s);
    if (redraw)
        ctl_button_draw(b, s | CTL_DRAW_BITMAP | CTL_DRAW_FLUSH);
    return old & CTL_STATE_PRESSED;
}

/* 0x4ba870 */
void ctl_button_set_focus(WmWindow *b, bool focus)
{
    if (!b)
        return;
    unsigned s = (unsigned)wm_get_data(b, CTL_BUTTON_STATE);
    s = focus ? (s | CTL_STATE_FOCUS) : (s & ~CTL_STATE_FOCUS);
    wm_set_data(b, CTL_BUTTON_STATE, s);
    ctl_button_draw(b, s | CTL_DRAW_BITMAP | CTL_DRAW_FLUSH);
}

/* 0x4ba8da */
unsigned ctl_button_has_focus(WmWindow *b)
{
    return b ? (unsigned)wm_get_data(b, CTL_BUTTON_STATE) & CTL_STATE_FOCUS : 0;
}

static void free_bitmap(WmWindow *b)
{
    GfxSurface *bmp = (GfxSurface *)wm_get_data(b, CTL_BUTTON_BITMAP);
    if (bmp) {
        gfx_surface_destroy(bmp);
        wm_set_data(b, CTL_BUTTON_BITMAP, 0);
    }
}

/* 0x4ba90c */
intptr_t ctl_button_proc(WmWindow *b, unsigned msg, intptr_t a, intptr_t c)
{
    unsigned state = (unsigned)wm_get_data(b, CTL_BUTTON_STATE);
    unsigned style = (unsigned)wm_get_data(b, CTL_BUTTON_STYLE);
    uint32_t id = (uint32_t)wm_get_data(b, CTL_BUTTON_ID);
    WmWindow *parent = b->parent;
    bool disabled = (state & CTL_STATE_DISABLED) != 0;
    switch (msg) {
    case WM_MSG_LBUTTONDOWN:
        if (disabled)
            return 0;
        wm_set_capture(b);
        if (!(style & CTL_STYLE_TOGGLE))
            state |= CTL_STATE_PRESSED;
        else
            state ^= CTL_STATE_PRESSED;
        wm_set_data(b, CTL_BUTTON_STATE, state);
        ctl_button_draw(b, (state & CTL_STATE_PRESSED) + CTL_DRAW_BITMAP + CTL_DRAW_FLUSH);
        wm_post(parent, b, WM_MSG_CTL_PRESS, id, (state & CTL_STATE_PRESSED) != 0);
        return 0;
    case WM_MSG_DESTROY:
        free_bitmap(b);
        return wm_default_proc(b, msg, a, c);
    case WM_MSG_CTL_RELEASE:
        free_bitmap(b);
        return 0;
    case WM_MSG_HOVER:
        wm_post(parent, b, WM_MSG_CTL_HOVER, id, 0);
        return 0;
    case WM_MSG_RBUTTONDOWN:
        if (!disabled)
            wm_post(parent, b, WM_MSG_CTL_RDOWN, id, 0);
        return 0;
    case WM_MSG_RBUTTONUP:
        if (!disabled)
            wm_post(parent, b, WM_MSG_CTL_RUP, id, 0);
        return 0;
    case WM_MSG_LBUTTONUP:
        if (disabled)
            return 0;
        wm_release_capture();
        if (style & (CTL_STYLE_STICKY | CTL_STYLE_TOGGLE)) {
            wm_post(parent, b, WM_MSG_CTL_CLICK, id, 0);
            return 0;
        }
        wm_set_data(b, CTL_BUTTON_STATE, state & ~CTL_STATE_PRESSED);
        ctl_button_draw(b, CTL_DRAW_BITMAP | CTL_DRAW_FLUSH);
        if (id == 0x230) { /* menu bar close */
            WmWindow *owner = parent ? parent->parent : NULL;
            wm_post(owner, owner, WM_MSG_CLOSE_BUTTON, 0, 0);
        } else if (id == 0x231) { /* menu bar minimize */
            WmWindow *owner = parent ? parent->parent : NULL;
            wm_post(owner, owner, WM_MSG_MINIMIZED, 1, 0);
        } else {
            wm_post(parent, b, WM_MSG_CTL_CLICK, id, 0);
        }
        return 0;
    default:
        return wm_default_proc(b, msg, a, c);
    }
}
