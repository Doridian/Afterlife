# `.000` resource bundles ("JIFF")

`ALIFE.000`, `FULL.000` and `MEDIUM.000` share one format. It's IFF-like, but tags
are stored as little-endian `u32`s (so `JIFF` reads as `FFIJ` on disk) and lengths
are little-endian. Handled by the `compbun` library in the original
(see [binaries.md](../binaries.md)).

Tools: `tools/jiff.py` (list/stats/dump), `src/formats/jiff.c` (runtime reader).

## Layout

```
"JIFF"  u32 0                     form header, length unused
"HEAD"  u32 4     u32 count       number of resources
"OFST"  u32 4*n   u32[count]      payload offsets, relative to RESC body start
"RCID"  u32 4*n   u32[count]      resource IDs, same order as OFST
"RESC"  u32 len   ...             resource entries back to back until EOF
                                  (len only covers the first entry; ignore it)
```

Each entry in `RESC`:

```
+0   u32  flags    always 2 so far (compression/codec selector? unconfirmed)
+4   u32  length   bytes following +8, i.e. 8 + payload size
+8   u8[8]         zero
+16  payload       an embedded file in its own format
```

`FULL.000` and `MEDIUM.000` hold the same IDs at the same offsets for their
leading entries. They're the subsets a "medium"/"full" hard-disk install copied
locally so the CD was hit less.

## Payload types seen in ALIFE.000

| Sniffed as | Count | Notes |
|---|---|---|
| `iMUS` | 757 | iMUSE digital audio (music, voice, SFX). `iMUS` / `MAP ` / `FRMT` chunks, big-endian like SCUMM |
| BMP | 317 | Plain Windows 3.x BMPs, all 8-bit paletted, many 640×480 |
| ? (`bin`) | 39 | Unknown. Several 1160-byte entries around IDs 921–951 look like tables or palettes |
| `TILE` | 31 | Tile sets for the map. Big-endian tag, little-endian length |
| `.cur` | 31 | Windows cursor resources |
| `ALSV` | 13 | Same format as the `ASCEN00x.ASC` files on the CD: saved/scenario worlds ("Dusk of the Demiurges") |
| FLI / FLC | 11 / 4 | Autodesk Animator animations (`0xAF11` / `0xAF12` at +4) |
| `CHAP`… | 10 | Plain-text chapters of the in-game guide |
| `AKOS` | 4 | SCUMM-style costume/animation (big-endian), likely the advisors Jasper and Aria |
