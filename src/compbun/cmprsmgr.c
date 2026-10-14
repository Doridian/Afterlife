/*
 * compbun compression manager (compbun:source:cmprsmgr.c, 0x4daa60-0x4db430).
 * See cmprsmgr.h for the COMP format.
 */
#include "compbun/cmprsmgr.h"

#include <string.h>

#include "compbun/codec.h"
#include "compbun/preload.h"
#include "game/vh_mem.h"

#define CMPRS_NUM_FILE_LIST_ENTRIES 100

static int g_cmp_initialized;            /* 0x675bd8 */
static unsigned g_num_file_list_entries; /* 0x55ed30 */
static uint8_t *g_read_buffer;           /* 0x55ed28 */
static uint8_t *g_decompress_buffer;     /* 0x55ed2c */
static CmprsFile *g_file_list;           /* 0x647998 */

/* Handles are fileList index + 1. */
static CmprsFile *file_of(CbHandle h)
{
    return &g_file_list[h - 1];
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* 0x4daa60. The original also registers the module. After preload_init it
 * calls codec_init and registers codecs 0-6 and 10-12; that part is built
 * only when codec.c is (COMPBUN_HAVE_CODECS, set by CMake). */
int cmprs_init(void)
{
    g_cmp_initialized = 1;
    g_num_file_list_entries = CMPRS_NUM_FILE_LIST_ENTRIES;
    g_read_buffer = VH_ALLOC(CMPRS_BUFFER_SIZE);
    g_decompress_buffer = VH_ALLOC(CMPRS_BUFFER_SIZE);
    g_file_list = VH_ALLOC(g_num_file_list_entries * sizeof(*g_file_list));
    memset(g_file_list, 0, g_num_file_list_entries * sizeof(*g_file_list));
    preload_init();
#ifdef COMPBUN_HAVE_CODECS
    codec_init();
    codec_register_builtin();
#endif
    return 1;
}

/* 0x4daba0 */
void cmprs_shutdown(void)
{
    if (!g_cmp_initialized)
        return;
    g_cmp_initialized = 0;
    for (unsigned i = 0; i < g_num_file_list_entries; i++) {
        VH_FREE(g_file_list[i].cache);
        g_file_list[i].cache = NULL;
        VH_FREE(g_file_list[i].blocks);
        g_file_list[i].blocks = NULL;
    }
    VH_FREE(g_file_list);
    g_file_list = NULL;
    VH_FREE(g_read_buffer);
    g_read_buffer = NULL;
    VH_FREE(g_decompress_buffer);
    g_decompress_buffer = NULL;
}

/* Reads the block table after a COMP header (part of cmprs_open). Returns the
 * number of bytes consumed so far, header included. */
static long read_block_table(CmprsFile *f)
{
    int n = f->num_blocks > 0 ? f->num_blocks : 0;
    uint8_t *raw = VH_ALLOC((size_t)n * 16);
    memset(raw, 0, (size_t)n * 16);
    f->blocks = VH_ALLOC((size_t)n * sizeof(CmprsBlock));
    cbres_lower_read(raw, 16, n, f->file);
    for (int i = 0; i < n; i++) {
        f->blocks[i].offset = be32(raw + i * 16);
        f->blocks[i].size = be32(raw + i * 16 + 4);
        f->blocks[i].codec = be32(raw + i * 16 + 8);
    }
    VH_FREE(raw);
    f->total_size = (long)(f->num_blocks - 1) * f->block_size + f->last_block_size;
    f->cached_block = -1;
    f->cache = VH_ALLOC(CMPRS_CACHE_SIZE);
    return (long)(n + 1) * 16;
}

/* 0x4dad10. Opens the stream below, then checks for a preload entry or a COMP
 * header. Returns index + 1, 0 on failure. */
static CbHandle cmprs_open(const char *name, const char *mode)
{
    if (!g_cmp_initialized)
        return cbres_lower_open(name, mode);

    /* First free slot. The original doesn't bound this search. */
    unsigned slot = 0;
    while (slot < g_num_file_list_entries && g_file_list[slot].in_use)
        slot++;
    if (slot == g_num_file_list_entries)
        return 0;
    CmprsFile *f = &g_file_list[slot];

    f->file = cbres_lower_open(name, mode);
    if (!f->file)
        return 0;
    f->in_use = 1;
    f->pos = 0;

    int pre = preload_find(name);
    if (pre != -1) {
        /* Dead in Afterlife: the preload directory is always empty. The
         * original leaves `base` stale here; use the current position. */
        f->compressed = 1;
        preload_open_entry(pre, f);
        f->base = cbres_lower_tell(f->file);
        return (CbHandle)slot + 1;
    }

    long consumed = 4;
    f->compressed = preload_read_comp_header(f->file, f) == 0;
    if (f->compressed) {
        /* Port-only sanity check: blocks must fit the fixed buffers. */
        if (f->block_size <= 0 || f->block_size > CMPRS_CACHE_SIZE || f->num_blocks < 0) {
            cbres_lower_close(f->file);
            f->in_use = 0;
            f->compressed = 0;
            return 0;
        }
        consumed = read_block_table(f);
    }
    /* Rewind to where the stream started; for plain streams that undoes the
     * 4-byte peek. */
    cbres_lower_seek(f->file, -consumed, 1);
    f->base = cbres_lower_tell(f->file);
    return (CbHandle)slot + 1;
}

/* 0x4daf90 */
static void cmprs_close(CbHandle h)
{
    if (!g_cmp_initialized) {
        cbres_lower_close(h);
        return;
    }
    CmprsFile *f = file_of(h);
    if (f->compressed) {
        VH_FREE(f->blocks);
        f->blocks = NULL;
        VH_FREE(f->cache);
        f->cache = NULL;
    }
    cbres_lower_close(f->file);
    f->in_use = 0;
}

/* 0x4db040. Plain streams answer with the lower layer's tell, not getpos. */
static int cmprs_getpos(CbHandle h, long *pos)
{
    if (!g_cmp_initialized)
        return cbres_lower_getpos(h, pos);
    CmprsFile *f = file_of(h);
    *pos = f->compressed ? f->pos : cbres_lower_tell(f->file);
    return 0;
}

/* 0x4db0a0 */
static long cmprs_tell(CbHandle h)
{
    if (!g_cmp_initialized)
        return cbres_lower_tell(h);
    CmprsFile *f = file_of(h);
    return f->compressed ? f->pos : cbres_lower_tell(f->file);
}

/* 0x4db0e0. On compressed streams SEEK_END ignores the offset. */
static int cmprs_seek(CbHandle h, long offset, int whence)
{
    if (!g_cmp_initialized)
        return cbres_lower_seek(h, offset, whence);
    CmprsFile *f = file_of(h);
    if (!f->compressed)
        return cbres_lower_seek(f->file, offset, whence);
    if (whence == 0)
        f->pos = offset;
    else if (whence == 1)
        f->pos += offset;
    else if (whence == 2)
        f->pos = f->total_size;
    return 0;
}

/* Puts block `index` into the decompress buffer, from the cache if it is the
 * cached block, else by reading and decompressing it (part of cmprs_read). */
static void load_block(CmprsFile *f, int index)
{
    if (f->cached_block == index) {
        memcpy(g_decompress_buffer, f->cache, (size_t)f->block_size);
        return;
    }
    if (index < 0 || index >= f->num_blocks || f->blocks[index].size > CMPRS_BUFFER_SIZE) {
        /* Port-only: the original reads past the block table here. */
        memset(g_decompress_buffer, 0, (size_t)f->block_size);
    } else {
        const CmprsBlock *b = &f->blocks[index];
        cbres_lower_seek(f->file, f->base + (long)b->offset, 0);
        cbres_lower_read(g_read_buffer, (long)b->size, 1, f->file);
        codec_decompress((int)b->codec, g_decompress_buffer, g_read_buffer, (int)b->size);
    }
    f->cached_block = index;
    memcpy(f->cache, g_decompress_buffer, (size_t)f->block_size);
}

/* 0x4db160. Decompresses every block the request touches and copies the
 * wanted range out. No clipping at end of stream; always returns `count`. */
static long cmprs_read(void *buf, long size, long count, CbHandle h)
{
    if (!g_cmp_initialized)
        return cbres_lower_read(buf, size, count, h);
    CmprsFile *f = file_of(h);
    if (!f->compressed)
        return cbres_lower_read(buf, size, count, f->file);

    uint8_t *dst = buf;
    long len = size * count;
    int first = (int)(f->pos >> f->block_shift);
    int last = (int)((f->pos + len - 1) >> f->block_shift);
    long in_block = f->pos % f->block_size;
    long done = 0;

    for (int i = first; i <= last; i++) {
        load_block(f, i);
        if (i == first && i == last) {
            memcpy(dst, g_decompress_buffer + in_block, (size_t)len);
        } else if (i == first) {
            done = f->block_size - in_block;
            memcpy(dst, g_decompress_buffer + in_block, (size_t)done);
        } else if (i == last) {
            memcpy(dst + done, g_decompress_buffer, (size_t)(len - done));
        } else {
            memcpy(dst + done, g_decompress_buffer, (size_t)f->block_size);
            done += f->block_size;
        }
    }
    f->pos += len;
    return count;
}

/* 0x4db3f0 */
static long cmprs_read_cb(void *buf, long size, long count, CbHandle h, CbReadCallback cb)
{
    long n = cmprs_read(buf, size, count, h);
    cb(buf, size, count, n);
    return n;
}

/* 0x675bb0 */
CbLayer cmprs_layer = {
    "Compression Layer", cmprs_init, cmprs_open,   cmprs_close, cmprs_read, cmprs_read_cb,
    cmprs_seek,          cmprs_tell, cmprs_getpos, NULL,
};
