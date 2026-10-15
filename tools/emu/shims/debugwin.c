/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * tools/emu wrappers for the debug window (ui/debugwin.h): its state as
 * plain ints and the editor flag its close box clears, for
 * tools/emu/test_debugwin.py.
 */
#include <stdint.h>

#include "game/build.h"
#include "ui/debugwin.h"

#if defined(_WIN32)
#define SHIM_API __declspec(dllexport)
#else
#define SHIM_API __attribute__((visibility("default")))
#endif

/* min | max << 8 | set << 16 | index << 24, as the bytes 0x65edd0/d4/d8/dc */
SHIM_API uint32_t shim_debugwin_get(void)
{
    DebugwinState s = debugwin_state();
    return (uint32_t)(uint8_t)s.index_min | (uint32_t)(uint8_t)s.index_max << 8 | (uint32_t)(uint8_t)s.set << 16 |
           (uint32_t)(uint8_t)s.index << 24;
}

SHIM_API void shim_debugwin_set(int min, int max, int set, int index)
{
    debugwin_set_state((DebugwinState){(int8_t)min, (int8_t)max, (int8_t)set, (int8_t)index});
}

SHIM_API int shim_debugwin_editor(int set)
{
    if (set >= 0)
        g_build.editor = (uint8_t)set;
    return g_build.editor;
}

/* debugwin_proc as a button's message (no window: nothing is drawn) */
SHIM_API void shim_debugwin_msg(int msg, int id)
{
    debugwin_proc(NULL, (unsigned)msg, id, 0);
}
