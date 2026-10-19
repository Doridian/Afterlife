/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The game shell (game/shell.h): game_main (0x401000) and its main loop,
 * the menu bar (0x409bb0 / 0x409c17) and game_view_proc's menu-command
 * switch (0x404023), game_frame_update (0x407282) with its 125 ms clock
 * (0x402747), the loading window (0x408208 / 0x408356 / 0x408995), the
 * load / save / new game flows of the main loop, and the wiring of every
 * package's hooks to the real UI. The main map view is shell_view.c.
 */
#include "game/shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "game/badthings.h"
#include "game/build.h"
#include "game/config.h"
#include "game/display.h"
#include "game/enhance.h"
#include "game/karma.h"
#include "game/mapobj.h"
#include "game/music.h"
#include "game/newworld.h"
#include "game/planet.h"
#include "game/resource.h"
#include "game/savegame.h"
#include "game/shell_int.h"
#include "game/sim.h"
#include "game/soultrack.h"
#include "game/sound.h"
#include "game/startup.h"
#include "game/timer.h"
#include "game/tutorial.h"
#include "game/util.h"
#include "game/world.h"
#include "gfx/akos.h"
#include "gfx/akos_talk.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/map_objects.h"
#include "gfx/mouse.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "platform/platform.h"
#include "ui/about.h"
#include "ui/advisors.h"
#include "ui/controls.h"
#include "ui/ctrlpanel.h"
#include "ui/debugwin.h"
#include "ui/graphview.h"
#include "ui/macro.h"
#include "ui/mapview.h"
#include "ui/menu.h"
#include "ui/microview.h"
#include "ui/news.h"
#include "ui/planetview.h"
#include "ui/soulview.h"
#include "ui/slotdlg.h"
#include "ui/statusbar.h"
#include "ui/title.h"
#include "ui/views.h"

WmWindow *g_main_window, *g_ctrlpanel_window, *g_tilechooser_window;
int g_game_paused;
uint8_t g_game_speed, g_game_in_progress, g_game_command, g_game_next_command, g_shell_repaint;
int g_game_running;
int g_shell_tool;
PlatCursor *g_shell_arrow, *g_shell_cursor, *g_shell_tool_cursors[0x17], *g_shell_busy_cursor, *g_shell_cursor_83;
ShellPickFn shell_pick_file_hook;
ShellHooks shell_hooks;

static WmWindow *g_menu_items[MENU_IDS]; /* 0x646560 */
static char g_pending_load[1024];        /* 0x65006c (a slot number in the original) */
static uint8_t *g_palette;               /* 0x638658: the game palette (resource 0x21) */
static PlatCursor *g_prev_cursor;        /* 0x6464bc */
static PlatCursor *g_busy_saved;         /* 0x4f301c */
static unsigned g_last_speed = MENU_SPEED_SLOW; /* 0x650070 */
static uint8_t g_save_kind;              /* 0x650050: the saver's 2nd argument */
static GfxSurface *g_scroll_bmps[8], *g_menu_skin[9];

/* game_frame_clock 0x402747 */
static uint32_t g_clock_last;            /* 0x4f3048 */
static int g_clock_ticks;                /* 0x650058 */
static int g_clock_status;               /* 0x65005c */
static int g_clock_music = 0x78;         /* 0x650060 */
static int g_edge_dx, g_edge_dy;         /* 0x650094 / 0x650098 */
static uint32_t g_edge_time;             /* 0x650090 */

/* the loading window (0x6464b0) */
static WmWindow *g_load_win;
static GfxSurface *g_load_bmp;           /* 0x6462e4 */
static unsigned g_load_steps, g_load_step; /* port: progress by steps (the original counts heap bytes) */
static unsigned g_load_shown;            /* 0x4f3024 */
static int g_load_joke;                  /* 0x6500a4 */
static uint32_t g_load_joke_time;        /* 0x6500a8 */

/* ======================================================================== */
/* Small services                                                             */
/* ======================================================================== */

void shell_menu_command(unsigned id)
{
    if (g_main_window)
        wm_send(g_main_window, shell_menu_item(id), WM_MSG_MENU_COMMAND, (intptr_t)id, 0);
}

WmWindow *shell_menu_item(unsigned id) { return id < MENU_IDS ? g_menu_items[id] : NULL; }
bool shell_menu_checked(unsigned id) { return shell_menu_item(id) && menu_is_checked(shell_menu_item(id)); }

void shell_menu_set_checked(unsigned id, bool checked)
{
    if (shell_menu_item(id))
        menu_set_checked(shell_menu_item(id), checked);
}

unsigned shell_last_speed(void) { return g_last_speed; }

/* 0x408ca4 ui_busy_cursor */
void shell_busy_cursor(bool on)
{
    if (on) {
        g_busy_saved = gfx_cursor_get();
        gfx_cursor_set(g_shell_busy_cursor);
    } else if (!g_busy_saved || g_busy_saved == g_shell_busy_cursor) {
        gfx_cursor_set(g_shell_arrow);
    } else {
        gfx_cursor_set(g_busy_saved);
    }
}

/* 0x408c4d views_refresh_all */
void views_refresh_all(void)
{
    graphview_refresh();
    planetview_refresh(true);
    mapview_refresh();
    if (shell_hooks.views_changed)
        shell_hooks.views_changed();
    advisor_update();
}

/* 0x408a78: closes the views before a new game */
static void close_views(void)
{
    if (shell_hooks.microview_show)
        shell_hooks.microview_show(false);
    if (shell_hooks.soulview_show)
        shell_hooks.soulview_show(false);
    if (shell_menu_checked(MENU_MACRO))
        shell_menu_command(MENU_MACRO);
    if (graphview_window() && wm_is_visible(graphview_window()))
        shell_menu_command(MENU_GRAPHVIEW);
    if (mapview_window() && wm_is_visible(mapview_window()))
        shell_menu_command(MENU_MAPVIEW);
    if (advisor_window() && wm_is_visible(advisor_window()))
        shell_menu_command(MENU_HELPERS);
    if (planetview_window() && wm_is_visible(planetview_window()))
        shell_menu_command(MENU_PLANETVIEW);
    if (shell_menu_checked(MENU_AUDIO))
        shell_menu_command(MENU_AUDIO);
    if (about_window() && wm_is_visible(about_window()))
        wm_show(about_window(), false);
    if (planetview_disaster_window() && wm_is_visible(planetview_disaster_window()))
        wm_show(planetview_disaster_window(), false);
}

/* 0x408d3c: hides (on = false) or shows the main window's scrollbars,
 * repainting the strip they leave */
static void show_scrollbars(bool on)
{
    WmWindow *m = g_main_window;
    if (!on) {
        GfxRect old = m->client;
        wm_show_scrollbars(m, 3, false);
        m->client.right = m->rect.right - m->rect.left;
        m->client.bottom = m->rect.bottom - m->rect.top;
        shell_view_paint(old.right, old.top, m->client.right - old.right + 1, m->client.bottom - old.top + 1);
        shell_view_paint(m->client.left, old.bottom + 1, old.right - m->client.left + 1,
                         m->client.bottom - old.bottom);
        wm_redraw(m, NULL);
    } else {
        wm_show_scrollbars(m, 3, true);
        if (m->vscroll)
            m->client.right -= m->vscroll->rect.right - m->vscroll->rect.left + 1;
        if (m->hscroll)
            m->client.bottom -= m->hscroll->rect.bottom - m->hscroll->rect.top + 1;
    }
}

/* ======================================================================== */
/* The 0x4510cd cheat and the editor keys (0x45129a)                          */
/* ======================================================================== */

/* 0x4510cd: the last 31 characters typed; "SAMNMAX" three times calls
 * 0x444ea5 (badthings_cheat). */
void shell_cheat_char(int ch)
{
    static char typed[32]; /* 0x4f3188 */
    memmove(typed, typed + 1, 30);
    typed[30] = (char)ch;
    typed[31] = '\0';
    if (strstr(typed, "SAMNMAXSAMNMAXSAMNMAX")) {
        memset(typed, 1, sizeof typed - 1);
        badthings_cheat();
    }
}

/* 0x45129a */
void shell_editor_key(int ch)
{
    if (!g_build.editor)
        return;
    int tool = g_shell_tool;
    switch (ch) {
    case '#':
        planet_generate();
        break;
    case '<':
        if (tool < 9) {
            if (tool < 8) {
                if (g_build.tool_index <= (tool - 1) * 0xd)
                    g_build.tool_index = (uint8_t)(tool * 0xd);
                g_build.tool_index--;
            } else if (g_build.tool_index < 0x5c) {
                g_build.tool_index = 0x6f;
            } else {
                g_build.tool_index--;
            }
        }
        g_build.tool_size = (uint8_t)tile_index_footprint(g_build.tool_index);
        g_build.status_dirty = 1;
        break;
    case '>':
        if (tool < 9) {
            g_build.tool_index++;
            if (tool < 8) {
                if (tool * 0xd <= g_build.tool_index)
                    g_build.tool_index = (uint8_t)((tool - 1) * 0xd);
            } else if (g_build.tool_index > 0x6f) {
                g_build.tool_index = 0x5b;
            }
        }
        g_build.tool_size = (uint8_t)tile_index_footprint(g_build.tool_index);
        g_build.status_dirty = 1;
        break;
    default:
        break;
    }
}

/* ======================================================================== */
/* Menus (0x409bb0, 0x409c17)                                                 */
/* ======================================================================== */

typedef struct MenuDef {
    uint32_t id;
    const char *text;
    uint32_t style;
} MenuDef;

#define T MENU_TEXT
#define TC (MENU_TEXT | MENU_CHECKABLE)
#define SEP {0x29a, "DEVIDER", MENU_SEPARATOR}

static const MenuDef k_file[] = { /* 0x6516c0 */
    {1, "&Save Game\tF5", T},
    {2, "&Load Game\tF2", T},
    {3, "Load S&cenario\tF3", T},
    {4, "&Delete Game", T},
    {5, "&New Game\tF10", T},
#ifdef __EMSCRIPTEN__
    SEP, /* port-only: the browser's files */
    {MENU_SAVE_DOWNLOAD, "Save to Do&wnload", T},
    {MENU_DOWNLOAD, "Download Sa&ved Game", T},
    SEP,
#endif
    {0x1c, "&Afterlife v1.0     ", TC},
    SEP,
    {6, "E&xit Game\tF8", T},
    {0, NULL, 0},
};
static const MenuDef k_global[] = { /* 0x651770 */
    {7, "&Audio Settings\tF9", TC},
    SEP,
    {8, "&Tempo", TC},
    {9, "&Bad Things", T},
    {10, "&Misc. Options", T},
    SEP, /* port-only */
    {MENU_PORT_OPTIONS, "&Port Options...", T},
    {0, NULL, 0},
};
static const MenuDef k_tempo[] = { /* 0x651808 */
    {0xb, "&Demonically Fast", TC},
    {0xc, "&Hounds of Hell", TC},
    {0xd, "&Mortal Plodding", TC},
    {0xe, "Divine &Intervention", TC},
    {0, NULL, 0},
};
static const MenuDef k_bad[] = { /* 0x6518f8 */
    {0x31, "N&o Bad Things", TC},
    SEP,
    {0xf, "B&irds of Paradise", TC},
    {0x10, "B&ats out of Hell", TC},
    {0x11, "My Bl&ue Heaven", TC},
    {0x12, "Hell Fr&eezes Over", TC},
    {0x13, "Heaven No&se", TC},
    {0x14, "Hell in a Ha&ndbasket", TC},
    {0x15, "Paradise Pair of Di&ce", TC},
    {0x16, "Disco In&ferno", TC},
    {0, NULL, 0},
};
static const MenuDef k_misc[] = { /* 0x6519f8 */
    {0x1f, "&Auto Scroll", TC},
    {0x20, "&Technology Announcements", TC},
    {0x21, "&Planet Disaster Announcements", TC},
    {0x22, "&Message Box Timeout", TC},
    {0x23, "&Hide Scrollbars", TC},
    {0, NULL, 0},
};
static const MenuDef k_window[] = { /* 0x651ab0 */
    {0x1b, "&Helpers\tH", TC},
    SEP,
    {0x18, "&Planetview\tP", TC},
    {0x19, "&Graphview\tD", TC},
    {0x1a, "Map&view\tF", TC},
    {0x17, "&MacroManager\tU", TC},
    SEP,
    {0x1d, "&Remote Control\tZ", TC},
    {0, NULL, 0},
};
static const MenuDef k_tutorial[] = { /* 0x651b98 */
    {0x25, "Getting S&tarted", T},
    {0x26, "Lost &SOULs", T},
    {0x27, "Basic &Karma", T},
    {0x28, "Your &Workforce", T},
    {0x29, "&Graphs and Maps", T},
    {0x2a, "&Limbos", T},
    {0x2b, "&Rocks and Siphons", T},
    {0x2c, "&Ports", T},
    {0, NULL, 0},
};

#undef T
#undef TC
#undef SEP

/* 0x409bb0 game_add_menu_items */
static void add_menu_items(WmWindow *menu, const MenuDef *d)
{
    for (; d->text; d++) {
        WmWindow *item = menu_add_item(menu, d->style, d->id, d->text, 0, 501);
        if (d->id < MENU_IDS)
            g_menu_items[d->id] = item;
    }
}

/* 0x409c17 game_create_menus */
static void create_menus(void)
{
    gfx_palette_build_highlight();
    WmWindow *bar = g_main_window->menubar;
    if (!bar)
        return;
    WmWindow *file = menu_add_item(bar, MENU_TEXT, 100, "&File", 0, 501);
    WmWindow *global = menu_add_item(bar, MENU_TEXT, 200, "&Global", 0, 501);
    WmWindow *window = menu_add_item(bar, MENU_TEXT, 300, "&Window", 0, 501);
    WmWindow *tutorial = menu_add_item(bar, MENU_TEXT, 400, "&Tutorial", 0, 501);
    add_menu_items(menu_create(file, NULL, 0), k_file);
    add_menu_items(menu_create(global, NULL, 0), k_global);
    add_menu_items(menu_create(g_menu_items[MENU_TEMPO], NULL, 0), k_tempo);
    add_menu_items(menu_create(g_menu_items[MENU_BAD_THINGS], NULL, 0), k_bad);
    add_menu_items(menu_create(g_menu_items[MENU_MISC], NULL, 0), k_misc);
    add_menu_items(menu_create(window, NULL, 0), k_window);
    add_menu_items(menu_create(tutorial, NULL, 0), k_tutorial);
}

/* ======================================================================== */
/* The menu-command switch of game_view_proc (0x404023, message 0x1006)       */
/* ======================================================================== */

/* the Tempo items: id 0xb..0xd = speed 1..3, 0xe = stopped */
static void set_speed(unsigned id)
{
    for (unsigned i = MENU_SPEED_FAST; i <= MENU_SPEED_PAUSE; i++)
        shell_menu_set_checked(i, i == id);
    if (id == MENU_SPEED_PAUSE) {
        g_game_speed = 0;
        gfx_cycle_disable(8);
        gfx_cycle_disable(7);
    } else {
        g_last_speed = id;
        g_game_speed = (uint8_t)(id - MENU_SPEED_FAST + 1);
        gfx_cycle_enable(8);
        gfx_cycle_enable(7);
    }
    statusbar_draw_date();
}

/* a view window's menu toggle: check mark, window, control panel button */
static void toggle_view(WmWindow *item, WmWindow *win, int button)
{
    bool on = !menu_is_checked(item);
    menu_set_checked(item, on);
    if (win) {
        if (on)
            wm_raise(win);
        wm_show(win, on);
    }
    if (button && shell_ctrl_button(button))
        ctl_button_set_pressed(shell_ctrl_button(button), on, true);
}

/* the save-first questions of Load / Load Scenario / New / Exit */
static int ask(const char *title, const char *text, const char *buttons)
{
    return ctl_message_box(-1, -1, title, text, buttons, 0);
}

void shell_menu_dispatch(WmWindow *item, unsigned id)
{
    if (!item)
        item = shell_menu_item(id);
    if (id < 0x33 || id > 0x36)
        shell_cancel_drag();
    int r;
    switch (id) {
    case MENU_SAVE:
        g_game_command = GAME_CMD_SAVE;
        g_save_kind = 1;
        shell_busy_cursor(true);
        break;
    case MENU_LOAD:
        r = 2;
        if (g_build.map_dirty)
            r = ask("Hey you!", "Save Game Before Loading?", "&Yes\0&No\0~&Cancel\0");
        if (r == 1) {
            g_game_command = GAME_CMD_SAVE;
            g_save_kind = 1;
            g_game_next_command = GAME_CMD_LOAD;
            shell_busy_cursor(true);
        } else if (r == 2) {
            g_game_command = GAME_CMD_LOAD;
            shell_busy_cursor(true);
        }
        break;
    case MENU_LOAD_SCENARIO:
        r = 2;
        if (g_build.map_dirty)
            r = ask("We wanna ask you something...", "Save Game Before Loading Scenario?",
                    "&Yes\0&No\0~&Cancel\0");
        if (r == 1) {
            g_game_command = GAME_CMD_SAVE;
            g_save_kind = 1;
            g_game_next_command = GAME_CMD_LOAD_SCENARIO;
            shell_busy_cursor(true);
        } else if (r == 2) {
            g_game_command = GAME_CMD_LOAD_SCENARIO;
            shell_busy_cursor(true);
        }
        break;
    case MENU_DELETE:
        g_game_command = GAME_CMD_DELETE;
        shell_busy_cursor(true);
        break;
    case MENU_SAVE_DOWNLOAD: /* port-only (web) */
        g_game_command = GAME_CMD_SAVE_DOWNLOAD;
        g_save_kind = 1;
        break;
    case MENU_DOWNLOAD: /* port-only (web) */
        g_game_command = GAME_CMD_DOWNLOAD;
        break;
    case MENU_NEW:
        if (!g_build.map_dirty) {
            if (ask("A poignant query...", "Do You Really Want To Start a New Game?", "&Yes\0~&No\0") != 2)
                g_game_command = GAME_CMD_NEW;
        } else {
            r = ask("Giving up, eh?", "Save Game Before Starting a New One?", "&Yes\0&No\0~&Cancel\0");
            if (r == 1) {
                g_game_command = GAME_CMD_SAVE;
                g_save_kind = 1;
                g_game_next_command = GAME_CMD_NEW;
            } else if (r == 2) {
                g_game_command = GAME_CMD_NEW;
            }
        }
        break;
    case MENU_EXIT:
        if (!g_build.map_dirty) {
            if (ask("Quitters never win...", "Do You Really Want To Quit?", "&Yes\0~&No\0") == 2)
                break;
        } else {
            r = ask("Hey, Bub!", "Save Game Before Quitting?", "&Yes\0&No\0~&Cancel\0");
            if (r == 1) {
                g_game_command = GAME_CMD_SAVE;
                g_save_kind = 1;
            } else if (r == 3) {
                break;
            }
        }
        g_game_running = 0;
        break;
    case MENU_AUDIO: {
        bool on = !menu_is_checked(item);
        menu_set_checked(item, on);
        if (shell_hooks.audio_dialog)
            shell_hooks.audio_dialog(on);
        break;
    }
    case MENU_SPEED_FAST:
    case MENU_SPEED_MEDIUM:
    case MENU_SPEED_SLOW:
    case MENU_SPEED_PAUSE:
        set_speed(id);
        break;
    case 0xf:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16: {
        static const char *const names[8] = {
            "Birds of Paradise",    "Bats out of Hell", "My Blue Heaven",        "Hell Freezes Over",
            "Heaven Nose",          "Hell in a Handbasket", "Paradise Pair of Dice", "Disco Inferno",
        };
        char text[200];
        if (!shell_menu_checked(MENU_NO_BAD_THINGS)) {
            snprintf(text, sizeof text, "Unleash the %s disaster?", names[id - 0xf]);
            if (ask("Are you sure?", text, "&You Bet!\0~Hell &No!\0") == 1)
                bad_thing_start((int)id - 0xf);
        } else {
            snprintf(text, sizeof text,
                     "You can't unleash the %s disaster now, because you have prevented Bad Things from happening "
                     "to you!",
                     names[id - 0xf]);
            ask("Wait a minute!", text, "&Oh Yeah...\0");
        }
        break;
    }
    case MENU_MACRO: {
        bool on = !menu_is_checked(item);
        if (shell_hooks.macro_show)
            shell_hooks.macro_show(on);
        menu_set_checked(item, on);
        if (shell_ctrl_button(0x25))
            ctl_button_set_pressed(shell_ctrl_button(0x25), on, true);
        break;
    }
    case MENU_PLANETVIEW: {
        planetview_create(g_main_window); /* toggles */
        bool on = !menu_is_checked(item);
        menu_set_checked(item, on);
        if (shell_ctrl_button(0x1e))
            ctl_button_set_pressed(shell_ctrl_button(0x1e), on, true);
        break;
    }
    case MENU_GRAPHVIEW:
        toggle_view(item, graphview_window(), 0x1b);
        break;
    case MENU_MAPVIEW:
        toggle_view(item, mapview_window(), 0x20);
        if (menu_is_checked(item))
            mapview_draw();
        break;
    case MENU_HELPERS:
        if (!menu_is_checked(item)) {
            if (g_state.unk_5f4cd0[0][0] == 0) {
                advisor_show(true);
                menu_set_checked(item, true);
                if (shell_ctrl_button(0x1f))
                    ctl_button_set_pressed(shell_ctrl_button(0x1f), true, true);
            } else {
                ask("No help here...", "Aria and Jasper have left your pathetic Afterlife in search of a new "
                                       "employer.",
                    "&Doh!\0");
            }
        } else {
            if (advisor_window())
                wm_show(advisor_window(), false);
            menu_set_checked(item, false);
            advisor_show(false);
            if (shell_ctrl_button(0x1f))
                ctl_button_set_pressed(shell_ctrl_button(0x1f), false, true);
        }
        break;
    case MENU_ABOUT:
        if (!menu_is_checked(item)) {
            WmWindow *a = about_window();
            if (a) {
                wm_set_title(a, wm_get_title(item));
                views_draw_title(a);
                wm_raise(a);
                wm_show(a, true);
            }
            menu_set_checked(item, true);
        } else {
            if (about_window())
                wm_show(about_window(), false);
            menu_set_checked(item, false);
        }
        break;
    case MENU_CTRLPANEL:
        if (!menu_is_checked(item)) {
            wm_show(g_ctrlpanel_window, true);
            menu_set_checked(item, true);
        } else {
            if (g_tilechooser_window && wm_is_visible(g_tilechooser_window))
                wm_send(g_tilechooser_window, g_tilechooser_window, WM_MSG_RBUTTONDOWN, 0, 0);
            wm_show(g_ctrlpanel_window, false);
            menu_set_checked(item, false);
        }
        break;
    case MENU_AUTOSCROLL:
    case MENU_TECH_ANNOUNCE:
    case MENU_DISASTER_ANNOUNCE:
    case MENU_MSGBOX_TIMEOUT:
        menu_set_checked(item, !menu_is_checked(item));
        planetview_set_menu_checks(shell_menu_checked(MENU_TECH_ANNOUNCE), shell_menu_checked(MENU_DISASTER_ANNOUNCE));
        break;
    case MENU_HIDE_SCROLLBARS:
        if (!menu_is_checked(item)) {
            show_scrollbars(false);
            menu_set_checked(item, true);
        } else {
            show_scrollbars(true);
            menu_set_checked(item, false);
        }
        break;
    case 0x25:
    case 0x26:
    case 0x27:
    case 0x28:
    case 0x29:
    case 0x2a:
    case 0x2b:
    case 0x2c:
    case 0x2d:
    case 0x2e:
        if (shell_hooks.tutorial)
            shell_hooks.tutorial((int)id - 0x24); /* 0x425e3b: lesson 1.. */
        break;
    case MENU_NO_BAD_THINGS:
        if (!menu_is_checked(item)) {
            if (ctl_message_box(-1, -1, NULL, "This will cut your SOUL Rate by 50%.  Are you sure?", "&Yes\0~&No\0",
                                0) == 1) {
                menu_set_checked(item, true);
                g_state.bad_things = 0;
            }
        } else if (ctl_message_box(-1, -1, NULL,
                                   "Are you sure you want to leave your Afterlife vulnerable to Bad Things?",
                                   "&Yes\0~&No\0", 0) == 1) {
            menu_set_checked(item, false);
            g_state.bad_things = 1;
        }
        break;
    case MENU_PORT_OPTIONS: /* port-only: enhancements and UI scale */
        enhance_options_dialog();
        break;
    default: /* 8, 9, 10, 0x1e, 0x24, 0x2f, 0x30: nothing; 0x33-0x36: 0x4089f4 (empty) */
        break;
    }
}

/* ======================================================================== */
/* Load / save / new                                                          */
/* ======================================================================== */

static const char *mode_ext(int mode) { return mode == 2 ? "asc" : "alf"; }

/* The save folder: INI "Save games" if it exists here, else the data dir
 * (port: the platform's own place first, plat_save_dir). */
const char *shell_save_dir_override;

static void save_dir(char *out, size_t n)
{
    if (shell_save_dir_override) {
        snprintf(out, n, "%s", shell_save_dir_override);
        return;
    }
    if (plat_save_dir()) {
        snprintf(out, n, "%s", plat_save_dir());
        return;
    }
    char ini[0x800];
    if (cfg_save_games_dir(ini) && ini[0] && !(ini[1] == ':')) {
        snprintf(out, n, "%s", ini);
        return;
    }
    snprintf(out, n, "%s", plat_data_dir());
}

/* Picks a file through the native dialog, or by typing a name into the
 * game's own prompt where there is none (headless, the web). */
bool shell_pick_file(int mode, char *out, size_t out_len)
{
    if (shell_pick_file_hook)
        return shell_pick_file_hook(mode, out, out_len);
    char dir[1024];
    save_dir(dir, sizeof dir);
    if (plat_file_dialog_begin(mode == 3, mode == 2 ? plat_data_dir() : dir,
                               mode == 2 ? "Afterlife scenarios" : "Afterlife games", mode == 3 ? "alf" : "alf;asc")) {
        int r;
        while ((r = plat_file_dialog_poll(out, out_len)) == 0)
            sys_pump();
        return r == 1;
    }
    char def[64];
    snprintf(def, sizeof def, mode == 2 ? "ASCEN001.ASC" : "AGAME000.ALF");
    const char *name = ctl_prompt(-1, -1, mode == 3 ? "Save Game" : mode == 2 ? "Load Scenario" : "Load Game",
                                  "File name:", def);
    if (!name || !name[0])
        return false;
    if (name[0] == '/' || (name[0] && name[1] == ':')) {
        snprintf(out, out_len, "%s", name);
        return true;
    }
    if (mode != 3 && plat_data_path(name, out, out_len))
        return true;
    const char *dot = strrchr(name, '.');
    snprintf(out, out_len, "%s/%s%s%s", mode == 2 ? plat_data_dir() : dir, name, dot ? "" : ".", dot ? "" : mode_ext(mode));
    return true;
}

bool shell_choose_load(int mode)
{
    char path[sizeof g_pending_load];
    if (!shell_pick_file_hook) { /* 0x494112: the slot dialog */
        int slot = -1;
        if (!slotdlg_choose(mode, &slot))
            return false;
        slotdlg_slot_file(mode, (unsigned)slot, path, sizeof path);
    } else if (!shell_pick_file(mode, path, sizeof path)) {
        return false;
    }
    snprintf(g_pending_load, sizeof g_pending_load, "%s", path);
    return true;
}

const char *shell_pending_load(void) { return g_pending_load; }

void shell_set_pending_load(const char *path) { snprintf(g_pending_load, sizeof g_pending_load, "%s", path); }

/* ---- port-only: dropped files, downloads (the web build) ---------------- */

static char g_dropped[1024];                         /* GAME_CMD_LOAD_DROPPED's file */
static char g_download_name[256] = "AGAME000.ALF"; /* Save to Download's default */

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash))
        slash = bslash;
    return slash ? slash + 1 : path;
}

/* `name` as a file name: no folder separators or characters browsers and
 * Windows refuse; `ext` appended when it has no extension. */
static void file_name(const char *name, const char *ext, char *out, size_t n)
{
    size_t j = 0;
    for (const char *p = name; *p && j + 1 < n; p++)
        out[j++] = (strchr("/\\:*?\"<>|", *p) || (unsigned char)*p < 0x20) ? '_' : *p;
    out[j] = '\0';
    while (j > 0 && (out[j - 1] == ' ' || out[j - 1] == '.'))
        out[--j] = '\0';
    if (!out[0])
        snprintf(out, n, "AGAME000");
    if (!strrchr(out, '.') && strlen(out) + strlen(ext) + 1 < n)
        strcat(strcat(out, "."), ext);
}

bool shell_take_drop(char *out, size_t out_len)
{
#ifndef __EMSCRIPTEN__
    /* ENHANCEMENT: natively, dropping a file onto the window loads it */
    if (!g_enhancements) {
        char ignored[1024];
        plat_take_dropped_file(ignored, sizeof ignored);
        return false;
    }
#endif
    if (!plat_take_dropped_file(out, out_len))
        return false;
    /* Save to Download offers the dropped file's name next, as a .alf */
    char stem[256];
    snprintf(stem, sizeof stem, "%s", base_name(out));
    char *dot = strrchr(stem, '.');
    if (dot && dot != stem)
        *dot = '\0';
    file_name(stem, "alf", g_download_name, sizeof g_download_name);
    return true;
}

/* A drop while the game runs: like Load Game, asking to save first. */
static void poll_drop(void)
{
    if (g_game_command || !shell_take_drop(g_dropped, sizeof g_dropped))
        return;
    int r = 2;
    if (g_build.map_dirty)
        r = ask("Hey you!", "Save Game Before Loading?", "&Yes\0&No\0~&Cancel\0");
    if (r == 1) {
        g_game_command = GAME_CMD_SAVE;
        g_save_kind = 1;
        g_game_next_command = GAME_CMD_LOAD_DROPPED;
        shell_busy_cursor(true);
    } else if (r == 2) {
        g_game_command = GAME_CMD_LOAD_DROPPED;
    } else {
        plat_release_dropped_file(g_dropped);
        g_dropped[0] = '\0';
    }
}

/* Save to Download: the game under a name (the last one used or dropped),
 * written to a temporary file that goes to the browser as a download. It
 * never lands among the saved games. */
static bool save_download(void)
{
    const char *name = ctl_prompt_filled(-1, -1, "Save to Download", "File name:", g_download_name);
    if (!name || !name[0])
        return false;
    char file[256], title[256], path[1024];
    file_name(name, "alf", file, sizeof file);
    snprintf(title, sizeof title, "%s", file);
    char *dot = strrchr(title, '.');
    if (dot && dot != title)
        *dot = '\0';
    snprintf(path, sizeof path, "/tmp/%s", file);
    bool ok = shell_save_game_to(path, title) && plat_download_file(path, file);
    remove(path);
    if (!ok) {
        ctl_message_box(-1, -1, "Uh oh...", "The game could not be saved.", "~&OK\0", 0);
        return false;
    }
    snprintf(g_download_name, sizeof g_download_name, "%s", file);
    return true;
}

/* Download Saved Game: a slot of the browser's saved games as a download
 * named after its title. */
static void download_slot(void)
{
    int slot = -1;
    char path[1100], file[256];
    if (!slotdlg_pick(1, "Download a Game", "Download", &slot) ||
        !slotdlg_slot_file(1, (unsigned)slot, path, sizeof path))
        return;
    SlotInfo info;
    file_name(slotdlg_read_info(path, &info) && info.title[0] ? info.title : base_name(path), "alf", file,
              sizeof file);
    if (!plat_download_file(path, file))
        ctl_message_box(-1, -1, "Uh oh...", "The game could not be downloaded.", "~&OK\0", 0);
}

/* 0x47fc20 save_pack_timestamp: (year - 1980) << 25 | month << 21 | day
 * << 16 | hour << 11 | minute << 5 | second / 2 (the FAT date layout). */
static uint32_t pack_timestamp(void)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (!tm)
        return 0;
    return (uint32_t)(tm->tm_year - 80) << 25 | (uint32_t)(tm->tm_mon + 1) << 21 | (uint32_t)tm->tm_mday << 16 |
           (uint32_t)tm->tm_hour << 11 | (uint32_t)tm->tm_min << 5 | (uint32_t)(tm->tm_sec / 2);
}

/* the header thumbnail: the main view scaled to 104 x 78 */
static void make_thumbnail(void)
{
    GameState *g = &g_state;
    free(g->hdr.thumb);
    g->hdr.thumb_w = 104;
    g->hdr.thumb_h = 78;
    g->hdr.thumb = calloc(104 * 78, 1);
    GfxSurface *s = g_main_window ? g_main_window->surface : NULL;
    if (!g->hdr.thumb || !s)
        return;
    int cw = g_main_window->client.right - g_main_window->client.left + 1;
    int ch = g_main_window->client.bottom - g_main_window->client.top + 1;
    for (int y = 0; y < 78; y++)
        for (int x = 0; x < 104; x++) {
            int sx = g_main_window->client.left + x * cw / 104, sy = g_main_window->client.top + y * ch / 78;
            g->hdr.thumb[y * 104 + x] = s->pixels[(size_t)sy * (size_t)s->stride + (size_t)sx];
        }
}

/* 0x480552 save_slot_path */
void shell_slot_path(int mode, unsigned slot, char *out, size_t n)
{
    char dir[1024];
    save_dir(dir, sizeof dir);
    snprintf(out, n, mode == 2 ? "%s/ascen%03u.asc" : "%s/agame%03u.alf", dir, slot);
}

/* save_write_game 0x485fb3: the header (version, kind, time stamp, year,
 * title, thumbnail) and the game. */
bool shell_save_game_to(const char *path, const char *title)
{
    GameState *g = &g_state;
    g->hdr.version = ALSV_VERSION_CURRENT;
    g->hdr.kind = g_save_kind;
    g->hdr.timestamp = pack_timestamp();
    g->hdr.year = g->year;
    snprintf(g->hdr.title, sizeof g->hdr.title, "%.200s", title);
    make_thumbnail();
    return savegame_save(g, path);
}

/* 0x480ea4(mode): save (1 a game, 2 a scenario: editor) */
static bool save_game(int mode)
{
    char path[1024];
    if (!shell_pick_file(3, path, sizeof path))
        return false;
    const char *base = strrchr(path, '/');
    (void)mode;
    if (!shell_save_game_to(path, base ? base + 1 : path)) {
        ctl_message_box(-1, -1, "Uh oh...", "The game could not be saved.", "~&OK\0", 0);
        return false;
    }
    g_build.map_dirty = 0;
    return true;
}

/* After a load or a new game: the views, the main view and the status. */
static void after_world_change(void)
{
    sim_reset();
    statusbar_reset();
    shell_view_attach();
    shell_view_reset_drag();
    shell_view_rotation_buttons();
    shell_center_on_cell(GS_PLANE_HEAVEN, 50, 50);
    build_select_tool(g_shell_tool);
    statusbar_draw();
    views_refresh_all();
    g_shell_repaint = 1;
    /* the No Bad Things checkmark follows the game (game_reset_world
     * 0x453993, save_load_game 0x4940c9) */
    shell_menu_set_checked(MENU_NO_BAD_THINGS, g_state.bad_things == 0);
}

/* Loads `path` into the game (save_load_game 0x48f837 at the end of
 * 0x489e90). */
static bool load_file(const char *path)
{
    char err[256] = "";
    uint32_t bad_things = g_state.bad_things;
    if (!world_load(path, err, sizeof err)) {
        char text[400];
        snprintf(text, sizeof text, "Couldn't load %s: %s", path, err);
        ctl_message_box(-1, -1, "Uh oh...", text, "~&OK\0", 0);
        return false;
    }
    g_build.map_dirty = 0;
    after_world_change();
    if ((g_state.bad_things != 0) != (bad_things != 0)) {
        /* save_load_game 0x494089: an FYI when the load flips the setting */
        if (g_state.bad_things)
            ctl_message_box(-1, -1, "FYI..." /* 0x66c2e4 */,
                            "You've loaded up a game with the Bad Things toggled on." /* 0x66c2ac */,
                            "&OK\0" /* 0x66c2a4 */, 0);
        else
            ctl_message_box(-1, -1, "FYI..." /* 0x66c330 */,
                            "You've loaded up a game with the Bad Things toggled off." /* 0x66c2f4 */,
                            "&OK\0" /* 0x66c2ec */, 0);
    }
    return true;
}

bool shell_load_game_from(const char *path) { return load_file(path); }

static uint8_t *load_resource(uint32_t id, size_t *len);

/* save_load_game with a slot >= 10000: the save is resource `id`
 * (save_open_slot 0x47fca7). */
bool shell_load_game_resource(uint32_t id)
{
    size_t len = 0;
    uint8_t *data = load_resource(id, &len);
    GameState *tmp = data ? calloc(1, sizeof *tmp) : NULL;
    char err[256] = "";
    bool ok = tmp && savegame_load_mem(tmp, data, len, err, sizeof err);
    free(data);
    if (!ok) {
        free(tmp);
        ctl_message_box(-1, -1, "Uh oh...", "Couldn't load an internal file.", "~&OK\0", 0);
        return false;
    }
    world_apply_loaded(tmp);
    free(tmp);
    g_build.map_dirty = 0;
    after_world_change();
    return true;
}

/* 0x489e90(mode): pick and load (1 a game, 2 a scenario) */
static bool load_game(int mode)
{
    char path[1024];
    if (!shell_pick_file(mode, path, sizeof path))
        return false;
    return load_file(path);
}

static void scenario_box(void)
{
    if (g_state.unk_63048c == 0 && g_state.scenario_title[0]) {
        const char *button = g_state.scenario_button[0] ? g_state.scenario_button : "OK";
        char buttons[96];
        size_t n = strlen(button);
        if (n > sizeof buttons - 2)
            n = sizeof buttons - 2;
        memcpy(buttons, button, n);
        buttons[n] = buttons[n + 1] = '\0';
        ctl_message_box(-1, -1, g_state.scenario_title, g_state.scenario_description, buttons, 0);
    }
}

/* What the main loop does before a dialog: stop the voice, pause, close the
 * chooser. */
static void before_dialog(void)
{
    snd_slot_stop_stream(SND_VOICE_SLOT);
    g_game_paused = 1;
    shell_busy_cursor(false);
    if (g_tilechooser_window && wm_is_visible(g_tilechooser_window))
        wm_send(g_tilechooser_window, g_tilechooser_window, WM_MSG_RBUTTONDOWN, 0, 0);
}

/* game_main's switch (game_command) */
static void run_command(void)
{
    switch (g_game_command) {
    case GAME_CMD_SAVE:
    case GAME_CMD_SAVE_SCENARIO:
        before_dialog();
        /* the original's slot dialog (0x480ea4); the file picker when a
         * test supplies the path */
        if (!shell_pick_file_hook) {
            if (!slotdlg_save(g_game_command == GAME_CMD_SAVE ? 1 : 2)) {
                g_game_running = 1;
                g_game_next_command = 0;
            } else {
                g_build.map_dirty = 0;
            }
        } else if (!save_game(g_game_command == GAME_CMD_SAVE ? 1 : 2)) {
            g_game_running = 1;
            g_game_next_command = 0;
        }
        g_game_command = 0;
        g_game_paused = 0;
        break;
    case GAME_CMD_LOAD:
    case GAME_CMD_LOAD_SCENARIO: {
        int mode = g_game_command == GAME_CMD_LOAD ? 1 : 2;
        before_dialog();
        if (planetview_disaster_window())
            wm_show(planetview_disaster_window(), false);
        if (!shell_pick_file_hook ? !slotdlg_load(mode) : !load_game(mode)) { /* 0x489e90 */
            g_game_next_command = 0;
        } else {
            graphview_refresh();
            if (mode == 2)
                scenario_box();
        }
        g_game_command = 0;
        g_game_paused = 0;
        break;
    }
    case GAME_CMD_SAVE_DOWNLOAD: /* port-only (web) */
        before_dialog();
        if (save_download())
            g_build.map_dirty = 0;
        else
            g_game_next_command = 0;
        g_game_command = 0;
        g_game_paused = 0;
        break;
    case GAME_CMD_DOWNLOAD: /* port-only (web) */
        before_dialog();
        download_slot();
        g_game_command = 0;
        g_game_paused = 0;
        break;
    case GAME_CMD_LOAD_DROPPED: { /* port-only */
        before_dialog();
        if (planetview_disaster_window())
            wm_show(planetview_disaster_window(), false);
        const char *dot = strrchr(g_dropped, '.');
        bool scenario = dot && (dot[1] | 0x20) == 'a' && (dot[2] | 0x20) == 's' && (dot[3] | 0x20) == 'c' && !dot[4];
        if (g_dropped[0] && load_file(g_dropped)) {
            graphview_refresh();
            if (scenario)
                scenario_box();
        }
        plat_release_dropped_file(g_dropped);
        g_dropped[0] = '\0';
        g_game_command = 0;
        g_game_paused = 0;
        break;
    }
    case GAME_CMD_DELETE: { /* 0x4946bd: pick a saved game and delete it */
        before_dialog();
        char path[1024];
        if (!shell_pick_file_hook) {
            if (!slotdlg_delete())
                g_game_next_command = 0;
        } else if (shell_pick_file(1, path, sizeof path)) {
            char text[1200];
            snprintf(text, sizeof text, "Delete %s?", path);
            if (ctl_message_box(-1, -1, "Delete Game", text, "&Yes\0~&No\0", 0) == 1)
                remove(path);
        } else {
            g_game_next_command = 0;
        }
        g_game_command = 0;
        g_game_paused = 0;
        break;
    }
    case GAME_CMD_NEW:
        g_game_paused = 1;
        g_game_speed = 0;
        g_game_command = 0;
        if (planetview_disaster_window())
            wm_show(planetview_disaster_window(), false);
        g_state.hdr.difficulty = (uint16_t)ctl_message_box(-1, -1, "One small bit of business...",
                                                           "What Difficulty Level Would You Like?",
                                                           "~&Easy\0&Medium\0&Hard\0", 0);
        snd_slot_stop_stream(SND_VOICE_SLOT);
        music_stop();
        close_views();
        game_reset_world();
        g_build.map_dirty = 0;
        after_world_change();
        shell_menu_command(MENU_SPEED_PAUSE);
        g_game_paused = 0;
        break;
    default:
        break;
    }
    if (g_game_next_command) {
        g_game_command = g_game_next_command;
        g_game_next_command = 0;
    }
}

/* ======================================================================== */
/* game_frame_update 0x407282                                                 */
/* ======================================================================== */

/* 0x402747 game_frame_clock */
static void frame_clock(void)
{
    uint32_t now = tm_get_time();
    if ((int32_t)(now - (g_clock_last + 0x7d)) > 0) {
        g_clock_ticks++;
        g_clock_music--;
        g_clock_status++;
        g_clock_last = now;
    }
}

void shell_frame_update(void)
{
    if (!g_main_window)
        return;
    int plane = g_map_view.plane;
    if (gfx_cursor_get() != g_shell_cursor && g_shell_cursor)
        wm_set_cursor(g_main_window, g_shell_cursor);
    if (about_credits_running())
        about_credits_tick();
    if (shell_menu_checked(MENU_AUTOSCROLL)) { /* edge scrolling */
        uint32_t dt;
        if (g_edge_dx == 0 && g_edge_dy == 0) {
            g_edge_time = tm_get_time();
            dt = 100;
        } else {
            dt = tm_get_time() - g_edge_time;
        }
        int mx, my, sx = 0, sy = 0;
        gfx_mouse_get_state(&mx, &my);
        if (mx > gfx_screen_width() - 3)
            sx = -1;
        if (mx < 3)
            sx = 1;
        if (my > gfx_screen_height() - 3)
            sy = -1;
        if (my < 3)
            sy = 1;
        if (sx == 0 && sy == 0) {
            g_edge_dx = g_edge_dy = 0;
        } else {
            g_edge_dx = (int)(dt / 0x34) * sx;
            g_edge_dy = (int)(dt / 0x34) * sy;
            shell_view_scroll(g_edge_dx, g_edge_dy);
            shell_view_update_scrollbars();
        }
    }
    if (g_shell_repaint) {
        if (g_drag_plane)
            shell_select_plane(g_drag_plane);
        shell_busy_cursor(true);
        shell_view_repaint();
        shell_busy_cursor(false);
        if (g_drag_plane)
            shell_select_plane(g_drag_plane);
        g_shell_repaint = 0;
    }
    if (g_build.status_dirty) {
        g_build.status_dirty = 0;
        statusbar_draw();
    }
    frame_clock();
    if (g_clock_ticks != 0) {
        g_clock_ticks = 0;
        if (!g_game_paused) {
            planetview_rotate_tick();
            /* 0x4ee088 + map_objects_step for planes 1, 2 (while the game
             * runs) and 3 (WP3) */
            mapobj_frame(g_game_speed);
        }
        if (g_clock_music < 0 && (-g_clock_music) % 8 == 0)
            g_clock_music = music_tick(g_clock_music);
        if ((unsigned)g_game_speed * 2 <= (unsigned)g_clock_status) {
            g_clock_status = 0;
            if (g_game_speed)
                statusbar_draw_weights();
        }
    }
    if (g_map_view.plane != plane && plane)
        shell_select_plane(plane);
}

/* ======================================================================== */
/* The loading window (0x408208, 0x408356, 0x408995)                          */
/* ======================================================================== */

static intptr_t load_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b) { return wm_default_proc(w, msg, a, b); }

/* 0x408356 loading_progress: the bar and its (mostly joke) caption */
static void loading_progress(unsigned done, unsigned total)
{
    if (!g_load_win || !g_load_bmp || total == 0)
        return;
    if (done < g_load_shown)
        done = g_load_shown;
    g_load_shown = done;
    GfxSurface *s = g_load_win->surface;
    GfxRect r = {0x0d, g_load_bmp->width - 1 - 0xe, 0xf, g_load_bmp->height - 1 - 0xe};
    int full_w = r.right - r.left + 1;
    uint32_t now = tm_get_time();
    if ((int32_t)(now - g_load_joke_time) > 0x5dc) {
        g_load_joke = 0;
        g_load_joke_time = now;
    }
    int bar = (int)((uint64_t)full_w * done / total);
    int pct = full_w ? bar * 100 / full_w : 0;
    int top = r.bottom; /* the text's baseline area: half the bottom (the original's local_40 >> 1) */
    char text[256];
    int left = r.left, right = r.left + bar - 1;
    if (rand_range(0, 0xc) == 0 || g_load_joke != 0) {
        if (g_load_joke == 0) {
            g_load_joke = rand_range(1, 0x14);
            g_load_joke_time = now;
        }
        switch (g_load_joke) {
        case 1: snprintf(text, sizeof text, "Fighting the Bear... %d%% Complete", pct); break;
        case 2: snprintf(text, sizeof text, "Ossifying Hard Drive... %d%% Complete", pct); break;
        case 3: snprintf(text, sizeof text, "Downloading Credit Card Numbers... %d%% Complete", pct); break;
        case 4:
            right = r.left + bar / 10;
            snprintf(text, sizeof text, "Disgusted With Hardware, Starting Over... %d%% Complete", pct / 10);
            break;
        case 5: snprintf(text, sizeof text, "Who Is John Galt?"); break;
        case 6: snprintf(text, sizeof text, "Projecting Subliminal Messages... %d%% Complete", pct); break;
        case 7: snprintf(text, sizeof text, "Microwaving Burrito... %d%% Complete", pct); break;
        case 8: snprintf(text, sizeof text, "Love Is Never Having To Say %d%% Complete", pct); break;
        case 9:
            right = r.left + full_w - 1;
            snprintf(text, sizeof text, "If We Were Done Loading, We'd Be %d%% Complete", 100);
            break;
        case 10:
            right = r.left + full_w - 1;
            left = right - bar;
            snprintf(text, sizeof text, "It's Backwards Day!... %d%% Complete", 100 - pct);
            break;
        case 11: snprintf(text, sizeof text, "Abstruding AI Into %d Dimensions...", pct); break;
        case 12:
            snprintf(text, sizeof text, "National Debt: $6,438,%03d,%03d,000", pct * 9, rand_range(0, 99));
            break;
        case 13: snprintf(text, sizeof text, "Scanning For Pornographic Pictures... %d%% Complete", pct); break;
        case 14: snprintf(text, sizeof text, "Rock 'n' Roll Died in %d...", pct / 10 + 0x7bc); break;
        case 15:
            left = r.left + ((full_w >> 1) - (bar >> 1));
            right = left + (bar >> 1) + (full_w >> 1) + 1;
            snprintf(text, sizeof text, "Mid-Life Crisis! Trying Something New... %d%% Complete", pct);
            break;
        case 16: snprintf(text, sizeof text, "Alice is %d Feet Tall.", pct); break;
        case 17:
            snprintf(text, sizeof text, "Most U.S. Math Students Think We're %d%% Complete", rand_range(0, 800));
            break;
        case 18: snprintf(text, sizeof text, "Big Words Used Incorrectly In This Game: %d", pct * 0xbb); break;
        case 19: snprintf(text, sizeof text, "Gratuitously Extracting Pi to %d Places...", (1 << (pct / 4 & 31)) + 10000); break;
        default: snprintf(text, sizeof text, "%d bottles of beer on the wall, %d bottles of beer...", pct, pct); break;
        }
    } else {
        snprintf(text, sizeof text, "Loading... %d%% Complete", pct);
        rand_range(0, 100);
    }
    (void)top;
    gfx_blit(s, 0, 0, g_load_bmp->width, g_load_bmp->height, g_load_bmp, 0, 0);
    if (right >= left) {
        GfxRect br = {left, right, r.top, r.bottom};
        gfx_bevel_rect(s, &br, 0x22, 0x28, 0x2e, false);
    }
    uint32_t prev = gfx_font_select(501);
    uint8_t saved[16];
    gfx_font_get_colormap(501, 0xf, saved);
    unsigned c[3] = {0, 0x22, 0x2e};
    gfx_font_set_colors(501, 0, 3, c);
    gfx_font_draw_text(s, ((r.right - r.left) >> 1) + r.left - 7, r.bottom >> 1, NULL, NULL, text, GFX_TEXT_CENTER);
    gfx_font_set_colormap(501, 0xf, saved);
    gfx_font_select(prev);
    wm_redraw(g_load_win, NULL);
    sys_pump();
}

static void load_step(void) { loading_progress(++g_load_step, g_load_steps); }

/* 0x408208 */
static bool loading_open(unsigned steps)
{
    g_load_steps = steps;
    g_load_step = 0;
    g_load_shown = 0;
    g_load_bmp = gfx_load_bmp_resource(0x245, NULL);
    if (!g_load_bmp)
        return false;
    GfxRect r = {0, g_load_bmp->width - 1, 0, g_load_bmp->height - 1};
    g_load_win = wm_create_window(g_main_window, &r, load_proc, WM_F_CENTER | WM_F_NOFOCUS | WM_F_TOPMOST, 0);
    if (!g_load_win) {
        gfx_surface_destroy(g_load_bmp);
        g_load_bmp = NULL;
        return false;
    }
    gfx_blit(g_load_win->surface, 0, 0, g_load_bmp->width, g_load_bmp->height, g_load_bmp, 0, 0);
    wm_resize(g_load_win, g_load_bmp->width, g_load_bmp->height);
    wm_center(g_load_win);
    loading_progress(0, steps);
    wm_show(g_load_win, true);
    return true;
}

/* 0x408995 */
static void loading_close(void)
{
    if (g_load_win) {
        wm_show(g_load_win, false);
        wm_destroy(g_load_win);
        g_load_win = NULL;
    }
    gfx_surface_destroy(g_load_bmp);
    g_load_bmp = NULL;
}

/* ======================================================================== */
/* Hooks                                                                      */
/* ======================================================================== */

static int hook_message(const char *title, const char *text, const char *buttons)
{
    return ctl_message_box(-1, -1, title, text, buttons, 120000);
}
/* snd_play; skipped while iMUSE isn't running (headless, Sound=NONE),
 * where it would only load the sample into a slot nobody frees. */
static void hook_sound(uint32_t id, int prio)
{
    if (snd_running())
        snd_play(id, prio);
}
static void hook_sound_unload(uint32_t id) { snd_unload(id); }
static void hook_sim_select_plane(int plane) { shell_select_plane(plane); }
static void hook_views_changed(void)
{
    if (shell_hooks.views_changed)
        shell_hooks.views_changed();
}
static void hook_redraw(int x, int y, int size) { shell_view_redraw_cells(x, y, size, size, 0x40); }
static void hook_redraw_fade(int x, int y, int size, int obj, int set, int frame, int fade_in)
{
    (void)obj, (void)set, (void)frame, (void)fade_in;
    shell_view_redraw_cells(x, y, size, size, 0x40);
}
static bool hook_confirm(int what)
{
    const char *text = "Are you absotively sure you wannna nuke this Special Structure?";
    if (what == BUILD_CONFIRM_OMNIBOLGE)
        text = g_world.plane == GS_PLANE_HELL ? "Are you absotively sure you wannna nuke this Omnibolge?"
                                              : "Are you absotively sure you wannna nuke this Love Dome?";
    return ctl_message_box(-1, -1, NULL, text, "&Yes\0~&No\0", 0) == 1;
}
static void hook_refresh(void)
{
    g_build.map_dirty = 1;
    statusbar_draw();
    if (shell_hooks.views_changed)
        shell_hooks.views_changed();
}
/* the Microview's questions: ctl_message_box(-1, -1, NULL, text, buttons, 0) */
static int hook_mv_message(const char *text, const char *buttons)
{
    return ctl_message_box(-1, -1, NULL, text, buttons, 0);
}
/* the Macromanager's: ctl_message_box(-1, -1, title, text, buttons, 0) */
static int hook_macro_message(const char *title, const char *text, const char *buttons)
{
    return ctl_message_box(-1, -1, title, text, buttons, 0);
}
/* the artist's rendering: news_popup_show(id, 0, 0) */
static void hook_mv_news(unsigned id) { news_popup_show(id, 0, 0); }
/* 0x46fc14 microview_secret_talk: loads costume 0x7d2 and binds talker 4
 * at (0x23, 0xe3) with direction 2 and animation 3. akos_talk_open and
 * the rest only accept talkers 1..3 (0x4d2ce7: n == 0 || n >= 4 -> -1),
 * so in the original nothing is shown and the costume slot leaks. The
 * port makes the same calls and frees the costume when the talker
 * refuses it. */
static void hook_mv_secret_talk(WmWindow *win)
{
    int costume = akos_costume_load(0x7d2);
    if (akos_talk_open(4, win, costume, 0xff, 0xff, 10, 0, 6) < 0) {
        akos_costume_free(costume);
        return;
    }
    akos_talk_place(4, 0x23, 0xe3, win);
    akos_talk_set_direction(4, 2);
    akos_talk_set_anim(4, 3);
}
static void hook_disturbance(void)
{
    ctl_message_box(-1, -1, NULL, "I sense a great disturbance in the Force...", "~&OK\0", 0);
}
static int hook_views_select(int plane) { return shell_select_plane(plane); }
static void hook_menu_command(unsigned id) { shell_menu_command(id); }
static void hook_pv_hidden(void)
{
    shell_menu_set_checked(MENU_PLANETVIEW, false);
    if (shell_ctrl_button(0x1e))
        ctl_button_set_pressed(shell_ctrl_button(0x1e), false, true);
}
static void hook_pump(void) { sys_pump(); }
static void hook_pv_cursor(bool price)
{
    PlatCursor *c = price ? g_shell_cursor_83 : g_shell_arrow;
    if (c)
        gfx_cursor_set(c);
}
static void hook_planet_pump(bool frame)
{
    sys_pump();
    if (frame)
        shell_frame_update();
}
static void hook_modal_begin(void)
{
    shell_cancel_drag();
    if (g_tilechooser_window && wm_is_visible(g_tilechooser_window))
        wm_send(g_tilechooser_window, g_tilechooser_window, WM_MSG_RBUTTONDOWN, 0, 0);
}
static bool hook_timeouts(void) { return shell_menu_checked(MENU_MSGBOX_TIMEOUT); }
static void hook_cd_spin_up(void) {}
static void hook_voice_hold(bool hold)
{
    if (hold)
        advisor_silence();
    else
        advisor_unsilence();
}
static void hook_window_update(void) { wm_update(g_main_window); }
/* soultrack_step 0x47e3be: 100 ms of frames after moving a walker */
static void hook_soultrack_wait(void)
{
    uint32_t start = tm_get_time();
    do {
        sys_pump();
        shell_frame_update();
    } while (tm_get_time() - start < 100);
}
static void hook_repaint(void) { g_shell_repaint = 1; }
static void hook_sound_stop(uint32_t id)
{
    if (snd_running())
        snd_stop(id);
}
/* the bankruptcy (0x444d23): the helpers resign */
static void hook_helpers_off(void)
{
    if (advisor_window() && wm_is_visible(advisor_window()))
        shell_menu_command(MENU_HELPERS);
}
static void hook_mapobj_plane(int plane)
{
    map_view_select_plane(&g_map_view, &g_map_planes, plane);
}
/* 0x4ed550 map_object_redraw, the drawing (gfx/map_objects.h) */
static void hook_mapobj_redraw(const MapObjRedraw *r)
{
    WmWindow *m = g_main_window;
    if (!m || !m->surface)
        return;
    shell_view_sync_origin();
    int cur = g_map_view.plane;
    if (r->plane >= 1 && r->plane <= 3 && r->plane != cur)
        map_view_select_plane(&g_map_view, &g_map_planes, r->plane);
    int32_t fx = g_map_view.focus_x, fy = g_map_view.focus_y;
    uint32_t mask = g_map_view.set_mask;
    if (r->focus) {
        g_map_view.focus_x = r->x;
        g_map_view.focus_y = r->y;
        if (r->focus == 2)
            g_map_view.set_mask = r->set_mask;
    }
    GfxRect a = map_view_redraw_object(m->surface, &g_map_view, &g_map_planes, &m->client, NULL, r->x, r->y, r->size,
                                       r->slot, r->set, r->frame);
    g_map_view.focus_x = fx, g_map_view.focus_y = fy, g_map_view.set_mask = mask;
    if (g_map_view.plane != cur)
        map_view_select_plane(&g_map_view, &g_map_planes, cur);
    if (a.right >= a.left && a.bottom >= a.top) {
        if (r->now)
            wm_redraw(m, &a);
        else
            wm_invalidate(m, &a);
    }
}

/* ---- the port window's relayout (game/display.c) ------------------------ */

/* Not while the title's animations hold pointers into the main window's
 * surface (FLC targets), nor while the loading screen is up. */
static bool display_can_relayout(void) { return !g_load_win && !title_holds_surface(); }

/* Keeps a child's rect at the same distance from the right / bottom edge
 * (anchor_x / anchor_y), the parent having grown by dw, dh. */
static void anchor(GfxRect *r, int dw, int dh, bool anchor_x, bool stretch_x, bool anchor_y, bool stretch_y)
{
    if (anchor_x)
        r->left += dw;
    if (anchor_x || stretch_x)
        r->right += dw;
    if (anchor_y)
        r->top += dh;
    if (anchor_y || stretch_y)
        r->bottom += dh;
}

static void relayout_scrollbar(WmWindow *sb, int dw, int dh, bool vertical)
{
    if (!sb)
        return;
    GfxRect r = sb->rect;
    if (vertical)
        anchor(&r, dw, dh, true, false, false, true);
    else
        anchor(&r, dw, dh, false, true, true, false);
    wm_resize_surface(sb, r.right - r.left + 1, r.bottom - r.top + 1, 0, 0, 0);
    wm_set_rect(sb, &r); /* the track puts its arrows at the ends and redraws */
}

/* The main window fills the screen; its menu bar and scrollbars follow
 * the edges. In the game the map keeps its centre and the views and the
 * control panel are moved back inside the client area; at the title the
 * picture is centred again and the buttons go along the new edges. */
static void display_relayout_game(int old_w, int old_h, int w, int h)
{
    (void)old_w, (void)old_h;
    WmWindow *m = g_main_window;
    if (!m)
        return;
    bool game = g_game_running != 0;
    int mw = m->rect.right - m->rect.left + 1, mh = m->rect.bottom - m->rect.top + 1;
    int dw = w - mw, dh = h - mh;
    int old_cw = m->client.right - m->client.left + 1, old_ch = m->client.bottom - m->client.top + 1;
    m->rect.left = m->rect.top = 0;
    wm_resize_surface(m, w, h, 0, 0, 0);
    m->client.right += dw;
    m->client.bottom += dh;
    if (m->menubar) {
        wm_menubar_update_buttons(m->menubar);
        menu_bar_resize(m->menubar, w);
    }
    relayout_scrollbar(m->hscroll, dw, dh, false);
    relayout_scrollbar(m->vscroll, dw, dh, true);
    for (WmWindow *c = m->children; c; c = c->next)
        if (c != m->menubar && c != m->hscroll && c != m->vscroll)
            wm_fit_into(c, &m->client);
    if (game)
        shell_view_resized(old_cw, old_ch);
    else
        title_relayout(); /* the title's buttons go back along the edges */
}

static void install_display_hooks(void)
{
    display_hooks.can_relayout = display_can_relayout;
    display_hooks.relayout = display_relayout_game;
}

static void install_hooks(void)
{
    /* the simulation (WP1) */
    g_sim_hooks.statusbar = statusbar_draw;
    g_sim_hooks.statusbar_date = statusbar_draw_date;
    g_sim_hooks.statusbar_refresh = statusbar_draw_weights;
    g_sim_hooks.message = hook_message;
    g_sim_hooks.news = news_popup;
    g_sim_hooks.sound = hook_sound;
    g_sim_hooks.sound_unload = hook_sound_unload;
    g_sim_hooks.music_fade = music_fade_out;
    g_sim_hooks.views_changed = hook_views_changed;
    g_sim_hooks.graph_record = graph_history_record;
    g_sim_hooks.graph_summary = graph_history_update;
    g_sim_hooks.graphview_draw = graphview_refresh;
    g_sim_hooks.mapview_refresh = mapview_refresh;
    g_sim_hooks.advisor_update = advisor_update;
    g_sim_hooks.cd_spin_up = hook_cd_spin_up;
    g_sim_hooks.select_plane = hook_sim_select_plane;
    /* the build tools (WP2) */
    g_build_hooks.sound = hook_sound;
    g_build_hooks.redraw = hook_redraw;
    g_build_hooks.redraw_fade = hook_redraw_fade;
    g_build_hooks.confirm = hook_confirm;
    g_build_hooks.refresh = hook_refresh;
    g_build_hooks.great_disturbance = hook_disturbance;
    g_build_hooks.views_refresh = views_refresh_all;
    /* the views (WP6a) and the Planet (WP5) */
    views_set_context(g_main_window, &g_map_view, &g_game_paused);
    views_hooks.select_plane = hook_views_select;
    views_hooks.help_balloon = help_balloon;
    views_hooks.center_on_cell = shell_center_on_cell;
    views_hooks.menu_command = hook_menu_command;
    planetview_set_context(g_main_window, &g_game_paused);
    planetview_hooks.help_balloon = help_balloon;
    planetview_hooks.menu_command = hook_menu_command;
    planetview_hooks.hidden = hook_pv_hidden;
    planetview_hooks.status_redraw = statusbar_draw;
    planetview_hooks.pump = hook_pump;
    planetview_hooks.busy_cursor = shell_busy_cursor;
    planetview_hooks.cursor = hook_pv_cursor;
    planetview_install_hooks();
    g_planet_hooks.pump = hook_planet_pump;
    g_planet_hooks.sound = hook_sound;
    /* the controls */
    ctl_hooks.modal_begin = hook_modal_begin;
    ctl_hooks.timeouts_enabled = hook_timeouts;
    ctl_set_pause_flag(&g_game_paused);
    akos_talk_frame_hook = shell_frame_update;
    /* the map objects (WP3) and the news pop-up */
    g_mapobj_hooks.redraw = hook_mapobj_redraw;
    g_mapobj_hooks.sound = hook_sound;
    g_mapobj_hooks.window_update = hook_window_update;
    g_mapobj_hooks.select_plane = hook_mapobj_plane;
    /* the Karma trains (WP1) */
    g_karma_hooks.sound = hook_sound;
    /* the Bad Things (WP4) */
    g_badthings_hooks.message = hook_message;
    g_badthings_hooks.news = news_popup;
    g_badthings_hooks.sound = hook_sound;
    g_badthings_hooks.sound_stop = hook_sound_stop;
    g_badthings_hooks.sound_unload = hook_sound_unload;
    g_badthings_hooks.music_play = music_switch;
    g_badthings_hooks.music_fade = music_fade_out;
    g_badthings_hooks.select_plane = hook_sim_select_plane;
    g_badthings_hooks.window_update = hook_window_update;
    g_badthings_hooks.repaint = hook_repaint;
    g_badthings_hooks.helpers_off = hook_helpers_off;
    news_hooks.paint_main = shell_view_paint;
    news_hooks.music = music_switch;
    news_hooks.voice_hold = hook_voice_hold;
    /* the Microview, the Macromanager and the Soulview (WP6b) */
    infoviews_install_hooks();
    microview_hooks.sound = hook_sound;
    microview_hooks.message = hook_mv_message;
    microview_hooks.news = hook_mv_news;
    microview_hooks.secret_talk = hook_mv_secret_talk;
    macro_hooks.sound = hook_sound;
    macro_hooks.message = hook_macro_message;
    macro_hooks.refresh_all = views_refresh_all;
    soultrack_hooks.step_wait = hook_soultrack_wait;
    shell_hooks.microview_cell = microview_open;
    shell_hooks.soulview_cell = soulview_track_cell;
    shell_hooks.microview_show = infoviews_microview_show;
    shell_hooks.soulview_show = infoviews_soulview_show;
    shell_hooks.macro_show = infoviews_macro_show;
    shell_hooks.views_changed = infoviews_changed;
    /* the tutorial (WP8) */
    shell_hooks.tutorial = tutorial_run;
    wm_hooks.attract_stop = tutorial_attract_stop;
}

/* ======================================================================== */
/* game_main 0x401000                                                         */
/* ======================================================================== */

static uint8_t *load_resource(uint32_t id, size_t *len)
{
    PlatFile *f = rc_open_resource(id);
    if (!f)
        return NULL;
    long l = rc_resource_size(id);
    uint8_t *buf = l > 0 ? malloc((size_t)l) : NULL;
    if (buf && plat_file_read(f, buf, 1, (size_t)l) != (size_t)l) {
        free(buf);
        buf = NULL;
    }
    plat_file_close(f);
    *len = buf ? (size_t)l : 0;
    return buf;
}

/* the game palette (resource 0x21) with its cycling ranges */
static void load_palette(void)
{
    size_t len = 0;
    uint8_t *raw = load_resource(0x21, &len);
    free(g_palette);
    g_palette = raw ? gfx_palette_load_resource(raw, len) : NULL;
    free(raw);
    if (g_palette)
        gfx_palette_set(g_palette, 10, 0xec);
}

int game_main_init(int argc, char **argv)
{
    akos_initialize(); /* 0x4b9470 */
    akos_talk_initialize();
    gfx_font_load(500);
    gfx_font_load(501);
    load_palette();
    g_prev_cursor = gfx_cursor_get();
    g_shell_arrow = gfx_cursor_load(0x7d);
    wm_set_default_cursor(g_shell_arrow);
    gfx_cursor_set(g_shell_arrow);
    g_state.hdr.difficulty = 1;
    planet_tables_load();

    static const uint32_t sb[8] = {0x4002, 0x4003, 0x4006, 0x4005, 0x4000, 0x4001, 0x4006, 0x4004};
    for (int i = 0; i < 8; i++)
        g_scroll_bmps[i] = gfx_load_bmp_resource(sb[i], NULL);
    ctl_scroll_set_hbitmaps(g_scroll_bmps[0], g_scroll_bmps[1], g_scroll_bmps[2], g_scroll_bmps[3]);
    ctl_scroll_set_vbitmaps(g_scroll_bmps[4], g_scroll_bmps[5], g_scroll_bmps[6], g_scroll_bmps[7]);
    for (int i = 0; i < 9; i++)
        g_menu_skin[i] = gfx_load_bmp_resource((uint32_t)(0x4010 + i), NULL);
    menu_set_skin(g_menu_skin[0], g_menu_skin[1], g_menu_skin[2], g_menu_skin[3], g_menu_skin[4], g_menu_skin[5],
                  g_menu_skin[6], g_menu_skin[7], g_menu_skin[8]);
    menu_set_font(501);

    GfxRect full = {0, gfx_screen_width() - 1, 0, gfx_screen_height() - 1};
    g_main_window = wm_create_toplevel(&full, title_wnd_proc, WM_F_MENUBAR | WM_F_VSCROLL | WM_F_HSCROLL | WM_F_CLIPMOUSE);
    if (!g_main_window)
        return -1;
    wm_set_title(g_main_window, "Afterlife");
    install_display_hooks(); /* port: the port window follows the window's size */
    if (g_main_window->menubar)
        wm_show(g_main_window->menubar, false);
    wm_show_scrollbars(g_main_window, 3, false);
    ctl_msgbox_init();
    news_init(); /* 0x478304 */
    ctl_hooks.pump = sys_pump;
    /* The original starts with wm_input_live 0, so the first click at the
     * title runs attract_stop(0) (0x4ac69c), which ends a tutorial that
     * isn't running (harmless there: silences the advisors, re-centres the
     * view). The port starts live; only tutorial_run turns it off. */
    wm_set_input_live(true);

    if (g_arg_load_file[0] == '\0') {
        g_game_in_progress = 0;
        if (title_run() != 0)
            return 1;
        wm_show(g_main_window, true);
        rand_range(0, 65000);
    } else {
        wm_show(g_main_window, true);
        g_game_in_progress = 1;
    }

    loading_open(14);
    gfx_fill_rect(g_main_window->surface, 0, 0, g_main_window->surface->width, g_main_window->surface->height, 0x13);
    wm_redraw(g_main_window, NULL);
    load_step();
    if (!shell_view_init(g_palette)) {
        fprintf(stderr, "game_main: no tile sets\n");
        shell_view_shutdown();
        return -1;
    }
    load_step();
    /* the views are children of the main window, as in the original
     * (their create functions take it from the views' context) */
    views_set_context(g_main_window, &g_map_view, &g_game_paused);
    soulview_create();
    load_step();
    mapview_create();
    load_step();
    planetview_create(g_main_window);
    load_step();
    advisor_create();
    load_step();
    macro_create();
    load_step();
    load_step();
    load_step(); /* the audio dialog (snd_audio_dialog_create) */
    graphview_create();
    load_step();
    microview_create();
    load_step();
    if (!ctrlpanel_create())
        return -1;
    load_step();
    if (g_palette)
        gfx_palette_set(g_palette, 10, 0xec);
    WmWindow *views[7] = {soulview_window(), microview_window(), advisor_window(), mapview_window(),
                          graphview_window(),  macro_window(),     planetview_window()};
    ctrlpanel_place_windows(views, 7);
    g_game_speed = 0;
    about_create();
    load_step();
    install_hooks();
    sim_reset();
    game_init_defaults();
    load_step();
    loading_close();
    wm_show(g_main_window, false);
    g_shell_cursor = g_shell_tool_cursors[0x15];

    g_main_window->proc = game_view_proc;
    shell_view_reset_drag();
    g_scroll_x = g_scroll_y = 0;
    shell_view_attach();
    shell_center_on_cell(GS_PLANE_HEAVEN, 99, 99);
    wm_raise(g_ctrlpanel_window);
    wm_show(g_ctrlpanel_window, true);
    statusbar_reset();
    statusbar_draw();
    create_menus();
    if (g_pending_load[0]) {
        load_file(g_pending_load);
        plat_release_dropped_file(g_pending_load); /* (a file dropped onto the title) */
        g_pending_load[0] = '\0';
    }
    if (g_arg_load_file[0]) {
        char path[1024];
        if (!plat_data_path(g_arg_load_file, path, sizeof path))
            snprintf(path, sizeof path, "%s", g_arg_load_file);
        load_file(path);
    }
    if (g_main_window->menubar)
        wm_show(g_main_window->menubar, true);
    wm_show_scrollbars(g_main_window, 3, true);
    shell_view_update_scrollbars();
    if (g_main_window->menubar)
        menu_show(g_main_window->menubar, true);
    shell_menu_set_checked(MENU_CTRLPANEL, true);
    shell_menu_set_checked(MENU_SPEED_PAUSE, true);
    shell_menu_set_checked(MENU_TECH_ANNOUNCE, true);
    shell_menu_set_checked(MENU_DISASTER_ANNOUNCE, true);
    shell_menu_set_checked(MENU_MSGBOX_TIMEOUT, true);
    planetview_set_menu_checks(true, true);
    g_shell_tool = 0x15;
    g_game_running = 1;
    gfx_cursor_show(true);
    wm_show(g_main_window, true);
    shell_view_repaint();
    g_shell_repaint = 0;
    wm_activate(g_main_window);
    /* port-only: the original never opens its debug window (0x457b01) */
    if (debugwin_requested(argc, argv))
        debugwin_open();
    wm_flush_input();
    g_game_in_progress = 1;
    scenario_box();
    return 0;
}

void game_main_step(void)
{
    sys_pump();
    shell_frame_update();
    g_sim.paused = (uint8_t)(g_game_paused != 0);
    sim_update(g_game_speed);
    sys_pump();
    planetview_blink_cursor();
    poll_drop();
    run_command();
}

void game_main_shutdown(void)
{
    display_hooks = (DisplayHooks){0};
    snd_slot_stop_stream(SND_VOICE_SLOT);
    music_stop();
    akos_talk_shutdown(); /* 0x4b94b0 */
    akos_shutdown();
    about_destroy();
    debugwin_destroy();
    microview_destroy();
    macro_destroy();
    soulview_destroy();
    advisor_destroy();
    graphview_destroy();
    mapview_destroy();
    planetview_shutdown();
    news_shutdown();
    ctrlpanel_destroy();
    shell_view_shutdown();
    if (g_main_window) {
        wm_show(g_main_window, false);
        wm_destroy(g_main_window);
        g_main_window = NULL;
    }
    for (int i = 0; i < 8; i++) {
        gfx_surface_destroy(g_scroll_bmps[i]);
        g_scroll_bmps[i] = NULL;
    }
    for (int i = 0; i < 9; i++) {
        gfx_surface_destroy(g_menu_skin[i]);
        g_menu_skin[i] = NULL;
    }
    memset(g_menu_items, 0, sizeof g_menu_items);
    gfx_cursor_set(g_prev_cursor);
    gfx_cursor_destroy(g_shell_arrow);
    g_shell_arrow = NULL;
    free(g_palette);
    g_palette = NULL;
}

/* 0x401000 */
int game_main(int argc, char **argv)
{
    int r = game_main_init(argc, argv);
    if (r == 0) {
        do
            game_main_step();
        while (g_game_running);
    }
    game_main_shutdown();
    return r < 0 ? 1 : 0;
}
