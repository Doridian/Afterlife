# SPDX-License-Identifier: AGPL-3.0-or-later
"""ALIFE.EXE under the unicorn x86-32 emulator, for differential tests.

The executable is loaded from the user's orig/ directory at runtime (never
commit it). Its sections are mapped at the image base (0x400000, the preferred
base, so no relocation is needed), .bss is zero, and there is a stack, a bump
heap and a page of import stubs. Nothing of the CRT startup runs: the parts of
the statically linked MSVC CRT that need an initialised runtime (malloc, the
stdio FILE layer) are replaced by Python implementations ("HLE"), and every
Win32 import goes to a Python stub. An import without a stub, an unmapped
memory access or a runaway loop raises EmuError with a backtrace.

    emu = Emu()                          # orig/ALIFE.EXE
    emu.stub(0x408ca4)                   # ui_busy_cursor: return 0, do nothing
    emu.hook(0x42b088, lambda e, a: 4)   # replace rand_range by "4"
    r = emu.call(0x42b088, 1, 6)         # cdecl call, returns eax (signed)

See tools/emu/README.md.
"""
import os
import struct
import sys

try:
    from unicorn import (Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE,
                         UC_HOOK_MEM_INVALID, UC_HOOK_MEM_WRITE, UC_PROT_ALL)
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX,
                                   UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESI,
                                   UC_X86_REG_ESP)
except ImportError:  # pragma: no cover
    sys.exit('tools/emu needs the unicorn module: pip install -r tools/emu/requirements.txt')

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe import PE  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ORIG = os.environ.get('AFTERLIFE_ORIG', os.path.join(REPO, 'orig'))

# sha256 of the retail ALIFE.EXE (docs/binaries.md)
ALIFE_SHA256 = '6f034f66c28c06f66e553baee90c9d5d91a98de54399309457b65e05df9e0842'

HEAP, HEAP_SIZE = 0x10000000, 0x04000000
STACK, STACK_SIZE = 0x20000000, 0x00100000
STUBS, STUBS_SIZE = 0x30000000, 0x00010000   # import thunks, 4 bytes each
RET_MAGIC = STUBS + STUBS_SIZE - 0x10          # return address of emu.call
FAKE_HANDLE = 0x7e000000

# .data/.bss/.rdata: where the game's globals live
GLOBALS_LO, GLOBALS_HI = 0x4f3000, 0x678000

# Statically linked MSVC CRT pieces replaced by Python (cdecl).
CRT = {
    'malloc': 0x4b4de9, 'free': 0x4b5e8e,
    'fopen': 0x4eff22, 'fclose': 0x4b89cd, 'fseek': 0x4eff35, 'ftell': 0x4f0255,
    'fread': 0x4effc9, 'fwrite': 0x4f0100, 'fgets': 0x4f038d,
    'fgetc': 0x4f03e8, '_filbuf': 0x4f197d, 'fputc': 0x4f0409, '_flsbuf': 0x4b32b6,
}
# The game's file layer (C:\\VHUMOR file_*), on top of the CRT; replaced
# too so each read is one hook (file_read also pumps messages).
FILE_LAYER = {'file_read': 0x4d2117, 'file_read_u8': 0x4d2307, 'file_read_u16': 0x4d22de,
              'file_read_u32': 0x4d22b6, 'file_write': 0x4d21db, 'file_write_u8': 0x4d2371,
              'file_write_u16': 0x4d2349, 'file_write_u32': 0x4d2323}
# The game's own allocators, also on the bump heap (their pools need init).
GAME_ALLOC = {'he_malloc': 0x4c9d8e, 'he_free': 0x4c9e18,
              'vh_mem_alloc': 0x4d8230, 'vh_mem_free': 0x4d8410}


class EmuError(Exception):
    pass


def s32(v):
    v &= 0xffffffff
    return v - 0x100000000 if v & 0x80000000 else v


class _Args:
    """args(i): the i-th 32-bit stack argument of a hooked call."""
    __slots__ = ('emu', 'esp', 'ra', 'first')

    def __init__(self, emu, esp):
        self.emu, self.esp = emu, esp
        w = struct.unpack('<7I', emu.mu.mem_read(esp, 28))
        self.ra, self.first = w[0], w[1:]

    def __call__(self, i):
        return self.first[i] if i < 6 else self.emu.u32(self.esp + 4 + 4 * i)


class VFile:
    def __init__(self, name, data, writing):
        self.name, self.data, self.pos, self.writing = name, bytearray(data), 0, writing


class Emu:
    """One emulated ALIFE.EXE process image."""

    def __init__(self, exe=None, trace_imports=False, check_hash=True):
        exe = exe or os.path.join(ORIG, 'ALIFE.EXE')
        if not os.path.exists(exe):
            raise EmuError('%s not found: extract your CD into orig/ (tools/extract_cd.py) '
                           'or set AFTERLIFE_ORIG' % exe)
        self.pe = pe = PE(exe)
        if check_hash:
            import hashlib
            h = hashlib.sha256(pe.data).hexdigest()
            if h != ALIFE_SHA256:
                print('warning: %s is not the retail ALIFE.EXE (sha256 %s)' % (exe, h), file=sys.stderr)
        self.trace_imports = trace_imports
        self.mu = mu = Uc(UC_ARCH_X86, UC_MODE_32)
        base = pe.image_base
        mu.mem_map(base, (pe.image_size + 0xfff) & ~0xfff, UC_PROT_ALL)
        mu.mem_write(base, pe.data[:pe.header_size])
        for s in pe.sections:
            n = min(s.raw_size, s.vsize or s.raw_size)
            if n:
                mu.mem_write(base + s.va, pe.data[s.raw_off:s.raw_off + n])
        mu.mem_map(HEAP, HEAP_SIZE)
        mu.mem_map(STACK, STACK_SIZE)
        mu.mem_map(STUBS, STUBS_SIZE)
        mu.mem_write(STUBS, b'\xc3' * STUBS_SIZE)
        self.heap_next = HEAP + 0x1000
        self.allocs = {}            # addr -> size (live)
        self.files = {}             # lower-case basename -> bytes (virtual files)
        self.fs_root = ORIG         # read-only host fallback for fopen(.., "r")
        self.open_files = {}        # FILE* -> VFile
        self.written = {}           # basename -> bytes of files closed after writing
        self.read_log = None        # list of (pos, size, dest or None) while tracing
        self.import_log = []
        self._hooks = {}            # addr -> (fn, stdcall arg bytes or None)
        self._uc_hooked = set()     # addrs with a unicorn code hook (never removed)
        self._imports = {}          # stub addr -> (dll, name)
        self._import_impl = {}      # name -> (nargs, fn)
        self._err = None
        self._write_hook = None
        self.insn_limit = 2_000_000_000
        self._setup_imports()
        self._setup_crt()
        mu.hook_add(UC_HOOK_CODE, self._on_code_stub, begin=STUBS, end=STUBS + STUBS_SIZE - 1)
        mu.hook_add(UC_HOOK_MEM_INVALID, self._on_invalid)

    # ---- memory ----------------------------------------------------------
    def read(self, addr, n):
        return bytes(self.mu.mem_read(addr, n))

    def write(self, addr, data):
        self.mu.mem_write(addr, bytes(data))

    def u8(self, a): return self.read(a, 1)[0]
    def u16(self, a): return struct.unpack('<H', self.read(a, 2))[0]
    def u32(self, a): return struct.unpack('<I', self.read(a, 4))[0]
    def i32(self, a): return struct.unpack('<i', self.read(a, 4))[0]
    def w8(self, a, v): self.write(a, struct.pack('<B', v & 0xff))
    def w16(self, a, v): self.write(a, struct.pack('<H', v & 0xffff))
    def w32(self, a, v): self.write(a, struct.pack('<I', v & 0xffffffff))

    def cstr(self, a, maxlen=4096):
        b = self.read(a, maxlen)
        return b[:b.index(0)].decode('latin1') if 0 in b else b.decode('latin1')

    def malloc(self, n, zero=True):
        n = max(int(n), 1)
        a = (self.heap_next + 15) & ~15
        if a + n > HEAP + HEAP_SIZE:
            raise EmuError('emulated heap exhausted (%d bytes requested)' % n)
        self.heap_next = a + n
        self.allocs[a] = n
        if zero:
            self.write(a, bytes(n))
        return a

    def alloc_bytes(self, data):
        a = self.malloc(len(data) + 1)
        self.write(a, bytes(data) + b'\0')
        return a

    def alloc_str(self, s):
        return self.alloc_bytes(s.encode('latin1'))

    # ---- RNG (MSVC rand at 0x4b1202) --------------------------------------
    RAND_STATE = 0x670e84   # rand_holdrand

    def rng_seed(self, state):
        self.w32(self.RAND_STATE, state)

    def rng_state(self):
        return self.u32(self.RAND_STATE)

    # ---- hooks -------------------------------------------------------------
    def hook(self, addr, fn, stdcall_args=None):
        """Replace the function at addr: fn(emu, args) runs instead and its
        return value (int or None) goes to eax. args(i) reads the i-th
        argument (0-based, 32-bit). cdecl unless stdcall_args (bytes popped)."""
        self._hooks[addr] = (fn, stdcall_args)
        # One unicorn hook per address, even after unhook(): a second one
        # would run fn again after the first returned, popping the caller's
        # frame twice (a stub re-added after unhook returned to garbage).
        if addr not in self._uc_hooked:
            self._uc_hooked.add(addr)
            self.mu.hook_add(UC_HOOK_CODE, self._on_code_hook, begin=addr, end=addr)

    def stub(self, addr, ret=0, log=None):
        """Make the function at addr return `ret` without running it.
        log: a list that gets (addr, first four args) per call."""
        def f(emu, args):
            if log is not None:
                log.append((addr, [args(i) for i in range(4)]))
            return ret
        self.hook(addr, f)

    def unhook(self, addr):
        self._hooks.pop(addr, None)   # the unicorn hook stays but does nothing

    def _ret(self, value, pop, esp=None, ra=None):
        mu = self.mu
        if esp is None:
            esp = mu.reg_read(UC_X86_REG_ESP)
            ra = struct.unpack('<I', mu.mem_read(esp, 4))[0]
        mu.reg_write(UC_X86_REG_ESP, esp + 4 + pop)
        if value is not None:
            mu.reg_write(UC_X86_REG_EAX, value & 0xffffffff)
        mu.reg_write(UC_X86_REG_EIP, ra)

    def _args(self):
        esp = self.mu.reg_read(UC_X86_REG_ESP)
        return _Args(self, esp)

    def _on_code_hook(self, mu, addr, size, _):
        h = self._hooks.get(addr)
        if not h:
            return
        fn, pop = h
        args = self._args()
        try:
            r = fn(self, args)
        except Exception as e:  # report after emu_start returns
            self._err = e
            mu.emu_stop()
            return
        self._ret(r, pop or 0, args.esp, args.ra)

    # ---- imports -----------------------------------------------------------
    def _setup_imports(self):
        for i, (dll, name, iat) in enumerate(self.pe.imports()):
            a = STUBS + 4 * i
            self._imports[a] = (dll, name)
            self.write(iat, struct.pack('<I', a))
        for name, (nargs, fn) in default_imports().items():
            self._import_impl[name] = (nargs, fn)

    def import_stub(self, name, nargs, fn):
        """Set (or replace) the stub of a Win32 import: fn(emu, args) -> eax.
        nargs: the stdcall argument count (popped on return)."""
        self._import_impl[name] = (nargs, fn)

    def _on_code_stub(self, mu, addr, size, _):
        if addr == RET_MAGIC:
            mu.emu_stop()
            return
        imp = self._imports.get(addr)
        if imp is None:
            self._err = EmuError('jump into the stub page at %#x' % addr)
            mu.emu_stop()
            return
        dll, name = imp
        impl = self._import_impl.get(name)
        args = self._args()
        if impl is None:
            self._err = EmuError('unexpected import %s!%s called (args %s); add a stub with '
                                 'emu.import_stub()' % (dll, name, ' '.join('%#x' % args(i) for i in range(4))))
            mu.emu_stop()
            return
        nargs, fn = impl
        self.import_log.append((name, [args(i) for i in range(nargs)]))
        if self.trace_imports:
            print('  import %s(%s)' % (name, ', '.join('%#x' % args(i) for i in range(nargs))), file=sys.stderr)
        try:
            r = fn(self, args)
        except Exception as e:
            self._err = e
            mu.emu_stop()
            return
        self._ret(r if r is not None else 0, 4 * nargs, args.esp, args.ra)

    # ---- CRT replacements ----------------------------------------------------
    def _setup_crt(self):
        def malloc(e, a):
            return e.malloc(a(0), zero=False)

        def free(e, a):
            e.allocs.pop(a(0), None)
            return 0

        self.hook(CRT['malloc'], malloc)
        self.hook(CRT['free'], free)
        self.hook(GAME_ALLOC['he_malloc'], malloc)
        self.hook(GAME_ALLOC['he_free'], free)
        self.hook(GAME_ALLOC['vh_mem_alloc'], malloc)
        self.hook(GAME_ALLOC['vh_mem_free'], free)
        self.hook(CRT['fopen'], lambda e, a: e._fopen(e.cstr(a(0)), e.cstr(a(1))))
        self.hook(CRT['fclose'], lambda e, a: e._fclose(a(0)))
        self.hook(CRT['fseek'], lambda e, a: e._fseek(a(0), s32(a(1)), a(2)))
        self.hook(CRT['ftell'], lambda e, a: e._file(a(0)).pos)
        self.hook(CRT['fread'], lambda e, a: e._fread(a(0), a(1) * a(2), a(3)) // max(a(1), 1))
        self.hook(CRT['fwrite'], lambda e, a: e._fwrite(a(0), a(1) * a(2), a(3)) // max(a(1), 1))
        self.hook(CRT['fgetc'], lambda e, a: e._fgetc(a(0)))
        self.hook(CRT['_filbuf'], lambda e, a: e._fgetc(a(0)))
        self.hook(CRT['fputc'], lambda e, a: e._fputc(a(0), a(1)))
        self.hook(CRT['_flsbuf'], lambda e, a: e._fputc(a(0), a(1)))
        self.hook(CRT['fgets'], lambda e, a: e._fgets(a(0), s32(a(1)), a(2)))
        F = FILE_LAYER
        self.hook(F['file_read'], lambda e, a: e._fread(a(0), s32(a(1)), a(2)))
        self.hook(F['file_read_u8'], lambda e, a: e._fread_val(a(0), 1))
        self.hook(F['file_read_u16'], lambda e, a: e._fread_val(a(0), 2))
        self.hook(F['file_read_u32'], lambda e, a: e._fread_val(a(0), 4))
        self.hook(F['file_write'], lambda e, a: e._fwrite(a(0), s32(a(1)), a(2)))
        self.hook(F['file_write_u8'], lambda e, a: e._fwrite_val(a(1), a(0), 1))
        self.hook(F['file_write_u16'], lambda e, a: e._fwrite_val(a(1), a(0), 2))
        self.hook(F['file_write_u32'], lambda e, a: e._fwrite_val(a(1), a(0), 4))

    def add_file(self, name, data):
        """Make `data` readable by fopen under any path ending in `name`."""
        self.files[os.path.basename(name.replace('\\', '/')).lower()] = bytes(data)

    def _fopen(self, path, mode):
        base = os.path.basename(path.replace('\\', '/')).lower()
        writing = 'w' in mode or 'a' in mode
        if writing:
            vf = VFile(base, b'', True)
        elif base in self.files:
            vf = VFile(base, self.files[base], False)
        else:
            host = None
            if self.fs_root:
                for f in os.listdir(self.fs_root):
                    if f.lower() == base:
                        host = os.path.join(self.fs_root, f)
            if host is None:
                self.import_log.append(('fopen-missing', [path]))
                return 0
            vf = VFile(base, open(host, 'rb').read(), False)
        fp = self.malloc(32)
        self.open_files[fp] = vf
        return fp

    def _file(self, fp):
        vf = self.open_files.get(fp)
        if vf is None:
            raise EmuError('stdio call on an unknown FILE* %#x' % fp)
        return vf

    def _fclose(self, fp):
        vf = self.open_files.pop(fp, None)
        if vf is None:
            return -1
        if vf.writing:
            self.written[vf.name] = bytes(vf.data)
        return 0

    def _fseek(self, fp, off, whence):
        vf = self._file(fp)
        vf.pos = [0, vf.pos, len(vf.data)][whence] + off
        return 0

    def _fread(self, dest, n, fp):
        vf = self._file(fp)
        chunk = bytes(vf.data[vf.pos:vf.pos + n])
        if self.read_log is not None:
            self.read_log.append((vf.pos, len(chunk), dest))
        vf.pos += len(chunk)
        self.write(dest, chunk)
        return len(chunk)

    def _fread_val(self, fp, n):
        """file_read_u8/u16/u32: the value read (garbage-free on EOF: 0)."""
        vf = self._file(fp)
        chunk = bytes(vf.data[vf.pos:vf.pos + n])
        if self.read_log is not None:
            self.read_log.append((vf.pos, len(chunk), None))
        vf.pos += len(chunk)
        return int.from_bytes(chunk, 'little')

    def _fwrite_val(self, fp, v, n):
        vf = self._file(fp)
        vf.data[vf.pos:vf.pos + n] = (v & ((1 << 8 * n) - 1)).to_bytes(n, 'little')
        vf.pos += n
        return n

    def _fgetc(self, fp):
        vf = self._file(fp)
        self.write(fp, bytes(8))     # _ptr = 0, _cnt = 0: keep inline getc on the slow path
        if vf.pos >= len(vf.data):
            return 0xffffffff
        c = vf.data[vf.pos]
        if self.read_log is not None:
            self.read_log.append((vf.pos, 1, None))
        vf.pos += 1
        return c

    def _fgets(self, buf, n, fp):
        vf = self._file(fp)
        if vf.pos >= len(vf.data) or n <= 0:
            return 0
        out = bytearray()
        while len(out) < n - 1 and vf.pos < len(vf.data):
            c = vf.data[vf.pos]
            vf.pos += 1
            out.append(c)
            if c == 10:
                break
        self.write(buf, bytes(out) + b'\0')
        return buf

    def _fwrite(self, src, n, fp):
        vf = self._file(fp)
        data = self.read(src, n)
        vf.data[vf.pos:vf.pos + n] = data
        vf.pos += n
        return n

    def _fputc(self, c, fp):
        vf = self._file(fp)
        self.write(fp, bytes(8))
        vf.data[vf.pos:vf.pos + 1] = bytes([c & 0xff])
        vf.pos += 1
        return c & 0xff

    # ---- running -------------------------------------------------------------
    def call(self, addr, *args, insn_limit=None):
        """Call a cdecl function with 32-bit integer arguments; returns eax
        as a signed int. Raises EmuError on faults and unexpected imports."""
        mu = self.mu
        sp = STACK + STACK_SIZE - 0x1000 - 4 * (len(args) + 1)
        sp &= ~0xf
        frame = struct.pack('<I', RET_MAGIC) + b''.join(struct.pack('<I', a & 0xffffffff) for a in args)
        mu.mem_write(sp, frame)
        mu.reg_write(UC_X86_REG_ESP, sp)
        mu.reg_write(UC_X86_REG_EBP, 0)
        self._err = None
        try:
            mu.emu_start(addr, RET_MAGIC, count=insn_limit or self.insn_limit)
        except UcError as e:
            raise EmuError('%s\n%s' % (self._err or e, self.backtrace())) from None
        if self._err is not None:
            err, self._err = self._err, None
            if isinstance(err, EmuError):
                raise EmuError('%s\n%s' % (err, self.backtrace()))
            raise err
        eip = mu.reg_read(UC_X86_REG_EIP)
        if eip != RET_MAGIC:
            raise EmuError('call %#x did not return (stopped at %#x, insn limit?)\n%s'
                           % (addr, eip, self.backtrace()))
        return s32(mu.reg_read(UC_X86_REG_EAX))

    def _on_invalid(self, mu, access, addr, size, value, _):
        eip = mu.reg_read(UC_X86_REG_EIP)
        self._err = EmuError('invalid memory access (type %d) at %#x size %d from eip %#x'
                             % (access, addr, size, eip))
        return False

    def backtrace(self, depth=12):
        mu = self.mu
        out = ['  eip %#x  eax %#x ecx %#x edx %#x ebx %#x esi %#x edi %#x esp %#x ebp %#x' % tuple(
            mu.reg_read(r) for r in (UC_X86_REG_EIP, UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX,
                                     UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_ESP,
                                     UC_X86_REG_EBP))]
        ebp = mu.reg_read(UC_X86_REG_EBP)
        for _ in range(depth):   # optimisations are off, so ebp frames are reliable
            if not (STACK <= ebp < STACK + STACK_SIZE - 8):
                break
            ebp, ra = struct.unpack('<II', self.read(ebp, 8))
            out.append('  called from %#x' % ra)
        return '\n'.join(out)

    # ---- snapshots -------------------------------------------------------------
    def snapshot(self):
        """Globals, heap and file state, for restore() (cheap: ~2 MB)."""
        import copy
        return {
            'globals': self.read(GLOBALS_LO, GLOBALS_HI - GLOBALS_LO),
            'heap_next': self.heap_next,
            'heap': self.read(HEAP, self.heap_next - HEAP),
            'allocs': dict(self.allocs),
            'open_files': copy.deepcopy(self.open_files),
            'written': dict(self.written),
        }

    def restore(self, snap):
        self.write(GLOBALS_LO, snap['globals'])
        if self.heap_next > snap['heap_next']:
            self.write(snap['heap_next'], bytes(self.heap_next - snap['heap_next']))
        self.write(HEAP, snap['heap'])
        self.heap_next = snap['heap_next']
        self.allocs = dict(snap['allocs'])
        import copy
        self.open_files = copy.deepcopy(snap['open_files'])
        self.written = dict(snap['written'])

    # ---- write tracking --------------------------------------------------------
    def track_writes(self, lo=GLOBALS_LO, hi=GLOBALS_HI):
        """Start recording every byte written in [lo, hi); returns the set."""
        written = set()

        def on_write(mu, access, addr, size, value, _):
            if lo <= addr < hi:
                written.update(range(addr, addr + size))
        self.stop_tracking()
        self._write_hook = self.mu.hook_add(UC_HOOK_MEM_WRITE, on_write, begin=lo, end=hi - 1)
        return written

    def stop_tracking(self):
        if self._write_hook is not None:
            self.mu.hook_del(self._write_hook)
            self._write_hook = None


# ---- default Win32 stubs ----------------------------------------------------------
# Plausible values for the calls simulation code can reach (message pump,
# time, cursor, palette/GDI handles). Anything else fails loudly.

def default_imports():
    clock = [0]

    def ticks(e, a):
        clock[0] += 16
        return clock[0]

    def zero(e, a):
        return 0

    def one(e, a):
        return 1

    def handle(e, a):
        return FAKE_HANDLE

    def local_time(e, a):   # SYSTEMTIME 1996-05-30 12:00:00 Thursday
        e.write(a(0), struct.pack('<8H', 1996, 5, 4, 30, 12, 0, 0, 0))
        return 0

    def get_cursor_pos(e, a):
        e.write(a(0), struct.pack('<ii', 320, 240))
        return 1

    def get_client_rect(e, a):
        e.write(a(1), struct.pack('<4i', 0, 0, 640, 480))
        return 1

    return {
        'PeekMessageA': (5, zero),          # no messages
        'TranslateMessage': (1, zero),
        'DispatchMessageA': (1, zero),
        'timeGetTime': (0, ticks),
        'GetLocalTime': (1, local_time),
        'GetCursorPos': (1, get_cursor_pos),
        'SetCursor': (1, zero),
        'GetCursor': (0, handle),
        'ShowCursor': (1, one),
        'SetCursorPos': (2, one),
        'InvalidateRect': (3, one),
        'GetClientRect': (2, get_client_rect),
        'GetDC': (1, handle),
        'ReleaseDC': (2, one),
        'SelectPalette': (3, handle),
        'RealizePalette': (1, zero),
        'SelectObject': (2, handle),
        'BitBlt': (9, one),
        'StretchBlt': (11, one),
        'SetDIBColorTable': (4, one),
        'MessageBeep': (1, one),
        'GetSystemMetrics': (1, lambda e, a: {0: 640, 1: 480}.get(a(0), 0)),
        'SendMessageA': (4, zero),
        'GetLastError': (0, zero),
    }
