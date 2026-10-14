/*
 * iMUSE:wave:mixer.c (0x4e36a0..0x4e5d90, "C:\iwin\WAVE\MIXER.C").
 *
 * Every voice is scaled through an amplitude table to 12-bit range (+-2048
 * at full volume) and summed into a 16-bit mix buffer; the sum then goes
 * through a soft-clip table ("softL") to the output format. There is no
 * real resampling: each mix routine handles in == out, 2x up (linear
 * interpolation), 2x down (drop every other frame) and anything else by
 * nearest-sample stepping. Volume is quantised to 17 levels, pan to 17
 * positions with an equal-power law.
 *
 * Source formats: 8-bit unsigned, 12-bit packed (two samples in three
 * bytes), 16-bit signed big-endian (reduced to 12 bits). Mono or stereo.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "imuse/imuse_internal.h"

#define AMP_LEVELS 17

static int16_t *g_amp8;   /* 0x56ff94 "amp8Table": 17 x 256 */
static int16_t *g_amp12;  /* 0x56ffa4 "amp12Table": 17 x 4096 */
static void *g_soft;      /* 0x56ffb0 "softLTable" */
static void *g_soft_mid;  /* 0x56ffac "softLMID" */
static int g_soft_half;   /* entries on each side of softLMID */
static int g_out_word_size;  /* 0x56ffa8 */
static int g_out_channels;   /* 0x56ffb4 */
static int16_t *g_mix_buf;   /* 0x56ff98 */
static int32_t g_mix_buf_size; /* 0x56ff90, bytes */
static int g_stereo_reverse; /* 0x56ff9c */
static int g_radio_chatter;  /* 0x6772c4 "radioChatter" */

/* 0x6771a8: pan levels, [vol level 0..16][position 0..16]. In the original
 * a byte table; it equals round(v * sin(p * pi / 32)), so it is computed
 * here (tests/imuse_test.c checks it against ALIFE.EXE when given). */
static int8_t g_pan_table[AMP_LEVELS][AMP_LEVELS];

static inline void acc(int16_t *d, int v)
{
    *d = (int16_t)(uint16_t)((uint16_t)*d + (uint16_t)v);
}

/* 0x4e36a0 */
int mixer_init(ImuseInitData *init, const ImuseWaveFormat *fmt)
{
    g_out_word_size = fmt->word_size;
    g_out_channels = fmt->channels;
    g_mix_buf = fmt->mix_buf;
    g_mix_buf_size = fmt->mix_buf_size;
    g_stereo_reverse = fmt->stereo_reverse;
    int n = init->wave_mix_count;
    g_soft_half = n * 2048;
    g_amp8 = malloc(sizeof(int16_t) * AMP_LEVELS * (256 + 4096));
    g_soft = malloc((size_t)(2 * g_soft_half) * (g_out_word_size == 8 ? 1 : sizeof(int16_t)));
    if (!g_amp8 || !g_soft) {
        imuse_log("ERR:allocating mixer buffers...\n");
        free(g_amp8);
        free(g_soft);
        g_amp8 = NULL;
        g_soft = NULL;
        return IMUSE_ERR;
    }
    g_amp12 = g_amp8 + AMP_LEVELS * 256;

    /* Volume levels 0, 7, 15, 23, ..., 127. */
    int lvl = 0;
    for (int k = 0; k < AMP_LEVELS; k++) {
        for (int i = 0; i < 256; i++)
            g_amp8[k * 256 + i] = (int16_t)((lvl * 16 * (i - 128)) / 127);
        for (int j = 0; j < 4096; j++)
            g_amp12[k * 4096 + j] = (int16_t)((lvl * (j - 2048)) / 127);
        lvl = lvl == 0 ? 7 : lvl + 8;
    }
    for (int v = 0; v < AMP_LEVELS; v++)
        for (int p = 0; p < AMP_LEVELS; p++)
            g_pan_table[v][p] = (int8_t)lround(v * sin(p * 3.14159265358979323846 / 32.0));

    /* Soft clip: y = (j*n*K / (n*2047 + j*(n-1)) + 1) / 2, which is linear
     * for one voice and saturates towards full scale for n. */
    if (g_out_word_size == 8) {
        uint8_t *mid = (uint8_t *)g_soft + g_soft_half;
        for (int j = 0; j < g_soft_half; j++) {
            int q = (j * n * 0xfe) / (n * 0x7ff + j * (n - 1));
            int y = (q + 1) / 2;
            mid[j] = (uint8_t)(y + 0x80);
            mid[-1 - j] = (uint8_t)(0x7f - y);
        }
        g_soft_mid = mid;
    } else {
        int16_t *mid = (int16_t *)g_soft + g_soft_half;
        for (int j = 0; j < g_soft_half; j++) {
#ifdef IMUSE_MIXER_ORIGINAL_OVERFLOW
            /* The original's 32-bit IDIV: with 8 voices, sums above 4096
             * wrap to negative output. */
            int32_t num = (int32_t)(uint32_t)((uint32_t)j * (uint32_t)n * 0xfffeu);
            int32_t q = num / (n * 0x7ff + j * (n - 1));
#else
            int64_t q = ((int64_t)j * n * 0xfffe) / (n * 0x7ff + j * (n - 1));
#endif
            int y = (int)((q + 1) / 2);
            mid[j] = (int16_t)y;
            mid[-1 - j] = (int16_t)(-1 - y);
        }
        g_soft_mid = mid;
    }
    return 0;
}

/* 0x4e5d60 */
void mixer_shutdown(void)
{
    free(g_amp8);
    free(g_soft);
    g_amp8 = g_amp12 = NULL;
    g_soft = g_soft_mid = NULL;
}

void mixer_set_radio_chatter(void) /* 0x4e3900 */
{
    g_radio_chatter = 1;
}

void mixer_clear_radio_chatter(void) /* 0x4e3910 */
{
    g_radio_chatter = 0;
}

/* 0x4e3920 */
int mixer_clear_buf(void)
{
    if (!g_mix_buf)
        return IMUSE_ERR;
    memset(g_mix_buf, 0, (size_t)g_mix_buf_size);
    return 0;
}

/* Sample fetchers: amplitude-table value of a source sample. */
#define A8(t, p) ((int)(t)[(p)[0]])
#define S12A(t, p) ((int)(t)[(((p)[1] & 0x0f) << 8) | (p)[0]])
#define S12B(t, p) ((int)(t)[(((p)[1] & 0xf0) << 4) | (p)[2]])
#define W16(t, p) ((int)(t)[((int16_t)(uint16_t)(((p)[0] << 8) | (p)[1]) >> 4) + 2048])

/* Radio chatter (8-bit, same rate only): subtracts the running mean of the
 * next four samples and boosts x4, a crude high-pass. Port: indexes are
 * clamped to the table and reads past the end see silence. */
static inline int rc_byte(const uint8_t *s, int32_t i, int32_t n)
{
    return i < n ? s[i] : 0x80;
}

static inline int rc_index(int v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

/* 0x4e3cc0: 8-bit mono -> mono */
static void mix_8m_m(const uint8_t *s, int32_t in, int32_t out, int16_t *d, const int16_t *t)
{
    if (in == out) {
        if (!g_radio_chatter) {
            for (int32_t i = 0; i < in; i++)
                acc(d++, A8(t, s + i));
        } else {
            int sum = rc_byte(s, 0, in) + rc_byte(s, 1, in) + rc_byte(s, 2, in) + rc_byte(s, 3, in) - 0x200;
            for (int32_t i = 0; i < in; i++) {
                int b = s[i];
                acc(d++, t[rc_index(b - (sum >> 2))] * 4);
                sum = sum - b + rc_byte(s, i + 4, in);
            }
        }
    } else if (in * 2 == out) {
        for (int32_t i = 0; i < in - 1; i++, s++, d += 2) {
            acc(d, A8(t, s));
            acc(d + 1, (A8(t, s + 1) + A8(t, s)) >> 1);
        }
        acc(d, A8(t, s));
        acc(d + 1, A8(t, s));
    } else if (out * 2 == in) {
        for (int32_t i = 0; i < out; i++, s += 2)
            acc(d++, A8(t, s));
    } else {
        int32_t e = -in;
        for (int32_t i = 0; i < out; i++) {
            acc(d++, A8(t, s));
            for (e += in; e >= 0; e -= out)
                s++;
        }
    }
}

/* 0x4e3ea0: 12-bit mono -> mono */
static void mix_12m_m(const uint8_t *s, int32_t in, int32_t out, int16_t *d, const int16_t *t)
{
    if (in & 1) {
        in &= ~1;
        imuse_log("WARNING:odd inFrameCount with 12-bit data...\n");
    }
    if (in == out) {
        for (int32_t i = 0; i < in / 2; i++, s += 3, d += 2) {
            acc(d, S12A(t, s));
            acc(d + 1, S12B(t, s));
        }
    } else if (in * 2 == out) {
        int32_t pairs = in / 2;
        for (int32_t i = 0; i < pairs - 1; i++, s += 3, d += 4) {
            int a = S12A(t, s), b = S12B(t, s);
            acc(d, a);
            acc(d + 1, (a + b) >> 1);
            acc(d + 2, b);
            acc(d + 3, (S12A(t, s + 3) + b) >> 1);
        }
        int a = S12A(t, s), b = S12B(t, s);
        acc(d, a);
        acc(d + 1, (b + a) >> 1);
        acc(d + 2, b);
        acc(d + 3, b);
    } else if (out * 2 == in) {
        for (int32_t i = 0; i < out; i++, s += 3)
            acc(d++, S12A(t, s));
    } else {
        int half = 0;
        int32_t e = -in;
        for (int32_t i = 0; i < out; i++) {
            acc(d++, half ? S12B(t, s) : S12A(t, s));
            for (e += in; e >= 0; e -= out) {
                half ^= 1;
                if (!half)
                    s += 3;
            }
        }
    }
}

/* 0x4e40f0: 16-bit mono -> mono */
static void mix_16m_m(const uint8_t *s, int32_t in, int32_t out, int16_t *d, const int16_t *t)
{
    if (in == out) {
        for (int32_t i = 0; i < in; i++, s += 2)
            acc(d++, W16(t, s));
    } else if (in * 2 == out) {
        for (int32_t i = 0; i < in - 1; i++, s += 2, d += 2) {
            acc(d, W16(t, s));
            acc(d + 1, (W16(t, s + 2) + W16(t, s)) >> 1);
        }
        acc(d, W16(t, s));
        acc(d + 1, W16(t, s));
    } else if (out * 2 == in) {
        for (int32_t i = 0; i < out; i++, s += 4)
            acc(d++, W16(t, s));
    } else {
        int32_t e = -in;
        for (int32_t i = 0; i < out; i++) {
            acc(d++, W16(t, s));
            for (e += in; e >= 0; e -= out)
                s += 2;
        }
    }
}

/* 0x4e4340: 8-bit stereo -> mono. Same rate and 2:1 take the left channel
 * only; the other cases average both. */
static void mix_8s_m(const uint8_t *s, int32_t in, int32_t out, int16_t *d, const int16_t *t)
{
    if (in == out) {
        for (int32_t i = 0; i < in; i++, s += 2)
            acc(d++, (int16_t)(A8(t, s) * 2) >> 1);
    } else if (in * 2 == out) {
        for (int32_t i = 0; i < in - 1; i++, s += 2, d += 2) {
            acc(d, (A8(t, s + 1) + A8(t, s)) >> 1);
            acc(d + 1, (((A8(t, s + 3) + A8(t, s + 1)) >> 1) + ((A8(t, s + 2) + A8(t, s)) >> 1)) >> 1);
        }
        int v = (A8(t, s + 1) + A8(t, s)) >> 1;
        acc(d, v);
        acc(d + 1, v);
    } else if (out * 2 == in) {
        for (int32_t i = 0; i < out; i++, s += 4)
            acc(d++, (int16_t)(A8(t, s) * 2) >> 1);
    } else {
        int32_t e = -in;
        for (int32_t i = 0; i < out; i++) {
            acc(d++, (A8(t, s + 1) + A8(t, s)) >> 1);
            for (e += in; e >= 0; e -= out)
                s += 2;
        }
    }
}

/* 0x4e4500: 12-bit stereo -> mono (one frame = one packed pair, L R) */
static void mix_12s_m(const uint8_t *s, int32_t in, int32_t out, int16_t *d, const int16_t *t)
{
    if (in == out) {
        for (int32_t i = 0; i < in; i++, s += 3)
            acc(d++, (S12B(t, s) + S12A(t, s)) >> 1);
    } else if (in * 2 == out) {
        for (int32_t i = 0; i < in - 1; i++, s += 3, d += 2) {
            int a = S12A(t, s), b = S12B(t, s);
            acc(d, (a + b) >> 1);
            acc(d + 1, (((S12B(t, s + 3) + b) >> 1) + ((S12A(t, s + 3) + a) >> 1)) >> 1);
        }
        int v = (S12B(t, s) + S12A(t, s)) >> 1;
        acc(d, v);
        acc(d + 1, v);
    } else if (out * 2 == in) {
        for (int32_t i = 0; i < out; i++, s += 6)
            acc(d++, (S12B(t, s) + S12A(t, s)) >> 1);
    } else {
        int32_t e = -in;
        for (int32_t i = 0; i < out; i++) {
            acc(d++, (S12B(t, s) + S12A(t, s)) >> 1);
            for (e += in; e >= 0; e -= out)
                s += 3;
        }
    }
}

/* 0x4e4750: 16-bit stereo -> mono */
static void mix_16s_m(const uint8_t *s, int32_t in, int32_t out, int16_t *d, const int16_t *t)
{
    if (in == out) {
        for (int32_t i = 0; i < in; i++, s += 4)
            acc(d++, (W16(t, s + 2) + W16(t, s)) >> 1);
    } else if (in * 2 == out) {
        for (int32_t i = 0; i < in - 1; i++, s += 4, d += 2) {
            acc(d, (W16(t, s) + W16(t, s + 2)) >> 1);
            acc(d + 1, (((W16(t, s + 6) + W16(t, s + 2)) >> 1) + ((W16(t, s + 4) + W16(t, s)) >> 1)) >> 1);
        }
        int v = (W16(t, s) + W16(t, s + 2)) >> 1;
        acc(d, v);
        acc(d + 1, v);
    } else if (out * 2 == in) {
        for (int32_t i = 0; i < out; i++, s += 8)
            acc(d++, (W16(t, s + 2) + W16(t, s)) >> 1);
    } else {
        int32_t e = -in;
        for (int32_t i = 0; i < out; i++) {
            acc(d++, (W16(t, s + 2) + W16(t, s)) >> 1);
            for (e += in; e >= 0; e -= out)
                s += 4;
        }
    }
}

/* 0x4e4b20: 8-bit mono -> stereo through left/right tables. (Its stepping
 * case advances on e > 0 where the others use e >= 0.) */
static void mix_8m_s(const uint8_t *s, int32_t in, int32_t out, int16_t *d, const int16_t *l, const int16_t *r)
{
    if (in == out) {
        if (!g_radio_chatter) {
            for (int32_t i = 0; i < in; i++, s++, d += 2) {
                acc(d, A8(l, s));
                acc(d + 1, A8(r, s));
            }
        } else {
            int sum = rc_byte(s, 0, in) + rc_byte(s, 1, in) + rc_byte(s, 2, in) + rc_byte(s, 3, in) - 0x200;
            for (int32_t i = 0; i < in; i++, d += 2) {
                int b = s[i];
                int v = rc_index(b - (sum >> 2));
                sum = sum - b + rc_byte(s, i + 4, in);
                acc(d, l[v] * 4);
                acc(d + 1, r[v] * 4);
            }
        }
    } else if (in * 2 == out) {
        for (int32_t i = 0; i < in - 1; i++, s++, d += 4) {
            acc(d, A8(l, s));
            acc(d + 1, A8(r, s));
            acc(d + 2, (A8(l, s + 1) + A8(l, s)) >> 1);
            acc(d + 3, (A8(r, s + 1) + A8(r, s)) >> 1);
        }
        acc(d, A8(l, s));
        acc(d + 1, A8(r, s));
        acc(d + 2, A8(l, s));
        acc(d + 3, A8(r, s));
    } else if (out * 2 == in) {
        for (int32_t i = 0; i < out; i++, s += 2, d += 2) {
            acc(d, A8(l, s));
            acc(d + 1, A8(r, s));
        }
    } else {
        int32_t e = -in;
        for (int32_t i = 0; i < out; i++, d += 2) {
            acc(d, A8(l, s));
            acc(d + 1, A8(r, s));
            for (e += in; e > 0; e -= out)
                s++;
        }
    }
}

/* 0x4e4db0: 12-bit mono -> stereo */
static void mix_12m_s(const uint8_t *s, int32_t in, int32_t out, int16_t *d, const int16_t *l, const int16_t *r)
{
    if (in == out) {
        for (int32_t i = 0; i < in / 2; i++, s += 3, d += 4) {
            acc(d, S12A(l, s));
            acc(d + 1, S12A(r, s));
            acc(d + 2, S12B(l, s));
            acc(d + 3, S12B(r, s));
        }
    } else if (in * 2 == out) {
        int32_t pairs = in / 2;
        for (int32_t i = 0; i < pairs - 1; i++, s += 3, d += 8) {
            int al = S12A(l, s), ar = S12A(r, s), bl = S12B(l, s), br = S12B(r, s);
            acc(d, al);
            acc(d + 1, ar);
            acc(d + 2, (al + bl) >> 1);
            acc(d + 3, (ar + br) >> 1);
            acc(d + 4, bl);
            acc(d + 5, br);
            acc(d + 6, (S12A(l, s + 3) + bl) >> 1);
            acc(d + 7, (S12A(r, s + 3) + br) >> 1);
        }
        int al = S12A(l, s), ar = S12A(r, s), bl = S12B(l, s), br = S12B(r, s);
        acc(d, al);
        acc(d + 1, ar);
        acc(d + 2, (bl + al) >> 1);
        acc(d + 3, (br + ar) >> 1);
        acc(d + 4, bl);
        acc(d + 5, br);
        acc(d + 6, bl);
        acc(d + 7, br);
    } else if (out * 2 == in) {
        for (int32_t i = 0; i < out; i++, s += 3, d += 2) {
            acc(d, S12A(l, s));
            acc(d + 1, S12A(r, s));
        }
    } else {
        int half = 0;
        int32_t e = -in;
        for (int32_t i = 0; i < out; i++, d += 2) {
            acc(d, half ? S12B(l, s) : S12A(l, s));
            acc(d + 1, half ? S12B(r, s) : S12A(r, s));
            for (e += in; e > 0; e -= out) {
                half ^= 1;
                if (!half)
                    s += 3;
            }
        }
    }
}

/* 0x4e5180: 16-bit mono -> stereo */
static void mix_16m_s(const uint8_t *s, int32_t in, int32_t out, int16_t *d, const int16_t *l, const int16_t *r)
{
    if (in == out) {
        for (int32_t i = 0; i < in; i++, s += 2, d += 2) {
            acc(d, W16(l, s));
            acc(d + 1, W16(r, s));
        }
    } else if (in * 2 == out) {
        for (int32_t i = 0; i < in - 1; i++, s += 2, d += 4) {
            acc(d, W16(l, s));
            acc(d + 1, W16(r, s));
            acc(d + 2, (W16(l, s + 2) + W16(l, s)) >> 1);
            acc(d + 3, (W16(r, s + 2) + W16(r, s)) >> 1);
        }
        acc(d, W16(l, s));
        acc(d + 1, W16(r, s));
        acc(d + 2, W16(l, s));
        acc(d + 3, W16(r, s));
    } else if (out * 2 == in) {
        for (int32_t i = 0; i < out; i++, s += 4, d += 2) {
            acc(d, W16(l, s));
            acc(d + 1, W16(r, s));
        }
    } else {
        int32_t e = -in;
        for (int32_t i = 0; i < out; i++, d += 2) {
            acc(d, W16(l, s));
            acc(d + 1, W16(r, s));
            for (e += in; e > 0; e -= out)
                s += 2;
        }
    }
}

/* 0x4e5570: 8-bit stereo -> stereo */
static void mix_8s_s(const uint8_t *s, int32_t in, int32_t out, int16_t *d, const int16_t *t)
{
    if (in == out) {
        for (int32_t i = 0; i < in; i++, s += 2, d += 2) {
            acc(d, A8(t, s));
            acc(d + 1, A8(t, s + 1));
        }
    } else if (in * 2 == out) {
        for (int32_t i = 0; i < in - 1; i++, s += 2, d += 4) {
            acc(d, A8(t, s));
            acc(d + 1, A8(t, s + 1));
            acc(d + 2, (A8(t, s + 2) + A8(t, s)) >> 1);
            acc(d + 3, (A8(t, s + 3) + A8(t, s + 1)) >> 1);
        }
        acc(d, A8(t, s));
        acc(d + 1, A8(t, s + 1));
        acc(d + 2, A8(t, s));
        acc(d + 3, A8(t, s + 1));
    } else if (out * 2 == in) {
        for (int32_t i = 0; i < out; i++, s += 4, d += 2) {
            acc(d, A8(t, s));
            acc(d + 1, A8(t, s + 1));
        }
    } else {
        int32_t e = -in;
        for (int32_t i = 0; i < out; i++, d += 2) {
            acc(d, A8(t, s));
            acc(d + 1, A8(t, s + 1));
            for (e += in; e >= 0; e -= out)
                s += 2;
        }
    }
}

/* 0x4e5740: 12-bit stereo -> stereo */
static void mix_12s_s(const uint8_t *s, int32_t in, int32_t out, int16_t *d, const int16_t *t)
{
    if (in == out) {
        for (int32_t i = 0; i < in; i++, s += 3, d += 2) {
            acc(d, S12A(t, s));
            acc(d + 1, S12B(t, s));
        }
    } else if (in * 2 == out) {
        for (int32_t i = 0; i < in - 1; i++, s += 3, d += 4) {
            int a = S12A(t, s), b = S12B(t, s);
            acc(d, a);
            acc(d + 1, b);
            acc(d + 2, (S12A(t, s + 3) + a) >> 1);
            acc(d + 3, (S12B(t, s + 3) + b) >> 1);
        }
        int a = S12A(t, s), b = S12B(t, s);
        acc(d, a);
        acc(d + 1, b);
        acc(d + 2, a);
        acc(d + 3, b);
    } else if (out * 2 == in) {
        for (int32_t i = 0; i < out; i++, s += 6, d += 2) {
            acc(d, S12A(t, s));
            acc(d + 1, S12B(t, s));
        }
    } else {
        int32_t e = -in;
        for (int32_t i = 0; i < out; i++, d += 2) {
            acc(d, S12A(t, s));
            acc(d + 1, S12B(t, s));
            for (e += in; e >= 0; e -= out)
                s += 3;
        }
    }
}

/* 0x4e59a0: 16-bit stereo -> stereo */
static void mix_16s_s(const uint8_t *s, int32_t in, int32_t out, int16_t *d, const int16_t *t)
{
    if (in == out) {
        for (int32_t i = 0; i < in; i++, s += 4, d += 2) {
            acc(d, W16(t, s));
            acc(d + 1, W16(t, s + 2));
        }
    } else if (in * 2 == out) {
        for (int32_t i = 0; i < in - 1; i++, s += 4, d += 4) {
            acc(d, W16(t, s));
            acc(d + 1, W16(t, s + 2));
            acc(d + 2, (W16(t, s + 4) + W16(t, s)) >> 1);
            acc(d + 3, (W16(t, s + 6) + W16(t, s + 2)) >> 1);
        }
        acc(d, W16(t, s));
        acc(d + 1, W16(t, s + 2));
        acc(d + 2, W16(t, s));
        acc(d + 3, W16(t, s + 2));
    } else if (out * 2 == in) {
        for (int32_t i = 0; i < out; i++, s += 8, d += 2) {
            acc(d, W16(t, s));
            acc(d + 1, W16(t, s + 2));
        }
    } else {
        int32_t e = -in;
        for (int32_t i = 0; i < out; i++, d += 2) {
            acc(d, W16(t, s));
            acc(d + 1, W16(t, s + 2));
            for (e += in; e >= 0; e -= out)
                s += 4;
        }
    }
}

/* 0x4e3950: adds in_frames of src (resampled to out_frames) at out_offset
 * frames into the mix buffer, at volume vol (0..127) and pan (0..127). */
void mixer_mix(const uint8_t *src, int32_t in_frames, int word_size, int channels, int32_t out_frames,
               int32_t out_offset, int vol, int pan)
{
    if (!g_mix_buf || !src || in_frames == 0)
        return;
    int level = vol >> 3;
    if (vol != 0)
        level++;
    if (level > 16)
        level = 16;
    if (channels == 1 && g_out_channels == 2) {
        int pos = (pan >> 3) - 8;
        if (pan > 0x40)
            pos = (pan >> 3) - 7;
        int ll = g_pan_table[level][8 - pos];
        int rl = g_pan_table[level][8 + pos];
        int16_t *d = g_mix_buf + out_offset * 2;
        if (word_size == 8)
            mix_8m_s(src, in_frames, out_frames, d, g_amp8 + ll * 256, g_amp8 + rl * 256);
        else if (word_size == 12)
            mix_12m_s(src, in_frames, out_frames, d, g_amp12 + ll * 4096, g_amp12 + rl * 4096);
        else
            mix_16m_s(src, in_frames, out_frames, d, g_amp12 + ll * 4096, g_amp12 + rl * 4096);
        return;
    }
    const int16_t *t = word_size == 8 ? g_amp8 + level * 256 : g_amp12 + level * 4096;
    if (g_out_channels == 1) {
        int16_t *d = g_mix_buf + out_offset;
        if (channels == 1) {
            if (word_size == 8)
                mix_8m_m(src, in_frames, out_frames, d, t);
            else if (word_size == 12)
                mix_12m_m(src, in_frames, out_frames, d, t);
            else
                mix_16m_m(src, in_frames, out_frames, d, t);
        } else {
            if (word_size == 8)
                mix_8s_m(src, in_frames, out_frames, d, t);
            else if (word_size == 12)
                mix_12s_m(src, in_frames, out_frames, d, t);
            else
                mix_16s_m(src, in_frames, out_frames, d, t);
        }
        return;
    }
    int16_t *d = g_mix_buf + out_offset * 2;
    if (word_size == 8)
        mix_8s_s(src, in_frames, out_frames, d, t);
    else if (word_size == 12)
        mix_12s_s(src, in_frames, out_frames, d, t);
    else
        mix_16s_s(src, in_frames, out_frames, d, t);
}

static inline int soft_index(int v)
{
    if (v < -g_soft_half)
        return -g_soft_half;
    return v >= g_soft_half ? g_soft_half - 1 : v;
}

/* 0x4e3b80: mix buffer -> output samples through the soft-clip table.
 * Port: out-of-range sums are clamped to the table (the original read
 * outside it). */
int mixer_output(void *out, int32_t frames)
{
    if (!g_mix_buf || !out || frames == 0)
        return IMUSE_ERR;
    int32_t n = g_out_channels == 2 ? frames * 2 : frames;
    const int16_t *m = g_mix_buf;
    int swap = g_stereo_reverse && g_out_channels != 1;
    if (g_out_word_size == 16) {
        const int16_t *mid = g_soft_mid;
        int16_t *o = out;
        if (!swap) {
            for (int32_t i = 0; i < n; i++)
                o[i] = mid[soft_index(m[i])];
        } else {
            for (int32_t i = 0; i < n; i += 2) {
                o[i] = mid[soft_index(m[i + 1])];
                o[i + 1] = mid[soft_index(m[i])];
            }
        }
    } else {
        const uint8_t *mid = g_soft_mid;
        uint8_t *o = out;
        if (!swap) {
            for (int32_t i = 0; i < n; i++)
                o[i] = mid[soft_index(m[i])];
        } else {
            for (int32_t i = 0; i < n; i += 2) {
                o[i] = mid[soft_index(m[i + 1])];
                o[i + 1] = mid[soft_index(m[i])];
            }
        }
    }
    return 0;
}

/* For tests: the pan table cell [vol level][position]. */
int imuse_mixer_pan_level(int level, int pos);
int imuse_mixer_pan_level(int level, int pos)
{
    return g_pan_table[level][pos];
}
