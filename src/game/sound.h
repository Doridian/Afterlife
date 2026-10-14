/*
 * The game's sound module ("So", HILEVEL "SoIntialize", 0x4b9470..0x4ba0f0)
 * and the game-level sound calls (0x4750f0..0x4754a1): a table of 200 sound
 * slots on top of iMUSE (src/imuse/).
 *
 * Slot n plays resource 0x10000 + n (resource IDs 65536..99999 are the iMUS
 * sounds; slot 199 is the advisor voice). iMUSE's sound IDs are the slot
 * numbers. A sound is either loaded whole into memory (effects, slot state
 * 3, or 1 = locked against expiry) or streamed from the bundle through one
 * of two ring buffers (state 2): bufID 1 = music ("Music stream", group 4),
 * 2 = voice ("Voice stream", group 2). Loaded sounds unused for 60 s are
 * freed by snd_idle.
 *
 * Startup hook points (snd_initialize 0x4b9510, in this order):
 *   cfg_read_sound -> snd_alloc_buffers -> rc_hook_compbun -> compbun_init(1,0)
 *   -> snd_start_imuse; then sys_register_task(snd_shutdown, snd_suspend,
 *   snd_resume, snd_idle) with the functions below as bodies.
 */
#ifndef AFTERLIFE_GAME_SOUND_H
#define AFTERLIFE_GAME_SOUND_H

#include <stdint.h>

#include "game/config.h"

#define SND_SLOTS 200
#define SND_VOICE_SLOT 199
#define SND_RES_BASE 0x10000

enum { SND_FREE = 0, SND_LOCKED = 1, SND_STREAMING = 2, SND_LOADED = 3 };
enum { SND_BUF_MUSIC = 1, SND_BUF_VOICE = 2 };

/* snd_initialize part 1 (before the C File Layer hook): allocate the music
 * and voice stream buffers. Returns 0, or -1 ("Unable to allocate music /
 * voice buffer", fatal in the original). */
int snd_alloc_buffers(const AlConfig *cfg);
/* snd_initialize part 2 (after compbun_init(1, 0)): fill the iMUSE init
 * data and call imuse_initialize. Returns 0 or -1. */
int snd_start_imuse(const AlConfig *cfg);

void snd_shutdown_imuse(void); /* body of snd_shutdown 0x4b97e8 */
void snd_suspend_imuse(void);  /* snd_suspend 0x4b9875: imuse_pause */
void snd_resume_imuse(void);   /* snd_resume 0x4b98a0: imuse_resume */
/* snd_idle 0x4b9d7c: registers the 60 Hz iMUSE timer service on first use,
 * refills streams every 100 ms and expires idle sounds. */
void snd_idle_imuse(void);

/* Slot level (0x4b98cb..0x4b9d5c). */
int snd_slot_load(uint32_t res_id, int slot);        /* 0x4b9913 */
int snd_slot_load_locked(uint32_t res_id, int slot); /* 0x4b98cb */
void snd_slot_free(int slot);                        /* 0x4b9a2b */
int snd_slot_start(int slot, int priority);          /* 0x4b9af0: group 1 (SFX) */
int snd_slot_stream(uint32_t res_id, int slot, int priority, int buf_id); /* 0x4b9b63 */
int snd_slot_stop(int slot);                         /* 0x4b9c7f */
int snd_slot_stop_stream(int slot);                  /* 0x4b9ca0 */
int snd_slot_get_param(int slot, int param);         /* 0x4b9d5c */
int snd_slot_state(int slot);

/* Game level (0x4750f0..). snd_play: priority 4 means music (streamed, at
 * most two at once); anything else loads and plays an effect. Returns the
 * slot or 0. Both honour the music/effects switches. */
int snd_play(uint32_t res_id, int priority); /* 0x4751af */
void snd_stop(uint32_t res_id);              /* 0x4750f0 */
void snd_preload(uint32_t res_id);           /* 0x475428 */
void snd_preload_locked(uint32_t res_id);    /* 0x475467 */
void snd_unload(uint32_t res_id);            /* 0x4754a1 */
extern int snd_music_enabled; /* 0x6643a8 */
extern int snd_sfx_enabled;   /* 0x6643ac */

#endif
