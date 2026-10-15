/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Port-only: the "enhancements" switch and the port's own settings. See
 * docs/enhancements.md.
 *
 * The game starts with the enhancements on (port_settings_defaults): the
 * places marked `ENHANCEMENT:` in the code fix obvious bugs of the
 * original, and quality-of-life additions (the UI scale) are available.
 * Turned off (Enhancements = 0, --no-enhancements), the port behaves like
 * the original, bugs included. g_enhancements itself starts off, so the
 * tests, which don't call enhance_init, run the original's behaviour unless
 * AFTERLIFE_ENHANCEMENTS=1.
 *
 * The settings live in their own INI (ALIFEPORT.INI in the user's settings
 * folder, plat_pref_path), so ALIFE.INI stays exactly what the original
 * reads:
 *
 *   [Port]
 *   Enhancements = 1
 *   UI scale = 0        ; 0 = automatic, else 1..4
 *   Window size = 1600x1000      ; the port window (desktop units)
 *   Window position = 120,80     ; missing: centred
 *   Maximized = 0
 *   Fullscreen = 0
 *
 * Precedence: command line (--enhancements, --no-enhancements,
 * --ui-scale=N, --port-ini=FILE), then the AFTERLIFE_ENHANCEMENTS
 * environment variable (1 / 0, used by the tests), then the file.
 */
#ifndef AFTERLIFE_GAME_ENHANCE_H
#define AFTERLIFE_GAME_ENHANCE_H

#include <stdbool.h>

#include "platform/platform.h"

#define PORT_INI_NAME "ALIFEPORT.INI"

/* The switch the gated code tests. Off until enhance_init (or
 * enhance_init_env) sets it; the settings default to on. */
extern bool g_enhancements;

typedef struct {
    bool enhancements;
    int ui_scale; /* 0 = automatic (from the pixel density), 1..4 */
    /* the port window (not an enhancement): last windowed size and place,
     * maximized, fullscreen. w = h = 0: the default size. */
    PlatWindowState window;
} PortSettings;

/* The settings as loaded (and as the Port Options dialog changes them). */
extern PortSettings g_port_settings;

void port_settings_defaults(PortSettings *s);
/* Reads `path` into *s (missing keys keep their value). False if the file
 * can't be opened. */
bool port_settings_load(const char *path, PortSettings *s);
/* Writes *s to `path`. False on failure. */
bool port_settings_save(const char *path, const PortSettings *s);

/* The settings file: --port-ini, else ALIFEPORT.INI in plat_pref_path.
 * Empty if there is none. */
void port_settings_set_path(const char *path);
const char *port_settings_path(void);

/* Startup (main): applies --port-ini, loads the file into g_port_settings,
 * applies the environment and command-line overrides (the overrides are not
 * saved; --window-size=WxH, --fullscreen and --windowed change the
 * window, whose state is saved at exit) and sets g_enhancements. Arguments
 * it doesn't know are ignored. */
void enhance_init(int argc, char **argv);
/* The tests: AFTERLIFE_ENHANCEMENTS=1 / 0 sets g_enhancements. */
void enhance_init_env(void);

/* The UI scale to ask the platform for: 1 with enhancements off, else the
 * setting (0 = automatic). display_apply_settings applies it live. */
int enhance_ui_scale_request(void);

/* Saves g_port_settings to port_settings_path(). */
bool enhance_save(void);
/* Saves the port window's current state (plat_window_get_state) into the
 * settings file, keeping the file's other keys (so command-line overrides
 * aren't saved). main registers it with atexit. */
bool enhance_save_window_state(void);

/* The Port Options window (Global menu, port-only; ui/portopts.c). Every
 * change applies at once; OK saves, Cancel puts the old settings back. */
void enhance_options_dialog(void);

#endif
