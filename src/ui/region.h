/*
 * Rectangle lists ("regions") of the window manager (0x4c4a61-0x4c50ae).
 *
 * There is no dirty-rectangle bitmap or clip region object in the game: to
 * repaint part of the screen, the WM builds a list of disjoint rectangles,
 * each tagged with the front-most visible window covering it
 * (rgn_build_visible), and sends each owner WM_MSG_PAINT for its piece
 * (rgn_paint). The list cells come from a fixed pool of 1000 nodes
 * (0x5247a8, 0x30 bytes each in the original; free list 0x53516c). When the
 * pool runs dry, the original silently drops pieces, and so does the port.
 */
#ifndef AFTERLIFE_UI_REGION_H
#define AFTERLIFE_UI_REGION_H

#include <stdbool.h>

#include "gfx/surface.h"
#include "ui/wm.h"

#define RGN_POOL_SIZE 1000

struct RgnNode {
    RgnNode *next, *prev; /* +0x00 +0x04 */
    WmWindow *owner;      /* +0x08 */
    GfxRect r;            /* +0x0c screen coordinates (inclusive) */
};

/* Port-only: (re)fills the free list (wm_initialize does this, 0x4c1580). */
void rgn_pool_init(void);
int rgn_pool_free_count(void);

/* 0x4c4add: a single node for `r`, or for the window's screen rect if r
 * is NULL. NULL if the pool is empty (or r and owner are both NULL). */
RgnNode *rgn_alloc(WmWindow *owner, const GfxRect *r);
/* 0x4c4b9f: returns a whole list to the pool */
void rgn_free(RgnNode *list);
/* 0x4c4a61: sets *r from two corners in any order */
void rgn_rect_set(GfxRect *r, int x0, int x1, int y0, int y1);
/* 0x4c4cc0: intersection of a and b into *out (if non-NULL; written even
 * when empty). True if it isn't empty. */
bool rgn_intersect(GfxRect *out, const GfxRect *a, const GfxRect *b);
/* 0x4c4d9a: appends to `list` the parts of `node`'s rect outside `hole`
 * (which must lie inside it): the band above, the parts left and right of
 * the hole, and the band below, each owned by `owner`. Returns the list. */
RgnNode *rgn_split(RgnNode *list, const RgnNode *node, const GfxRect *hole, WmWindow *owner);
/* 0x4c5017: removes `r` from every node of `list`; the remaining pieces go
 * to `owner`. No callers in the game. */
RgnNode *rgn_subtract(WmWindow *owner, RgnNode *list, const GfxRect *r);
/* 0x4c50ae: splits screen rectangle `r` (or `list`, if given) among the
 * windows of sibling list `windows` and (if `recurse`) their descendants,
 * so that each piece is owned by the front-most visible window over it
 * (lowest z). Starts from one piece owned by the root. */
RgnNode *rgn_build_visible(WmWindow *windows, const GfxRect *r, RgnNode *list, bool recurse);
/* 0x4c476a: sends each piece's owner WM_MSG_PAINT with the piece in its
 * local coordinates */
void rgn_paint(const RgnNode *list);

/* Port-only: list helpers (0x4d11c1 list_append, 0x4d124d list_remove) */
RgnNode *rgn_append(RgnNode *list, RgnNode *node);
RgnNode *rgn_remove(RgnNode *list, RgnNode *node);

#endif
