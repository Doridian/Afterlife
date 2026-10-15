/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The save / load / delete dialogs of the File menu (ALIFE.EXE
 * 0x47fdc6-0x480ea3, 0x480ea4-0x481ba3, 0x489e90-0x48a839,
 * 0x494112-0x4952ff; WP8): a window (bitmap 0x25d) with the list of the
 * 100 save slots ("agame000.alf".."agame099.alf" in the save folder, or
 * "ascen%03u.asc" for scenarios, which also come from the CD), the title,
 * the slot's thumbnail, its year and difficulty, and the buttons.
 *
 *   slotdlg_save   0x480ea4 (proc 0x48137d): a list with an empty line at
 *                  the end for a new slot, a text field for the title, the
 *                  current game's picture shrunk from the main view
 *                  (0x47fdc6); "Overwrite existing save game?".
 *   slotdlg_load   0x489e90 (proc 0x48a3dc): loads the chosen slot.
 *   slotdlg_choose 0x494112 (same proc): the title screen's version: only
 *                  picks the slot (the game loads it once it is set up),
 *                  thumbnails remapped to the title's palette (resource
 *                  0x14b6), no help balloons.
 *   slotdlg_delete 0x4946bd (proc 0x494a93): "Delete game?", the dialog
 *                  stays open.
 *
 * Each runs a modal loop (sys_pump) and returns the original's result:
 * nonzero if something was saved / loaded / deleted / chosen, 0 if
 * cancelled or failed. Saving and loading go through the shell
 * (shell_save_game_to / shell_load_game_from).
 */
#ifndef AFTERLIFE_UI_SLOTDLG_H
#define AFTERLIFE_UI_SLOTDLG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx/surface.h"
#include "ui/wm.h"

#define SLOTDLG_SLOTS 100

/* control ids */
enum {
    SLOTDLG_ID_LOAD = 1000,
    SLOTDLG_ID_SAVE = 0x3ea,
    SLOTDLG_ID_CANCEL = 0x3eb, /* also the close button */
    SLOTDLG_ID_DELETE = 0x3ec,
    SLOTDLG_ID_TITLE = 10000, /* the save dialog's text field */
    SLOTDLG_ID_LIST = 0x2711,
};

/* mode: 1 saved games, 2 scenarios */
int slotdlg_save(int mode);
int slotdlg_load(int mode);
/* Returns true with the chosen slot in *slot. */
bool slotdlg_choose(int mode, int *slot);
int slotdlg_delete(void);

/* The open dialog and its list / text field (0x66ae74 / 0x66ae78 /
 * 0x66ae7c), NULL when none is open. */
WmWindow *slotdlg_window(void);
WmWindow *slotdlg_list(void);
WmWindow *slotdlg_text(void);

/* The file of slot `slot` (mode 1 / 2) that exists: the save folder,
 * then (scenarios) the CD, i.e. the data dir. False if there is none;
 * `out` then holds the save folder's path. */
bool slotdlg_slot_file(int mode, unsigned slot, char *out, size_t n);

/* The header fields the dialogs read from a save (0x480a10 / 0x47fff4). */
typedef struct SlotInfo {
    int version;
    uint32_t timestamp, year;
    uint16_t difficulty; /* 1 when the version has none */
    char title[256];
    uint32_t thumb_w, thumb_h; /* 0 if none */
} SlotInfo;
bool slotdlg_read_info(const char *path, SlotInfo *info);

/* 0x47fdc6, pure: shrinks src's rectangle `from` into dst's rectangle
 * `to`, each pixel the majority of its sample and the samples left and
 * right of it (the sample if all three differ). */
void slotdlg_shrink(GfxSurface *dst, const GfxRect *to, const GfxSurface *src, const GfxRect *from);

#endif
