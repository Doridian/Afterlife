/*
 * Port-only: ALIFE.EXE's image data (PE resources, the built-in font) from
 * the user's copy, through a minimal PE32 reader. See exe_res.h.
 */
#include "gfx/exe_res.h"

#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"

static uint8_t *g_img;
static uint32_t g_len;
static uint32_t g_base;     /* ImageBase */
static uint32_t g_rsrc_rva; /* resource directory */
static uint32_t g_nsect;
static uint32_t g_sect;     /* file offset of the section table */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* RVA -> file offset; *avail = bytes left in that section's raw data. */
static bool rva_to_off(uint32_t rva, uint32_t *off, uint32_t *avail)
{
    for (uint32_t i = 0; i < g_nsect; i++) {
        const uint8_t *s = g_img + g_sect + i * 40;
        uint32_t va = rd32(s + 12), raw_size = rd32(s + 16), raw = rd32(s + 20);
        if (rva >= va && rva - va < raw_size && raw + raw_size <= g_len) {
            *off = raw + (rva - va);
            *avail = raw_size - (rva - va);
            return true;
        }
    }
    return false;
}

void exe_res_close(void)
{
    free(g_img);
    g_img = NULL;
    g_len = 0;
}

bool exe_res_open(const char *name)
{
    if (g_img)
        return true;
    PlatFile *f = plat_file_open(name);
    if (!f)
        return false;
    plat_file_seek(f, 0, 2);
    long len = plat_file_tell(f);
    plat_file_seek(f, 0, 0);
    if (len < 0x40 || len > 64L * 1024 * 1024 || !(g_img = malloc((size_t)len))) {
        plat_file_close(f);
        return false;
    }
    g_len = (uint32_t)len;
    size_t got = plat_file_read(f, g_img, 1, (size_t)len);
    plat_file_close(f);
    uint32_t pe = got == (size_t)len ? rd32(g_img + 0x3c) : 0;
    if (pe == 0 || pe > g_len - 0xf8 || memcmp(g_img + pe, "PE\0\0", 4) != 0 || rd16(g_img + pe + 24) != 0x10b) {
        exe_res_close();
        return false;
    }
    g_nsect = rd16(g_img + pe + 6);
    g_sect = pe + 24 + rd16(g_img + pe + 20);
    g_base = rd32(g_img + pe + 24 + 28);
    g_rsrc_rva = rd32(g_img + pe + 24 + 96 + 2 * 8);
    if (g_sect + g_nsect * 40 > g_len) {
        exe_res_close();
        return false;
    }
    return true;
}

/* Looks up `id` in the resource directory at `dir` (offset from the
 * resource base); id = ~0 takes the first entry. Returns the entry's
 * OffsetToData, or 0. */
static uint32_t dir_lookup(uint32_t rsrc, uint32_t avail, uint32_t dir, uint32_t id)
{
    if (dir + 16 > avail)
        return 0;
    const uint8_t *d = g_img + rsrc + dir;
    uint32_t n = (uint32_t)rd16(d + 12) + rd16(d + 14);
    for (uint32_t i = 0; i < n && dir + 16 + i * 8 + 8 <= avail; i++) {
        const uint8_t *e = d + 16 + i * 8;
        if (id == ~0u || rd32(e) == id)
            return rd32(e + 4);
    }
    return 0;
}

const uint8_t *exe_res_find(uint32_t type, uint32_t id, uint32_t *size)
{
    uint32_t rsrc, avail;
    if (!g_img || !g_rsrc_rva || !rva_to_off(g_rsrc_rva, &rsrc, &avail))
        return NULL;
    uint32_t e = dir_lookup(rsrc, avail, 0, type);
    if (!(e & 0x80000000u))
        return NULL;
    e = dir_lookup(rsrc, avail, e & 0x7fffffffu, id);
    if (!(e & 0x80000000u))
        return NULL;
    e = dir_lookup(rsrc, avail, e & 0x7fffffffu, ~0u);
    if (e == 0 || (e & 0x80000000u) || e + 8 > avail)
        return NULL;
    uint32_t off, data_avail, len = rd32(g_img + rsrc + e + 4);
    if (!rva_to_off(rd32(g_img + rsrc + e), &off, &data_avail) || len > data_avail)
        return NULL;
    *size = len;
    return g_img + off;
}

const uint8_t *exe_res_va(uint32_t va, uint32_t *avail)
{
    uint32_t off;
    if (!g_img || va < g_base || !rva_to_off(va - g_base, &off, avail))
        return NULL;
    return g_img + off;
}
