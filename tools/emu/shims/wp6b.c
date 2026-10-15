/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * tools/emu wrappers for the WP6b views (src/ui/microview.c, macro.c,
 * soulview*.c, src/game/soultrack.c), used by tools/emu/test_wp6b.py.
 */
#include <stdint.h>
#include <string.h>

#include "game/soultrack.h"
#include "game/world.h"
#include "platform/platform.h"
#include "ui/macro.h"
#include "ui/microview.h"
#include "ui/soulview.h"

#if defined(_WIN32)
#define SHIM_API __declspec(dllexport)
#else
#define SHIM_API __attribute__((visibility("default")))
#endif

static int g_answer = 1;

static int answer_mv(const char *text, const char *buttons)
{
    (void)text, (void)buttons;
    return g_answer;
}

static int answer_macro(const char *title, const char *text, const char *buttons)
{
    (void)title, (void)text, (void)buttons;
    return g_answer;
}

/* The texts are read from ALIFE.EXE in `data_dir`; hooks off, message
 * boxes answered with shim_wp6b_answer's button. */
SHIM_API void shim_wp6b_init(const char *data_dir)
{
    plat_set_data_dir(data_dir);
    memset(&soultrack_hooks, 0, sizeof soultrack_hooks);
    memset(&macro_hooks, 0, sizeof macro_hooks);
    memset(&microview_hooks, 0, sizeof microview_hooks);
    microview_hooks.message = answer_mv;
    macro_hooks.message = answer_macro;
}

SHIM_API void shim_wp6b_answer(int button) { g_answer = button; }

SHIM_API void shim_wp6b_name(uint32_t seed, char *out) { soulview_soul_name(seed, out); }

/* the story of tracker record i (as it is in g_state) */
SHIM_API size_t shim_wp6b_describe(int i, char *out, size_t size)
{
    return soulview_describe(soultrack_rec((unsigned)i), out, size);
}

/* raw access to tracker record i in the original's 0x26-byte layout */
static void put32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v, b[1] = (uint8_t)(v >> 8), b[2] = (uint8_t)(v >> 16), b[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

SHIM_API void shim_wp6b_rec_get(int i, uint8_t *b)
{
    const Rec26 *r = soultrack_rec((unsigned)i);
    memset(b, 0, 0x26);
    memcpy(b, r->unk_00, 8);
    put32(b + 8, r->unk_08);
    put32(b + 0xc, r->unk_0c);
    b[0x10] = r->unk_10, b[0x11] = r->unk_11, b[0x12] = r->unk_12;
    put32(b + 0x13, r->unk_13);
    put32(b + 0x17, r->unk_17);
    b[0x1b] = r->unk_1b;
    put32(b + 0x1c, r->unk_1c);
    b[0x20] = (uint8_t)r->unk_20, b[0x21] = (uint8_t)(r->unk_20 >> 8);
    b[0x22] = r->unk_22, b[0x23] = r->unk_23, b[0x25] = r->unk_25;
}

SHIM_API void shim_wp6b_rec_set(int i, const uint8_t *b)
{
    Rec26 *r = soultrack_rec((unsigned)i);
    memcpy(r->unk_00, b, 8);
    r->unk_08 = get32(b + 8);
    r->unk_0c = get32(b + 0xc);
    r->unk_10 = b[0x10], r->unk_11 = b[0x11], r->unk_12 = b[0x12];
    r->unk_13 = get32(b + 0x13);
    r->unk_17 = get32(b + 0x17);
    r->unk_1b = b[0x1b];
    r->unk_1c = get32(b + 0x1c);
    r->unk_20 = (uint16_t)(b[0x20] | b[0x21] << 8);
    r->unk_22 = b[0x22], r->unk_23 = b[0x23], r->unk_25 = b[0x25];
}

SHIM_API void shim_wp6b_counts(int used, int walking, int selected)
{
    g_state.block_4f33f8.unk_664808 = (uint8_t)used;
    g_state.block_4f33f8.unk_66480c = (uint8_t)walking;
    g_state.block_4f33f8.selected = (uint8_t)selected;
}

SHIM_API void shim_wp6b_age(void) { soultrack_age(); }

SHIM_API void shim_wp6b_start(int i, int plane, int x, int y)
{
    soultrack_start(soultrack_rec((unsigned)i), plane, x, y);
}

SHIM_API int shim_wp6b_years_left(int i) { return soultrack_years_left(soultrack_rec((unsigned)i)); }

SHIM_API void shim_wp6b_rotated(void) { soultrack_rotated(); }

SHIM_API int shim_wp6b_macro(uint32_t kinds, int focus, int lock, int unlock, int auto_bal, int manual, int commit,
                             uint32_t *cost)
{
    MacroSettings s = {kinds, (uint8_t)focus, (uint8_t)lock, (uint8_t)unlock, (uint8_t)auto_bal, (uint8_t)manual};
    macro_set_settings(&s);
    int n = macro_apply(commit != 0);
    *cost = macro_cost();
    return n;
}

SHIM_API int shim_wp6b_macro_focus(int x) { return macro_slider_focus(x); }

SHIM_API void shim_wp6b_mv_target(int plane, int x, int y) { microview_target(plane, x, y); }

SHIM_API void shim_wp6b_mv_action(int act, int x) { microview_action(act, x); }

SHIM_API uint32_t shim_wp6b_mv_cost(void) { return microview_upgrade_cost(); }

SHIM_API void shim_wp6b_mv_open(int x, int y) { microview_open(x, y); }

SHIM_API void shim_wp6b_pennies(int32_t hi, uint32_t lo)
{
    g_state.pennies.hi = hi;
    g_state.pennies.lo = lo;
}

SHIM_API void shim_wp6b_year(uint32_t year) { g_state.year = year; }

#include "game/build.h"
SHIM_API int shim_wp6b_obj_57e018(void) { return g_build.obj_57e018; }
