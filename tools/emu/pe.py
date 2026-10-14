# SPDX-License-Identifier: AGPL-3.0-or-later
"""Minimal PE32 reader: sections, imports, relocations (no dependencies)."""
import struct


class Section:
    def __init__(self, name, va, vsize, raw_off, raw_size, chars):
        self.name, self.va, self.vsize = name, va, vsize
        self.raw_off, self.raw_size, self.chars = raw_off, raw_size, chars


class PE:
    def __init__(self, path):
        self.data = d = open(path, 'rb').read()
        if d[:2] != b'MZ':
            raise ValueError('%s: not an MZ executable' % path)
        nt = struct.unpack_from('<I', d, 0x3c)[0]
        if d[nt:nt + 4] != b'PE\0\0':
            raise ValueError('%s: not a PE executable' % path)
        nsec, = struct.unpack_from('<H', d, nt + 6)
        optsz, = struct.unpack_from('<H', d, nt + 20)
        opt = nt + 24
        self.entry, = struct.unpack_from('<I', d, opt + 16)
        self.image_base, = struct.unpack_from('<I', d, opt + 28)
        self.image_size, = struct.unpack_from('<I', d, opt + 56)
        self.header_size, = struct.unpack_from('<I', d, opt + 60)
        ndirs, = struct.unpack_from('<I', d, opt + 92)
        self.dirs = [struct.unpack_from('<II', d, opt + 96 + 8 * i) for i in range(ndirs)]
        self.sections = []
        so = opt + optsz
        for i in range(nsec):
            name = d[so + 40 * i: so + 40 * i + 8].rstrip(b'\0').decode('latin1')
            vsize, va, rsize, roff = struct.unpack_from('<IIII', d, so + 40 * i + 8)
            chars, = struct.unpack_from('<I', d, so + 40 * i + 36)
            self.sections.append(Section(name, va, vsize, roff, rsize, chars))

    def rva_to_off(self, rva):
        for s in self.sections:
            if s.va <= rva < s.va + max(s.vsize, s.raw_size):
                if rva - s.va >= s.raw_size:
                    return None
                return s.raw_off + rva - s.va
        if rva < self.header_size:
            return rva
        return None

    def u32(self, rva):
        return struct.unpack_from('<I', self.data, self.rva_to_off(rva))[0]

    def cstr(self, rva):
        o = self.rva_to_off(rva)
        return self.data[o:self.data.index(b'\0', o)].decode('latin1')

    def imports(self):
        """[(dll, name_or_ordinal, iat_va)]"""
        rva, size = self.dirs[1]
        out = []
        while rva and True:
            ilt, _, _, name_rva, iat = struct.unpack_from('<IIIII', self.data, self.rva_to_off(rva))
            if not name_rva:
                break
            dll = self.cstr(name_rva).upper()
            thunks = ilt or iat
            i = 0
            while True:
                t = self.u32(thunks + 4 * i)
                if not t:
                    break
                name = ('#%d' % (t & 0xffff)) if t & 0x80000000 else self.cstr(t + 2)
                out.append((dll, name, self.image_base + iat + 4 * i))
                i += 1
            rva += 20
        return out

    def relocations(self):
        """[va] of HIGHLOW fixups."""
        rva, size = self.dirs[5] if len(self.dirs) > 5 else (0, 0)
        out, end = [], rva + size
        while rva < end:
            page, bsize = struct.unpack_from('<II', self.data, self.rva_to_off(rva))
            if bsize < 8:
                break
            for k in range((bsize - 8) // 2):
                e, = struct.unpack_from('<H', self.data, self.rva_to_off(rva + 8 + 2 * k))
                if e >> 12 == 3:
                    out.append(self.image_base + page + (e & 0xfff))
            rva += bsize
        return out
