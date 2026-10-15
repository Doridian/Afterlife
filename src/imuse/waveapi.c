/*
 * Replacement for iMUSE:wavedrvs:win32:waveapi.c (0x4d4150..0x4d4790,
 * "C:\iwin\wavedrvs\win32\waveapi.c"), the waveOut driver.
 *
 * The original opens the wave mapper at 22050 Hz, 16-bit if the card has
 * any 16-bit format, with the card's channel count, and keeps 8 buffers of
 * 1024 frames queued. Each mixing slice, if the oldest buffer is done
 * (WHDR_DONE), it is re-prepared, written back, and handed to the mixer as
 * the next block to fill (the previous block, filled in the last slice, is
 * what gets written). A toggle makes only the first of the two calls per
 * slice count.
 *
 * Port: always signed 16-bit stereo, pushed to plat_audio_queue. A block is
 * handed out when fewer than 8 blocks are queued, filled by the mixer and
 * queued at once (waveapi_submit). With a sink set (imuse_wave_set_sink) or
 * when the device can't be opened, a virtual device consumes audio at the
 * real rate, one timer period per slice, so sounds still progress and end;
 * the sink receives every block (used by tests).
 */
#include <string.h>

#include "imuse/imuse_internal.h"
#include "imuse/waveapi.h"
#include "platform/platform.h"

#define BLOCK_FRAMES 0x400
#define BLOCK_COUNT 8
#define CHANNELS 2

static int16_t g_out_buf[BLOCK_FRAMES * CHANNELS]; /* 0x54f318 "outBuf" */
static int16_t g_mix_buf[BLOCK_FRAMES * CHANNELS]; /* 0x54f31c "mixBuf" */
static int32_t g_rate;          /* 0x54f324 "sampleRate" */
static int g_closed = 1;        /* 0x67444c */
static int g_have_block;        /* port: a block was handed out this slice */
static int g_use_device;        /* port: plat_audio is open */
static ImuseWaveSink g_sink;
static void *g_sink_user;
static int64_t g_virtual_queued; /* frames x 1000000 */

void imuse_wave_set_sink(ImuseWaveSink sink, void *user)
{
    g_sink = sink;
    g_sink_user = user;
}

int imuse_wave_rate(void)
{
    return g_rate;
}

/* 0x4d4150 */
int waveapi_init(int rate, ImuseWaveFormat *fmt)
{
    g_rate = rate;
    g_use_device = !g_sink && plat_audio_open(rate);
    if (!g_use_device && !g_sink)
        imuse_log("iWIN init: waveOutOpen failed, running without output\n");
    memset(g_out_buf, 0, sizeof(g_out_buf));
    memset(g_mix_buf, 0, sizeof(g_mix_buf));
    g_virtual_queued = 0;
    g_have_block = 0;
    fmt->word_size = 16;
    fmt->channels = CHANNELS;
    fmt->mix_buf = g_mix_buf;
    fmt->mix_buf_size = (int32_t)sizeof(g_mix_buf);
    fmt->stereo_reverse = 0;
    g_closed = 0;
    return 0;
}

static int32_t queued_frames(void)
{
    if (g_use_device)
        return plat_audio_queued_frames();
    /* The virtual device plays one timer period per slice. */
    g_virtual_queued -= (int64_t)g_rate * time_usec_per_int();
    if (g_virtual_queued < 0)
        g_virtual_queued = 0;
    return (int32_t)(g_virtual_queued / 1000000);
}

/* 0x4d4470: the next block to mix into, or *frames = 0. */
void waveapi_get_buffer(void **out, int32_t *frames, int32_t *rate)
{
    *frames = 0;
    g_have_block = 0;
    if (g_closed)
        return;
    int32_t limit = g_use_device ? plat_audio_queue_limit() : BLOCK_COUNT * BLOCK_FRAMES;
    if (queued_frames() + BLOCK_FRAMES > limit)
        return;
    *out = g_out_buf;
    *frames = BLOCK_FRAMES;
    *rate = g_rate;
    g_have_block = 1;
}

/* Port: the second waveapi call of a slice; queues the filled block. */
void waveapi_submit(void)
{
    if (!g_have_block || g_closed)
        return;
    g_have_block = 0;
    if (g_use_device)
        plat_audio_queue(g_out_buf, BLOCK_FRAMES);
    else
        g_virtual_queued += (int64_t)BLOCK_FRAMES * 1000000;
    if (g_sink)
        g_sink(g_out_buf, BLOCK_FRAMES, g_sink_user);
}

/* 0x4d4590 */
int waveapi_shutdown(void)
{
    g_closed = 1;
    if (g_use_device)
        plat_audio_close();
    g_use_device = 0;
    return 0;
}

/* 0x4d45e0: waveOutReset + waveOutClose */
void waveapi_pause(void)
{
    if (!g_closed) {
        g_closed = 1;
        if (g_use_device)
            plat_audio_pause(true);
        g_virtual_queued = 0;
    }
}

/* 0x4d4610: reopen, queue silence */
void waveapi_resume(void)
{
    if (g_closed) {
        if (g_use_device)
            plat_audio_pause(false);
        g_closed = 0;
    }
}
