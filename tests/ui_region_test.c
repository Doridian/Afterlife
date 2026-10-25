/*
 * Unit tests for the window manager's rectangle lists and stacking
 * (src/ui/region.c, src/ui/wm.c). No game data needed; runs headless.
 *
 *   ui_region_test
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx/surface.h"
#include "platform/platform.h"
#include "ui/region.h"
#include "ui/wm.h"

static int g_failures;

#define CHECK(cond)                                                                                                \
    do {                                                                                                           \
        if (!(cond)) {                                                                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                       \
            g_failures++;                                                                                          \
        }                                                                                                          \
    } while (0)

static bool rect_eq(const GfxRect *r, int l, int rt, int t, int b)
{
    return r->left == l && r->right == rt && r->top == t && r->bottom == b;
}

static int list_len(const RgnNode *n)
{
    int c = 0;
    for (; n; n = n->next)
        c++;
    return c;
}

static void test_intersect(void)
{
    GfxRect a = {0, 9, 0, 9}, b = {5, 14, 3, 20}, o;
    CHECK(rgn_intersect(&o, &a, &b) && rect_eq(&o, 5, 9, 3, 9));
    GfxRect c = {10, 12, 0, 9}; /* touching at x = 10 vs 9: disjoint */
    CHECK(!rgn_intersect(&o, &a, &c));
    CHECK(o.left == 10 && o.right == 9); /* written even when empty */
    GfxRect d = {9, 9, 9, 9};            /* a single shared pixel */
    CHECK(rgn_intersect(&o, &a, &d) && rect_eq(&o, 9, 9, 9, 9));
    CHECK(rgn_intersect(NULL, &a, &a));
    CHECK(!rgn_intersect(&o, NULL, &a));
    GfxRect r;
    rgn_rect_set(&r, 7, 2, 9, 1);
    CHECK(rect_eq(&r, 2, 7, 1, 9));
}

static void test_split(void)
{
    rgn_pool_init();
    int free0 = rgn_pool_free_count();
    CHECK(free0 == RGN_POOL_SIZE);
    GfxRect whole = {0, 9, 0, 9};
    RgnNode *n = rgn_alloc(NULL, &whole);
    CHECK(n && rgn_pool_free_count() == free0 - 1);

    /* hole in the middle: above, left, right, below */
    GfxRect hole = {3, 5, 4, 6};
    RgnNode *l = rgn_split(NULL, n, &hole, NULL);
    CHECK(list_len(l) == 4);
    const RgnNode *p = l;
    CHECK(rect_eq(&p->r, 0, 9, 0, 3));
    p = p->next;
    CHECK(rect_eq(&p->r, 0, 2, 4, 6));
    p = p->next;
    CHECK(rect_eq(&p->r, 6, 9, 4, 6));
    p = p->next;
    CHECK(rect_eq(&p->r, 0, 9, 7, 9));
    rgn_free(l);

    /* hole in the top-left corner: right and below only */
    GfxRect corner = {0, 4, 0, 4};
    l = rgn_split(NULL, n, &corner, NULL);
    CHECK(list_len(l) == 2);
    CHECK(rect_eq(&l->r, 5, 9, 0, 4));
    CHECK(rect_eq(&l->next->r, 0, 9, 5, 9));
    rgn_free(l);

    /* hole == node: nothing left */
    l = rgn_split(NULL, n, &whole, NULL);
    CHECK(l == NULL);

    /* full-height hole in the middle: left and right */
    GfxRect band = {4, 5, 0, 9};
    l = rgn_split(NULL, n, &band, NULL);
    CHECK(list_len(l) == 2 && rect_eq(&l->r, 0, 3, 0, 9) && rect_eq(&l->next->r, 6, 9, 0, 9));

    /* subtract a rect crossing both pieces */
    GfxRect cut = {2, 7, 2, 3};
    l = rgn_subtract(NULL, l, &cut);
    /* left {0..3} minus {2..3,2..3}: above {0..3,0..1}, left {0..1,2..3},
     * below {0..3,4..9}; right {6..9} minus {6..7,2..3}: above
     * {6..9,0..1}, right {8..9,2..3}, below {6..9,4..9} */
    CHECK(list_len(l) == 6);
    int area = 0;
    for (p = l; p; p = p->next)
        area += (p->r.right - p->r.left + 1) * (p->r.bottom - p->r.top + 1);
    CHECK(area == 80 - 8);
    rgn_free(l);
    rgn_free(n);
    CHECK(rgn_pool_free_count() == free0);

    /* exhausting the pool: allocation fails, nothing crashes */
    RgnNode *all = NULL;
    for (int i = 0; i < RGN_POOL_SIZE; i++)
        all = rgn_append(all, rgn_alloc(NULL, &whole));
    CHECK(rgn_alloc(NULL, &whole) == NULL);
    CHECK(rgn_split(NULL, all, &hole, NULL) == NULL);
    rgn_free(all);
    CHECK(rgn_pool_free_count() == free0);
}

/* A proc that records paint rectangles. */
typedef struct PaintLog {
    WmWindow *w;
    GfxRect r;
} PaintLog;
static PaintLog g_log[64];
static int g_nlog;

static intptr_t log_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    if (msg == WM_MSG_PAINT && g_nlog < 64) {
        g_log[g_nlog].w = w;
        g_log[g_nlog].r = *(const GfxRect *)a;
        g_nlog++;
    }
    return wm_default_proc(w, msg, a, b);
}

static WmWindow *make(WmWindow *parent, int l, int r, int t, int b, uint32_t flags)
{
    GfxRect rc = {l, r, t, b};
    WmWindow *w = wm_create_window(parent, &rc, log_proc, flags | WM_F_NOTITLE, 0);
    CHECK(w != NULL);
    return w;
}

static void test_build(void)
{
    CHECK(wm_initialize() == 0);
    WmWindow *root = wm_root();
    CHECK(root && rect_eq(&root->rect, 0, 99, 0, 99));
    WmWindow *a = make(NULL, 10, 49, 10, 49, 0);
    WmWindow *b = make(NULL, 30, 69, 30, 69, 0);
    /* new windows go behind their siblings */
    CHECK(root->children == a && a->next == b);
    CHECK(a->z < b->z && b->z < root->z);
    wm_show(a, true);
    wm_show(b, true);
    wm_raise(b);
    CHECK(root->children == b && b->z < a->z);

    /* the hand-computed split of the whole screen (see docs/ui.md) */
    GfxRect all = {0, 99, 0, 99};
    RgnNode *l = rgn_build_visible(root, &all, NULL, true);
    CHECK(list_len(l) == 10);
    struct {
        WmWindow *w;
        int l, r, t, b;
    } want[10] = {
        {b, 30, 69, 30, 69},   {a, 10, 49, 10, 29},    {a, 10, 29, 30, 49},   {root, 70, 99, 30, 69},
        {root, 0, 99, 70, 99}, {root, 0, 99, 0, 9},    {root, 0, 9, 10, 29},  {root, 50, 99, 10, 29},
        {root, 0, 9, 30, 49},  {root, 0, 29, 50, 69},
    };
    int i = 0, area = 0;
    for (RgnNode *p = l; p && i < 10; p = p->next, i++) {
        CHECK(p->owner == want[i].w && rect_eq(&p->r, want[i].l, want[i].r, want[i].t, want[i].b));
        area += (p->r.right - p->r.left + 1) * (p->r.bottom - p->r.top + 1);
    }
    CHECK(area == 100 * 100);
    rgn_free(l);

    /* hit testing follows the same order */
    CHECK(wm_window_at(35, 35) == b);
    CHECK(wm_window_at(15, 15) == a);
    CHECK(wm_window_at(80, 5) == root);

    /* lowering b exposes a over the overlap */
    wm_lower(b);
    CHECK(root->children == a && wm_window_at(35, 35) == a);

    /* a child is in front of its parent and moves with it; paint rects
     * arrive in local coordinates */
    WmWindow *c = make(a, 5, 14, 5, 14, 0); /* screen 15..24 */
    wm_show(c, true);
    CHECK(c->z < a->z && wm_window_at(20, 20) == c);
    g_nlog = 0;
    wm_redraw(c, NULL);
    CHECK(g_nlog == 1 && g_log[0].w == c && rect_eq(&g_log[0].r, 0, 9, 0, 9));

    /* repainting a screen rect sends each window its visible part */
    g_nlog = 0;
    GfxRect probe = {20, 39, 20, 39};
    wm_repaint_screen(&probe);
    int got_a = 0, got_b = 0, got_c = 0;
    for (i = 0; i < g_nlog; i++) {
        if (g_log[i].w == a)
            got_a++;
        if (g_log[i].w == b)
            got_b++;
        if (g_log[i].w == c && rect_eq(&g_log[i].r, 5, 9, 5, 9))
            got_c++;
    }
    CHECK(got_a > 0 && got_b == 0 && got_c == 1); /* b is hidden behind a there */

    /* moving: the parent's move carries the child */
    wm_move(a, 50, 50);
    CHECK(wm_window_at(57, 57) == c && wm_window_at(20, 20) != c);
    GfxRect s = {0, 0, 0, 0};
    wm_local_to_screen(c, &s);
    CHECK(s.left == 55 && s.top == 55);

    /* hidden windows and NOCLIP windows don't take part */
    wm_show(a, false);
    CHECK(!wm_is_visible(c) && wm_window_at(57, 57) == b);
    WmWindow *n = make(NULL, 0, 99, 0, 99, WM_F_NOCLIP);
    wm_show(n, true);
    wm_raise(n);
    l = rgn_build_visible(root, &all, NULL, true);
    for (RgnNode *p = l; p; p = p->next)
        CHECK(p->owner != n);
    rgn_free(l);

    /* topmost windows stay in front of normal raises */
    WmWindow *t = make(NULL, 0, 9, 0, 9, WM_F_TOPMOST);
    wm_show(t, true);
    wm_raise(t);
    wm_raise(b);
    CHECK(root->children == t && t->next == b);

    /* the invalid list repaints on wm_update */
    g_nlog = 0;
    GfxRect part = {1, 2, 1, 2};
    wm_invalidate(b, &part);
    CHECK(g_nlog == 0);
    wm_update(b);
    CHECK(g_nlog == 1 && g_log[0].w == b && rect_eq(&g_log[0].r, 1, 2, 1, 2));
    CHECK(b->invalid == NULL);

    /* destroy unlinks; the pool is back to full */
    wm_destroy(a);
    CHECK(root->children == t && t->next == b && b->next == n);
    CHECK(rgn_pool_free_count() == RGN_POOL_SIZE);

    /* posted messages are dispatched one per wm_idle */
    wm_set_input_live(true);
    g_nlog = 0;
    wm_post(b, b, WM_MSG_PAINT, (intptr_t)&part, 0);
    wm_post(b, b, WM_MSG_PAINT, (intptr_t)&part, 0);
    wm_idle();
    CHECK(g_nlog == 1);
    wm_idle();
    CHECK(g_nlog == 2);
    wm_idle();
    CHECK(g_nlog == 2);
    wm_shutdown();
}

int main(void)
{
#ifdef _WIN32
    _putenv_s("SDL_VIDEODRIVER", "dummy");
#else
    SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1);
#endif
    test_intersect();
    test_split();
    if (!plat_init("ui_region_test", 100, 100) || !gfx_init(100, 100, false)) {
        fprintf(stderr, "no video\n");
        return 1;
    }
    test_build();
    gfx_shutdown();
    plat_shutdown();
    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("ui_region_test: all passed\n");
    return 0;
}
