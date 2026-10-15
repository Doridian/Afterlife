/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The "Afterlife Tutorial" text window (ALIFE.EXE 0x4ab8e0-0x4ac2dd, WP8):
 * a framed window (bitmap 607) with a scrolling text viewer and "<< Prev" /
 * "Next >>" buttons that pages through ten text resources (1401..1410,
 * "\n" escapes, wrapping at the end, "\0" ends a page).
 *
 * Nothing in ALIFE.EXE creates it: tutorial_window_create has no callers
 * and no pointer to it exists, though game_main lists its window
 * (0x5fa5f0) among the game's windows. The lessons the Tutorial menu runs
 * are the scripted ones (game/tutorial.h). Ported for completeness and
 * left unwired, like the original.
 */
#ifndef AFTERLIFE_UI_TUTORIALWIN_H
#define AFTERLIFE_UI_TUTORIALWIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui/wm.h"

#define TUTORIALWIN_FIRST_PAGE 0x579 /* 1401 */
#define TUTORIALWIN_LAST_PAGE 0x582  /* 1410 */

/* 0x4ab8e0: creates the window (hidden) under the main window. True if
 * it exists. */
bool tutorialwin_create(WmWindow *main);
void tutorialwin_destroy(void); /* port-only */
WmWindow *tutorialwin_window(void); /* 0x5fa5f0 */
/* 0x4abeaf: loads page `id` (wrapping into 0x579..0x582), shows it and
 * the window. False if the page can't be read. */
bool tutorialwin_show_page(uint32_t id);
uint32_t tutorialwin_page(void); /* 0x66fccc */
/* 0x4abc3d */
intptr_t tutorialwin_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b);

/* The text filter of 0x4abeaf, pure: control characters are dropped, a
 * CR becomes a space (unless one is there already or a "\n" came just
 * before), "\n" a line break, "\0" ends the text, "\x" is x. `out` must
 * hold n + 1 bytes. Returns the length written (NUL-terminated). */
size_t tutorialwin_filter(const uint8_t *in, size_t n, char *out);

#endif
