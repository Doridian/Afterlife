/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The advisors' speeches (ALIFE.EXE 0x419b65, 0x41b768, 0x41db2b and the
 * quips of 0x4196ae) and the issue labels of 0x41801f.
 *
 * Each of the three original functions is a switch on the issue code
 * whose cases are a fixed sequence of lines, each
 *
 *     if (!silenced && stamp unchanged)       (the first line: !silenced)
 *         snd_talk_start(talker, &text_rect[talker], text, voice, 0x13);
 *         akos_talk_wait(talker);
 *
 * so here they are tables of {talker, text, voice}. The subtitles (some
 * 30 KB) are read from the user's ALIFE.EXE at runtime by their original
 * addresses (exe_res_va); only addresses and voice resource ids are here.
 */
#include <stddef.h>
#include <string.h>

#include "gfx/akos_talk.h"
#include "gfx/exe_res.h"
#include "ui/advisors_int.h"

typedef struct AdvLine {
    uint8_t talker; /* 1 Jasper, 2 Aria */
    uint32_t text;  /* VA of the subtitle in ALIFE.EXE */
    uint32_t voice; /* voice resource (sound slot 199) */
} AdvLine;

#define ADV_DEFAULT 0xff /* the switch's default case */

typedef struct AdvCase {
    uint8_t issue;
    uint8_t first, count; /* lines */
} AdvCase;

/* 0x419b65: one issue in Heaven only */
static const AdvLine k_heaven_lines[] = {
    {2, 0x654750, 0x10125},
    {1, 0x6547b0, 0x10dda},
    {1, 0x656494, 0x10d9c},
    {1, 0x6564d0, 0x10d9d},
    {1, 0x654678, 0x10ecc},
    {2, 0x6546d4, 0x1021f},
    {2, 0x654740, 0x10220},
    {2, 0x6547e0, 0x10126},
    {1, 0x654818, 0x10ddb},
    {2, 0x656324, 0x1019c},
    {2, 0x65636c, 0x1019d},
    {2, 0x654868, 0x10127},
    {1, 0x6548b4, 0x10ddc},
    {2, 0x6548c4, 0x10128},
    {2, 0x6548f4, 0x10129},
    {2, 0x654958, 0x1012a},
    {2, 0x65499c, 0x1012b},
    {1, 0x6549ec, 0x10ddd},
    {2, 0x6560b0, 0x10185},
    {2, 0x6560dc, 0x10186},
    {2, 0x656124, 0x10187},
    {1, 0x656158, 0x10e24},
    {2, 0x6561a0, 0x10188},
    {2, 0x654a50, 0x1012c},
    {2, 0x654aa8, 0x1012d},
    {1, 0x654afc, 0x10dde},
    {2, 0x654b28, 0x1012e},
    {2, 0x654b68, 0x1012f},
    {1, 0x654b94, 0x10ddf},
    {2, 0x654be4, 0x10130},
    {2, 0x654c54, 0x10131},
    {1, 0x654cb4, 0x10de0},
    {1, 0x654d08, 0x10de1},
    {1, 0x654d30, 0x10de2},
    {2, 0x654da0, 0x10132},
    {2, 0x654e00, 0x10133},
    {2, 0x654e5c, 0x10134},
    {1, 0x654ec0, 0x10de3},
    {2, 0x654eec, 0x10135},
    {2, 0x6563ac, 0x1019e},
    {2, 0x6563f0, 0x1019f},
    {2, 0x656444, 0x101a0},
    {2, 0x654ef8, 0x10136},
    {2, 0x654f3c, 0x10137},
    {2, 0x654fa8, 0x10138},
    {2, 0x655024, 0x10139},
    {1, 0x655060, 0x10de4},
    {1, 0x6550dc, 0x10de5},
    {2, 0x655138, 0x1013a},
    {2, 0x6551b0, 0x1013b},
    {1, 0x6551e4, 0x10de6},
    {2, 0x656280, 0x1019a},
    {2, 0x6562d4, 0x1019b},
    {2, 0x65522c, 0x1013c},
    {1, 0x65526c, 0x10de7},
    {2, 0x6552c0, 0x1013d},
    {2, 0x6553b0, 0x10140},
    {2, 0x655414, 0x10141},
    {1, 0x655450, 0x10de8},
    {2, 0x655460, 0x10142},
    {2, 0x6554c0, 0x10143},
    {2, 0x65552c, 0x10144},
    {2, 0x655588, 0x10145},
    {1, 0x6555d4, 0x10de9},
    {2, 0x6561c8, 0x10198},
    {2, 0x656220, 0x10199},
    {2, 0x655320, 0x1013e},
    {2, 0x655380, 0x1013f},
    {2, 0x655618, 0x10148},
    {2, 0x655644, 0x10149},
    {2, 0x6556a8, 0x1014a},
    {1, 0x655720, 0x10dec},
    {2, 0x6558c0, 0x1014e},
    {2, 0x655914, 0x1014f},
    {1, 0x655988, 0x10def},
    {2, 0x655748, 0x1014b},
    {2, 0x655798, 0x1014c},
    {1, 0x6557e4, 0x10ded},
    {2, 0x65582c, 0x1014d},
    {1, 0x65588c, 0x10dee},
    {2, 0x6559f8, 0x10150},
    {2, 0x655a50, 0x10151},
    {1, 0x655a6c, 0x10df0},
    {2, 0x655a7c, 0x10152},
    {2, 0x655ae8, 0x10153},
    {2, 0x655b48, 0x10154},
    {1, 0x655f40, 0x10df1},
    {2, 0x655fb4, 0x10155},
    {1, 0x655fd4, 0x10df2},
    {2, 0x656044, 0x10156},
    {2, 0x655ba8, 0x10157},
    {2, 0x655c14, 0x10158},
    {1, 0x655c3c, 0x10df3},
    {2, 0x655c68, 0x10159},
    {2, 0x655ce0, 0x1015a},
    {2, 0x655d40, 0x1015b},
    {2, 0x655d84, 0x1015c},
    {1, 0x655dc8, 0x10df4},
    {1, 0x655e24, 0x10df5},
    {2, 0x655e60, 0x1018e},
    {2, 0x655eac, 0x1018f},
    {2, 0x655ed8, 0x10190},
    {2, 0x655f14, 0x10191},
};
static const AdvCase k_heaven_cases[] = {
    {20, 0, 2}, {25, 4, 3}, {30, 7, 2}, {35, 9, 2}, {40, 11, 3},
    {45, 14, 2}, {50, 16, 2}, {55, 18, 5}, {60, 23, 3}, {65, 26, 3},
    {70, 29, 4}, {75, 33, 6}, {80, 39, 3}, {85, 42, 2}, {90, 44, 4},
    {95, 48, 3}, {100, 51, 2}, {105, 53, 3}, {110, 56, 4}, {115, 60, 2},
    {120, 62, 2}, {125, 64, 2}, {130, 66, 2}, {145, 68, 2}, {155, 70, 2},
    {160, 72, 3}, {165, 75, 5}, {171, 80, 4}, {175, 84, 2}, {185, 86, 4},
    {190, 90, 4}, {195, 94, 2}, {200, 96, 3}, {210, 99, 4}, {ADV_DEFAULT, 2, 2},
};

/* 0x41b768: one issue in Hell only */
static const AdvLine k_hell_lines[] = {
    {1, 0x656610, 0x10d89},
    {1, 0x658770, 0x10dd9},
    {1, 0x656538, 0x10ecc},
    {2, 0x656594, 0x1021f},
    {2, 0x656600, 0x10220},
    {1, 0x656658, 0x10d8a},
    {1, 0x65669c, 0x10d8b},
    {2, 0x6566c4, 0x10109},
    {1, 0x6566d8, 0x10dd9},
    {1, 0x658410, 0x10e40},
    {1, 0x658460, 0x10e41},
    {1, 0x6584a8, 0x10e42},
    {1, 0x6584f0, 0x10e43},
    {1, 0x656938, 0x10d91},
    {1, 0x656970, 0x10d92},
    {2, 0x6569d4, 0x1010c},
    {2, 0x656a0c, 0x1010d},
    {1, 0x656a54, 0x10d93},
    {1, 0x656a78, 0x10d94},
    {1, 0x656ac0, 0x10d95},
    {2, 0x656b14, 0x1010e},
    {2, 0x656b34, 0x1010f},
    {1, 0x656b88, 0x10d96},
    {1, 0x656ba4, 0x10d97},
    {1, 0x656c0c, 0x10d98},
    {1, 0x6580a0, 0x10e25},
    {2, 0x6580f0, 0x10189},
    {1, 0x65812c, 0x10e26},
    {1, 0x658198, 0x10e27},
    {1, 0x65683c, 0x10d8e},
    {1, 0x656898, 0x10d8f},
    {1, 0x6568d4, 0x10d90},
    {1, 0x65671c, 0x10d8c},
    {1, 0x656770, 0x10d8d},
    {2, 0x6567bc, 0x1010a},
    {2, 0x6567e8, 0x1010b},
    {1, 0x656c2c, 0x10d9a},
    {1, 0x656c78, 0x10d9b},
    {2, 0x656c90, 0x10110},
    {2, 0x656cbc, 0x10111},
    {1, 0x656d00, 0x10d9c},
    {1, 0x656d3c, 0x10d9d},
    {2, 0x656da4, 0x10112},
    {1, 0x656dc4, 0x10d9e},
    {1, 0x656e18, 0x10d9f},
    {2, 0x656e64, 0x10113},
    {1, 0x656e78, 0x10da0},
    {1, 0x658514, 0x10e44},
    {1, 0x658558, 0x10e45},
    {1, 0x6585b4, 0x10e46},
    {1, 0x656ea8, 0x10da1},
    {1, 0x656f14, 0x10da2},
    {1, 0x656f30, 0x10da3},
    {1, 0x656f94, 0x10da4},
    {1, 0x656fd4, 0x10da5},
    {1, 0x657018, 0x10da6},
    {1, 0x6570a0, 0x10da7},
    {2, 0x6570d8, 0x10114},
    {2, 0x657110, 0x10115},
    {1, 0x657148, 0x10da8},
    {1, 0x657184, 0x10da9},
    {1, 0x6571d8, 0x10daa},
    {1, 0x657248, 0x10dab},
    {2, 0x657260, 0x10116},
    {2, 0x657290, 0x10117},
    {1, 0x6572d0, 0x10dac},
    {1, 0x65830c, 0x10e3d},
    {1, 0x658364, 0x10e3e},
    {1, 0x6583c8, 0x10e3f},
    {1, 0x657300, 0x10dad},
    {1, 0x65734c, 0x10dae},
    {1, 0x6573a4, 0x10daf},
    {2, 0x6573f4, 0x10118},
    {1, 0x65740c, 0x10db0},
    {1, 0x657500, 0x10db3},
    {1, 0x657554, 0x10db4},
    {1, 0x6575a4, 0x10db5},
    {1, 0x6575e0, 0x10db6},
    {2, 0x657614, 0x1011b},
    {1, 0x657638, 0x10db7},
    {1, 0x657670, 0x10db8},
    {1, 0x6576bc, 0x10db9},
    {1, 0x6576d4, 0x10dba},
    {1, 0x6581f8, 0x10e3a},
    {1, 0x658244, 0x10e3b},
    {1, 0x6582b0, 0x10e3c},
    {1, 0x65741c, 0x10db1},
    {1, 0x657464, 0x10db2},
    {2, 0x6574a4, 0x10119},
    {2, 0x6574dc, 0x1011a},
    {1, 0x6577f4, 0x10dbd},
    {1, 0x657848, 0x10dbe},
    {2, 0x6578a0, 0x1011e},
    {1, 0x6578c8, 0x10dbf},
    {1, 0x657924, 0x10dc0},
    {1, 0x657968, 0x10dc1},
    {1, 0x657998, 0x10dc2},
    {1, 0x657730, 0x10dbb},
    {2, 0x65779c, 0x1011c},
    {1, 0x6577dc, 0x10dbc},
    {2, 0x6577e8, 0x1011d},
    {1, 0x657ae4, 0x10dc6},
    {1, 0x657b38, 0x10dc7},
    {2, 0x657b64, 0x10120},
    {1, 0x657b78, 0x10dc8},
    {1, 0x657bcc, 0x10dc9},
    {1, 0x6579d0, 0x10dc3},
    {2, 0x657a34, 0x1011f},
    {1, 0x657a60, 0x10dc4},
    {1, 0x657ab8, 0x10dc5},
    {1, 0x657c2c, 0x10dca},
    {2, 0x657c88, 0x10121},
    {1, 0x657ca4, 0x10dcb},
    {1, 0x657cbc, 0x10dcc},
    {1, 0x657cf4, 0x10dcd},
    {1, 0x657d48, 0x10dce},
    {2, 0x657d98, 0x10122},
    {1, 0x657da8, 0x10dcf},
    {1, 0x657de8, 0x10dd0},
    {1, 0x658600, 0x10df1},
    {2, 0x658674, 0x10155},
    {1, 0x658694, 0x10df2},
    {2, 0x658704, 0x10156},
    {1, 0x657e5c, 0x10dd1},
    {1, 0x657ea0, 0x10dd2},
    {1, 0x657edc, 0x10dd3},
    {1, 0x657f10, 0x10dd4},
    {2, 0x657f54, 0x10123},
    {1, 0x657fa4, 0x10dd5},
    {1, 0x657fd0, 0x10dd6},
    {1, 0x658020, 0x10dd7},
    {1, 0x658074, 0x10dd8},
};
static const AdvCase k_hell_cases[] = {
    {20, 0, 1}, {25, 2, 3}, {30, 5, 4}, {35, 9, 4}, {40, 13, 5},
    {45, 18, 5}, {50, 23, 2}, {55, 25, 4}, {60, 29, 3}, {65, 32, 4},
    {70, 36, 3}, {75, 39, 8}, {80, 47, 3}, {85, 50, 3}, {90, 53, 8},
    {95, 61, 5}, {100, 66, 3}, {105, 69, 5}, {110, 74, 5}, {115, 79, 2},
    {120, 81, 2}, {125, 83, 3}, {130, 86, 4}, {140, 90, 7}, {150, 97, 4},
    {160, 101, 5}, {165, 106, 4}, {170, 110, 5}, {180, 115, 4}, {185, 119, 4},
    {190, 123, 2}, {205, 125, 4}, {215, 129, 3}, {ADV_DEFAULT, 1, 1},
};

/* 0x41db2b: the same issue in both realms */
static const AdvLine k_both_lines[] = {
    {1, 0x659bf8, 0x10e2b},
    {2, 0x659c58, 0x1018c},
    {2, 0x659c80, 0x1018d},
    {1, 0x65a0ac, 0x10dd9},
    {1, 0x659b3c, 0x10e29},
    {1, 0x659b7c, 0x10e2a},
    {2, 0x659bb8, 0x1018b},
    {2, 0x659aec, 0x1018a},
    {1, 0x659b0c, 0x10e28},
    {1, 0x658994, 0x10df6},
    {2, 0x6589f4, 0x1015d},
    {1, 0x659cd8, 0x10ecc},
    {2, 0x659d34, 0x1021f},
    {2, 0x659da0, 0x10220},
    {2, 0x658a1c, 0x1015e},
    {1, 0x658a64, 0x10df7},
    {2, 0x658ae4, 0x1015f},
    {2, 0x659f60, 0x10221},
    {1, 0x659fb8, 0x10ed0},
    {1, 0x658b38, 0x10df8},
    {2, 0x658b80, 0x10160},
    {2, 0x658bc0, 0x10161},
    {1, 0x658c1c, 0x10df9},
    {1, 0x658c60, 0x10dfa},
    {2, 0x658cbc, 0x10162},
    {1, 0x658d00, 0x10dfb},
    {2, 0x658d68, 0x10163},
    {2, 0x659a38, 0x10184},
    {1, 0x659a8c, 0x10ec9},
    {1, 0x658d90, 0x10dfc},
    {2, 0x658de8, 0x10164},
    {1, 0x658e2c, 0x10dfd},
    {1, 0x658e74, 0x10dfe},
    {2, 0x658ecc, 0x10165},
    {1, 0x658f04, 0x10dff},
    {1, 0x658f60, 0x10e00},
    {1, 0x658fb0, 0x10e01},
    {2, 0x658fe4, 0x10166},
    {1, 0x65a018, 0x10ed1},
    {2, 0x65a07c, 0x10222},
    {1, 0x659040, 0x10e2c},
    {2, 0x6590a0, 0x10210},
    {2, 0x659104, 0x10167},
    {2, 0x659138, 0x10168},
    {1, 0x65916c, 0x10e02},
    {1, 0x6591ac, 0x10e03},
    {1, 0x6591e8, 0x10e04},
    {1, 0x659228, 0x10e2d},
    {1, 0x659264, 0x10e2e},
    {2, 0x6592a8, 0x10211},
    {1, 0x659e8c, 0x10ecf},
    {2, 0x659ee4, 0x1021f},
    {2, 0x659f50, 0x10220},
    {2, 0x6592e4, 0x10212},
    {1, 0x659328, 0x10e2f},
    {1, 0x65936c, 0x10e30},
    {1, 0x6593c0, 0x10e31},
    {2, 0x659420, 0x10213},
    {1, 0x65948c, 0x10e32},
    {1, 0x6594d8, 0x10e33},
    {2, 0x659540, 0x10214},
    {1, 0x659560, 0x10e34},
    {2, 0x6595b0, 0x10215},
    {1, 0x659db0, 0x10ece},
    {2, 0x659e24, 0x1021e},
    {1, 0x6588b0, 0x10db1},
    {1, 0x6588f8, 0x10db2},
    {2, 0x658938, 0x10119},
    {2, 0x658970, 0x1011a},
    {2, 0x6587b4, 0x10146},
    {2, 0x6587f4, 0x10147},
    {1, 0x658818, 0x10dea},
    {1, 0x65884c, 0x10deb},
    {1, 0x659780, 0x10e37},
    {2, 0x6597e8, 0x10197},
    {1, 0x6597fc, 0x10e38},
    {1, 0x659868, 0x10e39},
    {2, 0x6595f8, 0x10194},
    {2, 0x659650, 0x10195},
    {1, 0x659684, 0x10e35},
    {1, 0x6596f4, 0x10e36},
    {2, 0x659764, 0x10196},
    {1, 0x6598c8, 0x10df1},
    {2, 0x65993c, 0x10155},
    {1, 0x65995c, 0x10df2},
    {2, 0x6599cc, 0x10156},
};
static const AdvCase k_both_cases[] = {
    {5, 0, 3}, {10, 4, 3}, {15, 7, 2}, {20, 9, 2}, {25, 11, 3},
    {30, 14, 3}, {35, 17, 2}, {40, 19, 2}, {45, 21, 3}, {50, 24, 3},
    {55, 27, 2}, {60, 29, 4}, {65, 29, 4}, {70, 33, 2}, {75, 35, 3},
    {80, 38, 2}, {85, 40, 2}, {90, 42, 5}, {95, 47, 3}, {100, 50, 3},
    {105, 53, 3}, {110, 56, 3}, {115, 59, 2}, {120, 61, 2}, {125, 63, 2},
    {130, 65, 4}, {135, 69, 4}, {160, 73, 4}, {165, 77, 5}, {185, 82, 4},
    {190, 86, 0}, {ADV_DEFAULT, 3, 1},
};

/* 0x4196ae: a button without an issue, rand_range(1, 13). */
static const AdvLine k_quips[13] = {
    {1, 0x6544d8, 0x10d9b}, {1, 0x6544f0, 0x10db0}, {2, 0x654500, 0x1011d}, {2, 0x65450c, 0x1011f},
    {2, 0x654538, 0x10122}, {2, 0x654548, 0x10135}, {1, 0x654554, 0x10de6}, {1, 0x65459c, 0x10df3},
    {2, 0x6545c8, 0x10197}, {2, 0x6545dc, 0x1018d}, {2, 0x654634, 0x101a2}, {2, 0x654658, 0x101c4},
    {1, 0x65466c, 0x10e5e},
};

typedef struct AdvTable {
    const AdvLine *lines;
    const AdvCase *cases;
    size_t n_cases;
} AdvTable;

#define TABLE(n) {k_##n##_lines, k_##n##_cases, sizeof k_##n##_cases / sizeof k_##n##_cases[0]}
static const AdvTable k_tables[3] = {TABLE(heaven), TABLE(hell), TABLE(both)};

/* 0x41801f: the labels (short UI strings, kept inline). */
static const struct {
    uint8_t code;
    const char *label;
} k_labels[] = {
    {5, "PENNIES!"},    {10, "Pennies!"},   {15, "Pennies"},   {20, "GATES!"},    {25, "FATES!"},
    {30, "Gates"},      {35, "LOSSES!"},    {40, "KStation"},  {45, "KStation"},  {50, "KStation"},
    {55, "Too Much"},   {60, "TOPIAS!"},    {65, "Topias"},    {70, "Topias"},    {75, "TCenters"},
    {80, "ROADS!"},     {85, "TCenters"},   {90, "TCENTERS"},  {95, "Ports"},     {100, "Losses!"},
    {105, "Limbos"},    {110, "Siphons"},   {115, "Siphons"},  {120, "Siphons"},  {125, "Losses"},
    {130, "Banks"},     {135, "Loans"},     {140, "ROADS!"},   {145, "ROADS!"},   {150, "Roads"},
    {155, "Roads"},     {160, "Balance!"},  {165, "Balance"},  {170, "Vibes"},    {171, "Vibes"},
    {175, "Diversity"}, {180, "Diversity"}, {185, "Belief"},   {190, "Karma"},    {195, "Karma"},
    {200, "Good"},      {205, "Evil"},      {210, "Heaven"},   {215, "Hell"},     {ADV_NONE, "-----"},
};

const char *advisor_issue_label(unsigned code)
{
    for (size_t i = 0; i < sizeof k_labels / sizeof k_labels[0]; i++)
        if (k_labels[i].code == code)
            return k_labels[i].label;
    return "Nada";
}

/* A NUL-terminated string at `va` in ALIFE.EXE, or "". */
static const char *xs(uint32_t va)
{
    static bool tried;
    if (!tried)
        exe_res_open("ALIFE.EXE");
    tried = true;
    uint32_t avail = 0;
    const uint8_t *p = exe_res_va(va, &avail);
    if (!p || !memchr(p, 0, avail))
        return "";
    return (const char *)p;
}

static const AdvCase *find_case(const AdvTable *t, unsigned code)
{
    const AdvCase *def = NULL;
    for (size_t i = 0; i < t->n_cases; i++) {
        if (t->cases[i].issue == code && code != ADV_DEFAULT)
            return &t->cases[i];
        if (t->cases[i].issue == ADV_DEFAULT)
            def = &t->cases[i];
    }
    return def;
}

/* One line; `first` lines don't check the stamp (it was just read). */
static void say_line(const AdvLine *l, bool first, uint32_t stamp)
{
    if (g_adv_talk.silenced || (!first && stamp != g_adv_talk.stamp))
        return;
    akos_talk_say(l->talker, &g_adv_talk.text_rect[l->talker], xs(l->text), l->voice);
    akos_talk_wait(l->talker);
}

static void say(int which, unsigned code)
{
    const AdvCase *c = find_case(&k_tables[which], code);
    if (!c)
        return;
    uint32_t stamp = g_adv_talk.stamp;
    for (int i = 0; i < c->count; i++)
        say_line(&k_tables[which].lines[c->first + i], i == 0, stamp);
}

/* 0x419b65 */
void advisor_say_heaven(unsigned code) { say(0, code); }

/* 0x41b768 */
void advisor_say_hell(unsigned code) { say(1, code); }

/* 0x41db2b */
void advisor_say_both(unsigned code) { say(2, code); }

void advisor_say_quip(int n)
{
    if (n >= 1 && n <= 13)
        say_line(&k_quips[n - 1], false, g_adv_talk.stamp);
}

int advisor_speech_length(int which, unsigned code)
{
    if (which < 0 || which > 2)
        return 0;
    const AdvCase *c = find_case(&k_tables[which], code);
    return c ? c->count : 0;
}

const char *advisor_speech_text(int which, unsigned code, int i)
{
    if (which < 0 || which > 2)
        return NULL;
    const AdvCase *c = find_case(&k_tables[which], code);
    if (!c || i < 0 || i >= c->count)
        return NULL;
    return xs(k_tables[which].lines[c->first + i].text);
}
