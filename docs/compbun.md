# compbun: layered resource I/O

`compbun` is a LucasArts library (`C:\COMPBUN\source\`, all modules dated Apr 18 1996)
that gives stdio-like file access through a stack of layers. Decompiled into
`src/compbun/`; the game-side code that plugs into it is in `src/game/resource.c`.

## Layers

A layer is a table `{name, init, open, close, read, read_cb, seek, tell, getpos, next}`
(`CbLayer`). `cbres_*` calls enter at `topLayer`. A layer either handles the call or
forwards it with `cbres_lower_*`, which temporarily moves `currentLayer` one step down.

```
cbres_open/read/seek/tell/close      cbresfm.c   0x4d53b0..  entry points (topLayer)
  Compression Layer    cmprs_layer   cmprsmgr.c  0x675bb0    pushed if compress
  Bundling layer       bndl_layer    bndlemgr.c  0x675c88    pushed if bundle
  C File Layer         cbres_c_file_layer 0x6749d0           CRT fopen/fread/...
```

`compbun_init(compress, bundle)` (0x4d51a0) builds the stack. **Afterlife calls it once,
from its sound init (0x4b9510), as `compbun_init(1, 0)`.** Just before that, the sound
init overwrites the C File Layer's open/close/read/seek/tell slots with game functions
(0x4ba03f..) that call the game's resource manager. So a compbun "file name" is a
resource ID, and the bottom of the stack reads the `.000` bundles. compbun only serves
iMUSE audio. Bitmaps and everything else call the resource manager directly.

- **Compression Layer.** On open it peeks at 4 bytes. A big-endian `COMP` tag starts
  a block-compressed stream: `numBlocks`, `blockShift`, `lastBlockSize`, then 16-byte
  `{offset, size, codec, -}` entries, all big-endian, with offsets relative to the
  tag. Reads decompress each block they touch with `codec_decompress(codec, ...)`
  into a 0x2200 buffer and keep the last block in a 0x2000 cache per file. Anything
  else is passed straight through. **No resource in any of the three bundles starts
  with `COMP`**, so in Afterlife this layer is a pass-through.
- **Bundling layer.** Serves 8.3-named files from up to two bundle files
  (`fileSystem[2]`: bunFile, per-entry `{inUse,pos}`, header, 20-byte TOC entries
  `name[8] ext[3] - offset size`), found with `bsearch` and a case-insensitive
  `NAME.EXT` compare.
- **Preload.** Helpers that parse the COMP header, plus `preloadDirectory[100]`.

## Answers

**How bundle files are chosen.** Not by compbun. `bndl_init` sets `numBunFiles = 0`
and `bundleInitialized = 0`, and nothing in ALIFE.EXE writes `fileSystem[]` or sets
`bundleInitialized`. The layer isn't pushed anyway. (If it were, every open would
fail: `bndl_open` returns 0 when uninitialized instead of passing the call down.)
The game's RcInitialize (0x4cd9b0) indexes two JIFF files named in ALIFE.INI:
`CD Path` (the CD's ALIFE.000, which must load or you get "Please insert the
Afterlife CD") and `Resource file` (the install's local FULL.000 or MEDIUM.000, or
"None", ignored if it fails). Each lookup scans the local index first, then the CD's,
and takes the first matching ID. Then it opens that bundle again for every resource.
There is no `%03d` bundle numbering.

**`'%s' Not found in %03d... illegal block len %ld`.** It belongs to a SCUMM-style
block search (`res_find_block` 0x4dbc23, plus an unreferenced copy at 0x4dbd59). The
search walks the child blocks of an in-memory resource (tag, then a **big-endian**
length that includes the 8-byte header) looking for a tag such as `WRAP`, `OFFS` or
`AKPL` (used for AKOS). A child length below 1 is "illegal". `%03d` prints a byte at
0x64b9e1 that is never written (a SCUMM room number left over). The error function
(0x4d5bf0) is an empty stub, so the check does nothing. A zero length makes the
search loop forever. It never looks at JIFF/RESC fields.

**RESC length or `+4`?** The body's `+4` size field drives the reads. RcResourceSize
(0x4cdd61) returns `+4 - 16`. The RESC chunk length is read and thrown away. OFST is
added to `0x24 + 8 * count`, the fixed header size, which is where the first RESC
chunk starts. The `+0` field is a **resource kind**, not a compression type: 2 means
the data follows (skip the 8 reserved bytes), 1 means the body holds a path to a
separate "remote" file. The stream that is returned isn't bounded, so callers read
exactly `size` bytes.

**What PRELOAD caches.** In principle, the block layout of compressed files (filename,
numSectors, sectorSize, sectorShift, lastSectorSize, a codec class, and a u16 per
sector: 13 bits for size - 1, 2 bits selecting a codec from a 3x4 class table), so
that opening a file doesn't have to read the COMP header from CD. In ALIFE.EXE it
caches nothing: the directory is in .bss, only `preload_init`'s memset writes it,
and `preload_find` always returns -1.

## Port notes

- Handles are `intptr_t` (the original used 32-bit ints that held FILE pointers).
  Names are strings, so resource IDs are passed as decimal strings
  (`rc_resource_name`). The original passed the integer itself as a `char *`.
- VHUMOR's memory and module runtime is out of scope. `src/game/vh_mem.h` maps
  `vh_mem_alloc`/`vh_mem_free` to malloc/free, and `compbun_shutdown()` stands in
  for the module runtime's shutdown calls. The `*_dump` functions are not ported.
- File I/O goes through `plat_file_*`. The game's retry box for read errors is
  dropped.
- Small safety guards were added where the original overruns (block table index,
  block sizes larger than the buffers, a full file list).
- Verified: `tests/compbun_dump` reads every resource of MEDIUM, FULL and ALIFE.000
  through the stack, and `tests/compare_jiff.py` matches all of them byte for byte
  against `tools/jiff.py`. The duplicate ID 760 resolves to its first entry.
