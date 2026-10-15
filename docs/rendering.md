# Rendering: the video core

ALIFE.EXE draws everything in software into 8-bit DIB sections and copies
them to the window with GDI. There is no DirectDraw and no full-screen back
buffer. This page covers the low-level layer: surfaces, palette, blits,
primitives, fonts and the cursor. Window management (`wm_*`, 0x4c1580),
FLI/FLC and TILE drawing sit on top of it and are documented separately.

Port code: `src/gfx/` (game side) and `plat_video_*` / `plat_cursor_*` in
`src/platform/`. `tests/gfx_test.c` exercises it against the user's bundle
(see [Verification](#verification)).

## Modules

| Range | Module | Port |
|---|---|---|
| 0x4ca1c0-0x4cd892 | Display interface ("Di"): surfaces, blits, palette, window | `gfx/surface.c`, `gfx/palette.c`, `gfx/draw.c` |
| 0x4ceba0-0x4cf2a2 | Palette cycling (`pcyc_*`) and shade tables | `gfx/palette.c` |
| 0x4cf400-0x4d056e | Bitmap fonts | `gfx/font.c` |
| 0x4d08e0-0x4d13fc | Primitives (`gr_*`) and rectangle helpers | `gfx/draw.c` |
| 0x4b8cd0-0x4b93fb | Mouse and cursor ("Ms") | `gfx/mouse.c` |
| 0x4d05e0-0x4d08d6 | Keyboard queue (`kb_*`) | `game/input.c`, see [startup.md](startup.md#input-and-timer) |

## Surface model

A surface (`di_create_surface` 0x4cafbb) is a 20-byte struct:

| Offset | Field |
|---|---|
| +0x00 | width |
| +0x04 | height |
| +0x08 | stride: width rounded up to 4 |
| +0x0c | bits: top-down 8-bit pixels (`biHeight = -height`) |
| +0x10 | HBITMAP of the DIB section |

The DIB's color table is a copy of the logical palette. Every blit refreshes
entries 10-245 from the shadow palette (`SetDIBColorTable`), so a surface is
effectively a plain buffer of palette indices. `di_destroy_surface`
(0x4cb244) frees it. A running total of surface bytes is kept at 0x672930.

**Rectangles** are four inclusive ints in the order `left, right, top,
bottom` (not the Win32 order). Helpers: `rect_normalize` 0x4d1066,
`rect_intersect` 0x4d12d3, `rect_contains` 0x4d1353, `rect_copy` 0x4d13aa,
`rect_set` 0x4d13fc. The same file has the intrusive doubly linked list
helpers the heap and window manager use (`list_append` 0x4d11c1,
`list_remove` 0x4d124d, `list_pop_head` 0x4d10d1, ...).

**A NULL surface means the main window.** Blits to or from NULL go through
`GetDC(app_hwnd)` with the palette selected. The window manager keeps a
surface per window and repaints dirty rectangles of the screen from it
(`wm` message 3 in 0x4c3331 calls `di_blit(NULL, ..., window->surface, ...)`).
WM_PAINT is forwarded to a single callback, `di_set_paint_handler` 0x4cb3fa
(the window manager installs 0x4c568f, which invalidates the region).

In the port, "the window" is the platform's 8-bit framebuffer, wrapped as
`gfx_screen()`. Drawing to it accumulates a dirty rectangle, and
`gfx_present()` hands the framebuffer to SDL when something changed. The
platform keeps the framebuffer contents, so WM_PAINT is only needed after a
mode change (`gfx_repaint`).

## Initialization and geometry

`di_initialize` (0x4ca1c0) runs third in `sys_initialize` (after the heap and
the resource files):

1. Reads INI `Color depth`. Unless it is `DONT CARE`, a mismatch with the
   desktop's bits per pixel shows "Afterlife is designed to run using a color
   depth of %d colors", with the option to quit.
2. Saves the system palette (0x4cbedb) so `di_shutdown` can restore it.
3. Desktop client size -> 0x64e390 / 0x64e394.
4. Display flags (0x545428) = 1. INI `Force display settings` = `ON` sets
   bit 2.
5. INI `Resolution`: `MAX` or missing means the desktop size, otherwise
   `%dx%d`. Clamped to the desktop.
6. Low memory warnings (under 10 MB free, or under 7 MB physical).
7. Without "force", a resolution smaller than the desktop sets the flags to
   2 (framed window). With "force", the resolution must be one that
   `EnumDisplaySettings` lists at the current depth (0x4cd58f, up to 20
   modes), or startup fails with "The resolution you have selected...".
8. INI `Window name`, identity palette (0x4cb67a), clear the system palette
   (0x4cb54c), create the main window at the resolution (0x4cd73a: popup
   when it fills the desktop, framed and centered otherwise). The client size
   is the screen size, 0x64dc50 / 0x64dc54. Two memory DCs for blitting.
9. Mouse clip = the screen. Color distance tables (below).

`di_change_display_mode` (0x4cd448, `ChangeDisplaySettingsA`) is only used
when the flags are exactly 4, which step 4 never produces (forcing gives
5). So the game always runs in a window the size of the resolution. The port
maps "force" to fullscreen.

**Port display** (`platform_sdl.c`): sizes are in pixels.
`plat_video_desktop_size` is the desktop mode times its pixel density (a
DPI-unaware Win95 program sees the whole screen in pixels; SDL's mode
size is in points, 1707x1067 for a 2560x1600 panel at 150 %), and the
window is `SDL_WINDOW_HIGH_PIXEL_DENSITY`, so the framebuffer maps 1:1 (or
k:1 with UI scale k) to screen pixels. The game itself runs in the **port
window** (below): a normal resizable window whose size decides the
screen, whatever the INI's `Resolution` says. The presentation is
integer-scaled and centred while the window is at least the framebuffer's
size, letterboxed only when it is smaller (`platform/present.c`, which
also maps mouse positions back). With the enhancements on, a UI scale k
makes the game see a desktop k times smaller, so the framebuffer is
presented k times larger (see [enhancements.md](enhancements.md)).
`plat_video_set_mode` without the port window (the tests that call
`gfx_init` directly) keeps the older behaviour: a window of the
resolution, borderless fullscreen when it fills the desktop.

### Port window

Port behaviour, on every run regardless of the enhancements switch
(`plat_window_*` in `platform_sdl.c`, `game/display.c`):

- **Opening** (`di_initialize`, `plat_window_open`): the size and place
  saved in ALIFEPORT.INI if there are any (clamped to the usable desktop;
  a position off every display is dropped); else the INI `Resolution`
  times the UI scale if it is smaller than the desktop and fits with room
  for the frame; else 80 % of the usable desktop, centred. `Maximized` and
  `Fullscreen` from the file are restored, and `Force display settings =
  ON` starts it fullscreen. The minimum window size is 640x480 pixels.
- **The screen follows the window**: the framebuffer is (window pixels /
  UI scale), with the scale lowered while the window is smaller than
  640x480 times it (`present_logical_size`), and never smaller than
  640x480 (a smaller window letterboxes it). `plat_video_target_size`
  says what the window wants, `plat_video_resize` reallocates the
  framebuffer.
- **Live relayout** (`display_relayout`): SDL's pixel-size event becomes
  `PLAT_EV_RESIZE`; `main_wnd_proc` marks a relayout pending and
  `sys_pump` runs it at most every 100 ms (`display_idle`), so dragging
  the window edge relays out about ten times a second and the
  presentation scales the old frame in between. A relayout resizes the
  screen (`gfx_screen_resize`: cleared, mouse clip = screen), the window
  manager's root (`wm_screen_resized`, 0x4c6096), then the shell's part
  (`display_hooks.relayout`, `game/shell.c`): the main window gets a
  surface of the new size (`wm_resize_surface`), its menu bar is laid out
  again at the new width with its close / minimize buttons at the right
  (`menu_bar_resize`, `wm_menubar_update_buttons`: shown when the screen
  fills the desktop, as the original does for its popup window), its
  scrollbars stay on the right / bottom edges with new track surfaces, its
  client rectangle follows; in the game the map keeps the point at its
  centre (`shell_view_resized`) and the control panel and views are moved
  back inside the client area (`wm_fit_into`); at the title the menu
  background is centred again and the buttons are placed along the new
  edges with 0x428d28's formulas (`title_relayout`). Finally every other
  top-level window (message boxes, news, dialogs) is moved inside the
  screen and the whole screen repainted. The relayout waits while the
  title's intro and animation run (their FLC targets point into the main
  window's surface, `title_holds_surface`) and while the loading screen
  is up.
- **Fullscreen**: Alt+Enter or F11 toggles borderless desktop fullscreen
  at native pixels (`plat_window_set_fullscreen`), with the same
  relayout. The original's keyboard handling (game_view_proc 0x404023)
  uses F1-F10 only, so F11 was free. The platform swallows the key, its
  release and, after Alt+Enter, Alt's release (which would open the menu
  bar), so none of it reaches the game; Alt's press does, as before.
  Alt and Ctrl are tracked from the key events (and SDL's keyboard
  state), not the event's modifier bits, which synthetic events may leave
  as garbage.
- **Remembered** in ALIFEPORT.INI at exit (`atexit`, however the game
  ends): `Window size` and `Window position` (the last windowed geometry,
  desktop units), `Maximized`, `Fullscreen`. Command line:
  `--window-size=WxH`, `--fullscreen`, `--windowed`.
- `--window-test=DIR` (`src/main.c`) checks a real window: it resizes it
  through SDL, changes the UI scale, toggles fullscreen with injected key
  events and saves a screenshot of the window after each step
  (`plat_video_screenshot`), answering message boxes with Enter. Run it on
  a headless compositor (`weston --backend=headless` with its own
  `WAYLAND_DISPLAY`, `DISPLAY` unset) so nothing appears on a desktop.
- **macOS**: Ctrl+Cmd+F toggles fullscreen too (F11 shows the desktop
  there), and Cmd+Q / the app menu's Quit ask like the close button
  (`PLAT_EV_CLOSE`) instead of ending at once like SIGTERM does elsewhere.
  Sizes work the same on Retina screens: SDL's window coordinates are
  points, the desktop and window are measured in pixels
  (`pixel_density`), so the faithful mode draws at native pixels and the
  UI scale (enhancements) is what makes it larger.
- **Web** ([web.md](web.md)): the page's CSS sizes the canvas to the
  browser window (times `devicePixelRatio`), so `plat_window_open` and
  `plat_window_resize` leave the size alone and nothing is restored from
  ALIFEPORT.INI; resizing the browser window is a window resize.
  Fullscreen is the canvas going fullscreen (Alt+Enter or F11).

The original can't do any of this: its main window is created once at the
resolution (0x4cd73a), WM_WINDOWPOSCHANGING adds SWP_NOSIZE, and on
WM_DISPLAYCHANGE it re-creates the window and broadcasts message 0x200,
which none of ALIFE.EXE's window procedures handles.

The title's 640x480 art is never scaled and the display mode never changes
(`di_change_display_mode` is only reached with flags 4): the menu
background is centred on black (`blit_centred` 0x42a5f3), the title FLCs
are centred (`flc_center` 0x40b134), and the 320x200 intro is tiled over
the whole window, each copy five frames behind the previous one
(0x4265dc).

Port: `gfx_parse_resolution(ini_value, &w, &h)` (step 5) and
`gfx_init(w, h, force)` (the rest, minus the INI and memory checks).

## Palette

The game runs on a 256-color desktop with an identity palette. Entries 0-9
and 246-255 are the Windows static colors; the game owns 10-245
(`GFX_PAL_FIRST..GFX_PAL_LAST`), all PC_RESERVED. The shadow palette at
0x545c38 holds 256 RGBQUAD-order entries (B, G, R, flags). Flags: 1 =
PC_RESERVED, 2 = in a cycling range.

Palette buffers passed between game functions are 256 x {R, G, B, x}.

| Address | Name | What |
|---|---|---|
| 0x4cb67a | `di_create_identity_palette` | game range starts as a grey ramp (i, i, i) |
| 0x4cacc7 | `di_set_palette(rgbx, first, count)` | clamps to 10-245, copies into the shadow palette (flags = 1), `AnimatePalette` + `RealizePalette`. `rgbx` NULL re-pushes the shadow palette (WM_ACTIVATE) |
| 0x4caece | `di_get_palette(rgbx, first, count)` | from the logical palette |
| 0x4cb433 | `di_closest_color(r, g, b)` | over 10-244 (the bound is `< 0xf5`, so 245 is never matched), skipping cycling entries; exact match wins, otherwise least 30 dR^2 + 60 dG^2 + 20 dB^2 (tables 0x545838 / 0x5468c0 / 0x545430) |
| 0x4cc3d4 | `di_rotate_palette(first, count, dir)` | rotates one step and re-pushes |
| 0x4ced05 | `pal_load_resource` | palette resource: 256 B,G,R,x + 15 cycling records |
| 0x4cec6d | `pal_from_rgb` | 768-byte RGB to R,G,B,x |
| 0x4ceee9 | `pal_build_shade_table` | 13 remap tables at 0x64cf10 (used by the tile/sprite renderer at 0x4e6a6a) |
| 0x4cf2a2 | `pal_closest_in_range` | like the above, over a caller's table, 6-bit precision |
| 0x4c7d79 | `pal_build_highlight_remap` | 0x535318[i] = closest to entry i brightened by 20%; hover highlighting |

The game's palette is resource 33. The game's main (0x401000) loads it with
`pal_load_resource` and sets 10-245 from it.

`di_rotate_palette` with `dir < 0` starts at entry 0 instead of `first`.
In the original it rotates the wrong entries and then pushes the unchanged
range, so it visibly does nothing. The port rotates the requested range.

### Cycling

There are 100 ranges at 0x5479c8, 20 bytes each: first, count, rate (signed:
period in ticks, sign = direction), countdown, enabled. `pcyc_idle` (0x4cf108,
a system task) runs at most every 100 ms while 0x673a00 is set. For each
range with a count, it decrements the countdown, and at zero reloads it and
rotates the range.

`pal_load_resource` fills ranges 0-14 from the resource's records (8 bytes:
+2 speed, +6 first, +7 last). Speed s gives a period of max(1, 1024 / (17 s))
ticks; s = 0 leaves the range empty. It also flags the entries as cycling.
`di_set_palette` resets the flags to 1, so color matching in practice never
skips them. Other entry points: `pcyc_set_range` 0x4cec02 (accepts slot 100,
one past the table), `pcyc_stop_all` 0x4cf042 / `pcyc_start_all` 0x4cf089
(the FLC player brackets its palette changes with these),
`pcyc_disable` / `pcyc_enable` 0x4cf0d0 / 0x4cf0ec.

## Blit path

| Address | Name | What |
|---|---|---|
| 0x4ca737 | `di_blit(dst, dx, dy, w, h, src, sx, sy)` | BitBlt SRCCOPY. NULL = window. About 100 call sites |
| 0x4ca905 | `di_stretch_blit` | StretchBlt (no callers) |
| 0x4caa9c | `di_tile_blit(..., tw, th)` | repeats a tw x th source rectangle over the destination with BitBlt |
| 0x4cb290 | `di_scroll_rect(surface, rect, dx, dy)` | ScrollDC within `rect`; returns the uncovered rectangle (0x546490) |
| 0x4cc827 | `di_blit_transparent(..., key)` | memory-only copy skipping `key` pixels (sprites). No clipping |
| 0x4cc906 | `di_tile_fill(..., tw, th)` | memory-only tiled fill (window backgrounds). No clipping |
| 0x4ccb2c | `di_tile_fill_phase` | tiled fill with a phase offset, one caller (0x4ed550). Not ported: as decompiled it copies only `tw` columns per row and reads left of the tile |

The port clips all of them against both surfaces, which gives the same
result for calls that stay inside.

Screen-only GDI helpers, none of which the game calls: `di_screen_fill_rect`
0x4cbfb5, `di_screen_fill_black` 0x4cb38b, `di_screen_fill_white` 0x4cc082,
`di_screen_xor_rect_white` / `_black` 0x4cc162 / 0x4cc251,
`di_screen_xor_frame` 0x4cc5a4, `di_screen_line` 0x4cc340. They're named but
not ported.

`di_save_screenshot` (0x4ccf6e) copies the screen into a surface and writes
a BMP chosen with `GetSaveFileNameA`. Not ported yet.

### Dirty rectangles

The Di layer has none. The window manager tracks invalidated regions as
rectangle lists (0x4c50ae builds the list of visible window pieces
intersecting a rectangle, 0x4c476a repaints them, 0x4c4b9f frees the list)
and repaints by sending each window message 3, which blits from the window's
surface. That belongs to the window-manager port. The port's `gfx_present`
just presents the whole framebuffer when anything changed.

## Primitives

All take a surface (the port also accepts NULL for the screen). Colors are
palette indices.

| Address | Name | What |
|---|---|---|
| 0x4d08e0 | `gr_fill_rect(s, x, y, w, h, c)` | clipped solid fill. Loops forever if the rectangle clips to nothing; the port returns |
| 0x4d09bb | `gr_frame_rect(s, rect*, c)` | normalizes and clamps the caller's rectangle in place, then draws a 1-pixel outline. Clamps right/bottom to width/height (one past the edge), and only when left/top didn't need clamping; the port clamps both edges inside |
| 0x4d0b34 | `gr_dotted_frame(s, x, y, w, h, c, pattern)` | outline where bits 7..0 of `pattern` pick pixels; top+bottom, then left+right |
| 0x4d0cd1 | `gr_line(s, x0, x1, y0, y1, c)` | note the argument order. DDA over n = max(\|dx\|, \|dy\|) with inclusive extents; n + 1 plots (the first pixel twice), each clipped |
| 0x4d0eb8 | `gr_bevel_rect(s, rect, light, fill, dark, sunken)` | fill + four lines: light top/left, dark bottom/right (swapped when sunken) |

## Fonts

There are 10 slots: IDs at 0x5485a8, data at 0x5485d0, current data at
0x5485a4, current ID at 0x5485f8. `font_initialize` (0x4cf400) puts the
built-in font, ID 0xf0000001, linked into the EXE at 0x673a10, in slot 0
and selects it. Other IDs are loaded from the bundle on first use
(`font_slot` 0x4d056e calls `font_load` 0x4cf456, which calls
`rc_load_resource` 0x4cdcbf). The game preloads 500 and 501. The port reads
the built-in font from the user's ALIFE.EXE at runtime (`gfx/exe_res.c`).

Format (offsets from the start of the data):

| Offset | Field |
|---|---|
| +0x06 | colormap: palette index per pixel value; value 0 is transparent |
| +0x15 | bits per pixel |
| +0x16 | line height |
| +0x19 | 256 x u32 glyph offsets, relative to +0x15 |

A glyph is width, height, x offset (s8), y offset (s8), then its pixels,
MSB first, rows packed with no padding. This looks like the SCUMM "CHAR"
charset layout.

| Address | Name | What |
|---|---|---|
| 0x4cf504 | `font_select(id)` | returns the previous ID (0 if `id` fails, which can't be told apart from "no previous font") |
| 0x4d0102 | `font_set_colors(id, first, count, ...)` | varargs colormap entries; `first + count < 15` |
| 0x4d01d3 / 0x4d02a8 | get / set colormap | limit `1 << (bpp - 1)` entries |
| 0x4d0361 | `font_char_width` | glyph width; control codes 1, tab 8 |
| 0x4d041f | `font_char_height` | |
| 0x4d045e | `font_text_width` | sum of x offset + width; tab 8, `&` 0 |
| 0x4d050d | `font_height(id)` | line height |
| 0x4cf56e | `font_draw_char(win, ch, x, y, clip)` | clipped glyph into `win->surface`; returns x offset + drawn width (smaller when clipped) |
| 0x4cf9f0 | `font_draw_text(win, x, y, clip, area, str, flags)` | see below |
| 0x4cff0e | `font_draw_wrapped(win, rect, str, font, c1, c2)` | word wrap at spaces into `rect`, colormap 1/2 = c1/c2 |

`font_draw_text` measures each line up to a newline (`\n`, `\r`, or a pair),
the clip's right edge, or the end. When more text follows, it breaks at the
last space and justifies: each space advances
`space_width + (clip_width - pos_of_last_space) / (spaces - 1)`, unless flag
0x10 is set. Flag 1 centers (x = clip center, each glyph shifts the line
start left by half its advance). Flag 0x20 stores the final y + 1 in
`area->bottom`. `&` underlines the next glyph in colormap entry 1. Tab
measures 9 but draws 8. Each line advances by line height + 1. The original
loops forever when a line makes no progress (text starting at the clip's
right edge), and `font_draw_wrapped` does the same on a word wider than the
rectangle; the port stops or breaks the word.

## Mouse and cursor

The cursor is the hardware cursor. `ms_load_cursor` (0x4b9125) is
`LoadCursorA(hInstance, id)` on ALIFE.EXE's own cursor resources (the game
loads 0x7c, 0x7d (default), 0x83, 0x84 and 0x65 + n), and `ms_set_cursor`
(0x4b928a) stores it in 0x67254c and calls `SetCursor`. WM_SETCURSOR
re-applies it, and windows carry their own cursor at +0xa9 (0x4c619d).
`ms_show_cursor` (0x4b9179) forces the ShowCursor count to visible or
hidden. Nothing is drawn into the surfaces. The port decodes the cursor
resources from the user's ALIFE.EXE into SDL color cursors
(`plat_cursor_create`).

Mouse state: position 0x522fb0/b4, buttons 0x522fac (1 left, 2 right,
4 middle), clip rectangle 0x523138 (saved copy at 0x522f98). Each button
transition queues {x, y, buttons} in a 32-entry ring (0x522fb8, read/write
indices 0x672548/0x672544). `main_wnd_proc` (0x4cb7ad) feeds it:
WM_MOUSEMOVE goes to `ms_on_move` (clamps to the clip), and button messages
go to `ms_on_*_down/up` (0x4b901d-0x4b90f9) with SetCapture/ReleaseCapture.
`ms_get_state` (0x4b8d5e) returns the oldest queued event, or the current
state, clamped to the clip. `ms_set_clip_rect` (0x4b8e3f) clamps to the
screen and saves the old rectangle. `ms_set_pos` (0x4b92af) warps the
pointer. `ms_set_handler` (0x4b8f57) stores a callback (the window manager
installs 0x4c523a, which turns events 1-8 into window messages), but nothing
in the mouse module calls it.

## Platform API

```c
bool plat_video_set_mode(int w, int h, bool fullscreen); /* framebuffer + window */
void plat_window_open(const PlatWindowState *saved, int pref_w, int pref_h); /* the port window */
bool plat_video_target_size(int *w, int *h);  /* the framebuffer the window wants */
bool plat_video_resize(int w, int h);         /* reallocate it, window untouched */
void plat_window_set_fullscreen(bool on);
void plat_video_desktop_size(int *w, int *h);
void plat_video_set_palette(const PlatColor *, int first, int count);
void plat_video_present(void);
PlatCursor *plat_cursor_create(const uint8_t *rgba, int w, int h, int hx, int hy);
void plat_cursor_set(PlatCursor *);   /* NULL = system arrow */
void plat_cursor_show(bool);
void plat_cursor_destroy(PlatCursor *);
void plat_mouse_warp(int x, int y);
```

## Verification

```
build/gfx_test orig MEDIUM.000 out.ppm [bitmap id] [--window]
```

This loads palette resource 33 and bitmap 1210 through
`gfx_load_bmp_resource` (0x4ce09f), blits it full size, tile-fills and
key-blits a piece of it, draws bevels, a dotted frame and lines, renders
text with the built-in font (with an `&` underline), font 500 and wrapped
font 501, and writes the framebuffer through the palette. It also runs
pixel-level self-checks on clipping, lines, frames, keyed and tiled blits,
scrolling, palette clamping, matching and rotation, and loads cursor 0x7d
from ALIFE.EXE.

`tests/display_test` runs the game headless in the port window and resizes
it through SDL at the title and in the game, at UI scales 1-4 and below
the minimum, checking the screen size, the main window, its menu bar and
scrollbars, that every window stays inside and that the map keeps its
centre; it also checks the fullscreen keys and the Port Options window
(PNGs in `test-out/display`).
