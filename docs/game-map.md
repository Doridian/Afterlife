# Game code map and work plan

A survey of the part of ALIFE.EXE that isn't decompiled yet: the simulation and
the game's own UI, 0x401000-0x4b0fff in `.text` (714 functions, about 640 KB,
roughly 60% of the program's code). It is meant to let several agents work in
parallel without stepping on each other. Names given here are in
`symbols/ALIFE.EXE.txt`. Addresses without a name are still `FUN_xxxxxx`.

Confidence: **high** = checked in the decompiler (strings, callers, data flow);
**medium** = strong circumstantial evidence (call graph plus strings);
**low** = guessed from neighbours.

MSVC links one object file after another, and each object's string literals
sit together in `.data` in source order. So a run of functions whose strings
form one block (e.g. all the "This button ... Graphview" help texts) is one
source file. The module boundaries below follow that rule plus the call graph.

## Already done (don't redo)

| Range | What | Doc / port |
|---|---|---|
| 0x409dc0-0x40b1bf | FLI/FLC player (`flc_*`) | rendering.md, `src/formats/flc.c` |
| 0x408eb0-0x409baf | `tile_load_all` / `tile_unload_all` (named; loads tile sets with the progress bar) | map.md (port loads sets directly) |
| 0x42b102 | `map_view_select_plane` (renderer half; the sim side effects aren't ported) | map.md |
| 0x4514a4 | `map_rotate_cells` (named, **not** ported) | map.md |
| 0x4750f0-0x47619f | game sound API (`snd_play`, `snd_stop`, preload, audio dialog) | imuse.md, `src/game/sound.c` (play/stop/preload) |
| 0x4b8cd0-0x4b9470 | mouse | `src/gfx/mouse.c` |
| 0x4b9470-0x4ba0f0 | sound slots / iMUSE host | `src/game/sound.c` |
| 0x4ba100-0x4c1568 | controls (button ported; scrollbar, text, list, message box: **UI agent**) | ui.md |
| 0x4c1580-0x4c62cc | window manager, regions | ui.md, `src/ui/wm.c`, `region.c` |
| 0x4c62e0-0x4c6bf8 | WinMain, init/shutdown, task table, pump | startup.md |
| 0x4c6bf9-0x4c97ff | menus (menu bar, pop-ups): **UI agent** | ui.md |
| 0x4c98bb-0x4d1e2b | heap, display, resources, INI, palette cycling, fonts, keyboard, timer | startup.md, rendering.md |
| 0x4d3e49-0x4e5d90 | iMUSE, VHUMOR runtime, compbun | imuse.md, compbun.md |
| 0x4e6210-0x4e6690 | LZSS core | compbun.md |
| 0x4e66b0-0x4ed0f4 | TILE sets, `tile_draw`, `map_view_draw`, transforms, highlight | map.md, formats/tile.md |
| 0x4ed550-0x4ef4fb | map object system (`map_objects_step` etc.) | map.md, WP3: `src/game/mapobj.c`, `src/gfx/map_objects.c` |
| WP0 (see below) | world core: live state, plane switch, cell/sim-cell accessors, RNG, bignum, `mul_div_*`, number formatting | "State model" below, `src/game/world.*`, `src/game/util.*` |

In flight elsewhere: the ALSV loader (`src/game/savegame.*`), AKOS
(`src/gfx/akos*`), the remaining controls, menus and message box (`src/ui/`).

## Frame and tick structure

```
game_main 0x401000
  loop: sys_pump
        game_frame_update 0x407282     edge scroll (mainview_scroll 0x40682a),
                                       repaint request (0x5f7474), cursor,
                                       credits animation (0x4ada7b when 0x57e0a0),
                                       game_frame_clock 0x402747: every 125 ms
                                       bump 0x650058/0x65005c/0x650060;
                                       on each bump (unpaused): map objects of
                                       planes 1..3 (0x4ee088 + map_objects_step),
                                       music change (0x475607), status (0x446dc5)
        sim_update 0x44876b            the simulation (below)
        sys_pump
        planetview_blink_cursor 0x4a479f   (startup.md calls this the text-cursor
                                            blink; it blinks the Planetview's
                                            selected-sector marker every 500 ms)
        game_command switch (0x650048): 1/6 save dialog 0x480ea4, 2/5 load
        dialog 0x489e90, 3 delete dialog 0x4946bd, 4 new game
```

The main window (`main_window` 0x5f7460) is created with `title_wnd_proc`
0x429c4d; after the title, game_main swaps its proc to **`game_wnd_proc`
0x404023** (map clicks, menu commands: pause messages, save/load/new/quit
confirmations, the Bad Things menu, advisors' resignation, Bad Things
protection). The **Control Panel** (`ctrlpanel_window` 0x5c6674, 37 buttons)
uses `ctrlpanel_proc` 0x402996; the **Choose Tile Window**
(`tilechooser_window` 0x5bb200) `tilechooser_proc` 0x402789. The menu bar
(&File &Global &Window &Tutorial) is built by `game_menubar_create` 0x409c17.

### sim_update 0x44876b: the simulation scheduler (high)

The doc in startup.md calls it `ui_update`. It is the whole simulation,
spread over frames. Arguments: (main window, ?, graph window, `&map_view`,
speed). Speed 0 = stopped. Besides the phases it runs timers:

- every 500 ms: plane 1 and 3 `0x447e8f` (animate port-like buildings in the
  realm's 50-entry list) once `sim_phase` > 30, plane 2 `0x434dc4` (Karma
  trains);
- every 240 ms while 0x5f4cd0: `0x4442dd`, `0x443c9e` (Bad Things in progress);
- Hell Freezes Over / My Blue Heaven (realm +0x80f9/+0x80fa/+0x80fb flags):
  spread 0x442122, then recover 0x442287, "Hell has thawed out!";
- Birds of Paradise / Bats out of Hell (0x5b2f61 bits 0/1): end after 5 s
  without activity (realm +0x80fc);
- 30 s: `cd_spin_up`.

Then `switch (sim_phase)` (`sim_phase` 0x5bb104, saved). Each phase runs when
`speed*5-5` times a per-phase delay has passed since 0x65ee64, and many phases
are incremental (they return 0 until they've walked the whole map or planet):

| Phase | Plane | Work | Next |
|---|---|---|---|
| 0 | | year start: 0x62659c = 0x7ff, status | 20 |
| 20 (0x14) | | `planet_stats_accumulate` 0x4aae01: 1024 planet sectors per call, averages of four sector bytes into 0x6301f3/0x57e010/0x5931c0/0x626590; at the end clears realm +0x7a08 tables (0x452267) | 30 |
| 30 (0x1e) | | `planet_emit_souls` 0x4ab77a (25 sectors per call: deaths -> SOULs via 0x4ab5d4 -> 0x445785); then `0x49c8b8` (planet year: tech decay, disasters), per realm `0x46114f` (SOULs arriving at ports/gates) and `0x460b16` (arrivals on random roads); soul-rate scaling | 10 |
| 10 | | `0x49a2da` planet sector update (incremental) | 15 |
| 15 (0xf) | | `planet_tech_update` 0x49b05c (EMBO inventions, "With Pottery, EMBOs can ..."); year-end resets of counters, graphs redrawn, `0x47d6fb` (Soulview trackers age) | 100 |
| 100 | 3 | `sim_zone_scan` 0x4607d6: up to 50 zone cells (sets 14/15) per call -> `0x45f382`; set 5 cells -> `0x4606cf`; end of map -> `0x445416` | 90 |
| 90 (0x5a) | 1 | same for Hell | 40 |
| 40 (0x28) | 3 | `0x45e44b` SOUL flow: SOULs walk roads to matching zones (0x45e1fc, 0x45d4ac recursive road walk, 0x45c038) | 50 |
| 50 (0x32) | 1 | same for Hell; totals into 0x630438 / 0x6390e8 | 60 |
| 60 (0x3c) | 3,1 | `0x43a81d` (per-cell influence/vibes), `0x43b86f`; `0x448150` (soul rate from the 0x6391d0 table) -> 0x630430; `0x42fddf` (Karma rail check) | 70 |
| 70 (0x46) | 3 | `0x43ed55` realm year: buildings, workforce, banks and loans, training centres | 80 |
| 80 (0x50) | 1 | same for Hell; then cross-realm overflow (`0x446242(3,1)`, `(1,3)`), budget `0x43eb7c`, status, Microview/Macromanager refresh, per realm `0x43188a`, `0x42dd50` (siphons, set 30), `0x435f3d`, `0x446829`; `graph_history_record` 0x40d34b, `0x4154ab`, `graphview_draw`, `mapview_refresh`, `0x43e4e5` (random Bad Things), bankruptcy check (`bignum_less(pennies, 0)` -> `0x444d23`), `0x434d2d`, `advisor_update`, `news_popup_show(0x36d)`, `scenario_event_step` | 0 |

So the dependency of the whole thing is: planet -> SOULs -> realms (zones,
roads, ports) -> economy -> views.

## State model (port)

Decided in WP0; details at the top of `src/game/world.h`.

- **All persistent state is one global, `GameState g_state`**
  (`src/game/state.h`, grouped by save block, the original address on every
  field). `world_load()` fills it through the ALSV loader (into a temporary
  first, then applies the load-time fixups and reselects the planes like
  `save_load_game`), the simulation reads and writes it in place, and
  `savegame_save*` writes it back. No shadow copies and no getters for plain
  fields: the original's `pennies_hi` is `g_state.pennies.hi`, Hell's realm
  record is `g_state.plane[1].stats`, the soul groups are
  `g_state.soul_groups[]`.
- **The "current plane" pointers** that `map_view_select_plane` sets
  (`map_cells_current`, `sim_cells_current`, `plane_stats_current`,
  `realm_tile_info`, the per-realm constants 0x65ed74..80) are in
  `World g_world` and point into `g_state`. `world_select_plane(p)` is the
  simulation half of 0x42b102. As in the original, plane 2 only switches the
  cell grid; realm, sim cells and tile info stay on the last realm. The
  renderer half stays `map_view_select_plane` (`src/gfx/map_view.c`); the
  owner of the main view (WP7) calls both.
- Accessors mirror the original's (current plane, coordinates 0..99):
  `cell_*` (0x42b37e-0x42b88a, 0x441a12-0x441a74), `sim_*`
  (0x43085d, 0x43089b, 0x431b40, 0x431bd6, 0x431c6c, 0x441aa4),
  `soul_group_get/set`, `realm_cur()`, `world_realm(plane)`.
- Wide counters (pennies, EMBOs, the realm SOUL pool +0x80fd) are a
  `Bignum {int32 hi; u32 lo}`: **sign and magnitude** (hi = -1 means -lo),
  not two's complement. Use `bignum_*` from `src/game/util.h`.
- **RNG**: the MSVC CRT `rand` (0x4b1202, `state = state * 214013 +
  2531011`, returns bits 16-30), state 1 at start; ALIFE.EXE never calls
  `srand`, but `tile_load_all` discards `clock() % 100000` values at startup
  (0x409a4d). The port keeps the state in `util.c` (`rng_seed`,
  `rng_state`, `rng_warmup`, `crt_rand`, `rand_range`), so runs are
  deterministic and replayable. Everything that called `rand` in the
  original must draw through `crt_rand`/`rand_range`, in the same order:
  besides `rand_range` the direct callers are 0x4456be, 0x445785, 0x45c038,
  0x497bbe and the AKOS interpreter (0x4d6b14, opcode 0xc082).
- Runtime-only state of a module (windows, timers, scan cursors such as
  `sim_zone_scan`'s 0x65fe48/0x65fe4c) stays static in that module. New
  persistent fields go into `state.h` and `savegame.c` together.

Consistency facts the saves confirm (`tests/world_test`): realm
`population` (+0x7fec) is exactly the sum of sim-cell +2 and +6 (permanent
and temporary SOULs) over all cells of zone buildings (sets 14/15 and their
set-0 parts); `n_zones` (+0x80df) is the number of set-14/15 cells; the
ports list (+0x7860, `n_ports` +0x8044) holds exactly the set-13 cells.

## Key global state

There is no single game-state struct in the original. State is a few hundred globals in
`.bss` (0x57e000-0x66cfff) plus two big per-realm structs. The ALSV loader
(`save_load_game` 0x48f837) is the authoritative list of what is persistent;
the savegame agent is naming these. What's known:

| Address | Size | What | Saved |
|---|---|---|---|
| `map_cells_hell/middle/heaven` 0x593270 / 0x5bb270 / 0x6265b0 | 100x100 u32 each | cell grids (map.md) | yes (column-major) |
| `map_objmap_*` | 100x100 u8 each | object map | yes |
| `sim_cells_hell` 0x5fa670, `sim_cells_heaven` 0x5c6680 | 100x100 x 18 B | sim cells; a union keyed by the cell's tile set. Known: +0xa/+0xb = origin x/y for building parts (set 0), +1 u32 with the soul-group index in its high 16 bits (`0x45852f`/`0x458490`), +1/+5 current/previous counts for set-30 and port cells, +0xe bit 7 a flag | yes (by set) |
| `realm_heaven` 0x5b2f70, `realm_hell` 0x630520 | ~0x8130 B each | per-realm statistics; `realm_current` 0x630428 points at one (set by `map_view_select_plane`). Layout below | yes ("~32 KB block" per plane) |
| `realm_tile_info` 0x5bb0a4 | ptr | per-realm tile-type table: `tile_info_heaven` 0x5c5c20 / `tile_info_hell` 0x5f7480, 0x84 x 0x14 B (`RealmTileInfo` in world.h: cost, base cost, zone kind, weight), built by 0x452581 | no |
| 0x65ed74/78/7c/80 | u32 | per-realm constants set by plane select (3/0x15, 0/3, 0/0x21, 0x24/0x2c) | no |
| `sim_phase` 0x5bb104 | u8 | scheduler phase | yes |
| 0x630268 | u32 | year (header and body; `== 666` plays a sound) | yes |
| 0x630438 / 0x63043c | {int hi; u32 lo} | Pennies, a sign-and-magnitude wide number: `bignum_add` 0x42a950, `bignum_sub` 0x42a9ec, `bignum_less` 0x42ac32, `bignum_ge`/`gt`/`ge_int` 0x42aaa8/0x42ab2f/0x42abb6 | yes |
| `embos_next` 0x6303c0 | bignum | EMBOs counted by the planet pass, moved to `embos` 0x630490 at year start | yes |
| 0x6390e8 | same | a SOUL total (same bignum ops) | ? |
| 0x630430 | u32 | soul rate (from 0x448150) | yes |
| 0x5c5c14 | u16 | difficulty 1-3 (header, version > 7219) | yes |
| 0x66aee4 | int | version of the file being loaded (`atoi` of the 4 digits) | n/a |
| `planet_sectors` 0x57e1c0 | 64x32 x 0x2a B | the Planet: per sector u32 population, then bytes (+7..+0xd sin/virtue/belief levels used by the averages, +0x27 kind, +0x28, +0x29 flags) | yes |
| 0x66c7e4 / 0x66c7e8 | ptr / size | extra planet buffer and the planet tech/event state (0x66c8cc-0x66c8e0, tables at 0x4f4030, 0x5068b8, 0x518d50); read/written by 0x495f7c / 0x495300 | yes |
| `soul_groups` 0x59cf10 | 8192 x 11 B | SOUL groups {u32 count, u16 sin/virtue mask, u32, u8}; cells refer to them by index; `0x4609c7` adds SOULs to one | yes |
| 0x593240, 0x57e060, 0x593260, 0x5fa600, 0x6303d0, 0x5bb210/22c/248 | 7 entries each | the seven sin/virtue pairs: weights (randomised by difficulty in `game_reset_world`), the "relative WEIGHTS" diagram | yes |
| `map_anim_cells` 0x64e500 | 25 x 9 B | animated map cells (`mapobj_place`, `mapobj_animate_cells`, game/mapobj.h) | yes |
| `map_objects` 0x64e5f0 | 63 x 0x17 B | map objects (map.md); Bad Things, trains, walkers are these | yes |
| 0x4f33f8 | 10 records | Soulview's tracked SOULs (type 2 walking, 6/7 finished, plane at +0x10, year at +0x1c); read by 0x47951c | yes |
| `graph_history` 0x6399d0 | 2 x record | yearly history for the graphs (copied from realm +0x77e0, +0x79c4, +0x7f3c..+0x7fcc); `graph_history_write` 0x40b1c0 / `_read` 0x40c106 | yes |
| 0x5b2f61 | u8 | active Bad Things bits (1 Birds, 2 Bats, 4 Blue Heaven, 8 Hell Freezes, 0x10 Heaven Nose, 0x20 Handbasket, 0x40 Dice, 0x80 Disco Inferno; game/badthings.h) | yes |
| 0x5bb180, 0x63048c | string, index | scripted scenario events ('D'+digit = Bad Thing, 'F' = planet flood, ...) stepped by `scenario_event_step` | yes |
| 0x57e034 (stride 13), 0x630204 / 0x63020c (stride 13, by index>>1) | per tile type | building parameters (capacity etc.) | ? |
| `game_paused` 0x5fa61c | u8 | pause | no |
| 0x630514 | u8 | "demo/locked" flag that blocks the build tools | ? |
| `main_window` 0x5f7460, `ctrlpanel_window` 0x5c6674, `tilechooser_window` 0x5bb200, 0x630270 (Mapview), 0x66c7d4 (Planetview), 0x57e1b0 (help balloon) | ptr | windows | no |

### Realm struct (`realm_current`, high for offsets, low for meanings)

| Offset | Layout | Notes |
|---|---|---|
| +0x0000 | 7 x 16 x 0x89 {u32, u32, u32[32], u8} | [zone kind][16] SOUL counts, v[] by age bucket (0x445416 ages them), this year |
| +0x3bf0 | same | previous year |
| +0x77e0 / +0x7820 | u32[16] x 2 | by sin/virtue (copied to the history) |
| +0x7860 | 50 x {u8 x, u8 y}, count at +0x8044 | ports/gates list: exactly the set-13 cells (walked by 0x46114f, 0x446829, 0x447e8f) |
| +0x78c4 | 128 x {x, y} | |
| +0x79c4 | 20 x {x, y} | (history) |
| +0x79ec | u32[7] | |
| +0x7a08 | 16 x {u32, u32[16]} | cleared at year start |
| +0x7e48 / +0x7e68 | 16 x {x, y} / u32[16] | best group per sin (0x4609c7) |
| +0x7ea8 / +0x7eac | u32, u32[16] | SOULs added, total and per sin |
| +0x7eec, +0x7ef0 | u32, u32[16] | |
| +0x7f30 / +0x7f38 | u32 | arrivals (0x460b16) / siphon pool (0x42dd50) |
| +0x7f3c..+0x7fcc | u32[7] x 4, u32[8] x 2 | (history) |
| +0x7fec..+0x8010 | u32 x 10 | +0x8000/+0x8004 compared by the cross-realm overflow |
| +0x809f | u32[16] | cleared by the SOUL flow pass |
| +0x80f9..+0x80fc | u8 | Freeze/Blue state, Bad-Thing activity flag |
| +0x80df | u16 | number of zone cells (sets 14/15) |
| +0x80fd | bignum | SOUL pool; set-5 cells drain into it (0x4606cf) |
| +0x8105..+0x8127 | u32/u16 | more counters; +0x811b cleared by the SOUL flow pass |

## Module map

Function counts are for the range (named ones in brackets). Port paths are
proposals.

| Range | Funcs | Module | Entry points | Conf. |
|---|---|---|---|---|
| 0x401000-0x408eaf | 40 (13) | **Game shell**: game_main, main/control-panel/tile-chooser procs, scrolling and zoom (0x406be8 centre on cell, 0x406d76/0x406eec zoom), frame update, full repaint, demo-only messages (0x407a0e), control panel help, module init 0x408208, `loading_progress` 0x408356 (joke messages), `views_refresh_all` 0x408c4d, `ui_busy_cursor` 0x408ca4 | `game_main`, `game_wnd_proc`, `ctrlpanel_proc`, `tilechooser_proc`, `game_frame_update` | high |
| 0x409bb0-0x409dbf | 2 (1) | menu bar definition | `game_menubar_create` | high |
| 0x40b1c0-0x41722f | 42 (14) | **Graphview**: history record/IO, window, budget, labor, belief, population, structures, history, tenet graphs | `graphview_create` 0x40d75e, `graphview_proc` 0x40e11a, `graphview_draw` 0x4159ba, `graph_history_*` | high |
| 0x417230-0x41f610 | 15 (5) | **Advisors** (Jasper & Aria helper window): issue evaluation 0x41801f/0x41878c/0x41894e, advice texts 0x419b65/0x41b768/0x41db2b, picker 0x4196ae | `advisor_create`, `advisor_proc`, `advisor_show`, `advisor_update` 0x418a39 | high |
| 0x41f611-0x42645f | 7 (7) | **Tutorial script** ("Getting Started", "Lost SOULs", ... "Temp Game From Hell"; 25 KB of scripted steps), runner `tutorial_run` 0x425e3b (also ends the attract mode), `tutorial_restore_windows` 0x425ab0 (WP8, done) | `tutorial_script` 0x41f611, 0x425e3b | high |
| 0x426460-0x42a6c2 | 19 (3) | **Title**: intro, animated title 0x426ad5, main menu 0x428b22/0x428d28 ("Start a New Game", "Load a Scenario", "Replay Cheesy Intro"), title window proc (quit box) | `title_run`, `intro_play`, `title_wnd_proc` | high |
| 0x42a6c3-0x42b101 | 12 (7) | `di_video_benchmark`, utilities: bignum ops, `mul_div_clamp`/`mul_div_round` (a*c/b rounded, overflow-safe), number formatting 0x42ad0c ("%s%03d" thousands groups) / 0x42aded / 0x42aaa8 / 0x42ab2f / 0x42abb6, `rand_range` 0x42b088 (CRT `rand` at 0x4b1202) | | high |
| 0x42b102-0x42b88a | 13 (13) | map cell accessors (`map_cell_get/set_*`, `map_cell_origin_set/index`, `tile_index_footprint`) | | high |
| 0x42b88b-0x42d637 | 6 (0) | structure category 0x42b88b (set+index -> class, 26 callers), 0x42bb64, 0x42bf5c, **destroy structure** 0x42c09b ("nuke this Special Structure?", called by tools, Bad Things, views) | 0x42c09b | medium |
| 0x42d638-0x4306cf | 11 (1) | Karma rail tracing on plane 2 (set 16 rails to stations on planes 1/3): 0x42d638 (recursive), 0x42e5fa, 0x42f129, 0x42fddf (phase 60), 0x43008b; `map_stamp_building` 0x4301b0 (writes a footprint, part cells point at the origin) | 0x42fddf | medium |
| 0x4306d0-0x43293c | 28 (0) | sim-cell accessors and population: 0x43085d, 0x43089b, 0x431b40/0x431bd6 (counts via origin), 0x431ca0/0x431ebf (perm / temp population; 0x4320de = sum), 0x43217a/0x43231d, yearly 0x43188a | 0x43188a | medium |
| 0x43293d-0x436470 | 15 (0) | Karma stations and trains (plane 2): rebuild 0x433106, trains 0x434dc4 (every 500 ms), 0x43404a, 0x43455c (recursive), 0x435509, yearly 0x435f3d | 0x433106, 0x434dc4 | medium |
| 0x436471-0x43a492 | 4 (1) | `map_object_think` 0x437184 (12.8 KB: per-object behaviour for Bad Things and other movers; called by `map_objects_step`), 0x436521, 0x4366ab | | high |
| 0x43a493-0x44180a | 30 (1) | **Realm yearly accounting**: influence/vibes 0x43a81d, 0x43b86f, building upgrade 0x43bbe7, workforce/training 0x43c9a0, recursive 0x43d3d8, `bad_thing_start` 0x43dc3c (Birds, Bats, Blues, Freeze, ...), random Bad Things 0x43e4e5, budget 0x43eb7c, realm year 0x43ed55 (banks, loans, "homegrown Angels/Demons") | 0x43ed55, 0x43eb7c, `bad_thing_start` | medium |
| 0x44180a-0x446903 | 36 (0) | **Bad Things** implementations (0x441c76-0x4451d7: spread/recover for Freeze and Blue, Birds/Bats, Heaven Nose, Handbasket, Dice, Disco Inferno 0x443c58), cell flag tests 0x441a12/0x441a44/0x441a74/0x441aa4, end-of-scan 0x445416, planet-SOUL entry 0x445785, cross-realm overflow 0x446242, 0x446829 | 0x4442dd, 0x443c9e, 0x442122, 0x442287, 0x444d23 (bankruptcy) | medium |
| 0x446904-0x4486bc | 10 (3) | status bar (`statusbar_draw` 0x447243: SOULs/Lost/Pennies; `statusbar_draw_date` 0x446904), port animation 0x447e8f, soul rate 0x448150, `scenario_event_step` 0x448594 | | medium |
| 0x4486bd-0x449c6d | 2 (2) | `cd_spin_up`, `sim_update` | | high |
| 0x449c6e-0x4514a3 | 23 (0) | **Build tools**: zoning/placement 0x44aede, 0x44b4fb (7.5 KB, place a building), road/zone drag 0x44d34f, map click tool dispatcher 0x44da29 (6.6 KB; called by the three procs), bulldoze/cost 0x44f44c/0x44fc9b, 0x45011d, recompute on load 0x4506b6, special structures / "great disturbance" 0x450aba, cheats 0x4510cd ("SAMNMAX") / 0x451055, 0x45129a | 0x44da29, 0x44d34f | medium |
| 0x4514a4-0x455ed6 | 9 (3) | `map_rotate_cells`, realm table reset 0x452267, 0x452581, 0x452da1 (recursive), **new world** `game_reset_world` 0x4535db, `game_init_defaults` 0x45546c (scenario constants: costs, thresholds) | | medium |
| 0x455ed7-0x457b00 | 3 (0) | tile chooser drawing 0x455f6e, tile info 0x456402 ("Capacity", "One Per Customer", "Can't Build This") | | medium |
| 0x457b01-0x45848f | 3 (2) | debug window | `debugwin_create`, `debugwin_proc` | high |
| 0x458490-0x4619ef | 49 (1) | **SOUL flow**: soul groups (0x458490 get, 0x45852f set, 0x4609c7 add), zone matching 0x4586ca/0x458cf1/0x4599f4/0x45a29d, road walking 0x45c038/0x45d19d/0x45d4ac, per-zone pass 0x45e1fc, phase 40/50 `0x45e44b`, area search 0x45ebe2 (24 callers), zone cell update 0x45f382 / 0x45fd84, `sim_zone_scan`, arrivals 0x460b16/0x46114f/0x461636 | 0x45e44b, `sim_zone_scan`, 0x460b16, 0x46114f | medium |
| 0x4619f0-0x463b6f | 10 (3) | **Macromanager** (funding, lock/auto-balance, punishments/rewards) | `macro_create`, `macro_proc`, `macro_refresh` | high |
| 0x463b70-0x4678f3 | 26 (4) | **Mapview** (minimap with data layers: vibes, vacancy, transport, temp/perm, efficiency, rocks, traffic, sins) | `mapview_create`, `mapview_proc`, `mapview_draw`, `mapview_refresh` | high |
| 0x4678f4-0x4750ef | 33 (3) | **Microview** (tile inspector: population bar, funding slider, upgrade 0x4696e6, banks/loans 0x468df0/0x4694df/0x46f7d8, training centres, Omnibolge/Love Dome, debug "Tile at %d, %d" 0x46ce40, descriptions 0x46a1e0 and 0x46ffc1 (20 KB)) | `microview_create`, `microview_proc`, `microview_refresh` | high |
| 0x4761a0-0x478d4f | 7 (2) | **News pop-up** (`news_popup_show` 0x476582: special-structure announcements, "Twilight of the Demiurges"; procs 0x4761a0, 0x4782a2, 0x478304, 0x478431), `help_balloon_show` 0x478a14 (right-click help used by every "This button ..." function) | | high |
| 0x478d50-0x47fca6 | 39 (3) | **Soulview** and SOUL tracking (0x4f33f8 trackers, SOUL description generator 0x47af6f "contented", "charitable", ..., walker placement 0x47e875/0x47eaf7, 0x47d6fb yearly, 0x47de6c) | `soulview_create`, `soulview_proc`, `soulview_refresh` | high |
| 0x47fca7-0x4952ff | 24 (5) | **Save/load**: file names (`agame%03u.alf`, `ascen%03u.asc`), save/load/delete dialogs (0x480ea4, 0x489e90, 0x4946bd; procs 0x48137d, 0x48a3dc, 0x494a93), slot info 0x4807c5, scenario/game writer 0x485fb3 ("Gimmee Scenario Title"), plane writer 0x481ba4, `save_load_game`, `save_load_plane`, load dialog list 0x494112 | **savegame agent** | high |
| 0x495300-0x4a2efc | 68 (1) | **Planet**: state write/read 0x495300/0x495f7c, world generation 0x496ed1/0x49733f..., sector update 0x49a2da, tech 0x49b05c, planet year 0x49c8b8, disasters 0x49c226/0x49de17 ("FLOOD STRIKES!"), Planetview helpers (wield influence 0x49f804) | 0x49a2da, `planet_tech_update`, 0x49c8b8 | medium |
| 0x4a2efd-0x4a8336 | 38 (3) | **Planetview** window (population / technology / sin-virtue layers, sector info 0x4a5270, 0x4a6254) | `planetview_create`, `planetview_proc`, `planetview_refresh` | high |
| 0x4a8337-0x4ab8df | 7 (2) | planet sector rendering 0x4a8337 (+ line helpers 0x4a7e58 etc.), planet deaths 0x4aa138, 0x4aa783, `planet_stats_accumulate`, `planet_emit_souls`, SOUL injection 0x4ab5d4 | | medium |
| 0x4ab8e0-0x4acc2f | 19 (19) | **Tutorial window** (unused leftover of the demo) and scripted input (`tut_*` 0x4ac2de-0x4acbee drive the game from the script), `attract_stop` (WP8, done) | `tutorial_window_create`, `tutorial_proc` | high |
| 0x4acc30-0x4b0fff | 13 (1) | **About box and credits** (0x4addfb credits, 12.7 KB; 0x4ada7b per-frame credits animation) | `about_proc` 0x4acdda | high |

Outside 0x401000-0x4b0fff the remaining unnamed code is CRT (0x4b1000-0x4b8cd0,
0x4ef798-0x4f2xxx), VHUMOR runtime internals, and the menus (UI agent). The
map object system 0x4ed550-0x4ef4fb (4 unnamed: 0x4edf31, 0x4ee088 per-plane
object animation, 0x4ee3a9, 0x4ee402) belongs with WP3.

## Dependencies

```
            WP0 world/state core
          /     |      |       \
     WP2 build  WP1 sim  WP5 planet  (savegame agent: ALSV)
          \     |  \      /
           WP3 karma+objects   \
              |                 WP1b soul flow
           WP4 Bad Things
              \______  ______/
                     WP6 views, WP7 shell
                         |
                     WP8 tutorial
```

1. **WP0 first** (small, a day): every module reads cells, sim cells, the
   realm struct and the plane switch. Nothing else should define these.
2. WP1, WP2 and WP5 can then start together. WP5 (planet) is the most
   independent: its only outputs are SOULs (`planet_emit_souls` ->
   0x4ab5d4 -> 0x445785) and the four averages.
3. WP3 needs WP2's `map_stamp_building`/destroy. WP4 needs WP3's map objects.
4. Views (WP6) only read state, so they can start right after WP0 against
   save files loaded by the savegame agent, and get actions wired up later.
5. WP8 drives the game through scripted input and loads saves, so it goes last.

## Data from ALIFE.EXE

Texts and number tables of ALIFE.EXE are compiled in from `src/data/`,
generated by `tools/extract_exe.py` (committed, checked against the EXE's
size and SHA-256; the build never runs it). Image data stays in the user's
EXE, which the port loads at runtime (`exe_res_open`, checked at startup):

- `exe_image.c`: `.rdata` and `.data` minus the image regions (141 KB of
  non-zero bytes; texts as string literals). Code looks things up by their
  original address: `exe_res_va()` (gfx/exe_res.h) and `exe_str` /
  `exe_strp` / `exe_stri` / `exe_fmt` / `exe_u32` (ui/exe_text.h). New code
  that needs an EXE text or table uses these with the address; no
  regeneration needed.
- Image regions (`exe_image_regions`, address and length only): the built-in
  font's glyphs (`font_builtin` 0x673a10, 2335 bytes), copied in from the
  EXE when it is loaded. `.rdata`/`.data` hold no palettes or bitmaps.
- The PE resources (31 cursors, their groups, the icon) are read from the
  EXE (`exe_res_find`).
- `credits_script.c`: the about box's credits, which are code in the EXE
  (0x4addfb), decoded into ops that `ui/about.c` runs.

Machine code is never copied: data that only exists as code (like the
credits) gets decoded by the generator into a table.

## Work packages

Each package owns its address ranges and files. Calls into another package go
through that package's header. Until it lands, a caller may add a stub with
the original address in a comment to the *owner's* header, never its own copy.

### WP0: world state core (priority 1)

- Ranges: 0x42a6c3-0x42b88a (utilities, cell accessors), the sim side of
  `map_view_select_plane` 0x42b102, sim-cell accessors 0x43085d, 0x43089b,
  0x431b40, 0x431bd6, 0x441a12-0x441aa4, soul-group get/set 0x458490/0x45852f.
- Files: `src/game/world.h/.c` (grids, `SimCell` with set-keyed accessors,
  `Realm` struct, `world_select_plane`), `src/game/util.h/.c` (`rand_range`
  with the MSVC LCG, bignum, `mul_div_*`, number formatting).
- The persistent state already has a home: `src/game/state.h` (the
  savegame agent's `GameState`, grouped by save block, with the original
  address per field) and `src/game/savegame.c`. WP0 builds on it rather than
  redefining anything: accessors and the realm/sim-cell views go in
  `world.h`, and new fields go into `state.h` in agreement with its owner.
  Non-persistent runtime globals (windows, timers, scan cursors) stay with the
  module that uses them.
- **Done.** Interface as built: `g_state`, `g_world`, `world_load()`,
  `world_select_plane()`, `cell_*()` (get/set tileset, index, strip, flags,
  `cell_origin*()`, `cell_object_slot()`), `tile_index_footprint()`,
  `sim_cell()`/`sim_zone_kind()`/`sim_perm()`/`sim_temp()`/`sim_pop()`,
  `soul_group_get/set()`, `realm_cur()`, `world_realm()`; `util.h`:
  `crt_rand()`, `rand_range()`, `rng_*()`, `bignum_*()`, `mul_div_clamp/round()`,
  `fmt_thousands(_k)()`. Not done: `RealmTileInfo` is declared but filled by
  WP2 (0x452581), and `world_load` doesn't run the post-load recompute yet
  (0x4506b6, 0x452267, WP2).

### WP1: simulation scheduler and realm economy (priority 2)

- Ranges: 0x4486bd-0x449c6d (`sim_update`), 0x4306d0-0x43293c (population),
  0x43a493-0x43dc3b and 0x43e4e5-0x44180a (realm year, budget, workforce,
  banks; not `bad_thing_start`), 0x445416, 0x445785 (planet SOULs entering
  a realm), 0x446242-0x446903 (cross-realm overflow, 0x446829),
  0x447e8f-0x4486bc (port animation, soul rate, scenario events), 0x42dd50.
- Files: `src/game/sim.c` (scheduler), `src/game/realm.c`, `src/game/economy.c`.
- Provides: `sim_update(speed)`, `realm_year()`, `economy_*`; calls WP1b, WP4
  (`badthings_tick`, `bad_thing_start`), WP5 (`planet_*` phase steps), WP6/WP7
  refresh hooks (`views_refresh_all`, `statusbar_draw`, `news_popup_show`).
- **Done.** As built: `game/sim.h` (`sim_update`, the phase machine and its
  timers, `sim_soul_rate` 0x448150, `scenario_event_step`; runtime `g_sim`,
  UI side effects through `g_sim_hooks`, a hookable clock), `game/economy.h`
  (`economy_realm_year` 0x43ed55 with the `RW_*` names of realm
  `unk_7ff0[]`, `economy_score` 0x43eb7c, `economy_random_bad_things`),
  `game/realm.h` (everything else in the ranges), `game/tables.h` (the
  constant tables of `game_init_defaults` / `game_reset_world`). The Bad
  Thing timers stayed in `sim_update` and call WP4's stubs
  (`game/badthings.h`); Karma calls are stubs in `game/karma.h`. Tests:
  `tests/sim_test` (invariants), `tools/emu/test_sim.py` (differential:
  every function and whole years of `sim_update` against the original).

### WP1b: SOUL flow (priority 2, after WP0)

- Ranges: 0x458490-0x4619ef; the non-UI SOUL tracker logic from Soulview
  (0x47d6fb, 0x47de6c, 0x47e165-0x47f8fe walker placement) if WP6 agrees.
- Files: `src/game/souls.c`, `src/game/zones.c`.
- Provides: phase steps `souls_zone_scan()`, `souls_flow()`, `souls_arrivals()`,
  `soul_group_add()`, `area_search()` (0x45ebe2, used by WP3/WP4).
- **Done** (all of 0x458490-0x4619ef, plus the tracker sim side 0x47de6c,
  0x47e165, 0x47e3be, 0x47e4be, 0x47eaf7, 0x47ed71 in `src/game/soultrack.c`).
  Interface as built (`src/game/souls.h`, which also explains the flow):
  `souls_zone_scan()`, `souls_flow(max_groups)`, `souls_gate_arrivals()`
  (0x46114f), `souls_siphon_release()` (0x460b16), `soul_group_add()`,
  `area_search()`, `souls_zone_evict()` (0x45fd84), `souls_gate_spawn()`
  (0x461636), runtime counters `souls_groups_created` / `souls_gate_arrived`
  / `souls_to_heaven` / `souls_to_hell`. Differential test against the
  original: `tools/emu/test_souls.py` (scenarios as shipped, and
  `--synthetic` with Omnibolges, siphons and Soulview trackers added).
  Map object marks (0x4ee5d8/0x4ed550) are in `game/mapobj.h` (WP3).

### WP2: build tools, structures, world setup (priority 2)

- Ranges: 0x42b88b-0x42d637 (category, destroy), 0x4301b0, 0x449c6e-0x4514a3,
  0x4514a4-0x455ed6 (`map_rotate_cells`, `game_reset_world`,
  `game_init_defaults`), 0x455ed7-0x457b00 (tile chooser logic and info text).
- Files: `src/game/build.c`, `src/game/structure.c`, `src/game/newgame.c`,
  `src/game/rotate.c`.
- Provides: `build_apply_tool()` (0x44da29) for the window procs,
  `structure_destroy()` (0x42c09b), `map_stamp_building()`,
  `structure_class()` (0x42b88b), `game_reset_world()`.

### WP3: Karma network and map objects (priority 3)

- Ranges: 0x42d638-0x4306cf except 0x4301b0, 0x43293d-0x436470,
  0x436471-0x43a492 (`map_object_think`), 0x4ed550-0x4ef4fb (object system,
  the renderer already reads the records).
- Files: `src/game/karma.c`, `src/game/mapobj.c` (extends `src/gfx/map_view.c`
  only through its existing object-record interface).
- Provides: `karma_rebuild()`, `karma_trains_tick()`, `mapobj_step(plane)`,
  `mapobj_spawn()`/`mapobj_remove()`.
- **Done** (all of the ranges but the Bad Things' parts of
  `map_object_think`). How it works: the top of `src/game/karma.h` (rails,
  networks, stations, trains, the realm boats of 0x435f3d) and of
  `src/game/mapobj.h` (records, object map, the per-frame step). As built:
  `game/karma.h`: `karma_clear/rebuild/trains_tick/rail_check/year/
  pending_scan()` for sim_update, `karma_rail_remove()` (0x42f129, called
  by `structure_destroy`; the `karma_rail_removed` build hook is gone),
  `karma_car_step()` / `karma_boat_step()` for the object behaviour, hooks
  `g_karma_hooks` (sound, Microview refresh), `karma_reset()` (sim_reset
  calls it). `game/mapobj.h`: `mapobj_mark_cells/redraw/spawn/place/
  free_count/animate_cells/step/think()`, `mapobj_frame(animate)` (the
  object block of game_frame_update 0x407282, for WP7), hooks
  `g_mapobj_hooks` (redraw requests as `MapObjRedraw`, sound,
  wm_update, renderer plane select). The footprint 0x4ee5d8 reads from
  the tile entry is derived from the map (`mapobj_footprint`; equal to the
  TILE data on every structure of the four saves). `gfx/map_objects.h`:
  `map_view_redraw_object()` (0x4ed550's drawing) and
  `map_view_set_focus()` in map_view.h; the glue a main view installs is
  `ov_redraw` in `tests/map_render.c`. `map_object_think`'s Bad Things
  kinds (sets 3, 6, 0x14/0x15, 0x18-0x1b; helpers 0x4366ab, 0x436521) go
  to WP4's `badthings_object_think()` (game/badthing_objects.c). g_state fields renamed:
  `karma_dirs`, `karma_busy`, `karma_pending`, `karma_stations`,
  `karma_load`, `karma_history`, `object_count`, `boat_count`. Tests:
  `tests/karma_test` (network, trains, object map, years; footprints
  against MEDIUM.000), `tests/map_render` (trains and boats drawn
  incrementally, `<save>_objects*.png`, `_train_z0`, `_boat_z0`),
  `tools/emu/test_karma.py` (differential); test_sim/test_souls/test_build
  run the real map object code on both sides (`tools/emu/mapobj_env.py`).

### WP4: Bad Things and disasters (priority 3)

- Ranges: 0x43dc3c-0x43e4e4 (`bad_thing_start`), 0x44180a-0x446241 except
  0x441a12-0x441aa4, 0x445416 and 0x445785, the Bad Thing timers in `sim_update`
  (exposed as `badthings_tick()` so WP1 just calls it), 0x444d23 (bankruptcy
  ending), Bad Things menu handling in `game_wnd_proc` (WP7 calls in).
- Files: `src/game/badthings.c`.
- **Done** (all of the ranges; how each Bad Thing works is at the top of
  `src/game/badthings.h`). Files: `src/game/badthings.c` (bad_thing_start
  0x43dc3c and the starts 0x4425a1/0x442878/0x442960/0x4438ed/0x443b27/
  0x4451d7, the freeze/blues spread 0x442122 + 0x441c76 and recovery
  0x442287, the bankruptcy 0x444d23 with its walker 0x4442dd, 0x4440b3 and
  fires 0x443c9e, SAMNMAX 0x444ea5, 0x445064, 0x443c58),
  `badthing_objects.c` (map_object_think's cases: the build cheat's giant
  (set 3), SAMNMAX's walkers (set 6, 0x4366ab), Birds/Bats (0x14/0x15),
  Handbasket/Nose (0x18/0x19 with 0x442a48/0x442e96 targets and
  0x4432e4/0x44363a steering), the Dice (0x1a, 0x436521, 0x443a21) and
  Disco Inferno (0x1b)). Not ported: 0x44180a and 0x441ae6 (no callers).
  Interface: `bad_thing_start(k)` (menu, scenario 'D', random: -k),
  `badthings_spread/recover()` and `badthings_doom_step/fire_step()`
  (sim_update's timers), `badthings_bankruptcy()`, `badthings_cheat()`
  (shell_cheat_char), `badthings_object_think(slot, before)` (mapobj_think
  passes the record as it was on entry), `badthings_reset()` (sim_reset);
  side effects through `g_badthings_hooks` (message boxes with the game's
  texts via exe_str, news 0x36b, sounds, snd_stop/unload, music, the main
  view's plane, wm_update, repaint, the helpers' resignation), all set by
  the shell's `install_hooks`. The shell's No Bad Things item now sets
  `g_state.bad_things`; its checkmark follows the game after a new game or
  a load (with the original's FYI box when a load flips it). Save fields
  were all in `state.h` already (unk_5b2f61, unk_5bb0b0/unk_593200 loot,
  unk_57e098.., unk_63050d.., unk_5f4cd0 walkers, realm +0x80f9..+0x80fc).
  Quirks kept: 0x442287's scan jumps to a building's origin; Nose's
  Heaven-side guard tests bit 3 at site 2 and marks site 7; the flocks'
  droppings test CELL_DEFER with x and y swapped; trample paths don't check
  the guard's existence bit. Deviations (see
  [enhancements.md](enhancements.md)): a bird soiling a building in the
  top rows (y - size = -1) loops forever in the original (byte row
  counter); by default the port hangs too, with the enhancements on it
  stops at row 0. Always on: a failed croupier spawn and off-grid cell
  accesses don't write past the tables. structure_destroy's flags 3/4 leave a
  realm's population a little above its cells' SOULs, in the original too.
  Tests: `tests/badthings_test` (every kind, bankruptcy, cheats, the
  object map, determinism, save round trip, PNGs), `tools/emu/
  test_badthings.py` (differential, side effects included, all saves,
  rotated views); test_sim no longer stubs any Bad Thing.

### WP5: the Planet (priority 2, independent)

- Ranges: 0x495300-0x4a2efc and 0x4a8337-0x4ab8df (simulation; planet
  write/read 0x495300/0x495f7c are shared with the savegame agent: WP5
  writes them, savegame calls them), plus Planetview 0x4a2efd-0x4a8336.
- Files: `src/game/planet.c`, `src/game/planet_gen.c`, `src/ui/planetview.c`.
- Provides: `planet_stats_step()`, `planet_emit_step()`, `planet_sector_step()`,
  `planet_tech_step()`, `planet_year()`, `planetview_*`.
- **Done** (simulation; the model is described at the top of
  `src/game/planet.h`). Files: `src/game/planet.c` (stats, births/deaths,
  SOULs, migration, tech, influence, disasters, the Flood),
  `planet_gen.c` (0x496e71-0x49818e), `planet_map.c` (coastline bitmap,
  0x4a6b61-0x4a8337, 0x49beee), `src/ui/planetview.c` (the window).
  Interface for sim_update: `planet_stats_step()` (phase 20),
  `planet_emit_step()` then `planet_year(0)` (phase 30),
  `planet_sector_step()` (10), `planet_tech_step()` (15), each returning
  true when its pass is complete; `planet_flood()` (scenario 'F'),
  `planet_generate()` / `planet_reset_counters()` (game_reset_world),
  `planet_summary()` after loading. Tables come from ALIFE.EXE's data in
  `src/data` (`planet_tables_load()`). Side effects go through `g_planet_hooks`
  (`souls` = 0x445785, UI hooks filled by the Planetview). Checked:
  `tests/planet_test` (invariants over the four scenarios and a generated
  planet) and `tools/emu/test_planet.py`, which runs every step function
  against the original in the emulator and finds RNG, all planet globals,
  the coastline map and the SOUL groups identical (all four scenarios,
  forced disasters, the Flood, world generation).

### WP6: information views (priority 4; two agents if needed)

- WP6a: Graphview 0x40b1c0-0x41722f (history IO shared with savegame), Mapview
  0x463b70-0x4678f3. Files `src/ui/graphview.c`, `src/ui/mapview.c`.
  **Done** (0x467890 is the Microview's title, WP6b). Interface as built:
  `ui/graphview.h`: `graph_history_record()` (0x40d34b) and
  `graph_history_update()` (0x4154ab) for sim_update's phase 80 and
  game_reset_world, `graphview_refresh()` (graphview_draw(0, 0, 1)),
  `graphview_create/destroy/window()`; `ui/mapview.h`: `mapview_refresh()`,
  `mapview_draw()`, `mapview_create/destroy/window()`; `ui/views.h`: the
  shell context (`views_set_context`: main window, main `MapView`, pause
  flag) and hooks (`select_plane` both halves of 0x42b102, `help_balloon`
  0x478a14, `center_on_cell` 0x406be8, `menu_command` for the close
  buttons). The summaries are `g_state.summary[]` (field map at the top of
  graphview.c), the History Graph's series `g_state.rec_5c4eb0[]` (`Rec72`).
  Tests: `tests/views_test` (all graphs and layers of each save through the
  mouse path, PNGs), `tools/emu/test_views.py` (record/update against the
  original over 430 years).
- WP6b: Microview 0x4678f4-0x4750ef, Macromanager 0x4619f0-0x463b6f, Soulview
  UI 0x478d50-0x47fca6 (minus what WP1b takes). Files `src/ui/microview.c`,
  `src/ui/macro.c`, `src/ui/soulview.c`. Bank/loan/upgrade/funding actions call
  WP1/WP2.
  **Done** (all three ranges; the news pop-up and help balloon 0x4761a0 are
  WP7's). Files: `src/ui/microview.c` (+ `microview_info.c`, THINGS TO
  KNOW), `macro.c`, `soulview.c` (+ `soulview_text.c`: the name generator
  0x47f8fe and the life-story template expander 0x47af6f), `exe_text.c`
  (ALIFE.EXE texts by address, from `src/data`), `infoviews.c` (hook wiring); the
  trackers' sim side grew in `src/game/soultrack.c` (yearly pass 0x47d6fb,
  start 0x47e7cc with seed/belief 0x47f7be/0x47ef9f, walker 0x47e875,
  remove/reset/rotated, years left). Interface for sim_update:
  `g_sim_hooks.views_changed = infoviews_changed` (microview_refresh, and
  macro_refresh if shown), `g_sim_hooks.soulview_age = soulview_age`; for
  WP2: `g_build_hooks.soulview_reset / soulview_rotated`; all set by
  `infoviews_install_hooks()` (ui/views.h), which also fills
  `planetview_hooks.soul_name`. For the shell: `microview_create/open(x,
  y)/refresh/key_slide`, `macro_create/open/refresh` (Window menu 0x17),
  `soulview_create/open/refresh/track_cell(x, y)` (tool message 0x83e8),
  hooks for sounds, message boxes, news and the secret building's talking
  head (`microview_hooks`, `macro_hooks`). Note: realm_score_mix /
  realm_score_neighbours (which the Microview calls) count into realm
  +0x8105 / +0x8109, in the original too. Tests: `tests/views2_test`
  (headless, all pages through the mouse path, pixels and state changes
  against the save), `tools/emu/test_wp6b.py` (names, stories, trackers,
  Macromanager, Microview actions against the original).
  **Wired into the shell**: game_main creates the three windows at the
  original's loading steps (Soulview, Macromanager, Microview) and
  `ctrlpanel_place_windows` stacks them like 0x402695; `install_hooks`
  calls `infoviews_install_hooks()` and sets `shell_hooks` (microview_cell
  = microview_open, soulview_cell = soulview_track_cell, the show hooks,
  views_changed = infoviews_changed), `macro_hooks.refresh_all =
  views_refresh_all`, sounds through snd_play (skipped while iMUSE isn't
  running), message boxes, the artist's rendering through
  `news_popup_show(id, 0, 0)`. The secret building's talking head
  (0x46fc14) uses talker 4, which akos_talk_open rejects (n must be 1..3),
  so the original shows nothing; the port makes the same calls and frees
  the costume. `tests/shell_test` opens all three in a running game.
- Depends on the UI agent's scrollbars, list boxes and message box.

### WP7: game shell, title, advisors, news (priority 4)

- Ranges: 0x401000-0x408eaf, 0x409bb0-0x409dbf, 0x426460-0x42a6c2,
  0x417230-0x41f610 (advisors; talking heads through the AKOS agent's API),
  0x446904-0x447e8e (status bar: 0x446904, 0x446dc5, 0x447243; 0x446f43 and
  0x447088 have no callers), 0x4761a0-0x478d4f (news pop-up,
  help balloon), 0x457b01-0x45848f (debug window), 0x4acc30-0x4b0fff (about,
  credits).
- Files: `src/game/game_main.c` (replaces the smoke test in `src/main.c`),
  `src/ui/ctrlpanel.c`, `src/ui/title.c`, `src/ui/advisors.c`,
  `src/ui/news.c`, `src/ui/statusbar.c`, `src/ui/about.c`.
- Provides: `views_refresh_all()`, `statusbar_draw()`, `news_popup_show(id)`,
  `help_balloon_show()` (used by every view's help function).
- **Done** (playable end to end). Files: `src/game/shell.c` (game_main,
  main loop and game_command flows, menu bar and the menu-command switch,
  game_frame_update and its 125 ms clock, loading window, all hook wiring),
  `shell_view.c` (main map view: tile sets, paint, scroll, zoom, rotation,
  tool drag and highlight, the rest of game_view_proc), `shell.h` (shared
  globals, `shell_hooks` for WP6b/WP8: Microview/Soulview/Macromanager,
  audio dialog, tutorial), `src/game/music.c` (0x4752c2-0x475607),
  `src/ui/ctrlpanel.c` (control panel, tile chooser 0x455ed7-0x456402),
  `statusbar.c`, `title.c`, `advisors.c`/`advisors_text.c`, `news.c`,
  `about.c`. Load/save use a native file picker
  (`plat_file_dialog_*`, SDL3) or the game's text prompt where there is
  none (since WP8, only when a test sets `shell_pick_file_hook`; the
  game uses the original's slot dialogs, `src/ui/slotdlg.c`). `world_apply_loaded` now ends with
  `planet_summary()`. Test: `tests/shell_test <data dir> <out dir>` boots
  through the title, starts a new game, runs the sim, zones, builds roads
  and a gate through the control panel and tile chooser, saves, loads,
  loads ASCEN001.ASC and quits, all through synthetic input, and opens
  the Microview (tool on the gate, all pages), the Soulview and the
  Macromanager. Quit: a window close request is WM_CLOSE (the game's
  "Do You Really Want To Quit?"); SDL_EVENT_QUIT (SIGTERM / SIGINT) and a
  second close request while that dialog is open exit at once
  (`main_wnd_proc`, game/input.c). Not done:
  the debug window (0x457b01), scenario-editor extras of the tile
  chooser.

### WP8: tutorial and attract mode (priority 5)

- Ranges: 0x41f611-0x42645f, 0x4ab8e0-0x4acc2f; then the slot dialogs of
  the save/load range (0x47fdc6-0x480ea3, 0x480ea4-0x481ba3,
  0x489e90-0x48a839, 0x494112-0x4952ff).
- **Done.** How it works: the top of `src/game/tutorial.h`. The tutorial
  is a self-running demonstration: a lesson (Tutorial menu 0x25..0x2c ->
  `tutorial_run(id - 0x24)`, 0x425e3b; the shell passed id - 9 before)
  saves the player's game as slot 200 "Temp Game From Hell", loads the
  lesson's save (ALSV resources 10000..10008 in the CD's ALIFE.000,
  `save_open_slot` slots >= 10000), hides the views, and Jasper and Aria
  talk while the game moves its own pointer and clicks: control panel
  buttons, menu items (the tempo), map cells (zoning, roads, gates,
  Karma, siphons, ports), the Graphview's and Mapview's buttons, right
  clicks for help balloons. Real input is off meanwhile (the window
  manager's "attract mode", `wm_input_live` 0); a click or Esc stops the
  lesson (0x4ac69c `attract_stop(0)`), the player's game is loaded back
  and the windows restored (0x425ab0), "All Done!". There is no other
  attract or demo mode: the title has no idle timeout (title_menu
  0x428b22 shows the splash 5 s, then waits). Lesson 9 "Basic Economics"
  exists but has no menu item (the original's too).
  The lesson scripts are compiled code in the original (tutorial_script
  0x41f611, 25 KB: a switch with one long call sequence per lesson). The
  port has them as op tables (`src/game/tutorial_script.c`: one op per
  call, the speech guards `if (!silenced && stamp unchanged)` as
  TO_IF/TO_ENDIF), interpreted by `run_ops`; prologue (zone blocks on,
  zoom out, unrotate, all by scripted clicks) and epilogue by hand. The
  240 speech lines are ALIFE.EXE texts looked up by address (`src/data`) and
  voiced from the bundles (slot 199). Scripted input: 0x4ac2de..0x4acbee
  (`tut_*` in the symbols): glides of 10 px per axis every 10 ms
  (`tutorial_glide_start/step`, pure) to a control's centre, a menu item,
  a map cell (scrolling until visible) or a window-relative point; clicks
  first glide back to where the last glide ended if the pointer moved.
  Enhancements (off by default, see [enhancements.md](enhancements.md)):
  the waits stop when the lesson is stopped (the original ran the
  reloaded player's game on for the rest of the seconds / years), the
  cell glide gives up scrolling after 4 tries (the original loops).
  `src/ui/tutorialwin.c` is the "Afterlife Tutorial" text window
  (0x4ab8e0-0x4ac2dd, pages 1401-1410 with Prev/Next): left over from
  the demo version, created by nothing (its 0x5fa5f0 is in game_main's
  window table, which is why the shell called it the audio dialog);
  ported, not wired. Shell changes: the hook wiring, `views_set_context`
  before the views are created (the Graphview, Mapview, Microview,
  Soulview and Macromanager were top-level windows, so activating the
  main window, e.g. when a help balloon closed, put them behind it),
  `shell_slot_path`, `shell_save_game_to`, `shell_load_game_from`,
  `shell_load_game_resource`, `shell_save_dir_override`;
  `advisor_tutorial_setup` (advisors.c), `mapview_zoom` (mapview.c).
- **Slot dialogs** (`src/ui/slotdlg.c`): save 0x480ea4 / 0x48137d (list
  with an empty line for a new slot, title field, the current view shrunk
  by 0x47fdc6, "Overwrite existing save game?"), load 0x489e90 /
  0x48a3dc, the title's chooser 0x494112 (picks only; thumbnails remapped
  to the title palette, resource 0x14b6), delete 0x4946bd / 0x494a93;
  list fill 0x480a10 (newest selected), free slot 0x480db8, info
  0x4807c5, thumbnail 0x47fff4, help 0x49513c. Slots are agame%03u.alf /
  ascen%03u.asc in the save folder; scenarios fall back to the data dir
  (the CD). game_main's commands 1/2/3/5/6 and the title's Load buttons
  use them.
- Tests: `tests/tutorial_test <data dir> <out dir> [lesson ...]` (a
  negative lesson stops it with Esc): every lesson through the menu on a
  virtual clock, every op runs with the input scripted, the controls the
  pointer clicks exist and are visible, lessons 1/3/4/5/7/8 changed what
  they should through the clicks (zones, roads, a gate, the tempo; Karma
  track; the Labor Graph; the Mapview layers; a siphon; two ports), the
  player's game (map, money, year) comes back, Esc mid-lesson; the text
  window; the slot dialogs (save new, save new, overwrite, load, choose,
  delete, a scenario) through clicks; PNGs. `tools/emu/test_tutorial.py`
  (differential): the original tutorial_script with every callee hooked
  against the port's op interpreter, all 9 lessons, talking and silenced
  (identical call sequences, arguments, window positions and the game
  speed at each wait), the glide (300 random glides, step by step) and
  the text window's filter (300 random pages).

### Save/load (savegame agent, already running)

0x47fca7-0x4952ff (file names, the three dialogs, writers 0x481ba4/0x485fb3,
`save_load_game`, `save_load_plane`). It calls `graph_history_read/write`
(WP6a), the planet IO (WP5), the Soulview tracker reader 0x47951c (WP6b) and
the recompute pass 0x4506b6 (WP2).

## Open questions

- The sim-cell payload union per tile set (see `sim_payload()` in
  `tests/map_render.c` for sizes); WP0 should pin down the field meanings.
- Where the per-tile-type tables 0x57e034 / 0x630204 come from (game data or
  `game_init_defaults`).
- Exact meaning of sets 5, 17, 19
  (19 = banks, from the Microview loan code).
