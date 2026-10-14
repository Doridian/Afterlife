#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Differential test of the SOUL flow (WP1b, src/game/souls.c, zones.c)
against ALIFE.EXE under the emulator.

For each save (default: orig/ASCEN00[1-4].ASC) both sides load it, the
port fills the tables a running game has besides the save (building specs,
tile type tables, 0x452267) and its whole state is pushed into the
emulated original, so both start identical. Then both run the SOUL phases
of sim_update for some years, in its order and with its plane selects:

    phase 30: 0x46114f / 0x460b16 (Gates, siphons), Heaven then Hell
    phase 100 / 90: sim_zone_scan 0x4607d6 until done, Heaven / Hell
    phase 40 / 50: 0x45e44b (flow) until done, Heaven / Hell

After every call: return values, RNG state, all saved state plus the raw
grids and tile tables (difftest.Harness.check) and the module's runtime
counters must be identical. The original's timeGetTime is frozen so its
flow never yields; the port runs with no budget. Map object marks
(0x4ee5d8 / 0x4ed550) are stubbed on both sides (WP3).

    tools/emu/test_souls.py [--years N] [save ...]
"""
import argparse
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bridge  # noqa: E402
from difftest import Harness, orig_path, SAVES  # noqa: E402

MAIN_WINDOW = 0x5f7460
TABLES = [  # (address, records) in shim_souls_tables order
    (0x6304a0, 8), (0x57e110, 6), (0x57e0b0, 6), (0x630200, 8), (0x5f7410, 6), (0x59cec0, 6), (0x630280, 4),
    (0x57e030, 3),
]
SPECIAL_POS, OBJ_5F7408 = 0x5bb0f0, 0x5f7408
RUNTIME = [('souls_to_heaven', 0x65fe54, 4), ('souls_to_hell', 0x65fe58, 4), ('souls_groups_created', 0x630510, 4),
           ('souls_gate_arrived', 0x57e01c, 4), ('next_free_group', 0x65fe50, 4), ('scan_x', 0x65fe48, 1),
           ('scan_y', 0x65fe4c, 1), ('flow_x', 0x65fe40, 1), ('flow_y', 0x65fe44, 1)]
# map objects (WP3) and the Soulview's drawing (WP6b): not simulation
STUBS = {0x4ee5d8: 'map_object_mark_cells', 0x4ed550: 'map_object_redraw', 0x47f259: 'soulview draw',
         0x47e875: 'soulview walker', 0x4c63f0: 'sys_pump', 0x407282: 'game_frame_update',
         # UI reached through a siphon burst (0x42c09b destroy)
         0x4751af: 'snd_play', 0x475428: 'snd_preload', 0x4c2830: 'wm_send', 0x4bfc69: 'ctl_message_box',
         0x408ca4: 'ui_busy_cursor', 0x408c4d: 'views_refresh_all'}

SELECT_PLANE = 0x42b102
ZONE_SCAN, FLOW, GATES, SIPHONS = 0x4607d6, 0x45e44b, 0x46114f, 0x460b16


_tick = [0]


def frozen_time(emu, args):
    """timeGetTime: frozen at 0 (the flow never yields), except for the
    Soulview walker animation 0x47e3be, whose 100 ms busy-wait sees the
    time jump by 1000 per call so it ends at once."""
    from unicorn.x86_const import UC_X86_REG_ESP
    ra = emu.u32(emu.mu.reg_read(UC_X86_REG_ESP))
    if 0x47e3be <= ra < 0x47e4be:
        _tick[0] += 1000
        return _tick[0]
    return 0


class Pair:
    def __init__(self):
        self.h = h = Harness()
        L = h.port_.lib
        L.shim_souls_tables.restype = ctypes.c_size_t
        L.shim_souls_tables.argtypes = [ctypes.c_void_p]
        L.shim_souls_runtime.argtypes = [ctypes.c_void_p]
        L.world_select_plane.argtypes = [ctypes.c_int]
        L.shim_souls_flow.argtypes = [ctypes.c_uint]
        self.L = L
        for a in STUBS:
            h.emu.stub(a)
        h.emu.import_stub('timeGetTime', 0, frozen_time)

    def load(self, path, synthetic=False):
        h = self.h
        h.load(path)
        self.L.shim_souls_setup()
        if synthetic:
            n = self.L.shim_souls_synthetic(3, 4, 6)
            print('  synthetic: %d Omnibolges/siphons added, trackers on' % n)
        # the port's state (with the rebuilt tables) into the original
        h.emu.restore(h.fresh)
        for a in STUBS:
            h.emu.stub(a)
        h.emu.import_stub('timeGetTime', 0, frozen_time)
        bridge.orig_load(h.emu, h.port_.save_mem(h.version))
        bridge.write_port_raw(h.emu, h.port_)
        buf = ctypes.create_string_buffer(4096)
        n = self.L.shim_souls_tables(buf)
        raw = buf.raw[:n]
        off = 0
        for addr, recs in TABLES:
            h.emu.write(addr, raw[off:off + 13 * recs])
            off += 13 * recs
        h.emu.write(SPECIAL_POS, raw[off:off + 20])
        h.emu.w8(OBJ_5F7408, raw[off + 20])
        # the original's runtime counters at process start
        for _, addr, _ in RUNTIME:
            h.emu.w32(addr, 0)
        h.emu.w32(0x65fe50, 1)
        h.emu.rng_seed(h.port_.rng_state())
        self.check('load')

    def select(self, plane):
        self.h.orig(SELECT_PLANE, bridge.MAP_VIEW, plane)
        self.L.world_select_plane(plane)

    def check(self, what):
        h = self.h
        e, p = h.emu.rng_state(), h.port_.rng_state()
        if e != p:
            raise AssertionError('%s: RNG state original %08x, port %08x' % (what, e, p))
        out = (ctypes.c_uint32 * len(RUNTIME))()
        self.L.shim_souls_runtime(out)
        for (name, addr, size), v in zip(RUNTIME, out):
            o = h.emu.u32(addr) if size == 4 else h.emu.u8(addr)
            if o != v:
                raise AssertionError('%s: %s original %d, port %d' % (what, name, o, v))
        h.check(what=what)

    def call(self, what, addr, args, port_fn, port_args=()):
        r1 = self.h.orig(addr, *args)
        r2 = getattr(self.L, port_fn)(*port_args)
        if r1 is not None and port_fn.startswith('shim_') and (r1 & 0xff != 0) != (r2 != 0):
            raise AssertionError('%s: original returned %d, port %d' % (what, r1, r2))
        self.check(what)
        return r2

    def year(self, y):
        for plane, name in ((3, 'Heaven'), (1, 'Hell')):
            self.select(plane)
            self.call('y%d %s gates' % (y, name), GATES, (), 'souls_gate_arrivals')
            self.call('y%d %s siphons' % (y, name), SIPHONS, (), 'souls_siphon_release')
        for plane, name in ((3, 'Heaven'), (1, 'Hell')):
            self.select(plane)
            k = 0
            while True:
                k += 1
                if self.call('y%d %s zone scan #%d' % (y, name, k), ZONE_SCAN, (), 'shim_souls_zone_scan'):
                    break
        for plane, name in ((3, 'Heaven'), (1, 'Hell')):
            self.select(plane)
            k = 0
            while True:
                k += 1
                if self.call('y%d %s flow #%d' % (y, name, k), FLOW, (MAIN_WINDOW, bridge.MAP_VIEW),
                             'shim_souls_flow', (0,)):
                    break
        print('  year %d: identical' % y)

    def evict(self, n):
        """0x45fd84 on n zone buildings of each realm (a third of their SOULs),
        and 0x45ebe2 on n spots with varying size and radius."""
        h = self.h
        L = self.L
        L.souls_zone_evict.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint32]
        L.realm_building_pop.restype = ctypes.c_uint32
        for plane, name in ((3, 'Heaven'), (1, 'Hell')):
            self.select(plane)
            k = 0
            for i in range(10000):
                x, y = i // 100, i % 100
                if (L.shim_cell_tileset(x, y) not in (14, 15)) or (i * 7919) % 5:
                    continue
                pop = L.realm_building_pop(x, y)
                if pop == 0:
                    continue
                h.orig(0x45fd84, x, y, pop // 3)
                L.souls_zone_evict(x, y, pop // 3)
                self.check('%s evict (%d,%d) %d' % (name, x, y, pop // 3))
                k += 1
                if k >= n:
                    break
            for i in range(n):
                x, y, size, rad = (i * 37) % 100, (i * 53) % 100, 1 + i % 4, 1 + (i * 3) % 6
                r1 = h.orig(0x45ebe2, x, y, size, rad, 0) & 0xff
                r1y = h.emu.mu.reg_read(__import__('unicorn').x86_const.UC_X86_REG_EDX) & 0xff
                r2 = L.shim_souls_area_search(x, y, size, rad)
                if (r1 | r1y << 8) != (r2 & 0xffff) and not (r1 == 0xff and r2 & 0xff == 0xff):
                    raise AssertionError('%s area_search(%d,%d,%d,%d): original %d,%d, port %d,%d'
                                         % (name, x, y, size, rad, r1, r1y, r2 & 0xff, r2 >> 8 & 0xff))
                self.check('%s area_search #%d' % (name, i))
            print('  %s: %d evictions, %d area searches identical' % (name, k, n))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--years', type=int, default=2)
    ap.add_argument('--synthetic', action='store_true',
                    help='add Omnibolges, siphons and Soulview trackers to each save first')
    ap.add_argument('--evict', type=int, default=0, metavar='N',
                    help='also compare 0x45fd84 (evict) on N zone buildings and area_search on N spots')
    ap.add_argument('saves', nargs='*')
    a = ap.parse_args()
    saves = a.saves or [orig_path(s) for s in SAVES]
    pair = Pair()
    failed = 0
    for s in saves:
        print(s)
        try:
            pair.load(s, a.synthetic)
            for y in range(a.years):
                pair.year(y + 1)
            if a.evict:
                pair.evict(a.evict)
        except AssertionError as ex:
            print('  FAIL:', ex)
            failed += 1
    lines, total = pair.h.uncompared_writes()
    if total:
        print('globals the original wrote that are not compared (%d ranges):' % total)
        for ln in lines:
            print('  ' + ln)
    print('all OK' if not failed else '%d save(s) differ' % failed)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
