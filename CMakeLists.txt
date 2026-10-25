cmake_minimum_required(VERSION 3.24)
project(afterlife C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(EMSCRIPTEN)
    # Emscripten ships SDL3 as a port.
    add_library(SDL3::SDL3 INTERFACE IMPORTED)
    target_compile_options(SDL3::SDL3 INTERFACE --use-port=sdl3)
    target_link_options(SDL3::SDL3 INTERFACE --use-port=sdl3)
else()
    # AFTERLIFE_STATIC_SDL: SDL3 always fetched and linked in, so that the
    # executable is the one file (the release packages)
    option(AFTERLIFE_STATIC_SDL "Fetch SDL3 and link it statically, even if one is installed" OFF)
    if(NOT AFTERLIFE_STATIC_SDL)
        find_package(SDL3 CONFIG QUIET)
    endif()
    if(NOT SDL3_FOUND)
        message(STATUS "System SDL3 not found or not wanted, fetching it")
        include(FetchContent)
        FetchContent_Declare(SDL3
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-3.2.24
            GIT_SHALLOW TRUE)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(SDL3)
    endif()
endif()

# Everything but the entry point, shared by the game and the tests.
add_library(afterlife_core STATIC
    src/platform/platform_sdl.c
    src/compbun/cbres.c
    src/compbun/bndlemgr.c
    src/compbun/cmprsmgr.c
    src/compbun/preload.c
    src/game/resource.c
    src/formats/bmp8.c
    src/formats/flc.c
    src/formats/tile.c
)
# The codecs are written separately; build them in when they're there.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/compbun/codec.c")
    target_sources(afterlife_core PRIVATE src/compbun/codec.c)
    target_compile_definitions(afterlife_core PRIVATE COMPBUN_HAVE_CODECS)
endif()
target_include_directories(afterlife_core PUBLIC src)
target_link_libraries(afterlife_core PUBLIC SDL3::SDL3)

add_executable(afterlife src/main.c)
target_link_libraries(afterlife PRIVATE afterlife_core)
if(NOT EMSCRIPTEN)
    # the game data when none is given or found: the player picks the CD or
    # an image of it (src/platform/datafind.c)
    target_sources(afterlife PRIVATE src/platform/datafind.c)
endif()
if(WIN32)
    # a GUI program with the packages' icon (res/), one file: SDL3 is
    # linked in (fetched, static) and so is MinGW's runtime
    enable_language(RC)
    target_sources(afterlife PRIVATE res/afterlife.rc)
    set_property(SOURCE res/afterlife.rc APPEND PROPERTY OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/icon.ico)
    set_target_properties(afterlife PROPERTIES WIN32_EXECUTABLE ON)
    if(MINGW)
        target_link_options(afterlife PRIVATE -static)
    endif()
endif()

if(NOT EMSCRIPTEN)
    # tests/compbun_dump: dumps every resource of a bundle through the compbun
    # stack, for tests/compare_jiff.py.
    add_executable(compbun_dump tests/compbun_dump.c)
    target_link_libraries(compbun_dump PRIVATE afterlife_core)
    # tests/formats_dump: decodes every FLI/FLC and TILE resource to PNGs.
    add_executable(formats_dump tests/formats_dump.c)
    target_link_libraries(formats_dump PRIVATE afterlife_core)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(t afterlife_core afterlife compbun_dump formats_dump)
        if(TARGET ${t})
            target_compile_options(${t} PRIVATE -Wall -Wextra)
        endif()
    endforeach()
endif()

if(EMSCRIPTEN)
    # The web build (docs/web.md). The game keeps its original blocking loops
    # (the main loop and every modal dialog pump through sys_pump), so
    # ASYNCIFY lets plat_yield()/plat_delay_ms() return to the browser.
    set(AFTERLIFE_WASM_DATA "" CACHE PATH
        "Folder with the game files to build into the web build (else the page asks for them)")
    set(AFTERLIFE_WASM_FILES "" CACHE STRING
        "Files of AFTERLIFE_WASM_DATA to package (default: ALIFE.INI, the bundles it names, ALIFE.EXE, ASCEN*.ASC)")
    # The page's glue is TypeScript (web/*.ts, docs/web.md): tsc compiles it
    # into the build folder, and the JavaScript goes to emcc as --pre-js.
    # Install the compiler once with `npm ci` in the repository root.
    find_program(AFTERLIFE_TSC tsc HINTS "${CMAKE_CURRENT_SOURCE_DIR}/node_modules/.bin" REQUIRED
        DOC "The TypeScript compiler (npm ci)")
    set(AFTERLIFE_WEB_JS_DIR "${CMAKE_CURRENT_BINARY_DIR}/web-js")
    # the --pre-js order: the readers first, pre.js (which uses them) last
    set(AFTERLIFE_WEB_GLUE
        "${AFTERLIFE_WEB_JS_DIR}/iso9660.js"
        "${AFTERLIFE_WEB_JS_DIR}/zip.js"
        "${AFTERLIFE_WEB_JS_DIR}/audio.js"
        "${AFTERLIFE_WEB_JS_DIR}/pre.js")
    add_custom_command(OUTPUT ${AFTERLIFE_WEB_GLUE}
        COMMAND "${AFTERLIFE_TSC}" -p "${CMAKE_CURRENT_SOURCE_DIR}/web/tsconfig.json"
            --outDir "${AFTERLIFE_WEB_JS_DIR}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/tsconfig.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/web/emscripten.d.ts"
            "${CMAKE_CURRENT_SOURCE_DIR}/web/iso9660.ts"
            "${CMAKE_CURRENT_SOURCE_DIR}/web/zip.ts"
            "${CMAKE_CURRENT_SOURCE_DIR}/web/audio.ts"
            "${CMAKE_CURRENT_SOURCE_DIR}/web/pre.ts"
        COMMENT "Compiling the web glue (TypeScript)"
        VERBATIM)
    add_custom_target(afterlife_web_glue DEPENDS ${AFTERLIFE_WEB_GLUE})
    add_dependencies(afterlife afterlife_web_glue)
    set_target_properties(afterlife PROPERTIES SUFFIX ".html"
        LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html;${AFTERLIFE_WEB_GLUE}")
    target_link_options(afterlife PRIVATE
        -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536
        -sSTACK_SIZE=8MB -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=64MB
        -sEXIT_RUNTIME -sFORCE_FILESYSTEM -lidbfs.js -sENVIRONMENT=web
        "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
    foreach(glue IN LISTS AFTERLIFE_WEB_GLUE)
        target_link_options(afterlife PRIVATE "--pre-js=${glue}")
    endforeach()
    if(AFTERLIFE_WASM_DATA)
        # Data file names are matched case-insensitively, like the game does.
        file(GLOB _all RELATIVE "${AFTERLIFE_WASM_DATA}" "${AFTERLIFE_WASM_DATA}/*")
        set(_want ${AFTERLIFE_WASM_FILES})
        if(NOT _want)
            set(_want ALIFE.INI ALIFE.EXE)
            foreach(f ${_all})
                string(TOUPPER "${f}" F)
                if(F MATCHES "^ASCEN.*\\.ASC$")
                    list(APPEND _want "${F}")
                elseif(F STREQUAL "ALIFE.INI")
                    # the bundles the INI names ("CD Path", "Resource file")
                    file(STRINGS "${AFTERLIFE_WASM_DATA}/${f}" _lines REGEX "^(CD Path|Resource file)")
                    foreach(l ${_lines})
                        string(REGEX REPLACE "^[^=]*= *([^\r]*[^ \r])?.*$" "\\1" v "${l}")
                        string(REGEX REPLACE "^.*[\\/:]" "" v "${v}")
                        string(TOUPPER "${v}" v)
                        list(APPEND _want "${v}")
                    endforeach()
                endif()
            endforeach()
        endif()
        list(TRANSFORM _want TOUPPER)
        set(_packed "")
        foreach(f ${_all})
            string(TOUPPER "${f}" F)
            if(F IN_LIST _want AND NOT IS_DIRECTORY "${AFTERLIFE_WASM_DATA}/${f}")
                target_link_options(afterlife PRIVATE "--preload-file=${AFTERLIFE_WASM_DATA}/${f}@/orig/${f}")
                set_property(TARGET afterlife APPEND PROPERTY LINK_DEPENDS "${AFTERLIFE_WASM_DATA}/${f}")
                list(APPEND _packed "${f}")
            endif()
        endforeach()
        message(STATUS "Web build data (${AFTERLIFE_WASM_DATA}): ${_packed}")
    endif()
endif()

# Startup / INI (docs/startup.md).
target_sources(afterlife_core PRIVATE src/game/ini.c src/game/config.c src/game/startup.c)
# Port-only: the enhancements switch, the port's settings and the UI scale
# (docs/enhancements.md).
target_sources(afterlife_core PRIVATE src/game/enhance.c src/game/display.c src/platform/present.c
    src/ui/portopts.c)
if(NOT EMSCRIPTEN)
    # tests/enhance_test: the settings file round trip, the switches, and the
    # UI scale's layout and mouse mapping. No game data.
    add_executable(enhance_test tests/enhance_test.c)
    target_link_libraries(enhance_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(enhance_test PRIVATE -Wall -Wextra)
    endif()
endif()
if(NOT EMSCRIPTEN)
    # tests/ini_dump: prints what the init sequence reads from an ALIFE.INI.
    add_executable(ini_dump tests/ini_dump.c)
    target_link_libraries(ini_dump PRIVATE afterlife_core)
endif()

# Video core (docs/rendering.md).
target_sources(afterlife_core PRIVATE src/gfx/surface.c src/gfx/palette.c src/gfx/draw.c src/gfx/font.c
    src/gfx/mouse.c src/gfx/exe_res.c)
# ALIFE.EXE's image data (cursors, icon, built-in font) is read from the
# user's EXE at runtime (gfx/exe_res.c).
if(NOT EMSCRIPTEN)
    # tests/gfx_test: draws a bundle bitmap, primitives and text, writes a PPM.
    add_executable(gfx_test tests/gfx_test.c)
    target_link_libraries(gfx_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(gfx_test PRIVATE -Wall -Wextra)
    endif()
endif()

# Input and timer (docs/startup.md): keyboard queue, main_wnd_proc, the
# emulated 16 ms multimedia timer.
target_sources(afterlife_core PRIVATE src/game/input.c src/game/timer.c)
if(NOT EMSCRIPTEN)
    # tests/input_test: synthetic SDL events through main_wnd_proc to the
    # keyboard queue and mouse module, and the emulated timer.
    add_executable(input_test tests/input_test.c)
    target_link_libraries(input_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(input_test PRIVATE -Wall -Wextra)
    endif()
endif()

# Window manager and controls (docs/ui.md).
target_sources(afterlife_core PRIVATE src/ui/wm.c src/ui/region.c src/ui/controls.c)
if(NOT EMSCRIPTEN)
    # tests/ui_region_test: region arithmetic and stacking, no game data.
    # tests/ui_test: overlapping windows with bundle bitmaps, writes a PPM.
    foreach(t ui_region_test ui_test)
        add_executable(${t} tests/${t}.c)
        target_link_libraries(${t} PRIVATE afterlife_core)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${t} PRIVATE -Wall -Wextra)
        endif()
    endforeach()
endif()

# iMUSE (docs/imuse.md): the digital iMUSE library and the game's sound
# slot module on top of it. The output goes to plat_audio_*.
target_sources(afterlife_core PRIVATE
    src/imuse/commands.c src/imuse/fades.c src/imuse/files.c src/imuse/groups.c
    src/imuse/time.c src/imuse/triggers.c src/imuse/wave.c src/imuse/tracks.c
    src/imuse/dispatch.c src/imuse/streamer.c src/imuse/mixer.c src/imuse/waveapi.c
    src/game/sound.c)
if(NOT EMSCRIPTEN AND NOT WIN32)
    target_link_libraries(afterlife_core PUBLIC m)
endif()
if(NOT EMSCRIPTEN)
    # tests/imuse_test: plays a music and an effect resource through iMUSE
    # headless and writes the mix to a WAV.
    add_executable(imuse_test tests/imuse_test.c)
    target_link_libraries(imuse_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(imuse_test PRIVATE -Wall -Wextra)
    endif()
endif()

# Map tile renderer and map view (docs/map.md, docs/formats/tile.md).
target_sources(afterlife_core PRIVATE src/gfx/tile_draw.c src/gfx/map_view.c)
if(NOT EMSCRIPTEN)
    # tests/map_render: tile_draw self-checks, zoom/effect sheets, synthetic and savegame maps.
    add_executable(map_render tests/map_render.c)
    target_link_libraries(map_render PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(map_render PRIVATE -Wall -Wextra)
    endif()
endif()

# ALSV saved games and scenarios (docs/formats/alsv.md): the game state
# (src/game/state.h) and its loader/saver.
target_sources(afterlife_core PRIVATE src/game/savegame.c)
if(NOT EMSCRIPTEN)
    # tests/savegame_test: loads .ASC/.ALF files, prints a summary, checks
    # that saving reproduces them byte for byte.
    add_executable(savegame_test tests/savegame_test.c)
    target_link_libraries(savegame_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(savegame_test PRIVATE -Wall -Wextra)
    endif()
endif()

# AKOS costumes (docs/formats/akos.md): the advisors' animated heads.
target_sources(afterlife_core PRIVATE src/formats/akos.c src/gfx/akos.c)
if(NOT EMSCRIPTEN)
    # tests/akos_test: decodes every AKOS resource, runs the interpreter,
    # renders cels, animations and the advisor window to PNGs.
    add_executable(akos_test tests/akos_test.c)
    target_link_libraries(akos_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(akos_test PRIVATE -Wall -Wextra)
    endif()
endif()
# The talking heads on top of AKOS (needs the WM, fonts and the voice slot).
target_sources(afterlife_core PRIVATE src/gfx/akos_talk.c)

# UI toolkit beyond the button (docs/ui.md): scrollbar, text field, list
# box and text viewer, message box, menus.
target_sources(afterlife_core PRIVATE src/ui/scrollbar.c src/ui/textfield.c src/ui/listbox.c src/ui/msgbox.c
    src/ui/menu.c)
if(NOT EMSCRIPTEN)
    # tests/ui_widgets_test: menus, scrollbars, list, text field, viewer,
    # message box and prompt through synthetic input; writes PPMs.
    add_executable(ui_widgets_test tests/ui_widgets_test.c)
    target_link_libraries(ui_widgets_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ui_widgets_test PRIVATE -Wall -Wextra)
    endif()
endif()

# World core (docs/game-map.md, WP0): the live GameState, plane switch,
# cell / sim-cell accessors, RNG, bignum and shared utilities.
target_sources(afterlife_core PRIVATE src/game/world.c src/game/util.c)
if(NOT EMSCRIPTEN)
    # tests/world_test: RNG/bignum/mul_div/format checks, then cross-checks
    # the accessors against the realm statistics of each save given.
    add_executable(world_test tests/world_test.c)
    target_link_libraries(world_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(world_test PRIVATE -Wall -Wextra)
    endif()
endif()

# tools/emu (differential tests against ALIFE.EXE under unicorn): the port as
# a shared library for ctypes. Not built by default; tools/emu builds it in
# build/emu itself. Package-specific C wrappers go in tools/emu/shims/*.c.
if(NOT EMSCRIPTEN AND NOT WIN32)
    file(GLOB AFTERLIFE_EMU_SHIMS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/emu/shims/*.c)
    add_library(afterlife_port SHARED EXCLUDE_FROM_ALL tools/emu/port_shim.c ${AFTERLIFE_EMU_SHIMS}
        $<TARGET_PROPERTY:afterlife_core,SOURCES>)
    target_compile_definitions(afterlife_port PRIVATE $<TARGET_PROPERTY:afterlife_core,COMPILE_DEFINITIONS>)
    target_include_directories(afterlife_port PRIVATE src)
    target_link_libraries(afterlife_port PRIVATE SDL3::SDL3 m)
endif()

# The Planet (docs/game-map.md WP5): planet simulation, world generation,
# coastline map and the Planetview window.
target_sources(afterlife_core PRIVATE src/game/planet.c src/game/planet_gen.c src/game/planet_map.c
    src/ui/planetview.c)
if(NOT EMSCRIPTEN)
    # tests/planet_test: steps the planet of each save for several years and
    # checks invariants; tests/planetview_test renders the Planetview of a
    # save headless into a PNG.
    add_executable(planet_test tests/planet_test.c)
    target_link_libraries(planet_test PRIVATE afterlife_core)
    add_executable(planetview_test tests/planetview_test.c)
    target_link_libraries(planetview_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(planet_test PRIVATE -Wall -Wextra)
        target_compile_options(planetview_test PRIVATE -Wall -Wextra)
    endif()
endif()

# Information views (docs/game-map.md WP6a): the Graphview (graphs and the
# yearly history records sim_update appends) and the Mapview (minimap).
# They read the building tables of src/game/tables.c (WP2's
# game_init_defaults fills them); CMake drops the duplicate if another
# block lists it too.
target_sources(afterlife_core PRIVATE src/ui/views.c src/ui/graphview.c src/ui/mapview.c
    src/ui/mapview_outline.c src/game/tables.c)
if(NOT EMSCRIPTEN)
    # tests/views_test: loads each save, opens both views headless, every
    # graph and Mapview layer through the synthetic mouse path, checks them
    # against the save's data and writes PNGs.
    add_executable(views_test tests/views_test.c tests/views_test_gfx.c)
    target_link_libraries(views_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(views_test PRIVATE -Wall -Wextra)
    endif()
endif()

# SOUL flow (docs/game-map.md WP1b): SOUL groups, the road walk, the zone
# scan, arrivals at Gates and siphons, and the Soulview trackers' sim side.
# Needs WP1's realm.c (population), WP2's structure.c/build.c and tables.c;
# CMake drops duplicates if their blocks list them too.
target_sources(afterlife_core PRIVATE src/game/souls.c src/game/zones.c src/game/soultrack.c
    src/game/realm.c src/game/structure.c src/game/build.c src/game/newworld.c src/game/tables.c)
if(NOT EMSCRIPTEN)
    # tests/souls_test: runs the SOUL phases on each save given and checks
    # the population and SOUL bookkeeping invariants.
    add_executable(souls_test tests/souls_test.c)
    target_link_libraries(souls_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(souls_test PRIVATE -Wall -Wextra)
    endif()
endif()

# Build tools, structures, rotation and new worlds (docs/game-map.md WP2).
# world.c runs build_after_load (newworld.c) when a save is applied.
target_sources(afterlife_core PRIVATE src/game/build.c src/game/structure.c src/game/rotate.c
    src/game/newworld.c src/game/tables.c)
if(NOT EMSCRIPTEN)
    # tests/build_test: places and destroys structures on each save given
    # and checks the map invariants, rotates 4x, generates new worlds and
    # (with a data dir) renders one to a PNG.
    add_executable(build_test tests/build_test.c)
    target_link_libraries(build_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(build_test PRIVATE -Wall -Wextra)
    endif()
endif()

# Simulation scheduler and realm economy (docs/game-map.md WP1): sim_update's
# phase machine, the realm year (jobs, workers, wages, banks), population and
# growth rules, the score. Calls into WP1b/WP2/WP5 (listed above); CMake drops
# duplicate sources.
target_sources(afterlife_core PRIVATE src/game/sim.c src/game/economy.c src/game/realm.c src/game/tables.c)
if(NOT EMSCRIPTEN)
    # tests/sim_test: runs the scheduler on each save given for N years with a
    # stopped clock and checks the money and realm bookkeeping invariants.
    add_executable(sim_test tests/sim_test.c)
    target_link_libraries(sim_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(sim_test PRIVATE -Wall -Wextra)
    endif()
endif()

# The game shell (docs/game-map.md WP7): game_main and the main window
# (src/game/shell*.c), the main map view, control panel and tile chooser,
# status bar, title and intro, advisors, news pop-up and help balloon,
# about box and credits, the Audio Settings window, the debug window
# (--debug-window).
target_sources(afterlife_core PRIVATE src/game/shell.c src/ui/title.c src/ui/advisors.c src/ui/news.c
    src/ui/about.c src/ui/about_credits.c src/ui/audiodlg.c src/ui/debugwin.c)

# tests/title_test: the title sequence (intro, animated title, menus)
# driven by synthetic input from the modal pump; writes PPM screenshots.
if(NOT EMSCRIPTEN)
    add_executable(title_test tests/title_test.c)
    target_link_libraries(title_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(title_test PRIVATE -Wall -Wextra)
    endif()
endif()

# The advisors' speech tables (src/ui/advisors_text.c, WP7 advisors) and
# tests/advisors_test: the Jasper and Aria window headless, into a PNG.
target_sources(afterlife_core PRIVATE src/ui/advisors_text.c)
if(NOT EMSCRIPTEN)
    add_executable(advisors_test tests/advisors_test.c)
    target_link_libraries(advisors_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(advisors_test PRIVATE -Wall -Wextra)
    endif()
endif()

# The Karma network and map objects (docs/game-map.md WP3): rails, stations,
# trains and boats (src/game/karma.c), the map object system
# (src/game/mapobj.c) and its renderer half (src/gfx/map_objects.c).
target_sources(afterlife_core PRIVATE src/game/karma.c src/game/mapobj.c src/gfx/map_objects.c)
if(NOT EMSCRIPTEN)
    # tests/karma_test: rebuilds the Karma network of each save, runs trains
    # and years of sim_update with the map objects moving, checks the
    # network, station, train and object map invariants.
    add_executable(karma_test tests/karma_test.c)
    target_link_libraries(karma_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(karma_test PRIVATE -Wall -Wextra)
    endif()
endif()

# tests/news_test: the news pop-up, help balloon, about box and credits
# (WP7) headless, writing PNGs.
if(NOT EMSCRIPTEN)
    add_executable(news_test tests/news_test.c)
    target_link_libraries(news_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(news_test PRIVATE -Wall -Wextra)
    endif()
endif()

# The game shell proper (WP7): the main loop and menus (src/game/shell.c),
# the main map view (shell_view.c), the in-game music, the control panel
# and tile chooser, the status bar.
target_sources(afterlife_core PRIVATE src/game/shell_view.c src/game/music.c src/ui/ctrlpanel.c
    src/ui/statusbar.c)
if(NOT EMSCRIPTEN)
    # tests/shell_test: the whole game headless through synthetic input:
    # title, new game, sim, building, save/load, scenario, quit (PNGs).
    add_executable(shell_test tests/shell_test.c)
    target_link_libraries(shell_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(shell_test PRIVATE -Wall -Wextra)
    endif()
endif()

# Information views, part 2 (docs/game-map.md WP6b): the Microview, the
# Macromanager and the Soulview, the Soulview trackers' yearly pass
# (soultrack.c, listed above too) and the hook wiring.
target_sources(afterlife_core PRIVATE src/ui/microview.c src/ui/microview_info.c src/ui/macro.c
    src/ui/soulview.c src/ui/soulview_text.c src/ui/infoviews.c src/game/soultrack.c)
if(NOT EMSCRIPTEN)
    # tests/views2_test: loads each save, opens the three windows headless,
    # drives them through the synthetic mouse path, checks their drawings
    # and the state changes against the save, writes PNGs.
    add_executable(views2_test tests/views2_test.c tests/views_test_gfx.c)
    target_link_libraries(views2_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(views2_test PRIVATE -Wall -Wextra)
    endif()
endif()
if(NOT EMSCRIPTEN)
    # tests/stride_test: the screen at an odd width (1707 x 1067) keeps the
    # platform framebuffer's row pitch end to end (no game data needed).
    add_executable(stride_test tests/stride_test.c)
    target_link_libraries(stride_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(stride_test PRIVATE -Wall -Wextra)
    endif()
endif()

# Bad Things (docs/game-map.md WP4): the eight disasters, their map objects,
# the freeze/blues spread and the bankruptcy ending.
target_sources(afterlife_core PRIVATE src/game/badthings.c src/game/badthing_objects.c)
if(NOT EMSCRIPTEN)
    # tests/badthings_test: forces every Bad Thing on each save given and
    # steps it through the map object code and sim_update's timers,
    # checking the bookkeeping; renders PNGs of disasters in progress.
    add_executable(badthings_test tests/badthings_test.c)
    target_link_libraries(badthings_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(badthings_test PRIVATE -Wall -Wextra)
    endif()
endif()

# The tutorial (docs/game-map.md WP8): the runner, the scripted input and
# attract mode, the lesson op tables with their speeches (tutorial_script.c),
# the unused tutorial text window, and the
# File menu's save / load / delete slot dialogs (slotdlg.c).
target_sources(afterlife_core PRIVATE src/game/tutorial.c src/game/tutorial_script.c src/ui/tutorialwin.c
    src/ui/slotdlg.c)
if(NOT EMSCRIPTEN)
    # tests/tutorial_test: runs every lesson headless in a game through the
    # scripted input and checks what each one built and that the player's
    # game comes back; also stops one early (Esc); then the text window and
    # the slot dialogs through clicks. PNGs.
    add_executable(tutorial_test tests/tutorial_test.c)
    target_link_libraries(tutorial_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(tutorial_test PRIVATE -Wall -Wextra)
    endif()
endif()

# The port window (docs/rendering.md): live relayout at several sizes and UI
# scales, the fullscreen shortcut, the window state, the Port Options window.
if(NOT EMSCRIPTEN)
    # tests/display_test: the real game headless, the window resized through
    # SDL and the Port Options window driven by clicks (PNGs).
    add_executable(display_test tests/display_test.c)
    target_link_libraries(display_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(display_test PRIVATE -Wall -Wextra)
    endif()
endif()

# The debug window (docs/ui.md, 0x457b01): opened by --debug-window.
if(NOT EMSCRIPTEN)
    # tests/debugwin_test: the real game headless with --debug-window: the
    # window's layout, its stepping logic, the buttons through clicks, the
    # preview it draws, dragging and the close box (PNGs).
    add_executable(debugwin_test tests/debugwin_test.c)
    target_link_libraries(debugwin_test PRIVATE afterlife_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(debugwin_test PRIVATE -Wall -Wextra)
    endif()
endif()

# ctest. Tests that need no game data always run; the rest need
# -DAFTERLIFE_DATA=<folder with the original game files>.
if(NOT EMSCRIPTEN)
    enable_testing()
    set(AFTERLIFE_DATA "" CACHE PATH "Folder with the original game files, for ctest")
    set(_out "${CMAKE_BINARY_DIR}/test-out")
    file(MAKE_DIRECTORY "${_out}")
    foreach(t ui_region_test world_test stride_test input_test build_test)
        add_test(NAME ${t} COMMAND ${t})
    endforeach()
    add_test(NAME enhance_test COMMAND enhance_test "${_out}")
    if(AFTERLIFE_DATA)
        set(D "${AFTERLIFE_DATA}")
        set(S "${D}/ASCEN001.ASC" "${D}/ASCEN002.ASC" "${D}/ASCEN003.ASC" "${D}/ASCEN004.ASC")
        add_test(NAME savegame_test COMMAND savegame_test ${S})
        add_test(NAME souls_test COMMAND souls_test ${S})
        add_test(NAME sim_test COMMAND sim_test "${D}" ${S})
        add_test(NAME karma_test COMMAND karma_test "${D}" ${S})
        add_test(NAME planet_test COMMAND planet_test "${D}" ${S})
        add_test(NAME badthings_test COMMAND badthings_test "${D}" - ${S})
        add_test(NAME map_render COMMAND map_render "${D}" MEDIUM.000 "${_out}" ${S})
        add_test(NAME views_test COMMAND views_test "${D}" MEDIUM.000 "${_out}" ${S})
        add_test(NAME views2_test COMMAND views2_test "${D}" MEDIUM.000,FULL.000 "${_out}" ${S})
        add_test(NAME planetview_test COMMAND planetview_test "${D}" MEDIUM.000 "${D}/ASCEN001.ASC" "${_out}")
        add_test(NAME advisors_test COMMAND advisors_test "${D}" MEDIUM.000 "${D}/ASCEN001.ASC" "${_out}")
        add_test(NAME news_test COMMAND news_test "${D}" MEDIUM.000 "${D}/ASCEN001.ASC" "${_out}")
        add_test(NAME akos_test COMMAND akos_test "${D}" FULL.000 "${_out}")
        add_test(NAME gfx_test COMMAND gfx_test "${D}" MEDIUM.000 "${_out}/gfx.ppm")
        add_test(NAME ui_test COMMAND ui_test "${D}" MEDIUM.000 "${_out}/ui.ppm")
        add_test(NAME ui_widgets_test COMMAND ui_widgets_test "${D}" MEDIUM.000 "${_out}/widgets")
        add_test(NAME title_test COMMAND title_test "${D}" MEDIUM.000 ALIFE.000 "${_out}/title")
        add_test(NAME imuse_test COMMAND imuse_test "${D}" "${_out}")
        add_test(NAME shell_test COMMAND shell_test "${D}" "${_out}/shell")
        add_test(NAME tutorial_test COMMAND tutorial_test "${D}" "${_out}/tutorial")
        add_test(NAME display_test COMMAND display_test "${D}" "${_out}/display")
        add_test(NAME debugwin_test COMMAND debugwin_test "${D}" "${_out}/debugwin")
        # The two that cover gated behaviour again, with the enhancements on.
        add_test(NAME badthings_test_enh COMMAND badthings_test "${D}" - ${S})
        add_test(NAME tutorial_test_enh COMMAND tutorial_test "${D}" "${_out}/tutorial_enh")
        set_tests_properties(badthings_test_enh tutorial_test_enh PROPERTIES ENVIRONMENT AFTERLIFE_ENHANCEMENTS=1)
        file(MAKE_DIRECTORY "${_out}/shell" "${_out}/tutorial" "${_out}/tutorial_enh" "${_out}/display"
            "${_out}/debugwin")
    endif()
endif()
