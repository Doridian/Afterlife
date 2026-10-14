# The original binaries

All from the retail Afterlife CD (volume `AFTERLIF`, mastered with OMI QuickTOPiX 2.00).
`tools/extract_cd.py` prints SHA-256 for every file; the ones that matter:

| File | SHA-256 | What |
|---|---|---|
| `ALIFE.EXE` | `6f034f66c28c06f66e553baee90c9d5d91a98de54399309457b65e05df9e0842` | Windows 95 game, **primary decomp target** |
| `ALIFEDOS.EXE` | `a59a12959c9e119f8cc71d3f85325c5756dd42b5cc9ca36591f47e8cc52e88df` | DOS game (LE, DOS/4GW): a second view of the same code |
| `ALIFE.000` | `1b4c059e9795f51852d8ac35a088c06f1ca42a878068ce32203300ed2f85719c` | Full resource bundle (1217 entries) |
| `FULL.000` | `9155d0f6b76669247c35206987496496db6b8335d17a35c925b9bfce0ceba003` | Install-size subset of `ALIFE.000` (601 entries) |
| `MEDIUM.000` | `ba509afc76e4e0748cac2591643f17a4c3956785d43a3fa0b9c82451c79058d0` | Smaller subset (308 entries) |
| `SETUP.EXE`, `SETUPDOS.EXE` | | Installer, not needed |
| `IMUSE.EXE` | | DOS/4GW iMUSE tool, unclear role |

## ALIFE.EXE

- PE32, i386, GUI. Link timestamp **1996-05-30 16:23:16 UTC**. Linker version 2.55.
- **Compiled with Microsoft Visual C++** (VC++ 2.x era, judging by linker 2.55), not Watcom.
  The entry point is the MSVC CRT startup (SEH frame, `GetVersion`, `__cinit`), Ghidra's
  Function ID matches MSVC CRT routines (`__cinit`, `__ftol`, `__adj_fdiv_m64`, ...), and
  the code is plain `__cdecl` with caller stack cleanup. The Watcom "1988-1994" runtime
  banners are in `ALIFEDOS.EXE`, not here. There's no Rich header and no debug directory,
  so no symbols.
- The code is C, apparently built with optimizations off (`jnz rel32`, dead blocks
  after `jmp`). Ghidra's default x86 Windows calling conventions are correct, so don't
  switch to `__watcall` for this binary.
- Image base `0x400000`, entry `0x4b1007`.

| Section | VA | Virtual size |
|---|---|---|
| `.text` | `0x401000` | `0xf1322` (~965 KB of code) |
| `.bss` | `0x4f3000` | `0x15bba4` |
| `.rdata` | `0x64f000` | `0x334` |
| `.data` | `0x650000` | `0x27614` |
| `.idata` | `0x678000` | `0xc94` |
| `.rsrc` | `0x679000` | `0x6c6c` |
| `.reloc` | `0x680000` | `0x113a2` |

### OS surface

The whole Windows dependency is small, so the platform layer (`src/platform/`) stays small too:

- **GDI32**: `CreateDIBSection`, `SetDIBColorTable`, palette calls, `BitBlt`/`StretchBlt`.
  The game draws into an 8-bit DIB and blits it. No DirectDraw.
- **WINMM**: `waveOut*` for audio, `timeSetEvent`/`timeKillEvent`/`timeGetTime` for timing.
  No DirectSound. All music and sound is digital, mixed in-process by iMUSE.
- **USER32**: window, message pump, cursor, `ChangeDisplaySettingsA` for mode switching.
- **KERNEL32**: file I/O, find-first/next, directories, `VirtualAlloc`, and the MSVC CRT's usual imports.
- **comdlg32**: `GetSaveFileNameA` only.

### Source-path strings (assert/debug leftovers)

These name the original modules, which gives a starting map for `src/`:

```
compbun:source:bndlemgr.c   C:\COMPBUN\source\Bndlemgr.c    resource bundle manager (.000)
compbun:source:cmprsmgr.c   C:\COMPBUN\source\Cmprsmgr.c    compression manager
compbun:source:codecmgr.c   C:\COMPBUN\source\CODECMGR.C    codec manager
compbun:source:cbresfm.c                                    resource file manager
compbun:source:preload.c    C:\COMPBUN\source\PRELOAD.C
C:\VHUMOR\source\mem.c                                      memory manager
C:\VHUMOR\source\module.c
iMUSE:commands.c  fades.c  files.c  groups.c  time.c  triggers.c
iMUSE:wave:dispatch.c  mixer.c  streamer.c  tracks.c  wave.c
iMUSE:wavedrvs:win32:waveapi.c
```

"VHUMOR" is presumably the game's own codebase, and `compbun` and iMUSE are LucasArts
shared libraries. iMUSE is also documented from the ScummVM side, which helps.
