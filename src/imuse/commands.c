/*
 * iMUSE:commands.c (0x4d3ea0..0x4d5170 and the game-side opcode wrappers):
 * the command dispatcher, init/terminate, pause/resume, the per-sound
 * commands and the periodic tick.
 */
#include <stdarg.h>
#include <stdio.h>

#include "imuse/imuse_internal.h"

static ImuseInitData *g_init; /* 0x67465c "initDataPtr" */
static int g_pause_count;     /* 0x674660 */
static int32_t g_running_host_count; /* 0x67466c */
static int32_t g_running_60hz_count; /* 0x674670 */
static int32_t g_running_10hz_count; /* 0x674674 */
static int g_debug;

void imuse_set_debug(int on)
{
    g_debug = on;
}

/* 0x4d7b40 is VHUMOR's debug printf; silent in the shipped game. */
void imuse_log(const char *fmt, ...)
{
    if (!g_debug)
        return;
    va_list ap;
    va_start(ap, fmt);
    fputs("imuse: ", stderr);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* 0x4d4c80: the module shutdown, in the original's order. */
static void commands_shutdown(void)
{
    g_init = NULL;
    time_shutdown();
    wave_shutdown();
    waveapi_shutdown();
    triggers_shutdown();
    fades_shutdown();
    groups_shutdown();
    files_shutdown();
    g_pause_count = 0;
    g_running_host_count = 0;
    g_running_60hz_count = 0;
    g_running_10hz_count = 0;
}

/* 0x4d4b80, opcode 0 */
static int commands_init(ImuseInitData *init)
{
    if (g_init) {
        imuse_log("ERROR: system already initialized...\n");
        return IMUSE_ERR;
    }
    if (!init) {
        imuse_log("ERROR: null initDataPtr...\n");
        return IMUSE_ERR;
    }
    g_running_host_count = 0;
    g_running_60hz_count = 0;
    g_running_10hz_count = 0;
    init->tick_count = 0;
    if (files_init(init) || groups_init() || fades_init() || triggers_init(init) || wave_init(init) ||
        time_init(init))
        return IMUSE_ERR;
    g_pause_count = 0;
    g_init = init;
    return IMUSE_VERSION;
}

/* 0x4d3ea0. The original stores the command entry (+0x2c) and the wave
 * driver entry (+0x34) in the init data first. */
int imuse_initialize(ImuseInitData *init)
{
    if (!init)
        return IMUSE_ERR;
    return commands_init(init);
}

/* 0x4d3ee0 -> opcode 1 (0x4d4c70: VHUMOR module shutdown of "iMUSE", which
 * runs every iMUSE module's shutdown hook). */
int imuse_terminate(void)
{
    if (!g_init)
        return IMUSE_ERR;
    commands_shutdown();
    return 0;
}

/* 0x4d4ea0, opcode 3: the first pause closes the output device. */
int imuse_pause(void)
{
    if (!g_init)
        return IMUSE_ERR;
    int r = 0;
    if (g_pause_count == 0) {
        waveapi_pause();
        r = wave_pause();
    }
    g_pause_count++;
    return r ? r : g_pause_count;
}

/* 0x4d4ed0, opcode 4 */
int imuse_resume(void)
{
    if (!g_init)
        return IMUSE_ERR;
    int r = 0;
    if (g_pause_count == 1) {
        r = wave_resume();
        waveapi_resume();
    }
    if (g_pause_count != 0)
        g_pause_count--;
    return r ? r : g_pause_count;
}

int imuse_set_group_vol(int group, int vol)
{
    if (!g_init)
        return IMUSE_ERR;
    return groups_set_vol(group, vol);
}

/* 0x4d5000, opcode 8: the sound must be loaded and start with "iMUS". */
int imuse_start_sound(int sound, int priority)
{
    if (!g_init)
        return IMUSE_ERR;
    const uint8_t *p = files_sound_addr(sound);
    if (!p) {
        imuse_log("ERR: null sound addr in StartSound()...\n");
        return IMUSE_ERR;
    }
    if (imuse_read_be32(p) != TAG_IMUS)
        return IMUSE_ERR;
    return wave_start_sound(sound, priority);
}

/* 0x4d5060, opcode 9 */
int imuse_stop_sound(int sound)
{
    if (!g_init)
        return IMUSE_ERR;
    if (files_check_sound(sound) == 2)
        return wave_stop_sound(sound);
    return IMUSE_ERR;
}

/* 0x4d5090, opcode 10. (Fades are not cleared: 0x4da160 is empty.) */
int imuse_stop_all_sounds(void)
{
    if (!g_init)
        return IMUSE_ERR;
    return fades_shutdown() | triggers_shutdown() | wave_stop_all_sounds();
}

/* 0x4d50b0, opcode 11 */
int imuse_get_next_sound(int sound)
{
    if (!g_init)
        return IMUSE_ERR;
    return wave_get_next_sound(sound);
}

/* 0x4d50c0, opcode 12 */
int imuse_set_param(int sound, int param, int value)
{
    if (!g_init)
        return IMUSE_ERR;
    if (files_check_sound(sound) == 2)
        return wave_set_param(sound, param, value);
    return IMUSE_ERR;
}

/* 0x4d50f0, opcode 13. Param 0 returns 2 (playing) or -1. */
int imuse_get_param(int sound, int param)
{
    if (!g_init)
        return IMUSE_ERR;
    int status = files_check_sound(sound);
    if (param == 0)
        return status;
    if (param == IMUSE_P_PENDING)
        return triggers_count_pending(sound);
    if (status == 2)
        return wave_get_param(sound, param);
    return param == IMUSE_P_PLAYING ? 0 : IMUSE_ERR;
}

int imuse_fade_param(int sound, int param, int value, int ticks)
{
    if (!g_init)
        return IMUSE_ERR;
    return fades_fade_param(sound, param, value, ticks);
}

/* 0x4d5140, opcode 15 */
int imuse_set_hook(int sound, int hook)
{
    if (!g_init)
        return IMUSE_ERR;
    if (files_check_sound(sound) == 2)
        return wave_set_hook(sound, hook);
    return IMUSE_ERR;
}

/* 0x4d5170, opcode 16 */
int imuse_get_hook(int sound)
{
    if (!g_init)
        return IMUSE_ERR;
    if (files_check_sound(sound) == 2)
        return wave_get_hook(sound);
    return IMUSE_ERR;
}

int imuse_set_trigger(int sound, const char *marker, int opcode, ImuseCallback fn, const intptr_t args[10])
{
    if (!g_init)
        return IMUSE_ERR;
    return triggers_set(sound, marker, opcode, fn, args);
}

int imuse_check_trigger(int sound, const char *marker, int opcode)
{
    if (!g_init)
        return IMUSE_ERR;
    return triggers_check(sound, marker, opcode);
}

int imuse_clear_trigger(int sound, const char *marker, int opcode)
{
    if (!g_init)
        return IMUSE_ERR;
    return triggers_clear(sound, marker, opcode);
}

int imuse_defer_command(int ticks, int opcode, ImuseCallback fn, const intptr_t args[10])
{
    if (!g_init)
        return IMUSE_ERR;
    return triggers_defer(ticks, opcode, fn, args);
}

/* 0x4d93e0 (opcode 25) */
int imuse_start_stream(int sound, int priority, int buf_id)
{
    if (!g_init)
        return IMUSE_ERR;
    return wave_start_stream(sound, priority, buf_id);
}

/* 0x4d9420 (opcode 26) */
int imuse_switch_stream(int old_sound, int new_sound, int fade_ms, int sync, int keep_offset)
{
    if (!g_init)
        return IMUSE_ERR;
    return wave_switch_stream(old_sound, new_sound, fade_ms, sync, keep_offset);
}

/* 0x4d9460 (opcode 27) */
int imuse_process_streams(void)
{
    if (!g_init)
        return IMUSE_ERR;
    return wave_process_streams();
}

/* 0x4d9480 (opcode 28) */
int imuse_query_stream(int sound, int32_t *buf_size, int32_t *critical, int32_t *free_space, int32_t *paused)
{
    if (!g_init)
        return IMUSE_ERR;
    return wave_query_stream(sound, buf_size, critical, free_space, paused);
}

/* 0x4d94c0 (opcode 29) */
int imuse_feed_stream(int sound, const uint8_t *data, int32_t size, int paused)
{
    if (!g_init)
        return IMUSE_ERR;
    return wave_feed_stream(sound, data, size, paused);
}

/* 0x4d4930: the original's single entry point. Opcodes 5/6 (save/restore)
 * dumped raw structs with pointers in them and are not supported. */
intptr_t imuse_command(int op, const intptr_t *a)
{
    static const intptr_t zero[14];
    if (!a)
        a = zero;
    if (!g_init && op != IMUSE_OP_INITIALIZE) {
        imuse_log("ERROR: system not initialized...\n");
        return IMUSE_ERR;
    }
    switch (op) {
    case IMUSE_OP_INITIALIZE: return commands_init((ImuseInitData *)a[0]);
    case IMUSE_OP_TERMINATE: return imuse_terminate();
    case IMUSE_OP_PRINT: return 0;
    case IMUSE_OP_PAUSE: return imuse_pause();
    case IMUSE_OP_RESUME: return imuse_resume();
    case IMUSE_OP_SET_GROUP_VOL: return imuse_set_group_vol((int)a[0], (int)a[1]);
    case IMUSE_OP_START_SOUND: return imuse_start_sound((int)a[0], (int)a[1]);
    case IMUSE_OP_STOP_SOUND: return imuse_stop_sound((int)a[0]);
    case IMUSE_OP_STOP_ALL_SOUNDS: return imuse_stop_all_sounds();
    case IMUSE_OP_GET_NEXT_SOUND: return imuse_get_next_sound((int)a[0]);
    case IMUSE_OP_SET_PARAM: return imuse_set_param((int)a[0], (int)a[1], (int)a[2]);
    case IMUSE_OP_GET_PARAM: return imuse_get_param((int)a[0], (int)a[1]);
    case IMUSE_OP_FADE_PARAM: return imuse_fade_param((int)a[0], (int)a[1], (int)a[2], (int)a[3]);
    case IMUSE_OP_SET_HOOK: return imuse_set_hook((int)a[0], (int)a[1]);
    case IMUSE_OP_GET_HOOK: return imuse_get_hook((int)a[0]);
    case IMUSE_OP_SET_TRIGGER:
        return imuse_set_trigger((int)a[0], (const char *)a[1], (int)a[2], NULL, &a[3]);
    case IMUSE_OP_CHECK_TRIGGER:
        return imuse_check_trigger((int)a[0], a[1] == -1 ? NULL : (const char *)a[1], (int)a[2]);
    case IMUSE_OP_CLEAR_TRIGGER:
        return imuse_clear_trigger((int)a[0], a[1] == -1 ? NULL : (const char *)a[1], (int)a[2]);
    case IMUSE_OP_DEFER_COMMAND: return imuse_defer_command((int)a[0], (int)a[1], NULL, &a[2]);
    case IMUSE_OP_START_STREAM: return imuse_start_stream((int)a[0], (int)a[1], (int)a[2]);
    case IMUSE_OP_SWITCH_STREAM:
        return imuse_switch_stream((int)a[0], (int)a[1], (int)a[2], (int)a[3], (int)a[4]);
    case IMUSE_OP_PROCESS_STREAMS: return imuse_process_streams();
    case IMUSE_OP_QUERY_STREAM:
        return imuse_query_stream((int)a[0], (int32_t *)a[1], (int32_t *)a[2], (int32_t *)a[3], (int32_t *)a[4]);
    case IMUSE_OP_FEED_STREAM:
        return imuse_feed_stream((int)a[0], (const uint8_t *)a[1], (int32_t)a[2], (int)a[3]);
    default:
        imuse_log("ERROR: bogus opcode...\n");
        return IMUSE_ERR;
    }
}

/* The 10 Hz music ducking: while any group-2 (voice) sound plays, group 4
 * (music) is pulled down to 5/8 of group 3 (the music setting) in steps of
 * 18 per 100 ms, and snaps back once the voice ends. Unsigned like the
 * original. */
static void commands_duck_music(void)
{
    uint32_t target = (uint32_t)groups_set_vol(IMUSE_GROUP_MUSIC_USER, -1);
    int s = 0;
    for (;;) {
        s = imuse_get_next_sound(s);
        if (s == 0)
            break;
        if (imuse_get_param(s, IMUSE_P_GROUP) == IMUSE_GROUP_VOICE) {
            target = ((target * 5) & 0xfffffff) >> 3;
            break;
        }
    }
    uint32_t cur = (uint32_t)groups_set_vol(IMUSE_GROUP_MUSIC, -1);
    uint32_t vol = (uint32_t)groups_set_vol(IMUSE_GROUP_MUSIC_USER, -1);
    if (target < cur) {
        cur -= 0x12;
        if (target < cur) {
            if (cur <= vol)
                vol = cur;
        } else if (target <= vol) {
            vol = target;
        }
        groups_set_vol(IMUSE_GROUP_MUSIC, (int)vol);
    } else if (cur < target) {
        cur += 3;
        if (cur < target) {
            if (vol <= cur)
                vol = cur;
        } else if (vol <= target) {
            vol = target;
        }
        groups_set_vol(IMUSE_GROUP_MUSIC, (int)vol);
    }
}

/* 0x4d4a10: everything periodic. Mixing first, then the optional host
 * handler, then (unless paused) 60 Hz fades/defers and 10 Hz ducking. */
void commands_tick(void)
{
    if (!g_init)
        return;
    int32_t usec = time_usec_per_int();
    wave_slice();
    if (g_init->host_int_handler && g_init->host_int_usec > 0) {
        g_running_host_count += usec;
        while (g_running_host_count >= g_init->host_int_usec) {
            g_running_host_count -= g_init->host_int_usec;
            g_init->host_int_handler();
        }
    }
    if (g_pause_count != 0)
        return;
    g_running_60hz_count += usec;
    while (g_running_60hz_count > 16666) {
        g_running_60hz_count -= 16667;
        g_init->tick_count++;
        fades_tick();
        triggers_defers_tick();
    }
    g_running_10hz_count += usec;
    while (g_running_10hz_count > 99999) {
        g_running_10hz_count -= 100000;
        commands_duck_music();
    }
}
