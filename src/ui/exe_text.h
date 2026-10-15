/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Texts and small tables the views read from the user's own ALIFE.EXE at
 * runtime, by their original virtual addresses (gfx/exe_res.h), so that none
 * of the game's text has to be in the repo. Used by the Microview, the
 * Macromanager and the Soulview (WP6b).
 *
 * Everything degrades to "" / 0 without the EXE.
 */
#ifndef AFTERLIFE_UI_EXE_TEXT_H
#define AFTERLIFE_UI_EXE_TEXT_H

#include <stddef.h>
#include <stdint.h>

/* The NUL-terminated string at `va`, or "" (also for va 0). */
const char *exe_str(uint32_t va);
/* The string the 32-bit pointer at `va` points to. */
const char *exe_strp(uint32_t va);
/* The string pointed to by entry i of a pointer table at `va`. */
const char *exe_stri(uint32_t va, unsigned i);
/* A format string from the EXE, used only if its conversions are exactly
 * `spec` (one letter per conversion: d, s, x, f, c; "%%" ignored; flags and
 * widths allowed); otherwise `fallback`. */
const char *exe_fmt(uint32_t va, const char *spec, const char *fallback);
/* A message box button list ("&Yes\0~&No\0") copied from `va` with the
 * terminating second NUL: the strings at va, va + strlen + 1, ... up to
 * `n` of them. */
const char *exe_buttons(uint32_t va, int n, char *buf, size_t size);
/* Raw little-endian values (0 if unmapped). */
uint8_t exe_u8(uint32_t va);
uint32_t exe_u32(uint32_t va);

#endif
