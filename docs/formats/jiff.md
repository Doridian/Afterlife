# `.000` resource bundles ("JIFF")

`ALIFE.000`, `FULL.000` and `MEDIUM.000` share one format. It's IFF-like, but tags
are stored as little-endian `u32`s (so `JIFF` reads as `FFIJ` on disk) and lengths
are little-endian. Read by the game's own resource code (RcInitialize /
RcOpenResource, `src/game/resource.c`), not by compbun's bundle manager;
see [compbun.md](../compbun.md).

Tools: `tools/jiff.py` (list/stats/verify/dump), `src/game/resource.c` (runtime reader, decompiled).

## Layout

```
"JIFF"  u32 0                     form header, length unused
"HEAD"  u32 4     u32 count       number of resources
"OFST"  u32 4*n   u32[count]      offsets of each resource's RESC body, relative
                                  to the body of the first RESC chunk
"RCID"  u32 4*n   u32[count]      resource IDs, same order as OFST
"RESC"  u32 len   body            one RESC chunk per resource, back to back, to EOF
"RESC"  u32 len   body
...
```

Each resource is its own `RESC` chunk. Unlike the other chunks, a RESC length
counts its own 8-byte header (`len` = body size + 8). So consecutive OFST values
differ by exactly `len`, and the last chunk ends exactly at EOF. The body:

```
+0   u32  type     compression type; 2 in every entry of all three bundles
+4   u32  size     body size including this 16-byte header (= RESC len - 8)
+8   u8[8]         zero in every entry
+16  payload       size - 16 bytes, an embedded file in its own format
```

`tools/jiff.py verify` checks all of this. It also checks every payload that
carries its own size field (BMP, FLI/FLC, TILE, AKOS) against the entry size,
and all 2126 entries across the three files pass. (Earlier notes used
`size - 8` as the payload length, which also pulled in the next chunk's
`RESC` header. That was wrong.)

### Body `+0` is the resource kind, not a compression type (resolved)

The decomp answered this ([compbun.md](../compbun.md)): RcOpenResource (0x4cda8e)
reads `+0` as a kind. **2** = data follows (it skips the 8 reserved bytes);
**1** = the body holds the path of a separate "remote" file to open instead;
anything else is "Resource type %d is invalid". The data size is `+4` - 16;
the RESC chunk length is read and ignored. Compression is detected
separately by compbun from a big-endian `COMP` tag at the start of the data,
and no Afterlife resource has one. The notes below predate this.

#### Earlier hypothesis: compression type 2 = stored

- `ALIFE.EXE` has `cmprsmgr.c` ("Compression Layer", `decompressBuffer`) and
  `codecmgr.c` (`codecList[]`), plus the strings `a) UNDEFINED compression type
  0x%04x?` / `b) …`. So `+0` is very likely the compression-type selector
  (printed as 16 bits) that `decompressBuffer` switches on.
- Every entry has type 2 and zero reserved bytes. No entry has any other value,
  so the bundles give no data to reverse a real codec from.
- Every payload is already plain data: BMPs parse and are all `BI_RGB`
  (uncompressed), FLI/FLC/TILE/AKOS sizes match exactly, and iMUS `FRMT` chunks
  say 8-bit mono PCM (22050 Hz mostly, also 11025/44100/22222). Entropy fits
  that too: ALSV ~1.9 bits/byte, BMP avg 3.6, raw audio up to 7.7.
- So type 2 is almost certainly "stored / no compression" (or a pass-through
  codec), and stored size = uncompressed size. The 8 zero bytes are probably
  codec parameters that a real codec would use, such as the unpacked size or a
  checksum.
- Comparing bundles gives nothing extra: every FULL/MEDIUM entry is
  byte-identical to the same ID in ALIFE.000 (same type, size and reserved
  bytes). Only the absolute file positions differ, because the tables are smaller.

The questions these notes raised (what `+0`/`+4` mean, the `illegal block
len` check, `numBunFiles`/`fileSystem[]`, `preloadDirectory[]`) are answered
in [compbun.md](../compbun.md).

### Oddities

- ID 760 appears twice in ALIFE.000 (indices 344 and 345), and in FULL.000 too. Both are 30406-byte
  BMPs with different content. A linear ID lookup returns the first one.
- 173 of the 317 BMPs store `bfSize` without the 14-byte file header.
- 538 of the 757 iMUS files have an outer `iMUS` length 4 bytes larger than the
  data present. The `DATA` chunk still ends exactly at the payload end, so this
  is a quirk of the authoring tool, not truncation.

`FULL.000` and `MEDIUM.000` hold the same IDs at the same offsets for their
leading entries. They're the subsets a "medium"/"full" hard-disk install copied
locally so the CD was hit less.

## Payload types seen in ALIFE.000

| Sniffed as | Count | Notes |
|---|---|---|
| `iMUS` | 757 | iMUSE digital audio (music, voice, SFX). `iMUS` / `MAP ` / `FRMT` chunks, big-endian like SCUMM |
| BMP | 317 | Plain Windows 3.x BMPs, all 8-bit paletted, many 640×480 |
| ? (`bin`) | 39 | Unknown. 33 are 1144 bytes and start with 256 RGBX palette entries (IDs ~874–951, 1201–1213, 33). 5300/5301 are 1444-byte `u32` tables that look like sine/cosine scaled by 10^6. 333 is 115600 zero bytes |
| `TILE` | 31 | Tile sets for the map. Big-endian tag, little-endian length |
| `.cur` | 31 | Windows cursor resources |
| `ALSV` | 13 | Same format as the `ASCEN00x.ASC` files on the CD: saved/scenario worlds ("Dusk of the Demiurges") |
| FLI / FLC | 11 / 4 | Autodesk Animator animations (`0xAF11` / `0xAF12` at +4) |
| `CHAP`… | 10 | Plain-text chapters of the in-game guide |
| `AKOS` | 4 | SCUMM-style costume/animation (big-endian), likely the advisors Jasper and Aria |
