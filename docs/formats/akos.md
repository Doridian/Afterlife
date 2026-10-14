# AKOS costumes

AKOS is LucasArts' SCUMM v7 costume format (Full Throttle, The Dig). ALIFE.EXE
has a trimmed copy of that engine and uses it for one thing: the talking
heads of the advisors Jasper and Aria.

Port: `src/formats/akos.c` (parsing, cel decoders), `src/gfx/akos.c` (the
interpreter and renderer), `src/gfx/akos_talk.c` (the talking-head module).
Test: `tests/akos_test.c`. ScummVM's `engines/scumm/akos.cpp` (GPL) was the
reference for names; behaviour follows the ALIFE.EXE disassembly.

## Resources

ALIFE.000 has four AKOS resources:

| ID | Codec | AKPL | Anims (x4 dirs) | Cels | Contents | Used by |
|---|---|---|---|---|---|---|
| 2003 | 1 | 32 | 57 (52 defined) | 84 | Jasper, 138x118 head and shoulders | advisor window, talker 1 |
| 2004 | 1 | 64 | 63 (58 defined) | 77 | Aria, 114x123 | advisor window, talker 2 |
| 2002 | 1 | 32 | 23 (18 defined) | 80 | a small walking man in a red jacket (about 20x40) | loaded by 0x46fc14 for talker 4, which `akos_talk_open` rejects, so never shown |
| 2005 | 5 | 256 | 2 (1 defined) | 122 | the LucasArts logo animation | nothing; codec 5 draws nothing anyway |

## Chunk layout

Big-endian IFF: `"AKOS"`, u32 BE total length, then chunks, each a 4-byte tag
and a u32 BE length that includes the 8-byte header. Everything inside the
chunks is little-endian, except that AKSQ opcodes are big-endian words.
The game looks chunks up by tag every time (`res_find_block` 0x4dbc23).

| Chunk | Contents |
|---|---|
| `AKHD` | 12 bytes: u16 version (1), u16 flags, u16 number of AKCH entries, u16 number of cels (unused), u16 codec, u16 unused (16). Flags bit 0: draw mirrored in direction 0 too. Bit 15 is set in 2003-2005 and is not read |
| `AKPL` | one byte per costume colour: the palette index it was drawn with. Its size picks the codec 1 depth: 32 -> 5-bit colour / 3-bit run, 64 -> 6/2, otherwise 4/4 |
| `RGBS` | R, G, B per AKPL entry. The game matches these against the live palette |
| `AKSQ` | sequence bytecode, see below |
| `AKCH` | u16 offset (into AKCH) per animation index = animation * 4 + direction; 0 = undefined. A record is a u16 limb mask (bit 15 = limb 0), then per set bit a command byte and, unless the command is 1, 4 or 5, u16 start and u16 length into AKSQ |
| `AKOF` | 6 bytes per cel: u32 offset into AKCD, u16 offset into AKCI |
| `AKCI` | per cel: u16 width, u16 height, s16 rel_x, s16 rel_y, s16 move_x, s16 move_y. In 2003-2005 the entries are only 4 bytes (width, height), and AKOF steps by 4 |
| `AKCD` | cel images |
| `AUXD`, `AKFO`, ... | not present. AKFO (the "cel goto" table of opcode 0xc088) is looked up but missing, which would give "AKOS:NO CEL GOTO OFFSET TABLE" |

## Cel codecs

**Codec 1** (`akos_codec1` 0x4dd6bb). One run-length stream over the whole
cel, column by column (left to right, each top to bottom). Each byte is
`color << run_bits | run`. A run of 0 means the run is the next byte; a
zero there wraps the original's counter, so the colour fills the rest of
the cel. Runs continue from one column into the next. Colour 0 is
transparent. Other colours go through the draw palette (below).

The renderer has 13 inner loops: unscaled or scaled, with or without
y clipping, with one or two mask planes (a 41-byte-wide bitmask, 8 pixels
per byte), and three shadow modes (colour 13 = remap the background through
a table at 0x64c510, colours below 8 = one of 8 remap tables, masked pixels
remapped). None of the masked or shadow paths can run in Afterlife: the mask
tests 0x4d5c11 / 0x4d5c23 return 0, the mask selector 0x64c410 and the
shadow mode 0x64cd90 are never set. Scaling (the 768-byte bit-reversal table
at 0x675df8) is only used when the scale isn't 0xff, 0xff, and the talker
always passes 0xff, 0xff.

**Codec 5** is BOMP: per row a u16 byte count, then codes where
`n = (code >> 1) + 1`, bit 0 set means repeat the next byte n times and
clear means n literal bytes. 255 is transparent. In ALIFE.EXE the codec 5
renderer (0x4d7a01) is an empty function. The port decodes it only in
`akos_decode_cel` for tools.

## Drawing

`akos_draw_actor` (0x4d76dc) draws actor `a`'s 16 limbs at (x, y):

1. Draw palette (0x4d5c35): entry i = the actor's remap[i], or AKPL[i] where
   the remap holds 0xff. The remap is built once per talker by
   `akos_build_remap` (0x4d5ef7): each RGBS colour, scaled by r/g/b / 256
   (the talker passes 255), matched against the current palette entries
   11-175 on 6-bit components with weights 30 R / 60 G / 20 B (0x4d5d9d).
2. Mirror flag: set if the direction isn't 0 or AKHD bit 0 is set, then
   inverted if opcode 0xc08a set the actor's flip value. Mirrored cels grow
   to the right of x; unmirrored ones grow to the left and their x offset is
   negated.
3. Each active limb that isn't stopped draws the code at its current
   position (0x4d6713): 0xc001 draws nothing; 0xc020 draws a list of cels at
   offsets given in the bytecode; anything else is a cel number
   (`code & 0xfff`) drawn at AKCI rel_x/rel_y plus a running x offset. That
   offset then grows by move_x - move_y (a bug: SCUMM moves y by move_y).
   Because of the 4-byte AKCI entries, rel/move would read the next cels'
   sizes, but all of Jasper's and Aria's sequences use 0xc020, so only
   width and height are read.

Every cel's box is merged into the talker's rectangle (0x4d3325, indexed by
actor number), which the talker erases and repaints on the next step.

## Animation

Per actor (arrays indexed by actor, 1..31; record at 0x649980 + 0x114 * a):
16 limbs of {command type, current, start and end AKSQ offset, AKCH index},
a stopped mask, 16 variables (0x64bb90), 8 sound slots (0x64c420),
direction (0x64bb10), flip (0x64cd10), the 256-byte colour remap
(0x647a20), and the costume slot (0x64bad0). Costume slots 1..31 hold the
loaded resources (0x54eca0).

`akos_set_anim(a, anim, dir)` (0x4d6434) resolves aliases 0x38-0x3c (never
set, so 0), resets the actor for animation 0, and starts AKCH entry
`anim * 4 + dir` on all limbs (0x4d6080). 0x3e only turns. Turning restarts
every limb on the same animation in the new direction (0x4d62a5). Limb
commands: 1 = off, 2 = cel list that loops, 3 = cel list that stops at its
end, 4 / 5 = set / clear the stopped bit, 6 = bytecode, 7 = frozen.

`akos_increase_anims(a)` (0x4d78c6) steps each active limb once
(0x4d6b14): skip the code at the current position (one cel for types 2/3,
the whole instruction for type 6), then run instructions until one that
draws (a cel, 0xc001 or 0xc020) or jumps. It returns whether any limb
moved; the talker uses "nothing moved" to detect a head at rest.

Opcodes (type 6). Arguments: b(n) = byte at +n, w(n) = s16 LE at +n.

| Op | Len | Meaning |
|---|---|---|
| cel | 1/2 | draw cel (two bytes when bit 7 is set, `& 0xfff`) |
| c001 | 2 | draw nothing |
| c010 | 5 | var[b4] = w2 |
| c015 | 3 | queue sound slot b2 (1..7) |
| c020 | 3+ | draw b2 cels: each s16 x, s16 y, cel (1 or 2 bytes) |
| c030 | 4 | jump to w2 |
| c031 | 5 | if var[b4]: clear it, jump to w2 |
| c040 | 5 | var[b4] += w2 |
| c050, c08b, c08c, c08d | 3/6/3/3 | nothing |
| c060 | 2 | var[0]++ |
| c061 | 2 | queue sound slot 1 |
| c070-c075 | 7 | if var[b4] (== != < <= > >=) w5: jump to w2 |
| c080 / c081 | 3 | queue "start animation" b2 / var[b2] |
| c082 | 7 | var[b6] = random in w2..w4 (MSVC rand) |
| c083 | 3 | queue "clip" b2 |
| c084 | 4 | queue "start animation var[b3]" for actor var[b2] |
| c085 | 6 | actor var[b2]'s var[b3] = w4 |
| c086 | 2 | queue "hide" |
| c087 | 6 | queue "draw offset" w2, w4 |
| c088 | 3 | jump through AKFO[var[b2] - 1] |
| c089 | 8 | queue sound slot b2 volume/pan/priority (b4 = 1/2/3) = b6 ("Cyst Stub") |
| c08a | 4 | flip = w2 |
| c090-c095 | 5 | unless var[b4] (op) w2: skip until c09f |
| c09f | 2 | stop skipping |

Unknown 0xc0xx codes report "UNDEFINED uSweat TOKEN" through an empty stub
and stop like a cel. The command queue (0x54fff8, room for 10 entries) is
filled but never processed: `akos_process_queue` (0x4d764e, which would start
queued animations) has no callers. Jasper's and Aria's sequences only use
0xc020 and 0xc030; 2002 uses only cel lists.

## The talking heads

`akos_talk_*` (0x4d2c50-0x4d3e71) runs up to three talkers (records at
0x54f158 + 0x68 * n). Talker n draws and steps AKOS actor n into its window's
surface. `akos_talk_open` (0x4d2ce7) takes the window, costume slot, scale,
text and background colours and a frame rate (8 fps for the advisors);
open and set_anim address the AKOS actor by costume slot, which matches the
talker number because Jasper's costume is loaded first.

The advisor window (0x417230 creates it, 0x417b94 shows it, 0x425e3b is the
tutorial variant; window proc 0x41751b or 0x41f2c9) opens talker 1 with
Jasper at (0x50, 0x8b) and talker 2 with Aria at (0xeb, 0x8b), direction 2,
animation 1. The idle task (0x4d35ac) steps each talker at its rate, erases
and redraws it when it moved, and after 7 still steps sends message 6001 to
the window, which picks a random idle animation (Jasper 0x29 / 0x2c, Aria
0x2d / 0x3d). Message 0x1772 sets both to animation 6.

Lip sync: `snd_talk_start` (0x4d3b8a) shows the subtitle (cleared after
strlen / 10 steps once the voice has ended), streams the voice into sound
slot 199 and sets a one-shot iMUSE trigger. Its callback (0x4d3d00) parses
the number in the voice's text marker into the talker record; the idle task
forwards it to the window as message 6000. The window re-arms the trigger
(0x4d3e49) and maps the number m to a mouth: m + 1 < 0x41 is a Jasper
animation (m + 1), otherwise Aria's (m - 0x3f). So each mouth shape is an
AKOS animation, a single 0xc020 frame followed by a jump back.
