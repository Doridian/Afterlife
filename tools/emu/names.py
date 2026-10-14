# SPDX-License-Identifier: AGPL-3.0-or-later
"""Address -> name for ALIFE.EXE's globals, from src/game/state.h (field
names and address comments, record layouts) and symbols/ALIFE.EXE.txt.

    n = Names()
    n.name(0x630520 + 0x7fec)   -> 'plane[1].stats.population'
    n.claims                     -> [(addr, size, path)] what state.h says

Parsed at runtime, so it follows edits to state.h.
"""
import bisect
import os
import re

from cstruct import Headers, PRIM, addrs_in, claimed_global, claimed_offset

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

# Grids whose port layout is the original's (row-major [y * 100 + x]).
RAW_GRIDS = [
    # (addr, kind, plane, elem size)
    (0x593270, 'cells', 1, 4), (0x5bb270, 'cells', 2, 4), (0x6265b0, 'cells', 3, 4),
    (0x5f4cf0, 'objmap', 1, 1), (0x5f25c0, 'objmap', 2, 1), (0x5f7ee0, 'objmap', 3, 1),
    (0x5fa670, 'sim', 1, 18), (0x5c6680, 'sim', 3, 18),
]
TILE_INFO = [(0x5f7480, 1), (0x5c5c20, 3)]   # RealmTileInfo[0x84], 0x14 bytes each

# Record arrays in the original that state.h doesn't tie to an address in
# a field comment: (addr, GameState path prefix, struct type, count).
EXTRA_RECORDS = [
    (0x630520, 'plane[1].stats', 'PlaneStats', 1),
    (0x5b2f70, 'plane[3].stats', 'PlaneStats', 1),
    (0x64e5f0, 'objects', 'MapObjectRec', 63),
    (0x5c4eb0, 'rec_5c4eb0', 'Rec72', 30),
]


class Names:
    def __init__(self, repo=REPO):
        self.h = Headers(os.path.join(repo, 'src/game/state.h'), os.path.join(repo, 'src/game/world.h'))
        self.syms = []      # sorted (addr, name)
        p = os.path.join(repo, 'symbols/ALIFE.EXE.txt')
        if os.path.exists(p):
            for line in open(p):
                m = re.match(r'\s*(?:0x)?([0-9a-fA-F]+)\s+(data|func)\s+(\w+)', line)
                if m:
                    self.syms.append((int(m.group(1), 16), m.group(3)))
        self.syms.sort()
        self._sym_addrs = [a for a, _ in self.syms]
        self.spans = []     # (addr, size, path)
        self.claims = []    # (addr, size, gamestate path) for scalars state.h ties to an address
        self.records = []   # (addr, size, path prefix, type, count)
        self._build()
        self.spans.sort()
        self._span_addrs = [s[0] for s in self.spans]

    # ---- building ----------------------------------------------------------
    def _record(self, addr, prefix, type_, count, dims=None):
        leaves = list(self._record_leaves(type_))
        # stride: the packed size, or more where claimed offsets leave holes
        psz = max([self.h.layout(type_)['psize']] + [o + s for o, s, _ in leaves])
        self.records.append((addr, psz * count, prefix, type_, count))
        dims = dims or [count]
        for i in range(count):
            base = addr + i * psz
            idx, r = [], i
            for d in reversed(dims):
                idx.insert(0, r % d)
                r //= d
            pre = prefix + (''.join('[%d]' % k for k in idx) if count > 1 else '')
            for poff, size, path in leaves:
                self.spans.append((base + poff, size, pre + '.' + path))
                self.claims.append((base + poff, size, pre + '.' + path))

    def _record_leaves(self, type_):
        """(packed offset, size, path) of a packed record, using the offsets
        the member names / comments claim where they do (state.h records
        can have unsaved holes, e.g. Rec26 +0x24)."""
        member_poff = {}
        for m, o, po, s, ps, n in self.h.layout(type_)['fields']:
            c = claimed_offset(m)
            member_poff[id(m)] = (po, c if c is not None else po)
        for leaf in self.h.flatten(type_):
            po, claimed = member_poff[id(leaf.top)]
            top_start = self._top_poff(type_, leaf.top)
            yield claimed + (leaf.poff - top_start), leaf.size, leaf.path

    def _top_poff(self, type_, member):
        for m, o, po, s, ps, n in self.h.layout(type_)['fields']:
            if m is member:
                return po
        raise KeyError(member.name)

    def _build(self):
        h = self.h
        for addr, kind, plane, esz in RAW_GRIDS:
            self.spans.append((addr, 100 * 100 * esz, 'plane[%d].%s' % (plane, kind)))
        for addr, plane in TILE_INFO:
            self._record(addr, 'g_world.tile_info_%s' % ('hell' if plane == 1 else 'heaven'),
                         'RealmTileInfo', 0x84)
        for addr, prefix, type_, count in EXTRA_RECORDS:
            if type_ in h.structs:
                self._record(addr, prefix, type_, count)
        self._claim_members('GameState', '')
        if 'PlanetState' in h.structs:
            self._claim_members('PlanetState', 'planet.')
        if 'Block26' in h.structs:
            self._claim_members('Block26', 'block_4f33f8.', rec_base=0x4f33f8)

    def _claim_members(self, type_, prefix, rec_base=None):
        h = self.h
        fields = h.layout(type_)['fields']
        # group members of one statement (same line + comment)
        groups = {}
        for f in fields:
            groups.setdefault((f[0].line, f[0].comment), []).append(f)
        for key, grp in groups.items():
            unk = set()
            for f in grp:
                c = claimed_global(f[0])
                if c and f[0].name.startswith('unk_'):
                    unk.add(c[0])
            free = []
            for f in grp:
                if not f[0].name.startswith('unk_') or len(f[0].name) < 10:
                    free.append(f)
            comment_addrs = [a for a in addrs_in(grp[0][0].comment) if a not in unk]
            for f in grp:
                m, o, po, s, ps, n = f
                if m.ptr:
                    continue
                c = claimed_global(m) if m.name.startswith('unk_') and len(m.name) >= 10 else []
                if not c and f in free:
                    k = free.index(f)
                    if m.type in h.structs and m.type != 'Bignum' and len(free) == 1 and n > 1 \
                            and len(comment_addrs) == n:
                        # one address per element: "0x6399d0, 0x639480"
                        for i, a in enumerate(comment_addrs):
                            self._record(a, prefix + m.name + '[%d]' % i, m.type, 1)
                        continue
                    if m.type == 'Bignum' and len(free) == 1:
                        c = comment_addrs[:2]
                    elif len(comment_addrs) == len(free):
                        c = [comment_addrs[k]]
                    elif k == 0 and comment_addrs and len(free) == 1:
                        c = [comment_addrs[0]]
                if not c:
                    if rec_base is not None and m.type in h.structs and m.name == 'rec':
                        self._record(rec_base, prefix + m.name, m.type, n, m.dims)
                    continue
                base = c[0]
                if m.type in h.structs and m.type != 'Bignum':
                    self._record(base, prefix + m.name, m.type, n, m.dims)
                    continue
                if m.type == 'Bignum':
                    hi, lo = base, c[1] if len(c) > 1 else base + 4
                    self.spans += [(hi, 4, prefix + m.name + '.hi'), (lo, 4, prefix + m.name + '.lo')]
                    self.claims += [(hi, 4, prefix + m.name + '.hi'), (lo, 4, prefix + m.name + '.lo')]
                    continue
                esz = PRIM[m.type][0]
                if not m.dims:
                    self.spans.append((base, esz, prefix + m.name))
                for k in range(n):
                    idx, r = [], k
                    for d in reversed(m.dims):
                        idx.insert(0, r % d)
                        r //= d
                    path = prefix + m.name + ''.join('[%d]' % i for i in idx)
                    self.claims.append((base + k * esz, esz, path))
                    if m.dims:
                        self.spans.append((base + k * esz, esz, path))

    # ---- lookup ---------------------------------------------------------------
    def field(self, addr):
        """(start address, size, name) of the field holding addr."""
        for gaddr, kind, plane, esz in RAW_GRIDS:
            if gaddr <= addr < gaddr + 10000 * esz:
                i, b = divmod(addr - gaddr, esz)
                y, x = divmod(i, 100)
                if kind == 'sim':
                    return addr, 1, 'plane[%d].sim[x=%d,y=%d].b[%d]' % (plane, x, y, b)
                return gaddr + i * esz, esz, 'plane[%d].%s[x=%d,y=%d]' % (plane, kind, x, y)
        k = bisect.bisect_right(self._span_addrs, addr) - 1
        # spans can nest (records inside a claimed array): take the smallest containing one
        best = None
        j = k
        while j >= 0 and j > k - 64:
            a, size, path = self.spans[j]
            if a <= addr < a + size and (best is None or size < best[1]):
                best = (a, size, path)
            j -= 1
        if best:
            return best
        k = bisect.bisect_right(self._sym_addrs, addr) - 1
        if k >= 0 and addr - self.syms[k][0] < 0x1000:
            a, n = self.syms[k]
            return addr, 1, n + ('+%#x' % (addr - a) if addr != a else '')
        return addr, 1, 'DAT_%08x' % addr

    def name(self, addr):
        """Best name for one byte address."""
        a, size, path = self.field(addr)
        return path + ('+%d' % (addr - a) if addr != a else '')
