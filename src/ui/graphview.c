/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Graphview (0x40b1c0-0x41722f): see ui/graphview.h.
 *
 * Layout of the summary records (g_state.summary[0] Heaven, [1] Hell) as
 * graph_history_record fills them from the realm record (PlaneStats):
 *   unk_000[16]   +0x77e0  SOULs per belief type (bit 0-1 HAHA/HOHO/OPRA/
 *                          OCRA, bit 2 RALF, bit 3 SUSA): tenet, belief graphs
 *   unk_040[20]   +0x79c4  {x, y} of the banks (count unk_118[17]): budget
 *   unk_068/084   +0x7f3c/+0x7f58  per zone kind: the population graph's
 *                          first bar ("PERMANENT" in its help text)
 *   unk_0a0/0bc   +0x7f74/+0x7f90  per zone kind: its second bar
 *   unk_0d8[8]    +0x7fac  capacity per zone kind; unk_0f8 +0x7fcc unusable
 *   unk_118[0]    +0x7fec  population; [1..16] +0x7ff4..+0x8030 (labor
 *                          graph: [1] .. [14] workforce, [15] wages, [16]
 *                          road costs); [17] +0x804b number of banks
 *   unk_160/174/188 [5]  +0x804f/+0x8063/+0x8077  capacity, unusable
 *                          capacity and population of gates, Karma stations,
 *                          topias, ports, training centres
 *   unk_19c       +0x80f8  average AQ/DQ
 *   unk_19d/1a1   +0x80fd  the realm's SOUL pool (bignum hi, lo)
 *   unk_1a5/1a9/1ad  +0x810f/+0x8117/+0x8127  SOUL income, lost SOULs,
 *                          fate lock costs
 */
#include "ui/graphview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/resource.h"
#include "game/util.h"
#include "game/world.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "ui/controls.h"
#include "ui/views.h"

#define FONT 500
#define BG 0x13 /* window background colour */

/* ---- state -------------------------------------------------------------- */

static WmWindow *g_win;       /* 0x63865c */
static WmWindow *g_btn[0x8d]; /* 0x63958c: buttons by id */
static GfxSurface *g_panel;   /* 0x639b88: bitmap 0xd3, the panel under the graph */
static int32_t *g_sin_tab;    /* 0x651c64: resource 0x14b4, sin * 10^6 per degree */
static int32_t *g_cos_tab;    /* 0x651c68: resource 0x14b5 */
static uint32_t g_tab_len;    /* entries in both */
/* 0x639650: sin toggles, [mode * 7 + id - 0x65]; the "all" id 0x6c is
 * index 7, which is the next mode's index 0, as in the original. */
static int g_sin_on[GRAPH_MODES * 7 + 1];
static int g_tenet_on[GRAPH_MODES][9];    /* 0x639380 */
static int g_realm_on[GRAPH_MODES][2];    /* 0x6397d0: [mode][0] Heaven, [1] Hell */
static int g_series_on[GRAPH_SERIES];     /* 0x639350 */
static uint8_t g_mode = GRAPH_POPULATION; /* 0x651c78 */
static uint8_t g_scale;                   /* 0x639808: History Graph time scale */
static int g_drawn;                       /* 0x651c7c: a graph is on screen (enables the hover help) */
static int g_dragging;                    /* 0x651c84 */
static int g_grab_x, g_grab_y;            /* 0x4f3054, 0x4f3050 */
static uint8_t g_pct[8];                  /* 0x651c70: HAHA HOHO OPRA OCRA ALF RALF SUMA SUSA percentages */
static uint32_t g_last_year;              /* 0x651c80: year of the last history sample */

/* zone kind colours: 0x651c38 (structures), 0x651c48 / 0x651c58 (population bars) */
static const uint8_t col_38[9] = {0x0a, 0x0b, 0x0d, 0x1d, 0x0f, 0x0c, 0x0e, 0x10, 0x10};
static const uint8_t col_48[9] = {0x0a, 0x4d, 0x56, 0x67, 0x36, 0x44, 0x7e, 0x2b, 0x2b};
static const uint8_t col_58[9] = {0x58, 0x0b, 0x0d, 0x62, 0x30, 0x0c, 0x0e, 0x24, 0x24};

/* 0x664918: the 16 belief types */
static const char *const belief_names[16] = {"HAHAALFSUMA",  "HOHOALFSUMA",  "OPRAALFSUMA",  "OCRAALFSUMA",
                                             "HAHARALFSUMA", "HOHORALFSUMA", "OPRARALFSUMA", "OCRARALFSUMA",
                                             "HAHAALFSUSA",  "HOHOALFSUSA",  "OPRAALFSUSA",  "OCRAALFSUSA",
                                             "HAHARALFSUSA", "HOHORALFSUSA", "OPRARALFSUSA", "OCRARALFSUSA"};
/* 0x66c600 / 0x66c658 */
static const char *const virtue_names[7] = {"CONTENTMENT", "CHARITY", "TEMPERANCE", "DILIGENCE",
                                            "CHASTITY",    "PEACE",   "HUMILITY"};
static const char *const sin_names[7] = {"ENVY", "AVARICE", "GLUTTONY", "SLOTH", "LUST", "WRATH", "PRIDE"};

static int32_t i32(uint32_t v) { return (int32_t)v; }

static void font_colors(unsigned c1, unsigned c2)
{
    unsigned c[2] = {c1 & 0xff, c2 & 0xff};
    gfx_font_set_colors(FONT, 1, 2, c);
}

static void text(int x, int y, const char *s) { gfx_font_draw_text(g_win->surface, x, y, NULL, NULL, s, 0); }

static void fill(int x, int y, int w, int h, uint8_t c) { gfx_fill_rect(g_win->surface, x, y, w, h, c); }

static void help(int x, int y, int width, const char *s)
{
    if (views_hooks.help_balloon)
        views_hooks.help_balloon(x, y, width, s);
}

static void button_set(unsigned id, bool pressed)
{
    if (id < 0x8d && g_btn[id])
        ctl_button_set_pressed(g_btn[id], pressed, true);
}

static void button_vis(unsigned id, bool on)
{
    if (id < 0x8d && g_btn[id]) {
        ctl_button_enable(g_btn[id], on);
        ctl_button_show(g_btn[id], on);
    }
}

static bool button_pressed(unsigned id) { return id < 0x8d && g_btn[id] && ctl_is_pressed(g_btn[id]); }

/* ---- the yearly records ----------------------------------------------------- */

/* 0x40d34b */
void graph_history_record(void)
{
    static const int plane[2] = {GS_PLANE_HEAVEN, GS_PLANE_HELL};
    for (int k = 0; k < 2; k++) {
        GraphSummary *d = &g_state.summary[k];
        const PlaneStats *r = &g_state.plane[plane[k]].stats;
        memcpy(d->unk_000, r->unk_77e0, sizeof d->unk_000);
        memcpy(d->unk_040, r->unk_79c4, sizeof d->unk_040);
        memcpy(d->unk_068, r->unk_7f3c, sizeof d->unk_068);
        memcpy(d->unk_084, r->unk_7f58, sizeof d->unk_084);
        memcpy(d->unk_0a0, r->unk_7f74, sizeof d->unk_0a0);
        memcpy(d->unk_0bc, r->unk_7f90, sizeof d->unk_0bc);
        memcpy(d->unk_0d8, r->unk_7fac, sizeof d->unk_0d8);
        memcpy(d->unk_0f8, r->unk_7fcc, sizeof d->unk_0f8);
        d->unk_118[0] = r->population;
        for (int i = 1; i <= 16; i++)
            d->unk_118[i] = r->unk_7ff0[i]; /* +0x7ff4 .. +0x8030 */
        d->unk_118[17] = r->unk_804b;
        memcpy(d->unk_160, r->unk_804f, sizeof d->unk_160);
        memcpy(d->unk_174, r->unk_8063, sizeof d->unk_174);
        memcpy(d->unk_188, r->unk_8077, sizeof d->unk_188);
        d->unk_19c = r->unk_80f8;
        d->unk_19d = (uint32_t)r->souls.hi;
        d->unk_1a1 = r->souls.lo;
        d->unk_1a5 = r->unk_810f;
        d->unk_1a9 = r->unk_8117;
        d->unk_1ad = r->unk_8127;
    }
}

/* x86 shifts use the count mod 32 */
static uint32_t shl32(uint32_t v, unsigned n) { return v << (n & 31); }
static uint32_t shr32(uint32_t v, unsigned n) { return v >> (n & 31); }

static Rec72 *hrec(int i) { return &g_state.rec_5c4eb0[i]; }

/* The records as the original's packed bytes at 0x5c4eb0, right after the
 * middle plane's cell grid (0x5bb270 + 40000): the 20/400-year averages
 * read up to 20 samples back from the newest, which for a record with fewer
 * samples runs into the previous record (or, for record 0, the last cells
 * of plane 2), and one write can land in front of a record. hist_byte
 * reproduces those reads byte for byte; `off` is relative to 0x5c4eb0. */
/* address of byte b (0 = least significant) of a host u32 */
static uint8_t *u32_byte(uint32_t *w, int b)
{
    const uint16_t one = 1;
    return (uint8_t *)w + (*(const uint8_t *)&one ? b : 3 - b);
}

static uint8_t *hist_byte(int off)
{
    static uint8_t dummy;
    if (off >= 0 && off < 30 * 0x72) {
        Rec72 *r = hrec(off / 0x72);
        int f = off % 0x72;
        if (f < 100)
            return u32_byte(&r->v[f / 4], f % 4);
        if (f < 104)
            return u32_byte(&r->min, f - 100);
        if (f < 108)
            return u32_byte(&r->max, f - 104);
        if (f == 108)
            return &r->count;
        if (f == 109)
            return &r->unk_6d;
        return u32_byte(&r->shift, f - 110);
    }
    if (off < 0 && off >= -GS_MAP_CELLS * 4) {
        int i = GS_MAP_CELLS * 4 + off;
        return u32_byte(&g_state.plane[GS_PLANE_MIDDLE].cells[i / 4], i % 4);
    }
    dummy = 0;
    return &dummy;
}

static int32_t hist_read32(int off)
{
    return (int32_t)((uint32_t)*hist_byte(off) | (uint32_t)*hist_byte(off + 1) << 8 |
                     (uint32_t)*hist_byte(off + 2) << 16 | (uint32_t)*hist_byte(off + 3) << 24);
}

static void hist_write32(int off, int32_t v)
{
    for (int i = 0; i < 4; i++)
        *hist_byte(off + i) = (uint8_t)((uint32_t)v >> (i * 8));
}

/* 0x414873: shifts the samples of records rec..rec+2 right by n and adds n
 * to their shift */
static void history_rescale(int rec, int n)
{
    for (int i = rec; i < rec + 3 && i < 30; i++) {
        Rec72 *r = hrec(i);
        for (int k = 0; k < 25; k++)
            r->v[k] = (uint32_t)(i32(r->v[k]) >> (n & 31));
        r->shift += (uint32_t)n;
    }
}

/* Appends `value` to a full (25) or growing record, keeping min/max over
 * the old samples in *lo / *hi (the part of 0x414903 used three times). */
static void history_push(Rec72 *r, int32_t value, int32_t *lo, int32_t *hi, int last_offset)
{
    int8_t count = (int8_t)r->count;
    if (count == 25) {
        int k;
        for (k = 0; k < 24; k++) {
            r->v[k] = r->v[k + 1];
            if (i32(r->v[k]) < *lo)
                *lo = i32(r->v[k]);
            if (*hi < i32(r->v[k]))
                *hi = i32(r->v[k]);
        }
        r->v[k + last_offset] = (uint32_t)value;
    } else {
        for (int k = 0; k < count + last_offset; k++) {
            if (i32(r->v[k]) < *lo)
                *lo = i32(r->v[k]);
            if (*hi < i32(r->v[k]))
                *hi = i32(r->v[k]);
        }
        int at = count + last_offset;
        if (at >= 0 && at < 25)
            r->v[at] = (uint32_t)value;
        r->count = (uint8_t)(count + 1);
    }
}

/* The 20- or 400-year step of 0x414903 for record p (the finer scale) into
 * p + 1. `bug_end` reproduces the 400-year branch writing the current value
 * one past the last sample. */
static void history_period(int p, int32_t value, bool bug_end)
{
    Rec72 *r = hrec(p);
    if (g_state.year == 0) {
        r->count = 1;
        r->min = r->max = (uint32_t)value;
        return;
    }
    int32_t div = i32(r->max) > 50000000 ? 20 : 1;
    int32_t sum = 0;
    for (int k = 1; k < 21; k++) /* (may read before the record, see hist_byte) */
        sum += hist_read32(p * 0x72 + ((int8_t)r->count - k) * 4) / div;
    int32_t avg = div * (sum / 20);
    Rec72 *n = hrec(p + 1);
    int32_t lo = avg, hi = avg;
    /* the average replaces the running (last) sample of the coarser scale */
    if ((int8_t)n->count == 25) {
        for (int k = 0; k < 24; k++) {
            n->v[k] = n->v[k + 1];
            if (i32(n->v[k]) < lo)
                lo = i32(n->v[k]);
            if (hi < i32(n->v[k]))
                hi = i32(n->v[k]);
        }
        n->v[23] = (uint32_t)avg;
    } else {
        for (int k = 0; k < (int8_t)n->count - 1; k++) {
            if (i32(n->v[k]) < lo)
                lo = i32(n->v[k]);
            if (hi < i32(n->v[k]))
                hi = i32(n->v[k]);
        }
        /* with no samples yet this writes in front of the record */
        hist_write32((p + 1) * 0x72 + ((int8_t)n->count - 1) * 4, avg);
        n->count = (uint8_t)((int8_t)n->count + 1);
    }
    int32_t m = avg < lo ? avg : lo;
    if (hi <= avg)
        hi = avg;
    /* and the current value starts the next period */
    int at = (int8_t)n->count - (bug_end ? 0 : 1);
    if (at >= 0 && at < 25)
        n->v[at] = (uint32_t)value;
    if (value <= m)
        m = value;
    if (hi <= value)
        hi = value;
    n->min = (uint32_t)m;
    n->max = (uint32_t)hi;
}

/* The running sample of the coarser scale p + 1 (no period boundary). */
static void history_running(int p, int32_t value)
{
    Rec72 *n = hrec(p + 1);
    if ((int8_t)n->count == 0)
        n->count = 1;
    int at = (int8_t)n->count - 1;
    if (at >= 0 && at < 25)
        n->v[at] = (uint32_t)value;
    int32_t lo = i32(n->min), hi = i32(n->max);
    if (value <= lo)
        lo = value;
    if (hi <= value)
        hi = value;
    n->min = (uint32_t)lo;
    n->max = (uint32_t)hi;
}

/* 0x414903 */
void graph_history_add(int rec, int32_t value, int shift)
{
    if (rec < 0 || rec + 2 >= 30)
        return;
    g_last_year = g_state.year;
    Rec72 *r = hrec(rec);
    if (i32(r->shift) < shift)
        history_rescale(rec, shift - i32(r->shift));
    int32_t lo = value, hi = value;
    history_push(r, value, &lo, &hi, 0);
    if (value <= lo)
        lo = value;
    if (hi <= value)
        hi = value;
    r->min = (uint32_t)lo;
    r->max = (uint32_t)hi;

    /* every 20 years the average of the last 20 samples goes to the next
     * scale; at year 0 only this record restarts. p follows the original:
     * it moves on to the next scale except in that year-0 case. */
    int p = rec;
    if (g_state.year % 20 == 0) {
        if (g_state.year != 0) {
            history_period(p, value, false);
            p++;
        } else
            history_period(p, value, false);
    } else {
        history_running(p, value);
        p++;
    }
    if (g_state.year % 400 == 0)
        history_period(p, value, true);
    else
        history_running(p, value);
}

/* The bignum-to-32-bit compression of 0x4154ab: shift needed to fit
 * {hi, lo} (hi >= 0) and the value at the record's (larger) shift. */
static void history_compress(int32_t hi, uint32_t lo, uint32_t limit, uint32_t rec_shift, uint32_t *value,
                             uint32_t *shift)
{
    uint32_t s, v;
    if (hi == 0) {
        s = lo > limit;
        v = rec_shift < s ? shr32(lo, s) : shr32(lo, rec_shift);
    } else {
        uint32_t n = 1, next = 1;
        int32_t h = hi;
        do {
            n = next;
            h >>= 1;
            next = n + 1;
            if (h < 1)
                break;
        } while (next < 0x18);
        if (lo > limit)
            next = n + 2;
        s = next;
        uint32_t top = shl32((uint32_t)hi, 32 - s);
        v = rec_shift < s ? shr32(lo, s) + top : shr32(top, rec_shift - s) + shr32(lo, rec_shift);
    }
    *value = v;
    *shift = s;
}

/* 0x4154ab */
void graph_history_update(void)
{
    uint32_t v, s;
    /* Hell (record 3): population plus the realm's SOUL pool */
    {
        const GraphSummary *h = &g_state.summary[1];
        Bignum b = {(int32_t)h->unk_19d, h->unk_1a1};
        if (h->unk_118[0] < 0x80000000u)
            bignum_add(&b, h->unk_118[0]);
        else {
            bignum_add(&b, h->unk_118[0] >> 1);
            bignum_add(&b, h->unk_118[0] >> 1);
        }
        history_compress(b.hi, b.lo, 0x7fffffffu, hrec(3)->shift, &v, &s);
        graph_history_add(3, (int32_t)v, (int)s);
    }
    /* Heaven (record 0) */
    {
        const GraphSummary *h = &g_state.summary[0];
        Bignum b = {(int32_t)h->unk_19d, h->unk_1a1};
        if (h->unk_118[0] < 0x80000000u)
            bignum_add(&b, h->unk_118[0]);
        else {
            bignum_add(&b, h->unk_118[0] >> 1);
            bignum_add(&b, h->unk_118[0] >> 1);
        }
        history_compress(b.hi, b.lo, 0x3fffffffu, hrec(0)->shift, &v, &s);
        graph_history_add(0, (int32_t)v, (int)s);
    }
    /* pennies (record 6) */
    if (g_state.pennies.hi < 0) {
        uint32_t lo = g_state.pennies.lo < 0x40000000u ? g_state.pennies.lo : 0x3fffffffu;
        graph_history_add(6, -((int32_t)lo >> (hrec(6)->shift & 31)), 0);
    } else {
        int32_t hi = g_state.pennies.hi;
        uint32_t lo = g_state.pennies.lo, rs = hrec(6)->shift;
        if (hi < 1) {
            s = lo > 0x7fffffffu;
            v = rs < s ? shr32(lo, s) : shr32(lo, rs);
        } else {
            /* unlike the populations, no cap on the bit count */
            uint32_t n = 1, next = 1;
            int32_t h = hi;
            do {
                n = next;
                h >>= 1;
                next = n + 1;
            } while (0 < h);
            if (lo > 0x7fffffffu)
                next = n + 2;
            s = next;
            uint32_t top = shl32((uint32_t)hi, 32 - s);
            v = rs < s ? shr32(lo, s) + top : shr32(top, rs - s) + shr32(lo, rs);
        }
        graph_history_add(6, (int32_t)v, (int)s);
    }
    graph_history_add(9, (int32_t)g_state.unk_630430, 0); /* soul rate, 8.8 */
#define CLAMP31(x) ((x) < 0x80000000u ? (int32_t)(x) : 0x7fffffff)
    graph_history_add(12, CLAMP31(g_state.unk_5fa5f4), 0); /* penny inflow */
    graph_history_add(15, CLAMP31(g_state.unk_5f7ed4), 0); /* penny outflow */
    graph_history_add(18, CLAMP31(g_state.unk_626598), 0); /* SOUL losses */
#undef CLAMP31
    graph_history_add(21, (int32_t)g_state.unk_638690, 0); /* average efficiency */
    graph_history_add(24, (int32_t)g_state.unk_593250, 0); /* fate structures */
    graph_history_add(27, (int32_t)g_state.unk_57e09c, 0); /* overall spiffiness */
}

/* ---- drawing helpers ------------------------------------------------------------ */

/* 0x40f4b8: clears the graph area */
static void graph_clear(void) { fill(0xe, 0x10, 299, 200, BG); }

/* 0x40f4e4: "Graphview - <name>" as the title */
static void graph_title(const char *name)
{
    if (!name)
        return;
    char buf[80];
    gfx_font_select(FONT);
    font_colors(0xb, BG);
    snprintf(buf, sizeof buf, "Graphview - %s", name);
    wm_set_title(g_win, buf);
    views_draw_title(g_win);
}

/* 0x40f559: an amount below `limit` with thousands separators, else in
 * K (limit < 10^6) or M with one decimal; `penny` appends the penny
 * glyph (character 8). */
static char *fmt_amount(char *buf, size_t n, uint32_t v, uint32_t limit, int penny)
{
    char t[FMT_NUM_LEN];
    uint32_t unit = limit < 1000000 ? 1000 : 1000000;
    if (v < limit)
        snprintf(buf, n, "%s", fmt_thousands(t, v));
    else {
        snprintf(buf, n, "%s", fmt_thousands(t, v / unit));
        uint32_t tenth = (v % unit) / (unit / 10);
        size_t l = strlen(buf);
        if (tenth)
            snprintf(buf + l, n - l, ".%u", (unsigned)tenth);
        l = strlen(buf);
        snprintf(buf + l, n - l, "%s", unit < 1000000 ? "K" : "M");
    }
    if (penny) {
        size_t l = strlen(buf);
        snprintf(buf + l, n - l, "%c", 8);
    }
    return buf;
}

/* 0x40f272: hides every mode-specific button and restores the panel */
static void panel_reset(void)
{
    button_vis(GV_ID_HELL, false);
    button_vis(GV_ID_HEAVEN, false);
    for (unsigned i = 0; i < 9; i++)
        button_vis(GV_ID_TENET + i, false);
    for (unsigned i = 0; i < 7; i++)
        button_vis(GV_ID_SIN + i, false);
    for (unsigned i = 0; i < 10; i++)
        button_vis(GV_ID_SERIES + i, false);
    button_vis(GV_ID_ALL_SINS, false);
    button_vis(GV_ID_ALL_TENETS, false);
    for (unsigned i = 0; i < 3; i++)
        button_vis(GV_ID_SCALE + i, false);
    if (g_panel) {
        gfx_blit(g_win->surface, 0xd, 0xd8, g_panel->width, g_panel->height, g_panel, 0, 0);
        GfxRect r = {0xf, g_panel->width + 0xe, 0xda, g_panel->height + 0xd9};
        wm_redraw(g_win, &r);
    }
}

/* ---- the graphs --------------------------------------------------------------- */

/* 0x40f6b3 graph_budget_draw */
static void graph_budget(void)
{
    char buf[80], a[40];
    graph_clear();
    graph_title("Budget Graph");
    int prev = views_select_plane(GS_PLANE_HEAVEN);

    /* Heaven's ledger */
    const GraphSummary *s = &g_state.summary[0];
    font_colors(0xe, BG);
    int32_t income = i32(s->unk_1a5);
    text(0x25, 0x11, "Heaven's Ledger");
    text(0x11, 0x25, "Pennies In:");
    text(0x1d, 0x2f, "SOULs:");
    text(0x5e, 0x2f, fmt_amount(a, sizeof a, (uint32_t)income, 10000000, 1));
    text(0x11, 0x43, "Pennies Out:");
    text(0x1d, 0x4d, "Angels:");
    text(0x5e, 0x4d, fmt_amount(a, sizeof a, s->unk_118[15], 10000000, 1));
    int32_t out = i32(s->unk_118[15]);
    text(0x1d, 0x57, "Roads:");
    text(0x5e, 0x57, fmt_amount(a, sizeof a, s->unk_118[16], 10000000, 1));
    out += i32(s->unk_118[16]);
    text(0x1d, 0x61, "Fate Locks:");
    text(0x5e, 0x61, fmt_amount(a, sizeof a, s->unk_1ad, 10000000, 1));
    out += i32(s->unk_1ad);
    text(0x1d, 0x6b, "Lost SOULs:");
    text(0x5e, 0x6b, fmt_amount(a, sizeof a, s->unk_1a9, 10000000, 1));
    out += i32(s->unk_1a9);
    /* loan payments of the banks: sim cell +1 (loan) / +5 (years) */
    int32_t pay = 0;
    for (uint16_t i = 0; i < s->unk_118[17] && i < 20; i++) {
        int x = (int8_t)s->unk_040[i][0], y = (int8_t)s->unk_040[i][1];
        if (x < 0 || x >= GS_MAP_W || y < 0 || y >= GS_MAP_H || !g_world.sim)
            continue;
        const SimCell *c = &g_world.sim[y * GS_MAP_W + x];
        uint32_t years = (uint32_t)(int32_t)(int8_t)c->b[5];
        if (years != 0)
            pay += (int32_t)(sim_cell_u32(c, 1) / years);
    }
    text(0x1d, 0x75, "Loan:");
    text(0x22, 0x7f, "Payments:");
    text(0x5e, 0x7f, fmt_amount(a, sizeof a, (uint32_t)pay, 10000000, 1));
    gfx_line(g_win->surface, 0x11, 0x93, 0xac, 0xac, 0xe);
    int32_t net = income - (out + pay);
    font_colors(net < 0 ? 0xc : 0xa, BG);
    text(0x11, 0xbb, "Net Income:");
    if (net < 0)
        snprintf(buf, sizeof buf, "-%s", fmt_amount(a, sizeof a, (uint32_t)abs(net), 10000000, 1));
    else
        snprintf(buf, sizeof buf, "%s", fmt_amount(a, sizeof a, (uint32_t)net, 10000000, 1));
    text(0x5e, 0xbb, buf);

    /* Hell's ledger */
    views_select_plane(GS_PLANE_HELL);
    s = &g_state.summary[1];
    font_colors(0xc, BG);
    income = i32(s->unk_1a5);
    text(0xba, 0x11, "Hell's Ledger");
    text(0xa6, 0x25, "Pennies In:");
    text(0xb2, 0x2f, "SOULs:");
    text(0xf3, 0x2f, fmt_amount(a, sizeof a, (uint32_t)income, 10000000, 1));
    text(0xa6, 0x43, "Pennies Out:");
    text(0xb2, 0x4d, "Demons:");
    text(0xf3, 0x4d, fmt_amount(a, sizeof a, s->unk_118[15], 10000000, 1));
    out = i32(s->unk_118[15]);
    text(0xb2, 0x57, "Roads:");
    text(0xf3, 0x57, fmt_amount(a, sizeof a, s->unk_118[16], 10000000, 1));
    out += i32(s->unk_118[16]);
    text(0xb2, 0x61, "Fate Locks:");
    text(0xf3, 0x61, fmt_amount(a, sizeof a, s->unk_1ad, 10000000, 1));
    out += i32(s->unk_1ad);
    text(0xb2, 0x6b, "Lost SOULs:");
    text(0xf3, 0x6b, fmt_amount(a, sizeof a, s->unk_1a9, 10000000, 1));
    out += i32(s->unk_1a9);
    /* SOULs at risk in Hell's banks */
    uint32_t risk = 0;
    for (uint16_t i = 0; i < s->unk_118[17] && i < 20; i++) {
        int x = (int8_t)s->unk_040[i][0], y = (int8_t)s->unk_040[i][1];
        if (x < 0 || x >= GS_MAP_W || y < 0 || y >= GS_MAP_H || !g_world.sim)
            continue;
        const SimCell *c = &g_world.sim[y * GS_MAP_W + x];
        if ((int8_t)c->b[5] > 0 && g_state.unk_630430 != 0)
            risk += (uint32_t)((int8_t)c->b[6] + 10) * (sim_cell_u32(c, 1) / 10) * 0x100 / g_state.unk_630430;
    }
    pay = 0;
    if (g_state.unk_5bb108 != 0)
        pay = (int32_t)mul_div_round(g_state.unk_630430, 0x100, g_state.unk_5bb108);
    risk += g_state.unk_57e058;
    text(0xb2, 0x75, "Loan:");
    text(0xb7, 0x7f, "Payments:");
    text(0xf3, 0x7f, fmt_amount(a, sizeof a, (uint32_t)pay, 10000000, 1));
    text(0xb2, 0x89, "SOULs");
    text(0xb7, 0x93, "At Risk:");
    if (risk < 10000001)
        snprintf(buf, sizeof buf, "%s", fmt_thousands(a, risk));
    else
        snprintf(buf, sizeof buf, "%sM", fmt_thousands(a, risk / 1000000));
    text(0xf3, 0x93, buf);
    gfx_line(g_win->surface, 0xa6, 0x128, 0xac, 0xac, 0xc);
    net = income - out; /* Hell's loan payments are not subtracted */
    font_colors(net < 0 ? 0xc : 0xa, BG);
    text(0xa6, 0xbb, "Net Income:");
    if (net < 0)
        snprintf(buf, sizeof buf, "-%s", fmt_amount(a, sizeof a, (uint32_t)abs(net), 10000000, 1));
    else
        snprintf(buf, sizeof buf, "%s", fmt_amount(a, sizeof a, (uint32_t)net, 10000000, 1));
    text(0xf3, 0xbb, buf);
    views_select_plane(prev);
    g_drawn = 1;
}

/* One realm's half of the labor graph. */
static void labor_column(const GraphSummary *s, int x0, int x1, int x2, const char *who, const char *Who,
                         const char *task, const char *quotient)
{
    char buf[80], a[40];
    static const char *const places[8] = {"Ports", "KStations", "KTrack", "Siphons",
                                          "Banks", "TCenters",  "Gates",  "Limbos"};
    int32_t total = i32(s->unk_118[4]) + i32(s->unk_118[2]) + i32(s->unk_118[3]);
    snprintf(buf, sizeof buf, "%s %s", fmt_thousands(a, (uint32_t)total), who);
    text(x0, 0x14, buf);
    snprintf(buf, sizeof buf, "%s in %s", fmt_thousands(a, s->unk_118[6]), task);
    text(x1, 0x1e, buf);
    for (int i = 0; i < 8; i++) {
        snprintf(buf, sizeof buf, "%s in %s", fmt_thousands(a, s->unk_118[7 + i]), places[i]);
        text(x1, 0x28 + i * 10, buf);
    }
    text(x0, 0x78, "________________");
    uint32_t employed = s->unk_118[2] + s->unk_118[3];
    snprintf(buf, sizeof buf, "%s Employed %s", fmt_amount(a, sizeof a, employed, 1000000, 0), Who);
    text(x2, 0x82, buf);
    snprintf(buf, sizeof buf, "%s Idle %s", fmt_thousands(a, s->unk_118[4]), Who);
    text(x2, 0x8c, buf);
    if (s->unk_118[5] != 0) {
        snprintf(buf, sizeof buf, "%s %s RUNNING AMOK!", fmt_thousands(a, s->unk_118[5]), who);
        if (gfx_font_text_width(buf) > 0x8f)
            snprintf(buf, sizeof buf, "%s %s AMOK!", fmt_thousands(a, s->unk_118[5]), who);
        text(x2, 0x96, buf);
    }
    uint32_t imported = mul_div_clamp(s->unk_118[3], employed, 100);
    uint32_t housed = s->unk_160[2] <= s->unk_118[1] ? s->unk_160[2] : s->unk_118[1];
    uint32_t commuting = mul_div_clamp(employed - housed, employed, 100);
    snprintf(buf, sizeof buf, "%d%% Imported", (int)imported);
    text(x1, 0xaa, buf);
    snprintf(buf, sizeof buf, "%d%% Commuting", (int)commuting);
    text(x1, 0xb4, buf);
    snprintf(buf, sizeof buf, "%s Pennies per Year", fmt_thousands(a, s->unk_118[15]));
    text(x2, 0xbe, buf);
    if (total == 0)
        snprintf(buf, sizeof buf, "Average %s: n/a", quotient);
    else
        snprintf(buf, sizeof buf, "Average %s: %d", quotient, s->unk_19c);
    text(x2, 200, buf);
}

/* 0x41067a graph_labor_draw */
static void graph_labor(void)
{
    graph_clear();
    graph_title("Labor Graph");
    font_colors(0xe, BG);
    labor_column(&g_state.summary[0], 0x11, 0x1d, 0x17, "ANGELS", "Angels", "Rewards", "AQ");
    font_colors(0xc, BG);
    labor_column(&g_state.summary[1], 0xa6, 0xb2, 0xac, "DEMONS", "Demons", "Punishments", "DQ");
    g_drawn = 1;
}

/* 0x412362 graph_belief_draw */
static void graph_belief(void)
{
    const GraphSummary *p = NULL;
    int n = 0;
    if (button_pressed(GV_ID_HEAVEN))
        p = &g_state.summary[0], n = 1;
    if (button_pressed(GV_ID_HELL)) {
        if (!p)
            p = &g_state.summary[1];
        n++;
    }
    uint32_t sums[16] = {0}, total = 0;
    for (int r = 0; r < n; r++) {
        for (int t = 0; t < 16; t++) {
            sums[t] += p->unk_000[t];
            total += p->unk_000[t];
        }
        p = &g_state.summary[1];
    }
    graph_title("Belief Graph");
    if (total == 0)
        return;
    graph_clear();
    uint8_t c = 0x7b;
    font_colors(0x7b, 0x7b);
    int top = 0x28, bottom = 0x31;
    for (int t = 0; t < 16; t++) {
        if (sums[t] != 0) {
            /* faithful: clamped to the count, not to the bar length */
            int w = (uint16_t)mul_div_clamp(199, total, sums[t]);
            fill(0x5e, top, w, 10, c);
            GfxRect r = {w + 0x5d, 0x124, top, bottom};
            gfx_frame_rect(g_win->surface, &r, c);
        }
        text(0xe, top, belief_names[t]);
        top += 0xb;
        bottom += 0xb;
        if (t == 3)
            c = 0xc;
        else if (t == 7)
            c = 0xb;
        else if (t == 0xb)
            c = 0x58;
        if (t == 3 || t == 7 || t == 0xb)
            font_colors(c, c);
    }
    g_drawn = 1;
}

/* 0x412620: the population graph's scale for a largest bar */
static uint32_t population_scale(uint32_t v)
{
    static const uint32_t steps[] = {1000, 5000, 25000, 125000, 625000, 3125000, 15625000, 78125000, 390625000};
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++)
        if (v < steps[i] + 1)
            return steps[i];
    return 3000000000u;
}

/* 0x41271e graph_population_draw: with scale 0 only measures; returns the
 * largest value (bar or capacity). */
static int32_t graph_population(int32_t scale)
{
    static const char *const heaven_word[7] = {"CONTENTED", "CHARITABLE", "TEMPERATE", "DILIGENT",
                                               "CHASTE",    "PEACEFUL",   "HUMBLE"};
    static const char *const hell_word[7] = {"ENVIOUS", "AVARICIOUS", "GLUTTONOUS", "SLOTHFUL",
                                             "LUSTFUL", "WRATHFUL",   "PROUD"};
    char buf[48], a[FMT_NUM_LEN];
    graph_title("Population Graph");
    int32_t largest = 0;
    bool both = g_realm_on[g_mode][0] && g_realm_on[g_mode][1];
    int row_h = both ? 0xb : 0x17, total_h = both ? 0x99 : 0xa0;
    int y = 0xd7 - total_h;
    int div = 2;
    if (scale != 0) {
        graph_clear();
        uint32_t top = population_scale((uint32_t)scale);
        div = (int)(top / 200);
        if (div < 3)
            div = 2;
        fill(0x124, y, 1, total_h, 0x1a);
        snprintf(buf, sizeof buf, "Scale: %s", fmt_thousands(a, top));
        int w = gfx_font_text_width(buf);
        if (w < 0x47)
            w = 0x46;
        font_colors(0x1a, 0x1a);
        text(0x133 - w, y - 10, buf);
    }
    for (int k = 0; k < 7; k++) {
        if (!g_sin_on[7 + k]) /* the population mode's row, 0x63966c */
            continue;
        for (int r = 0; r < 2; r++) {
            int32_t sum = 0;
            int x = 0x54;
            const GraphSummary *s = NULL;
            if (r == 0 && g_realm_on[g_mode][0])
                s = &g_state.summary[0];
            else if (r == 1 && g_realm_on[g_mode][1])
                s = &g_state.summary[1];
            if (s) {
                uint8_t z = g_state.unk_593240[k];
                if (z > 6)
                    z = 6;
                sum = i32(s->unk_084[z] + s->unk_068[z]);
                if (sum != 0 && scale != 0) {
                    int w = sum / div;
                    if (w + 0x54 > 0x131)
                        w = 0xdd;
                    if (w > 0) {
                        fill(0x54, y, w, row_h, col_48[z]);
                        x += w;
                    }
                }
                int32_t t = i32(s->unk_0bc[z] + s->unk_0a0[z]);
                sum += t;
                if (t != 0 && scale != 0) {
                    int w = t / div;
                    if (w + x > 0x131)
                        w = 0x131 - x;
                    if (w > 0)
                        fill(x, y, w, row_h, col_58[z]);
                }
                if (scale != 0) {
                    snprintf(buf, sizeof buf, "%s", r == 0 ? heaven_word[z] : hell_word[z]);
                    font_colors(col_58[z], col_58[z]);
                    text(0x10, y + 1, buf);
                }
                int32_t cap = i32(s->unk_0d8[z]);
                if (largest < cap)
                    largest = cap;
                x = 0x54;
                if (cap != 0 && scale != 0) {
                    int w = cap / div;
                    if (w + 0x54 > 0x131)
                        w = 0xdd;
                    if (w > 0) {
                        uint8_t c = col_58[z];
                        fill(0x54, y, w, 1, c);
                        fill(x, y, 1, row_h, c);
                        fill(w + x - 1, y, 1, row_h, c);
                        fill(x, y + row_h - 1, w, 1, c);
                        if (s->unk_0f8[z] != 0) {
                            int g = (int)(s->unk_0f8[z] / (uint32_t)div);
                            if (g > 1)
                                fill(w + x - g, y + 1, g - 1, row_h - 2, 0x10);
                        }
                    }
                }
            }
            if (largest < sum)
                largest = sum;
            if (both)
                y += row_h;
        }
        if (!both)
            y += row_h;
    }
    if (scale != 0)
        g_drawn = 1;
    return largest;
}

/* 0x412ece: one structures bar: outline `h` high, filled in proportion
 * pop/cap, the unusable part dark at the top */
static void structure_bar(int x, int y, uint32_t pop, uint32_t cap, uint32_t unusable, int h, int w, uint8_t frame,
                          uint8_t fillc)
{
    if (cap == 0)
        return;
    int f = (int)mul_div_clamp((uint32_t)h, cap, pop);
    fill(x, y - f, w, f, fillc);
    fill(x, y - h, w, 1, frame);
    fill(x, y, w, 1, frame);
    fill(x, y - h, 1, h, frame);
    fill(w + x - 1, y - h, 1, h, frame);
    if (unusable != 0) {
        int d = (int)mul_div_clamp((uint32_t)h, cap, unusable);
        if (d > 0)
            fill(x + 1, y - h + 1, w - 2, d, 0x10);
    }
}

static void structure_count(char *buf, size_t n, const char *realm, uint32_t c)
{
    if (c < 10000)
        snprintf(buf, n, " %s: %d", realm, (int)c);
    else if (c < 10000000)
        snprintf(buf, n, " %s: %dK", realm, (int)(c / 1000));
    else
        snprintf(buf, n, " %s: %dM", realm, (int)(c / 1000000));
}

/* 0x413018 graph_structures_draw */
static void graph_structures(void)
{
    static const char *const names[5] = {"GATES", "STATIONS", "TOPIAS", "PORTS", "T-CENTERS"};
    const GraphSummary *hv = &g_state.summary[0], *hl = &g_state.summary[1];
    char buf[40];
    graph_clear();
    graph_title("Structures Graph");
    int x = 0xe;
    for (int i = 0; i < 5; i++) {
        int lx = x, ly = 0x10;
        uint32_t mx = hl->unk_160[i] <= hv->unk_160[i] ? hv->unk_160[i] : hl->unk_160[i];
        if (mx != 0)
            structure_bar(x, 0xd7, hl->unk_188[i], hl->unk_160[i], hl->unk_174[i],
                          (int)mul_div_clamp(hl->unk_160[i], mx, 0xa9), 0x14, col_58[5], col_38[5]);
        x += 0x14;
        if (mx != 0)
            structure_bar(x, 0xd7, hv->unk_188[i], hv->unk_160[i], hv->unk_174[i],
                          (int)mul_div_clamp(hv->unk_160[i], mx, 0xa9), 0x14, col_58[6], col_38[6]);
        x += 0x28;
        font_colors(0xb, BG);
        text(lx, ly, names[i]);
        ly += 10;
        font_colors(0xc, BG);
        structure_count(buf, sizeof buf, "Hell", hl->unk_160[i]);
        text(lx, ly, buf);
        ly += 10;
        font_colors(0xe, BG);
        structure_count(buf, sizeof buf, "Hvn", hv->unk_160[i]);
        text(lx, ly, buf);
    }
    g_drawn = 1;
}

/* 0x41341c: the spiffiness rating as two words over (x, y) */
static void spiffiness_label(uint32_t v, int x, int y)
{
    static const struct {
        uint32_t below;
        const char *a, *b;
    } t[] = {{100000, "Ultra", "Dork"},   {500000, "Astro", "Dweeb"},  {1000000, "Mega", "Shmuck"},
             {2000000, "Feh", "Master"},  {3000000, "B", "Ark"},       {5000000, "Sorta", "Sucky"},
             {10000000, "Quasi", "Deft"}, {20000000, "Para", "Kewl"},  {40000000, "Omni", "Hip"},
             {50000000, "Uber", "Suave"}, {67000000, "Hoopy", "Frood"}};
    const char *a = "Project", *b = "Leader";
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (v < t[i].below) {
            a = t[i].a, b = t[i].b;
            break;
        }
    int y1 = y - 0x14;
    if (y1 < 0x11)
        y1 = 0x10;
    text(x, y1, a);
    y -= 10;
    if (y < 0x1b)
        y = 0x1a;
    text(x, y, b);
}

static void series_label(int rec, int32_t v, char ch, int x, int y)
{
    char buf[40];
    int32_t shift = i32(hrec(rec)->shift);
    y -= 10;
    if (y < 0x11)
        y = 0x10;
    if (rec >= 0x1b) {
        spiffiness_label((uint32_t)v, x, y);
        return;
    }
    if (rec >= 9 && rec <= 0xb) {
        /* the soul rate is 8.8 fixed point */
        int32_t m = (int32_t)((uint32_t)v * 100u);
        int32_t t = (m + ((m >> 31) & 0xff)) >> 8;
        snprintf(buf, sizeof buf, "%c: %d.%02d", ch, t / 100, t % 100);
    } else if (shift == 0) {
        if (v < -9999999)
            snprintf(buf, sizeof buf, "%c: %dM", ch, v / 1000000);
        else if (v < -99999)
            snprintf(buf, sizeof buf, "%c: %dK", ch, v / 1000);
        else if (v < 10000)
            snprintf(buf, sizeof buf, "%c: %d", ch, v);
        else if (v < 10000000)
            snprintf(buf, sizeof buf, "%c: %dK", ch, v / 1000);
        else
            snprintf(buf, sizeof buf, "%c: %dM", ch, v / 1000000);
    } else if (shift < 5)
        snprintf(buf, sizeof buf, "%c: %dM", ch, (int32_t)shl32((uint32_t)(v / 1000000), (unsigned)shift));
    else
        snprintf(buf, sizeof buf, "%c: %dB", ch, (int32_t)shl32((uint32_t)(v / 1000000), (unsigned)shift) / 1000);
    text(x, y, buf);
}

/* y of a sample scaled from [lo, hi] into [top, bottom] */
static int series_y(int32_t v, int32_t lo, int mul, int step, int top, int bottom)
{
    int32_t d = (int32_t)((uint32_t)(v - lo) * (uint32_t)mul);
    int y = bottom - d / step;
    if (y <= top)
        y = top;
    if (y > 0xd6)
        y = 0xd7;
    if (y < 0x11)
        y = 0x10;
    return y;
}

/* 0x4136ea: one series of the History Graph, 9 px per sample from x 0x26,
 * scaled from [lo, hi] into [top, bottom], with its value at the end */
static void history_series(int rec, int32_t lo, int32_t hi, uint8_t colour, char ch, int top, int bottom)
{
    const Rec72 *r = hrec(rec);
    int32_t range = (int32_t)((uint32_t)hi - (uint32_t)lo);
    int step = range / (bottom - top);
    if (step < 2)
        step = 1;
    int mul = step < 100 ? 100 : 1;
    step = (int32_t)((uint32_t)range * (uint32_t)mul) / (bottom - top);
    if (step < 2)
        step = 1;
    int8_t count = (int8_t)r->count;
    int period = rec % 3 == 0 ? 1 : rec % 3 == 1 ? 20 : 400;
    int x0 = 0x26;
    int y_prev = series_y(i32(r->v[0]), lo, mul, step, top, bottom);
    if (count < 2) {
        if (count == 1) {
            int y = series_y(i32(r->v[0]), lo, mul, step, top, bottom);
            int x1 = 0x2f;
            gfx_line(g_win->surface, 0x2a, 0x2f, y, y, colour);
            font_colors(colour, BG);
            gfx_font_select(FONT);
            series_label(rec, i32(r->v[0]), ch, x1 + 1, y);
        }
    } else {
        int x1 = 0x2f;
        for (int i = 1; i < count && i < 25; i++) {
            int y = series_y(i32(r->v[i]), lo, mul, step, top, bottom);
            if (period == 1 || count - 1 != i)
                gfx_line(g_win->surface, x0, x1, y_prev, y, colour);
            else {
                /* the running period of the coarse scales: a short stub */
                x1 = x0 + 9;
                gfx_line(g_win->surface, x0 + 4, x1, y, y, colour);
            }
            if (count - 1 == i) {
                font_colors(colour, BG);
                gfx_font_select(FONT);
                series_label(rec, i32(r->v[i]), ch, x1 + 1, y);
            }
            x0 += 9;
            x1 += 9;
            y_prev = y;
        }
    }
    g_drawn = 1;
}

/* 0x4140ca graph_history_draw */
static void graph_history(void)
{
    int s = g_scale;
    graph_clear();
    graph_title("History Graph");
    int32_t lo = i32(hrec(s)->min) <= i32(hrec(s + 3)->min) ? i32(hrec(s)->min) : i32(hrec(s + 3)->min);
    int32_t hi = i32(hrec(s + 3)->max) <= i32(hrec(s)->max) ? i32(hrec(s)->max) : i32(hrec(s + 3)->max);
    if (g_series_on[0])
        history_series(s + 3, lo, hi, 0xc, 'P', 0x10, 0xd7);
    if (g_series_on[1])
        history_series(s, lo, hi, 0xe, 'C', 0x10, 0xd7);
    if (g_series_on[2])
        history_series(s + 6, i32(hrec(s + 6)->min), i32(hrec(s + 6)->max), 0xb, 8, 0x1c, 0xd7);
    if (g_series_on[3])
        history_series(s + 9, 0x1a, 0xa00, 0x1a, 'S', 0x28, 0xcb);
    lo = i32(hrec(s + 15)->min) <= i32(hrec(s + 12)->min) ? i32(hrec(s + 15)->min) : i32(hrec(s + 12)->min);
    hi = i32(hrec(s + 12)->max) <= i32(hrec(s + 15)->max) ? i32(hrec(s + 15)->max) : i32(hrec(s + 12)->max);
    if (g_series_on[4])
        history_series(s + 12, lo, hi, 10, '+', 0x34, 0xbf);
    if (g_series_on[5])
        history_series(s + 15, lo, hi, 0x10, '-', 0x34, 0xbf);
    if (g_series_on[6])
        history_series(s + 18, i32(hrec(s + 18)->min), i32(hrec(s + 18)->max), 0xd, '*', 0x43, 0xa4);
    if (g_series_on[7])
        history_series(s + 21, i32(hrec(s + 21)->min), i32(hrec(s + 21)->max), 0x62, 'E', 0x37, 0xb0);
    if (g_series_on[8])
        history_series(s + 24, i32(hrec(s + 24)->min), i32(hrec(s + 24)->max), 100, 'F', 0x2b, 0xbc);
    if (g_series_on[9])
        history_series(s + 27, i32(hrec(s + 27)->min), i32(hrec(s + 27)->max), 0xf, '!', 0x1f, 200);
    g_drawn = 1;
}

/* 0x4144b2: the History Graph's legend and series toggles in the panel */
static void history_panel(void)
{
    static const struct {
        uint8_t colour;
        char ch;
        const char *fmt;
    } legend[10] = {{0xc, 'P', " %c: Hell Pop."},       {0xe, 'C', " %c: Heaven Pop."},
                    {0xb, 8, " %c: Total Pennies"},     {0x1a, 'S', " %c: SOUL Rate"},
                    {10, '+', " %c: Penny Inflow"},     {0x10, '-', " %c: Penny Outflow"},
                    {0xd, '*', " %c: SOUL Losses"},     {0x62, 'E', " %c: Average Efficiency"},
                    {100, 'F', " %c: Fate Structures"}, {0xf, '!', " %c: Overall Spiffiness"}};
    char buf[40];
    fill(0x10, 0xdc, 0x118, 0x50, BG);
    for (int i = 0; i < 10; i++) {
        font_colors(legend[i].colour, BG);
        snprintf(buf, sizeof buf, legend[i].fmt, legend[i].ch);
        text(i < 6 ? 0x22 : 0xac, 0xde + (i < 6 ? i : i - 6) * 10, buf);
    }
    for (unsigned i = 0; i < 10; i++) {
        if (g_btn[GV_ID_SERIES + i]) {
            ctl_button_show(g_btn[GV_ID_SERIES + i], true);
            ctl_button_enable(g_btn[GV_ID_SERIES + i], true);
        }
        button_set(GV_ID_SERIES + i, g_series_on[i] != 0);
    }
    GfxRect r = {0x10, 0x127, 0xdc, 299};
    wm_redraw(g_win, &r);
}

/* ---- the tenet pies ------------------------------------------------------------ */

/* 0x416ef5: the eight symmetric points of a circle */
static void plot8(int cx, int cy, int x, int y, uint8_t c)
{
    GfxSurface *s = g_win->surface;
    const int px[8] = {cx + x, cx - x, cx - x, cx + x, cx + y, cx - y, cx - y, cx + y};
    const int py[8] = {cy + y, cy + y, cy - y, cy - y, cy + x, cy + x, cy - x, cy - x};
    for (int i = 0; i < 8; i++)
        if (px[i] >= 0 && px[i] < s->width && py[i] >= 0 && py[i] < s->height)
            s->pixels[(size_t)py[i] * (size_t)s->stride + (size_t)px[i]] = c;
}

/* 0x416e58: circle outline (its own midpoint variant) */
static void circle(int cx, int cy, int r, uint8_t c)
{
    int x = 0, y = r, d = 1 - r;
    plot8(cx, cy, 0, r, c);
    while (x < y) {
        int t = x;
        if (-1 < d) {
            t = x - y;
            y--;
        }
        d += t * 2 + 5;
        x++;
        plot8(cx, cy, x, y, c);
    }
}

/* 0x416e13: polar offset at `deg` degrees and radius r */
static void polar(int *dx, int *dy, unsigned deg, int r)
{
    if (deg >= g_tab_len) {
        *dx = *dy = 0;
        return;
    }
    *dx = (g_cos_tab[deg] * r) / 1000000;
    *dy = (g_sin_tab[deg] * r) / 1000000;
}

#define PIE_PTS 100 /* 800 bytes of {x, y} in the original */

/* 0x417017: line from (x0, y0) to (x1, y1) (both ends one pixel longer),
 * recording the drawn points; returns how many */
static int line_record(int x0, int x1, int y0, int y1, uint8_t c, int pts[PIE_PTS][2])
{
    GfxSurface *s = g_win->surface;
    int dx = x1 - x0, dy = y1 - y0;
    dx += x0 < x1 ? 1 : x1 < x0 ? -1 : 0;
    dy += y0 < y1 ? 1 : y1 < y0 ? -1 : 0;
    int sx = dx > 0 ? 1 : dx == 0 ? 0 : -1, sy = dy > 0 ? 1 : dy == 0 ? 0 : -1;
    int ax = abs(dx), ay = abs(dy), n = ax < ay ? ay : ax;
    int ex = 0, ey = 0, count = 0;
    for (int i = 0; i <= n; i++) {
        if (y0 >= 0 && y0 < s->height && x0 >= 0 && x0 < s->width) {
            s->pixels[(size_t)y0 * (size_t)s->stride + (size_t)x0] = c;
            if (count < PIE_PTS) {
                pts[count][0] = x0;
                pts[count][1] = y0;
                count++;
            }
        }
        ex += ax;
        ey += ay;
        if (n < ex)
            ex -= n, x0 += sx;
        if (n < ey)
            ey -= n, y0 += sy;
    }
    return count;
}

/* 0x416db1: a radius at `deg` degrees */
static int radius_line(int cx, int cy, unsigned deg, int r, uint8_t c, int pts[PIE_PTS][2])
{
    int dx, dy;
    polar(&dx, &dy, deg, r);
    return line_record(cx, cx + dx, cy, cy - dy, c, pts);
}

/* 0x416d39: joins point i of two recorded lines, for the first n points */
static void join_lines(int a[PIE_PTS][2], unsigned na, int b[PIE_PTS][2], unsigned nb, uint8_t c)
{
    unsigned n = na <= nb ? na : nb;
    for (unsigned i = 0; i < n && i < PIE_PTS; i++)
        gfx_line(g_win->surface, a[i][0], b[i][0], a[i][1], b[i][1], c);
}

/* 0x416ad8: scanline flood fill of colour `from` with `to` from (x, y) */
static void flood_fill(int x, int y, uint8_t from, uint8_t to)
{
    GfxSurface *s = g_win->surface;
    int16_t sx[500], sy[500];
    int n = 1;
    sx[0] = (int16_t)x, sy[0] = (int16_t)y;
#define PX(xx, yy) s->pixels[(size_t)(yy) * (size_t)s->stride + (size_t)(xx)]
    while (n != 0) {
        n--;
        int cx = sx[n], cy = sy[n];
        if (cy < 0 || cy >= s->height || cx < 0 || cx >= s->width || PX(cx, cy) != from)
            continue;
        while (cx > 0 && PX(cx - 1, cy) == from)
            cx--;
        int above = -1, below = -1;
        for (; cx < s->width && PX(cx, cy) == from; cx++) {
            PX(cx, cy) = to;
            int up = cy > 0 ? PX(cx, cy - 1) : -1;
            if (above != from && up == from && n < 500) {
                sx[n] = (int16_t)cx, sy[n] = (int16_t)(cy - 1);
                n++;
            }
            above = up;
            int dn = cy + 1 < s->height ? PX(cx, cy + 1) : -1;
            if (below != from && dn == from && n < 500) {
                sx[n] = (int16_t)cx, sy[n] = (int16_t)(cy + 1);
                n++;
            }
            below = dn;
        }
    }
#undef PX
}

/* 0x4166c5: a pie of n slices (degrees in deg[], counterclockwise from 0)
 * in colours col[] at (cx, cy) with radius r; `ring` outlines it while
 * filling and is erased afterwards */
static void pie(int cx, int cy, int r, uint8_t ring, int n, const uint32_t *deg, const uint8_t *col)
{
    static int lines[2][PIE_PTS][2], first_line[PIE_PTS][2];
    unsigned cnt[2] = {0, 1};
    uint32_t start = 0;
    unsigned first_n = 0;
    int k = 1, have_first = 0;
    circle(cx, cy, r, ring);
    memset(lines, 0, sizeof lines);
    memset(first_line, 0, sizeof first_line);
    int i;
    for (i = 0; i < n - 1; i++) {
        uint32_t end = deg[i] + start - 1;
        if (deg[i] != 0) {
            if (!have_first) {
                first_n = (unsigned)radius_line(cx, cy, 0, r, col[i], first_line);
                memcpy(lines[k], first_line, sizeof first_line);
                cnt[k] = first_n;
                k ^= 1;
                have_first = 1;
            }
            cnt[k] = (unsigned)radius_line(cx, cy, end, r, col[i], lines[k]);
            k ^= 1;
            if (deg[i] < 10) {
                for (uint32_t a = start; a <= end; a++) {
                    cnt[k] = (unsigned)radius_line(cx, cy, a, r, col[i], lines[k]);
                    k ^= 1;
                }
            } else {
                int px, py;
                polar(&px, &py, end, 0x2e);
                px += cx;
                py = cy - py;
                if (end < 0x5b)
                    px += 2, py += 2;
                else if (end < 0xb5)
                    px += 2, py -= 2;
                else if (end < 0x10f)
                    px -= 2, py -= 2;
                else
                    px -= 2, py += 2;
                flood_fill(px, py, BG, col[i]);
                if (deg[i] < 0x14 && 1 < deg[i])
                    join_lines(lines[0], cnt[0] >> 1, lines[1], cnt[1] >> 1, col[i]);
            }
        }
        start += deg[i];
    }
    if (deg[i] != 0) {
        flood_fill(cx + 0x30, cy + 1, BG, col[i]);
        if (deg[i] < 0x14 && 1 < deg[i])
            join_lines(lines[k ^ 1], cnt[k ^ 1] >> 1, first_line, first_n >> 1, col[i]);
    }
    circle(cx, cy, r, BG);
}

/* 0x415d87 graph_tenet_draw */
static void graph_tenet(void)
{
    static const uint8_t fate_col[4] = {0xc, 0x7b, 0x58, 0xb}, pair_col[2] = {0xc, 0x7b};
    const GraphSummary *p = NULL;
    int n = 0;
    if (button_pressed(GV_ID_HEAVEN))
        p = &g_state.summary[0], n = 1;
    if (button_pressed(GV_ID_HELL)) {
        if (!p)
            p = &g_state.summary[1];
        n++;
    }
    uint32_t suma = 0, alf = 0, haha = 0, hoho = 0, opra = 0, ocra = 0, total = 0;
    for (int r = 0; r < n; r++) {
        const uint32_t *t = p->unk_000;
        suma += t[0] + t[1] + t[2] + t[3] + t[4] + t[5] + t[6] + t[7];
        alf += t[0] + t[1] + t[2] + t[3] + t[8] + t[9] + t[10] + t[11];
        haha += t[0] + t[4] + t[8] + t[12];
        hoho += t[1] + t[5] + t[9] + t[13];
        opra += t[2] + t[6] + t[10] + t[14];
        ocra += t[3] + t[7] + t[11] + t[15];
        for (int i = 0; i < 16; i++)
            total += t[i];
        p = &g_state.summary[1];
    }
    graph_title("Tenet Graph");
    if (total == 0) {
        memset(g_pct, 0, sizeof g_pct);
        return;
    }
    graph_clear();
    char buf[40];
    GfxRect r = {0x18, 0x7b, 0x88, 0x91};
    g_pct[2] = (uint8_t)mul_div_clamp(opra, total, 100);
    g_pct[3] = (uint8_t)mul_div_clamp(ocra, total, 100);
    g_pct[0] = (uint8_t)mul_div_clamp(haha, total, 100);
    unsigned sum = (unsigned)g_pct[2] + g_pct[3] + g_pct[0];
    if (sum > 99)
        sum = 100;
    g_pct[1] = (uint8_t)(100 - sum);
    static const char *const fates[4] = {"HAHA:  %ld%%", "HOHO:  %ld%%", "OPRA:  %ld%%", "OCRA:  %ld%%"};
    for (int i = 0; i < 4; i++) {
        font_colors(fate_col[i], fate_col[i]);
        snprintf(buf, sizeof buf, fates[i], (long)g_pct[i]);
        gfx_font_draw_text(g_win->surface, r.left, r.top, &r, &r, buf, GFX_TEXT_CENTER);
        r.top += 10, r.bottom += 10;
    }
    r = (GfxRect){0x71, 0xd4, 0xc3, 0xcc};
    g_pct[4] = (uint8_t)mul_div_clamp(alf, total, 100);
    font_colors(pair_col[0], pair_col[0]);
    snprintf(buf, sizeof buf, "ALF:   %ld%%", (long)g_pct[4]);
    gfx_font_draw_text(g_win->surface, r.left, r.top, &r, &r, buf, GFX_TEXT_CENTER);
    r.top += 10, r.bottom += 10;
    g_pct[5] = (uint8_t)(100 - g_pct[4]);
    font_colors(pair_col[1], pair_col[1]);
    snprintf(buf, sizeof buf, "RALF:  %ld%%", (long)g_pct[5]);
    gfx_font_draw_text(g_win->surface, r.left, r.top, &r, &r, buf, GFX_TEXT_CENTER);
    r = (GfxRect){0xca, 0x12d, 0x88, 0x91};
    g_pct[6] = (uint8_t)mul_div_clamp(suma, total, 100);
    font_colors(pair_col[0], pair_col[0]);
    snprintf(buf, sizeof buf, "SUMA:  %ld%%", (long)g_pct[6]);
    gfx_font_draw_text(g_win->surface, r.left, r.top, &r, &r, buf, GFX_TEXT_CENTER);
    r.top += 10, r.bottom += 10;
    g_pct[7] = (uint8_t)(100 - g_pct[6]);
    font_colors(pair_col[1], pair_col[1]);
    snprintf(buf, sizeof buf, "SUSA:  %ld%%", (long)g_pct[7]);
    gfx_font_draw_text(g_win->surface, r.left, r.top, &r, &r, buf, GFX_TEXT_CENTER);

    uint32_t a[4];
    a[0] = mul_div_clamp(haha, total, 360);
    a[1] = mul_div_clamp(hoho, total, 360);
    a[2] = mul_div_clamp(opra, total, 360);
    unsigned k = 0;
    while (a[1] + a[0] + a[2] > 360) {
        do
            k = (k + 1) % 3;
        while (a[k] == 0);
        a[k]--;
    }
    a[3] = 360 - (a[1] + a[0] + a[2]);
    pie(0x4a, 0x4c, 0x32, 0x58, 4, a, fate_col);
    a[0] = mul_div_clamp(suma, total, 360);
    a[1] = 360 - a[0];
    a[2] = a[3] = 0;
    pie(0xfc, 0x4c, 0x32, 0x58, 2, a, pair_col);
    a[0] = mul_div_clamp(alf, total, 360);
    a[1] = 360 - a[0];
    a[2] = a[3] = 0;
    pie(0xa3, 0x87, 0x32, 0x58, 2, a, pair_col);
    g_drawn = 1;
}

/* ---- graphview_draw ------------------------------------------------------------ */

/* 0x4159ba */
void graphview_draw(unsigned id, int value, bool draw)
{
    if (!g_win)
        return;
    if (id > 100 && id < 0x6c)
        g_sin_on[g_mode * 7 + (id - GV_ID_SIN)] = value;
    else if (id > 0x6c && id < 0x75)
        g_tenet_on[g_mode][id - GV_ID_TENET] = value;
    else if (id == GV_ID_ALL_TENETS)
        for (int i = 0; i < 9; i++)
            g_tenet_on[g_mode][i] = value;
    else if (id == GV_ID_HELL)
        g_realm_on[g_mode][1] = value;
    else if (id == GV_ID_HEAVEN)
        g_realm_on[g_mode][0] = value;
    /* (the original also derives a [mode][4][2][2] table of tenet
     * combinations at 0x639810 here, which nothing reads) */
    graph_clear();
    font_colors(0xc, BG);
    gfx_font_select(FONT);
    gfx_font_draw_text(g_win->surface, 0x87, 100, NULL, NULL, "Nothing to graph.", GFX_TEXT_CENTER);
    g_drawn = 0;
    if (draw) {
        switch (g_mode) {
        case GRAPH_TENET:
            graph_tenet();
            break;
        case GRAPH_POPULATION: {
            int32_t mx = graph_population(0);
            if (mx != 0)
                graph_population(mx);
            break;
        }
        case GRAPH_STRUCTURES:
            graph_structures();
            break;
        case GRAPH_HISTORY:
            graph_history();
            break;
        case GRAPH_LABOR:
            graph_labor();
            break;
        case GRAPH_BELIEF:
            graph_belief();
            break;
        case GRAPH_BUDGET:
            graph_budget();
            break;
        }
    }
    GfxRect r = {0xe, 0x138, 0x10, 0xd7};
    wm_redraw(g_win, &r);
}

void graphview_refresh(void) { graphview_draw(0, 0, true); }

/* ---- help ------------------------------------------------------------------------- */

/* 0x413f2e: the History Graph's series toggles */
static void series_help(int i, int x, int y)
{
    static const char *const t[10] = {
        "This button toggles HEAVEN POPULATION in and out of the History Graph.",
        "This button toggles HELL POPULATION in and out of the History Graph.",
        "This button toggles TOTAL PENNIES FROM HEAVEN in and out of the History Graph.",
        "This button toggles SOUL RATE in and out of this graph.  The SOUL RATE is the number of Pennies you get "
        "for every SOUL you process.",
        "This button toggles PENNY INFLOW in and out of the History Graph.",
        "This button toggles PENNY OUTFLOW in and out of the History Graph.",
        "This button toggles SOUL LOSSES in and out of the History Graph.",
        "This button toggles AVERAGE EFFICIENCY in and out of the History Graph.",
        "This button toggles the TOTAL NUMBER OF FATE STRUCTURES in and out of the History Graph.",
        "This button toggles your Afterlife's OVERALL SPIFFINESS in and out of the History Graph."};
    if (i >= 0 && i < 10)
        help(x, y, 0x78, t[i]);
}

/* 0x40ec50 graphview_help: right click on a button */
static void graphview_help(WmWindow *c)
{
    static const char *const sins[8] = {
        "This button toggles the presence of CONTENTMENT and ENVY in and out of this graph.",
        "This button toggles the presence of CHARITY and AVARICE in and out of this graph.",
        "This button toggles the presence of TEMPERANCE and GLUTTONY in and out of this graph.",
        "This button toggles the presence of DILIGENCE and SLOTH in and out of this graph.",
        "This button toggles the presence of CHASTITY and LUST in and out of this graph.",
        "This button toggles the presence of PEACEFULNESS and WRATH in and out of this graph.",
        "This button toggles the presence of HUMILITY and PRIDE in and out of this graph.",
        "This button toggles the presence of ALL VIRTUES and/or SINS in and out of this graph."};
    static const char *const tenets[11] = {
        "This button toggles HAHAists in and out of this graph.  HAHAists believe that SOULS go to Heaven AND Hell.",
        "This button toggles HOHOists in and out of this graph.  HOHOists believe that SOULS go to Heaven OR Hell.",
        "This button toggles OPRAists in and out of this graph.  OPRAists believe that all SOULs go to Hell.",
        "This button toggles OCRAists in and out of this graph.  OCRAists believe that all SOULs go to Heaven.",
        "This button toggles ALFists in and out of this graph.  ALFists DO NOT believe in REINCARNATION.",
        "This button toggles RALFists in and out of this graph.  RALFists DO believe in REINCARNATION.",
        "This button toggles SUMAists in and out of this graph.  SUMAists believe that SOULS have multiple "
        "Afterlife experiences",
        "This button toggles SUSAists in and out of this graph.  SUSAists believe that SOULS only experience one "
        "Afterlife",
        "This button toggles ALL the TENETS OF BELIEF into this graph.",
        "This button toggles HELL in and out of this graph.",
        "This button toggles HEAVEN in and out of this graph."};
    static const char *const modes[7] = {
        "This button puts you into TENET GRAPH mode, which can be used to check the TENET levels of all SOULS.  "
        "TENETS are the building blocks upon which every belief system is based.",
        "This button puts the Graphview into POPULATION GRAPH mode.",
        "This button puts the Graphview into STRUCTURES GRAPH mode.  You can use it to watch the population levels "
        "of Gates, Karma Stations, Topias, Ports, and Training Centers.",
        "This button puts the Graphview into HISTORY GRAPH mode, which you can use to track your progress as a "
        "demiurge.",
        "This button puts you into LABOR GRAPH mode, which can be used to check up on your workforce.",
        "This button puts you into BELIEF GRAPH mode, which can be used to check the BELIEF SYSTEM levels of all "
        "SOULS.",
        "This button puts you into BUDGET GRAPH mode, which shows where your cash goes."};
    static const char *const scales[3] = {
        "This button changes the focus of the History Graph to the most recent 25 YEARS.",
        "This button changes the focus of the History Graph to the most recent 500 YEARS.",
        "This button changes the focus of the History Graph to the most recent 10000 YEARS."};
    unsigned id = (unsigned)wm_get_data(c, 0);
    int x = ((c->rect.right - c->rect.left) >> 1) + g_win->rect.left + c->rect.left;
    int y = ((c->rect.bottom - c->rect.top) >> 1) + g_win->rect.top + c->rect.top;
    if (id == 1)
        help(x, y, 0x78,
             wm_is_minimized(c->parent) ? "This button MAXIMIZES the window" : "This button MINIMIZES the window");
    else if (id >= GV_ID_SIN && id <= GV_ID_ALL_SINS)
        help(x, y, 0x78, sins[id - GV_ID_SIN]);
    else if (id >= GV_ID_TENET && id <= GV_ID_HEAVEN)
        help(x, y, 0x78, tenets[id - GV_ID_TENET]);
    else if (id >= GV_ID_MODE && id < GV_ID_MODE + 7)
        help(x, y, 0x78, modes[id - GV_ID_MODE]);
    else if (id >= GV_ID_SCALE && id < GV_ID_SCALE + 3)
        help(x, y, 0x78, scales[id - GV_ID_SCALE]);
    else if (id == 0x82)
        series_help(1, x, y);
    else if (id == 0x83)
        series_help(0, x, y);
    else if (id >= 0x84 && id <= 0x8b)
        series_help((int)id - 0x82, x, y);
    else if (id == GV_ID_CLOSE)
        help(x, y, 0x78, "This button CLOSES the Graphview.");
}

/* 0x41135b: (px, py) is within r of (cx, cy) along both axes' chords */
static bool in_circle(int cx, int cy, int r, int px, int py)
{
    int dx = px - cx, dy = py - cy;
    return !(r * r - dy * dy < dx * dx || r * r - dx * dx < dy * dy);
}

/* 0x4113ca: right click in the graph area (window coordinates) */
static void graph_hover_help(unsigned x, unsigned y)
{
    char buf[700];
    if (!g_drawn)
        return;
    int sx = g_win->rect.left + (int)x, sy = g_win->rect.top + (int)y;
    GfxRect area = {0xe, 0x138, 0x10, 0xd7};
    if (!gfx_rect_contains(&area, (int)x, (int)y)) {
        if (g_mode == GRAPH_HISTORY) {
            GfxRect p = {0x10, 0x127, 0xde, 0x12d};
            if (gfx_rect_contains(&p, (int)x, (int)y)) {
                uint8_t i = (uint8_t)((y - 0xde) / 10);
                if (i > 4)
                    i = 5;
                if (x > 0x9e)
                    i += 6;
                if (i < 10)
                    series_help(i == 0 ? 1 : i == 1 ? 0 : i, sx, sy);
            }
        }
        return;
    }
    buf[0] = 0;
    switch (g_mode) {
    case GRAPH_TENET:
        if (in_circle(0x4a, 0x4c, 0x32, (int)x, (int)y)) {
            static const char *const fate[4] = {"HAHA", "HOHO", "OPRA", "OCRA"};
            static const char *const dest[4] = {"Heaven AND Hell", "Heaven OR Hell", "Hell", "Heaven"};
            uint8_t best = 1;
            for (uint8_t i = 1; i < 5; i++)
                if (g_pct[best - 1] < g_pct[i - 1]) /* 0x651c6f[i]: the fates are 1-based */
                    best = i;
            snprintf(buf, sizeof buf,
                     "This graph shows the percentage of HAHA, HOHO, OPRA and OCRA SOULS.  Currently, most SOULS "
                     "are %sists, who believe that all SOULS go to %s.",
                     fate[best - 1], dest[best - 1]);
        } else if (in_circle(0xfc, 0x4c, 0x32, (int)x, (int)y)) {
            bool multi = g_pct[7] < g_pct[6];
            snprintf(buf, sizeof buf,
                     "This graph shows the percentage of SUMA and SUSA SOULS.  Currently, most SOULS are SU%cAists, "
                     "who believe in %s afterlife experience%s.",
                     multi ? 'M' : 'S', multi ? "multiple" : "a single", multi ? "s" : "");
        } else if (in_circle(0xa3, 0x87, 0x32, (int)x, (int)y)) {
            bool ralf = g_pct[5] < g_pct[4] ? false : true;
            snprintf(buf, sizeof buf,
                     "This graph shows the percentage of ALF and RALF SOULS.  Currently, most souls are %sALFists, "
                     "who DO%s believe in Reincarnation.",
                     ralf ? "R" : "", ralf ? "" : " NOT");
        } else
            return;
        help(sx, sy, 0x78, buf);
        break;
    case GRAPH_POPULATION: {
        int row_h, two;
        unsigned h;
        if (g_realm_on[g_mode][0] && g_realm_on[g_mode][1])
            row_h = 0xb, h = 0x9a, two = 1;
        else if (!g_realm_on[g_mode][0] && !g_realm_on[g_mode][1])
            return;
        else
            row_h = 0x17, h = 0xa1, two = 0;
        unsigned top = 0xd7 - (uint16_t)(h - 1);
        if (y < top) {
            if (x > 0xec && y >= top - 10)
                help(sx, sy, 0x78,
                     "This is the SCALE to which this graph is drawn to.  The number is the total capacity of "
                     "largest bar visible.");
            break;
        }
        unsigned row = (y - top) / (unsigned)row_h;
        uint8_t k = 0;
        int s;
        for (s = 0; s < 7; s++)
            if (g_sin_on[7 + s]) {
                if ((int)(row & 0xff) >> two == k)
                    break;
                k++;
            }
        if (s >= 7)
            break;
        if (x < 0x54) {
            snprintf(buf, sizeof buf,
                     "This graph represents %s / %s.\nThe dark color represents the total PERMANENT population, "
                     "and the light color represents the total TEMPORARY population.  The black area inside the "
                     "bar, if any, represents the unused capacity for this virtue/sin.If your unused capacity is "
                     "being eaten away by a grey bar, you've got FROZEN or ABANDONED buildings.",
                     virtue_names[s], sin_names[s]);
            help(sx, sy, 0x118, buf);
            break;
        }
        int heaven = two ? (row & 1) == 0 : g_realm_on[g_mode][0] != 0;
        const GraphSummary *S = &g_state.summary[heaven ? 0 : 1];
        uint32_t perm = S->unk_068[s] + S->unk_084[s];
        uint32_t temp = S->unk_0a0[s] + S->unk_0bc[s];
        uint32_t cap = S->unk_0d8[s], tot = temp + perm, pct;
        if (cap == 0)
            break;
        if (tot < 10000001)
            pct = tot * 100 / cap;
        else if (cap < 100)
            pct = 0;
        else
            pct = tot / (cap / 100);
        if (pct > 99)
            pct = 100;
        char a[FMT_NUM_LEN], b[FMT_NUM_LEN], c[FMT_NUM_LEN];
        const char *name = heaven ? virtue_names[s] : sin_names[s];
        snprintf(buf, sizeof buf,
                 "Totals for %s:\n    Permanent SOULS:   %s\n    Temporary SOULS:   %s\n    Maximum Capacity:  %s\n"
                 "    %s is %lu%% full",
                 name, fmt_thousands(a, perm), fmt_thousands(b, temp), fmt_thousands(c, cap), name, (unsigned long)pct);
        if (pct > 0x54) {
            size_t l = strlen(buf);
            snprintf(buf + l, sizeof buf - l, "\nYOU MAY WISH TO ZONE MORE %s!", name);
        }
        help(sx, sy, 200, buf);
        break;
    }
    case GRAPH_STRUCTURES: {
        static const char *const names[5] = {"GATES", "KARMA STATIONS", "TOPIAS", "PORTS", "TRAINING CENTERS"};
        static const char *const about[5] = {
            "GATES are where SOULS enter the afterlife.  They are crucial for a realm to function.  Without them, "
            "SOULS cannot enter the afterlife and will be lost.",
            "KARMA STATIONS transport reincarnating SOULS back to the planet.  They are required for SOULS to "
            "reincarnate.",
            "TOPIAS house your workforce and reduce maintenance costs by eliminating their commute time.  "
            "Otherwise, commuting Angels and Demons demand higher compensation, like most people.",
            "PORTS allow SOULS to cross rivers by transporting them from one PORT to another.  Hence, building only "
            "one PORT is of no value.",
            "TRAINING CENTERS train SOULS to become Angels or Demons, thereby increasing your local workforce.  "
            "Local workers demand less compensation than imported ones."};
        const unsigned w = 0x14;
        if (y < 0x2e) {
            if (x > 0xd && x < 0x138) {
                unsigned i = (x - 0xe) / 0x3c;
                if (i < 5)
                    help(sx, sy, 0x78, about[i]);
            }
            break;
        }
        if (!(x > 0xd && x < 0x139))
            break;
        unsigned i = (x - 0xe) / (w * 3);
        if (!(x - 0xe <= w * 3 * i + w * 2 && i < 5))
            break;
        const GraphSummary *hv = &g_state.summary[0], *hl = &g_state.summary[1];
        char extra[200] = "";
        uint32_t chv = hv->unk_160[i], chl = hl->unk_160[i];
        if (chv == 0 && chl == 0)
            snprintf(extra, sizeof extra, "  You have NO %s!", names[i]);
        else if (chl == 0)
            snprintf(extra, sizeof extra, "  You have NO %s in Hell.", names[i]);
        else if (chv == 0)
            snprintf(extra, sizeof extra, "  You have NO %s in Heaven.", names[i]);
        snprintf(buf, sizeof buf, "This graph shows the population levels of all %s.", names[i]);
        strncat(buf, extra, sizeof buf - strlen(buf) - 1);
        uint32_t phv = hv->unk_188[i], phl = hl->unk_188[i];
        uint8_t fhv, fhl;
        if (phv < 10000001)
            fhv = (uint8_t)(phv * 100 / (chv < 2 ? 1 : chv));
        else
            fhv = (uint8_t)(phv / (chv / 100 < 2 ? 1 : chv / 100));
        if (phl < 10000001)
            fhl = (uint8_t)(phl * 100 / (chl < 2 ? 1 : chl));
        else
            fhl = (uint8_t)(phl / (chl / 100 < 2 ? 1 : chl / 100));
        if (fhv > 0x4f || fhl > 0x4f) {
            snprintf(extra, sizeof extra, "  %s are getting very full! You may wish to BUILD AND/OR UPGRADE some %s.",
                     names[i], names[i]);
            strncat(buf, extra, sizeof buf - strlen(buf) - 1);
        }
        help(sx, sy, 0x78, buf);
        break;
    }
    case GRAPH_HISTORY:
        help(sx, sy, 0x78, "This is the HISTORY GRAPH.");
        break;
    case GRAPH_LABOR:
        help(sx, sy, 0x78,
             "This is the LABOR GRAPH.  You use it to get all kinds of interesting information about your "
             "workforce.");
        break;
    case GRAPH_BELIEF:
        if (y > 0x27 && y < 0xd8) {
            uint8_t t = (uint8_t)((y - 0x28) / 0xb);
            if (t > 15)
                break;
            uint32_t n = 0;
            bool hv = false, hl = false;
            if (button_pressed(GV_ID_HEAVEN))
                n = g_state.summary[0].unk_000[t], hv = true;
            if (button_pressed(GV_ID_HELL))
                n += g_state.summary[1].unk_000[t], hl = true;
            const char *where = hv && hl ? "Heaven and Hell." : hv ? "Heaven." : "Hell.";
            if (n == 0)
                snprintf(buf, sizeof buf, "There are NO %sists in%s", belief_names[t], where);
            else {
                char a[FMT_NUM_LEN];
                snprintf(buf, sizeof buf, "There are %s %sists in %s", fmt_thousands(a, n), belief_names[t], where);
            }
            help(sx, sy, 0x78, buf);
        }
        break;
    case GRAPH_BUDGET:
        help(sx, sy, 0x78,
             "This is the BUDGET GRAPH.  You use it to get detailed information on where your pennies are going.");
        break;
    }
}

/* ---- the window ------------------------------------------------------------------- */

static void menu_toggle(void)
{
    if (views_hooks.menu_command)
        views_hooks.menu_command(0x19);
    else if (g_win)
        wm_show(g_win, false);
}

/* graphview_proc's handling of a button press (message 0x10) */
static intptr_t graphview_press(unsigned id, intptr_t raw)
{
    int on = raw != 0;
    if (id == GV_ID_ALL_TENETS) {
        if (on)
            for (unsigned k = GV_ID_TENET; k < GV_ID_ALL_TENETS; k++) {
                button_set(k, true);
                graphview_draw(k, 1, true);
            }
    } else if (id == GV_ID_ALL_SINS) {
        for (unsigned k = GV_ID_SIN; k < GV_ID_ALL_SINS; k++) {
            button_set(k, raw != 0);
            g_sin_on[g_mode * 7 + (k - GV_ID_SIN)] = (int)raw;
        }
        graphview_draw(GV_ID_ALL_SINS, (int)raw, true);
    } else if (id == GV_ID_CLOSE || id == 0x3ea) {
        menu_toggle();
    } else if (id == GV_ID_HELL || id == GV_ID_HEAVEN) {
        graphview_draw(id, on, true);
    } else if (id >= GV_ID_MODE && id < GV_ID_MODE + GRAPH_MODES) {
        int m = (int)(id - GV_ID_MODE);
        bool change = on && g_mode != m;
        switch (m) {
        case GRAPH_TENET:
        case GRAPH_BELIEF:
            if (change) {
                panel_reset();
                button_set(GV_ID_MODE + g_mode, false);
                button_vis(GV_ID_HELL, true);
                button_vis(GV_ID_HEAVEN, true);
                button_set(GV_ID_HELL, true);
                button_set(GV_ID_HEAVEN, true);
                g_mode = (uint8_t)m;
                graphview_draw(id, 1, true);
            }
            break;
        case GRAPH_POPULATION:
            if (change) {
                panel_reset();
                button_set(GV_ID_MODE + g_mode, false);
                g_mode = GRAPH_POPULATION;
                button_vis(GV_ID_HELL, true);
                button_vis(GV_ID_HEAVEN, true);
                button_set(GV_ID_HELL, g_realm_on[g_mode][1] != 0);
                button_set(GV_ID_HEAVEN, g_realm_on[g_mode][0] != 0);
                for (unsigned k = 0; k < 7; k++) {
                    button_vis(GV_ID_SIN + k, true);
                    button_set(GV_ID_SIN + k, g_sin_on[7 + k] != 0);
                }
                button_vis(GV_ID_ALL_SINS, true);
                button_set(GV_ID_ALL_SINS, button_pressed(GV_ID_ALL_SINS));
            }
            graphview_draw(id, on, true);
            break;
        case GRAPH_STRUCTURES:
            if (change) {
                button_set(GV_ID_MODE + g_mode, false);
                g_mode = GRAPH_STRUCTURES;
                panel_reset();
            }
            graphview_draw(id, on, true);
            break;
        case GRAPH_HISTORY:
            if (change) {
                panel_reset();
                button_set(GV_ID_MODE + g_mode, false);
                g_mode = GRAPH_HISTORY;
                for (unsigned k = 0; k < 3; k++) {
                    if (g_btn[GV_ID_SCALE + k]) {
                        ctl_button_show(g_btn[GV_ID_SCALE + k], true);
                        ctl_button_enable(g_btn[GV_ID_SCALE + k], true);
                    }
                    button_set(GV_ID_SCALE + k, g_scale == k);
                }
            }
            graphview_draw(id, on, true);
            history_panel();
            break;
        default: /* labor, budget */
            if (change) {
                panel_reset();
                button_set(GV_ID_MODE + g_mode, false);
                g_mode = (uint8_t)m;
                graphview_draw(id, 1, true);
            }
            break;
        }
    } else if (id >= GV_ID_SCALE && id < GV_ID_SCALE + 3) {
        if (g_scale != id - GV_ID_SCALE) {
            button_set(GV_ID_SCALE + g_scale, false);
            g_scale = (uint8_t)(id - GV_ID_SCALE);
        }
        graphview_draw(id, on, true);
    } else if (id >= GV_ID_SERIES && id < GV_ID_SERIES + GRAPH_SERIES) {
        g_series_on[id - GV_ID_SERIES] = on;
        graphview_draw(id, on, true);
    } else if (id == 0x8d) {
        return 0;
    } else if (id > 100) {
        if (id < 0x6d) {
            g_sin_on[g_mode * 7 + (id - GV_ID_SIN)] = on;
            int all = 1;
            for (unsigned k = GV_ID_SIN; k < 0x6d; k++)
                all &= g_sin_on[g_mode * 7 + (k - GV_ID_SIN)];
            button_set(GV_ID_ALL_SINS, all != 0);
        } else if (id < 0x77)
            button_set(GV_ID_ALL_TENETS, false);
        graphview_draw(id, on, true);
    }
    if (g_win)
        wm_raise(g_win);
    return 1;
}

/* 0x40e11a graphview_proc */
static intptr_t graphview_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_MOUSEMOVE:
        if (g_dragging) {
            GfxRect r = w->rect;
            rect_move_to(&r, (int)a - g_grab_x, (int)b - g_grab_y);
            wm_local_to_screen(w, &r);
            if (w->parent) /* (the original passes screen coordinates; its parent is at 0, 0) */
                wm_screen_to_local(w->parent, &r);
            wm_set_rect(w, &r);
        }
        break;
    case WM_MSG_LBUTTONDOWN:
        wm_set_capture(w);
        g_grab_x = (int)a;
        g_grab_y = (int)b;
        g_dragging = 1;
        views_set_paused(1);
        break;
    case WM_MSG_LBUTTONUP:
        wm_release_capture();
        g_dragging = 0;
        views_set_paused(0);
        break;
    case WM_MSG_CTL_PRESS:
        if (w->parent == g_win && graphview_press((unsigned)a, b) == 0)
            return 0;
        break;
    case WM_MSG_HOVER:
        if (wm_get_mode() == 0)
            break;
        /* fall through */
    case WM_MSG_RBUTTONDOWN:
        if (w == g_win)
            graph_hover_help((unsigned)a, (unsigned)b);
        break;
    case WM_MSG_CTL_HOVER:
        if (wm_get_mode() == 0)
            break;
        /* fall through */
    case WM_MSG_CTL_RDOWN:
        if (w->parent == g_win)
            graphview_help(w);
        break;
    case WM_MSG_CAPTURE_LOST:
        g_dragging = 0;
        views_set_paused(0);
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

static int32_t *load_table(uint32_t id, uint32_t *n)
{
    PlatFile *f = rc_open_resource(id);
    if (!f)
        return NULL;
    long len = rc_resource_size(id);
    uint8_t *raw = len >= 4 ? malloc((size_t)len) : NULL;
    int32_t *t = NULL;
    if (raw && plat_file_read(f, raw, 1, (size_t)len) == (size_t)len) {
        *n = (uint32_t)len / 4;
        t = malloc((size_t)*n * sizeof *t);
        for (uint32_t i = 0; t && i < *n; i++)
            t[i] = (int32_t)((uint32_t)raw[i * 4] | (uint32_t)raw[i * 4 + 1] << 8 | (uint32_t)raw[i * 4 + 2] << 16 |
                             (uint32_t)raw[i * 4 + 3] << 24);
    }
    free(raw);
    plat_file_close(f);
    return t;
}

/* Button placement of graphview_create: id -> position, bitmap, style. */
static bool button_spec(unsigned id, int *x, int *y, uint32_t *bmp, uint32_t *style)
{
    *style = CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE;
    if (id < GV_ID_ALL_SINS) {
        /* the pair index comes from 0x593260, which create set to k */
        static const int16_t pos[7][2] = {{0xc2, 0xea},  {0xc2, 0x102}, {0xda, 0xde}, {0xda, 0xf6},
                                          {0xda, 0x10e}, {0xf2, 0xea},  {0xf2, 0x102}};
        unsigned k = id - GV_ID_SIN;
        *x = pos[k][0], *y = pos[k][1];
        *bmp = k + 0x67;
    } else if (id > GV_ID_ALL_SINS && id < GV_ID_ALL_TENETS) {
        int k = (int)id - GV_ID_TENET;
        *x = (k / 2) * 0x24 + (id > 0x70 ? 0x16 : 0x12);
        *y = (k % 2) * 0x20 + 0xe4;
        *bmp = id + 0x5b;
    } else if (id == GV_ID_ALL_SINS) {
        *x = 0x10a, *y = 0xf6, *bmp = 0x6e;
    } else if (id == GV_ID_ALL_TENETS) {
        *x = 0xa6, *y = 0xf6, *bmp = 0x6e;
    } else if (id == GV_ID_HELL) {
        *x = 0x130, *y = 0x102, *bmp = 0x7d;
    } else if (id == GV_ID_HEAVEN) {
        *x = 0x130, *y = 0xea, *bmp = 0x7e;
    } else if (id >= GV_ID_MODE && id < GV_ID_SCALE) {
        *style = CTL_STYLE_BITMAP | CTL_STYLE_STICKY;
        *x = 0x13b, *y = (int)(id - GV_ID_MODE) * 0x1c + 0x18, *bmp = id + 0x60;
    } else if (id >= GV_ID_SCALE && id < GV_ID_SERIES) {
        *style = CTL_STYLE_BITMAP | CTL_STYLE_STICKY;
        *x = 0x130, *y = 0xde + (int)(id - GV_ID_SCALE) * 0x18, *bmp = 0xd4 + (id - GV_ID_SCALE);
    } else if (id >= GV_ID_SERIES && id < 0x88) {
        *x = 0x14, *y = (int)(id - GV_ID_SERIES) * 10 + 0xdc, *bmp = 0xdf;
    } else if (id >= 0x88 && id < GV_ID_CLOSE) {
        *x = 0x9e, *y = (int)(id - 0x88) * 10 + 0xdc, *bmp = 0xdf;
    } else if (id == GV_ID_CLOSE) {
        *style = CTL_STYLE_BITMAP;
        *x = 0, *y = 0, *bmp = 0x259;
    } else
        return false;
    return true;
}

/* 0x40d75e */
bool graphview_create(void)
{
    if (g_win)
        return true;
    GfxSurface *bg = gfx_load_bmp_resource(0x25b, NULL);
    if (!bg)
        return false;
    GfxRect r = {0, bg->width - 1, 0, bg->height - 1};
    g_win = wm_create_window(views_main_window(), &r, graphview_proc, WM_F_MINIMIZABLE | WM_F_NOFOCUS | WM_F_CENTER, 0);
    if (!g_win) {
        gfx_surface_destroy(bg);
        return false;
    }
    gfx_blit(g_win->surface, 0, 0, bg->width, bg->height, bg, 0, 0);
    gfx_surface_destroy(bg);
    g_sin_tab = load_table(0x14b4, &g_tab_len);
    uint32_t n2 = 0;
    g_cos_tab = load_table(0x14b5, &n2);
    if (!g_sin_tab || !g_cos_tab)
        g_tab_len = 0;
    else if (n2 < g_tab_len)
        g_tab_len = n2;
    g_panel = gfx_load_bmp_resource(0xd3, NULL);
    wm_set_title(g_win, "Graphview");
    for (unsigned id = GV_ID_SIN; id <= GV_ID_CLOSE; id++) {
        int x, y;
        uint32_t bmp, style;
        if (!button_spec(id, &x, &y, &bmp, &style))
            continue;
        GfxSurface *s = gfx_load_bmp_resource(bmp, NULL);
        if (!s)
            continue;
        GfxRect br = {x, s->width + x - 1, y, s->height + y - 1};
        g_btn[id] = ctl_button_create(g_win, &br, id, style, s);
        if (!g_btn[id])
            gfx_surface_destroy(s);
    }
    g_win->close_button = g_btn[GV_ID_CLOSE];
    for (int m = 0; m < GRAPH_MODES; m++) {
        for (int k = 0; k < 7; k++)
            g_sin_on[m * 7 + k] = 1;
        for (int k = 0; k < 9; k++)
            g_tenet_on[m][k] = 1;
        g_realm_on[m][0] = g_realm_on[m][1] = 1;
    }
    g_mode = GRAPH_POPULATION;
    memset(g_series_on, 0, sizeof g_series_on);
    g_series_on[0] = g_series_on[1] = g_series_on[2] = 1;
    panel_reset();
    for (unsigned k = 0; k < 9; k++)
        button_vis(GV_ID_TENET + k, false);
    for (unsigned k = 0; k < 7; k++) {
        button_vis(GV_ID_SIN + k, true);
        button_set(GV_ID_SIN + k, true);
    }
    button_vis(GV_ID_ALL_SINS, true);
    button_set(GV_ID_ALL_SINS, true);
    button_vis(GV_ID_ALL_TENETS, false);
    for (unsigned k = 0; k < 3; k++)
        button_vis(GV_ID_SCALE + k, false);
    for (unsigned m = 0; m < GRAPH_MODES; m++)
        button_set(GV_ID_MODE + m, m == GRAPH_POPULATION);
    button_set(GV_ID_HELL, g_realm_on[g_mode][1] != 0);
    button_set(GV_ID_HEAVEN, g_realm_on[g_mode][0] != 0);
    button_vis(GV_ID_HELL, true);
    button_vis(GV_ID_HEAVEN, true);
    g_scale = 0;
    views_draw_title(g_win);
    wm_redraw(g_win, NULL);
    graphview_draw(GV_ID_MODE + g_mode, 1, true);
    wm_show(g_win, false);
    return true;
}

void graphview_destroy(void)
{
    if (g_win) {
        wm_show(g_win, false);
        wm_destroy(g_win);
    }
    g_win = NULL;
    memset(g_btn, 0, sizeof g_btn);
    gfx_surface_destroy(g_panel);
    g_panel = NULL;
    free(g_sin_tab);
    free(g_cos_tab);
    g_sin_tab = g_cos_tab = NULL;
    g_tab_len = 0;
    g_dragging = 0;
}

WmWindow *graphview_window(void) { return g_win; }
int graphview_mode(void) { return g_mode; }
