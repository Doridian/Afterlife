/*
 * Shim for the VHUMOR memory manager (C:\VHUMOR\source\mem.c). The original
 * vh_mem_alloc / vh_mem_free track every block with the caller's file and line
 * (pools, leak reports, "vh_MemFree: Block not found"). That runtime isn't
 * decompiled yet, so this maps straight onto malloc/free and drops the
 * bookkeeping. Callers keep passing __FILE__/__LINE__ so the real thing can be
 * dropped in later without touching them.
 */
#ifndef AFTERLIFE_GAME_VH_MEM_H
#define AFTERLIFE_GAME_VH_MEM_H

#include <stdlib.h>

/* 0x4d8230 */
static inline void *vh_mem_alloc(size_t size, const char *file, int line)
{
    (void)file;
    (void)line;
    return malloc(size ? size : 1);
}

/* 0x4d8410 */
static inline void vh_mem_free(void *ptr, const char *file, int line)
{
    (void)file;
    (void)line;
    free(ptr);
}

#define VH_ALLOC(size) vh_mem_alloc((size), __FILE__, __LINE__)
#define VH_FREE(ptr) vh_mem_free((ptr), __FILE__, __LINE__)

#endif
