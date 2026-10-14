/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The shell context shared by the Graphview and the Mapview (ui/views.h). */
#include "ui/views.h"

#include "game/world.h"

ViewsHooks views_hooks;

static WmWindow *g_main;       /* 0x5f7460 main_window */
static struct MapView *g_view; /* 0x5bb120 map_view */
static int *g_paused;          /* 0x5fa61c game_paused */

void views_set_context(WmWindow *main_window, struct MapView *map_view, int *paused)
{
    g_main = main_window;
    g_view = map_view;
    g_paused = paused;
}

WmWindow *views_main_window(void) { return g_main; }
struct MapView *views_map_view(void) { return g_view; }

void views_set_paused(int paused)
{
    if (g_paused)
        *g_paused = paused;
}

int views_select_plane(int plane)
{
    if (plane < GS_PLANE_HELL || plane > GS_PLANE_HEAVEN)
        return g_world.plane;
    if (views_hooks.select_plane)
        return views_hooks.select_plane(plane);
    int prev = g_world.plane;
    world_select_plane(plane);
    return prev;
}

/* 0x4081e6 */
void views_draw_title(WmWindow *w) { wm_draw_title(w, NULL, 500, 0x1a, 0x66); }

void views_control_help(WmWindow *c, const char *text)
{
    if (!views_hooks.help_balloon || !c || !c->parent)
        return;
    int x = ((c->rect.right - c->rect.left) >> 1) + c->parent->rect.left + c->rect.left;
    int y = ((c->rect.bottom - c->rect.top) >> 1) + c->rect.top + c->parent->rect.top;
    views_hooks.help_balloon(x, y, 0x78, text);
}
