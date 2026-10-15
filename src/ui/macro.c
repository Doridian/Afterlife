/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The Macromanager (0x4619f0-0x463b6f): see ui/macro.h. */
#include "ui/macro.h"

#include <stdio.h>
#include <string.h>

#include "game/build.h"
#include "game/realm.h"
#include "game/util.h"
#include "game/world.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "ui/controls.h"
#include "ui/exe_text.h"
#include "ui/views.h"

#define FONT 500

MacroHooks macro_hooks;

static WmWindow *g_win;                  /* 0x5f7468 */
static WmWindow *g_btn[MACRO_CLOSE + 1]; /* 0x638e4c: by id */
static GfxSurface *g_knob;               /* 0x63904c: bitmap 0x516 */
static GfxSurface *g_under;              /* 0x639044: what the knob covers */
static int g_knob_x = -1, g_knob_y = -1; /* 0x639050, 0x639054 */
static MacroSettings g_set = {0, 0x80, 0, 0, 0, 1};
static uint32_t g_cost;        /* 0x65fe90 */
static int g_done;             /* 0x65fe8c: "Done implementing settings..." */
static int g_dragging;         /* 0x65fea4 */
static int g_sliding;          /* 0x65fea8 */
static int g_grab_x, g_grab_y; /* 0x4f31b8, 0x4f31bc */

static void font_colors(unsigned c1, unsigned c2)
{
    unsigned c[2] = {c1 & 0xff, c2 & 0xff};
    gfx_font_set_colors(FONT, 1, 2, c);
}

static void text(int x, int y, const char *s) { gfx_font_draw_text(g_win->surface, x, y, NULL, NULL, s, 0); }

static void redraw(int l, int r, int t, int b)
{
    GfxRect a = {l, r, t, b};
    wm_redraw(g_win, &a);
}

static void press(int id, bool on)
{
    if (id >= 0 && id <= MACRO_CLOSE && g_btn[id])
        ctl_button_set_pressed(g_btn[id], on, true);
}

WmWindow *macro_window(void) { return g_win; }
MacroSettings macro_settings(void) { return g_set; }
void macro_set_settings(const MacroSettings *s) { g_set = *s; }
uint32_t macro_cost(void) { return g_cost; }

/* ---- the action ------------------------------------------------------------- */

static void apply_realm(int plane, unsigned set, unsigned mask, bool commit, int *n)
{
    uint32_t rate = g_state.unk_630430;
    views_select_plane(plane);
    for (int x = 0; x < GS_MAP_W; x++)
        for (int y = 0; y < GS_MAP_H; y++) {
            if (cell_tileset(x, y) != set)
                continue;
            unsigned k = cell_index(x, y) / 13;
            if (k > 7)
                k = 7;
            if (!(mask & (1u << k)))
                continue;
            (*n)++;
            if (g_set.auto_bal)
                g_cost += rate * 50;
            else if (g_set.manual)
                g_cost += rate;
            if (g_set.lock || g_set.unlock)
                g_cost++;
            if (!commit)
                continue;
            SimCell *s = sim_cell(x, y);
            if (!g_set.auto_bal) {
                if (g_set.manual) {
                    s->b[0x10] = g_set.focus;
                    s->b[0xf] = (uint8_t)~g_set.focus;
                }
            } else {
                uint32_t temp = realm_building_temp(x, y), pop = realm_building_pop(x, y);
                if (pop == 0) {
                    s->b[0x10] = 0x80;
                    s->b[0xf] = 0x80;
                } else {
                    uint8_t c = (uint8_t)mul_div_clamp(temp, pop, 0xff);
                    s->b[0x10] = (uint8_t)~c;
                    s->b[0xf] = c;
                }
            }
            if (g_set.lock)
                s->b[0xe] |= 0x80;
            if (g_set.unlock)
                s->b[0xe] &= 0x7f;
        }
}

/* 0x462164 */
int macro_apply(bool commit)
{
    int n = 0;
    g_cost = 0;
    if (g_set.kinds & 0xff)
        apply_realm(GS_PLANE_HELL, 14, g_set.kinds & 0xff, commit, &n);
    if ((g_set.kinds >> 8) & 0xff)
        apply_realm(GS_PLANE_HEAVEN, 15, (g_set.kinds >> 8) & 0xff, commit, &n);
    if (commit && g_cost != 0) {
        if (macro_hooks.sound)
            macro_hooks.sound(0x10056, 2);
        bignum_sub(&g_state.pennies, g_cost);
        if (macro_hooks.refresh_all)
            macro_hooks.refresh_all();
    }
    return n;
}

/* ---- drawing ------------------------------------------------------------------ */

/* 0x46313b */
void macro_refresh(void)
{
    if (!g_win)
        return;
    int saved = g_world.plane;
    char buf[160], num[FMT_NUM_LEN];
    gfx_fill_rect(g_win->surface, 0xf, 0x150, 0x101, 0x16, 0x66);
    gfx_fill_rect(g_win->surface, 0x14, 0x33, 0x1b, 10, 0x6b);
    gfx_fill_rect(g_win->surface, 0x133, 0x33, 0x1b, 10, 0x6b);
    gfx_fill_rect(g_win->surface, 0x46, 0x44, 0x50, 0x14, 0x6b);
    gfx_fill_rect(g_win->surface, 0xe5, 0x44, 0x6b, 0x14, 0x6b);
    font_colors(0x1a, 0x1a);
    gfx_font_select(FONT);
    if (!g_set.auto_bal) {
        if (g_set.manual) {
            unsigned p = (unsigned)g_set.focus * 100 / 0xff;
            snprintf(buf, sizeof buf, exe_fmt(0x660218, "d", "%d%%"), (int)p);
            text(0x134, 0x35, buf);
            snprintf(buf, sizeof buf, exe_fmt(0x660220, "d", "%d%%"), (int)(100 - p));
            text(0x14, 0x35, buf);
        }
    } else {
        text(0x134, 0x35, exe_str(0x660208)); /* "AUTO" */
        text(0x14, 0x35, exe_str(0x660210));
    }
    redraw(0x13, 0x30, 0x33, 0x3f);
    redraw(0x133, 0x150, 0x33, 0x3f);
    uint32_t va = g_set.lock ? 0x660228 : g_set.unlock ? 0x660234 : 0x660244;
    GfxRect r = {0x46, 0x92, 0x43, 0x58};
    gfx_font_draw_text(g_win->surface, 0x46, 0x44, &r, &r, exe_str(va), 0);
    r = (GfxRect){0x44, 0x92, 0x43, 0x58};
    wm_redraw(g_win, &r);
    va = g_set.auto_bal ? 0x660258 : g_set.manual ? 0x66026c : 0x660284;
    r = (GfxRect){0xe3, 0x14a, 0x43, 0x58};
    gfx_font_draw_text(g_win->surface, 0xe5, 0x44, &r, &r, exe_str(va), 0);
    wm_redraw(g_win, &r);
    if (!g_done) {
        int n = macro_apply(false);
        snprintf(buf, sizeof buf, exe_fmt(0x6602bc, "s", "This setting would affect %s structures."),
                 fmt_thousands(num, (uint32_t)n));
        text(0x19, 0x152, buf);
        if (n != 0) {
            snprintf(buf, sizeof buf, exe_fmt(0x6602e8, "s", "Activating it would cost %s Pennies."),
                     fmt_thousands(num, g_cost));
            text(0x19, 0x15c, buf);
        }
        redraw(0xd, 0x10d, 0x150, 0x165);
        views_select_plane(saved);
    } else {
        text(0x19, 0x152, exe_str(0x66029c));
        redraw(0xd, 0x10d, 0x150, 0x165);
    }
}

/* 0x46369f: moves the slider knob to (x, y) (-1: just remove it) */
static void knob_at(int x, int y)
{
    if (!g_knob || !g_under)
        return;
    int w = g_knob->width, h = g_knob->height;
    if (g_knob_x != -1)
        gfx_blit(g_win->surface, g_knob_x, g_knob_y, w, h, g_under, 0, 0);
    if (x != -1) {
        gfx_blit(g_under, 0, 0, w, h, g_win->surface, x, y);
        g_knob_x = x;
        g_knob_y = y;
        gfx_blit_transparent(g_win->surface, x, y, w, h, g_knob, 0, 0, 0);
    }
    redraw(0x2d, 0x12d, 0x13, 0x3a);
}

/* 0x4630d9 */
static void knob_reset(void)
{
    knob_at(0x9d, MACRO_SLIDER_Y);
    macro_refresh();
}

uint8_t macro_slider_focus(int x)
{
    int v = x;
    if (v > 0x119)
        v = 0x11a;
    if (v < 0x42)
        v = 0x41;
    return v - 0x41 < 1 ? 1 : (uint8_t)mul_div_clamp((uint32_t)(v - 0x41), 0xd9, 0xff);
}

/* 0x4635d5: the knob follows the mouse x */
static void slide(int x)
{
    g_set.focus = macro_slider_focus(x);
    x -= 0x10;
    if (x > 0x109)
        x = 0x10a;
    if (x < 0x32)
        x = 0x31;
    knob_at(x, MACRO_SLIDER_Y);
    macro_refresh();
}

/* ---- input ---------------------------------------------------------------------- */

/* 0x4637a0: right click on a button */
static void macro_help(WmWindow *c)
{
    int id = (int)ctl_get_id(c);
    char buf[200];
    const char *t = NULL;
    if (id == 1)
        t = exe_str(wm_is_minimized(c->parent) ? 0x660684 : 0x6606a8);
    else if (id >= MACRO_HELL_KIND && id < MACRO_HELL_KIND + 7) {
        snprintf(buf, sizeof buf, exe_fmt(0x660310, "s", "This button toggles all %s PUNISHMENTS."),
                 exe_stri(0x66c658, (unsigned)(id - MACRO_HELL_KIND)));
        t = buf;
    } else if (id >= MACRO_HEAVEN_KIND && id < MACRO_HEAVEN_KIND + 7) {
        snprintf(buf, sizeof buf, exe_fmt(0x660358, "s", "This button toggles all %s REWARDS."),
                 exe_stri(0x66c600, (unsigned)(id - MACRO_HEAVEN_KIND)));
        t = buf;
    } else {
        static const uint32_t va[] = {
            [MACRO_HELL_KIND + 7 - 0x6c] = 0x66039c, [MACRO_HEAVEN_KIND + 7 - 0x6c] = 0x6603ec,
            [MACRO_ALL_HELL - 0x6c] = 0x660438,      [MACRO_ALL_HEAVEN - 0x6c] = 0x66047c,
            [MACRO_IMPLEMENT - 0x6c] = 0x660628,     [MACRO_LOCK - 0x6c] = 0x6604bc,
            [MACRO_UNLOCK - 0x6c] = 0x660508,        [MACRO_AUTO - 0x6c] = 0x6605b0,
            [MACRO_MANUAL - 0x6c] = 0x660558,        [MACRO_CLOSE - 0x6c] = 0x66065c,
        };
        if (id >= 0x6c && id <= MACRO_CLOSE && va[id - 0x6c])
            t = exe_str(va[id - 0x6c]);
    }
    if (t)
        views_control_help(c, t);
}

static void reset_settings(uint8_t manual)
{
    g_set.kinds = 0;
    g_set.focus = 0x80;
    g_set.lock = g_set.unlock = g_set.auto_bal = 0;
    g_set.manual = manual;
}

/* a message box (macro_hooks.message, else ctl_message_box) */
static int ask(const char *title, const char *text, uint32_t buttons_va, int n)
{
    char b[64];
    exe_buttons(buttons_va, n, b, sizeof b);
    if (macro_hooks.message)
        return macro_hooks.message(title, text, b);
    return ctl_message_box(-1, -1, title, text, b, 0);
}

static void implement(void)
{
    char buf[200], num[FMT_NUM_LEN];
    if (g_cost == 0) {
        ask(exe_str(0x6601f8), exe_str(0x6601bc), 0x6601b4, 1);
        press(MACRO_IMPLEMENT, false);
        return;
    }
    if (!bignum_ge_int(g_state.pennies, (int32_t)g_cost)) {
        ask(exe_str(0x6601a8), exe_str(0x660160), 0x660154, 1);
        press(MACRO_IMPLEMENT, false);
        return;
    }
    if (g_state.pennies.hi == 0) {
        snprintf(buf, sizeof buf, exe_fmt(0x66011c, "s", "This will cost %s Pennies.  Are you sure?"),
                 fmt_thousands(num, g_cost));
        if (ask(NULL, buf, 0x660148, 2) == 2) {
            press(MACRO_IMPLEMENT, false);
            return;
        }
    }
    g_done = 1;
    macro_apply(true);
    for (int id = MACRO_HELL_KIND; id <= MACRO_CLOSE; id++)
        press(id, false);
    reset_settings(0);
    knob_reset();
    g_build.status_dirty = 1;
    if (macro_hooks.microview_refresh)
        macro_hooks.microview_refresh();
}

static void clicked(int id)
{
    g_done = 0;
    switch (id) {
    case MACRO_ALL_HELL:
    case MACRO_ALL_HEAVEN: {
        int shift = id == MACRO_ALL_HELL ? 0 : 8;
        bool on = ((g_set.kinds >> shift) & 0xff) != 0xff;
        if (on)
            g_set.kinds |= 0xffu << shift;
        else
            g_set.kinds &= ~(0xffu << shift);
        for (int i = 0; i < 8; i++)
            press((id == MACRO_ALL_HELL ? MACRO_HELL_KIND : MACRO_HEAVEN_KIND) + i, on);
        macro_refresh();
        break;
    }
    case MACRO_IMPLEMENT:
        implement();
        break;
    case MACRO_LOCK:
        if (!g_set.lock) {
            g_set.lock = 1;
            if (g_set.unlock) {
                g_set.unlock = 0;
                press(MACRO_UNLOCK, false);
            }
        } else
            g_set.lock = 0;
        macro_refresh();
        break;
    case MACRO_UNLOCK:
        if (!g_set.unlock) {
            g_set.unlock = 1;
            if (g_set.lock) {
                g_set.lock = 0;
                press(MACRO_LOCK, false);
            }
        } else
            g_set.unlock = 0;
        macro_refresh();
        break;
    case MACRO_AUTO:
        if (!g_set.auto_bal) {
            g_set.auto_bal = 1;
            if (g_set.manual) {
                g_set.manual = 0;
                press(MACRO_MANUAL, false);
            }
        } else
            g_set.auto_bal = 0;
        macro_refresh();
        break;
    case MACRO_MANUAL:
        if (!g_set.manual) {
            g_set.manual = 1;
            if (g_set.auto_bal) {
                g_set.auto_bal = 0;
                press(MACRO_AUTO, false);
            }
        } else
            g_set.manual = 0;
        macro_refresh();
        break;
    case MACRO_CLOSE:
        if (views_hooks.menu_command)
            views_hooks.menu_command(0x17);
        else if (g_win)
            wm_show(g_win, false);
        break;
    default:
        if (id > 100 && id < MACRO_HEAVEN_KIND + 8) {
            unsigned k = (unsigned)(id - MACRO_HELL_KIND);
            if (!(g_set.kinds & (1u << k))) {
                g_set.kinds |= 1u << k;
                if (k < 8) {
                    if ((g_set.kinds & 0xff) == 0xff)
                        press(MACRO_ALL_HELL, true);
                } else if (((g_set.kinds >> 8) & 0xff) == 0xff)
                    press(MACRO_ALL_HEAVEN, true);
            } else {
                g_set.kinds &= ~(1u << k);
                press(k < 8 ? MACRO_ALL_HELL : MACRO_ALL_HEAVEN, false);
            }
            macro_refresh();
        }
        break;
    }
}

/* 0x4628f5 macro_proc */
static intptr_t macro_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    int x = (int)a, y = (int)b;
    switch (msg) {
    case WM_MSG_MOUSEMOVE:
        if (w != g_win)
            break;
        if (g_dragging) {
            GfxRect r = w->rect;
            rect_move_to(&r, x - g_grab_x, y - g_grab_y);
            wm_local_to_screen(w, &r);
            if (w->parent)
                wm_screen_to_local(w->parent, &r);
            wm_set_rect(w, &r);
        } else if (g_sliding)
            slide(x);
        break;
    case WM_MSG_LBUTTONDOWN:
        if (w != g_win)
            break;
        g_grab_x = x;
        g_grab_y = y;
        wm_set_capture(w);
        if (x < 0x37 || x > 0x124 || y < 0xd || y > 0x41) {
            g_dragging = 1;
            views_set_paused(1);
        } else {
            g_done = 0;
            g_sliding = 1;
            slide(x);
        }
        break;
    case WM_MSG_RBUTTONDOWN:
        if (w == g_win && x > 0x41 && x < 0x11a && y > 0x13 && y < 0x3b && views_hooks.help_balloon)
            views_hooks.help_balloon(w->rect.left + x, w->rect.top + y, 0xa0, exe_str(0x660020));
        break;
    case WM_MSG_LBUTTONUP:
        if (w != g_win)
            break;
        wm_release_capture();
        if (g_sliding) {
            g_sliding = 0;
            slide(x);
        }
        g_dragging = 0;
        views_set_paused(0);
        break;
    case WM_MSG_CTL_RDOWN:
        if (w->parent == g_win)
            macro_help(w);
        break;
    case WM_MSG_CTL_CLICK:
        clicked((int)ctl_get_id(w));
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

/* ---- creation --------------------------------------------------------------------- */

/* 0x4619f0: the kinds' labels, in the order the original draws them */
static const uint32_t k_labels[2][9] = {
    {0x65feac, 0x65fec0, 0x65fed4, 0x65feec, 0x65ff00, 0x65ff14, 0x65ff28, 0x65ff3c, 0x65ff50},
    {0x65ff64, 0x65ff78, 0x65ff88, 0x65ff9c, 0x65ffb0, 0x65ffc4, 0x65ffdc, 0x65fff0, 0x660000},
};

static void labels(void)
{
    gfx_font_select(FONT);
    font_colors(0x1a, 0x13);
    for (int i = 0; i < 9; i++)
        text(0x14, 0x66 + 0x18 * i, exe_str(k_labels[0][i]));
    font_colors(0x1a, 0x13);
    for (int i = 0; i < 9; i++)
        text(0xcc, 0x66 + 0x18 * i, exe_str(k_labels[1][i]));
    font_colors(0x13, 0x13);
    macro_refresh();
}

/* 0x461c9b */
bool macro_create(void)
{
    if (g_win)
        return true;
    GfxSurface *bg = gfx_load_bmp_resource(0x514, NULL);
    if (!bg)
        return false;
    WmWindow *mw = views_main_window();
    int left = (mw ? mw->client.left : 0) + 0x14, top = (mw ? mw->client.top : 0) + 0x14;
    GfxRect r = {left, left + bg->width - 1, top, top + bg->height - 1};
    g_win = wm_create_window(mw, &r, macro_proc, WM_F_MINIMIZABLE | WM_F_NOFOCUS, 0);
    if (!g_win) {
        gfx_surface_destroy(bg);
        return false;
    }
    wm_set_title(g_win, exe_str(0x660010));
    gfx_blit(g_win->surface, 0, 0, bg->width, bg->height, bg, 0, 0);
    gfx_surface_destroy(bg);
    for (int id = MACRO_HELL_KIND; id <= MACRO_CLOSE; id++) {
        uint32_t style = CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE, bmp;
        int x, y;
        if (id < MACRO_HEAVEN_KIND) {
            x = 0x98, y = (id * 3 - 0x12f) * 8 + 0x5f, bmp = (uint32_t)id + 2;
        } else if (id < MACRO_ALL_HELL) {
            x = 0xb0, y = (id * 3 - 0x147) * 8 + 0x5f, bmp = (uint32_t)id - 6;
        } else {
            switch (id) {
            case MACRO_ALL_HELL:
                x = 0x98, y = 0x11f, bmp = 0x7d;
                break;
            case MACRO_ALL_HEAVEN:
                x = 0xb0, y = 0x11f, bmp = 0x7e;
                break;
            case MACRO_IMPLEMENT:
                style = CTL_STYLE_BITMAP, x = 0x9a, y = 0x137, bmp = 0x515;
                break;
            case MACRO_LOCK:
                x = 0x2a, y = 0x42, bmp = 0x517;
                break;
            case MACRO_UNLOCK:
                x = 0x12, y = 0x42, bmp = 0x518;
                break;
            case MACRO_AUTO:
                x = 200, y = 0x42, bmp = 400;
                break;
            case MACRO_MANUAL:
                x = 0xb0, y = 0x42, bmp = 0x519;
                break;
            default: /* close */
                style = CTL_STYLE_BITMAP, x = 0, y = 0, bmp = 0x259;
                break;
            }
        }
        GfxSurface *s = gfx_load_bmp_resource(bmp, NULL);
        if (!s)
            continue;
        GfxRect br = {x, s->width + x - 1, y, s->height + y - 1};
        g_btn[id] = ctl_button_create(g_win, &br, (uint32_t)id, style, s);
        if (!g_btn[id])
            gfx_surface_destroy(s);
        if (id == MACRO_CLOSE)
            g_win->close_button = g_btn[id];
    }
    labels();
    g_knob = gfx_load_bmp_resource(0x516, NULL);
    g_under = g_knob ? gfx_surface_create(g_knob->width, g_knob->height) : NULL;
    g_knob_x = g_knob_y = -1;
    views_draw_title(g_win);
    wm_show(g_win, false);
    return true;
}

/* 0x4620aa */
void macro_open(void)
{
    if (!g_win && !macro_create())
        return;
    for (int id = MACRO_HELL_KIND; id <= MACRO_CLOSE; id++)
        press(id, false);
    press(MACRO_MANUAL, true);
    reset_settings(1);
    knob_reset();
    wm_show(g_win, true);
    wm_raise(g_win);
}

void macro_destroy(void)
{
    if (g_win) {
        wm_show(g_win, false);
        wm_destroy(g_win);
    }
    g_win = NULL;
    memset(g_btn, 0, sizeof g_btn);
    gfx_surface_destroy(g_knob);
    gfx_surface_destroy(g_under);
    g_knob = g_under = NULL;
    g_knob_x = g_knob_y = -1;
    g_dragging = g_sliding = g_done = 0;
}
