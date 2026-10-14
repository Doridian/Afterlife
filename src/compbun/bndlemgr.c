/*
 * compbun bundle manager (compbun:source:bndlemgr.c, 0x4db430-0x4db9d0).
 */
#include "compbun/bndlemgr.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "game/vh_mem.h"
#include "platform/platform.h"

#define BNDL_MAX_BUN_FILES 2 /* fileSystem[] spans 0x55ed38-0x55ed58 */

/* One table-of-contents entry, 20 bytes in the original:
 * char name[8], char ext[3], pad, u32 offset, u32 size. */
typedef struct {
    char name[8]; /* not NUL-terminated when 8 long */
    char ext[3];
    uint32_t offset; /* of the file inside the bundle */
    uint32_t size;
} BndlTocEntry;

/* Open state of one TOC entry, 8 bytes in the original. */
typedef struct {
    int in_use;
    long pos;
} BndlOpenFile;

/* Bundle header. Only num_entries (+8) is used; the first two words are
 * unknown. */
typedef struct {
    uint32_t unknown[2];
    uint32_t num_entries;
} BndlHeader;

/* fileSystem[] entry, 16 bytes in the original (dump labels: bunFile, files,
 * an unnamed header pointer, tocData). */
typedef struct {
    PlatFile *bun_file;
    BndlOpenFile *files; /* one per TOC entry */
    BndlHeader *header;
    BndlTocEntry *toc_data; /* sorted by "NAME.EXT", case-insensitive */
} BndlFileSystem;

static BndlFileSystem g_file_system[BNDL_MAX_BUN_FILES]; /* 0x55ed38 */
static int g_num_bun_files;                               /* 0x55ee5c */
static int g_bundle_initialized;                          /* 0x675cb0 */

/* Handles are (bundle << 16 | entry) + 1. */
static BndlOpenFile *open_file(CbHandle h)
{
    return &g_file_system[h >> 16].files[(h & 0xffff) - 1];
}

static const BndlTocEntry *toc_entry(CbHandle h)
{
    return &g_file_system[h >> 16].toc_data[(h & 0xffff) - 1];
}

/* 0x4db430. Also registers the module ("compbun:source:bndlemgr.c") with the
 * VHUMOR runtime and copies an unknown word (0x675cec) to 0x55ed58. Nothing
 * mounts a bundle afterwards, so this always returns 0. */
int bndl_init(void)
{
    g_bundle_initialized = 0;
    g_num_bun_files = 0;
    return g_num_bun_files;
}

/* 0x4db480. Closes bunFile with the CRT directly, not through the stack. */
void bndl_shutdown(void)
{
    if (!g_bundle_initialized)
        return;
    for (int i = 0; i < g_num_bun_files; i++) {
        BndlFileSystem *fs = &g_file_system[i];
        plat_file_close(fs->bun_file);
        fs->bun_file = NULL;
        VH_FREE(fs->header);
        fs->header = NULL;
        VH_FREE(fs->files);
        fs->files = NULL;
        VH_FREE(fs->toc_data);
        fs->toc_data = NULL;
    }
}

/* 0x4db910: bsearch comparator. Rebuilds "NAME.EXT" from the entry (strncpy
 * of 8, ".", strncat of 3) and compares case-insensitively (_strcmpi). */
static size_t field_len(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n])
        n++;
    return n;
}

static int bndl_compare_entry(const void *key, const void *elem)
{
    const BndlTocEntry *e = elem;
    char full[80] = {0};
    size_t n = field_len(e->name, sizeof(e->name));
    memcpy(full, e->name, n);
    full[n++] = '.';
    memcpy(full + n, e->ext, field_len(e->ext, sizeof(e->ext)));

    const unsigned char *a = key, *b = (const unsigned char *)full;
    for (;; a++, b++) {
        int d = tolower(*a) - tolower(*b);
        if (d || !*a)
            return d;
    }
}

/* 0x4db890: (bundle << 16 | entry) of the first bundle holding `name`, or -1.
 * (The original reads an uninitialized result when numBunFiles is 0.) */
int bndl_find_entry(const char *name)
{
    for (int i = 0; i < g_num_bun_files; i++) {
        const BndlFileSystem *fs = &g_file_system[i];
        const BndlTocEntry *hit =
            bsearch(name, fs->toc_data, fs->header->num_entries, sizeof(*fs->toc_data), bndl_compare_entry);
        if (hit)
            return (int)(hit - fs->toc_data) + i * 0x10000;
    }
    return -1;
}

/* 0x4db5f0. Unlike every other slot this does not fall through to the layer
 * below when the bundle manager isn't initialized; it just fails. Pushing
 * this layer without mounting a bundle would break every open. */
static CbHandle bndl_open(const char *name, const char *mode)
{
    (void)mode;
    if (!g_bundle_initialized)
        return 0;
    int found = bndl_find_entry(name);
    if (found == -1)
        return 0;
    CbHandle h = (CbHandle)found + 1;
    open_file(h)->in_use = 1;
    open_file(h)->pos = 0;
    return h;
}

/* 0x4db650 */
static void bndl_close(CbHandle h)
{
    if (g_bundle_initialized) {
        open_file(h)->in_use = 0;
        return;
    }
    cbres_lower_close(h);
}

/* 0x4db690 */
static int bndl_getpos(CbHandle h, long *pos)
{
    if (g_bundle_initialized) {
        *pos = open_file(h)->pos;
        return 0;
    }
    return cbres_lower_getpos(h, pos);
}

/* 0x4db6f0 */
static long bndl_tell(CbHandle h)
{
    if (g_bundle_initialized)
        return open_file(h)->pos;
    return cbres_lower_tell(h);
}

/* 0x4db730. SEEK_END is relative to the TOC size. No bounds checks. */
static int bndl_seek(CbHandle h, long offset, int whence)
{
    if (!g_bundle_initialized)
        return cbres_lower_seek(h, offset, whence);
    if (whence == 0)
        open_file(h)->pos = offset;
    else if (whence == 1)
        open_file(h)->pos += offset;
    else if (whence == 2)
        open_file(h)->pos = (long)toc_entry(h)->size + offset;
    return 0;
}

/* 0x4db7f0. Seeks the bundle file with the CRT directly, then reads through
 * the lower layer. Reads aren't clipped to the entry size. */
static long bndl_read(void *buf, long size, long count, CbHandle h)
{
    if (!g_bundle_initialized)
        return cbres_lower_read(buf, size, count, h);
    PlatFile *f = g_file_system[h >> 16].bun_file;
    plat_file_seek(f, (long)toc_entry(h)->offset + open_file(h)->pos, 0);
    long n = cbres_lower_read(buf, size, count, (CbHandle)f);
    open_file(h)->pos += n * size;
    return n;
}

/* 0x4db9a0 */
static long bndl_read_cb(void *buf, long size, long count, CbHandle h, CbReadCallback cb)
{
    long n = bndl_read(buf, size, count, h);
    cb(buf, size, count, n);
    return n;
}

/* 0x675c88 */
CbLayer bndl_layer = {
    "Bundling layer", bndl_init, bndl_open,   bndl_close, bndl_read, bndl_read_cb,
    bndl_seek,        bndl_tell, bndl_getpos, NULL,
};
