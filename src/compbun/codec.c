/*
 * Codec manager and codecs (compbun:source:codecmgr.c).
 *
 * Every codec is built on one LZSS coder (Haruhiko Okumura's binary-tree
 * LZSS with a LucasArts bitstream, 4 KiB window, matches of 2..256 bytes),
 * optionally followed by a byte-delta filter and, for 12-bit audio, by a
 * reordering that splits the packed samples into an 8-bit "high byte" plane
 * and a 4-bit "low nibble" plane before delta coding. See
 * docs/formats/comp.md.
 *
 * Differences from the original:
 *  - scratch buffers are static instead of vh_mem_alloc'ed in codec_init;
 *  - the decoders check their input and output bounds (the original trusts
 *    the stream) and return -1 on corrupt data;
 *  - compressors don't modify their input (the original transformed src in
 *    place and restored it from codec_saveBuffer) and zero their scratch
 *    buffer first, so bytes the format doesn't define are deterministic.
 */
#include "codec.h"

#include <string.h>

/* ---- codec manager ------------------------------------------------------ */

static codec_proc codec_list[CODEC_LIST_SIZE]; /* 0x567a80 codecList[15] */

/* codec_outputTempBuffer (0x64798c): holds the LZSS output of the 12-bit
 * codecs before reordering, and the reordered input when compressing. A few
 * bytes of slack because the reorder steps may touch one byte past the
 * block. */
static uint8_t codec_temp[CODEC_BLOCK_SIZE + 16];
/* codec_saveBuffer (0x647990) took the original input; here it is the
 * transformed copy the compressors work on. */
static uint8_t codec_work[CODEC_BLOCK_SIZE + 16];

/* 0x4e1800 */
void codec_init(void)
{
    for (int i = 0; i < CODEC_LIST_SIZE; i++)
        codec_list[i] = NULL;
}

/* 0x4e1870 */
void codec_shutdown(void)
{
    for (int i = 0; i < CODEC_LIST_SIZE; i++)
        if (codec_list[i])
            codec_list[i](CODEC_CMD_SHUTDOWN, NULL, NULL, 0);
}

/* 0x4e1950 */
int codec_register(codec_proc proc)
{
    int id = proc(CODEC_CMD_ID, NULL, NULL, 0);
    if (id < 0 || id >= CODEC_LIST_SIZE)
        return -1;
    if (codec_list[id])
        return 1; /* original returns the registered proc pointer */
    codec_list[id] = proc;
    proc(CODEC_CMD_INIT, NULL, NULL, 0);
    return 0;
}

void codec_register_builtin(void)
{
    codec_register(codec_raw_proc);
    codec_register(codec_lz_proc);
    codec_register(codec_lz_delta1_proc);
    codec_register(codec_lz_delta2_proc);
    codec_register(codec_lz_12mono_0_proc);
    codec_register(codec_lz_12mono_1_proc);
    codec_register(codec_lz_12mono_2_proc);
    codec_register(codec_lz_12stereo_0_proc);
    codec_register(codec_lz_12stereo_1_proc);
    codec_register(codec_lz_12stereo_2_proc);
}

/* 0x4e1990 */
int codec_decompress(int id, void *dst, const void *src, int size)
{
    if (id < 0 || id >= CODEC_LIST_SIZE || !codec_list[id])
        return 0;
    return codec_list[id](CODEC_CMD_DECOMPRESS, dst, src, size);
}

int codec_compress(int id, void *dst, const void *src, int size)
{
    if (id < 0 || id >= CODEC_LIST_SIZE || !codec_list[id])
        return -1;
    return codec_list[id](CODEC_CMD_COMPRESS, dst, src, size);
}

/* ---- LZSS decoder ------------------------------------------------------- */

/*
 * Bitstream: a little-endian u16 of flag bits, consumed LSB first; a new one
 * is read as soon as the previous one runs out, interleaved with the byte
 * operands. Tokens:
 *   1                  literal byte follows
 *   0 0 h l   d        copy (h*2+l)+3 bytes from offset d-256 (d: 1 byte)
 *   0 1       a b      offset ((b&0xf0)<<4 | a) - 4096, length (b&15)+3;
 *                      length nibble 0 means: next byte n, length n+1,
 *                      and n == 0 ends the stream.
 */

/* 0x4e5ee0 / 0x4e6210 */
long codec_lz_decode(uint8_t *dst, size_t dst_cap, const uint8_t *src, size_t src_len)
{
    size_t in = 0, out = 0;
    unsigned mask, bitsleft = 16;

#define LZ_NEED(n) \
    do { \
        if (src_len - in < (size_t)(n)) \
            return -1; \
    } while (0)
#define LZ_BIT(b) \
    do { \
        (b) = mask & 1; \
        mask >>= 1; \
        if (--bitsleft == 0) { \
            LZ_NEED(2); \
            mask = (unsigned)src[in] | (unsigned)src[in + 1] << 8; \
            in += 2; \
            bitsleft = 16; \
        } \
    } while (0)

    LZ_NEED(2);
    mask = (unsigned)src[0] | (unsigned)src[1] << 8;
    in = 2;

    for (;;) {
        unsigned bit;
        size_t len, back;

        LZ_BIT(bit);
        if (bit) {
            LZ_NEED(1);
            if (out >= dst_cap)
                return -1;
            dst[out++] = src[in++];
            continue;
        }
        LZ_BIT(bit);
        if (!bit) {
            unsigned hi, lo;
            LZ_BIT(hi);
            LZ_BIT(lo);
            len = (hi << 1 | lo) + 3;
            LZ_NEED(1);
            back = 256 - src[in++];
        } else {
            unsigned a, b;
            LZ_NEED(2);
            a = src[in];
            b = src[in + 1];
            in += 2;
            back = 4096 - (((b & 0xf0u) << 4) | a);
            len = (b & 0x0f) + 3;
            if (len == 3) {
                LZ_NEED(1);
                len = (size_t)src[in++] + 1;
                if (len == 1)
                    return (long)out;
            }
        }
        if (back > out || len > dst_cap - out)
            return -1;
        for (; len; len--, out++)
            dst[out] = dst[out - back];
    }
#undef LZ_BIT
#undef LZ_NEED
}

/* ---- LZSS encoder ------------------------------------------------------- */

#define LZ_N 4096   /* window */
#define LZ_F 18     /* longest match the tree finds */
#define LZ_NIL LZ_N

typedef struct LzEnc {
    uint8_t text[LZ_N + LZ_F - 1];
    int lson[LZ_N + 1];
    int rson[LZ_N + 257];
    int dad[LZ_N + 1];
    int match_pos, match_len;
    const uint8_t *in;
    size_t in_len, in_pos;
    uint8_t *out;
    size_t cap, out_pos; /* cap = input length: give up when reached */
    unsigned bits, bitcount;
    uint8_t pend[64]; /* operand bytes waiting for their flag word */
    size_t npend;
} LzEnc;

static LzEnc lz_enc; /* the original's globals around 0x56ffb8..0x57d474 */

/* 0x4e6680 */
static int lz_getc(LzEnc *e)
{
    return e->in_pos < e->in_len ? e->in[e->in_pos++] : -1;
}

/* 0x4e6560 */
static void lz_flush(LzEnc *e)
{
    if (!e->npend)
        return;
    if (e->cap <= e->out_pos + e->npend) {
        e->out_pos = e->cap + 1;
        return;
    }
    memcpy(e->out + e->out_pos, e->pend, e->npend);
    e->out_pos += e->npend;
    e->npend = 0;
}

/* 0x4e65c0 */
static void lz_putbyte(LzEnc *e, unsigned b)
{
    if (e->npend < sizeof(e->pend))
        e->pend[e->npend++] = (uint8_t)b;
}

/* 0x4e65e0 */
static void lz_putbit(LzEnc *e, unsigned bit)
{
    if (e->cap <= e->out_pos + 2) {
        e->out_pos = e->cap + 1;
        return;
    }
    e->bits = (e->bits >> 1 | (bit & 1) << 15) & 0xffff;
    if (--e->bitcount == 0) {
        e->out[e->out_pos++] = (uint8_t)e->bits;
        e->out[e->out_pos++] = (uint8_t)(e->bits >> 8);
        lz_flush(e);
        e->bits = 0;
        e->bitcount = 16;
    }
}

/* 0x4e6330: Okumura's InsertNode */
static void lz_insert(LzEnc *e, int r)
{
    const uint8_t *key = &e->text[r];
    int cmp = 1, p = LZ_N + 1 + key[0], i;

    e->rson[r] = e->lson[r] = LZ_NIL;
    e->match_len = 0;
    for (;;) {
        if (cmp >= 0) {
            if (e->rson[p] == LZ_NIL) {
                e->rson[p] = r;
                e->dad[r] = p;
                return;
            }
            p = e->rson[p];
        } else {
            if (e->lson[p] == LZ_NIL) {
                e->lson[p] = r;
                e->dad[r] = p;
                return;
            }
            p = e->lson[p];
        }
        for (i = 1; i < LZ_F; i++)
            if ((cmp = key[i] - e->text[p + i]) != 0)
                break;
        if (i > e->match_len) {
            e->match_pos = p;
            if ((e->match_len = i) >= LZ_F)
                break;
        }
    }
    /* r replaces p in the tree */
    e->dad[r] = e->dad[p];
    e->lson[r] = e->lson[p];
    e->rson[r] = e->rson[p];
    e->dad[e->lson[p]] = r;
    e->dad[e->rson[p]] = r;
    if (e->rson[e->dad[p]] == p)
        e->rson[e->dad[p]] = r;
    else
        e->lson[e->dad[p]] = r;
    e->dad[p] = LZ_NIL;
}

/* 0x4e6470: Okumura's DeleteNode */
static void lz_delete(LzEnc *e, int p)
{
    int q;

    if (e->dad[p] == LZ_NIL)
        return;
    if (e->rson[p] == LZ_NIL) {
        q = e->lson[p];
    } else if (e->lson[p] == LZ_NIL) {
        q = e->rson[p];
    } else {
        q = e->lson[p];
        if (e->rson[q] != LZ_NIL) {
            do
                q = e->rson[q];
            while (e->rson[q] != LZ_NIL);
            e->rson[e->dad[q]] = e->lson[q];
            e->dad[e->lson[q]] = e->dad[q];
            e->lson[q] = e->lson[p];
            e->dad[e->lson[p]] = q;
        }
        e->rson[q] = e->rson[p];
        e->dad[e->rson[p]] = q;
    }
    e->dad[q] = e->dad[p];
    if (e->rson[e->dad[p]] == p)
        e->rson[e->dad[p]] = q;
    else
        e->lson[e->dad[p]] = q;
    e->dad[p] = LZ_NIL;
}

/* 0x4e5ea0 / 0x4e5f20 */
long codec_lz_encode(uint8_t *dst, const uint8_t *src, size_t src_len)
{
    LzEnc *e = &lz_enc;
    int r = 0, s = LZ_F, len = 0, c, i, last;

    e->in = src;
    e->in_len = src_len;
    e->in_pos = 0;
    e->out = dst;
    e->cap = src_len;
    e->out_pos = 0;
    e->npend = 0;
    e->bits = 0;
    e->bitcount = 16;
    e->match_len = e->match_pos = 0;
    for (i = LZ_N + 1; i <= LZ_N + 256; i++)
        e->rson[i] = LZ_NIL;
    for (i = 0; i < LZ_N; i++)
        e->dad[i] = LZ_NIL;
    /* not in the original (it compares against stale bytes; harmless) */
    memset(e->text, 0, sizeof(e->text));

    /* Unlike Okumura's, the window isn't pre-filled with spaces: the
     * current position starts at 0 and the lookahead is read to 0..17. */
    while (len < LZ_F && (c = lz_getc(e)) != -1)
        e->text[len++] = (uint8_t)c;
    if (len == 0)
        return 0;
    lz_insert(e, r);

    do {
        if (e->match_len > len)
            e->match_len = len;
        if (e->match_len < 3) {
            e->match_len = 1;
            lz_putbit(e, 1);
            lz_putbyte(e, e->text[r]);
        } else {
            int off = e->match_pos < r ? r - e->match_pos : r - e->match_pos + LZ_N;
            int neg = -off;
            int ml = e->match_len - 3;

            lz_putbit(e, 0);
            if (off >= 0x100 || ml > 3) {
                lz_putbit(e, 1);
                lz_putbyte(e, (unsigned)neg & 0xff);
                lz_putbyte(e, (((unsigned)neg >> 4) & 0xf0) | ((unsigned)ml & 0x0f));
                if (ml == 0)
                    lz_putbyte(e, 2); /* extended length 2+1 = 3 */
            } else {
                lz_putbit(e, 0);
                lz_putbit(e, (unsigned)ml >> 1 & 1);
                lz_putbit(e, (unsigned)ml & 1);
                lz_putbyte(e, (unsigned)neg & 0xff);
            }
        }
        last = e->match_len;
        for (i = 0; i < last; i++) {
            if ((c = lz_getc(e)) == -1)
                break;
            lz_delete(e, s);
            e->text[s] = (uint8_t)c;
            if (s < LZ_F - 1)
                e->text[s + LZ_N] = (uint8_t)c;
            s = (s + 1) & (LZ_N - 1);
            r = (r + 1) & (LZ_N - 1);
            lz_insert(e, r);
        }
        while (i++ < last) {
            lz_delete(e, s);
            s = (s + 1) & (LZ_N - 1);
            r = (r + 1) & (LZ_N - 1);
            if (--len)
                lz_insert(e, r);
        }
        if (e->out_pos >= e->cap)
            return (long)e->out_pos;
    } while (len > 0);

    /* end marker: long match, offset byte 0, length nibble 0, extension 0 */
    lz_putbit(e, 0);
    lz_putbit(e, 1);
    lz_putbyte(e, 0);
    lz_putbyte(e, 0);
    lz_putbyte(e, 0);
    if (e->bitcount == 16)
        lz_putbit(e, 0); /* the decoder reads a fresh flag word here */
    if (e->bitcount < 16) {
        e->bits >>= e->bitcount;
        if (e->cap <= e->out_pos + 2)
            return (long)e->cap + 1;
        e->out[e->out_pos++] = (uint8_t)e->bits;
        e->out[e->out_pos++] = (uint8_t)(e->bits >> 8);
    }
    lz_flush(e);
    return (long)e->out_pos;
}

/* ---- delta filters ------------------------------------------------------ */

static void delta1_decode(uint8_t *p, size_t n)
{
    for (size_t i = 1; i < n; i++)
        p[i] = (uint8_t)(p[i] + p[i - 1]);
}

/* Inverse of two delta1 passes, the second starting at index 2 (so p[1]
 * keeps its first-order delta). */
static void delta2_decode(uint8_t *p, size_t n)
{
    if (n < 2)
        return;
    uint8_t d = p[1];
    p[1] = (uint8_t)(p[0] + d);
    for (size_t i = 2; i < n; i++) {
        d = (uint8_t)(d + p[i]);
        p[i] = (uint8_t)(p[i - 1] + d);
    }
}

static void delta1_encode_from(uint8_t *p, size_t n, size_t start)
{
    if (n <= start)
        return;
    uint8_t prev = p[start - 1];
    for (size_t i = start; i < n; i++) {
        uint8_t cur = p[i];
        p[i] = (uint8_t)(cur - prev);
        prev = cur;
    }
}

static void delta2_encode(uint8_t *p, size_t n)
{
    delta1_encode_from(p, n, 1);
    delta1_encode_from(p, n, 2);
}

/* ---- 12-bit sample reordering ------------------------------------------ */

/*
 * iMUSE packs two 12-bit samples into 3 bytes b0 b1 b2:
 *     A = (b1 & 0x0f) << 8 | b0,   B = (b1 & 0xf0) << 4 | b2
 * For a block of L bytes the codecs keep n = 8L/12 samples as
 *     temp[0 .. n)          the top 8 bits of each sample
 *     temp[n .. n + n/2]    the low 4 bits, two per byte
 * (the "stereo" variants store the even samples' high bytes in the first
 * half of the plane and the odd ones in the second half, i.e. one channel
 * after the other). Blocks are 0x2000 bytes, not a multiple of 3, so a block
 * can start 0, 1 or 2 bytes into a frame: that is what the three variants
 * of each are for. Bytes a variant can't place (a block's leading or
 * trailing partial frame) are stored at fixed spots in the planes.
 *
 * The decoders below follow the original byte for byte, quirks included.
 */

/* high byte of pair i: the "mono" layout interleaves, "stereo" splits */
#define HI_MONO(t, n, i) ((unsigned)(t)[2 * (i)] << 8 | (t)[2 * (i) + 1])
#define HI_STEREO(t, n, i) ((unsigned)(t)[(i)] << 8 | (t)[((n) >> 1) + (i)])

/* phase 0 (0x4e2dd0 mono, 0x4e2360 stereo) */
static void unpack12_phase0(uint8_t *dst, const uint8_t *t, size_t n, int stereo)
{
    const uint8_t *nib = t + n;
    unsigned nb = *nib;
    uint8_t *o = dst;
    size_t k;

    for (size_t i = 0; i < n / 2; i++) {
        unsigned w = stereo ? HI_STEREO(t, n, i) : HI_MONO(t, n, i);
        unsigned b6 = (w >> 4) & 0xff, b7 = w & 0xff;
        nib++;
        o[0] = (uint8_t)((b6 & 0xf0) | (nb & 0x0f));
        nb = *nib;
        o[1] = (uint8_t)((b7 & 0xf0) | ((w >> 12) & 0x0f));
        o[2] = (uint8_t)((b7 << 4) | (nb >> 4));
        o += 3;
    }
    k = (3 * n - 3) / 2;
    if (n & 1) {
        size_t m = n - 1;
        dst[k] = (uint8_t)((t[n + (m >> 1)] & 0x0f) | (t[m] << 4));
        dst[k + 1] = (uint8_t)(t[m] >> 4);
    }
    dst[k + 1] |= t[n] & 0xf0;
}

/* phase 1 (0x4e2a70 mono, 0x4e1fc0 stereo) */
static void unpack12_phase1(uint8_t *dst, const uint8_t *t, size_t n, int stereo)
{
    const uint8_t *nib = t + n;
    unsigned nb = *nib, carry = nb >> 4;
    uint8_t *o = dst;

    for (size_t i = 0; i < n / 2; i++) {
        unsigned w = stereo ? HI_STEREO(t, n, i) : HI_MONO(t, n, i);
        unsigned b6 = (w >> 4) & 0xff;
        nib++;
        o[0] = (uint8_t)(((w >> 8) & 0xf0) + carry);
        carry = b6 & 0x0f;
        o[1] = (uint8_t)((b6 & 0xf0) | (nb & 0x0f));
        nb = *nib;
        o[2] = (uint8_t)((w << 4) | (nb >> 4));
        o += 3;
    }
    *o = (uint8_t)carry;
    if (n & 1) {
        size_t k = (3 * n - 3) / 2;
        dst[k] |= t[n - 1] & 0xf0;
        dst[k + 1] = (uint8_t)((t[n + (n >> 1)] & 0x0f) | (t[n - 1] << 4));
    }
}

/* phase 2 (0x4e26f0 mono, 0x4e1c00 stereo) */
static void unpack12_phase2(uint8_t *dst, const uint8_t *t, size_t L, size_t n, int stereo)
{
    const uint8_t *nib = t + n;
    uint8_t *o = dst + 1;

    dst[0] = t[L - 1];
    dst[L - 1] = t[n - 1];
    for (size_t i = 0; i < (n - 1) / 2; i++) {
        unsigned w = stereo ? HI_STEREO(t, n, i) : HI_MONO(t, n, i);
        unsigned nb = *nib++;
        unsigned b8 = (w >> 4) & 0xff, lo = w & 0xff;
        o[0] = (uint8_t)((b8 & 0xf0) | (nb & 0x0f));
        o[1] = (uint8_t)((lo & 0xf0) | ((w >> 12) & 0x0f));
        o[2] = (uint8_t)((lo << 4) | (nb >> 4));
        o += 3;
    }
    if (!(n & 1)) {
        size_t k = (3 * n - 6) / 2, m = n - 2;
        dst[k + 2] = (uint8_t)(t[m] >> 4);
        dst[k + 1] = (uint8_t)((t[n + (m >> 1)] & 0x0f) | (t[m] << 4));
    }
}

/* Encoders: 0x4e2c30/0x4e2190 (phase 0), 0x4e28d0/0x4e1df0 (phase 1),
 * 0x4e2550/0x4e1a30 (phase 2). t must be zeroed. The original may read one
 * byte past the input; S() reads 0 there instead. */
#define S(i) ((size_t)(i) < len ? s[(i)] : 0u)
static void pack12_phase0(uint8_t *t, const uint8_t *s, size_t len, size_t n)
{
    t[n] = S((n * 12) / 8) & 0xf0;
    for (size_t u = 0; u < n; u++) {
        if (!(u & 1)) {
            size_t k = (u * 12) / 8;
            t[u] = (uint8_t)(S(k + 1) << 4 | S(k) >> 4);
            t[n + u / 2] |= S(k) & 0x0f;
        } else {
            size_t k = ((u - 1) * 12) / 8;
            t[u] = (uint8_t)(S(k + 2) >> 4 | (S(k + 1) & 0xf0));
            t[n + (u + 1) / 2] = (uint8_t)(S(k + 2) << 4);
        }
    }
}

static void pack12_phase1(uint8_t *t, const uint8_t *s, size_t len, size_t n)
{
    t[n] = (uint8_t)(s[0] << 4);
    for (size_t u = 1; u < n + 1; u++) {
        if (!(u & 1)) {
            size_t k = (u * 12) / 8;
            t[u - 1] = (uint8_t)(S(k) << 4 | S(k - 1) >> 4);
            t[n + u / 2] = (uint8_t)(S(k - 1) << 4);
        } else {
            size_t k = ((u - 1) * 12) / 8;
            t[u - 1] = (uint8_t)(S(k + 1) >> 4 | (S(k) & 0xf0));
            t[n + u / 2] |= S(k + 1) & 0x0f;
        }
    }
}

static void pack12_phase2(uint8_t *t, const uint8_t *s, size_t len, size_t n)
{
    t[len - 1] = s[0];
    t[n - 1] = s[len - 1];
    for (size_t u = 0; u + 1 < n; u++) {
        if (!(u & 1)) {
            size_t k = (u * 12) / 8;
            t[u] = (uint8_t)(S(k + 2) << 4 | S(k + 1) >> 4);
            t[n + u / 2] = S(k + 1) & 0x0f;
        } else {
            size_t k = ((u - 1) * 12) / 8;
            t[u] = (uint8_t)(S(k + 3) >> 4 | (S(k + 2) & 0xf0));
            t[n + u / 2] |= (uint8_t)(S(k + 3) << 4);
        }
    }
}

#undef S

/* ---- codec bodies ------------------------------------------------------- */

static int clamp_len(long n)
{
    return n < 0 ? -1 : (int)n;
}

static int lz_family_decompress(int id, uint8_t *dst, const uint8_t *src, int size)
{
    long L;

    if (size < 0)
        return -1;
    if (id <= CODEC_LZ_DELTA2) {
        L = codec_lz_decode(dst, CODEC_DST_SIZE, src, (size_t)size);
        if (L < 0)
            return -1;
        if (id == CODEC_LZ_DELTA1)
            delta1_decode(dst, (size_t)L);
        else if (id == CODEC_LZ_DELTA2)
            delta2_decode(dst, (size_t)L);
        return clamp_len(L);
    }

    memset(codec_temp, 0, sizeof(codec_temp));
    L = codec_lz_decode(codec_temp, CODEC_BLOCK_SIZE, src, (size_t)size);
    if (L < 0)
        return -1;
    delta2_decode(codec_temp, (size_t)L);

    size_t n = (size_t)L * 8 / 12;
    if (n < 2)
        return -1; /* the original would run off the buffers */
    int stereo = id >= CODEC_LZ_12STEREO_0;
    switch (id) {
    case CODEC_LZ_12MONO_0:
    case CODEC_LZ_12STEREO_0:
        unpack12_phase0(dst, codec_temp, n, stereo);
        break;
    case CODEC_LZ_12MONO_1:
    case CODEC_LZ_12STEREO_1:
        unpack12_phase1(dst, codec_temp, n, stereo);
        break;
    default:
        unpack12_phase2(dst, codec_temp, (size_t)L, n, stereo);
        break;
    }
    return clamp_len(L);
}

static int lz_family_compress(int id, uint8_t *dst, const uint8_t *src, int size)
{
    size_t len = (size_t)size;

    if (size <= 0 || len > CODEC_BLOCK_SIZE)
        return -1;
    if (id >= CODEC_LZ_12MONO_0) {
        size_t n = len * 8 / 12;
        if (n < 2)
            return -1;
        memset(codec_temp, 0, sizeof(codec_temp));
        switch (id) {
        case CODEC_LZ_12MONO_0:
        case CODEC_LZ_12STEREO_0:
            pack12_phase0(codec_temp, src, len, n);
            break;
        case CODEC_LZ_12MONO_1:
        case CODEC_LZ_12STEREO_1:
            pack12_phase1(codec_temp, src, len, n);
            break;
        default:
            pack12_phase2(codec_temp, src, len, n);
            break;
        }
        memcpy(codec_work, codec_temp, len);
        if (id >= CODEC_LZ_12STEREO_0)
            for (size_t i = 0; i < n / 2; i++) {
                codec_work[i] = codec_temp[2 * i];
                codec_work[n / 2 + i] = codec_temp[2 * i + 1];
            }
        delta2_encode(codec_work, len);
    } else {
        memcpy(codec_work, src, len);
        if (id == CODEC_LZ_DELTA1)
            delta1_encode_from(codec_work, len, 1);
        else if (id == CODEC_LZ_DELTA2)
            delta2_encode(codec_work, len);
    }
    return clamp_len(codec_lz_encode(dst, codec_work, len));
}

/* Every codec proc has the same shape: 0/1 do nothing, 2 returns the id,
 * 3 compresses, 4 decompresses. */
static int lz_family_proc(int id, int cmd, void *dst, const void *src, int size)
{
    switch (cmd) {
    case CODEC_CMD_INIT:
    case CODEC_CMD_SHUTDOWN:
        return 0;
    case CODEC_CMD_ID:
        return id;
    case CODEC_CMD_COMPRESS:
        return lz_family_compress(id, dst, src, size);
    case CODEC_CMD_DECOMPRESS:
        return lz_family_decompress(id, dst, src, size);
    default:
        return -1;
    }
}

/* 0x4e3270: stored. Compress and decompress are both a copy. */
int codec_raw_proc(int cmd, void *dst, const void *src, int size)
{
    switch (cmd) {
    case CODEC_CMD_INIT:
    case CODEC_CMD_SHUTDOWN:
    case CODEC_CMD_ID:
        return CODEC_RAW;
    case CODEC_CMD_COMPRESS:
    case CODEC_CMD_DECOMPRESS:
        if (size < 0 || (cmd == CODEC_CMD_DECOMPRESS && size > CODEC_DST_SIZE))
            return -1;
        memmove(dst, src, (size_t)size);
        return size;
    default:
        return -1;
    }
}

/* 0x4e31c0 (decode 0x4e3250, encode 0x4e3230) */
int codec_lz_proc(int cmd, void *dst, const void *src, int size)
{
    return lz_family_proc(CODEC_LZ, cmd, dst, src, size);
}

/* 0x4e30a0 (decode 0x4e3180, encode 0x4e3110) */
int codec_lz_delta1_proc(int cmd, void *dst, const void *src, int size)
{
    return lz_family_proc(CODEC_LZ_DELTA1, cmd, dst, src, size);
}

/* 0x4e2f40 (decode 0x4e3050, encode 0x4e2fb0) */
int codec_lz_delta2_proc(int cmd, void *dst, const void *src, int size)
{
    return lz_family_proc(CODEC_LZ_DELTA2, cmd, dst, src, size);
}

/* 0x4e2bc0 (decode 0x4e2dd0, encode 0x4e2c30) */
int codec_lz_12mono_0_proc(int cmd, void *dst, const void *src, int size)
{
    return lz_family_proc(CODEC_LZ_12MONO_0, cmd, dst, src, size);
}

/* 0x4e2860 (decode 0x4e2a70, encode 0x4e28d0) */
int codec_lz_12mono_1_proc(int cmd, void *dst, const void *src, int size)
{
    return lz_family_proc(CODEC_LZ_12MONO_1, cmd, dst, src, size);
}

/* 0x4e24e0 (decode 0x4e26f0, encode 0x4e2550) */
int codec_lz_12mono_2_proc(int cmd, void *dst, const void *src, int size)
{
    return lz_family_proc(CODEC_LZ_12MONO_2, cmd, dst, src, size);
}

/* 0x4e2120 (decode 0x4e2360, encode 0x4e2190) */
int codec_lz_12stereo_0_proc(int cmd, void *dst, const void *src, int size)
{
    return lz_family_proc(CODEC_LZ_12STEREO_0, cmd, dst, src, size);
}

/* 0x4e1d80 (decode 0x4e1fc0, encode 0x4e1df0) */
int codec_lz_12stereo_1_proc(int cmd, void *dst, const void *src, int size)
{
    return lz_family_proc(CODEC_LZ_12STEREO_1, cmd, dst, src, size);
}

/* 0x4e19c0 (decode 0x4e1c00, encode 0x4e1a30) */
int codec_lz_12stereo_2_proc(int cmd, void *dst, const void *src, int size)
{
    return lz_family_proc(CODEC_LZ_12STEREO_2, cmd, dst, src, size);
}
