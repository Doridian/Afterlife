#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Differential tests of the Karma network and the map objects (WP3:
src/game/karma.c, mapobj.c) against ALIFE.EXE under the emulator.

For each save (default: orig/ASCEN00[1-4].ASC) both sides load it, the
original gets the tile entries and a surface-less main window a running
game has (tools/emu/mapobj_env.py), both RNGs the same state, and then:

  rebuild   karma_clear 0x434c38 + karma_rebuild 0x433106 (plane 2)
  check     karma_rail_check 0x42fddf, 20 times (one dangling rail each)
  remove    0x42f129(wnd, view, x, y, remove) on rail cells, counting
            (remove 0) and removing (1); return values compared
  trains    karma_trains_tick 0x434dc4 every "500 ms" with the map object
            block of game_frame_update (0x4ee088 + map_objects_step
            0x4eedc1 for planes 1, 2, 3) four times in between, for
            --ticks ticks plus a run-out; the trains' cars think through
            map_object_think 0x437184 (0x43404a)
  boats     karma_year 0x435f3d per realm, many times, with the objects
            moving (0x435509 boats)
  pending   karma_pending_scan 0x434d2d
  marks     map_object_mark_cells 0x4ee5d8 on random areas, adding and
            removing random slots

After each step every saved global (map objects and the 0x64e500 cells
included), the grids (cells, object maps, sim cells), the current-plane
globals and the RNG must be identical. Display calls (redraws through a
window without a surface, sounds, the Microview) do nothing on either side.

    tools/emu/test_karma.py [--only NAME[,NAME]] [--ticks N] [--every N] [--keep-going] [save ...]
"""
import argparse
import ctypes
import os
import random
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from difftest import Harness, SAVES, orig_path  # noqa: E402
import bridge  # noqa: E402
import mapobj_env  # noqa: E402

SELECT_PLANE = 0x42b102
MAP_VIEW = bridge.MAP_VIEW
MAIN_WINDOW = 0x5f7460
UI_STUBS = {0x4751af: 'snd_play', 0x46f414: 'microview_refresh', 0x4c59a2: 'wm_is_visible',
            0x4c2830: 'wm_send', 0x46313b: 'macro_refresh'}
# karma.c's runtime globals (the scan cursor 0x65eda8 / 0x65edac, the
# whistle timer 0x65ede4) are compared through their effects only.


class Mismatch(Exception):
    pass


class Pair:
    def __init__(self):
        self.h = h = Harness(run_rebuilds=True)
        self.L = L = h.port_.lib
        c = ctypes
        L.world_select_plane.argtypes = [c.c_int]
        L.karma_rail_remove.argtypes = [c.c_int, c.c_int, c.c_bool]
        L.mapobj_mark_cells.argtypes = [c.c_int] * 5
        L.mapobj_frame.argtypes = [c.c_int]
        L.mapobj_animate_cells.argtypes = [c.c_int, c.c_int]
        L.mapobj_step.argtypes = [c.c_int, c.c_int]
        L.shim_sim_setup.argtypes = [c.c_uint32, c.c_uint32]
        for a in UI_STUBS:
            h.emu.stub(a)
        self.time = 1000000
        h.emu.import_stub('timeGetTime', 0, lambda e, a: self.time)
        mapobj_env.install(h)

    def prepare(self, save, seed=4321):
        h = self.h
        h.load(save)
        for a in UI_STUBS:
            h.emu.stub(a)
        self.L.shim_sim_setup(self.time, 0)
        self.L.shim_karma_setup()
        bridge.write_port_raw(h.emu, h.port_)
        h.seed(seed)
        self.check('after load')

    def select(self, plane):
        self.h.orig(SELECT_PLANE, MAP_VIEW, plane, track=False)
        self.L.world_select_plane(plane)

    def check(self, what):
        try:
            self.h.check(what=what, ignore=['plane_select_count'])
        except AssertionError as e:
            raise Mismatch(str(e))

    def wnd(self):
        return self.h.emu.u32(MAIN_WINDOW)

    def orig_frame(self, animate=1):
        """game_frame_update's map object block (0x407282)."""
        h, w = self.h, self.wnd()
        h.orig(SELECT_PLANE, MAP_VIEW, 1, track=False)
        h.orig(0x4ee088, w, MAP_VIEW, 1, animate)
        h.orig(0x4eedc1, w, MAP_VIEW, 1, animate)
        h.orig(SELECT_PLANE, MAP_VIEW, 2, track=False)
        if animate:
            h.orig(0x4ee088, w, MAP_VIEW, 2, animate)
            h.orig(0x4eedc1, w, MAP_VIEW, 2, animate)
        h.orig(SELECT_PLANE, MAP_VIEW, 3, track=False)
        h.orig(0x4ee088, w, MAP_VIEW, 3, animate)
        h.orig(0x4eedc1, w, MAP_VIEW, 3, animate)

    def frame(self, animate=1):
        plane = self.h.emu.u8(MAP_VIEW + 0x47)
        self.orig_frame(animate)
        if self.h.emu.u8(MAP_VIEW + 0x47) != plane:
            self.h.orig(SELECT_PLANE, MAP_VIEW, plane, track=False)
        self.L.mapobj_frame(animate)

    def rails(self):
        """Rail cells of plane 2 (from the port)."""
        cells = self.h.port_.bytes('cells', 2)
        out = []
        for i in range(10000):
            c = int.from_bytes(cells[4 * i:4 * i + 4], 'little')
            if c >> 27 == 16:
                out.append((i % 100, i // 100))
        return out


def t_rebuild(s):
    s.select(2)
    s.h.orig(0x434c38)
    s.L.karma_clear()
    s.check('karma_clear')
    s.select(2)
    s.h.orig(0x433106)
    s.L.karma_rebuild()
    s.check('karma_rebuild')


def t_check(s):
    for i in range(20):
        s.select(1 + i % 3)
        s.h.orig(0x42fddf)
        s.L.karma_rail_check()
        s.check('karma_rail_check #%d' % i)


def t_remove(s):
    r = random.Random(7)
    rails = s.rails()
    if not rails:
        return
    for k in range(12):
        x, y = r.choice(rails)
        remove = k % 3 == 2
        s.select(r.choice((1, 2, 3)))
        o = s.h.orig(0x42f129, s.wnd(), MAP_VIEW, x, y, int(remove))
        p = s.L.karma_rail_remove(x, y, remove)
        if o != p:
            raise Mismatch('0x42f129(%d, %d, %d): orig %d port %d' % (x, y, remove, o, p))
        s.check('karma_rail_remove(%d, %d, %d)' % (x, y, remove))
        rails = s.rails() or rails


EVERY = [5]   # full state comparisons every N ticks (--every)


def trains(s, ticks, runout=300):
    """karma_trains_tick every 500 ms, four object steps in between."""
    e = s.h.emu
    cars = 0
    for t in range(ticks + runout):
        if t < ticks:
            s.select(2)
            s.h.orig(0x434dc4)
            s.L.karma_trains_tick()
            if t % EVERY[0] == 0:
                s.check('karma_trains_tick #%d' % t)
            s.time += 500
            s.L.shim_sim_clock(s.time, 0)
        for k in range(4):
            s.frame(1)
        n = sum(1 for i in range(1, 63) if e.u8(0x64e5f0 + i * 0x17) == 2 and e.u8(0x64e5f6 + i * 0x17) == 16)
        cars = max(cars, n)
        last = t >= ticks and n == 0
        if t % EVERY[0] == EVERY[0] - 1 or last:
            s.check('objects after tick %d' % t)
        if last:
            break
    s.check('trains done')
    return cars


def t_trains(s, ticks=150):
    s.select(2)
    s.h.orig(0x433106)
    s.L.karma_rebuild()
    cars = trains(s, ticks)
    print('    (up to %d cars at once)' % cars, end='')


def t_boats(s):
    for k in range(60):
        for plane in (3, 1):
            s.select(plane)
            s.h.orig(0x435f3d, MAP_VIEW)
            s.L.karma_year()
        s.check('karma_year #%d' % k)
        for i in range(6):
            s.frame(1)
        if k % EVERY[0] == 0:
            s.check('boats after year %d' % k)
    boats = s.h.emu.u8(0x63050e)
    print('    (%d boats sailing)' % boats, end='')
    for i in range(200):
        s.frame(1)
    s.check('boats run out')


def t_pending(s):
    for v in (0, 3):
        s.h.emu.w8(0x6301f4, v)
        s.L.shim_karma_set_pending(v)
        s.select(3)
        s.h.orig(0x434d2d)
        s.L.karma_pending_scan()
        s.check('karma_pending_scan(pending %d)' % v)


def t_marks(s):
    r = random.Random(11)
    e = s.h.emu
    # give some slots a plane and priorities so the priority rule matters
    for slot in range(1, 9):
        pr = r.randrange(4)
        e.w8(0x64e5f0 + slot * 0x17 + 0x12, pr)
        s.L.shim_karma_set_priority(slot, pr)
    s.check('priorities')
    marked = []
    for k in range(300):
        plane = r.choice((1, 2, 3))
        s.select(plane)
        if marked and r.random() < 0.45:
            x, y, size, slot, pl = marked.pop(r.randrange(len(marked)))
            s.select(pl)
            add = 0
        else:
            x, y, size, slot = r.randrange(-2, 101), r.randrange(-2, 101), r.choice((1, 1, 2, 3, 4)), r.randrange(1, 9)
            add = 1
            marked.append((x, y, size, slot, plane))
        o = s.h.orig(0x4ee5d8, MAP_VIEW, x, y, size, slot, add) & 0xff
        p = s.L.mapobj_mark_cells(x, y, size, slot, add) & 0xff
        if o != p:
            raise Mismatch('mark_cells(%d, %d, %d, %d, %d): orig %d port %d' % (x, y, size, slot, add, o, p))
        if k % 20 == 19:
            s.check('mark_cells #%d' % k)
    s.check('mark_cells')


TESTS = [('rebuild', t_rebuild), ('check', t_check), ('remove', t_remove), ('trains', t_trains),
         ('boats', t_boats), ('pending', t_pending), ('marks', t_marks)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('saves', nargs='*')
    ap.add_argument('--only', default='')
    ap.add_argument('--ticks', type=int, default=150)
    ap.add_argument('--every', type=int, default=5, help='compare every N train ticks / boat years')
    ap.add_argument('--keep-going', action='store_true')
    args = ap.parse_args()
    EVERY[0] = max(1, args.every)
    saves = args.saves or [orig_path(n) for n in SAVES]
    only = set(filter(None, args.only.split(',')))
    s = Pair()
    s.L.shim_karma_set_priority.argtypes = [ctypes.c_int, ctypes.c_int]
    s.L.shim_karma_set_pending.argtypes = [ctypes.c_int]
    fails = 0
    for save in saves:
        print(os.path.basename(save))
        for name, fn in TESTS:
            if only and name not in only:
                continue
            try:
                s.prepare(save)
                if name == 'trains':
                    fn(s, args.ticks)
                else:
                    fn(s)
                print('  %-10s ok' % name)
            except (Mismatch, AssertionError) as e:
                fails += 1
                print('  %-10s MISMATCH\n    %s' % (name, str(e).replace('\n', '\n    ')))
                if not args.keep_going:
                    break
            except Exception:
                fails += 1
                print('  %-10s ERROR' % name)
                traceback.print_exc()
                if not args.keep_going:
                    break
    print('%d failures' % fails if fails else 'all ok')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
