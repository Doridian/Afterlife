#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Differential checks of the world core (WP0: game/util.h, game/world.h,
the ALSV loader) against ALIFE.EXE. Also the worked example for writing
such tests; see tools/emu/README.md.

    tools/emu/check_wp0.py [rng|bignum|cells|load|all] [--saves ASCEN001.ASC ...]
"""
import argparse
import ctypes
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from difftest import Harness, SAVES, orig_path  # noqa: E402
import bridge  # noqa: E402
from alife_emu import Emu  # noqa: E402

# Original addresses (symbols/ALIFE.EXE.txt).
RAND, RAND_RANGE = 0x4b1202, 0x42b088
BIGNUM_ADD, BIGNUM_SUB, BIGNUM_GE, BIGNUM_GT = 0x42a950, 0x42a9ec, 0x42aaa8, 0x42ab2f
BIGNUM_GE_INT, BIGNUM_LESS = 0x42abb6, 0x42ac32
MUL_DIV_ROUND, MUL_DIV_CLAMP = 0x42ae96, 0x42af95
FMT_THOUSANDS, FMT_THOUSANDS_K = 0x42ad0c, 0x42aded

# (orig address, port function, result mask): getters taking (x, y)
CELL_GETTERS = [
    (0x42b46e, 'shim_cell_tileset', 0xff),        # map_cell_set_at (returns a byte)
    (0x42b5cb, 'shim_cell_index', 0xff),
    (0x42b37e, 'shim_cell_strip', 0xff),
    (0x441a12, 'shim_cell_test_bracket', 0xffffffff),
    (0x441a44, 'shim_cell_test_shade', 0xffffffff),
    (0x441a74, 'shim_cell_test_overlay', 0xffffffff),
    (0x42b4f4, 'cell_origin_tileset', 0xff),
    (0x42b65f, 'cell_origin_index', 0xff),
    (0x42b75f, 'cell_object_slot', 0xff),
]
SIM_GETTERS = [
    (0x43085d, 'shim_sim_zone_kind', 0xff),
    (0x431b40, 'shim_sim_perm', 0xffffffff),
    (0x431bd6, 'shim_sim_temp', 0xffffffff),
    (0x431c6c, 'shim_sim_pop', 0xffffffff),
    (0x441aa4, 'shim_sim_test_flag_e7', 0xff),
]
# setters taking (x, y, v)
CELL_SETTERS = [
    (0x42b4a0, 'shim_cell_set_tileset', lambda r: r.randrange(32)),
    (0x42b60b, 'shim_cell_set_index', lambda r: r.randrange(128)),
    (0x42b3be, 'shim_cell_set_strip', lambda r: r.randrange(16)),
    (0x42b412, 'shim_cell_set_flags', lambda r: 1 << r.randrange(16)),
    (0x42b43f, 'shim_cell_clear_flags', lambda r: 1 << r.randrange(16)),
    (0x43089b, 'shim_sim_set_zone_kind', lambda r: r.randrange(16)),
    (0x45852f, 'soul_group_set', lambda r: r.randrange(0x2000)),
]
SELECT_PLANE = 0x42b102   # map_view_select_plane(&map_view, plane)

fails = 0


def report(name, bad, total, examples):
    global fails
    if total is None:
        status = 'ok' if not bad else 'MISMATCH (%d differences)' % bad
    else:
        status = ('ok' if not bad else 'MISMATCH') + ' (%d/%d)' % (total - bad, total)
    print('  %-34s %s' % (name, status))
    for e in examples[:5]:
        print('    ', e)
    fails += bool(bad)


def check_rng(h):
    """crt_rand and rand_range from identical RNG states, incl. edge ranges."""
    r = random.Random(1)
    port_rand = h.port_.fn('crt_rand')
    port_range = h.port_.fn('rand_range', ctypes.c_int, [ctypes.c_int, ctypes.c_int])
    for seed in (1, 0, 0xdeadbeef, r.getrandbits(32)):
        h.seed(seed)
        bad = [(i, a, b) for i in range(2000) for a, b in [(h.orig(RAND, track=False), port_rand())] if a != b]
        report('crt_rand seed %#x' % seed, len(bad) + (h.emu.rng_state() != h.port_.rng_state()), 2000, bad)
    cases = [(1, 6), (6, 1), (0, 0), (-5, 5), (0, 0x7ffe), (0, 0x7fff), (0, 0x8000), (-1000000, 1000000),
             (0, 0x7fffffff), (-0x80000000, 0x7ffffffe), (-0x80000000, -1)]
    cases += [(r.randint(-2 ** 31, 2 ** 31 - 1), r.randint(-2 ** 31, 2 ** 31 - 1)) for _ in range(300)]
    cases += [(r.randint(-100, 100), r.randint(-100, 100)) for _ in range(300)]
    h.seed(0x1234)
    bad = []
    for lo, hi in cases:
        if (hi - lo if hi >= lo else lo - hi) + 1 == 2 ** 32:
            continue   # the original divides by zero
        a = h.orig(RAND_RANGE, lo, hi, track=False)
        b = port_range(lo, hi)
        if a != b:
            bad.append('rand_range(%d, %d): orig %d port %d' % (lo, hi, a, b))
    if h.emu.rng_state() != h.port_.rng_state():
        bad.append('rng state after: orig %#x port %#x' % (h.emu.rng_state(), h.port_.rng_state()))
    report('rand_range', len(bad), len(cases), bad)


def check_bignum(h):
    r = random.Random(2)
    e = h.emu
    vals = [0, 1, 2, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff]
    nums = [(hi, lo) for hi in (-2, -1, 0, 1, 2, 0x7fffffff) for lo in vals]
    nums += [(r.choice([-1, 0, 0, 1, r.randint(0, 5)]), r.getrandbits(32)) for _ in range(150)]
    amounts = vals + [r.getrandbits(32) for _ in range(20)] + [r.randint(0, 1000) for _ in range(10)]
    ints = [0, 1, -1, 5, -5, 0x7fffffff, -0x80000000] + [r.randint(-2 ** 31, 2 ** 31 - 1) for _ in range(10)]
    buf = e.malloc(8)
    L = h.port_.lib
    hi_p, lo_p = ctypes.c_int32(), ctypes.c_uint32()
    for name, oaddr, pf in (('bignum_add', BIGNUM_ADD, L.shim_bignum_add), ('bignum_sub', BIGNUM_SUB, L.shim_bignum_sub)):
        pf.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_uint32), ctypes.c_uint32]
        bad, n = [], 0
        for hi, lo in nums:
            for v in amounts:
                e.w32(buf, hi)
                e.w32(buf + 4, lo)
                h.orig(oaddr, buf, v, track=False)
                hi_p.value, lo_p.value = hi, lo
                pf(ctypes.byref(hi_p), ctypes.byref(lo_p), v)
                n += 1
                if (e.i32(buf), e.u32(buf + 4)) != (hi_p.value, lo_p.value):
                    bad.append('%s({%d, %#x}, %#x): orig {%d, %#x} port {%d, %#x}' % (
                        name, hi, lo, v, e.i32(buf), e.u32(buf + 4), hi_p.value, lo_p.value))
        report(name, len(bad), n, bad)
    for name, oaddr, pf in (('bignum_less', BIGNUM_LESS, L.shim_bignum_less),
                            ('bignum_ge_int', BIGNUM_GE_INT, L.shim_bignum_ge_int)):
        pf.argtypes = [ctypes.c_int32, ctypes.c_uint32, ctypes.c_int32]
        bad = ['%s({%d, %#x}, %d): orig %d port %d' % (name, hi, lo, v, a, b)
               for hi, lo in nums for v in ints
               for a, b in [(h.orig(oaddr, hi, lo, v, track=False) & 0xff, pf(hi, lo, v))] if bool(a) != bool(b)]
        report(name, len(bad), len(nums) * len(ints), bad)
    sample = nums[::3]
    for name, oaddr, pf in (('bignum_ge', BIGNUM_GE, L.shim_bignum_ge), ('bignum_gt', BIGNUM_GT, L.shim_bignum_gt)):
        pf.argtypes = [ctypes.c_int32, ctypes.c_uint32, ctypes.c_int32, ctypes.c_uint32]
        bad = ['%s({%d, %#x}, {%d, %#x}): orig %d port %d' % (name, ah, al, bh, bl, a, b)
               for ah, al in sample for bh, bl in sample
               for a, b in [(h.orig(oaddr, ah, al, bh, bl, track=False) & 0xff, pf(ah, al, bh, bl))]
               if bool(a) != bool(b)]
        report(name, len(bad), len(sample) ** 2, bad)
    u = [0, 1, 2, 3, 100, 0xffff, 0x10000, 0x7fffffff, 0x80000000, 0xffffffff] + [r.getrandbits(32) for _ in range(12)] \
        + [r.randint(0, 5000) for _ in range(12)]
    for name, oaddr, pn in (('mul_div_round', MUL_DIV_ROUND, 'mul_div_round'),
                            ('mul_div_clamp', MUL_DIV_CLAMP, 'mul_div_clamp')):
        pf = h.port_.fn(pn, ctypes.c_uint32, [ctypes.c_uint32] * 3)
        bad = ['%s(%#x, %#x, %#x): orig %#x port %#x' % (name, a, b, c, x, y)
               for a in u for b in u for c in u
               for x, y in [(h.orig(oaddr, a, b, c, track=False) & 0xffffffff, pf(a, b, c))] if x != y]
        report(name, len(bad), len(u) ** 3, bad)
    for name, oaddr, pn in (('fmt_thousands', FMT_THOUSANDS, 'fmt_thousands'),
                            ('fmt_thousands_k', FMT_THOUSANDS_K, 'fmt_thousands_k')):
        pf = h.port_.fn(pn, ctypes.c_char_p, [ctypes.c_char_p, ctypes.c_uint32])
        out = ctypes.create_string_buffer(32)
        nums_f = u + [999, 1000, 999999, 1000000, 1000001, 1500000, 999999999, 1000000000, 4000000000]
        bad = []
        for v in nums_f:
            a = e.cstr(h.orig(oaddr, v, track=False) & 0xffffffff)
            b = pf(out, v).decode('latin1')
            if a != b:
                bad.append('%s(%d): orig %r port %r' % (name, v, a, b))
        report(name, len(bad), len(nums_f), bad)


def check_cells(h, saves):
    """Every getter on every cell of each plane, then random setters with a
    full state diff after each batch."""
    r = random.Random(3)
    for save in saves:
        h.load(save)
        print(' ', save)
        for plane in (1, 2, 3):
            h.orig(SELECT_PLANE, bridge.MAP_VIEW, plane, track=False)
            h.port_.lib.world_select_plane(plane)
            getters = CELL_GETTERS + (SIM_GETTERS if plane != 2 else [])
            for oaddr, pname, mask in getters:
                pf = h.port_.fn(pname, ctypes.c_uint32, [ctypes.c_int, ctypes.c_int])
                bad = []
                for y in range(100):
                    for x in range(100):
                        a = h.orig(oaddr, x, y, track=False) & mask
                        b = pf(x, y) & mask
                        if a != b:
                            bad.append('plane %d (%d,%d): orig %#x port %#x' % (plane, x, y, a, b))
                report('plane %d %s' % (plane, pname), len(bad), 10000, bad)
        if save == saves[0]:
            pf = h.port_.fn('soul_group_get', ctypes.c_uint, [ctypes.c_int, ctypes.c_int])
            for plane in (1, 3):
                h.orig(SELECT_PLANE, bridge.MAP_VIEW, plane, track=False)
                h.port_.lib.world_select_plane(plane)
                bad = ['(%d,%d): orig %d port %d' % (x, y, a, b) for x in range(-2, 102, 3) for y in range(0, 100)
                       for a, b in [(h.orig(0x458490, x & 0xff, y & 0xff, track=False) & 0xffff, pf(x, y))] if a != b]
                report('plane %d soul_group_get' % plane, len(bad), 3500, bad)
            h.check(what='after getters (soul_group_get may clear stale indices)')
            ft = h.port_.fn('tile_index_footprint', ctypes.c_int, [ctypes.c_uint])
            bad = [(i, a, b) for i in range(256) for a, b in [(h.orig(0x42b7c5, i, track=False), ft(i))] if a != b]
            report('tile_index_footprint', len(bad), 256, bad)
        # setters, a full diff after each round
        for rnd in range(4):
            plane = (1, 3, 2, 1)[rnd]
            h.orig(SELECT_PLANE, bridge.MAP_VIEW, plane)
            h.port_.lib.world_select_plane(plane)
            for _ in range(60):
                oaddr, pname, gen = r.choice(CELL_SETTERS if plane != 2 else CELL_SETTERS[:5])
                x, y, v = r.randrange(100), r.randrange(100), gen(r)
                h.orig(oaddr, x, y, v)
                h.port(pname, x, y, v, restype=None, argtypes=[ctypes.c_int, ctypes.c_int, ctypes.c_uint])
            d = h.diff()
            report('setters round %d (plane %d)' % (rnd, plane), len(d), None, [str(x) for x in d])
        lines, n = h.uncompared_writes()
        # informational: map_view_select_plane's renderer half (MapView
        # fields, per-plane view flags) has no counterpart in g_state/g_world
        print('  (info) globals the original wrote outside the compared state:')
        for line in lines:
            print('     ', line)


def check_load(h, saves):
    """Emulated save_load_game vs port world_load: the whole saved state,
    grids raw, the current-plane globals, the planet map."""
    for save in saves:
        t = time.time()
        h.load(save)
        d = h.diff()
        report('load %s (%.1fs)' % (save, time.time() - t), len(d), None, [str(x) for x in d])
    # negative control: the comparison must see a change on either side
    a, n = h.port_.ptr('sim', 3)
    ctypes.memmove(a + 18 * (100 * 42 + 17) + 3, b'\x5a', 1)
    h.emu.w32(0x630520 + 0x7fec, h.emu.u32(0x630520 + 0x7fec) + 1)
    d = [str(x) for x in h.diff()]
    want = ['plane[3].sim[x=17,y=42].b[3]', 'plane[1].stats.population']
    ok = len(d) == 2 and all(w in ' '.join(d) for w in want)
    report('negative control (2 planted differences)', 0 if ok else 1, None, d)
    h.reset()
    lm = h.loadmap
    print('  load map: %d file bytes -> globals, %d unmapped (header/thumbnail), %d values transformed' % (
        len(lm.off2addr), lm.file_len - len(lm.off2addr), len(lm.transformed)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('what', nargs='*', default=['all'], choices=['rng', 'bignum', 'cells', 'load', 'all'])
    ap.add_argument('--saves', nargs='+', default=SAVES)
    a = ap.parse_args()
    what = set(a.what)
    if 'all' in what:
        what = {'rng', 'bignum', 'cells', 'load'}
    h = Harness()
    h.load(a.saves[0])
    if 'rng' in what:
        print('RNG (util.h vs MSVC rand 0x4b1202, rand_range 0x42b088)')
        check_rng(h)
    if 'bignum' in what:
        print('bignum / mul_div / fmt (0x42a950-0x42b101)')
        check_bignum(h)
    if 'load' in what:
        print('save_load_game 0x48f837 vs world_load')
        check_load(h, a.saves)
    if 'cells' in what:
        print('cell / sim cell accessors')
        check_cells(h, a.saves)
    print('FAILED' if fails else 'all ok')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
