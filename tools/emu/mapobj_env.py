# SPDX-License-Identifier: AGPL-3.0-or-later
"""What the original's map object code (WP3: 0x4ee5d8 map_object_mark_cells,
0x4ed550 map_object_redraw, 0x4eedc1 map_objects_step) needs from a
running game that a fresh emulator lacks:

  - tile_set_entries (0x57d478): mark_cells reads each structure's footprint
    from its tile entry (+0x1a), redraw the sprite heights (+0x14, +0x16).
    Built from the TILE resources of the user's MEDIUM.000 (through the
    port's reader), as entries holding just those fields;
  - a main window (0x5f7460) without a surface (+0x10 = NULL), so the
    original's map_object_redraw runs and draws nothing, like the port
    with no redraw hook;
  - wm_update (0x4c2733), which map_objects_step calls at the end: stubbed;
  - the train kinds (0x5fa620, 3 x 15 bytes), which game_init_defaults
    (0x45546c) writes: cars, first sprite and wrap per car.

    import mapobj_env
    mapobj_env.install(h)      # h: difftest.Harness; re-snapshots h.fresh
"""
import ctypes
import struct

from alife_emu import ORIG

TILE_SET_ENTRIES = 0x57d478
MAIN_WINDOW = 0x5f7460
WM_UPDATE = 0x4c2733
TRAIN_KINDS = 0x5fa620
# what game_init_defaults writes there (k_trains in src/game/karma.c)
TRAINS = [(3, [0x0d, 0x0d, 0x0d], [6, 6, 6]), (1, [0x13], [0x0c]), (4, [0x1f, 0x2b, 0x2b, 0x1f], [0x0c, 6, 6, 0x0c])]
# the port's side: g_karma_hooks / g_mapobj_hooks cleared, runtime reset
_tables = {}


def _entries(port):
    L = port.lib
    if 'sets' in _tables:
        return _tables['sets']
    L.shim_karma_tiles.argtypes = [ctypes.c_char_p]
    L.shim_karma_tile_entry.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_int32)]
    if L.shim_karma_tiles(ORIG.encode()) <= 0:
        raise SystemExit('mapobj_env: no TILE sets in %s/MEDIUM.000' % ORIG)
    out = (ctypes.c_int32 * 3)()
    sets = {}
    for s in range(32):
        n = L.shim_karma_tile_entry(s, -1, out)
        if n <= 0:
            continue
        ents = []
        for i in range(n):
            if L.shim_karma_tile_entry(s, i, out) < 0:
                ents.append(None)
            else:
                ents.append((out[0], out[1], out[2]))
        sets[s] = ents
    _tables['sets'] = sets
    return sets


def window(h):
    """The surface-less main window (for the original's window arguments)."""
    return h.emu.u32(MAIN_WINDOW)


def install(h):
    """Set up the emulator of Harness h (and the port's WP3 runtime)."""
    e = h.emu
    h.port_.lib.shim_karma_setup()
    for s, ents in _entries(h.port_).items():
        table = e.malloc(4 * len(ents))
        for i, ent in enumerate(ents):
            if ent is None:
                continue
            p = e.malloc(0x1c)
            e.write(p, bytes(0x1c))
            e.write(p + 0x14, struct.pack('<HH', ent[0], ent[1]))
            e.w8(p + 0x1a, ent[2])
            e.w32(table + 4 * i, p)
        e.w32(TILE_SET_ENTRIES + 4 * s, table)
    wnd = e.malloc(0x100)
    e.write(wnd, bytes(0x100))
    e.w32(MAIN_WINDOW, wnd)
    e.stub(WM_UPDATE)
    for k, (cars, frames, wraps) in enumerate(TRAINS):
        rec = bytearray(15)
        rec[0] = cars
        rec[1:1 + len(frames)] = bytes(frames)
        rec[8:8 + len(wraps)] = bytes(wraps)
        e.write(TRAIN_KINDS + 15 * k, bytes(rec))
    h.fresh = e.snapshot()
