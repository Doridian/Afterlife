/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The title sequence (ALIFE.EXE 0x426460-0x42a6c2). Overview: title.h.
 *
 * Flow of title_run (0x426460):
 *
 *   take over the main window (0x42655a)
 *   do {
 *       intro (0x4265dc), fade out, animated title (0x426ad5), fade out,
 *       menu loop (0x428b22): background, fade in, wait up to 5 s, the
 *       buttons, then the pump until title_wnd_proc ends the wait
 *   } while ("Replay Cheesy Intro")
 *   give the main window back (0x4298e5)
 *
 * State (0x65ebbc-0x65ebe4) is in `g` below. The skip flag is set by
 * title_wnd_proc: 1 skips the current animation (any key or click),
 * 2 skips everything up to the menu (Esc, or a missing resource).
 */
#include "ui/title.h"

#include <stdlib.h>
#include <string.h>

#include "formats/flc.h"
#include "game/config.h"
#include "game/resource.h"
#include "game/shell.h"
#include "game/music.h"
#include "game/sound.h"
#include "game/timer.h"
#include "game/world.h"
#include "gfx/draw.h"
#include "gfx/mouse.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "platform/platform.h"
#include "ui/controls.h"
#include "ui/controls_int.h"

#define BG 0x13          /* background fill colour */
#define FONT_BIG 0x1f5   /* 501 */
#define LABEL_COLORS 0x13131a13u

/* Resources */
#define RES_INTRO_FLC 5000
#define RES_INTRO_PAL 0x4b1
#define RES_TITLE_A 0x13ed /* 5101: opening, loops while the ticker starts */
#define RES_TITLE_B 0x13ef /* 5103: talking, loops */
#define RES_TITLE_C 0x13f0 /* 5104: one-shot interlude */
#define RES_TITLE_PAL 0x4bd
#define RES_TICKER 0x4be
#define RES_MENU_BMP 0x4b4
#define RES_MENU_PAL 0x4b5
#define RES_MENU_DECO 0x4b3
#define RES_BUTTON 0x229
#define RES_GAME_PAL 0x21

#define SND_INTRO_MUSIC 0x10031
#define SND_TITLE_MUSIC 0x10069
#define SND_MENU_MUSIC 0x1002c
#define SND_TICK 0x10037
#define SND_SWOOSH 0x10036
#define SND_FX_A 0x10004
#define SND_FX_B 0x1002e
#define VOICE_SLOT 1

#define MAIN_BUTTONS 5
#define DIFF_BUTTONS 4

static struct {
    uint8_t skip;         /* 0x65ebbc */
    unsigned bench;       /* 0x65ebc0 di_video_benchmark */
    WmProc old_proc;      /* 0x65ebc4 */
    uint8_t *pal;         /* 0x65ebc8 the palette of the current screen */
    uint8_t waiting;      /* 0x65ebcc menu: pump until cleared */
    int quit;             /* 0x65ebd0 */
    uint8_t again;        /* 0x65ebd4 run the sequence again */
    uint8_t leave_menu;   /* 0x65ebd8 the menu loop ends when the wait does */
    uint8_t main_focus;   /* 0x65ebdc */
    uint8_t menu;         /* 0x65ebe0 0 none, 1 main, 3 difficulty */
    uint8_t diff_focus;   /* 0x65ebe4 */
    WmWindow *main_btn[MAIN_BUTTONS]; /* 0x4f3090 */
    WmWindow *diff_btn[DIFF_BUTTONS]; /* 0x4f30a8 */
    GfxSurface *deco;     /* 0x63926c */
    TitleStage stage;
} g = {.waiting = 1, .again = 1, .leave_menu = 1};

TitleStage title_stage(void) { return g.stage; }

/* Port: the intro and the animated title draw through FlcTargets that
 * point into the main window's surface (target_init), so the port window's
 * relayout waits until they are done. */
static bool g_holds_surface;
bool title_holds_surface(void) { return g_holds_surface; }

/* ---- helpers --------------------------------------------------------------- */

static WmWindow *main_window(void) { return g_main_window; }
static GfxSurface *main_surface(void) { return g_main_window->surface; }

static void pump(void) { ctl_modal_pump(); } /* sys_pump */

static void fill_main(void)
{
    GfxSurface *s = main_surface();
    gfx_fill_rect(s, 0, 0, s->width, s->height, BG);
}

/* Pumps at least once, until `deadline` (timeGetTime). */
static void wait_until(uint32_t deadline)
{
    do
        pump();
    while (tm_get_time() < deadline);
}

/* 0x4cdcbf rc_load_resource: the whole resource in a heap block. */
static uint8_t *load_resource(uint32_t id, size_t *len)
{
    long size = rc_resource_size(id);
    PlatFile *f = size > 0 ? rc_open_resource(id) : NULL;
    if (!f)
        return NULL;
    uint8_t *buf = malloc((size_t)size);
    if (buf && plat_file_read(f, buf, 1, (size_t)size) != (size_t)size) {
        free(buf);
        buf = NULL;
    }
    plat_file_close(f);
    *len = buf ? (size_t)size : 0;
    return buf;
}

/* A voice line in slot 1 (snd_slot_stream(id, 1, 2, 2)), optionally
 * stopping the previous one first. */
static void voice(uint32_t id, bool stop_first)
{
    if (stop_first)
        snd_slot_stop_stream(VOICE_SLOT);
    snd_slot_stream(id, VOICE_SLOT, 2, SND_BUF_VOICE);
}

/* ---- palettes (0x42a2a1-0x42a5f3) ---------------------------------------- */

/* 0x42a2a1: loads palette resource `id` as the current screen's palette,
 * and sets it if `apply`. */
static void pal_load(uint32_t id, bool apply)
{
    free(g.pal);
    g.pal = NULL;
    size_t len = 0;
    uint8_t *raw = load_resource(id, &len);
    if (raw)
        g.pal = gfx_palette_load_resource(raw, len);
    if (apply && g.pal)
        gfx_palette_set(g.pal, GFX_PAL_FIRST, GFX_PAL_COUNT);
    free(raw);
}

/* 0x42a598: one Bresenham step of a fade: how many of `total` units fall
 * into this one of `steps` steps. */
static int fade_units(int16_t *acc, int total, int steps)
{
    int n = 0;
    *acc = (int16_t)(*acc - total);
    while (*acc < 0) {
        *acc = (int16_t)(*acc + steps);
        n++;
    }
    return n;
}

/* 0x42a336 (dir < 0, towards black) / 0x42a467 (dir > 0, from black):
 * one of 32 fade steps of `cur` towards / away from `target`, then the
 * whole main window is blitted to the screen. */
static void fade_step(int16_t acc[3][256], uint8_t *cur, const uint8_t *target, int dir)
{
    for (int i = 0; i < 256; i++)
        for (int c = 0; c < 3; c++) {
            int n = fade_units(&acc[c][i], target[i * 4 + c], 32);
            cur[i * 4 + c] = (uint8_t)(cur[i * 4 + c] + (dir < 0 ? -n : n));
        }
    gfx_palette_set(cur, GFX_PAL_FIRST, GFX_PAL_COUNT);
    GfxSurface *s = main_surface();
    gfx_blit(NULL, 0, 0, s->width, s->height, s, 0, 0);
}

/* The 32-step fade loop of 0x4294f2 / 0x42970c, `ms` long. Stops early
 * when the skip flag is set. */
static void fade_run(unsigned ms, uint8_t *cur, int dir)
{
    static int16_t acc[3][256];
    memset(acc, 0, sizeof(acc));
    gfx_palette_set(cur, GFX_PAL_FIRST, GFX_PAL_COUNT);
    TitleStage was = g.stage;
    g.stage = TITLE_STAGE_FADE;
    for (int i = 0; i < 32; i++) {
        uint32_t t0 = tm_get_time();
        fade_step(acc, cur, g.pal, dir);
        do {
            pump();
            if (g.skip)
                break;
        } while (tm_get_time() < t0 + (ms >> 5));
        if (g.skip)
            break;
    }
    g.stage = was;
}

/* 0x4294f2: fades the current palette out to black over `ms`; without a
 * fade (skipped, slow display) just goes black. */
static void fade_out(unsigned ms)
{
    static uint8_t buf[1024];
    if (!g.pal) {
        g.skip = 2;
    } else if (g.skip == 0 && !(g.bench & 1)) {
        memcpy(buf, g.pal, sizeof(buf));
        fade_run(ms, buf, -1);
        return;
    }
    memset(buf, 0, sizeof(buf));
    gfx_palette_set(buf, GFX_PAL_FIRST, GFX_PAL_COUNT);
}

/* 0x42970c: fades the current palette in from black over `ms` (clears a
 * pending single skip first); nothing once everything is skipped. */
static void fade_in(unsigned ms)
{
    static uint8_t buf[1024];
    if (g.skip == 2)
        return;
    if (!g.pal) {
        g.skip = 2;
    } else if (!(g.bench & 1)) {
        g.skip = 0;
        memset(buf, 0, sizeof(buf));
        fade_run(ms, buf, 1);
        return;
    }
    gfx_palette_set(g.pal, GFX_PAL_FIRST, GFX_PAL_COUNT);
}

/* 0x42a5f3: blits `src` centred onto `dst` (clipped at the left / top). */
static void blit_centred(GfxSurface *dst, const GfxSurface *src)
{
    int x = (dst->width >> 1) - (src->width >> 1);
    if (x < 1)
        x = 0;
    int y = (dst->height >> 1) - (src->height >> 1);
    if (y < 1)
        y = 0;
    int w = dst->width - x, h = dst->height - y;
    if (src->width <= w)
        w = src->width;
    if (src->height <= h)
        h = src->height;
    gfx_blit(dst, x, y, w, h, src, 0, 0);
}

/* ---- FLC playback ------------------------------------------------------------ */

typedef struct {
    FlcPlayer p;
    uint8_t *data;
} Anim;

static FlcTarget g_target;      /* flc_target_init(main_window) */
static PlatColor g_flc_pal[256];

/* 0x40b0c9 flc_target_init: the main window's surface. */
static void target_init(void)
{
    GfxSurface *s = main_surface();
    g_target = (FlcTarget){s->pixels, s->stride, s->width, s->height, g_flc_pal, 1000};
}

/* flc_open(p, id, 1): in memory. */
static bool anim_open(Anim *a, uint32_t id)
{
    size_t len = 0;
    memset(a, 0, sizeof(*a));
    a->data = load_resource(id, &len);
    if (!a->data)
        return false;
    if (flc_open(&a->p, a->data, len) != FLC_OK) {
        free(a->data);
        a->data = NULL;
        return false;
    }
    return true;
}

static void anim_close(Anim *a)
{
    flc_close(&a->p);
    free(a->data);
    memset(a, 0, sizeof(*a));
}

/* flc_next_frame; in-memory players apply COLOR chunks to entries
 * 10..245 (flc_apply_palette 0x40a469). */
static void next_frame(FlcPlayer *p)
{
    int r = flc_next_frame(p, &g_target);
    if (r >= 0 && (r & FLC_FRAME_PALETTE)) {
        uint8_t rgbx[1024];
        for (int i = 0; i < 256; i++) {
            rgbx[i * 4] = g_flc_pal[i].r;
            rgbx[i * 4 + 1] = g_flc_pal[i].g;
            rgbx[i * 4 + 2] = g_flc_pal[i].b;
            rgbx[i * 4 + 3] = 0;
        }
        gfx_palette_set(rgbx, GFX_PAL_FIRST, GFX_PAL_COUNT);
    }
}

/* ---- intro (0x4265dc) -------------------------------------------------------- */

#define INTRO_W 320
#define INTRO_H 200
#define INTRO_MAX 40 /* the original's array of players (0x1a90 bytes) */

/* 0x4265dc intro_play: the 320x200 intro tiled over the window (one
 * copy per 320x200 that fits unless the display is slow), each copy
 * starting five frames after the previous one, until the last copy is
 * done and the intro music has ended. */
static void intro_play(void)
{
    wm_flush_input();
    if (g.skip == 2)
        return;
    WmWindow *w = main_window();
    int width = w->rect.right - w->rect.left + 1, height = w->rect.bottom - w->rect.top + 1;
    int nx = 1, ny = 1;
    if (!(g.bench & 2)) {
        nx = width / INTRO_W;
        ny = height / INTRO_H;
    }
    if (nx * ny > INTRO_MAX)
        ny = INTRO_MAX / (nx > 0 ? nx : 1);
    int n = nx * ny;
    int x0 = (width - nx * INTRO_W) >> 1, y0 = (height - ny * INTRO_H) >> 1;
    GfxRect r = {x0, nx * INTRO_W + x0 - 1, y0, ny * INTRO_H + y0 - 1};

    Anim intro;
    if (n == 0 || !anim_open(&intro, RES_INTRO_FLC)) {
        g.skip = 1;
        return;
    }
    g.stage = TITLE_STAGE_INTRO;
    static FlcPlayer copies[INTRO_MAX]; /* flc_clone (0x40a0d4): share the data */
    for (int row = 0, k = 0; row < ny; row++)
        for (int col = 0; col < nx; col++, k++) {
            copies[k] = intro.p;
            copies[k].x = col * INTRO_W + x0;
            copies[k].y = row * INTRO_H + y0;
        }
    target_init();
    fill_main();
    pal_load(RES_INTRO_PAL, true);
    wm_redraw(w, NULL);
    music_stop();
    music_switch(SND_INTRO_MUSIC);

    int idle_every = n >> 2;
    if (idle_every <= 1)
        idle_every = 1;
    int last = intro.p.hdr.frames - 1;
    int active = 0; /* copies running, minus one */
    unsigned frame = 0;
    bool done = false;
    do {
        uint32_t deadline = intro.p.hdr.speed * (uint32_t)g_target.speed_scale / 1500 + tm_get_time();
        for (int i = 0; i <= active; i++)
            if (copies[i].frame < last) {
                next_frame(&copies[i]);
                if (i % idle_every == 0)
                    snd_idle_imuse();
            }
        if (!(g.bench & 1) || (frame & 1))
            wm_redraw(w, &r);
        frame++;
        if (frame % 5 == 0 && active < n - 1)
            active++;
        done = copies[n - 1].frame == last && snd_slot_get_param(SND_INTRO_MUSIC - SND_RES_BASE, 0x100) < 1;
        done |= g.skip != 0;
        if (!(g.bench & 1) || (frame & 1))
            wait_until(deadline);
    } while (!done);
    music_stop();
    snd_unload(SND_INTRO_MUSIC);
    anim_close(&intro);
}

/* ---- animated title (0x426ad5) ---------------------------------------------- *
 * Three 640x480 FLCs: A (5101) opens and loops while a ticker strip
 * (bitmap 0x4be) is revealed four pixels per frame along the bottom; then
 * C (5104, one-shot, with a swoosh) and B (5103, the talking loop) take
 * turns, each B a little shorter, with voice lines streamed in slot 1.
 * Every frame takes 125 ms. */

typedef struct {
    Anim a, b, c;
    GfxSurface *ticker; /* local_40 */
    GfxSurface *under;  /* local_28: the window under the ticker strip */
    int tx0, tx1, ty;   /* strip x range (inclusive) and top */
    int cur, srcx;      /* next column in the window / in the ticker */
    GfxRect r;          /* the redraw rectangle (B's frame) */
    unsigned count;     /* frames of the current part */
} Title;

typedef struct {
    uint16_t frame;
    uint8_t kind;
    uint32_t id;
} TitleCue;

enum { CUE_SFX, CUE_VOICE, CUE_VOICE_FIRST, CUE_SFX_STOP };

/* Phase A cues (by frame). */
static const TitleCue k_cues_a[] = {
    {4, CUE_SFX, SND_TICK},          {10, CUE_VOICE_FIRST, 0x10072}, {0xd, CUE_SFX, SND_TICK},
    {0x12, CUE_VOICE, 0x10086},      {0x17, CUE_SFX, SND_TICK},      {0x21, CUE_SFX, SND_TICK},
    {0x22, CUE_VOICE, 0x10073},      {0x2a, CUE_VOICE, 0x10087},     {0x2b, CUE_SFX, SND_TICK},
    {0x35, CUE_SFX, SND_TICK},       {0x3a, CUE_VOICE, 0x10074},     {0x3e, CUE_SFX, SND_TICK},
    {0x42, CUE_VOICE, 0x10088},      {0x48, CUE_SFX, SND_TICK},      {0x4c, CUE_SFX, SND_TICK},
    {0x52, CUE_VOICE, 0x10075},      {0x53, CUE_SFX, SND_TICK},      {0x5a, CUE_VOICE, 0x10089},
    {0x5c, CUE_SFX, SND_TICK},       {0x5e, CUE_SFX_STOP, SND_FX_A}, {0x68, CUE_VOICE, 0x10076},
    {0x70, CUE_VOICE, 0x1008a},      {0x84, CUE_VOICE, 0x10077},     {0x9a, CUE_SFX, SND_FX_B},
    {0x9c, CUE_VOICE, 0x1008b},      {0xac, CUE_VOICE, 0x10078},     {0xc6, CUE_VOICE, 0x1008c},
    {0xce, CUE_VOICE, 0x10079},      {0xe2, CUE_VOICE, 0x1008d},     {0xee, CUE_VOICE, 0x1007a},
};

static void cue(const TitleCue *cues, size_t n, unsigned frame)
{
    for (size_t i = 0; i < n; i++) {
        if (cues[i].frame != frame)
            continue;
        switch (cues[i].kind) {
        case CUE_SFX:
            snd_play(cues[i].id, 2);
            break;
        case CUE_SFX_STOP:
            snd_slot_stop_stream(VOICE_SLOT);
            snd_play(cues[i].id, 2);
            break;
        default:
            voice(cues[i].id, cues[i].kind == CUE_VOICE);
            break;
        }
    }
}

/* The next 4-pixel column of the ticker. */
static void ticker_column(Title *t)
{
    gfx_blit_transparent(main_surface(), t->cur, t->ty, 4, t->ticker->height, t->ticker, t->srcx, 0, 0);
    t->cur += 4;
    t->srcx += 4;
    if (t->tx1 <= t->cur)
        t->cur = t->tx0;
    if (t->ticker->width <= t->srcx)
        t->srcx = 0;
}

/* Redraws the part of the ticker revealed so far (after a full frame). */
static void ticker_revealed(Title *t)
{
    int w = t->cur - t->tx0;
    gfx_blit_transparent(main_surface(), t->tx0, t->ty, w, t->ticker->height, t->ticker, t->srcx - w, 0, 0);
}

/* Restores the window under the strip (when the ticker starts over). */
static void ticker_restore(Title *t)
{
    gfx_blit(main_surface(), t->tx0, t->ty, t->under->width, t->under->height, t->under, 0, 0);
}

static void title_frame_end(Title *t, uint32_t deadline)
{
    wm_redraw(main_window(), &t->r);
    t->count++;
    wait_until(deadline);
}

/* A run of C, the one-shot interlude. False if skipped. */
static bool title_part_c(Title *t)
{
    flc_rewind(&t->c.p);
    t->count = 0;
    snd_play(SND_SWOOSH, 2);
    do {
        uint32_t deadline = tm_get_time() + 125;
        next_frame(&t->c.p);
        t->count++;
        ticker_revealed(t);
        ticker_column(t);
        wm_redraw(main_window(), &t->r);
        if (t->count == 2)
            snd_play(SND_TICK, 2);
        wait_until(deadline);
    } while (t->count < t->c.p.hdr.frames && !g.skip);
    return !g.skip;
}

/* A run of B, the talking loop, for `last` + 1 frames with voice cues.
 * The first run takes a new copy of the window under the strip. False if
 * skipped. */
static bool title_part_b(Title *t, unsigned last, bool first, const TitleCue *cues, size_t ncues)
{
    snd_stop(SND_SWOOSH);
    if (first) {
        flc_rewind(&t->b.p);
        next_frame(&t->b.p);
        gfx_blit(t->under, 0, 0, t->under->width, t->under->height, main_surface(), t->tx0, t->ty);
    }
    flc_rewind(&t->b.p);
    t->count = 0;
    do {
        uint32_t deadline = tm_get_time() + 125;
        if (t->b.p.frame >= t->b.p.hdr.frames)
            flc_seek_ring_frame(&t->b.p);
        next_frame(&t->b.p);
        if (t->b.p.frame == 1)
            ticker_revealed(t);
        else if (t->cur == t->tx0)
            ticker_restore(t);
        ticker_column(t);
        t->count++;
        wm_redraw(main_window(), &t->r);
        cue(cues, ncues, t->count);
        wait_until(deadline);
    } while (t->count <= last && !g.skip);
    return !g.skip;
}

/* 0x426ad5 */
static void title_animate(void)
{
    static const TitleCue k_cues_b1[] = {{8, CUE_VOICE_FIRST, 0x1008e}, {0x1c, CUE_VOICE, 0x1007b}};
    static const TitleCue k_cues_b2[] = {{8, CUE_VOICE_FIRST, 0x1007c}};
    static const TitleCue k_cues_b5[] = {{0x10, CUE_VOICE_FIRST, 0x1008f}, {0x28, CUE_VOICE, 0x1007d}};
    static Title t;

    memset(&t, 0, sizeof(t));
    wm_flush_input();
    if (g.skip == 2)
        return;
    music_stop();
    snd_preload_locked(SND_TICK);
    snd_preload_locked(SND_SWOOSH);
    g.skip = 0;
    if (!anim_open(&t.a, RES_TITLE_A) || !anim_open(&t.b, RES_TITLE_B) || !anim_open(&t.c, RES_TITLE_C)) {
        g.skip = 1;
        goto out;
    }
    t.ticker = gfx_load_bmp_resource(RES_TICKER, NULL);
    if (!t.ticker) {
        g.skip = 1;
        goto out;
    }
    g.stage = TITLE_STAGE_ANIM;
    target_init();
    flc_center(&t.a.p, &g_target);
    flc_center(&t.b.p, &g_target);
    flc_center(&t.c.p, &g_target);
    int bw = t.b.p.hdr.width, bh = t.b.p.hdr.height;
    int tw = (bw - 14) - ((bw - 14) & 3);
    t.tx0 = ((bw - tw) >> 1) + t.b.p.x;
    t.tx1 = tw + t.tx0 - 1;
    t.srcx = 0;
    t.ty = t.b.p.y + 0x131;
    t.cur = t.tx0;
    t.under = gfx_surface_create(t.tx1 - t.tx0 + 1, t.ticker->height);
    if (!t.under) {
        g.skip = 1;
        goto out;
    }
    fill_main();
    wm_redraw(main_window(), NULL);
    next_frame(&t.a.p);
    gfx_blit(t.under, 0, 0, t.under->width, t.under->height, main_surface(), t.tx0, t.ty);
    pal_load(RES_TITLE_PAL, true);
    fade_in(1000);
    wm_redraw(main_window(), NULL);
    flc_rewind(&t.a.p);
    music_switch(SND_TITLE_MUSIC);
    t.r = (GfxRect){t.b.p.x, bw + t.b.p.x - 1, t.b.p.y, bh + t.b.p.y - 1};

    /* A, with the ticker starting */
    t.count = 0;
    do {
        uint32_t deadline = tm_get_time() + 125;
        if (t.a.p.frame >= t.a.p.hdr.frames)
            flc_seek_ring_frame(&t.a.p);
        next_frame(&t.a.p);
        if (t.cur == t.tx0)
            ticker_restore(&t);
        ticker_column(&t);
        cue(k_cues_a, sizeof(k_cues_a) / sizeof(k_cues_a[0]), t.count);
        title_frame_end(&t, deadline);
        /* (the original touches every page of C's data here from frame
         * 0xf2 on, and at some cues, to have it paged in) */
    } while (t.count <= 0xf9 && !g.skip);
    if (g.skip)
        goto out;

    if (!title_part_c(&t) || !title_part_b(&t, 0x2b, true, k_cues_b1, 2))
        goto out;
    if (!title_part_c(&t) || !title_part_b(&t, 0x17, false, k_cues_b2, 1))
        goto out;
    if (!title_part_c(&t) || !title_part_b(&t, 7, false, NULL, 0))
        goto out;
    if (!title_part_c(&t) || !title_part_b(&t, 3, false, NULL, 0))
        goto out;
    for (int i = 0; i < 3; i++)
        if (!title_part_c(&t))
            goto out;
    title_part_b(&t, 0x4f, false, k_cues_b5, 2);

out:
    snd_slot_stop_stream(VOICE_SLOT);
    snd_stop(SND_TITLE_MUSIC);
    snd_stop(SND_TICK);
    snd_stop(SND_SWOOSH);
    snd_unload(SND_TITLE_MUSIC);
    snd_unload(SND_TICK);
    snd_unload(SND_SWOOSH);
    anim_close(&t.a);
    anim_close(&t.b);
    anim_close(&t.c);
    if (t.ticker)
        gfx_surface_destroy(t.ticker);
    if (t.under)
        gfx_surface_destroy(t.under);
    t.ticker = t.under = NULL;
}

/* ---- menus (0x428b22-0x429414, 0x429ae6) ----------------------------------- */

static const char *const k_main_labels[MAIN_BUTTONS] = {
    "Start a New Game", "Load a Game", "Load a Scenario", "Replay Cheesy Intro", "Quit Afterlife",
};
static const char *const k_diff_labels[DIFF_BUTTONS] = {"Easy", "Medium", "Hard", "Return to Main Menu"};

/* 0x4291e3 (main) / 0x4292fb (difficulty): shows or hides a menu's
 * buttons; showing draws them (the focused one with its frame) and the
 * decoration next to each. */
static void menu_show(WmWindow **btn, int n, uint8_t focus, bool show)
{
    for (int i = 0; i < n; i++) {
        if (!btn[i])
            continue;
        ctl_button_show(btn[i], show);
        if (!show)
            continue;
        ctl_button_draw(btn[i], focus == i ? CTL_DRAW_BITMAP | CTL_DRAW_FLUSH | CTL_STATE_FOCUS
                                           : CTL_DRAW_BITMAP | CTL_DRAW_FLUSH);
        if (g.deco)
            gfx_blit_transparent(main_surface(), btn[i]->rect.left - 0x12, btn[i]->rect.top - 0xb, g.deco->width,
                                 g.deco->height, g.deco, 0, 0, 0);
    }
    if (show)
        wm_redraw(main_window(), NULL);
}

static void menu_show_main(bool show) { menu_show(g.main_btn, MAIN_BUTTONS, g.main_focus, show); }       /* 0x4291e3 */
static void menu_show_difficulty(bool show) { menu_show(g.diff_btn, DIFF_BUTTONS, g.diff_focus, show); } /* 0x4292fb */

static WmWindow *menu_button(uint32_t id, int x, int y, const char *label)
{
    GfxRect r = {x, x + 0x145, y, y + 0x2b};
    WmWindow *b = ctl_button_create(main_window(), &r, id, CTL_STYLE_BITMAP, gfx_load_bmp_resource(RES_BUTTON, NULL));
    if (b)
        ctl_button_set_label(b, label, FONT_BIG, LABEL_COLORS, false);
    return b;
}

/* where 0x428d28 puts the buttons: along the top and bottom edges */
static void main_positions(int right, int bottom, int pos[MAIN_BUTTONS][2])
{
    const int p[MAIN_BUTTONS][2] = {
        {0x16, 0x13},
        {(right >> 1) - 0x49, 0x13},
        {right - 0xab, 0x13},
        {(right >> 2) - 0x51, bottom - 0x3f},
        {(right >> 2) + (right >> 1) - 0x50, bottom - 0x3f},
    };
    memcpy(pos, p, sizeof p);
}

static void diff_positions(int right, int bottom, int pos[DIFF_BUTTONS][2])
{
    const int p[DIFF_BUTTONS][2] = {
        {0x16, 0x13},
        {(right >> 1) - 0x49, 0x13},
        {right - 0xab, 0x13},
        {(right >> 1) - 0x49, bottom - 0x3f},
    };
    memcpy(pos, p, sizeof p);
}

/* 0x428d28: creates the main menu's buttons, and the difficulty menu's
 * when it's the current one, unless they exist (hidden). */
static void menu_create(void)
{
    WmWindow *w = main_window();
    int right = w->client.right, bottom = w->client.bottom;
    if (!g.deco) /* (the original reloads it every time) */
        g.deco = gfx_load_bmp_resource(RES_MENU_DECO, NULL);
    bool exist = false;
    for (int i = 0; i < MAIN_BUTTONS; i++)
        exist |= g.main_btn[i] != NULL;
    if (!exist) {
        int pos[MAIN_BUTTONS][2];
        main_positions(right, bottom, pos);
        for (int i = 0; i < MAIN_BUTTONS; i++)
            g.main_btn[i] = menu_button((uint32_t)(TITLE_BTN_NEW + i), pos[i][0], pos[i][1], k_main_labels[i]);
        g.main_focus = 0;
        if (g.main_btn[0])
            ctl_button_set_focus(g.main_btn[0], true);
        menu_show_main(false);
    }
    if (g.menu != 3)
        return;
    for (int i = 0; i < DIFF_BUTTONS; i++)
        if (g.diff_btn[i])
            return;
    int pos[DIFF_BUTTONS][2];
    diff_positions(right, bottom, pos);
    for (int i = 0; i < DIFF_BUTTONS; i++)
        g.diff_btn[i] = menu_button((uint32_t)(TITLE_BTN_EASY + i), pos[i][0], pos[i][1], k_diff_labels[i]);
    g.diff_focus = 0;
    if (g.diff_btn[0])
        ctl_button_set_focus(g.diff_btn[0], true);
    menu_show_difficulty(false);
}

/* 0x429414: hides and destroys both menus and the decoration. */
static void menu_destroy(void)
{
    menu_show_main(false);
    menu_show_difficulty(false);
    if (g.deco)
        gfx_surface_destroy(g.deco);
    g.deco = NULL;
    for (int i = 0; i < MAIN_BUTTONS; i++)
        if (g.main_btn[i]) {
            ctl_button_destroy(g.main_btn[i]);
            g.main_btn[i] = NULL;
        }
    for (int i = 0; i < DIFF_BUTTONS; i++)
        if (g.diff_btn[i]) {
            ctl_button_destroy(g.diff_btn[i]);
            g.diff_btn[i] = NULL;
        }
}

/* 0x429ae6: moves the keyboard focus to the next (forward) or previous
 * button of the current menu, wrapping around. */
static void menu_move_focus(bool forward)
{
    WmWindow **btn;
    uint8_t *focus;
    int n;
    if (g.menu == 1) {
        btn = g.main_btn, focus = &g.main_focus, n = MAIN_BUTTONS;
    } else if (g.menu == 3) {
        btn = g.diff_btn, focus = &g.diff_focus, n = DIFF_BUTTONS;
    } else {
        return;
    }
    if (btn[*focus])
        ctl_button_set_focus(btn[*focus], false);
    int i = *focus + (forward ? 1 : -1);
    if (i >= n)
        i = 0;
    else if (i < 0)
        i = n - 1;
    *focus = (uint8_t)i;
    if (btn[i])
        ctl_button_set_focus(btn[i], true);
}

/* 0x428b22: the menu loop. Shows the background (fading in, up to 5 s
 * before the buttons), then the current menu, and pumps until
 * title_wnd_proc ends the wait; switching between the main and the
 * difficulty menu goes round again. */
static void title_menu(void)
{
    music_stop();
    music_switch(SND_MENU_MUSIC);
    for (;;) {
        wm_flush_input();
        GfxSurface *bg = gfx_load_bmp_resource(RES_MENU_BMP, NULL);
        if (!bg) {
            g.skip = 2;
            return;
        }
        g.stage = TITLE_STAGE_MENU_BG;
        fill_main();
        blit_centred(main_surface(), bg);
        pal_load(RES_MENU_PAL, false);
        fade_in(1000);
        wm_redraw(main_window(), NULL);
        uint32_t t0 = tm_get_time();
        while (g.skip == 0 && tm_get_time() < t0 + 5000)
            pump();
        if (g.menu == 0)
            g.menu = 1;
        gfx_surface_destroy(bg);
        pal_load(RES_MENU_PAL, true);
        wm_redraw(main_window(), NULL);
        menu_create();
        if (g.menu == 1)
            menu_show_main(true);
        else if (g.menu == 3)
            menu_show_difficulty(true);
        if (g.leave_menu)
            gfx_cursor_show(true);
        g.leave_menu = 1;
        g.stage = g.menu == 3 ? TITLE_STAGE_DIFFICULTY : TITLE_STAGE_MENU;
        do
            pump();
        while (g.waiting);
        if (g.leave_menu)
            break;
        g.again = 0;
        g.waiting = 1;
        g.quit = 0;
        menu_show_main(false);
        menu_show_difficulty(false);
    }
    music_stop();
    snd_unload(SND_MENU_MUSIC);
}

/* ---- enter / leave ------------------------------------------------------------ */

/* 0x42655a: takes over the main window. Port: also resets the menu state
 * so that title_run can run more than once. */
static void title_enter(void)
{
    gfx_cursor_show(true);
    g.skip = 0;
    g.old_proc = wm_set_proc(main_window(), title_wnd_proc);
    wm_show(main_window(), true);
    wm_raise(main_window());
    wm_activate(main_window());
    memset(g.main_btn, 0, sizeof(g.main_btn));
    memset(g.diff_btn, 0, sizeof(g.diff_btn));
    g.menu = 0;
    g.leave_menu = 1;
}

/* 0x4298e5: stops and unloads every title sound, destroys the menus,
 * clears the window, sets the game palette (resource 0x21), gives the
 * window its procedure back and marks the title as over. */
static void title_leave(void)
{
    music_stop();
    static const uint32_t k_sounds[] = {SND_INTRO_MUSIC, SND_TITLE_MUSIC, SND_MENU_MUSIC,
                                        SND_FX_B,        SND_FX_A,        SND_TICK, SND_SWOOSH};
    for (size_t i = 0; i < sizeof(k_sounds) / sizeof(k_sounds[0]); i++) {
        snd_stop(k_sounds[i]);
        snd_unload(k_sounds[i]);
    }
    for (uint32_t id = 0x10072; id < 0x1007e; id++) {
        snd_stop(id);
        snd_unload(id);
    }
    for (uint32_t id = 0x10086; id < 0x10090; id++) {
        snd_stop(id);
        snd_unload(id);
    }
    free(g.pal);
    g.pal = NULL;
    menu_destroy();
    fill_main();
    wm_redraw(main_window(), NULL);
    /* the original decodes the game palette again (0x638658 =
     * pal_load_resource(0x5c5c0c)) */
    size_t len = 0;
    uint8_t *raw = load_resource(RES_GAME_PAL, &len);
    uint8_t *pal = raw ? gfx_palette_load_resource(raw, len) : NULL;
    if (pal)
        gfx_palette_set(pal, GFX_PAL_FIRST, GFX_PAL_COUNT);
    free(pal);
    free(raw);
    wm_redraw(main_window(), NULL);
    wm_set_proc(main_window(), g.old_proc);
    wm_flush_input();
    g_game_in_progress = 1;
    g.stage = TITLE_STAGE_NONE;
}

/* ---- entry --------------------------------------------------------------------- */

/* 0x42a6c3 */
unsigned di_video_benchmark(void)
{
    int flags = 0;
    if (cfg_video_settings(&flags))
        return (unsigned)flags & 0xff;
    return 0;
}

/* Port-only (the port window, game/display.c): the main window has a new
 * size; puts the menu background back in the middle and the buttons along
 * the new edges, as 0x428b22 / 0x428d28 would have for this size. */
void title_relayout(void)
{
    if (!g_main_window || !g_main_window->surface || g_holds_surface || g.stage == TITLE_STAGE_NONE)
        return;
    fill_main();
    GfxSurface *bg = gfx_load_bmp_resource(RES_MENU_BMP, NULL);
    if (bg) {
        blit_centred(main_surface(), bg);
        gfx_surface_destroy(bg);
    }
    WmWindow *w = main_window();
    int right = w->client.right, bottom = w->client.bottom;
    int mp[MAIN_BUTTONS][2], dp[DIFF_BUTTONS][2];
    main_positions(right, bottom, mp);
    diff_positions(right, bottom, dp);
    bool main_shown = false, diff_shown = false;
    for (int i = 0; i < MAIN_BUTTONS; i++)
        if (g.main_btn[i]) {
            rect_move_to(&g.main_btn[i]->rect, mp[i][0], mp[i][1]);
            main_shown |= (g.main_btn[i]->flags & WM_F_VISIBLE) != 0;
        }
    for (int i = 0; i < DIFF_BUTTONS; i++)
        if (g.diff_btn[i]) {
            rect_move_to(&g.diff_btn[i]->rect, dp[i][0], dp[i][1]);
            diff_shown |= (g.diff_btn[i]->flags & WM_F_VISIBLE) != 0;
        }
    if (main_shown)
        menu_show_main(true);
    if (diff_shown)
        menu_show_difficulty(true);
}

/* 0x426460 */
int title_run(void)
{
    if (!g_main_window || !g_main_window->surface)
        return 0;
    title_enter();
    g.bench = di_video_benchmark();
    g.skip = 0;
    do {
        wm_flush_input();
        g.again = 0;
        g.waiting = 1;
        g.quit = 0;
        gfx_cursor_show(false);
        menu_show_main(false);
        menu_show_difficulty(false);
        g_holds_surface = true;
        intro_play();
        fade_out(1000);
        title_animate();
        fade_out(1000);
        g_holds_surface = false;
        title_menu();
        if (g.again) {
            fill_main();
            wm_redraw(main_window(), NULL);
        }
    } while (g.again);
    gfx_cursor_show(false);
    title_leave();
    return g.quit;
}

/* The quit box of 0x7d4 and the close request. */
static void ask_quit(void)
{
    if (ctl_message_box(-1, -1, NULL, "Do You Really Want To Quit?", "&Yes\0~&No\0", 0) == 1) {
        g.waiting = 0;
        g.quit = 1;
        g.again = 0;
    }
}

/* 0x429c4d. Control notifications arrive with w = the button. */
intptr_t title_wnd_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_LBUTTONUP:
        g.skip = 1;
        break;
    case WM_MSG_CTL_PRESS: /* a button went down: it takes the focus */
        if (g.menu == 1) {
            if (g.main_btn[g.main_focus])
                ctl_button_set_focus(g.main_btn[g.main_focus], false);
            g.main_focus = (uint8_t)(a - TITLE_BTN_NEW);
        } else if (g.menu == 3) {
            if (g.diff_btn[g.diff_focus])
                ctl_button_set_focus(g.diff_btn[g.diff_focus], false);
            g.diff_focus = (uint8_t)(a - TITLE_BTN_EASY);
        }
        ctl_button_set_focus(w, true);
        break;
    case WM_MSG_KEYDOWN:
        if (a == PLAT_VK_RETURN) {
            if (g.menu == 1 && g.main_btn[g.main_focus])
                ctl_button_set_pressed(g.main_btn[g.main_focus], true, true);
            else if (g.menu == 3 && g.diff_btn[g.diff_focus])
                ctl_button_set_pressed(g.diff_btn[g.diff_focus], true, true);
        }
        break;
    case WM_MSG_CTL_CLICK:
        switch (a) {
        case TITLE_BTN_NEW:
            g.skip = 2;
            g.waiting = 0;
            g.quit = 0;
            g.again = 1;
            g.menu = 3;
            g.leave_menu = 0;
            break;
        case TITLE_BTN_LOAD:
        case TITLE_BTN_SCENARIO:
            g.waiting = !shell_choose_load(a == TITLE_BTN_LOAD ? 1 : 2);
            g.quit = 0;
            g.again = 0;
            break;
        case TITLE_BTN_INTRO:
            g.waiting = 0;
            g.quit = 0;
            g.again = 1;
            g.skip = 0;
            g.menu = 0;
            break;
        case TITLE_BTN_QUIT:
            ask_quit();
            break;
        case TITLE_BTN_EASY:
        case TITLE_BTN_MEDIUM:
        case TITLE_BTN_HARD:
            g_state.hdr.difficulty = (uint16_t)(a - (TITLE_BTN_EASY - 1));
            g.waiting = 0;
            g.quit = 0;
            g.again = 0;
            break;
        case TITLE_BTN_BACK:
            menu_show_difficulty(false);
            g.waiting = 0;
            g.quit = 0;
            g.again = 1;
            g.skip = 2;
            g.menu = 1;
            g.leave_menu = 0;
            break;
        }
        break;
    case WM_MSG_KEYUP:
        switch (a) {
        case PLAT_VK_TAB:
            menu_move_focus(!wm_shift_down());
            break;
        case PLAT_VK_RETURN:
            if (g.menu == 1 && g.main_btn[g.main_focus]) {
                ctl_button_set_pressed(g.main_btn[g.main_focus], false, true);
                wm_post(w, g.main_btn[g.main_focus], WM_MSG_CTL_CLICK, g.main_focus + TITLE_BTN_NEW, 0);
            } else if (g.menu == 3 && g.diff_btn[g.diff_focus]) {
                ctl_button_set_pressed(g.diff_btn[g.diff_focus], false, true);
                wm_post(w, g.diff_btn[g.diff_focus], WM_MSG_CTL_CLICK, g.diff_focus + TITLE_BTN_EASY, 0);
            } else {
                g.skip = 1;
            }
            break;
        case PLAT_VK_ESCAPE:
            g.skip = 2;
            break;
        case PLAT_VK_LEFT:
            menu_move_focus(false);
            break;
        case PLAT_VK_RIGHT:
            menu_move_focus(true);
            break;
        default:
            g.skip = 1;
            break;
        }
        break;
    case WM_MSG_CLOSE_BUTTON:
    case WM_MSG_CLOSE:
        ask_quit();
        break;
    }
    return wm_default_proc(w, msg, a, b);
}
