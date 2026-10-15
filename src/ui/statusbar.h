/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The status bar in the control panel (WP7, ALIFE.EXE 0x446904-0x447e8e):
 * pennies and the price of the current tool at the top, the tool's name,
 * the year with the scheduler's progress, the sin/virtue weights diagram,
 * and SOULs, lost SOULs, EMBOs and the soul rate at the bottom. Drawn into
 * the control panel window's surface (g_ctrlpanel_window).
 */
#ifndef AFTERLIFE_UI_STATUSBAR_H
#define AFTERLIFE_UI_STATUSBAR_H

/* 0x447243 statusbar_draw(ctrlpanel_window) */
void statusbar_draw(void);
/* 0x446904 statusbar_draw_date: "Year: n" with the digits rolling over as
 * the scheduler's phases pass. */
void statusbar_draw_date(void);
/* 0x446dc5: the weights diagram, one bar highlighted in turn (every 125 ms
 * clock tick while the game runs). */
void statusbar_draw_weights(void);
/* Port: forget the remembered values (new game / load), so the colours
 * don't compare against the previous game. */
void statusbar_reset(void);

#endif
