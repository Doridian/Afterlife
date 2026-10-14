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

/* Window events, in the order and granularity the original's main window
 * procedure (main_wnd_proc 0x4cb7ad) got them as Win32 messages. */
typedef enum {
    PLAT_EV_NONE,
    PLAT_EV_QUIT,       /* the application is being told to quit (no window message in the original; treat as CLOSE) */
    PLAT_EV_KEY_DOWN,   /* WM_KEYDOWN / WM_SYSKEYDOWN (auto-repeat included): vk */
    PLAT_EV_KEY_UP,     /* WM_KEYUP / WM_SYSKEYUP: vk */
    PLAT_EV_MOUSE_MOVE, /* WM_MOUSEMOVE: x, y */
    PLAT_EV_MOUSE_DOWN, /* WM_xBUTTONDOWN: button, x, y */
    PLAT_EV_MOUSE_UP,   /* WM_xBUTTONUP: button, x, y */
    PLAT_EV_CHAR,       /* WM_CHAR (what TranslateMessage makes of a key down): ch */
    PLAT_EV_ACTIVATE,   /* WM_ACTIVATE: active = gained (true) or lost focus */
    PLAT_EV_MINIMIZE,   /* WM_SYSCOMMAND SC_MINIMIZE */
    PLAT_EV_RESTORE,    /* WM_SYSCOMMAND SC_RESTORE */
    PLAT_EV_EXPOSE,     /* WM_PAINT for the whole window */
    PLAT_EV_CLOSE,      /* WM_CLOSE: the window's close button */
} PlatEventType;

typedef struct {
    PlatEventType type;
    int vk;         /* key events: Windows virtual-key code (PLAT_VK_*, 'A'-'Z', '0'-'9') */
    uint8_t ch;     /* PLAT_EV_CHAR: character, Windows-1252 */
    bool active;    /* PLAT_EV_ACTIVATE */
    int button;     /* 1 = left, 2 = middle, 3 = right */
    int x, y;       /* mouse position in framebuffer coordinates */
} PlatEvent;

/* Windows virtual-key codes: the game's key vocabulary (WM_KEYDOWN wParam).
 * Letters and digits are their uppercase ASCII codes. */
enum {
    PLAT_VK_BACK = 0x08, PLAT_VK_TAB = 0x09, PLAT_VK_RETURN = 0x0d,
    PLAT_VK_SHIFT = 0x10, PLAT_VK_CONTROL = 0x11, PLAT_VK_MENU = 0x12, PLAT_VK_PAUSE = 0x13,
    PLAT_VK_CAPITAL = 0x14, PLAT_VK_ESCAPE = 0x1b, PLAT_VK_SPACE = 0x20,
    PLAT_VK_PRIOR = 0x21, PLAT_VK_NEXT = 0x22, PLAT_VK_END = 0x23, PLAT_VK_HOME = 0x24,
    PLAT_VK_LEFT = 0x25, PLAT_VK_UP = 0x26, PLAT_VK_RIGHT = 0x27, PLAT_VK_DOWN = 0x28,
    PLAT_VK_SNAPSHOT = 0x2c, PLAT_VK_INSERT = 0x2d, PLAT_VK_DELETE = 0x2e,
    PLAT_VK_NUMPAD0 = 0x60, /* .. 0x69 */
    PLAT_VK_MULTIPLY = 0x6a, PLAT_VK_ADD = 0x6b, PLAT_VK_SUBTRACT = 0x6d, PLAT_VK_DECIMAL = 0x6e,
    PLAT_VK_DIVIDE = 0x6f, PLAT_VK_F1 = 0x70, /* .. PLAT_VK_F12 = 0x7b */
    PLAT_VK_NUMLOCK = 0x90, PLAT_VK_SCROLL = 0x91,
    PLAT_VK_OEM_1 = 0xba /* ;: */, PLAT_VK_OEM_PLUS = 0xbb, PLAT_VK_OEM_COMMA = 0xbc,
    PLAT_VK_OEM_MINUS = 0xbd, PLAT_VK_OEM_PERIOD = 0xbe, PLAT_VK_OEM_2 = 0xbf /* /? */,
    PLAT_VK_OEM_3 = 0xc0 /* `~ */, PLAT_VK_OEM_4 = 0xdb /* [{ */, PLAT_VK_OEM_5 = 0xdc /* \| */,
    PLAT_VK_OEM_6 = 0xdd /* ]} */, PLAT_VK_OEM_7 = 0xde /* '" */,
};

/* Starts the video and input backend and creates the main window hidden,
 * like the original's hidden 1x1 window from app_register_class (0x4cd892).
 * plat_video_set_mode shows it. */
bool plat_init(const char *title, int width, int height);
void plat_shutdown(void);
/* The main window's caption (INI "Window name"). */
void plat_video_set_title(const char *title);

/* 8-bit indexed framebuffer, width * height bytes, rows top to bottom. */
uint8_t *plat_video_framebuffer(void);
int plat_video_width(void);
int plat_video_height(void);
void plat_video_set_palette(const PlatColor *colors, int first, int count);
void plat_video_present(void);
/* Replaces CreateWindowEx at the resolution / ChangeDisplaySettingsA:
 * (re)creates the framebuffer at width x height (contents undefined) and
 * sizes the window, fullscreen or not. The palette is kept. */
bool plat_video_set_mode(int width, int height, bool fullscreen);
/* Desktop size in pixels, or 0 x 0 if unknown. */
void plat_video_desktop_size(int *width, int *height);

/* Mouse cursor (USER32 LoadCursor/SetCursor/ShowCursor/SetCursorPos).
 * plat_cursor_create takes w * h RGBA pixels (4 bytes each, A = 0 is
 * transparent). A NULL cursor means the system arrow. */
typedef struct PlatCursor PlatCursor;
PlatCursor *plat_cursor_create(const uint8_t *rgba, int w, int h, int hot_x, int hot_y);
void plat_cursor_destroy(PlatCursor *cursor);
void plat_cursor_set(PlatCursor *cursor);
void plat_cursor_show(bool show);
/* Moves the pointer to framebuffer coordinates (x, y). */
void plat_mouse_warp(int x, int y);

/* PeekMessage(PM_REMOVE): takes one pending event, false when there is
 * none. Never blocks. */
bool plat_poll_event(PlatEvent *ev);
/* Gives the host a chance to run (WebAssembly: back to the browser). */
void plat_yield(void);

/* timeGetTime: milliseconds since some fixed point, wraps at 2^32. There is
 * no timer thread (timeSetEvent): game/timer.c emulates the periodic
 * callback from the message pump with this clock, so it works on WASM. */
uint32_t plat_ticks_ms(void);
void plat_delay_ms(uint32_t ms);

/* Audio (winmm waveOut*): the game mixes signed 16-bit stereo itself
 * (iMUSE, src/imuse/waveapi.c) and pushes it. Single-threaded: nothing is
 * called back; the game tops the queue up from its periodic tick.
 * plat_audio_open needs plat_init (SDL audio) first. */
bool plat_audio_open(int rate);
void plat_audio_close(void);
void plat_audio_queue(const int16_t *frames, int frame_count);
int plat_audio_queued_frames(void);
/* waveOutReset + close on pause, reopen on resume: pausing also drops
 * whatever is still queued. */
void plat_audio_pause(bool paused);

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
