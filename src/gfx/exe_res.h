/*
 * Port-only: the image data the original kept inside ALIFE.EXE, read from the
 * user's copy at runtime: the PE resources (cursors, the icon) by type/id,
 * and the built-in font by its virtual address. A minimal PE32 reader; the
 * EXE's texts and number tables are ordinary C data in the modules that use
 * them.
 */
#ifndef AFTERLIFE_GFX_EXE_RES_H
#define AFTERLIFE_GFX_EXE_RES_H

#include <stdbool.h>
#include <stdint.h>

#define EXE_RT_CURSOR 1
#define EXE_RT_GROUP_CURSOR 12

/* Loads `name` (looked up in the data dir) once; later calls are no-ops.
 * Returns false if it isn't there or isn't a PE image. */
bool exe_res_open(const char *name);
void exe_res_close(void);

/* Data of resource type/id (first language) from the loaded EXE, or NULL. */
const uint8_t *exe_res_find(uint32_t type, uint32_t id, uint32_t *size);
/* The bytes at virtual address `va` of the loaded EXE, with *avail bytes
 * readable up to the end of its section's file data; NULL if the EXE isn't
 * loaded or `va` isn't in a section's file data. */
const uint8_t *exe_res_va(uint32_t va, uint32_t *avail);

#endif
