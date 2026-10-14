/*
 * Afterlife resource file manager. See resource.h.
 */
#include "game/resource.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compbun/cbres.h"

/* Chunk tags as the original compares them: the first 4 bytes of the chunk
 * read as a little-endian u32. */
#define TAG_JIFF 0x4a494646u
#define TAG_HEAD 0x48454144u
#define TAG_RESC 0x52455343u

#define RC_TYPE_REMOTE 1 /* body holds the path of a separate file */
#define RC_TYPE_LOCAL 2  /* body holds the data */

static char g_cd_path[0x200];       /* 0x64d810 "CD Path" */
static char g_resource_file[0x100]; /* 0x64da10 "Resource file" */
static char g_remote_name[0x104];   /* 0x546cc0 */
static RcIndex g_cd_index;          /* 0x6735b0 offsets, 0x6735b4 ids, 0x6735b8 count */
static RcIndex g_local_index;       /* 0x6735bc offsets, 0x6735c0 ids, 0x6735c4 count */

/* Fatal error box in the original ("HILEVEL ERROR ..."). */
static void rc_error(const char *title, const char *msg)
{
    fprintf(stderr, "%s: %s\n", title, msg);
}

/* --- file helpers (0x4d202d-0x4d22d0) ------------------------------------ */

/* 0x4d202d: mode 1 = "rb" (2 and 3 are write modes, unused here) */
static PlatFile *file_open(const char *name)
{
    return name ? plat_file_open(name) : NULL;
}

/* 0x4d20d2 */
static void file_close(PlatFile *f)
{
    if (f)
        plat_file_close(f);
}

/* 0x4d2117: returns bytes read. The original pumps window messages first and
 * offers a retry/abort box on short reads ("File I/O Error"). */
static long file_read(void *buf, long len, PlatFile *f)
{
    if (len <= 0)
        return 0;
    return (long)plat_file_read(f, buf, 1, (size_t)len);
}

/* 0x4d20f3 */
static int file_seek(PlatFile *f, long offset, int whence)
{
    return plat_file_seek(f, offset, whence);
}

/* 0x4d2220 */
static long file_tell(PlatFile *f)
{
    return plat_file_tell(f);
}

/* 0x4d22b6: little-endian u32. Short reads give 0 here (garbage originally). */
static uint32_t file_read_u32(PlatFile *f)
{
    uint8_t b[4] = {0};
    file_read(b, 4, f);
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

/* 0x4d223c */
static long file_length(PlatFile *f)
{
    long here = file_tell(f);
    file_seek(f, 0, 2);
    long len = file_tell(f);
    file_seek(f, here, 0);
    return len;
}

/* --- index ---------------------------------------------------------------- */

static bool read_u32_table(PlatFile *f, uint32_t *out, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        uint8_t b[4];
        if (file_read(b, 4, f) != 4)
            return false;
        out[i] = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    }
    return true;
}

/* 0x4ce706: reads HEAD, OFST and RCID. Assumes the fixed layout JIFF, HEAD
 * (length 4), OFST, RCID: only the JIFF and HEAD tags are checked, and the
 * OFST/RCID headers are skipped blind. */
static bool rc_load_index(const char *path, RcIndex *index)
{
    PlatFile *f = file_open(path);
    if (!f)
        return false;
    bool ok = false;
    if (file_read_u32(f) == TAG_JIFF) {
        file_read_u32(f);
        if (file_read_u32(f) == TAG_HEAD) {
            file_read_u32(f);
            uint32_t count = file_read_u32(f);
            uint32_t *ids = count ? malloc((size_t)count * 4) : NULL;
            uint32_t *offsets = count ? malloc((size_t)count * 4) : NULL;
            if (ids && offsets) {
                file_seek(f, 8, 1);
                ok = read_u32_table(f, offsets, count);
                file_seek(f, 8, 1);
                ok = ok && read_u32_table(f, ids, count);
            }
            if (ok) {
                index->ids = ids;
                index->offsets = offsets;
                index->count = count;
            } else {
                if (count && (!ids || !offsets))
                    rc_error("HILEVEL ERROR RcInitialize", "Unable to allocate index.");
                free(ids);
                free(offsets);
            }
        }
    }
    file_close(f);
    return ok;
}

/* 0x4cd9b0 */
bool rc_initialize(const char *resource_file, const char *cd_path)
{
    snprintf(g_cd_path, sizeof(g_cd_path), "%s", cd_path ? cd_path : "");
    snprintf(g_resource_file, sizeof(g_resource_file), "%s", resource_file ? resource_file : "");
    bool cd_ok = rc_load_index(g_cd_path, &g_cd_index);
    if (!cd_ok)
        rc_error("Where oh where is the Afterlife CD?", "Please insert the Afterlife CD in the drive.");
    rc_load_index(g_resource_file, &g_local_index);
    return cd_ok;
}

void rc_shutdown(void)
{
    free(g_cd_index.ids);
    free(g_cd_index.offsets);
    free(g_local_index.ids);
    free(g_local_index.offsets);
    memset(&g_cd_index, 0, sizeof(g_cd_index));
    memset(&g_local_index, 0, sizeof(g_local_index));
}

const RcIndex *rc_local_index(void)
{
    return &g_local_index;
}

const RcIndex *rc_cd_index(void)
{
    return &g_cd_index;
}

/* Absolute offset of the RESC chunk of the first entry with `id`, 0 if none.
 * OFST values are relative to the first RESC chunk, which follows the fixed
 * 0x24 + 8 * count byte header (JIFF 8, HEAD 12, OFST and RCID 8 + 4n each).
 * A linear scan, so for a duplicated ID (760 in ALIFE.000) the first wins. */
static long find_in(const RcIndex *index, uint32_t id)
{
    if (!index->ids || !index->offsets)
        return 0;
    for (uint32_t i = 0; i < index->count; i++)
        if (index->ids[i] == id)
            return (long)index->offsets[i] + 0x24 + (long)index->count * 8;
    return 0;
}

/* 0x4cdf6c */
static long rc_find_local(uint32_t id)
{
    return find_in(&g_local_index, id);
}

/* 0x4cdff4 */
static long rc_find_cd(uint32_t id)
{
    return find_in(&g_cd_index, id);
}

/* Opens the bundle holding `id` (local first), positioned on its RESC chunk.
 * Shared start of rc_open_resource and rc_resource_size. */
static PlatFile *open_bundle_at(uint32_t id, bool *found)
{
    long offset = rc_find_local(id);
    const char *path = g_resource_file;
    if (!offset) {
        offset = rc_find_cd(id);
        path = g_cd_path;
    }
    *found = offset != 0;
    if (!offset)
        return NULL;
    PlatFile *f = file_open(path);
    if (f)
        file_seek(f, offset, 0);
    return f;
}

/* Reads a remote file name of `len` bytes into g_remote_name. */
static void read_remote_name(PlatFile *f, long len)
{
    memset(g_remote_name, 0, sizeof(g_remote_name));
    if (len > (long)sizeof(g_remote_name) - 1)
        len = (long)sizeof(g_remote_name) - 1;
    file_read(g_remote_name, len, f);
}

/* 0x4cda8e. Every call opens the bundle file again. The returned stream isn't
 * bounded: callers read rc_resource_size() bytes. */
PlatFile *rc_open_resource(uint32_t id)
{
    char msg[300];
    bool found;
    PlatFile *f = open_bundle_at(id, &found);
    if (!found) {
        snprintf(msg, sizeof(msg), "Unable to locate resource ID %u", (unsigned)id);
        rc_error("HILEVEL ERROR RcLoadResource", msg);
        return NULL;
    }
    if (!f) {
        /* The original names the local file here even for the CD. */
        snprintf(msg, sizeof(msg), "Unable to open resource file %s", g_resource_file);
        rc_error("HILEVEL ERROR RcLoadResource", msg);
        return NULL;
    }
    uint32_t tag = file_read_u32(f);
    file_read_u32(f); /* RESC chunk length: never used */
    if (tag != TAG_RESC) {
        file_close(f);
        snprintf(msg, sizeof(msg), "ID %u has invalid offset in resource file %s", (unsigned)id, g_resource_file);
        rc_error("HILEVEL ERROR RcLoadResource", msg);
        return NULL;
    }
    uint32_t type = file_read_u32(f);
    uint32_t size = file_read_u32(f);
    if (type == RC_TYPE_REMOTE) {
        read_remote_name(f, (long)size - 8);
        file_close(f);
        f = file_open(g_remote_name);
        if (!f) {
            snprintf(msg, sizeof(msg), "Unable to open remote file %s", g_remote_name);
            rc_error("HILEVEL ERROR RcOpenResource", msg);
        }
    } else if (type == RC_TYPE_LOCAL) {
        file_seek(f, 8, 1); /* skip the 8 reserved bytes */
    } else {
        file_close(f);
        snprintf(msg, sizeof(msg), "Resource type %u is invalid.", (unsigned)type);
        rc_error("HILEVEL ERROR RcOpenResource", msg);
        f = NULL;
    }
    return f;
}

/* 0x4cdd61. The size comes from the body's own size field (+4), minus the
 * 16-byte body header; the RESC chunk length is read and ignored. For a
 * remote resource it reads `size` bytes of name (rc_open_resource reads
 * size - 8) and returns that file's length. Unknown types return the raw
 * size field. */
long rc_resource_size(uint32_t id)
{
    bool found;
    PlatFile *f = open_bundle_at(id, &found);
    if (!f)
        return 0;
    long result = 0;
    if (file_read_u32(f) == TAG_RESC) {
        file_read_u32(f);
        uint32_t type = file_read_u32(f);
        result = (long)file_read_u32(f);
        if (type == RC_TYPE_REMOTE) {
            read_remote_name(f, result);
            file_close(f);
            f = file_open(g_remote_name);
            if (!f)
                return 0;
            result = file_length(f);
        } else if (type == RC_TYPE_LOCAL) {
            result -= 16;
        }
    }
    file_close(f);
    return result;
}

void rc_resource_name(uint32_t id, char out[RC_NAME_LEN])
{
    snprintf(out, RC_NAME_LEN, "%u", (unsigned)id);
}

/* --- the compbun bottom layer (0x4ba03f-0x4ba0f0) ------------------------- */

static PlatFile *to_file(CbHandle h)
{
    return (PlatFile *)h;
}

/* 0x4ba03f. The original also stores the handle in the sound slot table. */
static CbHandle snd_res_open(const char *name, const char *mode)
{
    (void)mode;
    char *end;
    unsigned long id = strtoul(name, &end, 10);
    if (end == name || *end)
        return 0;
    return (CbHandle)rc_open_resource((uint32_t)id);
}

/* 0x4ba07b */
static void snd_res_close(CbHandle h)
{
    file_close(to_file(h));
}

/* 0x4ba092. Returns bytes, not items like fread. */
static long snd_res_read(void *buf, long size, long count, CbHandle h)
{
    return file_read(buf, size * count, to_file(h));
}

/* 0x4ba0d6 */
static int snd_res_seek(CbHandle h, long offset, int whence)
{
    return file_seek(to_file(h), offset, whence);
}

/* 0x4ba0ba */
static long snd_res_tell(CbHandle h)
{
    return file_tell(to_file(h));
}

void rc_hook_compbun(void)
{
    cbres_c_file_layer.open = snd_res_open;
    cbres_c_file_layer.close = snd_res_close;
    cbres_c_file_layer.read = snd_res_read;
    cbres_c_file_layer.seek = snd_res_seek;
    cbres_c_file_layer.tell = snd_res_tell;
}
