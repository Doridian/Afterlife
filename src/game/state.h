/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The persistent game state: everything ALIFE.EXE writes into an ALSV saved
 * game (.ALF saved game or .ASC scenario; docs/formats/alsv.md).
 *
 * In the original these are scattered globals (the address of each is noted
 * per field) and packed records; here they are grouped by the save block they
 * come from, with plain portable types. Field names follow what is known;
 * `unk_XXXX` keeps the original address (for globals) or the record offset
 * (inside records) so the field can be traced back.
 *
 * The original's wide counters (money, EMBOs, realm SOUL pools) are pairs
 * of 32-bit globals {int hi; u32 lo} handled by its own bignum routines;
 * they are kept as a Bignum (below; arithmetic in game/util.h).
 *
 * Loading and saving: src/game/savegame.c. The live game state is one
 * global GameState, see game/world.h ("State model").
 */
#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <stddef.h>
#include <stdint.h>

#define GS_MAP_W 100
#define GS_MAP_H 100
#define GS_MAP_CELLS (GS_MAP_W * GS_MAP_H)

/* Plane numbers, as map_view_select_plane (0x42b102) uses them. */
enum { GS_PLANE_HELL = 1, GS_PLANE_MIDDLE = 2, GS_PLANE_HEAVEN = 3 };

/* The original's wide number, two adjacent globals {int hi; u32 lo}
 * (bignum_add 0x42a950 and friends, game/util.h). It is sign and magnitude,
 * not two's complement: hi >= 0 is the value hi * 2^32 + lo, hi = -1 is
 * -lo (hi < -1 only appears for magnitudes >= 2^32, where the original's
 * arithmetic is unreliable anyway). */
typedef struct Bignum {
    int32_t hi;
    uint32_t lo;
} Bignum;

static inline int64_t bignum_value(Bignum b)
{
    if (b.hi >= 0)
        return (int64_t)((uint64_t)(uint32_t)b.hi << 32 | b.lo);
    return -(int64_t)((uint64_t)(uint32_t)(-(b.hi + 1)) << 32 | b.lo);
}

/* ---- Header ------------------------------------------------------------- */

/* Save format versions ("ALSV" + 4 ASCII digits). ALIFE.EXE writes 7300 and
 * reads anything; the shipped scenarios are 7195. */
#define ALSV_VERSION_CURRENT 7300

typedef struct SaveHeader {
    int version;            /* 0x66aee4 save_version: the 4 digits as a number */
    uint8_t kind;           /* writer's 2nd argument (0x650050 at the call); 1 in the scenarios */
    uint32_t timestamp;     /* real-world time of saving, packed (see savegame_decode_timestamp) */
    uint32_t year;          /* 0x630268 game_year at saving (the body repeats it) */
    char title[256];        /* save slot description, NUL-terminated */
    uint16_t difficulty;    /* 0x5c5c14 game_difficulty: 1 easy, 2 medium, 3 hard (ver > 7219) */
    /* Thumbnail of the map window (8-bit palette indices), ver > 7025.
     * 104 x 78 for games; scenario slots >= 200 write 1 x 1. */
    uint32_t thumb_w, thumb_h;
    uint8_t *thumb;         /* thumb_w * thumb_h bytes, row-major; owned */
} SaveHeader;

/* ---- Map planes --------------------------------------------------------- */

/* One cell of the simulation grid (18-byte packed record, 0x5fa670 Hell /
 * 0x5c6680 Heaven, `cells[y * 100 + x]`). Byte 0 is the zone/building kind;
 * the meaning of the rest depends on the tile set of the cell (map cell
 * bits 27-31) and is kept as raw little-endian bytes. Which bytes are saved
 * per set: savegame.c (sim_cell_fields). Helpers: sim_cell_u32 etc. */
typedef struct SimCell {
    uint8_t b[18];
} SimCell;

static inline uint32_t sim_cell_u32(const SimCell *c, int off)
{
    return (uint32_t)c->b[off] | (uint32_t)c->b[off + 1] << 8 | (uint32_t)c->b[off + 2] << 16 |
           (uint32_t)c->b[off + 3] << 24;
}
static inline uint16_t sim_cell_u16(const SimCell *c, int off)
{
    return (uint16_t)(c->b[off] | c->b[off + 1] << 8);
}
static inline void sim_cell_set_u32(SimCell *c, int off, uint32_t v)
{
    c->b[off] = (uint8_t)v, c->b[off + 1] = (uint8_t)(v >> 8);
    c->b[off + 2] = (uint8_t)(v >> 16), c->b[off + 3] = (uint8_t)(v >> 24);
}
static inline void sim_cell_set_u16(SimCell *c, int off, uint16_t v)
{
    c->b[off] = (uint8_t)v, c->b[off + 1] = (uint8_t)(v >> 8);
}

/* Per-realm statistics record, 0x812b bytes packed in the original
 * (Hell 0x630520, Heaven 0x5b2f70; the current one is *0x630428, see
 * realm_cur() in game/world.h). Offsets are the record offsets. */
typedef struct PlaneStatHist {
    uint32_t unk_00, unk_04;
    uint32_t v[32];       /* +0x08 */
    uint8_t unk_88;
} PlaneStatHist;          /* 0x89 bytes */

typedef struct PlaneStatRow {
    uint32_t unk_00;
    uint32_t v[16];       /* +0x04 */
} PlaneStatRow;           /* 0x44 bytes */

typedef struct PlaneStats {
    /* [zone kind 0..6][16] (index kind * 16 + j, 0x445416 walks it as
     * kind * 0x890 + j * 0x89): SOUL counts, v[] by age bucket.
     * hist_a this year, hist_b the previous one. */
    PlaneStatHist hist_a[112];   /* +0x0000 */
    PlaneStatHist hist_b[112];   /* +0x3bf0 */
    uint32_t unk_77e0[16];
    uint32_t unk_7820[16];
    /* +0x7860: the realm's ports/gates (tile set 13 origin cells), {x, y},
     * n_ports used; walked by 0x46114f, 0x446829, 0x447e8f. */
    uint8_t ports[50][2];
    uint8_t unk_78c4[128][2];
    uint8_t unk_79c4[20][2];     /* ver > 7109 */
    uint32_t unk_79ec[7];
    PlaneStatRow unk_7a08[16];
    uint8_t unk_7e48[16][2];
    uint32_t unk_7e68[16];
    uint32_t unk_7ea8;
    uint32_t unk_7eac[16];
    uint32_t unk_7eec;
    uint32_t unk_7ef0[16];
    uint32_t unk_7f30, unk_7f34, unk_7f38;
    uint32_t unk_7f3c[7], unk_7f58[7], unk_7f74[7], unk_7f90[7];
    uint32_t unk_7fac[8];
    uint32_t unk_7fcc[8];        /* ver > 7179 */
    uint32_t population;         /* +0x7fec: SOULs in the realm (status bar, 0x447243) */
    uint32_t unk_7ff0[21];       /* +0x7ff0..+0x8040 */
    uint8_t n_ports;             /* +0x8044: entries used in ports[] (= set-13 cells) */
    uint8_t unk_8045, unk_8046;
    uint32_t unk_8047, unk_804b;
    uint32_t unk_804f[5];
    uint32_t unk_8063[5];        /* ver > 7179 */
    uint32_t unk_8077[5];
    uint32_t unk_808b[5];
    uint32_t unk_809f[16];
    uint16_t n_zones;            /* +0x80df: zone cells (tile sets 14/15, origin cells) */
    uint16_t unk_80e1;
    uint32_t unk_80e3, unk_80e7, unk_80eb;
    uint8_t unk_80ef;            /* clamped to <= 100 on load (else 0) */
    uint32_t unk_80f0, unk_80f4;
    uint8_t unk_80f8, unk_80f9, unk_80fa, unk_80fb, unk_80fc;
    /* +0x80fd: a SOUL pool (bignum) that the status bar adds to both
     * realms' population for its "SOULs:" total; set-5 cells drain into it
     * (0x4606cf bignum_sub). */
    Bignum souls;
    uint16_t unk_8105, unk_8107, unk_8109; /* ver > 6999 */
    uint32_t unk_810b, unk_810f, unk_8113, unk_8117; /* ver > 7010 */
    uint32_t unk_811b;           /* ver > 7024 */
    uint32_t unk_811f;           /* ver > 7139 */
    uint32_t unk_8123;           /* ver > 7165 */
    uint32_t unk_8127;           /* ver > 7174 */
} PlaneStats;

typedef struct MapPlaneState {
    /* Map cells (u32, CELL_* bits in gfx/tile_draw.h), row-major
     * `[y * 100 + x]`: 0x593270 Hell, 0x5bb270 middle, 0x6265b0 Heaven.
     * Bit 7 (CELL_HILITE) is cleared on load, bit 4 (CELL_ALT) on save. */
    uint32_t cells[GS_MAP_CELLS];
    /* Object map (u8): 0x5f4cf0 Hell, 0x5f25c0 middle, 0x5f7ee0 Heaven. */
    uint8_t objmap[GS_MAP_CELLS];
    /* Hell and Heaven only: */
    SimCell sim[GS_MAP_CELLS];
    PlaneStats stats;
} MapPlaneState;

/* ---- Fixed tables after the planes --------------------------------------- */

/* 0x57e1c0: 64 x 32 records of 0x2a bytes ([i * 0x540 + j * 0x2a]); used by
 * the planet view code (0x4aaca0, 0x4ab5d4 read +0x10). */
typedef struct GridRec2a {
    uint32_t unk_00;
    uint8_t unk_04, unk_05, unk_06;
    uint8_t unk_07[9];
    uint8_t unk_10[16];
    uint8_t unk_20[7];
    uint8_t unk_27, unk_28, unk_29;
} GridRec2a;

/* 0x64e5f0: map objects, the sprites moving over the map (docs/map.md;
 * gfx/map_view.h MapObject is the renderer's view of the same record). */
typedef struct MapObjectRec {
    uint8_t plane;              /* +0x00: 0 = free */
    int8_t x, y;                /* +0x01, +0x02: origin cell */
    int8_t dx, dy;              /* +0x03, +0x04: step */
    int8_t size;                /* +0x05 */
    uint8_t set;                /* +0x06: tile set slot */
    uint8_t frame;              /* +0x07: entry - 1 */
    uint8_t first_frame;        /* +0x08 */
    uint8_t delay, delay_reset; /* +0x09, +0x0a: frame delay */
    uint32_t set_mask;          /* +0x0b */
    uint8_t fade;               /* +0x0f */
    uint8_t unk_10, unk_11;
    uint8_t priority;           /* +0x12 */
    uint32_t flags;             /* +0x13 */
} MapObjectRec;

#define GS_MAP_OBJECTS 63

/* 0x59cf10 soul_groups: 0x2000 SOUL groups of 11 bytes. Road cells refer
 * to one by index (sim cell u32 at +1, high 16 bits: soul_group_get/set
 * 0x458490 / 0x45852f in game/world.h); a group with count 0 is free. */
#define GS_SOUL_GROUPS 0x2000
typedef struct SoulGroup {
    uint32_t count;             /* +0x0: SOULs in the group; 0 = free */
    uint16_t mask;              /* +0x4: sin/virtue bits (game-map.md) */
    uint32_t unk_06;
    uint8_t unk_0a;
} SoulGroup;

/* 0x5c4eb0: 30 records of 0x72 bytes (18 before version 7130). */
typedef struct Rec72 {
    uint32_t v[25];             /* +0x00 */
    uint32_t unk_64, unk_68;
    uint8_t unk_6c, unk_6d;
    uint32_t unk_6e;            /* ver > 7124 */
} Rec72;

/* 0x6399d0 and 0x639480: two summary records read by the graph views
 * (0x4113ca budget graph, 0x415d87, 0x412362). All fields ver > 7160. */
typedef struct GraphSummary {
    uint32_t unk_000[16];
    uint8_t unk_040[20][2];
    uint32_t unk_068[7], unk_084[7], unk_0a0[7], unk_0bc[7];
    uint32_t unk_0d8[8];
    uint32_t unk_0f8[8];        /* ver > 7179 */
    uint32_t unk_118[18];       /* +0x118..+0x15c */
    uint32_t unk_160[5];
    uint32_t unk_174[5];        /* ver > 7179 */
    uint32_t unk_188[5];
    uint8_t unk_19c;
    uint32_t unk_19d, unk_1a1, unk_1a5, unk_1a9;
    uint32_t unk_1ad;           /* ver > 7174 */
} GraphSummary;

/* The planet (Earth) view module (block written by 0x495300). */
typedef struct PlanetRecA { uint32_t unk_0, unk_4; uint16_t v[7]; } PlanetRecA; /* 0x4f4030 / 0x5068b8 */
typedef struct PlanetRecB { uint32_t unk_0, unk_4; uint16_t v[9]; } PlanetRecB; /* 0x518d50 / 0x506438 */
typedef struct PlanetRecC { uint32_t unk_0, unk_4; uint8_t unk_518cf0, unk_4f3fe8; } PlanetRecC; /* 0x4f4238 */

typedef struct PlanetState {
    uint32_t map_size;          /* 0x66c7e8 */
    uint8_t *map;               /* 0x66c7e4: map_size bytes, bit-packed terrain/inhabitants; owned */
    uint8_t unk_66c8dc;
    uint32_t unk_66c8e0;
    uint16_t unk_66c8cc;        /* ver > 7011 */
    uint8_t n_a, n_b, n_c;      /* 0x66c8d0, 0x66c8d4, 0x66c8d8 */
    PlanetRecA rec_a[256];
    PlanetRecB rec_b[256];
    PlanetRecC rec_c[256];
    uint8_t unk_66c8c0, unk_66c8c4;     /* ver > 7194 */
    uint32_t grid[64][32][2];   /* 0x506c40, ver > 7194; saved j-major */
    uint32_t unk_50acf0[0x3800];
    uint32_t unk_4f4438[0x4800];
    uint32_t unk_66c8e4;        /* ver > 7100 */
    uint8_t unk_66c8e8;         /* ver > 7204 */
    uint32_t unk_66c8c8;        /* ver > 7166 */
    uint8_t unk_4f4028[7];      /* ver > 7219 */
    uint32_t unk_4f3f90[7];     /* ver > 7219 */
    uint32_t unk_66c89c;        /* ver > 7219 */
} PlanetState;

/* 0x4f33f8: 10 records of 0x26 bytes (block written by 0x478d50); the
 * selected one's +1 byte picks a pressed button in window 0x66481c. */
typedef struct Rec26 {
    uint8_t unk_00[8];
    uint32_t unk_08, unk_0c;
    uint8_t unk_10, unk_11, unk_12;
    uint32_t unk_13, unk_17;
    uint8_t unk_1b;
    uint32_t unk_1c;
    uint16_t unk_20;
    uint8_t unk_22, unk_23;
    uint8_t unk_25;             /* +0x24 is not saved */
} Rec26;

typedef struct Block26 {
    uint8_t selected;           /* 0x664810 */
    uint8_t unk_664808, unk_66480c;
    Rec26 rec[10];
} Block26;

/* ---- The whole state ------------------------------------------------------ */

typedef struct GameState {
    SaveHeader hdr;
    MapPlaneState plane[4];         /* [1] Hell, [2] middle, [3] Heaven; [0] unused */

    GridRec2a grid_57e1c0[64][32];
    uint8_t unk_64e500[25][9];      /* 0x64e500 */
    MapObjectRec objects[GS_MAP_OBJECTS];
    SoulGroup soul_groups[GS_SOUL_GROUPS]; /* 0x59cf10 */
    uint32_t unk_5b2f10[16];

    /* Seven entries each (sins / virtues?), saved interleaved. */
    uint8_t unk_593240[7];
    uint8_t unk_57e060[7];
    uint8_t unk_593260[7][2];
    uint32_t unk_5fa600[7];
    uint32_t unk_6303d0[7][2];
    uint32_t unk_5bb210[7], unk_5bb22c[7], unk_5bb248[7];

    /* Four entries each, interleaved. */
    uint8_t unk_5f25b0[4][3];
    uint8_t unk_630410[4][3];
    uint32_t unk_5fa660[4];

    Rec72 rec_5c4eb0[30];

    uint32_t unk_62659c;
    uint32_t unk_5b2f54;
    uint8_t unk_5b2f58, unk_630426, unk_5fa61d, unk_57e067;
    uint32_t unk_6265a0, unk_57e020, unk_57e1b4, unk_5bb114, unk_5f7464, unk_630434;
    uint8_t unk_593254, unk_5b2f60;
    /* 0x6303c0/c4: EMBOs counted during the planet pass (bignum_add in
     * 0x49704f, 0x4aa138); copied to `embos` and cleared at year start
     * (sim_update phase 0). hi ver > 7299. */
    Bignum embos_next;
    Bignum embos;                       /* 0x630490, 0x630494: EMBOs (Earth population); hi ver > 7299 */
    uint32_t unk_630430;                /* 8.8 fixed point, shown as "%d.%02d" under the SOULs */
    Bignum pennies;                     /* 0x630438 (hi), 0x63043c (lo): money; hi ver > 7014 */
    uint32_t year;                      /* 0x630268 game_year */
    int8_t view_dir_x, view_dir_y;      /* 0x5fa5f8, 0x5bb118: map rotation, copied to MapView +0x39/+0x3a */
    uint32_t unk_63042c, unk_638668, unk_626594, unk_57e1ac, unk_5bb10c;
    uint8_t unk_63041c, unk_630424, unk_63050e;
    uint8_t sim_phase;                  /* 0x5bb104: sim_update scheduler phase (0, 10, 15, 20, 30, 40..100) */
    uint32_t unk_630488;
    uint32_t unk_638694;                /* ver > 7100 */
    uint8_t unk_6301f4, unk_59ceb0;
    uint32_t unk_630508;
    uint8_t unk_5b2f61;
    uint32_t unk_5f746c, unk_5b2f5c;
    uint8_t unk_6301f2;
    uint8_t unk_5bb0b0[20][3];          /* [i][0] ver > 7149 */
    uint8_t unk_593200[20][3];          /* [i][0] ver > 7149 */
    uint8_t unk_57e098, unk_57e09a, unk_5bb110, unk_638653, unk_638654, unk_63050c,
        unk_630484, unk_638664, unk_5f25a8, unk_5931f0, unk_5f7ed0;
    uint8_t unk_5f7400[8];
    uint32_t unk_638670[8];
    uint8_t unk_5f4cd0[4][6];
    uint8_t unk_593247;                 /* ver > 6999 */
    uint16_t unk_5bb1e0[16];            /* ver > 7009 */
    uint16_t unk_5931d0[16];            /* ver > 7009 */
    uint16_t unk_5c6678;                /* ver > 7009 */
    uint8_t unk_630425;                 /* ver > 7009 */
    uint32_t unk_626598;                /* ver > 7099 */
    uint32_t lost_souls;                /* 0x5c5c10, ver > 7169: status bar "Lost: %s" */
    uint8_t unk_6301f3;                 /* ver > 7109 */
    uint32_t unk_638690, unk_593250;    /* ver > 7129 */
    uint32_t unk_57e09c;                /* ver > 7139 */
    uint8_t unk_5931c1, unk_63026c, unk_59324c, unk_63050d;  /* ver > 7159 */
    uint32_t unk_5fa5f4, unk_5f7ed4;                         /* ver > 7159 */
    uint32_t unk_57e058, unk_630420, unk_5bb108;             /* ver > 7199 */
    /* Scenario texts (ver > 7029), entered when saving a scenario. */
    char scenario_title[80];            /* 0x57e170 */
    char scenario_description[256];     /* 0x6302c0 */
    char scenario_button[40];           /* 0x57e070 */
    char scenario_commands[80];         /* 0x5bb180 */
    uint32_t unk_63048c, unk_57e160;    /* ver > 7184 */
    uint32_t bad_things;                /* 0x593248: "Bad Things" (disasters) enabled, ver > 7249 */
    uint16_t unk_5b2f5a;                /* ver > 7279 */

    GraphSummary summary[2];            /* 0x6399d0, 0x639480 */
    PlanetState planet;
    Block26 block_4f33f8;
} GameState;

static inline int64_t gs_pennies(const GameState *g)
{
    return bignum_value(g->pennies);
}
static inline int64_t gs_embos(const GameState *g)
{
    return bignum_value(g->embos);
}

#endif
