#include "jiff.h"

#include <stdlib.h>
#include <string.h>

#define TAG(a, b, c, d) ((uint32_t)(d) | (uint32_t)(c) << 8 | (uint32_t)(b) << 16 | (uint32_t)(a) << 24)

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int read_at(FILE *f, long pos, void *buf, size_t len)
{
    return fseek(f, pos, SEEK_SET) == 0 && fread(buf, 1, len, f) == len;
}

static uint32_t *read_table(FILE *f, long pos, uint32_t count)
{
    uint8_t *raw = malloc((size_t)count * 4);
    uint32_t *out = malloc((size_t)count * 4);
    if (!raw || !out || !read_at(f, pos, raw, (size_t)count * 4)) {
        free(raw);
        free(out);
        return NULL;
    }
    for (uint32_t i = 0; i < count; i++)
        out[i] = rd32(raw + i * 4);
    free(raw);
    return out;
}

int jiff_open(JiffBundle *b, FILE *f)
{
    uint8_t hdr[8];
    long pos = 8, ofst = 0, rcid = 0, resc = 0;

    memset(b, 0, sizeof(*b));
    if (!read_at(f, 0, hdr, 8) || rd32(hdr) != TAG('J', 'I', 'F', 'F'))
        return 0;
    /* HEAD, OFST and RCID come first; RESC holds the payloads and runs to EOF
     * (its length field only covers the first entry, so don't trust it). */
    while (!resc) {
        if (!read_at(f, pos, hdr, 8))
            return 0;
        uint32_t tag = rd32(hdr), len = rd32(hdr + 4);
        if (tag == TAG('H', 'E', 'A', 'D')) {
            uint8_t n[4];
            if (!read_at(f, pos + 8, n, 4))
                return 0;
            b->count = rd32(n);
        } else if (tag == TAG('O', 'F', 'S', 'T')) {
            ofst = pos + 8;
        } else if (tag == TAG('R', 'C', 'I', 'D')) {
            rcid = pos + 8;
        } else if (tag == TAG('R', 'E', 'S', 'C')) {
            resc = pos + 8;
        }
        pos += 8 + (long)len;
    }
    if (!b->count || !ofst || !rcid)
        return 0;

    uint32_t *offsets = read_table(f, ofst, b->count);
    uint32_t *ids = read_table(f, rcid, b->count);
    b->entries = calloc(b->count, sizeof(*b->entries));
    if (!offsets || !ids || !b->entries)
        goto fail;
    for (uint32_t i = 0; i < b->count; i++) {
        /* 16-byte entry header: u32 flags, u32 length counted from +8, 8 zero
         * bytes. */
        long epos = resc + (long)offsets[i];
        if (!read_at(f, epos, hdr, 8) || rd32(hdr + 4) < 8)
            goto fail;
        b->entries[i].id = ids[i];
        b->entries[i].offset = (uint32_t)epos + 16;
        b->entries[i].length = rd32(hdr + 4) - 8;
    }
    free(offsets);
    free(ids);
    b->f = f;
    return 1;

fail:
    free(offsets);
    free(ids);
    free(b->entries);
    b->entries = NULL;
    return 0;
}

void jiff_close(JiffBundle *b)
{
    if (b->f)
        fclose(b->f);
    free(b->entries);
    memset(b, 0, sizeof(*b));
}

const JiffEntry *jiff_find(const JiffBundle *b, uint32_t id)
{
    for (uint32_t i = 0; i < b->count; i++)
        if (b->entries[i].id == id)
            return &b->entries[i];
    return NULL;
}

uint8_t *jiff_load(const JiffBundle *b, const JiffEntry *e)
{
    uint8_t *buf = malloc(e->length ? e->length : 1);
    if (buf && !read_at(b->f, (long)e->offset, buf, e->length)) {
        free(buf);
        return NULL;
    }
    return buf;
}
