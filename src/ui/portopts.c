/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Port-only: the Port Options window. See portopts.h.
 */
#include "ui/portopts.h"

#include <stdio.h>
#include <string.h>

#include "game/display.h"
#include "game/enhance.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/mouse.h"
#include "platform/platform.h"
#include "ui/controls.h"
#include "ui/controls_int.h"

#define FONT 500
#define INK 0x1a   /* the message box's text colour */
#define PAPER 0x13 /* its background */
#define GREY 0x10  /* a disabled button's label */

static struct {
    WmWindow *win, *toggle, *slider, *ok, *cancel;
    GfxSurface *bg; /* the composed background, to redraw the texts on */
    int result;     /* 0 while open, 1 OK, 2 Cancel */
    bool enh, applied_enh, orig_enh;
    int scale, applied_scale, orig_scale;
    int right;                      /* the content's right edge */
    int row_y, desc_y, label_y, ticks_y;
    int tick_x[PORTOPTS_SCALE_MAX + 1]; /* thumb centres, window-local */
    WmWindow *prev_focus;
    uint32_t prev_font;
} g;

WmWindow *portopts_window(void) { return g.win; }

WmWindow *portopts_control(int id)
{
    switch (id) {
    case PORTOPTS_ENHANCE: return g.toggle;
    case PORTOPTS_SCALE: return g.slider;
    case PORTOPTS_OK: return g.ok;
    case PORTOPTS_CANCEL: return g.cancel;
    default: return NULL;
    }
}

int portopts_slider_x(int pos)
{
    if (!g.win || pos < 0 || pos > PORTOPTS_SCALE_MAX)
        return -1;
    int x = g.tick_x[pos], y = 0;
    wm_point_to_screen(g.win, &x, &y);
    return x;
}

static void colors(unsigned c)
{
    unsigned cc[2] = {c, c};
    gfx_font_set_colors(FONT, 1, 2, cc);
}

static void text(int x, int y, unsigned color, const char *s)
{
    colors(color);
    gfx_font_draw_text(g.win->surface, x, y, NULL, NULL, s, GFX_TEXT_NOJUSTIFY);
}

static void text_right(int right, int y, unsigned color, const char *s)
{
    text(right - gfx_font_text_width(s) + 1, y, color, s);
}

static void text_centred(int cx, int y, unsigned color, const char *s)
{
    text(cx - gfx_font_text_width(s) / 2, y, color, s);
}

/* The message box's background (msgbox.c): bitmap 0x260's quarters in the
 * corners, stretched by repeating 10 px strips from its middle. */
static void compose_background(GfxSurface *s, const GfxSurface *bmp, int xs, int ys)
{
    int dw = s->width, dh = s->height;
    gfx_fill_rect(s, 0, 0, dw, dh, PAPER);
    int hw = bmp->width >> 1, hh = bmp->height >> 1;
    gfx_blit(s, 0, 0, hw, hh, bmp, 0, 0);
    gfx_blit(s, dw - hw, 0, hw, hh, bmp, hw, 0);
    gfx_blit(s, 0, dh - hh, hw, hh, bmp, 0, hh);
    gfx_blit(s, dw - hw, dh - hh, hw, hh, bmp, hw, hh);
    for (int i = 0, at = hw; i < xs; i++, at += 10) {
        gfx_blit(s, at, 0, 10, hh, bmp, hw - 9, 0);
        gfx_blit(s, at, dh - hh, 10, hh, bmp, hw - 9, hh);
    }
    for (int i = 0, at = hh; i < ys; i++, at += 10) {
        gfx_blit(s, 0, at, hw, 10, bmp, 0, hh);
        gfx_blit(s, dw - hw, at, hw, 10, bmp, hw, hh);
    }
}

/* Redraws everything from the settings: texts, the toggle, the slider. */
static void refresh(void)
{
    if (!g.win)
        return;
    GfxSurface *s = g.win->surface;
    gfx_blit(s, 0, 0, g.bg->width, g.bg->height, g.bg, 0, 0);
    gfx_font_select(FONT);
    int fh = gfx_font_height(FONT);
    GfxRect tr = {5, 0xda, 4, 0xc};
    colors(INK);
    gfx_font_draw_text(s, 5, 4, &tr, &tr, "Port Options", 0);

    int bh = g.toggle->rect.bottom - g.toggle->rect.top + 1;
    text(0x18, g.row_y + (bh - fh) / 2, INK, "Enhancements");
    GfxRect desc = {0x18, g.right, g.desc_y, g.desc_y + 2 * (fh + 1) - 1};
    gfx_font_draw_wrapped(s, &desc, "Bug fixes, and the UI scale below.", FONT, INK, PAPER);

    unsigned scale_ink = g.enh ? INK : GREY;
    text(0x18, g.label_y, scale_ink, "UI scale");
    char value[64];
    int eff = plat_ui_scale();
    bool settled = g.scale == g.applied_scale && g.enh == g.applied_enh;
    if (!g.enh)
        snprintf(value, sizeof value, "1x (needs the enhancements)");
    else if (g.scale == 0)
        snprintf(value, sizeof value, settled ? "Automatic (%dx)" : "Automatic", eff);
    else if (settled && eff < g.scale)
        snprintf(value, sizeof value, "%dx (%dx: window too small)", g.scale, eff);
    else
        snprintf(value, sizeof value, "%dx", g.scale);
    text_right(g.right, g.label_y, scale_ink, value);
    static const char *const ticks[PORTOPTS_SCALE_MAX + 1] = {"Auto", "1x", "2x", "3x", "4x"};
    for (int i = 0; i <= PORTOPTS_SCALE_MAX; i++)
        text_centred(g.tick_x[i], g.ticks_y, scale_ink, ticks[i]);

    ctl_button_set_pressed(g.toggle, g.enh, false);
    ctl_button_set_label(g.toggle, g.enh ? "On" : "Off", FONT, INK, false);
    ctl_button_draw(g.toggle, (unsigned)wm_get_data(g.toggle, CTL_BUTTON_STATE) | CTL_DRAW_BITMAP);
    ctl_button_draw(g.ok, (unsigned)wm_get_data(g.ok, CTL_BUTTON_STATE) | CTL_DRAW_BITMAP);
    ctl_button_draw(g.cancel, (unsigned)wm_get_data(g.cancel, CTL_BUTTON_STATE) | CTL_DRAW_BITMAP);

    /* the slider: no thumb (an empty range) while it is inactive */
    if (g.enh) {
        ctl_scroll_set_range(g.slider, 0, PORTOPTS_SCALE_MAX);
        ctl_scroll_set_pos(g.slider, g.scale);
    } else {
        ctl_scroll_set_range(g.slider, 0, 0);
        ctl_scroll_set_pos(g.slider, 0);
    }
    ctl_scroll_layout(g.slider, 0, false);
    wm_redraw(g.win, NULL);
}

/* Applies the settings now (live relayout), unless they already are. */
static void apply(void)
{
    if (g.enh == g.applied_enh && g.scale == g.applied_scale)
        return;
    display_apply_settings(g.enh, g.scale);
    g.applied_enh = g.enh;
    g.applied_scale = g.scale;
    if (g.win) {
        wm_center(g.win);
        refresh();
    }
}

static void set_scale(int pos)
{
    if (!g.enh)
        return;
    if (pos < 0)
        pos = 0;
    if (pos > PORTOPTS_SCALE_MAX)
        pos = PORTOPTS_SCALE_MAX;
    if (pos == g.scale)
        return;
    g.scale = pos;
    refresh();
}

static void set_enh(bool on)
{
    g.enh = on;
    refresh();
    apply();
}

static intptr_t portopts_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_CTL_CLICK:
        switch (ctl_get_id(w)) {
        case PORTOPTS_ENHANCE:
            set_enh(ctl_is_pressed(w));
            break;
        case PORTOPTS_OK:
            g.result = 1;
            break;
        case PORTOPTS_CANCEL:
            g.result = 2;
            break;
        }
        break;
    case WM_MSG_HTRACK:
    case WM_MSG_HSCROLL:
        /* the slider (w = its track); applied when the mouse is let go */
        if (w == g.slider)
            set_scale((int)a);
        break;
    case WM_MSG_KEYDOWN:
        if (a == PLAT_VK_LEFT)
            set_scale(g.scale - 1);
        else if (a == PLAT_VK_RIGHT)
            set_scale(g.scale + 1);
        break;
    case WM_MSG_KEYUP:
        if (a == PLAT_VK_RETURN)
            g.result = 1;
        else if (a == PLAT_VK_ESCAPE)
            g.result = 2;
        else if (a == 'E')
            set_enh(!g.enh);
        break;
    case WM_MSG_DESTROY:
        if (w == g.win) {
            wm_release_capture();
            g.win = NULL;
        }
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

static WmWindow *button(int x, int y, int id, uint32_t style, const char *label)
{
    GfxSurface *bmp = gfx_load_bmp_resource(0x227, NULL);
    if (!bmp)
        return NULL;
    GfxRect r = {x, x + bmp->width - 1, y, y + bmp->height - 1};
    WmWindow *b = ctl_button_create(g.win, &r, (uint32_t)id, style, bmp);
    if (!b) {
        gfx_surface_destroy(bmp);
        return NULL;
    }
    ctl_button_set_label(b, label, FONT, INK, false);
    return b;
}

static void destroy(void)
{
    if (g.win) {
        WmWindow *w = g.win;
        wm_show(w, false);
        wm_destroy(w); /* the buttons free their bitmaps */
    }
    g.win = g.toggle = g.slider = g.ok = g.cancel = NULL;
    gfx_surface_destroy(g.bg);
    g.bg = NULL;
}

static bool create(void)
{
    GfxSurface *bmp = gfx_load_bmp_resource(0x260, NULL);
    GfxSurface *btn = gfx_load_bmp_resource(0x227, NULL);
    if (!bmp || !btn) {
        gfx_surface_destroy(bmp);
        gfx_surface_destroy(btn);
        return false;
    }
    gfx_font_select(FONT);
    int fh = gfx_font_height(FONT), bw = btn->width >> 1, bh = btn->height;
    int sh = ctl_scroll_thickness(false);
    gfx_surface_destroy(btn);
    /* the message box's text area is 0x18..0xc4 x 0x18..0x47, plus 10 px
     * per strip */
    int need_h = bh + 4 + 2 * (fh + 1) + 6 + (fh + 1) + 3 + sh + 2 + (fh + 1);
    int xs = 11, ys = need_h > 0x30 ? (need_h - 0x30 + 9) / 10 : 0;
    int dw = bmp->width + xs * 10, dh = bmp->height + ys * 10;
    g.bg = gfx_surface_create(dw, dh);
    if (!g.bg) {
        gfx_surface_destroy(bmp);
        return false;
    }
    compose_background(g.bg, bmp, xs, ys);
    gfx_surface_destroy(bmp);

    GfxRect r = {0, dw - 1, 0, dh - 1};
    g.win = wm_create_window(NULL, &r, portopts_proc, WM_F_TOPMOST | WM_F_HITCHILDREN, 0);
    if (!g.win)
        return false;
    wm_set_title(g.win, "Port Options");
    g.right = 0xc4 + xs * 10;
    g.row_y = 0x18;
    g.toggle = button(g.right - bw + 1, g.row_y, PORTOPTS_ENHANCE, CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE, "Off");
    g.desc_y = g.row_y + bh + 4;
    g.label_y = g.desc_y + 2 * (fh + 1) + 6;
    int slider_y = g.label_y + fh + 1 + 3;
    GfxRect sr = {0x18, g.right, slider_y, slider_y + sh - 1};
    g.slider = ctl_scroll_create(g.win, &sr, PORTOPTS_SCALE, CTL_SCROLL_SHOW);
    g.ticks_y = slider_y + sh + 2;
    int by = 0x5a + ys * 10 + ((0x23 - bh) >> 1), bx = (dw - (2 * bw + 10)) >> 1;
    g.ok = button(bx, by, PORTOPTS_OK, CTL_STYLE_BITMAP, "OK");
    g.cancel = button(bx + bw + 10, by, PORTOPTS_CANCEL, CTL_STYLE_BITMAP, "Cancel");
    if (!g.toggle || !g.slider || !g.ok || !g.cancel)
        return false;
    /* where the thumb sits for each position */
    ctl_scroll_set_range(g.slider, 0, PORTOPTS_SCALE_MAX);
    for (int i = 0; i <= PORTOPTS_SCALE_MAX; i++) {
        WmWindow *thumb = NULL;
        ctl_scroll_set_pos(g.slider, i);
        ctl_scroll_parts(g.slider, NULL, NULL, &thumb);
        g.tick_x[i] = thumb ? g.slider->rect.left + (thumb->rect.left + thumb->rect.right) / 2
                            : sr.left + (sr.right - sr.left) * i / PORTOPTS_SCALE_MAX;
    }
    return true;
}

bool portopts_run(void)
{
    if (g.win)
        return false;
    g.prev_font = gfx_font_select(FONT);
    if (!create()) {
        destroy();
        gfx_font_select(g.prev_font);
        return false;
    }
    g.result = 0;
    g.enh = g.applied_enh = g.orig_enh = g_enhancements;
    g.scale = g.applied_scale = g.orig_scale = g_port_settings.ui_scale;
    refresh();
    if (ctl_pause_flag)
        *ctl_pause_flag = 1;
    if (ctl_hooks.modal_begin)
        ctl_hooks.modal_begin();
    wm_center(g.win);
    g.prev_focus = wm_focus();
    wm_raise(g.win);
    wm_show(g.win, true);
    wm_activate(g.win);
    wm_set_capture(g.win);
    while (g.result == 0 && g.win) {
        ctl_modal_pump();
        if (!g.win)
            break;
        /* the controls take the capture while pressed and let it go */
        if (!wm_capture())
            wm_set_capture(g.win);
        /* the slider applies once the mouse is let go (a live relayout
         * under a dragged thumb would move it away from the pointer) */
        if (!(gfx_mouse_get_buttons() & GFX_MOUSE_LEFT))
            apply();
    }
    bool ok = g.result == 1;
    if (g.win) {
        if (ok) {
            apply();
            if (!enhance_save())
                fprintf(stderr, "Port Options: can't save %s\n", port_settings_path());
        } else {
            g.enh = g.orig_enh;
            g.scale = g.orig_scale;
            apply();
        }
        wm_release_capture();
    }
    destroy();
    if (g.prev_focus)
        wm_activate(g.prev_focus);
    g.prev_focus = NULL;
    if (ctl_pause_flag)
        *ctl_pause_flag = 0;
    gfx_font_select(g.prev_font);
    return ok;
}
