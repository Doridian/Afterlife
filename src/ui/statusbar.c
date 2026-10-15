/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The status bar (ui/statusbar.h). */
#include "ui/statusbar.h"

#include <stdio.h>
#include <string.h>

#include "game/build.h"
#include "game/shell.h"
#include "game/util.h"
#include "game/world.h"
#include "gfx/draw.h"
#include "gfx/font.h"

/* The values of the last draw: a number that went down is drawn red
 * (colour 0xc), one that went up or stayed the same after going up green
 * (0x58). */
static Bignum g_last_souls;   /* 0x65ee50 */
static int g_souls_up;        /* 0x65ee4c */
static Bignum g_last_embos;   /* 0x65ee30 */
static uint32_t g_last_rate;  /* 0x65ee38 */
static Bignum g_last_pennies; /* 0x65ee40 */
static int g_pennies_up;      /* 0x65ee48 */
static int g_weight_hot;      /* 0x65ee20 */

#define RED 0x0c
#define GREEN 0x58
#define PANEL_BG 0x2f

static void colors(unsigned c)
{
    unsigned cc[2] = {c, c};
    gfx_font_set_colors(500, 1, 2, cc);
}

static GfxSurface *surf(void) { return g_ctrlpanel_window ? g_ctrlpanel_window->surface : NULL; }

static void text_center(int y, const char *s)
{
    gfx_font_draw_text(surf(), 0x40, y, NULL, NULL, s, GFX_TEXT_CENTER);
}

void statusbar_reset(void)
{
    memset(&g_last_souls, 0, sizeof g_last_souls);
    memset(&g_last_embos, 0, sizeof g_last_embos);
    memset(&g_last_pennies, 0, sizeof g_last_pennies);
    g_last_rate = 0;
    g_souls_up = g_pennies_up = 0;
}

/* "%s", "%sM" or "%sB" for a sign-and-magnitude count */
static void fmt_big(char *out, size_t n, const char *label, Bignum v)
{
    char num[FMT_NUM_LEN];
    if (v.hi < 1)
        snprintf(out, n, "%s: %s", label, fmt_thousands_k(num, v.lo));
    else if (v.hi < 0x1a)
        snprintf(out, n, "%s: %sM", label, fmt_thousands(num, v.lo / 1000000u + (uint32_t)v.hi * 0x10c7u));
    else
        snprintf(out, n, "%s: %sB", label,
                 fmt_thousands(num, (v.lo / 100000000u + (uint32_t)v.hi * 0x2bu) / 10u));
}

/* green if `now` >= `last`, sticky green while it keeps not falling */
static void trend_colors(Bignum now, Bignum last, int *up)
{
    if (!bignum_ge(now, last)) {
        *up = 0;
        colors(RED);
    } else if (!bignum_gt(now, last)) {
        colors(*up ? GREEN : RED);
    } else {
        colors(GREEN);
        *up = 1;
    }
}

/* 0x447243 statusbar_draw */
void statusbar_draw(void)
{
    GfxSurface *s = surf();
    if (!s)
        return;
    char buf[64], num[FMT_NUM_LEN];
    gfx_fill_rect(s, 0xf, 0x12, 0x62, 0x1f, PANEL_BG);
    gfx_fill_rect(s, 0xf, 0x166, 0x61, 0x2a, PANEL_BG);
    colors(GREEN);
    gfx_font_select(500);

    /* SOULs: both realms' SOUL pools plus their populations */
    const PlaneStats *hell = &g_state.plane[GS_PLANE_HELL].stats, *heaven = &g_state.plane[GS_PLANE_HEAVEN].stats;
    Bignum souls = hell->souls;
    bignum_add(&souls, heaven->souls.lo);
    souls.hi += heaven->souls.hi;
    uint32_t pops[2] = {hell->population, heaven->population};
    for (int i = 0; i < 2; i++) {
        if (pops[i] < 0x80000000u) {
            bignum_add(&souls, pops[i]);
        } else {
            bignum_add(&souls, pops[i] >> 1);
            bignum_add(&souls, pops[i] >> 1);
        }
    }
    trend_colors(souls, g_last_souls, &g_souls_up);
    g_last_souls = souls;
    fmt_big(buf, sizeof buf, "SOULs", souls);
    text_center(0x167, buf);

    colors(g_state.lost_souls == 0 ? GREEN : RED);
    snprintf(buf, sizeof buf, "Lost: %s", fmt_thousands_k(num, g_state.lost_souls));
    text_center(0x171, buf);

    colors(bignum_ge(g_state.embos, g_last_embos) ? GREEN : RED);
    g_last_embos = g_state.embos;
    fmt_big(buf, sizeof buf, "EMBOs", g_state.embos);
    text_center(0x17c, buf);

    uint32_t rate = g_state.unk_630430;
    colors(rate < g_last_rate ? RED : GREEN);
    g_last_rate = rate;
    uint32_t r = rate * 100 >> 8;
    snprintf(buf, sizeof buf, "%u.%02u\b per SOUL", (unsigned)(r / 100), (unsigned)(r % 100));
    text_center(0x187, buf);

    statusbar_draw_date();

    Bignum p = g_state.pennies;
    trend_colors(p, g_last_pennies, &g_pennies_up);
    g_last_pennies = p;
    if (p.hi == 0)
        snprintf(buf, sizeof buf, "%s\b", fmt_thousands(num, p.lo));
    else if (p.hi == -1)
        snprintf(buf, sizeof buf, "-%s\b", fmt_thousands(num, p.lo));
    else if (p.hi < 1)
        snprintf(buf, sizeof buf, "-%sM\b",
                 fmt_thousands(num, (uint32_t)((p.hi + 1) * -0x10c7) + p.lo / 1000000u));
    else
        snprintf(buf, sizeof buf, "%sM\b", fmt_thousands(num, p.lo / 1000000u + (uint32_t)p.hi * 0x10c7u));
    text_center(0x12, buf);

    /* the price of the tool (0x57e100) */
    colors(bignum_ge_int(g_state.pennies, g_build.cost_shown) ? GREEN : RED);
    if (!g_build.editor) {
        if (g_build.cost_shown != 0) {
            snprintf(buf, sizeof buf, "Cost: %s \b", fmt_thousands_k(num, (uint32_t)g_build.cost_shown));
            text_center(0x28, buf);
        }
    } else {
        snprintf(buf, sizeof buf, "Uber!: %d", g_build.tool_index);
        text_center(0x28, buf);
    }

    static const char *const names[] = {
        NULL,
        "Envy/Contented",
        "Avarice/Charity",
        "Gluttny/Temprnce",
        "Sloth/Diligence",
        "Lust/Chastity",
        "Wrath/Peaceful",
        "Pride/Humility",
        "Zoning: Generic",
        "Zoning: Gates",
        "Zoning: Roads",
        "Zoning: Stations",
        "Zoning: Track",
        "Zoning: Topias",
        "Zoning: T-Centers",
        "Zoning: Ports",
        NULL, /* banks or siphons */
        "Zoning: SPECIALS",
        "LDomes/Omniblges",
        "Zoning: Limbos",
        "Nuke Structure",
        "Microview",
        "SOULview",
    };
    const char *name;
    int tool = g_shell_tool;
    if (tool == 0x10)
        name = g_build.tool_set == 0x13 ? "Zoning: Banks" : "Zoning: Siphons";
    else if (tool >= 1 && tool <= 0x16)
        name = names[tool];
    else
        name = "Unknown Command";
    text_center(0x1d, name);

    GfxRect r1 = {0xf, 0x70, 7, 0x30};
    wm_redraw(g_ctrlpanel_window, &r1);
    GfxRect r2 = {0xf, 0x6f, 0x166, 399};
    wm_redraw(g_ctrlpanel_window, &r2);
}

/* 0x446904 statusbar_draw_date */
void statusbar_draw_date(void)
{
    GfxSurface *s = surf();
    if (!s)
        return;
    int year = (int)g_state.year;
    /* digits that roll (the trailing 9s and the one before them) and the
     * ones that don't */
    int rolling = 0, fixed = 0, v = year;
    bool nine;
    for (;;) {
        nine = v % 10 == 9;
        v /= 10;
        if (v == 0)
            break;
        if (!nine)
            break;
        rolling++;
    }
    rolling += nine ? 2 : 1;
    for (; v != 0; v /= 10)
        fixed++;
    /* the scheduler's progress: where the next digit shows (y of the
     * rolling ones, 0 = not shown) */
    int y_cur, y_next;
    switch (g_state.sim_phase) {
    case 10:
    case 15:
    case 100:
        y_cur = 7, y_next = 0x10;
        break;
    case 40:
        y_cur = 5, y_next = 0xe;
        break;
    case 50:
        y_cur = 4, y_next = 0xd;
        break;
    case 60:
        y_cur = 3, y_next = 0xc;
        break;
    case 70:
        y_cur = 2, y_next = 0xb;
        break;
    case 80:
        y_cur = 1, y_next = 10;
        break;
    case 90:
        y_cur = 6, y_next = 0xf;
        break;
    default:
        y_cur = 8, y_next = 0;
        break;
    }
    GfxRect clip = {0xf, 0x5f, 7, 0x11};
    gfx_fill_rect(s, 0xf, 7, 0x50, 0xb, PANEL_BG);
    v = year;
    int x = clip.right - 0xc;
    gfx_font_select(500);
    colors(g_game_speed == 0 ? RED : GREEN);
    bool carry = true;
    char d[8];
    while (rolling-- > 0) {
        int digit = v % 10;
        if (digit == 0 && v < 10 && year != 0) {
            if (v < 10 && carry && y_next != 0) {
                snprintf(d, sizeof d, "%d", 1);
                gfx_font_draw_text(s, x, y_next, &clip, &clip, d, 0);
            }
        } else {
            snprintf(d, sizeof d, "%d", digit);
            gfx_font_draw_text(s, x, y_cur, &clip, &clip, d, 0);
            if (digit == 9) {
                digit = 0;
            } else {
                carry = false;
                digit++;
            }
            if (y_next != 0) {
                snprintf(d, sizeof d, "%d", digit);
                gfx_font_draw_text(s, x, y_next, &clip, &clip, d, 0);
            }
        }
        v /= 10;
        x -= 6;
    }
    while (fixed-- > 0) {
        snprintf(d, sizeof d, "%d", v % 10);
        gfx_font_draw_text(s, x, 8, &clip, &clip, d, 0);
        x -= 6;
        v /= 10;
    }
    x -= 0x18;
    gfx_font_draw_text(s, x, 8, &clip, &clip, "Year:", 0);
    wm_redraw(g_ctrlpanel_window, &clip);
}

/* 0x446dc5 */
void statusbar_draw_weights(void)
{
    GfxSurface *s = surf();
    if (!s)
        return;
    static const uint8_t hot[7] = {0x0a, 0x4d, 0x56, 0x67, 0x36, 0x44, 0x7e};  /* 0x65ed58 */
    static const uint8_t cold[7] = {0x58, 0x0b, 0x0d, 0x62, 0x30, 0x0c, 0x0e}; /* 0x65ed68 */
    if (++g_weight_hot > 6)
        g_weight_hot = 0;
    int x = 0x60, y = 7;
    gfx_fill_rect(s, 0x60, 7, 0xe, 10, PANEL_BG);
    y += 10;
    for (int i = 0; i < 7; i++) {
        int h = g_state.unk_593260[i][1] / 0x18;
        if (h > 9)
            h = 10;
        if (i == g_weight_hot) {
            gfx_fill_rect(s, x - 1, y - h + 1, 4, h, hot[i]);
        } else {
            gfx_line(s, x, x, y, y - h, cold[i]);
            gfx_line(s, x + 1, x + 1, y, y - h, cold[i]);
        }
        x += 2;
    }
    GfxRect r = {0x5f, 0x6e, 7, 0x16};
    wm_redraw(g_ctrlpanel_window, &r);
}
