# ALSV: saved games (.ALF) and scenarios (.ASC)

Saved games (`<Save games>\agame%03u.alf`) and scenarios (`ascen%03u.asc`,
e.g. the shipped `ASCEN001`–`004.ASC`) use the same format. Which name a slot
gets depends on `save_mode_game` (0x66aeec): 1 in normal play, otherwise the
game is in its scenario-authoring mode, which also prompts for the scenario
texts when saving. The command line accepts either extension
(`arg_load_file`, docs/startup.md). Slot numbers of 10000 and up open a
resource from the bundle instead (`save_open_slot` 0x47fca7).

Code: `save_load_game` 0x48f837 / `save_write_game` 0x485fb3 in the
original; port `src/game/savegame.c`, state `src/game/state.h`. The file is
the game's globals dumped field by field, little-endian, with no padding and
no checksums. Fields were added over development, each behind a version
test `version > N`. ALIFE.EXE writes version **7300** and reads all older ones.
The shipped scenarios are **7195**. In the tables, `[>N]` marks such a gate.
The binary compares against hex constants, so `[>7219]` is `> 0x1c33`.

The port reads and writes through one field list, so `savegame_save_mem`
can also write older versions. All four sample scenarios round-trip byte for
byte at 7195 (`tests/savegame_test`).

## Header

| Field | Type | Notes |
|---|---|---|
| magic | `"ALSV"` | anything else: "not a valid Afterlife save game" |
| version | 4 ASCII digits | `save_version` 0x66aee4 |
| kind | u8 | writer's 2nd argument (0x650050 at the call); ignored on load; 1 in samples |
| timestamp | u32 | real time of saving, from `save_pack_timestamp` 0x47fc20: `(tm_year+32)<<26 \| tm_mon<<22 \| tm_mday<<17 \| tm_hour<<12 \| tm_min<<6 \| tm_sec`. The year wraps every 64 years. The samples decode to 1996-04-29. |
| year | u32 | `game_year` 0x630268 (the body stores it again) |
| title | char[], NUL-terminated | slot description |
| difficulty [>7219] | u16 | `game_difficulty` 0x5c5c14: 1 easy, 2 medium, 3 hard |
| thumb_w, thumb_h [>7025] | u32, u32 | 104 x 78 for slots < 200, else 1 x 1 |
| thumbnail [>7025] | w*h bytes | the map window, palette indices, row-major. The loader skips it. |

## Map planes (`save_load_plane` 0x48a83a / `save_write_plane` 0x481ba4)

Planes 1 (Hell), 2 (Karma middle plane), 3 (Heaven), in that order. Each plane
walks its 100 x 100 grids **column-major** (x outer, y inner):

```
u32 cell          map cell (docs/map.md): bit 7 cleared on load, bit 4 on save
u8  objmap        object map byte
planes 1, 3 only:
  u8 kind         simulation cell byte 0
  payload         by the cell's tile set (cell >> 27), into the 18-byte sim cell
```

| Set | Payload fields (offset:size in the 18-byte sim cell) |
|---|---|
| 0 (part of a big building) | 1:1 2:4 6:4 10:1 11:1 |
| 1, 4, 16 | 1:1 |
| 2 | 1:1 2:1 |
| 3 | 1:4 5:4 9:1 |
| 5 | 1:4 5:4 9:4 13:1, [>7104] 14:2 |
| 6 | 1:4 5:1 6:9 15:1 |
| 7 | 1:4 5:1 |
| 8, 13 | 1:4 5:4 9:4 13:1 14:1 |
| 9 | 1:4 5:4 9:4 13:1 14:1 15:1 |
| 12 | 1:4 5:4 9:4 13:1, [>7019] 14:2 |
| 14, 15 | 1:1 2:4 6:4 10:4 14:1 15:1 16:1 17:1 |
| 17 | 1:4 5:4 9:1 10:1 |
| 18 | 1:4 |
| 19 | [>7089] 1:4 5:1 6:1 (else zeroed) |
| 29 | [>7089] 1:4 5:4 |
| 30 | [>7089] 1:4 5:4 9:4, [>7209] 13:1 14:1 |
| 10, 11, 20–28 | none |

After the cells of planes 1 and 3 comes that realm's **statistics record**
(`plane_stats_hell` 0x630520 / `plane_stats_heaven` 0x5b2f70, 0x812b bytes
packed; `plane_stats_current` 0x630428 points at the selected one).
`PlaneStats` in state.h lists every field in file order with its record
offset. In outline: two 112-entry history tables of 0x89 bytes
(`u32,u32,u32[32],u8`), several u32[16]/u8-pair tables, per-sin/virtue u32[7]
sets, then about 60 scalars. +0x7fec is the realm's population ("SOULs"), and
+0x80fd/+0x8101 is a SOUL pool (bignum hi, lo). It is 33,067 bytes at version 7195.

## Global block (rest of 0x48f837)

In file order (state.h `GameState`, addresses are the globals):

1. `0x57e1c0` 64 x 32 records of 42 bytes: `u32, u8,u8,u8, 9 bytes, 16 bytes, 7 bytes, u8,u8,u8`.
2. `0x64e500` 25 x 9 bytes.
3. `0x64e5f0` 63 map objects, 23 bytes each: 11 u8, u32 set mask, 4 u8, u32 flags
   (docs/map.md). Slots 1–10 are parked with plane 99 in the samples.
4. `0x59cf10` 8192 records `u32, u16, u32, u8` (11 bytes).
5. `0x5b2f10` u32[16].
6. 7 entries, interleaved: `u8 0x593240, u8 0x57e060, u8[2] 0x593260, u32 0x5fa600,
   u32[2] 0x6303d0, u32 0x5bb210, u32 0x5bb22c, u32 0x5bb248`.
7. 4 entries: `u8[3] 0x5f25b0, u8[3] 0x630410, u32 0x5fa660` (the byte triples look like dx, dy, direction).
8. `0x5c4eb0` 0x72-byte records, 18 of them if version < 7130, else 30:
   `u32[25], u32, u32, u8, u8, [>7124] u32`.
9. Scalars. These have names: `pennies_lo` 0x63043c, `pennies_hi` 0x630438
   [>7014] (money, a sign-and-magnitude bignum, see `Bignum` in state.h),
   `embos_lo`/`embos_hi` 0x630494/0x630490
   (Earth population; the hi word [>7299]), `game_year` 0x630268,
   `view_dir_x`/`view_dir_y` 0x5fa5f8/0x5bb118 (map rotation, copied into the
   map view after loading), `sim_phase` 0x5bb104 (the scheduler phase of `sim_update`), `lost_souls` 0x5c5c10
   [>7169], `bad_things_enabled` 0x593248 [>7249]. Loading a game whose
   setting differs from the current one shows an "FYI..." box.
10. Scenario texts [>7029], NUL-terminated: `scenario_title` 0x57e170,
    `scenario_description` 0x6302c0, `scenario_button` 0x57e070,
    `scenario_commands` 0x5bb180 (e.g. `"x"`, `"w4fx"`).
11. `[>7184]` u32 0x63048c, u32 0x57e160; `[>7249]` bad things; `[>7279]` u16 0x5b2f5a.

## Sub-module blocks

| Block | Load / save | Contents |
|---|---|---|
| graph summaries | `save_load_graph_summaries` 0x40c106 / 0x40b1c0 | two records (0x6399d0, 0x639480) read by the graph views. All fields [>7160]. |
| planet | `save_load_planet` 0x495f7c / 0x495300 | u32 size + `planet_map` bytes (bit-packed Earth map, 25601 in the samples), three counted lists (u8 count each), a [>7194] 64 x 32 grid of u32 pairs saved j-major, u32[0x3800], u32[0x4800], trailing scalars |
| block26 | `save_load_block26` 0x47951c / 0x478d50 | u8 selected, u8, u8, then 10 records of 0x26 bytes (byte +0x24 not saved) |

The file ends right after block26. After loading, the game reselects each
plane (rebuilding derived tables), resets the view and un-pauses.

## Not in the file

iMUSE state (sound), the window layout, and every derived table the game
rebuilds after loading.
