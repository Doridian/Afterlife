# UI: window manager and controls

The game's whole interface (main view, panels, dialogs, buttons, scrollbars)
is built on its own window manager, inside the single Win32 main window. It
sits on top of the video core ([rendering.md](rendering.md)).

| Range | Module | Port |
|---|---|---|
| 0x4c1580-0x4c62cc | Window manager (`wm_*`) | `src/ui/wm.c` |
| 0x4c4a61-0x4c50ae | Rectangle lists ("regions"), part of the WM | `src/ui/region.c` |
| 0x4ba100-0x4c1568 | Controls (`ctl_*`): button, scrollbar, text field, list box, text viewer, prompt, message box | `src/ui/controls.c`, `scrollbar.c`, `textfield.c`, `listbox.c`, `msgbox.c` |
| 0x4c6bf9-0x4c97de | Menus (`menu_*`: menu bar, pop-ups, items; 0x6727cc = open menu) | `src/ui/menu.c` |

Tests: `build/ui_region_test` (no game data),
`build/ui_test orig MEDIUM.000 out.ppm [--window]` and
`build/ui_widgets_test orig MEDIUM.000 out-prefix [--window]` (see [Verification](#verification)).

## Model

- Every window has a rectangle in its **parent's** coordinates and,
  unless `WM_F_NOSURFACE`, its own 8-bit surface of that size. Game code
  draws into `w->surface` and then calls `wm_redraw(w, rect)` to put it on
  screen. The surface is not resized when the window is (moving or
  resizing only changes the rectangle). Port-only: `wm_resize_surface`
  gives a window a new surface (the port window's relayout uses it for the
  main window and its scrollbar tracks), `wm_fit_into` moves a window back
  inside an area, `menu_bar_resize` re-lays out a menu bar at a new width.
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
| 0x19 / 0x1a | scrollbar thumb dragged or track clicked (w = the scrollbar, a = position, b = old - new) | |
| 0x1b / 0x1c | scrollbar arrow (w = the scrollbar, a = position +/- 1, b = delta) | |
| 0x1e / 0x1f / 0x20 | char / key down / key up to the focus window | Alt (0x12) up without Tab: open the menu bar |
| 0x28 | minimized state (a) | |
| 0x29 | hover (mouse still 1.5 s) | |
| 0x2a | control hover | |
| 0x2b-0x2d | double click (release within 500 ms and 3 px) | |
| 0x40 | mouse entered (a = 1) / left | |
| 0x45 | shown (a) / hidden | minimize button redraw; hiding an icon closes the gap |
| 0x47 | capture lost (a = new capture) | |
| 0x200 | screen resized (a, b) | (no window procedure of ALIFE.EXE handles it) |
| 0x208 | before create (a = rect, editable) | |
| 0x400 | release resources | controls free their bitmap |
| 0x401 | menu bar close button | |
| 0x406 | quit request (main window's WM_CLOSE, sent to the root) | forwarded to the children |
| 0x1001 | menu: paint the frame (sent by the menu bar button update) | |
| 0x1006 | menu command, to the menu bar's owner (w = item, a = id) | |
| 0x8000+ | per-control requests and notifications (`controls.h`) | |

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
the state (4 pressed, 8 disabled, 0x10 focus frame) for buttons
(`ctl_get_id` 0x4bc5c1, which Ghidra had matched as `__mkgmtime`;
`ctl_is_pressed` 0x4bc5df). Controls report to their parent either by
posting (button, scrollbar: `wm_post(parent, control, msg, ...)`, so the
parent's proc sees `w` = the control) or by sending to the parent itself
(text field, list box: `wm_send(parent, parent, msg, control, ...)`).

`ctl_initialize` (0x4ba100) registers a task: suspend/resume save the
game's pause flag (0x5fa61c) and set it; `ctl_idle` (0x4bf68a) closes a
timed message box (result 1), auto-repeats a held scroll arrow every 100
ms after five ticks, reporting each position once, and blinks the text
cursor every 250 ms. Other helpers: `ctl_create_child` / `ctl_destroy_child`
(0x4bf8d8 / 0x4bf959, unused), `ctl_find_by_id` (0x4c1064, wrapper 0x4c10ea:
depth-first search for a window whose data 0 is the id; the tutorial uses
it to move the mouse onto a dialog's button, 0x4ac939, and 0x42634a fakes
a click on it through `wm_mouse_event`).

### Button (0x4ba18e-0x4ba90c)

`ctl_button_create` (0x4ba2eb, 73 call sites) makes a NOSURFACE | NOFOCUS
| NOCLIP window with 0x18 bytes of extra data (id, style, state, bitmap,
font, label colors). The bitmap holds the normal and pressed frames side by
side and is owned by the button. Styles: 4 bitmap (required), 0x20 sticky,
0x80 toggle. Left down captures, presses (or toggles), redraws and posts
0x10; left up releases and posts 0x14 (ids 0x230/0x231 post close/minimize
to the menu bar's owner). The label is centred, clipped 7 px from the right
edge, shifted a pixel when pressed, and drawn with colormap 0-3 from the
packed colors (grey 0x10 when disabled). `ctl_button_set_pressed`
(0x4ba7e9) has 231 call sites.

### Scrollbar (0x4baca2-0x4bc482, 0x4c110a)

`ctl_scroll_create(owner, rect, id, style)` (0x4bb012; style 1 = vertical,
0x20 = shown) builds a **track** window (flags NOFOCUS | NOTITLE | NORAISE
| 0x80000, so it stacks in the owner's front group; it has a surface)
with three NOCLIP **parts** drawn into the track's surface by
`ctl_scroll_layout` (0x4c110a): the forward arrow (kind 0x7ffe,
down/right, created first, so it heads the child list), the back arrow
(0x7fff) and the thumb (0x7ffd, centred across). Extra data (0x20 bytes,
the same layout on all four): 0 id, 4 kind (track 0x7ffc), 8 style, 0x0c
bitmap, 0x10 position, 0x14/0x18 range, 0x1c position last reported by the
auto-repeat. A NULL rect puts it along the owner's right/bottom edge (below
the menu bar, left of the vertical one) and shrinks the owner's client
rect; this is what `WM_F_VSCROLL` / `WM_F_HSCROLL` do (id 0).

The part bitmaps are global (`ctl_scroll_set_vbitmaps` /
`_hbitmaps`, 0x4baca2 / 0x4bad6d; game_main loads 0x4000 up, 0x4001 down,
0x4006 thumb, 0x4004 track and 0x4002 left, 0x4003 right, 0x4006, 0x4005).
Arrows and the thumb have normal and pressed frames side by side; the track
is tiled. Without bitmaps the parts are 24 px and nothing is drawn. The
horizontal thumb takes its height from the vertical thumb bitmap (the same
one in the game).

Input: an arrow captures, starts the auto-repeat (`ctl_repeat_arrow`,
0x524790) and posts 0x1c / 0x1b (position + 1 or - 1, delta); the thumb
captures, and while dragged every mouse move posts 0x1a / 0x19 with the
position under the grab point; a track click sends 0x1a / 0x19 for the
thumb centred on the click. The scrollbar never moves itself: the owner
calls `ctl_scroll_set_pos` (0x4bb878, clamps, places the thumb with
`ctl_scroll_clamp_thumb` 0x4bc482, redraws). `ctl_scroll_set_range`
(0x4bba71) hides the thumb for an empty range. Message 0x0f (the track
resized) moves the arrows to the new ends. 0x4bae38 (re-skin one bar) is
never called.

### Text field (0x4bc611-0x4bcb13)

`ctl_text_create(parent, rect, id, bg, fg1, fg2, cursor1, cursor2, font,
style)` (0x4bc611): a window with a surface and 0x2c bytes of data (0 id, 4
length, 8-0x10 colors, 0x14 cursor glyph `|`, 0x18/0x1c cursor colors, 0x20
style, 0x24 font, 0x28 cursor x). It has `WM_F_NOTITLE`; a 256-byte buffer
is installed as the title (255 characters). Styles: 1 right click sends
0x80c8 to the parent, 2 upper-case, 4 read-only (grey 0x24 / 0x13, no
cursor), 8 keeps the blinking cursor (0x6726c4) even without the focus.
Characters 0x20-0x7c except `@ { [ & \` = ] ^ _` are appended, backspace
deletes the last one, Enter sends 0x8000 (a = field, b = id) to the parent.
Requests: 0x8001 set text, 0x8002 draw, 0x8003 id, 0x8004 read-only.
Drawing (0x4bc7cd) shows the tail of the text that fits; the cursor
(0x4bc9c0) goes after it.

`ctl_prompt(x, y, title, prompt, default)` (0x4bf970, 4 call sites): a
modal window on bitmap 0x260 with a text field (id 1000, font 500); runs
`sys_pump` until Enter (its proc 0x4bfc10 sees 0x8000) and returns a static
copy of the text (0x64e3c0), or the default if it was left empty.

### List box (0x4bcfbc-0x4be5d2)

`ctl_list_create(parent, rect, style, user, scroll id, font, bg, fg)`
(0x4bcfbc, 3 call sites) keeps a 0x35e-byte block in data 0: up to 101
lines of 255 characters with an int of item data each, the top line,
selected line, count and number of visible rows (row height = font height
+ 2). The height is cut to whole rows unless style 8; a 26 px vertical
scrollbar runs along the right. Styles: 1 the line after the last can be
selected, 2 characters and key-ups go on to the parent, 4 no selection
bar, 8 partial last row. A click or a drag with the button down selects;
the scrollbar arrows and thumb move the selection; key-ups: PgUp/PgDn by a
page, Home, End, Up, Down. The selected row is drawn by swapping its
background and text colors (0x4be5d2). Notifications to the parent: 0x8064
selected (a = list, b = index), 0x8065 double click. Requests 0x8000-0x800e
(`CTL_LIST_*`): add a line, delete, set item data, scroll, select, row
rectangle, get selection / top / text / length / data / count / the
creator's value, set a whole text (word-wrapped into lines, 0x4be04f), clear.

### Text viewer (0x4be691-0x4bf5e3)

`ctl_view_create(parent, rect, style, id, font, bg, fg1, fg2, bufsize)`
(0x4be691, 3 call sites): read-only text with the window manager's
vertical scrollbar (`WM_F_VSCROLL`). 0x8000 sets the text (kept; freed by
the viewer with style 1), which is word-wrapped into a buffer of at least
32 KB with a line table of 2500 entries (0x4bf214); the scrollbar scrolls
by lines; 0x8001 redraws; 0x8002 resizes. A left click is passed to the
parent in its coordinates.

### Message box (0x4bfc69, 89 call sites)

`ctl_message_box(x, y, title, text, buttons, timeout)` (0x4bfc69): the
game's one modal dialog. `ctl_msgbox_init` (0x4c0d2e, also called by
game_main) creates the window once (0x6726b0: 450x270, TOPMOST |
HITCHILDREN, proc 0x4c06d7) with four buttons (ids 1000-1003, bitmap
0x227 each, font 500) and keeps background bitmap 0x260. Each call:

- Size from the text: width clamp(text width, 173, 303) plus its value
  mod 10, height (width / that + 2 lines + one per blank line) clamped to
  48..150, widened for the buttons; the background bitmap's quarters go to
  the corners and 10 px strips from its middle fill the extra size.
- Title at (5, 4), text word-wrapped into (24, 24)-(196 + extra, 71 +
  extra) in colors 0x1a / 0x13, buttons centred in a row at y = 90 +
  extra. `buttons` is double-NUL terminated, up to four: `&` marks the key
  (stored upper-cased), a leading `~` the Esc / F1 button (with one button,
  that one).
- Pauses the game, calls 0x403c23 and closes the window at 0x5bb200 (port:
  `ctl_hooks.modal_begin`), centres if x or y is -1, captures, raises,
  activates, and with a timeout (only if menu item 0x6465e8 is checked;
  port: `ctl_hooks.timeouts_enabled`) sets the deadline for `ctl_idle`.
- Runs `sys_pump` until a button is chosen and returns its number (1-4).

Keys (proc 0x4c06d7): Enter, Esc / F1 or a button's key press it on key
down and choose it on key up; Tab / Shift-Tab and Left / Right move the
focus frame. A click on a button chooses it (0x14 from the button).

The port also splits it into `ctl_msgbox_open` / `ctl_msgbox_result` /
`ctl_msgbox_close` for callers with their own loop.

## Menus

`src/ui/menu.c`, `menu.h`. Menus are windows with `menu_proc` (0x4c84c7)
and a style word in data 0:

- A **menu** (`menu_create` 0x4c6e66; flags NOSURFACE | NOTITLE | 0x80000,
  data 0 = style | 0x400000) is a container; its children are items. Style
  bit 4 lays items out side by side, 8 keeps the rectangle; 0x3c is the
  menu bar `wm_create_window` makes for `WM_F_MENUBAR` (as wide as the
  owner, top edge + font + bottom edge + 1 high, and the owner's client top
  moves below it) with the close (0x230) and minimize (0x231) buttons.
- An **item** (`menu_add_item` 0x4c6fce; NOSURFACE | NOCLIP | NORAISE,
  0x14 bytes: style | 0x800000, font, value, id) is text (0x1000000, the
  title: `&` marks the key, a tab starts a right-aligned part) or a
  separator (0x8000000). 0x400 checkable, 0x200 checked
  (`menu_set_checked` 0x4c8393, `menu_is_checked` 0x4c8355), 0x800
  highlighted. An item's child is its pop-up menu.
- `menu_set_skin` (0x4c6c64; game_main loads 0x4010-0x4018: four corners,
  four edges, fill) and `menu_set_font` (0x4c6e1f; 501, measures the check
  mark glyph 1 and the sub-menu arrow glyph 3). 0x4c6bf9 (sound slots
  700-702) is never called.

**Layout**: `menu_show(m, 1)` (0x4c70f6 -> 0x4c714b) lays a menu out the
first time it opens (window flag 0x40000000 afterwards): it measures the
items (0x1003: text width + 20 by font height + 2, padding 10 / 1;
separators 2 high), creates the surface, places a pop-up right of its
item's menu or below a bar item, paints the frame (0x1001), places and
draws every item (0x1005) and shows them, then raises and shows the menu.
The game builds its menus once (0x409c17: File 100, Global 200, Window
300, Tutorial 400 on the bar, each with a pop-up from a table of {id,
text, style}) and opens the bar after loading. Items are drawn into their
menu's surface: fill, text in white with a black shadow, check mark,
right-aligned part, arrow, and highlighting by remapping the pixels through
the 20% brighter palette remap (0x535318).

**State**: the highlighted item (0x6727c4; highlighting an item highlights
the items it hangs under), the open pop-up (0x6727cc, which the window
manager also reads), the focus to give back (0x6727d0) and a queue of items
to redraw (0x535290).

**Mouse**: pressing an item with a pop-up opens it (pressing it again
closes it); moving over items while a pop-up is open moves the highlight,
opening pop-ups on the way. Releasing on an item chooses it, closing
everything; releasing on an item with a pop-up does the same (so a simple
click on a bar item opens and closes its menu, and the owner gets that
item's id, which the game ignores: the menus work by press, drag, release).
Releasing anywhere else closes the menus without a command (the window
manager forwards that release to the open menu's item).

**Keyboard**: Alt (key up) on a window with a menu bar activates its first
item (0x4c916a), which takes the focus. Then Down opens the pop-up, Up /
Down / Left / Right move (skipping separators, wrapping, crossing to the
neighbouring bar menu), Enter chooses, Alt closes, and a character chooses
the item with that `&` key among the highlighted item's siblings (opening
its pop-up if it has one). Losing the focus to a non-menu window closes
everything.

**Commands**: choosing an item sends `WM_MSG_MENU_COMMAND` (0x1006) to the
bar's owner with w = the item and a = its id. In the game the owner is
the main window (0x5f7460), whose proc game_main replaces with
`game_view_proc` (0x404023). It switches on the id (0x33-0x36 excepted,
which skip the 0x403c23 call) and either acts at once or sets
`game_command` (0x650048) for game_main's loop, which switches on it once
per frame: id 1 -> command 1; id 2 (asks "Save Game Before Loading?" if
there is a game: yes -> command 1 then 2 via `game_next_command`, no -> 2);
id 3 likewise -> 5; id 4 -> 3; id 5 -> 4 (new game, after a query). In
game_main 1 and 6 call 0x480ea4(1 / 2), 2 and 5 call 0x489e90(1 / 2), 3
calls 0x4946bd, and 4 asks for the difficulty with a message box and starts
a new game. The game also sends 0x1006 itself to run a command (its
keyboard shortcuts, and 0x0e after a new game).

## Port notes

- Messages carry `intptr_t` arguments and extra data is stored in
  `intptr_t` slots (one per 4 bytes of the original offset), so pointers
  fit on 64-bit. The list box's block is a C struct.
- `wm_hooks` keeps the game/platform hooks (attract mode stop, keyboard
  polling, sound idle); the menu bar, scrollbars and Alt activation are
  direct calls again. `ctl_hooks`: `pump` (the modal loops' `sys_pump`;
  NULL runs `wm_idle`, `ctl_idle`, `gfx_present`), `modal_begin` and
  `timeouts_enabled` (game state the message box looks at). All may be
  NULL. Without `attract_stop`, the first input just sets `wm_input_live`.
- Fixed original bugs: `wm_center` and `wm_invalidate(w, NULL)` passed a
  parent-relative rectangle through local-to-screen (offset twice), so the
  wrong area was repainted; the message queue's pop could advance the write
  index after a wrap and replay stale entries; destroying a window left a
  dangling capture and leaked its invalid list, and left queued messages
  for it (now dropped); `wm_set_title_size` didn't update the size; the
  menu bar's cached close/minimize bitmaps were freed by the buttons that
  use them (each bar now loads its own); the message box's button press
  compared `1000 - id` with the focus and never updated it; the list box's
  row-rectangle request read an uninitialized row; the text viewer compared
  a new top line with the last line instead of the current top, and could
  set a negative scroll range or overrun its buffers; the menus' redraw
  queue could overrun; a hidden scrollbar thumb was still drawn.
  `wm_shutdown` also frees the windows (the original only unhooks) and
  `ctl_shutdown` the message box.
- Kept: the region pool limit, the empty "under the button" save at
  creation, the capture clip using the parent's client rect in local
  coordinates, message 0x0f painting new/old/new, the horizontal thumb's
  height, the click-closes-the-menu behaviour, the previous-item search
  that only wraps to a selectable last item, a timed-out message box
  returning 1, the message box button that takes the capture from the
  dialog.

## Wiring it up

After `gfx_init` (and `gfx_mouse_init`, `gfx_font_init`):

```c
wm_hooks.key_poll = ...;             /* the keyboard queue (kb_flush_one) */
if (wm_initialize() != 0) fail;      /* sys_initialize step 11 */
sys_register_task(wm_initialize, wm_shutdown, wm_suspend, wm_resume, wm_idle);
ctl_initialize();                    /* step 12 */
ctl_hooks.pump = sys_pump;
sys_register_task(ctl_initialize, NULL, ctl_suspend, ctl_resume, ctl_idle);
ctl_set_pause_flag(&game_paused);
...
/* game_main: fonts 500/501, the skins, then the main window */
ctl_scroll_set_hbitmaps(0x4002, 0x4003, 0x4006, 0x4005 loaded);
ctl_scroll_set_vbitmaps(0x4000, 0x4001, 0x4006, 0x4004 loaded);
menu_set_skin(0x4010 ... 0x4018 loaded);
menu_set_font(501);
main = wm_create_toplevel(&screen, proc, WM_F_MENUBAR | WM_F_VSCROLL | WM_F_HSCROLL | WM_F_CLIPMOUSE);
ctl_msgbox_init();
/* build the menus under main->menubar, then menu_show(main->menubar, true) */
wm_set_input_live(true);             /* or call it from the attract mode code */
/* each frame: sys_pump -> task idles (wm_idle, ctl_idle) -> gfx_present() */
```

Shutdown order: `ctl_shutdown`, then `wm_shutdown`. The main window's
WM_CLOSE becomes `wm_send(wm_root(), wm_root(), WM_MSG_CLOSE, 0, 0)` and
WM_DISPLAYCHANGE `wm_screen_resized(w, h)`. The port calls it from the
port window's relayout (`game/display.c`, see
[rendering.md](rendering.md#port-window)).

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

`ui_widgets_test` uses the game's skins on a main window like the game's
(menu bar, both scrollbars) and drives everything through the mouse module
and the `key_poll` hook, with `wm_idle` making the messages:

- menus: press File, drag to Load Game (pop-up on screen, item brightened),
  release: command 2, pop-up gone, focus back; Alt, Down, Down, Down (over
  the separator), Enter: command 7; Alt, `g`, `x`: command 22; a release
  outside closes without a command; check marks; find by id;
- the main window's scrollbar: arrow clicks, a track click near the end, a
  thumb drag to the top (positions, thumb movement, capture);
- a list box of 30 lines: click a row (selection, notification, item data,
  text, inverted row pixels), 12 clicks on its down arrow (selection 14,
  top line moved, scrollbar position), delete;
- a text field: click, type (a rejected `@`), backspace, Enter
  notification, white text pixels, set text;
- a text viewer: wrapped text, two arrow clicks scroll two lines;
- message boxes: modal with a click on button 2 from the pump (2, focus and
  pause flag restored), Esc on a `~` button (3, screen restored), a button
  key, Tab + Enter, a timeout (1); the text prompt typed from the pump.

It writes `<prefix>_menu.ppm` (File open, Load Game highlighted),
`<prefix>_widgets.ppm` and `<prefix>_msgbox.ppm`. Clean under ASan/UBSan.
