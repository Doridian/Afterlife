/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Port-only: the port window's live relayout. See display.h.
 */
#include "game/display.h"

#include "game/enhance.h"
#include "gfx/surface.h"
#include "platform/platform.h"
#include "ui/wm.h"

DisplayHooks display_hooks;

static bool g_pending;
static uint32_t g_last;
static unsigned g_count;

void display_request_relayout(void) { g_pending = true; }

unsigned display_relayout_count(void) { return g_count; }

bool display_relayout(void)
{
    int w, h;
    if (!plat_window_managed() || !plat_video_target_size(&w, &h)) {
        g_pending = false;
        return true;
    }
    if (display_hooks.can_relayout && !display_hooks.can_relayout()) {
        g_pending = true;
        return false;
    }
    int ow = gfx_screen_width(), oh = gfx_screen_height();
    if (!gfx_screen_resize(w, h)) {
        g_pending = false;
        return true;
    }
    g_pending = false;
    g_count++;
    /* 0x4c6096: the root follows the screen (and WM_MSG_SCREEN_RESIZED,
     * which nothing handles) */
    wm_screen_resized(w, h);
    if (display_hooks.relayout)
        display_hooks.relayout(ow, oh, w, h);
    /* every other top-level window back inside the screen */
    WmWindow *root = wm_root();
    if (root) {
        GfxRect screen = {0, w - 1, 0, h - 1};
        for (WmWindow *c = root->children; c; c = c->next)
            wm_fit_into(c, &screen);
    }
    gfx_repaint();
    return true;
}

void display_idle(void)
{
    if (!g_pending)
        return;
    uint32_t now = plat_ticks_ms();
    if (now - g_last < DISPLAY_RELAYOUT_MS)
        return;
    g_last = now;
    display_relayout();
}

void display_apply_settings(bool enhancements, int ui_scale)
{
    g_enhancements = enhancements;
    g_port_settings.enhancements = enhancements;
    g_port_settings.ui_scale = ui_scale;
    plat_set_ui_scale(enhance_ui_scale_request());
    if (!display_relayout())
        g_pending = true;
}
