/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The title sequence (WP7, ALIFE.EXE 0x426460-0x42a6c2): the intro
 * animation (FLC, src/formats/flc.c), the animated title, the main menu
 * ("Start a New Game", "Load a Saved Game", "Load a Scenario", "Replay
 * Cheesy Intro", "Quit") with the difficulty menu, palette fades, and
 * title_wnd_proc (0x429c4d), the main window's procedure while it runs.
 *
 * game_main calls title_run() when no file was given on the command line.
 * A new game leaves the chosen difficulty in g_state.hdr.difficulty; the
 * load entries call shell_choose_load() (game/shell.h), which remembers
 * the file game_main loads afterwards.
 *
 * Resources: intro FLI 5000 (palette 0x4b1, music 0x10031), title FLCs
 * 5101 / 5103 / 5104 with the ticker bitmap 0x4be (palette 0x4bd, music
 * 0x10069, effects 0x10036 / 0x10037 / 0x10004 / 0x1002e, voice lines
 * 0x10072-0x1007d and 0x10086-0x1008f streamed in slot 1), menu
 * background 0x4b4 (palette 0x4b5, music 0x1002c), button decoration
 * 0x4b3, button bitmap 0x229. The FLCs are only in the CD bundle.
 *
 * Everything is driven by the modal pump (ctl_hooks.pump, sys_pump in the
 * game), so it runs headless and any key or click skips the current
 * animation (Esc skips them all).
 */
#ifndef AFTERLIFE_UI_TITLE_H
#define AFTERLIFE_UI_TITLE_H

#include "ui/wm.h"

/* Main menu button ids (title_wnd_proc's WM_MSG_CTL_CLICK). */
enum {
    TITLE_BTN_NEW = 2000,      /* -> difficulty menu */
    TITLE_BTN_LOAD = 0x7d1,    /* shell_choose_load(1) */
    TITLE_BTN_SCENARIO = 0x7d2,/* shell_choose_load(2) */
    TITLE_BTN_INTRO = 0x7d3,   /* replay the intro */
    TITLE_BTN_QUIT = 0x7d4,    /* "Do You Really Want To Quit?" */
    TITLE_BTN_EASY = 0x898,    /* difficulty = id - 0x897 */
    TITLE_BTN_MEDIUM = 0x899,
    TITLE_BTN_HARD = 0x89a,
    TITLE_BTN_BACK = 0x89b,    /* back to the main menu */
};

/* Port-only: what the title is doing (for tests and the shell). */
typedef enum {
    TITLE_STAGE_NONE = 0, /* title_run isn't running */
    TITLE_STAGE_INTRO,    /* the intro animation */
    TITLE_STAGE_ANIM,     /* the animated title */
    TITLE_STAGE_MENU_BG,  /* the menu background before the buttons */
    TITLE_STAGE_MENU,     /* waiting in the main menu */
    TITLE_STAGE_DIFFICULTY, /* waiting in the difficulty menu */
    TITLE_STAGE_FADE,     /* a palette fade */
} TitleStage;
TitleStage title_stage(void);
/* Port-only: true while the intro and the animated title run (they hold
 * pointers into the main window's surface; game/display.c waits). */
bool title_holds_surface(void);
/* Port-only: the main window changed size; the menu background and
 * buttons are laid out again for it. */
void title_relayout(void);

/* 0x426460 title_run: returns non-zero if the player quit. */
int title_run(void);

/* 0x429c4d title_wnd_proc */
intptr_t title_wnd_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b);

/* 0x42a6c3 di_video_benchmark: bit 1 = slow bitmap copy (the intro isn't
 * scaled up), bit 0 = slow window update (no fades, every other intro
 * frame shown). Port: the INI "Video Settings" value, or 0 (no timing
 * benchmark). */
unsigned di_video_benchmark(void);

#endif
