/*
 * Settings read from ALIFE.INI. The original has no settings struct: each
 * subsystem's init reads its own keys with ini_get_string into its own
 * globals (addresses below). AlConfig gathers them, and the cfg_read_*
 * functions are the INI-reading parts of those init functions, so the
 * subsystems can be ported separately. See docs/startup.md.
 */
#ifndef AFTERLIFE_GAME_CONFIG_H
#define AFTERLIFE_GAME_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/* di_display_mode (0x545428) */
enum {
    DI_MODE_FULLSCREEN = 1, /* window covers the desktop */
    DI_MODE_WINDOWED = 2,   /* smaller than the desktop: framed window */
    DI_MODE_FORCE = 4,      /* "Force display settings = ON": switch modes */
};

typedef struct {
    /* he_initialize 0x4c98bb */
    uint32_t heap_size; /* he_heap_size 0x6727ec: "Heap size" KB * 1024, default 4 MB */

    /* rc_initialize 0x4cd9b0 */
    char cd_path[0x200];       /* rc_cd_path 0x64d810: "CD Path", required */
    char resource_file[0x100]; /* rc_resource_file 0x64da10: "Resource file", required */

    /* di_initialize 0x4ca1c0 */
    bool has_color_depth;        /* "Color depth" present and not "DONT CARE" */
    int color_depth;             /* bits per pixel the user expects */
    bool force_display;          /* "Force display settings" == "ON" */
    bool resolution_max;         /* "Resolution" missing or "MAX" */
    int req_width, req_height;   /* parsed "Resolution" (desktop size if MAX) */
    int width, height;           /* after clamping to the desktop */
    int display_mode;            /* DI_MODE_* */
    char window_name[0x400];     /* window_name 0x5464a0: "Window name" */

    /* snd_initialize 0x4b9510 */
    bool sound_enabled;          /* false if "Sound" == "NONE" (any case) */
    int32_t music_stream[3];     /* "Music stream" size, critical, load: 0x67259c, 0x6725a4, 0x6725a0 */
    int32_t voice_stream[3];     /* "Voice stream" size, critical, load: 0x67258c, 0x672594, 0x672590 */
    int mixer_channels;          /* "Mixer channels", default 8 */
} AlConfig;

extern AlConfig g_config;

/* Defaults from the original's .data / .bss initial values. */
void cfg_set_defaults(AlConfig *cfg);

/* he_initialize (0x4c98bb), INI part. */
void cfg_read_heap(AlConfig *cfg);

/* rc_initialize (0x4cd9b0), INI part. Returns 0 on success, else 1 if
 * "CD Path" is missing, 2 if "Resource file" is missing (both fatal in the
 * original: "Cannot locate the resource file path in the INI file"). */
int cfg_read_resource_paths(AlConfig *cfg);

/* di_initialize (0x4ca1c0), INI part: "Color depth", "Force display
 * settings", "Resolution", "Window name". The desktop size and depth come
 * from the caller. Sets *depth_mismatch when the original would ask whether
 * to continue with the wrong color depth. */
void cfg_read_display(AlConfig *cfg, int desktop_w, int desktop_h, int desktop_bpp,
                      bool *depth_mismatch);

/* snd_initialize (0x4b9510), INI part: "Sound", "Music stream", "Voice
 * stream", "Mixer channels". Each stream setting is "size, critical,
 * load": the stream buffer size and its refill thresholds, which iMUSE
 * reads through buf_info (see docs/imuse.md). */
void cfg_read_sound(AlConfig *cfg);

/* cfg_save_games_dir (0x480375): copies "Save games" into out (>= 0x800
 * bytes). Returns false, after "Missing INI profile string", if absent. */
bool cfg_save_games_dir(char *out);

/* cfg_cd_dir (0x4803dc): drive + directory of "CD Path", no trailing
 * backslash (e.g. "D:" for "D:\ALIFE.000"). out >= 0x800 bytes. */
bool cfg_cd_dir(char *out);

/* di_save_screenshot (0x4ccf6e), INI part: "Savepath" cut at the first
 * space, with a trailing backslash. out >= 0x400 bytes. */
void cfg_savepath(char *out);

/* di_video_benchmark (0x42a6c3), INI part: "Video Settings" as a number
 * (bit 1: slow bitmap copy, bit 0: slow window update). Returns false if
 * absent, in which case the original times 50 blits of each kind. */
bool cfg_video_settings(int *flags);

#endif
