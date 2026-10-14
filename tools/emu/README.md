# tools/emu: differential tests against ALIFE.EXE

Runs the original game code under the [unicorn](https://www.unicorn-engine.org/)
x86-32 emulator next to the port (built as a shared library and called through
ctypes). You check a ported function by behaviour: load the same save on both
sides, give both the same RNG state, call the original and the port, then diff
the whole game state.

`ALIFE.EXE` and the saves are read from your `orig/` at runtime (or
`$AFTERLIFE_ORIG`). Never commit them, or anything dumped from them.

## Setup

```
python3 -m venv .venv && .venv/bin/pip install -r tools/emu/requirements.txt
```

The only dependency is `unicorn`. Run the scripts with `.venv/bin/python`
(gitignored).
The port library (`libafterlife_port.so`,
CMake target `afterlife_port`, not built by default) is configured and built
in `build/emu` (Debug, asserts on) the first time the harness runs, then
rebuilt on every run. The build is serialized with flock. If the build fails
(another agent's half-finished file, say), the harness warns and uses the
stale library. Set `AFTERLIFE_EMU_STRICT=1` to fail instead, or
`AFTERLIFE_PORT_LIB=path/to.so` to skip the build.

## What's checked

```
tools/emu/check_wp0.py              # all of the below, ~5 min
tools/emu/check_wp0.py rng bignum   # util.h vs the original, ~1 min
tools/emu/check_wp0.py load         # save_load_game vs world_load, 4 saves
tools/emu/check_wp0.py cells        # every world.h accessor on every cell
tools/emu/check_state_h.py [-v]     # state.h's address comments, ~5 s
```

- **RNG**: `crt_rand` and `rand_range` against the MSVC `rand` 0x4b1202 and
  0x42b088, including huge and overflowing ranges. The RNG states match
  afterwards.
- **bignum / mul_div / fmt**: all 10 functions on edge values and random
  values.
- **Loading**: the original's `save_load_game` (0x48f837), post-load table
  rebuilds included, against the port's `world_load`, for all four
  `ASCEN00x.ASC`. Compared: every global the original's loader stores (about
  646 KB), the grids raw (map cells, object maps, sim cells), both
  `RealmTileInfo` tables, the current-plane globals, the planet map and the
  RNG. They are identical. A negative control checks that planted
  differences are found and named.
- **Accessors**: every `cell_*` / `sim_*` getter, `soul_group_get` and
  `tile_index_footprint`, on all 3 x 10000 cells of each save. Random setter
  sequences on both sides, with a full state diff after each round.
- **state.h**: for each `GameState` field (array: first and last element),
  the port changes that field alone and saves. The changed file bytes are
  looked up in the traced original loader, and the resulting address is
  checked against the one state.h claims (`unk_XXXXXX`, a `0x...` comment,
  or record base + `+0x..` offset). Currently all 621 claimed fields match.

## Writing a differential test (package agents)

```python
import ctypes, sys
sys.path.insert(0, 'tools/emu')
from difftest import Harness, SAVES

h = Harness()                    # emulator + port library
for save in SAVES:
    h.load(save)                 # both: orig save_load_game / port world_load
    h.seed(0x1234)               # both RNGs (raw MSVC rand state)
    h.emu.stub(0x4c2830)         # wm_send: nothing the sim needs
    h.emu.stub(0x4750f0, log=calls)  # snd_stop, recording the calls
    r1 = h.orig(0x460b16, 1)              # original, cdecl int args -> eax
    r2 = h.port('souls_arrive_roads', 1)  # port, by C name
    assert r1 == r2
    h.check(what=save)           # AssertionError listing every differing field
    print(h.uncompared_writes()) # what the original wrote that isn't compared
```

- **Stub what isn't simulation.** An import without a stub raises
  `EmuError` with a backtrace (`unexpected import USER32.DLL!ShowWindow`). Then
  stub the *internal* caller: `h.emu.stub(addr, ret=0, log=list)`, or
  `h.emu.hook(addr, fn)` with `fn(emu, args) -> eax` (`args(i)` is the i-th
  stack argument). For a Win32 call that is fine to fake:
  `h.emu.import_stub('Name', nargs, fn)` (stdcall).
- **Same inputs.** `h.seed()` sets both RNGs. `h.reset()` returns both sides
  to the loaded state, which is cheaper than `load()`. Plane selection goes
  through `map_view_select_plane` (0x42b102) with `bridge.MAP_VIEW` on the
  original side, and `world_select_plane` on the port side.
- **Function results** come back as eax. A function declared to return a byte
  leaves garbage in the upper bits, so mask it (`& 0xff`). Strings and
  structs are read from emulator memory: `h.emu.cstr(ptr)`, `h.emu.u32(addr)`,
  `h.emu.read(addr, n)`, `h.emu.malloc(n)` for argument buffers.
- **Static inline / unexported port functions** need a C wrapper. Put it in
  `tools/emu/shims/<package>.c` (globbed into `afterlife_port`) and mark it
  `__attribute__((visibility("default")))`. `port_shim.c` has the world.h
  ones.
- **State the diff doesn't cover yet.** `h.diff()` compares the saved
  globals, the grids, the tile tables, the current-plane globals, the planet
  map and the RNG. For runtime-only tables your package owns, compare raw
  memory yourself (`h.emu.read(addr, n)` against a ctypes view of the port's
  table), or put them in `bridge.state_regions`.
  `h.uncompared_writes()` shows what the original changed outside the
  compared set.
- `h.diff(ignore=['plane[1].stats.unk_80f8', 'objects['])` skips known
  differences (exact names, or prefixes ending in `.` or `[`).

## Pieces

| File | What |
|---|---|
| `alife_emu.py` | `Emu`: PE mapping (preferred base, so no relocations), stack, bump heap, Win32 import stubs, HLE of the CRT heap/stdio and the game's `file_*`/`he_malloc`/`vh_mem_*`, `call()`, `hook()`/`stub()`, write tracking, snapshots |
| `bridge.py` | `orig_load` (the original loader on an in-memory file), `LoadMap` (file offset <-> global address, traced), `PortImage` (the port state loaded into a scratch emulator by the original loader, then grids and current-plane globals copied raw), `diff` |
| `names.py`, `cstruct.py` | address -> field name from `state.h`/`world.h` (parsed at runtime: names, address comments, packed record layouts) and `symbols/ALIFE.EXE.txt` |
| `port.py`, `port_shim.c` | the port library: build, load, state pointers, save/load |
| `difftest.py` | `Harness`: both sides in step |
| `check_wp0.py`, `check_state_h.py` | the checks above, and worked examples |

## How the comparison works, and its limits

The comparison happens in the original's address space. The port's state is
saved as ALSV 7300, loaded by the *original's* loader into a second
emulator, and then the parts whose layout is the same are overwritten raw.
That covers the map cells, object maps, all 18 bytes of every sim cell, the
tile tables and the current-plane pointers. The two emulators are then
compared byte by byte over the saved-globals set.

- The bridge goes through `savegame.c`. A field the port keeps but doesn't
  save is invisible, unless it's in a raw region. A value the original's
  loader transforms (realm +0x80ef is clamped to 0 when > 100) can hide a
  difference.
- `plane_select_count` is ignored: the original's loader selects each plane
  while reading it, which makes 6 selects to the port's 3.
- The map view starts as 100 x 100, zoom 2, view flags 0xc0 (the port's
  `g_build.view_flags` default). Before loading, the tile tables are filled
  with the original's 0x452581, because a fresh process has none.
  `Harness(run_rebuilds=False)` stubs the post-load rebuilds instead.
- Header-only fields (title, timestamp, thumbnail, `save_version`) aren't
  globals, so they aren't compared.
- No CRT startup runs. Code that needs CRT state the HLE layer doesn't cover
  (locale, `time()`, `sprintf` with floats, ...) can misbehave. Unmapped
  accesses and unknown imports fail loudly. Silent wrong values are possible
  in principle.
- Speed: about 12 µs per short call, 2-5 s per load (Python hooks on every
  file read).
- The emulator never reads a GameState back directly. To get one, run the
  original's `save_write_game` (0x485fb3) with the stdio HLE capturing the
  file (`emu.written`), then call `port.world_load_mem`. This is untested:
  the function prompts and grabs a thumbnail, so those calls need stubs.
