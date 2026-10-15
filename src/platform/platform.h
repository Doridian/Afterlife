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
    PLAT_EV_QUIT,       /* the application is being told to quit (SIGTERM/SIGINT; no window message in the original): main_wnd_proc exits at once */
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
    PLAT_EV_RESIZE,     /* port: the port window's pixel size changed (plat_video_target_size) */
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

/* The window's icon, w * h RGBA pixels; on the web, the page's favicon. */
void plat_window_set_icon(const uint8_t *rgba, int w, int h);

/* 8-bit indexed framebuffer, plat_video_pitch() * height bytes, rows top to
 * bottom. */
uint8_t *plat_video_framebuffer(void);
int plat_video_width(void);
/* Bytes per framebuffer row (>= width; SDL aligns INDEX8 rows to 4). */
int plat_video_pitch(void);
int plat_video_height(void);
void plat_video_set_palette(const PlatColor *colors, int first, int count);
void plat_video_present(void);
/* Replaces CreateWindowEx at the resolution / ChangeDisplaySettingsA:
 * (re)creates the framebuffer at width x height (contents undefined) and
 * sizes the window, fullscreen or not. The palette is kept. With the port
 * window (plat_window_open) the window keeps its geometry; fullscreen
 * still switches to fullscreen. */
bool plat_video_set_mode(int width, int height, bool fullscreen);
/* Desktop size in pixels, or 0 x 0 if unknown. Divided by the UI scale. */
void plat_video_desktop_size(int *width, int *height);
/* Port enhancement (docs/enhancements.md): the UI scale k, 1 by default.
 * The game sees a desktop k times smaller (plat_video_desktop_size) and
 * the framebuffer is presented k times larger, crisp. 0 picks one from the
 * desktop's pixel density. Clamped so the scaled desktop stays at least
 * 640x480. Must be set before the display init (plat_init first). */
void plat_set_ui_scale(int scale);
/* The UI scale in effect: the requested one, lowered while the port
 * window is too small for it (present_logical_size). */
int plat_ui_scale(void);
/* The requested UI scale (0 resolved from the density, clamped). */
int plat_ui_scale_requested(void);

/* --- port window (docs/rendering.md "Port window") --------------------------
 * By default the game runs in a normal resizable window, and its logical
 * desktop (the framebuffer) follows the window: window pixels / UI scale,
 * at least 640x480. The platform only reports the change
 * (PLAT_EV_RESIZE); the game reallocates the framebuffer and lays its
 * windows out again (game/display.c). Alt+Enter and F11 toggle borderless
 * desktop fullscreen; those keys never reach the game. */
typedef struct {
    int w, h;        /* windowed size in desktop units (SDL window coordinates); 0 = default */
    int x, y;        /* windowed position; PLAT_WINDOW_POS_DEFAULT = centred */
    bool fullscreen; /* borderless desktop fullscreen */
    bool maximized;
} PlatWindowState;
#define PLAT_WINDOW_POS_DEFAULT (-0x7fffffff)

/* Opens the port window (instead of plat_video_set_mode sizing it):
 * `saved` geometry if it fits the desktop, else the logical size pref_w x
 * pref_h (0: none) times the UI scale if it fits, else 80 % of the usable
 * desktop. Then call plat_video_target_size and plat_video_set_mode with
 * that size. */
void plat_window_open(const PlatWindowState *saved, int pref_w, int pref_h);
/* Whether plat_window_open ran (the framebuffer follows the window). */
bool plat_window_managed(void);
/* The current state: the last windowed geometry (not the fullscreen or
 * maximized one) and the flags. Still valid after plat_shutdown. */
void plat_window_get_state(PlatWindowState *out);
void plat_window_set_fullscreen(bool on);
bool plat_window_fullscreen(void);
/* Programmatic resize in desktop units (tests, --window-size); leaves
 * fullscreen and maximized first. */
void plat_window_resize(int w, int h);
/* The window's size in pixels (0 x 0 without a window). */
void plat_window_pixel_size(int *w, int *h);
/* The framebuffer size the port window wants now (window pixels / UI
 * scale, at least 640x480). True if it differs from the current one or
 * the effective UI scale would change. */
bool plat_video_target_size(int *w, int *h);
/* Reallocates the framebuffer at w x h (contents undefined, palette kept)
 * without touching the window, and updates the effective UI scale. */
bool plat_video_resize(int w, int h);
/* Framebuffer position -> window coordinates of its centre (what an SDL
 * mouse event at that pixel carries). Tests use it to inject clicks. */
void plat_video_fb_to_window(int x, int y, float *wx, float *wy);
/* Writes what the window shows now (the presented frame) to a BMP file.
 * For the port's real-window checks. */
bool plat_video_screenshot(const char *path);

/* Mouse cursor (USER32 LoadCursor/SetCursor/ShowCursor/SetCursorPos).
 * plat_cursor_create takes w * h RGBA pixels (4 bytes each, A = 0 is
 * transparent). A NULL cursor means the system arrow. */
typedef struct PlatCursor PlatCursor;
PlatCursor *plat_cursor_create(const uint8_t *rgba, int w, int h, int hot_x, int hot_y);
void plat_cursor_destroy(PlatCursor *cursor);
void plat_cursor_set(PlatCursor *cursor);
/* Returns whether the cursor was visible before. */
bool plat_cursor_show(bool show);
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
/* How many frames the mixer may keep queued ahead (the latency the device
 * adds to each sound). */
int plat_audio_queue_limit(void);
/* waveOutReset + close on pause, reopen on resume: pausing also drops
 * whatever is still queued. */
void plat_audio_pause(bool paused);

/* Opens a data file by DOS-style name, matching case-insensitively against
 * the data directory. Returns NULL if not found. */
void plat_set_data_dir(const char *dir);
FILE *plat_fopen_data(const char *name);
/* The data directory, and the real path of a data file (same lookup as
 * plat_fopen_data). False if it doesn't exist. */
const char *plat_data_dir(void);
bool plat_data_path(const char *name, char *out, size_t out_len);
/* Port-only: `name` in the user's writable settings folder (created if
 * needed; ~/.local/share/Afterlife/Afterlife/ on Linux). */
bool plat_pref_path(const char *name, char *out, size_t out_len);
/* Where saved games go when the platform has its own place for them
 * (WebAssembly: the pref folder, which web/pre.js keeps in IndexedDB; the
 * data folder there is a package in memory), else NULL (the INI "Save
 * games", else the data dir). */
const char *plat_save_dir(void);

/* A native file picker (port: the original has its own slot dialogs).
 * Asynchronous: begin opens it (false if there is none, e.g. headless or
 * the web build), then poll from a modal loop: 0 still open, 1 chosen
 * (path in out), -1 cancelled. `pattern` is a ';'-separated list of
 * extensions without dots ("alf;asc"). */
bool plat_file_dialog_begin(bool save, const char *default_location, const char *filter_name, const char *pattern);
int plat_file_dialog_poll(char *out, size_t out_len);

/* Port-only: a file dropped onto the window (SDL_EVENT_DROP_FILE; on the
 * web SDL copies it to /tmp/filedrop/N/). The latest drop waits until it is
 * taken; take copies its path into out and clears it. release deletes the
 * file if it is such a temporary copy (else does nothing). */
bool plat_take_dropped_file(char *out, size_t out_len);
void plat_release_dropped_file(const char *path);

/* Port-only: hands the file at `path` to the user as a download named
 * `name` (the web build; false elsewhere). The file can be removed after. */
bool plat_can_download(void);
bool plat_download_file(const char *path, const char *name);

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
