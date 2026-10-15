/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The news pop-up and the help balloon (WP7, ALIFE.EXE 0x4761a0-0x478d4f).
 *
 * One top-level window (0x57e1b0, bitmap 0x44c) serves both:
 *   - help_balloon_show (0x478a14): the right-click help of every
 *     "This button ..." function, a balloon with word-wrapped text that
 *     takes the capture and closes on a left click inside it, any right
 *     click, Esc, or losing the capture;
 *   - news_popup_show (0x476582): announcements (special structures,
 *     "Twilight of the Demiurges", ...) by id: the main view is darkened
 *     and the other windows, the menu bar and the scrollbars hidden, the
 *     picture `id` (palette resource id + 1) is shown in the middle with
 *     a balloon over it, and the announcer's voice lines play. While it
 *     is up the main window runs news_main_proc (0x4761a0) instead of its
 *     own procedure. A click in the balloon (or the timeout task
 *     0x4782a2) closes it and restores everything, reloading the game
 *     palette (resource 0x21).
 * news_init (0x478304) creates the window and registers the timeout task.
 */
#ifndef AFTERLIFE_UI_NEWS_H
#define AFTERLIFE_UI_NEWS_H

#include <stdbool.h>
#include <stdint.h>

#include "ui/wm.h"

/* What the pop-up needs from modules that aren't ported or belong to
 * someone else. Every hook may be NULL. */
typedef struct NewsHooks {
    /* 0x4076f5 map_view_paint(main_window, map_view, scroll, x, y, w, h):
     * repaint that rectangle of the main window's surface with the map
     * (the strips under the menu bar and scrollbars, which the pop-up
     * hides; the main window's client rect is the whole window while this
     * is called). NULL: those strips keep what they had. */
    void (*paint_main)(int x, int y, int w, int h);
    /* 0x4754d7: switch the music to track `id` (stop the current track
     * 0x66439c, remember and stream the new one if music is on). NULL:
     * snd_play(id, 4). */
    void (*music)(uint32_t id);
    /* 0x426427 / 0x426449: set / clear 0x6392c8, which holds back the
     * advisors' and the tutorial's voices (the pop-up stops the voice
     * stream itself). NULL: skipped. */
    void (*voice_hold)(bool hold);
} NewsHooks;
extern NewsHooks news_hooks;

/* 0x478304: loads the bitmaps, creates the window (hidden) and registers
 * the timeout task. Call after the main window exists. */
void news_init(void);
/* Port-only: unregisters the task, destroys the window, frees bitmaps. */
void news_shutdown(void);
/* 0x57e1b0 */
WmWindow *news_window(void);

/* 0x478a14 help_balloon_show(x, y, width, text, set_font, capture):
 * screen position, text width limit in pixels. set_font selects font 500
 * in colors 0x1a / 0x13 (otherwise the current font draws); capture
 * hides the mouse cursor while the balloon is up. */
void help_balloon_show(int x, int y, int width, const char *text, bool set_font, bool capture);
/* views_hooks / planetview_hooks form (set_font and capture on). */
void help_balloon(int x, int y, int width, const char *text);
/* The balloon or a pop-up is up. */
bool news_active(void);

/* 0x476582 news_popup_show(id, arg, timeout_ms): shows pop-up `id` (arg
 * is unused, timeout 0 = none). Like the original it pumps (sys_pump)
 * only while the voice lines play and returns with the pop-up still up;
 * a click in the balloon or the timeout closes it later. */
void news_popup_show(unsigned id, int arg, uint32_t timeout_ms);
/* g_sim_hooks.news form: news_popup_show(id, 0, 120000). */
void news_popup(unsigned id);
/* A pop-up is up (0x66445c). */
bool news_popup_active(void);

/* 0x4764da: remaps every pixel of w's surface through shade table
 * `table` (gfx_palette_shade_table) and redraws it. */
void news_darken_window(WmWindow *w, unsigned table);

/* 0x4761a0: the main window's procedure while a pop-up is up. */
intptr_t news_main_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b);
/* 0x478431: the balloon window's procedure. */
intptr_t news_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b);

#endif
