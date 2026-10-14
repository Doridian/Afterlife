# Startup, ALIFE.INI and the init sequence

Port: `src/main.c` (entry and WinMain), `src/game/startup.c` (init order, task
table, pump), `src/game/input.c` (keyboard queue, main_wnd_proc),
`src/game/timer.c` (the 16 ms timer), `src/game/ini.c` (INI reader), `src/game/config.c` (the
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
| `Music stream` | snd_initialize | `size, critical, load`: the music stream buffer (he_malloc'd) and its refill thresholds, read by iMUSE through `buf_info` (0x4b9eb5, `{buf, size, loadSize, criticalSize}` at 0x672598): streams below `critical` bytes are refilled first, `load` bytes at a time. See imuse.md | 204800, 153600, 32768 |
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
| 10 | font_initialize 0x4cf400 | font cache, built-in font in slot 0 (see rendering.md) | |
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
every task's idle callback, then the heap high-water mark (`he_bytes_peak`
0x6727e4 = max(itself, `he_bytes_used` 0x6727e0)). There is no blocking
GetMessage loop anywhere. game_main and every modal loop (dialogs, tutorials,
file pickers, more than 100 call sites) call sys_pump.

`main_wnd_proc` (0x4cb7ad) ignores the hidden 1x1 window (DefWindowProc) and
handles, for the main window:

| Message | Action |
|---|---|
| WM_DESTROY | PostQuitMessage(0) |
| WM_PAINT | BeginPaint, paint hook (`di_paint_handler`, set by wm_initialize) with the paint rectangle, EndPaint |
| WM_ACTIVATE inactive | `app_active` 0x672934 (initially 1) → 0 and `sys_suspend` 0x4c666a (task suspends, then di, tm, ms, kb, null, he). A desktop-sized window goes to the bottom of the Z order |
| WM_ACTIVATE active | ignored while `app_minimized` 0x67292c. Otherwise topmost if desktop-sized, re-push the palette (`di_set_palette(NULL, 10, 236)`), and if inactive: `app_active` = 1, `sys_resume` 0x4c66e3 (reverse order, then task resumes) |
| WM_SYSCOMMAND | SC_MINIMIZE: `app_minimized` = 1 and suspend. SC_RESTORE: `app_minimized` = 0 (resume follows with WM_ACTIVATE). SC_MOVE and SC_SCREENSAVE are swallowed |
| WM_WINDOWPOSCHANGING | adds SWP_NOSIZE: the window can't be resized |
| WM_KEYDOWN, WM_SYSKEYDOWN | `kb_on_key_down(wParam)` 0x4d06f1 |
| WM_KEYUP, WM_SYSKEYUP | `kb_on_key_up(wParam)` 0x4d070e |
| WM_CHAR | `kb_on_char(wParam)` 0x4d072b (WM_SYSCHAR is not handled) |
| WM_MOUSEMOVE | `ms_on_move(x, y)` 0x4b8f90 |
| WM_L/R/MBUTTONDOWN, ..UP | SetCapture / ReleaseCapture, `ms_on_*_down/up` 0x4b901d-0x4b90f9 |
| WM_SETCURSOR | `ms_set_cursor(current)`, returns TRUE |
| WM_CLOSE | guarded by `app_closing` 0x672938: ShowWindow(SW_SHOWNORMAL), then message 0x406 to the window manager's root window (the quit dialog). The window is never destroyed here |
| WM_DISPLAYCHANGE | new desktop size, re-create the main window (0x4cd73a), tell the window manager (0x4c6096) |
| WM_QUERYNEWPALETTE, WM_PALETTECHANGED | (not from itself) realize the identity palette, repaint if it changed |

The window class (style CS_OWNDC) has no CS_DBLCLKS, so Windows never sends
double-click messages. The window manager makes them (see below).

## Input and timer

### Keyboard (kb_*, 0x4d05e0-0x4d08d6) → `game/input.c`

A 32-entry FIFO (`kb_queue` 0x548628, write/read/count 0x548600 / 0x54860c /
0x548604) of packed 9-byte records `{int type, int vk, char ch}`:

| type | Pushed by | vk | ch |
|---|---|---|---|
| 1 | kb_on_char (WM_CHAR) | 0 | the character |
| 2 | kb_on_key_down (WM_(SYS)KEYDOWN, auto-repeat included) | wParam | low byte of wParam |
| 3 | kb_on_key_up | wParam | low byte of wParam |

A full queue drops new events. `kb_get_event` 0x4d0666 (named kb_flush_one
before) pops one into a static record (0x548618) and returns it, or NULL.
The only consumer is the window manager's idle task 0x4c178f: one event per
pump, sent to the focus window as message 0x1e (char: ch, vk), 0x1f (key down:
vk, ch) or 0x20 (key up), and it tracks VK_SHIFT (0x10) in 0x674330. While
the window manager is disabled (0x672750 = 0), any mouse button or an Escape
char calls 0x4ac69c(0) instead and '.' still goes through. `kb_post` 0x4d08b7
injects events (0x4acbee fakes a char + key up). `kb_wait_key` 0x4d0690 (no
callers) dispatches messages until a key is queued. `kb_set_handler` stores a
callback nobody calls. `kb_debug_mda` 0x4d0746 draws the queue indices on a
monochrome debug monitor at 0xb0000 (not ported).

### Mouse (ms_*, 0x4b8cd0-0x4b9470) → `gfx/mouse.c`

See rendering.md. Position and buttons (1 left, 2 right, 4 middle) are
updated by the window messages. Each button transition queues {x, y,
buttons} (32 entries). Moves are not queued. `ms_get_state` returns the oldest
queued transition, else the current state. The window manager's idle task
polls it once per pump and turns changes into hook events (1 move, 2/3/4
down, 5/6/7 up, 8 hover: the pointer stayed within 1 pixel for 1500 ms). The
hook 0x4c523a finds the window under the pointer, applies its cursor, sends
enter/leave (0x40), and on a button up sends a double-click message (0x2b /
0x2c / 0x2d for left / right / middle) before the up message (0xc / 0xd /
0xe) when the previous up was the same button less than 500 ms ago (by
timeGetTime) and less than 4 pixels away in x and y.

### Timer (tm_*, 0x4d14e0-0x4d1e2b) → `game/timer.c`

`tm_initialize` clears `tm_stopped` 0x674350 and calls `tm_start(16, 0x10000)`
0x4d15a1. The first (nested, `tm_start_count` 0x674340) start clears slots
1-19, stores the period (`tm_period_ms` 0x54ec78 = 16) and arms
`timeSetEvent(16, 16, tm_callback, 0, TIME_PERIODIC)`. `tm_stop` 0x4d166f
kills it when the count reaches 0.

Slots (`tm_slots` 0x54e980, 20 x 38 bytes, slot 0 holds only the tm_start
argument): callback(slot, user) +0x00, period ms +0x04, flags +0x08 (1 =
immediate, 2 = one-shot, 4 = paused), user +0x0a, data +0x0e (freed on
removal, never set), busy +0x1a, accumulator +0x1e, running +0x22 (waited for
on removal, never set). `tm_add` 0x4d1756 takes the first free slot (returns
1-19, or 0). `tm_add_service(cb, hz)` 0x4d1550 is tm_add(cb, 1000 / hz, 0, 0).
`tm_remove` 0x4d1865, `tm_remove_service` 0x4d1583, `tm_set_period` 0x4d18f6
(resets the accumulator), `tm_lock`/`tm_unlock` 0x4d170e/0x4d1720 (nested;
ticks still count but run no callbacks), `tm_wait(ms)` 0x4d1940 (spins on the
counter), `tm_get_elapsed` 0x4d198d, `tm_get_period` 0x4d19a2. The last four
have no callers.

`tm_callback` 0x4d19b7, every 16 ms on winmm's thread, unless stopped:
`tm_elapsed_ms` 0x54e978 += 16, `tm_tick_count` 0x674364 += 1. Unless locked:
pass 1 adds 16 to every live slot's accumulator and runs the immediate,
unpaused ones. Pass 2 runs the normal (flags 0) ones, each guarded by its busy
flag. "Runs" means: while period <= accumulator, subtract the period and call
it (a one-shot clears its period and stops after one call). Around this it
saves and clears a VHUMOR thread-switch request (0x674334; `tm_in_callback`
0x674338) so threads never switch inside a callback. That cooperative
threading (setjmp-based, 0x4d1d39) is never enabled in ALIFE.EXE (0x67435c
is always 0).

The only client is snd_idle (0x4b9d7c): once iMUSE has filled in its service
routine (0x523160), it registers it with `tm_add_service(cb, 60)`, a 16 ms
period, so iMUSE runs once per tick. snd_shutdown removes it (so does
tile_unload_all, with a handle that is always 0).

Everything else reads the clock directly with `timeGetTime` (about 80 call
sites): game_frame_update (0x407282), ui_update (0x44876b), the window
manager's idle task and mouse hook (hover, double-click), ctl_idle (blink,
auto-repeat), pcyc_idle (100 ms), snd_idle (iMUSE every 100 ms, sound
timeouts after 60 s), the FLC player and the intro, the video benchmark,
the text cursor blink (0x4a479f) and the progress bar (0x408356).

### Port

The platform layer (`plat_poll_event`) turns SDL events into one
`PlatEvent` per window message: key down/up with Windows virtual-key codes
(`PLAT_VK_*`, letters follow the layout), WM_CHAR from SDL text input
(UTF-8 to Windows-1252) plus the control characters TranslateMessage adds
(Backspace 8, Tab 9, Enter 13, Escape 27, Ctrl+letter 1-26; nothing with Alt,
as WM_SYSCHAR is ignored), mouse move (runs of motion events are collapsed,
as Windows does with WM_MOUSEMOVE) and buttons, focus (WM_ACTIVATE),
minimize/restore (WM_SYSCOMMAND), expose (WM_PAINT), close (WM_CLOSE).
`sys_pump` keeps the original shape: `tm_poll()` (port), one event through
`main_wnd_proc`, the idle tasks, then `gfx_present()` (port: GDI drew to the
window at once) and `plat_yield()`. WM_CLOSE sends 0x406 to the window
manager's root (which forwards it to the top-level windows); before
wm_initialize it sets `sys_quit_requested()` instead. Mouse capture is
implicit in SDL.

The tick has no thread (WebAssembly has none): `tm_poll` runs every 16 ms
tick that is due by `plat_ticks_ms()` (catching up at most 64 after a
stall), so callbacks run on the game thread between messages. `tm_wait`
polls it. `tm_get_time()` is timeGetTime. `tests/input_test` pushes synthetic
SDL events through the whole path and checks the queues and the timer.

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

Display mode list (any size is accepted with "Force display settings"), the
system palette save/restore, memory status warnings, the game heap (malloc
instead), vh_initialize, the controls' pause flag (no game module yet),
WM_DISPLAYCHANGE, and game_main (main.c runs a smoke test in its place: a
bundle bitmap in a WM window with a button, music through `snd_play`;
arrows page through the bitmaps, the button or Escape quits,
`AFTERLIFE_FRAMES=n` exits after n pumps). The display init asks the platform
for the desktop size and reports 8 bits per pixel.
