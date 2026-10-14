# Decompilation workflow

## Approach: non-matching, port-as-you-go

The goal is a native port (Linux, macOS, WebAssembly), not a byte-identical rebuild:

- `ALIFE.EXE` was built with a mid-90s MSVC (VC++ 2.x era; `ALIFEDOS.EXE` with
  Watcom C 10.x). A *matching* decomp would need that exact compiler and flags,
  and matching code wouldn't be portable anyway.
- So each function is decompiled in Ghidra, cleaned up into readable, portable C,
  and checked by behaviour (same output and state as the original) rather than
  by bytes.
- Every OS call goes through `src/platform/`. Game code never includes SDL or
  Windows headers.

If matching becomes interesting later (e.g. to prove correctness of tricky
simulation code), it can be added per-function without changing the layout.

## Ghidra setup

Needs Ghidra 12 at `/opt/ghidra` (or set `GHIDRA_INSTALL_DIR`) and Java 21.

1. `tools/extract_cd.py <your .cue> orig/`
2. `tools/ghidra/setup.sh` imports `orig/ALIFE.EXE` into `ghidra/Afterlife.gpr`
   (gitignored), auto-analyzes it (~1 min), creates functions for orphan code
   (`AfterlifePost.java`) and applies `symbols/ALIFE.EXE.txt`. `--force` rebuilds
   it from scratch.
3. Query it with `tools/gh` (`tools/gh --help`):

   ```
   tools/gh funcs [-u] [regex]          # addr, size, name (-u: user-named only)
   tools/gh decompile <addr|name>       # C for the containing function
   tools/gh disasm <addr|name>          # disassembly of the function
   tools/gh listing <addr|name> [n]     # n code units (data or code) from addr
   tools/gh xrefs <addr|name>           # references to addr
   tools/gh strings [-x] [regex]        # defined strings (-x: referencing functions)
   tools/gh rename <addr|name> <new> [func|data]   # DB and symbol file
   tools/gh apply                       # symbols/ALIFE.EXE.txt -> DB
   tools/gh export                      # DB user names -> symbols/ALIFE.EXE.txt
   printf 'decompile bndl_open\nxrefs bndl_open\n' | tools/gh batch
   ```

   Each call starts a JVM (~3 s). `batch` runs many commands in one JVM.
   Calls are serialized with `flock` on `ghidra/.gh.lock` (Ghidra can't open a
   project twice), so parallel callers are safe but queue up. Query commands
   open the program read-only; only `apply` and `rename` save to the DB.

`ALIFE.EXE` is MSVC-built, so Ghidra's default `__cdecl`/`__stdcall` handling is
right. `ALIFEDOS.EXE` (Watcom, DOS/4GW LE) doesn't import with stock Ghidra:
it loads only the 16-bit MZ stub. It needs an LE/LX loader extension. Once it
imports, run `tools/ghidra/scripts/AfterlifeSetup.java` as a `-preScript`
before analysis to install the `__watcall` prototype
(`tools/ghidra/watcall.xml`) as a compiler-spec extension and make it the
default evaluation model. `AfterlifePost.java` then marks internal functions
`__watcall`.

The Ghidra project is binary and contains copyrighted code, so it stays out of
git. Knowledge belongs in the repo as text: names, signatures and notes go in
`symbols/` (format in `symbols/README.md`). Don't commit disassembly or
decompiler dumps.

## Layout

```
src/platform/   SDL3 backend replacing GDI / winmm / USER32 / file I/O
src/formats/    clean-room readers for data formats (may be replaced by decompiled originals)
src/game/       decompiled game code ("VHUMOR"), split by original module where known (planned)
src/compbun/    decompiled bundle/compression library (see docs/compbun.md)
src/imuse/      decompiled iMUSE (planned)
tools/          Python helpers: CD extraction, bundle inspection
docs/           format and binary notes
```

## Rules

- **Never commit original game data or binaries**, including extracted
  resources, disassembly dumps or large copied byte tables. Users supply their
  own CD. Data tables the code needs should be loaded from the user's files at
  runtime.
- Name functions after what they do. Note the original address in a comment
  (`/* 0x4012a0 */`) so it can be traced back.
- Keep game code endian-safe and 64-bit-safe: the original assumes 32-bit
  pointers and little-endian, and saved games and `ALSV` files use the raw
  struct layout, so parse them field by field.

## Suggested order

1. **Bundle manager and compression** (`compbun`): done, see
   [compbun.md](compbun.md). The .000 bundles are read by the game's own
   resource code (`src/game/resource.c`) at the bottom of the compbun stack.
2. **Startup and INI parsing**: done, see [startup.md](startup.md) (init
   order, `ALIFE.INI` keys, main loop shape).
3. **Rendering**: DIB blit paths, TILE drawing, FLI/FLC player, cursors.
4. **iMUSE**: mixer and streamer onto `plat_audio_queue`.
5. **Simulation**: the actual game. This is the bulk of `.text`.
6. **UI**: menus, graph views, advisors (AKOS).
