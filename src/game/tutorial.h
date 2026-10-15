/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The tutorial (WP8 in docs/game-map.md, ALIFE.EXE 0x41f611-0x42645f and
 * 0x4ac2de-0x4acbee).
 *
 * Afterlife's tutorial is a self-running demonstration, not an exercise:
 * picking a lesson from the Tutorial menu loads a prepared game (ALSV
 * resources 10000..10008 in the CD's ALIFE.000), and Jasper and Aria talk
 * the player through it while the game drives its own mouse: the pointer
 * glides to control panel buttons, menu items, map cells and parts of the
 * views and clicks, drags or right-clicks there. The real input is shut
 * off meanwhile (the window manager's "attract mode", wm_input_live 0); a
 * click or Esc ends the lesson early. When it is over the game the player
 * had (saved as "Temp Game From Hell", slot 200, before the lesson) is
 * loaded back and the windows are put back where they were.
 *
 * Pieces:
 *   0x425e3b  tutorial_run(n): n 1..9 starts lesson n (hides the views,
 *             sets up the advisor window, attract mode on, runs the
 *             script); n 0 ends it (silences, reloads the player's game,
 *             re-centres the view). The Tutorial menu's commands 0x25..0x2c
 *             call it with id - 0x24; lesson 9, "Basic Economics", has no
 *             menu item.
 *   0x41f611  the script: saves, loads the lesson, sets the view up
 *             (zone blocks on, zoomed out, unrotated) through scripted
 *             clicks, then runs the lesson (a switch with one long
 *             sequence of calls per lesson; here an op table per lesson,
 *             tutorial_script.c), then ends: attract mode off, windows
 *             restored (0x425ab0, 0x4259c6), "All Done!".
 *   0x4ac69c  attract_stop(on): the attract mode (wm_input_live) and the
 *             task that glides the pointer (0x4ac72b, every 10 ms, 10
 *             pixels per axis); the window manager calls it with 0 when
 *             real input arrives.
 *   0x4ac2de..0x4acbee  the scripted input: glide to a control (0x4ac939),
 *             a menu item (0x4ac9dd), a map cell (0x4aca7d, scrolls the
 *             main view until the cell is visible) or a point (0x4ac880);
 *             wait for the glide (0x4ac56f); press/release left and right
 *             (0x4ac3dc click, 0x4ac471 down, 0x4ac4f0 up, 0x4ac2de right
 *             down, 0x4ac35d right up; each first glides back to where the
 *             last glide ended if the pointer moved); run the game for n
 *             seconds (0x4ac5a3) or n years (0x4ac61f); post a key
 *             (0x4acbee, unused).
 *
 * The lines Jasper and Aria speak are ALIFE.EXE texts looked up by their
 * original addresses (ui/exe_text.h, data compiled in from src/data) and
 * voiced from the bundles, like the
 * advisors' speeches. Game code only; no SDL.
 */
#ifndef AFTERLIFE_GAME_TUTORIAL_H
#define AFTERLIFE_GAME_TUTORIAL_H

#include <stdbool.h>
#include <stdint.h>

#define TUTORIAL_LESSONS 9       /* 1..9; the menu has 1..8 */
#define TUTORIAL_TEMP_SLOT 200   /* "Temp Game From Hell" */
#define TUTORIAL_SAVE_BASE 9999  /* lesson n is resource 9999 + n */

/* 0x425e3b */
void tutorial_run(int n);
/* 0x4ac69c(0): for wm_hooks.attract_stop (real input during the lesson). */
void tutorial_attract_stop(void);
/* 0x63869c: a lesson is running (the scripted input is live). */
bool tutorial_active(void);
/* The lesson's title (the script's name table, 0x65a100..), "" if none. */
const char *tutorial_lesson_name(int n);

/* 0x4acbee (no callers): posts a key press and release while a lesson
 * runs. */
void tutorial_post_key(uint8_t ch);

/* ---- the lesson scripts (tutorial_script.c) ------------------------------- */

/* One step of a lesson: each is one call (or one guard) of the original
 * lesson code, in order. */
enum {
    TO_END = 0,
    TO_IF,          /* if (!advisor_silenced && talk stamp unchanged) { */
    TO_ENDIF,       /* } */
    TO_SAY,         /* snd_talk_start(a, &text_rect[a], exe text b, voice c) */
    TO_WAIT_TALK,   /* akos_talk_wait(a) */
    TO_PUMP,        /* sys_pump */
    TO_FRAME,       /* game_frame_update */
    TO_GLIDE_CTL,   /* 0x4ac939(window a, control id b) */
    TO_GLIDE_MENU,  /* 0x4ac9dd(menu item id a) */
    TO_GLIDE_CELL,  /* 0x4aca7d(plane a, x b, y c) */
    TO_GLIDE_WIN,   /* 0x4ac880(window a's left + b, window c's top + d) */
    TO_SYNC,        /* 0x4ac56f: wait until the glide has arrived */
    TO_CLICK,       /* 0x4ac3dc */
    TO_DOWN,        /* 0x4ac471 */
    TO_UP,          /* 0x4ac4f0 */
    TO_RDOWN,       /* 0x4ac2de */
    TO_RUP,         /* 0x4ac35d */
    TO_SECONDS,     /* 0x4ac5a3(a) */
    TO_YEARS,       /* 0x4ac61f(a) */
    TO_CENTER,      /* mainview_center_on_cell(a, b, c) */
    TO_MOVE_WIN,    /* wm_move(window a, main client left/right (b) + c, top/bottom (d) + e) */
    TO_SPEED_PUSH,  /* save game_speed, set it to a */
    TO_SPEED_POP,   /* restore it */
    TO_ANIM_CELL,   /* map_anim_cell_add(&map_view, 0x52, 0x60, 0, 3, 2, 2, 3, 5) */
    TO_MAPVIEW_OUT, /* mapview_zoom(0, 0, 0) */
    TO_CLEAR_TEXT,  /* gr_fill_rect(advisor surface, 0xe, 0x91, 0x120, 0x46, 0x13) */
    TO_MICROVIEW,   /* 0x654030 = 1: the lesson opened the Microview */
    TO_COUNT
};

/* Windows the ops name. */
enum { TW_CTRLPANEL, TW_GRAPHVIEW, TW_MAPVIEW, TW_MICROVIEW, TW_ADVISOR, TW_MAIN };

typedef struct TutOp {
    uint8_t op;
    int32_t a, b, c, d, e;
} TutOp;

/* Lesson n's ops (ending with TO_END), NULL if n is out of range. */
const TutOp *tutorial_ops(int n);
const char *tutorial_op_name(int op);

/* ---- the pointer glide (0x4ac880 / 0x4ac72b), pure --------------------- */

typedef struct TutGlide {
    bool active;           /* 0x518f50 */
    int target_x, target_y; /* 0x518f54 / 0x518f58 */
    int step_x, step_y;    /* 0x518f5c / 0x518f60: +-speed */
    int x, y;              /* 0x518f64 / 0x518f68: the pointer */
    int dx, dy;            /* 0x518f6c / 0x518f70 (never read) */
    int end_x, end_y;      /* 0x518f74 / 0x518f78: where the last glide arrived */
} TutGlide;

#define TUTORIAL_GLIDE_SPEED 10 /* 0x66fcdc: pixels per step and axis */
#define TUTORIAL_GLIDE_MS 10    /* a step every > 10 ms */

/* 0x4ac880 with the pointer at (x, y). */
void tutorial_glide_start(TutGlide *g, int x, int y, int tx, int ty, int speed);
/* 0x4ac72b's step (after the timer check): moves the pointer, returns
 * true if a step was made; clears `active` on arrival. */
bool tutorial_glide_step(TutGlide *g);

/* ---- hooks (tests) --------------------------------------------------------- */

typedef struct TutorialHooks {
    /* timeGetTime for the glide and the waits; NULL = tm_get_time */
    uint32_t (*clock)(void);
    /* before each op of the lesson (index into tutorial_ops(lesson)) */
    void (*step)(int lesson, int index, const TutOp *op);
    /* after the lesson's ops, before the epilogue */
    void (*lesson_done)(int lesson);
    /* dry run: walk the ops without doing them (TO_IF counts as taken
     * unless dry_silent); the prologue and epilogue are skipped */
    bool dry, dry_silent;
} TutorialHooks;

extern TutorialHooks tutorial_hooks;

/* Port: the runtime glide state (tests). */
const TutGlide *tutorial_glide(void);

#endif
