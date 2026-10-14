/*
 * The selectable list box (0x4bcfbc-0x4be5d2) and the scrolling text
 * viewer (0x4be691-0x4bf5e3). See controls.h and docs/ui.md.
 */
#include <stdlib.h>
#include <string.h>

#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/mouse.h"
#include "ui/controls.h"
#include "ui/controls_int.h"

/* ================================================================ list box */

#define LINE_SIZE 0x100

/* The original's 0x35e-byte block (data slot 0 of the window). */
typedef struct ListBox {
    intptr_t user;               /* +0x000 */
    WmWindow *win;               /* +0x004 */
    WmWindow *scroll;            /* +0x008 */
    GfxRect area;                /* +0x00c rows, local */
    intptr_t data[CTL_LIST_MAX]; /* +0x01c */
    char *text;                  /* +0x1b0 CTL_LIST_MAX lines of 256 bytes */
    char *line[CTL_LIST_MAX];    /* +0x1b4 */
    int16_t top;                 /* +0x348 first visible line */
    int16_t sel;                 /* +0x34a selected line */
    int16_t count;               /* +0x34c */
    int16_t rows;                /* +0x34e visible rows */
    unsigned style;              /* +0x350 */
    uint8_t fg;                  /* +0x354 */
    uint8_t bg;                  /* +0x355 */
    uint32_t font;               /* +0x356 */
    int font_h;                  /* +0x35a */
} ListBox;

static ListBox *list_of(WmWindow *w) { return (ListBox *)wm_get_data(w, 0); }

/* 0x4be4d2 */
static int row_height(uint32_t font) { return (uint8_t)(gfx_font_height(font) + 2); }

/* 0x4bd27e */
static void list_free(ListBox *lb)
{
    if (!lb)
        return;
    free(lb->text);
    free(lb);
}

/* 0x4be453: whole rows in `r` (plus a partial one with CTL_LIST_PARTIAL) */
static int16_t visible_rows(const ListBox *lb, const GfxRect *r)
{
    int rh = row_height(lb->font), h = r->bottom - r->top + 1;
    int n = h / rh;
    if ((lb->style & CTL_LIST_PARTIAL) && rh * n < h)
        n++;
    return (int16_t)n;
}

/* 0x4be54b: rectangle of visible row `row` */
static void row_rect(const ListBox *lb, GfxRect *out, uint8_t row)
{
    out->left = lb->area.left;
    out->right = lb->area.right - lb->area.left;
    out->top = row_height(lb->font) * row + lb->area.top;
    out->bottom = lb->font_h + out->top + 1;
    if (out->bottom > lb->win->client.bottom)
        out->bottom = lb->win->client.bottom;
}

/* 0x4be5d2: swaps the two colors in `r` (anything else becomes `a`) */
static void invert_rect(GfxSurface *s, const GfxRect *r, uint8_t a, uint8_t b)
{
    if (!s)
        return;
    for (int y = r->top; y <= r->bottom; y++) {
        if (y < 0 || y >= s->height)
            continue;
        for (int x = r->left; x <= r->right; x++) {
            if (x < 0 || x >= s->width)
                continue;
            uint8_t *p = s->pixels + (size_t)y * s->stride + x;
            *p = *p == a ? b : a;
        }
    }
}

/* 0x4be262: draws visible row `row` (line top + row) */
static void draw_row(ListBox *lb, unsigned row)
{
    row &= 0xff;
    if ((int)row >= lb->rows)
        return;
    GfxSurface *s = lb->win->surface;
    GfxRect r;
    row_rect(lb, &r, (uint8_t)row);
    gfx_fill_rect(s, r.left, r.top, r.right - r.left + 1, r.bottom - r.top + 1, lb->bg);
    GfxRect clip = {r.left + 1, r.right - 1, r.top + 1, r.bottom - 1};
    if (!(lb->style & CTL_LIST_ALLOW_END) && lb->count == 0)
        return;
    int idx = lb->top + (int)row;
    if (idx >= 0 && idx < CTL_LIST_MAX) {
        gfx_font_select(lb->font);
        unsigned colors[2] = {lb->fg, lb->fg};
        gfx_font_set_colors(lb->font, 1, 2, colors);
        int x = clip.left;
        for (const char *p = lb->line[idx]; *p && x <= clip.right; p++) {
            gfx_font_draw_char(s, (uint8_t)*p, x, clip.top, &clip);
            x += (uint8_t)gfx_font_char_width((uint8_t)*p);
        }
    }
    if (!(lb->style & CTL_LIST_NOSELECT) && lb->sel - lb->top == (int)row) {
        row_rect(lb, &r, (uint8_t)row);
        invert_rect(s, &r, lb->bg, lb->fg);
    }
}

/* 0x4be4f3: visible row under local (x, y), 0xff outside the rows */
static uint8_t row_at(const ListBox *lb, int x, int y)
{
    if (!gfx_rect_contains(&lb->area, x, y))
        return 0xff;
    return (uint8_t)(y / row_height(lb->font));
}

static void update_range(ListBox *lb)
{
    int max = (lb->style & CTL_LIST_ALLOW_END) ? lb->count : lb->count - 1;
    if (max < 1)
        max = 0;
    ctl_scroll_set_range(lb->scroll, 0, max);
}

/* 0x4be04f: word-wraps `text` into lines (CTL_LIST_ADD each) */
static void list_set_text(WmWindow *w, const char *text)
{
    ListBox *lb = list_of(w);
    if (!lb || !text)
        return;
    int maxw = (lb->area.right - lb->area.left) - 1;
    uint32_t prev = gfx_font_select(lb->font);
    char line[LINE_SIZE];
    const char *start = text, *brk = text, *p = text;
    int n = 0, width = 0;
    while (*p) {
        if (width < maxw && n < LINE_SIZE - 1) {
            if (*p == '\n') {
                line[n] = 0;
                wm_send(w, w, CTL_LIST_ADD, (intptr_t)line, 0);
                start = brk = ++p;
                n = width = 0;
                while (*p == ' ')
                    start = brk = ++p;
            } else {
                if (*p == ' ')
                    brk = p - 1;
                width += (uint8_t)gfx_font_char_width((uint8_t)*p);
                line[n++] = *p++;
            }
        } else {
            /* break after the last word that fit (the whole run if none) */
            int keep = (int)(brk - start) + 1;
            if (keep <= 0 || keep > n)
                keep = n;
            line[keep] = 0;
            wm_send(w, w, CTL_LIST_ADD, (intptr_t)line, 0);
            start = brk = p = start + keep;
            n = width = 0;
            while (*p == ' ')
                start = brk = ++p;
        }
    }
    wm_send(w, w, CTL_LIST_ADD, (intptr_t)start, 0);
    if (prev && prev != lb->font)
        gfx_font_select(prev);
}

/* 0x4bcfbc */
WmWindow *ctl_list_create(WmWindow *parent, const GfxRect *rect, unsigned style, intptr_t user, uint32_t scroll_id,
                          uint32_t font, uint8_t bg, uint8_t fg)
{
    uint32_t prev = gfx_font_select(font);
    if (!prev)
        return NULL;
    gfx_font_select(prev);
    ListBox *lb = calloc(1, sizeof *lb);
    if (!lb)
        return NULL;
    lb->text = calloc(CTL_LIST_MAX, LINE_SIZE);
    if (!lb->text) {
        free(lb);
        return NULL;
    }
    for (int i = 0; i < CTL_LIST_MAX; i++)
        lb->line[i] = lb->text + i * LINE_SIZE;
    lb->user = user;
    lb->style = style;
    lb->font = font;
    lb->font_h = gfx_font_height(font);
    lb->fg = fg;
    lb->bg = bg;
    GfxRect r = *rect;
    lb->rows = visible_rows(lb, &r);
    if (!(style & CTL_LIST_PARTIAL))
        r.bottom -= (r.bottom - r.top + 1) % row_height(font);
    lb->win = wm_create_window(parent, &r, ctl_list_proc, WM_F_VISIBLE | WM_F_NOFOCUS, 4);
    if (!lb->win) {
        list_free(lb);
        return NULL;
    }
    gfx_rect_set(&lb->area, 0, (r.right - 0x1a) - r.left, 0, r.bottom - r.top);
    GfxRect sr = {r.right - r.left - 0x19, r.right - r.left, 0, r.bottom - r.top};
    lb->scroll = ctl_scroll_create(lb->win, &sr, scroll_id, CTL_SCROLL_VERTICAL);
    if (!lb->scroll) {
        wm_destroy(lb->win); /* port: the original leaks the window */
        list_free(lb);
        return NULL;
    }
    wm_set_data(lb->win, 0, (intptr_t)lb);
    wm_show(lb->scroll, true);
    wm_show(lb->win, true);
    return lb->win;
}

static void select_at(WmWindow *w, ListBox *lb, int x, int y)
{
    uint8_t row = row_at(lb, x, y);
    int idx = lb->top + row;
    if ((lb->style & CTL_LIST_ALLOW_END) ? idx <= lb->count : idx < lb->count)
        wm_send(w, w, CTL_LIST_SELECT, idx, 0);
}

/* 0x4bd2c6 */
intptr_t ctl_list_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    ListBox *lb = list_of(w);
    if (msg == WM_MSG_VTRACK || msg == WM_MSG_VSCROLL) {
        /* from the scrollbar: w is the track */
        w = w->parent;
        lb = list_of(w);
        if (lb)
            wm_send(w, w, CTL_LIST_SELECT, msg == WM_MSG_VTRACK ? lb->sel - b : lb->sel + b, 0);
        return wm_default_proc(w, msg, a, b);
    }
    if (!lb)
        return wm_default_proc(w, msg, a, b);
    switch (msg) {
    case WM_MSG_MOUSEMOVE:
        if (gfx_mouse_get_buttons() & GFX_MOUSE_LEFT)
            select_at(w, lb, (int)a, (int)b);
        break;
    case WM_MSG_LBUTTONDOWN:
        select_at(w, lb, (int)a, (int)b);
        break;
    case WM_MSG_DESTROY:
        wm_set_data(w, 0, 0);
        list_free(lb);
        break;
    case WM_MSG_LDBLCLK:
        wm_send(w->parent, w->parent, CTL_LIST_MSG_DBLCLK, (intptr_t)w, lb->sel);
        break;
    case WM_MSG_CHAR:
        if (w->parent && (lb->style & CTL_LIST_FORWARD))
            wm_send(w->parent, w->parent, WM_MSG_CHAR, a, b);
        break;
    case WM_MSG_KEYUP:
        switch (a) {
        case 0x21: /* page up */
            wm_send(w, w, CTL_LIST_SELECT, lb->sel - lb->rows, 0);
            break;
        case 0x22: /* page down */
            wm_send(w, w, CTL_LIST_SELECT, lb->sel + lb->rows, 0);
            break;
        case 0x23: /* end */
            wm_send(w, w, CTL_LIST_SELECT, lb->count, 0);
            break;
        case 0x24: /* home */
            wm_send(w, w, CTL_LIST_SELECT, 0, 0);
            break;
        case 0x26: /* up */
            wm_send(w, w, CTL_LIST_SELECT, lb->sel - 1, 0);
            break;
        case 0x28: /* down */
            wm_send(w, w, CTL_LIST_SELECT, lb->sel + 1, 0);
            break;
        }
        if (w->parent && (lb->style & CTL_LIST_FORWARD))
            wm_send(w->parent, w->parent, WM_MSG_KEYUP, a, b);
        break;
    case CTL_LIST_ADD: {
        if (!a || lb->count > 100)
            return 0;
        int idx = lb->count;
        strncpy(lb->line[idx], (const char *)a, LINE_SIZE - 1);
        lb->line[idx][LINE_SIZE - 1] = 0;
        wm_send(w, w, CTL_LIST_SET_DATA, idx, b);
        lb->count++;
        /* the original passes the line index as the visible row */
        draw_row(lb, (unsigned)idx);
        GfxRect r;
        row_rect(lb, &r, (uint8_t)(lb->count - 1));
        wm_redraw(lb->win, &r);
        update_range(lb);
        break;
    }
    case WM_MSG_CTL_RELEASE:
        wm_post(w, w, WM_MSG_DESTROY, 0, 0);
        break;
    case CTL_LIST_DELETE: {
        if (lb->count == 0)
            return 0;
        int idx = a == -1 ? lb->sel : (int)a;
        if (idx < 0 || idx >= CTL_LIST_MAX || idx >= lb->count)
            return 0;
        int after = lb->count - idx - 1;
        memmove(lb->line[idx], lb->line[idx + 1 < CTL_LIST_MAX ? idx + 1 : idx], (size_t)after * LINE_SIZE);
        memmove(&lb->data[idx], &lb->data[idx + 1 < CTL_LIST_MAX ? idx + 1 : idx], (size_t)after * sizeof lb->data[0]);
        lb->line[lb->count - 1][0] = 0;
        lb->data[lb->count - 1] = 0;
        lb->count--;
        wm_send(w, w, CTL_LIST_SELECT, lb->sel, 0);
        update_range(lb);
        return 1;
    }
    case CTL_LIST_SET_DATA:
        if (a >= 0 && a < CTL_LIST_MAX)
            lb->data[a] = b;
        break;
    case CTL_LIST_SCROLL: {
        gfx_fill_rect(w->surface, lb->area.left, lb->area.top, lb->area.right - lb->area.left + 1,
                      lb->area.bottom - lb->area.top + 1, lb->bg);
        lb->top = (int16_t)(lb->top + a);
        if (lb->top < 0)
            lb->top = 0;
        else if (lb->top > CTL_LIST_MAX - lb->rows)
            lb->top = (int16_t)(CTL_LIST_MAX - lb->rows);
        if (lb->sel < lb->top)
            lb->sel = lb->top;
        else if (lb->rows + lb->top <= lb->sel)
            lb->sel = (int16_t)(lb->rows + lb->top - 1);
        for (int i = 0; i < lb->rows; i++)
            draw_row(lb, (unsigned)i);
        wm_redraw(lb->win, NULL);
        break;
    }
    case CTL_LIST_SELECT: {
        lb->sel = (int16_t)a;
        if (lb->sel < 0)
            lb->sel = 0;
        else if (lb->sel >= lb->count)
            lb->sel = (int16_t)((lb->style & CTL_LIST_ALLOW_END) ? lb->count : lb->count - 1);
        int d = 0;
        if (lb->sel < lb->top)
            d = lb->sel - lb->top;
        else if (lb->rows + lb->top - 1 < lb->sel)
            d = lb->sel - (lb->rows + lb->top - 1);
        ctl_scroll_set_pos(lb->scroll, lb->sel);
        wm_send(w, w, CTL_LIST_SCROLL, d, 0);
        wm_send(w->parent, w->parent, CTL_LIST_MSG_SELECTED, (intptr_t)w, lb->sel);
        break;
    }
    case CTL_LIST_ROW_RECT:
        /* the original reads an uninitialized row unless b is -1 */
        if (!a)
            return 0;
        row_rect(lb, (GfxRect *)a, (uint8_t)(b == -1 ? lb->sel - lb->top : b));
        break;
    case CTL_LIST_GET_SEL:
        return lb->sel;
    case CTL_LIST_GET_TOP:
        return lb->top;
    case CTL_LIST_GET_TEXT:
    case CTL_LIST_TEXT_LEN:
    case CTL_LIST_GET_DATA: {
        if (msg == CTL_LIST_GET_TEXT && b)
            *(char *)b = 0;
        int idx = a == -1 ? lb->sel : (int)a;
        if (idx < 0 || idx >= lb->count)
            return -1;
        if (msg == CTL_LIST_GET_DATA)
            return lb->data[idx];
        if (msg == CTL_LIST_GET_TEXT && b)
            strcpy((char *)b, lb->line[idx]);
        return (intptr_t)strlen(lb->line[idx]);
    }
    case CTL_LIST_COUNT:
        return lb->count;
    case CTL_LIST_USER:
        return lb->user;
    case CTL_LIST_SET_TEXT:
        if (!a)
            return 0;
        wm_send(w, w, CTL_LIST_CLEAR, 0, 0);
        list_set_text(w, (const char *)a);
        wm_send(w, w, CTL_LIST_SCROLL, 0, 0);
        break;
    case CTL_LIST_CLEAR:
        for (int i = 0; i < lb->count; i++) {
            lb->line[i][0] = 0;
            lb->data[i] = 0;
        }
        lb->count = lb->sel = lb->top = 0;
        ctl_scroll_set_range(lb->scroll, 0, 0);
        ctl_scroll_set_pos(lb->scroll, 0);
        memset(lb->text, 0, (size_t)CTL_LIST_MAX * LINE_SIZE);
        if (a)
            wm_send(w, w, CTL_LIST_SCROLL, 0, 0);
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

/* ============================================================= text viewer */

#define VIEW_ID 0x00
#define VIEW_TEXT 0x04  /* the text passed in */
#define VIEW_LINES 0x08 /* char *[2500] into the buffer */
#define VIEW_FONT 0x0c
#define VIEW_BG 0x10
#define VIEW_FG1 0x14
#define VIEW_FG2 0x18
#define VIEW_AREA 0x1c /* GfxRect * */
#define VIEW_TOP 0x20
#define VIEW_COUNT 0x24
#define VIEW_STYLE 0x28
#define VIEW_LAST 0x2c /* last line index (+ 1 with CTL_VIEW_PARTIAL) */
#define VIEW_BUF 0x30
#define VIEW_BUFSIZE 0x34 /* port-only */
#define VIEW_MAX_LINES (10000 / 4)

/* 0x4beeba: `r` with its height cut to whole lines */
static void view_fit(unsigned style, GfxRect *out, const GfxRect *r, uint32_t font)
{
    *out = *r;
    if (!(style & CTL_VIEW_PARTIAL))
        out->bottom -= (out->bottom - out->top + 1) % (gfx_font_height(font) + 1);
}

/* 0x4bef0f: the text area, left of the scrollbar */
static void view_area(WmWindow *w, GfxRect *area, const GfxRect *r)
{
    int sw = w->vscroll ? w->vscroll->rect.right - w->vscroll->rect.left + 1 : 0;
    gfx_rect_set(area, 0, (r->right - sw) - r->left, 0, r->bottom - r->top);
}

/* 0x4bef5f: the scrollbar's rectangle for window rect `r` */
static void view_scroll_rect(WmWindow *w, GfxRect *out, const GfxRect *r)
{
    int sw = w->vscroll ? w->vscroll->rect.right - w->vscroll->rect.left + 1 : 0;
    *out = *r;
    out->right -= out->left;
    out->bottom -= out->top;
    out->left = out->right - sw + 1;
    out->top = 0;
}

/* 0x4bf54f */
static int view_rows(WmWindow *w)
{
    const GfxRect *area = (const GfxRect *)wm_get_data(w, VIEW_AREA);
    int lh = gfx_font_height((uint32_t)wm_get_data(w, VIEW_FONT)) + 1;
    int h = area->bottom - area->top + 1;
    int n = h / lh;
    if ((wm_get_data(w, VIEW_STYLE) & CTL_VIEW_PARTIAL) && lh * n < h)
        n++;
    return n;
}

/* 0x4bf5e3: rectangle of visible line `row` */
static void view_row_rect(WmWindow *w, int row, GfxRect *out)
{
    int lh = gfx_font_height((uint32_t)wm_get_data(w, VIEW_FONT)) + 1;
    const GfxRect *area = (const GfxRect *)wm_get_data(w, VIEW_AREA);
    *out = *area;
    out->bottom = out->top + lh - 1;
    out->left++;
    out->right--;
    out->top += lh * row;
    out->bottom += lh * row;
    if (out->bottom > area->bottom)
        out->bottom = area->bottom;
}

/* 0x4befcb: draws from line `top` */
static void view_draw(WmWindow *w, int top)
{
    const GfxRect *area = (const GfxRect *)wm_get_data(w, VIEW_AREA);
    int count = (int)wm_get_data(w, VIEW_COUNT);
    uint32_t font = (uint32_t)wm_get_data(w, VIEW_FONT);
    int fh = (uint8_t)gfx_font_height(font);
    int rows = view_rows(w);
    int end = (top + rows - 1 <= count - 1) ? top + rows : count;
    int last = end - 1;
    GfxRect r;
    view_row_rect(w, 0, &r);
    gfx_fill_rect(w->surface, area->left, area->top, area->right - area->left + 1, area->bottom - area->top + 1,
                  (uint8_t)wm_get_data(w, VIEW_BG));
    uint32_t prev = gfx_font_select(font);
    unsigned colors[2] = {(uint8_t)wm_get_data(w, VIEW_FG1), (uint8_t)wm_get_data(w, VIEW_FG2)};
    gfx_font_set_colors(font, 1, 2, colors);
    char **lines = (char **)wm_get_data(w, VIEW_LINES);
    if (count && lines) {
        int i = top;
        for (; i < last; i++) {
            if (i >= 0 && i < VIEW_MAX_LINES && lines[i])
                gfx_font_draw_text(w->surface, r.left, r.top, &r, &r, lines[i], GFX_TEXT_NOJUSTIFY);
            r.top += fh + 1;
            r.bottom += fh + 1;
        }
        if ((wm_get_data(w, VIEW_STYLE) & CTL_VIEW_PARTIAL) && r.bottom > area->bottom)
            r.bottom = area->bottom;
        if (i >= 0 && i < VIEW_MAX_LINES && lines[i] && r.top <= r.bottom)
            gfx_font_draw_text(w->surface, r.left, r.top, &r, &r, lines[i], GFX_TEXT_NOJUSTIFY);
        if (prev)
            gfx_font_select(prev);
        wm_set_data(w, VIEW_TOP, top);
        wm_redraw(w, area);
    } else if (prev) {
        gfx_font_select(prev);
    }
}

/* 0x4bf214: word-wraps `text` into the buffer and the line table */
static void view_set_text(WmWindow *w, const char *text)
{
    if (!text)
        return;
    char *buf = (char *)wm_get_data(w, VIEW_BUF), *end = buf + wm_get_data(w, VIEW_BUFSIZE);
    char **lines = (char **)wm_get_data(w, VIEW_LINES);
    const GfxRect *area = (const GfxRect *)wm_get_data(w, VIEW_AREA);
    uint32_t prev = gfx_font_select((uint32_t)wm_get_data(w, VIEW_FONT));
    wm_set_data(w, VIEW_TEXT, (intptr_t)text);
    int maxw = (area->right - area->left) - 1;
    int n = 0, width = 0;
    const char *start = text, *brk = text, *p = text;
    /* port: the buffer and the line table are bounds-checked */
#define ADD_LINE(src, len)                                                                                             \
    do {                                                                                                               \
        size_t l_ = (size_t)(len);                                                                                     \
        if (n < VIEW_MAX_LINES - 1 && buf + l_ + 1 <= end) {                                                           \
            lines[n] = buf;                                                                                            \
            memcpy(buf, (src), l_);                                                                                    \
            buf[l_] = 0;                                                                                               \
            buf += l_ + 1;                                                                                             \
            n++;                                                                                                       \
        }                                                                                                              \
    } while (0)
    while (*p) {
        if (width < maxw) {
            if (*p == '\n') {
                ADD_LINE(start, p - start);
                start = brk = ++p;
                width = 0;
            } else {
                if (*p == ' ')
                    brk = p - 1;
                width += (uint8_t)gfx_font_char_width((uint8_t)*p);
                p++;
            }
        } else {
            const char *cut = brk >= start ? brk + 1 : p;
            ADD_LINE(start, cut - start);
            width = 0;
            start = brk = p = cut;
            while (*p == ' ')
                start = brk = ++p;
        }
    }
    int last = n;
    if (*start)
        ADD_LINE(start, strlen(start));
    else
        last--;
#undef ADD_LINE
    if (last >= n)
        last = n - 1;
    wm_set_data(w, VIEW_COUNT, last + 1);
    wm_set_data(w, VIEW_TOP, 0);
    if (wm_get_data(w, VIEW_STYLE) & CTL_VIEW_PARTIAL)
        last++;
    wm_set_data(w, VIEW_LAST, last);
    int max = last - view_rows(w) + 1;
    if (max < 0) /* port: the original lets the range go negative */
        max = 0;
    ctl_scroll_set_range(w->vscroll, 0, max);
    ctl_scroll_set_pos(w->vscroll, 0);
    if (prev)
        gfx_font_select(prev);
}

/* 0x4be691 */
WmWindow *ctl_view_create(WmWindow *parent, const GfxRect *rect, unsigned style, uint32_t id, uint32_t font, uint8_t bg,
                          uint8_t fg1, uint8_t fg2, uint32_t bufsize)
{
    GfxRect *area = malloc(sizeof *area);
    if (!area)
        return NULL;
    GfxRect r;
    view_fit(style, &r, rect, font);
    WmWindow *w = wm_create_window(parent, &r, ctl_view_proc, WM_F_VSCROLL | WM_F_NOFOCUS | WM_F_VISIBLE, 0x38);
    if (!w) {
        free(area);
        return NULL;
    }
    view_area(w, area, &r);
    wm_set_data(w, VIEW_AREA, (intptr_t)area);
    char **lines = calloc(VIEW_MAX_LINES, sizeof *lines);
    if (bufsize < 0x8001)
        bufsize = 0x8000;
    char *buf = lines ? calloc(bufsize, 1) : NULL;
    if (!buf) {
        free(lines);
        wm_send(w, w, WM_MSG_CTL_RELEASE, 0, 0);
        wm_destroy(w); /* port: the original leaks the window */
        return NULL;
    }
    wm_set_data(w, VIEW_ID, id);
    wm_set_data(w, VIEW_TEXT, 0);
    wm_set_data(w, VIEW_LINES, (intptr_t)lines);
    wm_set_data(w, VIEW_FONT, font);
    wm_set_data(w, VIEW_BG, bg);
    wm_set_data(w, VIEW_FG1, fg1);
    wm_set_data(w, VIEW_FG2, fg2);
    wm_set_data(w, VIEW_TOP, 0);
    wm_set_data(w, VIEW_COUNT, 0);
    wm_set_data(w, VIEW_STYLE, style);
    wm_set_data(w, VIEW_LAST, 0);
    wm_set_data(w, VIEW_BUF, (intptr_t)buf);
    wm_set_data(w, VIEW_BUFSIZE, bufsize);
    wm_show(w->vscroll, true);
    return w;
}

static void view_free(WmWindow *w)
{
    if (wm_get_data(w, VIEW_STYLE) & CTL_VIEW_OWNS_TEXT)
        free((void *)wm_get_data(w, VIEW_TEXT));
    free((void *)wm_get_data(w, VIEW_AREA));
    free((void *)wm_get_data(w, VIEW_LINES));
    free((void *)wm_get_data(w, VIEW_BUF));
    wm_set_data(w, VIEW_LINES, 0);
    wm_set_data(w, VIEW_TEXT, 0);
    wm_set_data(w, VIEW_AREA, 0);
    wm_set_data(w, VIEW_BUF, 0);
}

static void view_scroll_to(WmWindow *w, int top)
{
    int max = (int)wm_get_data(w, VIEW_LAST) - (view_rows(w) - 1);
    if (top < 1)
        top = 0;
    if (max < 1)
        max = 0;
    if (top > max)
        top = max;
    /* the original compares with VIEW_LAST instead of the current top */
    if (top != (int)wm_get_data(w, VIEW_TOP)) {
        view_draw(w, top);
        ctl_scroll_set_pos(w->vscroll, top);
    }
}

/* 0x4be8d4 */
intptr_t ctl_view_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_VTRACK:
    case WM_MSG_VSCROLL: {
        /* from the scrollbar: w is the track */
        WmWindow *v = w->parent;
        if (v && wm_get_data(v, VIEW_AREA)) {
            int top = (int)wm_get_data(v, VIEW_TOP);
            view_scroll_to(v, msg == WM_MSG_VTRACK ? top - (int)b : top + (int)b);
        }
        break;
    }
    case WM_MSG_LBUTTONDOWN:
        /* a click goes to the parent, in its coordinates */
        if (w->parent)
            wm_send(w->parent, w->parent, WM_MSG_LBUTTONDOWN, a + w->rect.left, b + w->rect.top);
        break;
    case WM_MSG_SHOW:
        if (w->vscroll)
            wm_show(w->vscroll, a != 0);
        break;
    case CTL_VIEW_SET_TEXT:
        if (!a)
            return 0;
        if ((wm_get_data(w, VIEW_STYLE) & CTL_VIEW_OWNS_TEXT) && wm_get_data(w, VIEW_TEXT)) {
            free((void *)wm_get_data(w, VIEW_TEXT));
            wm_set_data(w, VIEW_TEXT, 0);
        }
        view_set_text(w, (const char *)a);
        break;
    case WM_MSG_CTL_RELEASE:
    case WM_MSG_DESTROY: /* port: the original only frees on 0x400 */
        if (wm_get_data(w, VIEW_AREA))
            view_free(w);
        break;
    case CTL_VIEW_REDRAW:
        if (wm_get_data(w, VIEW_AREA))
            view_draw(w, (int)wm_get_data(w, VIEW_TOP));
        break;
    case CTL_VIEW_RESIZE: {
        GfxRect *area = (GfxRect *)wm_get_data(w, VIEW_AREA);
        if (!area)
            break;
        const char *text = (const char *)wm_get_data(w, VIEW_TEXT);
        GfxRect r = w->rect, fit, sr;
        r.right = r.left + (a ? (int)a : w->rect.right - w->rect.left + 1) - 1;
        r.bottom = r.top + (b ? (int)b : w->rect.bottom - w->rect.top + 1) - 1;
        view_fit((unsigned)wm_get_data(w, VIEW_STYLE), &fit, &r, (uint32_t)wm_get_data(w, VIEW_FONT));
        view_area(w, area, &fit);
        view_scroll_rect(w, &sr, &fit);
        wm_set_data(w, VIEW_TOP, 0);
        ctl_scroll_set_pos(w->vscroll, 0);
        wm_resize(w, fit.right - fit.left + 1, fit.bottom - fit.top + 1);
        wm_resize(w->vscroll, sr.right - sr.left + 1, sr.bottom - sr.top + 1);
        if (text)
            view_set_text(w, text);
        view_draw(w, 0);
        wm_redraw(w, NULL);
        break;
    }
    }
    return wm_default_proc(w, msg, a, b);
}
