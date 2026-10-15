#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Differential test of the Graphview's yearly records (WP6a,
src/ui/graphview.c) against ALIFE.EXE under the emulator.

For each save (default: orig/ASCEN00[1-4].ASC) the port loads it and the
original gets the port's state (bridge.PortImage: the port's save loaded by
the original's own loader, so both start equal whatever the post-load
rebuilds do), then for a run of game years the same inputs are poked into both (year, realm
populations and SOUL pools, pennies, the soul rate and the six other
History Graph inputs; values grow so that the series rescale, pennies go
negative, years cross the 20- and 400-year boundaries and year 0), and
graph_history_record (0x40d34b) and graph_history_update (0x4154ab) run on
both. The two summaries (0x6399d0 / 0x639480) and the History Graph
records (0x5c4eb0) must be identical byte for byte: after the first ten
years, every --every'th year (default 10), around each multiple of 20
years and after the last. Each comparison loads the port's state through
the original's loader (~2 s), so --every 1 (every year) takes about 15 min
per save.

    tools/emu/test_views.py [--years N] [--every K] [save ...]
"""
import argparse
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bridge  # noqa: E402
from difftest import SAVES, orig_path  # noqa: E402
from port import Port  # noqa: E402

RECORD, UPDATE = 0x40d34b, 0x4154ab
HEAVEN, HELL = 0x5b2f70, 0x630520
POP, POOL_HI, POOL_LO = 0x7fec, 0x80fd, 0x8101
YEAR, PEN_HI, PEN_LO, SOUL_RATE = 0x630268, 0x630438, 0x63043c, 0x630430
PLAIN = [0x5fa5f4, 0x5f7ed4, 0x626598, 0x638690, 0x593250, 0x57e09c]
REGIONS = [('summary[0]', 0x6399d0, 0x1b1), ('summary[1]', 0x639480, 0x1b1), ('rec_5c4eb0', 0x5c4eb0, 30 * 0x72)]


def inputs(k, start):
    """Year k's inputs: a few patterns, growing past 2^31 and 2^32."""
    year = start + k
    if k == 5:
        year = 0          # the year-0 restart
    big = k * k * 4093 + 7
    hv_pop = (big * 3001) & 0xffffffff
    hv_hi = k // 40       # pools of 2^32 and more from year 40 on
    hl_pop = (big * 17) & 0x7fffffff
    hl_hi = (k // 25) if k % 3 else 0
    if k % 7 == 3:
        pen = (-1, (k * 977) & 0xffffffff)
    else:
        pen = (k // 30, (big * 101) & 0xffffffff)
    plain = [(big * (i + 3) * (1 if k % (i + 2) else 1000)) & 0xffffffff for i in range(6)]
    return [year, hv_pop, hv_hi, (big * 7) & 0xffffffff, hl_pop, hl_hi, (big * 11) & 0xffffffff,
            pen[0], pen[1], 0x100 + k * 5] + plain


def poke_orig(emu, v):
    year, hv_pop, hv_hi, hv_lo, hl_pop, hl_hi, hl_lo, pen_hi, pen_lo, rate = v[:10]
    emu.w32(YEAR, year)
    emu.w32(HEAVEN + POP, hv_pop)
    emu.w32(HEAVEN + POOL_HI, hv_hi)
    emu.w32(HEAVEN + POOL_LO, hv_lo)
    emu.w32(HELL + POP, hl_pop)
    emu.w32(HELL + POOL_HI, hl_hi)
    emu.w32(HELL + POOL_LO, hl_lo)
    emu.w32(PEN_HI, pen_hi)
    emu.w32(PEN_LO, pen_lo)
    emu.w32(SOUL_RATE, rate)
    for a, x in zip(PLAIN, v[10:]):
        emu.w32(a, x)


def compare(a, b, what):
    for name, addr, size in REGIONS:
        x, y = a.read(addr, size), b.read(addr, size)
        if x != y:
            i = next(k for k in range(size) if x[k] != y[k])
            raise AssertionError('%s: %s differs at +0x%x: orig %s port %s' % (what, name, i, x[i:i + 8].hex(),
                                                                             y[i:i + 8].hex()))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--years', type=int, default=430)
    ap.add_argument('--every', type=int, default=10, help='compare every K years (1: every year)')
    ap.add_argument('saves', nargs='*')
    a = ap.parse_args()
    port = Port()
    orig, image = bridge.PortImage(), bridge.PortImage()
    poke = port.fn('shim_views_poke', None, [ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int32, ctypes.c_uint32,
                                             ctypes.c_uint32, ctypes.c_int32, ctypes.c_uint32, ctypes.c_int32,
                                             ctypes.c_uint32] + [ctypes.c_uint32] * 7)
    record, update = port.fn('graph_history_record', None, []), port.fn('graph_history_update', None, [])
    fails = 0
    for s in a.saves or [orig_path(x) for x in SAVES]:
        port.world_load(s)
        e = orig.load(port)
        try:
            start = e.u32(YEAR) - 3     # the years from here cross 20/400 boundaries
            e.call(RECORD)
            record()
            compare(e, image.load(port), 'graph_history_record')
            for k in range(a.years):
                v = inputs(k, start)
                poke_orig(e, v)
                poke(*v)
                e.call(RECORD)
                record()
                e.call(UPDATE)
                update()
                if k < 10 or k % max(a.every, 1) == 0 or v[0] % 20 <= 1 or k == a.years - 1:
                    compare(e, image.load(port), '%s year %d (k %d)' % (os.path.basename(s), v[0], k))
            print('%s: %d years identical' % (os.path.basename(s), a.years), flush=True)
        except AssertionError as err:
            print(err, flush=True)
            fails += 1
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
