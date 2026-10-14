/*
 * AKOS costume parsing. See akos.h and docs/formats/akos.md.
 *
 * The original keeps the resource in memory and walks it with
 * res_find_block (0x4dbc23) on every use, reading chunk contents with plain
 * pointer arithmetic and no bounds checks. Here the chunks are located once
 * and every read is checked against the resource.
 */
#include "formats/akos.h"

#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t rd32be(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

/* 0x4dbc23 res_find_block: chunks follow the 8-byte "AKOS" header back to
 * back; each length (big-endian) includes its own 8-byte header. A length
 * below 1 is an error ("%s: Not found in %03d (illegal block length)"); the
 * port also rejects lengths under 8 or past the end. */
int akos_find_chunk(const AkosFile *a, const char tag[4], AkosChunk *out)
{
    out->data = NULL;
    out->len = 0;
    size_t total = rd32be(a->data + 4);
    if (total > a->len)
        total = a->len;
    size_t pos = 8;
    while (pos + 8 <= total) {
        uint32_t n = rd32be(a->data + pos + 4);
        if (n < 8 || n > total - pos)
            return AKOS_ERR_TRUNCATED;
        if (memcmp(a->data + pos, tag, 4) == 0) {
            out->data = a->data + pos + 8;
            out->len = n - 8;
            return AKOS_OK;
        }
        pos += n;
    }
    return AKOS_ERR_FORMAT;
}

int akos_open(AkosFile *a, const uint8_t *data, size_t len)
{
    memset(a, 0, sizeof *a);
    if (!data || len < 8 || memcmp(data, "AKOS", 4) != 0)
        return AKOS_ERR_FORMAT;
    a->data = data;
    a->len = len;
    if (rd32be(data + 4) > len)
        return AKOS_ERR_TRUNCATED;

    /* Every chunk the game looks up; only AKFO is optional (it is only
     * needed by opcode 0xc088, which reports "AKOS:NO CEL GOTO OFFSET
     * TABLE" without it). */
    static const struct {
        const char *tag;
        size_t off;
        uint32_t min_len;
    } req[] = {
        {"AKHD", offsetof(AkosFile, akhd), 12}, {"AKPL", offsetof(AkosFile, akpl), 1},
        {"RGBS", offsetof(AkosFile, rgbs), 0},  {"AKSQ", offsetof(AkosFile, aksq), 0},
        {"AKCH", offsetof(AkosFile, akch), 0},  {"AKOF", offsetof(AkosFile, akof), 0},
        {"AKCI", offsetof(AkosFile, akci), 0},  {"AKCD", offsetof(AkosFile, akcd), 0},
    };
    for (size_t i = 0; i < sizeof req / sizeof req[0]; i++) {
        AkosChunk *c = (AkosChunk *)((uint8_t *)a + req[i].off);
        int r = akos_find_chunk(a, req[i].tag, c);
        if (r < 0)
            return r;
        if (c->len < req[i].min_len)
            return AKOS_ERR_TRUNCATED;
    }
    akos_find_chunk(a, "AKFO", &a->akfo);

    const uint8_t *h = a->akhd.data;
    a->version = rd16(h);
    a->flags = rd16(h + 2);
    a->num_anims = rd16(h + 4);
    a->unk6 = rd16(h + 6);
    a->codec = rd16(h + 8);
    a->unk10 = rd16(h + 10);
    a->num_cels = a->akof.len / 6;
    if ((size_t)a->num_anims * 2 > a->akch.len)
        return AKOS_ERR_TRUNCATED;
    /* RGBS must give a colour for each AKPL entry (0x4d5ef7 reads them in
     * step). */
    if (a->rgbs.len < a->akpl.len * 3u)
        return AKOS_ERR_TRUNCATED;
    return AKOS_OK;
}

int akos_get_cel(const AkosFile *a, unsigned cel, AkosCel *out)
{
    memset(out, 0, sizeof *out);
    if (cel >= a->num_cels)
        return AKOS_ERR_RANGE;
    const uint8_t *e = a->akof.data + (size_t)cel * 6;
    uint32_t cd = rd32le(e);
    uint32_t ci = rd16(e + 4);
    if (cd >= a->akcd.len || (size_t)ci + 4 > a->akci.len)
        return AKOS_ERR_TRUNCATED;
    const uint8_t *p = a->akci.data + ci;
    size_t left = a->akci.len - ci;
    out->width = rd16(p);
    out->height = rd16(p + 2);
    int16_t *f[4] = {&out->rel_x, &out->rel_y, &out->move_x, &out->move_y};
    for (int i = 0; i < 4; i++)
        *f[i] = left >= (size_t)(6 + 2 * i) ? (int16_t)rd16(p + 4 + 2 * i) : 0;
    out->data = a->akcd.data + cd;
    out->avail = a->akcd.len - cd;
    return AKOS_OK;
}

int akos_rle_shift(const AkosFile *a)
{
    if (a->akpl.len == 32)
        return 3;
    if (a->akpl.len == 64)
        return 2;
    return 4;
}

const uint8_t *akos_anim_record(const AkosFile *a, unsigned index, size_t *avail)
{
    if (index >= a->num_anims)
        return NULL;
    uint16_t off = rd16(a->akch.data + 2 * index);
    if (off == 0 || (size_t)off + 2 > a->akch.len)
        return NULL;
    *avail = a->akch.len - off;
    return a->akch.data + off;
}

int akos_transparent_index(const AkosFile *a) { return a->codec == AKOS_CODEC_BOMP ? 255 : 0; }

/* Codec 1: one continuous run-length stream over the columns, left to
 * right, each top to bottom. A byte holds the colour in its high bits and
 * the run length in the low `shift` bits; a zero length means the length
 * is the next byte. Runs carry over from one column into the next. */
static int decode_rle(const AkosFile *a, const AkosCel *c, uint8_t *out)
{
    int shift = akos_rle_shift(a);
    unsigned mask = (1u << shift) - 1;
    const uint8_t *p = c->data, *end = c->data + c->avail;
    uint32_t run = 0;
    uint8_t color = 0;
    for (int x = 0; x < c->width; x++) {
        for (int y = 0; y < c->height; y++) {
            if (run == 0) {
                if (p >= end)
                    return AKOS_ERR_TRUNCATED;
                color = (uint8_t)(*p >> shift);
                run = *p++ & mask;
                if (run == 0) {
                    if (p >= end)
                        return AKOS_ERR_TRUNCATED;
                    run = *p++;
                }
                if (run == 0) /* the original's counter wraps: the run fills the rest of the cel */
                    run = UINT32_MAX;
            }
            out[(size_t)y * c->width + (size_t)x] = color;
            run--;
        }
    }
    return AKOS_OK;
}

/* Codec 5, BOMP (port-only; see akos.h): per row a u16 byte count, then
 * codes: n = (code >> 1) + 1; bit 0 set = repeat the next byte n times,
 * clear = n literal bytes. */
static int decode_bomp(const AkosCel *c, uint8_t *out)
{
    const uint8_t *p = c->data, *end = c->data + c->avail;
    for (int y = 0; y < c->height; y++) {
        if (end - p < 2)
            return AKOS_ERR_TRUNCATED;
        unsigned n = rd16(p);
        p += 2;
        if ((size_t)(end - p) < n)
            return AKOS_ERR_TRUNCATED;
        const uint8_t *q = p, *qe = p + n;
        uint8_t *row = out + (size_t)y * c->width;
        int x = 0;
        while (x < c->width && q < qe) {
            unsigned code = *q++;
            unsigned cnt = (code >> 1) + 1;
            if (code & 1) {
                if (q >= qe)
                    return AKOS_ERR_TRUNCATED;
                uint8_t v = *q++;
                for (unsigned i = 0; i < cnt && x < c->width; i++)
                    row[x++] = v;
            } else {
                if ((size_t)(qe - q) < cnt)
                    return AKOS_ERR_TRUNCATED;
                for (unsigned i = 0; i < cnt && x < c->width; i++)
                    row[x++] = q[i];
                q += cnt;
            }
        }
        while (x < c->width)
            row[x++] = 255;
        p = qe;
    }
    return AKOS_OK;
}

int akos_decode_cel(const AkosFile *a, const AkosCel *cel, uint8_t *out)
{
    switch (a->codec) {
    case AKOS_CODEC_RLE:
        return decode_rle(a, cel, out);
    case AKOS_CODEC_BOMP:
        return decode_bomp(cel, out);
    default:
        return AKOS_ERR_CODEC;
    }
}
