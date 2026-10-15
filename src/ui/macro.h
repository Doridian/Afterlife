/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Macromanager (0x4619f0-0x463b6f, docs/game-map.md WP6b): realm-wide
 * settings for the Fate Structures (zone buildings, tile sets 14 in Hell /
 * 15 in Heaven), applied to every building of the kinds selected, for a
 * price:
 *
 *   - which kinds: the seven punishments of Hell plus generic ones, the
 *     seven rewards of Heaven plus generic ones (a bit each, Hell in the
 *     low byte, Heaven in the next);
 *   - lock / unlock (sim cell +0xe bit 7, "locked in its current form",
 *     the Microview's lock button), or leave the lock alone; 1 Penny each;
 *   - the funding focus slider, Research (left, permanent residents) to
 *     Production (right, temporary ones): manual balance sets sim +0x10 to
 *     the slider and +0xf to its complement, at the soul rate per building;
 *     auto balance sets them from each building's temporary share, at 50
 *     times that; or leave the balance alone.
 *
 * "Implement" (macro_apply(true)) charges the Pennies and resets the
 * window. The window shows how many buildings the setting affects and
 * what it would cost (macro_apply(false)).
 *
 * The shell (WP7) creates the window once (macro_create, hidden, as
 * game_main does) and shows / hides it with the Window menu's command
 * 0x17 (macro_open() shows it, the close button sends 0x17 through
 * views_hooks.menu_command); sim_update calls macro_refresh() at the end
 * of the year if it is open (g_sim_hooks.views_changed, see
 * infoviews_install_hooks in ui/views.h), as do views_refresh_all and the
 * build tools. Context and hooks: ui/views.h.
 */
#ifndef AFTERLIFE_UI_MACRO_H
#define AFTERLIFE_UI_MACRO_H

#include <stdbool.h>
#include <stdint.h>

#include "ui/wm.h"

/* Control ids (the original keeps its buttons in an array by id). */
enum {
    MACRO_HELL_KIND = 0x65,   /* 0x65..0x6c: Hell kind k (7 = generic) */
    MACRO_HEAVEN_KIND = 0x6d, /* 0x6d..0x74: Heaven kind k */
    MACRO_ALL_HELL = 0x75,
    MACRO_ALL_HEAVEN = 0x76,
    MACRO_IMPLEMENT = 0x77,
    MACRO_LOCK = 0x78,
    MACRO_UNLOCK = 0x79,
    MACRO_AUTO = 0x7a,   /* auto balance */
    MACRO_MANUAL = 0x7b, /* manual balance (the slider) */
    MACRO_CLOSE = 0x7c
};

/* The slider track: x 0x31..0x10a for the knob, y 0x13 (window-local). */
#define MACRO_SLIDER_Y 0x13

typedef struct MacroSettings {
    uint32_t kinds;   /* 0x639040: bit k Hell kind k, bit 8 + k Heaven kind k */
    uint8_t focus;    /* 0x639048: 1..0xff, 0x80 in the middle */
    uint8_t lock;     /* 0x65fe94 */
    uint8_t unlock;   /* 0x65fe98 */
    uint8_t auto_bal; /* 0x65fe9c */
    uint8_t manual;   /* 0x65fea0 */
} MacroSettings;

/* 0x461c9b macro_create: the window (bitmap 0x514 at the main window's
 * client top-left + 20) with its 24 buttons and the slider, hidden. */
bool macro_create(void);
/* Port-only: destroys the window and frees what create loaded. */
void macro_destroy(void);
WmWindow *macro_window(void); /* 0x5f7468 */

/* 0x4620aa: resets the settings (nothing selected, manual balance at the
 * middle), shows and raises the window. */
void macro_open(void);
/* 0x46313b macro_refresh: redraws the settings' labels and the count and
 * cost line (or "Done implementing settings..."). No-op without the window. */
void macro_refresh(void);

/* 0x462164: counts the buildings the settings affect and their cost
 * (macro_cost()); with `commit` applies them, pays and refreshes the views
 * (views_hooks... the shell's views_refresh_all through
 * macro_hooks.refresh_all). Returns the count. Switches the selected plane
 * (to 1 then 3) as the original does and leaves it there; macro_refresh
 * restores it. */
int macro_apply(bool commit);
uint32_t macro_cost(void); /* 0x65fe90, from the last macro_apply */

/* 0x4635d5's focus for the slider at window x (port-only). */
uint8_t macro_slider_focus(int x);

MacroSettings macro_settings(void);
void macro_set_settings(const MacroSettings *s);

typedef struct MacroHooks {
    void (*refresh_all)(void);       /* views_refresh_all 0x408c4d after implementing */
    void (*microview_refresh)(void); /* microview_refresh after implementing */
    /* the questions: ctl_message_box(-1, -1, title, text, buttons, 0);
     * NULL = that */
    int (*message)(const char *title, const char *text, const char *buttons);
    void (*sound)(uint32_t id, int prio); /* snd_play (the cash register); NULL = silent */
} MacroHooks;
extern MacroHooks macro_hooks;

#endif
