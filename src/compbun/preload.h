/*
 * compbun preload directory (compbun:source:preload.c).
 *
 * preloadDirectory[] is meant to hold the block layout of compressed files in
 * memory (filename, numSectors, sectorSize, sectorShift, lastSectorSize, a
 * codec class and a packed u16-per-sector table), so the Compression Layer
 * can open them without reading the COMP header and block table from disk,
 * which is slow on CD. In ALIFE.EXE the directory lives in .bss and nothing
 * ever writes it except preload_init's memset, so preload_find always
 * returns -1. The read helpers here are what the Compression Layer actually
 * uses to parse COMP headers.
 */
#ifndef AFTERLIFE_COMPBUN_PRELOAD_H
#define AFTERLIFE_COMPBUN_PRELOAD_H

#include <stdint.h>

#include "compbun/cbres.h"
#include "compbun/cmprsmgr.h"

#define PRELOAD_MAX_ENTRIES 100

/* preloadDirectory[] entry, 0x28 bytes in the original. */
typedef struct {
    char filename[12];        /* +0x00, empty = unused */
    char unknown_c;           /* +0x0c, dumped as a char */
    int32_t num_sectors;      /* +0x10 */
    int32_t sector_size;      /* +0x14 */
    int32_t sector_shift;     /* +0x18 */
    int32_t last_sector_size; /* +0x1c */
    uint8_t codec_class;      /* +0x20, row of preload_class_codecs */
    /* +0x24: per sector, bits 0-12 = compressed size - 1, bits 13-14 =
     * column of preload_class_codecs */
    uint16_t *sectors;
} PreloadEntry;

void preload_init(void);
void preload_shutdown(void);
int preload_find(const char *filename);
void preload_open_entry(int index, CmprsFile *file);
int preload_read_comp_header(CbHandle h, CmprsFile *file);

#endif
