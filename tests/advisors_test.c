/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Headless test of the advisor window, Jasper and Aria (src/ui/advisors.c).
 * Needs the user's game files; none are in the repo.
 *
 *   advisors_test <data dir> <bundle> <save> <out dir> [--window]
 *
 * e.g. advisors_test orig MEDIUM.000 orig/ASCEN001.ASC /tmp/out
 *
 * Creates the window like the game does (hidden at startup), loads the
 * save, evaluates the issues, opens the window and runs the talking heads
 * for a few hundred frames, then drives the talker messages (lip-sync
 * marker, idle, hush), the right-click help, minimizing, a button press
 * (no audio here, so the speech runs through without waiting) and the
 * editor's speech browser. Writes the screen as <out dir>/advisors.png.
 * Also checks that every speech's subtitles resolve in ALIFE.EXE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compbun/cbres.h"
#include "game/build.h"
#include "game/planet.h"
#include "game/resource.h"
#include "game/sound.h"
#include "game/util.h"
#include "game/world.h"
#include "gfx/akos.h"
#include "gfx/akos_talk.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/mouse.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "platform/platform.h"
#include "ui/advisors.h"
#include "ui/advisors_int.h"
#include "ui/controls.h"
#include "ui/views.h"
#include "ui/wm.h"

#define W 640
#define H 480

static int g_failures;
static const char *g_out;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

/* --- minimal PNG writer (stored deflate blocks), from formats_dump.c ----- */

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
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
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

/* Indexed 8-bit PNG; pal is 256 R,G,B,x entries. */
static int write_png(const char *path, const uint8_t *pix, int w, int h, int pitch, const uint8_t *pal)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8, ihdr[9] = 3, ihdr[10] = 0, ihdr[11] = 0, ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    uint8_t plte[768];
    for (int i = 0; i < 256; i++)
        plte[i * 3] = pal[i * 4], plte[i * 3 + 1] = pal[i * 4 + 1], plte[i * 3 + 2] = pal[i * 4 + 2];
    chunk(f, "PLTE", plte, 768);

    size_t raw_len = (size_t)(w + 1) * (size_t)h;
    size_t blocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *raw = malloc(raw_len), *z = malloc(z_len);
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * (size_t)(w + 1)] = 0;
        memcpy(raw + (size_t)y * (size_t)(w + 1) + 1, pix + (size_t)y * (size_t)pitch, (size_t)w);
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
    return fclose(f);
}

static void shot(const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.png", g_out, name);
    const GfxSurface *scr = gfx_screen();
    uint8_t pal[256 * 4];
    gfx_palette_get(pal, 0, 256);
    CHECK(write_png(path, scr->pixels, scr->width, scr->height, scr->stride, pal) == 0);
    printf("wrote %s\n", path);
}

/* --- helpers ------------------------------------------------------------ */

static uint8_t *read_resource(uint32_t id, long *len)
{
    PlatFile *f = rc_open_resource(id);
    if (!f)
        return NULL;
    *len = rc_resource_size(id);
    uint8_t *buf = *len > 0 ? malloc((size_t)*len) : NULL;
    if (buf)
        plat_file_read(f, buf, 1, (size_t)*len);
    plat_file_close(f);
    return buf;
}

static intptr_t main_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b) { return wm_default_proc(w, msg, a, b); }

/* distinct colours in a window-local rectangle of the screen */
static int colours_in(const WmWindow *w, int x0, int y0, int x1, int y1)
{
    bool seen[256] = {false};
    int n = 0;
    const GfxSurface *s = gfx_screen();
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int sx = x, sy = y;
            wm_point_to_screen(w, &sx, &sy);
            uint8_t c = s->pixels[(size_t)sy * s->stride + sx];
            n += !seen[c];
            seen[c] = true;
        }
    return n;
}

static char g_help[256];
static void help_balloon(int x, int y, int width, const char *text)
{
    (void)x, (void)y, (void)width;
    snprintf(g_help, sizeof g_help, "%s", text);
}

/* frames of the game loop as far as the advisors care; each one is a
 * talker step (the clock is pushed back so every frame steps) */
static void frames(int n)
{
    for (int i = 0; i < n; i++) {
        for (int t = 1; t <= 2; t++) {
            AkosTalker *k = akos_talk(t);
            if (k->anim)
                k->last_time = plat_ticks_ms() - k->period - 1;
        }
        wm_idle();
        ctl_idle();
        akos_talk_idle();
    }
}

/* every speech's subtitles are in the executable */
static void check_texts(void)
{
    int lines = 0;
    for (int which = 0; which < 3; which++)
        for (unsigned code = 0; code <= ADV_NONE; code++) {
            int n = advisor_speech_length(which, code);
            /* at least the default speech, except that 0x41db2b's case
             * 190 (Karma in both realms) is empty in the original */
            if (n <= 0 && !(which == 2 && code == 190)) {
                fprintf(stderr, "no speech for table %d code %u\n", which, code);
                g_failures++;
            }
            for (int i = 0; i < n; i++) {
                const char *s = advisor_speech_text(which, code, i);
                CHECK(s && strlen(s) > 3);
            }
            if (code % 5 == 0)
                lines += n;
        }
    printf("speech lines (codes 0, 5, .. 220): %d\n", lines);
    CHECK(strcmp(advisor_speech_text(2, 20, 0), advisor_speech_text(2, 20, 0)) == 0);
    CHECK(strcmp(advisor_issue_label(20), "GATES!") == 0 && strcmp(advisor_issue_label(1), "Nada") == 0);
}

static void print_issues(const char *what)
{
    printf("%s:", what);
    for (int i = 0; i < 5; i++)
        printf(" [%u/%u]", advisor_issue(i, false), advisor_issue(i, true));
    printf("\n");
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: %s <data dir> <bundle> <save> <out dir> [--window]\n", argv[0]);
        return 2;
    }
    g_out = argv[4];
    bool window = argc > 5 && strcmp(argv[5], "--window") == 0;
    if (!window) {
#ifdef _WIN32
        _putenv_s("SDL_VIDEODRIVER", "dummy");
#else
        setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    }
    crc_init();
    snd_sfx_enabled = 0; /* no iMUSE here */
    snd_music_enabled = 0;
    plat_set_data_dir(argv[1]);
    /* the voices are on the CD bundle */
    PlatFile *cd = plat_file_open("ALIFE.000");
    if (cd)
        plat_file_close(cd);
    if (!rc_initialize(argv[2], cd ? "ALIFE.000" : argv[2]))
        return 1;
    /* the voices stream through the compbun layers (iMUSE isn't started:
     * they don't play, so akos_talk_wait returns at once) */
    rc_hook_compbun();
    compbun_init(1, 0);
    if (!plat_init("advisors_test", W, H) || !gfx_init(W, H, false))
        return 1;
    gfx_mouse_init();
    gfx_font_init();
    gfx_font_load(500);
    gfx_font_load(501);
    gfx_palette_init();
    long len = 0;
    uint8_t *raw = read_resource(33, &len);
    uint8_t *pal = raw ? gfx_palette_load_resource(raw, (size_t)len) : NULL;
    free(raw);
    CHECK(pal != NULL);
    if (pal)
        gfx_palette_set(pal, GFX_PAL_FIRST, GFX_PAL_COUNT);
    free(pal);
    CHECK(wm_initialize() == 0);
    ctl_initialize();
    int paused = 0;
    ctl_set_pause_flag(&paused);
    wm_set_input_live(true);
    akos_initialize();
    akos_talk_initialize();

    if (!planet_tables_load()) {
        fprintf(stderr, "can't unpack the planet tables\n");
        return 1;
    }
    check_texts();

    GfxRect full = {0, W - 1, 0, H - 1};
    WmWindow *mainw = wm_create_toplevel(&full, main_proc, 0);
    CHECK(mainw != NULL);
    if (!mainw)
        return 1;
    GfxSurface *back = gfx_load_bmp_resource(1210, NULL);
    if (back) {
        gfx_tile_fill(mainw->surface, 0, 0, W, H, back, 0, 0, back->width, back->height);
        gfx_surface_destroy(back);
    }
    wm_show(mainw, true);
    views_set_context(mainw, NULL, &paused);
    views_hooks.help_balloon = help_balloon;

    /* startup: created hidden, before any game is loaded */
    CHECK(advisor_create());
    WmWindow *win = advisor_window();
    CHECK(win != NULL);
    if (!win)
        return 1;
    CHECK(!wm_is_visible(win));
    printf("advisor window at %d,%d-%d,%d\n", win->rect.left, win->rect.top, win->rect.right, win->rect.bottom);
    CHECK(win->rect.left == 0x10 && win->rect.bottom == H - 1 - 0x19);

    char err[256];
    if (!world_load(argv[3], err, sizeof err)) {
        fprintf(stderr, "can't load %s: %s\n", argv[3], err);
        return 1;
    }
    planet_summary();
    rng_seed(1);
    world_select_plane(GS_PLANE_HELL);

    /* the yearly update works with the window hidden; the plane is kept */
    advisor_update();
    CHECK(g_world.plane == GS_PLANE_HELL);
    print_issues("issues (year 1)");
    /* the merge is sorted, and a button has a code unless both lists ran out */
    unsigned last = 0;
    for (int i = 0; i < 5; i++) {
        unsigned he = advisor_issue(i, false), hl = advisor_issue(i, true);
        unsigned c = he < hl ? he : hl;
        CHECK(c >= last);
        last = c;
        if (he != ADV_NONE && hl != ADV_NONE)
            CHECK(he == hl);
    }
    /* the label shows the realms */
    for (int i = 0; i < 5; i++) {
        WmWindow *b = ctl_find_by_id(win->children, ADVISOR_ID_ISSUE + i);
        CHECK(b != NULL);
        const char *l = b ? ctl_button_get_label(b) : NULL;
        CHECK(l != NULL);
        if (!l)
            continue;
        printf("button %d: \"%s\"\n", i, l);
        CHECK((advisor_issue(i, true) != ADV_NONE) == (l[0] == 0x13));
        CHECK((advisor_issue(i, false) != ADV_NONE) == (l[strlen(l) - 1] == 0x14));
    }
    /* the money issue is shared by both realms: in debt -> Pennies */
    uint8_t debt = g_state.unk_593247;
    g_state.unk_593247 = 3;
    advisor_update();
    CHECK(advisor_issue(0, false) == 15 && advisor_issue(0, true) == 15);
    g_state.unk_593247 = debt;
    advisor_update();

    /* Helpers on */
    advisor_show(true);
    CHECK(wm_is_visible(win));
    CHECK(akos_talk(1)->win == win && akos_talk(2)->win == win);
    CHECK(akos_talk(1)->anim == 1 && akos_talk(2)->anim == 1);
    frames(300);
    int cj = colours_in(win, 0xe, 0xe, 0xe + 0x8c, 0xe + 0x7d);
    int ca = colours_in(win, 0xa1, 0xe, 0xa1 + 0x8b, 0xe + 0x7d);
    printf("colours in the head areas: Jasper %d, Aria %d\n", cj, ca);
    CHECK(cj > 8 && ca > 8);
    GfxRect bj = akos_get_bounds(1), ba = akos_get_bounds(2);
    CHECK(bj.right > bj.left && ba.right > ba.left);

    /* talker messages: a lip-sync marker picks a mouth, hush */
    wm_send(win, win, AKOS_TALK_MSG_MARKER, 1, 12);
    CHECK(akos_talk(1)->anim == 13);
    wm_send(win, win, AKOS_TALK_MSG_MARKER, 2, 0x40 + 4); /* Aria's mouth 5 */
    CHECK(akos_talk(2)->anim == 5);
    wm_send(win, win, ADVISOR_MSG_HUSH, 0, 0);
    CHECK(akos_talk(1)->anim == 6 && akos_talk(2)->anim == 6);
    /* idle: one time in six an idle animation */
    int idles = 0;
    for (int i = 0; i < 60; i++) {
        akos_talk_set_anim(1, 1);
        wm_send(win, win, AKOS_TALK_MSG_IDLE, 1, 0);
        idles += akos_talk(1)->anim == 0x29 || akos_talk(1)->anim == 0x2c;
    }
    printf("idle animations: %d of 60\n", idles);
    CHECK(idles > 0 && idles < 30);
    frames(100);

    /* right-click help */
    WmWindow *b0 = ctl_find_by_id(win->children, ADVISOR_ID_ISSUE + 1);
    wm_send(win, b0, WM_MSG_CTL_RDOWN, ADVISOR_ID_ISSUE + 1, 0);
    CHECK(strstr(g_help, "2nd MOST IMPORTANT") != NULL);
    wm_send(win, win->close_button, WM_MSG_CTL_RDOWN, ADVISOR_ID_CLOSE, 0);
    CHECK(strstr(g_help, "CLOSES") != NULL);

    /* a press: no voice, so each line is shown and removed at once */
    WmWindow *b1 = ctl_find_by_id(win->children, ADVISOR_ID_ISSUE);
    wm_send(win, b1, WM_MSG_CTL_CLICK, ADVISOR_ID_ISSUE, 0);
    CHECK(akos_talk(1)->text == NULL && akos_talk(2)->text == NULL);
    CHECK(wm_is_visible(win));

    /* minimizing stops the heads, restoring reopens them */
    wm_send(win, win, WM_MSG_MINIMIZED, 1, 0);
    CHECK(akos_talk(1)->win == NULL && g_adv_talk.silenced);
    wm_send(win, win, WM_MSG_MINIMIZED, 0, 0);
    CHECK(akos_talk(1)->win == win && !g_adv_talk.silenced);

    /* the editor's speech browser */
    g_build.editor = 1;
    advisor_update();
    CHECK(advisor_issue(0, false) == 5 && advisor_issue(1, true) == 5 && advisor_issue(2, false) == 5 &&
          advisor_issue(2, true) == 5 && advisor_issue(0, true) == ADV_NONE && advisor_issue(3, false) == ADV_NONE);
    WmWindow *b4 = ctl_find_by_id(win->children, ADVISOR_ID_ISSUE + 4);
    wm_send(win, b4, WM_MSG_CTL_CLICK, ADVISOR_ID_ISSUE + 4, 0);
    wm_send(win, b4, WM_MSG_CTL_CLICK, ADVISOR_ID_ISSUE + 4, 0);
    CHECK(advisor_issue(0, false) == 15);
    WmWindow *b3 = ctl_find_by_id(win->children, ADVISOR_ID_ISSUE + 3);
    for (int i = 0; i < 5; i++)
        wm_send(win, b3, WM_MSG_CTL_CLICK, ADVISOR_ID_ISSUE + 3, 0);
    CHECK(advisor_issue(0, false) == 5);
    g_build.editor = 0;
    advisor_update();

    /* a subtitle for the picture */
    frames(50);
    akos_talk_say(2, &g_adv_talk.text_rect[2], advisor_speech_text(2, 20, 0), 0);
    frames(5);
    wm_redraw(win, NULL);
    shot("advisors");

    advisor_show(false);
    wm_show(win, false);
    advisor_destroy();
    CHECK(advisor_window() == NULL);
    akos_talk_shutdown();
    akos_shutdown();
    ctl_shutdown();
    wm_destroy(mainw);
    wm_shutdown();
    gfx_font_shutdown();
    gfx_shutdown();
    plat_shutdown();
    if (g_failures)
        fprintf(stderr, "%d failure(s)\n", g_failures);
    else
        printf("ok\n");
    return g_failures ? 1 : 0;
}
