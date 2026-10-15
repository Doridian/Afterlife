# The WebAssembly build

Port: the `EMSCRIPTEN` block of `CMakeLists.txt`, `web/shell.html` (the page),
`web/pre.js` (the JavaScript glue), the `__EMSCRIPTEN__` parts of
`src/platform/platform_sdl.c`. SDL3 comes from Emscripten's port.

```sh
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
  the files (or the CD's folder) and keeps them as blobs in an IndexedDB store
  of its own (`afterlife-game-data`), so this happens once per browser. They
  are copied into `/userdata` at every start. IDBFS can't hold them: browsers
  cap one stored value below ALIFE.000's size. `?pick` in the URL asks again.

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

iMUSE mixes on the game's tick and queues into an SDL audio stream
(`plat_audio_queue`). SDL's Emscripten backend plays it through a Web Audio
ScriptProcessorNode on the main thread, which is why the browser warns that
it is deprecated. Browsers start audio only after a click or key press. SDL
resumes it then, and until then the game runs with its output discarded.

## Storage

SDL's pref folder `/libsdl/Afterlife/Afterlife/` is an IDBFS mount with
`autoPersist`. `web/pre.js` loads it before `main` runs, and closing or
deleting a file there writes it back to IndexedDB. It holds `ALIFEPORT.INI`
and, on the web, the saved games (`plat_save_dir`; natively they go to the
INI `Save games` folder or the data folder). The slot dialogs list and delete
them as usual.

## Page

- The canvas fills the browser window by CSS. SDL sizes its pixel buffer as
  the CSS size times `devicePixelRatio` and reports resizes, so the screen
  follows the browser window as it follows the window natively
  ([rendering.md](rendering.md#port-window)). The window is created with
  `SDL_WINDOW_OPENGL`: otherwise SDL recreates it for the WebGL renderer, and
  destroying a window leaves an inline 1x1 CSS size on a HiDPI canvas.
- Alt+Enter and F11 make the canvas fullscreen. SDL keeps F1-F12 and
  Backspace from the browser, so F5 saves instead of reloading.
- URL arguments: each `?arg=` is passed on the command line after the data
  folder, e.g. `afterlife.html?arg=ASCEN001.ASC&arg=--enhancements`.
- Quitting (File > Exit Game, or Quit at the title) ends the program
  (`-sEXIT_RUNTIME`, so `atexit` runs). The page then offers a reload. A crash
  says so too.

## Checking it headless

Serve `build/wasm` locally and drive it with a headless Chromium (e.g.
Playwright with `executablePath: '/usr/bin/chromium'`, `headless: true`).
Mouse and keyboard events reach SDL as from a user. Menus open on a press and
choose on release, so drag from the bar to the item.
