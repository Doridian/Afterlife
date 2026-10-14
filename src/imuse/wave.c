/*
 * iMUSE:wave:wave.c (0x4d9180..0x4d94f0): the front of the wave modules.
 * Every entry holds off the mixing slice ("wvSlicingHalted") while it runs,
 * because in the original the slice ran from a timer interrupt. The port is
 * single-threaded, but the streamer still drops the hold around its file
 * reads, so the counter is kept.
 */
#include "imuse/imuse_internal.h"

static int g_slicing_halted; /* 0x675758 "wvSlicingHalted" */

/* 0x4d91b0 */
int wave_init(ImuseInitData *init)
{
    if (tracks_init(init) != 0)
        return IMUSE_ERR;
    g_slicing_halted = 0;
    return 0;
}

/* 0x4d9200. The tracks/dispatch/mixer teardown hangs off VHUMOR module
 * shutdown hooks in the original; the port runs them here. */
int wave_shutdown(void)
{
    tracks_shutdown();
    return 0;
}

void wave_halt(void) /* 0x4d9190 */
{
    g_slicing_halted++;
}

void wave_unhalt(void) /* 0x4d91a0 */
{
    if (g_slicing_halted != 0)
        g_slicing_halted--;
}

/* 0x4d9180 */
void wave_slice(void)
{
    if (g_slicing_halted == 0)
        tracks_slice();
}

int wave_pause(void) /* 0x4d9210 */
{
    wave_halt();
    tracks_pause();
    wave_unhalt();
    return 0;
}

int wave_resume(void) /* 0x4d9230 */
{
    wave_halt();
    tracks_resume();
    wave_unhalt();
    return 0;
}

int wave_update_group_vols(void) /* 0x4d92a0 */
{
    wave_halt();
    int r = tracks_update_group_vols();
    wave_unhalt();
    return r;
}

int wave_start_sound(int sound, int priority) /* 0x4d92c0 */
{
    wave_halt();
    int r = tracks_start_sound(sound, priority, 0);
    wave_unhalt();
    return r;
}

int wave_start_stream(int sound, int priority, int buf_id) /* 0x4d93e0 */
{
    if (!files_valid_sound(sound))
        return IMUSE_ERR;
    wave_halt();
    int r = tracks_start_sound(sound, priority, buf_id);
    wave_unhalt();
    return r;
}

int wave_stop_sound(int sound) /* 0x4d92f0 */
{
    wave_halt();
    int r = tracks_stop_sound(sound);
    wave_unhalt();
    return r;
}

int wave_stop_all_sounds(void) /* 0x4d9310 */
{
    wave_halt();
    int r = tracks_stop_all_sounds();
    wave_unhalt();
    return r;
}

int wave_get_next_sound(int sound) /* 0x4d9330 */
{
    wave_halt();
    int r = tracks_get_next_sound(sound);
    wave_unhalt();
    return r;
}

int wave_set_param(int sound, int param, int value) /* 0x4d9350 */
{
    wave_halt();
    int r = tracks_set_param(sound, param, value);
    wave_unhalt();
    return r;
}

int wave_get_param(int sound, int param) /* 0x4d9380 */
{
    wave_halt();
    int r = tracks_get_param(sound, param);
    wave_unhalt();
    return r;
}

int wave_set_hook(int sound, int hook) /* 0x4d93b0 */
{
    return tracks_set_hook(sound, hook);
}

int wave_get_hook(int sound) /* 0x4d93d0 */
{
    return tracks_get_hook(sound);
}

int wave_switch_stream(int old_sound, int new_sound, int fade_ms, int sync, int keep_offset) /* 0x4d9420 */
{
    wave_halt();
    int r = dispatch_switch_stream(old_sound, new_sound, fade_ms, sync, keep_offset);
    wave_unhalt();
    return r;
}

int wave_process_streams(void) /* 0x4d9460 */
{
    wave_halt();
    int r = streamer_process_streams();
    wave_unhalt();
    return r;
}

int wave_query_stream(int sound, int32_t *size, int32_t *crit, int32_t *free_space, int32_t *paused) /* 0x4d9480 */
{
    wave_halt();
    int r = tracks_query_stream(sound, size, crit, free_space, paused);
    wave_unhalt();
    return r;
}

int wave_feed_stream(int sound, const uint8_t *data, int32_t size, int paused) /* 0x4d94c0 */
{
    wave_halt();
    int r = tracks_feed_stream(sound, data, size, paused);
    wave_unhalt();
    return r;
}
