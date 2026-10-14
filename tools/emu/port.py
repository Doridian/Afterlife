# SPDX-License-Identifier: AGPL-3.0-or-later
"""The port (src/) as a shared library, through ctypes.

Port() builds the CMake target afterlife_port in build/emu (configured on
first use, Debug so asserts are on) and loads libafterlife_port.so. Every
non-static C function of the port is reachable as port.lib.<name>; the
helpers below cover state access (tools/emu/port_shim.c).

One library instance per process: g_state/g_world are process globals.
"""
import ctypes
import fcntl
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
BUILD = os.environ.get('AFTERLIFE_EMU_BUILD', os.path.join(REPO, 'build', 'emu'))

_lib_cache = {}


def build(build_dir=BUILD, quiet=True):
    """Configure (once) and build afterlife_port; returns the .so path.
    Serialized with flock so concurrent agents don't trip over each other."""
    os.makedirs(build_dir, exist_ok=True)
    with open(os.path.join(build_dir, '.emu-build.lock'), 'w') as lk:
        fcntl.flock(lk, fcntl.LOCK_EX)
        out = None if not quiet else subprocess.PIPE
        if not os.path.exists(os.path.join(build_dir, 'CMakeCache.txt')):
            r = subprocess.run(['cmake', '-S', REPO, '-B', build_dir, '-DCMAKE_BUILD_TYPE=Debug'],
                               stdout=out, stderr=subprocess.STDOUT, text=True)
            if r.returncode:
                sys.exit('cmake configure failed:\n%s' % (r.stdout or ''))
        r = subprocess.run(['cmake', '--build', build_dir, '--target', 'afterlife_port',
                            '-j%d' % (os.cpu_count() or 4)], stdout=out, stderr=subprocess.STDOUT, text=True)
        so = os.path.join(build_dir, 'libafterlife_port.so')
        if r.returncode:
            tail = '\n'.join((r.stdout or '').splitlines()[-15:])
            if not os.path.exists(so) or os.environ.get('AFTERLIFE_EMU_STRICT'):
                sys.exit('building afterlife_port failed:\n%s' % tail)
            print('WARNING: building afterlife_port failed, using the STALE %s\n%s' % (so, tail),
                  file=sys.stderr)
    return so


class Port:
    def __init__(self, lib=None, rebuild=True):
        lib = lib or os.environ.get('AFTERLIFE_PORT_LIB')
        if not lib:
            lib = build() if rebuild else os.path.join(BUILD, 'libafterlife_port.so')
        lib = os.path.abspath(lib)
        if lib not in _lib_cache:
            _lib_cache[lib] = ctypes.CDLL(lib)
        self.lib = L = _lib_cache[lib]
        c = ctypes
        L.shim_ptr.restype = c.c_void_p
        L.shim_ptr.argtypes = [c.c_char_p, c.c_int, c.POINTER(c.c_size_t)]
        L.shim_save_mem.restype = c.POINTER(c.c_uint8)
        L.shim_save_mem.argtypes = [c.c_int, c.POINTER(c.c_size_t)]
        L.shim_free.argtypes = [c.c_void_p]
        L.shim_world_load.argtypes = [c.c_char_p, c.c_char_p, c.c_int]
        L.shim_world_load_mem.argtypes = [c.c_char_p, c.c_size_t, c.c_char_p, c.c_int]
        L.rng_seed.argtypes = [c.c_uint32]
        L.rng_state.restype = c.c_uint32
        for n in ('shim_cell_get', 'shim_cell_test_bracket', 'shim_cell_test_shade', 'shim_cell_test_overlay',
                  'shim_sim_perm', 'shim_sim_temp', 'shim_sim_pop', 'rng_state'):
            getattr(L, n).restype = c.c_uint32

    # ---- state -------------------------------------------------------------
    def world_load(self, path):
        err = ctypes.create_string_buffer(256)
        if not self.lib.shim_world_load(path.encode(), err, 256):
            raise RuntimeError('world_load(%s): %s' % (path, err.value.decode()))

    def world_load_mem(self, data):
        err = ctypes.create_string_buffer(256)
        if not self.lib.shim_world_load_mem(bytes(data), len(data), err, 256):
            raise RuntimeError('world_load_mem: %s' % err.value.decode())

    def save_mem(self, version=0):
        """g_state as an ALSV image (format `version`, 0 = its own)."""
        n = ctypes.c_size_t()
        p = self.lib.shim_save_mem(version, ctypes.byref(n))
        if not p:
            raise RuntimeError('savegame_save_mem failed')
        data = ctypes.string_at(p, n.value)
        self.lib.shim_free(p)
        return data

    def ptr(self, what, plane=0):
        """(address, size) of a part of the state (see shim_ptr)."""
        n = ctypes.c_size_t()
        a = self.lib.shim_ptr(what.encode(), plane, ctypes.byref(n))
        if not a:
            raise KeyError(what)
        return a, n.value

    def bytes(self, what, plane=0):
        a, n = self.ptr(what, plane)
        return ctypes.string_at(a, n)

    def set_bytes(self, what, plane, data):
        a, n = self.ptr(what, plane)
        assert len(data) == n
        ctypes.memmove(a, bytes(data), n)

    def world_info(self):
        out = (ctypes.c_int32 * 11)()
        self.lib.shim_world_info(out)
        keys = ('plane', 'cells_plane', 'sim_plane', 'realm_plane', 'tile_info_plane', 'water_entry_max',
                'unk_65ed78', 'road_entry', 'objframe_base', 'select_count', 'objmap_plane')
        return dict(zip(keys, out))

    # ---- RNG (util.h) ---------------------------------------------------------
    def rng_seed(self, s):
        self.lib.rng_seed(s & 0xffffffff)

    def rng_state(self):
        return self.lib.rng_state()

    def fn(self, name, restype=ctypes.c_int, argtypes=None):
        """A port function with types set, e.g. port.fn('rand_range')(1, 6)."""
        f = getattr(self.lib, name)
        f.restype = restype
        if argtypes is not None:
            f.argtypes = argtypes
        return f
