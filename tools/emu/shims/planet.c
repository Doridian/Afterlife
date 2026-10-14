/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * tools/emu wrappers for the planet (WP5, src/game/planet.c): the planet
 * state converted to and from the original's memory layout (field by field,
 * little-endian), and a log of the SOULs handed to the souls hook. Used by
 * tools/emu/test_planet.py.
 */
#include <stdint.h>
#include <string.h>

#include "game/planet.h"
#include "platform/platform.h"

#if defined(_WIN32)
#define SHIM_API __declspec(dllexport)
#else
#define SHIM_API __attribute__((visibility("default")))
#endif

/* ---- SOUL log ------------------------------------------------------------ */

#define SOUL_LOG_MAX 65536
static uint32_t g_soul_log[SOUL_LOG_MAX][5];
static int g_soul_n;

static void log_souls(int x, int y, int type, uint32_t count, uint32_t size)
{
    if (g_soul_n < SOUL_LOG_MAX) {
        uint32_t *e = g_soul_log[g_soul_n];
        e[0] = (uint32_t)x, e[1] = (uint32_t)y, e[2] = (uint32_t)type, e[3] = count, e[4] = size;
    }
    g_soul_n++;
}

SHIM_API int shim_planet_init(const char *data_dir)
{
    plat_set_data_dir(data_dir);
    memset(&g_planet_hooks, 0, sizeof g_planet_hooks);
    g_planet_hooks.souls = log_souls;
    return planet_tables_load() ? 1 : 0;
}

/* Number of entries since the last call; copies up to max of them. */
SHIM_API int shim_planet_soul_log(uint32_t *out, int max)
{
    int n = g_soul_n < SOUL_LOG_MAX ? g_soul_n : SOUL_LOG_MAX;
    memcpy(out, g_soul_log, (size_t)(n < max ? n : max) * sizeof g_soul_log[0]);
    int all = g_soul_n;
    g_soul_n = 0;
    return all;
}

/* ---- state in the original layout ------------------------------------------ */

static void put(uint8_t *b, uint64_t v, int n)
{
    for (int i = 0; i < n; i++)
        b[i] = (uint8_t)(v >> (8 * i));
}
static uint64_t get(const uint8_t *b, int n)
{
    uint64_t v = 0;
    for (int i = 0; i < n; i++)
        v |= (uint64_t)b[i] << (8 * i);
    return v;
}

/* One integer array: `count` elements of `elem` bytes at p (host ints of
 * that width), `stride` bytes apart in the original. */
static int xfer(void *p, int elem, int count, int stride, uint8_t *buf, int write)
{
    for (int i = 0; i < count; i++) {
        uint8_t *o = buf + i * stride;
        uint8_t *h = (uint8_t *)p + i * elem;
        uint64_t v;
        if (write) {
            v = get(o, elem);
            if (elem == 1)
                *h = (uint8_t)v;
            else if (elem == 2)
                *(uint16_t *)(void *)h = (uint16_t)v;
            else
                *(uint32_t *)(void *)h = (uint32_t)v;
        } else {
            v = elem == 1 ? *h : elem == 2 ? *(uint16_t *)(void *)h : *(uint32_t *)(void *)h;
            put(o, v, elem);
        }
    }
    return count * stride;
}

static int sectors(uint8_t *buf, int write)
{
    for (int x = 0; x < PLANET_W; x++)
        for (int y = 0; y < PLANET_H; y++) {
            PlanetSector *s = &g_state.sectors[x][y];
            uint8_t *o = buf + x * 0x540 + y * 0x2a;
            if (write) {
                s->pop = (uint32_t)get(o, 4);
                s->death_rate = o[4], s->birth_rate = o[5], s->tech = o[6];
                memcpy(s->tenet, o + 7, 9);
                memcpy(s->soul_frac, o + 0x10, 16);
                memcpy(s->sin, o + 0x20, 7);
                s->terrain = o[0x27], s->height = o[0x28], s->flags = o[0x29];
            } else {
                put(o, s->pop, 4);
                o[4] = s->death_rate, o[5] = s->birth_rate, o[6] = s->tech;
                memcpy(o + 7, s->tenet, 9);
                memcpy(o + 0x10, s->soul_frac, 16);
                memcpy(o + 0x20, s->sin, 7);
                o[0x27] = s->terrain, o[0x28] = s->height, o[0x29] = s->flags;
            }
        }
    return PLANET_SECTORS * 0x2a;
}

/* Converts the named part between the port state and buf (the original's
 * bytes at the address test_planet.py knows for that name). write = 1:
 * buf -> port. Returns the byte size, -1 for an unknown name. Records are
 * limited to the original's 64 entries. */
SHIM_API int shim_planet_raw(const char *name, uint8_t *buf, int write)
{
    GameState *g = &g_state;
    PlanetState *ps = &g->planet;
    PlanetRuntime *r = &g_planet;
    if (!strcmp(name, "sectors"))
        return sectors(buf, write);
    if (!strcmp(name, "rec_a_xy")) {
        for (int i = 0; i < 64; i++)
            xfer(&ps->rec_a[i].x, 4, 2, 4, buf + i * 8, write);
        return 64 * 8;
    }
    if (!strcmp(name, "rec_a_v")) {
        for (int i = 0; i < 64; i++)
            xfer(ps->rec_a[i].v, 2, 7, 2, buf + i * 14, write);
        return 64 * 14;
    }
    if (!strcmp(name, "rec_b_xy")) {
        for (int i = 0; i < 64; i++)
            xfer(&ps->rec_b[i].x, 4, 2, 4, buf + i * 8, write);
        return 64 * 8;
    }
    if (!strcmp(name, "rec_b_v")) {
        for (int i = 0; i < 64; i++)
            xfer(ps->rec_b[i].v, 2, 9, 2, buf + i * 18, write);
        return 64 * 18;
    }
    if (!strcmp(name, "rec_c_xy")) {
        for (int i = 0; i < 64; i++)
            xfer(&ps->rec_c[i].x, 4, 2, 4, buf + i * 8, write);
        return 64 * 8;
    }
    if (!strcmp(name, "rec_c_left")) {
        for (int i = 0; i < 64; i++)
            xfer(&ps->rec_c[i].left, 1, 1, 1, buf + i, write);
        return 64;
    }
    if (!strcmp(name, "rec_c_initial")) {
        for (int i = 0; i < 64; i++)
            xfer(&ps->rec_c[i].initial, 1, 1, 1, buf + i, write);
        return 64;
    }
#define F(n, p, e, c)                                                                                                  \
    if (!strcmp(name, n))                                                                                              \
    return xfer(p, e, c, e, buf, write)
    F("grid", ps->grid, 4, 64 * 32 * 2);
    F("sin_acc", ps->sin_acc, 4, 0x3800);
    F("tenet_acc", ps->tenet_acc, 4, 0x4800);
    F("n_a", &ps->n_a, 1, 1);
    F("n_b", &ps->n_b, 1, 1);
    F("n_c", &ps->n_c, 1, 1);
    F("disaster", &ps->disaster, 1, 1);
    F("disaster_year", &ps->disaster_year, 4, 1);
    F("belief_drift", &ps->belief_drift, 2, 1);
    F("sector_row", &ps->sector_row, 1, 1);
    F("tech_row", &ps->tech_row, 1, 1);
    F("tech_year", &ps->tech_year, 4, 1);
    F("tech_best", &ps->tech_best, 1, 1);
    F("coastal_pop", &ps->coastal_pop, 4, 1);
    F("sin_avg", ps->sin_avg, 1, 7);
    F("sin_sum", ps->sin_sum, 4, 7);
    F("pop_k", &ps->pop_k, 4, 1);
    F("planet_cursor", &g->planet_cursor, 4, 1);
    F("planet_deaths_last", &g->planet_deaths_last, 4, 1);
    F("planet_deaths", &g->planet_deaths, 4, 1);
    F("planet_aaaa", &g->planet_aaaa, 1, 1);
    F("embos_hi", &g->embos.hi, 4, 1);
    F("embos_lo", &g->embos.lo, 4, 1);
    F("embos_next_hi", &g->embos_next.hi, 4, 1);
    F("embos_next_lo", &g->embos_next.lo, 4, 1);
    F("year", &g->year, 4, 1);
    F("unk_5c6678", &g->unk_5c6678, 2, 1);
    F("unk_57e058", &g->unk_57e058, 4, 1);
    F("unk_630420", &g->unk_630420, 4, 1);
    F("unk_5bb108", &g->unk_5bb108, 4, 1);
    F("avg_ralf", &r->avg_ralf, 1, 1);
    F("avg_not_ocra", &r->avg_not_ocra, 1, 1);
    F("avg_not_opra", &r->avg_not_opra, 1, 1);
    F("acc", r->acc, 4, 4);
    F("max_tech", &r->max_tech, 1, 1);
    F("sin_min", r->sin_min, 1, 7);
    F("sin_min_pop", r->sin_min_pop, 4, 7);
    F("drift", &r->drift, 2, 1);
    F("announced", &r->announced, 1, 1);
    F("cheats", &r->cheats, 1, 1);
#undef F
    if (!strcmp(name, "sin_min_xy")) {
        for (int i = 0; i < 7; i++) {
            xfer(&r->sin_min_x[i], 4, 1, 4, buf + i * 8, write);
            xfer(&r->sin_min_y[i], 4, 1, 4, buf + i * 8 + 4, write);
        }
        return 56;
    }
    if (!strcmp(name, "map")) {
        if (!ps->map)
            return 0;
        if (write)
            memcpy(ps->map, buf, ps->map_size);
        else
            memcpy(buf, ps->map, ps->map_size);
        return (int)ps->map_size;
    }
    return -1;
}

SHIM_API void shim_planet_set_sel(int x, int y)
{
    g_planet.sel_x = x;
    g_planet.sel_y = y;
}
