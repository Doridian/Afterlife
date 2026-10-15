/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Behaviour test for the WP6b views (src/ui/microview.c, macro.c,
 * soulview.c). Needs the user's game files; none are in the repo.
 *
 *   views2_test <data dir> <bundle>[,<CD bundle>] <out dir> <save.ASC|.ALF> ...
 *
 * (The Soulview's faces are in the CD's FULL.000: MEDIUM.000,FULL.000.)
 *
 * For each save: loads it (world_load), paints the main map into a main
 * window, opens the three windows headless and drives them through the
 * synthetic mouse path (gfx_mouse_on_move/on_button + wm_idle):
 *
 *   - Microview on a Fate Structure of each realm: every page, the zoom
 *     levels, the population bars and the funding knob against the
 *     building's figures, the funding slider dragged and the lock clicked
 *     (the sim cell changes exactly as the original would), a bank's loan
 *     taken and repaid (Pennies, the loan count), a training centre's
 *     slider and switch;
 *   - Macromanager: a kind selected (count and cost against the map),
 *     the focus slider dragged, lock, implemented (every building of that
 *     kind changed, nothing else, Pennies paid);
 *   - Soulview: a resident of a building followed (the record, the name in
 *     the title, the face drawn), the story page against
 *     soulview_describe, previous / next, tracking and centring.
 *
 * After each part the intended changes are checked and undone, and the
 * save must then be byte-identical to the loaded one. PNGs go to <out dir>:
 *   <save>_micro_<page>.png, <save>_macro.png, <save>_soul_<page>.png,
 *   <save>_screen2.png
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/build.h"
#include "game/newworld.h"
#include "game/realm.h"
#include "game/resource.h"
#include "game/savegame.h"
#include "game/soultrack.h"
#include "game/tables.h"
#include "game/util.h"
#include "game/world.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/mouse.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "platform/platform.h"
#include "ui/controls.h"
#include "ui/macro.h"
#include "ui/microview.h"
#include "ui/soulview.h"
#include "ui/views.h"
#include "ui/wm.h"

#define W 1024
#define H 768

static int g_failures;
static const char *g_out;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

/* ---- PNG (stored deflate) ---------------------------------------------- */

static uint32_t crc_table[256];

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t c, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, size_t len)
{
    uint8_t hdr[8];
    put32(hdr, (uint32_t)len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len)
        fwrite(data, 1, len, f);
    uint32_t c = crc_update(0xffffffffu, hdr + 4, 4);
    c = crc_update(c, data, len) ^ 0xffffffffu;
    put32(hdr, c);
    fwrite(hdr, 1, 4, f);
}

/* the window's visible part (its rect may be smaller than its surface) */
static void write_png(const char *name, const GfxSurface *s, int w, int h)
{
    if (!g_out || !s)
        return;
    if (w <= 0 || w > s->width)
        w = s->width;
    if (h <= 0 || h > s->height)
        h = s->height;
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", g_out, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "can't write %s\n", path);
        g_failures++;
        return;
    }
    uint8_t rgbx[1024], pal[768];
    gfx_palette_get(rgbx, 0, 256);
    for (int i = 0; i < 256; i++)
        pal[i * 3] = rgbx[i * 4], pal[i * 3 + 1] = rgbx[i * 4 + 1], pal[i * 3 + 2] = rgbx[i * 4 + 2];
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8, ihdr[9] = 3, ihdr[10] = 0, ihdr[11] = 0, ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "PLTE", pal, 768);
    size_t raw_len = (size_t)(w + 1) * (size_t)h;
    size_t blocks = (raw_len + 65534) / 65535;
    uint8_t *raw = malloc(raw_len), *z = malloc(2 + raw_len + blocks * 5 + 4);
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * (size_t)(w + 1)] = 0;
        memcpy(raw + (size_t)y * (size_t)(w + 1) + 1, s->pixels + (size_t)y * (size_t)s->stride, (size_t)w);
    }
    uint8_t *q = z;
    *q++ = 0x78, *q++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw_len;) {
        size_t n = raw_len - off < 65535 ? raw_len - off : 65535;
        *q++ = off + n == raw_len;
        *q++ = (uint8_t)n, *q++ = (uint8_t)(n >> 8);
        *q++ = (uint8_t)~n, *q++ = (uint8_t)(~n >> 8);
        memcpy(q, raw + off, n);
        for (size_t i = 0; i < n; i++) {
            a = (a + raw[off + i]) % 65521;
            b = (b + a) % 65521;
        }
        q += n;
        off += n;
    }
    put32(q, b << 16 | a);
    q += 4;
    chunk(f, "IDAT", z, (size_t)(q - z));
    chunk(f, "IEND", NULL, 0);
    free(raw);
    free(z);
    fclose(f);
}

static void png_win(const char *stem, const char *what, const WmWindow *w)
{
    char name[256];
    snprintf(name, sizeof name, "%s_%s.png", stem, what);
    write_png(name, w->surface, w->rect.right - w->rect.left + 1, w->rect.bottom - w->rect.top + 1);
}

/* a window as it is on the screen (with its child windows) */
static void png_screen(const char *stem, const char *what, const WmWindow *w)
{
    GfxRect r = w->rect;
    wm_local_to_screen(w, &(GfxRect){0, 0, 0, 0});
    GfxRect z = {0, r.right - r.left, 0, r.bottom - r.top};
    wm_local_to_screen(w, &z);
    GfxSurface *s = gfx_surface_create(z.right - z.left + 1, z.bottom - z.top + 1);
    if (!s)
        return;
    gfx_blit(s, 0, 0, s->width, s->height, gfx_screen(), z.left, z.top);
    char name[256];
    snprintf(name, sizeof name, "%s_%s.png", stem, what);
    write_png(name, s, s->width, s->height);
    gfx_surface_destroy(s);
}

/* ---- input -------------------------------------------------------------- */

static uint8_t *read_resource(uint32_t id, size_t *len)
{
    PlatFile *f = rc_open_resource(id);
    if (!f)
        return NULL;
    long l = rc_resource_size(id);
    uint8_t *buf = l > 0 ? malloc((size_t)l) : NULL;
    if (buf)
        plat_file_read(f, buf, 1, (size_t)l);
    plat_file_close(f);
    *len = buf ? (size_t)l : 0;
    return buf;
}

static void pump(int n)
{
    for (int i = 0; i < n; i++) {
        wm_idle();
        ctl_idle();
    }
}

static void mouse_to(int x, int y)
{
    gfx_mouse_on_move(x, y);
    pump(2);
}

static void click_at(int x, int y, unsigned button)
{
    mouse_to(x, y);
    gfx_mouse_on_button(button, true);
    pump(4);
    gfx_mouse_on_button(button, false);
    pump(6);
}

static void click_local(WmWindow *w, int x, int y, unsigned button)
{
    wm_point_to_screen(w, &x, &y);
    click_at(x, y, button);
}

/* left-drag from (x0, y0) to (x1, y1), window-local */
static void drag_local(WmWindow *w, int x0, int y0, int x1, int y1)
{
    wm_point_to_screen(w, &x0, &y0);
    wm_point_to_screen(w, &x1, &y1);
    mouse_to(x0, y0);
    gfx_mouse_on_button(GFX_MOUSE_LEFT, true);
    pump(4);
    mouse_to((x0 + x1) / 2, (y0 + y1) / 2);
    mouse_to(x1, y1);
    gfx_mouse_on_button(GFX_MOUSE_LEFT, false);
    pump(6);
}

static bool click_button(WmWindow *win, intptr_t id)
{
    WmWindow *b = ctl_find_by_id(win->children, id);
    if (!b || !wm_is_visible(b))
        return false;
    click_local(win, (b->rect.left + b->rect.right) / 2, (b->rect.top + b->rect.bottom) / 2, GFX_MOUSE_LEFT);
    return true;
}

static uint8_t wpx(const WmWindow *w, int x, int y)
{
    return w->surface->pixels[(size_t)y * (size_t)w->surface->stride + (size_t)x];
}

static int count_px(const WmWindow *w, int l, int r, int t, int b, int c, bool equal)
{
    int n = 0;
    for (int y = t; y <= b; y++)
        for (int x = l; x <= r; x++)
            n += (wpx(w, x, y) == c) == equal;
    return n;
}

/* ---- the main view (views_test_gfx.c) and the shell's hooks -------------- */

void vt_gfx_init(const uint8_t *rgbx);
void vt_gfx_free(void);
struct MapView *vt_gfx_setup(uint32_t *cells[4], uint8_t *objmap[4], int w, int h);
void vt_gfx_select_plane(int plane);
int vt_gfx_plane(void);
void vt_gfx_paint(GfxSurface *dst);
void vt_gfx_center(int plane, int x, int y);
void vt_gfx_rotate(int dir_x, int dir_y);

static WmWindow *g_main;
static int g_paused;
static int g_center_calls, g_center_plane, g_center_x, g_center_y;
static int g_help_calls;
static unsigned g_menu_cmd;
static int g_answer = 1, g_questions;

static intptr_t main_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b) { return wm_default_proc(w, msg, a, b); }

static int select_plane(int plane)
{
    int prev = vt_gfx_plane();
    world_select_plane(plane);
    vt_gfx_select_plane(plane);
    return prev;
}

static void center_on_cell(int plane, int x, int y)
{
    g_center_calls++;
    g_center_plane = plane, g_center_x = x, g_center_y = y;
}

static void help_balloon(int x, int y, int width, const char *text)
{
    (void)x, (void)y, (void)width;
    if (text && *text)
        g_help_calls++;
}

static void menu_command(unsigned id)
{
    g_menu_cmd = id;
    if (id == 0x17)
        wm_show(macro_window(), false);
}

static int answer_mv(const char *text, const char *buttons)
{
    (void)text, (void)buttons;
    g_questions++;
    return g_answer;
}

static int answer_macro(const char *title, const char *text, const char *buttons)
{
    (void)title, (void)text, (void)buttons;
    g_questions++;
    return g_answer;
}

static void view_setup(void)
{
    uint32_t *cells[4] = {0};
    uint8_t *objmap[4] = {0};
    for (int pl = 1; pl <= 3; pl++)
        cells[pl] = g_state.plane[pl].cells, objmap[pl] = g_state.plane[pl].objmap;
    views_set_context(g_main, vt_gfx_setup(cells, objmap, W, H), &g_paused);
    select_plane(GS_PLANE_HEAVEN);
}

/* ---- helpers on the save ------------------------------------------------------ */

/* the n-th structure origin of tile set `set` on `plane` (with residents
 * for zone sets) */
static bool find(int plane, unsigned set, int n, int *ox, int *oy)
{
    select_plane(plane);
    for (int i = 0; i < GS_MAP_CELLS; i++) {
        int x = i % 100, y = i / 100;
        if (cell_tileset(x, y) != set)
            continue;
        if ((set == 14 || set == 15) && realm_building_pop(x, y) == 0)
            continue;
        if (n-- == 0) {
            *ox = x, *oy = y;
            return true;
        }
    }
    return false;
}

static uint8_t *save_bytes(size_t *len) { return savegame_save_mem(&g_state, len); }

static bool same_as(const uint8_t *before, size_t len0)
{
    size_t len1 = 0;
    uint8_t *after = save_bytes(&len1);
    bool ok = before && after && len0 == len1 && memcmp(before, after, len0) == 0;
    if (!ok && before && after && len0 == len1)
        for (size_t i = 0; i < len0; i++)
            if (before[i] != after[i]) {
                fprintf(stderr, "  save differs at byte %zu (%02x -> %02x)\n", i, before[i], after[i]);
                break;
            }
    free(after);
    return ok;
}

/* ---- the Microview ----------------------------------------------------------------- */

static const uint8_t k_dark[8] = {0x0a, 0x4d, 0x56, 0x67, 0x36, 0x44, 0x7e, 0x2b};
static const uint8_t k_light[8] = {0x58, 0x0b, 0x0d, 0x62, 0x30, 0x0c, 0x0e, 0x24};

static void test_microview_zone(const char *stem, int plane)
{
    WmWindow *mv = microview_window();
    int x, y;
    if (!find(plane, plane == GS_PLANE_HELL ? 14 : 15, 3, &x, &y))
        return;
    const char *rn = plane == GS_PLANE_HELL ? "hell" : "heaven";
    char what[64];
    microview_open(x, y);
    MicroviewState st = microview_state();
    CHECK(wm_is_visible(mv) && st.x == x && st.y == y && st.plane == plane && st.set == cell_tileset(x, y));
    CHECK(strcmp(wm_get_title(mv), "Microview - Normal View") == 0);
    /* PICTURE: the building is drawn in the picture area */
    int drawn = count_px(mv, 0xe, 0xf1, 0x38, st.rows * 8 + 0xe3, 0x13, false);
    CHECK(drawn > 200);
    snprintf(what, sizeof what, "micro_picture_%s", rn);
    png_win(stem, what, mv);
    /* zoom in (left click in the picture) and out again (right click) */
    click_local(mv, 0x80, 0x80, GFX_MOUSE_LEFT);
    CHECK(microview_state().zoom == 0 && strcmp(wm_get_title(mv), "Microview - Zoomed In") == 0);
    snprintf(what, sizeof what, "micro_zoomed_%s", rn);
    png_win(stem, what, mv);
    click_local(mv, 0x80, 0x80, GFX_MOUSE_RIGHT);
    CHECK(microview_state().zoom == 1);
    /* DESCRIPTION */
    CHECK(click_button(mv, MICROVIEW_DESCRIPTION));
    CHECK(microview_state().page == MICROVIEW_DESCRIPTION);
    snprintf(what, sizeof what, "micro_description_%s", rn);
    png_screen(stem, what, mv);
    /* THINGS TO KNOW: white lines from (0x15, 0x3f) */
    CHECK(click_button(mv, MICROVIEW_KNOW));
    CHECK(microview_state().page == MICROVIEW_KNOW && count_px(mv, 0x15, 0xe0, 0x3f, 0x90, 0x1a, true) > 100);
    snprintf(what, sizeof what, "micro_know_%s", rn);
    png_win(stem, what, mv);
    /* THINGS YOU CAN CHANGE: population bars, knob, lock */
    CHECK(click_button(mv, MICROVIEW_CHANGE));
    CHECK(microview_state().page == MICROVIEW_CHANGE);
    unsigned index = microview_state().index;
    uint32_t cap = g_world.tile_info[index].cost, temp = realm_building_temp(x, y), perm = realm_building_perm(x, y);
    if (index < 0x5b && cap) {
        unsigned k = index / 13 > 6 ? 7 : index / 13;
        int l1 = (int)mul_div_clamp(temp, cap, 0xcd), l2 = (int)mul_div_clamp(perm, cap, 0xcd);
        if (l1 > 0xcd)
            l1 = 0xcd;
        int bad = 0;
        for (int i = 0; i < l1; i++)
            bad += wpx(mv, 0x15 + i, 0xa4) != k_dark[k];
        for (int i = 0; i < l2 && l1 + i < 0xcd; i++)
            bad += wpx(mv, 0x15 + l1 + i, 0xa4) != k_light[k];
        printf("  microview %s (%d,%d): %u temp + %u perm of %u: bars %d + %d px, %d wrong\n", rn, x, y, temp, perm,
               cap, l1, l2, bad);
        CHECK(bad == 0);
    }
    SimCell *s = sim_cell(x, y);
    int kx = (int)mul_div_clamp(s->b[0x10], 0xff, 99) + 0x40;
    kx = kx > 0xa2 ? 0xa3 : kx < 0x41 ? 0x40 : kx;
    CHECK(microview_slider_x() == kx + 10);
    snprintf(what, sizeof what, "micro_change_%s", rn);
    png_win(stem, what, mv);
    /* drag the funding slider: sim +0x10 = (x - 0x4a) * 0xff / 99, +0xf = -that */
    uint8_t f10 = s->b[0x10], f0f = s->b[0xf], fe = s->b[0xe];
    drag_local(mv, 0x60, 0x6a, 0x90, 0x6a);
    uint8_t want = (uint8_t)mul_div_clamp(0x90 - 0x4a, 99, 0xff);
    CHECK(s->b[0x10] == want && s->b[0xf] == (uint8_t)-want);
    CHECK(microview_slider_x() == 0x90 - 10 + 10);
    CHECK(g_paused == 0);
    /* the lock toggles sim +0xe bit 7 */
    CHECK(click_button(mv, MICROVIEW_LOCK));
    CHECK((s->b[0xe] & 0x80) != (fe & 0x80));
    CHECK(click_button(mv, MICROVIEW_LOCK));
    CHECK(s->b[0xe] == fe);
    s->b[0x10] = f10, s->b[0xf] = f0f;
    microview_refresh();
    CHECK(click_button(mv, MICROVIEW_PICTURE));
}

static void test_microview_bank(const char *stem)
{
    WmWindow *mv = microview_window();
    int x, y, plane = GS_PLANE_HEAVEN;
    if (!find(plane, 19, 0, &x, &y)) {
        plane = GS_PLANE_HELL;
        if (!find(plane, 19, 0, &x, &y))
            return;
    }
    select_plane(plane);
    microview_open(x, y);
    CHECK(click_button(mv, MICROVIEW_CHANGE));
    SimCell *s = sim_cell(x, y);
    if (sim_cell_u32(s, 1) != 0)
        return; /* a loan running already */
    SimCell keep = *s;
    Bignum pennies = g_state.pennies;
    uint8_t loans = realm_cur()->unk_80ef;
    uint32_t amount = g_tables.set19[microview_state().index % 4].capacity;
    char name[32];
    png_win(stem, "micro_bank", mv);
    g_answer = 2;
    CHECK(click_button(mv, MICROVIEW_LOAN));
    CHECK(sim_cell_u32(s, 1) == 0 && bignum_to_i64(g_state.pennies) == bignum_to_i64(pennies));
    g_answer = 1;
    CHECK(click_button(mv, MICROVIEW_LOAN));
    printf("  microview bank (%d,%d): loan of %u taken\n", x, y, amount);
    CHECK(sim_cell_u32(s, 1) == amount && s->b[5] == 100 && realm_cur()->unk_80ef == loans + 1 && s->b[6] == loans + 1);
    CHECK(bignum_to_i64(g_state.pennies) == bignum_to_i64(pennies) + amount);
    snprintf(name, sizeof name, "micro_loan");
    png_win(stem, name, mv);
    CHECK(click_button(mv, MICROVIEW_LOAN)); /* repay */
    CHECK(sim_cell_u32(s, 1) == 0 && realm_cur()->unk_80ef == loans);
    CHECK(bignum_to_i64(g_state.pennies) == bignum_to_i64(pennies));
    *s = keep;
    g_state.pennies = pennies;
    g_build.status_dirty = 0;
    CHECK(click_button(mv, MICROVIEW_PICTURE));
    int drawn = count_px(mv, 0xe, 0xf1, 0x38, microview_state().rows * 8 + 0xe3, 0x13, false);
    printf("  bank picture: %d px\n", drawn);
    CHECK(drawn > 200);
    png_win(stem, "micro_bank_picture", mv);
}

static void test_microview_training(const char *stem)
{
    WmWindow *mv = microview_window();
    int x, y, plane = GS_PLANE_HELL;
    if (!find(plane, 8, 0, &x, &y)) {
        plane = GS_PLANE_HEAVEN;
        if (!find(plane, 8, 0, &x, &y))
            return;
    }
    select_plane(plane);
    microview_open(x, y);
    CHECK(click_button(mv, MICROVIEW_CHANGE));
    SimCell *s = sim_cell(x, y);
    uint8_t keep = s->b[0xd];
    unsigned index = microview_state().index;
    uint8_t lo = (uint8_t)((index & 0xfe) * -10 + 0x32);
    png_win(stem, "micro_training", mv);
    /* the slider far right: lo + 50 % */
    drag_local(mv, 0x80, 0xaa, 0xd8, 0xaa);
    CHECK((s->b[0xd] & 0x7f) == (unsigned)(lo + 0x32 > 0x7f ? (lo + 0x32) & 0x7f : lo + 0x32));
    CHECK((s->b[0xd] & 0x80) == (keep & 0x80));
    /* the switch */
    CHECK(click_button(mv, MICROVIEW_ACTIVE));
    CHECK((s->b[0xd] & 0x80) != (keep & 0x80));
    CHECK(click_button(mv, MICROVIEW_ACTIVE));
    printf("  microview training centre (%d,%d): accepting %u..%u%%, slider to %u\n", x, y, lo, lo + 50,
           s->b[0xd] & 0x7f);
    s->b[0xd] = keep;
    CHECK(click_button(mv, MICROVIEW_PICTURE));
}

/* ---- the Macromanager ----------------------------------------------------------------- */

static void test_macro(const char *stem)
{
    WmWindow *w = macro_window();
    macro_open();
    CHECK(wm_is_visible(w));
    MacroSettings st = macro_settings();
    CHECK(st.kinds == 0 && st.manual && st.focus == 0x80);
    /* the generic punishments... pick the first Hell kind that exists */
    int kind = -1, n = 0;
    select_plane(GS_PLANE_HELL);
    for (int k = 0; k < 8 && kind < 0; k++) {
        n = 0;
        for (int x = 0; x < 100; x++)
            for (int y = 0; y < 100; y++)
                if (cell_tileset(x, y) == 14 && (cell_index(x, y) / 13 > 7 ? 7u : cell_index(x, y) / 13) == (unsigned)k)
                    n++;
        if (n > 0)
            kind = k;
    }
    if (kind < 0) {
        wm_show(w, false);
        return;
    }
    CHECK(click_button(w, MACRO_HELL_KIND + kind));
    CHECK(macro_settings().kinds == 1u << kind);
    CHECK(macro_cost() == (uint32_t)n * g_state.unk_630430);
    /* the focus slider: left-drag the knob to x 0x60 */
    drag_local(w, 0xa0, 0x20, 0x60, 0x20);
    CHECK(macro_settings().focus == macro_slider_focus(0x60));
    CHECK(click_button(w, MACRO_LOCK));
    CHECK(macro_settings().lock);
    uint32_t cost = macro_cost();
    CHECK(cost == (uint32_t)n * (g_state.unk_630430 + 1));
    png_win(stem, "macro", w);
    /* implement: every such building gets the focus and the lock */
    size_t len0 = 0;
    uint8_t *before = save_bytes(&len0);
    SimCell *keep = malloc(sizeof g_state.plane[1].sim);
    memcpy(keep, g_state.plane[1].sim, sizeof g_state.plane[1].sim);
    Bignum pennies = g_state.pennies;
    uint8_t focus = macro_settings().focus;
    g_answer = 1;
    g_questions = 0;
    CHECK(click_button(w, MACRO_IMPLEMENT));
    printf("  macro: Hell kind %d, %d buildings, focus %u, lock, %u Pennies (%d question)\n", kind, n, focus, cost,
           g_questions);
    CHECK(bignum_to_i64(g_state.pennies) == bignum_to_i64(pennies) - cost);
    int bad = 0, changed = 0;
    select_plane(GS_PLANE_HELL);
    for (int i = 0; i < GS_MAP_CELLS; i++) {
        SimCell *s = &g_state.plane[1].sim[i], *o = &keep[i];
        int x = i % 100, y = i / 100;
        bool hit =
            cell_tileset(x, y) == 14 && (cell_index(x, y) / 13 > 7 ? 7u : cell_index(x, y) / 13) == (unsigned)kind;
        if (hit) {
            changed++;
            bad += s->b[0x10] != focus || s->b[0xf] != (uint8_t)~focus || s->b[0xe] != (o->b[0xe] | 0x80);
            s->b[0x10] = o->b[0x10], s->b[0xf] = o->b[0xf], s->b[0xe] = o->b[0xe];
        }
    }
    CHECK(bad == 0 && changed == n);
    CHECK(memcmp(keep, g_state.plane[1].sim, sizeof g_state.plane[1].sim) == 0);
    CHECK(macro_settings().kinds == 0);
    g_state.pennies = pennies;
    g_build.status_dirty = 0;
    CHECK(same_as(before, len0));
    free(before);
    free(keep);
    png_win(stem, "macro_done", w);
    CHECK(click_button(w, MACRO_CLOSE));
    CHECK(g_menu_cmd == 0x17 && !wm_is_visible(w));
}

/* ---- the Soulview ------------------------------------------------------------------------ */

static void test_soulview(const char *stem)
{
    WmWindow *w = soulview_window();
    int x, y;
    if (!find(GS_PLANE_HELL, 14, 5, &x, &y))
        return;
    soulview_open();
    CHECK(wm_is_visible(w));
    Block26 keep = g_state.block_4f33f8;
    g_state.block_4f33f8.selected = 0;
    soultrack_remove(soultrack_rec(0));
    size_t len0 = 0;
    uint8_t *before = save_bytes(&len0);
    select_plane(GS_PLANE_HELL);
    CHECK(soulview_track_cell(x, y));
    const Rec26 *t = soultrack_rec(0);
    CHECK(t->unk_00[0] == TRACK_RESIDENT && t->unk_00[1] == 1 && t->unk_10 == GS_PLANE_HELL && t->unk_00[4] == x &&
          t->unk_00[5] == y && t->unk_08 != 0);
    char name[SOULVIEW_NAME_LEN], title[128];
    soulview_soul_name(t->unk_08, name);
    snprintf(title, sizeof title, "Soulview - %s  (#1)", name);
    CHECK(strcmp(wm_get_title(w), title) == 0);
    int face = count_px(w, SOULVIEW_PIC_LEFT, SOULVIEW_PIC_RIGHT, SOULVIEW_PIC_TOP, SOULVIEW_PIC_BOTTOM, 0x13, false);
    int status = count_px(w, SOULVIEW_STATUS_LEFT, SOULVIEW_STATUS_RIGHT, SOULVIEW_STATUS_TOP, SOULVIEW_STATUS_BOTTOM,
                          0x1a, true);
    printf("  soulview: following %s at (%d,%d), belief %u, %s, face %d px, status %d px\n", name, x, y, t->unk_12,
           t->unk_0c ? "temporary" : "permanent", face, status);
    CHECK(face > 1000 && status > 50);
    png_win(stem, "soul_picture", w);
    /* the story */
    CHECK(click_button(w, SOULVIEW_DATA));
    CHECK(!soulview_state().picture);
    static char story[0x800];
    soulview_describe(t, story, sizeof story);
    CHECK(strlen(story) > 100);
    png_screen(stem, "soul_story", w);
    CHECK(click_button(w, SOULVIEW_PICTURE));
    CHECK(soulview_state().picture);
    /* the next slot is empty, and back */
    CHECK(click_button(w, SOULVIEW_NEXT));
    CHECK(soulview_state().selected == 1);
    CHECK(count_px(w, SOULVIEW_PIC_LEFT, SOULVIEW_PIC_RIGHT, SOULVIEW_PIC_TOP, SOULVIEW_PIC_BOTTOM, 0x13, false) == 0);
    CHECK(strcmp(wm_get_title(w), "Soulview - No Soul  (#2)") == 0);
    CHECK(click_button(w, SOULVIEW_PREV));
    CHECK(soulview_state().selected == 0);
    /* tracking off and on, centring */
    CHECK(click_button(w, SOULVIEW_TRACK));
    CHECK(soultrack_rec(0)->unk_00[1] == 0);
    CHECK(click_button(w, SOULVIEW_TRACK));
    CHECK(soultrack_rec(0)->unk_00[1] == 1);
    int c0 = g_center_calls;
    CHECK(click_button(w, SOULVIEW_CENTER));
    CHECK(g_center_calls == c0 + 1 && g_center_plane == GS_PLANE_HELL && g_center_x == t->unk_00[6] &&
          g_center_y == t->unk_00[7]);
    /* right-click help */
    int h0 = g_help_calls;
    WmWindow *b = ctl_find_by_id(w->children, SOULVIEW_NEXT);
    if (b)
        click_local(w, (b->rect.left + b->rect.right) / 2, (b->rect.top + b->rect.bottom) / 2, GFX_MOUSE_RIGHT);
    CHECK(g_help_calls == h0 + 1);
    /* stop following (hides the walker): the save is as before */
    soultrack_remove(soultrack_rec(0));
    soulview_refresh();
    CHECK(same_as(before, len0));
    free(before);
    g_state.block_4f33f8 = keep;
    soulview_refresh();
}

/* ---- per save ------------------------------------------------------------------------------ */

static void run_save(const char *path)
{
    char err[200];
    if (!world_load(path, err, sizeof err)) {
        fprintf(stderr, "%s: %s\n", path, err);
        g_failures++;
        return;
    }
    tables_init_defaults();
    realm_tile_info_init();
    world_select_plane(GS_PLANE_HEAVEN);
    realm_tile_costs_update(true);
    world_select_plane(GS_PLANE_HELL);
    realm_tile_costs_update(true);
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char stem[64];
    snprintf(stem, sizeof stem, "%.*s", (int)strcspn(base, "."), base);
    printf("%s: \"%s\", year %u, Pennies %lld\n", base, g_state.hdr.title, (unsigned)g_state.year,
           (long long)bignum_to_i64(g_state.pennies));
    size_t len0 = 0;
    uint8_t *before = save_bytes(&len0);

    view_setup();
    vt_gfx_paint(g_main->surface);
    wm_redraw(g_main, NULL);
    /* realm_score_mix / realm_score_neighbours (0x43b2a7, 0x43b469), which
     * the Microview's balance bar and THINGS TO KNOW call, count badly
     * balanced / placed buildings in realm +0x8105 / +0x8109 as a side
     * effect, in the original too: undone below */
    PlaneStats *rh = &g_state.plane[1].stats, *rv = &g_state.plane[3].stats;
    uint16_t c0[2][3] = {{rh->unk_8105, rh->unk_8107, rh->unk_8109}, {rv->unk_8105, rv->unk_8107, rv->unk_8109}};
#define UNDO_COUNTS()                                                                                                  \
    (rh->unk_8105 = c0[0][0], rh->unk_8107 = c0[0][1], rh->unk_8109 = c0[0][2], rv->unk_8105 = c0[1][0],               \
     rv->unk_8107 = c0[1][1], rv->unk_8109 = c0[1][2])
    test_microview_zone(stem, GS_PLANE_HEAVEN);
    test_microview_zone(stem, GS_PLANE_HELL);
    printf("  (realm +0x8105 / +0x8109 counted %d, %d / %d, %d)\n", rv->unk_8105 - c0[1][0], rv->unk_8109 - c0[1][2],
           rh->unk_8105 - c0[0][0], rh->unk_8109 - c0[0][2]);
    UNDO_COUNTS();
    CHECK(same_as(before, len0));
    test_microview_bank(stem);
    UNDO_COUNTS();
    CHECK(same_as(before, len0));
    test_microview_training(stem);
    UNDO_COUNTS();
    CHECK(same_as(before, len0));
    test_macro(stem);
    UNDO_COUNTS();
    CHECK(same_as(before, len0));
    test_soulview(stem);
    microview_refresh();
    UNDO_COUNTS();
    char name[128];
    snprintf(name, sizeof name, "%s_screen2.png", stem);
    write_png(name, gfx_screen(), W, H);
    CHECK(same_as(before, len0));
    free(before);
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: %s <data dir> <bundle>[,<CD bundle>] <out dir> <save> ...\n", argv[0]);
        return 2;
    }
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    g_out = argv[3];
    crc_init();
    plat_set_data_dir(argv[1]);
    /* "LOCAL.000,CD.000": the Soulview's faces are only in the CD bundle */
    char local[1024], *cd;
    snprintf(local, sizeof local, "%s", argv[2]);
    cd = strchr(local, ',');
    if (cd)
        *cd++ = 0;
    if (!rc_initialize(local, cd ? cd : local))
        return 1;
    if (!plat_init("views2_test", W, H) || !gfx_init(W, H, false))
        return 1;
    gfx_mouse_init();
    gfx_font_init();
    gfx_font_load(500);
    gfx_font_load(501);
    size_t plen = 0;
    uint8_t *raw = read_resource(33, &plen);
    uint8_t *rgbx = raw ? gfx_palette_load_resource(raw, plen) : NULL;
    free(raw);
    CHECK(rgbx != NULL);
    if (!rgbx)
        return 1;
    gfx_palette_set(rgbx, GFX_PAL_FIRST, GFX_PAL_COUNT);
    gfx_palette_build_highlight();
    vt_gfx_init(rgbx);
    free(rgbx);

    CHECK(wm_initialize() == 0);
    ctl_initialize();
    ctl_set_pause_flag(&g_paused);
    wm_set_input_live(true);
    GfxRect full = {0, W - 1, 0, H - 1};
    g_main = wm_create_toplevel(&full, main_proc, 0);
    CHECK(g_main != NULL);
    if (!g_main)
        return 1;
    wm_show(g_main, true);
    views_hooks.select_plane = select_plane;
    views_hooks.center_on_cell = center_on_cell;
    views_hooks.help_balloon = help_balloon;
    views_hooks.menu_command = menu_command;
    infoviews_install_hooks();
    microview_hooks.message = answer_mv;
    macro_hooks.message = answer_macro;

    world_reset();
    view_setup();
    uint32_t rng0 = rng_state();
    CHECK(microview_create());
    CHECK(rng_state() != rng0); /* two draws, as the original */
    CHECK(macro_create());
    CHECK(soulview_create());
    WmWindow *mv = microview_window(), *ma = macro_window(), *sv = soulview_window();
    CHECK(mv && ma && sv && !wm_is_visible(mv) && !wm_is_visible(ma) && !wm_is_visible(sv));
    if (!mv || !ma || !sv)
        return 1;
    printf("Microview %dx%d, Macromanager %dx%d, Soulview %dx%d\n", mv->surface->width, mv->surface->height,
           ma->surface->width, ma->surface->height, sv->surface->width, sv->surface->height);
    wm_move(ma, 16, H - ma->surface->height - 16);

    for (int i = 4; i < argc; i++)
        run_save(argv[i]);

    CHECK(click_button(mv, MICROVIEW_CLOSE));
    CHECK(!wm_is_visible(mv));
    CHECK(click_button(sv, SOULVIEW_CLOSE));
    CHECK(!wm_is_visible(sv));

    microview_destroy();
    macro_destroy();
    soulview_destroy();
    ctl_shutdown();
    wm_shutdown();
    vt_gfx_free();
    world_free();
    gfx_font_shutdown();
    gfx_shutdown();
    plat_shutdown();
    rc_shutdown();
    printf("%d failures\n", g_failures);
    return g_failures != 0;
}
