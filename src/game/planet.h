/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Planet (WP5 in docs/game-map.md): ALIFE.EXE 0x495300-0x4a2efc and
 * 0x4a8337-0x4ab8df. The Planetview window is ui/planetview.c.
 *
 * Model
 * -----
 * The Earth is a 64 x 32 grid of sectors (g_state.sectors[x][y], the
 * original's planet_sectors 0x57e1c0, wrapping east-west), each a
 * PlanetSector (game/state.h): a population of EMBOs, birth and death
 * rates taken from its tech level, nine tenet bytes (belief systems), the
 * derived share of deaths per SOUL belief type, seven signed sin/virtue
 * levels and the terrain (land / water / arctic, height). The rest of the
 * persistent planet state is g_state.planet (PlanetState): the pending
 * influence accumulators, migration grid, divine-influence lists, disaster
 * and tech bookkeeping, and the 640 x 320 one-bit coastline map the
 * Planetview draws (game/planet_map.c).
 *
 * Each game year sim_update (WP1) runs these steps in its phases:
 *   phase 20  planet_stats_step()   1024 sectors per call: tenet drift
 *             (0x4aa783), soul shares (0x4aaca0), population-weighted
 *             belief averages (g_state.planet_aaaa, g_planet.avg_*).
 *   phase 30  planet_emit_step()    25 sectors per call: births and deaths
 *             (0x4aa138; the population is summed into g_state.embos_next),
 *             deaths become SOULs through the souls hook (0x4ab5d4 ->
 *             0x445785), counted in g_state.planet_deaths;
 *             then planet_year(0): disasters and the flood's end (0x49c8b8).
 *   phase 10  planet_sector_step()  one row per call: migration between
 *             sectors (0x49a2da), carrying tenets, sins and tech.
 *   phase 15  planet_tech_step()    one row per call: migration applied,
 *             pending influence, tech progress and announcements (0x49b05c);
 *             at the end the divine influence spreads (0x4a2092, 0x4a26fb,
 *             0x4a2b8a) and the summary is rebuilt (0x499a77).
 * The step functions return true when their pass is complete (the original
 * returns 1 and sim_update then moves to the next phase).
 *
 * Everything draws from the shared RNG (game/util.h) in the original order.
 * UI side effects (message boxes, the disaster window, refreshing the
 * Planetview, sounds) go through g_planet_hooks so this module stays free of
 * window code; hooks left NULL are skipped.
 *
 * The data tables (tech levels, disasters, tech names) are g_planet_tables.
 */
#ifndef GAME_PLANET_H
#define GAME_PLANET_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "game/world.h"

#define PLANET_W 64
#define PLANET_H 32
#define PLANET_SECTORS (PLANET_W * PLANET_H)
/* The coastline bitmap: 10 x 10 pixels per sector, 1 bit per pixel, MSB
 * first, rows of 0x50 bytes (stride in pixels 0x280). One spare byte. */
#define PLANET_MAP_W 640
#define PLANET_MAP_H 320
#define PLANET_MAP_SIZE 0x6401

#define PLANET_TECHS 15     /* Fire .. Advanced Golf */
#define PLANET_DISASTERS 10 /* 0 flood .. 9 gravity; 100 = the Flood (0x49c226) */

/* Sector terrain (PlanetSector.terrain). */
enum { PLANET_LAND = 0, PLANET_WATER = 1, PLANET_ARCTIC = 2 };

/* Tenets (PlanetSector.tenet[]). */
enum {
    TENET_AAAA = 0, /* believe in an afterlife (NAAA = 255 - AAAA) */
    TENET_HAHA = 1, /* fates, summing to about 255: heaven and hell */
    TENET_HOHO = 2, /* heaven or hell */
    TENET_OPRA = 3, /* only hell */
    TENET_OCRA = 4, /* only heaven */
    TENET_ALF = 5,  /* no reincarnation (ALF + RALF = 255) */
    TENET_RALF = 6, /* reincarnation */
    TENET_SUMA = 7, /* multiple afterlife experiences (SUMA + SUSA = 255) */
    TENET_SUSA = 8, /* single */
};

/* ---- Data tables ------------------------------------------------------- */

typedef struct PlanetTechInfo { /* 0x64f048, 0x14 bytes per tech level */
    uint32_t capacity;      /* +0x00: EMBOs a sector holds comfortably */
    uint32_t unk_04;        /* +0x04: 0, 1 or 2 (late techs); not read here */
    uint16_t sea_range;     /* +0x08: migration distance from coasts */
    uint16_t land_range;    /* +0x0a: migration distance */
    uint8_t birth_rate;     /* +0x0c */
    uint8_t unk_0d;
    uint16_t death_rate;    /* +0x0e (low byte used) */
    uint32_t terrain_mask;  /* +0x10: terrains migrants can enter, bit per PLANET_* */
} PlanetTechInfo;

typedef struct PlanetDisasterInfo { /* 0x66c780, 4 bytes each */
    uint8_t min_tech, max_tech; /* max 0 = no limit */
    uint8_t radius;             /* sectors around the centre */
    uint8_t kill_percent;
} PlanetDisasterInfo;

typedef struct PlanetTables {
    PlanetTechInfo tech[PLANET_TECHS];
    int8_t tech_sin[32][7];        /* 0x64f178: sin shift per tech step (0x498681; 16 rows, the rest is what follows in .rdata, read like the original would) */
    int8_t belief_index[9];        /* 0x66c918: soul type bit of each tenet (0x4aaca0) */
    PlanetDisasterInfo disaster[PLANET_DISASTERS];
    const char *tech_name[PLANET_TECHS];  /* 0x66c740: "Fire" ... */
    const char *tech_blurb[PLANET_TECHS]; /* the texts of 0x49b05c's announcement */
} PlanetTables;

extern const PlanetTables g_planet_tables;

/* Always true: the tables are static data (kept for its callers). */
bool planet_tables_load(void);

/* ---- Runtime state (not saved) ----------------------------------------- */

typedef struct PlanetRuntime {
    /* Population-weighted averages from planet_stats_step (the AAAA one is
     * saved: g_state.planet_aaaa). 0..255. */
    uint8_t avg_ralf;       /* 0x57e010 */
    uint8_t avg_not_ocra;   /* 0x5931c0: 255 - OCRA */
    uint8_t avg_not_opra;   /* 0x626590: 255 - OPRA */
    uint32_t acc[4];        /* 0x66c924..0x66c930 */
    /* 0x499a77 summary: highest tech level on the planet and, per sin, the
     * most sinful populated sector (lowest level, ties to the bigger pop). */
    uint8_t max_tech;       /* 0x66c898 */
    int8_t sin_min[7];      /* 0x4f3f88 */
    uint32_t sin_min_x[7], sin_min_y[7]; /* 0x4f3fb0 */
    uint32_t sin_min_pop[7]; /* 0x518d30 */
    /* planet_tech_step's year-start values */
    int16_t drift;          /* 0x66c900: RALF drift from g_state.unk_5c6678 */
    uint8_t announced;      /* 0x66c904: a tech was announced this pass */
    /* The Planetview's selected sector (0x66c880 / 0x66c884); forced
     * disasters strike there. */
    int sel_x, sel_y;
    uint8_t cheats;         /* 0x630514: allows forced disasters (planet_year arg) */
} PlanetRuntime;

extern PlanetRuntime g_planet;

/* ---- Hooks into the rest of the game ------------------------------------- */

typedef struct PlanetHooks {
    /* 0x445785 (WP1, planet SOULs entering the afterlife): SOULs of belief
     * type `type` (bits 0-1 fate HAHA/HOHO/OPRA/OCRA, bit 2 RALF, bit 3
     * SUSA) dying in sector (x, y), passed on exactly as 0x4ab5d4 does:
     * `count` and `size` are the original's 5th and 6th arguments (count
     * groups of size, see planet_send_souls). The original draws from
     * the RNG in there. */
    void (*souls)(int x, int y, int type, uint32_t count, uint32_t size);
    /* sys_pump (frame = false) or sys_pump + game_frame_update (true)
     * inside the long disaster loops. */
    void (*pump)(bool frame);
    /* Menu "Planet Tech Announcements" (0x6465e0) checked? */
    bool (*tech_announcements)(void);
    /* Announce tech `tech` reached first (message box, sound 0x100a4 /
     * 0x100b2 + tech - 1 by variant 0/1). */
    void (*tech_discovered)(unsigned tech, unsigned variant);
    /* The disaster window exists and "Planet Disaster Announcements"
     * (0x6465e4) is checked: then the report counts the injured (an RNG
     * draw, so it's decided here). */
    bool (*disaster_reports)(void);
    /* 0x49de17: show disaster `kind` (0..9, 100 = the Flood). */
    void (*disaster)(unsigned kind, uint32_t killed, uint32_t injured);
    /* True while the disaster animation plays (0x50ace9): no new ones. */
    bool (*disaster_busy)(void);
    /* The "Here it comes!" box before the Flood (0x49c226). */
    void (*flood_warning)(void);
    /* wm_send(planetview, 0x83e8, 0, full): redraw the globe. */
    void (*view_refresh)(bool full);
    /* After a disaster: select the struck sector and show population
     * (0x49be17 twice and the layer buttons in 0x49c8b8). */
    void (*view_focus)(int x, int y);
    /* Planet regenerated (0x496ed1: wm_send(planetview, 0x83e9)). */
    void (*view_reset)(void);
    void (*busy_cursor)(bool on); /* ui_busy_cursor */
    void (*sound)(uint32_t id, int mode); /* snd_play */
} PlanetHooks;

extern PlanetHooks g_planet_hooks;

/* ---- Sector access --------------------------------------------------------- */

static inline PlanetSector *planet_sector(int x, int y)
{
    assert(x >= 0 && x < PLANET_W && y >= 0 && y < PLANET_H);
    return &g_state.sectors[x][y];
}
/* Pending influence and migration of a sector (int32 views of the saved
 * u32 arrays). */
static inline int32_t *planet_sin_acc(int x, int y) /* [7] */
{
    assert(x >= 0 && x < PLANET_W && y >= 0 && y < PLANET_H);
    return (int32_t *)&g_state.planet.sin_acc[(x * PLANET_H + y) * 7];
}
static inline int32_t *planet_tenet_acc(int x, int y) /* [9] */
{
    assert(x >= 0 && x < PLANET_W && y >= 0 && y < PLANET_H);
    return (int32_t *)&g_state.planet.tenet_acc[(x * PLANET_H + y) * 9];
}
static inline int32_t *planet_migration(int x, int y) /* [0] people, [1] tech */
{
    assert(x >= 0 && x < PLANET_W && y >= 0 && y < PLANET_H);
    return (int32_t *)g_state.planet.grid[x][y];
}

/* 0x49920d: sector touches a different terrain (or the map edge) and isn't
 * water itself: a coast. */
bool planet_is_coastal(int x, int y);
/* 0x498eb0: largest sector population. */
uint32_t planet_max_pop(void);
/* 0x497b72: (int)sqrt(dx^2 + dy^2), the original's float distance. */
int planet_distance(int x0, int y0, int x1, int y1);

/* ---- Simulation steps (sim_update phases) ---------------------------------- */

bool planet_stats_step(void);  /* 0x4aae01 planet_stats_accumulate, phase 20 */
bool planet_emit_step(void);   /* 0x4ab77a planet_emit_souls, phase 30 */
bool planet_sector_step(void); /* 0x49a2da, phase 10 */
bool planet_tech_step(void);   /* 0x49b05c planet_tech_update, phase 15 */
/* 0x49c8b8, phase 30 after the emit pass: disasters, end of the Flood.
 * `forced` 1..10 = disaster forced - 1 at g_planet.sel (Bad Things menu,
 * only with g_planet.cheats). Returns the EMBOs killed. */
uint32_t planet_year(unsigned forced);
/* 0x49c226: the Flood (scenario command 'F'): drowns all but one sector. */
void planet_flood(void);

/* Pieces, exposed for the view and for tests. */
uint32_t planet_sector_deaths(int x, int y); /* 0x4aa138: births/deaths, returns deaths */
void planet_tenet_drift(int x, int y);       /* 0x4aa783 */
void planet_soul_shares(int x, int y);       /* 0x4aaca0 */
/* 0x4ab5d4: split `deaths` of a sector into SOUL groups of about `size`
 * per belief type and pass them to the souls hook. */
void planet_send_souls(int x, int y, uint32_t deaths, uint32_t size);
void planet_set_rates(int x, int y);         /* 0x498dd5: rates from the tech level */
void planet_clear_sector(int x, int y, bool full); /* 0x49704f */
void planet_summary(void);                   /* 0x499a77 (also after loading) */
/* 0x4a2437: add `amount` to fate tenet `fate` (1..4), taking it from the
 * other fates. */
void planet_add_fate(int x, int y, unsigned amount, unsigned fate);

/* ---- World generation (game/planet_gen.c) ---------------------------------- */

/* 0x496ed1: a new planet (game_reset_world): clears every sector, generates
 * continents, ice caps and mountains, seeds the first tribe of 5000. */
void planet_generate(void);
/* 0x496e71: resets the planet counters (game_reset_world when the planet is
 * kept). */
void planet_reset_counters(void);

/* ---- The coastline map (game/planet_map.c) --------------------------------- */

/* Pixel test / set on the 640 x 320 bitmap (0x4a7d88 / 0x4a7d53; stride in
 * pixels). */
static inline int planet_map_test(const uint8_t *map, unsigned x, unsigned y, unsigned stride)
{
    return map[(y * stride + x) >> 3] >> (7 - (x & 7)) & 1;
}
static inline void planet_map_set(uint8_t *map, unsigned x, unsigned y, unsigned stride)
{
    map[(y * stride + x) >> 3] |= (uint8_t)(1u << (7 - (x & 7)));
}
/* 0x4a6b61: 3 x 3 neighbourhood mask of sector (x, y) (bit 4 = itself),
 * which picks the coastline shape. */
uint32_t planet_map_neighbours(int x, int y);
/* 0x4a8337: draws sector (x, y)'s land into the bitmap (rough coastlines:
 * draws from the RNG). Water sectors draw nothing. */
void planet_map_draw_sector(uint8_t *map, int x, int y, unsigned stride);
/* 0x4a7ca1: every sector, row by row. */
void planet_map_draw_all(uint8_t *map);
/* 0x49beee: clears sector (x, y)'s 10 x 10 pixels. */
void planet_map_clear_sector(uint8_t *map, int x, int y);

#endif
