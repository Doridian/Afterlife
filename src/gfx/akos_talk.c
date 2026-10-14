/*
 * The talking heads (ALIFE.EXE 0x4d2c50-0x4d3e71). See akos_talk.h.
 *
 * Quirk kept from the original: akos_talk_open and akos_talk_set_anim pass
 * the costume slot, not the talker number, as the AKOS actor to reset and
 * animate, while drawing and stepping use the talker number. The game loads
 * Jasper's costume first and Aria's second, so slot and talker coincide.
 */
#include "gfx/akos_talk.h"

#include <string.h>

#include "game/sound.h"
#include "game/startup.h"
#include "gfx/akos.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/palette.h"
#include "imuse/imuse.h"
#include "platform/platform.h"

static AkosTalker g_talk[AKOS_TALKERS]; /* 0x54f158 */
static uint32_t g_still[AKOS_TALKERS];  /* 0x54ed20 */
static uint32_t g_font;                 /* 0x674444 */

void (*akos_talk_frame_hook)(void);

static bool valid(int n) { return n >= 1 && n <= 3; }

AkosTalker *akos_talk(int n) { return n >= 0 && n < AKOS_TALKERS ? &g_talk[n] : NULL; }

static void shutdown_task(void) { akos_talk_shutdown(); }

/* The original passes NULL windows straight to the WM; the port skips. */
static void redraw(const AkosTalker *t, const GfxRect *r)
{
    if (t->win)
        wm_redraw(t->win, r);
}

/* 0x4d2c50 */
int akos_talk_initialize(void)
{
    sys_register_task(akos_talk_initialize, shutdown_task, NULL, NULL, akos_talk_idle);
    memset(g_talk, 0, sizeof g_talk);
    return 0;
}

/* 0x4d2c91 */
int akos_talk_shutdown(void)
{
    sys_unregister_task(akos_talk_initialize);
    return 0;
}

/* 0x4d2cb0 */
uint32_t akos_talk_set_font(uint32_t id)
{
    uint32_t old = g_font;
    if (id == 0)
        return 0;
    g_font = id;
    return old;
}

/* 0x4d33e3: fills [left, right) x [top, bottom) of `rect`, which is first
 * clamped to the surface in place. Nothing happens when the rectangle
 * starts right of or below the surface. */
static void fill_rect(GfxSurface *s, GfxRect *r, uint8_t color)
{
    if (!s || !s->pixels || r->left >= s->width || r->top >= s->height)
        return;
    if (r->left < 0)
        r->left = 0;
    if (r->top < 0)
        r->top = 0;
    if (r->right > s->width - 1)
        r->right = s->width - 1;
    if (r->bottom > s->height - 1)
        r->bottom = s->height - 1;
    if (r->right > r->left && r->bottom > r->top)
        gfx_fill_rect(s, r->left, r->top, r->right - r->left, r->bottom - r->top, color);
}

/* 0x4d2ce7 */
int akos_talk_open(int n, WmWindow *win, int costume, uint8_t scale_x, uint8_t scale_y, uint8_t text_color,
                   uint8_t bg_color, unsigned fps)
{
    if (!valid(n) || fps == 0)
        return -1;
    AkosTalker *t = &g_talk[n];
    t->costume = costume;
    t->text_color = text_color;
    t->bg_color = bg_color;
    t->win = win;
    t->scale_x = scale_x;
    t->scale_y = scale_y;
    t->period = t->countdown = 1000u / fps;
    t->last_time = plat_ticks_ms();
    t->marker = -1;
    akos_set_costume(n, costume);
    uint8_t rgbx[256 * 4];
    gfx_palette_get(rgbx, 0, 256); /* di_get_palette into 0x54ed40 */
    akos_build_remap(n, rgbx, 0xff, 0xff, 0xff);
    akos_reset(costume); /* quirk: actor = costume slot (0x4d2ad1) */
    akos_talk_set_direction(n, 2);
    t->anim = 1;
    akos_set_anim(costume, 1, t->direction);
    return 0;
}

/* 0x4d2eae */
void akos_talk_close(int n)
{
    AkosTalker *t = akos_talk(n);
    if (!t)
        return;
    t->anim = 0;
    t->costume = 0;
    t->text_color = t->bg_color = 0;
    t->win = NULL;
    t->scale_x = t->scale_y = 0;
    t->period = t->countdown = 0;
}

uint8_t akos_talk_text_color(int n) { return akos_talk(n) ? g_talk[n].text_color : 0; }
uint8_t akos_talk_bg_color(int n) { return akos_talk(n) ? g_talk[n].bg_color : 0; }

/* 0x4d2fcb. Without a window the original draws with whatever the shared
 * parameter block last held; the port draws nothing. */
void akos_talk_draw(int n)
{
    AkosTalker *t = akos_talk(n);
    if (!t || !t->win || !t->win->surface)
        return;
    akos_draw_actor(n, t->win->surface, t->x, t->y, t->scale_x, t->scale_y);
}

/* 0x4d30e6 */
void akos_talk_erase(int n, uint8_t color)
{
    AkosTalker *t = akos_talk(n);
    if (!t || !t->win)
        return;
    AkosActor *a = akos_actor(n);
    if (a)
        fill_rect(t->win->surface, &a->bounds, color);
}

/* 0x4d3149 */
int akos_talk_place(int n, int x, int y, WmWindow *win)
{
    if (!valid(n))
        return -1;
    AkosTalker *t = &g_talk[n];
    t->x = x;
    t->y = y;
    if (win)
        t->win = win;
    akos_reset_bounds(n);
    akos_talk_draw(n);
    return 0;
}

/* 0x4d31fd */
int akos_talk_set_anim(int n, int anim)
{
    if (!valid(n) || !g_talk[n].win)
        return -1;
    AkosTalker *t = &g_talk[n];
    t->anim = anim;
    akos_set_anim(t->costume, anim, t->direction); /* quirk: actor = costume slot */
    return 0;
}

/* 0x4d3297 */
int akos_talk_set_direction(int n, int dir)
{
    if (!valid(n) || dir < 0 || dir > 3)
        return -1;
    g_talk[n].direction = dir;
    akos_talk_set_anim(n, AKOS_ANIM_TURN);
    return 0;
}

/* 0x4d3a23 */
void akos_talk_clear_text(int n)
{
    AkosTalker *t = akos_talk(n);
    if (!t)
        return;
    if (t->win)
        fill_rect(t->win->surface, &t->text_rect, t->bg_color);
    t->text = NULL;
}

/* 0x4d3aa4: draws the subtitle into `rect` (font colours 1 and 2 = the
 * text colour, no justification) and remembers the rectangle. */
static void draw_text(int n, GfxRect *rect)
{
    AkosTalker *t = &g_talk[n];
    if (!t->text)
        return;
    gfx_font_select(g_font);
    unsigned colors[2] = {t->text_color, t->text_color};
    gfx_font_set_colors(g_font, 1, 2, colors);
    if (t->win && t->win->surface)
        gfx_font_draw_text(t->win->surface, rect->left, rect->top, rect, rect, t->text, 0x10);
    t->text_rect = *rect;
}

/* 0x4d3d00 (snd_lipsync_marker_cb): the marker text is "MK nn" or similar;
 * the number is the first run of digits after the first two characters.
 * args[1] is the talker. */
static void lipsync_marker(const char *marker, const intptr_t args[10])
{
    int n = (int)args[1];
    AkosTalker *t = akos_talk(n);
    if (!t)
        return;
    unsigned v = 0;
    const char *p = marker && marker[0] && marker[1] ? marker + 2 : "";
    while (*p && (*p < '0' || *p > '9'))
        p++;
    while (*p >= '0' && *p <= '9')
        v = (v * 10 + (unsigned)(*p++ - '0')) & 0xffff;
    t->marker = (int32_t)v;
}

/* 0x4d3b8a */
void akos_talk_say(int n, const GfxRect *rect, const char *text, uint32_t voice)
{
    AkosTalker *t = akos_talk(n);
    if (!t || !rect)
        return;
    t->text_rect = *rect;
    akos_talk_clear_text(n);
    redraw(t, &t->text_rect);
    t->voice = voice;
    t->text = text;
    t->text_x = rect->left;
    t->text_y = rect->top;
    t->text_hold = text ? (uint32_t)strlen(text) / 10 : 0;
    t->unk60 = 0x20;
    GfxRect r = *rect;
    draw_text(n, &r);
    redraw(t, &r);
    if (voice) {
        snd_slot_stream(voice, AKOS_TALK_VOICE_SLOT, 0x7f, 2);
        intptr_t args[10] = {(intptr_t)voice, n};
        imuse_set_trigger(AKOS_TALK_VOICE_SLOT, NULL, IMUSE_OP_CALLBACK, lipsync_marker, args);
    }
}

/* 0x4d3e49 */
void akos_talk_arm_lipsync(int n)
{
    intptr_t args[10] = {AKOS_TALK_VOICE_SLOT, n};
    imuse_set_trigger(AKOS_TALK_VOICE_SLOT, NULL, IMUSE_OP_CALLBACK, lipsync_marker, args);
}

/* 0x4d3e71 */
void akos_talk_clear_marker(int n)
{
    AkosTalker *t = akos_talk(n);
    if (t)
        t->marker = -1;
}

/* 0x4d385c. Parameter 0x100 of the voice slot is what the original polls
 * for "still playing". */
void akos_talk_wait(int n)
{
    AkosTalker *t = akos_talk(n);
    if (!t || !t->text)
        return;
    int left = snd_slot_get_param(AKOS_TALK_VOICE_SLOT, 0x100);
    while (t->voice != 0 && left > 0) {
        sys_pump();
        if (akos_talk_frame_hook)
            akos_talk_frame_hook();
        left = snd_slot_get_param(AKOS_TALK_VOICE_SLOT, 0x100);
    }
    t->voice = 0;
    akos_talk_clear_text(n);
    redraw(t, &t->text_rect);
}

/* 0x4d3937: the subtitle stays for strlen / 10 redraws, and after that
 * until the voice has stopped. Text without a voice stays up. */
static void text_tick(int n)
{
    AkosTalker *t = &g_talk[n];
    if (!t->text)
        return;
    if (t->text_hold != 0) {
        t->text_hold--;
        return;
    }
    if (t->voice != 0 && snd_slot_get_param(AKOS_TALK_VOICE_SLOT, 0x100) < 1) {
        t->voice = 0;
        akos_talk_clear_text(n);
        redraw(t, &t->text_rect);
    }
}

/* 0x4d35ac */
void akos_talk_idle(void)
{
    uint32_t now = plat_ticks_ms();
    for (int n = 0; n < AKOS_TALKERS; n++) {
        AkosTalker *t = &g_talk[n];
        if (t->marker > 0) {
            if (t->win)
                wm_send(t->win, t->win, AKOS_TALK_MSG_MARKER, n, t->marker);
            t->marker = -1;
        }
        if (t->anim == 0)
            continue;
        if (now - t->last_time < t->period && t->anim == t->drawn_anim) {
            t->countdown--;
            continue;
        }
        t->countdown = t->period;
        t->last_time = now;
        bool moved = akos_increase_anims(n);
        if (!moved && t->anim == t->drawn_anim) {
            if (++g_still[n] > 6) {
                if (t->win)
                    wm_send(t->win, t->win, AKOS_TALK_MSG_IDLE, n, 0);
                g_still[n] = 0;
            }
        } else {
            g_still[n] = 0;
            t->drawn_anim = t->anim;
            akos_talk_erase(n, t->bg_color);
            akos_reset_bounds(n);
            akos_talk_draw(n);
            GfxRect b = akos_get_bounds(n);
            redraw(t, &b);
            text_tick(n);
        }
    }
}
