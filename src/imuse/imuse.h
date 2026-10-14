/*
 * iMUSE (digital), the LucasArts interactive music and sound system as built
 * into ALIFE.EXE ("iMUSE:*.c" / "C:\iwin\..." source paths, modules dated
 * May 10 1996). See docs/imuse.md for the module map.
 *
 * The game talks to iMUSE through one variadic entry point (the "command"
 * function, 0x4d4930) with an opcode and up to 14 int arguments, wrapped by
 * small game-side helpers (0x4d3ea0..0x4d40f0). This header gives each opcode
 * a typed function; imuse_command() is the generic dispatcher that triggers,
 * deferred commands and fades use.
 *
 * Sound IDs are whatever the host's sound_addr/seek/read callbacks accept.
 * Afterlife uses slot numbers 1..199 (resource ID - 0x10000, 199 = voice).
 * Every sound is an "iMUS" file: a big-endian MAP of FRMT/REGN/TEXT/JUMP/STOP
 * events followed by raw PCM (docs/formats/jiff.md, docs/imuse.md).
 *
 * Time: the host calls imuse_timer_handler() every usec_per_int microseconds
 * (16666, i.e. 60 Hz). That one call mixes audio when the output wants more,
 * runs fades and deferred commands and the 10 Hz music ducking.
 */
#ifndef AFTERLIFE_IMUSE_H
#define AFTERLIFE_IMUSE_H

#include <stdint.h>

/* Return codes. */
#define IMUSE_OK 0
#define IMUSE_ERR (-1)
#define IMUSE_ERR_NOT_READY (-3) /* stream data not loaded yet */
#define IMUSE_ERR_NOT_FOUND (-4) /* sound not playing */
#define IMUSE_ERR_ARG (-5)
#define IMUSE_ERR_FULL (-6) /* no free slot */

/* Opcodes of the command function (table at 0x674678). 21..24 are empty. */
enum {
    IMUSE_OP_INITIALIZE = 0,
    IMUSE_OP_TERMINATE = 1,
    IMUSE_OP_PRINT = 2,
    IMUSE_OP_PAUSE = 3,
    IMUSE_OP_RESUME = 4,
    IMUSE_OP_SAVE = 5,
    IMUSE_OP_RESTORE = 6,
    IMUSE_OP_SET_GROUP_VOL = 7,
    IMUSE_OP_START_SOUND = 8,
    IMUSE_OP_STOP_SOUND = 9,
    IMUSE_OP_STOP_ALL_SOUNDS = 10,
    IMUSE_OP_GET_NEXT_SOUND = 11,
    IMUSE_OP_SET_PARAM = 12,
    IMUSE_OP_GET_PARAM = 13,
    IMUSE_OP_FADE_PARAM = 14,
    IMUSE_OP_SET_HOOK = 15,
    IMUSE_OP_GET_HOOK = 16,
    IMUSE_OP_SET_TRIGGER = 17,
    IMUSE_OP_CHECK_TRIGGER = 18,
    IMUSE_OP_CLEAR_TRIGGER = 19,
    IMUSE_OP_DEFER_COMMAND = 20,
    IMUSE_OP_START_STREAM = 25,
    IMUSE_OP_SWITCH_STREAM = 26,
    IMUSE_OP_PROCESS_STREAMS = 27,
    IMUSE_OP_QUERY_STREAM = 28,
    IMUSE_OP_FEED_STREAM = 29,
    IMUSE_OP_COUNT = 30,
    /* Port-only: marks a trigger/deferred command whose action is a host
     * function instead of an opcode (the original stored the function
     * pointer in the opcode field and told them apart by value >= 30). */
    IMUSE_OP_CALLBACK = 30,
};

/* Sound parameters for set/get/fade_param. */
enum {
    IMUSE_P_PLAYING = 0x100,     /* get: number of tracks playing the sound */
    IMUSE_P_PENDING = 0x200,     /* get: triggers/defers that would start it */
    IMUSE_P_MARKER = 0x300,      /* get */
    IMUSE_P_GROUP = 0x400,       /* 0..15 */
    IMUSE_P_PRIORITY = 0x500,    /* 0..127 */
    IMUSE_P_VOLUME = 0x600,      /* 0..127 */
    IMUSE_P_PAN = 0x700,         /* 0..127, 64 = centre */
    IMUSE_P_DETUNE = 0x800,      /* -9216..9216, stored but not used by the mixer */
    IMUSE_P_TRANSPOSE = 0x900,   /* -12..12, ditto */
    IMUSE_P_MAILBOX = 0xa00,     /* non-zero: "radio chatter" filter (8-bit mono) */
    IMUSE_P_STREAMING = 0x1800,  /* get: 1 if the sound is streamed */
    IMUSE_P_STREAM_BUF = 0x1900, /* get: stream buffer ID */
};

/* Groups as Afterlife uses them (snd_initialize, the audio settings dialog
 * 0x475bd9 and the ducking in imuse_timer_handler). */
enum {
    IMUSE_GROUP_MASTER = 0,
    IMUSE_GROUP_SFX = 1,
    IMUSE_GROUP_VOICE = 2,
    IMUSE_GROUP_MUSIC_USER = 3, /* the music slider; group 4 follows it */
    IMUSE_GROUP_MUSIC = 4,      /* ducked to 5/8 while a voice plays */
};

/* Trigger / deferred-command host callback. For a trigger, marker is the TEXT
 * marker that fired; for a deferred command it is NULL. */
typedef void (*ImuseCallback)(const char *marker, const intptr_t args[10]);

/* Stream buffer description returned by buf_info (0x4b9eb5 in Afterlife):
 * bufID 1 = music, 2 = voice, sized by ALIFE.INI "Music/Voice stream". */
typedef struct {
    uint8_t *buf;
    int32_t size;          /* "Music stream" first number */
    int32_t load_size;     /* third number: bytes read per refill */
    int32_t critical_size; /* second number: refill first below this */
} ImuseBufInfo;

/* Init data the host fills in and passes to imuse_initialize (0x523148 in
 * Afterlife, 0x5c bytes). Offsets are the original's. */
typedef struct ImuseInitData {
    /* +0x00 address of a sound loaded whole into memory, or NULL */
    const uint8_t *(*sound_addr)(int sound);
    /* +0x04 address of a sound's MAP if kept separately (NULL in Afterlife) */
    const uint8_t *(*sound_map)(int sound);
    /* +0x08 default action of triggers / deferred commands with opcode 0 */
    ImuseCallback trigger_callback;
    /* +0x10 optional extra periodic host handler and its period */
    void (*host_int_handler)(void);
    int32_t host_int_usec;
    /* +0x18 out: what the host must call every usec_per_int microseconds */
    void (*timer_handler)(void);
    /* +0x1c period of timer_handler, 16666 in Afterlife */
    int32_t usec_per_int;
    /* +0x20 optional handler run every wave_int_usec of mixed audio */
    void (*wave_int_handler)(void);
    uint32_t wave_int_usec;
    /* +0x28 out: 60 Hz tick counter */
    int32_t tick_count;
    /* +0x34 the original's wave-driver entry; always set by imuse_initialize.
     * Port: 0 makes iMUSE run without output (the "no WAVE driver" path). */
    int has_wave_driver;
    /* +0x40 non-zero: tell the dispatcher the output runs at half rate */
    int half_rate;
    /* +0x44 mixer voices ("Mixer channels"), 1..8, else 4 */
    int wave_mix_count;
    /* +0x4c..+0x54 streaming: seek (whence 0/1/2, returns the new offset),
     * read (returns bytes read) and stream buffer lookup by bufID. */
    int32_t (*seek)(int sound, int32_t offset, int whence);
    int32_t (*read)(int sound, uint8_t *buf, int32_t size);
    const ImuseBufInfo *(*buf_info)(int buf_id);
} ImuseInitData;

/* Generic command (0x4d4930). args holds up to 14 arguments. */
intptr_t imuse_command(int opcode, const intptr_t *args);

/* 0x4d3ea0. Returns the iMUSE version (0x30) or IMUSE_ERR. */
int imuse_initialize(ImuseInitData *init);
int imuse_terminate(void);           /* 0x4d3ee0, opcode 1 */
int imuse_pause(void);               /* 0x4d3f10, opcode 3 */
int imuse_resume(void);              /* 0x4d3f30, opcode 4 */
/* 0x4d3f50, opcode 7: returns the old volume. vol -1 only queries. */
int imuse_set_group_vol(int group, int vol);
int imuse_start_sound(int sound, int priority); /* 0x4d3f80, opcode 8 */
int imuse_stop_sound(int sound);                /* 0x4d3fb0, opcode 9 */
int imuse_stop_all_sounds(void);                /* opcode 10 */
int imuse_get_next_sound(int sound);            /* opcode 11: next ID > sound, 0 = none */
int imuse_set_param(int sound, int param, int value);           /* 0x4d3fd0, 12 */
int imuse_get_param(int sound, int param);                      /* 0x4d4000, 13 */
int imuse_fade_param(int sound, int param, int value, int ticks); /* 0x4d4030, 14 */
int imuse_set_hook(int sound, int hook);                        /* opcode 15 */
int imuse_get_hook(int sound);                                  /* opcode 16 */
/* 0x4d4060, opcode 17: when sound plays a TEXT marker equal to marker (NULL
 * or "" = any), run opcode with args, or fn (opcode IMUSE_OP_CALLBACK), or
 * the init data's trigger_callback (opcode 0). One-shot. */
int imuse_set_trigger(int sound, const char *marker, int opcode, ImuseCallback fn,
                      const intptr_t args[10]);
/* opcode 18/19: -1 (or NULL marker) matches anything. */
int imuse_check_trigger(int sound, const char *marker, int opcode);
int imuse_clear_trigger(int sound, const char *marker, int opcode);
/* opcode 20: run opcode/fn with args after ticks 60 Hz ticks. */
int imuse_defer_command(int ticks, int opcode, ImuseCallback fn, const intptr_t args[10]);
/* 0x4d40c0, opcode 25: play sound streamed through buffer buf_id. */
int imuse_start_stream(int sound, int priority, int buf_id);
/* opcode 26: replace the streamed sound old_sound by new_sound, crossfading
 * fade_ms (max 2000) of the old one's buffered data. */
int imuse_switch_stream(int old_sound, int new_sound, int fade_ms, int sync, int keep_offset);
int imuse_process_streams(void); /* 0x4d40f0, opcode 27: refill stream buffers */
int imuse_query_stream(int sound, int32_t *buf_size, int32_t *critical, int32_t *free_space,
                       int32_t *paused);                                    /* opcode 28 */
int imuse_feed_stream(int sound, const uint8_t *data, int32_t size, int paused); /* 29 */

/* 0x4da7b0: the periodic handler (also stored in init->timer_handler). */
void imuse_timer_handler(void);

/* Port-only: print iMUSE's own diagnostics ("ERR: ...") to stderr. */
void imuse_set_debug(int on);

#endif
