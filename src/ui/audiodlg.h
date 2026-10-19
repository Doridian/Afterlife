/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The "Audio Settings" window (ALIFE.EXE 0x4756f4-0x4761a0, the end of the
 * game sound API, docs/imuse.md): four on / off toggles and four volume
 * sliders over the iMUSE groups (0 master, 3 + 4 music, 2 voice, 1 sound
 * effects), on bitmap 4000 with the close box 0x259.
 *
 *   Master   off: group 0 to 0 (remembered); on: a pause / resume of
 *            iMUSE (restarting the output) and the volume back.
 *   Music    off: groups 3 and 4 to 0 and the music off (music_off: the
 *            track stops, snd_music_enabled 0); on: back to the
 *            remembered volume and music_on picks a new track.
 *   Voice    group 2 to 0 and back.
 *   Sound FX group 1 to 0 and back, snd_sfx_enabled (snd_play skips
 *            effects while it's off).
 * A slider only moves while its toggle is on; it sets its group(s) (0..127)
 * as it moves.
 *
 * game_main creates it once (hidden, centred) and sets every group to 127,
 * so the sliders start at the top; nothing is saved (no INI key, nothing
 * in the saved games), as in the original. The Global menu's "&Audio
 * Settings\tF9" (MENU_AUDIO, shell_hooks.audio_dialog) shows and hides it;
 * its close box hides it and clears the menu's check mark.
 */
#ifndef AFTERLIFE_UI_AUDIODLG_H
#define AFTERLIFE_UI_AUDIODLG_H

#include <stdbool.h>
#include <stdint.h>

#include "ui/wm.h"

enum {
    AUDIODLG_CLOSE = 0x66,
    AUDIODLG_MASTER_VOL = 0x67, /* the sliders */
    AUDIODLG_MUSIC_VOL = 0x68,
    AUDIODLG_VOICE_VOL = 0x69,
    AUDIODLG_SFX_VOL = 0x6a,
    AUDIODLG_MASTER = 0x6d, /* the toggles */
    AUDIODLG_MUSIC = 0x6e,
    AUDIODLG_VOICE = 0x6f,
    AUDIODLG_SFX = 0x70,
};

/* 0x4756f4 snd_audio_dialog_create: sets the four groups to 127 and
 * creates the window under `main` (hidden, centred). True if it exists. */
bool audiodlg_create(WmWindow *main);
/* Port-only: destroys it. */
void audiodlg_destroy(void);
/* 0x638ea0 */
WmWindow *audiodlg_window(void);
/* game_view_proc's MENU_AUDIO (0x405430): raise and show / hide (the
 * caller sets the menu's check mark). */
void audiodlg_show(bool on);
/* 0x475bd9 snd_audio_dialog_proc */
intptr_t audiodlg_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b);

#endif
