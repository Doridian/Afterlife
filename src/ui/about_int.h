/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The about box's credits script (ui/about_credits.c), run by ui/about.c.
 *
 * In ALIFE.EXE the credits are code, not data: about_credits_restart
 * (0x4addfb) is a long run of credits_add calls with two rand_range
 * switches that pick one of several lines. Here that sequence is a list of
 * ops; targets are op indexes, comments give each call's address.
 */
#ifndef AFTERLIFE_UI_ABOUT_INT_H
#define AFTERLIFE_UI_ABOUT_INT_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    CREDITS_ADD,    /* credits_add(kind, text, x, y, arg or font slot, flags) */
    CREDITS_RAND,   /* var = rand_range(lo, hi) */
    CREDITS_SWITCH, /* var < n_cases ? goto cases[var] : goto target (unsigned) */
    CREDITS_GOTO,   /* goto target */
    CREDITS_END,
} CreditsOpKind;

typedef struct {
    CreditsOpKind op;
    int kind;         /* ADD */
    const char *text; /* ADD of a text entry */
    int32_t x, y;     /* ADD */
    int font;         /* ADD: 0..2 = the font slot (small1, small2, big) as arg; -1 = `arg` */
    int32_t arg;      /* ADD */
    uint32_t flags;   /* ADD */
    int var;          /* RAND, SWITCH: the local variable (frame offset) */
    int32_t lo, hi;   /* RAND */
    int target;       /* SWITCH (default), GOTO: an op index */
    int n_cases;      /* SWITCH */
    int cases[8];     /* SWITCH: op indexes */
} CreditsOp;

extern const CreditsOp credits_script[];
extern const size_t credits_script_len;

#endif
