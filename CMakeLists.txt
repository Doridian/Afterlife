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
    find_package(SDL3 CONFIG QUIET)
    if(NOT SDL3_FOUND)
        message(STATUS "System SDL3 not found, fetching it")
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
    # The game keeps its original blocking main loop, so we need ASYNCIFY to
    # yield to the browser from plat_yield()/plat_delay_ms().
    set(AFTERLIFE_WASM_DATA "" CACHE PATH
        "Directory of game files to bundle into the web build (e.g. a dir holding only MEDIUM.000)")
    set_target_properties(afterlife PROPERTIES SUFFIX ".html")
    target_link_options(afterlife PRIVATE -sASYNCIFY -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=64MB)
    if(AFTERLIFE_WASM_DATA)
        target_link_options(afterlife PRIVATE "--preload-file=${AFTERLIFE_WASM_DATA}@/orig")
    endif()
endif()

# Startup / INI (docs/startup.md).
target_sources(afterlife_core PRIVATE src/game/ini.c src/game/config.c src/game/startup.c)
if(NOT EMSCRIPTEN)
    # tests/ini_dump: prints what the init sequence reads from an ALIFE.INI.
    add_executable(ini_dump tests/ini_dump.c)
    target_link_libraries(ini_dump PRIVATE afterlife_core)
endif()

# Video core (docs/rendering.md).
target_sources(afterlife_core PRIVATE src/gfx/surface.c src/gfx/palette.c src/gfx/draw.c src/gfx/font.c
    src/gfx/mouse.c src/gfx/exe_res.c)
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
# about box and credits.
target_sources(afterlife_core PRIVATE src/game/shell.c src/ui/title.c src/ui/advisors.c src/ui/news.c
    src/ui/about.c)

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
# (soultrack.c, listed above too) and the hook wiring; texts come from the
# user's ALIFE.EXE (exe_text.c).
target_sources(afterlife_core PRIVATE src/ui/exe_text.c src/ui/microview.c src/ui/microview_info.c src/ui/macro.c
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
