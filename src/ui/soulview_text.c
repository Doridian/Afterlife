/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Soulview's texts: a tracked SOUL's name (0x47f8fe) and its life story
 * (0x47adea's text, the template expander 0x47af6f and its helpers
 * 0x47aca3-0x47d018). See ui/soulview.h.
 *
 * Both are pure functions of the tracker's seed (record +8), plus its
 * plane, temporary flag and building's zone kind, the game year (for
 * dates) and the selected plane's sim cells. They draw no random numbers.
 *
 * The story is one of 18 templates (pointer table 0x66a7f0), each a format
 * string with three lists of words picked by seed bits; the result is
 * expanded: "&(XY ...)" inserts an item, mostly a word from a list picked
 * by seed bits (lists of string pointers at the addresses below, all in
 * ALIFE.EXE's data, src/data), sometimes a nested template, a name, a
 * number, a date or one of two alternatives "&(OR (a) (b))". After '.',
 * '?' or '!' the next letter is capitalised; "&(CP)" forces it.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/util.h"
#include "game/world.h"
#include "ui/exe_text.h"
#include "ui/soulview.h"

/* 0x47aca3: n bits of v from bit `shift`, with the original's mask
 * (1 << (shift + n)) - 1 taken mod 32, so a field reaching bit 32 or past
 * it reads (mostly) zeros */
static uint32_t bits(uint32_t v, unsigned shift, unsigned n)
{
    uint32_t m = (1u << ((shift + n) & 31)) - 1u;
    return (v & m) >> (shift & 31);
}

/* 0x47accf: v rotated left by k, as the original does it: k > 31 is taken
 * mod 31 and the right shift is by 31 - k, so bit 31 - k is dropped */
static uint32_t rot(uint32_t v, unsigned k)
{
    k &= 0xff;
    if (k > 31)
        k %= 31;
    if (k != 0)
        v = v << k | v >> ((31 - k) & 31);
    return v;
}

/* ---- the name ------------------------------------------------------------ */

/* 0x47f8fe */
void soulview_soul_name(uint32_t seed, char *out)
{
    if (seed == 0) {
        snprintf(out, SOULVIEW_NAME_LEN, "%s", exe_str(0x66ac40)); /* "No Soul" */
        return;
    }
    out[0] = 0;
    size_t len = 0;
    unsigned n = ((seed >> 29) & 1) + 3;
    for (unsigned i = 0; i < n; i++) {
        unsigned nib = (seed >> ((i * 4) & 31)) & 0xf;
        const char *s = (i & 1) == 0 ? exe_stri(0x66a960, nib % 5) : exe_stri(0x66a998, nib & 3);
        size_t l = strlen(s);
        if (len + l >= SOULVIEW_NAME_LEN)
            l = SOULVIEW_NAME_LEN - 1 - len;
        memcpy(out + len, s, l);
        len += l;
        out[len] = 0;
    }
    out[0] = (char)(out[0] - 0x20);
}

/* ---- the expander -------------------------------------------------------- */

typedef struct Ctx {
    const Rec26 *t;
    uint32_t seed;
    char *end; /* the output buffer's last byte (kept for the NUL) */
} Ctx;

static uint8_t g_off; /* 0x66482c: added to most list picks, set by BS/WS/FB/FW */
static uint8_t g_cap; /* 0x664830: capitalise the next letter */
static uint8_t g_adj; /* 0x66a838: adjectives inserted so far */

static char *put(Ctx *c, char *d, const char *s)
{
    while (*s && d < c->end)
        *d++ = *s++;
    *d = 0;
    return d;
}

/* sprintf(out, fmt, a[0], a[1], a[2]) for the templates' "%s"s */
static void fmt_s(char *out, size_t size, const char *fmt, const char *const *a, int na)
{
    size_t o = 0;
    int k = 0;
    for (const char *p = fmt; *p && o + 1 < size; p++) {
        if (p[0] == '%' && p[1] == 's') {
            const char *s = k < na ? a[k] : "";
            k++;
            p++;
            while (*s && o + 1 < size)
                out[o++] = *s++;
        } else if (p[0] == '%' && p[1] == '%') {
            out[o++] = '%';
            p++;
        } else {
            out[o++] = *p;
        }
    }
    out[o] = 0;
}

/* a word group {char *fmt; u8 n; char *word[n]} (packed, at va):
 * sprintf(out, fmt, word[i % n]) */
static void group_pick(uint32_t va, unsigned i, char *out, size_t size)
{
    unsigned n = exe_u8(va + 4);
    const char *w = n ? exe_strp(va + 5 + 4 * (i % n)) : "";
    fmt_s(out, size, exe_strp(va), &w, 1);
}

static char *expand(Ctx *c, const char *src, char *dst);

static char *expand_group(Ctx *c, uint32_t va, unsigned i, char *dst)
{
    char tmp[1024];
    group_pick(va, i, tmp, sizeof tmp);
    return expand(c, tmp, dst);
}

/* 0x47cd0f: the zone kind a story is about */
static unsigned story_kind(const Rec26 *t)
{
    if (t->unk_00[0] == 0)
        return 0;
    if (t->unk_0c == 0)
        return bits(t->unk_08, 26, 4) % 7;
    return sim_zone_kind(t->unk_00[2] % GS_MAP_W, t->unk_00[3] % GS_MAP_H);
}

/* 0x47cd8a (BS): a good deed of the story's kind */
static char *best_deed(Ctx *c, char *dst)
{
    if (c->t->unk_00[0] == 0)
        return dst;
    return expand_group(c, exe_u32(0x668090 + 4 * story_kind(c->t)), bits(c->seed, 9, 4), dst);
}

/* 0x47ce2d (WS): a bad deed of another kind */
static char *worst_sin(Ctx *c, char *dst)
{
    if (c->t->unk_00[0] == 0)
        return dst;
    unsigned k = (c->t->unk_0c == 0 ? bits(c->seed, 27, 4) : bits(c->seed, 13, 4)) % 7;
    if (story_kind(c->t) == k)
        k = (k + 1) % 7;
    return expand_group(c, exe_u32(0x6680b0 + 4 * k), bits(c->seed, 7, 4), dst);
}

/* 0x47cf66 (FB) */
static char *good_habit(Ctx *c, char *dst)
{
    if (c->t->unk_00[0] == 0)
        return dst;
    unsigned k = bits(c->seed, 20, 2) % 7;
    return expand_group(c, exe_u32(0x668090 + 4 * k), bits(c->seed, 1, 4), dst);
}

/* 0x47d018 (FW) */
static char *bad_habit(Ctx *c, char *dst)
{
    if (c->t->unk_00[0] == 0)
        return dst;
    unsigned a = bits(c->seed, 20, 2) % 7, b = bits(c->seed, 23, 2) % 7;
    if (a == b)
        b = (b + 1) % 7;
    return expand_group(c, exe_u32(0x6680b0 + 4 * b), bits(c->seed, 4, 4), dst);
}

/* 0x47cc60: a noun {char *word; char *singular; u8 plural} (9 bytes at
 * va): the word, then the singular ending for 'S', else the plural one
 * (0x664970: "s", "ies", "es", "") */
static char *noun(Ctx *c, uint32_t va, char *dst, char form)
{
    dst = put(c, dst, exe_strp(va));
    if (form == 'S')
        return put(c, dst, exe_strp(va + 4));
    return put(c, dst, exe_stri(0x664970, exe_u8(va + 8)));
}

/* 0x47cc04: the last character inside the parentheses opened just before p */
static const char *paren_end(const char *p)
{
    int depth = 1;
    const char *q = p;
    for (;;) {
        if (*q == 0)
            return q - 1;
        if (*q == '(')
            depth++;
        else if (*q == ')')
            depth--;
        if (depth == 0)
            return q - 1;
        q++;
    }
}

static const char *after(const char *s, int ch)
{
    const char *p = strchr(s, ch);
    return p ? p + 1 : s + strlen(s);
}

/* a list word: index (bits + offset) mod n */
static unsigned pick(Ctx *c, unsigned shift, unsigned n_bits, unsigned n)
{
    return ((bits(c->seed, shift, n_bits) + g_off) & 0xffff) % n;
}

/* the seed, or for "&(XY k)" the seed rotated by k */
static uint32_t arg_seed(Ctx *c, const char *s) { return *s == ')' ? c->seed : rot(c->seed, (unsigned)atoi(s)); }

#define CODE(a, b) ((uint32_t)(a) << 16 | (uint32_t)(b))

/* 0x47af6f */
static char *expand(Ctx *c, const char *src, char *dst)
{
    for (;;) {
        while (*src != '&') {
            unsigned char ch = (unsigned char)*src;
            if (ch == 0)
                return dst;
            if (dst < c->end)
                *dst++ = g_cap && ch < 0x80 ? (char)toupper(ch) : (char)ch;
            *dst = 0;
            if (ch == '.' || ch == '?' || ch == '!')
                g_cap = 1;
            else if (ch < 0x80 && isalnum(ch))
                g_cap = 0;
            src++;
        }
        const char *op = strchr(src, '(');
        if (!op || !op[1] || !op[2])
            return dst;
        uint32_t code = CODE((unsigned char)op[1], (unsigned char)op[2]);
        src = op + 3;
        char *start = dst;
        char tmp[2048];
        switch (code) {
        case CODE('A', 'J'): { /* adjective; each one rotates the seed further */
            uint32_t v = rot(c->seed, g_adj);
            dst = put(c, dst, exe_stri(0x666510, bits(v, 24, 5) % 21));
            g_adj++;
            break;
        }
        case CODE('A', 'C'):
            dst = put(c, dst, exe_stri(0x6658d8, pick(c, 6, 6, 29)));
            break;
        case CODE('B', 'F'):
            dst = put(c, dst, exe_stri(0x666730, bits(arg_seed(c, src), 13, 3) & 7));
            break;
        case CODE('A', 'N'):
            dst = put(c, dst, exe_stri(0x6665d8, bits(c->seed, 18, 4) % 14));
            break;
        case CODE('C', 'H'):
            dst = put(c, dst, exe_stri(0x665ea0, pick(c, 21, 5, 12)));
            break;
        case CODE('B', 'N'):
            dst = put(c, dst, exe_stri(0x666c38, bits(c->seed, 27, 4) % 9));
            break;
        case CODE('B', 'P'):
            dst = put(c, dst, exe_stri(0x6668a0, bits(arg_seed(c, src), 5, 6) % 41));
            break;
        case CODE('B', 'S'):
            g_off = 0;
            dst = best_deed(c, dst);
            break;
        case CODE('C', 'L'):
            dst = put(c, dst, exe_stri(0x666b70, bits(arg_seed(c, src), 24, 5) % 28));
            break;
        case CODE('C', 'I'):
            dst = put(c, dst, exe_stri(0x666288, pick(c, 21, 5, 11)));
            break;
        case CODE('D', 'I'):
            dst = put(c, dst, exe_stri(0x666418, pick(c, 28, 7, 10)));
            break;
        case CODE('D', 'A'): { /* a date */
            static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            unsigned nb = 0;
            uint32_t y = g_state.year;
            uint32_t ay = (int32_t)y < 0 ? (uint32_t)-(int32_t)y : y;
            for (nb = 32; nb > 0 && !(ay & (1u << (nb - 1))); nb--)
                ;
            unsigned m = bits(c->seed, 14, 4) % 11 + 1;
            /* (the days of the month after m, as in the original) */
            unsigned d = bits(c->seed, 18, 5) % (unsigned)(days[m] - 1) + 1;
            uint32_t yy = bits(c->seed, 16, nb);
            yy = mul_div_clamp(yy, (1u << (nb & 31)) - 1u, y);
            snprintf(tmp, sizeof tmp, exe_fmt(0x66a9d8, "ddd", "%d-%d-%d"), (int)m, (int)d, (int)yy);
            dst = put(c, dst, tmp);
            break;
        }
        case CODE('E', 'D'):
            dst = expand_group(c, exe_u32(0x6680b0 + 4 * (bits(c->seed, 16, 3) % 7)), bits(c->seed, 2, 4), dst);
            break;
        case CODE('D', 'R'):
            dst = put(c, dst, exe_stri(0x6659d8, pick(c, 12, 4, 12)));
            break;
        case CODE('F', 'B'):
            g_off = 2;
            dst = good_habit(c, dst);
            break;
        case CODE('E', 'M'):
            dst = put(c, dst, exe_stri(0x6661d8, pick(c, 29, 5, 10)));
            break;
        case CODE('F', 'R'):
            snprintf(tmp, sizeof tmp, "%s", exe_stri(0x6662f0, pick(c, 26, 5, 5)));
            dst = expand(c, tmp, dst);
            break;
        case CODE('F', 'O'): {
            unsigned i = pick(c, 7, 6, 37);
            char form = *src;
            if (*src)
                src++;
            dst = noun(c, 0x664ef8 + 9 * i, dst, form);
            break;
        }
        case CODE('G', 'D'):
            dst = expand_group(c, exe_u32(0x668090 + 4 * (bits(c->seed, 22, 3) % 7)), bits(c->seed, 4, 4), dst);
            break;
        case CODE('F', 'W'):
            g_off = 3;
            dst = bad_habit(c, dst);
            break;
        case CODE('H', 'O'):
            dst = expand_group(c, 0x667e38, bits(c->seed, 15, 5), dst);
            break;
        case CODE('G', 'O'):
            snprintf(tmp, sizeof tmp, "%s", exe_stri(0x665f20, (bits(c->seed, 13, 4) + g_off) & 7));
            dst = expand(c, tmp, dst);
            break;
        case CODE('L', 'N'):
            dst = put(c, dst, exe_stri(0x666050, pick(c, 22, 6, 31)));
            break;
        case CODE('I', 'N'):
            dst = put(c, dst, exe_stri(0x6651a0, (bits(c->seed, 29, 7) + g_off) & 7));
            break;
        case CODE('N', 'A'): { /* a name: this SOUL's, or another one's */
            uint32_t v = c->seed;
            if (*src != ')') {
                src = op + 4;
                v = rot(v, (unsigned)atoi(src));
            }
            char name[SOULVIEW_NAME_LEN];
            soulview_soul_name(v, name);
            dst = put(c, dst, name);
            break;
        }
        case CODE('M', 'E'):
            dst = put(c, dst, exe_stri(0x665a40, pick(c, 5, 3, 6)));
            break;
        case CODE('N', 'U'): { /* a number lo..hi */
            src = op + 4;
            int lo = atoi(src);
            src = after(src, ' ');
            int hi = atoi(src);
            uint32_t r = mul_div_clamp(bits(c->seed, 0, 10), 0x3ff, (uint32_t)(hi - lo));
            snprintf(tmp, sizeof tmp, "%d", (int)(r + (uint32_t)lo));
            dst = put(c, dst, tmp);
            break;
        }
        case CODE('N', 'O'): {
            unsigned i;
            if (op[3] && op[4] == ')')
                i = pick(c, 10, 6, 35);
            else
                i = rot(c->seed, (unsigned)atoi(op[3] ? op + 5 : op + 3)) % 35;
            char form = *src;
            if (*src)
                src++;
            dst = noun(c, 0x664bb8 + 9 * i, dst, form);
            break;
        }
        case CODE('O', 'R'): { /* one of two parenthesised alternatives */
            const char *a = after(src, '(');
            const char *ae = paren_end(a);
            if (bits(c->seed, 1, 1) == 0) {
                src = paren_end(after(ae, '('));
            } else {
                a = after(ae, '(');
                ae = paren_end(a);
                src = ae;
            }
            if (*src == ')')
                src++;
            src = after(src, ')');
            size_t len = ae >= a ? (size_t)(ae - a + 1) : 0;
            if (len >= sizeof tmp)
                len = sizeof tmp - 1;
            memcpy(tmp, a, len);
            tmp[len] = 0;
            dst = expand(c, tmp, dst);
            break;
        }
        case CODE('O', 'G'):
            dst = put(c, dst, exe_stri(0x666a08, bits(arg_seed(c, src), 22, 5) % 19));
            break;
        case CODE('W', 'S'):
            g_off = 1;
            dst = worst_sin(c, dst);
            break;
        case CODE('Z', 'O'):
            dst = put(c, dst, exe_stri(0x665588, pick(c, 2, 4, 12)));
            break;
        case CODE('V', 'I'): { /* a virtue (0x66aa0c...) */
            static const uint32_t virtue[7] = {0x66aa0c, 0x66aa24, 0x66aa3c, 0x66aa54, 0x66aa68, 0x66aa7c, 0x66aa90};
            dst = put(c, dst, exe_str(virtue[bits(c->seed, 12, 3) % 7]));
            break;
        }
        case CODE('S', 'Y'): { /* a synonym: &(SYxx) */
            uint32_t sub = CODE((unsigned char)op[3], op[3] ? (unsigned char)op[4] : 0);
            uint32_t va = 0;
            unsigned i = 0;
            if (sub == CODE('G', 'O'))
                va = 0x665068, i = pick(c, 6, 4, 3);
            else if (sub == CODE('F', 'O'))
                va = 0x6680f0, i = pick(c, 6, 4, 3);
            else if (sub == CODE('H', 'A'))
                va = 0x665098, i = pick(c, 6, 4, 3);
            else if (sub == CODE('N', 'N'))
                va = 0x6650d0, i = (bits(c->seed, 6, 4) + g_off) & 3;
            else if (sub == CODE('S', 'T'))
                va = 0x665138, i = pick(c, 6, 4, 7);
            if (va)
                dst = put(c, dst, exe_stri(va, i));
            break;
        }
        case CODE('S', 'X'): { /* he / him / his or she / her / her */
            char form = *src;
            if (*src)
                src++;
            uint32_t va;
            if (bits(c->seed, 16, 1) == 0)
                va = form == 'N' || form == 'P' ? 0x66a9f4 : form == 'O' ? 0x66a9f8 : 0x66a9fc;
            else
                va = form == 'N' ? 0x66a9e8 : form == 'O' || form == 'P' ? 0x66a9e4 : 0x66a9f0;
            dst = put(c, dst, exe_str(va));
            break;
        }
        case CODE('S', 'H'):
            dst = put(c, dst, exe_stri(0x6656c8, pick(c, 27, 4, 20)));
            break;
        case CODE('R', 'L'):
            dst = put(c, dst, exe_stri(0x666368, pick(c, 27, 5, 11)));
            break;
        case CODE('R', 'E'):
            dst = put(c, dst, exe_stri(0x665d58, (bits(c->seed, 25, 5) + g_off) & 15));
            break;
        case CODE('R', 'A'):
            dst = put(c, dst, exe_str(c->t->unk_10 == GS_PLANE_HELL ? 0x66aa98 : 0x66aaa0)); /* Hell / Heaven */
            break;
        case CODE('Q', 'O'):
            dst = expand_group(c, 0x668058, bits(c->seed, 15, 4), dst);
            break;
        case CODE('P', 'R'):
            dst = put(c, dst, exe_stri(0x665440, ((bits(arg_seed(c, src), 8, 7) + g_off) & 0xffff) % 55));
            break;
        case CODE('P', 'L'):
            dst = put(c, dst, exe_stri(0x665b88, pick(c, 11, 6, 26)));
            break;
        case CODE('C', 'U'):
            dst = put(c, dst, exe_stri(0x666148, pick(c, 3, 4, 15)));
            break;
        case CODE('C', 'O'):
            dst = put(c, dst, exe_stri(0x6666a0, bits(arg_seed(c, src), 1, 5) % 18));
            break;
        case CODE('C', 'P'):
            g_cap = 1;
            src = after(src, ')');
            continue;
        default:
            break;
        }
        if (g_cap && dst > start && (unsigned char)*start < 0x80)
            *start = (char)toupper((unsigned char)*start);
        src = after(src, ')');
        g_cap = 0;
    }
}

/* 0x47ad32: the story template: fmt with a word from each of its three
 * lists (group {char *fmt; u8 n0, n1, n2; char *word[n0 + n1 + n2]}) */
static void story_template(uint32_t va, uint32_t seed, char *out, size_t size)
{
    unsigned n0 = exe_u8(va + 4), n1 = exe_u8(va + 5), n2 = exe_u8(va + 6);
    const char *w[3] = {"", "", ""};
    if (n0)
        w[0] = exe_strp(va + 7 + 4 * (bits(seed, 10, 4) % n0));
    if (n1)
        w[1] = exe_strp(va + 7 + 4 * (n0 + bits(seed, 11, 4) % n1));
    if (n2)
        w[2] = exe_strp(va + 7 + 4 * (n0 + n1 + bits(seed, 12, 4) % n2));
    fmt_s(out, size, exe_strp(va), w, 3);
}

/* 0x47adea, the text */
size_t soulview_describe(const Rec26 *t, char *out, size_t size)
{
    if (size == 0)
        return 0;
    out[0] = 0;
    if (t->unk_00[0] == 0 || size < 2)
        return 0;
    char tmpl[2048];
    story_template(exe_u32(0x66a7f0 + 4 * (bits(t->unk_08, 0, 5) % 18)), t->unk_08, tmpl, sizeof tmpl);
    Ctx c = {t, t->unk_08, out + size - 1};
    g_off = 0;
    g_cap = 1;
    g_adj = 0;
    char *end = expand(&c, tmpl, out);
    *end = 0;
    return (size_t)(end - out);
}
