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
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(t afterlife_core afterlife compbun_dump)
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
