/*
 * Timer ("Tm", 0x4d14e0-0x4d1e2b): a 16 ms multimedia timer driving up to 19
 * periodic or one-shot callbacks, plus timeGetTime for everyone else.
 *
 * Original: tm_initialize calls tm_start(16, ...), which arms
 * timeSetEvent(16 ms, resolution 16, tm_callback, TIME_PERIODIC). winmm
 * calls tm_callback (0x4d19b7) on its own thread every 16 ms; it adds 16 to
 * the elapsed-ms counter and runs the slot callbacks that are due. The only
 * client in ALIFE.EXE is the sound module: snd_idle (0x4b9d7c) registers
 * iMUSE's service routine at 60 Hz (tm_add_service, period 1000 / 60 = 16
 * ms). Everything else that needs time calls timeGetTime directly (~80 call
 * sites: game_frame_update, the window manager's hover and double-click
 * timing, control blink/auto-repeat, palette cycling, the FLC player,
 * intro, benchmarks...); the port's equivalent is tm_get_time().
 *
 * Port: there are no threads (WebAssembly), so the tick is emulated:
 * tm_poll() runs every 16 ms tick that has come due by plat_ticks_ms() since
 * the last call. sys_pump calls it on every pump, which is how often the
 * original could have noticed a callback's effects anyway; tm_wait polls it.
 */
#ifndef AFTERLIFE_GAME_TIMER_H
#define AFTERLIFE_GAME_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#define TM_SLOTS 20 /* slot 0 holds the tm_start settings, 1..19 are timers */

/* Slot flags (the u16 at +0x08 of a 38-byte slot). */
#define TM_FLAG_IMMEDIATE 1 /* run in the first pass, without the reentrancy guard */
#define TM_FLAG_ONESHOT 2   /* period is cleared after the first call */
#define TM_FLAG_PAUSED 4    /* not called (its accumulator still runs) */

/* Called with the slot number and the user value given to tm_add. */
typedef void (*TmCallback)(int slot, void *user);

int tm_initialize(void); /* 0x4d14e0: tm_start(16, 0x10000) */
int tm_shutdown(void);   /* 0x4d150b: stops callbacks, tm_stop */
int tm_suspend(void);    /* 0x4d152c: empty */
int tm_resume(void);     /* 0x4d153e: empty */

/* 0x4d15a1: starts the tick (only the first of nested calls does anything):
 * clears slots 1..19 and ticks every period_ms. `slot0` is stored in slot 0's
 * period field and never read. */
int tm_start(uint32_t period_ms, uint32_t slot0);
void tm_stop(void); /* 0x4d166f: undoes one tm_start; the last one stops the tick */

/* 0x4d1756: installs `cb` in the first free slot, to be called every
 * period_ms. Returns the slot (1..19), or 0 if all are in use. */
int tm_add(TmCallback cb, uint32_t period_ms, unsigned flags, void *user);
/* 0x4d1550: tm_add(cb, 1000 / hz, 0, NULL). */
int tm_add_service(TmCallback cb, int hz);
void tm_remove(int slot);         /* 0x4d1865 */
int tm_remove_service(int slot);  /* 0x4d1583: tm_remove, returns 0 */
/* 0x4d18f6: new period, accumulator reset. */
void tm_set_period(int slot, uint32_t period_ms);

/* 0x4d170e / 0x4d1720: while locked (nested count), ticks advance the
 * elapsed counter but run no callbacks. */
void tm_lock(void);
void tm_unlock(void);

/* 0x4d1940: busy-waits until the elapsed counter has advanced by ms. */
void tm_wait(uint32_t ms);
uint32_t tm_get_elapsed(void); /* 0x4d198d: ms counted by ticks since start */
uint32_t tm_get_period(void);  /* 0x4d19a2: tick period (16) */

/* Port: runs the 16 ms ticks (tm_callback 0x4d19b7) that are due. */
void tm_poll(void);

/* Port: timeGetTime (winmm). */
uint32_t tm_get_time(void);

#endif
