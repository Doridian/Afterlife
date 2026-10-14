/*
 * The talking heads (ALIFE.EXE 0x4d2c50-0x4d3e71): up to three AKOS actors
 * shown in a window, each with a subtitle area, driven by a timer and by
 * lip-sync markers in the voice stream. The advisor window ("Jasper and
 * Aria", 0x417230 / 0x417b94 / 0x425e3b) uses talker 1 for Jasper (costume
 * 2003) and talker 2 for Aria (2004). See docs/formats/akos.md.
 *
 * Talker n draws and animates AKOS actor n. The original keeps talker
 * records at 0x54f158 + n * 0x68 (n = 0..3, 0 unused).
 */
#ifndef AFTERLIFE_GFX_AKOS_TALK_H
#define AFTERLIFE_GFX_AKOS_TALK_H

#include <stdint.h>

#include "gfx/surface.h"
#include "ui/wm.h"

#define AKOS_TALKERS 4 /* records 0..3; open/place/anim accept 1..3 */

/* Window messages the talker sends to its own window (wm_send(win, win,
 * msg, talker, value)). */
#define AKOS_TALK_MSG_MARKER 6000 /* a lip-sync marker: value = its number */
#define AKOS_TALK_MSG_IDLE 6001   /* the head has been still for 7 steps */

/* The voice stream's sound slot (snd_slot_stream). */
#define AKOS_TALK_VOICE_SLOT 199

typedef struct {
    WmWindow *win;      /* +0x00 */
    int32_t x, y;       /* +0x04 / +0x08 actor position in the window */
    int32_t direction;  /* +0x0c */
    int32_t costume;    /* +0x10 costume slot */
    uint8_t text_color; /* +0x14 */
    uint8_t bg_color;   /* +0x15 */
    uint8_t scale_x;    /* +0x16 */
    uint8_t scale_y;    /* +0x17 */
    /* +0x18 the drawn area: kept as AKOS actor n's bounds (akos_get_bounds) */
    GfxRect text_rect;      /* +0x28 */
    int32_t anim;           /* +0x38 current animation, 0 = not animating */
    uint32_t period;        /* +0x3c ms per step (1000 / fps) */
    uint32_t countdown;     /* +0x40 */
    int32_t drawn_anim;     /* +0x44 */
    uint32_t last_time;     /* +0x48 */
    uint32_t voice;         /* +0x4c voice resource while speaking */
    const char *text;       /* +0x50 subtitle, NULL = none */
    uint32_t text_hold;     /* +0x54 steps left before the text may go */
    int32_t text_x, text_y; /* +0x58 / +0x5c */
    int32_t unk60;          /* +0x60 set to 0x20, never read */
    int32_t marker;         /* +0x64 pending lip-sync marker, -1 = none */
} AkosTalker;

/* 0x4d2c50: clears the records and registers akos_talk_idle as a task. */
int akos_talk_initialize(void);
/* 0x4d2c91 */
int akos_talk_shutdown(void);
/* Port-only: talker n (0..3) or NULL. */
AkosTalker *akos_talk(int n);

/* 0x4d2cb0: sets the subtitle font; returns the previous one (0 when id is
 * 0, which changes nothing). */
uint32_t akos_talk_set_font(uint32_t id);

/* 0x4d2ce7: binds talker n (1..3) to a window and a costume slot, builds
 * the colour remap from the current palette, turns it to direction 2 and
 * starts animation 1. fps sets the animation rate. -1 if n is invalid. */
int akos_talk_open(int n, WmWindow *win, int costume, uint8_t scale_x, uint8_t scale_y, uint8_t text_color,
                   uint8_t bg_color, unsigned fps);
/* 0x4d2eae */
void akos_talk_close(int n);
uint8_t akos_talk_text_color(int n); /* 0x4d2f87 */
uint8_t akos_talk_bg_color(int n);   /* 0x4d2fa9 */
/* 0x4d3149: moves talker n to (x, y) (and to `win` unless NULL), redraws. */
int akos_talk_place(int n, int x, int y, WmWindow *win);
/* 0x4d31fd: starts animation `anim` in the talker's direction. */
int akos_talk_set_anim(int n, int anim);
/* 0x4d3297: direction 0..3 (applied through animation AKOS_ANIM_TURN). */
int akos_talk_set_direction(int n, int dir);

/* 0x4d2fcb: draws the actor into the window's surface (no repaint). */
void akos_talk_draw(int n);
/* 0x4d30e6: fills the drawn area with `color`. */
void akos_talk_erase(int n, uint8_t color);

/* 0x4d3b8a (snd_talk_start): shows `text` in `rect` of the window and, if
 * voice is non-zero, streams that resource as the voice with a lip-sync
 * trigger. */
void akos_talk_say(int n, const GfxRect *rect, const char *text, uint32_t voice);
/* 0x4d385c: blocks (pumping the system) until the voice has finished,
 * then removes the text. */
void akos_talk_wait(int n);
/* 0x4d3a23: erases the subtitle and forgets it. */
void akos_talk_clear_text(int n);
/* 0x4d3e49 (snd_voice_set_trigger): re-arms the one-shot lip-sync
 * trigger on the voice for talker n (the window does this on each marker). */
void akos_talk_arm_lipsync(int n);
/* 0x4d3e71: drops a pending marker. */
void akos_talk_clear_marker(int n);

/* 0x4d35ac: the task's idle callback. Delivers lip-sync markers, steps
 * each animating talker at its rate, redraws it when it moved, sends
 * AKOS_TALK_MSG_IDLE after 7 still steps, and ages the subtitle. */
void akos_talk_idle(void);

/* Port-only: what akos_talk_wait runs between pumps (the original calls
 * game_frame_update, 0x4d385c). NULL by default. */
extern void (*akos_talk_frame_hook)(void);

#endif
