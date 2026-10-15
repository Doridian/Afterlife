/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Microview's THINGS TO KNOW page (0x46ffc1, with 0x46fcab and
 * 0x46fd49): a few lines about the structure, red where something is
 * wrong. The texts are read from ALIFE.EXE; the original repeats the
 * shared ones (vibes, road access, ...) per tile set with identical copies,
 * the port reads one copy.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/build.h"
#include "game/realm.h"
#include "game/structure.h"
#include "game/tables.h"
#include "game/util.h"
#include "game/world.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "ui/microview_int.h"

#define FONT 500
#define RED 0xc
#define WHITE 0x1a

typedef struct Page {
    GfxSurface *s;
    int x, y; /* the next line (local_5c, local_60) */
    char buf[128];
} Page;

static const GfxRect clip = {0x12, 0xee, 0x3c, 0xde}; /* 0x638f40 */

static void colors(unsigned c)
{
    unsigned cc[2] = {c, c};
    gfx_font_set_colors(FONT, 1, 2, cc);
}

static void draw(Page *p, int x, int y, const char *s, unsigned flags)
{
    GfxRect a = clip;
    gfx_font_draw_text(p->s, x, y, &a, &a, s, flags);
}

/* a line at the cursor; red if `bad` */
static void line(Page *p, const char *s, bool bad, unsigned flags)
{
    if (bad)
        colors(RED);
    draw(p, p->x, p->y, s, flags);
    if (bad)
        colors(WHITE);
    p->y += 10;
}

static void header(Page *p, const char *text)
{
    draw(p, 0x15, 0x3f, text, GFX_TEXT_NOJUSTIFY);
    p->y += 0x14;
}

static bool hell(void) { return g_world.plane == GS_PLANE_HELL; }

/* map cell of the selected plane, flat index like the original */
static uint32_t cell_flat(int x, int y)
{
    int i = y * GS_MAP_W + x;
    return i >= 0 && i < GS_MAP_CELLS ? g_world.cells[i] : 0;
}

/* 0x46fcab: how full: "unpopulated" .. "completely populated" (or the
 * Gate's traffic words) */
static void fullness(Page *p, uint32_t n, uint32_t cap, const char *const words[6])
{
    uint8_t pct = 0;
    if (n < 10000001u)
        pct = cap ? (uint8_t)(n * 100u / cap) : 0;
    else
        pct = cap / 100 ? (uint8_t)(n / (cap / 100)) : 0;
    unsigned i = (n != 0) + pct / 25u;
    if (i > 5)
        i = 5; /* (the original reads past its six words) */
    draw(p, p->x, p->y, words[i], 0);
    p->y += 10;
}

static const char *const k_populated[6] = {"unpopulated" /* 0x6625b4 */,       "sparsley populated" /* 0x6625d4 */,
                                           "lightly populated" /* 0x6625fc */, "moderately populated" /* 0x662628 */,
                                           "heavily populated" /* 0x662654 */, "completely populated" /* 0x662680 */};

static void employing(Page *p, int x, int y, unsigned set)
{
    char num[FMT_NUM_LEN];
    snprintf(p->buf, sizeof p->buf,
             (g_world.plane == GS_PLANE_HEAVEN ? "employing %s Angels" /* 0x66277c */
                                               : "employing %s Demons" /* 0x662790 */),
             fmt_thousands(num, structure_upkeep(x, y, set)));
    line(p, p->buf, false, 0);
}

static void charged(Page *p, int x, int y)
{
    bool on = sim_scan_mark(x, y) != 0;
    line(p, on ? "charged" /* 0x662924 */ : "NOT charged" /* 0x662918 */, !on, 0);
}

/* "under somewhat Good Vibes" */
static void vibes(Page *p, int x, int y)
{
    static const char *const level[6] = {"no Vibes" /* 0x662938 */,   "somewhat" /* 0x662950 */,
                                         "moderately" /* 0x662968 */, "very" /* 0x66297c */,
                                         "extremely" /* 0x662990 */,  "completely" /* 0x6629a8 */};
    int v = (int8_t)cell_vibes(x, y);
    unsigned l = (unsigned)(abs(v) >> 2) + (v != 0);
    if (l > 5)
        l = 5;
    snprintf(p->buf, sizeof p->buf, "under %s " /* 0x6629b4 */, level[l]);
    if (v != 0) {
        const char *kind = hell() ? (v >= 0 ? "Bad Vibes" /* 0x6629cc */ : "Good Vibes" /* 0x6629c0 */)
                                  : (v >= 0 ? "Good Vibes" /* 0x6629e4 */ : "Bad Vibes" /* 0x6629d8 */);
        strncat(p->buf, kind, sizeof p->buf - strlen(p->buf) - 1);
    }
    line(p, p->buf, v < 0, 0);
}

/* 0x46fd49: a road within `r` (Manhattan) of the structure's footprint */
static bool near_road(int x, int y, int r)
{
    int ox = x, oy = y;
    if (cell_tileset(x, y) == 0) {
        const SimCell *s = sim_cell(x, y);
        ox = s->b[10];
        oy = s->b[11];
    }
    int n = (uint8_t)structure_size(ox, oy);
    int x1 = (uint8_t)(ox + n - 1), y0 = (uint8_t)(oy - (n - 1));
    int i0 = (int8_t)ox - r, i1 = n - 1 + r + (int8_t)ox;
    if (i0 < 1)
        i0 = 0;
    if (i1 > 0x62)
        i1 = 99;
    int j0 = (int8_t)oy - (n - 1) - r, j1 = r + (int8_t)oy;
    if (j0 < 1)
        j0 = 0;
    if (j1 > 0x62)
        j1 = 99;
    for (int i = i0; i <= i1; i++)
        for (int j = j0; j <= j1; j++) {
            if (cell_flat(i, j) >> 27 != 12)
                continue;
            int dx = 0, dy = 0;
            if (i < ox || i > x1) {
                int a = abs(i - ox), b = abs(i - x1);
                dx = b <= a ? b : a;
            }
            if (j < y0 || j > oy) {
                int a = abs(j - y0), b = abs(j - oy);
                dy = b <= a ? b : a;
            }
            if ((uint8_t)dx + (uint8_t)dy <= r)
                return true;
        }
    return false;
}

static void road(Page *p, int x, int y, int r)
{
    bool ok = near_road(x, y, r);
    line(p, ok ? "close enough to a road" /* 0x662a0c */ : "NOT close enough to a road" /* 0x6629f0 */, !ok, 0);
}

static void attacked(Page *p, int x, int y)
{
    if (cell_flat(x, y) & 0x8000u)
        line(p,
             hell() ? "Being attacked by idle Angels!" /* 0x662a24 */ : "Being attacked by idle Demons!" /* 0x662a44 */,
             true, 0);
}

static void shaded(Page *p, int x, int y)
{
    if (cell_test_shade(x, y))
        line(p, g_world.plane == GS_PLANE_HEAVEN ? "Singing the Blues" /* 0x662a64 */ : "Frozen" /* 0x662a78 */, true,
             0);
}

static void guano(Page *p, int x, int y)
{
    if (cell_test_overlay(x, y)) {
        snprintf(p->buf, sizeof p->buf, "covered by B.O.%s. Guano" /* 0x662a88 */,
                 microview_cell_plane() == GS_PLANE_HELL ? "H" /* 0x662a80 */ : "P" /* 0x662a84 */);
        line(p, p->buf, true, 0);
    }
}

static void train_run(Page *p, const char *fmt)
{
    snprintf(p->buf, sizeof p->buf, fmt, (int)g_state.unk_5c6678);
    line(p, p->buf, false, 0);
}

/* 0x4666de mapview_log_bucket */
static unsigned log_bucket(uint32_t v, uint8_t n)
{
    uint32_t c = 0x80000000u;
    unsigned step = 32u / n;
    for (;;) {
        n--;
        if (n == 0 || v >= c)
            return n;
        c >>= step & 31;
    }
}

static uint32_t cap_of(const BuildingSpec *t, size_t n, unsigned i) { return i < n ? t[i].capacity : 0; }
#define CAP(tab, i) cap_of(g_tables.tab, sizeof g_tables.tab / sizeof g_tables.tab[0], (i))

/* Efficiency grades */
static const char *const k_grades[16] = {
    "F-" /* 0x662880 */, "F" /* 0x662888 */,  "F+" /* 0x662890 */, "D-" /* 0x662898 */,
    "D" /* 0x6628a0 */,  "D+" /* 0x6628a8 */, "C-" /* 0x6628b0 */, "C" /* 0x6628b8 */,
    "C+" /* 0x6628c0 */, "B-" /* 0x6628c8 */, "B" /* 0x6628d0 */,  "B+" /* 0x6628d8 */,
    "A-" /* 0x6628e0 */, "A" /* 0x6628e8 */,  "A+" /* 0x6628f0 */, "A++" /* 0x6628f8 */};

static void zone_page(Page *p, int x, int y, unsigned set, unsigned index)
{
    static const char *const balance[8] = {"totally un" /* 0x6626a4 */,      "very poorly " /* 0x6626c0 */,
                                           "poorly " /* 0x6626d8 */,         "slightly un" /* 0x6626ec */,
                                           "moderately " /* 0x662704 */,     "well " /* 0x662718 */,
                                           "extremely well " /* 0x662730 */, "perfectly " /* 0x66274c */};
    static const char *const diverse[2][4] = {
        {"too darn diverse" /* 0x6627b0 */, "uncomfortably diverse" /* 0x6627c4 */,
         "marginally undiverse" /* 0x6627dc */, "spectacularly undiverse" /* 0x6627f4 */},
        {"horribly undiverse" /* 0x66280c */, "slightly undiverse" /* 0x662820 */, "partially diverse" /* 0x662834 */,
         "maximally diverse" /* 0x662848 */}};
    char num[FMT_NUM_LEN];
    uint32_t cap;
    if (hell()) {
        header(p, "This PUNISHMENT is" /* 0x662758 */);
        cap = index < REALM_TILE_TYPES ? g_world.tile_info_hell[index].cost : 0;
    } else {
        header(p, "This REWARD is" /* 0x66276c */);
        cap = index < REALM_TILE_TYPES ? g_world.tile_info_heaven[index].cost : 0;
    }
    fullness(p, realm_building_pop(x, y), cap, k_populated);
    employing(p, x, y, set);
    unsigned mix = (uint8_t)realm_score_mix(x, y);
    snprintf(p->buf, sizeof p->buf, "%sbalanced" /* 0x6627a4 */, balance[mix & 7]);
    line(p, p->buf, mix < 4, 0);
    int nb = realm_score_neighbours(x, y);
    /* (any other value redraws the previous line's text, as the original) */
    if (nb >= 0 && nb <= 3)
        snprintf(p->buf, sizeof p->buf, "%s", diverse[hell() ? 0 : 1][nb]);
    line(p, p->buf, nb < 2, 0);
    snprintf(p->buf, sizeof p->buf, "taking about %s steps to reach" /* 0x66285c */,
             fmt_thousands(num, realm_building_years_per_soul(x, y)));
    line(p, p->buf, false, 0);
    unsigned e = sim_happiness(x, y) & 0xff;
    snprintf(p->buf, sizeof p->buf, "graded %s for Efficiency" /* 0x6628fc */, k_grades[e & 15]);
    line(p, p->buf, e < 7, 0);
    charged(p, x, y);
    vibes(p, x, y);
    road(p, x, y, 3);
    attacked(p, x, y);
    shaded(p, x, y);
    guano(p, x, y);
    int w = index < REALM_TILE_TYPES && g_world.tile_info ? g_world.tile_info[index].unk_13 : 0;
    colors(w < 1 ? RED : WHITE);
    if (hell())
        w = (int8_t)-w;
    const char *worth = w < -4   ? "Putting out very bad vibes"        /* 0x662aa4 */
                        : w < -2 ? "Putting out moderately bad vibes"  /* 0x662ac0 */
                        : w < 0  ? "Putting out slightly bad vibes"    /* 0x662ae4 */
                        : w >= 5 ? "Putting out very good vibes"       /* 0x662b04 */
                        : w >= 3 ? "Putting out moderately good vibes" /* 0x662b20 */
                        : w >= 1 ? "Putting out slightly good vibes"   /* 0x662b44 */
                                 : "Putting out no vibes" /* 0x662b64 */;
    draw(p, p->x, p->y, worth, GFX_TEXT_NOJUSTIFY);
}

/* 0x46ffc1 */
void microview_info_draw(int x, int y, unsigned set, unsigned index)
{
    WmWindow *win = microview_win();
    if (!win)
        return;
    Page pg = {win->surface, 0x15, 0x3f, {0}}, *p = &pg;
    const SimCell *sc = sim_cell(x, y);
    uint32_t s1 = sim_cell_u32(sc, 1), s5 = sim_cell_u32(sc, 5), s9 = sim_cell_u32(sc, 9), sd = sim_cell_u32(sc, 0xd);
    char num[FMT_NUM_LEN];
    colors(WHITE);
    gfx_font_select(FONT);
    switch (set) {
    case 1:
        header(p, index == 0xb ? "This SNOWMAN (er, SNOWPERSON) is!"   /* 0x664244 */
                               : "This BLANK TILE is" /* 0x664268 */); /* snowman / blank tile */
        vibes(p, x, y);
        if (index == 0xb)
            line(p, "like, cool, daddy-o!" /* 0x664340 */, false, GFX_TEXT_NOJUSTIFY);
        break;
    case 2:
        if (index < 0x34) {
            header(p, "This RIVER is" /* 0x663394 */);
            charged(p, x, y);
            vibes(p, x, y);
        } else {
            draw(p, 0x15, 0x3f, "Surf's up!" /* 0x66347c */, GFX_TEXT_NOJUSTIFY);
        }
        break;
    case 3:
        header(p, "This SECRET BUILDING is really cool" /* 0x662e04 */);
        break;
    case 4:
        header(p, "This SPECIAL BUILDING is" /* 0x662ce4 */);
        vibes(p, x, y);
        shaded(p, x, y);
        guano(p, x, y);
        break;
    case 5:
        header(
            p,
            g_world.plane == GS_PLANE_HEAVEN ? "This LOVE DOME is" /* 0x663be4 */ : "This OMNIBOLGE is" /* 0x663bf8 */);
        fullness(p, s5 + s9 + s1, CAP(set5, index), k_populated);
        charged(p, x, y);
        vibes(p, x, y);
        road(p, x, y, 3);
        shaded(p, x, y);
        break;
    case 6:
        draw(p, 0x15, 0x3f, "This GUIDE is" /* 0x663d34 */, GFX_TEXT_NOJUSTIFY);
        break;
    case 7:
        header(p, "This TOPIA BUILDING is" /* 0x662e28 */);
        vibes(p, x, y);
        shaded(p, x, y);
        guano(p, x, y);
        break;
    case 8:
        header(p, "This TRAINING CENTER is" /* 0x6635d4 */);
        fullness(p, s5, CAP(set8, index), k_populated);
        employing(p, x, y, set);
        line(p, sd & 0x80 ? "Inactive" /* 0x663614 */ : "Active" /* 0x663620 */, (sd & 0x80) != 0, GFX_TEXT_NOJUSTIFY);
        vibes(p, x, y);
        road(p, x, y, 3);
        attacked(p, x, y);
        shaded(p, x, y);
        guano(p, x, y);
        break;
    case 9:
        header(p, "This PORT is" /* 0x664094 */);
        employing(p, x, y, set);
        vibes(p, x, y);
        road(p, x, y, 3);
        attacked(p, x, y);
        shaded(p, x, y);
        guano(p, x, y);
        break;
    case 10:
        if (index < 0x10 || index > 0x19) {
            draw(p, 0x15, 0x3f, "This tile is really an UNKNOWN TILE, really!" /* 0x663488 */, GFX_TEXT_NOJUSTIFY);
            break;
        }
        header(p, "This ROCK is" /* 0x6634b8 */);
        {
            unsigned n = 0;
            if (cell_test_overlay(x, y))
                for (unsigned nx = (uint8_t)(x - 1); nx <= (unsigned)x + 1; nx++)
                    for (unsigned ny = (uint8_t)(y - 1); ny <= (unsigned)y + 1; ny++)
                        if (cell_flat((int)nx, (int)ny) >> 27 == 0x1d)
                            n = (n + 1) & 0xff;
            if (n == 0) {
                line(p, "untapped" /* 0x6634e8 */, true, 0);
            } else {
                snprintf(p->buf, sizeof p->buf, "tapped by %d siphon%s" /* 0x6634d0 */, (int)n,
                         (n > 1 ? "s" /* 0x6634c8 */ : "" /* 0x6634cc */));
                line(p, p->buf, false, 0);
            }
        }
        vibes(p, x, y);
        shaded(p, x, y);
        break;
    case 12: {
        static const char *const traffic[4] = {"lightly" /* 0x6631dc */, "moderately" /* 0x6631f0 */,
                                               "heavily" /* 0x663204 */, "very heavily" /* 0x66321c */};
        header(p, "This ROAD is" /* 0x66322c */);
        if (s9 == 0)
            snprintf(p->buf, sizeof p->buf, "%s", "untrafficked" /* 0x66323c */);
        else
            snprintf(p->buf, sizeof p->buf, "%s trafficked" /* 0x66324c */, traffic[log_bucket(s9, 4) & 3]);
        line(p, p->buf, false, 0);
        int digits = 0;
        for (uint32_t v = s9; v != 0; v /= 10)
            digits++;
        int k = digits ? digits - 1 : 0;
        int32_t per = (int32_t)(10000u << (k & 31)) / 0x45;
        snprintf(p->buf, sizeof p->buf, "maintained for %7.4f%c / year" /* 0x66325c */, per / 10000.0, 8);
        line(p, p->buf, false, 0);
        charged(p, x, y);
        vibes(p, x, y);
        shaded(p, x, y);
        guano(p, x, y);
        break;
    }
    case 13: {
        static const char *const traffic[6] = {
            "not trafficked" /* 0x662f54 */,     "barely trafficked" /* 0x662f78 */,
            "lightly trafficked" /* 0x662fa0 */, "moderately trafficked" /* 0x662fcc */,
            "heavily trafficked" /* 0x662ff8 */, "congested" /* 0x663018 */};
        header(p, "This GATE is" /* 0x663024 */);
        fullness(p, s5, CAP(set13, index >> 1), traffic);
        employing(p, x, y, set);
        vibes(p, x, y);
        road(p, x, y, 1);
        attacked(p, x, y);
        shaded(p, x, y);
        guano(p, x, y);
        break;
    }
    case 14:
    case 15:
        zone_page(p, x, y, set, index);
        break;
    case 16:
        header(p, "This KARMA TRACK is" /* 0x663aa0 */);
        train_run(p, "Average Train Run is %d Mega-Cubits" /* 0x663ab4 */);
        employing(p, x, y, set);
        vibes(p, x, y);
        snprintf(p->buf, sizeof p->buf, "holding up %d pieces of track" /* 0x663bc4 */, (int)(s1 & 0xff));
        line(p, p->buf, false, GFX_TEXT_NOJUSTIFY);
        break;
    case 17:
        header(p, "This KARMA STATION is" /* 0x6637a0 */);
        fullness(p, s5, CAP(set17, (s9 >> 8) & 0xff), k_populated);
        employing(p, x, y, set);
        line(p, (s9 & 0xff) ? "fully functioning" /* 0x6637f0 */ : "NOT functioning" /* 0x6637e0 */, (s9 & 0xff) == 0,
             0);
        train_run(p, "Average Train Run is %d Mega-Cubits" /* 0x663804 */);
        vibes(p, x, y);
        road(p, x, y, 3);
        attacked(p, x, y);
        shaded(p, x, y);
        guano(p, x, y);
        break;
    case 18:
        header(p, "This KARMA PORTAL is" /* 0x6639a0 */);
        train_run(p, "Average Train Run is %d Mega-Cubits" /* 0x6639b8 */);
        vibes(p, x, y);
        break;
    case 19:
        header(p, "This BANK is" /* 0x663d44 */);
        employing(p, x, y, set);
        vibes(p, x, y);
        attacked(p, x, y);
        shaded(p, x, y);
        guano(p, x, y);
        break;
    case 29:
        header(p, "This INFINITUM SIPHON is" /* 0x662b7c */);
        employing(p, x, y, set);
        p->buf[0] = 0;
        if (s1 == 0) {
            colors(RED);
            snprintf(p->buf, sizeof p->buf, "%s", "not " /* 0x662bc0 */);
        }
        strncat(p->buf, "siphoning Ad Infinitum" /* 0x662bc8 */, sizeof p->buf - strlen(p->buf) - 1);
        draw(p, p->x, p->y, p->buf, 0);
        colors(WHITE);
        p->y += 10;
        vibes(p, x, y);
        shaded(p, x, y);
        guano(p, x, y);
        break;
    case 30: {
        header(p, "This LIMBO is" /* 0x663ec0 */);
        uint32_t cap = CAP(set30, index);
        fullness(p, s5, cap, k_populated);
        employing(p, x, y, set);
        uint32_t cap42 = cap * 0x2a;
        uint8_t pct = (uint8_t)(100 - (uint8_t)(cap42 ? s9 * 100u / cap42 : 0));
        const uint8_t (*sp)[2] = g_build.special_pos;
        if (hell()) {
            if ((g_state.unk_638668 >> 8) & 1) {
                int d = abs((int8_t)sp[8][1] - 1 - (uint8_t)y) + abs((int8_t)sp[8][0] + 1 - (uint8_t)x);
                if (d <= 11) {
                    pct >>= 1;
                    line(p, "Under the W.O.U.'s influence!" /* 0x663ef8 */, false, 0); /* W.O.U. */
                }
            }
        } else if (g_state.unk_638668 & 8) {
            int d = abs((int8_t)sp[3][0] + 1 - (uint8_t)x) + abs((int8_t)sp[3][1] - 1 - (uint8_t)y);
            if (d <= 11) {
                pct = (uint8_t)(pct << 1);
                line(p, "Under the C.C.C.'s influence!" /* 0x663f18 */, false, 0); /* C.C.C. */
            }
        }
        snprintf(p->buf, sizeof p->buf, "Bar supplies at %d%%" /* 0x663f38 */, (int)pct);
        line(p, p->buf, false, 0);
        vibes(p, x, y);
        attacked(p, x, y);
        shaded(p, x, y);
        guano(p, x, y);
        break;
    }
    default:
        draw(p, 0x15, 0x3f, "This is an UNKNOWN TILE" /* 0x664358 */, GFX_TEXT_NOJUSTIFY);
        break;
    }
    (void)num;
    wm_redraw(win, NULL);
}
