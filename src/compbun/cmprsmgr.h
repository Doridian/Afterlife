/*
 * compbun compression manager, "Compression Layer" (compbun:source:cmprsmgr.c).
 *
 * On open it peeks at the first 4 bytes of the stream below. If they are the
 * big-endian tag 'COMP', the stream is a block-compressed file:
 *
 *     +0   "COMP"
 *     +4   u32 BE  numBlocks
 *     +8   u32 BE  blockShift         block size = 1 << blockShift
 *     +12  u32 BE  lastBlockSize      uncompressed size of the last block
 *     +16  numBlocks x { u32 BE offset (from "COMP"), u32 BE size,
 *                        u32 BE codec, u32 unused }
 *
 * and reads decompress one block at a time through codec_decompress().
 * Anything else is passed through untouched. (No Afterlife resource starts
 * with 'COMP', so in practice every stream is passed through.)
 */
#ifndef AFTERLIFE_COMPBUN_CMPRSMGR_H
#define AFTERLIFE_COMPBUN_CMPRSMGR_H

#include <stdint.h>

#include "compbun/cbres.h"

#define CMPRS_BUFFER_SIZE 0x2200 /* read/decompress buffers */
#define CMPRS_CACHE_SIZE 0x2000  /* per-file cache of the last block */

/* One compressed block (12 bytes in memory, 16 on disk). */
typedef struct {
    uint32_t offset; /* from the start of the COMP header */
    uint32_t size;   /* compressed size */
    uint32_t codec;  /* codecList[] index */
} CmprsBlock;

/* fileList[] entry, 0x34 bytes in the original; offsets in comments. */
typedef struct {
    int in_use;          /* +0x00 */
    int compressed;      /* +0x04 */
    CbHandle file;       /* +0x08 handle of the layer below */
    long pos;            /* +0x0c position in the uncompressed stream */
    long total_size;     /* +0x10 uncompressed size */
    int num_blocks;      /* +0x14 */
    int block_size;      /* +0x18 1 << block_shift */
    int block_shift;     /* +0x1c */
    int last_block_size; /* +0x20 */
    CmprsBlock *blocks;  /* +0x24 */
    int cached_block;    /* +0x28 index of the block in cache, -1 if none */
    uint8_t *cache;      /* +0x2c */
    long base;           /* +0x30 lower-stream offset of the COMP header */
} CmprsFile;

extern CbLayer cmprs_layer;

int cmprs_init(void);
void cmprs_shutdown(void);

#endif
