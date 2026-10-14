/*
 * TILE sprite sets (ALIFE.EXE tile_load_set 0x4e688a, tile_get_* 0x4ef4fb-
 * 0x4ef729, tile_draw 0x4e6a6a). See tile.h and docs/formats/tile.md.
 *
 * The original loads the body into memory and rewrites the offset table in
 * place into pointers; here the offsets are resolved on lookup and every
 * access is bounds-checked against the resource.
 */
#include "tile.h"

#include <stdbool.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

#define ENTRY_HEADER 0x1c
#define BODY_HEADER 8

/* 0x4e688a. The original reads the "TILE" tag and length from the
 * resource, allocates `length` bytes for the body, and turns each offset
 * into body + 8 + 4*count + offset. (A "TEST" tag is recognised and
 * rejected.) */
int tile_set_open(TileSet *ts, const uint8_t *data, size_t len)
{
    memset(ts, 0, sizeof *ts);
    if (!data || len < 8 || memcmp(data, "TILE", 4) != 0)
        return TILE_ERR_FORMAT;
    uint32_t blen = rd32(data + 4);
    if (blen > len - 8 || blen < BODY_HEADER)
        return TILE_ERR_TRUNCATED;
    const uint8_t *b = data + 8;
    ts->body = b;
    ts->len = blen;
    ts->count = rd16(b);
    ts->version = b[2];
    ts->cell_width = b[3];
    ts->cell_height = rd32(b + 4);
    if ((size_t)BODY_HEADER + 4u * ts->count > blen)
        return TILE_ERR_TRUNCATED;
    return TILE_OK;
}

int tile_get_entry(const TileSet *ts, int index, TileEntry *e)
{
    memset(e, 0, sizeof *e);
    if (index < 0 || index >= ts->count)
        return TILE_ERR_RANGE;
    size_t table_end = BODY_HEADER + 4u * ts->count;
    uint32_t off = rd32(ts->body + BODY_HEADER + 4u * (size_t)index);
    if (off > ts->len - table_end || ts->len - table_end - off < ENTRY_HEADER)
        return TILE_ERR_TRUNCATED;
    const uint8_t *p = ts->body + table_end + off;
    e->base = p;
    e->avail = ts->len - table_end - off;
    for (int i = 0; i < 4; i++)
        e->text_off[i] = rd32(p + 4 + 4 * i);
    e->height = rd16(p + 0x14);
    e->anchor = rd16(p + 0x16);
    e->type = p[0x18];
    e->strips = p[0x19];
    e->size = p[0x1a];
    /* tile_draw: strip v (1-based) starts at base + 0x1c + 4*strips +
     * offset[v-1]; the colour map sits between the offset table and the
     * first strip, so the first offset is its length. */
    size_t lut = ENTRY_HEADER + 4u * e->strips;
    if (e->strips == 0 || lut > e->avail)
        return TILE_ERR_TRUNCATED;
    uint32_t first = rd32(p + ENTRY_HEADER);
    if (first > e->avail - lut)
        return TILE_ERR_TRUNCATED;
    e->lut = p + lut;
    e->lut_len = first;
    return TILE_OK;
}

/* 0x4ef603 / 0x4ef665 / 0x4ef6c7 / 0x4ef729 */
const char *tile_entry_text(const TileEntry *e, int which)
{
    if (which < 0 || which > 3 || e->text_off[which] == 0)
        return NULL;
    size_t at = ENTRY_HEADER + (size_t)e->text_off[which];
    if (at >= e->avail || !memchr(e->base + at, 0, e->avail - at))
        return NULL;
    return (const char *)(e->base + at);
}

int tile_drawn_strips(const TileEntry *e)
{
    int n = 2 * e->size - 1;
    return n < e->strips ? n : e->strips;
}

/* Start of strip `strip`'s row data, or NULL. */
static const uint8_t *strip_data(const TileEntry *e, int strip, const uint8_t **end)
{
    if (e->type != 1 || strip < 0 || strip >= e->strips)
        return NULL;
    size_t at = ENTRY_HEADER + 4u * e->strips + rd32(e->base + ENTRY_HEADER + 4 * strip);
    if (at >= e->avail)
        return NULL;
    *end = e->base + e->avail;
    return e->base + at;
}

/* Packets: low 6 bits = count; top 2 bits: 00 skip (transparent), 01 that
 * many literal LUT indexes, 10 a run of one LUT index. 11 isn't used by the
 * data; tile_draw would neither draw nor advance for it. */
int tile_strip_width(const TileEntry *e, int strip)
{
    const uint8_t *end, *s = strip_data(e, strip, &end);
    if (!s || e->height == 0)
        return 0;
    int len = *s++;
    if (end - s < len)
        return 0;
    const uint8_t *row_end = s + len;
    int w = 0;
    while (s < row_end) {
        int c = *s++, n = c & 0x3f;
        if ((c & 0xc0) == 0x40)
            s += n;
        else if ((c & 0xc0) == 0x80)
            s++;
        w += n;
    }
    return w;
}

/* The zoom-0, no-effects path of tile_draw (0x4e6a6a): each row is a length
 * byte followed by that many bytes of packets; the next row follows. */
int tile_draw_strip(const TileEntry *e, int strip, uint8_t *dst, int pitch, int w, int h, int x, int y)
{
    const uint8_t *end, *s = strip_data(e, strip, &end);
    if (!s)
        return TILE_ERR_RANGE;
    for (int row = 0; row < e->height; row++, y++) {
        if (s >= end)
            return TILE_ERR_TRUNCATED;
        int len = *s++;
        if (end - s < len)
            return TILE_ERR_TRUNCATED;
        const uint8_t *p = s, *row_end = s + len;
        s = row_end;
        bool vis = y >= 0 && y < h;
        uint8_t *line = vis ? dst + (size_t)y * (size_t)pitch : NULL;
        int px = x;
        while (p < row_end) {
            int c = *p++, n = c & 0x3f, kind = c & 0xc0;
            if (kind == 0x40 && row_end - p < n)
                return TILE_ERR_TRUNCATED;
            if (kind == 0x80 && p >= row_end)
                return TILE_ERR_TRUNCATED;
            for (int i = 0; i < n && kind != 0 && kind != 0xc0; i++) {
                uint8_t idx = kind == 0x40 ? p[i] : p[0];
                if (idx >= e->lut_len)
                    return TILE_ERR_TRUNCATED;
                if (vis && px + i >= 0 && px + i < w)
                    line[px + i] = e->lut[idx];
            }
            if (kind == 0x40)
                p += n;
            else if (kind == 0x80)
                p++;
            px += n;
        }
    }
    return TILE_OK;
}

int tile_sprite_width(const TileEntry *e)
{
    int total = 0, n = tile_drawn_strips(e);
    for (int i = 0; i < n; i++)
        total += tile_strip_width(e, i);
    return total;
}

int tile_draw_sprite(const TileEntry *e, uint8_t *dst, int pitch, int w, int h, int x, int y)
{
    int n = tile_drawn_strips(e);
    for (int i = 0; i < n; i++) {
        int r = tile_draw_strip(e, i, dst, pitch, w, h, x, y);
        if (r < 0)
            return r;
        x += tile_strip_width(e, i);
    }
    return TILE_OK;
}
