/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Simulation constant tables: game/tables.h.
 */
#include "game/tables.h"

#include <string.h>

SimTables g_tables;

static void spec(BuildingSpec *t, int n, int dup, uint32_t jobs, uint32_t cap, uint32_t cost, uint8_t size)
{
    for (int i = 0; i < dup; i++) {
        t[n * dup + i].jobs = jobs;
        t[n * dup + i].capacity = cap;
        t[n * dup + i].cost = cost;
        t[n * dup + i].size = size;
    }
}

/* 0x45546c game_init_defaults (tables) and 0x4535db game_reset_world
 * (0x6391d0 soul-rate curve) */
void tables_init_defaults(void)
{
    SimTables *t = &g_tables;
    memset(t, 0, sizeof *t);

    spec(t->set7, 0, 2, 0, 22222, 100000, 4); /* 0x6304a0 */
    spec(t->set7, 1, 2, 0, 222222, 1000000, 4);
    spec(t->set7, 2, 2, 0, 2222222, 10000000, 4);
    spec(t->set7, 3, 2, 0, 22222222, 100000000, 4);

    spec(t->set17, 0, 1, 500, 200, 10000, 3); /* 0x5f7410, repeated at 0x5f7437 */
    spec(t->set17, 1, 1, 10000, 10000, 500000, 3);
    spec(t->set17, 2, 1, 200000, 500000, 25000000, 3);
    for (int i = 0; i < 3; i++)
        t->set17[3 + i] = t->set17[i];

    spec(t->set13, 0, 2, 2500, 2000, 50000, 3); /* 0x630200 */
    spec(t->set13, 1, 2, 5000, 20000, 500000, 3);
    spec(t->set13, 2, 2, 10000, 200000, 5000000, 4);
    spec(t->set13, 3, 2, 25000, 2000000, 50000000, 4);

    spec(t->set9, 0, 2, 2000, 50000, 250000, 3); /* 0x57e0b0 */
    spec(t->set9, 1, 2, 10000, 200000, 1000000, 3);
    spec(t->set9, 2, 2, 40000, 2000000, 10000000, 4);

    spec(t->set8, 0, 2, 100, 100, 100000, 3); /* 0x57e110 */
    spec(t->set8, 1, 2, 250, 250, 250000, 3);
    spec(t->set8, 2, 2, 1000, 1000, 1000000, 3);

    spec(t->set5, 0, 2, 0, 250000000, 200000000, 5); /* 0x59cec0 */
    spec(t->set5, 1, 2, 0, 500000000, 400000000, 6);
    spec(t->set5, 2, 2, 0, 1000000000, 1000000000, 7);

    spec(t->set19, 0, 2, 10, 1000000, 100000, 1); /* 0x630280 */
    spec(t->set19, 1, 2, 200, 10000000, 1000000, 2);

    spec(t->set30, 0, 1, 10000, 750, 30000, 2); /* 0x57e030 */
    spec(t->set30, 1, 1, 1000000, 15000, 120000, 3);
    spec(t->set30, 2, 1, 100000000, 300000, 4800000, 4);

    static const uint8_t zl[13][2] = {{0, 0},       {0, 6},       {7, 9},       {10, 12},     {13, 15},
                                      {0, 8},       {9, 0x13},    {0x14, 0x1b}, {0x1c, 0x20}, {0x21, 0x29},
                                      {0x2a, 0x32}, {0x33, 0x3c}, {0x5c, 0x87}};
    static const uint8_t sl[21][2] = {{0, 0},       {0, 6},       {7, 9},       {10, 12},     {13, 15},
                                      {0, 3},       {4, 8},       {9, 0xe},     {0xf, 0x13},  {0x14, 0x17},
                                      {0x18, 0x1b}, {0x1c, 0x20}, {0x21, 0x25}, {0x26, 0x29}, {0x2a, 0x2e},
                                      {0x2f, 0x34}, {0x35, 0x38}, {0x39, 0x3c}, {0x54, 0x68}, {0x69, 0x87},
                                      {0xb8, 0xf0}};
    for (int i = 0; i < 13; i++)
        t->zone_levels[i].lo = zl[i][0], t->zone_levels[i].hi = zl[i][1];
    for (int i = 0; i < 21; i++)
        t->special_levels[i].lo = sl[i][0], t->special_levels[i].hi = sl[i][1];

    static const uint32_t sr[6][3] = {{0, 0x1a, 0x1a},
                                      {5000000, 0x37e, 0x180},
                                      {75000000, 0x400, 0x80},
                                      {200000000, 0x680, 0x140},
                                      {500000000, 0xa00, 0x400},
                                      {1000000000, 0xa00, 0xa00}};
    for (int i = 0; i < 6; i++)
        t->soul_rate[i].souls = sr[i][0], t->soul_rate[i].rate_hi = sr[i][1], t->soul_rate[i].rate_lo = sr[i][2];
}
