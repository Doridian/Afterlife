#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Differential test of the planet simulation (WP5, src/game/planet.c)
against ALIFE.EXE under the emulator.

For each save given (default: orig/ASCEN00[1-4].ASC) the planet state is
loaded into the port and copied into the emulated original's globals, both
RNGs get the same state, and then both run the same game years: phase 20
(planet_stats_accumulate 0x4aae01), phase 30 (planet_emit_souls 0x4ab77a,
then 0x49c8b8), phase 10 (0x49a2da) and phase 15 (planet_tech_update
0x49b05c), plus the scheduler's bookkeeping between them. After every call
the return values, the RNG state, every planet global and the SOUL groups
handed to 0x445785 (stubbed in the original, a logging hook in the port)
must be identical. --gen also compares world generation (0x496ed1) for a few
seeds, --year forces a disaster each year (0x49c8b8 with cheats) and
--flood runs the Flood (0x49c226).

    tools/emu/test_planet.py [--years N] [--gen K] [save ...]
"""
import argparse
import ctypes
import glob
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from alife_emu import Emu, ORIG  # noqa: E402
from port import Port  # noqa: E402

# name -> (address in ALIFE.EXE, byte size); the port side is shim_planet_raw
FIELDS = {
    'sectors': (0x57e1c0, 64 * 32 * 0x2a),
    'grid': (0x506c40, 0x4000),
    'sin_acc': (0x50acf0, 0xe000),
    'tenet_acc': (0x4f4438, 0x12000),
    'rec_a_xy': (0x4f4030, 64 * 8), 'rec_a_v': (0x5068b8, 64 * 14),
    'rec_b_xy': (0x518d50, 64 * 8), 'rec_b_v': (0x506438, 64 * 18),
    'rec_c_xy': (0x4f4238, 64 * 8), 'rec_c_left': (0x518cf0, 64), 'rec_c_initial': (0x4f3fe8, 64),
    'n_a': (0x66c8d0, 1), 'n_b': (0x66c8d4, 1), 'n_c': (0x66c8d8, 1),
    'disaster': (0x66c8dc, 1), 'disaster_year': (0x66c8e0, 4), 'belief_drift': (0x66c8cc, 2),
    'sector_row': (0x66c8c0, 1), 'tech_row': (0x66c8c4, 1), 'tech_year': (0x66c8e4, 4),
    'tech_best': (0x66c8e8, 1), 'coastal_pop': (0x66c8c8, 4), 'sin_avg': (0x4f4028, 7),
    'sin_sum': (0x4f3f90, 28), 'pop_k': (0x66c89c, 4),
    'planet_cursor': (0x630434, 4), 'planet_deaths_last': (0x630488, 4), 'planet_deaths': (0x638694, 4),
    'planet_aaaa': (0x6301f3, 1), 'embos_hi': (0x630490, 4), 'embos_lo': (0x630494, 4),
    'embos_next_hi': (0x6303c0, 4), 'embos_next_lo': (0x6303c4, 4), 'year': (0x630268, 4),
    'unk_5c6678': (0x5c6678, 2), 'unk_57e058': (0x57e058, 4), 'unk_630420': (0x630420, 4),
    'unk_5bb108': (0x5bb108, 4),
    # runtime (not saved)
    'avg_ralf': (0x57e010, 1), 'avg_not_ocra': (0x5931c0, 1), 'avg_not_opra': (0x626590, 1),
    'acc': (0x66c924, 16), 'max_tech': (0x66c898, 1), 'sin_min': (0x4f3f88, 7),
    'sin_min_xy': (0x4f3fb0, 56), 'sin_min_pop': (0x518d30, 28), 'drift': (0x66c900, 2),
    'announced': (0x66c904, 1), 'cheats': (0x630514, 1),
}
MAP_PTR, MAP_SIZE = 0x66c7e4, 0x66c7e8

# UI and other modules' functions the planet code calls: stubbed in the
# original (the port's hooks are NULL, except souls).
STUBS = {
    0x4c63f0: 'sys_pump', 0x407282: 'game_frame_update', 0x4c8355: 'menu_is_checked',
    0x4bfc69: 'ctl_message_box', 0x4751af: 'snd_play', 0x475428: 'snd_preload', 0x4c2830: 'wm_send',
    0x408ca4: 'ui_busy_cursor', 0x4ba7e9: 'ctl_button_set_pressed', 0x49de17: 'disaster window',
    0x49be17: 'planetview select', 0x4a6254: 'planetview legend', 0x4a5270: 'planetview info',
}


class Diff(Exception):
    pass


class Pair:
    def __init__(self, data_dir):
        self.port = Port()
        L = self.port.lib
        L.shim_planet_raw.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int]
        L.shim_planet_soul_log.argtypes = [ctypes.c_void_p, ctypes.c_int]
        L.planet_year.argtypes = [ctypes.c_uint]
        L.planet_year.restype = ctypes.c_uint32
        for f in ('planet_stats_step', 'planet_emit_step', 'planet_sector_step', 'planet_tech_step'):
            getattr(L, f).restype = ctypes.c_bool
        if not L.shim_planet_init(data_dir.encode()):
            sys.exit('port: planet_tables_load failed (ALIFE.EXE in %s?)' % data_dir)
        self.L = L
        self.emu = emu = Emu()
        for a in STUBS:
            emu.stub(a)
        self.emu_souls = []
        emu.hook(0x445785, lambda e, args: self.emu_souls.append(tuple(args(i) for i in range(1, 6))))
        self.map_addr = emu.malloc(0x6401)
        emu.w32(MAP_PTR, self.map_addr)
        emu.w32(MAP_SIZE, 0x6401)

    # ---- state transfer ---------------------------------------------------
    def port_raw(self, name, size):
        buf = ctypes.create_string_buffer(size)
        n = self.L.shim_planet_raw(name.encode(), buf, 0)
        if name == 'map' and n == 0:
            return None  # no map in the port (after world_reset)
        assert n == size, (name, n, size)
        return buf.raw

    def port_set(self, name, data):
        buf = ctypes.create_string_buffer(bytes(data), len(data))
        self.L.shim_planet_raw(name.encode(), buf, 1)

    def push(self):
        for name, (addr, size) in FIELDS.items():
            self.emu.write(addr, self.port_raw(name, size))
        m = self.port_raw('map', 0x6401)
        if m is not None:
            self.emu.write(self.map_addr, m)

    def compare(self, what):
        e = self.emu
        if self.port.rng_state() != e.rng_state():
            raise Diff('%s: RNG state port %08x, original %08x' % (what, self.port.rng_state(), e.rng_state()))
        for name, (addr, size) in FIELDS.items():
            a, b = self.port_raw(name, size), e.read(addr, size)
            if a != b:
                i = next(k for k in range(size) if a[k] != b[k])
                where = ''
                if name == 'sectors':
                    where = ' (sector %d,%d +%#x)' % (i // 0x540, i % 0x540 // 0x2a, i % 0x2a)
                raise Diff('%s: %s differs at byte %d%s: port %s, original %s'
                           % (what, name, i, where, a[i:i + 8].hex(), b[i:i + 8].hex()))
        m = self.port_raw('map', 0x6401)
        if m is not None and m != e.read(self.map_addr, 0x6401):
            raise Diff('%s: planet map differs' % what)
        log = (ctypes.c_uint32 * (5 * 65536))()
        n = self.L.shim_planet_soul_log(log, 65536)
        ps = [tuple(log[i * 5:i * 5 + 5]) for i in range(min(n, 65536))]
        es = [tuple(v & 0xffffffff for v in s) for s in self.emu_souls]
        es = [(x, y, t & 0xff, c, sz) for (x, y, t, c, sz) in es]
        self.emu_souls = []
        if n > 65536:
            es = es[:65536]
        if ps != es:
            k = next((i for i in range(min(len(ps), len(es))) if ps[i] != es[i]), min(len(ps), len(es)))
            raise Diff('%s: SOUL groups differ at #%d of %d/%d: port %s, original %s'
                       % (what, k, len(ps), len(es), ps[k:k + 1], es[k:k + 1]))
        return len(ps)

    def get(self, name):
        addr, size = FIELDS[name]
        return int.from_bytes(self.port_raw(name, size), 'little')

    def set_both(self, name, value):
        addr, size = FIELDS[name]
        data = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')
        self.port_set(name, data)
        self.emu.write(addr, data)

    # ---- running ------------------------------------------------------------
    def both(self, what, port_fn, orig_addr, *args):
        r1 = port_fn(*args)
        r2 = self.emu.call(orig_addr, *args)
        r1, r2 = int(r1), r2
        return r1, r2

    def step(self, what, port_name, orig_addr, mask=0xff, args=()):
        r1 = getattr(self.L, port_name)(*args)
        r2 = self.emu.call(orig_addr, *args)
        souls = self.compare(what)
        if (int(r1) & mask) != (r2 & mask):
            raise Diff('%s: returned port %d, original %d' % (what, int(r1), r2))
        return int(r1), souls

    def run_pass(self, what, port_name, orig_addr, max_calls):
        souls = 0
        for i in range(max_calls):
            done, s = self.step('%s call %d' % (what, i), port_name, orig_addr,
                                mask=0xff if orig_addr != 0x49b05c else 0xffffffff)
            souls += s
            if done:
                return i + 1, souls
        raise Diff('%s did not finish' % what)

    def year(self, forced=0):
        n1, _ = self.run_pass('stats', 'planet_stats_step', 0x4aae01, 4)
        # sim_update phase 20 end
        self.set_both('embos_hi', self.get('embos_next_hi'))
        self.set_both('embos_lo', self.get('embos_next_lo'))
        self.set_both('embos_next_hi', 0)
        self.set_both('embos_next_lo', 0)
        n2, souls = self.run_pass('emit', 'planet_emit_step', 0x4ab77a, 100)
        killed, s2 = self.step('planet year', 'planet_year', 0x49c8b8, mask=0xffffffff, args=(forced,))
        n3, _ = self.run_pass('sectors', 'planet_sector_step', 0x49a2da, 40)
        n4, _ = self.run_pass('tech', 'planet_tech_step', 0x49b05c, 40)
        # phase 15 end, year end
        self.set_both('planet_deaths_last', self.get('planet_deaths'))
        self.set_both('planet_deaths', 0)
        self.set_both('year', self.get('year') + 1)
        return souls + s2, killed


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('saves', nargs='*')
    ap.add_argument('--years', type=int, default=3)
    ap.add_argument('--gen', type=int, default=3, help='world generation seeds to compare')
    ap.add_argument('--seed', type=int, default=0x1234)
    ap.add_argument('--disasters', action='store_true', help='also force each disaster kind once')
    ap.add_argument('--flood', action='store_true', help='also run the Flood (0x49c226)')
    a = ap.parse_args()
    saves = a.saves or sorted(glob.glob(os.path.join(ORIG, 'ASCEN00[1-4].ASC')))
    p = Pair(ORIG)
    fails = 0
    for path in saves:
        try:
            p.port.world_load(path)
            p.L.planet_summary()
            p.port.rng_seed(a.seed)
            p.emu.rng_seed(a.seed)
            p.push()
            p.compare('after loading')
            for y in range(a.years):
                souls, killed = p.year()
                print('  %s year %d: ok (%d SOUL groups, %d killed)' % (os.path.basename(path), y + 1, souls,
                                                                          killed))
            if a.disasters:
                p.set_both('cheats', 1)
                for kind in range(1, 11):
                    p.L.shim_planet_set_sel(20 + kind, 12)
                    p.emu.w32(0x66c880, 20 + kind)
                    p.emu.w32(0x66c884, 12)
                    k, s = p.step('forced disaster %d' % (kind - 1), 'planet_year', 0x49c8b8,
                                  mask=0xffffffff, args=(kind,))
                    print('  %s disaster %d: ok (%d killed, %d SOUL groups)' % (os.path.basename(path),
                                                                                kind - 1, k, s))
                    p.set_both('disaster', 0xff)
                p.set_both('cheats', 0)
            if a.flood:
                p.L.planet_flood()
                p.emu.call(0x49c226)
                s = p.compare('flood')
                souls, killed = p.year()
                print('  %s flood: ok (%d SOUL groups), next year ok' % (os.path.basename(path), s))
        except Diff as e:
            fails += 1
            print('FAIL %s: %s' % (os.path.basename(path), e))
    for i in range(a.gen):
        seed = 1 + i * 7919
        try:
            p.port.world_load(saves[0]) if saves else None
            p.L.world_reset()
            p.push()
            p.port.rng_seed(seed)
            p.emu.rng_seed(seed)
            p.L.planet_generate()
            p.emu.call(0x496ed1)
            p.compare('generate seed %d' % seed)
            for y in range(a.years):
                p.year()
            print('  new planet seed %d: ok (+%d years)' % (seed, a.years))
        except Diff as e:
            fails += 1
            print('FAIL generate seed %d: %s' % (seed, e))
    print('%d failures' % fails if fails else 'all identical')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
