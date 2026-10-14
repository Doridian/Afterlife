# FLI/FLC animations

The bundles hold 15 Autodesk Animator animations (IDs 5000-5209 in
`ALIFE.000`; none in `FULL.000`/`MEDIUM.000`). They're standard FLI/FLC files
with **no custom chunk types**. The player is decompiled in
`src/formats/flc.c` (`flc_*` symbols, 0x409dc0-0x40b1a1 in `ALIFE.EXE`).
`tests/formats_dump` decodes all of them.

| IDs | Kind | Size | Frames | Speed | Chunks used |
|---|---|---|---|---|---|
| 5000 | FLI | 320x200 | 224 | 5/70 s (71 ms) | COLOR_64 + BYTE_RUN (frame 1), DELTA_FLI |
| 5101-5104 | FLC | 640x480 | 3-10 | 67 ms | COLOR_256 + COPY (frame 1), DELTA_FLI |
| 5200-5209 | FLI | 320x200 | 5-80 | 71 ms | COLOR_64 + BYTE_RUN (frame 1), DELTA_FLI |

5000 is the LucasArts logo intro (`intro_play`, 0x4265dc). Every file has a
ring frame (frames + 1 frame records), one palette chunk (in frame 1, a single
256-colour packet), and no prefix (0xF100) chunk. The FLCs use the FLI-style
byte delta (DELTA_FLI, type 12) even at 640x480, and their first frame
(a raw COPY chunk) is 308000 bytes. The FLI headers have `oframe2` filled in,
like Animator Pro writes them, though the player ignores it for FLI.

## Header (128 bytes, little-endian)

| Offset | Type | Field | Use |
|---|---|---|---|
| 0 | u32 | size | not checked |
| 4 | u16 | magic | 0xAF11 FLI, 0xAF12 FLC, anything else: error -3 |
| 6 | u16 | frames | frames `flc_play` shows (the ring frame isn't counted) |
| 8 | u16 | width | |
| 10 | u16 | height | |
| 16 | u32 | speed | FLC: ms/frame. FLI: 1/70 s, converted with `speed * 1000 / 70` |
| 0x50 | u32 | oframe1 | file offset of frame 1. Forced to 128 for FLI |
| 0x54 | u32 | oframe2 | file offset of frame 2, used to loop an FLC |

## Frames and chunks

A frame is a 16-byte header (`u32 size`, `u16 type`, `u16 chunks`, 8
unused bytes) and `size - 16` bytes of chunks. A type other than 0xF1FA is
error -4. A frame with no chunk data is skipped and doesn't advance the frame
counter. Each chunk is `u32 size` (including its 6-byte header), `u16 type`,
data; the player always steps by `size`.

| Type | Name | Decoder | Notes |
|---|---|---|---|
| 4 | COLOR_256 | 0x40a7ac | 8-bit RGB |
| 7 | DELTA_FLC (SS2) | 0x40aa10 | supported, unused by the data |
| 11 | COLOR_64 | 0x40a7d0 | 6-bit RGB, scaled with `<< 2` (so 63 -> 252) |
| 12 | DELTA_FLI (LC) | 0x40a8e8 | |
| 13 | BLACK | 0x40abe8 | unused by the data |
| 15 | BYTE_RUN | 0x40a7f4 | the per-line packet count byte is skipped, lines run until `width` pixels are done |
| 16 | COPY | 0x40ac9c | |
| 5, 6, 8, 9, 10, 14, other | | | ignored |

All decoders draw through four clipped span primitives (`flc_fill_run`,
`flc_fill_run16`, `flc_copy_span`, `flc_put_pixel`) into a target (pixels,
pitch, height; the pitch doubles as the clip width) at the player's x/y offset
(`flc_center` centres the animation in the target).

## Player quirks

- **Streamed vs in-memory.** `flc_open(player, id, in_memory)`. Streamed
  players read each frame from the bundle into a static 51000-byte buffer
  (`flc_frame_buf`, 0x639b90); a larger frame is error -2, so the FLCs must be
  opened in memory. In-memory players read the whole resource
  (`size - 128` bytes after the header) and can be cloned (`flc_clone`,
  0x40a0d4) so several players share one buffer. The intro does that to tile
  the 320x200 animation over larger screens.
- **Palette.** The COLOR callbacks (`flc_apply_palette`, 0x40a469) ignore the
  packet's skip/count. They convert 256 triplets from the packet data and set
  only entries 10..245 (Windows reserves the 20 static colours), and only for
  in-memory players. Streamed animations never change the palette; they're
  shown with the current one. COLOR_64 shifts the data in place (streamed) or
  in a copy (in memory). The port's decoder handles packets properly and
  writes all 256 entries into the caller's palette; applying them, and the
  10..245 restriction, is up to the caller.
- **Timing.** `flc_play` (0x40a27f) takes `timeGetTime()` before decoding a
  frame, blits the frame rectangle, then spins pumping messages until
  `start + scale * speed / 1000` ms have passed. The playback context
  (`flc_target_init`, 0x40b0c9) sets `scale` to 1000. Clearing the player's
  "playing" byte (+0xa9) stops it after the current frame. The intro instead
  runs its own loop (frame delay derived from `speed`, divided by 1500).
- **Looping.** `flc_rewind` (0x40a145) goes back to `oframe1` and resets the
  frame counter. `flc_seek_ring_frame` (0x40a1cf) jumps to `oframe2` with the
  counter at 1 (FLC only), so after the ring frame playback continues at
  frame 2.
- **Edge cases the port fixes.** BYTE_RUN with a zero count would loop forever;
  DELTA_FLC word runs clipped on the left by an odd number of pixels come out
  byte-swapped (`flc_fill_run16` stores whole words from the clipped start);
  a count byte of -128 in DELTA_FLI is negated back to -128 and draws nothing.
  None of these occur in the data.

## Player struct (0xaa bytes)

| Offset | Field |
|---|---|
| 0x00 | the 128-byte header (0x50 rewritten to 128 for FLI, 0x10 to ms) |
| 0x80 | resource file handle (streamed) |
| 0x84 | file position of the header start |
| 0x88 | in-memory buffer (data after the header) |
| 0x8c | in-memory read pointer |
| 0x90 | in-memory buffer size |
| 0x94 | u8 in-memory flag |
| 0x95 | int frame counter (unaligned) |
| 0x99 | pointer to 0x651c2c (unknown) |
| 0x9d / 0xa1 | int x / y offset in the target |
| 0xa9 | u8 playing |
