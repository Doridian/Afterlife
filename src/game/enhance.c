/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Port-only: the enhancements switch, the port's settings file and the Port
 * Options dialog. See enhance.h and docs/enhancements.md.
 */
#include "game/enhance.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"
#include "platform/present.h"
#include "ui/portopts.h"

bool g_enhancements;
PortSettings g_port_settings;

static char g_path[1024];
static bool g_path_set;

void port_settings_defaults(PortSettings *s)
{
    s->enhancements = false;
    s->ui_scale = 0;
    s->window = (PlatWindowState){0, 0, PLAT_WINDOW_POS_DEFAULT, PLAT_WINDOW_POS_DEFAULT, false, false};
}

/* (no strcasecmp: not in C11) */
static int ci_cmp(const char *a, const char *b)
{
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b))
        a++, b++;
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static char *trim(char *p)
{
    while (*p && isspace((unsigned char)*p))
        p++;
    size_t n = strlen(p);
    while (n && isspace((unsigned char)p[n - 1]))
        p[--n] = '\0';
    return p;
}

static bool parse_bool(const char *v, bool *out)
{
    if (!strcmp(v, "1") || !ci_cmp(v, "on") || !ci_cmp(v, "yes") || !ci_cmp(v, "true")) {
        *out = true;
        return true;
    }
    if (!strcmp(v, "0") || !ci_cmp(v, "off") || !ci_cmp(v, "no") || !ci_cmp(v, "false")) {
        *out = false;
        return true;
    }
    return false;
}

/* "WxH", both 1..32767 */
static bool parse_size(const char *v, int *w, int *h)
{
    int a, b;
    if (sscanf(v, "%d x %d", &a, &b) != 2 || a < 1 || b < 1 || a > 32767 || b > 32767)
        return false;
    *w = a;
    *h = b;
    return true;
}

/* "X,Y" (may be negative on multi-monitor desktops) */
static bool parse_pos(const char *v, int *x, int *y)
{
    int a, b;
    if (sscanf(v, "%d , %d", &a, &b) != 2 || a < -32767 || b < -32767 || a > 32767 || b > 32767)
        return false;
    *x = a;
    *y = b;
    return true;
}

static int clamp_scale(int v) { return v < 0 ? 0 : v > PRESENT_SCALE_MAX ? PRESENT_SCALE_MAX : v; }

bool port_settings_load(const char *path, PortSettings *s)
{
    FILE *f = path && path[0] ? fopen(path, "r") : NULL;
    if (!f)
        return false;
    char line[256];
    bool in_port = false;
    while (fgets(line, sizeof line, f)) {
        char *p = trim(line);
        if (*p == ';' || *p == '#' || !*p)
            continue;
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (end)
                *end = '\0';
            in_port = !ci_cmp(trim(p + 1), "Port");
            continue;
        }
        char *eq = strchr(p, '=');
        if (!in_port || !eq)
            continue;
        *eq = '\0';
        char *key = trim(p), *val = trim(eq + 1);
        char *semi = strchr(val, ';');
        if (semi) {
            *semi = '\0';
            val = trim(val);
        }
        if (!ci_cmp(key, "Enhancements"))
            parse_bool(val, &s->enhancements);
        else if (!ci_cmp(key, "UI scale"))
            s->ui_scale = clamp_scale(atoi(val));
        else if (!ci_cmp(key, "Window size"))
            parse_size(val, &s->window.w, &s->window.h);
        else if (!ci_cmp(key, "Window position"))
            parse_pos(val, &s->window.x, &s->window.y);
        else if (!ci_cmp(key, "Maximized"))
            parse_bool(val, &s->window.maximized);
        else if (!ci_cmp(key, "Fullscreen"))
            parse_bool(val, &s->window.fullscreen);
    }
    fclose(f);
    return true;
}

bool port_settings_save(const char *path, const PortSettings *s)
{
    FILE *f = path && path[0] ? fopen(path, "w") : NULL;
    if (!f)
        return false;
    fprintf(f, "; Afterlife port settings (the original game never reads this file)\n"
               "[Port]\n"
               "; 1: fix bugs of the original and allow the extras below\n"
               "Enhancements = %d\n"
               "; 0 = automatic, else 1..%d (needs Enhancements = 1)\n"
               "UI scale = %d\n"
               "; the window, saved at exit (desktop units; no size: 80%% of the desktop)\n",
            s->enhancements ? 1 : 0, PRESENT_SCALE_MAX, clamp_scale(s->ui_scale));
    if (s->window.w > 0 && s->window.h > 0)
        fprintf(f, "Window size = %dx%d\n", s->window.w, s->window.h);
    if (s->window.x != PLAT_WINDOW_POS_DEFAULT && s->window.y != PLAT_WINDOW_POS_DEFAULT)
        fprintf(f, "Window position = %d,%d\n", s->window.x, s->window.y);
    fprintf(f, "Maximized = %d\nFullscreen = %d\n", s->window.maximized ? 1 : 0, s->window.fullscreen ? 1 : 0);
    return fclose(f) == 0;
}

void port_settings_set_path(const char *path)
{
    snprintf(g_path, sizeof g_path, "%s", path ? path : "");
    g_path_set = true;
}

const char *port_settings_path(void)
{
    if (!g_path_set) {
        if (!plat_pref_path(PORT_INI_NAME, g_path, sizeof g_path))
            g_path[0] = '\0';
        g_path_set = true;
    }
    return g_path;
}

void enhance_init_env(void)
{
    const char *e = getenv("AFTERLIFE_ENHANCEMENTS");
    bool on;
    if (e && parse_bool(e, &on)) {
        g_port_settings.enhancements = on;
        g_enhancements = on;
    }
}

void enhance_init(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (!strncmp(argv[i], "--port-ini=", 11))
            port_settings_set_path(argv[i] + 11);
    port_settings_defaults(&g_port_settings);
    port_settings_load(port_settings_path(), &g_port_settings);
    g_enhancements = g_port_settings.enhancements;
    enhance_init_env();
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--enhancements"))
            g_port_settings.enhancements = true;
        else if (!strcmp(argv[i], "--no-enhancements"))
            g_port_settings.enhancements = false;
        else if (!strncmp(argv[i], "--ui-scale=", 11))
            g_port_settings.ui_scale = clamp_scale(atoi(argv[i] + 11));
        else if (!strncmp(argv[i], "--window-size=", 14)) {
            if (parse_size(argv[i] + 14, &g_port_settings.window.w, &g_port_settings.window.h))
                g_port_settings.window.maximized = false;
        } else if (!strcmp(argv[i], "--fullscreen"))
            g_port_settings.window.fullscreen = true;
        else if (!strcmp(argv[i], "--windowed"))
            g_port_settings.window.fullscreen = false;
    }
    g_enhancements = g_port_settings.enhancements;
}

int enhance_ui_scale_request(void)
{
    return g_enhancements ? clamp_scale(g_port_settings.ui_scale) : 1;
}

bool enhance_save(void)
{
    if (plat_window_managed())
        plat_window_get_state(&g_port_settings.window);
    return port_settings_save(port_settings_path(), &g_port_settings);
}

bool enhance_save_window_state(void)
{
    if (!plat_window_managed())
        return false;
    PortSettings file;
    port_settings_defaults(&file);
    port_settings_load(port_settings_path(), &file);
    plat_window_get_state(&file.window);
    g_port_settings.window = file.window;
    return port_settings_save(port_settings_path(), &file);
}

void enhance_options_dialog(void) { portopts_run(); }
