# Startup, ALIFE.INI and the init sequence

Port: `src/main.c` (entry and WinMain), `src/game/startup.c` (init order, task
table, pump), `src/game/ini.c` (INI reader), `src/game/config.c` (the
INI-reading parts of each init function, gathered in `AlConfig`). Run
`build/ini_dump [DIR [FILE.INI]]` to see what an INI parses to.

## Entry

```
entry 0x4b1007          MSVC CRT: GetVersion, heap, __cinit, skips argv[0] in the
                        command line, GetStartupInfo -> nCmdShow
win_main 0x4c62e0       WinMain(hInstance, 0, cmdline, nCmdShow)
  memset the app globals (0x64dc50, 0x64e070)
  GetModuleFileNameA -> exe_path 0x64e090
  app_already_running 0x4c6b3e   FindWindowA(class = exe file name), then "ALIFE.EXE";
                                 if found, return 0. Otherwise app_register_class
                                 0x4cd892 registers that class (wndproc main_wnd_proc
                                 0x4cb7ad, icon 101) and creates a hidden 1x1 window,
                                 which is what a second instance finds.
  app_chdir_to_exe 0x4c6a64      _chdrive + _chdir to the exe's folder
  app_split_cmdline 0x4c689e     strtok(cmdline, "\t ") -> argv (argv[0] = exe_path)
  app_parse_args 0x4c6911        "/x" or "-x": switch, only "/?" does anything (exit(0)).
                                 *.INI -> arg_ini_path 0x64e190
                                 *.ALF (saved game) or *.ASC (scenario) -> arg_load_file
                                 0x64dc64 (extension compared after _strupr)
  ini_set_path 0x4c6ad6          arg_ini_path, else exe base name + ".ini" ("ALIFE.ini",
                                 relative to the exe folder)
  sys_initialize 0x4c6485        stops at the first init that returns non-zero
  game_main 0x401000 (argc, argv)
  PostQuitMessage(0); sys_shutdown 0x4c65ef
  return app_exit_code 0x64e08c
```

Port differences: no single-instance check or chdir. The data directory (first
plain argument, else the folder of a given .INI, else `orig`) takes the place of
the exe folder, and every file name, INI included, is resolved in it
case-insensitively (DOS drive letters stripped). Only `-x` and `/?` count as
switches, so absolute Unix paths work. `--quit` exits after init.

## ALIFE.INI

There are no `[sections]` and no `GetPrivateProfileString`. `ini_get_string`
(0x4ce2e3) opens the file (`"rb"`) on every call, reads it with `fgets`, and
takes the first line that **starts with** the key (case-sensitive `strncmp`
over the key's length). The value starts after `=` with leading spaces skipped,
and is cut at the first `\r`, or else at the first `\n`. Trailing spaces stay. With no buffer
it uses a shared 256-byte static (0x546dc0). Quirk: lines are read into the
caller's buffer, so when the key is missing, an unchecked caller sees the file's
last line (this happens with `Window name` and `Savepath`).

`ini_set_string` (0x4ce465, rewrites one key via a temp file) has no callers.

| Key | Read by | Meaning | Default if missing |
|---|---|---|---|
| `Heap size` | he_initialize 0x4c98bb | Game heap in KB (`atoi << 10`), malloc'd as one block | 4096 KB (0x400000) |
| `CD Path` | rc_initialize 0x4cd9b0 (also cd_spin_up 0x4486bd, cfg_cd_dir 0x4803dc) | JIFF bundle on the CD (`D:\ALIFE.000`). It must index, or "Please insert the Afterlife CD" | **fatal**: "Cannot locate the resource file path in the INI file" |
| `Resource file` | rc_initialize | Local bundle (FULL.000, MEDIUM.000 or `None`). Searched before the CD, failure ignored | **fatal**, same message |
| `Color depth` | di_initialize 0x4ca1c0 | Expected bits per pixel. `DONT CARE` (any case) skips the check. If it differs from the desktop, an OK/Cancel warning appears, and Cancel exits | no check |
| `Force display settings` | di_initialize | `ON` (exact): switch display mode (`di_display_mode` bit 4) and require the resolution in the EnumDisplaySettings list ("The resolution you have selected (%dx%d) is invalid") | off |
| `Resolution` | di_initialize | `MAX` (exact) or `%dx%d`, clamped to the desktop. Smaller than the desktop gives a framed window (mode 2), otherwise a desktop-sized popup (mode 1) | desktop size |
| `Window name` | di_initialize | Main window caption | (last INI line, see the quirk above) |
| `Sound` | snd_initialize 0x4b9510 | `NONE` (any case): no sound at all, and no compbun or iMUSE init | on |
| `Music stream` | snd_initialize | `size, a, b`. Only `size` (the music stream buffer, he_malloc'd) is used. `a`/`b` are parsed into 0x6725a4/0x6725a0 and never read | 204800, 153600, 32768 |
| `Voice stream` | snd_initialize | Same for voice (0x67258c, 0x672594, 0x672590) | 66560, 49152, 22528 |
| `Mixer channels` | snd_initialize | iMUSE mixer voices (`atoi`) | 8 |
| `Save games` | cfg_save_games_dir 0x480375 | Save folder. A miss shows "Missing INI profile string" | error box |
| `Savepath` | di_save_screenshot 0x4ccf6e | Screenshot folder (cut at the first space, `\` appended) | (last INI line) |
| `Video Settings` | di_video_benchmark 0x42a6c3 | Number. Bit 1 = slow bitmap copy, bit 0 = slow window update. Skips the startup benchmark (50 blits of each kind, thresholds 2500 ms and 3625 ms) | benchmark |

The shipped INIs also have `Installation` and (AFTERDOS.INI) `BIOS`, and
AFTERDOS.INI spells `Mixer Channels` with a capital C. ALIFE.EXE never reads the
first two, and the third doesn't match its key. They are for the setup program
and ALIFEDOS.EXE. The setup's example `Color depth = 8` matches only on a
256-colour desktop.

## Init order: sys_initialize (0x4c6485)

Every init returns 0 on success. Any other value aborts with -1. Then
`sys_state` (0x64dc60) becomes 1 (running). Message boxes check it to decide
between MessageBoxA and the game's own dialog.

| # | Function | What | Task registered |
|---|---|---|---|
| 1 | he_initialize 0x4c98bb | GlobalMemoryStatus, `Heap size`, heap block + 4096-node free list ("HeInitialize") | |
| 2 | sys_null_initialize 0x4d1de0 | empty | |
| 3 | rc_initialize 0x4cd9b0 | `CD Path`, `Resource file`, index both JIFF bundles | |
| 4 | di_initialize 0x4ca1c0 | `Color depth`, system palette, desktop size, `Force display settings`, `Resolution`, low-memory warnings (< 10 MB phys+page: warning, < 7 MB phys: notice), `Window name`, identity palette, main window (di_set_main_window_size 0x4cd73a), DCs, mouse clip, squares tables | |
| 5 | tm_initialize 0x4d14e0 | timeSetEvent 16 ms periodic (tm_start 0x4d15a1) | |
| 6 | ms_initialize 0x4b8cd0 | centre cursor, clip rect | |
| 7 | kb_initialize 0x4d05e0 | clear key state | |
| 8 | snd_initialize 0x4b9510 | `Sound`, stream buffers, C File Layer hooked to the resource manager, **vh_initialize 0x4d51d0** (VHUMOR error, module and memory runtime: vh_error_init 0x4dbaf0, vh_module_init 0x4d7a10, vh_mem_init 0x4d8190), **compbun_init(1, 0)**, `Mixer channels`, **imuse_initialize 0x4d3ea0** | snd (all four) |
| 9 | pcyc_initialize 0x4ceba0 | palette cycling (up to 100 ranges, rotated every 100 ms) | idle pcyc_idle 0x4cf108 |
| 10 | cur_initialize 0x4cf400 | cursor table, default arrow | |
| 11 | wm_initialize 0x4c1580 | window manager: mouse, timer and paint hooks, root window (wm_create_window 0x4c1bce) | all four |
| 12 | ctl_initialize 0x4ba100 | controls: button blink, scroll auto-repeat | suspend, resume, idle ctl_idle 0x4bf68a |
| 13 | pal_highlight_initialize 0x4c6bd0 | 256-entry remap to the colour 20% brighter | |

So the VHUMOR runtime, compbun and iMUSE all come up inside the sound init, and
none of them come up with `Sound = NONE`. game_main registers one more task,
via 0x4b9470 (0x4d29b0, 0x4d2c50: four timed channels that post message 6000).

**Task table** `sys_tasks` (0x5351c8): 10 entries `{owner, shutdown, suspend,
resume, idle}`, added by sys_register_task 0x4c6755 and removed by
sys_unregister_task 0x4c67f6 (keyed by owner, the init function).

**Shutdown** sys_shutdown 0x4c65ef (once, `sys_shutdown_done` 0x672780, sets
`sys_state` 2): pal_highlight 0x4c6be7, ctl 0x4ba130, wm 0x4c1722, cur 0x4cf444,
pcyc 0x4cebe3, **di 0x4ca6ac**, snd 0x4b97e8, kb 0x4d05f7, ms 0x4b8d28, tm
0x4d150b, rc 0x4cdf21, null 0x4d1df2, he 0x4c9a86. This is the reverse of init
except that the display goes down before sound. Fatal errors (error_box
0x4d5570 with fatal set) call sys_shutdown and exit(0).

## Message pump and window procedure

`sys_pump` (0x4c63f0): one `PeekMessage(PM_REMOVE)` → Translate/Dispatch, then
every task's idle callback, then a tick high-water mark (0x6727e4). There is no
blocking GetMessage loop anywhere. game_main and every modal loop (dialogs,
tutorials, file pickers, more than 100 call sites) call sys_pump.

`main_wnd_proc` (0x4cb7ad), for the main window:
- WM_DESTROY → PostQuitMessage. WM_PAINT → paint hook (set by wm_initialize).
- WM_ACTIVATE: deactivate → `sys_suspend` 0x4c666a (task suspends, then di,
  tm, ms, kb, null, he). Activate → palette, `sys_resume` 0x4c66e3 (reverse
  order, then task resumes). SC_MINIMIZE also suspends.
- WM_KEYDOWN/UP/CHAR (and SYS variants) → kb 0x4d06f1 / 0x4d070e / 0x4d072b.
- Mouse move/buttons → ms 0x4b8f90, 0x4b901d.. (with Set/ReleaseCapture).
- WM_SETCURSOR → current cursor. WM_DISPLAYCHANGE → re-create the window.
  WM_QUERYNEWPALETTE / WM_PALETTECHANGED → realise the identity palette.
  WM_CLOSE → a message 0x406 to the root window (the game's quit dialog).

The port's sys_pump drains all pending platform events, forwards them to a
hook until kb/ms are ported, and reports the close button through
`sys_quit_requested()`. Focus changes don't call suspend/resume yet.

## game_main (0x401000) and the main loop

1. iMUSE/game-time setup (0x4b9470), cursors 500/501, palette resource 0x21,
   UI bitmaps 0x4000-0x4018, the main window ("Afterlife", wndproc 0x429c4d).
2. If no `.ALF/.ASC` was passed: the intro/title (0x426460, which can quit
   straight away), then 0x42b088. Otherwise skip it.
3. Game module inits (0x408208, 0x408eb0, 0x479e29, 0x463b70, 0x4a2efd,
   0x417230, 0x461c9b, 0x4756f4, 0x40d75e, 0x4678f4, ...) with a progress
   bar (0x408356) between them. Then the "Control Panel" window with 37
   buttons, the "Choose Tile Window", the world (0x5bb120) and the views.
4. A passed file is loaded with 0x48f837(world, 50000).
5. Main loop:

```c
game_running = 1;                     /* 0x650054 */
do {
    sys_pump();
    game_frame_update();              /* 0x407282: edge scrolling, redraw, sim tick */
    ui_update(main, ctrl, ..., world);/* 0x44876b */
    sys_pump();
    0x4a479f();                       /* text cursor blink */
    switch (game_command) {           /* 0x650048, set by menus */
    case 1: case 6: load (0x480ea4(1|2)); break;
    case 2: case 5: 0x489e90(1|2); break;
    case 3: 0x4946bd(); break;
    case 4: new game: difficulty dialog, reset world; break;
    }
    if (game_next_command) game_command = game_next_command;   /* 0x65004c */
} while (game_running);
```

game_running is cleared by the main window's handler 0x404023 (quit). Then
game_main stops sound (0x4b9ca0(199)) and returns, and win_main runs
sys_shutdown.

## Not ported yet (stubs in startup.c)

Desktop size and depth (the port assumes 640x480x8), display mode list, the
identity palette and main window details (the port calls `plat_init`), memory
status warnings, the game heap (malloc instead), timer, mouse, keyboard,
iMUSE, palette cycling, cursors, window manager, controls, highlight table,
and game_main (main.c runs a resource viewer in its place).
