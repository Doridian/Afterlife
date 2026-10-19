/*
 * The game's sound module (0x4b9470..0x4ba0f0) and game-level sound calls
 * (0x4750f0..0x4754a1, 0x4755dd/0x4755f2). See sound.h.
 */
#include "game/sound.h"

#include <stdlib.h>
#include <string.h>

#include "compbun/cbres.h"
#include "game/resource.h"
#include "game/timer.h"
#include "imuse/imuse.h"

typedef struct {
    uint8_t *data;   /* +0x00 whole resource (loaded sounds) */
    CbHandle stream; /* +0x08 compbun handle (streamed sounds) */
    int32_t base;    /* +0x0c bundle position of the resource's data */
    int32_t size;    /* +0x10 */
    int state;       /* +0x14 SND_FREE.. */
    uint32_t time;   /* +0x18 timeGetTime of the last use */
} SndSlot;           /* 0x1c bytes in the original (+0x04: the PlatFile) */

static SndSlot g_slots[SND_SLOTS];     /* 0x5231a8 */
static ImuseInitData g_init;           /* 0x523148 */
static ImuseBufInfo g_music_buf;       /* 0x672598 */
static ImuseBufInfo g_voice_buf;       /* 0x672588 */
static int g_snd_running;              /* 0x6725ac */
static int g_timer_slot;               /* 0x6725a8 */
static uint32_t g_last_stream_service; /* 0x524788 */
static uint32_t g_music_ids[2];        /* 0x4f3228 */
int snd_music_enabled = 1;             /* 0x6643a8 */
int snd_sfx_enabled = 1;               /* 0x6643ac */

static int valid_slot(int slot)
{
    return slot >= 0 && slot < SND_SLOTS;
}

int snd_slot_state(int slot)
{
    return valid_slot(slot) ? g_slots[slot].state : SND_FREE;
}

/* --- iMUSE host callbacks ------------------------------------------------ */

/* 0x4b9e82 */
static const uint8_t *snd_sound_addr(int slot)
{
    return valid_slot(slot) ? g_slots[slot].data : NULL;
}

/* 0x4b9eb5: bufID 1 = music, 2 = voice. */
static const ImuseBufInfo *snd_buf_info(int buf_id)
{
    if (buf_id == SND_BUF_MUSIC)
        return &g_music_buf;
    if (buf_id == SND_BUF_VOICE)
        return &g_voice_buf;
    return NULL;
}

/* 0x4b9f00: seek within the resource; whence 2 counts back from its end.
 * Returns the new offset in the resource. */
static int32_t snd_stream_seek(int slot, int32_t offset, int whence)
{
    if (!valid_slot(slot) || !g_slots[slot].stream)
        return 0;
    SndSlot *s = &g_slots[slot];
    if (whence == 0) {
        offset += s->base;
    } else if (whence == 2) {
        offset = s->base + s->size - offset;
        whence = 0;
    }
    cbres_seek(s->stream, offset, whence);
    return (int32_t)cbres_tell(s->stream) - s->base;
}

/* 0x4b9ff1 */
static int32_t snd_stream_read(int slot, uint8_t *buf, int32_t size)
{
    if (!valid_slot(slot) || !g_slots[slot].stream)
        return 0;
    return (int32_t)cbres_read(buf, 1, size, g_slots[slot].stream);
}

/* --- init / tasks -------------------------------------------------------- */

static void free_buf(ImuseBufInfo *b)
{
    free(b->buf);
    memset(b, 0, sizeof(*b));
}

/* snd_initialize 0x4b9510: he_malloc of the two stream buffers. The INI's
 * second and third numbers are the critical and load sizes (read by iMUSE
 * through buf_info). */
int snd_alloc_buffers(const AlConfig *cfg)
{
    memset(g_slots, 0, sizeof(g_slots));
    g_music_buf.size = cfg->music_stream[0];
    g_music_buf.critical_size = cfg->music_stream[1];
    g_music_buf.load_size = cfg->music_stream[2];
    g_voice_buf.size = cfg->voice_stream[0];
    g_voice_buf.critical_size = cfg->voice_stream[1];
    g_voice_buf.load_size = cfg->voice_stream[2];
    g_music_buf.buf = malloc(g_music_buf.size > 0 ? (size_t)g_music_buf.size : 1);
    g_voice_buf.buf = malloc(g_voice_buf.size > 0 ? (size_t)g_voice_buf.size : 1);
    if (!g_music_buf.buf || !g_voice_buf.buf) {
        free_buf(&g_music_buf);
        free_buf(&g_voice_buf);
        return -1;
    }
    return 0;
}

/* snd_initialize 0x4b9510, after compbun_init(1, 0). */
int snd_start_imuse(const AlConfig *cfg)
{
    memset(&g_init, 0, sizeof(g_init));
    g_init.sound_addr = snd_sound_addr;
    g_init.usec_per_int = 16666; /* 0x411a */
    g_init.has_wave_driver = 1;
    g_init.wave_mix_count = cfg->mixer_channels;
    g_init.seek = snd_stream_seek;
    g_init.read = snd_stream_read;
    g_init.buf_info = snd_buf_info;
    g_timer_slot = 0;
    g_last_stream_service = 0;
    if (imuse_initialize(&g_init) < 0)
        return -1;
    g_snd_running = 1;
    return 0;
}

int snd_running(void) { return g_snd_running; }

/* 0x4b97e8 */
void snd_shutdown_imuse(void)
{
    if (!g_snd_running)
        return;
    if (g_timer_slot)
        tm_remove_service(g_timer_slot);
    g_timer_slot = 0;
    imuse_terminate();
    /* The original only memsets the table (the heap goes away anyway). */
    for (int i = 0; i < SND_SLOTS; i++) {
        if (g_slots[i].stream)
            cbres_close(g_slots[i].stream);
        free(g_slots[i].data);
    }
    memset(g_slots, 0, sizeof(g_slots));
    free_buf(&g_music_buf);
    free_buf(&g_voice_buf);
    g_snd_running = 0;
}

void snd_suspend_imuse(void) /* 0x4b9875 */
{
    if (g_snd_running)
        imuse_pause();
}

void snd_resume_imuse(void) /* 0x4b98a0 */
{
    if (g_snd_running)
        imuse_resume();
}

static void snd_timer_service(int slot, void *user)
{
    (void)slot;
    (void)user;
    if (g_init.timer_handler)
        g_init.timer_handler();
}

/* 0x4b9d7c. (The original skips the stream refill while 0x64cdb0 is set;
 * that flag isn't identified yet.) */
void snd_idle_imuse(void)
{
    if (!g_snd_running)
        return;
    if (g_timer_slot == 0 && g_init.timer_handler)
        g_timer_slot = tm_add_service(snd_timer_service, 60);
    uint32_t now = tm_get_time();
    if (g_last_stream_service + 100 < now) {
        g_last_stream_service = now;
        imuse_process_streams();
    }
    for (int i = 0; i < SND_SLOTS; i++) {
        SndSlot *s = &g_slots[i];
        if (s->state == SND_LOADED && s->data && s->time + 60000u < now &&
            snd_slot_get_param(i, IMUSE_P_PLAYING) == 0)
            snd_slot_free(i);
    }
}

/* --- slots --------------------------------------------------------------- */

/* 0x4b9913: loads the whole resource. slot 0 = res_id - 0x10000. Returns
 * the slot, 0 if the resource can't be opened, -1 if out of memory. */
int snd_slot_load(uint32_t res_id, int slot)
{
    if (slot == 0)
        slot = (int)(res_id - SND_RES_BASE);
    if (!valid_slot(slot))
        return 0;
    SndSlot *s = &g_slots[slot];
    if (s->state != SND_FREE)
        return slot;
    char name[RC_NAME_LEN];
    rc_resource_name(res_id, name);
    CbHandle h = cbres_open(name, "rb");
    if (!h)
        return 0;
    long size = rc_resource_size(res_id);
    uint8_t *buf = malloc(size > 0 ? (size_t)size : 1);
    if (!buf) {
        cbres_close(h);
        return -1;
    }
    cbres_read(buf, size, 1, h);
    cbres_close(h);
    s->data = buf;
    s->size = (int32_t)size;
    s->state = SND_LOADED;
    s->time = tm_get_time();
    return slot;
}

/* 0x4b98cb */
int snd_slot_load_locked(uint32_t res_id, int slot)
{
    slot = snd_slot_load(res_id, slot);
    if (slot > 0)
        g_slots[slot].state = SND_LOCKED;
    return slot;
}

/* 0x4b9a2b */
void snd_slot_free(int slot)
{
    if (slot == 0 || !valid_slot(slot) || g_slots[slot].state == SND_FREE)
        return;
    snd_slot_stop(slot);
    SndSlot *s = &g_slots[slot];
    if (s->state == SND_LOADED || s->state == SND_LOCKED) {
        free(s->data);
        s->data = NULL;
        s->state = SND_FREE;
    }
}

/* 0x4b9af0: plays a loaded sound in group 1 (effects). */
int snd_slot_start(int slot, int priority)
{
    if (!valid_slot(slot))
        return 0;
    if (g_slots[slot].state != SND_FREE)
        snd_slot_stop(slot);
    imuse_start_sound(slot, priority);
    imuse_set_param(slot, IMUSE_P_GROUP, IMUSE_GROUP_SFX);
    g_slots[slot].time = tm_get_time();
    return 1;
}

/* 0x4b9b63: streams resource res_id from the bundle in slot; buf_id 1
 * (music, group 4) or 2 (voice, group 2). */
int snd_slot_stream(uint32_t res_id, int slot, int priority, int buf_id)
{
    if (!valid_slot(slot))
        return 0;
    if (g_slots[slot].stream)
        snd_slot_stop_stream(slot);
    char name[RC_NAME_LEN];
    rc_resource_name(res_id, name);
    CbHandle h = cbres_open(name, "rb");
    if (!h)
        return 0;
    SndSlot *s = &g_slots[slot];
    s->stream = h;
    s->base = (int32_t)cbres_tell(h);
    s->size = (int32_t)rc_resource_size(res_id);
    s->state = SND_STREAMING;
    imuse_start_stream(slot, priority, buf_id);
    return imuse_set_param(slot, IMUSE_P_GROUP, buf_id == SND_BUF_MUSIC ? IMUSE_GROUP_MUSIC : IMUSE_GROUP_VOICE);
}

/* 0x4b9c7f */
int snd_slot_stop(int slot)
{
    imuse_stop_sound(slot);
    return 1;
}

/* 0x4b9ca0 */
int snd_slot_stop_stream(int slot)
{
    imuse_stop_sound(slot);
    if (!valid_slot(slot))
        return 1;
    SndSlot *s = &g_slots[slot];
    if (s->stream)
        cbres_close(s->stream);
    s->stream = 0;
    s->base = 0;
    s->size = 0;
    s->state = SND_FREE;
    return 1;
}

/* 0x4b9d5c */
int snd_slot_get_param(int slot, int param)
{
    return imuse_get_param(slot, param);
}

/* --- game level ---------------------------------------------------------- */

static int is_sound_id(uint32_t id)
{
    return id > 0xffff && id < 100000;
}

/* 0x4751af */
int snd_play(uint32_t res_id, int priority)
{
    if (!is_sound_id(res_id))
        return 0;
    int slot = (int)(res_id - SND_RES_BASE);
    if (priority == 4) {
        if (!snd_music_enabled)
            return 0;
        int i = 0;
        while (i < 2 && g_music_ids[i] != 0)
            i++;
        if (i == 2)
            return 0;
        g_music_ids[i] = res_id;
        snd_slot_stream(res_id, slot, 4, SND_BUF_MUSIC);
        return slot;
    }
    if (!snd_sfx_enabled)
        return 0;
    snd_slot_load(res_id, slot);
    snd_slot_start(slot, priority);
    return slot;
}

/* 0x4750f0: stops a sound; music also leaves the two-entry music list
 * and its stream is closed. */
void snd_stop(uint32_t res_id)
{
    if (!is_sound_id(res_id))
        return;
    int slot = (int)(res_id - SND_RES_BASE);
    int i = 0;
    while (i < 2 && g_music_ids[i] != res_id)
        i++;
    if (i < 2) {
        if (i == 0)
            g_music_ids[0] = g_music_ids[1];
        g_music_ids[1] = 0;
        snd_slot_stop_stream(slot);
    } else {
        snd_slot_stop(slot);
    }
}

void snd_preload(uint32_t res_id) /* 0x475428 */
{
    if (is_sound_id(res_id))
        snd_slot_load(res_id, (int)(res_id - SND_RES_BASE));
}

void snd_preload_locked(uint32_t res_id) /* 0x475467 */
{
    if (is_sound_id(res_id))
        snd_slot_load_locked(res_id, (int)(res_id - SND_RES_BASE));
}

void snd_unload(uint32_t res_id) /* 0x4754a1 */
{
    if (is_sound_id(res_id))
        snd_slot_free((int)(res_id - SND_RES_BASE));
}

void snd_sfx_off(void) { snd_sfx_enabled = 0; } /* 0x4755dd */
void snd_sfx_on(void) { snd_sfx_enabled = 1; }  /* 0x4755f2 */
