# Enhancements and port deviations

By default the port behaves like the original, bugs included. The
**enhancements** switch (`g_enhancements`, `src/game/enhance.{c,h}`) turns on

- fixes for obvious bugs of the original, and
- quality-of-life additions, starting with the UI scale.

Some deviations can't be switched off: where the original reads or writes
memory it doesn't own, the port can't reproduce that, so those guards are
always on (listed below under *Always on*).

## Turning it on

| Where | How |
|---|---|
| Settings file | `ALIFEPORT.INI` in the user's settings folder (`plat_pref_path`, `~/.local/share/Afterlife/Afterlife/` on Linux), or the file `--port-ini=FILE` names. A `[Port]` section with `Enhancements = 0/1` and `UI scale = 0..4` (0 = automatic). ALIFE.INI is left alone: the original reads it, and it has no sections. |
| Command line | `--enhancements`, `--no-enhancements`, `--ui-scale=N`, `--port-ini=FILE`. They override the file for this run and aren't saved. |
| Environment | `AFTERLIFE_ENHANCEMENTS=1` / `0` (between the file and the command line). The tests use it (`enhance_init_env`). |
| In game | Global menu > **Port Options...** (port-only item, a message box with *Enhance*, *Scale* and *Done*). The switch applies at once; a UI scale change applies at the next start. Each change is saved to the settings file. |

Precedence: command line > environment > file > default (off).

## Gated behaviour

Each place has an `/* ENHANCEMENT: ... */` comment and a `g_enhancements`
test (`grep -rn "ENHANCEMENT:" src`).

| Where | Original (default) | Enhanced |
|---|---|---|
| Tutorial waits, `wait_seconds` 0x4ac5a3 / `wait_years` 0x4ac61f (`game/tutorial.c`) | The lesson's stopped state is checked only on entry: after a click or Esc ends a lesson, the wait goes on running the player's reloaded game for the rest of its seconds or years (up to 7 years at the lesson's tempo). | The wait ends with the lesson. |
| Tutorial cell glide, `glide_cell` 0x4aca7d (`game/tutorial.c`) | Re-centres the main view until the cell is in the client area; if it never gets there the lesson loops forever (a hang, unless the lesson is stopped). | Gives up after 4 tries and glides to where the cell is. |
| Bird droppings on a building in the top rows (`game/badthing_objects.c`) | The row counter is a byte compared against y - size; for y - size = -1 it wraps 0 -> 255 and never ends: **the game hangs**. (Its writes outside the grid go nowhere, see *Always on*.) | Stops at row 0. |
| Planet tenet shift (`game/planet.c`, the loop taking the points a fate gains from the others) | Loops forever when nothing is left to take: **the game hangs**. | Stops after 5 idle rounds. |
| UI scale (`platform/present.c`, `platform_sdl.c`) | 1: the framebuffer maps 1:1 to screen pixels. | See below. |

The hangs are reproduced as hangs on purpose: that is what the original
does. Neither is known to be reached by the shipped saves and lessons
(`badthings_test` and `tutorial_test` pass either way).

## UI scale

At `Resolution=MAX` the port runs at the screen's native pixels (e.g.
2560x1600), where the 1996 UI is tiny. With the enhancements on, UI scale k
makes the game see a desktop k times smaller (`plat_video_desktop_size`),
so `MAX` gives a 1280x800 framebuffer at k = 2, and the presentation scales
it back up by the largest whole factor that fits, nearest-neighbour (crisp),
centred. A smaller `Resolution` gives a framed window k times its size.
Mouse positions are mapped back through the same layout (`present_to_fb`),
and pointer warps forward (`present_from_fb`).

- `UI scale = 0` (automatic) picks from the desktop's pixel density: 1 below
  140 %, 2 from 140 % to below 240 % (so 150-200 % gives 2x), then 3, 4.
- The scale is clamped to 1..4 and so that the scaled desktop is still at
  least 640x480 (the title art's size).
- With the enhancements off the scale is always 1.
- It applies at the next start: the game's windows are laid out for the
  screen size at startup, so changing it live would need every window
  rebuilt.

## Always on

Out-of-bounds memory accesses and malformed-data loops can't be reproduced
faithfully. These guards stay on regardless of the switch (marked
`SAFETY` or with a comment naming the original's access):

- Map grid: `bt_cellp` / `bt_sim` (`game/badthings_int.h`): off-grid cells
  read 0 and writes go nowhere; the original reads and writes whatever
  memory is there.
- Croupier spawn failure, `bt_dice_start` 0x4438ed (`game/badthings.c`):
  the original writes the object's fields past the object table.
- `game/economy.c` (writes past `unk_7fac` / `unk_7fcc`),
  `game/newworld.c` (table index), `ui/microview_info.c`,
  `ui/tutorialwin.c`, `ui/slotdlg.c` (colour remap), `imuse/mixer.c`,
  `compbun/cmprsmgr.c` (block table): reads or writes past a table.
- Drawing clips everything (`gfx/draw.c`; `gr_fill_rect` loops forever on a
  rectangle that clips to nothing), wrapped text stops on a line that
  makes no progress (`gfx/font.c`), a failed font load doesn't recurse
  (`font_slot`), and corrupt FLC / TILE packets end the frame
  (`formats/flc.c`, `gfx/tile_draw.c`).

## Tests

- `enhance_test` (no data): the settings round trip, the switches and
  their precedence, the layout and mouse mapping at scales 1-3, the
  automatic scale and its clamping.
- `badthings_test_enh` and `tutorial_test_enh`: the two tests that cover
  gated code, run again with `AFTERLIFE_ENHANCEMENTS=1`.
