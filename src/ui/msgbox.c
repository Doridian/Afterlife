/*
 * The modal message box (0x4bfc69-0x4c0f99). See controls.h and docs/ui.md.
 *
 * There is one dialog window (0x6726b0), created on first use by
 * ctl_msgbox_init with four bitmap buttons (ids 1000-1003) and reused.
 * Its extra data: 0x00 button count, 0x04-0x10 the buttons' keys
 * (upper-cased character after '&'), 0x14-0x20 the buttons, 0x24 the
 * index of the button with the focus frame.
 */
#include <ctype.h>
#include <string.h>

#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/mouse.h"
#include "platform/platform.h"
#include "ui/controls.h"
#include "ui/controls_int.h"

#define MB_COUNT 0x00
#define MB_KEY(i) (0x04 + (i) * 4)
#define MB_BUTTON(i) (0x14 + (i) * 4)
#define MB_FOCUS 0x24
#define MB_FONT 500
#define MB_ID0 1000

static GfxSurface *g_bg;         /* 0x6726d8: bitmap 0x260 */
static GfxSurface *g_btn_bmp[4]; /* 0x6726e0: bitmaps 0x227, owned by the buttons */
static WmWindow *g_btn[4];       /* 0x6726f0 */
static WmWindow *g_prev_focus;   /* 0x6726b4 */
static uint8_t g_cancel;         /* 0x6726c0: Esc / F1 button, 0xff = none */
static WmWindow *g_pressed;      /* 0x672700: button pressed by a key */
static bool g_prev_cursor_shown;
static uint32_t g_prev_font;

WmWindow *ctl_msgbox_window(void) { return ctl_msgbox_dlg; }
WmWindow *ctl_msgbox_button(int i) { return i >= 0 && i < 4 ? g_btn[i] : NULL; }
int ctl_msgbox_result(void) { return ctl_msgbox_res; }

/* 0x4c0d2e */
bool ctl_msgbox_init(void)
{
    if (!g_bg && !(g_bg = gfx_load_bmp_resource(0x260, NULL)))
        return false;
    for (int i = 0; i < 4; i++)
        if (!g_btn_bmp[i] && !(g_btn_bmp[i] = gfx_load_bmp_resource(0x227, NULL)))
            return false;
    if (!ctl_msgbox_dlg) {
        GfxRect r = {0, 0x1c1, 0, 0x10d};
        ctl_msgbox_dlg = wm_create_window(NULL, &r, ctl_msgbox_proc, WM_F_TOPMOST | WM_F_HITCHILDREN, 0x28);
        if (!ctl_msgbox_dlg)
            return false;
    }
    GfxRect br = {0, g_btn_bmp[0]->width - 1, 0, g_btn_bmp[0]->height - 1};
    for (int i = 0; i < 4; i++) {
        if (!g_btn[i]) {
            g_btn[i] = ctl_button_create(ctl_msgbox_dlg, &br, MB_ID0 + i, CTL_STYLE_BITMAP, g_btn_bmp[i]);
            if (!g_btn[i])
                return false;
        }
        ctl_button_set_focus(g_btn[i], false);
        ctl_button_show(g_btn[i], false);
        br.top += g_btn_bmp[i]->height;
        br.bottom += g_btn_bmp[i]->height;
    }
    gfx_blit(ctl_msgbox_dlg->surface, 0, 0, g_bg->width, g_bg->height, g_bg, 0, 0);
    wm_redraw(ctl_msgbox_dlg, NULL);
    wm_show(ctl_msgbox_dlg, false);
    return true;
}

void ctl_msgbox_shutdown(void)
{
    if (ctl_msgbox_dlg) {
        wm_show(ctl_msgbox_dlg, false);
        wm_destroy(ctl_msgbox_dlg); /* the buttons free their bitmaps */
    }
    ctl_msgbox_dlg = NULL;
    for (int i = 0; i < 4; i++) {
        g_btn[i] = NULL;
        g_btn_bmp[i] = NULL;
    }
    if (g_bg)
        gfx_surface_destroy(g_bg);
    g_bg = NULL;
    g_prev_focus = g_pressed = NULL;
}

/* 0x4bfc69, up to the modal loop */
bool ctl_msgbox_open(int x, int y, const char *title, const char *text, const char *buttons, uint32_t timeout_ms)
{
    if (!ctl_msgbox_init())
        return false;
    ctl_msgbox_res = 0;
    if (!text || !buttons)
        return false;
    WmWindow *dlg = ctl_msgbox_dlg;
    GfxSurface *s = dlg->surface;

    /* size from the text: 173..303 px wide (a multiple of 10 plus the
     * remainder), 48..150 px tall */
    g_prev_font = gfx_font_select(MB_FONT);
    unsigned fh = (unsigned)gfx_font_height(MB_FONT);
    unsigned tw = (unsigned)gfx_font_text_width(text);
    unsigned paras = 0;
    for (const char *p = text; *p; p++)
        if (p[0] == '\n' && p[1] == '\n')
            paras++;
    unsigned w = tw < 0xae ? 0xad : tw;
    if (w > 0x12e)
        w = 0x12f;
    w += w % 10;
    unsigned h = (tw / w + 2) * (fh + 1) + (fh + 1) * paras;
    if (h < 0x31)
        h = 0x30;
    if (h > 0x95)
        h = 0x96;
    if (fh)
        h += h % fh;
    int n = 0;
    for (const char *p = buttons;;) {
        p = strchr(p, 0) + 1;
        n++;
        if (!*p || n == 4) /* port: at most four buttons */
            break;
    }
    int bw = g_btn_bmp[0]->width, bh = g_btn_bmp[0]->height;
    int row_w = n * (bw >> 1) + (n * 5 - 5) * 2;
    unsigned dw_min = (unsigned)row_w + 0x22 > w ? (unsigned)row_w + 0x22 : w;
    int xs = (uint8_t)((dw_min - 0xad) / 10), ys = (uint8_t)((h - 0x30) / 10);

    GfxRect r = {x, g_bg->width + x - 1 + xs * 10, y, g_bg->height + y - 1 + ys * 10};
    int dw = r.right - r.left + 1, dh = r.bottom - r.top + 1;
    wm_set_rect(dlg, &r);
    if (dw >= 0x1c3 || dh >= 0x10f) {
        gfx_font_select(g_prev_font);
        return false;
    }
    for (int i = 0; i < 10; i++)
        wm_set_data(dlg, (uint32_t)i * 4, 0);

    /* the background: the bitmap's quarters in the corners, stretched by
     * repeating a 10 px strip from its middle */
    gfx_fill_rect(s, 0, 0, dw, dh, 0x13);
    int hw = g_bg->width >> 1, hh = g_bg->height >> 1;
    gfx_blit(s, 0, 0, hw, hh, g_bg, 0, 0);
    gfx_blit(s, dw - hw, 0, hw, hh, g_bg, hw, 0);
    gfx_blit(s, 0, dh - hh, hw, hh, g_bg, 0, hh);
    gfx_blit(s, dw - hw, dh - hh, hw, hh, g_bg, hw, hh);
    for (int i = 0, at = hw; i < xs; i++, at += 10) {
        gfx_blit(s, at, 0, 10, hh, g_bg, hw - 9, 0);
        gfx_blit(s, at, dh - hh, 10, hh, g_bg, hw - 9, hh);
    }
    for (int i = 0, at = hh; i < ys; i++, at += 10) {
        gfx_blit(s, 0, at, hw, 10, g_bg, 0, hh);
        gfx_blit(s, dw - hw, at, hw, 10, g_bg, hw, hh);
    }

    gfx_font_select(MB_FONT);
    unsigned colors[2] = {0x1a, 0x1a};
    gfx_font_set_colors(MB_FONT, 1, 2, colors);
    if (title) {
        GfxRect tr = {5, 0xda, 4, 0xc};
        gfx_font_draw_text(s, 5, 4, &tr, &tr, title, 0);
    }
    GfxRect txt = {0x18, xs * 10 + 0xc4, 0x18, ys * 10 + 0x47};
    gfx_font_draw_wrapped(s, &txt, text, MB_FONT, 0x1a, 0x13);

    /* the buttons, centred in a row */
    int bx = (dw - row_w) >> 1, by = ((0x23 - bh) >> 1) + 0x5a + ys * 10;
    g_cancel = n == 1 ? 0 : 0xff;
    const char *p = buttons;
    for (int i = 0; i < n; i++) {
        wm_move(g_btn[i], bx, by);
        ctl_button_show(g_btn[i], true);
        if (*p == '~') {
            p++;
            g_cancel = (uint8_t)i;
        }
        char label[60];
        strncpy(label, p, sizeof label - 1);
        label[sizeof label - 1] = 0;
        const char *amp = strchr(p, '&');
        if (amp)
            wm_set_data(dlg, MB_KEY(i), toupper((unsigned char)amp[1]));
        ctl_button_set_label(g_btn[i], label, MB_FONT, 0x1a, true);
        wm_set_data(dlg, MB_BUTTON(i), (intptr_t)g_btn[i]);
        if (i == 0) {
            ctl_button_set_focus(g_btn[0], true);
            wm_set_data(dlg, MB_FOCUS, 0);
        }
        p = strchr(p, 0) + 1;
        bx += (bw >> 1) + 10;
    }
    wm_set_data(dlg, MB_COUNT, n);

    if (ctl_pause_flag)
        *ctl_pause_flag = 1;
    if (ctl_hooks.modal_begin)
        ctl_hooks.modal_begin();
    wm_redraw(dlg, NULL);
    if (x == -1 || y == -1)
        wm_center(dlg);
    wm_set_capture(dlg);
    g_prev_focus = wm_focus();
    wm_raise(dlg);
    wm_show(dlg, true);
    wm_activate(dlg);
    /* ms_show_cursor_get(1) here, undone on closing: the box is usable
     * with the mouse even while the game has the cursor hidden (e.g. a
     * load between the title and the game) */
    g_prev_cursor_shown = gfx_cursor_show(true);
    if (timeout_ms && (!ctl_hooks.timeouts_enabled || ctl_hooks.timeouts_enabled()))
        ctl_msgbox_deadline = plat_ticks_ms() + timeout_ms;
    else
        ctl_msgbox_deadline = 0;
    return true;
}

/* 0x4bfc69, after the modal loop */
int ctl_msgbox_close(void)
{
    ctl_msgbox_deadline = 0;
    wm_release_capture();
    wm_show(ctl_msgbox_dlg, false);
    if (!g_prev_cursor_shown)
        gfx_cursor_show(false);
    if (g_prev_focus)
        wm_activate(g_prev_focus);
    g_prev_focus = NULL;
    if (ctl_pause_flag)
        *ctl_pause_flag = 0;
    gfx_font_select(g_prev_font);
    return ctl_msgbox_res;
}

/* 0x4bfc69 */
int ctl_message_box(int x, int y, const char *title, const char *text, const char *buttons, uint32_t timeout_ms)
{
    if (!ctl_msgbox_open(x, y, title, text, buttons, timeout_ms))
        return 0;
    do
        ctl_modal_pump();
    while (ctl_msgbox_res == 0);
    return ctl_msgbox_close();
}

/* 0x4c0f99: moves the focus frame to the next (or previous) button */
static void cycle_focus(WmWindow *dlg, bool forward)
{
    unsigned cur = (unsigned)wm_get_data(dlg, MB_FOCUS), n = (unsigned)wm_get_data(dlg, MB_COUNT);
    ctl_button_set_focus((WmWindow *)wm_get_data(dlg, MB_BUTTON(cur)), false);
    unsigned next;
    if (!forward) {
        next = cur - 1;
        if (next >= n)
            next = n - 1;
    } else {
        next = cur + 1;
        if (next >= n)
            next = 0;
    }
    ctl_button_set_focus((WmWindow *)wm_get_data(dlg, MB_BUTTON(next)), true);
    wm_set_data(dlg, MB_FOCUS, next);
}

static void key_press(WmWindow *b)
{
    ctl_button_set_pressed(b, true, true);
    g_pressed = b;
}

static void key_release(WmWindow *dlg, WmWindow *b, unsigned idx, intptr_t how)
{
    ctl_button_set_pressed(b, false, true);
    g_pressed = NULL;
    wm_post(dlg, b, WM_MSG_CTL_CLICK, idx + MB_ID0, how);
}

/* 0x4c06d7. Messages from the buttons arrive with w = the button. */
intptr_t ctl_msgbox_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_DESTROY:
        ctl_msgbox_deadline = 0;
        wm_release_capture();
        ctl_msgbox_dlg = NULL;
        wm_show(w, false);
        if (g_prev_focus && g_prev_focus != w)
            wm_activate(g_prev_focus);
        g_prev_focus = NULL; /* port: forgotten */
        break;
    case WM_MSG_CTL_PRESS: {
        WmWindow *dlg = w->parent;
        int idx = (int)ctl_get_id(w) - MB_ID0;
        int cur = (int)wm_get_data(dlg, MB_FOCUS);
        /* the original compares 1000 - id with the focus and doesn't
         * update it; the port moves the focus to the pressed button */
        if (idx != cur) {
            ctl_button_set_focus((WmWindow *)wm_get_data(dlg, MB_BUTTON(cur)), false);
            ctl_button_set_focus(w, true);
            wm_set_data(dlg, MB_FOCUS, idx);
        }
        break;
    }
    case WM_MSG_CTL_CLICK:
        ctl_msgbox_res = (int)ctl_get_id(w) - MB_ID0 + 1;
        break;
    case WM_MSG_KEYDOWN: {
        unsigned n = (unsigned)wm_get_data(w, MB_COUNT);
        if (a == 0xd) {
            key_press((WmWindow *)wm_get_data(w, MB_BUTTON(wm_get_data(w, MB_FOCUS))));
        } else if (a == 0x1b || a == 0x70) {
            if (g_cancel < n)
                key_press((WmWindow *)wm_get_data(w, MB_BUTTON(g_cancel)));
        } else {
            for (unsigned i = 0; i < n; i++) {
                uint8_t k = (uint8_t)wm_get_data(w, MB_KEY(i));
                if (k && k == (uint8_t)b) {
                    WmWindow *btn = (WmWindow *)wm_get_data(w, MB_BUTTON(i));
                    ctl_button_set_focus((WmWindow *)wm_get_data(w, MB_BUTTON(wm_get_data(w, MB_FOCUS))), false);
                    ctl_button_set_focus(btn, true);
                    wm_set_data(w, MB_FOCUS, i);
                    key_press(btn);
                    break;
                }
            }
        }
        break;
    }
    case WM_MSG_KEYUP: {
        unsigned n = (unsigned)wm_get_data(w, MB_COUNT);
        switch (a) {
        case 9: /* tab, shift-tab */
            cycle_focus(w, !wm_shift_down());
            break;
        case 0xd: {
            unsigned cur = (unsigned)wm_get_data(w, MB_FOCUS);
            WmWindow *btn = (WmWindow *)wm_get_data(w, MB_BUTTON(cur));
            if (btn && g_pressed == btn)
                key_release(w, btn, cur, 0);
            break;
        }
        case 0x1b:
        case 0x70:
            if (g_cancel < n) {
                WmWindow *btn = (WmWindow *)wm_get_data(w, MB_BUTTON(g_cancel));
                if (btn && g_pressed == btn)
                    key_release(w, btn, g_cancel, 0);
            }
            break;
        case 0x25:
            cycle_focus(w, false);
            break;
        case 0x27:
            cycle_focus(w, true);
            break;
        default:
            for (unsigned i = 0; i < n; i++) {
                uint8_t k = (uint8_t)wm_get_data(w, MB_KEY(i));
                if (k && k == (uint8_t)b) {
                    WmWindow *btn = (WmWindow *)wm_get_data(w, MB_BUTTON(i));
                    if (btn == g_pressed)
                        key_release(w, btn, i, 1);
                    break;
                }
            }
            break;
        }
        break;
    }
    }
    return wm_default_proc(w, msg, a, b);
}
