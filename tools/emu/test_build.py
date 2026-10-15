#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Differential test of the build tools, structures, rotation and world
setup (WP2: src/game/build.c, structure.c, rotate.c, newworld.c) against
ALIFE.EXE under the emulator.

For each save given (default: orig/ASCEN00[1-4].ASC) both sides load it
(the original's loader runs its post-load rebuilds 0x4506b6 / 0x452267,
the port's world_apply_loaded runs build_after_load), then both run the
same operations with the same RNG state, and after each one every saved
global, the grids, the tile type tables and the RNG must be identical:

  - map_rotate_cells (0x4514a4), four quarter turns each way;
  - placing zones, set-8 buildings and ports (0x44b4fb), zoning a rectangle
    (0x44d34f), road lines (0x44fc9b), destroying them again (0x42c09b);
  - --gen K: game_reset_world (0x4535db) for K seeds per difficulty.

UI calls (sounds, dialogs, the Soulview trackers) are stubbed in the
original; the port's hooks are NULL. The map object code (marks, redraws
through a window without a surface, Karma rail removal) runs on both sides
(tools/emu/mapobj_env.py). The other packages' functions the
build code calls (area search, SOUL eviction, vibes, planet generation,
graph history) run for real on both sides.

    tools/emu/test_build.py [--gen K] [save ...]
"""
import argparse
import ctypes
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from difftest import Harness, orig_path, SAVES  # noqa: E402
from alife_emu import ORIG  # noqa: E402
import mapobj_env  # noqa: E402

MAP_VIEW = 0x5bb120
SELECT_PLANE = 0x42b102
VIEW_FLAGS = {1: 0x65ed98, 2: 0x65eda0, 3: 0x65ed9c}
DIFFICULTY = 0x5c5c14
VIEW_DIR = (0x5fa5f8, 0x5bb118)     # view_dir_x / view_dir_y

UI_STUBS = {
    0x4751af: 'snd_play', 0x475428: 'snd_preload', 0x4754a1: 'snd_unload', 0x475569: 'snd_stop (music)',
    0x46f414: 'microview_refresh',
    0x4c59a2: 'wm_is_visible', 0x46313b: 'macro_refresh', 0x4bfc69: 'ctl_message_box', 0x403bfd: 'after message box',
    0x4c2733: 'wm_update', 0x4c2830: 'wm_send', 0x4ebfbc: 'map_hilite_area', 0x4ebd8a: 'map_redraw_cells',
    0x47e229: 'Soulview trackers after rotating', 0x47e5bc: 'Soulview trackers reset',
    0x4ba7e9: 'ctl_button_set_pressed', 0x4c8393: 'menu_set_checked', 0x408c4d: 'views_refresh_all',
    # what the planet generator calls (as tools/emu/test_planet.py)
    0x4c63f0: 'sys_pump', 0x407282: 'game_frame_update', 0x4c8355: 'menu_is_checked', 0x408ca4: 'ui_busy_cursor',
    0x49de17: 'disaster window', 0x49be17: 'planetview select', 0x4a6254: 'planetview legend',
    0x4a5270: 'planetview info',
}

RING = [(1, 1), (1, -1), (-1, -1), (-1, 1)]   # flag 1 steps forward, flag 0 back


class Test:
    def __init__(self):
        self.h = Harness(run_rebuilds=True)
        L = self.h.port_.lib
        L.plat_set_data_dir.argtypes = [ctypes.c_char_p]
        L.plat_set_data_dir(ORIG.encode())
        if not L.planet_tables_load():  # the port's planet generator reads ALIFE.EXE
            sys.exit('planet_tables_load failed (no ALIFE.EXE in %s?)' % ORIG)
        e = self.h.emu
        for a in UI_STUBS:
            e.stub(a)
        # A running game has its tile type tables (0x452581 at the first new
        # world) and every plane showing zone detail (0xc0) before it loads.
        for p in (1, 3):
            e.call(SELECT_PLANE, MAP_VIEW, p)
            e.call(0x452581)
        for a in VIEW_FLAGS.values():
            e.w8(a, 0xc0)
        e.w8(MAP_VIEW + 0x48, 0xc0)
        self.h.fresh = e.snapshot()
        mapobj_env.install(self.h)
        self.failures = 0
        self.checks = 0
        self.verbose = False

    def check(self, what, past_nul=False):
        h = self.h
        self.checks += 1
        d = [x for x in h.diff() if not x.name.startswith('plane_select_count')]
        if past_nul:
            # game_reset_world sprintf's "x" over the scenario texts; the bytes
            # after the NUL can't go through the port's ALSV image
            d = [x for x in d if not re.match(r'scenario_\w+\[([2-9]|\d\d+)\]$', x.name)]
        r1, r2 = h.emu.rng_state(), h.port_.rng_state()
        if d or r1 != r2:
            self.failures += 1
            print('  FAIL %s: %d differences, rng %08x / %08x' % (what, len(d), r1, r2))
            for x in d[:12]:
                print('    ' + str(x))
            return False
        if self.verbose:
            print('  ok %s' % what)
        return True

    def both_plane(self, p):
        self.h.emu.call(SELECT_PLANE, MAP_VIEW, p)
        self.h.port('world_select_plane', p)

    # ---- rotation -------------------------------------------------------------
    def rotate(self, flag):
        e = self.h.emu
        cur = (e.read(VIEW_DIR[0], 1)[0], e.read(VIEW_DIR[1], 1)[0])
        cur = tuple(v - 256 if v > 127 else v for v in cur)
        i = RING.index(cur)
        nd = RING[(i + (1 if flag else 3)) % 4]
        e.write(MAP_VIEW + 0x39, bytes([nd[0] & 0xff, nd[1] & 0xff]))
        self.h.orig(0x4514a4, MAP_VIEW, flag)
        self.h.port('map_rotate_cells', nd[0], nd[1], flag)
        return nd

    def test_rotation(self, name):
        ok = True
        for flag in (1, 0):
            for k in range(4):
                nd = self.rotate(flag)
                ok &= self.check('%s: rotation %d (flag %d) to %s' % (name, k + 1, flag, nd))
        return ok

    # ---- building ---------------------------------------------------------------
    def cell(self, x, y):
        return self.h.port('shim_cell_get', x, y) & 0xffffffff

    def free_spot(self, n, skip=0):
        for y in range(12, 88):
            for x in range(12, 88):
                if all(self.cell(cx, cy) >> 27 == 1 and not self.cell(cx, cy) & 0x20
                       for cx in range(x, x + n) for cy in range(y - n + 1, y + 1)):
                    if skip == 0:
                        return x, y
                    skip -= 1
        return None

    def spot_for(self, set_, size):
        if set_ == 9:  # land and water, not at the border
            for y in range(size, 98):
                for x in range(1, 99 - size):
                    sets = [self.cell(cx, cy) >> 27 for cx in range(x, x + size) for cy in range(y - size + 1, y + 1)]
                    if all(v in (1, 2) for v in sets) and 1 in sets and 2 in sets:
                        return x, y
            return None
        if set_ == 29:  # next to a rock
            for y in range(12, 88):
                for x in range(12, 88):
                    if self.cell(x, y) >> 27 == 1 and any(self.cell(x + dx, y + dy) >> 27 == 10
                                                          for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
                        return x, y
            return None
        if set_ in (17, 18):  # free on plane 2 and in the other realm too
            plane = self.h.port_.world_info()['plane']
            grids = {}
            for p in (plane, 2, 4 - plane):
                self.h.port('world_select_plane', p)
                grids[p] = [[self.cell(x, y) for x in range(100)] for y in range(100)]
            self.h.port('world_select_plane', plane)

            def free(p, x, y):
                return all(grids[p][cy][cx] >> 27 == 1 and not grids[p][cy][cx] & 0x20
                           for cx in range(x, x + 3) for cy in range(y - 2, y + 1))
            for y in range(12, 88):
                for x in range(12, 88):
                    if not (45 <= x <= 55 and 45 <= y <= 55) and free(plane, x, y) and free(2, x, y) \
                            and free(4 - plane, x, y):
                        return x, y
            return None
        return self.free_spot(size, 12)

    def place(self, set_, idx, size, x, y, raw=0):
        r1 = self.h.orig(0x44b4fb, mapobj_env.window(self.h), MAP_VIEW, set_, idx, size, x, y, 0, raw) & 0xff
        r2 = self.h.port('build_place', set_, idx, size, x, y, 2 if raw else 0) & 0xff
        return r1, r2

    def destroy(self, x, y, flags=4):
        self.h.orig(0x42c09b, mapobj_env.window(self.h), MAP_VIEW, x, y, 0, flags)
        self.h.port('structure_destroy', x, y, 0, flags)

    def test_building(self, name, plane):
        h = self.h
        self.both_plane(plane)
        h.emu.w32(0x630438, 0)              # pennies: enough for everything
        h.emu.w32(0x63043c, 2000000000)
        h.port('shim_set_pennies', 2000000000)
        zset = 14 if plane == 1 else 15
        spots = []
        # zones, one cell at a time (each draws the area search's randoms)
        s = self.free_spot(3)
        if s:
            x, y = s
            for k, (cx, cy) in enumerate([(x, y), (x + 1, y), (x + 2, y - 1)]):
                r = self.place(zset, (k % 7) * 13, 1, cx, cy)
                self.check('%s plane %d: zone at %d,%d -> %s' % (name, plane, cx, cy, r))
            spots.append((x, y))
        # a zone rectangle with tool 2
        s = self.free_spot(4, 3)
        if s:
            x, y = s
            h.orig(0x450aba, MAP_VIEW, 2)
            h.port('build_select_tool', 2)
            h.orig(0x44d34f, mapobj_env.window(h), MAP_VIEW, x, y - 3, x + 3, y, 2, 2)
            h.port('build_zone_rect', x, y - 3, x + 3, y, 2)
            self.check('%s plane %d: zone rectangle at %d,%d' % (name, plane, x, y))
            spots.append((x + 1, y - 1))
        # a set-8 building and a port
        for set_, idx, size in ((8, 0 if plane == 3 else 1, 3), (13, 0 if plane == 3 else 2, 3)):
            s = self.free_spot(size, 6)
            if s:
                r = self.place(set_, idx, size, s[0], s[1])
                self.check('%s plane %d: set %d at %d,%d -> %s' % (name, plane, set_, s[0], s[1], r))
                spots.append((s[0] + 1, s[1] - 1))
        # a road with a bend
        s = self.free_spot(6, 10)
        if s:
            x, y = s
            h.orig(0x44fc9b, mapobj_env.window(h), MAP_VIEW, x, y, x + 5, y - 2, 2, 100)
            h.port('build_road_line', x, y, x + 5, y - 2, 1)
            self.check('%s plane %d: road from %d,%d' % (name, plane, x, y))
            spots.append((x + 3, y - 2))
            # an ornament on the road (set 6)
            r = self.place(6, 13, 1, x + 3, y - 2)
            self.check('%s plane %d: set 6 on the road -> %s' % (name, plane, r))
        # the other buildings (Heaven entries; Hell uses the next one where
        # the tool does): (set, Heaven index, Hell index, size)
        heaven = plane == 3
        for set_, ih, il, size in ((7, 0, 1, 4), (19, 0, 1, 1), (30, 0, 0, 2), (5, 0, 1, 5), (4, 0, 5, 4),
                                   (17, 0, 0, 3), (18, 0, 0, 3), (9, 0, 1, 3), (29, 0, 0, 1)):
            idx = ih if heaven else il
            s = self.spot_for(set_, size)
            if not s:
                print('  (no spot for set %d on plane %d)' % (set_, plane))
                continue
            r = self.place(set_, idx, size, s[0], s[1])
            self.check('%s plane %d: set %d/%d at %d,%d -> %s' % (name, plane, set_, idx, s[0], s[1], r))
            if r[0]:
                spots.append((s[0], s[1]))
        # a Karma rail between two points (plane 2), and a rail station
        s = self.free_spot(8, 20)
        if s:
            x, y = s
            r1 = h.orig(0x45011d, mapobj_env.window(h), MAP_VIEW, x, y, x + 7, y - 4, plane)
            r2 = h.port('build_rail_line', x, y, x + 7, y - 4)
            self.check('%s plane %d: rail from %d,%d -> %s' % (name, plane, x, y, (r1, r2)))
            spots.append((x, y))
        # and away with it all (forced for what asks first)
        for (x, y) in spots:
            self.destroy(x, y, 5)
            self.check('%s plane %d: destroy at %d,%d' % (name, plane, x, y))
        # bulldozing water and terrain needs the forced flag
        for (x, y) in ((0, 50), (50, 0)):
            self.destroy(x, y, 4)
            self.check('%s plane %d: refuse to destroy water at %d,%d' % (name, plane, x, y))

    # ---- new worlds ----------------------------------------------------------------
    def test_new_world(self, diff, seed):
        h = self.h
        h.reset()  # both sides back to the loaded save
        h.emu.w16(DIFFICULTY, diff)
        h.port('shim_set_difficulty', diff)
        h.seed(seed)
        h.orig(0x4535db)
        h.port('game_reset_world')
        return self.check('new world, difficulty %d, seed %d' % (diff, seed), past_nul=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--gen', type=int, default=2, help='new worlds per difficulty (default 2)')
    ap.add_argument('-v', action='store_true', help='list every check')
    ap.add_argument('saves', nargs='*')
    a = ap.parse_args()
    saves = a.saves or [orig_path(s) for s in SAVES]
    t = Test()
    t.verbose = a.v
    for path in saves:
        name = os.path.basename(path)
        t.h.load(path)
        print('%s' % name)
        t.check('%s: load' % name)
        t.test_rotation(name)
        t.h.seed(0x1234567)
        for plane in (1, 3):
            t.test_building(name, plane)
        t.test_rotation(name + ' (after building)')
    if a.gen:
        t.h.load(saves[0])
        for diff in (1, 2, 3):
            for k in range(a.gen):
                t.test_new_world(diff, 1 + 7919 * k + diff)
    print('%d checks, %d failed' % (t.checks, t.failures))
    return 1 if t.failures else 0


if __name__ == '__main__':
    sys.exit(main())
