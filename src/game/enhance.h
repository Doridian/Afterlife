/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Port-only: the "enhancements" switch and the port's own settings. See
 * docs/enhancements.md.
 *
 * By default the port behaves like the original, bugs included. With
 * g_enhancements on, the places marked `ENHANCEMENT:` in the code fix
 * obvious bugs of the original, and quality-of-life additions (the UI
 * scale) become available.
 *
 * The settings live in their own INI (ALIFEPORT.INI in the user's settings
 * folder, plat_pref_path), so ALIFE.INI stays exactly what the original
 * reads:
 *
 *   [Port]
 *   Enhancements = 1
 *   UI scale = 0        ; 0 = automatic, else 1..4
 *
 * Precedence: command line (--enhancements, --no-enhancements,
 * --ui-scale=N, --port-ini=FILE), then the AFTERLIFE_ENHANCEMENTS
 * environment variable (1 / 0, used by the tests), then the file.
 */
#ifndef AFTERLIFE_GAME_ENHANCE_H
#define AFTERLIFE_GAME_ENHANCE_H

#include <stdbool.h>

#define PORT_INI_NAME "ALIFEPORT.INI"

/* The switch the gated code tests. Off unless the settings turn it on. */
extern bool g_enhancements;

typedef struct {
    bool enhancements;
    int ui_scale; /* 0 = automatic (from the pixel density), 1..4 */
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
 * saved) and sets g_enhancements. Arguments it doesn't know are ignored. */
void enhance_init(int argc, char **argv);
/* The tests: AFTERLIFE_ENHANCEMENTS=1 / 0 sets g_enhancements. */
void enhance_init_env(void);

/* The UI scale to ask the platform for: 1 with enhancements off, else the
 * setting (0 = automatic). Takes effect at the next start. */
int enhance_ui_scale_request(void);

/* Saves g_port_settings to port_settings_path(). */
bool enhance_save(void);

/* The Port Options dialog (Global menu, port-only). Toggles take effect at
 * once; a UI scale change at the next start. */
void enhance_options_dialog(void);

#endif
