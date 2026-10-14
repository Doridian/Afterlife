# `COMP` compressed streams and the compbun codecs

compbun's Compression Layer (`cmprsmgr.c`) can transparently decompress files
that start with a `COMP` header. It splits a file into fixed-size blocks, and
each block names one of the codecs in `codecmgr.c`. Decompiled in
`src/compbun/codec.c` / `codec.h` (codecs) and handled by the cmprs layer.

**Afterlife ships no compressed data.** No file on the CD and no bundle entry
starts with `COMP`. (The one `COMP` byte sequence inside `ALIFE.000`, at
0x5e284f5, is coincidental bytes in an audio sample.) The bundles are all
"type 2" (stored, see [jiff.md](jiff.md)), ALSV saves (`ASCEN00x.ASC`) are
plain, and no game code writes `COMP` files. The layer is still active:
`snd_initialize` calls `compbun_init(1, 0)`, which pushes the Compression
Layer, so every `cbres_open` (resource loads at 0x4b9913, iMUSE streaming at
0x4b9b63) reads 4 bytes, finds no magic, seeks back and passes reads through.
The codecs never run.

The design (12-bit sample planes, 0x2000-byte blocks) is for iMUSE digital
audio. It looks like the same family as the `COMP` tables in The Dig's `.BUN`
files that ScummVM decodes (`compDecode`, codecs 0–6 and 10–12), minus the
later ADPCM codecs.

## Header

All fields are big-endian `u32`s (read by `preload_read_comp_header`
0x4e3640 and `cmprs_open` 0x4dad10):

```
+0    'COMP'                 0x434f4d50
+4    num_blocks
+8    block_shift            block size = 1 << block_shift (0x2000 = 13 in practice:
                             the layer's buffers are 0x2200 bytes, the codecs' 0x2000)
+12   last_block_size        decompressed size of the last block
+16   num_blocks entries of 16 bytes:
        +0  offset           of the compressed block, from the start of the header
        +4  comp_size        compressed bytes
        +8  codec            codec id (codecList index)
        +12 unused           not read
```

Decompressed size = `(num_blocks - 1) << block_shift + last_block_size`.
`cmprs_read` maps a read to block numbers (`pos >> block_shift`), reads
`comp_size` bytes at `offset`, calls `codec_decompress(codec, buf, comp,
comp_size)`, ignores its return value and treats the result as one full block.
The last decoded block is cached.

If the first 4 bytes aren't `COMP`, the layer seeks back 4 bytes and the file
is read unmodified.

### Preloaded tables (unused)

`preload.c` can hold the block table for a named file in memory
(`preloadDirectory[100]`, 0x28 bytes each), so the file needs no header:
name (16 bytes), num_blocks (+0x10), block size (+0x14), shift (+0x18), last
block size (+0x1c), sample format byte (+0x20), pointer to one `u16` per block
(+0x24). Each `u16` holds `comp_size - 1` in bits 0–12 and a 2-bit block type
in bits 13–14. `preload_codec_map` (0x677038) maps (format, type) to a codec:

| format | type 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| 0 (8-bit) | 0 | 1 | 2 | 3 |
| 1 (12-bit mono) | 0 | 4 | 5 | 6 |
| 2 (12-bit stereo) | 0 | 10 | 11 | 12 |

Block offsets are cumulative, starting at `(num_blocks + 1) * 16`, as if the
header were there. ALIFE.EXE never fills the directory: `preload_init` only
zeroes it.

## Codec manager

A codec is `proc(cmd, a, b, c)`, registered by `codec_register` (0x4e1950)
under the id it returns for cmd 2. `cmprs_init` registers 0, 1, 2, 3, 4, 5, 6,
0xa, 0xb, 0xc. `codec_decompress(id, dst, src, size)` (0x4e1990) calls
`codecList[id](4, dst, src, size)`, or returns 0 if nothing is registered.

| cmd | meaning |
|---|---|
| 0 / 1 | init / shutdown (all return 0) |
| 2 | return the codec id |
| 3 | compress `(dst, src, size)`. Every codec has one, but nothing in ALIFE.EXE issues cmd 3 |
| 4 | decompress `(dst, src, size)`, returns the LZSS output length |

## Codecs

| id | our name | algorithm |
|---|---|---|
| 0 | `codec_raw` | stored: copy `size` bytes (both directions) |
| 1 | `codec_lz` | LZSS |
| 2 | `codec_lz_delta1` | LZSS, then `x[i] += x[i-1]` |
| 3 | `codec_lz_delta2` | LZSS, then second-order delta (two delta passes, the second starting at index 2) |
| 4 / 5 / 6 | `codec_lz_12mono_{0,1,2}` | LZSS, delta2, then 12-bit plane merge. Mono; block starts 0 / 1 / 2 bytes into a 3-byte frame |
| 0xa / 0xb / 0xc | `codec_lz_12stereo_{0,1,2}` | Same, but the high-byte plane is split by channel |

### LZSS (0x4e5ee0 decode, 0x4e5ea0 encode)

Window 4096, matches of up to 18 bytes from the encoder (the format allows
256). Flags are little-endian `u16`s consumed LSB first. A new flag word is
read **as soon as** the previous one is used up, so flag words sit between
operand bytes:

```
1            literal: next byte
0 0 h l  d   short match: length (h*2+l)+3 (3..6), offset d - 256 (-256..-1)
0 1  a b     long match: offset ((b & 0xf0) << 4 | a) - 4096 (-4096..-1),
             length (b & 15) + 3; if that nibble is 0, the next byte n
             gives length n + 1, and n == 0 ends the stream
```

Offsets are relative to the current output position (byte-wise copy, so
overlapping runs work). The decoder ignores the input size: it stops only at
the end marker.

The encoder is Haruhiko Okumura's 1989 binary-tree LZSS (`InsertNode` /
`DeleteNode`, N = 4096, F = 18). Differences: the window isn't pre-filled with
spaces (the first position is 0 and nothing before it can match), only
matches of 3 or more are used, and it uses the long form when the offset is
256 or more or the length is over 6. It gives up once the output would reach
the input size, returning a size larger than the input, so the caller would
store the block raw. The end marker is `0 1 00 00 00`, plus one padding flag
bit if a flag word was just completed, because the decoder reads the next
word right away.

### 12-bit sample planes (codecs 4–6, 0xa–0xc)

iMUSE packs two 12-bit samples into 3 bytes `b0 b1 b2`:
`A = (b1 & 0x0f) << 8 | b0`, `B = (b1 & 0xf0) << 4 | b2`. For a block of L
bytes the encoder makes `n = 8L / 12` samples into two planes, applies delta2
to the whole buffer and LZSS-compresses it:

```
[0, n)          top 8 bits of each sample (mono: in order;
                stereo: even samples, then odd samples, i.e. one channel per half)
[n, n + n/2]    low 4 bits of each sample, two per byte
```

Blocks are 0x2000 bytes, and 0x2000 isn't a multiple of 3, so consecutive
blocks start 0, 2, 1, 0, … bytes into a frame. The three variants of each
codec cover those phases. Bytes of a partial frame at the block edges are
stored at fixed places, e.g. phase 2 keeps the leading byte at `L - 1` and the
trailing byte at `n - 1`. Compressing synthetic 12-bit audio confirms the
phases: each variant compresses best (to 55% mono, 63% stereo) when the block
really starts at its phase, and with the wrong phase it doesn't compress at all
or compresses much worse.

The transform is only lossless when `L % 3 == 2` (n odd), which is the case
for 0x2000-byte blocks. For other lengths, some variants lose the last one or
two bytes. The original binary behaves the same way, so presumably the tool
that made these files never used the 12-bit codecs for a short last block.

## Verification

There is no real compressed data, so `codec.c` was checked against the
original code run under an x86 emulator (unicorn, loading ALIFE.EXE's sections
and calling the codec procs directly). Inputs were 67 buffers: 8-bit iMUS PCM
from `ALIFE.000`, README text, random data, zeros, synthetic 12-bit
mono/stereo audio, and random cuts of 3–8192 bytes. Results, for each of the
10 codecs:

- compress (cmd 3): output byte-identical to the original in 670/670 cases
  (stale scratch and window bytes zeroed on both sides, see below);
- decompress (cmd 4): output and return value identical in all 156 cases
  where the data compressed;
- round trip: lossless except for 13 short last blocks with `L % 3 != 2` in
  the 12-bit codecs, where the original is equally lossy.

Port deviations: the decoders check bounds and return -1 on corrupt input.
Compressors leave `src` untouched (the original transformed it in place and
restored it from `codec_saveBuffer`). Scratch and LZ window buffers are zeroed
where the original reads stale memory, at most one byte past the input.
