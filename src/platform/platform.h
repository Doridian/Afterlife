/*
 * Platform layer. This is the only code that talks to the OS; the decompiled
 * game code calls these instead of the Win32 APIs ALIFE.EXE imported.
 *
 * The original's OS surface is small (see docs/binaries.md):
 *   GDI 8-bit DIB section + palette  -> plat_video_*
 *   winmm waveOut*                   -> plat_audio_*
 *   winmm timeSetEvent/timeGetTime   -> plat_ticks_ms, plat_timer_*
 *   USER32 message pump, cursor      -> plat_pump, plat_event
 *   KERNEL32 file I/O                -> plat_fopen (case-insensitive lookup)
 */
#ifndef AFTERLIFE_PLATFORM_H
#define AFTERLIFE_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint8_t r, g, b;
} PlatColor;

typedef enum {
    PLAT_EV_NONE,
    PLAT_EV_QUIT,
    PLAT_EV_KEY_DOWN,
    PLAT_EV_KEY_UP,
    PLAT_EV_MOUSE_MOVE,
    PLAT_EV_MOUSE_DOWN,
    PLAT_EV_MOUSE_UP,
} PlatEventType;

typedef struct {
    PlatEventType type;
    int key;        /* SDL keycode for key events */
    int button;     /* 1 = left, 2 = middle, 3 = right */
    int x, y;       /* mouse position in framebuffer coordinates */
} PlatEvent;

bool plat_init(const char *title, int width, int height);
void plat_shutdown(void);

/* 8-bit indexed framebuffer, width * height bytes, rows top to bottom. */
uint8_t *plat_video_framebuffer(void);
int plat_video_width(void);
int plat_video_height(void);
void plat_video_set_palette(const PlatColor *colors, int first, int count);
void plat_video_present(void);

/* Returns false when the queue is empty. Call regularly; on WebAssembly this
 * is also where control is handed back to the browser. */
bool plat_poll_event(PlatEvent *ev);
void plat_yield(void);

uint32_t plat_ticks_ms(void);
void plat_delay_ms(uint32_t ms);

/* Audio: the game mixes signed 16-bit stereo itself (iMUSE) and pushes it. */
bool plat_audio_open(int rate);
void plat_audio_queue(const int16_t *frames, int frame_count);
int plat_audio_queued_frames(void);

/* Opens a data file by DOS-style name, matching case-insensitively against
 * the data directory. Returns NULL if not found. */
void plat_set_data_dir(const char *dir);
FILE *plat_fopen_data(const char *name);

/* Read-only data files (KERNEL32/CRT file I/O). Same name lookup as
 * plat_fopen_data. seek/tell use the CRT whence values 0 = set, 1 = current,
 * 2 = end, which is what the original passes around. */
typedef struct PlatFile PlatFile;

PlatFile *plat_file_open(const char *name);
void plat_file_close(PlatFile *f);
size_t plat_file_read(PlatFile *f, void *buf, size_t size, size_t count); /* items read */
int plat_file_seek(PlatFile *f, long offset, int whence);                 /* 0 on success */
long plat_file_tell(PlatFile *f);                                          /* -1 on error */

#endif
