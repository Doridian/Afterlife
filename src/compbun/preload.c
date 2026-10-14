/*
 * compbun preload directory (compbun:source:preload.c, 0x4e32f0-0x4e3700).
 */
#include "compbun/preload.h"

#include <ctype.h>
#include <string.h>

#include "game/vh_mem.h"

/* 0x567ac0 preloadDirectory[100] */
static PreloadEntry g_preload_directory[PRELOAD_MAX_ENTRIES];

/* 0x677038: codec id by (class, 2-bit selector). Column 0 is always codec 0. */
static const uint8_t k_preload_class_codecs[3][4] = {
    {0, 1, 2, 3},
    {0, 4, 5, 6},
    {0, 10, 11, 12},
};

/* 0x4e32f0. Also registers the module ("compbun:source:preload.c"). */
void preload_init(void)
{
    memset(g_preload_directory, 0, sizeof(g_preload_directory));
}

/* 0x4e3330 */
void preload_shutdown(void)
{
    for (int i = 0; i < PRELOAD_MAX_ENTRIES; i++) {
        if (g_preload_directory[i].filename[0]) {
            VH_FREE(g_preload_directory[i].sectors);
            g_preload_directory[i].sectors = NULL;
        }
    }
}

static int name_equal_ci(const char *a, const char *b, size_t max)
{
    for (size_t i = 0; i < max; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return 0;
        if (!a[i])
            return 1;
    }
    return a[max] == '\0';
}

/* 0x4e3510: index of `filename` (case-insensitive), -1 if absent. */
int preload_find(const char *filename)
{
    for (int i = 0; i < PRELOAD_MAX_ENTRIES; i++) {
        const PreloadEntry *e = &g_preload_directory[i];
        if (e->filename[0] && name_equal_ci(filename, e->filename, sizeof(e->filename)))
            return i;
    }
    return -1;
}

/* 0x4e3560: unpack the u16 sector table into blocks. Data starts right after
 * where the COMP header and block table would be, (n + 1) * 16. */
static void preload_build_block_table(int index, CmprsFile *file)
{
    const PreloadEntry *e = &g_preload_directory[index];
    uint32_t offset = (uint32_t)(e->num_sectors + 1) * 16;
    for (int i = 0; i < e->num_sectors; i++) {
        uint16_t s = e->sectors[i];
        CmprsBlock *b = &file->blocks[i];
        b->offset = offset;
        b->size = (uint32_t)(s & 0x1fff) + 1;
        b->codec = e->codec_class < 3 ? k_preload_class_codecs[e->codec_class][(s & 0x6000) >> 13] : 0;
        offset += b->size;
    }
}

/* 0x4e3480: fill a Compression Layer file from the directory instead of the
 * COMP header. */
void preload_open_entry(int index, CmprsFile *file)
{
    const PreloadEntry *e = &g_preload_directory[index];
    file->num_blocks = e->num_sectors;
    file->block_size = e->sector_size;
    file->block_shift = e->sector_shift;
    file->last_block_size = e->last_sector_size;
    file->blocks = VH_ALLOC((size_t)(e->num_sectors > 0 ? e->num_sectors : 0) * sizeof(CmprsBlock));
    preload_build_block_table(index, file);
    file->cached_block = -1;
    file->total_size = (long)(file->num_blocks - 1) * file->block_size + file->last_block_size;
    file->cache = VH_ALLOC(CMPRS_CACHE_SIZE);
}

/* 0x4e35e0: big-endian u32 from the layer below (bswap32, 0x4e3610, in the
 * original; here assembled byte by byte). Short reads give 0. */
static uint32_t preload_read_be32(CbHandle h)
{
    uint8_t b[4] = {0};
    cbres_lower_read(b, 4, 1, h);
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}

/* 0x4e3640: 0 and the header fields if the stream starts with 'COMP',
 * otherwise 1 (4 bytes consumed either way). */
int preload_read_comp_header(CbHandle h, CmprsFile *file)
{
    if (preload_read_be32(h) != 0x434f4d50u) /* 'COMP' */
        return 1;
    file->num_blocks = (int)preload_read_be32(h);
    file->block_shift = (int)preload_read_be32(h);
    file->block_size = (int)(1u << (file->block_shift & 31));
    file->last_block_size = (int)preload_read_be32(h);
    return 0;
}
