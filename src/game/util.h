/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Shared utilities of the simulation (0x42a6c3-0x42b101 in ALIFE.EXE, plus
 * the CRT rand it builds on): the random number generator, the bignum used
 * for money and SOUL pools, overflow-safe a*b/c, and number formatting.
 *
 * All of these reproduce the original bit for bit, including its quirks,
 * because the simulation's behaviour (and comparisons against the original)
 * depends on them.
 */
#ifndef GAME_UTIL_H
#define GAME_UTIL_H

#include <stddef.h>
#include <stdint.h>

#include "game/state.h" /* Bignum */

/* ---- Random numbers -------------------------------------------------------
 *
 * The game uses the MSVC CRT rand() (0x4b1202): one global 32-bit state
 * (0x670e84 rand_holdrand, initially 1), state = state * 214013 + 2531011,
 * result (state >> 16) & 0x7fff. ALIFE.EXE never calls srand. Instead
 * tile_load_all (0x409a4d) discards clock() % 100000 values once at
 * startup, so a real run's sequence depends on how long startup took.
 * The port is deterministic: the state starts at 1 and only changes through
 * these functions; rng_warmup() is that startup step with an explicit count.
 *
 * Every consumer of the original's rand must draw through here, in the same
 * order, for runs to stay comparable: rand_range, and direct rand() calls in
 * 0x4456be, 0x445785 (planet SOULs), 0x45c038 (road walk), 0x497bbe
 * (planet) and the AKOS interpreter (0x4d6b14, opcode 0xc082). */

#define RNG_INITIAL_STATE 1u

void rng_seed(uint32_t state); /* set the raw state (srand semantics) */
uint32_t rng_state(void);      /* raw state, e.g. to save a replay point */
void rng_warmup(uint32_t n);   /* 0x409a4d: discard n values */
int crt_rand(void);            /* 0x4b1202 rand: 0..0x7fff */

/* 0x42b088: uniform-ish integer in [lo, hi] (swapped if hi < lo). Ranges of
 * 0x8000 or more combine two draws as (first << 16) + second. The result is
 * lo + r % (hi - lo + 1) with x86 signed division, so a range that
 * overflows int behaves like the original (except that the port returns lo
 * where the original would divide by zero). */
int rand_range(int lo, int hi);

/* ---- Bignum ---------------------------------------------------------------
 *
 * Sign and magnitude in {int hi; u32 lo} (state.h Bignum). Adding/subtracting
 * is by an unsigned 32-bit amount. Faithful to the original, including that
 * results are only right while the magnitude of a negative number stays
 * below 2^32. */

void bignum_add(Bignum *b, uint32_t v); /* 0x42a950 */
void bignum_sub(Bignum *b, uint32_t v); /* 0x42a9ec */
int bignum_less(Bignum b, int32_t v);   /* 0x42ac32: b < v */
int bignum_ge(Bignum a, Bignum b);      /* 0x42aaa8: a >= b */
int bignum_gt(Bignum a, Bignum b);      /* 0x42ab2f: a > b */
/* 0x42abb6: b >= v for v >= 1; for v <= 0 just "b is not negative"
 * (so it's wrong for negative b against negative v, as in the original). */
int bignum_ge_int(Bignum b, int32_t v);
/* For tests and display: the exact value of a well-formed bignum, and the
 * bignum of a value with magnitude < 2^63. */
static inline int64_t bignum_to_i64(Bignum b) { return bignum_value(b); }
Bignum bignum_from_i64(int64_t v);

/* ---- a * c / b --------------------------------------------------------------
 *
 * Both compute a * c / b in 32 bits, rounding to nearest (remainder > b/2
 * rounds up). If a * c would overflow, all three are halved until it fits
 * and the quotient is shifted back (the rounding increment is not). 0 if b
 * or c is (or becomes) 0. */

/* 0x42af95: the result is clamped to c (so a/b is treated as a fraction). */
uint32_t mul_div_clamp(uint32_t a, uint32_t b, uint32_t c);
/* 0x42ae96: clamped to c only when a <= b (after the halving). */
uint32_t mul_div_round(uint32_t a, uint32_t b, uint32_t c);

/* ---- Number formatting --------------------------------------------------------
 *
 * The original writes into static buffers (0x4f3170, 0x4f30b8); here the
 * caller passes one of at least FMT_NUM_LEN bytes. Both return it. */

#define FMT_NUM_LEN 24

/* 0x42ad0c: decimal with ',' between groups of three ("1,234,567"; "0"). */
char *fmt_thousands(char *buf, uint32_t v);
/* 0x42aded: fmt_thousands, or above 1,000,000 the thousands with a "K"
 * suffix ("1,500K"). The original's "M" branch (> 10^9) is unreachable. */
char *fmt_thousands_k(char *buf, uint32_t v);

#endif
