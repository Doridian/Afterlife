# Afterlife

A decompilation and native port of **Afterlife** (LucasArts, 1996) to portable
C11 and SDL3, for Linux, macOS and WebAssembly.

Status: **playable.** All of `ALIFE.EXE`'s game code is ported:
- the simulation, economy and SOUL flow
- building, the Karma rail network and the Bad Things
- every view window, the advisors, the news and the tutorial
- save and load, and iMUSE music and sound

The differential emulator tests check the simulation against the original.
It's a non-matching decompilation: the code behaves like the original but
doesn't compile to the same bytes. Linux is the main platform. The
WebAssembly build runs the full game in a browser. macOS builds and passes
the data-less tests in CI, but nobody has played it on a Mac yet.

This repository contains **no game data files or original code** (only the
texts and number tables from `ALIFE.EXE` the port needs, as C data in the
modules that use them).
You need your own copy of the Afterlife CD.

## Getting the game data

From a BIN/CUE (or ISO) image of the CD:

```sh
tools/extract_cd.py /path/to/Afterlife.cue orig/
```

This writes the CD's files to `orig/` (gitignored) and prints their SHA-256s.
Compare them against [docs/binaries.md](docs/binaries.md). Only the
Windows 95 release has been checked so far.

At runtime the port needs the data files: the `.000` bundles (`ALIFE.000`,
`MEDIUM.000`, `FULL.000`), `ALIFE.INI`, the scenarios (`ASCEN*.ASC`) and
`ALIFE.EXE`, which it reads for its image data: the cursors, the icon and the
built-in font. The texts, number tables and credits the EXE carries are part
of the source, so building doesn't need the EXE.

## Building

Requires CMake ≥ 3.24, Ninja, a C11 compiler and SDL3 (fetched automatically if
not installed).

```sh
cmake --preset native
cmake --build --preset native
build/native/afterlife orig/
```

There's also a `debug` preset with ASan/UBSan.

**macOS**: `brew install cmake ninja sdl3`, then the same commands. Without
a `DATA_DIR` argument the port looks for `orig/` in the current folder, then
next to the executable. Ctrl+Cmd+F toggles fullscreen and Cmd+Q asks before
quitting. On a Retina screen the faithful mode draws at native pixels, so
it looks small; turn on the enhancements and leave the UI scale on automatic.

**WebAssembly** (Emscripten, [docs/web.md](docs/web.md)):

```sh
emcmake cmake --preset wasm -DAFTERLIFE_WASM_DATA=$PWD/orig
cmake --build --preset wasm
cd build/wasm && python3 -m http.server 8000   # http://localhost:8000/afterlife.html
```

`AFTERLIFE_WASM_DATA` builds the game files the port needs into
`afterlife.data` (about 150 MB with the CD's `ALIFE.INI`). Don't publish such
a build. Without it the page asks for the files once and keeps them in the
browser. Saves and the port settings are kept in the browser too
(IndexedDB). Dropping a `.alf` or `.asc` onto the page loads it, and
File > Save to Download / Download Saved Game save to your computer. The
canvas follows the browser window. Alt+Enter or F11 toggles
fullscreen. `?arg=...` in the URL passes command-line options.

CI (`.github/workflows/build.yml`) builds on Linux and macOS, runs the tests
that need no game data, and builds the WebAssembly target.

## Running

```
afterlife [DATA_DIR] [FILE.INI] [SAVE.ALF|SCENARIO.ASC] [options]
```

The port opens in a resizable window that remembers its size and position.
**Alt+Enter** or **F11** toggles fullscreen.

| Option | |
|---|---|
| `--enhancements` / `--no-enhancements` | turn the enhancements on or off |
| `--ui-scale=N` | UI scale, `0` = auto, `1`–`4` (enhancements only) |
| `--window-size=WxH` | starting window size |
| `--fullscreen` / `--windowed` | start fullscreen or windowed |
| `--port-ini=FILE` | use another port settings file |
| `--debug-window` | open the original's hidden debug window (a tile browser, [docs/ui.md](docs/ui.md#debug-window)) |

### Enhancements

By default the port behaves like the original, bugs included. **Global → Port
Options...** turns on *enhancements*: fixes for the original's bugs (two hangs
among them) and an integer UI scale, so the 1996 interface stays usable on
high-resolution screens. [docs/enhancements.md](docs/enhancements.md) lists
them all.

The port keeps its own settings in `ALIFEPORT.INI` in the SDL preferences
folder (`~/.local/share/Afterlife/Afterlife/` on Linux,
`~/Library/Application Support/Afterlife/Afterlife/` on macOS, IndexedDB in
the browser), so the original's `ALIFE.INI` stays untouched.

## Tests

```sh
cmake --preset native -DAFTERLIFE_DATA=$PWD/orig
cmake --build --preset native
ctest --test-dir build/native
```

Without `AFTERLIFE_DATA`, only the tests that need no game data run.

`tools/emu/` runs the original `ALIFE.EXE`'s functions in a Unicorn emulator
side by side with the port, on real saves, and compares state and RNG. See
[tools/emu/README.md](tools/emu/README.md).

## Tools

- `tools/extract_cd.py`: pure-Python ISO9660 extractor for BIN/CUE and ISO images.
- `tools/jiff.py`: list, summarise or dump the resources in the `.000` bundles.
- `tools/gh`, `tools/ghidra/`: headless Ghidra setup and scripts for the decompilation.
- `tools/emu/`: the differential emulator tests.

## Docs

- [docs/decomp-workflow.md](docs/decomp-workflow.md): approach, Ghidra setup, layout, conventions
- [docs/game-map.md](docs/game-map.md): module map of the game and the port
- [docs/binaries.md](docs/binaries.md): what's on the CD and what the executables are built with
- [docs/startup.md](docs/startup.md), [docs/rendering.md](docs/rendering.md),
  [docs/ui.md](docs/ui.md), [docs/map.md](docs/map.md),
  [docs/imuse.md](docs/imuse.md), [docs/compbun.md](docs/compbun.md):
  subsystem notes
- [docs/web.md](docs/web.md): the WebAssembly build
- [docs/enhancements.md](docs/enhancements.md): what the enhancements flag changes
- [docs/formats/](docs/formats/): file formats (JIFF bundles, compression,
  FLC, TILE, AKOS, ALSV saves)

## Legal

Afterlife is © LucasArts Entertainment Company. This project is not affiliated
with or endorsed by LucasArts or Disney. It's intended for owners of the
original game, for preservation and interoperability.

The code in this repository is licensed under the
[GNU Affero General Public License v3.0 or later](LICENSE). Game data is not
included and is not covered by this license; neither are the texts and number
tables from `ALIFE.EXE` in the sources, which remain © LucasArts.
