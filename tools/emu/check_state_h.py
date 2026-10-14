#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Checks the original addresses that src/game/state.h claims for each
GameState field, against where ALIFE.EXE's own loader puts that field.

For every field (arrays: first and last element) the port's state is
changed in that field alone and saved (savegame_save_mem, version 7300);
the bytes that change in the file are then looked up in the load map
(the original loader traced on the unchanged file), which gives the
address the original stores them at. That is compared with the address
state.h claims (field name unk_XXXXXX, a "0x..." comment, or record base +
the record offset).

    tools/emu/check_state_h.py [--save ASCEN001.ASC] [-v]
"""
import argparse
import ctypes
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge  # noqa: E402
from difftest import orig_path  # noqa: E402
from names import Names  # noqa: E402
from port import Port  # noqa: E402

SKIP_PREFIX = ('hdr.', 'plane[0].', 'plane[2].sim', 'plane[2].stats')
GRIDS = ('.cells', '.objmap', '.sim')
LAYOUT = ('planet.map_size', 'planet.n_a', 'planet.n_b', 'planet.n_c')   # change the file layout


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--save', default='ASCEN001.ASC')
    ap.add_argument('-v', action='store_true', help='list every field')
    a = ap.parse_args()

    port = Port()
    names = Names()
    port.world_load(orig_path(a.save))
    base = port.save_mem(7300)
    img = bridge.PortImage()
    lm = bridge.orig_load(img.emu, base, trace=True)
    claims = {path: addr for addr, size, path in names.claims}
    st, _ = port.ptr('state')
    base_int = int.from_bytes(base, 'little')

    ok = bad = unsaved = unclaimed = 0
    for leaf in names.h.flatten('GameState', max_elems=4):
        p = leaf.path
        if p.startswith(SKIP_PREFIX) or any(g + '[' in p for g in GRIDS) or leaf.member.ptr:
            continue
        if p in LAYOUT or p.startswith(LAYOUT):
            continue
        old = ctypes.string_at(st + leaf.off, leaf.size)
        ctypes.memmove(st + leaf.off, bytes(b ^ 0x5a for b in old), leaf.size)
        try:
            d = port.save_mem(7300)
        finally:
            ctypes.memmove(st + leaf.off, old, leaf.size)
        if len(d) != len(base):
            if a.v:
                print('  %-40s changes the file layout, skipped' % p)
            continue
        x = int.from_bytes(d, 'little') ^ base_int
        if not x:
            unsaved += 1
            if a.v:
                print('  %-40s not written to the file' % p)
            continue
        lo = ((x & -x).bit_length() - 1) // 8
        hi = (x.bit_length() - 1) // 8
        addr = lm.off2addr.get(lo)
        claim = claims.get(p)
        # the field's first byte: the lowest changed file byte, minus where
        # in the field it sits (a value can be stored transformed)
        if addr is None:
            print('  %-40s file +%d..%d: the original loader does not store it' % (p, lo, hi))
            bad += 1
            continue
        if claim is None:
            unclaimed += 1
            if a.v:
                print('  %-40s at %#x (%s); state.h claims no address' % (p, addr, names.name(addr)))
            continue
        if addr == claim:
            ok += 1
            if a.v:
                print('  %-40s %#x ok' % (p, addr))
        else:
            bad += 1
            print('  %-40s state.h says %#x, the original stores it at %#x (%s)'
                  % (p, claim, addr, names.name(addr)))
    print('%d fields at their claimed address, %d wrong, %d without a claim, %d not saved'
          % (ok, bad, unclaimed, unsaved))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
