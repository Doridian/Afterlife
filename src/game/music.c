/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The in-game music (game/music.h). */
#include "game/music.h"

#include "game/sound.h"
#include "game/util.h"
#include "game/world.h"
#include "imuse/imuse.h"
#include "ui/wm.h"

static uint32_t g_current;  /* 0x66439c */
static uint32_t g_previous; /* 0x6643a0 */
static int g_delay = 0xf0;  /* 0x6643b0: clock ticks (125 ms) of silence after a track */

uint32_t music_current(void) { return g_current; }

/* 0x4752c2 */
uint32_t music_pick(uint32_t previous)
{
    uint32_t track = 0;
    uint8_t bad = g_state.unk_5b2f61; /* 0x5b2f61: Bad Things in progress */
    if (g_state.unk_5f4cd0[0][0]) /* 0x5f4cd0 */
        track = 0x10052;
    else if (bad & 0x80)
        track = 0x10053;
    else if (bad & 0x40)
        track = 0x1006d;
    else if (bad & 0x10)
        track = 0x10055;
    else if (bad & 0x20)
        track = 0x1006c;
    else if (bad & 8)
        track = 0x1006e;
    else if (bad & 4)
        track = 0x10054;
    if (track == 0) {
        do
            track = (uint32_t)rand_range(0x10039, 0x1003f);
        while (track == previous);
    }
    /* no music in the attract mode */
    if (snd_music_enabled && wm_input_live()) {
        g_current = track;
        snd_play(track, 4);
    }
    return track;
}

/* 0x475607 */
int music_tick(int countdown)
{
    if (g_current == 0) {
        if (snd_slot_get_param(SND_VOICE_SLOT, IMUSE_P_PLAYING) < 1) {
            imuse_pause();
            imuse_resume();
        }
        g_current = music_pick(g_previous);
    } else if (g_current < SND_RES_BASE || g_current > 99999) {
        g_previous = g_current;
        g_current = 0;
        countdown = g_delay;
    } else {
        countdown = snd_slot_get_param((int)(g_current - SND_RES_BASE), IMUSE_P_PLAYING);
        if (countdown < 1) {
            snd_stop(g_current);
            g_previous = g_current;
            g_current = 0;
            countdown = g_delay;
        }
    }
    return countdown;
}

/* 0x475569 */
void music_stop(void)
{
    snd_stop(g_current);
    g_current = 0;
}

/* 0x4753f2 snd_fade_out_music */
void music_fade_out(int ms)
{
    if (g_current)
        imuse_fade_param((int)(g_current - SND_RES_BASE), IMUSE_P_VOLUME, 0, ms);
}

/* 0x4754d7 */
void music_switch(uint32_t id)
{
    if (!snd_music_enabled) {
        g_current = id;
        return;
    }
    if (g_current)
        snd_stop(g_current);
    g_current = id;
    snd_play(id, 4);
}
