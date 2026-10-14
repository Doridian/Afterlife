/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * ALSV saved games (.ALF) and scenarios (.ASC): docs/formats/alsv.md.
 *
 * The original reads and writes straight into its globals
 * (save_load_game 0x48f837, save_write_game 0x485fb3). Here both go through
 * a GameState (game/state.h), field by field, so the code is endian- and
 * 64-bit-safe. One field list serves both directions.
 */
#ifndef GAME_SAVEGAME_H
#define GAME_SAVEGAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "game/state.h"

/* Parses an ALSV image into *gs (which is overwritten; heap parts of a
 * previous state are not freed, call savegame_free first). Fails on a bad
 * magic, truncation, overlong strings or trailing bytes; `err` (may be NULL)
 * then gets a message. On failure *gs is freed and zeroed. */
bool savegame_load_mem(GameState *gs, const uint8_t *data, size_t len, char *err, size_t err_len);
bool savegame_load(GameState *gs, const char *path, char *err, size_t err_len);

/* Serializes *gs in format version gs->hdr.version (the original always
 * writes ALSV_VERSION_CURRENT; older versions are written as the original
 * would have read them, which round-trips the shipped scenarios). Returns a
 * malloc'd buffer (free it) and its length, or NULL. */
uint8_t *savegame_save_mem(const GameState *gs, size_t *len);
bool savegame_save(const GameState *gs, const char *path);

/* Frees the heap parts (thumbnail, planet map) and zeroes the pointers. */
void savegame_free(GameState *gs);

/* 0x47fc20 packs localtime() into the header timestamp. */
typedef struct {
    int year, month, day, hour, minute, second; /* month 1..12 */
} SaveTimestamp;
SaveTimestamp savegame_decode_timestamp(uint32_t ts);

#endif
