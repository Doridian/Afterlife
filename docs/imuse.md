# iMUSE

Afterlife's sound is LucasArts' digital iMUSE, the same generation as in Full
Throttle and The Dig. Its modules are dated May 10 1996, and its source
paths are `iMUSE:*.c` and `C:\iwin\...`. All audio is PCM, mixed in process
and played through winmm `waveOut*`. There is no MIDI and no DirectSound.

Port: `src/imuse/` keeps the original module names. `src/game/sound.c` is
the game's sound module on top of it. `tests/imuse_test.c` checks the port
by behaviour. The port is written from the ALIFE.EXE decompilation, and no
ScummVM code is used. ScummVM's iMUSE is C++ built on Common::/Audio::, so a
submodule wouldn't fit a C11 port.

## Layers

```
game                      snd_play / snd_stop / snd_slot_* / the advisor voice     src/game/sound.c
  so (0x4b9470..)         200 sound slots, iMUSE init data, host callbacks
iMUSE command function    imuse_command(opcode, args) 0x4d4930                      commands.c
  commands.c              init, pause, per-sound commands, 60 Hz / 10 Hz tick
  files.c groups.c        host callbacks, group volumes
  fades.c triggers.c      parameter fades, marker triggers, deferred commands
  time.c                  the periodic handler handed to the host
  wave:wave.c             front of the wave modules (slice hold counter)
    tracks.c              playing sounds (one per mixer voice), the mixing slice
    dispatch.c            MAP walker: regions, markers, jumps, crossfades, stream zones
    streamer.c            stream ring buffers filled through the host's seek/read
    mixer.c               amplitude tables, mix routines, soft clip
  wavedrvs:win32:waveapi.c  waveOut driver   ->  port: plat_audio_* (waveapi.c)
```

## Module map (ALIFE.EXE)

All symbols are in `symbols/ALIFE.EXE.txt`. The prefixes are `imuse_` (core),
`wave_` (wave.c, `wave_tracks_`, `wave_dispatch_`, `wave_api_`), `mixer_`,
`streamer_` and `snd_` (the game).

| Module | Range | Contents | Port |
|---|---|---|---|
| game wrappers | 0x4d3e49..0x4d4140 | `imuse_start_sound` 0x4d3f80, `imuse_set_param` 0x4d3fd0, etc.: one call each to the command function stored at 0x54f2f8 | imuse.h |
| waveapi.c | 0x4d4150..0x4d4790 | init (8 × 1024-frame `WAVEHDR`s, 22050 Hz, 16-bit if the card has any 16-bit format), get_buffer, pause/resume (close/reopen), dump | replaced |
| commands.c | 0x4d4930..0x4d5170 | dispatcher, opcode table 0x674678, init/terminate, tick 0x4d4a10, save/restore | decompiled (no save/restore) |
| wave.c | 0x4d9180..0x4d94f0 | `wvSlicingHalted` counter, halting wrappers | decompiled |
| triggers.c | 0x4d9510..0x4d9da0 | `trigs[8]`, `defers[8]` | decompiled |
| fades.c | 0x4da110..0x4da430 | `fades[16]` | decompiled |
| groups.c | 0x4da590..0x4da690 | `groupVols[16]`, `groupEffVols[16]` | decompiled |
| time.c | 0x4da730..0x4da7f0 | timer handler 0x4da7b0 | decompiled |
| files.c | 0x4da860..0x4daa40 | init data callbacks | decompiled |
| tracks.c | 0x4de0f0..0x4dec20 | `tracks[8]`, slice 0x4de440, set/get param | decompiled |
| dispatch.c | 0x4dee70..0x4e0950 | `dispatches[8]`, `streamZones[50]`, fade buffers | decompiled |
| streamer.c | 0x4e0d90..0x4e17c0 | `streams[3]`, plus list/string/BE helpers | decompiled |
| mixer.c | 0x4e36a0..0x4e5d90 | tables, 12 mix routines, output | decompiled |
| so (game) | 0x4b9470..0x4ba0f0 | `snd_initialize`, slots, callbacks | decompiled (src/game/sound.c) |
| game sound API | 0x4750f0..0x4761a0 | `snd_play`, `snd_stop`, preload, the music, the audio settings dialog | ported (src/game/sound.c, music.c, src/ui/audiodlg.c) |

0x4de0f0..0x4de170 are a host query hook and a printf-style message hook
(`wave_set_msg_callback`, `wave_printf`) that nothing in Afterlife installs.
Every module also registers a VHUMOR module record with a dump function (the
`*_dump` names). Those dumps are where the field names below come from. They
aren't ported.

## Command API

The command function takes an opcode plus up to 14 ints. Opcodes 21..24 are
empty. Return codes: 0 ok, -1 error, -3 not ready, -4 not playing, -5 bad
argument, -6 no free slot.

| Op | Port function | Game wrapper | Notes |
|---|---|---|---|
| 0 | `imuse_initialize(init)` | 0x4d3ea0 | returns version 0x30 |
| 1 | `imuse_terminate` | 0x4d3ee0 | |
| 3/4 | `imuse_pause` / `imuse_resume` | 0x4d3f10 / 0x4d3f30 | nested; the first pause closes the device |
| 5/6 | save / restore | (VHUMOR save hook 0x4d4d90) | raw struct dumps, not ported |
| 7 | `imuse_set_group_vol(group, vol)` | 0x4d3f50 | returns the old value; -1 reads |
| 8 | `imuse_start_sound(sound, prio)` | 0x4d3f80 | sound in memory, must start with `iMUS` |
| 9 | `imuse_stop_sound` | 0x4d3fb0 | |
| 10 | `imuse_stop_all_sounds` | | |
| 11 | `imuse_get_next_sound(after)` | | iterates the playing sounds |
| 12/13 | `imuse_set_param` / `imuse_get_param` | 0x4d3fd0 / 0x4d4000 | parameters below |
| 14 | `imuse_fade_param(sound, param, val, ticks)` | 0x4d4030 | 60 Hz ticks |
| 15/16 | `imuse_set_hook` / `imuse_get_hook` | | jump hook 0..128 |
| 17 | `imuse_set_trigger(sound, marker, op, fn, args)` | 0x4d4060 | one-shot |
| 18/19 | check / clear trigger | | -1 wildcards |
| 20 | `imuse_defer_command(ticks, op, fn, args)` | | |
| 25 | `imuse_start_stream(sound, prio, bufID)` | 0x4d40c0 | |
| 26 | `imuse_switch_stream(old, new, fade_ms, sync, keep_offset)` | | crossfade ≤ 2000 ms |
| 27 | `imuse_process_streams` | 0x4d40f0 | refill, called by the host |
| 28/29 | query / feed stream | | host-fed streams |

Parameters: 0x100 playing count, 0x200 pending triggers/defers that start
it, 0x300 marker, 0x400 group (0..15), 0x500 priority, 0x600 volume, 0x700
pan (64 = centre), 0x800 detune, 0x900 transpose (stored, unused by the
mixer), 0xa00 mailbox (non-zero: "radio chatter" high-pass, 8-bit mono same
rate only), 0x1800 is-streaming, 0x1900 stream bufID.

**Groups in Afterlife.** 0 master, 1 effects, 2 voice, 3 the music slider,
4 music. Every 100 ms the tick moves group 4 towards group 3, or towards 5/8
of it while a group-2 sound plays: down 18 per step, back up at once.

**Init data** (0x523148, 0x5c bytes, `ImuseInitData`):

| Offset | Field |
|---|---|
| +0x00 | sound_addr |
| +0x04 | sound_map (NULL) |
| +0x08 | trigger callback (NULL) |
| +0x10/+0x14 | host handler / µs |
| +0x18 | out: timer handler |
| +0x1c | µs per tick = 16666 |
| +0x20/+0x24 | wave handler / µs |
| +0x28 | tick count |
| +0x2c | command function |
| +0x34 | wave driver entry |
| +0x3c | driver arg |
| +0x40 | half-rate |
| +0x44 | mixer voices ("Mixer channels") |
| +0x4c/+0x50/+0x54 | seek / read / buf_info |
| +0x58 | hwnd |

## The game's sound module

`snd_initialize` (0x4b9510) reads the INI, he_mallocs the two stream buffers,
hooks the C File Layer (resources by ID), runs `vh_initialize`,
`compbun_init(1, 0)`, fills the init data and calls `imuse_initialize`.

- **Slots** (0x5231a8, 200 × 0x1c `{data, PlatFile, cbres handle, base,
  size, state, time}`). Slot n = resource 0x10000 + n, and iMUSE's sound ID
  is the slot number. Slot 199 is the advisor voice. States: 0 free, 1
  locked, 2 streaming, 3 loaded. `snd_idle` frees loaded slots that have
  been unused for 60 s.
- **Effects** `snd_play(id, prio)` (0x4751af): load the whole resource
  through compbun, then start the sound in group 1.
- **Music** `snd_play(id, 4)`: at most two at once. It streams through bufID
  1 in group 4.
- **Voice** (0x4d3b8a): streams through bufID 2 in group 2. It sets one
  trigger whose callback (0x4d3d00) parses the number out of the `MK nn`
  lip-sync markers into the talking head's frame. Triggers are one-shot, so
  this fires once per line.
- **Stream buffers** `buf_info` (0x4b9eb5): `{buf, size, loadSize,
  criticalSize}` = "Music stream"/"Voice stream" `size, critical, load`.
  The second and third numbers are used: iMUSE reads them through
  buf_info.
- **Timer.** `snd_idle` registers `init.timer_handler` with
  `tm_add_service(.., 60)` the first time it runs, and calls opcode 27
  (stream refill) every 100 ms.
- **Audio Settings** (0x4756f4 create, 0x475bd9 window proc;
  `src/ui/audiodlg.c`): the Global menu's "Audio Settings" (F9) window,
  created hidden by game_main, which first sets groups 0, 1, 2 and 4 to
  127. Four toggles (bitmap 0xfa1) and four sliders (0..127): Master
  (group 0; turning it back on also pauses and resumes iMUSE), Music
  (groups 3 and 4; off also stops the track and clears
  `snd_music_enabled`, 0x47558c, and on picks a new one, 0x4755b9), Voice
  (group 2, through `imuse_set_voice_vol` 0x4d4110) and Sound FX (group
  1 and `snd_sfx_enabled`, 0x4755dd / 0x4755f2). Switching off remembers
  the group's volume for switching back on; a slider ignores input while
  its toggle is off. Nothing is saved: every start is at full volume
  with everything on.

## Data formats

An iMUS resource is a big-endian `iMUS` header, then a `MAP` of events, then
`DATA`. All events are `tag, size, offset, ...`:

| Event | Fields | Meaning |
|---|---|---|
| FRMT | +12 ?, +16 bits, +20 rate, +24 channels | sets the sample format |
| REGN | +12 length | a run of PCM |
| TEXT | +12 string | a marker |
| JUMP | +12 destination, +16 hook ID, +20 fade ms | a jump |
| STOP | (none) | the end |

The offsets are file offsets. ALIFE.000 holds 757 iMUS resources, all
8-bit mono, mostly at 22050 Hz (also 11025, 44100 and 22222). They contain
16950 REGN, 16194 TEXT (lip-sync `MK nn` and labels) and exactly one JUMP
(65549, a 1.48 s loop).

## How it runs

- **Mixing slice** (`tracks_slice`, every timer tick). The driver hands out
  at most one 1024-frame block per tick, and only when its oldest buffer
  has played. The slice clears the mix buffer, runs each track's dispatcher
  (`dispatch_process`) and converts the mix to the output buffer.
- **Dispatcher.** At the end of a region it walks map events at the current
  offset: TEXT fires triggers, FRMT sets the format, JUMP is taken when the
  hook matches, REGN starts the next region, STOP ends the sound. Hook 0
  jumps always, unless the track's hook is 0x80 (which is then reset); hook
  n jumps once when the track's hook is n.
- **Backward jumps** keep up to the jump's fade length of the audio after
  the hook point in a fade buffer. The old position fades out while the new
  one fades in.
- **Streams.** A ring buffer whose size is set in the INI, refilled
  `loadSize` at a time. Streams below `criticalSize` are refilled first.
  The dispatcher keeps "stream zones", the file ranges the ring holds, and
  `predict_stream` rewires loading at a jump before playback reaches it.
  `switch_stream` replaces one streamed sound by another with a crossfade
  of up to 2 s.
- **Mixer.** Volume is quantised to 17 levels (0, 7, 15, ..., 127).
  `amp8[l][s] = l*16*(s-128)/127`, `amp12[l][s] = l*(s-2048)/127`, both in
  12-bit range. Pan uses a 17×17 table at 0x6771a8 (centre column 8) that
  equals `round(v * sin(p*pi/32))` exactly, so the port computes it. 16-bit
  input is cut to 12 bits. Resampling: same rate, 2× up (linear), 2× down
  (drop), else nearest-sample stepping. Detune, transpose and pitch shift
  are never applied.
- **Soft clip.** The sum of up to 8 voices goes through `y = (j*n*K/(n*2047
  + j*(n-1)) + 1)/2`, with K = 65534 for 16-bit or 254 for 8-bit output.

## Port notes

- **Timing.** The host calls `imuse_timer_handler()` 60 times a second. In
  the port, `snd_idle` registers it with the emulated timer
  (`tm_add_service`, polled from `sys_pump`), as the original does. The
  timer's period is 16 ms against iMUSE's 16666 µs, which only matters for
  the virtual device. Single-threaded, so the halt counter is kept only for
  structure.
- **Output** (`waveapi.c`): signed 16-bit stereo at 22050 Hz to
  `plat_audio_queue`. A block is handed out while fewer than 8 blocks
  (0.37 s, the original's latency) are queued. `plat_audio_pause` replaces
  waveOutReset/Close. If the device can't be opened, or a sink is set
  (`imuse_wave_set_sink`, used by tests), a virtual device plays one tick of
  audio per tick, so sounds still end and the game's "wait for the voice"
  loops terminate.
- **Fixes and guards**, each marked in the code:
  - The 16-bit soft-clip table is computed in 64 bits. The original's
    32-bit `IDIV` overflows for mix sums above 4096 with 8 voices (above
    about two full-scale voices), producing wrapped, negative output.
    `-DIMUSE_MIXER_ORIGINAL_OVERFLOW` restores it.
  - Soft-clip and radio-chatter indexes are clamped.
  - Streamer end-marker writes wrap.
  - A NULL stream read during a fade copy is guarded.
  - The dispatcher's stream and fade-buffer pointers are cleared on release.
- **Triggers and defers** store an opcode or a host function (the original
  kept function pointers in the opcode field, ≥ 30). A deferred host
  function gets `marker = NULL` (the original passed `args[0]` there).
- **Not ported:** save/restore (ops 5/6 and the VHUMOR save hook: raw
  struct dumps with pointers), the VHUMOR module records and dumps, the
  stream-refill skip flag
  0x64cdb0 in snd_idle, and 0x4b9470 (game_main's four timed channels,
  0x4d29b0/0x4d2c50).

## Verification

`build/imuse_test orig OUTDIR` writes `imuse_sfx.wav`, `imuse_mix.wav` and
`imuse_loop*.wav`. It checks:

- **Exact output.** An effect played alone and the first 3 s of a streamed
  music track equal, sample for sample on both channels, the resource PCM
  put through the tables, computed independently in the test: level 16,
  centre pan (level 11 = volume 87), soft clip for 8 voices.
- **Timing.** The effect ends after its length in real time, and the mix
  duration matches the ticks run.
- **Streaming.** Music streams through bufID 1. The voice streams through
  bufID 2, and its trigger fires once with an `MK` marker.
- **Ducking.** Group 4 ducks to 79 (5/8 of 127) during the voice and is back
  to 127 after it.
- **Jumps.** The JUMP loop keeps playing from memory and when streamed, with
  no gap at the jump, and ends after `set_hook(0x80)`.
- **Sanity.** No sample is clipped, the mix isn't silent, and the pan table
  matches ALIFE.EXE's bytes.

It runs clean under ASan/UBSan.
