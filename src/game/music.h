/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The in-game music (ALIFE.EXE 0x4752c2-0x475607, part of the game sound
 * API next to snd_play): one streamed track at a time (resources
 * 0x10039-0x1003f at random, or the theme of a Bad Thing in progress),
 * the next one picked when it ends. game_frame_update calls music_tick
 * every 8th 125 ms clock tick.
 */
#ifndef AFTERLIFE_GAME_MUSIC_H
#define AFTERLIFE_GAME_MUSIC_H

#include <stdint.h>

/* 0x4752c2: picks the next track (not `previous`) and starts it if music
 * is on. Returns it. */
uint32_t music_pick(uint32_t previous);
/* 0x475607: checks the current track (0x66439c) and starts the next one
 * when it has ended; returns the clock countdown to the next check. */
int music_tick(int countdown);
/* 0x4754d7: switches to track `id` (stops the current one; streams the new
 * one if music is on). */
void music_switch(uint32_t id);
/* 0x475569: stops the current track. */
void music_stop(void);
/* 0x47558c: the Audio Settings' Music toggle off: snd_music_enabled 0,
 * the current track stops. */
void music_off(void);
/* 0x4755b9: on again: snd_music_enabled 1 and a new track. */
void music_on(void);
/* 0x4753f2 snd_fade_out_music: fades the current track out over `ms`. */
void music_fade_out(int ms);
/* The current track (0x66439c), 0 = none. */
uint32_t music_current(void);

#endif
