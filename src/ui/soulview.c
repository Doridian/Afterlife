/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The Soulview window (0x479e29-0x47fbf8): see ui/soulview.h. */
#include "ui/soulview.h"

#include <stdio.h>
#include <string.h>

#include "game/build.h"
#include "game/realm.h"
#include "game/sim.h"
#include "game/soultrack.h"
#include "game/world.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "ui/controls.h"
#include "ui/views.h"

#define FONT 500
#define BG 0x13

static WmWindow *g_win;        /* 0x5b2f50 */
static WmWindow *g_btn_pic;    /* 0x664814 */
static WmWindow *g_btn_data;   /* 0x664818 */
static WmWindow *g_btn_track;  /* 0x66481c */
static WmWindow *g_viewer;     /* 0x664820: the story */
static int g_picture = 1;      /* 0x664824 */
static int g_busy;             /* 0x664828 */
static int g_dragging;         /* 0x664958 */
static int g_grab_x, g_grab_y; /* 0x4f33f0, 0x4f33f4 */
static char g_text[0x800];     /* 0x4f3588 */

static const GfxRect status_area = {SOULVIEW_STATUS_LEFT, SOULVIEW_STATUS_RIGHT, SOULVIEW_STATUS_TOP,
                                    SOULVIEW_STATUS_BOTTOM};
static const GfxRect pic_area = {SOULVIEW_PIC_LEFT, SOULVIEW_PIC_RIGHT, SOULVIEW_PIC_TOP, SOULVIEW_PIC_BOTTOM};

static Block26 *blk(void) { return &g_state.block_4f33f8; }
static Rec26 *cur(void) { return soultrack_rec(blk()->selected); }

static void font_colors(unsigned c1, unsigned c2)
{
    unsigned c[2] = {c1 & 0xff, c2 & 0xff};
    gfx_font_set_colors(FONT, 1, 2, c);
}

static void fill(const GfxRect *r, uint8_t c)
{
    gfx_fill_rect(g_win->surface, r->left, r->top, r->right - r->left + 1, r->bottom - r->top + 1, c);
}

static void pump(void)
{
    /* the original's sys_pump calls while busy keep the game responsive;
     * nothing to do here */
}

SoulviewState soulview_state(void) { return (SoulviewState){g_picture != 0, blk()->selected % SOULTRACK_MAX}; }

WmWindow *soulview_window(void) { return g_win; }

/* ---- status lines ------------------------------------------------------------ */

/* sin / virtue names; the original indexes past its tables for the zone
 * kinds above 6 (generic zoning), which gives no name */
static const char *kind_name(int plane, unsigned k)
{
    if (k >= 7)
        return "";
    return plane == GS_PLANE_HELL ? views_sin_names[k] : views_virtue_names[k];
}

/* 0x47f259 */
void soulview_status(unsigned i)
{
    if (!g_win || i != blk()->selected)
        return;
    const Rec26 *t = cur();
    int fh = gfx_font_height(FONT);
    GfxRect r = {status_area.left, status_area.right, status_area.top + 1, 0};
    r.bottom = fh + r.top - 1;
    char name[SOULVIEW_NAME_LEN], buf[256];
    gfx_font_select(FONT);
    font_colors(0x1a, 0x1a);
    fill(&status_area, 100);
    soulview_soul_name(t->unk_08, name);
    snprintf(buf, sizeof buf, "%s - %s  (#%d)" /* 0x66aab4 */, "Soulview" /* 0x66aaa8 */, name,
             (int)blk()->selected + 1);
    wm_set_title(g_win, buf);
    views_draw_title(g_win);
    if (t->unk_00[0] == TRACK_NONE) {
        fill(&pic_area, BG);
        wm_show(g_viewer, false);
        wm_redraw(g_win, NULL);
        return;
    }
    bool hell = t->unk_10 == GS_PLANE_HELL;
    buf[0] = 0;
    switch (t->unk_00[0]) {
    case TRACK_RESIDENT: /* "Punished for ENVY" / "Rewarded for ..." */
        snprintf(buf, sizeof buf, "%sed for %s" /* 0x66abbc */,
                 (hell ? "Punish" /* 0x66abac */ : "Reward" /* 0x66abb4 */), kind_name(t->unk_10, t->unk_11));
        break;
    case TRACK_WALKING: {
        int8_t cat = (int8_t)t->unk_1b;
        if (cat < 14)
            snprintf(buf, sizeof buf, "Walking towards %s" /* 0x66aac4 */, kind_name(t->unk_10, cat & 7));
        else if (cat == 14)
            snprintf(buf, sizeof buf, "%s", "Walking towards Karma Station" /* 0x66aad8 */);
        else if (t->unk_1b == 0xff) /* (not reached: -1 is < 14) */
            snprintf(buf, sizeof buf, "Walking towards %s" /* 0x66aaf8 */, kind_name(t->unk_10, t->unk_11));
        else if (cat == 15)
            snprintf(buf, sizeof buf, "%s", "Walking towards a Gate" /* 0x66ab0c */);
        else
            snprintf(buf, sizeof buf, "%s", "Walking towards unknown" /* 0x66ab24 */);
        break;
    }
    case TRACK_REINCARNATED:
        snprintf(buf, sizeof buf, "%s", "Reincarnated to planet" /* 0x66ab3c */);
        break;
    case TRACK_LOST:
        snprintf(buf, sizeof buf, "%s", "Lost forever" /* 0x66abd4 */);
        break;
    case TRACK_GATE:
        snprintf(buf, sizeof buf, "%s", "Exited through a Gate" /* 0x66abe4 */);
        break;
    case TRACK_TRAINING:
        snprintf(buf, sizeof buf, "Training to become a%s" /* 0x66ab64 */,
                 (hell ? " Demon" /* 0x66ab54 */ : "n Angel" /* 0x66ab5c */));
        break;
    case TRACK_OMNIBOLGE:
        snprintf(buf, sizeof buf, "Disappearing into a%s" /* 0x66ab94 */,
                 (hell ? "n Omnibolge" /* 0x66ab7c */ : " Love Dome" /* 0x66ab88 */));
        break;
    }
    GfxRect a = r;
    gfx_font_draw_text(g_win->surface, r.left, r.top, &a, &a, buf, GFX_TEXT_NOJUSTIFY);
    r.top += fh + 1;
    r.bottom += fh + 1;
    int years = soultrack_years_left(t);
    if (years < 0)
        snprintf(buf, sizeof buf, "%s", "Permanent resident" /* 0x66abfc */);
    else
        snprintf(buf, sizeof buf, "Years left at location:  %d" /* 0x66ac10 */, years);
    a = r;
    gfx_font_draw_text(g_win->surface, r.left, r.top, &a, &a, buf, GFX_TEXT_NOJUSTIFY);
    r.top += fh + 1;
    r.bottom += fh + 1;
    snprintf(buf, sizeof buf, "Believes in %sism" /* 0x66ac2c */, t->unk_12 < 16 ? views_belief_names[t->unk_12] : "");
    a = r;
    gfx_font_draw_text(g_win->surface, r.left, r.top, &a, &a, buf, GFX_TEXT_NOJUSTIFY);
    wm_redraw(g_win, &status_area);
}

/* ---- picture and story ----------------------------------------------------------- */

/* 0x47aca3 */
static uint32_t bits(uint32_t v, unsigned shift, unsigned n)
{
    uint32_t m = (1u << ((shift + n) & 31)) - 1u;
    return (v & m) >> (shift & 31);
}

/* 0x47ab67: copies the non-zero pixels of `src` to (x, y), remapping the
 * skin colours 0xaa..0xae to shades of `tint` */
static void blit_tinted(GfxSurface *dst, int x, int y, const GfxSurface *src, uint8_t tint)
{
    for (int j = 0; j < src->height; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= dst->height)
            continue;
        const uint8_t *sp = src->pixels + (size_t)j * (size_t)src->stride;
        uint8_t *dp = dst->pixels + (size_t)dy * (size_t)dst->stride;
        for (int i = 0; i < src->width; i++) {
            int dx = x + i;
            uint8_t c = sp[i];
            if (c == 0 || dx < 0 || dx >= dst->width)
                continue;
            if (tint != 0 && c >= 0xaa && c <= 0xae)
                c = c < 0xae ? (uint8_t)(tint - (c * 2 - 0x54)) : (uint8_t)(tint - 7);
            dp[dx] = c;
        }
    }
    gfx_touch(dst, x, x + src->width - 1, y, y + src->height - 1);
}

static void layer(uint32_t id, uint8_t tint)
{
    GfxSurface *s = gfx_load_bmp_resource(id, NULL);
    if (!s)
        return;
    blit_tinted(g_win->surface, pic_area.left, pic_area.top, s, tint);
    gfx_surface_destroy(s);
}

/* 0x47d111: the face, five layers (head, ..., hair) by seed bits */
static void draw_picture(const Rec26 *t)
{
    static const uint8_t tints[4] = {0x5f, 0x57, 0x67, 0x7f};
    uint32_t seed = t->unk_08;
    fill(&pic_area, BG);
    if (t->unk_00[0] == TRACK_NONE) {
        wm_show(g_viewer, false);
        return;
    }
    uint8_t tint = tints[(seed & 1) | ((seed >> 17) & 1) << 1];
    bool female = bits(seed, 16, 1) != 0;
    layer((female ? 0x302 : 0x2da) + bits(seed, 0, 3) % 5, tint);
    layer((female ? 0x2e4 : 700) + bits(seed, 3, 3) % 5, tint);
    layer(0x316 + bits(seed, 6, 3) % 5, tint);
    layer((female ? 0x2ee : 0x2c6) + bits(seed, 9, 3) % 5, tint);
    layer(female ? 0x2f8 + (bits(seed, 12, 3) & 3) : 0x2d0 + bits(seed, 12, 3) % 5, tint);
    wm_show(g_viewer, false);
}

/* 0x47adea */
static void draw_story(const Rec26 *t)
{
    gfx_font_select(FONT);
    font_colors(0x1a, 0x1a);
    fill(&pic_area, BG);
    if (t->unk_00[0] == TRACK_NONE) {
        wm_show(g_viewer, false);
        return;
    }
    soulview_describe(t, g_text, sizeof g_text);
    wm_send(g_viewer, g_viewer, CTL_VIEW_SET_TEXT, (intptr_t)g_text, 0);
    wm_send(g_viewer, g_viewer, CTL_VIEW_REDRAW, 0, 0);
    if (!wm_is_minimized(g_win))
        wm_show(g_viewer, true);
}

/* 0x47e6cc */
void soulview_refresh(void)
{
    if (!g_win)
        return;
    soulview_status(blk()->selected);
    if (g_busy)
        pump();
    if (!g_picture)
        draw_story(cur());
    else
        draw_picture(cur());
    if (g_busy)
        pump();
    if (cur()->unk_00[0] == TRACK_NONE)
        ctl_button_set_pressed(g_btn_track, false, true);
    wm_redraw(g_win, &pic_area);
    wm_flush_input();
}

/* 0x47e5bc */
void soulview_reset(void)
{
    soultrack_reset();
    g_picture = 1;
    soulview_refresh();
    if (g_btn_track)
        ctl_button_set_pressed(g_btn_track, cur()->unk_00[1] != 0, true);
}

void soulview_age(void) { soultrack_age(); }

/* ---- input ------------------------------------------------------------------------- */

/* message 0x83e8 */
bool soulview_track_cell(int x, int y)
{
    if (g_busy || x < 0 || y < 0 || x >= GS_MAP_W || y >= GS_MAP_H)
        return false;
    unsigned set = cell_tileset(x, y);
    int ox = x, oy = y;
    if (set == 0) { /* 0x466575 / 0x46666d: through the sim cell */
        const SimCell *s = sim_cell(x, y);
        ox = s->b[10];
        oy = s->b[11];
        set = ox < GS_MAP_W && oy < GS_MAP_H ? cell_tileset(ox, oy) : 0;
    }
    if (ox >= GS_MAP_W || oy >= GS_MAP_H)
        return false;
    g_busy = 1;
    Rec26 *t = cur();
    if (g_win)
        wm_set_minimized(g_win, false);
    if (cell_index(ox, oy) != 0 && (set == 14 || set == 15) && realm_building_pop(ox, oy) != 0) {
        if (g_win)
            wm_raise(g_win);
        t->unk_00[1] = 1;
        soultrack_start(t, g_world.plane, ox, oy);
        if (g_btn_track)
            ctl_button_set_pressed(g_btn_track, t->unk_00[1] != 0, true);
        soulview_refresh();
        g_busy = 0;
        return true;
    }
    if (t->unk_00[6] != 0)
        soultrack_walker(t, false);
    soultrack_remove(t);
    soulview_status(blk()->selected);
    soulview_refresh();
    g_busy = 0;
    return true;
}

/* 0x47f9f9: right click on a button */
static void soulview_help(WmWindow *c)
{
    const char *text = "I have no idea what this button does." /* 0x66ae1c */;
    switch ((int)wm_get_data(c, 0)) {
    case SOULVIEW_CLOSE:
        text = "This button CLOSES the Soulview." /* 0x66ac48 */;
        break;
    case 1:
        text = wm_is_minimized(c->parent) ? "This button MAXIMIZES the window" /* 0x66add4 */
                                          : "This button MINIMIZES the window" /* 0x66adf8 */;
        break;
    case SOULVIEW_PICTURE:
        text = "This button brings up a PICTURE of the current SOUL's appearance in life." /* 0x66ac6c */;
        break;
    case SOULVIEW_DATA:
        text = "This button brings up some DATA about the current SOUL's deeds." /* 0x66acb8 */;
        break;
    case SOULVIEW_TRACK:
        text = "This button toggles the TRACKING of the SOUL through the Afterlife." /* 0x66acf8 */;
        break;
    case SOULVIEW_NEXT:
        text = "This button shows the NEXT tracked soul slot." /* 0x66ad70 */;
        break;
    case SOULVIEW_PREV:
        text = "This button shows the PREVIOUS tracked soul slot." /* 0x66ad3c */;
        break;
    case SOULVIEW_CENTER:
        text = "This button CENTERS the map on the current soul." /* 0x66ada0 */;
        break;
    }
    views_control_help(c, text);
}

static void clicked(int id)
{
    Rec26 *t = cur();
    int busy = g_busy;
    switch (id) {
    case SOULVIEW_PICTURE:
        if (!g_picture) {
            g_picture = 1;
            soulview_refresh();
            ctl_button_set_pressed(g_btn_data, false, true);
        }
        break;
    case SOULVIEW_CLOSE:
        wm_show(g_win, false);
        break;
    case SOULVIEW_DATA:
        if (g_picture) {
            g_picture = 0;
            soulview_refresh();
            ctl_button_set_pressed(g_btn_pic, false, true);
        }
        break;
    case SOULVIEW_TRACK:
        if (t->unk_00[0] != TRACK_NONE) {
            t->unk_00[1] = ctl_is_pressed(g_btn_track);
            soultrack_walker(t, t->unk_00[1] != 0);
        }
        break;
    case SOULVIEW_NEXT:
    case SOULVIEW_PREV:
        if (!g_busy) {
            unsigned s = blk()->selected;
            s = id == SOULVIEW_NEXT ? (s + 1 > 9 ? 0 : s + 1) : (s == 0 || s > 9 ? 9 : s - 1);
            blk()->selected = (uint8_t)s;
            g_busy = 1;
            soulview_refresh();
            g_busy = busy;
        }
        break;
    case SOULVIEW_CENTER:
        if (t->unk_00[0] != TRACK_NONE && views_hooks.center_on_cell)
            views_hooks.center_on_cell(t->unk_10, t->unk_00[6], t->unk_00[7]);
        break;
    }
    if (g_btn_track)
        ctl_button_set_pressed(g_btn_track, cur()->unk_00[1] != 0, true);
}

/* 0x47a2f9 soulview_proc */
static intptr_t soulview_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_MOUSEMOVE:
        if (g_dragging) {
            GfxRect r = w->rect;
            rect_move_to(&r, (int)a - g_grab_x, (int)b - g_grab_y);
            wm_local_to_screen(w, &r);
            if (w->parent)
                wm_screen_to_local(w->parent, &r);
            wm_set_rect(w, &r);
        }
        break;
    case WM_MSG_MINIMIZED:
        if (w != g_win)
            break;
        views_draw_title(w);
        if (!g_picture && cur()->unk_00[0] != TRACK_NONE)
            wm_show(g_viewer, a == 0);
        break;
    case WM_MSG_LBUTTONDOWN:
        if (w != g_win)
            break;
        wm_raise(w);
        wm_set_capture(w);
        g_grab_x = (int)a;
        g_grab_y = (int)b;
        g_dragging = 1;
        views_set_paused(1);
        break;
    case WM_MSG_RBUTTONDOWN:
        /* with the data button pressed: where the SOUL was picked and
         * whether it is temporary (a debugging aid) */
        if (w == g_win && ctl_is_pressed(g_btn_data)) {
            const Rec26 *t = cur();
            char buf[64];
            fill(&pic_area, BG);
            gfx_font_select(FONT);
            font_colors(0x1a, 0x1a);
            GfxRect r = pic_area;
            snprintf(buf, sizeof buf, "%d, %d" /* 0x66a9b4 */, t->unk_00[2], t->unk_00[3]);
            gfx_font_draw_text(w->surface, r.left, r.top, &r, &r, buf, GFX_TEXT_NOJUSTIFY);
            r = pic_area;
            snprintf(buf, sizeof buf, "%s soul" /* 0x66a9d0 */,
                     (t->unk_0c != 0 ? "Permanent" /* 0x66a9bc */ : "Temp" /* 0x66a9c8 */));
            gfx_font_draw_text(w->surface, r.left, r.top + 10, &r, &r, buf, GFX_TEXT_NOJUSTIFY);
            wm_redraw(w, &pic_area);
        }
        break;
    case WM_MSG_LBUTTONUP:
        if (w != g_win)
            break;
        wm_release_capture();
        g_dragging = 0;
        views_set_paused(0);
        break;
    case WM_MSG_CTL_PRESS:
        if (ctl_get_id(w) == 1 && wm_is_visible(g_viewer))
            wm_show(g_viewer, false);
        if (g_win)
            wm_raise(g_win);
        break;
    case WM_MSG_CTL_CLICK:
        clicked((int)ctl_get_id(w));
        break;
    case WM_MSG_CTL_HOVER:
        if (wm_get_mode() == 0)
            break;
        /* fall through */
    case WM_MSG_CTL_RDOWN:
        if (w->parent == g_win)
            soulview_help(w);
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

/* ---- creation ------------------------------------------------------------------------ */

static WmWindow *button(uint32_t bmp, int y, uint32_t id, uint32_t style, int *bottom)
{
    GfxSurface *s = gfx_load_bmp_resource(bmp, NULL);
    if (!s)
        return NULL;
    int x = ((0x18 - (s->width >> 1)) >> 1) + 0xb0;
    GfxRect r = {x, s->width + x - 1, y, y + s->height - 1};
    WmWindow *b = ctl_button_create(g_win, &r, id, style, s);
    if (!b)
        gfx_surface_destroy(s);
    *bottom = r.bottom;
    return b;
}

/* 0x479e29 */
bool soulview_create(void)
{
    if (g_win)
        return true;
    GfxSurface *bg = gfx_load_bmp_resource(0x25a, NULL);
    if (!bg)
        return false;
    WmWindow *mw = views_main_window();
    int right = mw ? mw->client.right : gfx_screen_width() - 1;
    GfxRect r = {right - bg->width - 8, 0, 0x3c, bg->height + 0x3b};
    r.right = r.left + bg->width - 1;
    g_win = wm_create_window(mw, &r, soulview_proc, WM_F_MINIMIZABLE | WM_F_NOFOCUS, 0);
    if (!g_win) {
        gfx_surface_destroy(bg);
        return false;
    }
    wm_set_title(g_win, "Soulview" /* 0x66a9a8 */);
    gfx_blit(g_win->surface, 0, 0, bg->width, bg->height, bg, 0, 0);
    gfx_surface_destroy(bg);
    memset(g_text, 0, sizeof g_text);
    g_viewer = ctl_view_create(g_win, &pic_area, CTL_VIEW_PARTIAL, 100, FONT, BG, 0x1a, 0x1a, 0);
    if (g_viewer)
        wm_show(g_viewer, false);
    GfxSurface *s = gfx_load_bmp_resource(0x259, NULL);
    if (s) {
        GfxRect c = {0, s->width - 1, 0, s->height - 1};
        g_win->close_button = ctl_button_create(g_win, &c, SOULVIEW_CLOSE, CTL_STYLE_BITMAP, s);
        if (!g_win->close_button)
            gfx_surface_destroy(s);
    }
    int y = 0x12, bottom = y;
    g_btn_pic = button(0x97, y, SOULVIEW_PICTURE, CTL_STYLE_BITMAP | CTL_STYLE_STICKY, &bottom);
    g_btn_data = button(0x115, bottom + 5, SOULVIEW_DATA, CTL_STYLE_BITMAP | CTL_STYLE_STICKY, &bottom);
    g_btn_track = button(0x70, bottom + 5, SOULVIEW_TRACK, CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE, &bottom);
    button(0x4000, bottom + 0x28, SOULVIEW_PREV, CTL_STYLE_BITMAP, &bottom);
    button(0x97, bottom + 5, SOULVIEW_CENTER, CTL_STYLE_BITMAP, &bottom);
    button(0x4001, bottom + 5, SOULVIEW_NEXT, CTL_STYLE_BITMAP, &bottom);
    g_picture = 1;
    soulview_refresh();
    if (g_btn_track)
        ctl_button_set_pressed(g_btn_track, cur()->unk_00[1] != 0, true);
    ctl_button_set_pressed(g_btn_pic, true, true);
    views_draw_title(g_win);
    wm_show(g_win, false);
    return true;
}

void soulview_open(void)
{
    if (!g_win && !soulview_create())
        return;
    wm_show(g_win, true);
    wm_raise(g_win);
}

void soulview_destroy(void)
{
    if (g_win) {
        wm_show(g_win, false);
        wm_destroy(g_win);
    }
    g_win = g_btn_pic = g_btn_data = g_btn_track = g_viewer = NULL;
    g_dragging = g_busy = 0;
}
