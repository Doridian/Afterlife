/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The "Debug" window (0x457b01-0x45848f), a developers' tile browser. See
 * ui/debugwin.h: unreachable in the original, opened by the port's
 * --debug-window.
 */
#include "ui/debugwin.h"

#include <string.h>

#include "game/build.h"
#include "game/world.h"
#include "gfx/draw.h"
#include "gfx/surface.h"
#include "gfx/tile_draw.h"
#include "ui/controls.h"
#include "ui/views.h"

#define TEXT_TITLE "Debug" /* 0x65fe04 */

#define BMP_FRAME 0x262 /* 610 */
#define BMP_UP 0x89
#define BMP_DOWN 0x8a
#define BMP_CLOSE 0x259

/* the preview box */
#define PREVIEW_X 3
#define PREVIEW_Y 0x14
#define PREVIEW_W 0x74
#define PREVIEW_H 0x8b

#define SET_COUNT 30 /* 0x1e */

/* 0x457d98's switch: each set's index range when it is stepped to (set 11
 * is replaced by 12 before the lookup) */
static const struct {
    int8_t min, max;
} k_set_range[SET_COUNT + 1] = {
    [1] = {0, 1},     [2] = {0, 1},     [3] = {0, 1},     [4] = {0, 9},     [5] = {0, 5},   [6] = {0, 1},
    [7] = {0, 7},     [8] = {0, 5},     [9] = {0, 5},     [10] = {0x11, 0x14}, [11] = {0, 1}, [12] = {0, 1},
    [13] = {0, 1},    [14] = {0, 0x6f}, [15] = {0, 0x6f}, [16] = {0, 10},   [17] = {0, 8},  [18] = {0, 4},
    [19] = {0, 3},    [20] = {0, 1},    [21] = {0, 1},    [22] = {0, 1},    [23] = {0, 1},  [24] = {0, 1},
    [25] = {0, 1},    [26] = {0, 1},    [27] = {0, 1},    [28] = {0, 1},    [29] = {0, 1},  [30] = {0, 2},
};

/* initial values of 0x65edd0..0x65eddc */
static const DebugwinState k_start = {.index_min = 0, .index_max = 0x6f, .set = 14, .index = 1};

static struct {
    WmWindow *win;                            /* 0x639244 */
    WmWindow *button[DEBUGWIN_LAST_ID + 1];   /* 0x63909c, by id */
    DebugwinState st;
    bool dragging;                            /* 0x4f30fc */
    int grab_x, grab_y;                       /* 0x4f31ac / 0x4f3138 */
} dw = {.st = {.index_min = 0, .index_max = 0x6f, .set = 14, .index = 1}}; /* = k_start */

WmWindow *debugwin_window(void) { return dw.win; }

WmWindow *debugwin_button(int id)
{
    return id >= DEBUGWIN_FIRST_ID && id <= DEBUGWIN_LAST_ID ? dw.button[id] : NULL;
}

DebugwinState debugwin_state(void) { return dw.st; }
void debugwin_set_state(DebugwinState s) { dw.st = s; }

bool debugwin_requested(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (argv[i] && strcmp(argv[i], "--debug-window") == 0)
            return true;
    return false;
}

/* 0x457d98 */
void debugwin_step(int dset, int dindex)
{
    int8_t set = dw.st.set;
    if (dset != 0) {
        set = (int8_t)(dset + set);
        if (set > SET_COUNT)
            set = 1;
        if (set < 1)
            set = SET_COUNT;
        if (set == 11)
            set = 12;
        dw.st.index_min = k_set_range[set].min;
        dw.st.index_max = k_set_range[set].max;
    }
    int8_t i = (int8_t)(dindex + dw.st.index);
    if (dw.st.index_max <= i)
        i = dw.st.index_max;
    dw.st.index = dw.st.index_min <= i ? i : dw.st.index_min;
    dw.st.set = set;
    if (!dw.win || !dw.win->surface)
        return; /* (the original has no such check) */

    /* A local copy of the main view's geometry; the original leaves the
     * fields it doesn't set (focus, plane, flags, ...) as stack garbage.
     * The port uses the Microview's choices: no focus, all sets, the
     * zone sets drawn as they are. */
    const MapView *mv = views_map_view();
    MapView v;
    memset(&v, 0, sizeof v);
    v.width = mv ? mv->width : 100;
    v.height = mv ? mv->height : 100;
    v.clip_x = PREVIEW_X;
    v.clip_y = PREVIEW_Y;
    v.clip_w = PREVIEW_W;
    v.clip_h = PREVIEW_H;
    v.cell_w = mv ? mv->cell_w : 32;
    v.cell_h = mv ? mv->cell_h : 16;
    v.key = mv ? mv->key : 0;
    v.zoom = 0;
    v.magnify = 0;
    v.dir_x = mv ? mv->dir_x : 1;
    v.dir_y = 1;
    v.cells = g_world.cells;
    v.focus_x = v.focus_y = -1;
    v.set_mask = 0xffffffffu;
    v.plane = (uint8_t)g_world.plane;
    v.flags = MAPVIEW_F_ZONE_DETAIL;
    gfx_fill_rect(dw.win->surface, PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, 0);
    tile_draw_preview(dw.win->surface, &v, (unsigned)dw.st.set, (unsigned)(dw.st.index + 1), 0, NULL);
    wm_redraw(dw.win, NULL);
}

/* 0x457b01 */
bool debugwin_create(void)
{
    if (dw.win)
        return true; /* (the original makes a second window inside the first) */
    GfxSurface *bg = gfx_load_bmp_resource(BMP_FRAME, NULL);
    if (!bg)
        return false;
    GfxRect r = {0xaa, bg->width + 0xa9, 0x28, bg->height + 0x27};
    /* The original passes its own window pointer (still NULL) as the
     * parent, so the window would sit on the root, where the main window
     * covers it whenever it is activated again (after any dialog). The
     * port makes it a child of the main window, like the game's other
     * windows; the main window is at 0, 0, so the place is the same. */
    dw.win = wm_create_window(views_main_window(), &r, debugwin_proc, WM_F_NOFOCUS, 0);
    if (!dw.win) {
        gfx_surface_destroy(bg);
        return false;
    }
    wm_set_title(dw.win, TEXT_TITLE);
    gfx_blit(dw.win->surface, 0, 0, bg->width, bg->height, bg, 0, 0);
    gfx_surface_destroy(bg);
    static const struct {
        int x, y;
        uint32_t bmp;
    } k_buttons[] = {
        [DEBUGWIN_SET_UP - DEBUGWIN_FIRST_ID] = {0xd2, 0x22, BMP_UP},
        [DEBUGWIN_SET_DOWN - DEBUGWIN_FIRST_ID] = {0x7f, 0x22, BMP_DOWN},
        [DEBUGWIN_INDEX_UP - DEBUGWIN_FIRST_ID] = {0xd2, 0x4b, BMP_UP},
        [DEBUGWIN_INDEX_DOWN - DEBUGWIN_FIRST_ID] = {0x7f, 0x4b, BMP_DOWN},
        [DEBUGWIN_CLOSE - DEBUGWIN_FIRST_ID] = {0, 0, BMP_CLOSE},
    };
    for (int id = DEBUGWIN_FIRST_ID; id <= DEBUGWIN_LAST_ID; id++) {
        int k = id - DEBUGWIN_FIRST_ID;
        GfxSurface *bmp = gfx_load_bmp_resource(k_buttons[k].bmp, NULL);
        if (!bmp)
            continue;
        GfxRect br = {k_buttons[k].x, bmp->width + k_buttons[k].x - 1, k_buttons[k].y,
                      bmp->height + k_buttons[k].y - 1};
        /* the button keeps the bitmap (the original copies it and frees
         * its own) */
        dw.button[id] = ctl_button_create(dw.win, &br, (uint32_t)id, CTL_STYLE_BITMAP, bmp);
        if (!dw.button[id])
            gfx_surface_destroy(bmp);
        if (id == DEBUGWIN_CLOSE)
            dw.win->close_button = dw.button[id];
    }
    return true;
}

void debugwin_destroy(void)
{
    if (dw.win) {
        if (dw.dragging)
            wm_release_capture();
        wm_show(dw.win, false);
        wm_destroy(dw.win);
    }
    memset(&dw, 0, sizeof dw);
    dw.st = k_start;
}

bool debugwin_open(void)
{
    if (!debugwin_create())
        return false;
    wm_show(dw.win, true);
    wm_raise(dw.win);
    return true;
}

/* 0x458200 */
intptr_t debugwin_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_MOUSEMOVE:
        if (dw.dragging) {
            GfxRect r = w->rect;
            rect_move_to(&r, (int)a - dw.grab_x, (int)b - dw.grab_y);
            wm_local_to_screen(w, &r);
            if (w->parent) /* (the original's parent is the root, at 0, 0) */
                wm_screen_to_local(w->parent, &r);
            wm_set_rect(w, &r);
        }
        break;
    case WM_MSG_LBUTTONDOWN:
        wm_set_capture(w);
        wm_raise(w);
        dw.grab_x = (int)a;
        dw.grab_y = (int)b;
        dw.dragging = true;
        break;
    case WM_MSG_LBUTTONUP:
        wm_release_capture();
        dw.dragging = false;
        break;
    case WM_MSG_CTL_CLICK: /* w = the button, a = its id */
        switch (a) {
        case DEBUGWIN_SET_UP: debugwin_step(1, 0); break;
        case DEBUGWIN_SET_DOWN: debugwin_step(-1, 0); break;
        case DEBUGWIN_INDEX_UP: debugwin_step(0, 1); break;
        case DEBUGWIN_INDEX_DOWN: debugwin_step(0, -1); break;
        case DEBUGWIN_CLOSE:
            g_build.editor = 0;
            wm_show(dw.win, false);
            break;
        }
        break;
    case WM_MSG_CTL_RUP:
        switch (a) {
        case DEBUGWIN_SET_UP: debugwin_step(10, 0); break;
        case DEBUGWIN_SET_DOWN: debugwin_step(-10, 0); break;
        case DEBUGWIN_INDEX_UP: debugwin_step(0, 10); break;
        case DEBUGWIN_INDEX_DOWN: debugwin_step(0, -10); break;
        }
        break;
    case WM_MSG_DESTROY:
        if (w == dw.win) { /* the buttons go with it */
            dw.win = NULL;
            memset(dw.button, 0, sizeof dw.button);
        }
        break;
    }
    return wm_default_proc(w, msg, a, b);
}
