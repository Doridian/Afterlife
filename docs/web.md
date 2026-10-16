# The WebAssembly build

Port: the `EMSCRIPTEN` block of `CMakeLists.txt`, `web/shell.html` (the page),
`web/pre.ts` (the TypeScript glue, compiled by tsc), the `__EMSCRIPTEN__` parts of
`src/platform/platform_sdl.c`. SDL3 comes from Emscripten's port.

```sh
npm ci                                  # the TypeScript compiler for web/*.ts (once)
emcmake cmake --preset wasm -DAFTERLIFE_WASM_DATA=$PWD/orig
cmake --build --preset wasm
cd build/wasm && python3 -m http.server 8000   # http://localhost:8000/afterlife.html
```

The page must come from a web server (not `file://`). The output is
`afterlife.html`, `.js`, `.wasm` and, with data, `afterlife.data`.

## Game data

- **Built in**: `AFTERLIFE_WASM_DATA` names a folder with the game files.
  CMake packages `ALIFE.INI`, the bundles its `CD Path` and `Resource file`
  name, `ALIFE.EXE` and `ASCEN*.ASC` (names matched case-insensitively) into
  `afterlife.data`, mounted at `/orig`. With the CD's INI that's `ALIFE.000`,
  about 150 MB. `AFTERLIFE_WASM_FILES` overrides the list (e.g.
  `"ALIFE.INI;MEDIUM.000;ALIFE.000;ALIFE.EXE;ASCEN001.ASC"`). The data is
  copyrighted: don't put such a build on a public server.
- **Picked by the player**: without `AFTERLIFE_WASM_DATA` the page asks for
  the CD: a CD image (`.bin` or `.iso`, read by `web/iso9660.ts`), a `.zip`
  of either (read by `web/zip.ts`, the archive.org download as it is), the
  CD's files, or its folder. The files are kept as blobs in an IndexedDB store of
  its own (`afterlife-game-data`), in 8 MB records (`NAME#000000`, ...) so the
  store's progress bar moves in steps, so this happens once per browser. They are
  copied into `/userdata` at every start. IDBFS can't hold them: browsers cap
  one stored value below ALIFE.000's size. `?pick` in the URL asks again.
  The picker links to the CD on archive.org (`AfterlifeUSA`, zip with the
  Redump `.bin`). The steps that take a while (reading a zip's deflated
  entries, copying the files, storing them, and reading them back at each
  start) show a progress bar in the status panel (`progress` in `pre.ts`,
  `setProgress` in `shell.html`).
- **Zips**: `zip.ts` reads the central directory and stored or deflated
  entries (deflate through `DecompressionStream`, so Firefox 113+, Safari
  16.4+). Zip64 isn't handled.
- **CD images**: `iso9660.ts` reads the ISO 9660 tree of the data track, from
  a 2048-byte `.iso` or a raw 2352-byte `.bin` (mode 1 or mode 2 form 1). The
  game files are Blobs that point into the image, so it is never loaded whole.

## Published build

`.github/workflows/pages.yml` builds the page without game data and publishes
it on GitHub Pages on every push to `main`: `web/index.html` (the front page,
with the archive.org link and steps) and `afterlife.html`, `.js` and `.wasm`
next to it. The job fails if the build has a `.data` package.

## Main loop

The game keeps the original's blocking loops: the main loop, the title, every
modal dialog and message box pump through `sys_pump`, which ends in
`plat_yield`. The build uses `-sASYNCIFY`, and `plat_yield` and
`plat_delay_ms` call `emscripten_sleep`, which unwinds to the browser and comes
back. So no loop had to be rewritten around `emscripten_set_main_loop`. Each
`sys_pump` presents a frame and yields once. The game's clock is
`SDL_GetTicks` and the 16 ms timer is emulated from the pump (`tm_poll`), so
the speed doesn't depend on how often the browser schedules it. The C stack
is 8 MB (`-sSTACK_SIZE`, as on Linux), the ASYNCIFY save stack 64 KB.

## Audio

iMUSE mixes on the game's tick and queues into `plat_audio_queue`. On the web
that goes to `web/audio.ts` (`--pre-js`), not SDL's audio stream: an
AudioWorklet plays it on the audio thread, in 22050 Hz stereo (the
AudioContext runs at the mixer's rate). The mixer keeps at most two blocks
queued (`plat_audio_queue_limit`, 2048 frames, about 93 ms); the queue is
read back from the worklet, which reports what it has played. Native builds
still use SDL's stream with eight blocks. Browsers start audio only after a
click or key press; the context is resumed on the first one.

## Storage

SDL's pref folder `/libsdl/Afterlife/Afterlife/` is an IDBFS mount with
`autoPersist`. `web/pre.ts` loads it before `main` runs, and closing or
deleting a file there writes it back to IndexedDB. It holds `ALIFEPORT.INI`
and, on the web, the saved games (`plat_save_dir`; natively they go to the
INI `Save games` folder or the data folder). The slot dialogs list and delete
them as usual.

## Saved games as files

- **Drag and drop**: dropping a `.alf` save or `.asc` scenario onto the page
  loads it, at any time: during the intro or the title animation (both are
  skipped), on the title menu, or in the game (where it asks to save first,
  like Load Game). SDL's Emscripten backend copies the file to
  `/tmp/filedrop/N/` and sends `SDL_EVENT_DROP_FILE`; the platform keeps the
  latest drop until the title's pump or `game_main_step` takes it
  (`shell_take_drop`), and the copy is deleted after the load. At the title
  the file is parsed first, so a bad one gets a message box instead of an
  empty game. Natively the same works with the enhancements on (the default).
- **File > Save to Download** (web only) asks for a file name (the last one
  saved or dropped, prefilled) and saves the game to a temporary file in
  `/tmp` that is handed to the browser as a download and removed. Nothing is
  written among the browser's saved games.
- **File > Download Saved Game** (web only) picks one of the browser's saved
  games in the load list and downloads it, named after its title.

## Page

- The canvas fills the browser window by CSS. SDL sizes its pixel buffer as
  the CSS size times `devicePixelRatio` and reports resizes, so the screen
  follows the browser window as it follows the window natively
  ([rendering.md](rendering.md#port-window)). The window is created with
  `SDL_WINDOW_OPENGL`: otherwise SDL recreates it for the WebGL renderer, and
  destroying a window leaves an inline 1x1 CSS size on a HiDPI canvas.
- The favicon is the game's icon (group icon 101 of `ALIFE.EXE`), set when
  the display starts (`plat_window_set_icon`, a PNG data URL). Natively it
  is the window's icon. Until then the page has an empty one.
- Alt+Enter and F11 make the canvas fullscreen. SDL keeps F1-F12 and
  Backspace from the browser, so F5 saves instead of reloading.
- URL arguments: each `?arg=` is passed on the command line after the data
  folder, e.g. `afterlife.html?arg=ASCEN001.ASC&arg=--no-enhancements`.
- Quitting (File > Exit Game, or Quit at the title) ends the program
  (`-sEXIT_RUNTIME`, so `atexit` runs). The page then offers a reload. A crash
  says so too.

## Checking it headless

Serve `build/wasm` locally and drive it with a headless Chromium (e.g.
Playwright with `executablePath: '/usr/bin/chromium'`, `headless: true`).
Mouse and keyboard events reach SDL as from a user. Menus open on a press and
choose on release, so drag from the bar to the item.
