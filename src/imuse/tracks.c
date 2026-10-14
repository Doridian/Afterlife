/*
 * iMUSE:wave:tracks.c (0x4de0f0..0x4dec20): the playing sounds ("tracks[]",
 * one per mixer voice) and the mixing slice that drives the dispatcher, the
 * mixer and the output driver.
 */
#include "imuse/imuse_internal.h"

static ImuseTrack g_tracks[IMUSE_MAX_TRACKS]; /* 0x55f088 */
static int g_track_count;                     /* 0x647994 "trackCount" */
static int g_wave_call;                       /* 0x55f050 "waveCall": driver present */
static int g_pause_timer;                     /* 0x55f07c */
static ImuseTrack *g_track_list;              /* 0x55f06c */
static int32_t g_pref_sample_rate;            /* 0x55f054 */
static ImuseInitData *g_init;                 /* 0x55f074 */
static ImuseWaveFormat g_fmt;                 /* 0x55f058 */
static void *g_out_buf;                       /* 0x55f080 */
static int32_t g_out_frames;                  /* 0x55f078 */
static int32_t g_out_rate;                    /* 0x55f070 */
static int32_t g_no_driver_usec;              /* 0x676100 */
static uint32_t g_wave_int_usec;              /* 0x676104 */

#define LIST(p) ((ImuseNode **)(void *)(p))

int tracks_count(void)
{
    return g_track_count;
}

/* 0x4de1a0 */
int tracks_init(ImuseInitData *init)
{
    g_init = init;
    if (init->wave_mix_count == 0 || init->wave_mix_count > IMUSE_MAX_TRACKS) {
        imuse_log("TR: waveMixCount NULL or too big, defaulting to 4...\n");
        init->wave_mix_count = 4;
    }
    g_track_count = init->wave_mix_count;
    g_wave_call = init->has_wave_driver;
    g_pause_timer = 0;
    g_track_list = NULL;
    g_pref_sample_rate = 22050;
    if (!g_wave_call) {
        imuse_log("TR: No WAVE driver loaded...\n");
        g_fmt = (ImuseWaveFormat){8, 1, NULL, 0, 0};
    } else if (waveapi_init(g_pref_sample_rate, &g_fmt) != 0) {
        return IMUSE_ERR;
    }
    if (mixer_init(init, &g_fmt) != 0 || dispatch_init(init) != 0 || streamer_init() != 0)
        return IMUSE_ERR;
    for (int i = 0; i < g_track_count; i++) {
        ImuseTrack *t = &g_tracks[i];
        t->node.prev = t->node.next = NULL;
        t->dispatch = dispatch_get(i);
        t->dispatch->track = t;
        t->sound = 0;
    }
    return 0;
}

/* Module shutdown hook 0x4dec10 (stop everything), then dispatch 0x4e0920
 * and mixer 0x4e5d60. */
int tracks_shutdown(void)
{
    tracks_stop_all_sounds();
    dispatch_shutdown();
    mixer_shutdown();
    g_track_count = 0;
    return 0;
}

int tracks_pause(void) /* 0x4de300 */
{
    g_pause_timer = 1;
    return 0;
}

int tracks_resume(void) /* 0x4de310 */
{
    g_pause_timer = 0;
    return 0;
}

/* 0x4de410 */
int tracks_update_group_vols(void)
{
    for (ImuseTrack *t = g_track_list; t; t = (ImuseTrack *)t->node.next)
        t->eff_vol = (groups_get_eff_vol(t->group) * (t->vol + 1)) >> 7;
    return 0;
}

/* 0x4de440: one mixing slice. While paused it mixes silence after two
 * slices. The driver hands out at most one 1024-frame block per slice. */
void tracks_slice(void)
{
    if (g_pause_timer != 0) {
        if (++g_pause_timer < 3)
            return;
        g_pause_timer = 3;
    }
    wave_halt();
    dispatch_predict_all_streams();
    if (!g_wave_call) {
        /* No output: pretend a 25 ms block went by. */
        g_no_driver_usec += time_usec_per_int();
        if (g_no_driver_usec < 25000) {
            g_out_frames = 0;
        } else {
            g_no_driver_usec -= 25000;
            g_out_rate = g_pref_sample_rate;
            g_out_frames = g_pref_sample_rate / 40;
        }
    } else {
        waveapi_get_buffer(&g_out_buf, &g_out_frames, &g_out_rate);
    }
    if (g_out_frames != 0) {
        if (g_init->half_rate)
            g_out_rate >>= 1;
        mixer_clear_buf();
        if (g_pause_timer == 0) {
            if (g_init->wave_int_handler && g_init->wave_int_usec) {
                g_wave_int_usec += (uint32_t)((uint64_t)(uint32_t)g_out_frames * 1000000u / (uint32_t)g_out_rate);
                while (g_init->wave_int_usec <= g_wave_int_usec) {
                    g_wave_int_usec -= g_init->wave_int_usec;
                    g_init->wave_int_handler();
                }
            }
            for (ImuseTrack *t = g_track_list; t;) {
                ImuseTrack *next = (ImuseTrack *)t->node.next;
                dispatch_process(t, g_out_frames, g_out_rate);
                t = next;
            }
        }
        if (g_wave_call) {
            mixer_output(g_out_buf, g_out_frames);
            waveapi_submit();
        }
    }
    wave_unhalt();
}

static int clamp(int v, int lo, int hi) /* 0x4e1750 */
{
    if (v < lo)
        return lo;
    return v > hi ? hi : v;
}

/* 0x4e1770: brings a transpose into range by octaves. */
static int clamp_octaves(int v, int lo, int hi)
{
    if (v < lo)
        v += (int)(((unsigned)(lo - v) + 11u) / 12u) * 12;
    if (hi < v)
        v -= (int)(((unsigned)(v - hi) + 11u) / 12u) * 12;
    return v;
}

/* 0x4deba0: a free track, else steal the lowest-priority one if it is not
 * above the new sound's priority. */
static ImuseTrack *tracks_alloc(int priority)
{
    for (int i = 0; i < g_track_count; i++)
        if (g_tracks[i].sound == 0)
            return &g_tracks[i];
    imuse_log("ERR: no spare tracks...\n");
    ImuseTrack *victim = NULL;
    int lowest = 127;
    for (ImuseTrack *t = g_track_list; t; t = (ImuseTrack *)t->node.next) {
        if (t->priority <= lowest) {
            lowest = t->priority;
            victim = t;
        }
    }
    if (victim && lowest <= priority) {
        tracks_stop_track(victim);
        return victim;
    }
    return NULL;
}

/* 0x4de5e0. buf_id 0 plays from memory, else streams through that buffer. */
int tracks_start_sound(int sound, int priority, int buf_id)
{
    priority = clamp(priority, 0, 127);
    ImuseTrack *t = tracks_alloc(priority);
    if (!t)
        return IMUSE_ERR_FULL;
    t->sound = sound;
    t->marker = 0;
    t->group = 0;
    t->priority = priority;
    t->vol = 127;
    t->eff_vol = groups_get_eff_vol(0);
    t->pan = 64;
    t->detune = 0;
    t->transpose = 0;
    t->pitch_shift = 0;
    t->mailbox = 0;
    t->jump_hook = 0;
    if (dispatch_start(t, buf_id) != 0) {
        imuse_log("ERR: dispatch couldn't start sound...\n");
        t->sound = 0;
        return IMUSE_ERR;
    }
    wave_halt();
    imuse_list_add(LIST(&g_track_list), &t->node);
    wave_unhalt();
    return 0;
}

/* 0x4de800 */
void tracks_stop_track(ImuseTrack *t)
{
    imuse_list_remove(LIST(&g_track_list), &t->node);
    dispatch_release(t);
    fades_clear(t->sound, -1);
    triggers_clear(t->sound, NULL, -1);
    t->sound = 0;
}

/* 0x4de6a0 */
int tracks_stop_sound(int sound)
{
    int r = IMUSE_ERR;
    for (ImuseTrack *t = g_track_list; t;) {
        ImuseTrack *next = (ImuseTrack *)t->node.next;
        if (t->sound == sound) {
            tracks_stop_track(t);
            r = 0;
        }
        t = next;
    }
    return r;
}

/* 0x4de6e0 */
int tracks_stop_all_sounds(void)
{
    wave_halt();
    for (ImuseTrack *t = g_track_list; t;) {
        ImuseTrack *next = (ImuseTrack *)t->node.next;
        tracks_stop_track(t);
        t = next;
    }
    wave_unhalt();
    return 0;
}

/* 0x4de710: the smallest playing sound ID above sound, 0 if none. */
int tracks_get_next_sound(int sound)
{
    unsigned best = 0;
    for (ImuseTrack *t = g_track_list; t; t = (ImuseTrack *)t->node.next) {
        unsigned s = (unsigned)t->sound;
        if ((unsigned)sound < s && (best == 0 || s < best))
            best = s;
    }
    return (int)best;
}

static ImuseTrack *find_streaming(int sound)
{
    for (ImuseTrack *t = g_track_list; t; t = (ImuseTrack *)t->node.next)
        if (t->sound != 0 && t->sound == sound && t->dispatch->stream)
            return t;
    return NULL;
}

/* 0x4de740 */
int tracks_query_stream(int sound, int32_t *size, int32_t *crit, int32_t *free_space, int32_t *paused)
{
    ImuseTrack *t = find_streaming(sound);
    if (!t)
        return IMUSE_ERR;
    streamer_query(t->dispatch->stream, size, crit, free_space, paused);
    return 0;
}

/* 0x4de7a0 */
int tracks_feed_stream(int sound, const uint8_t *data, int32_t size, int paused)
{
    ImuseTrack *t = find_streaming(sound);
    if (!t)
        return IMUSE_ERR;
    return streamer_feed(t->dispatch->stream, data, size, paused);
}

static ImuseTrack *find_track(int sound)
{
    for (ImuseTrack *t = g_track_list; t; t = (ImuseTrack *)t->node.next)
        if (t->sound == sound)
            return t;
    return NULL;
}

/* 0x4de850. Only the first track playing the sound is changed. */
int tracks_set_param(int sound, int param, int value)
{
    ImuseTrack *t = find_track(sound);
    if (!t)
        return IMUSE_ERR_NOT_FOUND;
    switch (param) {
    case IMUSE_P_PRIORITY:
        if ((unsigned)value > 127)
            return IMUSE_ERR_ARG;
        t->priority = value;
        return 0;
    case IMUSE_P_GROUP:
        if ((unsigned)value > 15)
            return IMUSE_ERR_ARG;
        t->group = value;
        t->eff_vol = (groups_get_eff_vol(value) * (t->vol + 1)) >> 7;
        return 0;
    case IMUSE_P_PAN:
        if ((unsigned)value > 127)
            return IMUSE_ERR_ARG;
        t->pan = value;
        return 0;
    case IMUSE_P_VOLUME:
        if ((unsigned)value > 127)
            return IMUSE_ERR_ARG;
        t->vol = value;
        t->eff_vol = (groups_get_eff_vol(t->group) * (value + 1)) >> 7;
        return 0;
    case IMUSE_P_DETUNE:
        if (value > -0x2401 && value < 0x2401) {
            t->detune = value;
            t->pitch_shift = t->transpose * 256 + value;
            return 0;
        }
        return IMUSE_ERR_ARG;
    case IMUSE_P_TRANSPOSE:
        if (value > -13 && value < 13) {
            t->transpose = value == 0 ? 0 : clamp_octaves(t->transpose + value, -12, 12);
            t->pitch_shift = t->transpose * 256 + t->detune;
            return 0;
        }
        return IMUSE_ERR_ARG;
    case IMUSE_P_MAILBOX:
        t->mailbox = value;
        return 0;
    default:
        imuse_log("ERR: TrSetParam() couldn't set param %lu...\n", (unsigned long)param);
        return IMUSE_ERR_ARG;
    }
}

/* 0x4dea00. IMUSE_P_PLAYING counts tracks; anything else answers from the
 * first track playing the sound. */
int tracks_get_param(int sound, int param)
{
    int count = 0;
    for (ImuseTrack *t = g_track_list; t; t = (ImuseTrack *)t->node.next) {
        if (t->sound != sound)
            continue;
        switch (param) {
        case IMUSE_P_PLAYING: count++; continue;
        case 0: return IMUSE_ERR;
        case IMUSE_P_PENDING: return IMUSE_ERR;
        case IMUSE_P_MARKER: return t->marker;
        case IMUSE_P_GROUP: return t->group;
        case IMUSE_P_PRIORITY: return t->priority;
        case IMUSE_P_VOLUME: return t->vol;
        case IMUSE_P_PAN: return t->pan;
        case IMUSE_P_DETUNE: return t->detune;
        case IMUSE_P_TRANSPOSE: return t->transpose;
        case IMUSE_P_MAILBOX: return t->mailbox;
        case IMUSE_P_STREAMING: return t->dispatch->stream != NULL;
        case IMUSE_P_STREAM_BUF: return t->dispatch->stream_buf_id;
        default: return IMUSE_ERR_ARG;
        }
    }
    return param == IMUSE_P_PLAYING ? count : IMUSE_ERR_NOT_FOUND;
}

/* 0x4deb30: jump hooks 0..128; 128 suppresses hook-0 jumps once. */
int tracks_set_hook(int sound, int hook)
{
    if ((unsigned)hook > 0x80)
        return IMUSE_ERR_ARG;
    ImuseTrack *t = find_track(sound);
    if (!t)
        return IMUSE_ERR_NOT_FOUND;
    t->jump_hook = hook;
    return 0;
}

/* 0x4deb70 */
int tracks_get_hook(int sound)
{
    ImuseTrack *t = find_track(sound);
    return t ? t->jump_hook : IMUSE_ERR_NOT_FOUND;
}
