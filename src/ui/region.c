/*
 * Window manager rectangle lists (0x4c4a61-0x4c50ae). See region.h.
 */
#include "ui/region.h"

#include <stddef.h>

static RgnNode g_pool[RGN_POOL_SIZE]; /* 0x5247a8 */
static RgnNode *g_free;               /* 0x53516c */

void rgn_pool_init(void)
{
    g_free = NULL;
    for (int i = RGN_POOL_SIZE - 1; i >= 0; i--) {
        g_pool[i].next = g_free;
        g_pool[i].prev = NULL;
        if (g_free)
            g_free->prev = &g_pool[i];
        g_free = &g_pool[i];
    }
}

int rgn_pool_free_count(void)
{
    int n = 0;
    for (RgnNode *p = g_free; p; p = p->next)
        n++;
    return n;
}

/* list_pop_head (0x4d10d1) on the free list */
static RgnNode *pool_pop(void)
{
    RgnNode *n = g_free;
    if (!n)
        return NULL;
    g_free = n->next;
    if (g_free)
        g_free->prev = NULL;
    n->next = n->prev = NULL;
    return n;
}

/* list_push_free on the free list */
static void pool_push(RgnNode *n)
{
    n->prev = NULL;
    n->next = g_free;
    if (g_free)
        g_free->prev = n;
    g_free = n;
}

RgnNode *rgn_append(RgnNode *list, RgnNode *node)
{
    node->next = NULL;
    if (!list) {
        node->prev = NULL;
        return node;
    }
    RgnNode *last = list;
    while (last->next)
        last = last->next;
    last->next = node;
    node->prev = last;
    return list;
}

RgnNode *rgn_remove(RgnNode *list, RgnNode *node)
{
    if (node->prev)
        node->prev->next = node->next;
    if (node->next)
        node->next->prev = node->prev;
    if (list == node)
        list = node->next;
    node->next = node->prev = NULL;
    return list;
}

/* 0x4c4a61 */
void rgn_rect_set(GfxRect *r, int x0, int x1, int y0, int y1)
{
    if (!r)
        return;
    if (x1 < x0) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y1 < y0) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    r->left = x0;
    r->right = x1;
    r->top = y0;
    r->bottom = y1;
}

/* 0x4c4add */
RgnNode *rgn_alloc(WmWindow *owner, const GfxRect *r)
{
    GfxRect tmp;
    if (!r) {
        if (!owner)
            return NULL;
        tmp = owner->rect;
        wm_parent_to_screen(owner, &tmp);
        r = &tmp;
    }
    RgnNode *n = pool_pop();
    if (!n)
        return NULL;
    n->owner = owner;
    n->r = *r;
    return n;
}

/* 0x4c4b9f */
void rgn_free(RgnNode *list)
{
    while (list) {
        RgnNode *next = list->next;
        pool_push(list);
        list = next;
    }
}

/* 0x4c4cc0 */
bool rgn_intersect(GfxRect *out, const GfxRect *a, const GfxRect *b)
{
    if (!a || !b)
        return false;
    GfxRect i;
    i.left = a->left > b->left ? a->left : b->left;
    i.top = a->top > b->top ? a->top : b->top;
    i.right = a->right < b->right ? a->right : b->right;
    i.bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
    if (out)
        *out = i;
    return i.left <= i.right && i.top <= i.bottom;
}

static RgnNode *add_piece(RgnNode *list, int l, int r, int t, int b, WmWindow *owner, bool *ok)
{
    RgnNode *n = pool_pop();
    if (!n) {
        *ok = false;
        return list;
    }
    n->r.left = l;
    n->r.right = r;
    n->r.top = t;
    n->r.bottom = b;
    n->owner = owner;
    return rgn_append(list, n);
}

/* 0x4c4d9a */
RgnNode *rgn_split(RgnNode *list, const RgnNode *node, const GfxRect *hole, WmWindow *owner)
{
    const GfxRect *n = &node->r;
    bool ok = true;
    /* above: full width */
    if (n->left <= n->right && n->top <= hole->top - 1)
        list = add_piece(list, n->left, n->right, n->top, hole->top - 1, owner, &ok);
    if (!ok)
        return list;
    /* left of the hole */
    if (n->left <= hole->left - 1 && hole->top <= hole->bottom)
        list = add_piece(list, n->left, hole->left - 1, hole->top, hole->bottom, owner, &ok);
    if (!ok)
        return list;
    /* right of the hole */
    if (hole->right + 1 <= n->right && hole->top <= hole->bottom)
        list = add_piece(list, hole->right + 1, n->right, hole->top, hole->bottom, owner, &ok);
    if (!ok)
        return list;
    /* below: full width */
    if (n->left <= n->right && hole->bottom + 1 <= n->bottom)
        list = add_piece(list, n->left, n->right, hole->bottom + 1, n->bottom, owner, &ok);
    return list;
}

/* 0x4c5017 */
RgnNode *rgn_subtract(WmWindow *owner, RgnNode *list, const GfxRect *r)
{
    RgnNode *p = list;
    while (p) {
        RgnNode *next = p->next;
        GfxRect hole;
        if (rgn_intersect(&hole, r, &p->r)) {
            list = rgn_split(list, p, &hole, owner);
            /* pieces appended after p are visited too; they don't
             * intersect r, so that's harmless */
            next = p->next;
            list = rgn_remove(list, p);
            pool_push(p);
        }
        p = next;
    }
    return list;
}

/* 0x4c50ae */
RgnNode *rgn_build_visible(WmWindow *windows, const GfxRect *r, RgnNode *list, bool recurse)
{
    if (!list)
        list = rgn_alloc(wm_root(), r);
    if (!list)
        return NULL;
    for (WmWindow *w = windows; w; w = w->next) {
        if (!(w->flags & WM_F_VISIBLE))
            continue;
        if (recurse && w->children) {
            /* the original calls snd_idle here to keep the streams fed */
            if (wm_hooks.sound_idle)
                wm_hooks.sound_idle();
            list = rgn_build_visible(w->children, r, list, recurse);
        }
        if (w->flags & WM_F_NOCLIP)
            continue;
        GfxRect wr = w->rect;
        wm_parent_to_screen(w, &wr);
        for (RgnNode *p = list; p; p = p->next) {
            GfxRect hole;
            if (rgn_intersect(&hole, &wr, &p->r) && p->owner != w && w->z < p->owner->z) {
                /* the rest stays with the old owner, the overlap becomes w's */
                list = rgn_split(list, p, &hole, p->owner);
                p->owner = w;
                p->r = hole;
            }
        }
    }
    return list;
}

/* 0x4c476a */
void rgn_paint(const RgnNode *list)
{
    for (; list; list = list->next) {
        WmWindow *w = list->owner;
        GfxRect r = list->r;
        wm_screen_to_local(w, &r);
        wm_send(w, w, WM_MSG_PAINT, (intptr_t)&r, 0);
    }
}
