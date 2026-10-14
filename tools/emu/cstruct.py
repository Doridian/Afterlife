# SPDX-License-Identifier: AGPL-3.0-or-later
"""A small parser for the struct definitions in src/game/state.h (and
world.h): enough to lay a struct out both the C way (natural alignment, for
reading the port's GameState through ctypes) and packed (the original's
records), and to read the address / offset each field's name or comment
claims.

Only what those headers use is supported: typedef struct { ... } Name;
with integer, char, pointer and struct members, multi-dimensional arrays,
several declarators per line and #define'd array sizes.
"""
import re

PRIM = {  # name: (size, signed)
    'uint8_t': (1, False), 'int8_t': (1, True), 'char': (1, True), 'bool': (1, False),
    'uint16_t': (2, False), 'int16_t': (2, True),
    'uint32_t': (4, False), 'int32_t': (4, True), 'int': (4, True), 'unsigned': (4, False),
    'uint64_t': (8, False), 'int64_t': (8, True), 'size_t': (8, False),
}
PTR_SIZE = 8


class Member:
    __slots__ = ('name', 'type', 'dims', 'ptr', 'comment', 'line', 'idx', 'nsib')

    def __init__(self, name, type_, dims, ptr, comment, line):
        self.name, self.type, self.dims, self.ptr = name, type_, dims, ptr
        self.comment, self.line = comment, line
        self.idx, self.nsib = 0, 1     # declarator index in its statement, count


class Leaf:
    """One scalar in a flattened struct."""
    __slots__ = ('path', 'size', 'signed', 'off', 'poff', 'member', 'top')

    def __init__(self, path, size, signed, off, poff, member, top):
        self.path, self.size, self.signed, self.off, self.poff = path, size, signed, off, poff
        self.member, self.top = member, top   # top: the top-level member of the flattened struct

    def __repr__(self):
        return '<%s +%#x/%#x %d>' % (self.path, self.off, self.poff, self.size)


class Headers:
    def __init__(self, *paths):
        self.defines = {}
        self.structs = {}      # name -> [Member]
        for p in paths:
            self._parse(open(p).read())
        self._layout = {}

    def _parse(self, text):
        for m in re.finditer(r'^#define\s+(\w+)\s+(.+?)\s*$', text, re.M):
            v = m.group(2).split('/*')[0].strip()
            try:
                self.defines[m.group(1)] = self.eval(v)
            except Exception:
                pass
        # typedef struct [Tag] { body } Name;
        for m in re.finditer(r'typedef\s+struct\s*\w*\s*\{(.*?)\}\s*(\w+)\s*;', text, re.S):
            body, name = m.group(1), m.group(2)
            line0 = text.count('\n', 0, m.start(1)) + 1
            self.structs[name] = self._members(body, line0)

    def eval(self, expr):
        expr = expr.strip()
        for k, v in self.defines.items():
            expr = re.sub(r'\b%s\b' % k, str(v), expr)
        if not re.fullmatch(r'[0-9a-fA-FxX\s()+*/-]+', expr):
            raise ValueError(expr)
        return int(eval(expr))

    def _members(self, body, line0):
        out = []
        # Split into statements; keep the comment that follows each one
        # (same line) or precedes it (block comment above).
        pos = 0
        pending_comment = ''
        tokens = re.finditer(r'/\*(.*?)\*/|([^;/]+;)', body, re.S)
        last_stmt = None
        for t in tokens:
            if t.group(1) is not None:
                c = ' '.join(t.group(1).split())
                # trailing comment on the statement's line?
                if last_stmt is not None and '\n' not in body[last_stmt[1]:t.start()]:
                    for mm in last_stmt[0]:
                        mm.comment = (mm.comment + ' ' + c).strip()
                else:
                    pending_comment = c
                continue
            stmt = ' '.join(t.group(2).split()).rstrip(';').strip()
            line = line0 + body.count('\n', 0, t.start(2) + len(t.group(2)) - len(t.group(2).lstrip()))
            mm = self._decl(stmt, pending_comment, line)
            pending_comment = ''
            out.extend(mm)
            last_stmt = (mm, t.end())
        return out

    def _decl(self, stmt, comment, line):
        m = re.match(r'((?:const\s+)?(?:struct\s+)?\w+)\s+(.*)$', stmt)
        if not m:
            return []
        type_ = m.group(1).replace('const ', '').replace('struct ', '')
        out = []
        for d in m.group(2).split(','):
            d = d.strip()
            ptr = d.startswith('*') or d.startswith('(')
            if d.startswith('('):   # function pointer
                name = re.search(r'\(\s*\*\s*(\w+)', d).group(1)
                dims = []
            else:
                dm = re.match(r'\**\s*(\w+)\s*((?:\[[^\]]+\])*)', d)
                name = dm.group(1)
                dims = [self.eval(x) for x in re.findall(r'\[([^\]]+)\]', dm.group(2))]
            out.append(Member(name, type_, dims, ptr, comment, line))
        for i, mm in enumerate(out):
            mm.idx, mm.nsib = i, len(out)
        return out

    # ---- layout -----------------------------------------------------------
    def type_info(self, t, ptr=False):
        """(size, align, packed size) of a type."""
        if ptr:
            return PTR_SIZE, PTR_SIZE, 4
        if t in PRIM:
            s = PRIM[t][0]
            return s, s, s
        if t in self.structs:
            lay = self.layout(t)
            return lay['size'], lay['align'], lay['psize']
        raise KeyError('unknown type %s' % t)

    def layout(self, name):
        if name in self._layout:
            return self._layout[name]
        off = poff = 0
        align = 1
        fields = []
        for m in self.structs[name]:
            s, a, ps = self.type_info(m.type, m.ptr)
            n = 1
            for d in m.dims:
                n *= d
            off = (off + a - 1) // a * a
            fields.append((m, off, poff, s, ps, n))
            off += s * n
            poff += ps * n
            align = max(align, a)
        size = (off + align - 1) // align * align
        lay = {'fields': fields, 'size': size, 'align': align, 'psize': poff}
        self._layout[name] = lay
        return lay

    def flatten(self, name, prefix='', off=0, poff=0, top=None, max_elems=None):
        """Yield a Leaf per scalar (arrays expanded; nested structs too).
        max_elems: expand at most that many elements per array dimension."""
        for m, o, po, s, ps, n in self.layout(name)['fields']:
            idxs = [()]
            for d in m.dims:
                rng = range(d) if max_elems is None or d <= max_elems else sorted({0, d - 1})
                idxs = [i + (j,) for i in idxs for j in rng]
            strides = []
            acc = 1
            for d in reversed(m.dims):
                strides.insert(0, acc)
                acc *= d
            for ix in idxs:
                k = sum(a * b for a, b in zip(ix, strides))
                path = prefix + m.name + ''.join('[%d]' % i for i in ix)
                t = top or m
                if m.ptr or m.type in PRIM:
                    size = PTR_SIZE if m.ptr else s
                    yield Leaf(path, size, (not m.ptr) and PRIM[m.type][1], off + o + k * s,
                               poff + po + k * ps, m, t)
                else:
                    yield from self.flatten(m.type, path + '.', off + o + k * s, poff + po + k * ps, t,
                                            max_elems)


ADDR_RE = re.compile(r'0x([0-9a-fA-F]{6})\b')
UNK_RE = re.compile(r'^unk_([0-9a-fA-F]+)$')


def claimed_global(member):
    """Addresses a GameState member claims: from its name (unk_5bb210) or its
    comment ("0x630438 (hi), 0x63043c (lo)"). Returns a list."""
    m = UNK_RE.match(member.name)
    if m and len(m.group(1)) >= 6:
        return [int(m.group(1), 16)]
    return addrs_in(member.comment)


def addrs_in(comment):
    """Global addresses (0x4f0000..0x67ffff) mentioned in a comment."""
    return [int(a, 16) for a in ADDR_RE.findall(comment or '') if 0x4f0000 <= int(a, 16) < 0x680000]


def claimed_offset(member):
    """Record offset a member of a packed record claims (unk_7e48, or a
    "+0x7fec" comment), or None."""
    m = UNK_RE.match(member.name)
    if m and len(m.group(1)) < 6:
        return int(m.group(1), 16)
    lead = re.match(r'((?:\+0x[0-9a-fA-F]+(?:\.\.\+0x[0-9a-fA-F]+)?\s*,?\s*)+)', member.comment or '')
    if not lead:
        return None
    offs = [int(x, 16) for x in re.findall(r'(?<![.])\+0x([0-9a-fA-F]+)', lead.group(1))]
    if len(offs) == member.nsib:
        return offs[member.idx]
    if member.idx == 0 and offs:
        return offs[0]
    return None
