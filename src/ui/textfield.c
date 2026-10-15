/*
 * Single-line text field (0x4bc611-0x4bcb13) and the modal text prompt
 * built on it (0x4bf970). See controls.h and docs/ui.md.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "gfx/draw.h"
#include "gfx/font.h"
#include "ui/controls.h"
#include "ui/controls_int.h"

#define TEXT_SIZE 0x100

/* 0x4bc783: frees the text buffer */
static void text_free(WmWindow *f)
{
    if (!f)
        return;
    if (ctl_blink_field == f)
        ctl_blink_field = NULL;
    free(f->title);
    f->title = NULL;
}

/* 0x4bc611. The window has WM_F_NOTITLE; the text buffer is allocated
 * here and installed as the title (255 usable characters). */
WmWindow *ctl_text_create(WmWindow *parent, GfxRect *r, uint32_t id, uint8_t bg, uint8_t fg1, uint8_t fg2, uint8_t cur1,
                          uint8_t cur2, uint32_t font, unsigned style)
{
    WmWindow *f = wm_create_window(parent, r, ctl_text_proc, WM_F_NOTITLE | WM_F_VISIBLE, 0x2c);
    if (!f)
        return NULL;
    f->title = calloc(TEXT_SIZE, 1);
    if (!f->title) {
        wm_destroy(f);
        return NULL;
    }
    if (style & CTL_TEXT_FOCUS)
        ctl_blink_field = f;
    f->title_size = TEXT_SIZE - 1;
    wm_set_data(f, CTL_TEXT_LEN, 0);
    wm_set_data(f, CTL_TEXT_BG, bg);
    wm_set_data(f, CTL_TEXT_FG1, fg1);
    wm_set_data(f, CTL_TEXT_FG2, fg2);
    wm_set_data(f, CTL_TEXT_CURSOR, '|');
    wm_set_data(f, CTL_TEXT_CUR1, cur1);
    wm_set_data(f, CTL_TEXT_CUR2, cur2);
    wm_set_data(f, CTL_TEXT_FONT, font);
    wm_set_data(f, CTL_TEXT_ID, id);
    wm_set_data(f, CTL_TEXT_STYLE, style);
    wm_show(f, true);
    wm_raise(f);
    return f;
}

const char *ctl_text_get(WmWindow *field) { return wm_get_title(field); }

/* 0x4bc7cd: background and text (the tail that fits, if it's too long);
 * remembers where the cursor goes. */
void ctl_text_draw(WmWindow *f)
{
    if (!f || !f->surface || !f->title)
        return;
    int w = f->rect.right - f->rect.left;
    uint32_t font = (uint32_t)wm_get_data(f, CTL_TEXT_FONT);
    uint8_t bg = (uint8_t)wm_get_data(f, CTL_TEXT_BG);
    unsigned c1 = (uint8_t)wm_get_data(f, CTL_TEXT_FG1), c2 = (uint8_t)wm_get_data(f, CTL_TEXT_FG2);
    int fh = gfx_font_height(font);
    int h = f->rect.bottom - f->rect.top + 1;
    if (h > fh + 3)
        h = fh + 3;
    GfxRect clip = {1, w - 1, 1, h - 1};
    if (wm_get_data(f, CTL_TEXT_STYLE) & CTL_TEXT_READONLY) {
        c1 = 0x24;
        c2 = 0x13;
    }
    gfx_fill_rect(f->surface, 1, 1, w - 1, h, bg);
    uint32_t prev = gfx_font_select(font);
    unsigned colors[2] = {c1, c2};
    gfx_font_set_colors(font, 1, 2, colors);
    const char *p = f->title;
    while (*p && gfx_font_text_width(p) >= w - 0xb)
        p++;
    GfxRect area = clip;
    int x = gfx_font_draw_text(f->surface, 1, 1, &clip, &area, p, 0);
    wm_set_data(f, CTL_TEXT_CURSOR_X, x + 1);
    if (prev)
        gfx_font_select(prev);
}

/* 0x4bc9c0 */
void ctl_text_draw_cursor(WmWindow *f, bool phase)
{
    if (!f || !f->surface)
        return;
    int w = f->rect.right - f->rect.left, h = f->rect.bottom - f->rect.top;
    uint32_t font = (uint32_t)wm_get_data(f, CTL_TEXT_FONT);
    int x = (int)wm_get_data(f, CTL_TEXT_CURSOR_X);
    int right = x + 0x13;
    if (right > w - 2)
        right = w - 2;
    GfxRect clip = {x, right, 1, h - 3};
    uint32_t prev = gfx_font_select(font);
    unsigned colors[2] = {0x13, 0x13};
    if (phase) {
        colors[0] = (uint8_t)wm_get_data(f, CTL_TEXT_CUR1);
        colors[1] = (uint8_t)wm_get_data(f, CTL_TEXT_CUR2);
    }
    gfx_font_set_colors(font, 1, 2, colors);
    gfx_font_draw_char(f->surface, (uint8_t)wm_get_data(f, CTL_TEXT_CURSOR), x, 1, &clip);
    if (prev)
        gfx_font_select(prev);
    if (clip.left <= clip.right)
        wm_redraw(f, &clip);
}

static bool accepted(int ch)
{
    if (ch < 0x20 || ch >= 0x7d)
        return false;
    return !strchr("@{[&`=]^_", ch);
}

/* 0x4bcb13 */
intptr_t ctl_text_proc(WmWindow *f, unsigned msg, intptr_t a, intptr_t b)
{
    unsigned style = (unsigned)wm_get_data(f, CTL_TEXT_STYLE);
    switch (msg) {
    case WM_MSG_ACTIVATE:
        if (style & CTL_TEXT_READONLY) {
            ctl_text_draw_cursor(f, false);
            ctl_blink_field = NULL;
        } else if (a || (style & CTL_TEXT_FOCUS)) {
            ctl_blink_field = f;
        } else if (!(b && (((WmWindow *)b)->flags & WM_F_NOFOCUS))) {
            /* losing the focus to a window that takes it */
            if (ctl_blink_field)
                ctl_text_draw_cursor(ctl_blink_field, false);
            ctl_blink_field = NULL;
        }
        break;
    case WM_MSG_DESTROY:
        ctl_blink_field = NULL;
        text_free(f);
        break;
    case WM_MSG_CHAR: {
        if (style & CTL_TEXT_READONLY)
            break;
        int len = (int)wm_get_data(f, CTL_TEXT_LEN);
        if (a == 8) {
            if (len) {
                wm_set_data(f, CTL_TEXT_LEN, len - 1);
                f->title[len - 1] = 0;
                ctl_text_draw(f);
                wm_redraw(f, NULL);
            }
        } else if (a == 0xd) {
            wm_send(f->parent, f->parent, CTL_TEXT_MSG_ENTER, (intptr_t)f, wm_get_data(f, CTL_TEXT_ID));
        } else if (accepted((int)a) && f->title && (uint32_t)len < f->title_size) {
            int ch = (int)a;
            if (style & CTL_TEXT_UPPER)
                ch = toupper(ch);
            f->title[len] = (char)ch;
            f->title[len + 1] = 0;
            wm_set_data(f, CTL_TEXT_LEN, len + 1);
            ctl_text_draw(f);
            wm_redraw(f, NULL);
            ctl_blink_phase = false;
        }
        break;
    }
    case WM_MSG_RBUTTONDOWN:
        if ((style & CTL_TEXT_RCLICK) && f->parent)
            wm_send(f->parent, f->parent, CTL_TEXT_MSG_RCLICK, (intptr_t)f, wm_get_data(f, CTL_TEXT_ID));
        break;
    case CTL_TEXT_MSG_SET:
        if (!a) {
            if (f->title)
                f->title[0] = 0;
            wm_set_data(f, CTL_TEXT_LEN, 0);
        } else {
            wm_set_title(f, (const char *)a);
            wm_set_data(f, CTL_TEXT_LEN, f->title ? (intptr_t)strlen(f->title) : 0);
        }
        ctl_text_draw(f);
        wm_redraw(f, NULL);
        break;
    case WM_MSG_CTL_RELEASE:
        wm_post(f, f, WM_MSG_DESTROY, 0, 0);
        break;
    case CTL_TEXT_MSG_DRAW:
        ctl_text_draw(f);
        break;
    case CTL_TEXT_MSG_GET_ID:
        /* the original reads the id and returns nothing useful */
        return wm_get_data(f, CTL_TEXT_ID);
    case CTL_TEXT_MSG_READONLY:
        style = a ? (style | CTL_TEXT_READONLY) : (style & ~CTL_TEXT_READONLY);
        wm_set_data(f, CTL_TEXT_STYLE, style);
        ctl_text_draw(f);
        ctl_text_draw_cursor(f, a == 0);
        break;
    }
    return wm_default_proc(f, msg, a, b);
}

/* ---------------------------------------------------------------- prompt */

static bool g_prompt_open;              /* 0x6726d4 */
static char g_prompt_result[TEXT_SIZE]; /* 0x64e3c0 */

/* 0x4bfc10 */
static intptr_t prompt_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    if (msg == CTL_TEXT_MSG_ENTER)
        g_prompt_open = false;
    return wm_default_proc(w, msg, a, b);
}

static void copy_result(const char *s)
{
    strncpy(g_prompt_result, s, sizeof g_prompt_result - 1);
    g_prompt_result[sizeof g_prompt_result - 1] = 0;
}

/* 0x4bf970; `fill` (port-only) puts `def` into the field to edit */
static const char *prompt_run(int x, int y, const char *title, const char *prompt, const char *def, bool fill)
{
    GfxSurface *bg = gfx_load_bmp_resource(0x260, NULL);
    if (!bg)
        return NULL;
    GfxRect r = {x, x + bg->width - 1, y, y + bg->height - 1};
    WmWindow *dlg = wm_create_window(NULL, &r, prompt_proc, WM_F_VISIBLE, 0);
    if (!dlg) {
        gfx_surface_destroy(bg);
        return NULL;
    }
    gfx_blit(dlg->surface, 0, 0, bg->width, bg->height, bg, 0, 0);
    gfx_surface_destroy(bg);
    if (x < 0 || y < 0)
        wm_center(dlg);
    GfxRect fr = {0x14, 0xca, 0x61, 0x7a};
    WmWindow *field = ctl_text_create(dlg, &fr, 1000, 0x13, 0x1a, 0x1a, 0x1a, 0x13, 500, 0);
    if (!field) {
        wm_destroy(dlg); /* port: the original leaks the window */
        return NULL;
    }
    if (title) {
        GfxRect tr = {4, 0xd4, 4, 8};
        gfx_font_draw_text(dlg->surface, 4, 4, &tr, &tr, title, GFX_TEXT_NOJUSTIFY);
    }
    GfxRect pr = {0xf, 0xce, 0x10, 0x4e};
    gfx_font_select(500);
    unsigned colors[2] = {0x1a, 0x1a};
    gfx_font_set_colors(500, 1, 2, colors);
    gfx_font_draw_text(dlg->surface, pr.left, pr.top, &pr, &pr, prompt ? prompt : "", GFX_TEXT_NOJUSTIFY);
    wm_redraw(dlg, NULL);
    wm_raise(dlg);
    wm_show(dlg, true);
    wm_show(field, true);
    if (fill && def)
        wm_send(field, field, CTL_TEXT_MSG_SET, (intptr_t)def, 0);
    wm_activate(field);
    g_prompt_open = true;
    do
        ctl_modal_pump();
    while (g_prompt_open);
    if (field->title && field->title[0] == 0 && def)
        copy_result(def);
    else
        copy_result(field->title ? field->title : "");
    wm_show(dlg, false);
    wm_destroy(dlg);
    return g_prompt_result;
}

const char *ctl_prompt(int x, int y, const char *title, const char *prompt, const char *def)
{
    return prompt_run(x, y, title, prompt, def, false);
}

const char *ctl_prompt_filled(int x, int y, const char *title, const char *prompt, const char *def)
{
    return prompt_run(x, y, title, prompt, def, true);
}
