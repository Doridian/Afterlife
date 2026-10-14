# SPDX-License-Identifier: AGPL-3.0-or-later
"""Differential tests: the original function (emulated) against the port's.

    from difftest import Harness
    h = Harness()                        # builds/loads the port, maps ALIFE.EXE
    h.load('orig/ASCEN001.ASC')          # both sides: orig save_load_game / port world_load
    h.seed(12345)                        # both RNGs
    h.emu.stub(0x4c2830)                 # stub what the original calls that isn't sim
    r1 = h.orig(0x42b4a0, 5, 7, 14)      # map_cell_set_tileset(5, 7, 14), cdecl
    r2 = h.port('shim_cell_set_tileset', 5, 7, 14)
    h.check()                            # raises AssertionError listing differing globals

h.diff() returns the differences instead (list of bridge.Difference), and
h.uncompared_writes() lists globals the original wrote that the comparison
doesn't cover (state the port has no field for yet, or runtime-only state).
"""
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from alife_emu import Emu, EmuError, ORIG  # noqa: E402,F401
import bridge  # noqa: E402
from names import Names  # noqa: E402
from port import Port  # noqa: E402

SAVES = ['ASCEN001.ASC', 'ASCEN002.ASC', 'ASCEN003.ASC', 'ASCEN004.ASC']


def orig_path(name):
    """Path of a file in the user's orig/ directory (case-insensitive)."""
    for f in os.listdir(ORIG):
        if f.lower() == name.lower():
            return os.path.join(ORIG, f)
    raise FileNotFoundError(os.path.join(ORIG, name))


class Harness:
    """One emulator (the original) and the port library, kept in step."""

    def __init__(self, port=None, version=7300, run_rebuilds=True, ignore=()):
        """run_rebuilds: let the original run its post-load table rebuilds
        (0x4506b6 zone stand-ins, 0x452267 cost scaling) like the port's
        world_load does (game/newworld.c build_after_load); the tile tables
        are filled first (bridge.orig_init_tile_info). False stubs them."""
        self.port_ = port or Port()
        self.names = Names()
        self.emu = Emu()
        bridge.init_map_view(self.emu)
        bridge.stub_load_deps(self.emu, run_rebuilds)
        if run_rebuilds:
            bridge.orig_init_tile_info(self.emu)
        self.fresh = self.emu.snapshot()
        self.image = bridge.PortImage(version=version)
        self.version = version
        self.run_rebuilds = run_rebuilds
        # select_count counts map_view_select_plane calls; the port's loader
        # makes 3 where the original makes 6 (it selects each plane while
        # reading it). Nothing reads it but sim_update, which resets it.
        self.ignore = ['plane_select_count'] + list(ignore)
        self.regions = None
        self.loadmap = None
        self._written = None
        self.loaded = None

    # ---- state ---------------------------------------------------------------
    def load(self, path):
        """Load an ALSV file into both sides (path, or a name in orig/)."""
        if not os.path.exists(path):
            path = orig_path(path)
        data = open(path, 'rb').read()
        self.emu.restore(self.fresh)
        bridge.orig_load(self.emu, data, run_rebuilds=self.run_rebuilds)
        self.port_.world_load(path)
        if self.regions is None:
            # The saved-globals map comes from tracing the original loader
            # on the port's save of this state at `version` (7300: every
            # field present).
            self.image.emu.restore(self.image.base)
            self.loadmap = bridge.orig_load(self.image.emu, self.port_.save_mem(self.version), trace=True)
            self.regions = bridge.state_regions(self.loadmap)
        self.loaded = path
        self.port_.rng_seed(self.emu.rng_state())
        self.snap = self.emu.snapshot()
        self._written = None

    def reset(self):
        """Back to the state right after load() on both sides."""
        self.emu.restore(self.snap)
        self.port_.world_load(self.loaded)
        self.port_.rng_seed(self.emu.rng_state())
        self._written = None

    def seed(self, state):
        """Same raw RNG state on both sides (MSVC rand / util.h rng)."""
        self.emu.rng_seed(state)
        self.port_.rng_seed(state)

    # ---- calls -----------------------------------------------------------------
    def orig(self, addr, *args, track=True):
        """Call the original (cdecl, int args); returns eax as signed int.
        Writes to globals are tracked for uncompared_writes()."""
        w = self.emu.track_writes() if track else None
        try:
            return self.emu.call(addr, *args)
        finally:
            if track:
                self.emu.stop_tracking()
                self._written = (self._written or set()) | w

    def port(self, name, *args, restype=ctypes.c_int, argtypes=None):
        """Call a port function by its C name (ints by default)."""
        return self.port_.fn(name, restype, argtypes)(*args)

    # ---- comparing ----------------------------------------------------------------
    def diff(self, ignore=()):
        img = self.image.load(self.port_)
        return bridge.diff(self.emu, img, self.regions, self.names, ignore=self.ignore + list(ignore))

    def check(self, ignore=(), limit=40, what=''):
        d = self.diff(ignore)
        if d:
            lines = ['%s: %d differences between the original and the port' % (what or 'state', len(d))]
            lines += ['  ' + str(x) for x in d[:limit]]
            if len(d) > limit:
                lines.append('  ...')
            raise AssertionError('\n'.join(lines))

    def uncompared_writes(self, limit=40):
        """Globals the original wrote (since load()/the last call to this)
        that diff() doesn't compare. Returns (lines, total ranges)."""
        w = self._written or set()
        self._written = None
        return bridge.describe_writes(w, self.regions or [], self.names, limit)
