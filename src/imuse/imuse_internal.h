/*
 * iMUSE internals shared between the modules. The structures follow the
 * original's layouts (names from its own debug dump functions), with
 * pointers widened to native size.
 */
#ifndef AFTERLIFE_IMUSE_INTERNAL_H
#define AFTERLIFE_IMUSE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "imuse/imuse.h"

#define IMUSE_VERSION 0x30
#define IMUSE_MAX_TRACKS 8
#define IMUSE_MAP_SIZE 0x1000
#define IMUSE_MAX_STREAMS 3
#define IMUSE_MAX_ZONES 50
#define IMUSE_STREAM_MAX_READ 0x2000

/* Big-endian tags as the map holds them after conversion. */
#define IMUSE_TAG(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define TAG_IMUS IMUSE_TAG('i', 'M', 'U', 'S')
#define TAG_MAP IMUSE_TAG('M', 'A', 'P', ' ')
#define TAG_FRMT IMUSE_TAG('F', 'R', 'M', 'T')
#define TAG_REGN IMUSE_TAG('R', 'E', 'G', 'N')
#define TAG_TEXT IMUSE_TAG('T', 'E', 'X', 'T')
#define TAG_JUMP IMUSE_TAG('J', 'U', 'M', 'P')
#define TAG_STOP IMUSE_TAG('S', 'T', 'O', 'P')

void imuse_log(const char *fmt, ...);
uint32_t imuse_read_be32(const uint8_t *p); /* 0x4e16b0 */

/* Doubly linked list node; every listed struct starts with one. */
typedef struct ImuseNode {
    struct ImuseNode *prev, *next;
} ImuseNode;
int imuse_list_add(ImuseNode **head, ImuseNode *item);    /* 0x4e15e0 */
int imuse_list_remove(ImuseNode **head, ImuseNode *item); /* 0x4e1630 */

/* ---- files.c ---- */
int files_init(ImuseInitData *init); /* 0x4da860 */
int files_shutdown(void);            /* 0x4da8a0 */
const uint8_t *files_sound_addr(int sound);
const uint8_t *files_sound_map(int sound);
int files_check_sound(int sound); /* 2 if playing, else -1 */
int files_valid_sound(int sound);
int32_t files_seek(int sound, int32_t offset, int whence);
int32_t files_read(int sound, uint8_t *buf, int32_t size);
const ImuseBufInfo *files_buf_info(int buf_id);

/* ---- groups.c ---- */
int groups_init(void);
int groups_shutdown(void);
int groups_set_vol(int group, int vol); /* opcode 7 */
int groups_get_eff_vol(int group);

/* ---- fades.c ---- */
int fades_init(void);
int fades_shutdown(void);
int fades_fade_param(int sound, int param, int value, int ticks); /* opcode 14 */
void fades_clear(int sound, int param);
void fades_tick(void);

/* ---- triggers.c ---- */
int triggers_init(ImuseInitData *init);
int triggers_shutdown(void);
int triggers_set(int sound, const char *marker, int opcode, ImuseCallback fn, const intptr_t args[10]);
int triggers_check(int sound, const char *marker, int opcode);
int triggers_clear(int sound, const char *marker, int opcode);
int triggers_defer(int ticks, int opcode, ImuseCallback fn, const intptr_t args[10]);
void triggers_process_marker(int sound, const char *marker);
void triggers_defers_tick(void);
int triggers_count_pending(int sound);

/* ---- time.c ---- */
int time_init(ImuseInitData *init);
int time_shutdown(void);
int32_t time_usec_per_int(void);

/* ---- commands.c ---- */
void commands_tick(void); /* 0x4d4a10 */

/* ---- wave.c: serialises the wave modules against the mixing tick ---- */
int wave_init(ImuseInitData *init);
int wave_shutdown(void);
void wave_halt(void);   /* 0x4d9190 */
void wave_unhalt(void); /* 0x4d91a0 */
void wave_slice(void);  /* 0x4d9180 */
int wave_pause(void);
int wave_resume(void);
int wave_update_group_vols(void);
int wave_start_sound(int sound, int priority);
int wave_start_stream(int sound, int priority, int buf_id);
int wave_stop_sound(int sound);
int wave_stop_all_sounds(void);
int wave_get_next_sound(int sound);
int wave_set_param(int sound, int param, int value);
int wave_get_param(int sound, int param);
int wave_set_hook(int sound, int hook);
int wave_get_hook(int sound);
int wave_switch_stream(int old_sound, int new_sound, int fade_ms, int sync, int keep_offset);
int wave_process_streams(void);
int wave_query_stream(int sound, int32_t *size, int32_t *crit, int32_t *free_space, int32_t *paused);
int wave_feed_stream(int sound, const uint8_t *data, int32_t size, int paused);

/* ---- tracks.c ---- */
typedef struct ImuseDispatch ImuseDispatch;

typedef struct ImuseTrack {
    ImuseNode node;
    ImuseDispatch *dispatch;
    int sound;
    int marker;
    int group;
    int priority;
    int vol;
    int eff_vol;
    int pan;
    int detune;
    int transpose;
    int pitch_shift;
    int mailbox;
    int jump_hook;
} ImuseTrack;

/* Output format negotiated with the wave driver (0x55f058). */
typedef struct {
    int word_size;     /* 8 or 16 */
    int channels;      /* 1 or 2 */
    int16_t *mix_buf;  /* channels * 1024 int16 */
    int32_t mix_buf_size; /* bytes */
    int stereo_reverse;
} ImuseWaveFormat;

int tracks_init(ImuseInitData *init);
int tracks_shutdown(void);
int tracks_pause(void);
int tracks_resume(void);
int tracks_update_group_vols(void);
void tracks_slice(void);
int tracks_start_sound(int sound, int priority, int buf_id);
int tracks_stop_sound(int sound);
int tracks_stop_all_sounds(void);
int tracks_get_next_sound(int sound);
int tracks_query_stream(int sound, int32_t *size, int32_t *crit, int32_t *free_space, int32_t *paused);
int tracks_feed_stream(int sound, const uint8_t *data, int32_t size, int paused);
void tracks_stop_track(ImuseTrack *t);
int tracks_set_param(int sound, int param, int value);
int tracks_get_param(int sound, int param);
int tracks_set_hook(int sound, int hook);
int tracks_get_hook(int sound);
int tracks_count(void);

/* ---- streamer.c ---- */
typedef struct ImuseStream {
    int sound;
    int32_t cur_offset;
    int32_t end_offset;
    int buf_id;
    uint8_t *buf;
    int32_t buf_size; /* ring size: buffer size - max_read - 4 */
    int32_t load_size;
    int32_t critical_size;
    int32_t max_read;
    int32_t load_index;
    int32_t read_index;
    int paused;
} ImuseStream;

int streamer_init(void);
ImuseStream *streamer_alloc(int sound, int buf_id, int32_t max_read);
void streamer_close(ImuseStream *s);
int streamer_process_streams(void);
const uint8_t *streamer_read(ImuseStream *s, int32_t size);
const uint8_t *streamer_view(ImuseStream *s, int32_t offset, int32_t size);
int streamer_skip(ImuseStream *s, int32_t size);
int streamer_set_load_index(ImuseStream *s, int32_t offset);
int32_t streamer_avail(const ImuseStream *s);
void streamer_restart(ImuseStream *s, int sound, int32_t offset);
void streamer_query(ImuseStream *s, int32_t *size, int32_t *crit, int32_t *free_space, int32_t *paused);
int streamer_feed(ImuseStream *s, const uint8_t *data, int32_t size, int paused);

/* ---- dispatch.c ---- */
typedef struct ImuseZone {
    ImuseNode node;
    int use_flag;
    int32_t offset;
    int32_t size;
    int fade_flag;
} ImuseZone;

struct ImuseDispatch {
    ImuseTrack *track;
    int word_size;
    int sample_rate;
    int channel_count;
    int32_t current_offset;
    int32_t audio_remaining;
    /* The converted MAP: the original's block with every 32-bit field
     * byte-swapped to native order, TEXT strings left as they are. */
    uint8_t map[IMUSE_MAP_SIZE];
    ImuseStream *stream;
    int stream_buf_id;
    ImuseZone *zones;
    int stream_err_flag;
    uint8_t *fade_buf;
    int32_t fade_offset;
    int32_t fade_remaining;
    int fade_word_size;
    int fade_sample_rate;
    int fade_channel_count;
    int fade_sync_flag;
    int32_t fade_sync_delta;
    int32_t fade_vol;   /* 16.16, 0x7f0000 = full */
    int32_t fade_slope;
};

int dispatch_init(ImuseInitData *init);
void dispatch_shutdown(void);
ImuseDispatch *dispatch_get(int index);
int dispatch_start(ImuseTrack *t, int buf_id);
int dispatch_release(ImuseTrack *t);
int dispatch_switch_stream(int old_sound, int new_sound, int fade_ms, int sync, int keep_offset);
void dispatch_process(ImuseTrack *t, int32_t frames, int32_t rate);
void dispatch_predict_all_streams(void);

/* ---- mixer.c ---- */
int mixer_init(ImuseInitData *init, const ImuseWaveFormat *fmt);
void mixer_shutdown(void);
void mixer_set_radio_chatter(void);
void mixer_clear_radio_chatter(void);
int mixer_clear_buf(void);
void mixer_mix(const uint8_t *src, int32_t in_frames, int word_size, int channels, int32_t out_frames,
               int32_t out_offset, int vol, int pan);
int mixer_output(void *out, int32_t frames);

/* ---- waveapi.c: the output driver (platform audio in the port) ---- */
int waveapi_init(int rate, ImuseWaveFormat *fmt);
void waveapi_get_buffer(void **out, int32_t *frames, int32_t *rate);
void waveapi_submit(void);
int waveapi_shutdown(void);
void waveapi_pause(void);
void waveapi_resume(void);

#endif
