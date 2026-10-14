/*
 * Scrollbars (0x4baca2-0x4bc482, 0x4c110a). See controls.h and docs/ui.md.
 *
 * A scrollbar is a track window with three part windows; the parts are
 * NOCLIP and are drawn into the track's surface by ctl_scroll_layout. All
 * four share ctl_scroll_proc and have the same extra data layout
 * (CTL_SCROLL_*); KIND tells them apart.
 */
#include <stddef.h>

#include "gfx/draw.h"
#include "ui/controls.h"
#include "ui/controls_int.h"

/* 0x672690..0x6726ac: part bitmaps, vertical then horizontal */
enum { SB_BACK, SB_FWD, SB_THUMB, SB_TRACK };
static GfxSurface *g_vbmp[4];
static GfxSurface *g_hbmp[4];

static bool g_dragging;        /* 0x524798: the thumb is being dragged */
static int g_drag_x, g_drag_y; /* 0x5247a4 / 0x52479c: where it was grabbed (thumb-local) */

#define PART_FLAGS (WM_F_NOFOCUS | WM_F_NOCLIP | WM_F_NOTITLE | WM_F_NORAISE)
/* the track: in the owner's front stacking group, like minimized windows */
#define TRACK_FLAGS (WM_F_NOFOCUS | WM_F_NOTITLE | WM_F_NORAISE | WM_F_MINIMIZED)

static int bmp_w(const GfxSurface *s) { return s ? s->width : 0x18; }
static int bmp_h(const GfxSurface *s) { return s ? s->height : 0x18; }
static int rect_w(const GfxRect *r) { return r->right - r->left + 1; }
static int rect_h(const GfxRect *r) { return r->bottom - r->top + 1; }

/* 0x4baca2 */
void ctl_scroll_set_vbitmaps(GfxSurface *up, GfxSurface *down, GfxSurface *thumb, GfxSurface *track)
{
    g_vbmp[SB_BACK] = up;
    g_vbmp[SB_FWD] = down;
    g_vbmp[SB_THUMB] = thumb;
    g_vbmp[SB_TRACK] = track;
}

/* 0x4bad6d */
void ctl_scroll_set_hbitmaps(GfxSurface *left, GfxSurface *right, GfxSurface *thumb, GfxSurface *track)
{
    g_hbmp[SB_BACK] = left;
    g_hbmp[SB_FWD] = right;
    g_hbmp[SB_THUMB] = thumb;
    g_hbmp[SB_TRACK] = track;
}

/* 0x4bc3f8 */
void ctl_scroll_parts(WmWindow *bar, WmWindow **back, WmWindow **fwd, WmWindow **thumb)
{
    if (!bar)
        return;
    WmWindow *f = bar->children;
    if (fwd)
        *fwd = f;
    if (!f)
        return;
    WmWindow *b = f->next;
    if (back)
        *back = b;
    if (b && thumb)
        *thumb = b->next;
}

/* 0x4bae38. The original stores each bitmap in the track's data slot 0x0c
 * in turn (so only the track bitmap remains); kept. */
void ctl_scroll_set_skin(WmWindow *bar, GfxSurface *back, GfxSurface *fwd, GfxSurface *thumb, GfxSurface *track)
{
    if (!bar)
        return;
    WmWindow *pb = NULL, *pf = NULL, *pt = NULL;
    ctl_scroll_parts(bar, &pb, &pf, &pt);
    if (!pb || !pf || !pt || !bar->surface)
        return;
    if (track)
        gfx_tile_fill(bar->surface, 0, 0, rect_w(&bar->rect), rect_h(&bar->rect), track, 0, 0, track->width,
                      track->height);
    if (back)
        gfx_blit(bar->surface, 0, 0, rect_w(&pb->rect), rect_h(&pb->rect), back, 0, 0);
    if (fwd)
        gfx_blit(bar->surface, 0, 0, rect_w(&pf->rect), rect_h(&pf->rect), fwd, 0, 0);
    if (thumb)
        gfx_blit(bar->surface, 0, 0, rect_w(&pt->rect), rect_h(&pt->rect), thumb, 0, 0);
    wm_set_data(bar, CTL_SCROLL_BITMAP, (intptr_t)track);
}

static WmWindow *new_part(WmWindow *track, GfxRect *r, GfxSurface *bmp, unsigned kind, unsigned style)
{
    WmWindow *p = wm_create_window(track, r, ctl_scroll_proc, PART_FLAGS, 0x20);
    if (!p)
        return NULL;
    wm_set_data(p, CTL_SCROLL_BITMAP, (intptr_t)bmp);
    wm_set_data(p, CTL_SCROLL_KIND, kind);
    wm_set_data(p, CTL_SCROLL_STYLE, style);
    wm_show(p, true);
    return p;
}

static WmWindow *new_track(WmWindow *owner, GfxRect *r, GfxSurface *bmp, uint32_t id, unsigned style)
{
    WmWindow *t = wm_create_window(owner, r, ctl_scroll_proc, TRACK_FLAGS, 0x20);
    if (!t)
        return NULL;
    wm_set_data(t, CTL_SCROLL_BITMAP, (intptr_t)bmp);
    wm_set_data(t, CTL_SCROLL_KIND, CTL_SCROLL_TRACK);
    wm_set_data(t, CTL_SCROLL_STYLE, style);
    wm_set_data(t, CTL_SCROLL_ID, id);
    return t;
}

/* 0x4bb095 */
static WmWindow *create_vertical(WmWindow *owner, const GfxRect *rect, uint32_t id, unsigned style)
{
    if (!owner)
        return NULL;
    GfxRect r;
    if (!rect) {
        wm_get_local_rect(owner, &r);
        int w = bmp_w(g_vbmp[SB_TRACK]);
        r.left = r.right - w + 1;
        if (owner->menubar)
            r.top = owner->menubar->rect.bottom + 1;
        if (owner->hscroll)
            r.bottom = owner->hscroll->rect.top - 1;
        owner->client.right = r.right - w;
    } else {
        r = *rect;
    }
    WmWindow *t = new_track(owner, &r, g_vbmp[SB_TRACK], id, style);
    if (!t)
        return NULL;
    /* forward (down) arrow at the bottom */
    wm_get_local_rect(t, &r);
    r.top = r.bottom - bmp_h(g_vbmp[SB_FWD]) + 1;
    if (!new_part(t, &r, g_vbmp[SB_FWD], CTL_SCROLL_FWD, style))
        goto fail;
    /* back (up) arrow at the top */
    wm_get_local_rect(t, &r);
    r.bottom = r.top + bmp_h(g_vbmp[SB_BACK]) - 1;
    if (!new_part(t, &r, g_vbmp[SB_BACK], CTL_SCROLL_BACK, style))
        goto fail;
    /* the thumb below it, centred across */
    r.top = r.bottom + 1;
    r.bottom = r.top + bmp_h(g_vbmp[SB_THUMB]) - 1;
    int half = bmp_w(g_vbmp[SB_THUMB]) >> 1;
    r.left = (rect_w(&t->rect) - half) >> 1;
    r.right = r.left + half - 1;
    if (!new_part(t, &r, g_vbmp[SB_THUMB], CTL_SCROLL_THUMB, style))
        goto fail;
    ctl_scroll_layout(t, 0, false);
    if (style & CTL_SCROLL_SHOW)
        wm_show(t, true);
    return t;
fail:
    wm_destroy(t);
    return NULL;
}

/* 0x4bb48b */
static WmWindow *create_horizontal(WmWindow *owner, const GfxRect *rect, uint32_t id, unsigned style)
{
    if (!owner)
        return NULL;
    GfxRect r;
    if (!rect) {
        wm_get_local_rect(owner, &r);
        int h = bmp_h(g_hbmp[SB_TRACK]);
        r.top = r.bottom - h + 1;
        if (owner->vscroll)
            r.right = owner->vscroll->rect.left - 1;
        owner->client.bottom = r.bottom - h;
    } else {
        r = *rect;
    }
    WmWindow *t = new_track(owner, &r, g_hbmp[SB_TRACK], id, style);
    if (!t)
        return NULL;
    /* forward (right) arrow at the right end */
    wm_get_local_rect(t, &r);
    r.left = r.right - (bmp_w(g_hbmp[SB_FWD]) >> 1) + 1;
    if (!new_part(t, &r, g_hbmp[SB_FWD], CTL_SCROLL_FWD, style))
        goto fail;
    /* back (left) arrow at the left end */
    wm_get_local_rect(t, &r);
    r.right = r.left + (bmp_w(g_hbmp[SB_BACK]) >> 1) - 1;
    if (!new_part(t, &r, g_hbmp[SB_BACK], CTL_SCROLL_BACK, style))
        goto fail;
    /* the thumb right of it, centred vertically (height from the vertical
     * thumb bitmap, as in the original) */
    r.left = r.right + 1;
    r.right = r.left + (bmp_w(g_hbmp[SB_THUMB]) >> 1) - 1;
    int th = bmp_h(g_vbmp[SB_THUMB]);
    r.top = (rect_h(&t->rect) - th) >> 1;
    r.bottom = r.top + th - 1;
    if (!new_part(t, &r, g_hbmp[SB_THUMB], CTL_SCROLL_THUMB, style))
        goto fail;
    ctl_scroll_layout(t, 0, false);
    if (style & CTL_SCROLL_SHOW)
        wm_show(t, true);
    return t;
fail:
    wm_destroy(t);
    return NULL;
}

/* 0x4bb012 */
WmWindow *ctl_scroll_create(WmWindow *owner, const GfxRect *r, uint32_t id, unsigned style)
{
    WmWindow *bar =
        (style & CTL_SCROLL_VERTICAL) ? create_vertical(owner, r, id, style) : create_horizontal(owner, r, id, style);
    if (bar) {
        ctl_scroll_set_range(bar, 0, 99);
        ctl_scroll_set_pos(bar, 0);
    }
    return bar;
}

/* 0x4bb878 */
void ctl_scroll_set_pos(WmWindow *bar, int pos)
{
    if (!bar)
        return;
    unsigned style = (unsigned)wm_get_data(bar, CTL_SCROLL_STYLE);
    int min = (int)wm_get_data(bar, CTL_SCROLL_MIN), max = (int)wm_get_data(bar, CTL_SCROLL_MAX);
    int span = max - min;
    if (pos < min)
        pos = min;
    if (pos > max)
        pos = max;
    wm_set_data(bar, CTL_SCROLL_POS, pos);
    WmWindow *back = NULL, *fwd = NULL, *thumb = NULL;
    ctl_scroll_parts(bar, &back, &fwd, &thumb);
    if (!back || !fwd || !thumb)
        return;
    int tw = rect_w(&thumb->rect), th = rect_h(&thumb->rect);
    int first, last;
    if (style & CTL_SCROLL_VERTICAL) {
        first = back->rect.bottom + 1;
        last = fwd->rect.top - th;
    } else {
        first = back->rect.right + 1;
        last = fwd->rect.left - tw;
    }
    int travel = last - first + 1;
    /* span is never 0 after ctl_scroll_set_range (port: guarded anyway) */
    int off = span ? travel * (pos - min) / span : 0;
    GfxRect r = thumb->rect;
    if (style & CTL_SCROLL_VERTICAL)
        rect_move_to(&r, r.left, off + first);
    else
        rect_move_to(&r, off + first, r.top);
    ctl_scroll_clamp_thumb(bar, &r);
    thumb->rect = r;
    ctl_scroll_layout(bar, CTL_SCROLL_THUMB, wm_capture() == thumb);
    wm_redraw(bar, NULL);
}

/* 0x4bba71 */
void ctl_scroll_set_range(WmWindow *bar, int min, int max)
{
    if (!bar)
        return;
    WmWindow *thumb = NULL;
    ctl_scroll_parts(bar, NULL, NULL, &thumb);
    if (max == min) {
        wm_show(thumb, false);
        max = min + 1;
    } else {
        wm_show(thumb, true);
    }
    wm_set_data(bar, CTL_SCROLL_MIN, min);
    wm_set_data(bar, CTL_SCROLL_MAX, max);
}

int ctl_scroll_get_pos(WmWindow *bar) { return (int)wm_get_data(bar, CTL_SCROLL_POS); }

/* 0x4bc482 */
void ctl_scroll_clamp_thumb(WmWindow *bar, GfxRect *r)
{
    if (!bar || !r)
        return;
    WmWindow *fwd = bar->children;
    if (!fwd)
        return;
    WmWindow *back = fwd->next;
    if (!back || !back->next)
        return;
    int w = rect_w(r), h = rect_h(r);
    if (wm_get_data(back->next, CTL_SCROLL_STYLE) & CTL_SCROLL_VERTICAL) {
        if (r->top < back->rect.bottom + 1)
            r->top = back->rect.bottom + 1;
        if (r->top > fwd->rect.top - h)
            r->top = fwd->rect.top - h;
        r->bottom = r->top + h - 1;
    } else {
        if (r->left < back->rect.right + 1)
            r->left = back->rect.right + 1;
        if (r->left > fwd->rect.left - w)
            r->left = fwd->rect.left - w;
        r->right = r->left + w - 1;
    }
}

static void draw_part(GfxSurface *s, const WmWindow *part, const GfxSurface *bmp, bool pressed)
{
    if (!part || !bmp)
        return;
    const GfxRect *r = &part->rect;
    gfx_blit(s, r->left, r->top, rect_w(r), rect_h(r), bmp, pressed ? bmp->width >> 1 : 0, 0);
}

/* 0x4c110a */
void ctl_scroll_layout(WmWindow *bar, unsigned kind, bool pressed)
{
    if (!bar || !bar->surface)
        return;
    WmWindow *back = NULL, *fwd = NULL, *thumb = NULL;
    ctl_scroll_parts(bar, &back, &fwd, &thumb);
    GfxSurface **bmp = (wm_get_data(bar, CTL_SCROLL_STYLE) & CTL_SCROLL_VERTICAL) ? g_vbmp : g_hbmp;
    GfxRect r;
    wm_get_local_rect(bar, &r);
    if (bmp[SB_TRACK])
        gfx_tile_fill(bar->surface, r.left, r.top, rect_w(&r), rect_h(&r), bmp[SB_TRACK], 0, 0, bmp[SB_TRACK]->width,
                      bmp[SB_TRACK]->height);
    draw_part(bar->surface, fwd, bmp[SB_FWD], pressed && kind == CTL_SCROLL_FWD);
    draw_part(bar->surface, back, bmp[SB_BACK], pressed && kind == CTL_SCROLL_BACK);
    /* port: a hidden thumb (empty range) isn't drawn; the original draws it
     * anyway */
    if (thumb && (thumb->flags & WM_F_VISIBLE))
        draw_part(bar->surface, thumb, bmp[SB_THUMB], pressed && kind == CTL_SCROLL_THUMB);
}

/* 0x4bbafe */
static void press_part(WmWindow *part, unsigned kind, bool pressed, bool redraw)
{
    if (!part)
        return;
    ctl_scroll_layout(part->parent, kind, pressed);
    if (redraw)
        wm_redraw(part->parent, NULL);
}

/* Position for a thumb at `off` pixels into the travel (0x4bbb4a, shared
 * by the drag and the track click). */
static int pos_at(WmWindow *bar, WmWindow *back, WmWindow *fwd, int tw, int th, int at, bool vertical)
{
    int min = (int)wm_get_data(bar, CTL_SCROLL_MIN), max = (int)wm_get_data(bar, CTL_SCROLL_MAX);
    int travel, off;
    if (vertical) {
        travel = fwd->rect.top - th - back->rect.bottom;
        off = at - back->rect.bottom;
    } else {
        travel = fwd->rect.left - tw - back->rect.right;
        off = at - back->rect.right;
    }
    if (off < 0)
        off = 0;
    if (off > travel)
        off = travel;
    return (max - min + 1) * off / (travel + 1) + min;
}

/* 0x4bbb4a */
intptr_t ctl_scroll_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    unsigned kind = (unsigned)wm_get_data(w, CTL_SCROLL_KIND);
    unsigned style = (unsigned)wm_get_data(w, CTL_SCROLL_STYLE);
    bool vertical = (style & CTL_SCROLL_VERTICAL) != 0;
    WmWindow *owner = NULL;
    if (kind == CTL_SCROLL_TRACK)
        owner = w->parent;
    else if (kind >= CTL_SCROLL_THUMB && kind <= CTL_SCROLL_BACK && w->parent)
        owner = w->parent->parent;
    switch (msg) {
    case WM_MSG_MOUSEMOVE:
        /* dragging the thumb: report the position under the grab point */
        if (g_dragging && w->parent) {
            WmWindow *bar = w->parent, *back = NULL, *fwd = NULL;
            ctl_scroll_parts(bar, &back, &fwd, NULL);
            if (!back || !fwd)
                break;
            int pos = (int)wm_get_data(bar, CTL_SCROLL_POS);
            int tw = rect_w(&w->rect), th = rect_h(&w->rect);
            int at = vertical ? w->rect.top + (int)b - g_drag_y : w->rect.left + (int)a - g_drag_x;
            int np = pos_at(bar, back, fwd, tw, th, at, vertical);
            wm_post(bar->parent, bar, vertical ? WM_MSG_VTRACK : WM_MSG_HTRACK, np, pos - np);
        }
        break;
    case WM_MSG_LBUTTONDOWN:
        switch (kind) {
        case CTL_SCROLL_TRACK: {
            /* jump: the thumb centred on the click */
            WmWindow *back = NULL, *fwd = NULL, *thumb = NULL;
            ctl_scroll_parts(w, &back, &fwd, &thumb);
            if (!back || !fwd || !thumb)
                break;
            int pos = (int)wm_get_data(w, CTL_SCROLL_POS);
            int tw = rect_w(&thumb->rect), th = rect_h(&thumb->rect);
            int at = vertical ? (int)b - (th >> 1) : (int)a - (tw >> 1);
            int np = pos_at(w, back, fwd, tw, th, at, vertical);
            wm_send(owner, w, vertical ? WM_MSG_VTRACK : WM_MSG_HTRACK, np, pos - np);
            /* the original then converts the point to the thumb and passes
             * the click on to the default handling as the thumb's */
            int x = (int)a, y = (int)b;
            wm_point_to_screen(w, &x, &y);
            wm_point_to_local(thumb, &x, &y);
            return wm_default_proc(thumb, msg, x, y);
        }
        case CTL_SCROLL_THUMB:
            press_part(w, kind, true, true);
            g_dragging = true;
            wm_set_capture(w);
            g_drag_x = (int)a;
            g_drag_y = (int)b;
            break;
        case CTL_SCROLL_FWD:
        case CTL_SCROLL_BACK: {
            wm_set_capture(w);
            ctl_repeat_arrow = w;
            ctl_repeat_delay = 5;
            WmWindow *bar = w->parent;
            int pos = (int)wm_get_data(bar, CTL_SCROLL_POS);
            wm_set_data(bar, CTL_SCROLL_LAST, pos);
            press_part(w, kind, true, true);
            int d = kind == CTL_SCROLL_FWD ? 1 : -1;
            wm_post(owner, bar, vertical ? WM_MSG_VSCROLL : WM_MSG_HSCROLL, pos + d, d);
            break;
        }
        }
        break;
    case WM_MSG_LBUTTONUP:
        switch (kind) {
        case CTL_SCROLL_THUMB:
            press_part(w, kind, false, true);
            g_dragging = false;
            wm_release_capture();
            break;
        case CTL_SCROLL_FWD:
        case CTL_SCROLL_BACK:
            wm_release_capture();
            ctl_repeat_arrow = NULL;
            press_part(w, kind, false, true);
            break;
        }
        break;
    case WM_MSG_SETRECT:
        /* the track moved or was resized: put the arrows at the ends */
        if (kind == CTL_SCROLL_TRACK && b) {
            WmWindow *back = NULL, *fwd = NULL;
            for (WmWindow *c = w->children; c; c = c->next) {
                intptr_t k = wm_get_data(c, CTL_SCROLL_KIND);
                if (k == CTL_SCROLL_BACK)
                    back = c;
                else if (k == CTL_SCROLL_FWD)
                    fwd = c;
            }
            if (!back || !fwd)
                break;
            GfxRect nr = *(const GfxRect *)b;
            rect_move_to(&nr, 0, 0);
            GfxRect br, fr;
            wm_get_local_rect(back, &br);
            wm_get_local_rect(fwd, &fr);
            rect_move_to(&br, nr.left, nr.top);
            if (vertical)
                rect_move_to(&fr, nr.left, nr.bottom - rect_h(&fr) + 1);
            else
                rect_move_to(&fr, nr.right - rect_w(&fr) + 1, nr.top);
            rect_move_to(&back->rect, br.left, br.top);
            rect_move_to(&fwd->rect, fr.left, fr.top);
            ctl_scroll_layout(w, 0, false);
        }
        break;
    case WM_MSG_DESTROY:
        /* port-only: forget the window if it is the one being repeated */
        if (ctl_repeat_arrow == w)
            ctl_repeat_arrow = NULL;
        if (g_dragging && wm_capture() == w)
            g_dragging = false;
        break;
    }
    return wm_default_proc(w, msg, a, b);
}
