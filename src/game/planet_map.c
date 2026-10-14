/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Planetview's coastline bitmap (ALIFE.EXE 0x4a6b61-0x4aa137, 0x49beee).
 *
 * The 640 x 320 one-bit map (g_state.planet.map) has 10 x 10 pixels per
 * sector. A land or arctic sector is drawn as a shape picked by which of its
 * eight neighbours share its terrain (planet_map_neighbours): a full square
 * in the interior, a cut corner, a peninsula, a strait and so on. Shapes are
 * outlined with lines, some of them "rough" (randomly jittered by a pixel,
 * drawing from the shared RNG), and then flood-filled from a point inside.
 *
 * Coordinates are pixels; the stride is the row length in pixels (0x280).
 */
#include <stdlib.h>

#include "game/planet.h"
#include "game/util.h"

#define MAP_STRIDE 0x280

/* 0x4a7d18: sets bit `bit` of *p. */
static void bit_set(uint8_t *p, unsigned bit)
{
    *p |= (uint8_t)(1u << (bit & 7));
}

/* 0x4a7d37: bit `bit` of v. */
static int bit_test(uint8_t v, unsigned bit)
{
    return v >> (bit & 7) & 1;
}

/* 0x4a7d53 (planet_map_set in planet.h): pixel (x, y) on. */
static void pixel_set(uint8_t *map, unsigned x, unsigned y, unsigned stride)
{
    bit_set(&map[(y * stride + x) >> 3], 7 - (x & 7));
}

/* 0x4a7d88 (planet_map_test in planet.h): pixel (x, y). */
static int pixel_test(const uint8_t *map, unsigned x, unsigned y, unsigned stride)
{
    return bit_test(map[(y * stride + x) >> 3], 7 - (x & 7));
}

/* 0x4a7dc4: `w` pixels from (x, y) to the right. */
static void fill_row(uint8_t *map, unsigned x, unsigned y, unsigned w, unsigned stride)
{
    for (unsigned i = x; i < x + w; i++)
        pixel_set(map, i, y, stride);
}

/* 0x4a7e0c: a w x h rectangle at (x, y). */
static void fill_rect(uint8_t *map, unsigned x, unsigned y, unsigned w, unsigned h,
                      unsigned stride)
{
    for (unsigned j = y; j < y + h; j++)
        fill_row(map, x, j, w, stride);
}

static int sign(int v)
{
    return v > 0 ? 1 : v < 0 ? -1 : 0;
}

static int clamp_px(int v, int limit)
{
    if (v > limit - 1)
        v = limit - 1;
    return v > 0 ? v : 0;
}

/* 0x4a7e58: Bresenham line from (x0, y0) to (x1, y1). A rough line wanders
 * across the minor axis: every pixel is drawn with an offset in -1..1 (and
 * thickened by one pixel towards the line's minor direction), and after each
 * pixel the offset moves by rand_range(max(off - 1, -1), min(off + 1, 1)).
 * The minor coordinate is clamped to the map. */
static void draw_line(uint8_t *map, int x0, int y0, int x1, int y1, bool rough,
                      unsigned stride)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx = sign(dx), sy = sign(dy);
    int adx = abs(dx), ady = abs(dy);
    int off = 0, err = 0;
    int x = x0, y = y0;

    if (adx >= ady) {
        for (int i = 0; i <= adx; i++) {
            int py = clamp_px(off + y, PLANET_MAP_H);
            pixel_set(map, (unsigned)x, (unsigned)py, stride);
            if (rough) {
                pixel_set(map, (unsigned)x, (unsigned)clamp_px(py + sy, PLANET_MAP_H), stride);
                off = rand_range(off - 1 > -1 ? off - 1 : -1, off + 1 < 1 ? off + 1 : 1);
            }
            err += ady;
            if (err >= adx) {
                err -= adx;
                y += sy;
            }
            x += sx;
        }
    } else {
        for (int i = 0; i <= ady; i++) {
            int px = clamp_px(off + x, PLANET_MAP_W);
            pixel_set(map, (unsigned)px, (unsigned)y, stride);
            if (rough) {
                pixel_set(map, (unsigned)clamp_px(px + sx, PLANET_MAP_W), (unsigned)y, stride);
                off = rand_range(off - 1 > -1 ? off - 1 : -1, off + 1 < 1 ? off + 1 : 1);
            }
            err += adx;
            if (err >= ady) {
                err -= ady;
                x += sx;
            }
            y += sy;
        }
    }
}

/* 0x4a8245: 4-way flood fill of the unset pixels connected to (x, y). The
 * original recurses (up, right, down, left); the filled set doesn't depend
 * on the order, so this uses an explicit stack and marks pixels when they
 * are pushed, so each pixel is pushed at most once. */
static void flood_fill(uint8_t *map, int x, int y, unsigned stride)
{
    uint32_t local[256];
    uint32_t *stack = local;
    size_t cap = sizeof local / sizeof local[0], n = 0;

    if (x < 0 || y < 0 || x >= PLANET_MAP_W || y >= PLANET_MAP_H ||
        pixel_test(map, (unsigned)x, (unsigned)y, stride))
        return;
    pixel_set(map, (unsigned)x, (unsigned)y, stride);
    stack[n++] = (uint32_t)y << 16 | (uint32_t)x;
    while (n > 0) {
        uint32_t v = stack[--n];
        int cx = (int)(v & 0xffff), cy = (int)(v >> 16);
        static const int8_t step[4][2] = { {0, -1}, {1, 0}, {0, 1}, {-1, 0} };
        for (int d = 0; d < 4; d++) {
            int nx = cx + step[d][0], ny = cy + step[d][1];
            if (nx < 0 || ny < 0 || nx >= PLANET_MAP_W || ny >= PLANET_MAP_H ||
                pixel_test(map, (unsigned)nx, (unsigned)ny, stride))
                continue;
            pixel_set(map, (unsigned)nx, (unsigned)ny, stride);
            if (n == cap) {
                uint32_t *grown = malloc(cap * 2 * sizeof *grown);
                if (!grown)
                    abort();
                for (size_t i = 0; i < n; i++)
                    grown[i] = stack[i];
                if (stack != local)
                    free(stack);
                stack = grown;
                cap *= 2;
            }
            stack[n++] = (uint32_t)ny << 16 | (uint32_t)nx;
        }
    }
    if (stack != local)
        free(stack);
}

/* 0x4a814c: triangle (x0, y0) (x1, y1) (x2, y2): the first edge rough, the
 * other two straight, then filled from a pixel diagonally next to the third
 * corner. */
static void draw_triangle(uint8_t *map, int x0, int y0, int x1, int y1, int x2, int y2,
                          unsigned stride)
{
    int fx, fy;

    draw_line(map, x0, y0, x1, y1, true, stride);
    draw_line(map, x0, y0, x2, y2, false, stride);
    draw_line(map, x1, y1, x2, y2, false, stride);
    if (x0 < x1) {
        if (x0 < x2) {
            fx = -1;
            fy = 1;
        } else {
            fx = 1;
            fy = -1;
        }
    } else if (x2 < x0) {
        fx = 1;
        fy = 1;
    } else {
        fx = -1;
        fy = -1;
    }
    flood_fill(map, x2 + fx, y2 + fy, stride);
}

/* ---- Shapes (the switch in 0x4a8337) ------------------------------------ */

enum { OP_END, OP_LINE, OP_ROUGH, OP_TRI, OP_FILL };

typedef struct ShapeOp {
    uint8_t op;
    uint8_t a[6]; /* pixel offsets in the sector: x0, y0, x1, y1 (, x2, y2) */
} ShapeOp;

#define LINE(x0, y0, x1, y1) {OP_LINE, {x0, y0, x1, y1, 0, 0}}
#define ROUGH(x0, y0, x1, y1) {OP_ROUGH, {x0, y0, x1, y1, 0, 0}}
#define TRI(x0, y0, x1, y1, x2, y2) {OP_TRI, {x0, y0, x1, y1, x2, y2}}
#define FILL(x, y) {OP_FILL, {x, y, 0, 0, 0, 0}}

/* Each shape in drawing order (the order of the RNG draws); the comment
 * lists the neighbour masks that select it. */
static const ShapeOp k_shapes[][7] = {
    /*  0: 012 013 016 017 057 116 117 157 */
    { ROUGH(0,0, 2,7), ROUGH(2,7, 7,7), ROUGH(7,7, 9,0), LINE(0,0, 9,0), FILL(4,1) },
    /*  1: 019 01d 058 059 05d 158 159 */
    { ROUGH(0,0, 7,2), ROUGH(7,2, 7,7), ROUGH(7,7, 0,9), LINE(0,0, 0,9), FILL(1,4) },
    /*  2: 01b */
    { ROUGH(5,0, 5,5), ROUGH(5,5, 0,5), LINE(5,0, 0,0), LINE(0,0, 0,5), FILL(1,1) },
    /*  3: 01f 11f */
    { ROUGH(0,5, 5,5), ROUGH(5,5, 9,0), LINE(0,5, 0,0), LINE(9,0, 0,0), FILL(1,1) },
    /*  4: 034 035 130 134 170 */
    { ROUGH(9,0, 2,2), ROUGH(2,2, 2,7), ROUGH(9,9, 2,7), LINE(9,0, 9,9), FILL(8,4) },
    /*  5: 036 */
    { ROUGH(5,0, 5,5), ROUGH(5,5, 9,5), LINE(5,0, 9,0), LINE(9,0, 9,5), FILL(8,1) },
    /*  6: 037 077 */
    { ROUGH(0,0, 5,5), ROUGH(5,5, 9,5), LINE(0,0, 9,0), LINE(9,5, 9,0), FILL(8,1) },
    /*  7: 03b 13f */
    { ROUGH(0,5, 5,5), ROUGH(5,5, 9,9), LINE(9,9, 9,0), LINE(0,5, 0,0), LINE(9,0, 0,0), FILL(8,1) },
    /*  8: 03f */
    { ROUGH(0,5, 9,5), LINE(0,5, 0,0), LINE(9,5, 9,0), LINE(0,0, 9,0), FILL(1,1) },
    /*  9: 05b */
    { ROUGH(5,0, 5,5), ROUGH(5,5, 0,9), LINE(0,9, 0,0), LINE(5,0, 0,0), FILL(1,1) },
    /* 10: 05e 05f 15f */
    { TRI(9,0, 0,9, 0,0) },
    /* 11: 07e 07f */
    { ROUGH(0,9, 5,5), ROUGH(5,5, 9,5), LINE(9,5, 9,0), LINE(0,9, 0,0), LINE(9,0, 0,0), FILL(8,1) },
    /* 12: 090 0d0 190 1d0 1d1 1d4 1d5 */
    { ROUGH(0,9, 2,2), ROUGH(2,2, 7,2), ROUGH(9,9, 7,2), LINE(0,9, 9,9), FILL(4,8) },
    /* 13: 097 1d2 1d3 1d6 1d7 */
    { ROUGH(1,0, 1,9), ROUGH(8,0, 8,9), LINE(1,9, 8,9), LINE(1,0, 8,0), FILL(6,8) },
    /* 14: 09b 1db */
    { ROUGH(5,0, 5,5), ROUGH(5,5, 9,9), LINE(9,9, 0,9), LINE(5,0, 0,0), LINE(0,9, 0,0), FILL(1,8) },
    /* 15: 09f 0da 0df */
    { ROUGH(9,0, 5,5), ROUGH(5,5, 5,9), LINE(5,9, 0,9), LINE(9,0, 0,0), LINE(0,9, 0,0), FILL(1,8) },
    /* 16: 0b6 1f6 */
    { ROUGH(5,0, 5,5), ROUGH(5,5, 0,9), LINE(0,9, 9,9), LINE(5,0, 9,0), LINE(9,9, 9,0), FILL(8,8) },
    /* 17: 0d8 0dc */
    { ROUGH(0,5, 5,5), ROUGH(5,5, 5,9), LINE(0,5, 0,9), LINE(0,9, 5,9), FILL(1,8) },
    /* 18: 0d9 */
    { ROUGH(0,0, 5,5), ROUGH(5,5, 5,9), LINE(5,9, 0,9), LINE(0,0, 0,9), FILL(1,8) },
    /* 19: 0db */
    { ROUGH(5,0, 5,9), LINE(5,0, 0,0), LINE(5,9, 0,9), LINE(0,0, 0,9), FILL(1,1) },
    /* 20: 0f7 1f7 */
    { LINE(0,0, 5,2), LINE(5,2, 5,7), LINE(5,7, 0,9), LINE(0,0, 9,0), LINE(9,0, 9,9), LINE(0,9, 9,9), FILL(5,8) },
    /* 21: 0f8 0fc 1fc */
    { ROUGH(0,5, 5,5), ROUGH(5,5, 9,0), LINE(9,0, 9,9), LINE(0,5, 0,9), LINE(9,9, 0,9), FILL(8,8) },
    /* 22: 132 137 177 */
    { TRI(0,0, 9,9, 9,0) },
    /* 23: 136 176 */
    { ROUGH(5,0, 5,5), ROUGH(5,5, 9,9), LINE(9,9, 9,0), LINE(5,0, 9,0), FILL(8,1) },
    /* 24: 17b 17f */
    { LINE(0,9, 2,5), LINE(2,5, 7,5), LINE(7,5, 9,9), LINE(0,0, 0,9), LINE(0,0, 9,0), LINE(9,9, 9,0), FILL(1,1) },
    /* 25: 1b0 */
    { ROUGH(5,5, 5,9), ROUGH(5,5, 9,5), LINE(9,5, 9,9), LINE(5,9, 9,9), FILL(8,8) },
    /* 26: 1b2 1b7 */
    { ROUGH(0,0, 5,5), ROUGH(5,5, 5,9), LINE(5,9, 9,9), LINE(0,0, 9,0), LINE(9,9, 9,0), FILL(8,8) },
    /* 27: 1b4 1b5 */
    { ROUGH(9,0, 5,5), ROUGH(5,5, 5,9), LINE(9,0, 9,9), LINE(5,9, 9,9), FILL(8,8) },
    /* 28: 1b6 */
    { ROUGH(5,0, 5,9), LINE(5,0, 9,0), LINE(5,9, 9,9), LINE(9,0, 9,9), FILL(8,1) },
    /* 29: 1bd 1fd */
    { LINE(0,0, 2,5), LINE(2,5, 7,5), LINE(7,5, 9,0), LINE(0,0, 0,9), LINE(0,9, 9,9), LINE(9,9, 9,0), FILL(1,8) },
    /* 30: 1d8 1dc */
    { ROUGH(0,5, 5,5), ROUGH(5,5, 9,9), LINE(0,5, 0,9), LINE(9,9, 0,9), FILL(1,8) },
    /* 31: 1d9 1dd */
    { TRI(0,0, 9,9, 0,9) },
    /* 32: 1df */
    { LINE(9,0, 5,2), LINE(5,2, 5,7), LINE(5,7, 9,9), LINE(0,0, 9,0), LINE(0,0, 0,9), LINE(0,9, 9,9), FILL(4,8) },
    /* 33: 1f0 */
    { ROUGH(0,9, 5,5), ROUGH(5,5, 9,5), LINE(0,9, 9,9), LINE(9,5, 9,9), FILL(8,8) },
    /* 34: 1f4 1f5 */
    { TRI(9,0, 0,9, 9,9) },
    /* 35: 1f8 */
    { ROUGH(0,5, 9,5), LINE(0,5, 0,9), LINE(0,9, 9,9), LINE(9,5, 9,9), FILL(1,8) },
    /* 36: 1f9 */
    { ROUGH(0,0, 5,5), ROUGH(5,5, 9,5), LINE(9,5, 9,9), LINE(0,0, 0,9), LINE(9,9, 0,9), FILL(8,8) },
};

/* Neighbour mask -> k_shapes index (the cases of 0x4a8337's switch, read off
 * the original by running it for all 512 masks). Any other mask is a full
 * 10 x 10 square. */
static const struct {
    uint16_t mask;
    uint8_t shape;
} k_mask_shape[] = {
    {0x012, 0}, {0x013, 0}, {0x016, 0}, {0x017, 0}, {0x019, 1}, {0x01b, 2},
    {0x01d, 1}, {0x01f, 3}, {0x034, 4}, {0x035, 4}, {0x036, 5}, {0x037, 6},
    {0x03b, 7}, {0x03f, 8}, {0x057, 0}, {0x058, 1}, {0x059, 1}, {0x05b, 9},
    {0x05d, 1}, {0x05e, 10}, {0x05f, 10}, {0x077, 6}, {0x07e, 11}, {0x07f, 11},
    {0x090, 12}, {0x097, 13}, {0x09b, 14}, {0x09f, 15}, {0x0b6, 16}, {0x0d0, 12},
    {0x0d8, 17}, {0x0d9, 18}, {0x0da, 15}, {0x0db, 19}, {0x0dc, 17}, {0x0df, 15},
    {0x0f7, 20}, {0x0f8, 21}, {0x0fc, 21}, {0x116, 0}, {0x117, 0}, {0x11f, 3},
    {0x130, 4}, {0x132, 22}, {0x134, 4}, {0x136, 23}, {0x137, 22}, {0x13f, 7},
    {0x157, 0}, {0x158, 1}, {0x159, 1}, {0x15f, 10}, {0x170, 4}, {0x176, 23},
    {0x177, 22}, {0x17b, 24}, {0x17f, 24}, {0x190, 12}, {0x1b0, 25}, {0x1b2, 26},
    {0x1b4, 27}, {0x1b5, 27}, {0x1b6, 28}, {0x1b7, 26}, {0x1bd, 29}, {0x1d0, 12},
    {0x1d1, 12}, {0x1d2, 13}, {0x1d3, 13}, {0x1d4, 12}, {0x1d5, 12}, {0x1d6, 13},
    {0x1d7, 13}, {0x1d8, 30}, {0x1d9, 31}, {0x1db, 14}, {0x1dc, 30}, {0x1dd, 31},
    {0x1df, 32}, {0x1f0, 33}, {0x1f4, 34}, {0x1f5, 34}, {0x1f6, 16}, {0x1f7, 20},
    {0x1f8, 35}, {0x1f9, 36}, {0x1fc, 21}, {0x1fd, 29},  
};

#undef LINE
#undef ROUGH
#undef TRI
#undef FILL

static int shape_for_mask(uint32_t mask)
{
    for (size_t i = 0; i < sizeof k_mask_shape / sizeof k_mask_shape[0]; i++)
        if (k_mask_shape[i].mask == mask)
            return k_mask_shape[i].shape;
    return -1;
}

/* ---- Public API ---------------------------------------------------------- */

static bool same_land(const PlanetSector *s)
{
    return s->terrain == PLANET_LAND && s->height != 0;
}

/* 0x4a6b61: bit (dy * 3 + dx + 4) is set when neighbour (x + dx, y + dy)
 * matches sector (x, y): for raised inland land (land, height != 0, not
 * coastal) a neighbour matches if it is raised land too, otherwise if it
 * has the same terrain. x wraps around; a row beyond the poles counts as
 * the sector itself. Water sectors get 0. */
uint32_t planet_map_neighbours(int x, int y)
{
    const PlanetSector *c = planet_sector(x, y);
    uint32_t mask = 0;
    bool inland;

    if (c->terrain == PLANET_WATER)
        return 0;
    inland = same_land(c) && !planet_is_coastal(x, y);
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            const PlanetSector *n;
            bool match;

            if (ny < 0 || ny >= PLANET_H) {
                nx = x;
                ny = y;
            } else if (nx < 0) {
                nx = PLANET_W - 1;
            } else if (nx >= PLANET_W) {
                nx = 0;
            }
            n = planet_sector(nx, ny);
            match = inland ? same_land(n) : n->terrain == c->terrain;
            if (match)
                mask |= 1u << (dy * 3 + dx + 4);
        }
    }
    return mask;
}

/* 0x4a8337 */
void planet_map_draw_sector(uint8_t *map, int x, int y, unsigned stride)
{
    int px = x * 10, py = y * 10;
    int shape;

    if (planet_sector(x, y)->terrain == PLANET_WATER)
        return;
    shape = shape_for_mask(planet_map_neighbours(x, y));
    if (shape < 0) {
        fill_rect(map, (unsigned)px, (unsigned)py, 10, 10, stride);
        return;
    }
    for (const ShapeOp *o = k_shapes[shape]; o < k_shapes[shape] + 7 && o->op != OP_END; o++) {
        const uint8_t *a = o->a;
        switch (o->op) {
        case OP_LINE:
        case OP_ROUGH:
            draw_line(map, px + a[0], py + a[1], px + a[2], py + a[3], o->op == OP_ROUGH,
                      stride);
            break;
        case OP_TRI:
            draw_triangle(map, px + a[0], py + a[1], px + a[2], py + a[3], px + a[4],
                          py + a[5], stride);
            break;
        case OP_FILL:
            flood_fill(map, px + a[0], py + a[1], stride);
            break;
        }
    }
}

/* 0x4a7ca1 (the original's first argument, a surface, is unused) */
void planet_map_draw_all(uint8_t *map)
{
    if (!map)
        return;
    for (int y = 0; y < PLANET_H; y++)
        for (int x = 0; x < PLANET_W; x++)
            planet_map_draw_sector(map, x, y, MAP_STRIDE);
}

/* 0x49beee: the original masks whole bytes; same pixels. */
void planet_map_clear_sector(uint8_t *map, int x, int y)
{
    unsigned px = (unsigned)x * 10, py = (unsigned)y * 10;

    for (unsigned j = py; j < py + 10; j++)
        for (unsigned i = px; i < px + 10; i++)
            map[(j * MAP_STRIDE + i) >> 3] &= (uint8_t)~(1u << (7 - (i & 7)));
}
