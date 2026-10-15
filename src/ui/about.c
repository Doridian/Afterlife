/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The about box and the credits (ALIFE.EXE 0x4acc30-0x4b0fff). Overview:
 * about.h.
 *
 * Layout (local coordinates of bitmap 0x261): the picture area, 8 x 5
 * tiles of 40 x 40 at (14, 16); the credits area from x 0x15f and y 0x10
 * to 13 pixels from the right and bottom edges.
 *
 * Credits entries (0x28-byte records from a pool of 1000, 0x518f98) are
 * created by credits_add (0x4ad624) in script order, each placed relative
 * to the one before, and scroll up 3 pixels per step:
 *   2 text in a font slot (centred with flag 1), 3 a horizontal line,
 *   4 a pause of (flags >> 16) & 0x7fff steps when it reaches the bottom,
 *   5 a picture (resource `arg`, 320 x 200) revealed in 40 random tiles.
 * Font slots (0x522bf0, 10 x 0x26 bytes) are a font with colormap entries
 * to apply while drawing.
 */
#include "ui/about.h"

#include <stdlib.h>
#include <string.h>

#include "game/shell.h"
#include "game/timer.h"
#include "game/util.h"
#include "gfx/draw.h"
#include "gfx/exe_res.h"
#include "gfx/font.h"
#include "gfx/surface.h"
#include "ui/controls.h"
#include "ui/news.h"

#define ABOUT_BMP 0x261
#define CLOSE_BMP 0x259

#define AREA_LEFT 0x15f /* credits area, insets of the local rect */
#define AREA_RIGHT 0xd
#define AREA_TOP 0x10
#define AREA_BOTTOM 0xd
#define LINE_RIGHT 0x1c3
#define SCROLL 3
#define BACK 0x13

#define TILE 40
#define TILE_COLS 8
#define TILES 40
#define PIC_X 0xe
#define PIC_Y 0x10

enum { CR_TEXT = 2, CR_LINE = 3, CR_PAUSE = 4, CR_PICTURE = 5 };
#define CR_CENTER 0x1u
#define CR_RELATIVE 0x80000000u

/* ---- state ------------------------------------------------------------------ */

static WmWindow *g_win;    /* 0x5f25a0 */
static bool g_running;     /* 0x57e0a0 */
static bool g_enabled;     /* 0x66fe2c: off while the window is dragged */
static bool g_dragging;    /* 0x522e3c */
static int g_grab_x;       /* 0x522e20 */
static int g_grab_y;       /* 0x522be4 */
static uint32_t g_last_tick; /* 0x522e48 */
static GfxRect g_area;     /* 0x518f80 */

static int g_pause;        /* 0x66fe38: steps left in a pause */
static bool g_pause_armed; /* 0x66fe3c: a pause entry has started its pause */
static uint32_t g_pic_id;  /* 0x66fe28: the picture being revealed */
static uint32_t g_pic_last; /* 0x66fe24: the picture loaded last */
static int g_tile;         /* 0x522e4c: tiles revealed */
static int g_order[TILES]; /* 0x522d70 */
static GfxSurface *g_pic;  /* 0x522be8 */

typedef struct CreditFont {
    uint32_t font;         /* +0x00, 0 = free */
    uint32_t prev_font;    /* +0x04 */
    unsigned colors[14];   /* +0x08 (0x100: keep the font's own) */
    uint8_t saved[14];     /* +0x17 */
    unsigned n_saved;
} CreditFont;
static CreditFont g_fonts[10]; /* 0x522bf0 */
static bool g_fonts_made;      /* 0x522e40 */
static int g_font_small1, g_font_small2, g_font_big; /* 0x522e14, 0x522e10, 0x522bd8 */

typedef struct Credit {
    struct Credit *next; /* +0x00 */
    int kind;            /* +0x08 */
    uint32_t flags;      /* +0x0c */
    intptr_t arg;        /* +0x10: font slot, colour, pause, resource */
    const char *text;    /* +0x14 */
    GfxRect r;           /* +0x18 left, right, top, bottom */
} Credit;
#define POOL 1000
static Credit g_pool[POOL];  /* 0x518f98 */
static Credit *g_free;       /* 0x66fe34 */
static Credit *g_list;       /* 0x66fe30 */
static bool g_pool_made;     /* 0x522bdc */
static int g_last_x;         /* 0x518f90 */
static int g_last_y;         /* 0x522e1c */
static int g_last_h;         /* 0x522be0 */
static int g_last_w;         /* 0x522e44 */

WmWindow *about_window(void) { return g_win; }
bool about_credits_running(void) { return g_running; }

int about_credits_pending(void)
{
    int n = 0;
    for (Credit *c = g_list; c; c = c->next)
        n++;
    return n;
}

/* ---- the credit list ------------------------------------------------------------- */

static void list_append(Credit *c)
{
    c->next = NULL;
    Credit **p = &g_list;
    while (*p)
        p = &(*p)->next;
    *p = c;
}

static void list_unlink(Credit *c)
{
    for (Credit **p = &g_list; *p; p = &(*p)->next)
        if (*p == c) {
            *p = c->next;
            return;
        }
}

static void list_free(Credit *c)
{
    list_unlink(c);
    c->next = g_free;
    g_free = c;
}

/* ---- font slots ---------------------------------------------------------------------- */

/* 0x4ad301: a free slot for `font` with colormap entries 0 and 1 (the
 * original takes 14 varargs and is called with two, so the rest is
 * whatever was on the stack; the port keeps the font's own there). */
static int font_slot_new(uint32_t font, unsigned c0, unsigned c1)
{
    if (font == 0)
        return -1;
    for (int i = 0; i < 10; i++)
        if (g_fonts[i].font == 0) {
            CreditFont *f = &g_fonts[i];
            f->font = font;
            f->colors[0] = c0;
            f->colors[1] = c1;
            for (int k = 2; k < 14; k++)
                f->colors[k] = 0x100;
            return i;
        }
    return -1;
}

/* 0x4ad3dd (unused) */
static void font_slot_free(unsigned i)
{
    if (i < 10 && g_fonts[i].font)
        memset(&g_fonts[i], 0, sizeof g_fonts[i]);
}

/* 0x4ad434: select the slot's font in its colours, remembering the
 * previous font and colormap */
static void font_slot_begin(intptr_t i)
{
    if (i < 0 || i >= 10 || !g_fonts[i].font)
        return;
    CreditFont *f = &g_fonts[i];
    f->prev_font = gfx_font_select(f->font);
    f->n_saved = gfx_font_get_colormap(f->font, 14, f->saved);
    gfx_font_set_colors(f->font, 0, 14, f->colors);
}

/* 0x4ad538 */
static void font_slot_end(intptr_t i)
{
    if (i < 0 || i >= 10 || !g_fonts[i].font)
        return;
    CreditFont *f = &g_fonts[i];
    if (f->prev_font)
        gfx_font_select(f->prev_font);
    gfx_font_set_colormap(f->font, f->n_saved, f->saved);
}

/* ---- entries ---------------------------------------------------------------------------- */

/* 0x4ad624 credits_add(kind, text, x, y, arg, flags): with CR_RELATIVE,
 * x is relative to the previous entry's x and y to the previous entry's
 * bottom. Kinds 3-5 pass no text; the original then substitutes the last
 * width, only to pass its "non-zero" test. */
static void credits_add(int kind, const char *text, int x, int y, intptr_t arg, uint32_t flags)
{
    if (!g_pool_made) {
        g_pool_made = true;
        g_free = NULL;
        for (int i = POOL - 1; i >= 0; i--) {
            g_pool[i].next = g_free;
            g_free = &g_pool[i];
        }
        g_last_x = g_last_y = g_last_h = g_last_w = 0;
    }
    bool present = text != NULL || (kind >= CR_LINE && kind <= CR_PICTURE && g_last_w != 0);
    if (!present || !g_free)
        return;
    Credit *c = g_free;
    g_free = c->next;
    list_append(c);
    if (flags & CR_RELATIVE) {
        x += g_last_x;
        y += g_last_y + g_last_h;
    }
    GfxRect r = {x, x + 1, y, y + 1};
    switch (kind) {
    case CR_TEXT: {
        font_slot_begin(arg);
        int tw = gfx_font_text_width(text);
        r.right = x + ((unsigned)(tw - 1) < 0x83 ? tw - 1 : 0x83);
        r.bottom = y + gfx_font_height(0) - 1;
        font_slot_end(arg);
        break;
    }
    case CR_LINE:
        r.left = AREA_LEFT;
        r.right = LINE_RIGHT;
        break;
    default:
        break;
    }
    c->kind = kind;
    c->flags = flags;
    c->arg = arg;
    c->text = text;
    c->r = r;
    g_last_x = x;
    g_last_y = y;
    g_last_h = r.bottom - r.top + 1;
    g_last_w = r.right - r.left + 1;
}

/* 0x4ad910: draws a text or line entry into the window, clipped to the
 * credits area */
static void credit_draw(const Credit *c, WmWindow *w, const GfxRect *clip)
{
    if (!c || !w || !w->surface)
        return;
    int x = c->r.left, y = c->r.top;
    int width = c->r.right - c->r.left + 1;
    if ((c->flags & CR_CENTER) && clip) {
        int room = clip->right - clip->left + 1 - width;
        if (room < 0)
            return; /* the original's unsigned halving puts it far off */
        x = (room >> 1) + clip->left;
    }
    if (c->kind == CR_TEXT) {
        GfxRect cl = *clip;
        font_slot_begin(c->arg);
        gfx_font_draw_text(w->surface, x, y, &cl, &cl, c->text, GFX_TEXT_NOJUSTIFY);
        font_slot_end(c->arg);
    } else if (c->kind == CR_LINE && clip->top <= y) {
        gfx_line(w->surface, x, x + width - 1, y, y, (uint8_t)c->arg);
    }
}

/* ---- the script, from the EXE ----------------------------------------------------------- */

/* Where 0x4addfb's calls start (after the font slots and the list reset),
 * the functions it calls and the globals holding its three font slots. */
#define SCRIPT_VA 0x4adec8u
#define SCRIPT_END_VA 0x4b1007u
#define CREDITS_ADD_VA 0x4ad624u
#define RAND_RANGE_VA 0x42b088u
static const uint32_t k_slot_vars[3] = {0x522e14u, 0x522e10u, 0x522bd8u};

static bool fetch(uint32_t va, unsigned n, const uint8_t **out)
{
    uint32_t avail = 0;
    const uint8_t *p = va >= SCRIPT_VA && va < SCRIPT_END_VA ? exe_res_va(va, &avail) : NULL;
    *out = p;
    return p && avail >= n;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static const char *exe_text(uint32_t va)
{
    uint32_t avail = 0;
    const uint8_t *p = va ? exe_res_va(va, &avail) : NULL;
    return p && memchr(p, 0, avail) ? (const char *)p : NULL;
}

/* Runs 0x4addfb's call sequence: the handful of x86 instructions it is
 * made of (pushes, the slot loads, calls with their stack cleanup, and the
 * two rand_range switches through their jump tables). Stops at the
 * epilogue or at anything else. Returns the number of entries added. */
static int run_script(void)
{
    if (!exe_res_open("ALIFE.EXE"))
        return 0;
    uint32_t stack[32];
    int sp = 0, added = 0;
    uint32_t eax = 0, locals[256] = {0};
    uint32_t cmp_a = 0, cmp_b = 0;
    uint32_t pc = SCRIPT_VA;
    const uint8_t *p;
    for (int steps = 0; steps < 20000 && fetch(pc, 1, &p); steps++) {
        uint8_t op = p[0];
        if (op == 0x6a && fetch(pc, 2, &p)) { /* push imm8 */
            if (sp >= 32)
                break;
            stack[sp++] = (uint32_t)(int32_t)(int8_t)p[1];
            pc += 2;
        } else if (op == 0x68 && fetch(pc, 5, &p)) { /* push imm32 */
            if (sp >= 32)
                break;
            stack[sp++] = rd32(p + 1);
            pc += 5;
        } else if (op == 0x50) { /* push eax */
            if (sp >= 32)
                break;
            stack[sp++] = eax;
            pc += 1;
        } else if (op == 0xa1 && fetch(pc, 5, &p)) { /* mov eax, [slot var] */
            uint32_t var = rd32(p + 1);
            int slot = var == k_slot_vars[0] ? g_font_small1 : var == k_slot_vars[1] ? g_font_small2
                       : var == k_slot_vars[2] ? g_font_big : -2;
            if (slot == -2)
                break;
            eax = (uint32_t)slot;
            pc += 5;
        } else if (op == 0xe8 && fetch(pc, 5, &p)) { /* call */
            uint32_t target = pc + 5 + rd32(p + 1);
            if (target == CREDITS_ADD_VA && sp >= 6) {
                uint32_t *a = &stack[sp - 1]; /* a[0] = first argument */
                int kind = (int)a[0];
                const char *text = kind == CR_TEXT ? exe_text(a[-1]) : NULL;
                credits_add(kind, text, (int32_t)a[-2], (int32_t)a[-3], (intptr_t)(int32_t)a[-4], a[-5]);
                added++;
            } else if (target == RAND_RANGE_VA && sp >= 2) {
                eax = (uint32_t)rand_range((int32_t)stack[sp - 1], (int32_t)stack[sp - 2]);
            } else {
                break;
            }
            pc += 5;
        } else if (op == 0x83 && fetch(pc, 3, &p) && p[1] == 0xc4) { /* add esp, imm8 */
            int n = p[2] / 4;
            sp = n > sp ? 0 : sp - n;
            pc += 3;
        } else if (op == 0x83 && fetch(pc, 4, &p) && p[1] == 0x7d) { /* cmp [ebp+d], imm8 */
            cmp_a = locals[p[2]];
            cmp_b = (uint32_t)(int32_t)(int8_t)p[3];
            pc += 4;
        } else if (op == 0x89 && fetch(pc, 3, &p) && p[1] == 0x45) { /* mov [ebp+d], eax */
            locals[p[2]] = eax;
            pc += 3;
        } else if (op == 0x8b && fetch(pc, 3, &p) && p[1] == 0x45) { /* mov eax, [ebp+d] */
            eax = locals[p[2]];
            pc += 3;
        } else if (op == 0x0f && fetch(pc, 6, &p) && p[1] == 0x87) { /* ja rel32 */
            pc += 6;
            if (cmp_a > cmp_b)
                pc += rd32(p + 2);
        } else if (op == 0xe9 && fetch(pc, 5, &p)) { /* jmp rel32 */
            pc += 5 + rd32(p + 1);
        } else if (op == 0xeb && fetch(pc, 2, &p)) { /* jmp rel8 */
            pc += 2 + (uint32_t)(int32_t)(int8_t)p[1];
        } else if (op == 0xff && fetch(pc, 7, &p) && p[1] == 0x24 && p[2] == 0x85) { /* jmp [table + eax*4] */
            uint32_t avail = 0;
            const uint8_t *t = exe_res_va(rd32(p + 3) + eax * 4, &avail);
            if (!t || avail < 4)
                break;
            pc = rd32(t);
        } else {
            break; /* pop edi: the epilogue */
        }
    }
    return added;
}

/* 0x4addfb: the font slots (once), then the whole list from the top */
void about_credits_restart(void)
{
    if (!g_fonts_made) {
        g_fonts_made = true;
        memset(g_fonts, 0, sizeof g_fonts);
        g_font_small1 = font_slot_new(500, 0xe, 0xe);
        g_font_small2 = font_slot_new(500, 0xe, 0xe);
        g_font_big = font_slot_new(500, 0x1a, 0x1a);
    }
    while (g_list)
        list_free(g_list);
    run_script();
}

/* ---- the animation ------------------------------------------------------------------------ */

/* 0x4ad1e4: reveals tile g_order[g_tile] of the picture */
static void reveal_tile(void)
{
    int i = g_order[g_tile];
    int sx = i % TILE_COLS * TILE, sy = i / TILE_COLS * TILE;
    if (!g_win || !g_pic)
        return;
    gfx_blit(g_win->surface, sx + PIC_X, sy + PIC_Y, TILE, TILE, g_pic, sx, sy);
    GfxRect r = {sx + PIC_X, sx + PIC_X + TILE, sy + PIC_Y, sy + PIC_Y + TILE};
    wm_redraw(g_win, &r);
}

/* 0x4ad298 */
static void shuffle_tiles(void)
{
    for (int i = 0; i < TILES; i++) {
        int j = rand_range(0, TILES - 1);
        int t = g_order[i];
        g_order[i] = g_order[j];
        g_order[j] = t;
    }
}

/* the body of 0x4ada7b */
void about_credits_step(void)
{
    if (!g_win)
        return;
    wm_get_local_rect(g_win, &g_area);
    g_area.left += AREA_LEFT;
    g_area.right -= AREA_RIGHT;
    g_area.top += AREA_TOP;
    g_area.bottom -= AREA_BOTTOM;
    /* a pause that comes with a picture lasts until it is complete */
    if (g_pause_armed && g_pause == 0 && g_pic_id && g_tile < TILES)
        g_pause = 1;
    if (g_pause) {
        g_pause--;
        if (g_pic_id && g_tile < TILES) {
            reveal_tile();
            g_tile++;
        }
        return;
    }
    GfxSurface *s = g_win->surface;
    int aw = g_area.right - g_area.left + 1;
    gfx_scroll_rect(s, &g_area, 0, -SCROLL);
    gfx_fill_rect(s, g_area.left, g_area.top, aw, SCROLL, BACK);
    gfx_fill_rect(s, g_area.left, g_area.bottom - SCROLL + 1, aw, SCROLL, BACK);
    for (Credit *c = g_list, *next; c; c = next) {
        next = c->next;
        bool gone = false;
        if (c->r.top < g_area.bottom) {
            if (c->kind == CR_PAUSE) {
                if (!g_pause_armed) {
                    g_pause = (int)((c->flags & 0x7fff0000u) >> 16);
                    g_pause_armed = true;
                } else {
                    g_pause_armed = false;
                    list_free(c);
                    gone = true;
                }
            } else if (c->kind == CR_PICTURE) {
                if (!g_pic_id) {
                    g_pic_id = (uint32_t)c->arg;
                    shuffle_tiles();
                    if (g_pic_id != g_pic_last) {
                        gfx_surface_destroy(g_pic);
                        g_pic = gfx_load_bmp_resource(g_pic_id, NULL);
                    }
                    g_tile = 0;
                } else if (g_tile < TILES) {
                    reveal_tile();
                    g_tile++;
                } else {
                    g_pic_last = g_pic_id;
                    g_pic_id = 0;
                    /* the original unlinks the record without returning
                     * it to the pool, which runs dry after ~45 passes */
                    list_free(c);
                    gone = true;
                }
            } else {
                credit_draw(c, g_win, &g_area);
                if (c->r.bottom < g_area.top) {
                    list_free(c);
                    gone = true;
                }
            }
        }
        if (!gone) {
            c->r.top -= SCROLL;
            c->r.bottom -= SCROLL;
        }
    }
    wm_redraw(g_win, &g_area);
    if (!g_list)
        about_credits_restart();
}

/* 0x4ada7b */
void about_credits_tick(void)
{
    if (!g_enabled)
        return;
    uint32_t now = tm_get_time();
    if (now <= g_last_tick + 100)
        return;
    g_last_tick = now;
    about_credits_step();
}

/* ---- the window ----------------------------------------------------------------------------- */

/* 0x4ad0cf: right click on one of the window's buttons */
static void about_help(WmWindow *c)
{
    if (!c || !g_win)
        return;
    intptr_t id = wm_get_data(c, 0);
    int x = ((c->rect.right - c->rect.left) >> 1) + c->rect.left + g_win->rect.left;
    int y = ((c->rect.bottom - c->rect.top) >> 1) + c->rect.top + g_win->rect.top;
    if (id == 1)
        help_balloon_show(x, y, 0x78,
                          wm_is_minimized(c->parent) ? "This button MAXIMIZES the window."
                                                     : "This button MINIMIZES the window.",
                          true, true);
    else if (id == ABOUT_ID_CLOSE)
        help_balloon_show(x, y, 0x78, "This button CLOSES the About Box.", true, true);
}

/* 0x4acdda */
intptr_t about_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_MOUSEMOVE:
        if (g_dragging) {
            GfxRect r = w->rect;
            rect_move_to(&r, (int)a - g_grab_x, (int)b - g_grab_y);
            /* local -> parent: the original converts to the screen, which
             * is the same for a child of the full-screen main window */
            r.left += w->rect.left, r.right += w->rect.left;
            r.top += w->rect.top, r.bottom += w->rect.top;
            wm_set_rect(w, &r);
        }
        break;
    case WM_MSG_LBUTTONDOWN:
        wm_set_capture(w);
        wm_raise(w);
        g_dragging = true;
        g_game_paused = 1;
        g_enabled = false;
        g_grab_x = (int)a;
        g_grab_y = (int)b;
        break;
    case WM_MSG_RBUTTONDOWN:
        wm_raise(w);
        break;
    case WM_MSG_LBUTTONUP:
        wm_release_capture();
        g_game_paused = 0;
        g_dragging = false;
        g_enabled = true;
        break;
    case WM_MSG_CTL_RDOWN:
        about_help(w);
        break;
    case WM_MSG_CTL_CLICK:
        if (a == ABOUT_ID_CLOSE)
            shell_menu_command(MENU_ABOUT);
        break;
    case WM_MSG_MINIMIZED:
        g_running = a == 0;
        break;
    case WM_MSG_SHOW: {
        if (!g_win)
            break;
        GfxRect r;
        if (!wm_is_minimized(g_win)) {
            wm_get_local_rect(g_win, &r);
        } else {
            r.left = r.top = 0;
            r.right = g_win->restore_rect.right - g_win->restore_rect.left - 1;
            r.bottom = g_win->restore_rect.bottom - g_win->restore_rect.top - 1;
        }
        r.left += AREA_LEFT;
        r.right -= AREA_RIGHT;
        r.top += AREA_TOP;
        r.bottom -= AREA_BOTTOM;
        gfx_fill_rect(g_win->surface, r.left, r.top, r.right - r.left + 1, r.bottom - r.top + 1, BACK);
        about_credits_restart();
        g_running = a != 0;
        g_enabled = true;
        break;
    }
    default:
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

/* 0x4acc30 */
bool about_create(void)
{
    if (g_win)
        return true;
    if (!g_main_window)
        return false;
    GfxSurface *bmp = gfx_load_bmp_resource(ABOUT_BMP, NULL);
    if (!bmp)
        return false;
    GfxRect r = {0, bmp->width - 1, 0, bmp->height - 1};
    g_win = wm_create_window(g_main_window, &r, about_proc, WM_F_MINIMIZABLE | WM_F_NOFOCUS, 0);
    if (g_win)
        gfx_blit(g_win->surface, 0, 0, bmp->width, bmp->height, bmp, 0, 0);
    gfx_surface_destroy(bmp);
    if (!g_win)
        return false;
    GfxSurface *cb = gfx_load_bmp_resource(CLOSE_BMP, NULL);
    if (cb) {
        /* the bitmap holds both frames side by side */
        GfxRect br = {0, cb->width - 1, 0, cb->height - 1};
        g_win->close_button = ctl_button_create(g_win, &br, ABOUT_ID_CLOSE, CTL_STYLE_BITMAP, cb);
        if (!g_win->close_button)
            gfx_surface_destroy(cb);
    }
    wm_resize(g_win, g_win->surface->width, g_win->surface->height);
    wm_center(g_win);
    g_running = false;
    g_pic_last = 0;
    g_pic_id = 0;
    for (int i = 0; i < TILES; i++)
        g_order[i] = i;
    return true;
}

void about_destroy(void)
{
    if (g_win) {
        if (wm_capture() == g_win)
            wm_release_capture();
        wm_destroy(g_win);
    }
    g_win = NULL;
    gfx_surface_destroy(g_pic);
    g_pic = NULL;
    while (g_list)
        list_free(g_list);
    g_running = g_enabled = g_dragging = false;
    g_pause = 0;
    g_pause_armed = false;
    g_pic_id = g_pic_last = 0;
    g_tile = 0;
    (void)font_slot_free;
}
