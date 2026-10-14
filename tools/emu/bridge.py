# SPDX-License-Identifier: AGPL-3.0-or-later
"""State bridge between the emulated ALIFE.EXE and the port.

- orig_load(emu, data): runs the original's own loader, save_load_game
  (0x48f837), on an ALSV image, with the file API served from memory.
- LoadMap: from a traced orig_load, which file bytes the loader stores at
  which global address. Its addresses are "the saved globals".
- port_image(): the port's g_state in the original's address space: the
  port saves it (savegame_save_mem), the original's loader loads that into a
  scratch emulator, then the grids whose layout matches (map cells, object
  maps, sim cells, tile tables) and the current-plane globals are copied
  over raw, so unsaved bytes are compared too.
- diff(): compares two emulators' state regions byte by byte and names
  every difference (names.py).
"""
import struct

from alife_emu import Emu, EmuError, GLOBALS_LO, GLOBALS_HI, HEAP, HEAP_SIZE
from names import Names, RAW_GRIDS, TILE_INFO

SAVE_LOAD_GAME = 0x48f837
ARG_LOAD_FILE = 0x64dc64     # char[], the file save_load_game(.., 50000) opens
MAP_VIEW = 0x5bb120          # the main MapView (gfx/tile_draw.h layout)
PLANET_MAP_PTR, PLANET_MAP_SIZE = 0x66c7e4, 0x66c7e8

# What save_load_game calls that has no business in a headless load.
LOAD_STUBS = {
    0x408ca4: 'ui_busy_cursor', 0x475569: 'snd_stop (music)', 0x4c2830: 'wm_send',
    0x408c4d: 'views_refresh_all', 0x4bfc69: 'ctl_message_box', 0x4c8393: 'menu_set_checked',
}
# The derived-table rebuilds after loading, which the port doesn't have yet
# (world.h world_load; WP2). Stubbed unless orig_load(run_rebuilds=True).
LOAD_REBUILDS = {0x4506b6: 'recompute (WP2)', 0x452267: 'realm table reset (WP2)'}

# The "current plane" globals (map_view_select_plane 0x42b102) and how they
# read as plane numbers.
CUR_PTRS = {
    'cells_plane': (0x630518, {0x593270: 1, 0x5bb270: 2, 0x6265b0: 3}),
    'sim_plane': (0x57e014, {0x5fa670: 1, 0x5c6680: 3}),
    'realm_plane': (0x630428, {0x630520: 1, 0x5b2f70: 3}),
    'tile_info_plane': (0x5bb0a4, {0x5f7480: 1, 0x5c5c20: 3}),
    'objmap_plane': (MAP_VIEW + 4, {0x5f4cf0: 1, 0x5f25c0: 2, 0x5f7ee0: 3}),
}
CUR_BYTES = {'plane': MAP_VIEW + 0x47, 'water_entry_max': 0x65ed74, 'unk_65ed78': 0x65ed78,
             'road_entry': 0x65ed7c, 'objframe_base': 0x65ed80}
CUR_U32 = {'select_count': 0x63921c}


VIEW_FLAGS = 0xc0


def init_map_view(emu):
    """The main map view as the game sets it up: 100 x 100, zoom 2."""
    v = MAP_VIEW
    emu.w32(v + 0x10, 100)
    emu.w32(v + 0x14, 100)
    emu.w32(v + 0x28, 32)
    emu.w32(v + 0x2c, 16)
    emu.write(v + 0x31, struct.pack('<ii', 2, 0))
    emu.write(v + 0x39, struct.pack('<bbii', 1, 1, -1, -1))
    # view flags (+0x48) and their per-plane copies 0x65ed98 Hell / 0x65eda0
    # middle / 0x65ed9c Heaven: 0xc0 (zone detail + grid), the port's
    # default (game/build.c g_build.view_flags). With 0x40 clear the loader's
    # rebuild sets CELL_ALT stand-ins on zone cells.
    emu.w8(v + 0x48, VIEW_FLAGS)
    for a in (0x65ed98, 0x65eda0, 0x65ed9c):
        emu.w8(a, VIEW_FLAGS)


REALM_TILE_INFO_FILL = 0x452581   # fills the current realm's RealmTileInfo (new game)
SELECT_PLANE = 0x42b102           # map_view_select_plane(&map_view, plane)


def orig_init_tile_info(emu):
    """Fill both realms' tile tables like a new game does (0x452581 per
    realm), which a fresh emulator lacks; the load-time rebuild (0x452267)
    scales them. Call before orig_load (it also touches realm +0x8046,
    which loading overwrites)."""
    init_map_view(emu)
    for plane in (3, 1):
        emu.call(SELECT_PLANE, MAP_VIEW, plane)
        emu.call(REALM_TILE_INFO_FILL)


def stub_load_deps(emu, run_rebuilds=False):
    for a in LOAD_STUBS:
        emu.stub(a)
    for a in LOAD_REBUILDS:
        if run_rebuilds:
            emu.unhook(a)
        else:
            emu.stub(a)


class LoadMap:
    """file offset <-> global address, from a traced load."""

    def __init__(self, events, data):
        # A read into a local is attributed to the first later global write
        # of the same size and value (or one of the loader's few known
        # transforms of it, e.g. cell bit 7 cleared).
        # Older pending reads are dropped then: they were never stored
        # (header kind/timestamp/title, ...).
        self.spans = []          # (file_off, size, addr)
        self.transformed = []    # (file_off, size, addr): stored with a different value
        self.unstored = []       # (file_off, size)
        pending = []             # (file_off, size) read into a local, not stored yet
        for ev in events:
            if ev[0] == 'w':
                _, addr, size, value = ev
                if not pending:
                    continue
                want = (value & ((1 << 8 * size) - 1)).to_bytes(size, 'little') if size <= 8 else None
                hit = None
                for k, (off, n) in enumerate(pending):
                    if n == size and data[off:off + n] == want:
                        hit = k
                        break
                exact = hit is not None
                if hit is None and want is not None:
                    # the loader's known transforms: map cells lose bit 7,
                    # out-of-range bytes are clamped to 0 (realm +0x80ef)
                    hit = next((k for k, (off, n) in enumerate(pending) if n == size and (
                        want == bytes([data[off] & 0x7f]) + data[off + 1:off + n] or not any(want))), None)
                if hit is None:
                    continue
                off, n = pending[hit]
                self.unstored += pending[:hit]
                del pending[:hit + 1]
                self.spans.append((off, n, addr))
                if not exact:
                    self.transformed.append((off, n, addr))
                continue
            pos, n, dest = ev
            if n == 0:
                continue
            if dest is not None and (GLOBALS_LO <= dest < GLOBALS_HI or HEAP <= dest < HEAP + HEAP_SIZE):
                self.spans.append((pos, n, dest))
            else:
                pending.append((pos, n))
        self.unstored += pending
        self.off2addr = {}
        for off, n, addr in self.spans:
            for i in range(n):
                self.off2addr[off + i] = addr + i
        self.file_len = len(data)

    def global_addrs(self):
        """Every global byte the loader stores (heap excluded)."""
        return {a for a in self.off2addr.values() if GLOBALS_LO <= a < GLOBALS_HI}


def orig_load(emu, data, run_rebuilds=False, trace=False, name='EMULOAD.ALF'):
    """save_load_game(&map_view, 50000) on `data` (an ALSV image). Returns a
    LoadMap if trace, else None. Raises EmuError if the loader fails."""
    init_map_view(emu)
    stub_load_deps(emu, run_rebuilds)
    emu.add_file(name, data)
    emu.write(ARG_LOAD_FILE, name.encode() + b'\0')
    events = None
    if trace:
        from unicorn import UC_HOOK_MEM_WRITE
        events = []

        def on_write(mu, access, addr, size, value, _):
            if GLOBALS_LO <= addr < GLOBALS_HI or HEAP <= addr < HEAP + HEAP_SIZE:
                events.append(('w', addr, size, value))
        h = emu.mu.hook_add(UC_HOOK_MEM_WRITE, on_write)
        emu.read_log = events
    try:
        r = emu.call(SAVE_LOAD_GAME, MAP_VIEW, 50000)
    finally:
        if trace:
            emu.mu.hook_del(h)
            emu.read_log = None
    if r != 1:
        raise EmuError('save_load_game returned %d' % r)
    return LoadMap(events, data) if trace else None


# ---- the port in the original's address space -----------------------------------

def write_port_raw(emu, port):
    """Copy the parts of the port state whose layout is the original's into
    the emulator: grids, tile tables, current-plane globals, RNG."""
    for addr, kind, plane, esz in RAW_GRIDS:
        emu.write(addr, port.bytes(kind, plane))
    for addr, plane in TILE_INFO:
        emu.write(addr, port.bytes('tile_info', plane))
    wi = port.world_info()
    for key, (addr, vals) in CUR_PTRS.items():
        inv = {p: a for a, p in vals.items()}
        emu.w32(addr, inv.get(wi[key], 0))
    for key, addr in CUR_BYTES.items():
        emu.w8(addr, wi[key])
    for key, addr in CUR_U32.items():
        emu.w32(addr, wi[key])
    emu.rng_seed(port.rng_state())


class PortImage:
    """A scratch emulator that holds the port's state in the original's
    layout (see the module doc)."""

    def __init__(self, emu=None, version=7300):
        self.emu = emu or Emu(check_hash=False)
        init_map_view(self.emu)
        stub_load_deps(self.emu)
        self.base = self.emu.snapshot()
        self.version = version

    def load(self, port):
        self.emu.restore(self.base)
        orig_load(self.emu, port.save_mem(self.version))
        write_port_raw(self.emu, port)
        return self.emu


# ---- comparing -------------------------------------------------------------------

class Region:
    def __init__(self, addr, size, what):
        self.addr, self.size, self.what = addr, size, what


def merge_ranges(addrs):
    out = []
    for a in sorted(addrs):
        if out and out[-1][0] + out[-1][1] == a:
            out[-1][1] += 1
        else:
            out.append([a, 1])
    return [(a, n) for a, n in out]


def state_regions(loadmap):
    """Regions compared by diff(): the saved globals (from a LoadMap) plus
    the raw grids, tile tables, current-plane globals and the RNG."""
    raw = set()
    regs = []
    for addr, kind, plane, esz in RAW_GRIDS:
        regs.append(Region(addr, 10000 * esz, 'grid'))
        raw.update(range(addr, addr + 10000 * esz))
    for addr, plane in TILE_INFO:
        regs.append(Region(addr, 0x84 * 0x14, 'tile_info'))
        raw.update(range(addr, addr + 0x84 * 0x14))
    for a, n in merge_ranges(loadmap.global_addrs() - raw):
        regs.append(Region(a, n, 'saved'))
    for key, (addr, _) in CUR_PTRS.items():
        regs.append(Region(addr, 4, 'cur:' + key))
    for key, addr in CUR_BYTES.items():
        regs.append(Region(addr, 1, 'cur:' + key))
    for key, addr in CUR_U32.items():
        regs.append(Region(addr, 4, 'cur:' + key))
    regs.append(Region(Emu.RAND_STATE, 4, 'rng'))
    return regs


class Difference:
    def __init__(self, name, addr, a, b):
        self.name, self.addr, self.a, self.b = name, addr, a, b

    def __str__(self):
        def val(bs):
            if len(bs) in (1, 2, 4):
                v = int.from_bytes(bs, 'little')
                return '%d (%#x)' % (v, v)
            return bs.hex()
        return '%-44s @%#x  orig %s  port %s' % (self.name, self.addr, val(self.a), val(self.b))


def diff(emu_a, emu_b, regions, names, ignore=(), planet_map=True):
    """Differences between two emulators over `regions`, merged per named
    field. ignore: names (or name prefixes ending in '.') to skip."""
    raw = []
    for r in regions:
        a = emu_a.read(r.addr, r.size)
        b = emu_b.read(r.addr, r.size)
        if a == b:
            continue
        for i in range(r.size):
            if a[i] != b[i]:
                raw.append(r.addr + i)
    out = _group(raw, emu_a, emu_b, names, ignore)
    if planet_map:
        ma = _planet_map(emu_a)
        mb = _planet_map(emu_b)
        if ma != mb:
            if len(ma) != len(mb):
                out.append(Difference('planet.map (size)', PLANET_MAP_SIZE,
                                      len(ma).to_bytes(4, 'little'), len(mb).to_bytes(4, 'little')))
            else:
                first = next(i for i in range(len(ma)) if ma[i] != mb[i])
                n = sum(1 for i in range(len(ma)) if ma[i] != mb[i])
                out.append(Difference('planet.map[%d] (%d bytes differ)' % (first, n), PLANET_MAP_PTR,
                                      ma[first:first + 1], mb[first:first + 1]))
    return out


def _planet_map(emu):
    p, n = emu.u32(PLANET_MAP_PTR), emu.u32(PLANET_MAP_SIZE)
    return emu.read(p, n) if p and n else b''


def _group(addrs, emu_a, emu_b, names, ignore):
    out = []
    last_field = None
    for a in addrs:
        start, size, base = names.field(a)
        if (start, base) == last_field:
            continue
        last_field = (start, base)
        if any(base == i or (i[-1] in '.[' and base.startswith(i)) for i in ignore):
            continue
        out.append(Difference(base, start, emu_a.read(start, size), emu_b.read(start, size)))
    return out


def describe_writes(written, regions, names, limit=40):
    """Named ranges of `written` (a set of byte addresses, from
    emu.track_writes()) that no region covers."""
    covered = set()
    for r in regions:
        covered.update(range(r.addr, r.addr + r.size))
    rest = merge_ranges(set(written) - covered)
    return ['%s (%d bytes @%#x)' % (names.name(a), n, a) for a, n in rest[:limit]], len(rest)
