# Decompilation workflow

## Approach: non-matching, port-as-you-go

The goal is a native port (Linux, macOS, WebAssembly), not a byte-identical rebuild:

- `ALIFE.EXE` was built with a 1994-era Watcom C 10.x. A *matching* decomp would
  need that exact compiler and flags. Open Watcom 1.9/2.0 won't reproduce its
  output byte for byte, and matching code wouldn't be portable anyway.
- So each function is decompiled in Ghidra, cleaned up into readable, portable C,
  and checked by behaviour (same output and state as the original) rather than
  by bytes.
- Every OS call goes through `src/platform/`. Game code never includes SDL or
  Windows headers.

If matching becomes interesting later (e.g. to prove correctness of tricky
simulation code), it can be added per-function without changing the layout.

## Ghidra setup

1. `tools/extract_cd.py <your .cue> orig/`
2. Create a Ghidra project in `ghidra/` (gitignored) and import `orig/ALIFE.EXE`.
3. Before auto-analysis, set the default calling convention to `__watcall`
   (Watcom register calling convention). CRT and imported Win32 functions stay `__stdcall`/`__cdecl`.
4. Optionally import `orig/ALIFEDOS.EXE` as a second program. It's the same game
   compiled for DOS/4GW, so cross-referencing the two helps separate game logic
   from platform glue.

The Ghidra project is binary and contains copyrighted code, so it stays out of
git. Knowledge belongs in the repo as text: names, types and notes go in
`symbols/` (planned: an address → name/signature map that a Ghidra script can
import and export, so everyone's database stays in sync).

## Layout

```
src/platform/   SDL3 backend replacing GDI / winmm / USER32 / file I/O
src/formats/    clean-room readers for data formats (may be replaced by decompiled originals)
src/game/       decompiled game code ("VHUMOR"), split by original module where known (planned)
src/compbun/    decompiled bundle/compression library (planned)
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

1. **Bundle manager and compression** (`compbun`): confirm what `flags = 2` means,
   then replace `src/formats/jiff.c` with the real thing.
2. **Startup and INI parsing** (`ALIFE.INI` keys: Resolution, Resource file,
   Heap size, Music/Voice stream, …).
3. **Rendering**: DIB blit paths, TILE drawing, FLI/FLC player, cursors.
4. **iMUSE**: mixer and streamer onto `plat_audio_queue`.
5. **Simulation**: the actual game. This is the bulk of `.text`.
6. **UI**: menus, graph views, advisors (AKOS).
