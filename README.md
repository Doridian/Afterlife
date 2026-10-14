# Afterlife

A decompilation and native port of **Afterlife** (LucasArts, 1996), targeting
Linux, macOS and WebAssembly.

Status: **early groundwork.** The CD tooling, resource-bundle reader and SDL3
platform layer work. The game itself isn't decompiled yet; the current binary is
a resource viewer that proves the pipeline end to end on every target.

This repository contains **no game data or original code**. You need your own
copy of the Afterlife CD.

## Getting the game data

From a BIN/CUE (or ISO) image of the CD:

```sh
tools/extract_cd.py /path/to/Afterlife.cue orig/
```

This writes the CD's files to `orig/` (gitignored) and prints their SHA-256s.
Compare them against [docs/binaries.md](docs/binaries.md). Only the
Windows 95 release has been checked so far.

## Building

Requires CMake ≥ 3.24, Ninja, a C11 compiler and SDL3 (fetched automatically if
not installed).

```sh
# Linux / macOS
cmake --preset native
cmake --build --preset native
build/native/afterlife orig/          # ←/→ to page through bitmaps, Esc to quit

# WebAssembly (needs Emscripten on PATH)
emcmake cmake --preset wasm -DAFTERLIFE_WASM_DATA=/dir/with/MEDIUM.000
cmake --build --preset wasm
cd build/wasm && python3 -m http.server   # open afterlife.html
```

`AFTERLIFE_WASM_DATA` gets bundled into `afterlife.data`. The full `ALIFE.000`
is 148 MB, so for the web point it at a directory containing just `MEDIUM.000`
(9.5 MB) until on-demand streaming exists.

There's also a `debug` preset with ASan/UBSan.

## Tools

- `tools/extract_cd.py`: pure-Python ISO9660 extractor for BIN/CUE and ISO images.
- `tools/jiff.py`: list, summarise or dump the resources in the `.000` bundles.

## Docs

- [docs/binaries.md](docs/binaries.md): what's on the CD and what the executables are built with
- [docs/formats/jiff.md](docs/formats/jiff.md): the `.000` resource bundle format
- [docs/decomp-workflow.md](docs/decomp-workflow.md): approach, Ghidra setup, layout, conventions

## Legal

Afterlife is © LucasArts Entertainment Company. This project is not affiliated
with or endorsed by LucasArts or Disney. It's intended for owners of the
original game, for preservation and interoperability.

The code in this repository is licensed under the
[GNU Affero General Public License v3.0 or later](LICENSE). Game data is not
included and is not covered by this license.
