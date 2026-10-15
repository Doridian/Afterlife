/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * ALSV saved games: docs/formats/alsv.md.
 *
 * Each block of the original has a load function and a mirror-image save
 * function; here one function per block walks the fields through a Ser,
 * which either reads them from a buffer or appends them to one. Everything
 * is little-endian; the version gates are the original's `version > N`
 * tests (decimal; the binary compares against hex constants).
 */
#include "game/savegame.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    bool writing;
    int ver;
    /* reading */
    const uint8_t *p, *end;
    /* writing */
    uint8_t *buf;
    size_t len, cap;
    bool bad;
    char err[128];
} Ser;

static void ser_fail(Ser *s, const char *fmt, ...)
{
    if (s->bad)
        return;
    s->bad = true;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s->err, sizeof s->err, fmt, ap);
    va_end(ap);
}

static uint8_t *ser_out(Ser *s, size_t n)
{
    if (s->bad)
        return NULL;
    if (s->len + n > s->cap) {
        size_t cap = s->cap ? s->cap : 1 << 20;
        while (cap < s->len + n)
            cap *= 2;
        uint8_t *nb = realloc(s->buf, cap);
        if (!nb) {
            ser_fail(s, "out of memory");
            return NULL;
        }
        s->buf = nb;
        s->cap = cap;
    }
    uint8_t *o = s->buf + s->len;
    s->len += n;
    return o;
}

static const uint8_t *ser_in(Ser *s, size_t n)
{
    if (s->bad)
        return NULL;
    if ((size_t)(s->end - s->p) < n) {
        ser_fail(s, "truncated");
        s->p = s->end;
        return NULL;
    }
    const uint8_t *i = s->p;
    s->p += n;
    return i;
}

static void s_bytes(Ser *s, uint8_t *v, size_t n)
{
    if (s->writing) {
        uint8_t *o = ser_out(s, n);
        if (o && n)
            memcpy(o, v, n);
    } else {
        const uint8_t *i = ser_in(s, n);
        if (i && n)
            memcpy(v, i, n);
        else if (n)
            memset(v, 0, n);
    }
}

/* 0x4d2307 file_read_u8 / 0x4d2371 file_write_u8 */
static void s_u8(Ser *s, uint8_t *v)
{
    s_bytes(s, v, 1);
}

static void s_i8(Ser *s, int8_t *v)
{
    uint8_t u = (uint8_t)*v;
    s_u8(s, &u);
    *v = (int8_t)u;
}

/* 0x4d22de file_read_u16 / 0x4d2349 file_write_u16 */
static void s_u16(Ser *s, uint16_t *v)
{
    uint8_t b[2] = {(uint8_t)*v, (uint8_t)(*v >> 8)};
    s_bytes(s, b, 2);
    *v = (uint16_t)(b[0] | b[1] << 8);
}

/* 0x4d22b6 file_read_u32 / 0x4d2323 file_write_u32 */
static void s_u32(Ser *s, uint32_t *v)
{
    uint8_t b[4] = {(uint8_t)*v, (uint8_t)(*v >> 8), (uint8_t)(*v >> 16), (uint8_t)(*v >> 24)};
    s_bytes(s, b, 4);
    *v = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

/* The hi word of a Bignum (a signed int in the original). */
static void s_bignum_hi(Ser *s, Bignum *b)
{
    uint32_t u = (uint32_t)b->hi;
    s_u32(s, &u);
    b->hi = u <= INT32_MAX ? (int32_t)u : -(int32_t)(~u) - 1;
}

static void s_u32s(Ser *s, uint32_t *v, size_t n)
{
    for (size_t i = 0; i < n; i++)
        s_u32(s, &v[i]);
}

static void s_u16s(Ser *s, uint16_t *v, size_t n)
{
    for (size_t i = 0; i < n; i++)
        s_u16(s, &v[i]);
}

/* A NUL-terminated string, read byte by byte like the original (which has
 * no bound; here an overlong one is an error). */
static void s_cstr(Ser *s, char *str, size_t cap)
{
    if (s->writing) {
        size_t n = 0;
        while (n + 1 < cap && str[n])
            n++;
        s_bytes(s, (uint8_t *)str, n);
        uint8_t z = 0;
        s_u8(s, &z);
        return;
    }
    size_t n = 0;
    for (;;) {
        uint8_t c = 0;
        s_u8(s, &c);
        if (s->bad || c == 0)
            break;
        if (n + 1 >= cap) {
            ser_fail(s, "string longer than %zu bytes", cap - 1);
            break;
        }
        str[n++] = (char)c;
    }
    str[n] = 0;
}

/* SimCell bytes at an offset, as u8/u16/u32 (the record keeps them LE). */
static void s_cell8(Ser *s, SimCell *c, int off)
{
    s_u8(s, &c->b[off]);
}
static void s_cell16(Ser *s, SimCell *c, int off)
{
    s_bytes(s, &c->b[off], 2);
}
static void s_cell32(Ser *s, SimCell *c, int off)
{
    s_bytes(s, &c->b[off], 4);
}

/* The fields of a simulation cell after the kind byte, by the cell's tile
 * set (switch in 0x48a83a / 0x481ba4). Cells covered by a big building
 * other than its origin have set 0. Sets 10, 11 and 20..28 have none. */
static void sim_cell_fields(Ser *s, SimCell *c, unsigned set)
{
    int v = s->ver;
    switch (set) {
    case 0:
        s_cell8(s, c, 1), s_cell32(s, c, 2), s_cell32(s, c, 6), s_cell8(s, c, 10), s_cell8(s, c, 11);
        break;
    case 1: case 4: case 16:
        s_cell8(s, c, 1);
        break;
    case 2:
        s_cell8(s, c, 1), s_cell8(s, c, 2);
        break;
    case 3:
        s_cell32(s, c, 1), s_cell32(s, c, 5), s_cell8(s, c, 9);
        break;
    case 5: case 12:
        s_cell32(s, c, 1), s_cell32(s, c, 5), s_cell32(s, c, 9), s_cell8(s, c, 13);
        if (v > (set == 5 ? 7104 : 7019))
            s_cell16(s, c, 14);
        break;
    case 6:
        s_cell32(s, c, 1), s_cell8(s, c, 5), s_bytes(s, &c->b[6], 9), s_cell8(s, c, 15);
        break;
    case 7:
        s_cell32(s, c, 1), s_cell8(s, c, 5);
        break;
    case 8: case 13:
        s_cell32(s, c, 1), s_cell32(s, c, 5), s_cell32(s, c, 9), s_cell8(s, c, 13), s_cell8(s, c, 14);
        break;
    case 9:
        s_cell32(s, c, 1), s_cell32(s, c, 5), s_cell32(s, c, 9);
        s_cell8(s, c, 13), s_cell8(s, c, 14), s_cell8(s, c, 15);
        break;
    case 14: case 15:
        s_cell8(s, c, 1), s_cell32(s, c, 2), s_cell32(s, c, 6), s_cell32(s, c, 10);
        s_cell8(s, c, 14), s_cell8(s, c, 15), s_cell8(s, c, 16), s_cell8(s, c, 17);
        break;
    case 17:
        s_cell32(s, c, 1), s_cell32(s, c, 5), s_cell8(s, c, 9), s_cell8(s, c, 10);
        break;
    case 18:
        s_cell32(s, c, 1);
        break;
    case 19:
        /* older saves leave +1..+6 zero */
        if (v > 7089)
            s_cell32(s, c, 1), s_cell8(s, c, 5), s_cell8(s, c, 6);
        break;
    case 29:
        if (v > 7089)
            s_cell32(s, c, 1), s_cell32(s, c, 5);
        break;
    case 30:
        if (v > 7089)
            s_cell32(s, c, 1), s_cell32(s, c, 5), s_cell32(s, c, 9);
        if (v > 7209)
            s_cell8(s, c, 13), s_cell8(s, c, 14);
        break;
    default:
        break;
    }
}

static void s_hist(Ser *s, PlaneStatHist *h)
{
    s_u32(s, &h->unk_00), s_u32(s, &h->unk_04);
    s_u32s(s, h->v, 32);
    s_u8(s, &h->unk_88);
}

/* The statistics record after the cells of Hell and Heaven. */
static void s_plane_stats(Ser *s, PlaneStats *t)
{
    int v = s->ver;
    for (int i = 0; i < 112; i++)
        s_hist(s, &t->hist_a[i]);
    for (int i = 0; i < 112; i++)
        s_hist(s, &t->hist_b[i]);
    s_u32s(s, t->unk_77e0, 16);
    s_u32s(s, t->unk_7820, 16);
    s_bytes(s, &t->ports[0][0], sizeof t->ports);
    if (v > 7109)
        s_bytes(s, &t->unk_79c4[0][0], sizeof t->unk_79c4);
    s_bytes(s, &t->unk_78c4[0][0], sizeof t->unk_78c4);
    s_u32s(s, t->unk_79ec, 7);
    for (int i = 0; i < 16; i++) {
        s_u32(s, &t->unk_7a08[i].unk_00);
        s_u32s(s, t->unk_7a08[i].v, 16);
    }
    s_bytes(s, &t->unk_7e48[0][0], sizeof t->unk_7e48);
    s_u32s(s, t->unk_7e68, 16);
    s_u32(s, &t->unk_7ea8);
    s_u32s(s, t->unk_7eac, 16);
    s_u32(s, &t->unk_7eec);
    s_u32s(s, t->unk_7ef0, 16);
    s_u32(s, &t->unk_7f30), s_u32(s, &t->unk_7f34), s_u32(s, &t->unk_7f38);
    s_u32s(s, t->unk_7f3c, 7);
    s_u32s(s, t->unk_7f58, 7);
    s_u32s(s, t->unk_7f74, 7);
    s_u32s(s, t->unk_7f90, 7);
    s_u32s(s, t->unk_7fac, 8);
    if (v > 7179)
        s_u32s(s, t->unk_7fcc, 8);
    s_u32(s, &t->population);
    s_u32s(s, t->unk_7ff0, 21);
    s_u8(s, &t->n_ports), s_u8(s, &t->unk_8045), s_u8(s, &t->unk_8046);
    s_u32(s, &t->unk_8047), s_u32(s, &t->unk_804b);
    s_u32s(s, t->unk_804f, 5);
    if (v > 7179)
        s_u32s(s, t->unk_8063, 5);
    s_u32s(s, t->unk_8077, 5);
    s_u32s(s, t->unk_808b, 5);
    s_u32s(s, t->unk_809f, 16);
    s_u16(s, &t->n_zones), s_u16(s, &t->unk_80e1);
    s_u32(s, &t->unk_80e3), s_u32(s, &t->unk_80e7), s_u32(s, &t->unk_80eb);
    s_u8(s, &t->unk_80ef);
    /* The original clamps on load; keep the file value so saves
     * round-trip, the clamp belongs to whoever applies the state. */
    s_u32(s, &t->unk_80f0), s_u32(s, &t->unk_80f4);
    s_u8(s, &t->unk_80f8), s_u8(s, &t->unk_80f9), s_u8(s, &t->unk_80fa);
    s_u8(s, &t->unk_80fb), s_u8(s, &t->unk_80fc);
    s_bignum_hi(s, &t->souls), s_u32(s, &t->souls.lo);
    if (v > 6999)
        s_u16(s, &t->unk_8105), s_u16(s, &t->unk_8107), s_u16(s, &t->unk_8109);
    if (v > 7010)
        s_u32(s, &t->unk_810b), s_u32(s, &t->unk_810f), s_u32(s, &t->unk_8113), s_u32(s, &t->unk_8117);
    if (v > 7024)
        s_u32(s, &t->unk_811b);
    if (v > 7139)
        s_u32(s, &t->unk_811f);
    if (v > 7165)
        s_u32(s, &t->unk_8123);
    if (v > 7174)
        s_u32(s, &t->unk_8127);
}

/* 0x48a83a save_load_plane / 0x481ba4 save_write_plane. The cells go
 * column by column (x outer), unlike the row-major grids. */
static void s_plane(Ser *s, MapPlaneState *pl, int plane)
{
    for (int x = 0; x < GS_MAP_W; x++)
        for (int y = 0; y < GS_MAP_H; y++) {
            int i = y * GS_MAP_W + x;
            uint32_t c = pl->cells[i];
            if (s->writing)
                c &= ~0x10u; /* the writer drops CELL_ALT */
            s_u32(s, &c);
            if (!s->writing)
                pl->cells[i] = c & ~0x80u; /* the reader drops CELL_HILITE */
            s_u8(s, &pl->objmap[i]);
            if (plane != GS_PLANE_MIDDLE) {
                SimCell *sc = &pl->sim[i];
                s_u8(s, &sc->b[0]);
                /* the set of the cell as stored in memory */
                sim_cell_fields(s, sc, pl->cells[i] >> 27);
            }
        }
    if (plane != GS_PLANE_MIDDLE)
        s_plane_stats(s, &pl->stats);
}

static void s_grid_rec(Ser *s, PlanetSector *r)
{
    s_u32(s, &r->pop);
    s_u8(s, &r->death_rate), s_u8(s, &r->birth_rate), s_u8(s, &r->tech);
    s_bytes(s, r->tenet, 9);
    s_bytes(s, r->soul_frac, 16);
    s_bytes(s, r->sin, 7);
    s_u8(s, &r->terrain), s_u8(s, &r->height), s_u8(s, &r->flags);
}

static void s_map_object(Ser *s, MapObjectRec *o)
{
    s_u8(s, &o->plane);
    s_i8(s, &o->x), s_i8(s, &o->y), s_i8(s, &o->dx), s_i8(s, &o->dy), s_i8(s, &o->size);
    s_u8(s, &o->set), s_u8(s, &o->frame), s_u8(s, &o->first_frame);
    s_u8(s, &o->delay), s_u8(s, &o->delay_reset);
    s_u32(s, &o->set_mask);
    s_u8(s, &o->fade), s_u8(s, &o->unk_10), s_u8(s, &o->unk_11), s_u8(s, &o->priority);
    s_u32(s, &o->flags);
}

static void s_rec72(Ser *s, Rec72 *r)
{
    s_u32s(s, r->v, 25);
    s_u32(s, &r->min), s_u32(s, &r->max);
    s_u8(s, &r->count), s_u8(s, &r->unk_6d);
    if (s->ver > 7124)
        s_u32(s, &r->shift);
}

/* Everything between the planes and the sub-module blocks (body of
 * 0x48f837 after the plane loop). */
static void s_globals(Ser *s, GameState *g)
{
    int v = s->ver;
    for (int i = 0; i < 64; i++)
        for (int j = 0; j < 32; j++)
            s_grid_rec(s, &g->sectors[i][j]);
    s_bytes(s, &g->unk_64e500[0][0], sizeof g->unk_64e500);
    for (int i = 0; i < GS_MAP_OBJECTS; i++)
        s_map_object(s, &g->objects[i]);
    for (int i = 0; i < 0x2000; i++) {
        SoulGroup *r = &g->soul_groups[i];
        s_u32(s, &r->count), s_u16(s, &r->mask), s_u32(s, &r->next), s_u8(s, &r->age);
    }
    s_u32s(s, g->unk_5b2f10, 16);
    for (int i = 0; i < 7; i++) {
        s_u8(s, &g->unk_593240[i]), s_u8(s, &g->unk_57e060[i]);
        s_u8(s, &g->unk_593260[i][0]), s_u8(s, &g->unk_593260[i][1]);
        s_u32(s, &g->unk_5fa600[i]);
        s_u32(s, &g->unk_6303d0[i][0]), s_u32(s, &g->unk_6303d0[i][1]);
        s_u32(s, &g->unk_5bb210[i]), s_u32(s, &g->unk_5bb22c[i]), s_u32(s, &g->unk_5bb248[i]);
    }
    for (int i = 0; i < 4; i++) {
        s_bytes(s, g->unk_5f25b0[i], 3);
        s_bytes(s, g->karma_dirs[i], 3);
        s_u32(s, &g->unk_5fa660[i]);
    }
    for (int i = 0, n = v < 7130 ? 18 : 30; i < n; i++)
        s_rec72(s, &g->rec_5c4eb0[i]);

    s_u32(s, &g->unk_62659c), s_u32(s, &g->unk_5b2f54);
    s_u8(s, &g->unk_5b2f58), s_u8(s, &g->unk_630426), s_u8(s, &g->unk_5fa61d), s_u8(s, &g->unk_57e067);
    s_u32(s, &g->unk_6265a0), s_u32(s, &g->unk_57e020), s_u32(s, &g->unk_57e1b4);
    s_u32(s, &g->unk_5bb114), s_u32(s, &g->unk_5f7464), s_u32(s, &g->planet_cursor);
    s_u8(s, &g->unk_593254), s_u8(s, &g->unk_5b2f60);
    s_u32(s, &g->embos_next.lo), s_u32(s, &g->embos.lo);
    if (v > 7299)
        s_bignum_hi(s, &g->embos_next), s_bignum_hi(s, &g->embos);
    else if (!s->writing)
        g->embos_next.hi = g->embos.hi = 0;
    s_u32(s, &g->unk_630430);
    s_u32(s, &g->pennies.lo);
    if (v > 7014)
        s_bignum_hi(s, &g->pennies);
    s_u32(s, &g->year);
    s_i8(s, &g->view_dir_x), s_i8(s, &g->view_dir_y);
    s_u32(s, &g->unk_63042c), s_u32(s, &g->unk_638668), s_u32(s, &g->unk_626594);
    s_u32(s, &g->unk_57e1ac), s_u32(s, &g->unk_5bb10c);
    s_u8(s, &g->unk_63041c), s_u8(s, &g->unk_630424), s_u8(s, &g->boat_count), s_u8(s, &g->sim_phase);
    s_u32(s, &g->planet_deaths_last);
    if (v > 7100)
        s_u32(s, &g->planet_deaths);
    s_u8(s, &g->karma_pending), s_u8(s, &g->karma_stations);
    s_u32(s, &g->karma_busy);
    s_u8(s, &g->unk_5b2f61);
    s_u32(s, &g->unk_5f746c), s_u32(s, &g->unk_5b2f5c);
    s_u8(s, &g->object_count);
    for (int i = 0; i < 20; i++) {
        if (v > 7149)
            s_u8(s, &g->unk_5bb0b0[i][0]);
        s_u8(s, &g->unk_5bb0b0[i][1]), s_u8(s, &g->unk_5bb0b0[i][2]);
        if (v > 7149)
            s_u8(s, &g->unk_593200[i][0]);
        s_u8(s, &g->unk_593200[i][1]), s_u8(s, &g->unk_593200[i][2]);
    }
    s_u8(s, &g->unk_57e098), s_u8(s, &g->unk_57e09a), s_u8(s, &g->unk_5bb110), s_u8(s, &g->unk_638653);
    s_u8(s, &g->unk_638654), s_u8(s, &g->unk_63050c), s_u8(s, &g->unk_630484), s_u8(s, &g->unk_638664);
    s_u8(s, &g->unk_5f25a8), s_u8(s, &g->unk_5931f0), s_u8(s, &g->unk_5f7ed0);
    for (int i = 0; i < 8; i++)
        s_u8(s, &g->unk_5f7400[i]), s_u32(s, &g->unk_638670[i]);
    s_bytes(s, &g->unk_5f4cd0[0][0], sizeof g->unk_5f4cd0);
    if (v > 6999)
        s_u8(s, &g->unk_593247);
    if (v > 7009) {
        s_u16s(s, g->karma_load, 16);
        s_u16s(s, g->karma_history, 16);
        s_u16(s, &g->unk_5c6678);
        s_u8(s, &g->unk_630425);
    }
    if (v > 7099)
        s_u32(s, &g->unk_626598);
    if (v > 7169)
        s_u32(s, &g->lost_souls);
    if (v > 7109)
        s_u8(s, &g->planet_aaaa);
    if (v > 7129)
        s_u32(s, &g->unk_638690), s_u32(s, &g->unk_593250);
    if (v > 7139)
        s_u32(s, &g->unk_57e09c);
    if (v > 7159) {
        s_u8(s, &g->unk_5931c1), s_u8(s, &g->unk_63026c), s_u8(s, &g->unk_59324c), s_u8(s, &g->unk_63050d);
        s_u32(s, &g->unk_5fa5f4), s_u32(s, &g->unk_5f7ed4);
    }
    if (v > 7199)
        s_u32(s, &g->unk_57e058), s_u32(s, &g->unk_630420), s_u32(s, &g->unk_5bb108);
    if (v > 7029) {
        s_cstr(s, g->scenario_title, sizeof g->scenario_title);
        s_cstr(s, g->scenario_description, sizeof g->scenario_description);
        s_cstr(s, g->scenario_button, sizeof g->scenario_button);
        s_cstr(s, g->scenario_commands, sizeof g->scenario_commands);
    }
    if (v > 7184)
        s_u32(s, &g->unk_63048c), s_u32(s, &g->unk_57e160);
    if (v > 7249)
        s_u32(s, &g->bad_things);
    if (v > 7279)
        s_u16(s, &g->unk_5b2f5a);
}

/* 0x40c106 / 0x40b1c0: the two graph summary records. */
static void s_summaries(Ser *s, GameState *g)
{
    int v = s->ver;
    if (v <= 7160)
        return; /* nothing below 7161 */
    for (int k = 0; k < 2; k++) {
        GraphSummary *r = &g->summary[k];
        s_u32s(s, r->unk_000, 16);
        s_bytes(s, &r->unk_040[0][0], sizeof r->unk_040);
        s_u32s(s, r->unk_068, 7);
        s_u32s(s, r->unk_084, 7);
        s_u32s(s, r->unk_0a0, 7);
        s_u32s(s, r->unk_0bc, 7);
        s_u32s(s, r->unk_0d8, 8);
        if (v > 7179)
            s_u32s(s, r->unk_0f8, 8);
        s_u32s(s, r->unk_118, 18);
        s_u32s(s, r->unk_160, 5);
        if (v > 7179)
            s_u32s(s, r->unk_174, 5);
        s_u32s(s, r->unk_188, 5);
        s_u8(s, &r->unk_19c);
        s_u32(s, &r->unk_19d), s_u32(s, &r->unk_1a1), s_u32(s, &r->unk_1a5), s_u32(s, &r->unk_1a9);
        if (v > 7174)
            s_u32(s, &r->unk_1ad);
    }
}

/* 0x495f7c save_load_planet / 0x495300 save_write_planet. */
static void s_planet(Ser *s, PlanetState *p)
{
    int v = s->ver;
    s_u32(s, &p->map_size);
    if (!s->writing && !s->bad) {
        /* the original allocates it once (0x4a50f6) and fails on 0 */
        if (p->map_size == 0 || p->map_size > (size_t)(s->end - s->p)) {
            ser_fail(s, "bad planet map size %u", (unsigned)p->map_size);
            return;
        }
        p->map = malloc(p->map_size);
        if (!p->map) {
            ser_fail(s, "out of memory");
            return;
        }
    }
    s_bytes(s, p->map, p->map_size);
    s_u8(s, &p->disaster);
    s_u32(s, &p->disaster_year);
    if (v > 7011)
        s_u16(s, &p->belief_drift);
    s_u8(s, &p->n_a);
    for (int i = 0; i < p->n_a; i++) {
        s_u32(s, &p->rec_a[i].x), s_u32(s, &p->rec_a[i].y);
        s_u16s(s, p->rec_a[i].v, 7);
    }
    s_u8(s, &p->n_b);
    for (int i = 0; i < p->n_b; i++) {
        s_u32(s, &p->rec_b[i].x), s_u32(s, &p->rec_b[i].y);
        s_u16s(s, p->rec_b[i].v, 9);
    }
    s_u8(s, &p->n_c);
    for (int i = 0; i < p->n_c; i++) {
        s_u32(s, &p->rec_c[i].x), s_u32(s, &p->rec_c[i].y);
        s_u8(s, &p->rec_c[i].left), s_u8(s, &p->rec_c[i].initial);
    }
    if (v > 7194) {
        s_u8(s, &p->sector_row), s_u8(s, &p->tech_row);
        for (int j = 0; j < 32; j++)
            for (int i = 0; i < 64; i++)
                s_u32(s, &p->grid[i][j][0]), s_u32(s, &p->grid[i][j][1]);
    }
    s_u32s(s, p->sin_acc, 0x3800);
    s_u32s(s, p->tenet_acc, 0x4800);
    if (v > 7100)
        s_u32(s, &p->tech_year);
    if (v > 7204)
        s_u8(s, &p->tech_best);
    if (v > 7166)
        s_u32(s, &p->coastal_pop);
    if (v > 7219) {
        s_bytes(s, p->sin_avg, 7);
        s_u32s(s, p->sin_sum, 7);
        s_u32(s, &p->pop_k);
    }
}

/* 0x47951c / 0x478d50 */
static void s_block26(Ser *s, Block26 *b)
{
    s_u8(s, &b->selected), s_u8(s, &b->unk_664808), s_u8(s, &b->unk_66480c);
    for (int i = 0; i < 10; i++) {
        Rec26 *r = &b->rec[i];
        s_bytes(s, r->unk_00, 8);
        s_u32(s, &r->unk_08), s_u32(s, &r->unk_0c);
        s_u8(s, &r->unk_10), s_u8(s, &r->unk_11), s_u8(s, &r->unk_12);
        s_u32(s, &r->unk_13), s_u32(s, &r->unk_17);
        s_u8(s, &r->unk_1b);
        s_u32(s, &r->unk_1c);
        s_u16(s, &r->unk_20);
        s_u8(s, &r->unk_22), s_u8(s, &r->unk_23), s_u8(s, &r->unk_25);
    }
}

/* 0x48f837 save_load_game / 0x485fb3 save_write_game */
static void s_game(Ser *s, GameState *g)
{
    SaveHeader *h = &g->hdr;
    uint8_t magic[8];
    if (s->writing) {
        if (h->version < 0 || h->version > 9999) {
            ser_fail(s, "bad version %d", h->version);
            return;
        }
        char tmp[9];
        snprintf(tmp, sizeof tmp, "ALSV%04d", h->version);
        memcpy(magic, tmp, 8);
    }
    s_bytes(s, magic, 8);
    if (!s->writing) {
        if (s->bad || memcmp(magic, "ALSV", 4) != 0) {
            ser_fail(s, "not an ALSV file");
            return;
        }
        int ver = 0;
        for (int i = 4; i < 8; i++) {
            if (magic[i] < '0' || magic[i] > '9') {
                ser_fail(s, "bad version digits");
                return;
            }
            ver = ver * 10 + (magic[i] - '0');
        }
        h->version = ver;
    }
    s->ver = h->version;
    int v = s->ver;

    s_u8(s, &h->kind);
    s_u32(s, &h->timestamp);
    s_u32(s, &h->year);
    s_cstr(s, h->title, sizeof h->title);
    if (v > 7219)
        s_u16(s, &h->difficulty);
    if (v > 7025) {
        s_u32(s, &h->thumb_w), s_u32(s, &h->thumb_h);
        if (!s->writing && !s->bad) {
            uint64_t n = (uint64_t)h->thumb_w * h->thumb_h;
            if (n > (uint64_t)(s->end - s->p)) {
                ser_fail(s, "bad thumbnail size %ux%u", (unsigned)h->thumb_w, (unsigned)h->thumb_h);
                return;
            }
            h->thumb = malloc(n ? (size_t)n : 1);
            if (!h->thumb) {
                ser_fail(s, "out of memory");
                return;
            }
        }
        /* the original seeks over it on load */
        s_bytes(s, h->thumb, (size_t)h->thumb_w * h->thumb_h);
    }

    for (int pl = 1; pl <= 3; pl++)
        s_plane(s, &g->plane[pl], pl);
    s_globals(s, g);
    s_summaries(s, g);
    s_planet(s, &g->planet);
    s_block26(s, &g->block_4f33f8);
}

void savegame_free(GameState *gs)
{
    free(gs->hdr.thumb);
    gs->hdr.thumb = NULL;
    free(gs->planet.map);
    gs->planet.map = NULL;
}

static void set_err(char *err, size_t err_len, const char *msg)
{
    if (err && err_len)
        snprintf(err, err_len, "%s", msg);
}

bool savegame_load_mem(GameState *gs, const uint8_t *data, size_t len, char *err, size_t err_len)
{
    memset(gs, 0, sizeof *gs);
    Ser s = {.writing = false, .p = data, .end = data + len};
    s_game(&s, gs);
    if (!s.bad && s.p != s.end)
        ser_fail(&s, "%zu trailing bytes", (size_t)(s.end - s.p));
    if (s.bad) {
        set_err(err, err_len, s.err);
        savegame_free(gs);
        memset(gs, 0, sizeof *gs);
        return false;
    }
    return true;
}

bool savegame_load(GameState *gs, const char *path, char *err, size_t err_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        set_err(err, err_len, "can't open file");
        return false;
    }
    uint8_t *data = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (len == cap) {
            cap = cap ? cap * 2 : 1 << 20;
            uint8_t *nd = realloc(data, cap);
            if (!nd) {
                free(data);
                fclose(f);
                set_err(err, err_len, "out of memory");
                return false;
            }
            data = nd;
        }
        size_t n = fread(data + len, 1, cap - len, f);
        len += n;
        if (n == 0)
            break;
    }
    bool rd_err = ferror(f) != 0;
    fclose(f);
    bool ok = !rd_err && savegame_load_mem(gs, data, len, err, err_len);
    if (rd_err)
        set_err(err, err_len, "read error");
    free(data);
    return ok;
}

uint8_t *savegame_save_mem(const GameState *gs, size_t *len)
{
    /* s_game only writes through the pointer when reading */
    Ser s = {.writing = true};
    s_game(&s, (GameState *)gs);
    if (s.bad) {
        free(s.buf);
        return NULL;
    }
    *len = s.len;
    return s.buf;
}

bool savegame_save(const GameState *gs, const char *path)
{
    size_t len;
    uint8_t *buf = savegame_save_mem(gs, &len);
    if (!buf)
        return false;
    FILE *f = fopen(path, "wb");
    bool ok = f && fwrite(buf, 1, len, f) == len;
    if (f && fclose(f) != 0)
        ok = false;
    free(buf);
    return ok;
}

/* 0x47fc20: (tm_year + 32) << 26 | tm_mon << 22 | tm_mday << 17 |
 * tm_hour << 12 | tm_min << 6 | tm_sec. The year field is 6 bits, so it
 * wraps every 64 years; it is placed in 1996..2059. */
SaveTimestamp savegame_decode_timestamp(uint32_t ts)
{
    SaveTimestamp t;
    t.second = (int)(ts & 0x3f);
    t.minute = (int)(ts >> 6 & 0x3f);
    t.hour = (int)(ts >> 12 & 0x1f);
    t.day = (int)(ts >> 17 & 0x1f);
    t.month = (int)(ts >> 22 & 0xf) + 1;
    int y = (int)(ts >> 26 & 0x3f);           /* (tm_year + 32) mod 64 */
    t.year = 1900 + ((y - 32 - 96) & 63) + 96; /* tm_year in 96..159 */
    return t;
}
