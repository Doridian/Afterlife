/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Port-only: the game data when no DATA_DIR is given and none is in the
 * default place (a packaged build started from its icon). See datafind.c.
 */
#ifndef AFTERLIFE_PLATFORM_DATAFIND_H
#define AFTERLIFE_PLATFORM_DATAFIND_H

#include <stdbool.h>

/* True if dir holds the game files the port needs: ALIFE.INI, ALIFE.EXE
 * and a .000 bundle (names matched case-insensitively). */
bool datafind_has_game_data(const char *dir);

/* The data folder to use: def if it has the game data; else, if `ask`
 * (a window is to come), the folder the player picked before, or one they
 * pick now: the CD (or a folder with its files), or a CD image whose game
 * files are copied into the pref folder. def if not asking, NULL if the
 * player gave up (Quit). Needs SDL's video initialised. */
const char *datafind_data_dir(const char *def, bool ask);

#endif
