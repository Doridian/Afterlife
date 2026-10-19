/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The game shell (WP7 in docs/game-map.md, ALIFE.EXE 0x401000-0x409dbf):
 * game_main and its main loop, the main window's procedure
 * (game_view_proc 0x404023: map clicks, scrolling, tools, keyboard
 * shortcuts, the menu-command switch), the menu bar, the main map view
 * (scroll, zoom, rotation, repaint), the save / load / new game flows.
 *
 * The other shell modules (src/ui/ctrlpanel.c, statusbar.c, title.c,
 * advisors.c, news.c, about.c) share the globals below, which the
 * original keeps as plain globals too. Game code only; no SDL.
 */
#ifndef AFTERLIFE_GAME_SHELL_H
#define AFTERLIFE_GAME_SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/platform.h"
#include "ui/wm.h"

/* ---- shared globals -------------------------------------------------------- */

extern WmWindow *g_main_window;        /* 0x5f7460 main_window */
extern WmWindow *g_ctrlpanel_window;   /* 0x5c6674 ctrlpanel_window */
extern WmWindow *g_tilechooser_window; /* 0x5bb200 tilechooser_window */
extern int g_game_paused;              /* 0x5fa61c game_paused (also g_sim.paused) */
extern uint8_t g_game_speed;           /* 0x57e09b: 0 stopped ("Divine Intervention"), 1 fastest .. 3 */
extern uint8_t g_game_in_progress;     /* 0x57e099: the title is over / a game is running */
extern int g_game_running;             /* 0x650054 game_running: cleared to quit */
extern uint8_t g_game_command;         /* 0x650048: run by the main loop (GAME_CMD_*) */
extern uint8_t g_game_next_command;    /* 0x65004c */
extern uint8_t g_shell_repaint;        /* 0x5f7474: repaint the whole main view next frame */
extern int g_shell_tool;               /* 0x5f25a4: control panel tool (button id) */
extern PlatCursor *g_shell_arrow;      /* 0x6464cc: the game's arrow cursor (0x7d) */
extern PlatCursor *g_shell_cursor;     /* 0x6462d0: cursor over the main view (by tool) */
extern PlatCursor *g_shell_tool_cursors[0x17]; /* 0x6464dc[1..0x16]: cursors 0x66.. per tool */
extern PlatCursor *g_shell_busy_cursor;        /* 0x646540 (0x84) */
extern PlatCursor *g_shell_cursor_83;          /* 0x64653c (0x83) */

/* game_command values (game_main's switch) */
enum {
    GAME_CMD_NONE = 0,
    GAME_CMD_SAVE = 1,          /* 0x480ea4(1) */
    GAME_CMD_LOAD = 2,          /* 0x489e90(1): a saved game */
    GAME_CMD_DELETE = 3,        /* 0x4946bd */
    GAME_CMD_NEW = 4,           /* difficulty box, game_reset_world */
    GAME_CMD_LOAD_SCENARIO = 5, /* 0x489e90(2) */
    GAME_CMD_SAVE_SCENARIO = 6, /* 0x480ea4(2), editor only */
    GAME_CMD_SAVE_DOWNLOAD = 7, /* port-only (web): save into a download */
    GAME_CMD_DOWNLOAD = 8,      /* port-only (web): download a saved game */
    GAME_CMD_LOAD_DROPPED = 9,  /* port-only: load the file dropped onto the window */
};

/* Menu command ids (the items' ids, WM_MSG_MENU_COMMAND a). */
enum {
    MENU_SAVE = 1, MENU_LOAD = 2, MENU_LOAD_SCENARIO = 3, MENU_DELETE = 4, MENU_NEW = 5, MENU_EXIT = 6,
    MENU_AUDIO = 7, MENU_TEMPO = 8, MENU_BAD_THINGS = 9, MENU_MISC = 10,
    MENU_SPEED_FAST = 0xb, MENU_SPEED_MEDIUM = 0xc, MENU_SPEED_SLOW = 0xd, MENU_SPEED_PAUSE = 0xe,
    MENU_BAD_FIRST = 0xf, /* .. 0x16: Birds, Bats, Blue Heaven, Freezes, Nose, Handbasket, Dice, Disco */
    MENU_MACRO = 0x17, MENU_PLANETVIEW = 0x18, MENU_GRAPHVIEW = 0x19, MENU_MAPVIEW = 0x1a,
    MENU_HELPERS = 0x1b, MENU_ABOUT = 0x1c, MENU_CTRLPANEL = 0x1d,
    MENU_AUTOSCROLL = 0x1f, MENU_TECH_ANNOUNCE = 0x20, MENU_DISASTER_ANNOUNCE = 0x21,
    MENU_MSGBOX_TIMEOUT = 0x22, MENU_HIDE_SCROLLBARS = 0x23,
    MENU_TUTORIAL_FIRST = 0x25, /* .. 0x2c */
    MENU_NO_BAD_THINGS = 0x31,
    MENU_PORT_OPTIONS = 0x3a, /* port-only: Global > Port Options... */
    MENU_SAVE_DOWNLOAD = 0x3c, /* port-only (web): File > Save to Download */
    MENU_DOWNLOAD = 0x3d,      /* port-only (web): File > Download Saved Game */
    MENU_IDS = 0x3e
};

/* Control panel button ids (ctrlpanel_proc). 1-8 zones, 9 ports, 10
 * roads, 11 Karma stations, 12 rails, 13-19 chooser tools, 20 bulldozer,
 * 21 Microview, 22 Soulview, 0x17-0x19 zone blocks, 0x1a grid, 0x1b
 * Graphview, 0x1c zoom in, 0x1d zoom out, 0x1e Planetview, 0x1f helpers,
 * 0x20 Mapview, 0x21-0x24 rotation, 0x25 Macromanager. */
#define CTRL_BUTTONS 0x26

/* Messages to the main window. */
#define SHELL_MSG_REPAINT 0x8235 /* full repaint of the main view (and the rotation buttons) */

/* ---- shell services ---------------------------------------------------------- */

/* wm_send(main_window, item, WM_MSG_MENU_COMMAND, id, 0), as the game
 * runs its own menu commands. */
void shell_menu_command(unsigned id);
WmWindow *shell_menu_item(unsigned id); /* 0x646560[id] */
bool shell_menu_checked(unsigned id);
void shell_menu_set_checked(unsigned id, bool checked);

/* The control panel's button `id` (0x6462f0[id]), or NULL. */
WmWindow *shell_ctrl_button(int id);

/* 0x407282 game_frame_update: cursor, edge scroll, repaint request,
 * credits, the 125 ms clock (map objects, music, status). Modal loops
 * that keep the game alive call it between pumps. */
void shell_frame_update(void);

/* 0x408ca4 ui_busy_cursor */
void shell_busy_cursor(bool on);

/* 0x408c4d views_refresh_all */
void views_refresh_all(void);

/* 0x406be8: scroll the main view so that cell (x, y) of `plane` is
 * centred. */
void shell_center_on_cell(int plane, int x, int y);

/* 0x403c23: ends a pending tool drag (removes its highlight). */
void shell_cancel_drag(void);

/* The current map plane of the main view (map_view +0x47) and the
 * selection of both halves of 0x42b102. Returns the plane before. */
int shell_select_plane(int plane);

/* ---- load / save --------------------------------------------------------- */

/* The original's load list (0x494112, mode 1 saved games / 2 scenarios)
 * as the title uses it: lets the player pick a file and remembers it as
 * the game to load once the game modules are up (0x65006c). True if one
 * was picked. */
bool shell_choose_load(int mode);
/* The pending load (0x65006c); "" = none. */
const char *shell_pending_load(void);

/* Port-only: a saved game or scenario dropped onto the window (on the web
 * always, natively with the enhancements on). Takes the latest drop into
 * out; the title then leaves for the game with it as the pending load, the
 * game loads it (asking to save first) from game_main_step. */
bool shell_take_drop(char *out, size_t out_len);
void shell_set_pending_load(const char *path);

/* A file picker (port: stands in for the original's slot dialogs). `mode`
 * 1 = load a saved game, 2 = load a scenario, 3 = save. Runs a modal
 * loop. Returns true with the path in out. Tests can replace it. */
typedef bool (*ShellPickFn)(int mode, char *out, size_t out_len);
extern ShellPickFn shell_pick_file_hook;
bool shell_pick_file(int mode, char *out, size_t out_len);

/* save_slot_path 0x480552: "<save folder>/agame%03u.alf" (mode 1, saved
 * games) or "ascen%03u.asc" (mode 2, scenarios). The save folder is INI
 * "Save games" when usable here, else the data dir, or
 * shell_save_dir_override (tests). */
extern const char *shell_save_dir_override;
void shell_slot_path(int mode, unsigned slot, char *out, size_t n);
/* save_write_game 0x485fb3: writes the game with `title` in its header. */
bool shell_save_game_to(const char *path, const char *title);
/* save_load_game 0x48f837 of a file, or (slot >= 10000, save_open_slot
 * 0x47fca7) of a save in the bundles, then what the shell does after a
 * load (views, main view, status). A message box on failure. */
bool shell_load_game_from(const char *path);
bool shell_load_game_resource(uint32_t id);

/* ---- hooks into packages that aren't ported yet (NULL = skipped) ------- */

typedef struct ShellHooks {
    /* map objects of `plane` every 125 ms (0x4ee088 + map_objects_step,
     * WP3); speed = the game speed */
    void (*map_objects_tick)(int plane, int speed);
    /* the Microview / Soulview tools clicked map cell (x, y) (0x46f613 and
     * the Soulview's 0x83e8, WP6b) */
    void (*microview_cell)(int x, int y);
    bool (*soulview_cell)(int x, int y);
    /* views WP6b owns: Microview 0x5c6670, Soulview 0x5b2f50, Macromanager
     * 0x5f7468 (show / hide), the Audio Settings window 0x638ea0
     * (ui/audiodlg.h; 0x5fa5f0 is the unused tutorial text window,
     * ui/tutorialwin.h) */
    void (*microview_show)(bool on);
    void (*soulview_show)(bool on);
    void (*macro_show)(bool on);
    void (*views_changed)(void); /* microview_refresh / macro_refresh */
    void (*audio_dialog)(bool on);
    /* the tutorial runner (0x425e3b, WP8) */
    void (*tutorial)(int n);
} ShellHooks;

extern ShellHooks shell_hooks;

/* ---- entry --------------------------------------------------------------- */

/* 0x401000 game_main(argc, argv): returns the exit code. */
int game_main(int argc, char **argv);

/* Port: one turn of the main loop (sys_pump, frame update, sim_update,
 * pump, cursor blink, game_command), for tests driving it step by step.
 * game_main is game_main_init + while (running) game_main_step +
 * game_main_shutdown. */
int game_main_init(int argc, char **argv); /* 0 ok, 1 quit at the title, -1 error */
void game_main_step(void);
void game_main_shutdown(void);

#endif
