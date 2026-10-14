# UI: window manager and controls

The game's whole interface (main view, panels, dialogs, buttons, scrollbars)
is built on its own window manager, inside the single Win32 main window. It
sits on top of the video core ([rendering.md](rendering.md)).

| Range | Module | Port |
|---|---|---|
| 0x4c1580-0x4c62cc | Window manager (`wm_*`) | `src/ui/wm.c` |
| 0x4c4a61-0x4c50ae | Rectangle lists ("regions"), part of the WM | `src/ui/region.c` |
| 0x4ba100-0x4c1568 | Controls (`ctl_*`): button, scrollbar, text field, list box, message box | `src/ui/controls.c` (button and task only) |
| 0x4c6bf9-0x4c95ca | Menus (menu bar, pop-ups; 0x6727cc = open menu) | not ported, hooked |

Tests: `build/ui_region_test` (no game data) and
`build/ui_test orig MEDIUM.000 out.ppm [--window]` (see [Verification](#verification)).

## Model

- Every window has a rectangle in its **parent's** coordinates and,
  unless `WM_F_NOSURFACE`, its own 8-bit surface of that size. Game code
  draws into `w->surface` and then calls `wm_redraw(w, rect)` to put it on
  screen. The surface is not resized when the window is (moving or
  resizing only changes the rectangle).
- Windows form a tree under a screen-sized **root** (`wm_root`, 0x672704)
  created by `wm_initialize`. The root has no surface and paints nothing:
  the game covers the screen with its main view.
- Siblings are a doubly linked list whose **head is in front**. New windows
  are appended (at the back) and are hidden until `wm_show`.
- **z-order**: `wm_update_zorder` (0x4c5c2b) numbers the tree front to back,
  each window's descendants before the window itself (so a subtree is
  always contiguous and in front of its root). Lower `z` = in front. It is
  recomputed after every stacking change.
- **Stacking groups** among siblings, front to back: minimized windows,
  then `WM_F_TOPMOST`, then normal ones. `wm_raise` (0x4c2248) moves a
  window to the front of its group, `wm_lower` (0x4c211d) to the back.
- Controls (`WM_F_NOCLIP`) don't occlude anything and draw into their
  parent's surface at their position.

### Window structure

Allocated as `0xad + extra` bytes (0x4c47ee), zeroed.

| Offset | Field | Notes |
|---|---|---|
| +0x00 / +0x04 | next / prev | sibling list |
| +0x08 | parent | |
| +0x0c | children | list head (front) |
| +0x10 | surface | |
| +0x14 | rect | left, right, top, bottom in parent coordinates |
| +0x24 | client | local; inset by the frame (`WM_F_FRAMED`), shrunk by scrollbars and the menu bar |
| +0x34 | z | |
| +0x38 | flags | below |
| +0x3c | extra size | |
| +0x40 | proc | `int proc(target, msg, a, b)` |
| +0x44 | invalid list | region nodes from `wm_invalidate` |
| +0x48 | (bar above the menu bar) | read by the menu bar layout |
| +0x4c / +0x50 / +0x54 | menu bar / vertical / horizontal scrollbar | |
| +0x58 / +0x5c | title buffer size / title | 256 bytes unless `WM_F_NOTITLE`; a control's label |
| +0x64 | icon height | 14 |
| +0x68 | minimize button | (on a menu bar: its minimize button) |
| +0x6c | rect before minimizing | |
| +0x7c | minimize button rect | local |
| +0x8c | title rect | where `wm_draw_title` draws: x 20.., y 4..11 |
| +0x9c | surface under the minimize button | |
| +0xa0 | minimized (byte) | |
| +0xa1 | flags before minimizing | |
| +0xa5 | close button | menu bars only |
| +0xa9 | cursor | `ms_set_cursor` when the mouse enters |
| +0xad | extra data | `wm_get_data` / `wm_set_data` (0x4c5a47 / 0x4c5a00) by byte offset |

### Flags

| Flag | Name | Meaning |
|---|---|---|
| 0x1 | EXISTS | always set |
| 0x2 | VISIBLE | `wm_show`; effective visibility needs all ancestors visible (0x4c59a2) |
| 0x4 | TOPMOST | stacking group |
| 0x8 | NOSURFACE | root, controls |
| 0x200 | CENTER | creation rect centred in the parent |
| 0x400 | NOFOCUS | activation doesn't take the keyboard focus |
| 0x800 | NOCLIP | ignored by region building (doesn't occlude); controls draw into the parent |
| 0x1000 | NOTITLE | no title buffer |
| 0x2000 | (controls) | set on buttons; not read by the WM |
| 0x8000 | NORAISE | activation doesn't raise |
| 0x10000 / 0x20000 | VSCROLL / HSCROLL | create scrollbars (0x4bb012) |
| 0x40000 | MENUBAR | create a menu bar (0x4c6e66) with close (bitmap 0x230) and minimize (0x231) buttons |
| 0x80000 | MINIMIZED | while minimized |
| 0x100000 | HITCHILDREN | while it has the capture, its children are hit-tested too |
| 0x200000 | FRAMED | client = rect inset by the frame pieces (`wm_set_frame`, 0x4c5f19, never called) |
| 0x400000 | CLIPMOUSE | a captured child clips the mouse to this window's client rect |
| 0x800000 | MINIMIZABLE | minimize button (bitmap 0x228) at the top right |
| 0x2000000 | DRAGGABLE | left-drag anywhere moves the window |

## Messages

`wm_send(w, target, msg, a, b)` (0x4c2830) calls `w->proc(target, msg, a,
b)`; usually `target == w`. Controls notify their parent with `target` =
the control. The root only accepts 0x406; `wm_send(NULL, ...)` broadcasts
0x200. Procs pass anything they don't handle to `wm_default_proc`
(0x4c3331), which the game's procs do (37 call sites). `wm_post` (0x4c5d03)
queues a message in a 1000-entry ring (0x530328); `wm_idle` dispatches one
per call.

| Msg | Meaning | Default handling |
|---|---|---|
| 0x01 | create | minimizable: load bitmap 0x228, save the surface under the button, create the toggle button (id 1); set the title rect; return 1 (anything else aborts creation) |
| 0x02 | destroy | destroy children, unlink, renumber, free (surface, title). No repaint |
| 0x03 | paint, a = local rect or 0 | blit that part of the surface to the screen |
| 0x06 | activate (a = 1) / deactivate | deactivate the old focus, raise (unless NORAISE), take focus and flush input (unless NOFOCUS) |
| 0x08 | mouse move (local) | drag a DRAGGABLE window |
| 0x09-0x0b / 0x0c-0x0e | left/right/middle down / up | left down starts a drag (capture), left up ends it |
| 0x0f | set rect (a = old, b = new) | store, repaint the old and new areas |
| 0x10 | control pressed (a = id, b = pressed) | from the minimize button: minimize/restore, lower when minimized |
| 0x11 / 0x15 | control right down / up | |
| 0x14 | control clicked (a = id) | |
| 0x1b / 0x1c | scrollbar moved (a = position, b = delta) | |
| 0x1e / 0x1f / 0x20 | char / key down / key up to the focus window | Alt (0x12) up without Tab: open the menu bar |
| 0x28 | minimized state (a) | |
| 0x29 | hover (mouse still 1.5 s) | |
| 0x2a | control hover | |
| 0x2b-0x2d | double click (release within 500 ms and 3 px) | |
| 0x40 | mouse entered (a = 1) / left | |
| 0x45 | shown (a) / hidden | minimize button redraw; hiding an icon closes the gap |
| 0x47 | capture lost (a = new capture) | |
| 0x200 | screen resized (a, b) | |
| 0x208 | before create (a = rect, editable) | |
| 0x400 | release resources | controls free their bitmap |
| 0x401 | menu bar close button | |
| 0x406 | quit request (main window's WM_CLOSE, sent to the root) | forwarded to the children |
| 0x1001 | menu bar layout | |

## Painting and dirty rectangles

There is no back buffer and no dirty bitmap. To repaint a screen rectangle
the WM builds a **rectangle list** (`rgn_build_visible`, 0x4c50ae):

1. Start with one node for the rectangle, owned by the root (highest z).
2. Walk the tree (children before their parent). For each visible window
   that isn't NOCLIP, for each node it overlaps whose owner is behind it
   (`w->z < owner->z`), split the node (`rgn_split`, 0x4c4d9a) into the
   parts outside the overlap (full-width band above, left part, right
   part, full-width band below, kept by the old owner and appended to the
   list) and the overlap, which becomes the window's.
3. `rgn_paint` (0x4c476a) sends each owner message 3 with its piece in
   local coordinates; `rgn_free` (0x4c4b9f) returns the nodes.

Nodes come from a pool of 1000 (0x5247a8, free list 0x53516c); when it runs
out, pieces are silently dropped (in the port too).

Worked example (checked in `ui_region_test`): screen 100x100, window A
{10..49, 10..49} behind B {30..69, 30..69}. Rebuilding the whole screen
gives 10 pieces in this order: B {30..69, 30..69}; A {10..49, 10..29},
A {10..29, 30..49}; root {70..99, 30..69}, {0..99, 70..99}, {0..99, 0..9},
{0..9, 10..29}, {50..99, 10..29}, {0..9, 30..49}, {0..29, 50..69}.

Entry points: `wm_redraw(w, local rect | NULL)` (0x4c25c4, 148 call sites)
repaints now; `wm_invalidate` (0x4c2690) + `wm_update` (0x4c2733) collect
rectangles and repaint them later; `wm_repaint_screen` (0x4c32f2) is the
WM_PAINT path (`di_set_paint_handler(0x4c568f)`).

In the port, message 3 blits to `gfx_screen()`, which marks the screen
dirty; whoever runs the frame calls `gfx_present()`.

## Input

`wm_initialize` installs `ms_set_handler(0x4c523a)`, `kb_set_handler(0x4c56b5)`
and the paint handler, but the mouse and keyboard modules never call their
handlers: input is polled by `wm_idle` (0x4c178f), a system task:

- `ms_get_state`: position changes become event 1 (move), button changes
  events 2-4 (down) / 5-7 (up); the mouse resting within a pixel for 1.5 s
  gives event 8 (hover), once.
- `kb_flush_one`: one keyboard record {type 1 char / 2 down / 3 up, key,
  char} goes to the focus window as 0x1e/0x1f/0x20. Shift is tracked
  (0x674330).
- While `wm_input_live` (0x672750) is 0 (attract mode), a click or Esc
  calls 0x4ac69c(0), which stops it and sets the flag. Note that the flag
  starts at 0.
- One posted message is dispatched.

The mouse handler (0x4c523a, also `wm_mouse_event` 0x4c617a) hit-tests
(`wm_window_at`, 0x4c2cf7: the capture if set, otherwise the front-most
visible window containing the point; minimized windows only through their
children), applies the window's cursor, sends 0x40 enter/leave, converts to
local coordinates and sends the message (left down also activates). A left
release outside an open menu goes to the menu's owner.

## API as used by the game

Most used (call sites): `wm_get_data` 199, `wm_send` 294, `wm_show` 151,
`wm_redraw` 148, `wm_set_data` 130, `wm_raise` 57, `wm_is_visible` 53,
`wm_create_window` 39, `wm_update` 38, `wm_default_proc` 37, `wm_post` 37,
`wm_set_capture` 35 / `wm_release_capture` 34, `wm_set_title` 31,
`wm_is_minimized` 28, `wm_activate` 27, `wm_local_to_screen` 25,
`wm_set_rect` 23, `wm_destroy` 20. See `src/ui/wm.h` for the full list with
addresses. Never called: 0x4c2b88 (scroll a window's screen area),
0x4c2fb3, 0x4c4683, 0x4c46bd, 0x4c4743, 0x4c4bdb (an alternative z
numbering with topmost windows first), 0x4c5017, 0x4c5856, 0x4c5896,
0x4c5acf, 0x4c5af7, 0x4c5b27, 0x4c5f19, 0x4c606e (so `wm_get_mode` is
always 0), 0x4c62cc.

## Minimizing

A MINIMIZABLE window gets a toggle button (bitmap 0x228, two frames) at its
top right, drawn into its own surface; the pixels under the button are
saved in +0x9c. Pressing it (message 0x10 to the window's default proc)
calls `wm_set_minimized` (0x4c3f4d) / `wm_toggle_minimized` (0x4c3bac):
the window shrinks to an icon (title width + half the button width + 25,
by 14), its button moves to the icon's right edge, and the icon is placed
at the first free slot in rows from the parent's client top-left
(0x4c414f). Restoring brings back the saved rectangle and closes the gap in
the icon rows (0x4c426a, collecting icons after it sorted by row and
column, 0x4c4346).

## Controls

Every control is a child window with extra data; offset 0 is the id and 8
the state (4 pressed, 8 disabled, 0x10 focus frame) for all of them
(`ctl_get_id` 0x4bc5c1, which Ghidra had matched as `__mkgmtime`;
`ctl_is_pressed` 0x4bc5df).

`ctl_initialize` (0x4ba100) registers a task: suspend/resume save the
game's pause flag (0x5fa61c) and set it; `ctl_idle` (0x4bf68a) closes a
timed message box, auto-repeats a held scroll arrow every 100 ms, and
blinks the text cursor every 250 ms.

**Button** (ported): `ctl_button_create` (0x4ba2eb, 73 call sites) makes a
NOSURFACE | NOFOCUS | NOCLIP window with 0x18 bytes of extra data (id,
style, state, bitmap, font, label colors). The bitmap holds the normal and
pressed frames side by side and is owned by the button. Styles: 4 bitmap
(required), 0x20 sticky, 0x80 toggle. Left down captures, presses (or
toggles), redraws and posts 0x10; left up releases and posts 0x14 (ids
0x230/0x231 post close/minimize to the menu bar's owner). The label is
centred, clipped 7 px from the right edge, shifted a pixel when pressed,
and drawn with colormap 0-3 from the packed colors (grey 0x10 when
disabled). `ctl_button_set_pressed` (0x4ba7e9) has 231 call sites.

Mapped, not ported:

| Range | What |
|---|---|
| 0x4baca2 / 0x4bad6d | scrollbar part bitmaps (vertical / horizontal: two arrows, thumb, track) |
| 0x4bae38 | re-skin a scrollbar |
| 0x4bb012 | scrollbar create (0x4bb095 vertical, 0x4bb48b horizontal): a track window (ids 0x7ffc) with arrows 0x7ffe/0x7fff and thumb 0x7ffd, proc 0x4bbb4a; shrinks the owner's client rect |
| 0x4bb878 / 0x4bba71 | scrollbar position / range (data 0x10 position, 0x14-0x18 range) |
| 0x4bc3f8, 0x4bc482, 0x4c110a | scrollbar parts, thumb clamping, layout |
| 0x4bc611-0x4bcb13 | single-line text field (proc 0x4bcb13, draw 0x4bc7cd, cursor 0x4bc9c0) |
| 0x4bcfbc-0x4be5d2 | multi-line text box (proc 0x4bd2c6) |
| 0x4be691-0x4bf5e3 | list box with a vertical scrollbar (proc 0x4be8d4, items 0x4befcb) |
| 0x4bf8d8 / 0x4bf959 | create / destroy a plain child window |
| 0x4bf970 | pop-up text box on bitmap 0x260 (proc 0x4bfc10) |
| 0x4bfc69 | message box (89 call sites; window 0x6726b0, bitmaps 0x260/0x227, font 500, proc 0x4c06d7, created by 0x4c0d2e) |

## Port notes

- Messages carry `intptr_t` arguments and extra data is stored in
  `intptr_t` slots (one per 4 bytes of the original offset), so pointers
  fit on 64-bit.
- Modules that aren't ported yet are reached through `wm_hooks`
  (menu bar creation, Alt menu activation, scrollbar creation, attract mode
  stop, keyboard polling, sound idle) and `ctl_hooks` (text cursor). All
  may be NULL. Without `attract_stop`, the first input just sets
  `wm_input_live`.
- Fixed original bugs: `wm_center` and `wm_invalidate(w, NULL)` passed a
  parent-relative rectangle through local-to-screen (offset twice), so the
  wrong area was repainted; the message queue's pop could advance the write
  index after a wrap and replay stale entries; destroying a window left a
  dangling capture and leaked its invalid list; `wm_set_title_size`
  didn't update the size. `wm_shutdown` also frees the windows (the
  original only unhooks).
- Kept: the region pool limit, the menu bar's cached close/minimize
  bitmaps being freed by the buttons that use them, the empty "under the
  button" save at creation, the capture clip using the parent's client rect
  in local coordinates, message 0x0f painting new/old/new.

## Wiring it up

After `gfx_init` (and `gfx_mouse_init`, `gfx_font_init`):

```c
wm_hooks.key_poll = ...;             /* the keyboard queue (kb_flush_one) */
if (wm_initialize() != 0) fail;      /* sys_initialize step 11 */
sys_register_task(wm_initialize, wm_shutdown, wm_suspend, wm_resume, wm_idle);
ctl_initialize();                    /* step 12 */
sys_register_task(ctl_initialize, NULL, ctl_suspend, ctl_resume, ctl_idle);
ctl_set_pause_flag(&game_paused);
...
wm_set_input_live(true);             /* or call it from the attract mode code */
/* each frame: sys_pump -> task idles (wm_idle, ctl_idle) -> gfx_present() */
```

Shutdown order: `ctl_shutdown`, then `wm_shutdown`. The main window's
WM_CLOSE becomes `wm_send(wm_root(), wm_root(), WM_MSG_CLOSE, 0, 0)` and
WM_DISPLAYCHANGE `wm_screen_resized(w, h)`.

## Verification

`ui_region_test` checks intersection and splitting (hole in the middle, in
a corner, equal to the node, full height), subtraction, pool exhaustion, the
10-piece example above, hit testing, raise/lower and topmost groups, child
coordinates, paint rectangles in local coordinates, invalidate/update,
destroy, and one-message-per-idle dispatch.

`ui_test` puts four windows filled with bundle bitmaps (581, 608, 1210) over
a backdrop window, a bitmap button (560) in one and a minimize button in
another, then clicks, presses the button through the mouse path (its click
arrives via the queue), drags a window by mouse, draws and invalidates,
hides, raises and minimizes/restores, checking screen pixels against the
windows' surfaces after each step, and writes the screen as a PPM.
