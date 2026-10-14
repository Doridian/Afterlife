/*
 * Autodesk FLI/FLC player (ALIFE.EXE 0x409dc0-0x40b1a1). See flc.h and
 * docs/formats/flc.md.
 *
 * Structure follows the original: a chunk dispatcher, one decoder per chunk
 * type, and four clipped span primitives the decoders draw through. The
 * original trusts the data; every read here is bounds-checked against the
 * chunk instead, so a bad file fails with FLC_ERR_TRUNCATED.
 */
#include "flc.h"

#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* --- span primitives ---------------------------------------------------- */

/* 0x40affc: clip a horizontal span at (*x, y) of *len pixels to the target.
 * Returns false if nothing is left. */
static bool clip_span(const FlcTarget *t, int *x, int y, int *len)
{
    int x0 = *x, n = *len, end = x0 + n;
    if (y < 0 || y >= t->height || end < 0 || x0 >= t->width)
        return false;
    if (x0 < 0) {
        n += x0;
        *x = 0;
    }
    if (end > t->width)
        n -= end - t->width;
    *len = n;
    return n >= 0;
}

/* 0x40ae2b: n copies of one byte. */
static void fill_run(const FlcTarget *t, int x, int y, uint8_t c, int n)
{
    if (clip_span(t, &x, y, &n))
        memset(t->pixels + (size_t)y * (size_t)t->pitch + (size_t)x, c, (size_t)n);
}

/* 0x40ae8d: n copies of a little-endian pixel pair (DELTA_FLC runs). */
static void fill_run16(const FlcTarget *t, int x, int y, uint16_t pair, int n)
{
    int len = n * 2, x0 = x;
    if (!clip_span(t, &x, y, &len))
        return;
    uint8_t *d = t->pixels + (size_t)y * (size_t)t->pitch + (size_t)x;
    /* The original stores whole words from the clipped start, so a span
     * clipped on the left by an odd amount comes out byte-swapped. Keep
     * the pair phase relative to the unclipped start instead. */
    for (int i = 0; i < len; i++)
        d[i] = (uint8_t)(((x - x0 + i) & 1) ? pair >> 8 : pair);
}

/* 0x40af1b: copy n source bytes. */
static void copy_span(const FlcTarget *t, int x, int y, const uint8_t *src, int n)
{
    int x0 = x;
    if (clip_span(t, &x, y, &n))
        memcpy(t->pixels + (size_t)y * (size_t)t->pitch + (size_t)x, src + (x - x0), (size_t)n);
}

/* 0x40afa1 */
static void put_pixel(const FlcTarget *t, int x, int y, uint8_t c)
{
    if (x >= 0 && y >= 0 && x < t->width && y < t->height)
        t->pixels[(size_t)y * (size_t)t->pitch + (size_t)x] = c;
}

/* --- chunk decoders ----------------------------------------------------- */

#define NEED(n)                                                                                                        \
    do {                                                                                                               \
        if ((size_t)(end - s) < (size_t)(n))                                                                           \
            return FLC_ERR_TRUNCATED;                                                                                  \
    } while (0)

/* 0x40a714 with the 0x40a469 / 0x40a4be callbacks: COLOR_256 (shift 0) and
 * COLOR_64 (shift 2, 6-bit values scaled with <<2 like the original).
 *
 * The original's palette callback ignores the packet's start index and
 * count: it converts 256 triplets from the packet data and sets entries
 * 10..245 (the non-static Windows colours), and only when the animation is
 * held in memory; streamed animations never change the palette. Every
 * palette chunk in the game is a single 256-colour packet, so decoding the
 * packets properly gives the same result. Applying the palette (and the
 * 10..245 restriction) is up to the caller. */
static int chunk_color(const uint8_t *s, const uint8_t *end, const FlcTarget *t, int shift)
{
    NEED(2);
    int packets = (int16_t)rd16(s);
    s += 2;
    int index = 0;
    while (--packets >= 0) {
        NEED(2);
        int skip = s[0], count = s[1] ? s[1] : 256;
        s += 2;
        NEED((size_t)count * 3);
        index += skip;
        for (int i = 0; i < count; i++, s += 3) {
            int k = index + i;
            if (t->palette && k < 256)
                t->palette[k] = (PlatColor){(uint8_t)(s[0] << shift), (uint8_t)(s[1] << shift),
                                            (uint8_t)(s[2] << shift)};
        }
        index += count;
    }
    return FLC_FRAME_PALETTE;
}

/* 0x40a7f4: BYTE_RUN. Each line starts with an (ignored) packet count byte,
 * then signed counts until the line is full: negative = literal bytes,
 * positive = run of the next byte. */
static int chunk_byte_run(const uint8_t *s, const uint8_t *end, const FlcPlayer *p, const FlcTarget *t)
{
    int w = p->hdr.width;
    for (int line = 0; line < p->hdr.height; line++) {
        int y = p->y + line, x = p->x;
        NEED(1);
        s++;
        while (x < p->x + w) {
            NEED(1);
            int n = (int8_t)*s++;
            if (n < 0) {
                n = -n;
                NEED(n);
                copy_span(t, x, y, s, n);
                s += n;
            } else {
                if (n == 0) /* the original would loop forever */
                    return FLC_ERR_TRUNCATED;
                NEED(1);
                fill_run(t, x, y, *s++, n);
            }
            x += n;
        }
    }
    return FLC_OK;
}

/* 0x40a8e8: DELTA_FLI ("LC"). s16 first line, s16 line count; per line a
 * packet count byte, then packets of (x skip byte, signed count): positive =
 * literal bytes, negative = run of the next byte. */
static int chunk_delta_fli(const uint8_t *s, const uint8_t *end, const FlcPlayer *p, const FlcTarget *t)
{
    NEED(4);
    int y = (int16_t)rd16(s) + p->y;
    int lines = (int16_t)rd16(s + 2);
    s += 4;
    while (--lines >= 0) {
        NEED(1);
        int packets = *s++, x = p->x;
        for (; packets != 0; packets--) {
            NEED(2);
            x += s[0];
            int n = (int8_t)s[1];
            s += 2;
            if (n < 0) {
                n = -n;
                NEED(1);
                fill_run(t, x, y, *s++, n);
            } else {
                NEED(n);
                copy_span(t, x, y, s, n);
                s += n;
            }
            x += n;
        }
        y++;
    }
    return FLC_OK;
}

/* 0x40aa10: DELTA_FLC ("SS2"). s16 count of lines with packets; each line
 * starts with words: 11xxxxxx xxxxxxxx = skip -w lines, 10xxxxxx = store the
 * low byte as the line's last pixel (then the packet count word follows),
 * 0 <= w = packet count. Packets: x skip byte, signed word count: positive =
 * literal pixel pairs, negative = run of the next pixel pair. */
static int chunk_delta_flc(const uint8_t *s, const uint8_t *end, const FlcPlayer *p, const FlcTarget *t)
{
    NEED(2);
    int lines = (int16_t)rd16(s);
    int y = p->y;
    s += 2;
    while (lines > 0) {
        NEED(2);
        uint16_t w = rd16(s);
        s += 2;
        if (w & 0x8000) {
            if (w & 0x4000) {
                y -= (int16_t)w;
                continue;
            }
            put_pixel(t, p->x + p->hdr.width - 1, y, (uint8_t)w);
            NEED(2);
            w = rd16(s);
            s += 2;
        }
        int x = p->x;
        for (; w != 0; w--) {
            NEED(2);
            x += s[0];
            int n = (int8_t)s[1];
            s += 2;
            if (n >= 0) {
                NEED(n * 2);
                copy_span(t, x, y, s, n * 2);
                s += n * 2;
                x += n * 2;
            } else {
                NEED(2);
                fill_run16(t, x, y, rd16(s), -n);
                s += 2;
                x += -n * 2;
            }
        }
        lines--;
        y++;
    }
    return FLC_OK;
}

/* 0x40abe8: BLACK, the whole frame to colour 0. */
static void chunk_black(const FlcPlayer *p, const FlcTarget *t)
{
    for (int line = 0; line < p->hdr.height; line++)
        fill_run(t, p->x, p->y + line, 0, p->hdr.width);
}

/* 0x40ac9c: COPY, an uncompressed frame. */
static int chunk_copy(const uint8_t *s, const uint8_t *end, const FlcPlayer *p, const FlcTarget *t)
{
    int w = p->hdr.width;
    NEED((size_t)w * p->hdr.height);
    for (int line = 0; line < p->hdr.height; line++, s += w)
        copy_span(t, p->x, p->y + line, s, w);
    return FLC_OK;
}

#undef NEED

/* 0x40a5ad */
int flc_decode_chunks(FlcPlayer *p, const uint8_t *chunks, size_t len, int nchunks, const FlcTarget *t)
{
    const uint8_t *s = chunks, *end = chunks + len;
    int result = FLC_OK;
    for (int i = 0; i < nchunks; i++) {
        if (end - s < 6)
            return FLC_ERR_TRUNCATED;
        uint32_t size = rd32(s);
        if (size < 6 || size > (size_t)(end - s))
            return FLC_ERR_TRUNCATED;
        const uint8_t *body = s + 6, *body_end = s + size;
        int r = FLC_OK;
        switch (rd16(s + 4)) {
        case FLC_CHUNK_COLOR_256:
            r = chunk_color(body, body_end, t, 0);
            break;
        case FLC_CHUNK_COLOR_64:
            r = chunk_color(body, body_end, t, 2);
            break;
        case FLC_CHUNK_DELTA_FLC:
            r = chunk_delta_flc(body, body_end, p, t);
            break;
        case FLC_CHUNK_DELTA_FLI:
            r = chunk_delta_fli(body, body_end, p, t);
            break;
        case FLC_CHUNK_BLACK:
            chunk_black(p, t);
            break;
        case FLC_CHUNK_BYTE_RUN:
            r = chunk_byte_run(body, body_end, p, t);
            break;
        case FLC_CHUNK_COPY:
            r = chunk_copy(body, body_end, p, t);
            break;
        default: /* 5, 6, 8, 9, 10, 14 and anything else: ignored */
            break;
        }
        if (r < 0)
            return r;
        result |= r;
        s += size;
    }
    return result;
}

/* --- player ------------------------------------------------------------- */

/* 0x409eb9 (flc_open_stream) and 0x409dc0 (flc_open; the in-memory case). */
int flc_open(FlcPlayer *p, const uint8_t *data, size_t len)
{
    memset(p, 0, sizeof *p);
    if (!data)
        return FLC_ERR_NOT_FOUND;
    if (len < FLC_HEADER_SIZE)
        return FLC_ERR_TRUNCATED;
    FlcHeader *h = &p->hdr;
    h->size = rd32(data);
    h->magic = rd16(data + 4);
    h->frames = rd16(data + 6);
    h->width = rd16(data + 8);
    h->height = rd16(data + 10);
    h->depth = rd16(data + 12);
    h->flags = rd16(data + 14);
    h->speed = rd32(data + 16);
    h->oframe1 = rd32(data + 0x50);
    h->oframe2 = rd32(data + 0x54);
    if (h->magic == FLC_MAGIC_FLI) {
        /* FLI: frame 1 follows the header; speed is in 1/70 s. */
        h->oframe1 = FLC_HEADER_SIZE;
        h->speed = h->speed * 1000 / 70;
    } else if (h->magic != FLC_MAGIC_FLC) {
        return FLC_ERR_BAD_MAGIC;
    }
    p->data = data;
    p->len = len;
    p->pos = h->oframe1;
    p->playing = true;
    return FLC_OK;
}

/* 0x40a030 -> 0x40a047 */
void flc_close(FlcPlayer *p) { memset(p, 0, sizeof *p); }

/* 0x40a145 */
void flc_rewind(FlcPlayer *p)
{
    p->pos = p->hdr.oframe1;
    p->frame = 0;
}

/* 0x40a1cf */
void flc_seek_ring_frame(FlcPlayer *p)
{
    if (p->hdr.magic != FLC_MAGIC_FLC)
        return;
    p->pos = p->hdr.oframe2;
    p->frame = 1;
}

/* 0x40b134 */
void flc_center(FlcPlayer *p, const FlcTarget *t)
{
    p->x = (t->width - (int)p->hdr.width) / 2;
    p->y = (t->height - (int)p->hdr.height) / 2;
}

/* 0x40a376 */
int flc_next_frame(FlcPlayer *p, const FlcTarget *t)
{
    if (p->pos > p->len || p->len - p->pos < FLC_FRAME_HEADER_SIZE)
        return FLC_ERR_TRUNCATED;
    const uint8_t *f = p->data + p->pos;
    uint32_t size = rd32(f);
    if (rd16(f + 4) != FLC_FRAME_TYPE)
        return FLC_ERR_BAD_FRAME;
    if (size < FLC_FRAME_HEADER_SIZE || size > p->len - p->pos)
        return FLC_ERR_TRUNCATED;
    p->pos += FLC_FRAME_HEADER_SIZE;
    int r = FLC_OK;
    /* An empty frame (no chunk data) doesn't count as a frame. */
    if (size > FLC_FRAME_HEADER_SIZE) {
        r = flc_decode_chunks(p, f + FLC_FRAME_HEADER_SIZE, size - FLC_FRAME_HEADER_SIZE, (int16_t)rd16(f + 6), t);
        p->pos += size - FLC_FRAME_HEADER_SIZE;
        if (r >= 0)
            p->frame++;
    }
    return r;
}

uint32_t flc_frame_delay_ms(const FlcPlayer *p, const FlcTarget *t)
{
    uint32_t scale = t->speed_scale > 0 ? (uint32_t)t->speed_scale : 1000;
    return (uint32_t)((uint64_t)scale * p->hdr.speed / 1000);
}

/* 0x40a27f */
int flc_play(FlcPlayer *p, const FlcTarget *t, FlcPresentFn present, void *ctx)
{
    int x0 = p->x, y0 = p->y;
    int x1 = x0 + p->hdr.width - 1, y1 = y0 + p->hdr.height - 1;
    int r = FLC_OK;
    for (int i = 0; i < p->hdr.frames; i++) {
        uint32_t delay = flc_frame_delay_ms(p, t);
        uint32_t start = plat_ticks_ms();
        r = flc_next_frame(p, t);
        if (r < 0)
            break;
        if (present)
            present(ctx, t, x0, y0, x1, y1, r);
        /* The original spins on timeGetTime(), pumping messages. */
        while (plat_ticks_ms() - start < delay)
            plat_yield();
        if (!p->playing)
            break;
    }
    return r;
}
