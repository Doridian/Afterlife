/*
 * Port-only: reads data the original kept inside ALIFE.EXE (cursor
 * resources, the built-in font) from the user's own copy at runtime, so none
 * of it has to live in the repo. Minimal PE32 reader: section mapping and
 * the resource directory.
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

/* Data of resource type/id (first language), or NULL. */
const uint8_t *exe_res_find(uint32_t type, uint32_t id, uint32_t *size);
/* Bytes at virtual address `va` of the image, with *avail bytes readable
 * up to the end of its section's file data. NULL if not mapped. */
const uint8_t *exe_res_va(uint32_t va, uint32_t *avail);

#endif
