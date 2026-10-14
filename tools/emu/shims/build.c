/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * tools/emu wrappers for the build tools (WP2, src/game/build.c,
 * newworld.c). Used by tools/emu/test_build.py.
 */
#include <stdint.h>

#include "game/build.h"
#include "game/world.h"

#if defined(_WIN32)
#define SHIM_API __declspec(dllexport)
#else
#define SHIM_API __attribute__((visibility("default")))
#endif

SHIM_API void shim_set_difficulty(int d) { g_state.hdr.difficulty = (uint16_t)d; }

/* MapView +0x48 per plane, as the emulated original has them */
SHIM_API void shim_build_view_flags(int plane, int flags) { g_build.view_flags[plane & 3] = (uint8_t)flags; }

SHIM_API void shim_set_pennies(uint32_t lo) { g_state.pennies.hi = 0, g_state.pennies.lo = lo; }
