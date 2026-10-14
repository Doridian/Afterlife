/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Shared simulation utilities: game/util.h.
 */
#include "game/util.h"

#include <stdio.h>
#include <string.h>

/* ---- Random numbers ------------------------------------------------------- */

/* 0x670e84 rand_holdrand */
static uint32_t g_rand_state = RNG_INITIAL_STATE;

void rng_seed(uint32_t state) { g_rand_state = state; }

uint32_t rng_state(void) { return g_rand_state; }

/* 0x4b1202 rand (MSVC CRT) */
int crt_rand(void)
{
    g_rand_state = g_rand_state * 214013u + 2531011u;
    return (int)((g_rand_state >> 16) & 0x7fff);
}

/* 0x409a4d (in tile_load_all): for (i = 0; i < clock() % 100000; i++) rand(); */
void rng_warmup(uint32_t n)
{
    while (n--)
        crt_rand();
}

/* 0x42b088 */
int rand_range(int lo, int hi)
{
    if (hi < lo) {
        int t = lo;
        lo = hi;
        hi = t;
    }
    /* 32-bit wrapping arithmetic like the original's SUB/INC */
    uint32_t un = (uint32_t)hi - (uint32_t)lo + 1u;
    int32_t n = un <= INT32_MAX ? (int32_t)un : -(int32_t)(~un) - 1;
    int32_t r;
    if (n < 0x8000) {
        r = crt_rand();
    } else {
        uint32_t h = (uint32_t)crt_rand() << 16; /* first draw is the high half */
        r = (int32_t)(h + (uint32_t)crt_rand()); /* <= 0x7fff7fff, no overflow */
    }
    if (n == 0)
        return lo; /* the original faults (IDIV by zero) */
    return (int)((uint32_t)lo + (uint32_t)(r % n));
}

/* ---- Bignum ----------------------------------------------------------------- */

/* 0x42a950 */
void bignum_add(Bignum *b, uint32_t v)
{
    if (v == 0)
        return;
    if (b->hi < 0) {
        /* negative: magnitude lo shrinks, or the sign flips */
        if (v < b->lo) {
            b->lo -= v;
        } else {
            b->hi++;
            b->lo = v - b->lo;
        }
    } else if (v - 1u < ~b->lo) { /* lo + v doesn't carry */
        b->lo += v;
    } else {
        b->hi++;
        b->lo += v;
    }
}

/* 0x42a9ec */
void bignum_sub(Bignum *b, uint32_t v)
{
    if (v == 0)
        return;
    if (b->hi < 0) {
        /* negative: the magnitude grows */
        if (v - 1u < ~b->lo) {
            b->lo += v;
        } else {
            b->hi--;
            b->lo += v;
        }
    } else if (b->lo < v) {
        b->hi--;
        if (b->hi < 0)
            b->lo = v - b->lo; /* crossed zero */
        else
            b->lo -= v; /* borrow */
    } else {
        b->lo -= v;
    }
}

/* 0x42ac32 */
int bignum_less(Bignum b, int32_t v)
{
    if (b.hi >= 1)
        return 0;
    if (b.hi < -1)
        return 1;
    if (v < 0) {
        if (b.hi == 0)
            return 0;
        return (uint32_t)0 - (uint32_t)v < b.lo; /* magnitude above |v| */
    }
    if (b.hi < 0)
        return 1;
    return b.lo < (uint32_t)v;
}

/* 0x42aaa8 */
int bignum_ge(Bignum a, Bignum b)
{
    if (b.hi < a.hi)
        return 1;
    if (a.hi < b.hi)
        return 0;
    if (a.hi < 0)
        return !(b.lo < a.lo);
    return !(a.lo < b.lo);
}

/* 0x42ab2f */
int bignum_gt(Bignum a, Bignum b)
{
    if (b.hi < a.hi)
        return 1;
    if (a.hi < b.hi)
        return 0;
    if (a.hi < 0)
        return a.lo < b.lo;
    return b.lo < a.lo;
}

/* 0x42abb6 */
int bignum_ge_int(Bignum b, int32_t v)
{
    if (v < 1)
        return b.hi >= 0;
    if (b.hi < 0)
        return 0;
    if (b.hi < 1 && b.lo < (uint32_t)v)
        return 0;
    return 1;
}

Bignum bignum_from_i64(int64_t v)
{
    Bignum b;
    if (v >= 0) {
        b.hi = (int32_t)((uint64_t)v >> 32);
        b.lo = (uint32_t)v;
    } else {
        uint64_t m = (uint64_t)0 - (uint64_t)v;
        b.hi = -(int32_t)(m >> 32) - 1;
        b.lo = (uint32_t)m;
    }
    return b;
}

/* ---- a * c / b ---------------------------------------------------------------- */

/* The shared body of 0x42ae96 / 0x42af95. Returns 0 for "result 0" exits
 * (with *early set), else the rounded quotient; *pa / *pb get the values
 * after the halving, for the final clamp. */
static uint32_t mul_div_core(uint32_t *pa, uint32_t *pb, uint32_t c, int *early)
{
    uint32_t a = *pa, b = *pb;
    uint32_t prod, q;
    unsigned shift = 0;
    *early = 0;
    if (c == 0 || b == 0) {
        *early = 1;
        return 0;
    }
    if (a < 0xffffffffu / c) {
        prod = a * c;
        q = prod / b;
    } else {
        do {
            a >>= 1;
            b >>= 1;
            c >>= 1;
            if (c == 0 || b == 0) {
                *early = 1;
                return 0;
            }
            shift++;
        } while (0xffffffffu / c < a);
        prod = a * c;
        q = (prod / b) << (shift & 31);
    }
    if (b >> 1 < prod % b)
        q++;
    *pa = a;
    *pb = b;
    return q;
}

/* 0x42af95 */
uint32_t mul_div_clamp(uint32_t a, uint32_t b, uint32_t c)
{
    int early;
    uint32_t q = mul_div_core(&a, &b, c, &early);
    if (!early && c < q)
        q = c;
    return q;
}

/* 0x42ae96 */
uint32_t mul_div_round(uint32_t a, uint32_t b, uint32_t c)
{
    int early;
    uint32_t q = mul_div_core(&a, &b, c, &early);
    if (!early && c < q && a <= b)
        q = c;
    return q;
}

/* ---- Number formatting ---------------------------------------------------------- */

/* 0x42ad0c */
char *fmt_thousands(char *buf, uint32_t v)
{
    size_t n = 0;
    int started = 0;
    strcpy(buf, "0");
    for (uint32_t div = 1000000000u; div != 0; div /= 1000) {
        uint32_t q = v / div;
        if (started)
            n += (size_t)snprintf(buf + n, FMT_NUM_LEN - n, "%03u", (unsigned)q);
        else if (q != 0) {
            started = 1;
            n = (size_t)snprintf(buf, FMT_NUM_LEN, "%u", (unsigned)q);
        }
        if (div != 1 && started)
            n += (size_t)snprintf(buf + n, FMT_NUM_LEN - n, ",");
        v %= div;
    }
    return buf;
}

/* 0x42aded */
char *fmt_thousands_k(char *buf, uint32_t v)
{
    if (v <= 1000000u)
        return fmt_thousands(buf, v);
    char tmp[FMT_NUM_LEN];
    snprintf(buf, FMT_NUM_LEN, "%sK", fmt_thousands(tmp, v / 1000));
    return buf;
}
