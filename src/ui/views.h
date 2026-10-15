/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * What the information views (Graphview, src/ui/graphview.h; Mapview,
 * src/ui/mapview.h) need from the game shell (WP7, docs/game-map.md).
 *
 * The original reads the shell's globals directly: the main window
 * (main_window 0x5f7460, the views are its children), the main map view
 * (map_view 0x5bb120: the Mapview turns with its rotation and outlines what
 * it shows; its plane switch 0x42b102 also switches the simulation's
 * current plane) and the pause flag (game_paused 0x5fa61c, set while a
 * view is dragged). The shell hands these over once with
 * views_set_context(). Calls into the shell's own modules go through
 * views_hooks; any hook may be NULL.
 */
#ifndef AFTERLIFE_UI_VIEWS_H
#define AFTERLIFE_UI_VIEWS_H

#include <stdbool.h>

#include "ui/wm.h"

struct MapView;

typedef struct ViewsHooks {
    /* 0x42b102 map_view_select_plane on the main view, both halves (the
     * shell owns the main view: world_select_plane plus the renderer's
     * map_view_select_plane). Returns the plane selected before (map_view
     * +0x47). NULL: only world_select_plane runs. */
    int (*select_plane)(int plane);
    /* 0x478a14 help_balloon_show(x, y, width, text, 1, 1): the right-click
     * help balloon at screen (x, y). NULL: no help. */
    void (*help_balloon)(int x, int y, int width, const char *text);
    /* 0x406be8: scroll the main view so map cell (x, y) of `plane` is in the
     * middle (the Mapview's click-to-scroll). */
    void (*center_on_cell)(int plane, int x, int y);
    /* wm_send(main_window, item, WM_MSG_MENU_COMMAND, id, 0): the views'
     * close buttons run the Window menu's toggle (0x19 Graphview, 0x1a
     * Mapview) through game_view_proc. NULL: the view just hides itself. */
    void (*menu_command)(unsigned id);
} ViewsHooks;

extern ViewsHooks views_hooks;

/* The seven virtues (the Heaven reward kinds, 0x66c600) and the seven sins
 * (the Hell punishment kinds, 0x66c658), as the views label them. */
extern const char *const views_virtue_names[7];
extern const char *const views_sin_names[7];
/* The sixteen belief systems (0x664918), "HAHAALFSUMA" .. "OCRARALFSUMA". */
extern const char *const views_belief_names[16];

/* The main window (parent of the views), the main map view (NULL: the
 * Mapview draws unrotated and without the view outline) and the pause flag
 * (NULL: none). */
void views_set_context(WmWindow *main_window, struct MapView *map_view, int *paused);

WmWindow *views_main_window(void);
struct MapView *views_map_view(void);
void views_set_paused(int paused);

/* map_view_select_plane (0x42b102) as the views call it (hooks.select_plane
 * or world_select_plane). Returns the plane selected before. */
int views_select_plane(int plane);

/* 0x4081e6 wnd_draw_title_std: wm_draw_title(w, NULL, 500, 0x1a, 0x66). */
void views_draw_title(WmWindow *w);

/* Help balloon for a control: centred on it, 120 px wide, as every
 * "This button ..." function of the views does. */
void views_control_help(WmWindow *control, const char *text);

/* WP6b (Microview, Macromanager, Soulview; src/ui/infoviews.c): connects
 * the simulation's hooks to those windows where they are still NULL:
 * g_sim_hooks.views_changed (microview_refresh, and macro_refresh if the
 * Macromanager is shown) and soulview_age, g_build_hooks.soulview_reset /
 * soulview_rotated, soultrack_hooks (the Soulview's redraws),
 * planetview_hooks.soul_name, macro_hooks.microview_refresh. The shell
 * may set its own afterwards (views_refresh_all for
 * macro_hooks.refresh_all, news for microview_hooks). */
void infoviews_install_hooks(void);
/* g_sim_hooks.views_changed: microview_refresh() and, if the Macromanager
 * is visible, macro_refresh() (sim_update phase 80). */
void infoviews_changed(void);
/* The Window menu's show / hide of the WP6b windows, in the form of
 * shell_hooks (game/shell.h): microview_show / soulview_show /
 * macro_show. (Showing the Macromanager resets it, as 0x4620aa does;
 * microview_cell = microview_open, soulview_cell = soulview_track_cell.) */
void infoviews_microview_show(bool on);
void infoviews_soulview_show(bool on);
void infoviews_macro_show(bool on);

#endif
