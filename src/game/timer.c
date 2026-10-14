/*
 * Timer. See timer.h and docs/startup.md.
 */
#include "game/timer.h"

#include <string.h>

#include "platform/platform.h"

/* tm_slots (0x54e980): 20 packed 38-byte records. */
typedef struct {
    TmCallback cb;     /* +0x00 */
    uint32_t period;   /* +0x04: ms; 0 = not running */
    uint16_t flags;    /* +0x08: TM_FLAG_* */
    void *user;        /* +0x0a */
    void *data;        /* +0x0e: freed on removal (nothing ever sets it) */
    int32_t busy;      /* +0x1a: second-pass reentrancy guard */
    uint32_t acc;      /* +0x1e: ms accumulated since the last call */
    int32_t running;   /* +0x22: tm_remove waits for it (never set) */
} TmSlot;

static TmSlot g_slots[TM_SLOTS];
static uint32_t g_elapsed;       /* tm_elapsed_ms 0x54e978 */
static uint32_t g_period;        /* tm_period_ms 0x54ec78 */
static int16_t g_start_count;    /* tm_start_count 0x674340 */
static int16_t g_lock_count;     /* tm_lock_count 0x674348 */
static int g_stopped;            /* tm_stopped 0x674350 */
static bool g_armed;             /* port: timeSetEvent is active (tm_event_id 0x64cf00) */
static uint32_t g_next_due;      /* port: plat_ticks_ms() of the next tick */

/* Port: catch up at most this many missed ticks per poll (about a second);
 * after a longer stall (debugger, suspended tab) the tick restarts from
 * now, as a multimedia timer that fell behind would. */
#define TM_MAX_CATCHUP 64

/* 0x4d14e0 */
int tm_initialize(void)
{
    g_stopped = 0;
    tm_start(16, 0x10000);
    return 0;
}

/* 0x4d150b */
int tm_shutdown(void)
{
    g_stopped = 1;
    tm_stop();
    return 0;
}

int tm_suspend(void) { return 0; } /* 0x4d152c */
int tm_resume(void) { return 0; }  /* 0x4d153e */

/* 0x4d15a1. The original returns the high word of the timeSetEvent ID
 * shifted up (or the sign of the old count): nobody reads it. */
int tm_start(uint32_t period_ms, uint32_t slot0)
{
    if (g_start_count++ != 0)
        return 0;
    for (int i = 1; i < TM_SLOTS; i++)
        g_slots[i].cb = NULL;
    g_period = period_ms ? period_ms : 1;
    (void)slot0; /* stored in slot 0's period field (0x54e984), never read */
    /* timeSetEvent(period_ms, 16, tm_callback, 0, TIME_PERIODIC) */
    g_armed = true;
    g_next_due = plat_ticks_ms() + g_period;
    return 0;
}

/* 0x4d166f */
void tm_stop(void)
{
    if (--g_start_count != 0)
        return;
    g_armed = false; /* timeKillEvent */
    for (int i = 1; i < TM_SLOTS; i++)
        g_slots[i].data = NULL; /* free (0x4d5850, an empty function) */
}

/* 0x4d1756 */
int tm_add(TmCallback cb, uint32_t period_ms, unsigned flags, void *user)
{
    for (int i = 1; i < TM_SLOTS; i++) {
        TmSlot *s = &g_slots[i];
        if (s->cb)
            continue;
        s->data = NULL;
        s->busy = 0;
        s->acc = 0;
        s->user = user;
        s->flags = (uint16_t)flags;
        s->period = period_ms;
        s->cb = cb;
        s->running = 0;
        return i;
    }
    return 0;
}

/* 0x4d1550 */
int tm_add_service(TmCallback cb, int hz)
{
    return tm_add(cb, hz ? (uint32_t)(1000 / hz) : 0, 0, NULL);
}

/* 0x4d1865. The original first yields to VHUMOR threads (0x4d1d39) while
 * the slot's `running` field is set; nothing sets it. */
void tm_remove(int slot)
{
    if (slot <= 0 || slot >= TM_SLOTS)
        return;
    g_slots[slot].cb = NULL;
    g_slots[slot].data = NULL;
}

/* 0x4d1583 */
int tm_remove_service(int slot)
{
    tm_remove(slot);
    return 0;
}

/* 0x4d18f6 */
void tm_set_period(int slot, uint32_t period_ms)
{
    if (slot <= 0 || slot >= TM_SLOTS)
        return;
    g_slots[slot].period = period_ms;
    g_slots[slot].acc = 0;
}

void tm_lock(void) { g_lock_count++; }   /* 0x4d170e */
void tm_unlock(void) { g_lock_count--; } /* 0x4d1720 */

/* Calls slot i for each whole period in its accumulator (once for a
 * one-shot, which also stops it). */
static void run_due(int i)
{
    TmSlot *s = &g_slots[i];
    while (s->period != 0 && s->period <= s->acc) {
        s->acc -= s->period;
        if (s->flags & TM_FLAG_ONESHOT) {
            s->period = 0;
            s->cb(i, s->user);
            break;
        }
        s->cb(i, s->user);
        if (!s->cb)
            break; /* port: the callback removed itself */
    }
}

/* 0x4d19b7, the timeSetEvent callback. It also saves and clears a VHUMOR
 * thread-switch request (0x674334, restored afterwards; tm_in_callback
 * 0x674338 marks the window) so that switching never happens inside a
 * callback. That threading (0x4d1d39) is never enabled in ALIFE.EXE. */
static void tm_callback(void)
{
    if (g_stopped)
        return;
    g_elapsed += g_period;
    /* tm_tick_count (0x674364) is incremented here and never read. */
    if (g_lock_count != 0)
        return;
    /* Pass 1: every live slot accumulates; immediate ones run. */
    for (int i = 1; i < TM_SLOTS; i++) {
        TmSlot *s = &g_slots[i];
        if (!s->cb)
            continue;
        s->acc += g_period;
        if ((s->flags & (TM_FLAG_IMMEDIATE | TM_FLAG_PAUSED)) == TM_FLAG_IMMEDIATE)
            run_due(i);
    }
    /* Pass 2: the normal ones, each guarded against reentry. */
    for (int i = 1; i < TM_SLOTS; i++) {
        TmSlot *s = &g_slots[i];
        if (!s->cb || (s->flags & (TM_FLAG_IMMEDIATE | TM_FLAG_PAUSED)) || s->period == 0 || s->busy)
            continue;
        s->busy = 1;
        run_due(i);
        s->busy = 0;
    }
}

void tm_poll(void)
{
    if (!g_armed)
        return;
    uint32_t now = plat_ticks_ms();
    int n = 0;
    while ((int32_t)(now - g_next_due) >= 0) {
        if (++n > TM_MAX_CATCHUP) {
            g_next_due = now + g_period;
            break;
        }
        g_next_due += g_period;
        tm_callback();
        if (!g_armed)
            break;
    }
}

/* 0x4d1940. The original spins on the counter (yielding to VHUMOR threads
 * if a switch is pending); the port has to drive the tick itself. */
void tm_wait(uint32_t ms)
{
    uint32_t start = g_elapsed;
    while (start <= g_elapsed && g_elapsed < start + ms) {
        uint32_t before = g_elapsed;
        tm_poll();
        if (!g_armed)
            break;
        if (g_elapsed == before)
            plat_delay_ms(1);
    }
}

uint32_t tm_get_elapsed(void) { return g_elapsed; } /* 0x4d198d */
uint32_t tm_get_period(void) { return g_period; }   /* 0x4d19a2 */

uint32_t tm_get_time(void) { return plat_ticks_ms(); }
