/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * tools/emu wrappers for WP3 (game/karma.h, game/mapobj.h): runtime state
 * and the tile entries the original's map object code reads.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "formats/tile.h"
#include "game/karma.h"
#include "game/mapobj.h"
#include "game/resource.h"
#include "game/world.h"
#include "platform/platform.h"

#if defined(_WIN32)
#define SHIM_API __declspec(dllexport)
#else
#define SHIM_API __attribute__((visibility("default")))
#endif

/* Fresh WP3 runtime state, no hooks. */
SHIM_API void shim_karma_setup(void)
{
    karma_reset();
    mapobj_reset();
    memset(&g_karma_hooks, 0, sizeof g_karma_hooks);
    memset(&g_mapobj_hooks, 0, sizeof g_mapobj_hooks);
}

static TileSet s_sets[32];
static uint8_t *s_data[32];

/* Opens the TILE resources 1..31 of <data_dir>/MEDIUM.000 (for building
 * the original's tile_set_entries in the emulator). Returns the sets
 * found. */
SHIM_API int shim_karma_tiles(const char *data_dir)
{
    plat_set_data_dir(data_dir);
    if (!rc_initialize("MEDIUM.000", "MEDIUM.000"))
        return 0;
    int n = 0;
    for (int id = 1; id <= 31; id++) {
        if (s_data[id - 1])
            continue;
        long size = rc_resource_size((uint32_t)id);
        PlatFile *f = rc_open_resource((uint32_t)id);
        if (!f || size <= 0) {
            if (f)
                plat_file_close(f);
            continue;
        }
        uint8_t *buf = malloc((size_t)size);
        if (buf && plat_file_read(f, buf, 1, (size_t)size) == (size_t)size &&
            tile_set_open(&s_sets[id - 1], buf, (size_t)size) == TILE_OK) {
            s_data[id - 1] = buf;
            n++;
        } else {
            free(buf);
        }
        plat_file_close(f);
    }
    return n;
}

/* Entry index0 of tile set `set` (slot): out = {height, anchor, size};
 * returns the set's entry count (0 = no such set), or -1 if index0 has
 * no entry. */
SHIM_API int shim_karma_tile_entry(int set, int index0, int32_t out[3])
{
    if (set < 0 || set >= 32 || !s_data[set])
        return 0;
    if (index0 < 0)
        return s_sets[set].count;
    TileEntry e;
    if (tile_get_entry(&s_sets[set], index0, &e) != TILE_OK)
        return -1;
    out[0] = e.height, out[1] = e.anchor, out[2] = e.size;
    return s_sets[set].count;
}


SHIM_API void shim_karma_set_pending(int v) { g_state.karma_pending = (uint8_t)v; }
SHIM_API void shim_karma_set_priority(int slot, int v)
{
    if (slot >= 0 && slot < GS_MAP_OBJECTS)
        g_state.objects[slot].priority = (uint8_t)v;
}
