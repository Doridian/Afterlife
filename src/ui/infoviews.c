/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The WP6b views' hook wiring: see infoviews_install_hooks in ui/views.h. */
#include "game/build.h"
#include "game/karma.h"
#include "game/sim.h"
#include "game/soultrack.h"
#include "ui/macro.h"
#include "ui/microview.h"
#include "ui/planetview.h"
#include "ui/soulview.h"
#include "ui/views.h"

void infoviews_changed(void)
{
    microview_refresh();
    if (wm_is_visible(macro_window()))
        macro_refresh();
}

void infoviews_install_hooks(void)
{
    if (!g_sim_hooks.views_changed)
        g_sim_hooks.views_changed = infoviews_changed;
    if (!g_sim_hooks.soulview_age)
        g_sim_hooks.soulview_age = soulview_age;
    if (!g_build_hooks.soulview_reset)
        g_build_hooks.soulview_reset = soulview_reset;
    if (!g_build_hooks.soulview_rotated)
        g_build_hooks.soulview_rotated = soultrack_rotated;
    if (!soultrack_hooks.changed)
        soultrack_hooks.changed = soulview_status;
    if (!soultrack_hooks.refresh)
        soultrack_hooks.refresh = soulview_refresh;
    if (!planetview_hooks.soul_name)
        planetview_hooks.soul_name = soulview_soul_name;
    if (!g_karma_hooks.views_changed)
        g_karma_hooks.views_changed = microview_refresh;
    if (!macro_hooks.microview_refresh)
        macro_hooks.microview_refresh = microview_refresh;
}

void infoviews_microview_show(bool on)
{
    WmWindow *w = microview_window();
    if (!w)
        return;
    wm_show(w, on);
    if (on)
        wm_raise(w);
}

void infoviews_soulview_show(bool on)
{
    if (on)
        soulview_open();
    else if (soulview_window())
        wm_show(soulview_window(), false);
}

void infoviews_macro_show(bool on)
{
    if (on)
        macro_open();
    else if (macro_window())
        wm_show(macro_window(), false);
}
